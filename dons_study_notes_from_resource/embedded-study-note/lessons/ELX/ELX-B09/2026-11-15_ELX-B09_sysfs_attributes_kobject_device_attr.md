---
id: ELX-B09
time: 70분
updated: 2026-11-15
---
# ELX-B09 sysfs 속성과 kobject: DEVICE_ATTR

## 학습 목표

- `DEVICE_ATTR_RW/RO/WO` + `ATTRIBUTE_GROUPS` + `driver.dev_groups` 로 디바이스 디렉터리에 속성 파일을 붙이고, kobject → 디렉터리, attribute → 파일 관계를 `ls -l` 실물로 설명한다.
- `store` 에서 `kstrtoint` 로 파싱하고 범위를 검사해 잘못된 입력을 `-EINVAL` 로 돌려준다. 그 errno가 셸에서 `sh: write error: Invalid argument` 로 보이는 경로를 커널 소스로 따라간다.
- `"  42"`, `0x10`, `+7`, `-0`, 빈 쓰기, 0바이트 쓰기 같은 경계 입력이 **왜** 받아들여지거나 거부되는지 `lib/kstrtox.c`, `fs/sysfs/file.c` 로 답한다.
- 임계값을 넘을 때 `sysfs_notify()` 를 부르고, 유저 프로그램이 `poll(POLLPRI)` 로 깨어나는 것을 타이밍 없이 검증한다.
- 면접에서 "설정값을 왜 ioctl 이 아니라 sysfs 로?"에 기준(값 하나 = 파일 하나, 텍스트, ABI 문서)을 들어 답한다.

## 1. 문제

센서·PMIC·FPGA 드라이버를 bring-up 하면 "임계값을 런타임에 바꾸게 해 주세요", "에러 카운터를 보여 주세요"라는 요청이 가장 먼저 온다. 그 답이 sysfs 속성이고, 면접에서는 **"sysfs 와 ioctl 중 무엇으로 노출하나, store 에서 잘못된 입력은 어떻게 막나"** 로 나온다.

> 환경: 모든 커널 출력은 **QEMU TCG cortex-a57 `-smp 1` `-m 512M` 위의 Linux 6.18.55-dbg** (`/var/lab/out/Image-dbg`, lockdep + DEBUG_ATOMIC_SLEEP) 실측이다. 모듈은 Lima VM `elinux` 안에서 `/var/lab/out/linux-build-dbg` 로 빌드하고 **게스트 안에서만** 올린다. 게스트 셸은 BusyBox 1.37 `sh`(ash) 다. TCG는 실제 CPU가 아니므로 시간 수치는 참고용이다.

준비물은 `thsense` 라는 가짜 온도 센서 하나다. 모듈이 **platform_driver 와 platform_device 를 둘 다** 등록하므로, DT 에서 장치가 생겨 드라이버가 probe 되는 진짜 경로(ELX-M01)를 그대로 탄다. 드라이버가 노출하는 속성은 세 개다.

| 파일 | 모드 | 의미 | 함수 |
|---|---|---|---|
| `threshold` | `0644` rw | 경보 임계값, 10진수 0–1000, 기본 500 | `threshold_show` / `threshold_store` |
| `count` | `0444` ro | 상승 교차(rising crossing) 횟수 | `count_show` |
| `sample` | `0200` wo | 가짜 센서 입력 0–1000 (진짜 HW면 ADC 인터럽트 경로) | `sample_store` |

`sample` 은 테스트용 주입구(injection point)다. 진짜 드라이버라면 data-ready IRQ의 threaded handler 가 ADC 값을 읽어 같은 함수를 부른다. 테스트가 타이밍 없이 결정적(deterministic)이 되도록 셸에서 값을 넣게 했다.

```svg
<svg viewBox="0 0 740 310" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">kobject = 디렉터리, attribute = 파일 (게스트 ls -l 실측)</text>
<rect x="10" y="38" width="220" height="36" class="f-mut" stroke="currentColor"/>
<text x="20" y="61" fill="currentColor" font-family="monospace">struct platform_device</text>
<rect x="10" y="92" width="220" height="36" class="f-mut" stroke="currentColor"/>
<text x="20" y="115" fill="currentColor" font-family="monospace">.dev  (struct device)</text>
<rect x="10" y="146" width="220" height="36" class="f-acc" stroke="currentColor"/>
<text x="20" y="169" fill="currentColor" font-family="monospace">.dev.kobj (kobject)</text>
<rect x="10" y="210" width="220" height="56" class="f-ok" stroke="currentColor"/>
<text x="20" y="232" fill="currentColor" font-family="monospace">ths_groups[]</text>
<text x="20" y="254" fill="currentColor" font-size="12">driver.dev_groups, probe 성공 후</text>
<line x1="120" y1="74" x2="120" y2="92" stroke="currentColor"/>
<line x1="120" y1="128" x2="120" y2="146" stroke="currentColor"/>
<line x1="230" y1="164" x2="288" y2="56" stroke="currentColor"/>
<rect x="290" y="38" width="440" height="36" class="f-acc" stroke="currentColor"/>
<text x="300" y="61" fill="currentColor" font-family="monospace">/sys/devices/platform/thsense/</text>
<rect x="310" y="84" width="420" height="26" class="f-ok" stroke="currentColor"/>
<text x="318" y="102" fill="currentColor" font-family="monospace">-rw-r--r-- threshold  show + store</text>
<rect x="310" y="114" width="420" height="26" class="f-ok" stroke="currentColor"/>
<text x="318" y="132" fill="currentColor" font-family="monospace">-r--r--r-- count      show</text>
<rect x="310" y="144" width="420" height="26" class="f-ok" stroke="currentColor"/>
<text x="318" y="162" fill="currentColor" font-family="monospace">--w------- sample     store</text>
<rect x="310" y="180" width="420" height="26" class="f-mut" stroke="currentColor"/>
<text x="318" y="198" fill="currentColor" font-family="monospace">driver -&gt; ../../../bus/platform/drivers/thsense</text>
<rect x="310" y="210" width="420" height="26" class="f-mut" stroke="currentColor"/>
<text x="318" y="228" fill="currentColor" font-family="monospace">uevent modalias driver_override power/ subsystem</text>
<line x1="230" y1="238" x2="308" y2="130" stroke="currentColor"/>
<text x="318" y="256" fill="currentColor" font-size="12">초록 = 우리 그룹이 만든 파일, 회색 = driver core가 만든 파일</text>
<text x="10" y="290" fill="currentColor" font-family="monospace" font-size="12">/sys/bus/platform/devices/thsense -&gt; ../../../devices/platform/thsense (심볼릭 링크)</text>
</svg>
```

### Part A — 속성 세 개 만들기

- A1. `ths_parse_value(buf, &v)` — `kstrtoint(buf, 10, &v)` 로 파싱하고 0–1000 밖이면 `-EINVAL`. 파싱 에러(`-EINVAL`)와 int 넘침(`-ERANGE`)도 이 ABI에서는 모두 `-EINVAL` 로 통일한다.
- A2. `threshold_show` / `count_show` — `sysfs_emit()` 으로 10진수 + `'\n'`.
- 이미 있는 것: `DEVICE_ATTR_RW(threshold)`, `DEVICE_ATTR_RO(count)`, `DEVICE_ATTR_WO(sample)`, `ATTRIBUTE_GROUPS(ths)`, `.driver.dev_groups = ths_groups`, store 의 락/반환 구조.

### Part B — 경계 입력을 먼저 예측하라

셸에서 `echo <입력> > threshold` 를 했을 때 결과를 **먼저 적고** 실행해 맞춰 본다.

| 입력 (`echo` 가 `\n` 을 붙임) | 받아들이나? | 이유를 한 줄로 |
|---|---|---|
| `42`, `1000`, `+7`, `007`, `-0` | ? | |
| `abc`, `42abc`, `4 2`, `0x10` | ? | |
| `1001`, `-1`, `99999999999` | ? | |
| `"  42"` (앞 공백), `""` (줄바꿈만), `printf '42\n\n'` | ? | |
| `printf 17` (줄바꿈 없음), `write(fd, "", 0)` | ? | |
| `echo 1 > count`, `cat sample` | ? | |

### Part C — 교차 감지와 통지, 그리고 수명

- C1. `ths_update_locked(p)` — `above = level >= threshold` 를 다시 계산하고, false → true 일 때만 `count++` 하고 true 를 돌려준다. `sample` 쓰기와 `threshold` 쓰기 **둘 다** 이 함수를 부른다 (임계값을 내려서 넘는 것도 교차다).
- C2. `ths_notify(dev, rising)` — 교차가 있었으면 `sysfs_notify(&dev->kobj, NULL, "count")`. 반드시 **mutex 밖에서** 부른다.
- 검증: 유저 프로그램 `systest poll` 이 `count` 를 읽고 `poll(POLLPRI)` 로 잠든 사이, 자식 프로세스가 `sample` 에 700을 쓰면 깨어나야 한다. 그리고 `unbind` 하면 속성이 사라지고 `bind` 하면 기본값으로 돌아와야 한다.
- bring-up 시나리오: 과열 감시 데몬이 부팅 때 `threshold` 를 쓰고, `count` 파일을 `poll()` 로 기다리다 깨어나면 팬을 올린다. 폴링 루프 없이 이벤트로 동작한다.

## 2. Problem (English)

> Expose a sensor's alarm configuration through sysfs on a platform device `thsense`:
>
> ```c
> static int  ths_parse_value(const char *buf, int *out);   /* decimal int in [0,1000] or -EINVAL */
> static bool ths_update_locked(struct ths_priv *p);       /* rising edge -> count++, return true */
> static void ths_notify(struct device *dev, bool rising); /* sysfs_notify(.., "count") */
> static ssize_t threshold_show(struct device *, struct device_attribute *, char *);
> static ssize_t count_show(struct device *, struct device_attribute *, char *);
> ```
>
> - `threshold` (0644): decimal only, range 0..1000. Any other input (garbage, hex, leading spaces, overflow, out of range) must fail with `-EINVAL` and leave the value unchanged. A single trailing newline is allowed.
> - `count` (0444): number of rising crossings (`level >= threshold` going false → true), whether caused by a new sample or a new threshold.
> - Attributes must be created by the driver core via `.driver.dev_groups`, not in `probe()`.
> - A crossing must wake a `poll(POLLPRI)` waiter on `count`. Never call `sysfs_notify()` with a spinlock held.
>
> Example (BusyBox sh in the guest):
> ```text
> # echo 42 > threshold; cat threshold
> 42
> # echo 0x10 > threshold
> sh: write error: Invalid argument
> ```
>
> Follow-ups: why sysfs and not an ioctl for this value? What happens if `store` returns 0? Where does the "[selected]" format in `/sys/kernel/mm/transparent_hugepage/enabled` come from?

## 3. 시작 코드

파일 구성: 모듈(`starter.c` · `solution.c` · `practice.c`, `Makefile` 의 `VARIANT=` 로 고름), 게스트에서 도는 셸 테스트 `sysfs_test.sh`, poll/0바이트 쓰기를 하는 정적 헬퍼 `systest.c`(`gcc -static`), 그리고 VM 안에서 빌드 → initramfs → QEMU 부팅 → 판정을 하는 `check.sh`.

```bash
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B09/check.sh starter
```

starter 는 컴파일되고(`-Werror`) 모듈도 올라가지만 TODO 때문에 `36/84 passed`, `RESULT: FAIL (starter)`, exit 1 이다 (약 18초, poll 테스트가 5초 타임아웃을 한 번 기다림).

<!-- code: starter.c -->

<!-- code: Makefile -->

<!-- code: sysfs_test.sh -->

<!-- code: systest.c -->

<!-- code: check.sh -->

## 4. 힌트

<details><summary>힌트 1 — kstrtoint 는 무엇을 이미 해 주나</summary>

`kstrtoint(buf, 10, &v)` 는 앞의 `+`/`-` 하나, **끝의 줄바꿈 하나**를 허용하고, 그 밖의 남는 글자가 있으면 `-EINVAL`, int 범위를 넘으면 `-ERANGE` 를 돌려준다. 범위 0–1000 검사만 직접 하면 된다. base 를 0 으로 주면 무엇이 달라지는지 생각해 보라.
</details>

<details><summary>힌트 2 — show 의 반환값</summary>

`sysfs_emit(buf, "%d\n", v)` 가 쓴 바이트 수를 돌려준다. 그 값을 그대로 반환한다. `buf` 는 커널이 준 PAGE_SIZE 버퍼다. 0을 돌려주면 `cat` 이 빈 파일을 본다 (starter 의 모습).
</details>

<details><summary>힌트 3 — 교차는 "상태"의 변화다</summary>

`above` 를 저장해 두고 새 `above` 와 비교한다. 이미 위에 있는 동안 샘플이 더 와도 세지 않는다. `threshold` 를 낮춰서 현재 `level` 이 넘게 되면 그것도 교차다.
</details>

<details><summary>힌트 4 — 왜 notify 는 락 밖인가</summary>

`sysfs_notify()` 는 이름으로 kernfs 노드를 찾으면서 rwsem 을 잡는다 (잠들 수 있다). 스핀락 안이면 바로 `BUG:` 가 난다 (§7 실측). mutex 안이어도 동작은 하지만, 깨어난 유저가 곧장 `count_show` → 같은 mutex 를 기다리게 되므로 풀고 부르는 게 맞다.
</details>

## 5. 풀이

### A1 — `echo abc > threshold` 가 우리 함수까지 가는 길

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">쓰기 경로와 에러가 돌아오는 길 (6.18.55 소스 기준)</text>
<rect x="10" y="34" width="400" height="30" class="f-mut" stroke="currentColor"/>
<text x="20" y="54" fill="currentColor" font-family="monospace">sh: echo abc &gt; threshold  → write(fd,"abc\n",4)</text>
<rect x="10" y="80" width="400" height="30" class="f-acc" stroke="currentColor"/>
<text x="20" y="100" fill="currentColor" font-family="monospace">kernfs_fop_write_iter  (≤ PAGE_SIZE, NUL 종료)</text>
<rect x="10" y="126" width="400" height="30" class="f-acc" stroke="currentColor"/>
<text x="20" y="146" fill="currentColor" font-family="monospace">sysfs_kf_write  (count == 0 → return 0)</text>
<rect x="10" y="172" width="400" height="30" class="f-acc" stroke="currentColor"/>
<text x="20" y="192" fill="currentColor" font-family="monospace">dev_attr_store  (container_of → device_attribute)</text>
<rect x="10" y="218" width="400" height="30" class="f-ok" stroke="currentColor"/>
<text x="20" y="238" fill="currentColor" font-family="monospace">threshold_store → ths_parse_value</text>
<rect x="10" y="264" width="400" height="30" class="f-ok" stroke="currentColor"/>
<text x="20" y="284" fill="currentColor" font-family="monospace">kstrtoint("abc\n", 10) = -EINVAL</text>
<line x1="210" y1="64" x2="210" y2="80" stroke="currentColor"/>
<line x1="210" y1="110" x2="210" y2="126" stroke="currentColor"/>
<line x1="210" y1="156" x2="210" y2="172" stroke="currentColor"/>
<line x1="210" y1="202" x2="210" y2="218" stroke="currentColor"/>
<line x1="210" y1="248" x2="210" y2="264" stroke="currentColor"/>
<line x1="410" y1="279" x2="450" y2="279" stroke="currentColor"/>
<line x1="450" y1="279" x2="450" y2="49" stroke="currentColor" stroke-dasharray="5 4"/>
<line x1="450" y1="49" x2="470" y2="49" stroke="currentColor"/>
<rect x="470" y="34" width="260" height="58" class="f-warn" stroke="currentColor"/>
<text x="480" y="54" fill="currentColor" font-family="monospace">write() = -1, errno=EINVAL</text>
<text x="480" y="80" fill="currentColor" font-family="monospace">echo 의 $? = 1</text>
<rect x="470" y="110" width="260" height="58" class="f-warn" stroke="currentColor"/>
<text x="480" y="130" fill="currentColor" font-size="12">BusyBox ash:</text>
<text x="480" y="152" fill="currentColor" font-family="monospace" font-size="12">sh: write error: Invalid argument</text>
<rect x="470" y="180" width="260" height="58" class="f-warn" stroke="currentColor"/>
<text x="480" y="200" fill="currentColor" font-size="12">bash (VM 실측):</text>
<text x="480" y="222" fill="currentColor" font-family="monospace" font-size="12">bash: … echo: write error: …</text>
<text x="10" y="318" fill="currentColor" font-size="12">store 가 돌려준 음수 errno 는 아무도 바꾸지 않고 write() 의 errno 가 된다. 메시지 문구는 셸이 정한다.</text>
</svg>
```

층별로 읽은 소스(`/var/lab/src/linux-6.18.55`):

| 층 | 파일 | 이 레슨에 중요한 줄 |
|---|---|---|
| kernfs | `fs/kernfs/file.c` `kernfs_fop_write_iter()` | `len = min_t(size_t, len, PAGE_SIZE)`, `kmalloc(len + 1)` 후 `buf[len] = '\0'` → store 의 `buf` 는 **항상 NUL 종료** |
| kernfs | 같은 파일 `kernfs_fop_open()` | sysfs 루트는 `KERNFS_ROOT_EXTRA_OPEN_PERM_CHECK`: 쓰기 모드로 여는데 `S_IWUGO` 가 없거나 write 가 없으면 `-EACCES` (root 도) |
| sysfs | `fs/sysfs/file.c` `sysfs_kf_write()` | `if (!count) return 0;` → 0바이트 쓰기는 store 를 **부르지도 않고** 성공 |
| driver core | `drivers/base/core.c` `dev_attr_store()` | `dev_sysfs_ops = { dev_attr_show, dev_attr_store }`. `to_dev_attr()`(container_of) 로 우리 `device_attribute` 를 찾는다 |
| 매크로 | `include/linux/device.h`, `include/linux/sysfs.h` | `DEVICE_ATTR_RW(threshold)` → `struct device_attribute dev_attr_threshold = __ATTR(threshold, 0644, threshold_show, threshold_store)` |

`DEVICE_ATTR_RW(x)` 는 이름 규칙(`x_show`, `x_store`)으로 함수를 찾는다. 오타가 있으면 링크가 아니라 **컴파일 에러**(선언 안 된 식별자)로 바로 드러난다. 모드는 `VERIFY_OCTAL_PERMISSIONS()` 를 거치므로 `0666` 같은 world-writable 은 빌드가 깨진다 (§7 실측).

### A2 — `ths_parse_value`: kstrtoint 가 해 주는 것, 우리가 할 것

`lib/kstrtox.c` 의 `kstrtoint()` → `kstrtoll()` → `_kstrtoull()` 순서다.

1. `kstrtoll`: 첫 글자가 `-` 면 떼고 음수로 처리. `kstrtoull` 은 첫 글자 `+` 를 하나 떼어 준다.
2. `_parse_integer_fixup_radix(s, &base)`: base 가 0 일 때만 `0x` → 16진, `0` → 8진으로 바꾼다. **base 10 이면 `0x10` 은 `0` 까지만 읽고** `x` 가 남는다.
3. `_parse_integer`: 숫자를 읽은 개수 `rv`. 넘침 비트가 서면 `-ERANGE`, 하나도 못 읽으면(`rv == 0`) `-EINVAL`.
4. `if (*s == '\n') s++; if (*s) return -EINVAL;` → **줄바꿈은 정확히 하나만** 허용, 앞 공백은 허용 안 함.
5. `kstrtoint`: `tmp != (int)tmp` 면 `-ERANGE`.

그래서 base 를 10 으로 고정하는 것이 이 ABI의 핵심 결정이다. base 0 이면 `010` 이 **8** 이 된다 (소스상 동작, 이 레슨에서는 실행하지 않음). 셸 스크립트가 0으로 채운 숫자(`printf %03d`)를 쓰는 순간 값이 바뀌는 버그가 된다.

```c
static int ths_parse_value(const char *buf, int *out)
{
	int v;

	if (kstrtoint(buf, 10, &v))	/* -EINVAL garbage, -ERANGE int overflow */
		return -EINVAL;		/* this ABI: every bad input is -EINVAL */
	if (v < THS_MIN || v > THS_MAX)
		return -EINVAL;
	*out = v;
	return 0;
}
```

`-ERANGE` 를 `-EINVAL` 로 바꾼 이유: 이 속성의 ABI 문서에 "잘못된 값은 EINVAL" 하나만 약속하기 위해서다. kstrtox 에러를 그대로 넘기는 드라이버도 많고, 그러면 `99999999999` 에 `Numerical result out of range` 가 보인다 (G8 노트 실측). 둘 다 맞고, **문서와 일치하는 것**이 중요하다.

### A3 — show: `sysfs_emit` 과 줄바꿈

`sysfs_emit()` (`fs/sysfs/file.c`) 은 `buf` 가 페이지 정렬인지 `WARN` 으로 검사하고 `vscnprintf(buf, PAGE_SIZE, ...)` 를 부른다. `sprintf` 와 달리 PAGE_SIZE 를 넘어 쓰지 않는다. 값 뒤 `'\n'` 은 관례다. 테스트의 `od -An -c` 체크가 `42\n` 을 확인한다. 줄바꿈이 없으면 `cat threshold; echo done` 이 `42done` 으로 붙는다.

show 안에서 `mutex_lock` 으로 `threshold` 를 읽는다. 지금은 int 하나라 찢어진 읽기(torn read)가 없지만, store 가 여러 필드를 함께 바꾸는 순간(practice 의 hysteresis 처럼) 락이 필요해지므로 처음부터 같은 락을 쓴다.

### A4 — 속성은 누가, 언제 만드나: `.dev_groups`

`drivers/base/dd.c` `really_probe()` 는 `call_driver_probe()` 가 **성공한 뒤** `device_add_groups(dev, drv->dev_groups)` (737행)를 부르고, 그 뒤 `driver_bound()` 에서 `KOBJ_BIND` uevent (481행)를 보낸다. unbind 때는 `device_remove_groups(dev, dev->driver->dev_groups)` (625행)로 자동으로 지운다. 실측:

```text
$ ls -l /sys/devices/platform/thsense/        (practice.ko 로 찍음, 색 코드 제거, 일부 생략)
total 0
-r--r--r--    1 0        0             4096 Nov 15 23:12 count
lrwxrwxrwx    1 0        0                0 Nov 15 23:12 driver -> ../../../bus/platform/drivers/thsense
-rw-r--r--    1 0        0             4096 Nov 15 23:12 driver_override
…
-r--r--r--    1 0        0             4096 Nov 15 23:12 modalias
drwxr-xr-x    2 0        0                0 Nov 15 23:12 power
…
--w-------    1 0        0             4096 Nov 15 23:12 sample
lrwxrwxrwx    1 0        0                0 Nov 15 23:12 subsystem -> ../../../bus/platform
-rw-r--r--    1 0        0             4096 Nov 15 23:12 threshold
-rw-r--r--    1 0        0             4096 Nov 15 23:12 uevent
$ cat /sys/devices/platform/thsense/uevent
DRIVER=thsense
MODALIAS=platform:thsense
$ echo thsense > /sys/bus/platform/drivers/thsense/unbind; ls /sys/devices/platform/thsense/
driver_override  power            uevent
modalias         subsystem
```

unbind 후에도 **디렉터리(= device 의 kobject)는 남고** 우리 파일만 사라진다. 장치와 드라이버의 수명이 다르기 때문이다. 다시 `bind` 하면 probe 가 새로 돌아 `devm_kzalloc` 한 상태가 새로 생기므로 `threshold=500`, `count=0` 으로 돌아온다 (테스트 `bind: threshold back at default` PASS).

probe 안에서 `sysfs_create_group()` 을 부르면: (1) 제거를 remove/에러 경로에서 직접 해야 하고, (2) `KOBJ_BIND` 와의 순서를 드라이버가 책임져야 한다. `.dev_groups` 는 그 둘을 driver core 가 대신 한다.

**왜 misc device 가 아니라 platform device 인가.** misc 도 `struct miscdevice.groups` 로 속성을 붙일 수 있고(`device_create_with_groups` → `KOBJ_ADD` 전에 생성) 간단하다. 하지만 misc 는 `/dev` 노드(문자 장치, ELX-B07)를 위한 것이고, 이 센서는 `/dev` 노드가 필요 없다. 그리고 실제 보드의 센서는 DT 에서 platform/i2c 장치로 생기고 드라이버가 bind 되므로, `.dev_groups` · bind/unbind 수명을 그대로 연습하려고 platform 을 골랐다.

### B — 경계 입력 결과 (실측)

| 입력 | 결과 | 근거 |
|---|---|---|
| `42`, `1000` | 받음 | 정상 |
| `+7` → 7, `007` → 7, `-0` → 0 | 받음 | `+` 하나 허용, base 10 에서 앞 0 은 그냥 10진, `-0` 은 `-tmp` = 0 |
| `printf 17` (줄바꿈 없음) | 받음 | 줄바꿈은 선택 |
| `abc`, `42abc`, `4 2`, `0x10` | `Invalid argument` | 남는 글자 |
| `1001`, `-1` | `Invalid argument` | 우리 범위 검사 |
| `99999999999` | `Invalid argument` | kstrtoint `-ERANGE` → 우리가 `-EINVAL` 로 |
| `"  42"` | `Invalid argument` | 앞 공백 허용 안 함 |
| `""` (실제로는 `"\n"`) | `Invalid argument` | 숫자 0개 (`rv == 0`) |
| `printf '42\n\n'` | 거부 | 줄바꿈은 하나만 |
| `write(fd, "", 0)` | `ZERO_RET=0`, 값 그대로 | `sysfs_kf_write` 가 store 전에 0 반환 |
| `echo 1 > count` | `can't create …/count: Permission denied` | open 단계 `-EACCES` (셸 redirect 실패) |
| `cat sample` | `cat: can't open '…/sample': Permission denied` | 같은 검사, 읽기 쪽 |

실제 로그 일부 (`check.sh solution`):

```text
  [abc] rc=1 err: sh: write error: Invalid argument
PASS: reject abc: rc!=0
PASS: reject abc: Invalid argument
PASS: reject abc: threshold unchanged
…
  [zero-length write] ZERO_RET=0
PASS: zero-length write returns 0
  [write count] rc=1 err: /sysfs_test.sh: line 70: can't create /sys/bus/platform/devices/thsense/count: Permission denied
  [read sample] rc=1 err: cat: can't open '/sys/bus/platform/devices/thsense/sample': Permission denied
```

과제 설명의 `echo: write error: Invalid argument` 는 **bash** 문구다. 같은 EINVAL 을 VM(bash, 커널 7.0)에서 무해하게 받아 본 결과(잘못된 값이라 아무것도 안 바뀐다):

```text
$ sudo bash -c "echo abc > /sys/kernel/mm/transparent_hugepage/enabled"
bash: line 1: echo: write error: Invalid argument
$ sudo busybox sh -c "echo abc > /sys/kernel/mm/transparent_hugepage/enabled"
sh: write error: Invalid argument
```

그래서 테스트는 셸 문구 전체가 아니라 `strerror` 부분 `Invalid argument` 와 `$?` 로 판정한다.

### C1 — 교차 상태 기계

```c
static bool ths_update_locked(struct ths_priv *p)
{
	bool above = p->level >= p->threshold;
	bool rising = above && !p->above;

	p->above = above;
	if (rising)
		p->count++;
	return rising;
}
```

테스트 순서와 실측 값 (threshold 500, C 구간 시작 시 `count = 1`):

| 동작 | level | threshold | above | count |
|---|---|---|---|---|
| (시작) | 0 | 500 | false | 1 |
| sample 100 | 100 | 500 | false | 1 |
| sample 600 | 600 | 500 | **true ↑** | 2 |
| sample 700 | 700 | 500 | true | 2 |
| sample 499 | 499 | 500 | false | 2 |
| sample 500 | 500 | 500 | **true ↑** (`>=`) | 3 |
| sample 1000, 0 | … | 500 | true, false | 3 |
| sample 999 | 999 | 500 | **true ↑** | 4 |
| sample 100, threshold 50 | 100 | 50 | **true ↑** | 5 |
| threshold 1000, 100 | 100 | 100 | false, **true ↑** | 6 |

시작 값이 1인 이유: Part A 의 `accept '-0'` 단계에서 threshold 가 0 이 됐고, 그때 level 0 ≥ 0 이라 교차가 이미 한 번 있었다 (`[count before C] 1`). "임계값을 바꾸는 것도 교차를 만든다"는 규칙이 테스트 자체에서 드러난 것이다.

### C2 — `sysfs_notify` 와 `poll(POLLPRI)`

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">systest poll: count 파일에서 잠들고 깨어나기</text>
<rect x="20" y="32" width="170" height="28" class="f-acc" stroke="currentColor"/>
<text x="30" y="51" fill="currentColor">부모 (systest)</text>
<rect x="290" y="32" width="170" height="28" class="f-mut" stroke="currentColor"/>
<text x="300" y="51" fill="currentColor">kernfs (count 노드)</text>
<rect x="550" y="32" width="170" height="28" class="f-ok" stroke="currentColor"/>
<text x="560" y="51" fill="currentColor">자식 → sample_store</text>
<line x1="105" y1="60" x2="105" y2="290" stroke="currentColor" stroke-dasharray="3 3"/>
<line x1="375" y1="60" x2="375" y2="290" stroke="currentColor" stroke-dasharray="3 3"/>
<line x1="635" y1="60" x2="635" y2="290" stroke="currentColor" stroke-dasharray="3 3"/>
<line x1="105" y1="84" x2="375" y2="84" stroke="currentColor"/>
<text x="115" y="78" fill="currentColor" font-size="12">① pread(fd,0) = n0 → of-&gt;event = on-&gt;event</text>
<line x1="105" y1="116" x2="375" y2="116" stroke="currentColor"/>
<text x="115" y="110" fill="currentColor" font-size="12">② poll(POLLPRI, 5000 ms): 같으면 잠듦</text>
<rect x="545" y="128" width="180" height="40" class="f-warn" stroke="currentColor"/>
<text x="553" y="145" fill="currentColor" font-size="12">③ echo 700 &gt; sample</text>
<text x="553" y="161" fill="currentColor" font-size="12">rising: count = n0+1</text>
<line x1="635" y1="196" x2="375" y2="196" stroke="currentColor"/>
<text x="400" y="190" fill="currentColor" font-size="12">④ unlock 후 sysfs_notify</text>
<text x="385" y="214" fill="currentColor" font-size="12">on-&gt;event++, wake_up</text>
<line x1="375" y1="236" x2="105" y2="236" stroke="currentColor"/>
<text x="115" y="230" fill="currentColor" font-size="12">⑤ revents = POLLPRI|POLLERR</text>
<line x1="105" y1="272" x2="375" y2="272" stroke="currentColor"/>
<text x="115" y="266" fill="currentColor" font-size="12">⑥ pread(fd,0) = n0+1, 다시 무장</text>
</svg>
```

소스 근거 (`fs/kernfs/file.c`):

- `kernfs_seq_show()`: 읽을 때마다 `of->event = atomic_read(&of_on(of)->event)` — 열린 파일마다 "마지막으로 본 이벤트 번호"를 기억한다. 그래서 **먼저 읽고 나서** poll 해야 한다.
- `kernfs_generic_poll()`: `of->event != on->event` 이면 `DEFAULT_POLLMASK|EPOLLERR|EPOLLPRI`, 아니면 `DEFAULT_POLLMASK`. sysfs 파일은 항상 `POLLIN` 이 켜져 있다 → `POLLIN` 으로 기다리면 즉시 돌아온다 (테스트 `poll(&p, 1, 0) == 1 && (p.revents & POLLIN)` PASS).
- `kernfs_notify()`: `atomic_inc(&on->event); wake_up_interruptible(&on->poll);` 후 fsnotify 용 work 를 `schedule_work`. 이 함수 자체는 RCU + `spin_lock_irqsave` 만 쓴다.
- `sysfs_notify()` (`fs/sysfs/file.c`): 이름 `"count"` 로 노드를 찾는 `kernfs_find_and_get()` 이 `kernfs_rwsem` 을 잡는다 → **잠들 수 있다**. 그래서 하드 IRQ 에서는 probe 때 `sysfs_get_dirent()` 로 노드를 잡아 두고 `sysfs_notify_dirent()` 를 쓰거나 threaded IRQ/workqueue 로 넘긴다.

검증이 타이밍에 기대지 않는 이유: notify 는 store 안에서 동기적으로 일어나므로 `write()` 가 돌아온 시점에 이벤트 번호는 이미 바뀌었거나 안 바뀌었다. 그래서 "교차 없음"은 `poll(timeout=0) == 0`, "교차 있음"은 `poll(timeout=0) == 1` 로 바로 판정한다. 블로킹 경우(②)만 5초 상한을 두는데, 이는 실패 시 무한 대기를 막는 안전장치일 뿐 판정 기준이 아니다.

```text
PASS: poll: poll_pri(fd, 0, &rev) == 0
PASS: poll: r == 1
PASS: poll: (rev & POLLPRI) && (rev & POLLERR)
PASS: poll: readval(fd) == n0 + 1
…
poll: 18/18 passed
```

### 실행 결과 (solution)

```text
PASS: unbind: device dir stays
PASS: unbind: threshold gone
PASS: bind: threshold back at default
PASS: bind: count reset
PASS: rmmod: device gone
sysfs_test: 66/66 passed
DMESG_BAD=0
84/84 passed, dmesg bad lines: 0
RESULT: PASS (solution)
```

3회 연속 실행 모두 `RESULT: PASS (solution)`, exit 0. 한 번에 VM 기준 약 10–11초 (빌드 + TCG 부팅 + 테스트, 참고용). 전체 로그는 `run_solution.txt`.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B09/check.sh solution
```

## 7. 자주 하는 실수

세 가지는 일부러 망가뜨린 모듈 `demo_bugs.c` 로 재현했다 (`check.sh demo <ret0|atomic|perm>`, 로그 `demo_*.txt`).

<!-- code: demo_bugs.c -->

**1. store 가 0 을 돌려준다** ("받았다"는 뜻으로 착각). 0 은 "0바이트 소비"다. 유저의 `write()` 가 0 을 받고, BusyBox 의 쓰기 루프는 남은 바이트를 다시 쓴다 → 무한 반복.

```bash
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B09/check.sh demo ret0
```

```text
--- echo 42 > threshold (store returns 0), killed after 3 s:
Terminated
rc=143
store calls: 39167  threshold: 42
```

`timeout 3` 이 죽일 때까지 store 가 수만 번 불렸다 (27785, 39167 — 실행마다 다름, TCG). 고침: 성공하면 `return count;`.

**2. 스핀락을 쥔 채 `sysfs_notify()`.** Image-dbg 의 DEBUG_ATOMIC_SLEEP + lockdep 이 바로 잡는다.

```text
BUG: sleeping function called from invalid context at kernel/locking/rwsem.c:1536
in_atomic(): 1, irqs_disabled(): 0, non_block: 0, pid: 65, name: test.sh
…
 #3: ffffdba061c47058 (demo_lock){+.+.}-{3:3}, at: sample_store+0x70/0x160 [demo_bugs]
Call trace:
…
 down_read+0x48/0x1c0
 kernfs_find_and_get_ns+0x30/0x68
 sysfs_notify+0xa8/0xc8
 sample_store+0x13c/0x160 [demo_bugs]
 dev_attr_store+0x20/0x40
 sysfs_kf_write+0x58/0x90
 kernfs_fop_write_iter+0x140/0x220
…
[ BUG: Invalid wait context ]
```

콜 트레이스가 그대로 A1 그림의 경로다. 고침: 락 안에서는 `rising` 만 계산하고, 풀고 나서 notify (solution 의 `ths_notify`).

**3. `DEVICE_ATTR(world, 0666, ...)`.** 빌드가 깨진다.

```text
…/include/linux/compiler.h:197:67: error: static assertion failed: "(0666) & 2 is true"
…/include/linux/kernel.h:210:10: note: in expansion of macro ‘BUILD_BUG_ON_ZERO’
  210 |          BUILD_BUG_ON_ZERO((perms) & 2) +
demo_bugs.c:81:8: note: in expansion of macro ‘DEVICE_ATTR’
```

권한을 넓혀야 하면 udev 규칙으로 그룹을 바꾼다 (`0664` + `GROUP=`). 커널이 other-writable 을 금지한다.

**4. 에러를 삼킨다.** `if (kstrtoint(...)) return count;` 처럼 실패해도 `count` 를 돌려주면 셸의 `$?` 가 0 이 되어 설정 스크립트가 실패를 모른다. 항상 음수 errno 를 돌려준다.

**5. base 0.** `kstrtoint(buf, 0, &v)` 는 `0x10` 을 받아 주지만 `010` 을 8 로 읽는다. 10진 ABI 면 base 10.

**6. probe 에서 `sysfs_create_group()`.** remove 와 에러 경로에서 지우는 걸 잊기 쉽고, uevent 와의 순서도 드라이버 책임이 된다. `.dev_groups` 를 쓴다.

**7. `sprintf(buf, ...)` / 줄바꿈 누락.** `sysfs_emit` 은 페이지 경계를 지키고 정렬을 `WARN` 으로 검사한다. 값 뒤에는 `\n`.

## 8. 면접 Q&A

<details><summary>Q1. 설정값 하나를 노출하는데 ioctl 대신 sysfs 를 쓰는 기준은?</summary>

sysfs: 값 하나 = 텍스트 파일 하나, `cat`/`echo` 와 udev 규칙(`ATTR{threshold}`)으로 바로 쓰고, 파일마다 권한을 따로 주고, `Documentation/ABI/` 에 문서로 약속한다. 설정·상태·토폴로지에 맞다. ioctl(ELX-B08): 여러 필드를 한 번에 원자적으로 바꾸거나, 바이너리 구조체, 데이터 경로, 호출 빈도가 높은 경우. 꼬리질문 "sysfs 의 비용은?" → 값 하나에 open/read/close 와 텍스트 파싱, 32/64비트 구조체 ABI 문제는 없다.
</details>

<details><summary>Q2. `DEVICE_ATTR_RW(threshold)` 는 무엇으로 펼쳐지나요?</summary>

`struct device_attribute dev_attr_threshold = __ATTR(threshold, 0644, threshold_show, threshold_store)`. `.attr.name = "threshold"`, `.attr.mode = VERIFY_OCTAL_PERMISSIONS(0644)`. `ATTRIBUTE_GROUPS(ths)` 는 `ths_attrs[]` 로 `ths_group` 과 NULL 로 끝나는 `ths_groups[]` 를 만든다. 꼬리질문 "읽기 시 누가 threshold_show 를 찾나?" → kobject 의 ktype 이 가진 `dev_sysfs_ops.show = dev_attr_show` 가 `container_of` 로 `device_attribute` 를 찾아 부른다.
</details>

<details><summary>Q3. kobject 는 무엇이고, 드라이버는 왜 kobject 를 직접 만들지 않나요?</summary>

kobject = 이름 + 부모 + 참조 카운트(kref) + ktype(release, sysfs_ops, 기본 그룹) + kset. `/sys` 의 디렉터리 하나가 kobject 하나(`kobj->sd` 가 kernfs 노드)다. `struct device` 가 kobject 를 품고 uevent, bus/driver 연결, PM, `uevent`/`driver` 파일을 더한다. 맨 kobject(추가 연습 P4)는 `uevent` 파일도, bind 도 없다 (테스트 `P4 kobject is not a device (no uevent file)` PASS). 장치 속성은 장치 kobject 에 붙여야 udev 와 수명 관리가 따라온다.
</details>

<details><summary>Q4. store 의 반환값 규칙은? 0이나 count 보다 작은 값을 돌려주면?</summary>

성공은 `count`(전부 소비), 실패는 음수 errno. 0 을 돌려주면 유저 `write()` 가 0 을 받고 대부분의 쓰기 루프가 다시 시도한다 → 무한 반복 (§7 실측: 3초에 store 수만 번). 작은 양수는 부분 쓰기라서 유저 루프가 나머지를 다시 보내고 store 가 또 불린다 (`kernfs_fop_write_iter` 의 `ki_pos += len` 기준 추론, 실행 안 함). 꼬리질문 "4096 바이트 넘게 쓰면?" → `kernfs_fop_write_iter` 가 PAGE_SIZE 로 자르므로 store 는 앞 4096 바이트만 받는다 (소스 기준, 이 레슨에서 실행 안 함).
</details>

<details><summary>Q5. `echo "  42" > threshold` 는 왜 실패하나요? 받아 주고 싶다면?</summary>

`kstrtoint` 는 앞 공백을 허용하지 않고 끝 줄바꿈 하나만 허용한다. 관대하게 하려면 `skip_spaces()`/`strim()` 으로 먼저 다듬을 수 있지만, 그러면 그것도 ABI 가 된다. 엄격하게 시작해서 문서화하는 편이 나중에 바꾸기 쉽다 (넓히기는 쉬워도 좁히기는 사용자를 깬다). 꼬리질문 "문자열 비교에서 줄바꿈은?" → `sysfs_streq()` / `sysfs_match_string()` 이 끝 `\n` 하나를 무시한다 (추가 연습 P2).
</details>

<details><summary>Q6. 앱이 값의 변화를 알려면? 하드 IRQ 에서 알릴 수 있나요?</summary>

드라이버가 `sysfs_notify(&dev->kobj, NULL, "count")`, 앱은 파일을 먼저 읽고 `poll(POLLPRI|POLLERR)` 로 기다린 뒤, 깨어나면 `pread(fd, …, 0)` 으로 다시 읽는다. `POLLIN` 은 항상 켜져 있어 소용없다. `sysfs_notify` 는 rwsem 을 잡아 잠들 수 있으므로 하드 IRQ 에서는 미리 잡아 둔 노드로 `sysfs_notify_dirent()` 를 쓰거나 threaded IRQ(ELX-M05)/workqueue(ELX-M06)로 넘긴다. 꼬리질문 "notify 를 지원 안 하는 속성을 poll 하면?" → 영원히 안 깨어난다, 타임아웃 필수.
</details>

<details><summary>Q7. 속성을 probe 에서 만들지 말고 `.dev_groups` 로 넘기라는 이유는?</summary>

`really_probe()` 가 probe 성공 뒤 그룹을 만들고 `KOBJ_BIND` 를 보내며, unbind 때 자동으로 지운다. 실패한 probe 에서는 아예 안 만든다. 직접 만들면 에러 경로/remove 정리를 빼먹기 쉽고, udev 가 속성이 없는 순간을 볼 수 있다. 장치를 직접 만드는 코드는 `device_create_with_groups()` 나 `class->dev_groups` 로 `KOBJ_ADD` 전에 만든다.
</details>

<details><summary>Q8. 새 sysfs 속성을 업스트림에 낼 때 같이 내야 하는 것은?</summary>

`Documentation/ABI/testing/sysfs-…` 항목: `What:`(경로), `Date:`, `KernelVersion:`, `Contact:`, `Description:`(단위, 범위, 에러). 한 번 나간 이름·형식·단위는 바꾸지 않는다. 그리고 표준 서브시스템(hwmon 의 `tempY_max` / `tempY_max_alarm`, IIO 의 `events/in_<type>_thresh_rising_value` — 둘 다 랩 소스 `Documentation/ABI/testing/` 에 있음)이 이미 표현할 수 있으면 커스텀 속성보다 그쪽을 쓰는 게 리뷰 기대치다.
</details>

## 9. 추가 연습

`practice.c` 는 solution 위에 네 가지를 더했다. `check.sh practice` 하나로 기본 테스트 + 연습 테스트를 모두 돌린다.

**P1. 히스테리시스(hysteresis).** `hysteresis` (rw, 0–100, 기본 0) 를 추가하라. 한 번 위로 넘은 뒤에는 `level < threshold - hysteresis` 가 되어야 다시 무장(re-arm)한다. 노이즈 낀 센서가 500 근처에서 떨 때 경보가 수십 번 울리지 않게 하는 것이다.

<details><summary>답</summary>

`ths_update_locked` 를 슈미트 트리거처럼 바꾼다: `!above && level >= thr` → 상승, `above && level < thr - hyst` → 하강. `hyst = 0` 이면 solution 과 같다. threshold 500, hyst 50 에서 600 → 1, 480 → 1 (아직 위), 600 → 1, 450 → 1 (`450 < 450` 아님), 449 → 무장, 500 → 2. 실측 `P1 hyst=50 sample 500 -> count 2` PASS. `hysteresis` 쓰기도 상태를 다시 평가한다.
</details>

**P2. 열거형 속성 `edge`.** `rising` / `falling` / `both` 중 어떤 교차를 셀지 고르는 속성을 만들어라. 읽으면 `/sys/kernel/mm/transparent_hugepage/enabled` (VM 실측 `always [madvise] never`) 처럼 선택된 값을 `[]` 로 보여라.

<details><summary>답</summary>

`static const char * const ths_edge_names[] = { "rising", "falling", "both" };`, store 는 `sysfs_match_string(ths_edge_names, buf)` (끝 `\n` 하나 무시, 없으면 `-EINVAL`), show 는 `sysfs_emit_at(buf, len, i == cur ? "%s[%s]" : "%s%s", ...)` 로 이어 쓴다. 실측: `[rising] falling both`, `echo both` 후 `rising falling [both]`, `bot`·`"both "` 는 `Invalid argument`. "값 하나 = 파일 하나"의 공인된 예외(선택지 목록)다.
</details>

**P3. 쓰기 전용 `reset`.** `1` 을 쓰면 `count` 를 0 으로 만들고 `count` 를 notify 하라. 다른 값은 모두 `-EINVAL`.

<details><summary>답</summary>

`if (!sysfs_streq(buf, "1")) return -EINVAL;`. `kstrtobool()` 을 쓰지 않은 이유: `lib/kstrtox.c` 를 보면 **첫 글자만** 본다 (`y`, `1`, `t`, `e`, `on` → true). `"yes please"`, `"1abc"` 도 true 가 된다. 실측: `reset 1 -> count 0`, `yes`, `0` 거부, `cat reset` 은 `Permission denied`.
</details>

**P4. 맨 kobject.** 장치와 무관한 모듈 전역 정보 `/sys/kernel/thsense_info/abi_version` (ro, `1`) 를 kobject 로 만들어라.

<details><summary>답</summary>

`kobject_create_and_add("thsense_info", kernel_kobj)` + `struct kobj_attribute` (`__ATTR_RO(abi_version)`, 함수 시그니처가 `struct kobject *` 를 받는다) + `sysfs_create_group()`. 정리는 `kobject_put()` 하나 (refcount 가 0 이 되면 sysfs 에서 지우고 해제). init 에러 경로에서도 `kobject_put`. 실측: `abi_version` = `1`, 모드 444, `uevent` 파일 없음, rmmod 후 디렉터리 사라짐. 장치 속성을 이렇게 만들면 안 된다는 게 Q3.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B09/check.sh practice
```

실측: `sysfs_test: 90/90 passed`, `poll: 18/18 passed`, `108/108 passed, dmesg bad lines: 0`, `RESULT: PASS (practice)`.

## 10. 핵심 정리

- kobject = `/sys` 디렉터리, attribute = 파일. `struct device` 가 kobject 를 품고, `dev_attr_show/store` 가 `container_of` 로 우리 함수를 부른다.
- `DEVICE_ATTR_RW/RO/WO(x)` → `x_show`/`x_store`, 모드 0644/0444/0200. `0666` 은 빌드 에러.
- 속성은 `ATTRIBUTE_GROUPS` + `driver.dev_groups`: probe 성공 후 생성, `KOBJ_BIND` 전, unbind 때 자동 삭제.
- store: `kstrtoint(buf, 10, …)` + 범위 검사, 실패는 음수 errno, 성공은 `count`. 0 반환은 무한 재시도.
- kstrtoint: `+`/`-` 하나, 끝 `\n` 하나만 허용. 앞 공백·`0x`(base 10)·남는 글자는 `-EINVAL`, 넘침은 `-ERANGE`.
- 0바이트 쓰기는 `sysfs_kf_write` 에서 store 없이 0. ro 쓰기/wo 읽기는 open 에서 `EACCES`.
- show: `sysfs_emit(buf, "%d\n", v)`.
- 변화 통지: 락 밖에서 `sysfs_notify`, 유저는 읽고 → `poll(POLLPRI)` → `pread(…, 0)`. IRQ 에서는 `sysfs_notify_dirent`.

## 참고자료

- 랩 소스 6.18.55: `fs/kernfs/file.c` (`kernfs_fop_write_iter`, `kernfs_fop_open`, `kernfs_seq_show`, `kernfs_generic_poll`, `kernfs_notify`), `fs/sysfs/file.c` (`sysfs_kf_write`, `sysfs_notify`, `sysfs_emit`), `drivers/base/core.c` (`dev_attr_store`, `dev_sysfs_ops`), `drivers/base/dd.c` (`really_probe`), `lib/kstrtox.c`, `lib/string.c` (`sysfs_streq`, `__sysfs_match_string`), `include/linux/sysfs.h`, `include/linux/kernel.h` (`VERIFY_OCTAL_PERMISSIONS`)
- Documentation/filesystems/sysfs.rst, Documentation/core-api/kobject.rst, Documentation/ABI/README
- Greg Kroah-Hartman, "How to create a sysfs file correctly" (`.dev_groups` 를 쓰라는 이유)
- 이 사이트: ELX-B07 (문자 디바이스 read/write), ELX-B08 (ioctl 설계), ELX-M01 (platform driver 와 DT), ELX-M02 (드라이버의 poll), ELX-M04 (락 선택), ELX-M05 (threaded IRQ)
- 배경 노트: EmbeddedLinux/G_driver_model/2026-10-10_G8_sysfs_debugfs_configfs.md (§4 에러, §7 notify, §8 dev_groups), G2 (디바이스 모델)
