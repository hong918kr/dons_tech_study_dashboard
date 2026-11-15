---
id: ELX-B06
time: 70분
updated: 2026-11-15
---
# ELX-B06 커널 모듈 Hello: init/exit, module_param, Kbuild

## 학습 목표

- out-of-tree 모듈 두 개(`hello.ko`, `hello_user.ko`)를 Kbuild `Makefile` 하나로 빌드하고, `modinfo` · `Module.symvers` · `.mod.c` 에서 **depends · vermagic · parm** 이 어디서 오는지 짚는다.
- `module_param(count, int, 0644)` 가 만드는 `/sys/module/hello/parameters/count` 를 읽고 쓰고, 0644 · 0444 권한이 root에게도 그대로 적용됨을 본다.
- `EXPORT_SYMBOL_GPL` 로 내보낸 함수를 다른 모듈이 쓰면 **refcnt · holders · rmmod 거부**가 어떻게 생기는지 실측한다.
- 브링업에서 만나는 로드 실패(`Unknown symbol`, `version magic`, struct module 크기, `invalid for parameter`, Proprietary 라이선스)를 **실제 커널 메시지**로 읽고, `load_module()` 의 어느 단계에서 났는지 말한다.
- `depmod -b` 로 스테이징 rootfs를 색인하고 `modprobe` 가 의존 모듈을 먼저 올리는 것을 게스트에서 확인한다.

## 1. 문제

드라이버 면접의 첫 실습 질문은 거의 항상 "hello 모듈을 써 보라"이고, BSP를 처음 받으면 첫 주에 벤더 `.ko` 가 `Unknown symbol` · `version magic` · `Resource temporarily unavailable` 로 안 올라가는 장면을 만난다. 이 레슨은 그 장면들을 직접 만들고, 각 메시지가 커널의 어느 검사에서 나오는지 확인한다.

> 환경: 빌드는 Lima VM(aarch64 Ubuntu)에서 `/var/lab/out/linux-build`(Linux 6.18.55, defconfig 기반 랩 커널)로, 로드는 **QEMU TCG cortex-a57 -smp 1 -m 512M 게스트(Linux 6.18.55, BusyBox 1.37 initramfs)** 안에서만 한다. VM 자신의 커널에는 절대 insmod 하지 않는다. 아래 커널 출력은 전부 이 게스트에서 실제로 찍힌 것이다. 펌웨어 쪽 비유로 보면 모듈은 "부트 후에 런타임 링커로 붙이는 오버레이 이미지"이고, 커널이 그 링크를 매번 검사한다.

준비물은 소스 파일 하나에서 나오는 두 모듈이다.

- **hello.ko** (제공자): `count` (int, 0644), `name` (charp, 0444) 파라미터. 로드할 때 인사를 `count` 번 찍는다. `hello_greet()` 를 **GPL 전용**으로 내보낸다.
- **hello_user.ko** (사용자): init에서 `hello_greet(KBUILD_MODNAME)` 를 부른다. 이 호출 한 줄 때문에 modpost가 `depends=hello` 를 기록한다.

`Makefile` 이 `starter.c`/`solution.c` 를 `hello.c`, `hello_user.c` 로 복사하고 `CFLAGS_hello.o := -DHELLO_PART_CORE`, `CFLAGS_hello_user.o := -DHELLO_PART_USER` 로 반쪽씩 컴파일한다. 실제 트리라면 `hello.h` 하나와 `.c` 두 개일 구조를 레슨 diff를 위해 한 파일에 담은 것이다.

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">두 모듈을 묶는 것은 심볼 하나: 빌드 · 설치 · 로드 세 번 확인된다</text>
<rect x="10" y="35" width="240" height="95" class="f-acc" stroke="currentColor"/>
<text x="20" y="55" fill="currentColor" font-weight="bold">hello.ko (제공자)</text>
<text x="20" y="75" fill="currentColor" font-size="12" font-family="monospace">count: int, 0644</text>
<text x="20" y="95" fill="currentColor" font-size="12" font-family="monospace">name: charp, 0444</text>
<text x="20" y="115" fill="currentColor" font-size="12" font-family="monospace">EXPORT_SYMBOL_GPL(hello_greet)</text>
<rect x="490" y="35" width="240" height="95" class="f-acc" stroke="currentColor"/>
<text x="500" y="55" fill="currentColor" font-weight="bold">hello_user.ko (사용자)</text>
<text x="500" y="75" fill="currentColor" font-size="12" font-family="monospace">init: hello_greet(...)</text>
<text x="500" y="95" fill="currentColor" font-size="12" font-family="monospace">readelf: UND hello_greet</text>
<text x="500" y="115" fill="currentColor" font-size="12" font-family="monospace">modinfo: depends=hello</text>
<line x1="490" y1="82" x2="262" y2="82" stroke="currentColor"/>
<polygon points="252,82 264,76 264,88" fill="currentColor"/>
<text x="300" y="75" fill="currentColor" font-size="12">부른다 (심볼 참조)</text>
<rect x="10" y="150" width="160" height="40" class="f-mut" stroke="currentColor"/>
<text x="20" y="175" fill="currentColor">① 빌드: modpost</text>
<text x="185" y="166" fill="currentColor" font-size="12" font-family="monospace">Module.symvers: hello_greet  hello  EXPORT_SYMBOL_GPL</text>
<text x="185" y="184" fill="currentColor" font-size="12" font-family="monospace">hello_user.mod.c: MODULE_INFO(depends, "hello");</text>
<rect x="10" y="205" width="160" height="40" class="f-mut" stroke="currentColor"/>
<text x="20" y="230" fill="currentColor">② 설치: depmod -b</text>
<text x="185" y="230" fill="currentColor" font-size="12" font-family="monospace">modules.dep: extra/hello_user.ko: extra/hello.ko</text>
<rect x="10" y="260" width="160" height="40" class="f-ok" stroke="currentColor"/>
<text x="20" y="285" fill="currentColor">③ 로드: 커널</text>
<text x="185" y="276" fill="currentColor" font-size="12">hello 먼저 → 심볼 해석 → hello refcnt 1, holders/hello_user</text>
<text x="185" y="294" fill="currentColor" font-size="12">순서 반대 → hello_user: Unknown symbol hello_greet (err -2)</text>
</svg>
```

### Part A — 생명주기, 파라미터, export

- A1. `count` 를 `module_param(count, int, 0644)`, `name` 을 `module_param(name, charp, 0444)` 로 노출하고 `MODULE_PARM_DESC` 를 단다.
- A2. `hello_greet(who)` — 호출 횟수를 원자적으로 올리고 `greet #n from <who> (count=<현재 count>)` 를 찍고 n을 돌려준다.
- A3. `hello_greet` 를 **GPL 호환 모듈에만** 내보낸다.
- A4. `hello_init()` — `count` 가 1..10 밖이면 `-EINVAL`, 아니면 `hello <name> i/count` 를 count 줄 찍는다.
- A5. `hello_exit()` — `bye (greets=N)`.
- A6. `hello_user` 의 init — `hello_greet(KBUILD_MODNAME)` 를 부르고 반환값을 찍는다.
- 게스트 검사: `insmod hello.ko count=3 name=don` → 3줄, sysfs 값 · 권한, `echo 7 > count` 후 `hello_user` 가 **count=7을 본다**, refcnt 1, holders, `rmmod hello` 거부, 순서대로 내리면 `bye (greets=1)`.

### Part B — 로드 실패 일곱 가지 (브링업에서 실제로 보는 것)

| # | 무엇을 했나 | 기대 (실제 텍스트는 §5) |
|---|---|---|
| B1/B2 | `hello_user.ko` 를 먼저 insmod | `Unknown symbol hello_greet (err -2)` |
| B3 | `count=0`, `count=11` | init이 `-EINVAL`, 아무것도 안 남음 |
| B4 | `count=abc` | 파라미터 파싱 실패 `` `abc' invalid for parameter `count' `` |
| B5 | 오타 `cnt=3` | **로드는 성공**, 경고만 `unknown parameter 'cnt' ignored` |
| B6 | 다른 트리(`linux-build-sub`, 6.18.55-sub)로 빌드한 hello | `version magic ... should be ...` |
| B7 | 디버그 트리(`linux-build-dbg`)로 빌드한 hello | vermagic보다 먼저 `struct module` 크기 검사에서 거부 |
| B8/B9 | `MODULE_LICENSE("Proprietary")` 인 `demo_prop.ko` 가 `hello_greet` 사용 | 심볼을 못 찾음 + 실패했는데도 커널이 `P` 로 오염 |

VM 쪽에서도 같은 규칙을 빌드 때 검사한다: `Module.symvers` 의 `EXPORT_SYMBOL_GPL`, `modinfo` 의 `depends`/`parm`/`vermagic`, 그리고 modpost가 Proprietary 모듈을 거부하는지 (H1–H6).

### Part C — modprobe와 depmod: 보드 rootfs에 넣는 방식

- 스테이징 rootfs의 `/lib/modules/6.18.55/extra/` 에 두 `.ko` 를 넣고, VM의 kmod `depmod -b <rootfs> 6.18.55` 로 색인한다 (Buildroot/Yocto가 하는 일과 같다).
- 게스트에서 `modprobe hello_user` → hello가 먼저 올라오는지, `modprobe -r hello_user` → 둘 다 내려가는지, BusyBox의 옵션 파일 `/etc/modules/hello` 가 적용되는지 확인한다.

## 2. Problem (English)

> Write two out-of-tree Linux kernel modules and their Kbuild makefile.
>
> ```c
> /* hello.ko */
> static int count = 1;      /* module parameter, int, mode 0644, valid 1..10 */
> static char *name = "world"; /* module parameter, charp, mode 0444 */
> int hello_greet(const char *who); /* exported GPL-only; returns the running call count */
>
> /* hello_user.ko */
> /* init calls hello_greet(KBUILD_MODNAME) and logs the return value */
> ```
>
> Requirements: `hello` prints `hello <name> i/<count>` for i = 1..count at load and refuses to load (`-EINVAL`) when count is outside 1..10; `hello_greet()` logs the *current* value of `count`, so a write to `/sys/module/hello/parameters/count` is visible to the next caller; `hello_user` must not load before `hello`, and `hello` must not unload while `hello_user` is loaded.
>
> Verification (inside a QEMU guest running Linux 6.18.55): `insmod hello.ko count=3 name=don` prints exactly 3 lines; after `echo 7 > .../count`, `insmod hello_user.ko` logs `greet #1 from hello_user (count=7)`; `/sys/module/hello/refcnt` is 1; `rmmod hello` fails; `modprobe hello_user` loads both from `/lib/modules/6.18.55/extra` after `depmod -b`.
>
> Follow-ups: what decides the load order? Why does a module built against another tree of the *same* kernel version fail? What does `EXPORT_SYMBOL_GPL` change for a module that declares a non-GPL license?

## 3. 시작 코드

파일 구성:

| 파일 | 역할 |
|---|---|
| `starter.c` / `solution.c` / `practice.c` | 모듈 소스 (한 파일 = hello + hello_user, practice는 hello_opt 추가) |
| `Makefile` | `VARIANT=` 로 소스 선택, `KDIR=` 로 커널 빌드 트리 선택 |
| `demo_prop.c` | 라이선스만 `"Proprietary"` 인 사용자 모듈 (B8/B9, H6) |
| `guest_test.sh` | 게스트 안의 `/test.sh`: PASS/FAIL 줄 출력 |
| `check.sh` | VM에서 빌드 → VM 쪽 검사 → initramfs → QEMU → 판정 |
| `solution_output.txt`, `demo_output.txt` | 아래에 인용한 실제 실행 기록 |

Mac에서 한 줄씩 그대로 붙여 넣는다.

```bash
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B06/check.sh starter
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B06/check.sh solution
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B06/check.sh demo
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B06/check.sh shell
```

`demo` 는 modpost 빌드 오류 두 개와 라이선스를 지운 `.ko` 의 로드를 보여 주고, `shell` 은 solution 모듈을 넣은 게스트를 대화형으로 띄운다 (`insmod /hello.ko count=3; lsmod` 를 직접 쳐 보고 `Ctrl-A x` 로 끝낸다). 한 번 도는 데 VM 부하에 따라 7–16초 걸렸다 (빌드 4개 + TCG 부팅 1회, 측정은 참고용).

<!-- code: Makefile -->

<!-- code: starter.c -->

<!-- code: guest_test.sh -->

<!-- code: check.sh -->

`check.sh starter` 의 실측: 빌드와 로드는 되지만 `11/36 passed`, `RESULT: FAIL (starter)`, exit 1. 파라미터가 없으니 `insmod hello.ko count=3` 은 `hello: unknown parameter 'count' ignored` 로 **성공해 버리고**, export가 없으니 hello_user는 hello에 의존하지 않는다.

## 4. 힌트

<details><summary>힌트 1 — module_param 은 어디에 쓰나</summary>

함수 안이 아니라 **파일 범위**에 쓴다. 변수 선언 바로 아래에 `module_param(이름, 타입, 권한)`. 타입은 `int`, `charp`, `bool`, `uint` 같은 이름이고 권한은 sysfs 파일 모드다. 0이면 sysfs에 안 보이고 insmod 인자로만 받는다.
</details>

<details><summary>힌트 2 — export 한 줄과 modpost</summary>

함수 정의 바로 뒤에 `EXPORT_SYMBOL_GPL(hello_greet);`. 이 줄이 없으면 hello_user가 `hello_greet` 를 부르는 순간 modpost가 `"hello_greet" [hello_user.ko] undefined!` 로 빌드를 멈춘다. 그래서 starter의 hello_user는 아직 아무것도 부르지 않는다. 두 TODO(A3, A6)를 함께 채워야 한다.
</details>

<details><summary>힌트 3 — init이 실패하면</summary>

`module_init` 함수가 음수를 돌려주면 커널은 모듈을 **올리지 않고** 메모리를 회수한다. exit 함수는 불리지 않는다. 그러니 init 안에서 실패 전에 만든 것은 init 안에서 되돌려야 한다 (이 레슨은 만든 게 없어서 바로 `return -EINVAL`).
</details>

<details><summary>힌트 4 — "count=7을 본다"를 어떻게 만족하나</summary>

sysfs 쓰기는 **같은 변수 `count`** 를 바꾼다. `hello_greet()` 가 로그를 찍을 때 `count` 를 그 자리에서 읽으면 된다. 다른 CPU의 쓰기와 겹칠 수 있는 단순 int 읽기이므로 `READ_ONCE(count)` 로 한 번만 읽는다.
</details>

<details><summary>힌트 5 — 게스트에서 rmmod 성공 여부</summary>

BusyBox `rmmod` 는 실패해도 **종료 코드 0** 을 돌려준다 (`modprobe-small.c` 의 rmmod 경로가 `EXIT_SUCCESS` 를 돌려준다). 그래서 `guest_test.sh` 는 반환값이 아니라 `/proc/modules` 에 남았는지로 판정한다.
</details>

## 5. 풀이

### A1 — module_param 이 실제로 만드는 것

```c
module_param(count, int, 0644);
MODULE_PARM_DESC(count, "greetings printed at load, 1..10 (default 1)");
module_param(name, charp, 0444);
```

`include/linux/moduleparam.h` 를 따라가면 매크로 한 줄이 세 가지를 만든다.

| 단계 | 매크로 (6.18.55) | 결과 |
|---|---|---|
| 타입 검사 | `param_check_int(count, &count)` | `count` 가 int가 아니면 컴파일 오류 |
| 등록 | `module_param_cb(count, &param_ops_int, &count, 0644)` | `__param` 섹션에 `struct kernel_param` (`nm hello.ko`: `__param_count`, `__param_name`) |
| 설명 | `__MODULE_PARM_TYPE(count, "int")` + `MODULE_PARM_DESC` | `.modinfo` 의 `parmtype=count:int`, `parm=count:...` → `modinfo` 의 `parm: count:... (int)` |
| 권한 검사 | `VERIFY_OCTAL_PERMISSIONS(0644)` (`include/linux/kernel.h`) | 남(other)에게 쓰기 권한(0002)을 주면 **빌드 오류** |

로드 순서는 `load_module()` → `parse_args()` (insmod 인자 → 변수에 대입) → `mod_sysfs_setup()` (`/sys/module/hello/parameters/*` 생성) → `do_init_module()` (내 init). 그래서 **init이 돌 때 `count` 는 이미 3이다.**

권한은 root에게도 그대로 적용된다. `fs/kernfs/file.c` 의 `kernfs_fop_open()` 이 쓰기 모드로 열 때 `S_IWUGO` 비트가 없으면 `-EACCES` 를 돌려준다 (일반 파일처럼 root의 DAC override가 통하지 않는다). 실측:

```text
--- modes: count -rw-r--r-- name -r--r--r--
--- echo bob > name:
/guest_test.sh: line 34: can't create /sys/module/hello/parameters/name: Permission denied
```

### A2–A3 — export 한 줄이 남기는 흔적

```c
int hello_greet(const char *who)
{
	int n = atomic_inc_return(&greets);

	pr_info("greet #%d from %s (count=%d)\n", n, who, READ_ONCE(count));
	return n;
}
EXPORT_SYMBOL_GPL(hello_greet);
```

6.18의 export는 두 단계다. 소스의 `EXPORT_SYMBOL_GPL` 은 `.export_symbol` 섹션에 표시만 남기고 (`include/linux/export.h`), **modpost** 가 그것을 읽어 `hello.mod.c` 에 `KSYMTAB_FUNC(hello_greet, "_gpl", "");` 를 써 넣는다. 그 결과가 `.ko` 의 `__ksymtab_gpl` 섹션이다. 같은 modpost가 hello_user의 미해결 심볼을 보고 `hello_user.mod.c` 에 `MODULE_INFO(depends, "hello");` 를 쓴다. VM에서 실제로 본 것:

```text
$ cat Module.symvers
0x00000000	hello_greet	hello	EXPORT_SYMBOL_GPL
$ readelf -sW hello_user.ko | grep hello_greet
    55: 0000000000000000     0 NOTYPE  GLOBAL DEFAULT  UND hello_greet
$ modinfo hello_user.ko | grep -E '^(depends|vermagic)'
depends:        hello
vermagic:       6.18.55 SMP preempt mod_unload aarch64
```

CRC가 `0x00000000` 인 것은 이 랩 커널이 `CONFIG_MODVERSIONS` 를 끄고 빌드되어서다 (`.config` 확인). 켜져 있으면 여기 심볼별 CRC가 들어가고, 로드 때 vermagic과 별개로 심볼 단위 검사를 한다 (Q3).

`atomic_inc_return` 은 여러 사용자 모듈이 동시에 부를 수 있어서다. 락 선택 자체는 ELX-M04 소관이다.

### A4–A6 — init/exit와 생명주기

```c
static int __init hello_init(void)
{
	...
	if (count < 1 || count > HELLO_MAX_COUNT) {
		pr_err("count=%d out of range 1..%d\n", count, HELLO_MAX_COUNT);
		return -EINVAL;
	}
	for (i = 1; i <= count; i++)
		pr_info("hello %s %d/%d\n", name, i, count);
	return 0;
}
```

`__init` 함수는 `.init.text` 섹션에 들어가고(`readelf -S hello.ko`: `[16] .init.text`), `do_init_module()` 이 성공 후 `init_free_wq` 로 그 메모리를 버린다. `__exit` 는 `.exit.text`. `pr_fmt` 를 `KBUILD_MODNAME ": " fmt` 로 정의해서 모든 줄이 `hello:` 로 시작한다 (Kbuild가 `-DKBUILD_MODNAME='"hello"'` 를 넘긴다). `hello_user` 를 같은 소스에서 빌드해도 접두어가 `hello_user:` 로 바뀌는 이유다.

게스트 실측 (Part A 부분, `solution_output.txt`):

```text
--- insmod hello.ko count=3 name=don (rc=0)
hello: loading out-of-tree module taints kernel.
hello: hello don 1/3
hello: hello don 2/3
hello: hello don 3/3
PASS: A3 tainted = 4096 (O: out-of-tree module)
--- insmod hello_user.ko (rc=0)
hello: greet #1 from hello_user (count=7)
hello_user: hello_greet returned 1
--- lsmod:
hello_user 12288 0 - Live 0xffffa87da9e03000 (O)
hello 12288 1 hello_user, Live 0xffffa87da9dfd000 (O)
--- refcnt=1 holders=hello_user
--- rmmod hello (in use):
rmmod: remove 'hello': Resource temporarily unavailable
--- rmmod hello_user; rmmod hello
hello_user: unloaded
hello: bye (greets=1)
```

읽는 법:

- `tainted = 4096` = 비트 12 = `TAINT_OOT_MODULE` (`include/linux/panic.h`). `.modinfo` 에 `intree=Y` 가 없는 첫 모듈이 올라올 때 한 번 찍힌다 (`module_augment_kernel_taints()`).
- BusyBox `lsmod` 는 `/proc/modules` 를 그대로 보여 준다: 이름, 크기, **refcnt**, 사용하는 모듈 목록(`hello_user,`), 상태, 주소, taint.
- refcnt 1은 `kernel/module/main.c` 의 `ref_module()` 이 만든다: hello_user의 심볼을 해석할 때 `strong_try_module_get(hello)` + `add_module_usage()` (hello의 `source_list` 에 hello_user 추가 → `holders/hello_user`).
- `rmmod hello` 거부는 `delete_module` 시스템 콜에서 `if (!list_empty(&mod->source_list)) ret = -EWOULDBLOCK;` → 문자열 "Resource temporarily unavailable".

### 로드 경로 한 장: 어느 검사에서 떨어졌나

`load_module()` (6.18.55, `kernel/module/main.c`) 의 순서를 소스에서 읽고, 각 단계에서 이 레슨이 일부러 떨어뜨린 실측 메시지를 붙였다.

```svg
<svg viewBox="0 0 740 440" xmlns="http://www.w3.org/2000/svg" font-size="12">
<text x="10" y="20" fill="currentColor" font-size="14" font-weight="bold">insmod → finit_module → load_module(): 위에서부터 검사, 떨어진 곳에서 멈춤</text>
<rect x="10" y="35" width="260" height="40" class="f-mut" stroke="currentColor"/>
<text x="20" y="60" fill="currentColor">1 ELF 검사: this_module 크기</text>
<text x="285" y="52" fill="currentColor">dbg 트리 .ko: 0x540 바이트 ≠ 이 커널 0x480</text>
<text x="285" y="68" fill="currentColor">→ ENOEXEC "invalid module format" (B7)</text>
<rect x="10" y="85" width="260" height="40" class="f-mut" stroke="currentColor"/>
<text x="20" y="110" fill="currentColor">2 early_mod_check: vermagic</text>
<text x="285" y="102" fill="currentColor" font-family="monospace">'6.18.55-sub ...' should be '6.18.55 ...'</text>
<text x="285" y="118" fill="currentColor">→ ENOEXEC "invalid module format" (B6)</text>
<rect x="10" y="135" width="260" height="40" class="f-warn" stroke="currentColor"/>
<text x="20" y="160" fill="currentColor">3 taint: O(4096), P(1)</text>
<text x="285" y="152" fill="currentColor">Proprietary → "taints kernel", 실패해도 남는다</text>
<text x="285" y="168" fill="currentColor">tainted 4096 → 4097 (B9)</text>
<rect x="10" y="185" width="260" height="40" class="f-mut" stroke="currentColor"/>
<text x="20" y="210" fill="currentColor">4 simplify_symbols + ref_module</text>
<text x="285" y="202" fill="currentColor" font-family="monospace">Unknown symbol hello_greet (err -2)</text>
<text x="285" y="218" fill="currentColor">hello 없음(B2) 또는 P 모듈이 GPL 심볼 요청(B8)</text>
<rect x="10" y="235" width="260" height="40" class="f-mut" stroke="currentColor"/>
<text x="20" y="260" fill="currentColor">5 parse_args: insmod 인자</text>
<text x="285" y="252" fill="currentColor" font-family="monospace">`abc' invalid for parameter `count'</text>
<text x="285" y="268" fill="currentColor">cnt=3 → "unknown parameter 'cnt' ignored" (통과!)</text>
<rect x="10" y="285" width="260" height="40" class="f-mut" stroke="currentColor"/>
<text x="20" y="310" fill="currentColor">6 mod_sysfs_setup</text>
<text x="285" y="310" fill="currentColor" font-family="monospace">/sys/module/hello/parameters/{count,name}</text>
<rect x="10" y="335" width="260" height="40" class="f-mut" stroke="currentColor"/>
<text x="20" y="360" fill="currentColor">7 do_init_module → hello_init()</text>
<text x="285" y="352" fill="currentColor">count=0 → -EINVAL → BusyBox "invalid parameter"</text>
<text x="285" y="368" fill="currentColor">(파싱 실패와 같은 errno: dmesg로 구분)</text>
<rect x="10" y="385" width="260" height="40" class="f-ok" stroke="currentColor"/>
<text x="20" y="410" fill="currentColor">8 LIVE</text>
<text x="285" y="402" fill="currentColor">refcnt · holders 로 rmmod 가능 여부가 정해진다</text>
<text x="285" y="418" fill="currentColor">hello: refcnt 1 → "Resource temporarily unavailable"</text>
</svg>
```

### Part B — 실측 메시지와 해석

모두 `check.sh solution` 한 번의 부팅에서 나온 줄이다 (`solution_output.txt`).

| # | 사용자 공간 (BusyBox insmod) | 커널 (dmesg) | 단계 |
|---|---|---|---|
| B2 | `can't insert '/hello_user.ko': unknown symbol in module or invalid parameter` | `hello_user: Unknown symbol hello_greet (err -2)` | 4 |
| B3 | `can't insert '/hello.ko': invalid parameter` | `hello: count=0 out of range 1..10` | 7 |
| B4 | `can't insert '/hello.ko': invalid parameter` | ``hello: `abc' invalid for parameter `count'`` | 5 |
| B5 | (성공, rc=0) | `hello: unknown parameter 'cnt' ignored` | 5 |
| B6 | `can't insert '/hello_sub.ko': invalid module format` | `hello: version magic '6.18.55-sub SMP preempt mod_unload aarch64' should be '6.18.55 SMP preempt mod_unload aarch64'` | 2 |
| B7 | `can't insert '/hello_dbg.ko': invalid module format` | `module hello: .gnu.linkonce.this_module section size must match the kernel's built struct module size at run time` | 1 |
| B8/B9 | `can't insert '/demo_prop.ko': unknown symbol in module or invalid parameter` | `demo_prop: module license 'Proprietary' taints kernel.` … `demo_prop: Unknown symbol hello_greet (err -2)` | 3, 4 |

짚을 점:

1. **사용자 공간 메시지는 errno 번역일 뿐이다.** BusyBox `modprobe-small.c` 의 `moderror()` 는 `ENOENT` → "unknown symbol in module or invalid parameter", `EINVAL` → "invalid parameter". 그래서 init이 `-EINVAL` 을 돌려준 B3와 파싱 실패 B4가 **똑같이 보인다**. 원인은 항상 `dmesg` 에서 찾는다.
2. **B5가 가장 위험하다.** 오타 파라미터는 `kernel/module/main.c` 의 `unknown_module_param_cb()` 가 경고만 찍고 0을 돌려주므로 모듈은 기본값(count=1)으로 **정상 로드된다**. 보드 bring-up에서 "옵션을 줬는데 안 먹는다"의 단골 원인이다.
3. **B7은 vermagic보다 먼저 떨어진다.** dbg 트리는 lockdep 등이 켜져 `struct module` 이 커서 `.gnu.linkonce.this_module` 가 0x540 바이트, 랩 Image의 것은 0x480이다 (`readelf -SW` 로 확인). `elf_validity_cache_copy()` 가 `early_mod_check()` (vermagic) 보다 앞이다. 같은 "6.18.55"라도 **설정이 다른 트리로 빌드한 모듈은 못 쓴다**는 것을 두 가지 메시지로 본 셈이다.
4. **B8/B9: 라이선스 검사는 두 겹이다.** 빌드 때 modpost가 hello의 export를 볼 수 있으면 `ERROR: modpost: GPL-incompatible module demo_prop.ko uses GPL-only symbol 'hello_greet'` 로 빌드를 막는다 (H6). 일부러 hello를 못 보게 따로 빌드하면 (`KBUILD_MODPOST_WARN=1`, 경고 `"hello_greet" [demo_prop.ko] undefined!`) 로드 때 커널이 막는다: Proprietary 모듈은 `resolve_symbol()` 에서 `.gplok = false` 로 찾으므로 `find_exported_symbol_in_section()` 이 GPL 섹션을 건너뛰고, 결과는 그냥 `Unknown symbol` 이다. 커널 메시지에 "GPL"이라는 말은 없다. 그리고 taint는 3단계에서 이미 걸렸으므로 **로드가 실패해도 `tainted=4097` 이 남는다**.

`MODULE_LICENSE` 가 아예 없으면? 6.18의 modpost는 `ERROR: modpost: missing MODULE_LICENSE() in nolic.o` 로 빌드를 멈춘다 (`check.sh demo`). 런타임 동작을 보려고 `demo` 는 hello_user.ko 복사본의 `license=GPL` 문자열을 바이너리로 지웠다. 결과 (`demo_output.txt`):

```text
--- insmod hello_nolic.ko
insmod: can't insert '/hello_nolic.ko': unknown symbol in module or invalid parameter
rc=1
tainted=4097
...
hello_user: module license 'unspecified' taints kernel.
Disabling lock debugging due to kernel taint
hello_user: module license taints kernel.
hello_user: Unknown symbol hello_greet (err -2)
```

라이선스가 없으면 커널은 `"unspecified"` 로 보고 Proprietary와 똑같이 취급한다 (`module_license_taint_check()`).

### Part C — depmod -b와 modprobe

VM 쪽 (`check.sh` 가 스테이징 rootfs에 대해 실행):

```text
$ depmod -b $W/rootfs 6.18.55
$ cat $W/rootfs/lib/modules/6.18.55/modules.dep
extra/hello.ko:
extra/hello_user.ko: extra/hello.ko
```

`-b` 는 "이 디렉터리를 `/` 로 보고 `lib/modules/<ver>` 를 색인하라"이다. **커널 버전 인자를 꼭 준다**: 안 주면 VM 자신의 `uname -r` (7.0)로 찾는다. modules.order · modules.builtin이 없다는 경고 3줄이 나오는데 (`make modules_install` 이 아니라 손으로 복사해서) 색인 결과에는 영향이 없다.

게스트 쪽:

```text
--- modprobe hello_user (rc=0)
hello: hello world 1/1
hello: greet #1 from hello_user (count=1)
hello_user: hello_greet returned 1
PASS: C1 modprobe hello_user pulls in hello first (depends=hello)
PASS: C2 modprobe -r hello_user removes both
--- modprobe hello with /etc/modules/hello = 'count=4 name=etc'
hello: hello etc 1/4
...
hello: hello etc 4/4
--- modprobe nosuch:
modprobe: module 'nosuch' not found
```

**주의: 이 게스트의 modprobe는 kmod가 아니다.** 랩 BusyBox는 `CONFIG_MODPROBE_SMALL=y` 로 빌드되어 있고 (`/var/lab/build/busybox/.config`), `modprobe-small.c` 는 kmod의 `modules.dep` 를 읽지 않는다. 대신 자체 색인 `modules.dep.bb` 가 있으면 그것을, 없으면 디렉터리의 `.ko` 를 훑어 `.modinfo` 의 `depends=` 를 직접 읽는다. 옵션 파일도 kmod의 `/etc/modprobe.d/*.conf` (`options hello count=4`) 가 아니라 `/etc/modules/<name>` 이다. 결론은 같다: **의존성 정보의 원천은 modpost가 쓴 `depends=`** 이고, depmod는 그것을 빠르게 찾게 해 주는 색인이다. 데스크톱/Yocto 이미지의 kmod `modprobe` 는 `modules.dep.bin` 을 읽는다.

### 실행 결과 (solution)

```text
=== VM side (6.18.55 tree) ===
PASS: H1 Module.symvers: hello_greet is EXPORT_SYMBOL_GPL
PASS: H2 modinfo hello_user.ko: depends=hello
PASS: H3 modinfo hello.ko: parm count (int) and name (charp)
PASS: H4 vermagic = '6.18.55 SMP preempt mod_unload aarch64'
PASS: H5 depmod -b: modules.dep has 'extra/hello_user.ko: extra/hello.ko'
PASS: H6 modpost refuses Proprietary demo_prop: 'uses GPL-only symbol 'hello_greet''
=== guest (Linux 6.18.55, QEMU TCG cortex-a57 -smp 1) ===
PASS: A1 insmod hello.ko count=3 name=don succeeds
...
PASS: B9 the attempt alone tainted the kernel: 'license 'Proprietary' taints', tainted = 4097
DMESG_BAD=0
GUEST_DONE
36/36 passed, dmesg BUG/WARNING/Oops lines: 0
RESULT: PASS (solution)
```

판정은 PASS/FAIL 줄 수, `GUEST_DONE`, 그리고 게스트 dmesg 전체에서 `BUG:|WARNING:|Oops|Call trace|refcount_t` 줄 수(`DMESG_BAD`)만 본다. 시간은 판정에 쓰지 않는다. solution은 연속 3회 모두 `36/36`, exit 0이었다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

바뀐 곳은 TODO A1–A6 자리뿐이다: 파라미터 4줄, `hello_greet` 본문 + `EXPORT_SYMBOL_GPL`, init의 범위 검사와 출력 루프, exit의 `pr_info`, hello_user의 호출. starter에 있던 `(void)count;` 같은 줄은 "선언만 하고 안 쓰면 `-Werror` 에서 `defined but not used`" 를 피하려던 자리 표시다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `module_param` 을 init 함수 안에 씀 | 파일 범위에서 `__param` 섹션 항목을 만드는 매크로. 함수 안에서는 컴파일 오류 | 변수 선언 바로 아래, 파일 범위 |
| 파라미터 이름 오타를 insmod가 잡아 줄 거라 믿음 | `unknown parameter 'cnt' ignored` 경고만, 로드는 성공 (B5) | dmesg 확인, 또는 `modinfo -p` 로 이름 확인 |
| sysfs 쓰기도 init처럼 검사된다고 생각 | `module_param(int)` 은 `kstrtoint` 만 한다. `echo 0 > count` 가 그대로 들어간다 | `module_param_cb` + `.set` 에서 범위 검사 (추가 연습 P1) |
| 권한을 0666으로 | `VERIFY_OCTAL_PERMISSIONS` 가 other-writable을 빌드 오류로 막는다 | 0644 (root만 쓰기) 또는 0444 |
| `EXPORT_SYMBOL` 을 잊고 `KBUILD_MODPOST_WARN=1` 로 우회 | 빌드는 되지만 로드 때 `Unknown symbol` | 제공 모듈에 export, 사용 모듈과 같은 디렉터리나 `KBUILD_EXTRA_SYMBOLS` 로 빌드 |
| 다른 트리로 빌드한 .ko를 "버전이 같으니까" 넣음 | vermagic 또는 `struct module` 크기 불일치 (B6, B7) | 대상 커널의 **그** 빌드 트리(`.config` + `Module.symvers`)로 빌드 |
| init 실패 뒤 exit에서 정리하려 함 | init이 음수를 돌려주면 exit는 불리지 않는다 | init 안에서 goto 언와인드 |
| BusyBox `rmmod` 의 종료 코드로 성공 판단 | modprobe-small의 rmmod 경로는 실패해도 0 | `/proc/modules` 로 확인 |
| `depmod -b rootfs` 에 버전 생략 | 호스트(VM)의 `uname -r` 디렉터리를 찾는다 | `depmod -b rootfs 6.18.55` |

## 8. 면접 Q&A

<details><summary>Q1. insmod와 modprobe는 무엇이 다른가요?</summary>

`insmod` 는 파일 하나를 `finit_module()`(또는 `init_module()`) 으로 넘길 뿐 의존성을 모른다. 그래서 hello_user를 먼저 넣으면 `Unknown symbol hello_greet (err -2)`. `modprobe` 는 이름으로 찾고, 의존 모듈을 먼저 올리고, 설정 파일의 옵션을 붙인다. kmod는 `depmod` 가 만든 `modules.dep(.bin)` 을, BusyBox modprobe-small은 `.modinfo` 의 `depends=` 를 직접(또는 `modules.dep.bb`) 쓴다. 이 레슨의 C1.

**꼬리질문: 그 의존성은 누가 처음 아나요?** modpost다. 미해결 심볼이 다른 모듈의 export로 해결되면 `.mod.c` 에 `MODULE_INFO(depends, "hello")` 를 쓴다. depmod는 그것과 심볼 테이블로 색인을 만든다.

**꼬리질문: 보드 이미지에서 depmod는 언제 도나요?** 빌드 호스트에서 `make modules_install INSTALL_MOD_PATH=...` 끝에 `depmod -b` 로 (Buildroot·Yocto 모두), 또는 첫 부팅 때 타깃에서. 버전 인자를 빠뜨리면 호스트 버전 디렉터리를 찾는다.
</details>

<details><summary>Q2. EXPORT_SYMBOL과 EXPORT_SYMBOL_GPL의 차이는 어디서 강제되나요?</summary>

두 곳이다. ① 빌드 때 modpost가 GPL 비호환 라이선스 모듈이 GPL 전용 심볼을 쓰면 `ERROR: modpost: GPL-incompatible module demo_prop.ko uses GPL-only symbol 'hello_greet'` (H6). ② 로드 때 커널이 Proprietary taint를 가진 모듈의 심볼 검색에서 GPL 섹션을 건너뛴다 (`find_exported_symbol_in_section()` 의 `!fsa->gplok && syms->license == GPL_ONLY`). 결과는 `Unknown symbol ... (err -2)` 이고 taint는 남는다 (B8/B9).

**꼬리질문: GPL 호환으로 인정되는 라이선스 문자열은?** `include/linux/license.h` 의 `license_is_gpl_compatible()`: "GPL", "GPL v2", "GPL and additional rights", "Dual BSD/GPL", "Dual MIT/GPL", "Dual MPL/GPL". "Proprietary"나 빈 값("unspecified")은 아니다.

**꼬리질문: 네임스페이스 export는?** `EXPORT_SYMBOL_NS_GPL(sym, "NS")` 로 내보내면 사용하는 쪽이 `MODULE_IMPORT_NS("NS")` 를 해야 한다. 안 하면 modpost가 `module X uses symbol Y from namespace NS, but does not import it.` 를 낸다 (modpost.c의 `check_exports()` 에서 확인, 이 레슨에서 실행은 안 함).
</details>

<details><summary>Q3. "invalid module format"을 보면 무엇부터 의심하나요?</summary>

`ENOEXEC` 이므로 커널이 파일 자체를 거부한 것이다. dmesg를 본다. ① `version magic 'X' should be 'Y'` → 다른 커널(또는 다른 `LOCALVERSION`)의 트리로 빌드 (B6). ② `.gnu.linkonce.this_module section size must match` → 같은 버전이라도 `.config` 가 달라 `struct module` 배치가 다름 (B7, 0x540 vs 0x480). ③ 아키텍처가 다른 .ko → ELF 헤더 검사의 `elf_check_arch()` 에서 `Invalid architecture in ELF header: <e_machine>` (소스에서 확인, 실행은 안 함). 해결은 항상 "대상 커널의 그 빌드 트리로 다시 빌드"다.

**꼬리질문: vermagic 문자열은 무엇으로 이루어지나요?** `include/linux/vermagic.h` 의 `VERMAGIC_STRING` = `UTS_RELEASE` + `SMP` + 선점 모델(`preempt`/`preempt_rt`) + `mod_unload` + `modversions` + 아키텍처. 이 레슨에서는 `6.18.55 SMP preempt mod_unload aarch64`.

**꼬리질문: MODVERSIONS가 켜져 있으면?** 심볼마다 CRC가 `Module.symvers` 와 `__versions` 섹션에 들어가서 `modprobe --force-vermagic` 으로 vermagic을 무시해도 심볼 단위로 ABI를 검사한다. 이 랩 커널은 꺼져 있어 CRC가 0이다.
</details>

<details><summary>Q4. refcnt와 holders는 같은 건가요? rmmod는 무엇을 보고 거부하나요?</summary>

다르다. holders(`/sys/module/hello/holders/`, `/proc/modules` 의 "Used by")는 **심볼로 링크된 모듈 목록**(`source_list`)이고, refcnt는 `try_module_get()` 횟수다. 정적 링크(`ref_module`)는 둘 다 올린다 (A12/A13: refcnt 1, holders/hello_user). `delete_module` 은 먼저 `source_list` 가 비었는지(아니면 `-EWOULDBLOCK`), 그다음 refcnt가 0인지를 본다. 추가 연습 P3의 `symbol_get()` 은 refcnt만 1로 만들고 holders는 비어 있는데, 그래도 rmmod가 거부된다 (실측).

**꼬리질문: 드라이버에서 refcnt를 올리는 흔한 방법은?** `file_operations.owner = THIS_MODULE` 이다. 장치 파일이 열려 있는 동안 VFS가 `try_module_get(owner)` 를 해 두어서 사용 중인 드라이버가 내려가지 않는다 (ELX-B07).

**꼬리질문: 강제로 내릴 수는?** `rmmod -f` 는 `CONFIG_MODULE_FORCE_UNLOAD` 가 있어야 하는데 이 랩 커널은 꺼져 있다 (`.config` 확인). 켜져 있어도 사용 중인 코드를 날리는 것이라 디버그 전용이다.
</details>

<details><summary>Q5. module_param의 권한 0644, 0444, 0은 각각 언제 쓰나요?</summary>

0644: 런타임에 root가 튜닝할 값 (디버그 레벨, 임계값). 0444: 로드 때만 정하고 보기만 할 값 (이름, 보드 ID). 0: sysfs에 안 보이고 insmod 인자로만. other-writable은 `VERIFY_OCTAL_PERMISSIONS` 가 빌드 오류로 막고, 0444 파일은 root도 못 쓴다 (`kernfs_fop_open()` → `Permission denied`, A8).

**꼬리질문: sysfs로 바꾼 값은 검증되나요?** `param_ops_int` 의 `.set` 은 `kstrtoint` 만 한다. 범위 검사는 init에만 있으므로 `echo 0 > count` 가 그대로 들어간다. 검증하려면 `module_param_cb` 로 자체 `.set` 을 단다 (P1: `echo 42 > count` → `sh: write error: Invalid argument`).

**꼬리질문: 모듈을 built-in(y)으로 넣으면 파라미터는?** 커널 커맨드라인의 `hello.count=3` 이 되고 `/sys/module/hello/parameters/` 도 그대로 생긴다. 모듈일 때 커맨드라인의 `hello.count=` 는 kmod modprobe가 `/proc/cmdline` 에서 읽어 넘겨 주는 방식이다 (이 레슨에서 실행은 안 함, ELX-B03의 y/m 참고).
</details>

<details><summary>Q6. __init과 __exit는 무엇을 하나요? init이 실패하면?</summary>

`__init` 은 함수를 `.init.text` 에 넣고, 로드 성공 후 `do_init_module()` 이 그 메모리를 해제한다 (모듈은 `init_free_wq`, built-in은 부팅 끝의 "Freeing unused kernel memory"). `__exit` 는 `.exit.text` 이고 built-in이면 아예 버려진다. init이 음수를 돌려주면 모듈은 LIVE가 되지 못하고 exit도 불리지 않는다 (B3: 아무것도 안 남음). 양수를 돌려주면 `'%s'->init suspiciously returned %d, it should follow 0/-E convention` 경고 후 그냥 로드된다 (`do_init_module()` 소스에서 확인).

**꼬리질문: init 뒤에 `__init` 함수를 부르면?** 해제된 메모리를 실행하는 것이라 Oops가 난다. modpost가 섹션 불일치(section mismatch) 경고로 미리 알려 준다.
</details>

<details><summary>Q7. taint 플래그는 왜 신경 쓰나요?</summary>

`/proc/sys/kernel/tainted` 비트 하나하나가 "이 커널은 순정이 아니다"라는 기록이다. 이 레슨에서 본 두 개: `O`(12, 4096) out-of-tree 모듈, `P`(0, 1) GPL 비호환 모듈. Oops 메시지 첫 줄에 `Tainted: P O` 처럼 찍히고, 업스트림은 P로 오염된 버그 리포트를 받지 않는다. P는 `LOCKDEP_NOW_UNRELIABLE` 로 추가되어 lockdep도 끈다 (`Disabling lock debugging due to kernel taint`). **로드에 실패한 모듈도 오염을 남긴다**는 것이 B9의 실측이다.

**꼬리질문: 벤더 모듈 때문에 P가 붙은 보드에서 크래시가 나면?** 먼저 그 모듈 없이 재현한다. 재현이 안 되면 벤더 쪽, 되면 업스트림에 낼 수 있다.
</details>

<details><summary>Q8. 어떤 드라이버를 모듈(m)로, 어떤 것을 built-in(y)으로 하나요?</summary>

루트 파일시스템을 마운트하는 데 필요한 것(스토리지 컨트롤러, 파일시스템, 콘솔 UART)은 built-in이거나 initramfs에 넣어야 한다. 모듈은 `/lib/modules` 가 보여야 올릴 수 있기 때문이다. 나머지(USB 가젯, 카메라, 선택 센서)는 모듈로 두면 부팅이 빨라지고, 개발 중엔 재부팅 없이 교체할 수 있다. 양산 보드는 보안상 모듈 서명(`MODULE_SIG`)을 강제하거나 모듈을 아예 끄기도 한다.

**꼬리질문: 장치가 나타날 때 모듈이 자동으로 올라오는 원리는?** `MODULE_DEVICE_TABLE` → `.modinfo` 의 `alias=` → depmod의 `modules.alias` → uevent의 `MODALIAS` 로 udev/mdev가 `modprobe` (ELX-M01, ELX-M11).
</details>

## 9. 추가 연습

`practice.c` 는 solution에 아래 P1–P3을 더한 것이다. P4는 생각 문제다.

**P1. 런타임 쓰기까지 검사하는 파라미터.** `echo 42 > /sys/module/hello/parameters/count` 를 거부하고 값을 유지하라. insmod 인자도 같은 규칙.

<details><summary>답</summary>

`module_param_cb(count, &count_ops, &count, 0644)` 에 `.set = count_set`, `.get = param_get_int`. `count_set()` 은 `kstrtoint(val, 0, &v)` (끝의 `\n` 하나는 허용) 후 1..10이 아니면 `-EINVAL`, 맞으면 `*(int *)kp->arg = v`. `module_param_cb` 는 타입 정보를 안 남기므로 `__MODULE_PARM_TYPE(count, "int")` 를 붙여야 `modinfo` 에 `(int)` 가 나온다. 실측:

```text
--- echo 42 > count:
sh: write error: Invalid argument
PASS: P1 runtime write 42 refused by the param op, count stays 2
PASS: B3 count=0 refused, nothing left loaded, dmesg 'hello: `0' invalid for parameter `count''
```

insmod `count=0` 이 이제 init(7단계)이 아니라 parse_args(5단계)에서 떨어진다는 점이 solution과 다르다.
</details>

**P2. 배열 파라미터.** `ids=5,6,7` 을 받아 개수와 값을 찍고, 최대 4개를 넘으면 로드를 거부하라.

<details><summary>답</summary>

`static int ids[4]; static unsigned int nr_ids; module_param_array(ids, int, &nr_ids, 0444);` 커널이 `nr_ids` 에 받은 개수를 넣는다. 실측: `hello: ids n=3: 5 6 7`, sysfs `ids` 는 `5,6,7`. 다섯 개를 주면:

```text
insmod: can't insert '/hello.ko': invalid parameter
ids: can only take 4 arguments
hello: `1' invalid for parameter `ids'
```

두 번째 줄의 값이 `1,2,3,4,5` 가 아니라 `` `1' `` 인 것은 `param_array()` 가 파싱하면서 인자 문자열의 쉼표 자리를 `\0` 으로 바꿔 버렸기 때문이다 (`kernel/params.c`). 버그 리포트에서 이 값을 그대로 믿지 말자.
</details>

**P3. 선택적 의존성.** hello가 있으면 쓰고, 없어도 로드되는 `hello_opt.ko` 를 만들어라. `modinfo` 의 `depends=` 는 비어 있어야 한다.

<details><summary>답</summary>

`greet = symbol_get(hello_greet);` 로 런타임에 찾고 (`NULL` 이면 없이 동작), exit에서 `symbol_put(hello_greet)`. 직접 호출이 없으니 modpost가 `depends=` 를 비워 둔다 (H7). 실측:

```text
hello_opt: hello not loaded, running without it
...
hello: greet #1 from hello_opt (count=1)
hello_opt: optional greet returned 1
--- refcnt=1 holders=[]
hello 12288 1 - Live 0xffffbdba5ad71000 (O)
rmmod: remove 'hello': Resource temporarily unavailable
```

refcnt는 1인데 "Used by"는 `-` 다. `__symbol_get()` 은 `strong_try_module_get()` 만 하고 `add_module_usage()` 는 안 하기 때문이다. 6.18의 `__symbol_get()` 은 **GPL 전용 export만** 돌려준다 (`failing symbol_get of non-GPLONLY symbol` 경고, 소스에서 확인). `hello_greet` 를 `EXPORT_SYMBOL` 로 바꾸면 이 연습은 실패한다.
</details>

**P4. (생각 문제) 벤더가 `.ko` 만 준 경우.** 벤더 BSP의 `wifi.ko` 가 `version magic '6.6.23-vendor ...' should be '6.6.23 ...'` 로 안 올라간다. 무엇을 확인하고 어떻게 고치나?

<details><summary>답</summary>

1) `modinfo wifi.ko` 의 vermagic과 보드의 `uname -r` 을 비교한다. 차이가 `LOCALVERSION` (`-vendor`) 뿐이면, 벤더가 쓴 커널 설정과 소스로 커널을 다시 빌드하는 것이 정석이다. 2) `.config` 까지 다르면 B7처럼 `struct module` 크기나 심볼 CRC에서 다시 떨어진다. `--force-vermagic` 은 이 레슨 커널(`CONFIG_MODULE_FORCE_LOAD` 꺼짐)에서는 거부되고, 켜져 있어도 ABI가 다르면 메모리를 깨뜨린다. 3) 그래서 결론은 소스를 받아 대상 트리로 다시 빌드하거나, 벤더 커널을 그대로 쓰는 것이다. (`CONFIG_MODULE_FORCE_LOAD` 는 랩 `.config` 에서 확인, 벤더 시나리오 자체는 실행하지 않은 사고 실험이다.)
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B06/check.sh practice
```

실측: `45/45 passed, dmesg BUG/WARNING/Oops lines: 0`, `RESULT: PASS (practice)` (H7 + P1×2 + P2×2 + P3×4 가 solution의 36개에 더해진다).

## 10. 핵심 정리

- 모듈 = `module_init`/`module_exit` + `MODULE_LICENSE` + Kbuild 한 줄(`obj-m := hello.o`), 빌드는 **대상 커널의 그 빌드 트리**로 (`make -C $KDIR M=$PWD modules`).
- `module_param(x, int, 0644)` → `/sys/module/<m>/parameters/x`. 권한은 root에게도 적용, 오타 인자는 무시되고 로드는 성공한다.
- `EXPORT_SYMBOL_GPL` → modpost가 `Module.symvers` 와 사용자의 `depends=` 를 쓴다 → 커널은 로드 때 심볼을 해석하며 refcnt · holders를 만든다.
- 로드 검사 순서: ELF/`struct module` 크기 → vermagic → taint → 심볼 → 파라미터 → sysfs → init. 메시지는 dmesg에서, 사용자 공간 문자열은 errno 번역일 뿐이다.
- Proprietary(또는 라이선스 없음) 모듈은 GPL 심볼을 못 보고(`Unknown symbol`), 실패해도 taint `P` 가 남는다.
- `modprobe` 는 `depends=` 를 따라 의존 모듈을 먼저 올린다. kmod는 `depmod -b <root> <ver>` 의 `modules.dep`, BusyBox small은 `.modinfo` 를 직접 읽는다.
- `rmmod` 거부 = 다른 모듈이 링크(`source_list`)했거나 refcnt > 0 → `Resource temporarily unavailable`.

## 참고자료

- Linux 6.18.55 소스 (`/var/lab/src/linux-6.18.55`, 실제로 읽고 확인): `kernel/module/main.c` (`load_module`, `resolve_symbol`, `ref_module`, `delete_module`, `check_modinfo`, `module_license_taint_check`, `__symbol_get`, `do_init_module`), `kernel/params.c`, `include/linux/moduleparam.h`, `include/linux/export.h`, `include/linux/export-internal.h`, `include/linux/vermagic.h`, `include/linux/kernel.h` (`VERIFY_OCTAL_PERMISSIONS`), `include/linux/panic.h`, `fs/kernfs/file.c`, `scripts/mod/modpost.c`, `scripts/Makefile.modpost`
- BusyBox 1.37.0 `modutils/modprobe-small.c` (`moderror`, depmod/rmmod 경로)
- [Documentation/kbuild/modules.rst](https://www.kernel.org/doc/html/latest/kbuild/modules.html) — out-of-tree 빌드, `KBUILD_EXTRA_SYMBOLS`, `Module.symvers`
- [Documentation/admin-guide/tainted-kernels.rst](https://www.kernel.org/doc/html/latest/admin-guide/tainted-kernels.html) — taint 비트 표
- [Documentation/core-api/symbol-namespaces.rst](https://www.kernel.org/doc/html/latest/core-api/symbol-namespaces.html) — `EXPORT_SYMBOL_NS_GPL`, `MODULE_IMPORT_NS`
- [The Linux Kernel Module Programming Guide](https://sysprog21.github.io/lkmpg/) — hello 모듈, module_param 예제
- 배경 노트: EmbeddedLinux E3 (커널 모듈), E2 (config/build). 교차 참조: ELX-B03 (Kconfig, y vs m), ELX-B04 (initramfs), ELX-B07 (`.owner = THIS_MODULE`), ELX-M01/M11 (alias 자동 로딩), ELX-M04 (락 선택)
