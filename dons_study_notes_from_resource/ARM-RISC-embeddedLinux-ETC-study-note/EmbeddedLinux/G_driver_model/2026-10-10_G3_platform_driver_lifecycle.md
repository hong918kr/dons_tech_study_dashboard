# G3. platform driver와 생명주기 — probe/remove, `devm_*`, deferred probe, driver_data (실무 드라이버의 90% 뼈대)

> **이 노트를 다 읽으면**: `struct platform_driver` 뼈대(probe·remove·shutdown·pm·of_match_table)를 외워서 쓸 수 있다 · 정석 probe 순서(자원 → 매핑 → 전원 → 클럭 → 리셋 → 칩 ID → 초기화 → drvdata → IRQ → 등록)를 goto 사다리와 `devm_*` 두 방식으로 짜고, 실패 주입으로 누수가 없음을 증명할 수 있다 · devres가 "역순으로 푸는 해제 콜백 리스트"라는 것과 remove()와 devres 해제 사이의 틈(use-after-free)을 설명할 수 있다 · `-EPROBE_DEFER`, `dev_err_probe`, `devices_deferred`, fw_devlink, async probe로 "probe가 안 불려요"를 진단할 수 있다
> **왜 필요한가**: 실무 드라이버의 대부분은 platform/I2C/SPI 드라이버이고 모양이 전부 같다. 면접에서는 "platform driver 뼈대를 화이트보드에 써 보세요", "devm이 뭐고 언제 위험하죠?", "EPROBE_DEFER가 뭐죠?"로 나온다
> **Don 기준 난이도**: 전원 시퀀스·클럭 게이팅·리셋 해제·칩 ID 확인·에러 시 역순 정리는 SSD 펌웨어 bring-up에서 매일 하던 일이다 / 새로 배울 것은 "그 init/deinit을 driver core가 **여러 번, 아무 때나, 실패 도중에도** 부른다"는 생명주기와, 정리를 자동화하는 devres
> **선행 노트**: D3(compatible 매칭과 `really_probe`), D4(DT 자원과 `devm_*_get`), E3(커널 모듈), E5(container_of, goto 사다리 vs devm vs cleanup.h), F6(인터럽트) — G2(디바이스 모델), G4(문자 디바이스), G8(sysfs), N(전원 관리)로 이어진다

---

## 0. 큰 그림 — 이게 왜 필요한가

Don의 SSD 펌웨어에서 새 주변장치를 붙이면 보통 이런 함수 두 개가 생겼다.

```c
int  imu_init(void);    /* 전원 → 클럭 → 리셋 해제 → ID 확인 → 레지스터 설정 → 인터럽트 등록 */
void imu_deinit(void);  /* 거의 안 불린다. 불려도 전원 끌 때 한 번 */
```

`imu_init()`은 부팅 때 **한 번**, 정해진 순서로 불렸다. 실패하면 대개 assert나 리셋이었다. 정리 코드(`imu_deinit`)는 대충 써도 티가 나지 않았다.

Linux의 platform driver에서는 이 두 함수가 `probe()`와 `remove()`가 되는데, 전제가 완전히 다르다.

- **probe는 여러 번 불린다.** 모듈을 뺐다 다시 넣을 때, `sysfs`로 unbind/bind 할 때, 공급자(레귤레이터·클럭)가 없어 `-EPROBE_DEFER`로 미뤘다가 재시도할 때.
- **probe는 중간에 실패할 수 있다.** 실패하면 그때까지 얻은 자원을 **하나도 남김없이** 돌려줘야 한다. 다음 재시도가 같은 자원을 다시 요청하기 때문이다(남겨 두면 `-EBUSY`).
- **remove는 실제로 불린다.** 개발 중에는 하루에 수십 번. 그리고 remove가 끝나도 IRQ 핸들러·workqueue·타이머·유저 공간의 열린 파일이 아직 돌 수 있다.
- **누가 부르는지는 driver core가 정한다.** 드라이버는 "장치를 만나면 이렇게, 헤어지면 이렇게"만 적는다.

그래서 이 노트의 목표는 하나다: **어떤 순서로 얻고, 실패하면 어떻게 정확히 되돌리며, 그걸 어떻게 증명하나.**

```svg
<svg viewBox="0 0 680 390" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="20" y="20" width="170" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="105.0" y="40.5" text-anchor="middle" font-size="13">드라이버·장치 등록</text>
<text x="105.0" y="58.5" text-anchor="middle" font-size="12">insmod · DT populate</text>
<rect x="240" y="20" width="150" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="315.0" y="40.5" text-anchor="middle" font-size="13">bus match</text>
<text x="315.0" y="58.5" text-anchor="middle" font-size="12">compatible 비교</text>
<rect x="440" y="20" width="220" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="550.0" y="40.5" text-anchor="middle" font-size="13" font-weight="bold">really_probe()</text>
<text x="550.0" y="58.5" text-anchor="middle" font-size="12">→ drv->probe(pdev)</text>
<line x1="190" y1="45" x2="237" y2="45" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="390" y1="45" x2="437" y2="45" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="550" y1="70" x2="550" y2="100" stroke="currentColor"/>
<line x1="110" y1="100" x2="565" y2="100" stroke="currentColor"/>
<line x1="110" y1="100" x2="110" y2="127" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="325" y1="100" x2="325" y2="127" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="565" y1="100" x2="565" y2="127" stroke="currentColor" marker-end="url(#ah1)"/>
<rect x="20" y="130" width="180" height="50" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="110.0" y="150.6" text-anchor="middle" font-size="13">기타 에러 (ret &lt; 0)</text>
<text x="110.0" y="168.6" text-anchor="middle" font-size="12">probe 실패</text>
<rect x="240" y="130" width="170" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="325.0" y="150.6" text-anchor="middle" font-size="13">-EPROBE_DEFER</text>
<text x="325.0" y="168.6" text-anchor="middle" font-size="12">deferred 목록에서 대기</text>
<rect x="470" y="130" width="190" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="565.0" y="150.6" text-anchor="middle" font-size="13" font-weight="bold">ret == 0</text>
<text x="565.0" y="168.6" text-anchor="middle" font-size="12">bound: 정상 동작</text>
<line x1="380" y1="130" x2="380" y2="73" stroke="currentColor" stroke-dasharray="5,4" marker-end="url(#ah1)"/>
<text x="386" y="118" font-size="12" text-anchor="start">재시도</text>
<rect x="470" y="240" width="190" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="565.0" y="260.6" text-anchor="middle" font-size="13">unbind 요청</text>
<text x="565.0" y="278.6" text-anchor="middle" font-size="12">rmmod · sysfs · 공급자</text>
<line x1="565" y1="180" x2="565" y2="237" stroke="currentColor" marker-end="url(#ah1)"/>
<rect x="240" y="240" width="170" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="325.0" y="260.6" text-anchor="middle" font-size="13">drv->remove()</text>
<text x="325.0" y="278.6" text-anchor="middle" font-size="12">드라이버가 직접 정리</text>
<line x1="470" y1="265" x2="413" y2="265" stroke="currentColor" marker-end="url(#ah1)"/>
<rect x="20" y="240" width="180" height="50" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="110.0" y="260.6" text-anchor="middle" font-size="13">devres_release_all</text>
<text x="110.0" y="278.6" text-anchor="middle" font-size="12">등록 역순으로 해제</text>
<line x1="240" y1="265" x2="203" y2="265" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="110" y1="180" x2="110" y2="237" stroke="currentColor" marker-end="url(#ah1)"/>
<text x="118" y="215" font-size="12" text-anchor="start">remove() 없이</text>
<rect x="20" y="320" width="180" height="46" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-dasharray="5,4"/>
<text x="110.0" y="338.6" text-anchor="middle" font-size="13">unbound</text>
<text x="110.0" y="356.6" text-anchor="middle" font-size="12">다시 bind 가능</text>
<line x1="110" y1="290" x2="110" y2="317" stroke="currentColor" marker-end="url(#ah1)"/>
<text x="240" y="338" font-size="12" text-anchor="start">bound 상태에서만: suspend/resume (driver.pm),</text>
<text x="240" y="358" font-size="12" text-anchor="start">shutdown (재부팅 직전) 콜백이 불린다</text>
</svg>
```
그림 1 — platform 장치 하나의 생명주기. probe의 결과는 세 갈래(성공 · defer · 실패)이고, 실패와 unbind는 결국 같은 정리 경로(`devres_release_all`)로 모인다. 이 그림의 모든 화살표를 이 노트에서 직접 실행해 본다.

비유로 정리하면 이렇다.

| bare-metal 펌웨어 | Linux platform driver |
|---|---|
| `board.h`의 주소·IRQ 번호 | DT 노드의 `reg`·`interrupts`… (D4) |
| `main()`이 `imu_init()`을 부른다 | driver core가 매칭되면 `probe()`를 부른다 (D3) |
| 전역 변수 `g_imu` | 장치마다 하나씩 `devm_kzalloc`한 private 구조체 + `platform_set_drvdata` |
| init 실패 → assert | 음수 에러 코드 반환 → driver core가 기록, 자원 회수 |
| init 순서를 손으로 맞춤 | `-EPROBE_DEFER` + fw_devlink가 순서를 맞춤 |
| deinit은 거의 안 씀 | remove/unbind가 수시로 일어남 → 정리 코드가 본업 |

> 실행 환경: 모든 출력은 Lima VM `elinux`(aarch64, 호스트 커널 `uname -r` = `7.0.0-38-generic`) 안의 공용 QEMU 랩에서 실제로 돌린 결과다. 게스트는 QEMU `virt`(cortex-a57, 512 MB) + Linux **6.18.55** + BusyBox initramfs. 단, devres 로그를 보려고 랩 커널 빌드 트리를 `/var/tmp/G3/kbuild`로 복사해 `CONFIG_DEBUG_DEVRES=y` 하나만 켜서 다시 빌드한 커널을 썼다(§2.3). 타임스탬프·virq 번호·포인터 해시는 실행마다 다르다.

### 0.1 이 노트의 실습 지도

| 예제 | 무엇을 하나 | 절 |
|---|---|---|
| 1~4 | DT에 sensorkit 노드, DEBUG_DEVRES 커널, 공통 헤더, 빌드·부팅 스크립트 | §2 |
| 5 | v1: goto 사다리 드라이버 | §4 |
| 6~7 | v2: devm 드라이버, 줄 수 비교 | §5 |
| 8~10 | 정상 경로 관찰, fail_at=1..10 실패 주입, 사다리 버그를 테스트가 잡는가 | §6 |
| 11 | devres 로그: ADD는 쌓이고 REL은 역순 | §7 |
| 12 | remove()와 devres 사이의 틈: 해제된 버퍼를 읽는 IRQ | §8 |
| 13 | deferred probe: 공급자가 늦게 오면 (fw_devlink on / permissive) | §9 |
| 14 | bind/unbind 100회 누수 검사 | §10 |
| 15 | 동기 vs 비동기 probe 시간 | §11 |
| 16 | suspend/resume, shutdown 콜백 | §12 |
| 17 | 6.18 소스에서 구조체·호출 경로 확인 | §1, §13 |

---

## 1. `struct platform_driver` 해부

### 1.1 직관 — "장치를 만나면 / 헤어지면 / 전원이 바뀌면"

platform driver는 콜백 몇 개를 담은 구조체 하나다. driver core는 장치의 생명주기 사건마다 그중 하나를 부른다. Don의 펌웨어 용어로는 **이벤트 핸들러 테이블**이다.

### 1.2 예제 17(a) — 6.18 소스에서 구조체 확인

무엇을 확인하나: 랩 커널 소스의 실제 정의. 특히 `.remove`의 반환형.

```sh
cd /var/lab/src/linux-6.18.55
grep -n "struct platform_driver {" -A 17 include/linux/platform_device.h
grep -rn "remove_new" drivers | head -6
```

```text
231:struct platform_driver {
232-	int (*probe)(struct platform_device *);
233-	void (*remove)(struct platform_device *);
234-	void (*shutdown)(struct platform_device *);
235-	int (*suspend)(struct platform_device *, pm_message_t state);
236-	int (*resume)(struct platform_device *);
237-	struct device_driver driver;
238-	const struct platform_device_id *id_table;
239-	bool prevent_deferred_probe;
240-	/*
241-	 * For most device drivers, no need to care about this flag as long as
242-	 * all DMAs are handled through the kernel DMA API. For some special
243-	 * ones, for example VFIO drivers, they know how to manage the DMA
244-	 * themselves and set this flag so that the IOMMU layer will allow them
245-	 * to setup and manage their own I/O address space.
246-	 */
247-	bool driver_managed_dma;
248-};
drivers/pcmcia/ds.c:158:pcmcia_remove_newid_file(struct pcmcia_driver *drv)
drivers/pcmcia/ds.c:206:	pcmcia_remove_newid_file(driver);
drivers/usb/core/driver.c:206:static void usb_remove_newid_files(struct usb_driver *usb_drv)
drivers/usb/core/driver.c:1120:	usb_remove_newid_files(driver);
drivers/ata/pata_parport/pata_parport.c:798:		goto out_remove_new;
drivers/ata/pata_parport/pata_parport.c:811:out_remove_new:
```

출력에서 볼 것: `.remove`는 **`void`를 반환**한다. `remove_new`라는 이름은 이제 무관한 식별자(`remove_newid` 등)에만 남아 있다.

역사: 예전 `.remove`는 `int`를 반환했지만 driver core는 그 값을 무시했다(remove는 "실패할 수 없는" 연산이다 — 장치는 어쨌든 떠난다). 그래서 6.3 무렵부터 `void`를 반환하는 `.remove_new`를 추가해 드라이버를 하나씩 옮겼고, **6.11에서 `.remove` 자체가 `void`로 바뀌었다**. `.remove_new`는 전환 기간 동안 별칭으로 남았다가 이후 제거됐다(6.18 소스에 필드가 없음을 위에서 확인). 오래된 BSP 커널(5.x, 6.1)에서는 `int (*remove)`이고 `return 0;`을 쓴다 — 회사 커널 버전을 먼저 확인하자.

| 필드 | 언제 불리나 | 반환 | 메모 |
|---|---|---|---|
| `probe` | 장치와 드라이버가 매칭될 때 (그리고 defer 재시도마다) | 0 / 음수 errno | 이 노트의 주인공 |
| `remove` | unbind: rmmod, sysfs `unbind`, 공급자 unbind, 장치 삭제 | void | 이 함수가 끝난 **뒤에** devres 해제 |
| `shutdown` | 재부팅·전원 끄기 직전 (`device_shutdown()`) | void | DMA 멈추기, 칩을 리셋에 두기 |
| `suspend`/`resume` | 레거시 PM 콜백 | int | 새 코드는 `driver.pm`(dev_pm_ops)을 쓴다 |
| `driver.name` | sysfs 이름, 이름 매칭 | | `/sys/bus/platform/drivers/<name>` |
| `driver.of_match_table` | DT `compatible` 매칭 | | D3 |
| `driver.pm` | 시스템 sleep·runtime PM | | §12 |
| `driver.probe_type` | 동기/비동기 probe 선호 | | §11 |
| `id_table` | 이름 기반 매칭(보드 파일, MFD 자식) | | DT 없는 장치 |
| `prevent_deferred_probe` | defer를 `-ENXIO`로 바꿔 버림 | | `platform_driver_probe()` 계열용 |

### 1.3 예제 17(b) — `module_platform_driver()`가 펼쳐지는 모양

```sh
sed -n 286,288p include/linux/platform_device.h
grep -n "define module_driver" -A 10 include/linux/device/driver.h
```

```text
#define module_platform_driver(__platform_driver) \
	module_driver(__platform_driver, platform_driver_register, \
			platform_driver_unregister)
257:#define module_driver(__driver, __register, __unregister, ...) \
258-static int __init __driver##_init(void) \
259-{ \
260-	return __register(&(__driver) , ##__VA_ARGS__); \
261-} \
262-module_init(__driver##_init); \
263-static void __exit __driver##_exit(void) \
264-{ \
265-	__unregister(&(__driver) , ##__VA_ARGS__); \
266-} \
267-module_exit(__driver##_exit);
```

출력에서 볼 것: 매크로 한 줄이 `module_init`에서 `platform_driver_register()`, `module_exit`에서 `platform_driver_unregister()`를 부르는 함수 두 개가 된다. 중요한 결론 — **모듈 init은 드라이버를 버스에 "등록"만 한다. probe는 등록 과정에서 매칭되는 장치가 있으면 그때 불린다.** 그래서 probe가 실패해도 `insmod`는 성공한다(E5 예제 11에서 이미 봤다). 반대로 `rmmod`는 `platform_driver_unregister()` → 모든 장치 unbind → 각 장치마다 `remove()` + devres 해제다.

`module_platform_driver()`를 못 쓰는 경우: 모듈 하나가 드라이버 여러 개를 등록하거나, 등록 전에 다른 일을 해야 할 때. 그때는 `platform_register_drivers()`나 직접 쓴 `module_init`을 쓴다.

### 1.4 매칭 테이블 — `of_match_table` + `MODULE_DEVICE_TABLE`

```c
static const struct of_device_id sk_of_match[] = {
	{ .compatible = "sensorkit,g3-imu" },
	{ }                                   /* 끝 표시: 빈 항목 필수 */
};
MODULE_DEVICE_TABLE(of, sk_of_match);     /* modalias → 자동 모듈 로드 (G9) */
```

- `of_match_table`은 **매칭**에 쓰인다(D3의 `platform_match()` → `of_driver_match_device()`).
- `MODULE_DEVICE_TABLE(of, ...)`은 **모듈 파일에 별칭(alias)을 기록**해서, udev/mdev가 장치의 `modalias`(`of:N...C sensorkit,g3-imu`)를 보고 `modprobe`로 이 모듈을 자동 로드하게 한다(D3 §9, G9). 빠뜨리면 빌트인일 때는 잘 되다가 **모듈로 빌드하면 부팅 때 자동으로 안 올라오는** 버그가 된다.
- 테이블 항목의 `.data`로 칩 변형별 설정을 넘기고 probe에서 `device_get_match_data(dev)`로 꺼낸다(D3 예제 2).

---

## 2. 실습 준비 — sensorkit IMU를 virt DT에 넣기

### 2.1 무엇을 붙이나

모듈 G의 공통 예제는 가상의 **sensorkit IMU**다. 여기서는 QEMU `virt` DT 사본에 platform 장치로 넣는다. 레지스터 창이 필요한데, D4처럼 남의 영역(PL061)을 빌리면 `-EBUSY`가 나서 `/proc/iomem` 관찰이 안 된다. 그래서 이번에는 **비어 있는 virtio-mmio 슬롯 하나**(0x0a003e00, 0x200바이트)를 통째로 가져온다. DT에서 그 virtio 노드를 지우면 커널의 virtio 드라이버는 그 주소를 모르고, QEMU 하드웨어는 그대로 있으므로 레지스터를 안전하게 읽을 수 있다. virtio-mmio의 0번 레지스터(MagicValue)는 항상 `0x74726976`("virt")이다 — 이걸 **칩 ID**로 쓴다.

| 노드 | 역할 | 드라이버 |
|---|---|---|
| `g3_vdd: g3-vdd` | `regulator-fixed` 3.3 V | 빌트인 (항상 먼저 준비됨) |
| `g3_osc: g3-osc` | `fixed-clock` 12 MHz, 이름 `core` | 빌트인 |
| `g3_clkgen: g3-clkgen` | 19.2 MHz 클럭 공급자, 이름 `ref` | **모듈 `g3_clkgen.ko`** — 늦게 올 수 있는 공급자 (§9) |
| `sensorkit@a003e00` | 소비자, `compatible = "sensorkit,g3-imu"` | 우리 드라이버 v1 또는 v2 |

인터럽트는 원래 그 슬롯의 SPI 47(엣지)을 그대로 쓴다. 하드웨어가 올릴 일은 없으므로, 필요할 때 드라이버가 GIC 분배기의 pending 비트를 직접 세운다(§8). 리셋 핀은 PL061의 5번 핀, active-low (D4와 같다).

### 2.2 예제 1 — DT 조각과 dtb 만들기

무엇을 확인하나: `virt.dts` 사본에 PL061 label 하나를 붙이고, 우리 노드를 `#include`해서 `cpp` → `dtc`로 `.dtb`를 만든다. `/delete-node/`로 virtio 슬롯을 지운다.

```dts
/* g3_nodes.dtsi — virt.dts 사본 끝에 #include: sensorkit IMU(가상)와 공급자들 */
#include <dt-bindings/interrupt-controller/arm-gic.h>
#include <dt-bindings/gpio/gpio.h>

/ {
	/delete-node/ virtio_mmio@a003e00;	/* 이 MMIO 창을 sensorkit이 빌린다 */

	g3_vdd: g3-vdd {
		compatible = "regulator-fixed";
		regulator-name = "g3_vdd_3v3";
		regulator-min-microvolt = <3300000>;
		regulator-max-microvolt = <3300000>;
	};

	g3_osc: g3-osc {
		compatible = "fixed-clock";
		#clock-cells = <0>;
		clock-frequency = <12000000>;
		clock-output-names = "g3_osc12m";
	};

	g3_clkgen: g3-clkgen {			/* 드라이버는 모듈 g3_clkgen.ko (늦게 온다) */
		compatible = "sensorkit,g3-clkgen";
		#clock-cells = <0>;
		clock-frequency = <19200000>;
		clock-output-names = "g3_ref19m2";
	};

	sensorkit@a003e00 {
		compatible = "sensorkit,g3-imu";
		reg = <0x0 0x0a003e00 0x0 0x200>;
		interrupts = <GIC_SPI 47 IRQ_TYPE_EDGE_RISING>;
		clocks = <&g3_osc>, <&g3_clkgen>;
		clock-names = "core", "ref";
		vdd-supply = <&g3_vdd>;
		reset-gpios = <&gpio 5 GPIO_ACTIVE_LOW>;
	};
};
```

```sh
#!/bin/sh
# mkdtb.sh → /var/tmp/G3/g3.dtb
set -e
W=/var/tmp/G3; K=/var/lab/src/linux-6.18.55/include
sed -e 's/^\tpl061@9030000 {/\tgpio: pl061@9030000 {/' /var/lab/out/virt.dts > $W/g3_virt.dts
echo '#include "/Users/donh/.elinux_scratch/G3/g3_nodes.dtsi"' >> $W/g3_virt.dts
cpp -nostdinc -I $K -undef -x assembler-with-cpp $W/g3_virt.dts > $W/g3.pp.dts
dtc -q -I dts -O dtb -o $W/g3.dtb $W/g3.pp.dts
fdtget -t s $W/g3.dtb /sensorkit@a003e00 compatible
```

```sh
sh /Users/donh/.elinux_scratch/G3/mkdtb.sh
```

```text
sensorkit,g3-imu
```

출력에서 볼 것: `fdtget`으로 컴파일된 dtb 안의 노드를 다시 읽었다. `-q`로 virt.dts 원본 노드의 dtc 경고(D4 예제 2에서 설명한 `clocks_property` 경고)는 숨겼다.

### 2.3 예제 2 — devres 로그가 나오는 커널 (CONFIG_DEBUG_DEVRES)

무엇을 확인하나: devres가 무엇을 쌓고 푸는지 눈으로 보려면 `CONFIG_DEBUG_DEVRES`가 필요한데 랩 커널에는 꺼져 있다. 랩은 읽기 전용이므로 **빌드 트리를 통째로 복사해서 그 옵션 하나만 켜고** 증분 빌드한다.

```sh
grep -n "DEBUG_DEVRES" /var/lab/out/linux-build/.config
cp -a /var/lab/build/linux /var/tmp/G3/kbuild            # 3.1 GB, 34초
cd /var/tmp/G3/kbuild
/var/lab/src/linux-6.18.55/scripts/config --file .config --enable DEBUG_DEVRES
make -C /var/lab/src/linux-6.18.55 O=/var/tmp/G3/kbuild olddefconfig
time make -C /var/lab/src/linux-6.18.55 O=/var/tmp/G3/kbuild -j2 Image
```

```text
1911:# CONFIG_DEBUG_DEVRES is not set
...
  CC      drivers/base/devres.o
...
  OBJCOPY arch/arm64/boot/Image
real	0m43.132s
```

출력에서 볼 것: 다시 컴파일된 C 파일은 `drivers/base/devres.o` 하나뿐이고(나머지는 링크), 43초에 끝났다. Kbuild의 `fixdep`이 파일마다 "어떤 CONFIG_ 심볼에 의존하는지"를 기록해 두기 때문이다(E2). 이 커널에서는 `/sys/module/devres/parameters/log`에 1을 쓰면 devres 노드의 추가·해제가 dmesg에 찍힌다(§7). 버전 문자열이 같으므로 모듈은 이 트리(`O=/var/tmp/G3/kbuild`)에 대해 빌드한다.

### 2.4 예제 3 — 공통 헤더 `g3_common.h`

v1과 v2가 공유하는 부분이다. 핵심은 **실패 주입 매크로** `INJ`/`INJ_PTR`: `fail_at`이 N이면 N번째 단계의 진짜 호출을 건너뛰고 `-EIO`를 낸다. 조건이 먼저 평가되므로 그 단계의 자원은 아예 얻지 않는다 — "N번째에서 실패했을 때 1..N-1이 정확히 반납되는가"를 시험할 수 있다.

```c
/* g3_common.h — v1/v2 공통: 레지스터, 실패 주입, IRQ 핸들러, misc 장치 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/cleanup.h>

#define SK_REG_MAGIC	0x000		/* virtio-mmio MagicValue: "virt" */
#define SK_REG_VERSION	0x004
#define SK_REG_DEVID	0x008
#define SK_CHIP_ID	0x74726976
#define SK_BUF_WORDS	1024		/* 4 KB 샘플 버퍼 */

static int fail_at;		/* N번째 단계를 -EIO로 실패시킨다 (0 = 안 함) */
module_param(fail_at, int, 0644);
static int leak;		/* 1 = remove에서 샘플 버퍼를 일부러 안 푼다 (v1) */
module_param(leak, int, 0644);
static int probe_delay_ms;	/* 느린 칩 흉내 (async probe 실험용) */
module_param(probe_delay_ms, int, 0644);
static int quiet;		/* 1 = probe/remove 로그 생략 (반복 실험용) */
module_param(quiet, int, 0644);

/* 실패 주입: 조건이 먼저 평가되므로 N번째 단계에서는 진짜 호출을 하지 않는다 */
#define INJ(n, expr)	 (fail_at == (n) ? -EIO : (expr))
#define INJ_PTR(n, expr) (fail_at == (n) ? ERR_PTR(-EIO) : (expr))

struct sk_priv {
	struct device *dev;
	void __iomem *base;
	struct clk *clk_core, *clk_ref;
	struct regulator *vdd;
	struct gpio_desc *reset;
	struct resource *res;
	int irq;
	u32 *buf;
	unsigned long irq_count;
	struct miscdevice misc;
};
```

뒷부분: IRQ 핸들러, `open`, 매칭 테이블. `sk_open`이 **`container_of`로 private 구조체를 되찾는** 장면에 주목하자(§3.4).

```c

static irqreturn_t sk_irq(int irq, void *data)
{
	struct sk_priv *p = data;

	p->irq_count++;
	dev_info(p->dev, "irq %d: buf[0]=0x%08x count=%lu\n", irq, p->buf[0], p->irq_count);
	return IRQ_HANDLED;
}

static int sk_open(struct inode *inode, struct file *f)
{
	/* misc_open()이 file->private_data = &miscdevice 로 채워 준다 */
	struct sk_priv *p = container_of(f->private_data, struct sk_priv, misc);

	dev_info(p->dev, "open: chip id 0x%08x\n", readl(p->base + SK_REG_MAGIC));
	return 0;
}

static const struct file_operations sk_fops = {
	.owner = THIS_MODULE,
	.open = sk_open,
};

static const struct of_device_id sk_of_match[] = {
	{ .compatible = "sensorkit,g3-imu" },
	{ }
};
MODULE_DEVICE_TABLE(of, sk_of_match);
```

### 2.5 예제 4 — 빌드·부팅 스크립트

```sh
#!/bin/sh
# build.sh — 소스를 /var/tmp/G3/mod 로 모아 랩 커널(DEBUG_DEVRES 사본 트리)에 대해 빌드
set -e
S=/Users/donh/.elinux_scratch/G3; M=/var/tmp/G3/mod
mkdir -p $M && cd $M
cp $S/g3_common.h $S/g3_v1.c $S/g3_clkgen.c $S/Kbuild .
cat $S/g3_v2.c $S/g3_v2_tail.c > g3_v2.c
make -C /var/lab/src/linux-6.18.55 O=/var/tmp/G3/kbuild M=$M -j2 modules 2>&1 | grep -v "^make"
```

```sh
#!/bin/sh
# boot.sh <게스트 스크립트> [추가 커널 인자]  — G3 커널(DEBUG_DEVRES) + g3.dtb + 모듈 3개
set -e
W=/var/tmp/G3; S=/Users/donh/.elinux_scratch/G3
rm -rf $W/r && mkdir $W/r && cp -a /var/lab/build/rootfs/. $W/r/
cp $W/mod/*.ko $S/$1 $S/t2_snap.sh $W/r/
(cd $W/r && find . | cpio -o -H newc --quiet | gzip) > $W/i.gz
timeout ${TMO:-180} qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -kernel $W/kbuild/arch/arm64/boot/Image -initrd $W/i.gz -dtb $W/g3.dtb \
  -append "console=ttyAMA0 quiet $2 LAB_CMD=sh~/$1"
```

게스트 스크립트 `t1.sh`~`t9.sh`는 각 절에서 보여 준다. 빌드 결과:

```sh
sh /Users/donh/.elinux_scratch/G3/build.sh
```

```text
  CC [M]  g3_v1.o
  CC [M]  g3_v2.o
  CC [M]  g3_clkgen.o
  MODPOST Module.symvers
  CC [M]  g3_v1.mod.o
  CC [M]  g3_v2.mod.o
  CC [M]  g3_clkgen.mod.o
  LD [M]  g3_v1.ko
  LD [M]  g3_v2.ko
  LD [M]  g3_clkgen.ko
```

경고 0개다. (첫 빌드에서는 `g3_clkgen.c`에 `MODULE_DESCRIPTION()`이 없다고 modpost가 경고했다 — 6.x부터 빠뜨리면 경고가 나온다. 추가해서 없앴다.)

---

## 3. 정석 probe 순서

### 3.1 직관 — bring-up 순서를 그대로 코드로

Don이 새 칩을 bring-up할 때의 순서를 떠올리자: 레지스터 창 확인 → 전원 레일 켜기 → 클럭 공급 → 리셋 해제 → 안정화 대기 → **ID 레지스터 읽어서 살아 있는지 확인** → 레지스터 초기화 → 인터럽트 연결 → 상위 소프트웨어에 "준비됨" 알림. probe도 똑같다. 다른 점은 각 단계가 "직접 레지스터를 치는 것"이 아니라 **프레임워크 API 호출**이라는 것(D4)과, 마지막 두 단계부터는 **바깥 세계가 드라이버를 부를 수 있게 된다**는 것이다.

```svg
<svg viewBox="0 0 680 520" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<circle cx="30" cy="30" r="13" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<text x="30" y="35" text-anchor="middle" font-size="12">1</text>
<rect x="55" y="12" width="250" height="36" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="180.0" y="34.5" text-anchor="middle" font-size="13">private 구조체 할당</text>
<text x="320" y="35" font-size="12" text-anchor="start">devm_kzalloc(dev, sizeof(*p), GFP_KERNEL)</text>
<line x1="180" y1="48" x2="180" y2="57" stroke="currentColor" marker-end="url(#ah2)"/>
<circle cx="30" cy="76" r="13" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<text x="30" y="81" text-anchor="middle" font-size="12">2</text>
<rect x="55" y="58" width="250" height="36" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="180.0" y="80.5" text-anchor="middle" font-size="13">레지스터 매핑 (reg)</text>
<text x="320" y="81" font-size="12" text-anchor="start">devm_platform_ioremap_resource(pdev, 0)</text>
<line x1="180" y1="94" x2="180" y2="103" stroke="currentColor" marker-end="url(#ah2)"/>
<circle cx="30" cy="122" r="13" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c"/>
<text x="30" y="127" text-anchor="middle" font-size="12">3</text>
<rect x="55" y="104" width="250" height="36" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="180.0" y="126.5" text-anchor="middle" font-size="13">전원 켜기 (vdd-supply)</text>
<text x="320" y="127" font-size="12" text-anchor="start">devm_regulator_get_enable(dev, "vdd")</text>
<line x1="180" y1="140" x2="180" y2="149" stroke="currentColor" marker-end="url(#ah2)"/>
<circle cx="30" cy="168" r="13" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c"/>
<text x="30" y="173" text-anchor="middle" font-size="12">4</text>
<rect x="55" y="150" width="250" height="36" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="180.0" y="172.6" text-anchor="middle" font-size="13">클럭 켜기 (clocks)</text>
<text x="320" y="173" font-size="12" text-anchor="start">devm_clk_get_enabled(dev, "core")</text>
<line x1="180" y1="186" x2="180" y2="195" stroke="currentColor" marker-end="url(#ah2)"/>
<circle cx="30" cy="214" r="13" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c"/>
<text x="30" y="219" text-anchor="middle" font-size="12">5</text>
<rect x="55" y="196" width="250" height="36" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="180.0" y="218.6" text-anchor="middle" font-size="13">리셋 해제 (reset-gpios)</text>
<text x="320" y="219" font-size="12" text-anchor="start">devm_gpiod_get() → 대기 → set 0</text>
<line x1="180" y1="232" x2="180" y2="241" stroke="currentColor" marker-end="url(#ah2)"/>
<circle cx="30" cy="260" r="13" fill="#3f9a6b" fill-opacity="0.15" stroke="#3f9a6b"/>
<text x="30" y="265" text-anchor="middle" font-size="12">6</text>
<rect x="55" y="242" width="250" height="36" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="180.0" y="264.6" text-anchor="middle" font-size="13">칩 ID 확인</text>
<text x="320" y="265" font-size="12" text-anchor="start">readl(base + ID) → 다르면 -ENODEV</text>
<line x1="180" y1="278" x2="180" y2="287" stroke="currentColor" marker-end="url(#ah2)"/>
<circle cx="30" cy="306" r="13" fill="#3f9a6b" fill-opacity="0.15" stroke="#3f9a6b"/>
<text x="30" y="311" text-anchor="middle" font-size="12">7</text>
<rect x="55" y="288" width="250" height="36" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="180.0" y="310.6" text-anchor="middle" font-size="13">하드웨어 초기화 · 버퍼</text>
<text x="320" y="311" font-size="12" text-anchor="start">devm_kcalloc, 레지스터 설정</text>
<line x1="180" y1="324" x2="180" y2="333" stroke="currentColor" marker-end="url(#ah2)"/>
<circle cx="30" cy="352" r="13" fill="#d0564a" fill-opacity="0.15" stroke="#d0564a"/>
<text x="30" y="357" text-anchor="middle" font-size="12">8</text>
<rect x="55" y="334" width="250" height="36" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="180.0" y="356.6" text-anchor="middle" font-size="13">platform_set_drvdata()</text>
<text x="320" y="357" font-size="12" text-anchor="start">콜백이 priv를 찾을 수 있게</text>
<line x1="180" y1="370" x2="180" y2="379" stroke="currentColor" marker-end="url(#ah2)"/>
<circle cx="30" cy="398" r="13" fill="#d0564a" fill-opacity="0.15" stroke="#d0564a"/>
<text x="30" y="403" text-anchor="middle" font-size="12">9</text>
<rect x="55" y="380" width="250" height="36" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="180.0" y="402.6" text-anchor="middle" font-size="13">IRQ 요청</text>
<text x="320" y="403" font-size="12" text-anchor="start">platform_get_irq + devm_request_irq</text>
<line x1="180" y1="416" x2="180" y2="425" stroke="currentColor" marker-end="url(#ah2)"/>
<circle cx="30" cy="444" r="13" fill="#d0564a" fill-opacity="0.15" stroke="#d0564a"/>
<text x="30" y="449" text-anchor="middle" font-size="12">10</text>
<rect x="55" y="426" width="250" height="36" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="180.0" y="448.6" text-anchor="middle" font-size="13">서브시스템 등록</text>
<text x="320" y="449" font-size="12" text-anchor="start">misc / iio / input / hwmon 등록</text>
<rect x="55" y="485" width="14" height="14" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/>
<text x="75" y="497" font-size="12" text-anchor="start">자원 얻기</text>
<rect x="205" y="485" width="14" height="14" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
<text x="225" y="497" font-size="12" text-anchor="start">전원 시퀀스</text>
<rect x="355" y="485" width="14" height="14" fill="#3f9a6b" fill-opacity="0.25" stroke="#3f9a6b"/>
<text x="375" y="497" font-size="12" text-anchor="start">하드웨어 확인</text>
<rect x="505" y="485" width="14" height="14" fill="#d0564a" fill-opacity="0.25" stroke="#d0564a"/>
<text x="525" y="497" font-size="12" text-anchor="start">외부에 노출</text>
</svg>
```
그림 2 — 정석 probe 순서. 위에서 아래로 얻고, 실패하거나 unbind되면 아래에서 위로 되돌린다. 빨간 단계부터는 IRQ 핸들러·유저 공간이 드라이버를 부를 수 있으므로, 그 전에 private 구조체가 완성돼 있어야 한다.

### 3.2 단계별 API 표

| # | 단계 | 고전 API (해제 짝) | devm API | 흔한 실패 |
|---|---|---|---|---|
| 1 | private 구조체 | `kzalloc` (`kfree`) | `devm_kzalloc` | `-ENOMEM` |
| 2 | 레지스터 매핑 | `platform_get_resource` + `request_mem_region` + `ioremap` (`iounmap`, `release_mem_region`) | `devm_platform_ioremap_resource` | `-EBUSY`(남이 점유), `-EINVAL` |
| 3 | 전원 | `regulator_get` + `regulator_enable` (`regulator_disable`, `regulator_put`) | `devm_regulator_get_enable` | `-EPROBE_DEFER` |
| 4 | 클럭 | `clk_get` + `clk_prepare_enable` (`clk_disable_unprepare`, `clk_put`) | `devm_clk_get_enabled` | `-EPROBE_DEFER`, `-ENOENT` |
| 5 | 리셋 해제 | `gpiod_get` (`gpiod_put`) / `reset_control_get` | `devm_gpiod_get`, `devm_reset_control_get_exclusive_deasserted` | `-EPROBE_DEFER`, `-EBUSY` |
| 6 | 칩 ID 확인 | `readl` / `regmap_read` | (같음) | `-ENODEV` (다른 칩) |
| 7 | 초기화·버퍼 | `kcalloc`, 레지스터 설정 | `devm_kcalloc` | `-ENOMEM`, `-ETIMEDOUT` |
| 8 | drvdata | `platform_set_drvdata` | (같음) | — |
| 9 | IRQ | `platform_get_irq` + `request_irq` (`free_irq`) | `devm_request_irq`, `devm_request_threaded_irq` | `-EPROBE_DEFER`, `-EBUSY` |
| 10 | 서브시스템 등록 | `misc_register` (`misc_deregister`) | `devm_iio_device_register`, `devm_input_register_device`, `devm_add_action_or_reset` | `-EEXIST` |

`devm_reset_control_get_exclusive_deasserted`는 얻으면서 리셋을 해제하고, unbind 때 다시 assert하는 비교적 최근(6.13 무렵) 추가된 헬퍼다(6.18의 `include/linux/reset.h`에 있다). 구 커널에는 없다.

### 3.3 왜 drvdata는 IRQ·등록 **전에**

`request_irq()`가 돌아오는 순간부터 핸들러가 불릴 수 있다(공유 IRQ라면 `request_irq` 안에서도 시험 삼아 불릴 수 있다). `misc_register()`가 돌아오는 순간 유저 공간이 `/dev/g3_sensorkit`을 `open()`할 수 있다. 그 콜백들이 `platform_get_drvdata()`로 private 구조체를 찾는데 아직 NULL이면 oops다. 그래서 **"외부에 노출하는 단계" 전에 private 구조체를 완성하고 drvdata를 세팅**한다. 같은 이유로 remove/해제는 반대로 **노출을 먼저 끊고** 내부를 정리한다(misc 해제 → IRQ 해제 → 버퍼 해제 …).

### 3.4 private 구조체로 돌아오는 네 가지 길

드라이버의 모든 콜백은 "내 장치의 상태"가 필요하다. 전역 변수는 안 된다(같은 칩이 보드에 두 개면?). 그래서 probe에서 장치마다 private 구조체를 하나 만들고, 각 콜백이 받는 포인터에서 그것을 되찾는다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="250" y="110" width="180" height="110" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="340.0" y="151.6" text-anchor="middle" font-size="13" font-weight="bold">struct sk_priv</text>
<text x="340.0" y="169.6" text-anchor="middle" font-size="12">base, clk, reset</text>
<text x="340.0" y="187.6" text-anchor="middle" font-size="12">irq, buf, misc</text>
<rect x="20" y="20" width="200" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="120.0" y="40.5" text-anchor="middle" font-size="13">remove(pdev)</text>
<text x="120.0" y="58.5" text-anchor="middle" font-size="12">platform_get_drvdata</text>
<rect x="460" y="20" width="200" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="560.0" y="40.5" text-anchor="middle" font-size="13">suspend(dev)</text>
<text x="560.0" y="58.5" text-anchor="middle" font-size="12">dev_get_drvdata</text>
<rect x="20" y="250" width="200" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="120.0" y="270.6" text-anchor="middle" font-size="13">sk_irq(irq, dev_id)</text>
<text x="120.0" y="288.6" text-anchor="middle" font-size="12">dev_id 그 자체</text>
<rect x="460" y="250" width="200" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="560.0" y="270.6" text-anchor="middle" font-size="13">sk_open(inode, file)</text>
<text x="560.0" y="288.6" text-anchor="middle" font-size="12">container_of(misc)</text>
<line x1="170" y1="70" x2="270" y2="107" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="510" y1="70" x2="410" y2="107" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="170" y1="250" x2="270" y2="223" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="510" y1="250" x2="410" y2="223" stroke="currentColor" marker-end="url(#ah3)"/>
<text x="340" y="40" font-size="12" text-anchor="middle">dev->driver_data</text>
<text x="340" y="58" font-size="12" text-anchor="middle">= platform_set_drvdata(pdev, p)</text>
<text x="340" y="262" font-size="12" text-anchor="middle">file->private_data = &amp;p->misc</text>
<text x="340" y="280" font-size="12" text-anchor="middle">→ 멤버에서 바깥 구조체로</text>
</svg>
```
그림 3 — 콜백마다 손에 쥔 포인터가 다르다. remove는 `pdev`, PM 콜백은 `struct device *`, IRQ 핸들러는 `request_irq`에 넘긴 `dev_id`, 파일 연산은 `file->private_data`(misc는 `&priv->misc`를 넣어 준다 → `container_of`). 모두 같은 `sk_priv`에 도착한다.

| 콜백 | 손에 쥔 것 | 되찾는 법 |
|---|---|---|
| `remove(pdev)`, `shutdown(pdev)` | `struct platform_device *` | `platform_get_drvdata(pdev)` |
| `dev_pm_ops`의 suspend/resume | `struct device *` | `dev_get_drvdata(dev)` |
| IRQ 핸들러 | `void *dev_id` | `request_irq(..., p)`로 넘긴 그 포인터 |
| `file_operations` | `struct file *` | `container_of(f->private_data, struct sk_priv, misc)` (G4·G7) |
| workqueue·timer | `struct work_struct *` 등 | `container_of(work, struct sk_priv, work)` (F7) |

`platform_get_drvdata(pdev)`는 그냥 `dev_get_drvdata(&pdev->dev)`이고, 그것은 `dev->driver_data` 필드를 읽는 것뿐이다. 그리고 unbind 마지막에 driver core가 `dev_set_drvdata(dev, NULL)`로 지운다(§13의 `device_unbind_cleanup`).

---

## 4. v1 — 고전적인 goto 사다리

### 4.1 규칙 (E5 §4.2 복습)

- 얻은 순서대로 라벨을 **거꾸로** 쌓는다. 단계 k가 실패하면 "k-1까지 되돌리는 라벨"로 점프한다.
- 라벨 이름은 "무엇을 되돌리나"로 짓는다(`err_vdd_off`, `err_core_put`). `err1`, `err2`는 끼워 넣기에 약하다.
- remove는 **에러 사다리를 처음부터 끝까지 따라 내려가는 것**과 같아야 한다. 둘이 어긋나면 버그다.

### 4.2 예제 5 — `g3_v1.c`

무엇을 확인하나: 10단계를 모두 고전 API로 얻고, 실패 시 goto 사다리로, 정상 해제는 remove로 되돌린다. 먼저 단계 1~6(얻기 + 전원 시퀀스).

```c
// SPDX-License-Identifier: GPL-2.0
/* g3_v1.c — sensorkit 드라이버 v1: 고전적인 goto 사다리 (devm 없음) */
#include "g3_common.h"

static int sk_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sk_priv *p;
	u32 id;
	int ret;

	p = INJ(1, 0) ? NULL : kzalloc(sizeof(*p), GFP_KERNEL);		/* 1 */
	if (!p)
		return -ENOMEM;
	p->dev = dev;
	p->res = platform_get_resource(pdev, IORESOURCE_MEM, 0);		/* 2 */
	if (!p->res || INJ(2, 0)) {
		ret = -EIO;
		goto err_free;
	}
	if (!request_mem_region(p->res->start, resource_size(p->res), dev_name(dev))) {
		ret = -EBUSY;
		goto err_free;
	}
	p->base = ioremap(p->res->start, resource_size(p->res));
	if (!p->base) {
		ret = -ENOMEM;
		goto err_release;
	}
	p->vdd = INJ_PTR(3, regulator_get(dev, "vdd"));			/* 3 */
	if (IS_ERR(p->vdd)) {
		ret = PTR_ERR(p->vdd);
		goto err_unmap;
	}
	ret = regulator_enable(p->vdd);
	if (ret)
		goto err_vdd_put;
	p->clk_core = INJ_PTR(4, clk_get(dev, "core"));			/* 4 */
	if (IS_ERR(p->clk_core)) {
		ret = PTR_ERR(p->clk_core);
		goto err_vdd_off;
	}
	ret = clk_prepare_enable(p->clk_core);
	if (ret)
		goto err_core_put;
	p->clk_ref = INJ_PTR(5, clk_get(dev, "ref"));			/* 5 */
	if (IS_ERR(p->clk_ref)) {
		ret = PTR_ERR(p->clk_ref);
		goto err_core_off;
	}
	ret = clk_prepare_enable(p->clk_ref);
	if (ret)
		goto err_ref_put;
	p->reset = INJ_PTR(6, gpiod_get(dev, "reset", GPIOD_OUT_HIGH));	/* 6 */
	if (IS_ERR(p->reset)) {
		ret = PTR_ERR(p->reset);
		goto err_ref_off;
	}
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(p->reset, 0);		/* 리셋 해제 */
	msleep(probe_delay_ms);
```

단계 7~10과 에러 사다리:

```c
	id = readl(p->base + SK_REG_MAGIC);					/* 7 */
	if (id != SK_CHIP_ID || INJ(7, 0)) {
		dev_err(dev, "bad chip id 0x%08x\n", id);
		ret = -ENODEV;
		goto err_reset;
	}
	p->buf = INJ(8, 0) ? NULL : kcalloc(SK_BUF_WORDS, sizeof(u32), GFP_KERNEL); /* 8 */
	if (!p->buf) {
		ret = -ENOMEM;
		goto err_reset;
	}
	p->buf[0] = readl(p->base + SK_REG_VERSION);
	platform_set_drvdata(pdev, p);		/* IRQ·등록보다 먼저 */
	p->irq = platform_get_irq(pdev, 0);					/* 9 */
	ret = p->irq < 0 ? p->irq : INJ(9, request_irq(p->irq, sk_irq, 0, "g3_sensorkit", p));
	if (ret)
		goto err_buf;
	p->misc.minor = MISC_DYNAMIC_MINOR;					/* 10 */
	p->misc.name = "g3_sensorkit";
	p->misc.fops = &sk_fops;
	p->misc.parent = dev;
	ret = INJ(10, misc_register(&p->misc));
	if (ret)
		goto err_irq;
	if (!quiet)
		dev_info(dev, "v1 probed: id=0x%08x ver=%u irq=%d\n", id, p->buf[0], p->irq);
	return 0;

err_irq:
	free_irq(p->irq, p);
err_buf:
	kfree(p->buf);
err_reset:
	gpiod_set_value_cansleep(p->reset, 1);
	gpiod_put(p->reset);
err_ref_off:
	clk_disable_unprepare(p->clk_ref);
err_ref_put:
	clk_put(p->clk_ref);
err_core_off:
	clk_disable_unprepare(p->clk_core);
err_core_put:
	clk_put(p->clk_core);
err_vdd_off:
	regulator_disable(p->vdd);
err_vdd_put:
	regulator_put(p->vdd);
err_unmap:
	iounmap(p->base);
err_release:
	release_mem_region(p->res->start, resource_size(p->res));
err_free:
	kfree(p);
	return dev_err_probe(dev, ret, "v1 probe failed\n");
}
```

remove와 드라이버 구조체:

```c

static void sk_remove(struct platform_device *pdev)
{
	struct sk_priv *p = platform_get_drvdata(pdev);

	misc_deregister(&p->misc);
	free_irq(p->irq, p);
	if (!leak)
		kfree(p->buf);
	gpiod_set_value_cansleep(p->reset, 1);
	gpiod_put(p->reset);
	clk_disable_unprepare(p->clk_ref);
	clk_put(p->clk_ref);
	clk_disable_unprepare(p->clk_core);
	clk_put(p->clk_core);
	regulator_disable(p->vdd);
	regulator_put(p->vdd);
	iounmap(p->base);
	release_mem_region(p->res->start, resource_size(p->res));
	kfree(p);
	if (!quiet)
		dev_info(&pdev->dev, "v1 removed\n");
}
/* --- end of probe/remove --- */

static struct platform_driver sk_driver = {
	.probe = sk_probe,
	.remove = sk_remove,
	.driver = {
		.name = "g3_sensorkit",
		.of_match_table = sk_of_match,
	},
};
module_platform_driver(sk_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("G3 sensorkit v1: goto ladder");
```

읽을 때 볼 것:

- 사다리에 **라벨 12개**, `goto`가 16줄. 단계 하나(예: 클럭 하나)를 끼워 넣으면 라벨 두 개와 그 아래 모든 점프 대상을 손봐야 한다.
- `sk_remove`는 사다리의 `err_irq:`부터 끝까지와 거의 한 줄씩 대응한다(맨 앞의 `misc_deregister`만 추가). 이 "두 벌 관리"가 goto 방식의 본질적인 비용이다.
- 리셋 GPIO는 `GPIOD_OUT_HIGH`(논리 1 = 리셋 assert)로 얻고 1~2 ms 뒤 논리 0으로 해제한다. active-low 변환은 gpiolib이 해 준다(D4 §8).
- 모든 실패가 마지막에 `dev_err_probe()` 한 곳을 지난다 — §9.3에서 이 함수가 에러 종류마다 다르게 행동하는 것을 본다.

---

## 5. v2 — `devm_*`로 다시 쓰기

### 5.1 직관 — "해제 콜백을 장치에 맡긴다"

devm(device-managed) API는 자원을 얻는 동시에 **"이걸 푸는 함수 + 인자"를 `struct device`의 리스트(devres)에 추가**한다. probe가 실패하거나 unbind되면 driver core가 그 리스트를 **뒤에서부터** 실행한다. Don에게 익숙한 말로는 **펌웨어의 cleanup 스택**(push한 순서의 역순으로 pop)이고, C++의 RAII를 "장치 수명" 단위로 한 것이다.

그래서 devm만 쓴 probe에는 정리 코드가 없다. 실패하면 그냥 `return`한다.

### 5.2 예제 6 — `g3_v2.c`

무엇을 확인하나: 같은 10단계를 devm으로. `devm_add_action_or_reset()`으로 devm 버전이 없는 것(리셋 재-assert, misc 해제)을 등록한다. `bug_order`/`fire_in_remove` 파라미터는 §8의 함정 재현용이다.

```c
// SPDX-License-Identifier: GPL-2.0
/* g3_v2.c — sensorkit 드라이버 v2: devm_* + dev_err_probe + cleanup.h */
#include "g3_common.h"

static int bug_order;		/* 1 = 버퍼를 kmalloc하고 remove에서 kfree (함정 재현) */
module_param(bug_order, int, 0644);
static int fire_in_remove;	/* 1 = remove 끝에서 IRQ를 강제로 한 번 발생 */
module_param(fire_in_remove, int, 0644);

static void sk_misc_unregister(void *data)
{
	misc_deregister(data);
}

static void sk_reset_assert(void *data)
{
	gpiod_set_value_cansleep(data, 1);
}

static int sk_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sk_priv *p;
	u32 id;
	int ret;

	p = INJ(1, 0) ? NULL : devm_kzalloc(dev, sizeof(*p), GFP_KERNEL);	/* 1 */
	if (!p)
		return -ENOMEM;
	p->dev = dev;
	p->base = INJ_PTR(2, devm_platform_ioremap_resource(pdev, 0));		/* 2 */
	if (IS_ERR(p->base))
		return dev_err_probe(dev, PTR_ERR(p->base), "map regs\n");
	ret = INJ(3, devm_regulator_get_enable(dev, "vdd"));			/* 3 */
	if (ret)
		return dev_err_probe(dev, ret, "vdd\n");
	p->clk_core = INJ_PTR(4, devm_clk_get_enabled(dev, "core"));		/* 4 */
	if (IS_ERR(p->clk_core))
		return dev_err_probe(dev, PTR_ERR(p->clk_core), "core clock\n");
	p->clk_ref = INJ_PTR(5, devm_clk_get_enabled(dev, "ref"));		/* 5 */
	if (IS_ERR(p->clk_ref))
		return dev_err_probe(dev, PTR_ERR(p->clk_ref), "ref clock\n");
	p->reset = INJ_PTR(6, devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH));	/* 6 */
	if (IS_ERR(p->reset))
		return dev_err_probe(dev, PTR_ERR(p->reset), "reset gpio\n");
	ret = devm_add_action_or_reset(dev, sk_reset_assert, p->reset);
	if (ret)
		return ret;
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(p->reset, 0);
	msleep(probe_delay_ms);
```

```c
	id = readl(p->base + SK_REG_MAGIC);					/* 7 */
	if (id != SK_CHIP_ID || INJ(7, 0))
		return dev_err_probe(dev, -ENODEV, "bad chip id 0x%08x\n", id);
	if (bug_order)								/* 8 */
		p->buf = INJ(8, 0) ? NULL : kcalloc(SK_BUF_WORDS, sizeof(u32), GFP_KERNEL);
	else
		p->buf = INJ(8, 0) ? NULL : devm_kcalloc(dev, SK_BUF_WORDS, sizeof(u32), GFP_KERNEL);
	if (!p->buf)
		return -ENOMEM;
	{	/* cleanup.h: 임시 버퍼는 스코프를 나가면 자동 kfree */
		u32 *regs __free(kfree) = kcalloc(3, sizeof(u32), GFP_KERNEL);

		if (!regs)
			return -ENOMEM;
		for (int i = 0; i < 3; i++)
			regs[i] = readl(p->base + 4 * i);
		p->buf[0] = 0x5a5a0000 | regs[1];
	}
	platform_set_drvdata(pdev, p);
	p->irq = platform_get_irq(pdev, 0);					/* 9 */
	if (p->irq < 0)
		return p->irq;
	ret = INJ(9, devm_request_irq(dev, p->irq, sk_irq, 0, "g3_sensorkit", p));
	if (ret)
		return dev_err_probe(dev, ret, "irq %d\n", p->irq);
	p->misc.minor = MISC_DYNAMIC_MINOR;					/* 10 */
	p->misc.name = "g3_sensorkit";
	p->misc.fops = &sk_fops;
	p->misc.parent = dev;
	ret = INJ(10, misc_register(&p->misc));
	if (ret)
		return dev_err_probe(dev, ret, "misc\n");
	ret = devm_add_action_or_reset(dev, sk_misc_unregister, &p->misc);
	if (ret)
		return ret;
	if (!quiet)
		dev_info(dev, "v2 probed: id=0x%08x buf0=0x%08x irq=%d\n", id, p->buf[0], p->irq);
	return 0;
}
```

remove와 실험용 IRQ 발생기(전원 관리 콜백·드라이버 구조체는 §12):

```c

static void sk_fire_irq(void)	/* GIC 분배기의 ISPENDR에 직접 써서 SPI 47을 pending */
{
	void __iomem *gicd = ioremap(0x08000000, 0x1000);
	unsigned int hw = 32 + 47;

	writel(BIT(hw % 32), gicd + 0x200 + 4 * (hw / 32));
	iounmap(gicd);
}

static void sk_remove(struct platform_device *pdev)
{
	struct sk_priv *p = platform_get_drvdata(pdev);

	if (bug_order)
		kfree(p->buf);		/* 버그: IRQ는 아직 devm에 살아 있다 */
	if (!quiet)
		dev_info(&pdev->dev, "v2 remove (devm releases after this)\n");
	if (fire_in_remove)
		sk_fire_irq();		/* remove~devres 사이에 들어온 인터럽트 흉내 */
}
/* --- end of probe/remove --- */
```

읽을 때 볼 것:

- **`devm_add_action_or_reset(dev, fn, data)`**: devm 버전이 없는 정리를 devres에 등록한다. "or_reset"은 등록 자체가 실패(메모리 부족)하면 `fn(data)`를 **즉시 불러 주고** 에러를 반환한다는 뜻이다. 그래서 `misc_register()` 성공 직후 등록이 실패해도 misc 장치가 새지 않는다. 그냥 `devm_add_action()`을 쓰면 그 경우를 직접 처리해야 한다.
- `sk_reset_assert`를 `devm_gpiod_get` **바로 다음**에 등록했다. devres는 역순으로 풀리므로 unbind 때 "리셋 assert → GPIO 반납 → 클럭 끔 → 전원 끔" 순서가 자동으로 나온다 — 하드웨어 power-down 시퀀스와 정확히 같다. **devm 순서 = 전원 시퀀스 설계**다.
- `devm_regulator_get_enable()`은 레귤레이터 핸들을 돌려주지 않는다. 런타임에 전압을 바꾸거나 끄고 켤 필요가 있으면 `devm_regulator_get()` + `regulator_enable()` + `devm_add_action_or_reset(dev, disable_fn, reg)`로 쓴다.
- cleanup.h: 칩 레지스터 3개를 임시 버퍼에 읽는 블록에서 `__free(kfree)`를 썼다. 스코프를 나가면(실패든 성공이든) 자동 `kfree`다. **devm은 "장치 수명"**, **`__free`는 "스코프 수명"** — 임시 버퍼에는 `__free`가 맞고, probe가 끝나도 살아야 하는 것에는 devm이 맞다. E5 §4.7의 규칙대로 같은 함수 안에서 goto와 `__free`를 섞지 않는다(v2 probe에는 goto가 없다).
- `dev_err_probe(dev, ret, "...")`는 **에러 코드를 그대로 돌려주는 로그 함수**라서 `return dev_err_probe(...)` 한 줄로 쓴다.

### 5.3 그림으로 비교

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="170" y="22" font-size="13" text-anchor="middle">v1: goto 사다리</text>
<text x="510" y="22" font-size="13" text-anchor="middle">v2: devm</text>
<rect x="20" y="35" width="300" height="32" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="170.0" y="55.2" text-anchor="middle" font-size="12">1 kzalloc</text>
<rect x="20" y="75" width="300" height="32" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="170.0" y="95.2" text-anchor="middle" font-size="12">2 request_mem_region + ioremap</text>
<rect x="20" y="115" width="300" height="32" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="170.0" y="135.2" text-anchor="middle" font-size="12">3 regulator_get + enable</text>
<rect x="20" y="155" width="300" height="32" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="170.0" y="175.2" text-anchor="middle" font-size="12">4 clk_get + enable  ✗ 실패</text>
<rect x="20" y="215" width="300" height="30" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="170.0" y="234.2" text-anchor="middle" font-size="12">err_vdd_off: regulator_disable</text>
<rect x="20" y="251" width="300" height="30" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="170.0" y="270.2" text-anchor="middle" font-size="12">err_vdd_put: regulator_put</text>
<rect x="20" y="287" width="300" height="30" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="170.0" y="306.2" text-anchor="middle" font-size="12">err_unmap: iounmap</text>
<rect x="20" y="323" width="300" height="30" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="170.0" y="342.2" text-anchor="middle" font-size="12">err_release: release_mem_region</text>
<rect x="20" y="359" width="300" height="30" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="170.0" y="378.2" text-anchor="middle" font-size="12">err_free: kfree(p)</text>
<polyline points="320,166 335,166 335,230 323,230" fill="none" stroke="currentColor" marker-end="url(#ah4)"/>
<text x="20" y="410" font-size="12" text-anchor="start">점프 대상을 손으로 고른다 + remove()에 한 벌 더</text>
<rect x="360" y="35" width="300" height="32" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="510.0" y="55.2" text-anchor="middle" font-size="12">1 devm_kzalloc → devres push</text>
<rect x="360" y="75" width="300" height="32" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="510.0" y="95.2" text-anchor="middle" font-size="12">2 devm_platform_ioremap_resource (3개)</text>
<rect x="360" y="115" width="300" height="32" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="510.0" y="135.2" text-anchor="middle" font-size="12">3 devm_regulator_get_enable (2개)</text>
<rect x="360" y="155" width="300" height="32" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="510.0" y="175.2" text-anchor="middle" font-size="12">4 devm_clk_get_enabled  ✗ 실패</text>
<rect x="360" y="215" width="300" height="30" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="510.0" y="234.2" text-anchor="middle" font-size="12">return dev_err_probe(dev, ret, ...)</text>
<line x1="510" y1="195" x2="510" y2="212" stroke="currentColor" marker-end="url(#ah4)"/>
<rect x="360" y="265" width="300" height="66" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="510.0" y="285.2" text-anchor="middle" font-size="12">driver core: devres_release_all()</text>
<text x="510.0" y="302.2" text-anchor="middle" font-size="11">노드를 꼬리부터: regulator → ioremap</text>
<text x="510.0" y="319.2" text-anchor="middle" font-size="11">→ region → 이름 → priv</text>
<line x1="510" y1="245" x2="510" y2="262" stroke="currentColor" marker-end="url(#ah4)"/>
<text x="360" y="410" font-size="12" text-anchor="start">정리 순서는 devres 리스트가 기억한다</text>
</svg>
```
그림 4 — 같은 실패(단계 4 클럭 실패)에서 두 방식의 정리 경로. 왼쪽은 개발자가 라벨로 경로를 짜고, 오른쪽은 driver core가 devres 리스트를 역순으로 돈다. remove()도 마찬가지다: 왼쪽은 사다리를 한 벌 더 쓰고, 오른쪽은 거의 비어 있다.

### 5.4 예제 7 — 줄 수 비교

무엇을 확인하나: 두 파일에서 probe, remove, devm 액션 함수의 줄 수(빈 줄 제외)를 센다.

```python
# count.py — 함수별 줄 수 (빈 줄 제외)
import re, sys
def funcs(path):
    src = open(path).read().split('\n'); out = {}; cur = None
    for ln in src:
        m = re.match(r'^static .*\b(sk_\w+)\(', ln)
        if m and not ln.endswith(';'): cur = m.group(1); out[cur] = []
        if cur: out[cur].append(ln)
        if cur and ln == '}': cur = None
    return out
for f in sys.argv[1:]:
    fs = funcs(f)
    for name in ('sk_misc_unregister', 'sk_reset_assert', 'sk_probe', 'sk_remove'):
        if name not in fs: continue
        body = [l for l in fs[name] if l.strip()]
        ladder = 0
        if name == 'sk_probe' and any(l.startswith('err_') for l in body):
            i = next(i for i, l in enumerate(body) if l.startswith('err_'))
            ladder = len(body) - 1 - i
        demo = sum(1 for l in body if re.search(r'bug_order|fire_in_remove|sk_fire_irq|quiet', l))
        print(f"{f:9s} {name:20s} lines={len(body):3d} ladder={ladder:2d} demo_only~={demo}")
```

```sh
cd /var/tmp/G3/mod && python3 /Users/donh/.elinux_scratch/G3/count.py g3_v1.c g3_v2.c
```

```text
g3_v1.c   sk_probe             lines=110 ladder=26 demo_only~=1
g3_v1.c   sk_remove            lines= 21 ladder= 0 demo_only~=1
g3_v2.c   sk_misc_unregister   lines=  4 ladder= 0 demo_only~=0
g3_v2.c   sk_reset_assert      lines=  4 ladder= 0 demo_only~=0
g3_v2.c   sk_probe             lines= 69 ladder= 0 demo_only~=2
g3_v2.c   sk_remove            lines= 10 ladder= 0 demo_only~=4
```

```svg
<svg viewBox="0 0 640 225" xmlns="http://www.w3.org/2000/svg">
<text x="100" y="52" text-anchor="end" font-size="13">v1 goto 사다리</text>
<rect x="110.0" y="30" width="285.6" height="34" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/>
<text x="252.8" y="52" text-anchor="middle" font-size="12">획득 84</text>
<rect x="395.6" y="30" width="88.4" height="34" fill="#d0564a" fill-opacity="0.25" stroke="#d0564a"/>
<text x="439.8" y="52" text-anchor="middle" font-size="12">사다리 26</text>
<rect x="484.0" y="30" width="71.4" height="34" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
<text x="519.7" y="52" text-anchor="middle" font-size="12">remove 21</text>
<text x="563.4" y="52" font-size="13" font-weight="bold">131줄</text>
<text x="100" y="112" text-anchor="end" font-size="13">v2 devm</text>
<rect x="110.0" y="90" width="234.6" height="34" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/>
<text x="227.3" y="112" text-anchor="middle" font-size="12">probe 69</text>
<rect x="344.6" y="90" width="27.2" height="34" fill="#3f9a6b" fill-opacity="0.25" stroke="#3f9a6b"/>
<text x="358.2" y="112" text-anchor="middle" font-size="12">8</text>
<rect x="371.8" y="90" width="34.0" height="34" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
<text x="388.8" y="112" text-anchor="middle" font-size="12">10</text>
<text x="413.8" y="112" font-size="13" font-weight="bold">87줄</text>
<line x1="110" y1="150" x2="586.0" y2="150" stroke="currentColor"/>
<line x1="110.0" y1="150" x2="110.0" y2="155" stroke="currentColor"/>
<text x="110.0" y="170" text-anchor="middle" font-size="12">0</text>
<line x1="178.0" y1="150" x2="178.0" y2="155" stroke="currentColor"/>
<text x="178.0" y="170" text-anchor="middle" font-size="12">20</text>
<line x1="246.0" y1="150" x2="246.0" y2="155" stroke="currentColor"/>
<text x="246.0" y="170" text-anchor="middle" font-size="12">40</text>
<line x1="314.0" y1="150" x2="314.0" y2="155" stroke="currentColor"/>
<text x="314.0" y="170" text-anchor="middle" font-size="12">60</text>
<line x1="382.0" y1="150" x2="382.0" y2="155" stroke="currentColor"/>
<text x="382.0" y="170" text-anchor="middle" font-size="12">80</text>
<line x1="450.0" y1="150" x2="450.0" y2="155" stroke="currentColor"/>
<text x="450.0" y="170" text-anchor="middle" font-size="12">100</text>
<line x1="518.0" y1="150" x2="518.0" y2="155" stroke="currentColor"/>
<text x="518.0" y="170" text-anchor="middle" font-size="12">120</text>
<line x1="586.0" y1="150" x2="586.0" y2="155" stroke="currentColor"/>
<text x="586.0" y="170" text-anchor="middle" font-size="12">140</text>
<text x="348.0" y="186" text-anchor="middle" font-size="12">빈 줄 제외 줄 수 (probe + remove + devm 액션 함수)</text>
<rect x="20" y="198" width="14" height="14" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/>
<text x="40" y="210" font-size="12">probe 본체</text>
<rect x="175" y="198" width="14" height="14" fill="#d0564a" fill-opacity="0.25" stroke="#d0564a"/>
<text x="195" y="210" font-size="12">에러 사다리</text>
<rect x="330" y="198" width="14" height="14" fill="#3f9a6b" fill-opacity="0.25" stroke="#3f9a6b"/>
<text x="350" y="210" font-size="12">devm 액션 함수</text>
<rect x="485" y="198" width="14" height="14" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
<text x="505" y="210" font-size="12">remove()</text>
</svg>
```
그림 5 — 직접 센 줄 수. v1은 131줄 중 47줄(에러 사다리 26 + remove 21)이 정리 코드다. v2는 87줄이고 정리 코드는 devm 액션 함수 8줄뿐이다. v2의 remove 10줄은 전부 §8 실험용(`bug_order`, `fire_in_remove`, 로그)이라 실제 드라이버라면 **remove 자체가 필요 없다**. v2 probe에는 v1에 없는 cleanup.h 블록(9줄)과 `bug_order` 분기(3줄)가 들어 있어 그만큼 불리한 비교다.

---

## 6. 같은 실패 주입 테스트 — 둘 다 새지 않는가

### 6.1 예제 8 — 정상 경로: 자원이 실제로 어디에 보이나

무엇을 확인하나: 공급자와 v2를 로드한 뒤, probe가 얻은 자원 하나하나를 커널의 관찰 창구에서 확인한다.

```sh
# t1.sh — 정상 경로: 공급자 → v2 로드, 자원 관찰, unbind, 다시 bind
insmod /g3_clkgen.ko; insmod /g3_v2.ko
D=/sys/bus/platform/devices/a003e00.sensorkit
echo "== dmesg"; dmesg | grep -E "g3|sensorkit" | grep -v taints
echo "== driver link"; readlink $D/driver
echo "== iomem";  grep -i a003e00 /proc/iomem
echo "== interrupts"; grep -E "CPU0|g3_" /proc/interrupts
echo "== clk"; grep -E "enable|g3_" /sys/kernel/debug/clk/clk_summary
echo "== regulator"; grep -E "use open|g3_|sensorkit" /sys/kernel/debug/regulator/regulator_summary
echo "== gpio"; grep -E "gpiochip|reset" /sys/kernel/debug/gpio
echo "== misc"; ls -l /dev/g3_sensorkit; cat /dev/g3_sensorkit 2>/dev/null; dmesg | tail -1
```

```text
lab initramfs: 6.18.55 up 1.14s
== dmesg
[    1.274591] g3_clkgen g3-clkgen: clock provider g3_ref19m2 @ 19200000 Hz ready
[    1.307054] g3_sensorkit a003e00.sensorkit: v2 probed: id=0x74726976 buf0=0x5a5a0001 irq=22
== driver link
../../../bus/platform/drivers/g3_sensorkit
== iomem
0a003e00-0a003fff : a003e00.sensorkit sensorkit@a003e00
== interrupts
           CPU0       
 22:          0 GIC-0  79 Edge      g3_sensorkit
== clk
                                 enable  prepare  protect                                duty  hardware                            connection
   clock                          count    count    count        rate   accuracy phase  cycle    enable   consumer                         id
 g3_ref19m2                          1       1        0        19200000    0          0     50000      Y   a003e00.sensorkit               ref                      
 g3_osc12m                           1       1        0        12000000    0          0     50000      Y   a003e00.sensorkit               core                     
== regulator
 regulator                      use open bypass  opmode voltage current     min     max
 g3_vdd_3v3                       2    1      0 unknown  3300mV     0mA  3300mV  3300mV 
    a003e00.sensorkit-vdd         1                                 0mA     0mV     0mV
== gpio
gpiochip0: 8 GPIOs, parent: amba/9030000.pl061, 9030000.pl061:
 gpio-5   (                    |reset               ) out hi ACTIVE LOW
== misc
crw-------    1 0        0          10, 258 Oct 11 01:37 /dev/g3_sensorkit
[    1.537204] g3_sensorkit a003e00.sensorkit: open: chip id 0x74726976
[    1.614681] reboot: Power down
```

(`ls` 출력의 색상 escape 코드는 지웠다.) 출력에서 볼 것 — 단계마다 관찰 창구가 하나씩 있다:

| 단계 | 관찰 창구 | 본 것 |
|---|---|---|
| 2 매핑 | `/proc/iomem` | `a003e00.sensorkit sensorkit@a003e00` — 이름은 `devm_ioremap_resource`가 `"<dev_name> <노드>"`로 만든다 |
| 3 전원 | `regulator_summary` | `g3_vdd_3v3`의 소비자 `a003e00.sensorkit-vdd`, use 2 (기준값 1에서 +1, §6.2) |
| 4 클럭 | `clk_summary` | 두 클럭 모두 enable count 1, consumer 이름과 `clock-names`(core/ref) |
| 5 리셋 | `debug/gpio` | `gpio-5` 라벨 reset, `out hi ACTIVE LOW` — 논리 0(해제) = 물리 hi |
| 6 칩 ID | dmesg | `id=0x74726976` ("virt") |
| 9 IRQ | `/proc/interrupts` | virq 22 = GIC hwirq 79 = SPI 47 + 32, Edge (D4 §4.2의 "+32") |
| 10 등록 | `/dev` | misc major 10, 동적 minor 258. `cat`이 `open()`을 불러 `container_of` 경로가 동작 |

### 6.2 예제 9 — fail_at=1..10: 실패한 직후 자원 상태

무엇을 확인하나: 단계 N을 실패시키고 **probe가 돌아온 직후**(rmmod 전) 모든 창구의 숫자를 센다. 기준값과 같아야 한다. v1과 v2에 같은 테스트를 돌린다.

```sh
# t2.sh — 실패 주입: fail_at=0..10 마다 probe 직후 자원 상태를 센다 (v1, v2)
snap() {
  io=$(grep -c a003e00.sensorkit /proc/iomem); irq=$(grep -c g3_sensorkit /proc/interrupts)
  core=$(awk '$1=="g3_osc12m"{print $2}' /sys/kernel/debug/clk/clk_summary)
  ref=$(awk '$1=="g3_ref19m2"{print $2}' /sys/kernel/debug/clk/clk_summary)
  vdd=$(awk '$1=="g3_vdd_3v3"{print $2}' /sys/kernel/debug/regulator/regulator_summary)
  gp=$(grep -c "|reset" /sys/kernel/debug/gpio); misc=$(ls /dev/g3_sensorkit 2>/dev/null | wc -l)
  echo "iomem=$io irq=$irq core=$core ref=$ref vdd=$vdd gpio=$gp misc=$misc"
}
insmod /g3_clkgen.ko
echo "baseline        : $(snap)"
for v in v1 v2; do
  for n in 0 1 2 3 4 5 6 7 8 9 10; do
    dmesg -c > /dev/null
    insmod /g3_$v.ko fail_at=$n quiet=1
    b=$(ls /sys/bus/platform/devices/a003e00.sensorkit/driver >/dev/null 2>&1 && echo B || echo -)
    printf "%s fail_at=%-2s %s: %s  " $v $n $b "$(snap)"
    dmesg | grep -oE "(error|failed).*" | tail -1
    [ $n = 0 ] && echo
    rmmod g3_$v
    [ $n = 0 ] && echo "$v after rmmod   : $(snap)"
  done
done
echo "final           : $(snap)"
```

```text
lab initramfs: 6.18.55 up 1.07s
baseline        : iomem=0 irq=0 core=0 ref=0 vdd=1 gpio=0 misc=0
v1 fail_at=0  B: iomem=1 irq=1 core=1 ref=1 vdd=2 gpio=1 misc=1  
v1 after rmmod   : iomem=0 irq=0 core=0 ref=0 vdd=1 gpio=0 misc=0
v1 fail_at=1  -: iomem=0 irq=0 core=0 ref=0 vdd=1 gpio=0 misc=0  failed with error -12
v1 fail_at=2  -: iomem=0 irq=0 core=0 ref=0 vdd=1 gpio=0 misc=0  failed with error -5
v1 fail_at=3  -: iomem=0 irq=0 core=0 ref=0 vdd=1 gpio=0 misc=0  failed with error -5
v1 fail_at=4  -: iomem=0 irq=0 core=0 ref=0 vdd=1 gpio=0 misc=0  failed with error -5
v1 fail_at=5  -: iomem=0 irq=0 core=0 ref=0 vdd=1 gpio=0 misc=0  failed with error -5
v1 fail_at=6  -: iomem=0 irq=0 core=0 ref=0 vdd=1 gpio=0 misc=0  failed with error -5
v1 fail_at=7  -: iomem=0 irq=0 core=0 ref=0 vdd=1 gpio=0 misc=0  error -ENODEV: v1 probe failed
v1 fail_at=8  -: iomem=0 irq=0 core=0 ref=0 vdd=1 gpio=0 misc=0  failed with error -12
v1 fail_at=9  -: iomem=0 irq=0 core=0 ref=0 vdd=1 gpio=0 misc=0  failed with error -5
v1 fail_at=10 -: iomem=0 irq=0 core=0 ref=0 vdd=1 gpio=0 misc=0  failed with error -5
v2 fail_at=0  B: iomem=1 irq=1 core=1 ref=1 vdd=2 gpio=1 misc=1  
v2 after rmmod   : iomem=0 irq=0 core=0 ref=0 vdd=1 gpio=0 misc=0
v2 fail_at=1  -: iomem=0 irq=0 core=0 ref=0 vdd=1 gpio=0 misc=0  failed with error -12
v2 fail_at=2  -: iomem=0 irq=0 core=0 ref=0 vdd=1 gpio=0 misc=0  failed with error -5
v2 fail_at=3  -: iomem=0 irq=0 core=0 ref=0 vdd=1 gpio=0 misc=0  failed with error -5
v2 fail_at=4  -: iomem=0 irq=0 core=0 ref=0 vdd=1 gpio=0 misc=0  failed with error -5
v2 fail_at=5  -: iomem=0 irq=0 core=0 ref=0 vdd=1 gpio=0 misc=0  failed with error -5
v2 fail_at=6  -: iomem=0 irq=0 core=0 ref=0 vdd=1 gpio=0 misc=0  failed with error -5
v2 fail_at=7  -: iomem=0 irq=0 core=0 ref=0 vdd=1 gpio=0 misc=0  error -ENODEV: bad chip id 0x74726976
v2 fail_at=8  -: iomem=0 irq=0 core=0 ref=0 vdd=1 gpio=0 misc=0  failed with error -12
v2 fail_at=9  -: iomem=0 irq=0 core=0 ref=0 vdd=1 gpio=0 misc=0  failed with error -5
v2 fail_at=10 -: iomem=0 irq=0 core=0 ref=0 vdd=1 gpio=0 misc=0  failed with error -5
final           : iomem=0 irq=0 core=0 ref=0 vdd=1 gpio=0 misc=0
[    5.933122] reboot: Power down
```

출력에서 볼 것:

- **22줄 전부가 baseline과 같다.** 두 방식 모두 어느 단계에서 실패해도 아무것도 남기지 않았다. 레귤레이터의 `use`는 소비자가 없어도 1이다(이 fixed 레귤레이터의 기준값) — 중요한 것은 +1이 사라졌는지다.
- 이 실행은 콘솔 소음을 줄이려고 `loglevel=1`로 부팅했다(먼저 돌린 같은 테스트에서는 `dev_err_probe`의 에러 줄이 콘솔에도 한 번씩 더 찍혔다). 맨 오른쪽 열은 각 실패의 dmesg 마지막 에러 줄이다. 세 종류가 보인다.
- `failed with error -5`: driver core(`call_driver_probe()`)가 찍는 "probe with driver g3_sensorkit failed with error -5". 바로 앞에 `dev_err_probe`의 `error -EIO: ...` 줄이 있다(§9.3).
- `-12`(-ENOMEM)일 때는 `dev_err_probe`가 **아무것도 찍지 않는다** — "메모리 부족은 이미 충분히 시끄럽다"는 설계다. driver core 줄만 남는다.
- `-ENODEV`(칩 ID 불일치)일 때는 **driver core 줄이 없다.** `-ENODEV`/`-ENXIO`는 "이 장치는 내 것이 아니다(매칭 거절)"라는 뜻이라 `dev_dbg`로만 남긴다. 그래서 칩 ID가 틀린 경우에는 드라이버가 직접 `dev_err`를 찍어야 현장에서 보인다(v1의 `bad chip id` 줄).

### 6.3 예제 10 — 이 테스트는 사다리 버그를 잡는가

테스트가 "다 통과"만 보여 주면 테스트가 무딘 것인지 코드가 맞는 것인지 모른다. 사다리 라벨 하나를 일부러 틀리게 해서(리셋 GPIO 실패 시 ref 클럭의 `disable`을 건너뛰고 `put`만 하도록) 같은 테스트를 돌린다.

```sh
#!/bin/sh
# build_bug.sh — v1의 라벨 하나를 일부러 틀리게: reset gpio 실패 시 ref 클럭 disable을 건너뛴다
set -e
M=/var/tmp/G3/modbug; mkdir -p $M && cd $M
cp /var/tmp/G3/mod/g3_common.h .
sed 's/goto err_ref_off;/goto err_ref_put;/' /var/tmp/G3/mod/g3_v1.c > g3_v1bug.c
diff /var/tmp/G3/mod/g3_v1.c g3_v1bug.c || true
echo "obj-m := g3_v1bug.o" > Kbuild
make -C /var/lab/src/linux-6.18.55 O=/var/tmp/G3/kbuild M=$M modules 2>&1 | grep -E "warning|error|LD" || true
cp g3_v1bug.ko /var/tmp/G3/mod/
```

```text
57c57
< 		goto err_ref_off;
---
> 		goto err_ref_put;
g3_v1bug.c:97:1: warning: label ‘err_ref_off’ defined but not used [-Wunused-label]
  LD [M]  g3_v1bug.ko
```

```sh
# t9.sh — 같은 실패 주입 테스트가 사다리 버그를 잡는가
. /t2_snap.sh
insmod /g3_clkgen.ko
for n in 5 6 7; do
  insmod /g3_v1bug.ko fail_at=$n quiet=1
  echo "v1bug fail_at=$n: $(snap)"; rmmod g3_v1bug
done
```

(`t2_snap.sh`는 `t2.sh`의 `snap()` 함수만 떼어 낸 파일이고, `boot.sh`가 initramfs에 같이 넣는다.)

```text
lab initramfs: 6.18.55 up 1.41s
v1bug fail_at=5: iomem=0 irq=0 core=0 ref=0 vdd=1 gpio=0 misc=0
v1bug fail_at=6: iomem=0 irq=0 core=0 ref=1 vdd=1 gpio=0 misc=0
v1bug fail_at=7: iomem=0 irq=0 core=0 ref=1 vdd=1 gpio=0 misc=0
```

출력에서 볼 것:

- `fail_at=6`에서 `ref=1` — 클럭이 켜진 채 남았다. 테스트가 정확히 그 단계를 짚었다.
- `fail_at=7`의 `ref=1`은 새 누수가 아니라 **앞의 누수가 남은 것**이다. clk의 enable count는 클럭 하나에 전역이므로, 7번 실험에서 +1 했다가 정상적으로 -1 하고 1로 돌아왔다. 실제 하드웨어라면 "클럭이 영원히 안 꺼짐 → 전력 소모 · 다음 probe에서 클럭 rate 변경 실패"로 나타난다.
- 이번에는 컴파일러가 `-Wunused-label`로 힌트를 줬지만, 라벨을 두 군데서 쓰는 버그(예: 다른 goto가 아직 그 라벨을 쓰는 경우)라면 경고도 없다. **실패 주입 테스트가 유일하게 믿을 만한 증거**다. 커널에도 같은 목적의 장치가 있다: `CONFIG_FAULT_INJECTION`의 `failslab`(kmalloc 실패 주입)과 `CONFIG_DEBUG_TEST_DRIVER_REMOVE`(모든 probe 직후 remove→probe를 한 번 더 해 본다 — §13의 `test_remove`).

---

## 7. devres 내부 — 스택처럼 쌓고 거꾸로 푼다

### 7.1 구조

`struct device`에는 `devres_head`라는 리스트가 있다. devm 함수는 대략 이렇게 동작한다.

1. `devres_alloc(release_fn, size)`: "헤더(`struct devres_node` — 리스트 링크, release 함수 포인터, 이름, 크기) + 데이터 영역"을 한 번에 kmalloc한다.
2. 진짜 자원을 얻고, 데이터 영역에 해제에 필요한 값(포인터, IRQ 번호 등)을 적는다.
3. `devres_add(dev, res)`: 리스트 **꼬리**에 붙인다(`list_add_tail`).

해제는 `devres_release_all(dev)` → `release_nodes()`가 리스트를 **꼬리부터**(`list_for_each_entry_safe_reverse`) 돌며 `release(dev, data)`를 부르고 노드를 `kfree`한다. 이 함수는 probe 실패 경로와 unbind 경로가 공유하는 `device_unbind_cleanup()`의 첫 줄이다(§13).

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="200" y="352" width="300" height="24" rx="3" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="212" y="368" font-size="12"> 1  devm_kzalloc_release (160) — priv</text>
<rect x="200" y="326" width="300" height="24" rx="3" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="212" y="342" font-size="12"> 2  devm_kzalloc_release (36) — 이름</text>
<rect x="200" y="300" width="300" height="24" rx="3" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="212" y="316" font-size="12"> 3  devm_region_release</text>
<rect x="200" y="274" width="300" height="24" rx="3" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="212" y="290" font-size="12"> 4  devm_ioremap_release</text>
<rect x="200" y="248" width="300" height="24" rx="3" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="212" y="264" font-size="12"> 5  devm_regulator_release</text>
<rect x="200" y="222" width="300" height="24" rx="3" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="212" y="238" font-size="12"> 6  &amp;regulator_action_disable</text>
<rect x="200" y="196" width="300" height="24" rx="3" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="212" y="212" font-size="12"> 7  devm_clk_release (core)</text>
<rect x="200" y="170" width="300" height="24" rx="3" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="212" y="186" font-size="12"> 8  devm_clk_release (ref)</text>
<rect x="200" y="144" width="300" height="24" rx="3" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="212" y="160" font-size="12"> 9  devm_gpiod_release</text>
<rect x="200" y="118" width="300" height="24" rx="3" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="212" y="134" font-size="12">10  sk_reset_assert</text>
<rect x="200" y="92" width="300" height="24" rx="3" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="212" y="108" font-size="12">11  devm_kzalloc_release (4096) — buf</text>
<rect x="200" y="66" width="300" height="24" rx="3" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="212" y="82" font-size="12">12  devm_irq_release</text>
<rect x="200" y="40" width="300" height="24" rx="3" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="212" y="56" font-size="12">13  sk_misc_unregister</text>
<text x="345" y="28" font-size="12" text-anchor="middle">dev->devres_head (꼬리 = 위)</text>
<line x1="180" y1="378" x2="180" y2="44" stroke="currentColor" marker-end="url(#ah6)"/>
<text x="170" y="215" font-size="12" text-anchor="end">probe:</text>
<text x="170" y="233" font-size="12" text-anchor="end">ADD</text>
<text x="170" y="251" font-size="12" text-anchor="end">list_add_tail</text>
<line x1="520" y1="44" x2="520" y2="378" stroke="currentColor" marker-end="url(#ah6)"/>
<text x="530" y="215" font-size="12" text-anchor="start">unbind·실패:</text>
<text x="530" y="233" font-size="12" text-anchor="start">REL 위에서부터</text>
<text x="530" y="251" font-size="12" text-anchor="start">release_nodes()</text>
<line x1="195" y1="117" x2="505" y2="117" stroke="#d0564a" stroke-width="2" stroke-dasharray="6,4"/>
<text x="170" y="113" font-size="12" text-anchor="end">fail_at=7:</text>
<text x="170" y="131" font-size="12" text-anchor="end">여기까지만</text>
<text x="345" y="405" font-size="12" text-anchor="middle">초록 = fail_at=7에서도 쌓이는 노드, 파랑 = 단계 8~10에서 추가</text>
</svg>
```
그림 6 — 예제 11에서 실제로 찍힌 devres 노드 13개(아래가 먼저 쌓인 것). probe가 아래부터 쌓고, unbind는 위부터 푼다. `fail_at=7`이면 10번(`sk_reset_assert`)까지만 쌓이고 그 지점부터 역순으로 풀린다.

### 7.2 예제 11 — devres 로그 보기

무엇을 확인하나: `CONFIG_DEBUG_DEVRES` 커널에서 로그를 켜고, v2의 정상 probe → unbind, 그리고 단계 7 실패를 본다.

```sh
# t3.sh — devres 로그(CONFIG_DEBUG_DEVRES): 정상 probe → unbind, 그리고 fail_at=7 probe 실패
insmod /g3_clkgen.ko
echo 1 > /sys/module/devres/parameters/log
dmesg -c > /dev/null
echo "== v2 probe"; insmod /g3_v2.ko quiet=1
dmesg -c | grep a003e00 | sed 's/^\[[ 0-9.]*\] //; s/ffff[0-9a-f]*/PTR/'
echo "== unbind"; echo a003e00.sensorkit > /sys/bus/platform/drivers/g3_sensorkit/unbind
dmesg -c | grep a003e00 | sed 's/^\[[ 0-9.]*\] //; s/ffff[0-9a-f]*/PTR/'
rmmod g3_v2
echo "== v2 fail_at=7"; insmod /g3_v2.ko quiet=1 fail_at=7
dmesg -c | grep a003e00 | sed 's/^\[[ 0-9.]*\] //; s/ffff[0-9a-f]*/PTR/'
```

```text
lab initramfs: 6.18.55 up 4.40s
== v2 probe
g3_sensorkit a003e00.sensorkit: DEVRES ADD 00000000f67bf890 devm_kzalloc_release (40 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES REM 00000000f67bf890 devm_kzalloc_release (40 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES ADD 00000000d6aa1f0a devm_kzalloc_release (160 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES ADD 00000000f67bf890 devm_kzalloc_release (36 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES ADD 00000000f145de0e devm_region_release (24 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES ADD 0000000011677abd devm_ioremap_release (8 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES ADD 0000000076345a75 devm_regulator_release (8 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES ADD 000000008adeef13 &regulator_action_disable (16 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES ADD 000000002678f97f devm_clk_release (16 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES ADD 00000000eedad64d devm_clk_release (16 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES ADD 0000000047b5066c devm_gpiod_release (16 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES ADD 00000000d77e69c7 sk_reset_assert (16 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES ADD 00000000f9ff79a1 devm_kzalloc_release (4096 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES ADD 000000008aed9a25 devm_irq_release (16 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES ADD 00000000b156c973 sk_misc_unregister (16 bytes)
== unbind
g3_sensorkit a003e00.sensorkit: DEVRES REL 00000000b156c973 sk_misc_unregister (16 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES REL 000000008aed9a25 devm_irq_release (16 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES REL 00000000f9ff79a1 devm_kzalloc_release (4096 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES REL 00000000d77e69c7 sk_reset_assert (16 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES REL 0000000047b5066c devm_gpiod_release (16 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES REL 00000000eedad64d devm_clk_release (16 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES REL 000000002678f97f devm_clk_release (16 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES REL 000000008adeef13 &regulator_action_disable (16 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES REL 0000000076345a75 devm_regulator_release (8 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES REL 0000000011677abd devm_ioremap_release (8 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES REL 00000000f145de0e devm_region_release (24 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES REL 00000000f67bf890 devm_kzalloc_release (36 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES REL 00000000d6aa1f0a devm_kzalloc_release (160 bytes)
== v2 fail_at=7
g3_sensorkit a003e00.sensorkit: DEVRES ADD 00000000ff49566f devm_kzalloc_release (40 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES REM 00000000ff49566f devm_kzalloc_release (40 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES ADD 00000000d6aa1f0a devm_kzalloc_release (160 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES ADD 00000000ff49566f devm_kzalloc_release (36 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES ADD 00000000f67bf890 devm_region_release (24 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES ADD 00000000f145de0e devm_ioremap_release (8 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES ADD 0000000011677abd devm_regulator_release (8 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES ADD 0000000056dedffc &regulator_action_disable (16 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES ADD 000000008adeef13 devm_clk_release (16 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES ADD 000000002678f97f devm_clk_release (16 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES ADD 00000000eedad64d devm_gpiod_release (16 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES ADD 0000000047b5066c sk_reset_assert (16 bytes)
g3_sensorkit a003e00.sensorkit: error -ENODEV: bad chip id 0x74726976
g3_sensorkit a003e00.sensorkit: DEVRES REL 0000000047b5066c sk_reset_assert (16 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES REL 00000000eedad64d devm_gpiod_release (16 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES REL 000000002678f97f devm_clk_release (16 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES REL 000000008adeef13 devm_clk_release (16 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES REL 0000000056dedffc &regulator_action_disable (16 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES REL 0000000011677abd devm_regulator_release (8 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES REL 00000000f145de0e devm_ioremap_release (8 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES REL 00000000f67bf890 devm_region_release (24 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES REL 00000000ff49566f devm_kzalloc_release (36 bytes)
g3_sensorkit a003e00.sensorkit: DEVRES REL 00000000d6aa1f0a devm_kzalloc_release (160 bytes)
[    6.201080] reboot: Power down
```

(포인터는 `%p` 해시값이라 실행마다 다르고 실제 주소가 아니다.) 출력에서 볼 것 — 한 줄 한 줄이 v2 소스의 한 줄과 대응한다:

- **첫 ADD/REM 40바이트는 우리 코드가 아니다.** `really_probe()`가 probe 전에 부르는 `pinctrl_bind_pins()`가 `dev->pins`(포인터 5개 = 40바이트)를 devm으로 잡았다가, 이 노드에 `pinctrl-0`이 없으니 `devm_kfree`로 바로 돌려준 것이다. "REM"은 release 콜백을 부르지 않고 노드를 리스트에서 떼어냈다는 뜻이다(`devm_kfree`는 그 뒤 메모리를 직접 해제한다).
- 160바이트 = `sizeof(struct sk_priv)`. 36바이트 = `/proc/iomem`에 보인 이름 문자열 `"a003e00.sensorkit sensorkit@a003e00"`(35자 + NUL) — `devm_platform_ioremap_resource()` 한 줄이 노드 **세 개**(이름, region, ioremap)를 쌓는다.
- `devm_regulator_get_enable()`도 두 개(핸들 반납 + `regulator_action_disable`). `devm_clk_get_enabled()`는 노드 하나에 "disable_unprepare + put"을 같이 담는다.
- **unbind의 REL 순서는 ADD의 정확한 역순**이다. misc 해제 → IRQ 해제 → 버퍼 → 리셋 assert → GPIO → 클럭 → 전원 → 매핑 → priv. 그림 2를 아래에서 위로 읽은 것과 같다.
- `fail_at=7`: 10번 노드까지만 쌓이고, `dev_err_probe` 줄 직후에 10 → 1 역순으로 풀렸다. 드라이버의 probe는 `return` 한 줄이었다.

### 7.3 devm 사용 규칙

| 규칙 | 이유 |
|---|---|
| devm 자원은 손으로 해제하지 않는다 | 이중 해제. 꼭 일찍 풀어야 하면 `devm_kfree`, `devm_free_irq`, `devm_release_action`처럼 **devm 전용 해제**를 쓴다(리스트에서도 빠진다) |
| devm은 probe 안에서만 | `open()`·`ioctl()`에서 `devm_kzalloc`하면 unbind 때까지 안 풀린다 → 열 때마다 쌓이는 누수 |
| 얻는 순서 = 역순 해제 순서를 설계한다 | 리셋 assert 액션은 GPIO를 얻은 직후, IRQ는 핸들러가 쓰는 데이터보다 나중에 |
| devm 다음에 non-devm을 두지 않는다 | 섞이면 remove()에서 non-devm을 먼저 풀고 devm은 나중에 풀린다 → 순서 역전 (§8) |
| 부분 묶음 해제는 devres group | `devres_open_group`/`devres_release_group` — 한 probe 안에서 "이 묶음만 실패 시 되돌리기" (드물다) |

---

## 8. teardown 순서 함정 — remove()와 devres 사이의 틈

### 8.1 직관

unbind 때 driver core는 **① `drv->remove()` → ② `devres_release_all()`** 순서로 부른다(§13). 그러니 remove()에서 손으로 푼 것은 devm 자원보다 **먼저** 풀린다. remove()에서 버퍼를 `kfree`했는데 그 버퍼를 읽는 IRQ 핸들러가 `devm_request_irq`로 등록돼 있다면, ①과 ② 사이에 들어온 인터럽트는 **해제된 메모리**를 읽는다.

```svg
<svg viewBox="0 0 680 290" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="20" y="24" font-size="13" text-anchor="start">bug_order=1 (버퍼는 kmalloc, remove에서 kfree)</text>
<rect x="20" y="36" width="130" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="85.0" y="53.7" text-anchor="middle" font-size="12">drv->remove()</text>
<text x="85.0" y="70.7" text-anchor="middle" font-size="11">kfree(p->buf)</text>
<rect x="170" y="36" width="170" height="44" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="255.0" y="53.7" text-anchor="middle" font-size="12">IRQ 도착</text>
<text x="255.0" y="70.7" text-anchor="middle" font-size="11">buf[0] = 0x6b6b6b6b</text>
<rect x="360" y="36" width="140" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="430.0" y="53.7" text-anchor="middle" font-size="12">devres: misc 해제</text>
<text x="430.0" y="70.7" text-anchor="middle" font-size="11">→ free_irq</text>
<rect x="520" y="36" width="140" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="590.0" y="53.7" text-anchor="middle" font-size="12">나머지 devres</text>
<text x="590.0" y="70.7" text-anchor="middle" font-size="11">역순 해제</text>
<line x1="150" y1="58" x2="167" y2="58" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="340" y1="58" x2="357" y2="58" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="500" y1="58" x2="517" y2="58" stroke="currentColor" marker-end="url(#ah7)"/>
<text x="170" y="100" font-size="12" text-anchor="start">↑ 해제된 메모리 읽기 (use-after-free)</text>
<text x="20" y="150" font-size="13" text-anchor="start">bug_order=0 (버퍼도 devm, IRQ보다 먼저 얻음)</text>
<rect x="20" y="162" width="130" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="85.0" y="179.7" text-anchor="middle" font-size="12">drv->remove()</text>
<text x="85.0" y="196.7" text-anchor="middle" font-size="11">(아무것도 안 풂)</text>
<rect x="170" y="162" width="170" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="255.0" y="179.7" text-anchor="middle" font-size="12">IRQ 도착</text>
<text x="255.0" y="196.7" text-anchor="middle" font-size="11">buf[0] = 0x5a5a0001</text>
<rect x="360" y="162" width="140" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="430.0" y="179.7" text-anchor="middle" font-size="12">devres: misc 해제</text>
<text x="430.0" y="196.7" text-anchor="middle" font-size="11">→ free_irq</text>
<rect x="520" y="162" width="140" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="590.0" y="179.7" text-anchor="middle" font-size="12">그다음 buf 해제</text>
<text x="590.0" y="196.7" text-anchor="middle" font-size="11">→ … → priv</text>
<line x1="150" y1="184" x2="167" y2="184" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="340" y1="184" x2="357" y2="184" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="500" y1="184" x2="517" y2="184" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="20" y1="250" x2="660" y2="250" stroke="currentColor" marker-end="url(#ah7)"/>
<text x="340" y="275" font-size="12" text-anchor="middle">시간 →  (unbind 한 번 안에서)</text>
</svg>
```
그림 7 — unbind 타임라인. 위(bug_order=1): 버퍼가 remove()에서 먼저 해제되고 IRQ는 devres가 나중에 해제한다 → 그 사이의 인터럽트가 해제된 버퍼를 읽는다. 아래(bug_order=0): 버퍼도 devm이고 IRQ보다 먼저 얻었으므로 IRQ가 먼저 풀린다.

### 8.2 예제 12 — 그 틈에 인터럽트를 넣어 보기

무엇을 확인하나: `fire_in_remove=1`이면 remove() 끝에서 GIC 분배기의 `ISPENDR`에 비트를 써서 SPI 47을 강제로 pending시킨다(진짜 하드웨어에서 "마침 그때 샘플이 도착"한 상황). 해제된 메모리를 눈에 보이게 하려고 `slub_debug=P,kmalloc-4k`(해제된 객체를 `0x6b`로 채우는 poisoning)로 부팅한다.

```sh
# t7.sh — remove()와 devres 해제 사이의 틈: IRQ가 해제된 버퍼를 읽는가
insmod /g3_clkgen.ko
DRV=/sys/bus/platform/drivers/g3_sensorkit
for b in 0 1; do
  dmesg -c > /dev/null
  insmod /g3_v2.ko bug_order=$b fire_in_remove=1
  echo "== bug_order=$b: unbind"; echo a003e00.sensorkit > $DRV/unbind
  dmesg | grep sensorkit | sed 's/^\[[ 0-9.]*\] //'
  rmmod g3_v2
done
```

```sh
sh /Users/donh/.elinux_scratch/G3/boot.sh t7.sh "loglevel=1 slub_debug=P,kmalloc-4k"
```

```text
lab initramfs: 6.18.55 up 1.45s
== bug_order=0: unbind
g3_sensorkit a003e00.sensorkit: v2 probed: id=0x74726976 buf0=0x5a5a0001 irq=22
g3_sensorkit a003e00.sensorkit: v2 remove (devm releases after this)
g3_sensorkit a003e00.sensorkit: irq 22: buf[0]=0x5a5a0001 count=1
== bug_order=1: unbind
g3_sensorkit a003e00.sensorkit: v2 probed: id=0x74726976 buf0=0x5a5a0001 irq=22
g3_sensorkit a003e00.sensorkit: v2 remove (devm releases after this)
g3_sensorkit a003e00.sensorkit: irq 22: buf[0]=0x6b6b6b6b count=1
[    1.772253] reboot: Power down
```

출력에서 볼 것:

- 두 경우 모두 **remove()가 끝난 뒤에도 IRQ 핸들러가 불렸다** — devm IRQ는 아직 등록돼 있다.
- `bug_order=1`: 핸들러가 읽은 값이 `0x6b6b6b6b` = SLUB poison. **use-after-free**다. 이 실험에서는 읽기만 해서 조용히 지나갔지만, 핸들러가 쓰기를 했다면 다른 객체를 망가뜨리고 한참 뒤 엉뚱한 곳에서 터진다. poisoning이 없는 양산 커널에서는 "가끔 이상한 값"으로만 보인다.
- `bug_order=0`: 버퍼가 devm이고 IRQ보다 **먼저** 얻었으므로 역순 해제에서 IRQ가 먼저 풀린다. remove 직후 들어온 인터럽트도 살아 있는 버퍼를 읽었다(`0x5a5a0001`).

### 8.3 고치는 법

1. **전부 devm으로, 의존 순서대로 얻는다** — 핸들러가 쓰는 모든 것(버퍼, 클럭, 매핑)을 `devm_request_irq`보다 먼저 얻는다. 가장 흔한 정답.
2. devm 버전이 없는 자원은 `devm_add_action_or_reset()`으로 devres에 끼워 넣어 순서를 지킨다(v2의 `sk_misc_unregister`).
3. 꼭 remove()에서 손으로 풀어야 한다면, 그 전에 IRQ를 먼저 끈다: `disable_irq()`(진행 중인 핸들러를 기다린다) 또는 `devm_free_irq()`. 하드웨어 쪽 인터럽트 enable 비트도 먼저 끈다.
4. workqueue·timer도 같다: remove 이후에 돌 수 있다. `devm_delayed_work_autocancel()`, 또는 `devm_add_action_or_reset`으로 `cancel_work_sync`를 등록한다(F7).
5. 유저 공간이 파일을 열어 둔 채 unbind되는 경우는 devm으로 해결되지 않는다 — 열린 파일이 private 구조체를 가리키고 있으므로 `kref`로 수명을 따로 관리해야 한다(G4에서 다룬다).

---

## 9. `-EPROBE_DEFER`와 deferred probe

### 9.1 직관 — "공급자가 아직 없으면 줄 서서 기다린다"

센서의 probe가 `devm_clk_get_enabled(dev, "ref")`를 불렀는데, 그 클럭을 만드는 드라이버(공급자)가 아직 probe되지 않았다면? 펌웨어라면 init 순서를 손으로 바꾼다. Linux는 수백 개 드라이버의 순서를 손으로 맞출 수 없으므로 이렇게 한다.

- 공급자를 못 찾은 API가 `-EPROBE_DEFER`를 돌려준다. 드라이버는 **그대로 위로 전달**한다.
- driver core는 그 장치를 **deferred pending 리스트**에 넣는다. 그동안 얻었던 devm 자원은 전부 풀린다(실패와 같은 경로).
- **어떤 드라이버든 bind에 성공할 때마다**(`driver_bound()` → `driver_deferred_probe_trigger()`) pending 리스트 전체를 active 리스트로 옮기고, workqueue(`deferred_probe_work_func`)가 하나씩 다시 `bus_probe_device()`한다. "누가 새로 생겼으니 혹시 너도 이제 되나?"를 묻는 것이다.
- 부팅 끝(late_initcall)에 한 번 더 전체 재시도. 모듈이 있는 커널에서는 `deferred_probe_timeout`(기본 10초, D3 §10)이 지나면 남은 장치를 경고와 함께 정리한다.

```svg
<svg viewBox="0 0 680 440" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah8" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="15" y="12" width="190" height="34" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="110.0" y="33.5" text-anchor="middle" font-size="13">sensorkit (소비자)</text>
<line x1="110" y1="46" x2="110" y2="430" stroke="currentColor" stroke-opacity="0.4" stroke-dasharray="3,4"/>
<rect x="245" y="12" width="190" height="34" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="340.0" y="33.5" text-anchor="middle" font-size="13">driver core</text>
<line x1="340" y1="46" x2="340" y2="430" stroke="currentColor" stroke-opacity="0.4" stroke-dasharray="3,4"/>
<rect x="475" y="12" width="190" height="34" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="570.0" y="33.5" text-anchor="middle" font-size="13">g3-clkgen (공급자)</text>
<line x1="570" y1="46" x2="570" y2="430" stroke="currentColor" stroke-opacity="0.4" stroke-dasharray="3,4"/>
<line x1="110" y1="80" x2="336" y2="80" stroke="currentColor" marker-end="url(#ah8)"/>
<text x="225.0" y="74" font-size="12" text-anchor="middle">insmod g3_v2 → match → really_probe</text>
<rect x="240" y="92" width="200" height="40" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="340.0" y="107.7" text-anchor="middle" font-size="12">링크 확인: 공급자 미bind</text>
<text x="340.0" y="124.7" text-anchor="middle" font-size="11">→ -EPROBE_DEFER</text>
<rect x="240" y="142" width="200" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340.0" y="157.7" text-anchor="middle" font-size="12">pending 목록에 추가</text>
<text x="340.0" y="174.7" text-anchor="middle" font-size="11">이유 문자열 저장</text>
<text x="118" y="125" font-size="12" text-anchor="start">probe() 호출 없음</text>
<text x="118" y="143" font-size="12" text-anchor="start">(전원·리셋 그대로)</text>
<line x1="570" y1="215" x2="344" y2="215" stroke="currentColor" marker-end="url(#ah8)"/>
<text x="455.0" y="209" font-size="12" text-anchor="middle">insmod g3_clkgen → probe 성공</text>
<rect x="240" y="227" width="200" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="340.0" y="242.7" text-anchor="middle" font-size="12">driver_bound()</text>
<text x="340.0" y="259.7" text-anchor="middle" font-size="11">→ deferred trigger</text>
<rect x="240" y="277" width="200" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340.0" y="292.7" text-anchor="middle" font-size="12">pending → active</text>
<text x="340.0" y="309.7" text-anchor="middle" font-size="11">queue_work()</text>
<rect x="240" y="327" width="200" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="340.0" y="342.7" text-anchor="middle" font-size="12">deferred_probe_work_func</text>
<text x="340.0" y="359.7" text-anchor="middle" font-size="11">→ bus_probe_device()</text>
<line x1="340" y1="395" x2="114" y2="395" stroke="currentColor" marker-end="url(#ah8)"/>
<text x="225.0" y="389" font-size="12" text-anchor="middle">다시 really_probe → probe() → 0</text>
<rect x="15" y="405" width="190" height="26" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="110.0" y="422.2" text-anchor="middle" font-size="12">bound</text>
</svg>
```
그림 8 — 예제 13(fw_devlink 기본값)의 실제 순서. 소비자의 probe 함수는 공급자가 오기 전에는 **한 번도 불리지 않았다** — fw_devlink가 driver core 단계에서 막았다. 공급자가 bind되는 순간 trigger → workqueue → 재probe가 자동으로 일어난다.

### 9.2 fw_devlink — 기다림을 DT에서 미리 계산

예전 방식(드라이버가 직접 `-EPROBE_DEFER`를 돌려주는 것)만 있으면, 소비자는 공급자가 올 때까지 **probe를 여러 번 시작했다가 중간에 포기**한다. 매번 전원을 켰다 끄고 로그를 남긴다. 그래서 fw_devlink가 생겼다: 장치를 만들 때 DT의 phandle 속성(`clocks`, `*-supply`, `*-gpios`, `interrupts`, `resets`, `pinctrl-0`…)을 읽어 **공급자 → 소비자 device link**를 미리 만든다. 그러면 `really_probe()`가 probe를 부르기 전에 `device_links_check_suppliers()`로 "공급자 드라이버가 다 bind됐나?"를 보고, 아니면 probe를 부르지도 않고 defer한다.

| `fw_devlink=` | 동작 |
|---|---|
| `off` | 링크를 만들지 않음. 순서는 드라이버의 `-EPROBE_DEFER`에만 의존 |
| `permissive` | 링크는 sync_state용으로만. probe 순서를 막지 않음 |
| `on` | 공급자가 bind될 때까지 소비자 probe를 막음 |
| `rpm` | on + runtime PM 링크까지. **6.18 소스의 기본값**(`fw_devlink_flags = FW_DEVLINK_FLAGS_RPM`) |

### 9.3 `dev_err_probe()` — 에러마다 다르게 행동하는 로그 함수

```sh
sed -n 5091,5100p drivers/base/core.c
```

```text
	switch (err) {
	case -EPROBE_DEFER:
		device_set_deferred_probe_reason(dev, &vaf);
		dev_dbg(dev, "error %pe: %pV", ERR_PTR(err), &vaf);
		break;

	case -ENOMEM:
		/* Don't print anything on -ENOMEM, there's already enough output */
		break;

```

| `err` | 동작 | 이유 |
|---|---|---|
| `-EPROBE_DEFER` | 이유를 저장(`devices_deferred`에 보임), 로그는 dbg | 부팅 중 defer는 정상 — 로그를 어지럽히지 않는다 |
| `-ENOMEM` | 아무것도 안 찍음 | 할당 실패는 mm이 이미 크게 경고한다 |
| 그 밖 | `dev_err`로 `error -EIO: <메시지>` | `%pe`로 에러 이름을 사람이 읽게 찍는다 |

그리고 **`err`를 그대로 반환**하므로 `return dev_err_probe(dev, ret, "...")` 한 줄이다. 직접 `dev_err` + `return ret`을 쓰면 defer마다 에러 로그가 찍혀 "부팅 로그에 빨간 줄이 수십 개"가 된다 — 리뷰에서 자주 지적받는 부분이다.

### 9.4 예제 13 — 공급자가 늦게 오면

무엇을 확인하나: 소비자(v2)를 먼저, 공급자(`g3_clkgen`)를 나중에 로드한다. `devices_deferred`, device link 상태, 자동 재probe, 그리고 **공급자가 떠날 때** 소비자에게 일어나는 일을 fw_devlink 기본값과 `permissive`에서 비교한다.

```sh
# t4.sh — deferred probe: 소비자(v2)를 먼저, 공급자(g3_clkgen)를 나중에
D=/sys/bus/platform/devices/a003e00.sensorkit
echo "== fw_devlink: $(cat /proc/cmdline | grep -o 'fw_devlink=[a-z]*' || echo 'default(on)')"
echo "== devlink 목록 (supplier--consumer)"; ls /sys/class/devlink | grep sensorkit
L=$(ls -d /sys/class/devlink/*sensorkit* | grep clkgen)
echo "== $L/status: $(cat $L/status 2>/dev/null)"
dmesg -c > /dev/null
echo "== insmod g3_v2 (공급자 없음)"; insmod /g3_v2.ko
dmesg -c | grep -E "sensorkit" | sed 's/^\[[ 0-9.]*\] //'
echo "== devices_deferred"; cat /sys/kernel/debug/devices_deferred
echo "== waiting_for_supplier: $(cat $D/waiting_for_supplier 2>&1)"
echo "== bound? $(readlink $D/driver || echo no)"
echo "== insmod g3_clkgen (공급자 도착)"; insmod /g3_clkgen.ko; sleep 1
dmesg -c | grep -E "g3|sensorkit" | sed 's/^\[[ 0-9.]*\] //'
echo "== devices_deferred"; cat /sys/kernel/debug/devices_deferred; echo "[end]"
echo "== bound? $(readlink $D/driver)"; echo "== link status: $(cat $L/status)"
echo "== lsmod"; lsmod | grep g3_
echo "== rmmod g3_clkgen"; rmmod g3_clkgen
echo "== 공급자 unbind (sysfs)"; echo g3-clkgen > /sys/bus/platform/drivers/g3_clkgen/unbind
dmesg -c | grep -E "g3|sensorkit" | sed 's/^\[[ 0-9.]*\] //'
echo "== bound? $(readlink $D/driver || echo no)"; echo "== link status: $(cat $L/status 2>&1)"
echo "== devices_deferred"; cat /sys/kernel/debug/devices_deferred
```

기본값(커널 인자 없음 — 스크립트가 "default(on)"이라고 찍지만 6.18 실제 기본값은 `rpm`이다):

```text
lab initramfs: 6.18.55 up 1.16s
== fw_devlink: default(on)
== devlink 목록 (supplier--consumer)
amba:9030000.pl061--platform:a003e00.sensorkit
platform:g3-clkgen--platform:a003e00.sensorkit
platform:g3-vdd--platform:a003e00.sensorkit
== /sys/class/devlink/platform:g3-clkgen--platform:a003e00.sensorkit/status: dormant
== insmod g3_v2 (공급자 없음)
== devices_deferred
a003e00.sensorkit	platform: supplier g3-clkgen not ready
== waiting_for_supplier: 0
== bound? no
== insmod g3_clkgen (공급자 도착)
g3_clkgen g3-clkgen: clock provider g3_ref19m2 @ 19200000 Hz ready
g3_sensorkit a003e00.sensorkit: v2 probed: id=0x74726976 buf0=0x5a5a0001 irq=22
== devices_deferred
[end]
== bound? ../../../bus/platform/drivers/g3_sensorkit
== link status: active
== lsmod
g3_clkgen 12288 1 - Live 0xffffaa18f7690000 (O)
g3_v2 12288 0 - Live 0xffffaa18f768a000 (O)
== rmmod g3_clkgen
rmmod: remove 'g3_clkgen': Resource temporarily unavailable
== 공급자 unbind (sysfs)
g3_sensorkit a003e00.sensorkit: v2 remove (devm releases after this)
== bound? no
== link status: dormant
== devices_deferred
[    6.606744] reboot: Power down
```

`fw_devlink=permissive`:

```text
lab initramfs: 6.18.55 up 1.71s
== fw_devlink: fw_devlink=permissive
== devlink 목록 (supplier--consumer)
amba:9030000.pl061--platform:a003e00.sensorkit
platform:g3-clkgen--platform:a003e00.sensorkit
platform:g3-vdd--platform:a003e00.sensorkit
== /sys/class/devlink/platform:g3-clkgen--platform:a003e00.sensorkit/status: dormant
== insmod g3_v2 (공급자 없음)
== devices_deferred
a003e00.sensorkit	g3_sensorkit: ref clock
== waiting_for_supplier: cat: can't open '/sys/bus/platform/devices/a003e00.sensorkit/waiting_for_supplier': No such file or directory
== bound? no
== insmod g3_clkgen (공급자 도착)
g3_clkgen g3-clkgen: clock provider g3_ref19m2 @ 19200000 Hz ready
g3_sensorkit a003e00.sensorkit: v2 probed: id=0x74726976 buf0=0x5a5a0001 irq=22
== devices_deferred
[end]
== bound? ../../../bus/platform/drivers/g3_sensorkit
cat: can't open '/sys/class/devlink/platform:g3-clkgen--platform:a003e00.sensorkit/status': No such file or directory
== link status: 
== lsmod
g3_clkgen 12288 1 - Live 0xffffaa18ffc02000 (O)
g3_v2 12288 0 - Live 0xffffaa18ffbfc000 (O)
== rmmod g3_clkgen
rmmod: remove 'g3_clkgen': Resource temporarily unavailable
== 공급자 unbind (sysfs)
clk_unregister: unregistering prepared clock: g3_ref19m2
== bound? ../../../bus/platform/drivers/g3_sensorkit
== link status: cat: can't open '/sys/class/devlink/platform:g3-clkgen--platform:a003e00.sensorkit/status': No such file or directory
== devices_deferred
[    3.558185] reboot: Power down
```

출력에서 볼 것 — 두 실행을 나란히 읽으면 fw_devlink가 하는 일이 다 보인다:

- **device link는 부팅 때 DT만 보고 이미 만들어졌다.** sensorkit의 공급자 셋(pl061 GPIO, g3-clkgen, g3-vdd)이 `/sys/class/devlink`에 있다. 우리 드라이버는 아직 로드도 안 됐다. 공급자 드라이버가 없으니 상태는 `dormant`.
- **defer 이유가 다르다.** 기본값에서는 `platform: supplier g3-clkgen not ready` — driver core가 probe를 부르기 전에 막았다(우리 `sk_probe`는 한 번도 안 불렸고, 그래서 전원·리셋도 안 건드렸다). permissive에서는 `g3_sensorkit: ref clock` — 우리 probe가 단계 1~4를 실행하고 5에서 `devm_clk_get_enabled`가 `-EPROBE_DEFER`를 받았으며, `dev_err_probe`가 저장한 메시지가 보인다. dmesg에는 두 경우 모두 아무것도 안 찍혔다.
- `waiting_for_supplier`가 0인 것은 "공급자 **장치**가 아직 생성되지 않아 기다리는 중인가"를 뜻하기 때문이다. g3-clkgen 장치는 이미 있고 드라이버만 없다.
- **자동 재probe**: 두 경우 모두 `insmod g3_clkgen` 한 번에 소비자가 bind됐다. 우리는 `drivers_probe`를 건드리지 않았다(D3 예제 14와 다른 점). 공급자의 bind가 trigger를 당겼다.
- **rmmod 공급자는 실패**(`EAGAIN`): `clk_get()`이 공급자 모듈의 참조 카운트를 올린다(`lsmod`의 `1`). 클럭을 쓰는 동안 공급자 코드가 메모리에서 사라지지 않게 하는 장치다.
- **sysfs로 공급자를 unbind하면 차이가 난다.** 기본값: link가 `active`이므로 driver core가 **소비자를 먼저 unbind**했다(`v2 remove` 줄, link는 다시 `dormant`). permissive: 소비자는 bind된 채 남고 공급자 클럭이 사라졌다 — `clk_unregister: unregistering prepared clock` 경고. 소비자는 이제 사라진 클럭의 핸들을 들고 있다. fw_devlink가 "순서"뿐 아니라 "**떠나는 순서**"도 지킨다는 증거다.
- permissive에서는 link가 sync_state 전용이라 소비자가 bind되자 지워졌다(`status` 파일이 사라짐 — `device_links_driver_bound()`가 SYNC_STATE_ONLY 링크를 정리한다).

### 9.5 deferred probe 함정

| 함정 | 결과 | 대책 |
|---|---|---|
| defer 될 수 있는 자원을 probe **뒤쪽**에서 얻는다 | 매 재시도마다 전원 켜고·리셋 풀고·칩 ID 읽고 되돌림. 부팅 느려짐 | defer 가능한 것(클럭·레귤레이터·GPIO·IRQ·PHY)은 **맨 앞에서 전부** 얻고, 하드웨어는 그 뒤에 만진다 |
| 자식 장치를 등록한 뒤 `-EPROBE_DEFER` 반환 | 자식 bind → trigger → 부모 재시도 → 무한 루프. 커널 문서(driver-model/driver.rst)가 경고로 명시 | 자식 등록은 probe 마지막에 |
| `-EPROBE_DEFER`를 다른 에러로 바꿔 반환 (`if (IS_ERR(clk)) return -EINVAL;`) | 재시도가 없어 영원히 안 붙음 | `PTR_ERR`를 그대로 전파 |
| 옵션 자원에 일반 `get` 사용 | 속성이 없으면 `-ENOENT`로 probe 실패 | `devm_clk_get_optional`, `devm_gpiod_get_optional`(없으면 NULL) |
| 공급자 드라이버가 끝내 안 옴 (모듈이 rootfs에 없음) | 10초 뒤 `deferred probe pending: ...` | `devices_deferred`로 원인 확인, 모듈 포함 여부 확인 |

---

## 10. bind/unbind 100회 — 누수 잡기

### 10.1 왜 100회인가

probe 실패 경로는 §6의 실패 주입으로 검증했다. 정상 경로(probe 성공 → remove)의 누수는 한 번으로는 안 보인다 — 4 KB 하나는 잡음에 묻힌다. 그래서 **같은 사이클을 많이 돌리고 기울기를 본다.** 이것이 드라이버 리뷰 전에 하는 가장 싼 누수 테스트다. (더 강력한 도구는 `CONFIG_DEBUG_KMEMLEAK` — 랩 커널에는 꺼져 있어 쓰지 않았다.)

### 10.2 예제 14 — kmalloc-4k 객체 수와 Slab

무엇을 확인하나: sysfs `unbind`/`bind`를 100번 하며 10회마다 `/proc/slabinfo`의 `kmalloc-4k` 활성 객체 수와 `/proc/meminfo`의 `Slab`(kB)를 기록한다. v1의 `leak=1`은 remove에서 4 KB 샘플 버퍼를 일부러 안 푼다.

```sh
# t5.sh — bind/unbind 100회: kmalloc-4k 활성 객체 수와 Slab(kB)를 10회마다 기록
insmod /g3_clkgen.ko
DRV=/sys/bus/platform/drivers/g3_sensorkit; DEV=a003e00.sensorkit
k4() { awk '$1=="kmalloc-4k"{print $2}' /proc/slabinfo; }
sl() { awk '/^Slab:/{print $2}' /proc/meminfo; }
run() {  # $1=모듈 $2=인자
  insmod /$1.ko quiet=1 $2
  line="$1 $2:"
  for i in $(seq 0 100); do
    [ $((i % 10)) = 0 ] && line="$line $i:$(k4)/$(sl)"
    [ $i = 100 ] && break
    echo $DEV > $DRV/unbind; echo $DEV > $DRV/bind
  done
  echo "$line"; rmmod $1
}
t0=$(cut -d' ' -f1 /proc/uptime)
run g3_v1 leak=0
run g3_v1 leak=1
run g3_v2 ""
t1=$(cut -d' ' -f1 /proc/uptime); echo "elapsed: $t0 -> $t1 s"
```

```text
lab initramfs: 6.18.55 up 1.18s
g3_v1 leak=0: 0:32/6732 10:32/6760 20:32/6760 30:32/6788 40:32/6788 50:32/6788 60:32/6788 70:32/6788 80:32/6796 90:32/6796 100:32/6796
g3_v1 leak=1: 0:32/6796 10:40/6836 20:48/6872 30:64/6936 40:72/6972 50:80/7004 60:88/7032 70:104/7096 80:112/7128 90:120/7160 100:128/7192
g3_v2 : 0:128/7192 10:128/7192 20:128/7192 30:128/7192 40:128/7192 50:128/7192 60:128/7192 70:128/7192 80:128/7192 90:128/7192 100:128/7192
elapsed: 1.32 -> 7.52 s
```

```svg
<svg viewBox="0 0 640 300" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="250" x2="490" y2="250" stroke="currentColor"/>
<line x1="70" y1="30" x2="70" y2="250" stroke="currentColor"/>
<line x1="66" y1="250.0" x2="490" y2="250.0" stroke="#888" stroke-opacity="0.3"/>
<text x="62" y="254.0" text-anchor="end" font-size="12">0</text>
<line x1="66" y1="195.0" x2="490" y2="195.0" stroke="#888" stroke-opacity="0.3"/>
<text x="62" y="199.0" text-anchor="end" font-size="12">25</text>
<line x1="66" y1="140.0" x2="490" y2="140.0" stroke="#888" stroke-opacity="0.3"/>
<text x="62" y="144.0" text-anchor="end" font-size="12">50</text>
<line x1="66" y1="85.0" x2="490" y2="85.0" stroke="#888" stroke-opacity="0.3"/>
<text x="62" y="89.0" text-anchor="end" font-size="12">75</text>
<line x1="66" y1="30.0" x2="490" y2="30.0" stroke="#888" stroke-opacity="0.3"/>
<text x="62" y="34.0" text-anchor="end" font-size="12">100</text>
<text x="70.0" y="268" text-anchor="middle" font-size="12">0</text>
<text x="154.0" y="268" text-anchor="middle" font-size="12">20</text>
<text x="238.0" y="268" text-anchor="middle" font-size="12">40</text>
<text x="322.0" y="268" text-anchor="middle" font-size="12">60</text>
<text x="406.0" y="268" text-anchor="middle" font-size="12">80</text>
<text x="490.0" y="268" text-anchor="middle" font-size="12">100</text>
<text x="280" y="290" text-anchor="middle" font-size="12">unbind→bind 반복 횟수</text>
<text x="16" y="140" text-anchor="middle" font-size="12" transform="rotate(-90 16 140)">kmalloc-4k 증가분 (객체)</text>
<polyline points="70.0,250.0 112.0,232.4 154.0,214.8 196.0,179.6 238.0,162.0 280.0,144.4 322.0,126.8 364.0,91.6 406.0,74.0 448.0,56.4 490.0,38.8" fill="none" stroke="#d0564a" stroke-width="2.5"/>
<circle cx="70.0" cy="250.0" r="3" fill="#d0564a"/>
<circle cx="112.0" cy="232.4" r="3" fill="#d0564a"/>
<circle cx="154.0" cy="214.8" r="3" fill="#d0564a"/>
<circle cx="196.0" cy="179.6" r="3" fill="#d0564a"/>
<circle cx="238.0" cy="162.0" r="3" fill="#d0564a"/>
<circle cx="280.0" cy="144.4" r="3" fill="#d0564a"/>
<circle cx="322.0" cy="126.8" r="3" fill="#d0564a"/>
<circle cx="364.0" cy="91.6" r="3" fill="#d0564a"/>
<circle cx="406.0" cy="74.0" r="3" fill="#d0564a"/>
<circle cx="448.0" cy="56.4" r="3" fill="#d0564a"/>
<circle cx="490.0" cy="38.8" r="3" fill="#d0564a"/>
<line x1="502" y1="60" x2="522" y2="60" stroke="#d0564a" stroke-width="2.5"/>
<text x="528" y="64" font-size="12">v1 leak=1</text>
<polyline points="70.0,250.0 112.0,250.0 154.0,250.0 196.0,250.0 238.0,250.0 280.0,250.0 322.0,250.0 364.0,250.0 406.0,250.0 448.0,250.0 490.0,250.0" fill="none" stroke="#4a7bd0" stroke-width="2.5"/>
<circle cx="70.0" cy="250.0" r="3" fill="#4a7bd0"/>
<circle cx="112.0" cy="250.0" r="3" fill="#4a7bd0"/>
<circle cx="154.0" cy="250.0" r="3" fill="#4a7bd0"/>
<circle cx="196.0" cy="250.0" r="3" fill="#4a7bd0"/>
<circle cx="238.0" cy="250.0" r="3" fill="#4a7bd0"/>
<circle cx="280.0" cy="250.0" r="3" fill="#4a7bd0"/>
<circle cx="322.0" cy="250.0" r="3" fill="#4a7bd0"/>
<circle cx="364.0" cy="250.0" r="3" fill="#4a7bd0"/>
<circle cx="406.0" cy="250.0" r="3" fill="#4a7bd0"/>
<circle cx="448.0" cy="250.0" r="3" fill="#4a7bd0"/>
<circle cx="490.0" cy="250.0" r="3" fill="#4a7bd0"/>
<line x1="502" y1="85" x2="522" y2="85" stroke="#4a7bd0" stroke-width="2.5"/>
<text x="528" y="89" font-size="12">v1 leak=0</text>
<polyline points="70.0,250.0 112.0,250.0 154.0,250.0 196.0,250.0 238.0,250.0 280.0,250.0 322.0,250.0 364.0,250.0 406.0,250.0 448.0,250.0 490.0,250.0" fill="none" stroke="#3f9a6b" stroke-width="2.5" stroke-dasharray="6,4"/>
<circle cx="70.0" cy="250.0" r="3" fill="#3f9a6b"/>
<circle cx="112.0" cy="250.0" r="3" fill="#3f9a6b"/>
<circle cx="154.0" cy="250.0" r="3" fill="#3f9a6b"/>
<circle cx="196.0" cy="250.0" r="3" fill="#3f9a6b"/>
<circle cx="238.0" cy="250.0" r="3" fill="#3f9a6b"/>
<circle cx="280.0" cy="250.0" r="3" fill="#3f9a6b"/>
<circle cx="322.0" cy="250.0" r="3" fill="#3f9a6b"/>
<circle cx="364.0" cy="250.0" r="3" fill="#3f9a6b"/>
<circle cx="406.0" cy="250.0" r="3" fill="#3f9a6b"/>
<circle cx="448.0" cy="250.0" r="3" fill="#3f9a6b"/>
<circle cx="490.0" cy="250.0" r="3" fill="#3f9a6b"/>
<line x1="502" y1="110" x2="522" y2="110" stroke="#3f9a6b" stroke-width="2.5" stroke-dasharray="6,4"/>
<text x="528" y="114" font-size="12">v2 (devm)</text>
<text x="484.0" y="28.8" text-anchor="end" font-size="12">+96 객체, Slab +396 kB</text>
</svg>
```
그림 9 — 예제 14의 kmalloc-4k 활성 객체 증가분(각 실행의 0회 시점 기준). leak=1만 기울기가 있다.

출력에서 볼 것:

- 형식은 `반복:kmalloc-4k객체/Slab kB`. **v1 leak=0과 v2는 100회 내내 평평**하다(v1 leak=0의 Slab은 처음 80회 동안 계단식으로 +64 kB 늘고 멈췄다. 같은 일을 하는 v2 실행에서는 완전히 평평했고 kmalloc-4k도 내내 평평하므로, 첫 실행의 캐시 워밍업으로 본다).
- **leak=1은 100회에 +96 객체, Slab +396 kB** ≈ 100 × 4 KB. `/proc/slabinfo`의 활성 객체 수는 SLUB이 슬랩(여기선 객체 8개) 단위로 세므로 8씩 계단으로 오른다.
- v2 행의 시작값이 128인 것은 바로 앞 leak=1 실행이 남긴 96개가 **영원히 회수되지 않았기** 때문이다. 모듈을 빼도 새어 나간 메모리는 돌아오지 않는다.
- 300번의 unbind/bind가 6.2초 — TCG 에뮬레이션에서도 한 사이클 약 20 ms(probe 안의 `usleep_range(1000, 2000)` 포함).

---

## 11. 비동기 probe — `PROBE_PREFER_ASYNCHRONOUS`

### 11.1 직관

probe는 기본적으로 **등록한 스레드에서 동기로** 돈다. 부팅 중이라면 init 스레드가, 모듈이라면 `insmod`가 probe가 끝날 때까지 멈춘다. 칩 하나가 전원 안정화에 500 ms, 펌웨어 로딩에 2초를 쓴다면 그동안 뒤의 모든 드라이버가 기다린다. 비동기 probe를 허용하면 driver core가 probe를 async 작업(`async_schedule_dev`)으로 넘기고 바로 돌아온다.

```c
static struct platform_driver sk_driver = {
	.probe = sk_probe,
	.driver = {
		.name = "g3_sensorkit",
		.of_match_table = sk_of_match,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,   /* 느린 장치 */
	},
};
```

`PROBE_FORCE_SYNCHRONOUS`는 반대로 "나는 반드시 동기"다. 소스 주석(`include/linux/device/driver.h`)에 따르면 `PROBE_PREFER_ASYNCHRONOUS`는 "느린 장치용 임시 조치"이고, 장기적으로는 비동기를 기본으로 만들려는 방향이다. 소스를 고치지 않고도 켤 수 있다: 모듈이면 `insmod x.ko async_probe=1`(모듈 공통 파라미터), 빌트인이면 커널 인자 `driver_async_probe=<드라이버 이름>`.

### 11.2 예제 15 — insmod는 언제 돌아오나

무엇을 확인하나: `probe_delay_ms=1000`(리셋 해제 후 1초 대기)으로 느린 칩을 흉내 내고, 동기/비동기에서 `insmod`가 돌아온 시각과 `/dev` 노드 존재 여부를 본다.

```sh
# t6.sh — 동기 vs 비동기 probe: probe가 1초 걸리는 칩에서 insmod가 언제 돌아오나
insmod /g3_clkgen.ko
up() { cut -d' ' -f1 /proc/uptime; }
for mode in sync async; do
  dmesg -c > /dev/null
  [ $mode = async ] && A=async_probe=1 || A=
  t0=$(up); insmod /g3_v2.ko probe_delay_ms=1000 $A; t1=$(up)
  echo "$mode: insmod start=$t0 return=$t1  dev-node-at-return=$(ls /dev/g3_sensorkit 2>/dev/null || echo none)"
  sleep 2; dmesg | grep "v2 probed" | sed "s/: v2 probed.*/: v2 probed/"; echo
  rmmod g3_v2
done
```

```text
lab initramfs: 6.18.55 up 1.33s
sync: insmod start=1.44 return=2.51  dev-node-at-return=/dev/g3_sensorkit
[    2.574931] g3_sensorkit a003e00.sensorkit: v2 probed

async: insmod start=4.63 return=4.66  dev-node-at-return=none
[    5.741841] g3_sensorkit a003e00.sensorkit: v2 probed

[    6.858304] reboot: Power down
```

출력에서 볼 것:

- **동기**: insmod가 1.07초 걸렸고, 돌아왔을 때 `/dev/g3_sensorkit`이 이미 있다. (`/proc/uptime`은 10 ms 단위의 다른 시계라 printk 타임스탬프와 수십 ms 어긋난다.)
- **비동기**: insmod는 0.03초 만에 돌아왔고 그 순간 장치 노드가 **없다**. probe는 약 1.1초 뒤 다른 스레드에서 끝났다.
- 그래서 비동기 probe의 대가: "모듈을 로드했으면 장치가 있다"는 가정이 깨진다. 부팅 스크립트가 `/dev/xxx`를 바로 열면 실패한다. 실무에서는 udev 이벤트를 기다리거나(`udevadm settle`, systemd의 device unit) 장치가 나타날 때까지 poll한다. 또 probe 안에서 전역 상태를 건드리는 드라이버는 다른 probe와 **동시에** 돌 수 있다는 것도 고려해야 한다.

---

## 12. 전원 관리 콜백과 shutdown

### 12.1 정의

- `driver.pm`에 `struct dev_pm_ops`를 건다. 시스템 sleep만 다루면 `DEFINE_SIMPLE_DEV_PM_OPS(name, suspend, resume)` 한 줄이면 된다. `pm_sleep_ptr(&ops)`는 `CONFIG_PM_SLEEP`이 꺼진 커널에서 NULL이 되어 함수들이 통째로 빠지게 한다(예전의 `#ifdef CONFIG_PM_SLEEP`과 `__maybe_unused`를 대체).
- suspend에서는 **probe의 뒷부분을 거꾸로**: IRQ 끄기 → 하드웨어 정지 → 클럭 끄기 → (필요하면) 전원 끄기. resume은 그 반대 + 레지스터 복원.
- runtime PM(`pm_runtime_*`, 장치가 놀 때 개별적으로 끄기)은 모듈 N에서 다룬다.
- `.shutdown`은 재부팅·전원 끄기 직전에 불린다. **remove가 아니다** — 자원을 풀 필요는 없고, 하드웨어를 조용히 만드는 것(DMA 정지, 인터럽트 끄기, 칩을 리셋에 두기)이 목적이다. kexec로 새 커널이 뜰 때 이전 커널의 DMA가 계속 메모리에 쓰는 사고를 막는다.

### 12.2 예제 16 — s2idle 진입·복귀와 poweroff

무엇을 확인하나: v2의 PM 콜백·shutdown(코드는 아래) — `rtcwake -m freeze -s 2`로 s2idle에 들어갔다가 PL031 RTC 알람으로 깬다. 그리고 `poweroff` 때 `.shutdown`이 불리는지 본다.

```c

static void sk_shutdown(struct platform_device *pdev)
{
	struct sk_priv *p = platform_get_drvdata(pdev);

	gpiod_set_value_cansleep(p->reset, 1);	/* 전원 끄기 전 칩을 리셋에 둔다 */
	dev_info(&pdev->dev, "shutdown: reset asserted\n");
}

static int sk_suspend(struct device *dev)
{
	struct sk_priv *p = dev_get_drvdata(dev);

	disable_irq(p->irq);
	clk_disable_unprepare(p->clk_ref);
	dev_info(dev, "suspend: irq off, ref clock off\n");
	return 0;
}

static int sk_resume(struct device *dev)
{
	struct sk_priv *p = dev_get_drvdata(dev);
	int ret = clk_prepare_enable(p->clk_ref);

	if (ret)
		return ret;
	enable_irq(p->irq);
	dev_info(dev, "resume: id=0x%08x\n", readl(p->base + SK_REG_MAGIC));
	return 0;
}
static DEFINE_SIMPLE_DEV_PM_OPS(sk_pm, sk_suspend, sk_resume);

static struct platform_driver sk_driver = {
	.probe = sk_probe,
	.remove = sk_remove,
	.shutdown = sk_shutdown,
	.driver = {
		.name = "g3_sensorkit",
		.of_match_table = sk_of_match,
		.pm = pm_sleep_ptr(&sk_pm),
	},
};
module_platform_driver(sk_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("G3 sensorkit v2: devm");
```

```sh
# t8.sh — 전원 관리 콜백: s2idle(freeze) 진입·복귀, 그리고 poweroff 때 .shutdown
insmod /g3_clkgen.ko; insmod /g3_v2.ko quiet=1
echo "== mem_sleep: $(cat /sys/power/mem_sleep)  state: $(cat /sys/power/state)"
dmesg -c > /dev/null
rtcwake -m freeze -s 2
echo "== rtcwake rc=$?"; dmesg | grep -E "sensorkit|PM:|Freezing|Restarting" | sed 's/^\[[ 0-9.]*\] //'
echo "== clk ref enable count now: $(awk '$1=="g3_ref19m2"{print $2}' /sys/kernel/debug/clk/clk_summary)"
echo "== poweroff 시작"
```

```text
lab initramfs: 6.18.55 up 1.77s
== mem_sleep: [s2idle]  state: freeze mem disk
wakeup from "freeze" at Sun Oct 11 01:40:52 2026
== rtcwake rc=0
PM: suspend entry (s2idle)
Freezing user space processes
Freezing user space processes completed (elapsed 0.002 seconds)
Freezing remaining freezable tasks
Freezing remaining freezable tasks completed (elapsed 0.001 seconds)
g3_sensorkit a003e00.sensorkit: suspend: irq off, ref clock off
g3_sensorkit a003e00.sensorkit: resume: id=0x74726976
Restarting tasks: Starting
Restarting tasks: Done
PM: suspend exit
== clk ref enable count now: 1
== poweroff 시작
[    9.679123] reboot: Power down
```

같은 스크립트를 `loglevel=7`로 부팅해서 마지막 부분만 걸러 보면:

```sh
sh /Users/donh/.elinux_scratch/G3/boot.sh t8.sh "loglevel=7" 2>&1 | grep -E "shutdown|Power down|poweroff"
```

```text
== poweroff 시작
[    6.473827] g3_sensorkit a003e00.sensorkit: shutdown: reset asserted
[    6.484719] reboot: Power down
```

출력에서 볼 것: 유저 공간을 얼린 **뒤** 우리 suspend가 불렸고, resume은 tasks를 다시 깨우기 **전**에 불렸다. resume 후 ref 클럭 enable count가 1로 돌아왔다(suspend에서 끄고 resume에서 켰다). `poweroff`(initramfs의 `poweroff -f`도 `reboot()` 시스템 콜을 거치므로 `device_shutdown()`이 돈다) 때 `.shutdown`이 불렸다 — remove는 불리지 않았다는 점에 주목.

---

## 13. 커널 안에서는 무슨 일이 일어나나

### 13.1 예제 17(c) — probe와 unbind의 호출 경로를 소스에서

```sh
cd /var/lab/src/linux-6.18.55
sed -n 607,631p drivers/base/dd.c
sed -n 1376,1386p drivers/base/platform.c
```

```text
static void device_unbind_cleanup(struct device *dev)
{
	devres_release_all(dev);
	arch_teardown_dma_ops(dev);
	kfree(dev->dma_range_map);
	dev->dma_range_map = NULL;
	device_set_driver(dev, NULL);
	dev_set_drvdata(dev, NULL);
	dev_pm_domain_detach(dev, dev->power.detach_power_off);
	if (dev->pm_domain && dev->pm_domain->dismiss)
		dev->pm_domain->dismiss(dev);
	pm_runtime_reinit(dev);
	dev_pm_set_driver_flags(dev, 0);
}

static void device_remove(struct device *dev)
{
	device_remove_file(dev, &dev_attr_state_synced);
	device_remove_groups(dev, dev->driver->dev_groups);

	if (dev->bus && dev->bus->remove)
		dev->bus->remove(dev);
	else if (dev->driver->remove)
		dev->driver->remove(dev);
}
	if (drv->probe)
		ret = drv->probe(dev);

out:
	if (drv->prevent_deferred_probe && ret == -EPROBE_DEFER) {
		dev_warn(_dev, "probe deferral not supported\n");
		ret = -ENXIO;
	}

	return ret;
}
```

출력에서 볼 것: `device_unbind_cleanup()`의 **첫 줄이 `devres_release_all()`**, 그다음에 `dev_set_drvdata(dev, NULL)`이다. `__device_release_driver()`는 `device_remove()`(→ `platform_remove()` → 우리 `sk_remove`)를 부른 **뒤** 이 함수를 부른다. probe 실패 경로(`really_probe()`의 `probe_failed:` 라벨들)도 마지막에 같은 함수로 떨어진다 — 그래서 "probe 실패 = remove 없는 unbind"다. `platform_probe()`는 우리 probe를 부르기 전에 `of_clk_set_defaults()`(DT의 `assigned-clocks` 적용)와 PM domain attach도 해 준다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah9" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="20" y="22" font-size="13" text-anchor="start">등록 → probe</text>
<rect x="20" y="34" width="200" height="30" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="120.0" y="53.2" text-anchor="middle" font-size="12">platform_driver_register</text>
<rect x="250" y="34" width="200" height="30" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="350.0" y="53.2" text-anchor="middle" font-size="12">driver_register</text>
<rect x="460" y="34" width="200" height="30" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="560.0" y="53.2" text-anchor="middle" font-size="12">bus_add_driver</text>
<rect x="460" y="84" width="200" height="30" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="560.0" y="103.2" text-anchor="middle" font-size="12">driver_attach</text>
<rect x="250" y="84" width="200" height="30" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="350.0" y="103.2" text-anchor="middle" font-size="12">__driver_attach</text>
<rect x="20" y="84" width="200" height="30" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="120.0" y="103.2" text-anchor="middle" font-size="12">driver_probe_device</text>
<rect x="20" y="134" width="200" height="30" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="120.0" y="153.2" text-anchor="middle" font-size="12">really_probe</text>
<rect x="250" y="134" width="200" height="30" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="350.0" y="153.2" text-anchor="middle" font-size="12">platform_probe</text>
<rect x="460" y="134" width="200" height="30" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="560.0" y="153.2" text-anchor="middle" font-size="12">sk_probe</text>
<line x1="220" y1="49" x2="247" y2="49" stroke="currentColor" marker-end="url(#ah9)"/>
<line x1="450" y1="49" x2="457" y2="49" stroke="currentColor" marker-end="url(#ah9)"/>
<line x1="560" y1="64" x2="560" y2="81" stroke="currentColor" marker-end="url(#ah9)"/>
<line x1="460" y1="99" x2="453" y2="99" stroke="currentColor" marker-end="url(#ah9)"/>
<line x1="250" y1="99" x2="223" y2="99" stroke="currentColor" marker-end="url(#ah9)"/>
<line x1="120" y1="114" x2="120" y2="131" stroke="currentColor" marker-end="url(#ah9)"/>
<line x1="220" y1="149" x2="247" y2="149" stroke="currentColor" marker-end="url(#ah9)"/>
<line x1="450" y1="149" x2="457" y2="149" stroke="currentColor" marker-end="url(#ah9)"/>
<text x="20" y="195" font-size="13" text-anchor="start">unbind → 정리</text>
<rect x="20" y="207" width="200" height="26" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="120.0" y="224.2" text-anchor="middle" font-size="12">unbind_store</text>
<rect x="250" y="207" width="200" height="26" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="350.0" y="224.2" text-anchor="middle" font-size="12">device_driver_detach</text>
<rect x="460" y="207" width="200" height="26" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="560.0" y="224.2" text-anchor="middle" font-size="12">__device_release_driver</text>
<rect x="460" y="257" width="200" height="26" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="560.0" y="274.2" text-anchor="middle" font-size="12">device_remove</text>
<rect x="250" y="257" width="200" height="26" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="350.0" y="274.2" text-anchor="middle" font-size="12">platform_remove</text>
<rect x="20" y="257" width="200" height="26" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="120.0" y="274.2" text-anchor="middle" font-size="12">sk_remove</text>
<rect x="20" y="300" width="200" height="26" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="120.0" y="317.2" text-anchor="middle" font-size="12">device_unbind_cleanup</text>
<rect x="250" y="300" width="200" height="26" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="350.0" y="317.2" text-anchor="middle" font-size="12">devres_release_all</text>
<line x1="220" y1="220" x2="247" y2="220" stroke="currentColor" marker-end="url(#ah9)"/>
<line x1="450" y1="220" x2="457" y2="220" stroke="currentColor" marker-end="url(#ah9)"/>
<line x1="560" y1="233" x2="560" y2="254" stroke="currentColor" marker-end="url(#ah9)"/>
<line x1="460" y1="270" x2="453" y2="270" stroke="currentColor" marker-end="url(#ah9)"/>
<line x1="250" y1="270" x2="223" y2="270" stroke="currentColor" marker-end="url(#ah9)"/>
<line x1="120" y1="283" x2="120" y2="297" stroke="currentColor" marker-end="url(#ah9)"/>
<line x1="220" y1="313" x2="247" y2="313" stroke="currentColor" marker-end="url(#ah9)"/>
<text x="460" y="318" font-size="12" text-anchor="start">remove 다음에 실행</text>
</svg>
```
그림 10 — 등록부터 probe까지(위)와 unbind부터 정리까지(아래)의 실제 함수 이름(6.18 `drivers/base/`). `driver_attach()`는 버스의 모든 장치에 대해 `__driver_attach()`를 부르고, 드라이버가 비동기를 허용하면 여기서 async 작업으로 넘긴다. 아래 줄의 `sk_remove` → `device_unbind_cleanup` 화살표는 호출이 아니라 "그다음"이다 — 둘 다 `__device_release_driver()`가 차례로 부른다.

### 13.2 드라이버를 쓸 때 다시 만나는 지점

| 이 노트의 개념 | 다시 나오는 곳 |
|---|---|
| bus · device · driver 매칭, kobject, sysfs `driver` 링크 | G2 |
| `misc_register`, `file->private_data`, 열린 파일과 unbind의 수명 문제 | G4, G7 |
| `DEVICE_ATTR`, `dev_groups`(driver core가 probe 후 자동 생성) | G8 |
| `MODULE_DEVICE_TABLE`, modalias, 자동 로드 | G9 |
| `request_firmware_nowait` — 느린 probe를 비동기로 | G10 |
| I2C/SPI 드라이버: 같은 뼈대, `i2c_driver`/`spi_driver`, `devm_regmap_init_i2c` | H 모듈 |
| IIO·input·hwmon 등록 (`devm_iio_device_register`) | I 모듈 |
| runtime PM, `pm_runtime_*`, PM domain | N 모듈 |

---

## 14. Embedded 실무에서 다시 보기

### 14.1 "IMU가 부팅 후 안 보여요" — 5분 진단 순서

```sh
ls /sys/bus/platform/devices/ | grep imu          # 1. 장치가 만들어졌나 (DT, D3)
cat /sys/kernel/debug/devices_deferred            # 2. 공급자를 기다리나 (§9)
ls /sys/class/devlink | grep imu                  # 3. 누구를 기다리나
dmesg | grep -iE "imu|probe with driver"          # 4. probe가 실패했나 (§6.2의 세 가지 메시지)
cat /sys/bus/platform/devices/<dev>/modalias      # 5. 모듈이 자동 로드될 이름인가 (G9)
```

(명령 목록 — 이 노트의 예제 8·9·13에서 각각 실제로 써 봤다.) 2번에 무언가 있으면 거의 항상 "공급자 드라이버가 빌드에서 빠졌거나 모듈이 rootfs에 없음"이다. 4번에 아무것도 없고 장치도 unbound라면 `-ENODEV`(칩 ID 불일치)를 의심한다 — driver core는 그걸 dbg로만 찍는다.

### 14.2 보드 리비전마다 다른 전원 시퀀스

리비전 B부터 IMU에 `vddio`가 추가되고 "vdd → 1 ms → vddio → 리셋 해제" 순서가 필요해졌다면? probe에서 `devm_regulator_get_optional(dev, "vddio")`로 받아 있으면 켜고, devm 순서로 전원 꺼짐 순서까지 같이 맞춘다(§5.2의 "devm 순서 = 전원 시퀀스"). 시퀀스 대기 시간은 데이터시트 값을 `usleep_range`/`fsleep`으로. DT에는 배선만 적고 **시퀀스는 드라이버 코드에** 둔다.

### 14.3 펌웨어를 받아야 하는 칩

ISP·DSP·Wi-Fi 칩처럼 probe에서 수 MB 펌웨어를 올려야 하면 probe가 수 초 걸린다. 선택지: `PROBE_PREFER_ASYNCHRONOUS`, 또는 probe는 빨리 끝내고 `request_firmware_nowait()` 콜백에서 나머지 초기화(G10). 후자라면 콜백과 remove의 경합(§8과 같은 종류)을 반드시 처리한다.

### 14.4 양산 품질 체크 — 드라이버 PR 전에

- [ ] `fail_at` 같은 실패 주입 또는 `failslab`으로 모든 실패 경로를 한 번씩 밟았다
- [ ] unbind/bind 100회 후 slab 기울기 0
- [ ] `CONFIG_DEBUG_TEST_DRIVER_REMOVE=y` 커널에서 부팅해 봤다
- [ ] `-EPROBE_DEFER` 경로가 하드웨어를 건드리기 전에 끝난다
- [ ] remove 이후 IRQ·work·timer가 해제된 것을 만지지 않는다

---

## 15. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 사다리 라벨 하나를 잘못 점프 | 특정 실패 후 클럭·전원이 켜진 채, 다음 probe `-EBUSY` | 수작업 역순 관리 | devm으로 전환, 실패 주입 테스트 (§6.3) |
| devm 자원을 remove에서 또 해제 | double free, `Trying to free already-free IRQ` | devm과 수동 해제 혼용 | devm 자원은 손대지 않는다 |
| remove에서 kfree, IRQ는 devm | 가끔 이상한 값, poison `0x6b6b6b6b`, 드문 oops | remove가 devres보다 먼저 (§8) | 전부 devm + 획득 순서, 또는 IRQ 먼저 끄기 |
| drvdata를 `request_irq` 뒤에 세팅 | 부팅 중 드물게 NULL 역참조 | 핸들러가 먼저 불림 | 노출 단계 전에 `platform_set_drvdata` |
| `-EPROBE_DEFER`를 `-EINVAL`로 바꿈 | 공급자가 와도 영원히 안 붙음 | 에러 코드 덮어쓰기 | `PTR_ERR` 그대로 `dev_err_probe` |
| 모든 실패에 `dev_err` | 부팅 로그에 defer 에러 수십 줄 | defer는 정상 상황 | `dev_err_probe` |
| `MODULE_DEVICE_TABLE` 누락 | 빌트인은 되는데 모듈로는 자동 로드 안 됨 | modalias 별칭 없음 | 테이블 뒤에 한 줄 |
| `open()`/`ioctl()`에서 devm 할당 | 장치를 열 때마다 메모리 증가 | devm은 unbind 때 해제 | 일반 `kmalloc` + `release()`에서 해제 |
| 칩 ID 불일치를 `-ENODEV`로만 반환 | 로그에 아무것도 없이 장치가 안 붙음 | driver core가 dbg로만 기록 | `dev_err_probe(dev, -ENODEV, "id 0x%x")` |
| 옛 커널 코드를 6.11+로 가져옴 | `.remove` 타입 불일치 컴파일 에러 | remove가 void로 바뀜 | `void` remove로, `return 0` 삭제 |

---

## 16. 면접에서 이렇게 말한다

**Q.** Walk me through what happens between `insmod` of a platform driver and its `probe()` being called.

**A.** 모듈 init이 `platform_driver_register()`로 드라이버를 platform 버스에 등록한다. driver core가 버스의 장치들을 돌며 `platform_match()`로 compatible을 비교하고, 맞으면 `really_probe()`에서 공급자 링크 확인(fw_devlink), pinctrl 기본 상태 적용, sysfs 링크 생성 후 `platform_probe()` → 우리 `probe()`를 부른다. 0이면 bound, `-EPROBE_DEFER`면 deferred 리스트, 다른 에러면 devres를 전부 풀고 실패로 기록한다. insmod는 probe 실패와 무관하게 성공한다.

> The module init just registers the driver with the platform bus. The driver core then walks the bus, matches each device by its compatible string, and for a match calls really_probe: it checks that all suppliers from fw_devlink are bound, binds default pinctrl state, and then calls my probe through platform_probe. If probe returns 0 the device is bound; -EPROBE_DEFER puts it on the deferred list; any other error releases all devres resources. Note that insmod succeeds even if probe fails, because registration and probing are separate.

**꼬리 질문**: "그럼 장치가 드라이버보다 나중에 생기면?" → 장치 등록 쪽(`device_add` → `bus_probe_device`)에서 같은 매칭이 일어난다(D3 §8).

**Q.** What is devm, and how does it work internally?

**A.** device-managed 자원이다. devm 함수는 자원을 얻으면서 "해제 함수 + 데이터"를 노드로 만들어 `struct device`의 devres 리스트 꼬리에 붙인다. probe가 실패하거나 unbind되면 driver core가 `devres_release_all()`로 리스트를 꼬리부터 돌며 해제한다. 그래서 획득의 역순 해제가 자동으로 보장되고 probe에 에러 사다리가 필요 없다. devm 버전이 없는 것은 `devm_add_action_or_reset()`으로 끼워 넣는다.

> devm APIs acquire a resource and at the same time append a devres node — a release callback plus its data — to a per-device list. When probe fails or the device is unbound, the driver core calls devres_release_all, which walks that list backwards. So you get LIFO cleanup for free and probe needs no goto ladder. For anything without a devm variant I use devm_add_action_or_reset, which also runs the action immediately if registration itself fails.

**Q.** When is devm dangerous?

**A.** 해제 시점이 remove() **이후**라는 것. remove에서 손으로 푼 데이터를 devm IRQ 핸들러가 그 틈에 읽으면 use-after-free다 — 실제로 QEMU에서 핸들러가 SLUB poison `0x6b6b6b6b`를 읽는 걸 재현해 봤다. 또 open/ioctl 같은 probe 밖에서 devm으로 할당하면 unbind까지 안 풀려 누수가 되고, devm 자원을 손으로 또 풀면 double free다. 규칙은 "전부 devm으로, 의존 순서대로 얻는다".

> The release happens after remove() returns. If remove frees a buffer manually while the IRQ was requested with devm, an interrupt arriving in that window touches freed memory — I reproduced exactly that in QEMU with slab poisoning, the handler read 0x6b6b6b6b. Other traps are devm allocations outside probe, which leak until unbind, and double-freeing a devm resource by hand. My rule is: make everything the handler touches devm, and acquire it before the IRQ.

**Q.** What is -EPROBE_DEFER and how do you debug a device stuck in deferred probe?

**A.** 공급자(클럭·레귤레이터·GPIO·IRQ 컨트롤러)가 아직 준비 안 됐으니 나중에 다시 불러 달라는 반환값이다. driver core는 장치를 pending 리스트에 두고, 다른 드라이버가 bind될 때마다 재시도한다. 요즘은 fw_devlink가 DT의 phandle로 링크를 미리 만들어 probe 자체를 막는다. 디버그는 `/sys/kernel/debug/devices_deferred`(이유 문자열), `/sys/class/devlink`(누구를 기다리나), 그리고 10초 타임아웃 후의 `deferred probe pending` 경고를 본다. 드라이버는 `dev_err_probe`로 이유를 남긴다.

> It means a supplier — a clock, regulator, GPIO or interrupt controller — isn't available yet, so try again later. The core parks the device on a pending list and retries it every time some other driver binds. With fw_devlink, links are created from the DT phandles up front, so the core doesn't even call probe until suppliers are bound. To debug, I read /sys/kernel/debug/devices_deferred, which shows the reason string recorded by dev_err_probe or by the device-link check, look at /sys/class/devlink, and check for "deferred probe pending" warnings after the timeout.

**꼬리 질문**: "probe에서 defer를 반환하면 안 되는 경우는?" → 자식 장치를 이미 등록한 뒤(무한 재시도 루프), 그리고 하드웨어를 이미 건드린 뒤(그래서 defer 가능한 자원은 맨 앞에서 얻는다).

**Q.** In what order do you acquire resources in probe, and why?

**A.** private 구조체 → 레지스터 매핑 → 전원 → 클럭 → 리셋 해제 → 칩 ID 확인 → 초기화 → drvdata → IRQ → 서브시스템 등록. defer 가능한 자원은 하드웨어를 만지기 전에 다 얻고, 전원 시퀀스는 데이터시트 순서를 따르며, 외부가 나를 부를 수 있는 단계(IRQ, 등록)는 맨 끝이다. devm에서는 이 순서가 그대로 역순 해제 순서이므로 power-down 시퀀스까지 결정한다.

> Allocate private data, map registers, then power: regulators, clocks, release reset per the datasheet sequence, verify the chip ID, initialize, set drvdata, and only then request the IRQ and register with the subsystem. Anything that can defer comes before touching hardware, and anything that exposes the device to callers comes last. With devm, this order also defines the teardown order, so it doubles as the power-down sequence.

**Q.** How do you make sure your driver doesn't leak on probe failure or unbind?

**A.** 두 가지 테스트를 한다. 첫째, 실패 주입: 단계 N을 강제로 실패시키는 파라미터(또는 `failslab`)로 모든 경로를 밟고, 매번 `/proc/iomem`, `/proc/interrupts`, `clk_summary`, `regulator_summary`, `debug/gpio`가 기준값으로 돌아오는지 확인한다. 둘째, sysfs unbind/bind를 100회 돌리며 slab 사용량의 기울기를 본다. 실제로 한 라벨을 틀리게 바꾸면 첫 테스트가 그 단계의 클럭 누수를 정확히 잡았다.

> Two tests. Fault injection — a parameter that fails step N, or failslab — and after each failure I check that /proc/iomem, /proc/interrupts, clk_summary, regulator_summary and the GPIO debugfs are back to baseline. And an unbind/bind loop, say a hundred times, watching slab usage; a 4 KB leak per cycle shows up as a clear slope. When I deliberately broke one goto label, the first test pinpointed the leaked clock at that exact step.

**Q.** What's the difference between remove(), shutdown() and suspend()?

**A.** remove는 드라이버와 장치가 **헤어질 때** — 자원을 모두 반납하고, 이후 다시 probe될 수 있다. shutdown은 **재부팅·전원 끄기 직전** — 자원은 안 풀어도 되고 DMA·인터럽트를 멈춰 하드웨어를 조용히 만드는 게 목적이다(kexec 안전성). suspend는 **다시 돌아올 것을 전제로** 상태를 저장하고 전원을 낮춘다. 6.11부터 platform remove는 void를 반환한다.

> remove is the unbind path: give everything back so the device can be probed again later. shutdown runs right before reboot or power-off; you don't free resources, you quiesce the hardware — stop DMA, mask interrupts — which matters for kexec. suspend saves state and lowers power expecting a resume. And since 6.11 the platform remove callback returns void.

### 16.1 화이트보드 드릴 — "platform driver 뼈대를 써 보세요"

**문제**: "가상의 IMU가 DT에 `compatible = "acme,imu"`, `reg`, `interrupts`, `clocks`(이름 `core`), `vdd-supply`, `reset-gpios`로 있다. 레지스터 0x00이 칩 ID(0x42)다. platform driver 뼈대를 써라. 인터럽트에서는 레지스터 0x10을 읽어 저장한다." — 10분.

먼저 말로 구조를 잡는다(30초): "private 구조체, devm으로 자원을 순서대로, 칩 ID 확인, drvdata, IRQ 마지막, 매칭 테이블과 `module_platform_driver`." 그다음 쓴다.

모범 답안(위의 `g3_v2.c`를 화이트보드 크기로 줄인 것):

```c
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/delay.h>

#define IMU_ID		0x00
#define IMU_DATA	0x10
#define IMU_ID_VAL	0x42

struct imu {
	struct device *dev;
	void __iomem *base;
	struct clk *clk;
	struct gpio_desc *reset;
	u32 last;
};

static irqreturn_t imu_irq(int irq, void *data)
{
	struct imu *imu = data;

	imu->last = readl(imu->base + IMU_DATA);
	return IRQ_HANDLED;
}

static int imu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct imu *imu;
	int irq, ret;
	u32 id;

	imu = devm_kzalloc(dev, sizeof(*imu), GFP_KERNEL);
	if (!imu)
		return -ENOMEM;
	imu->dev = dev;

	imu->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(imu->base))
		return PTR_ERR(imu->base);

	ret = devm_regulator_get_enable(dev, "vdd");
	if (ret)
		return dev_err_probe(dev, ret, "vdd\n");

	imu->clk = devm_clk_get_enabled(dev, "core");
	if (IS_ERR(imu->clk))
		return dev_err_probe(dev, PTR_ERR(imu->clk), "clock\n");

	imu->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(imu->reset))
		return dev_err_probe(dev, PTR_ERR(imu->reset), "reset\n");
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(imu->reset, 0);

	id = readl(imu->base + IMU_ID);
	if (id != IMU_ID_VAL)
		return dev_err_probe(dev, -ENODEV, "bad id 0x%x\n", id);

	platform_set_drvdata(pdev, imu);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;
	ret = devm_request_irq(dev, irq, imu_irq, 0, dev_name(dev), imu);
	if (ret)
		return dev_err_probe(dev, ret, "irq\n");

	return 0;
}

static const struct of_device_id imu_of_match[] = {
	{ .compatible = "acme,imu" },
	{ }
};
MODULE_DEVICE_TABLE(of, imu_of_match);

static struct platform_driver imu_driver = {
	.probe = imu_probe,
	.driver = {
		.name = "acme-imu",
		.of_match_table = imu_of_match,
	},
};
module_platform_driver(imu_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ACME IMU");
```

(이 답안은 `/var/tmp/G3/acme`에서 랩 커널에 대해 **빌드만** 해서 경고 0개를 확인했다. `acme,imu` 하드웨어가 없어 로드·실행은 하지 않았다 — 구조와 API는 실행 검증한 예제 6의 v2와 같다.) 채점 포인트 — 면접관이 보는 것:

| 포인트 | 왜 |
|---|---|
| `devm_kzalloc`으로 장치별 상태, 전역 변수 없음 | 같은 칩 두 개 |
| `IS_ERR`/`PTR_ERR` vs NULL 검사를 API마다 맞게 | `devm_kzalloc`은 NULL, 나머지는 ERR_PTR (E5 §3.5) |
| `dev_err_probe`로 defer 조용히 | 부팅 로그 |
| 전원 → 클럭 → 리셋 해제 → ID 확인 순서 | 하드웨어 이해 |
| `platform_set_drvdata`가 IRQ 요청 전 | race |
| IRQ가 마지막, `platform_get_irq` 음수 전파 | defer 포함 |
| 빈 항목으로 끝나는 match 테이블 + `MODULE_DEVICE_TABLE` | 자동 로드 |
| remove 없음 — 그리고 **왜 없어도 되는지** 말하기 | devres 역순 해제 |

**말로 덧붙일 것**(가산점): "remove가 필요해지는 경우는 하드웨어를 멈춰야 할 때(인터럽트 enable 비트 끄기)이고, 그때도 IRQ는 devm이 나중에 풀어 주므로 핸들러가 쓰는 데이터는 devm이어야 한다. 6.11 이후 remove는 void다. 느린 칩이면 `PROBE_PREFER_ASYNCHRONOUS`를 고려한다."

**자주 틀리는 것**: `.driver.owner = THIS_MODULE`을 쓰는 것(platform은 `platform_driver_register` 매크로가 채워 준다 — 써도 동작은 하지만 불필요), match 테이블의 끝 `{ }` 누락, `devm_clk_get` 후 enable을 잊음, `request_irq`를 `devm_` 없이 쓰고 remove를 안 씀.

---

## 17. 직접 해보기

1. (개념) v1의 사다리에서 `err_core_put:`과 `err_core_off:`의 순서를 바꾸면 어떤 실패에서 무엇이 잘못되나?
   정답/힌트: `clk_prepare_enable(core)` 실패 → `err_core_put`으로 가면 put한 뒤 켜지지도 않은 클럭을 disable한다(enable count 경고 + 반납한 핸들 사용). ref `clk_get` 실패 → `err_core_off`로 가면 disable만 하고 put을 건너뛴다(참조 누수). "얻은 순서의 정확한 역순"이 아니면 어딘가에서 짝이 어긋난다.
2. (코드) v2에 `vddio` 레귤레이터를 옵션으로 추가하라. DT에 없으면 건너뛰고, 있으면 vdd 다음에 켠다. `fail_at`을 하나 늘려 §6.2 테스트가 여전히 22줄 모두 기준값인지 확인하라.
   정답/힌트: `devm_regulator_get_optional(dev, "vddio")`는 속성이 없으면 `-ENODEV`를 돌려준다(NULL이 아니다). `-ENODEV`만 "없음"으로 처리하고 나머지는 전파.
3. (관찰) `CONFIG_DEBUG_DEVRES` 커널에서 v1을 로드하면 devres 로그에 무엇이 찍힐까? 예측하고 실행해 보라.
   정답/힌트: 실행해 보면 sensorkit 쪽은 `pinctrl_bind_pins`의 40바이트 ADD/REM 두 줄뿐이다(나머지 ADD 두 줄 `devm_clk_hw_register_fixed_rate_release`, `devm_of_clk_release_provider`는 같이 로드한 g3_clkgen의 것). 그래서 v1의 정리는 devres 도구로 검증할 수 없고 §6의 실패 주입이 유일한 증거다.
4. (코드) v2에 `.probe_type = PROBE_PREFER_ASYNCHRONOUS`를 넣고 `async_probe=1` 없이 예제 15를 다시 돌려 같은 결과가 나오는지 확인하라.
   정답/힌트: insmod가 수십 ms에 돌아오고 `/dev` 노드가 아직 없어야 한다.
5. (관찰) 예제 13에서 `deferred_probe_timeout=3`을 커널 인자로 주고, 공급자를 **로드하지 않은 채** 5초 기다리면 dmesg에 무엇이 찍히나?
   정답/힌트: 랩에서 실행한 결과 `platform a003e00.sensorkit: deferred probe pending: platform: supplier g3-clkgen not ready` 경고가 찍혔고, 장치는 여전히 `devices_deferred`에 남았다. 경고는 "포기"가 아니라 "아직 기다리는 중"이라는 알림이다 — 나중에 공급자 모듈을 넣으면 그때 붙는다.
6. (함정) §8의 버그를 "전부 devm"이 아닌 방법으로 고쳐라: `bug_order=1`을 유지하면서 remove 맨 앞에 한 줄을 추가해 예제 12가 poison을 읽지 않게 하라.
   정답/힌트: `devm_free_irq(&pdev->dev, p->irq, p);` 또는 `disable_irq(p->irq);`를 `kfree` 전에. 단, `fire_in_remove`로 넣은 pending 인터럽트가 이후 어떻게 되는지도 설명해 보라.
7. (관찰) sysfs로 unbind한 직후 `cat /sys/bus/platform/devices/a003e00.sensorkit/driver_override`, `ls .../driver`를 보고, 다시 bind하기 전까지 `/proc/iomem`에 무엇이 남는지 확인하라.
   정답/힌트: 아무것도 남지 않아야 한다(§6.2의 after rmmod 줄과 같다).

---

## 18. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| platform device | 플랫폼 장치 | 열거(enumeration) 불가능한 SoC 내장 장치. DT 노드에서 생긴다 |
| platform driver | 플랫폼 드라이버 | probe/remove 등 콜백을 담은 `struct platform_driver` |
| probe | 프로브 | 장치와 드라이버가 매칭될 때 불리는 초기화 함수. 여러 번 불릴 수 있다 |
| remove | 리무브 | unbind 때 불리는 정리 함수. 6.11부터 void |
| bind / unbind | 결합 / 분리 | 장치에 드라이버가 붙고 떨어지는 사건. sysfs로 손으로도 가능 |
| drvdata | 드라이버 데이터 | `dev->driver_data`. 콜백이 private 구조체를 찾는 열쇠 |
| devres | device resource | 장치별 "해제 콜백 리스트". unbind·probe 실패 시 역순 실행 |
| `devm_*` | device-managed API | 얻으면서 devres에 해제를 등록하는 함수군 |
| `devm_add_action_or_reset` | | 임의의 해제 함수를 devres에 등록. 등록 실패 시 즉시 실행 |
| `dev_err_probe` | | 에러 종류별로 로그·defer 이유 저장을 다르게 하고 에러를 반환 |
| `-EPROBE_DEFER` | 프로브 연기 | 공급자 미준비. 나중에 재시도 요청 |
| deferred probe list | 연기 목록 | pending/active 두 리스트. 다른 bind마다 재시도 |
| `devices_deferred` | | debugfs 파일. 연기된 장치와 이유 |
| fw_devlink | firmware device link | DT phandle로 공급자→소비자 링크를 미리 만드는 기능 |
| device link | 장치 링크 | 공급자·소비자 관계. probe 순서와 unbind 순서를 강제 |
| supplier / consumer | 공급자 / 소비자 | 클럭·전원 등을 주는 장치 / 받는 장치 |
| async probe | 비동기 프로브 | probe를 별도 스레드에서. `PROBE_PREFER_ASYNCHRONOUS` |
| `dev_pm_ops` | | 시스템 sleep·runtime PM 콜백 테이블 |
| shutdown | 셧다운 | 재부팅·전원 끄기 직전 하드웨어 정지 콜백 |
| slub poisoning | 슬랩 독칠 | 해제된 객체를 `0x6b`로 채워 use-after-free를 드러내는 디버그 기능 |

---

## 19. 요약 & 체크리스트

platform driver는 콜백 테이블이고, 그중 probe가 전부다. probe는 bring-up 순서(자원 → 매핑 → 전원 → 클럭 → 리셋 해제 → 칩 ID → 초기화 → drvdata → IRQ → 등록)를 그대로 코드로 옮긴 것이며, driver core가 여러 번·실패 도중에도 부른다. 그래서 정리가 본업이다. goto 사다리는 정리를 손으로 두 벌(사다리와 remove) 관리하고, devm은 해제 콜백을 devres 리스트에 쌓아 driver core가 역순으로 실행하게 한다 — 이 노트의 QEMU 랩에서 v1(131줄)과 v2(87줄)가 같은 실패 주입 테스트(10단계 각각을 실패시키기)와 100회 bind/unbind를 모두 깨끗하게 통과했고, 일부러 넣은 사다리 버그와 4 KB 누수를 테스트가 정확히 잡았다. devm의 대가는 해제가 remove() **뒤**라는 것이며, 그 틈에 들어온 인터럽트가 해제된 버퍼(`0x6b6b6b6b`)를 읽는 것을 재현했다. 공급자가 늦으면 `-EPROBE_DEFER`와 fw_devlink가 순서를 맞추고(`devices_deferred`로 진단), 공급자가 떠날 때도 소비자를 먼저 떼어 낸다.

- [ ] `struct platform_driver`의 필드(probe, remove, shutdown, driver.name/of_match_table/pm/probe_type)를 외워서 쓸 수 있다
- [ ] 6.11 이후 `.remove`가 void인 이유와 구 커널과의 차이를 말할 수 있다
- [ ] 정석 probe 순서 10단계를 말하고, drvdata와 IRQ의 순서 이유를 설명할 수 있다
- [ ] 같은 드라이버를 goto 사다리와 devm 두 방식으로 쓸 수 있다
- [ ] devres가 리스트 꼬리에 쌓고 꼬리부터 푼다는 것을 `devres_release_all`/`release_nodes`로 설명할 수 있다
- [ ] `devm_add_action_or_reset`의 "or_reset"이 무엇을 막는지 말할 수 있다
- [ ] remove()와 devres 해제 사이의 틈이 왜 생기고 어떻게 막는지 설명할 수 있다
- [ ] `dev_err_probe`가 `-EPROBE_DEFER`·`-ENOMEM`·그 밖에서 각각 어떻게 행동하는지 말할 수 있다
- [ ] `devices_deferred`, `/sys/class/devlink`로 defer 원인을 찾을 수 있다
- [ ] fw_devlink on과 permissive의 차이를 probe 순서와 unbind 순서 두 측면에서 설명할 수 있다
- [ ] 실패 주입과 bind/unbind 반복으로 누수가 없음을 증명하는 테스트를 짤 수 있다
- [ ] 비동기 probe의 이득과 "모듈 로드 = 장치 존재" 가정이 깨지는 대가를 말할 수 있다

---

## 참고 자료

- docs.kernel.org — Driver Model: Device Drivers (`Documentation/driver-api/driver-model/driver.rst`, probe·`-EPROBE_DEFER` 경고 포함): https://docs.kernel.org/driver-api/driver-model/driver.html
- docs.kernel.org — Devres - Managed Device Resource (`Documentation/driver-api/driver-model/devres.rst`, devm API 전체 목록): https://docs.kernel.org/driver-api/driver-model/devres.html
- docs.kernel.org — Platform Devices and Drivers: https://docs.kernel.org/driver-api/driver-model/platform.html
- docs.kernel.org — Device links (fw_devlink 배경): https://docs.kernel.org/driver-api/device_link.html
- 커널 소스 (랩 트리 `/var/lab/src/linux-6.18.55`, 또는 elixir.bootlin.com의 v6.18): `include/linux/platform_device.h`, `drivers/base/platform.c`(`platform_probe`, `platform_remove`), `drivers/base/dd.c`(`really_probe`, `device_unbind_cleanup`, deferred probe), `drivers/base/devres.c`, `drivers/base/core.c`(`dev_err_probe`, fw_devlink), `include/linux/cleanup.h`
- Bootlin — Linux kernel and driver development training 슬라이드 (platform driver, devm, device model 장): https://bootlin.com/training/kernel/
- 선행·연결 노트: D3(매칭), D4(DT 자원), E5(container_of · goto · devm · cleanup.h), F6(인터럽트), F7(지연 작업)
