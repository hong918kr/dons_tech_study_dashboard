# E2. 커널 설정과 빌드 — Kconfig · defconfig · Kbuild · 산출물

> **이 노트를 다 읽으면**: Kconfig의 `depends on`/`select`/`imply`/`visible if`가 `.config` 값을 어떻게 정하는지 실험 결과로 설명하고 `select` 함정을 피할 수 있다 · `.config`가 `auto.conf`·`autoconf.h`·`include/config/`로 바뀌어 `IS_ENABLED()`와 증분 빌드에 쓰이는 길을 그릴 수 있다 · defconfig/savedefconfig/olddefconfig/fragment/`scripts/config`로 설정을 재현 가능하게 관리하고 tinyconfig에서 QEMU 부팅 가능한 최소 커널을 만들 수 있다 · `obj-y`/`obj-m`/composite/`ccflags-y`로 트리 안에 드라이버를 추가하고 `V=1`, `.i`/`.lst`, modpost, `modules_install`, `ARCH=`/`CROSS_COMPILE=`로 빌드를 들여다볼 수 있다
> **왜 필요한가**: 드라이버 엔지니어는 매일 "이 드라이버를 켜고, 다시 빌드하고, 보드에 올린다"를 한다. 면접에서는 "What is the difference between select and depends on?", "How do you add a new driver to the kernel build?", "vmlinux vs Image?"가 단골이다
> **Don 기준 난이도**: 펌웨어 빌드의 `config.h` + Makefile + 링커 스크립트는 이미 안다 / 새로 배울 부분은 설정 언어(Kconfig)의 의존성 해석, Kbuild의 재귀·링크 순서·modpost, 그리고 "설정 파일 하나로 2만 개 옵션을 재현 가능하게 관리하는" 실무 흐름
> **선행 노트**: B4(빌드 시스템 — Kbuild/Kconfig 미리보기), C6(커널 시작), E1(커널 구조) — E3(커널 모듈)·K(Buildroot/Yocto)로 이어진다

---

## 0. 큰 그림 — 이게 왜 필요한가

Don의 SSD 펌웨어를 떠올려 보자. 제품마다 `config_prodA.h`가 있고, 그 안의 `#define FEATURE_X 1`이 `#ifdef`로 코드를 켜고 끈다. 빌드는 Makefile이 하고, 결과물은 링커 스크립트가 정한 하나의 `.bin`이다. 기능 사이의 의존성("X를 켜려면 Y도 켜야 한다")은 사람이 기억하거나 `#error`로 막는다.

Linux 커널은 이 구조를 그대로 키운 것이다. 다만 규모가 다르다. 이 노트에서 쓰는 6.18.55 소스에는 Kconfig 파일이 1,880개, `config` 심볼이 21,750개, Makefile/Kbuild가 3,282개 있다. 사람이 `#define`을 손으로 관리할 수 없으니 세 개의 층을 둔다.

- **Kconfig** — "무엇을 켤 수 있고, 무엇에 의존하며, 기본값이 무엇인가"를 적는 **설정 언어**. 펌웨어의 `config.h` + 의존성 검사기.
- **`.config`** — 그 언어로 고른 **결과** 한 파일. 펌웨어의 `config_prodA.h`에 해당한다. 이 파일이 곧 "이 보드의 커널"이다.
- **Kbuild** — `.config`를 읽어 "어떤 `.c`를 컴파일해서 어디에 링크할지"를 정하는 **Make 프레임워크**. 결과물은 `vmlinux`(ELF), `Image`(부트로더가 올리는 바이너리), `.ko`(모듈), `.dtb`.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" font-size="13" font-weight="bold">설정 단계 (Kconfig)</text>
  <rect x="10" y="32" width="150" height="52" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="85" y="54" text-anchor="middle" font-size="12">Kconfig 파일 1,880개</text>
  <text x="85" y="72" text-anchor="middle" font-size="12">심볼 21,750개</text>
  <rect x="185" y="32" width="150" height="52" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="260" y="54" text-anchor="middle" font-size="12">scripts/kconfig</text>
  <text x="260" y="72" text-anchor="middle" font-size="12">conf · mconf 등</text>
  <rect x="360" y="32" width="140" height="52" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="430" y="54" text-anchor="middle" font-size="12">.config</text>
  <text x="430" y="72" text-anchor="middle" font-size="12">(보드의 정체)</text>
  <rect x="525" y="26" width="145" height="64" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="597" y="44" text-anchor="middle" font-size="12">auto.conf</text>
  <text x="597" y="62" text-anchor="middle" font-size="12">autoconf.h</text>
  <text x="597" y="80" text-anchor="middle" font-size="12">include/config/</text>
  <line x1="160" y1="58" x2="183" y2="58" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="335" y1="58" x2="358" y2="58" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="500" y1="58" x2="523" y2="58" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="10" y="128" font-size="13" font-weight="bold">빌드 단계 (Kbuild)</text>
  <rect x="10" y="140" width="150" height="52" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="85" y="162" text-anchor="middle" font-size="12">Makefile · Kbuild</text>
  <text x="85" y="180" text-anchor="middle" font-size="12">obj-y · obj-m</text>
  <rect x="185" y="140" width="150" height="52" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="260" y="162" text-anchor="middle" font-size="12">Makefile.build</text>
  <text x="260" y="180" text-anchor="middle" font-size="12">디렉터리마다 재귀</text>
  <rect x="360" y="140" width="140" height="52" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="430" y="162" text-anchor="middle" font-size="12">built-in.a</text>
  <text x="430" y="180" text-anchor="middle" font-size="12">→ vmlinux.a</text>
  <rect x="525" y="140" width="145" height="52" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="597" y="162" text-anchor="middle" font-size="12">modpost</text>
  <text x="597" y="180" text-anchor="middle" font-size="12">Module.symvers</text>
  <line x1="160" y1="166" x2="183" y2="166" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="335" y1="166" x2="358" y2="166" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="500" y1="166" x2="523" y2="166" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="597" y1="90" x2="85" y2="138" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah1)"/>
  <text x="10" y="236" font-size="13" font-weight="bold">산출물</text>
  <rect x="10" y="248" width="150" height="52" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="85" y="270" text-anchor="middle" font-size="12">vmlinux (ELF)</text>
  <text x="85" y="288" text-anchor="middle" font-size="12">System.map</text>
  <rect x="185" y="248" width="150" height="52" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="260" y="270" text-anchor="middle" font-size="12">Image · Image.gz</text>
  <text x="260" y="288" text-anchor="middle" font-size="12">(부트로더가 적재)</text>
  <rect x="360" y="248" width="140" height="52" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="430" y="270" text-anchor="middle" font-size="12">.ko 모듈</text>
  <text x="430" y="288" text-anchor="middle" font-size="12">modules_install</text>
  <rect x="525" y="248" width="145" height="52" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="597" y="270" text-anchor="middle" font-size="12">.dtb</text>
  <text x="597" y="288" text-anchor="middle" font-size="12">(make dtbs)</text>
  <line x1="430" y1="192" x2="85" y2="246" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="85" y1="300" x2="85" y2="318" stroke="currentColor"/>
  <line x1="85" y1="318" x2="260" y2="318" stroke="currentColor"/>
  <line x1="260" y1="318" x2="260" y2="302" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="597" y1="192" x2="430" y2="246" stroke="currentColor" marker-end="url(#ah1)"/>
</svg>
```
그림 1 — 커널 빌드의 세 층. 위층(Kconfig)이 `.config`를 만들고 그것이 헤더와 make 변수(`auto.conf`)로 번역된다(점선). 가운데 층(Kbuild)이 디렉터리를 재귀로 돌며 `built-in.a`와 `.ko`를 만들고, 아래층 산출물로 링크·변환된다. `vmlinux` → `Image`는 objcopy, `Image` → `Image.gz`는 gzip이다.

이 노트는 그림 1의 모든 상자를 실제로 돌린다. 순서는 이렇다.

| 절 | 내용 | 실습 |
|---|---|---|
| §1–§2 | Kconfig 언어, 의존성 의미론(`depends on`/`select`/`imply`) | 예제 1–4 |
| §3 | `.config` → `autoconf.h`/`auto.conf` → `IS_ENABLED()` | 예제 5 |
| §4 | 설정 워크플로(defconfig, fragment, `scripts/config`, `/proc/config.gz` …) | 예제 6–11 |
| §5 | **실습 1**: tinyconfig → QEMU 부팅 가능한 최소 커널 | 예제 12–13 |
| §6 | Kbuild — `obj-y`/`obj-m`/composite/재귀/링크 순서, **실습 2**: 트리 안 드라이버 추가 | 예제 14–15 |
| §7 | `V=1`, `V=2`, `.i`/`.s`/`.lst` 단일 타깃 | 예제 16–17 |
| §8 | **실습 3**: 증분 빌드와 fixdep | 예제 18 |
| §9 | 모듈 빌드 — modpost, `Module.symvers`, `modules_install` | 예제 19–20 |
| §10 | 산출물 — `vmlinux`/`Image`/`Image.gz`/`System.map`/dtbs | 예제 21 |
| §11 | 크로스 컴파일 `ARCH=`/`CROSS_COMPILE=` | 예제 22 |

> 실행 환경: 모든 출력은 Lima VM `elinux`(aarch64, 6코어, 호스트 커널 `uname -r` = `7.0.0-38-generic`, gcc 15.2) 안에서 실제로 돌린 결과다. 커널 소스는 랩의 `/var/lab/src/linux-6.18.55`를 `/var/tmp/E2/src`로 **통째로 복사**해서 썼고(`cp -a`, 1.7 GB, 7.6초), 빌드 출력은 전부 `O=/var/tmp/E2/<이름>`으로 따로 뒀다. 부팅은 QEMU `virt`(cortex-a57, 512 MB, TCG)에 랩 BusyBox initramfs. 빌드는 `-j4`로 제한했고, 같은 VM에서 다른 노트 작성자들이 동시에 빌드하고 있어서(측정 중 load average 3~8) **시간 수치는 ±수십 % 흔들린다**. 크기·개수는 정확하다.

---

## 1. Kconfig 언어 — 심볼, 타입, 메뉴

### 1.1 직관 — "설정 항목마다 타입·조건·기본값을 가진 변수"

Kconfig의 `config FOO` 하나는 **전역 변수 하나**다. 이름은 `FOO`, 결과 파일에서는 `CONFIG_FOO`가 된다. 변수에는 타입(bool/tristate/int/hex/string), 화면에 보일 프롬프트, 기본값, 그리고 **언제 존재할 수 있는지**(의존성)가 붙는다. `make menuconfig`의 메뉴 트리는 이 변수들을 소스 순서대로 나열한 것이다.

B4 §7.2에서 키워드 표와 `conf` 도구로 작은 Kconfig를 처리해 봤다. 여기서는 **진짜 커널 트리** 안에서, 각 키워드가 실제 값을 어떻게 바꾸는지 실험으로 확인한다.

### 1.2 진짜 커널의 Kconfig 세 장면

예제 1 — 무엇을 확인하나: 커널 소스에서 `config`, `choice`, "조건부 프롬프트"가 실제로 어떻게 쓰이는지 grep으로 본다.

```sh
cd /var/tmp/E2/src
grep -n -A12 "^config SERIAL_AMBA_PL011$" drivers/tty/serial/Kconfig
grep -n -B2 -A14 "^choice" kernel/Kconfig.hz | head -30
grep -n -B1 -A6 "^config PRINTK$" init/Kconfig
```

```text
48:config SERIAL_AMBA_PL011
49-	tristate "ARM AMBA PL011 serial port support"
50-	depends on ARM_AMBA
51-	select SERIAL_CORE
52-	help
53-	  This selects the ARM(R) AMBA(R) PrimeCell PL011 UART.  If you have
54-	  an Integrator/PP2, Integrator/CP or Versatile platform, say Y or M
55-	  here.
56-
57-	  If unsure, say N.
58-
59-config SERIAL_AMBA_PL011_CONSOLE
60-	bool "Support for console on AMBA serial port"
4-#
5-
6:choice
7-	prompt "Timer frequency"
8-	default HZ_250
9-	help
...
20-	config HZ_100
1704-
1705:config PRINTK
1706-	default y
1707-	bool "Enable support for printk" if EXPERT
1708-	select IRQ_WORK
1709-	help
1710-	  This option enables normal printk support. Removing it
1711-	  eliminates most of the message strings from the kernel image
```

출력에서 볼 것:

- QEMU `virt`의 콘솔 UART 드라이버 `SERIAL_AMBA_PL011`은 `tristate`(내장 y / 모듈 m / 끔 n)이고, AMBA 버스(`ARM_AMBA`)가 있어야 존재할 수 있으며(`depends on`), 켜는 순간 serial core 계층(`SERIAL_CORE`)을 **끌고 온다**(`select`). 드라이버 Kconfig의 전형적인 3줄이다.
- `choice`는 "여러 bool 중 정확히 하나"다. HZ는 100/250/300/1000 중 하나만 고를 수 있다. 랩 커널은 기본값 `HZ_250`, Ubuntu VM 호스트 커널은 `HZ=1000`이다(B4에서 본 값).
- `bool "Enable support for printk" if EXPERT` — **프롬프트에 조건**이 붙어 있다. `EXPERT`가 꺼져 있으면 이 항목은 메뉴에 안 보이고 **기본값 y로 고정**된다. 일반 사용자가 실수로 printk를 끄지 못하게 하는 장치다. 이 한 줄이 §4.6의 "allnoconfig인데 printk가 켜져 있다" 수수께끼의 답이다.

### 1.3 문법 요약 — 이 노트에서 쓰는 전부

| 구문 | 뜻 | 결과에 미치는 영향 |
|---|---|---|
| `config FOO` | 심볼 정의 | `CONFIG_FOO` |
| `menuconfig FOO` | 심볼 + 그 아래 항목들을 하위 메뉴로 묶어 보여 줌 | 값은 `config`와 같다. 표시만 다르다 |
| `bool` / `tristate` | y·n / y·m·n | `=m`이면 `CONFIG_FOO_MODULE` 매크로 |
| `int` / `hex` / `string` | 값 타입, `range a b`로 제한 | `#define CONFIG_FOO 250`, `"문자열"` |
| `bool "프롬프트" if EXPR` | 조건부 프롬프트 | EXPR이 거짓이면 숨고 default로 고정 |
| `default X [if EXPR]` | 기본값. 여러 개면 **조건이 참인 첫 번째** | 사용자 값이 없을 때만 쓰임 |
| `depends on EXPR` | 존재 조건(상한) | 거짓이면 심볼이 사라짐. m이면 최대 m |
| `select BAR` | BAR를 **강제로** 하한 이상으로 | BAR의 depends 무시 → 경고 |
| `imply BAR` | BAR의 **기본값을** 올림 | BAR의 depends 존중, 사용자가 끌 수 있음 |
| `choice` … `endchoice` | 정확히 하나 선택 | 고른 것만 `=y` |
| `comment "…"` | 메뉴에 설명 줄만 표시 | 값 없음 |
| `menu "…"` … `endmenu` | 묶음. `visible if EXPR`로 표시 조건 | 숨어도 안의 default는 적용됨 |
| `if EXPR` … `endif` | 안의 모든 항목에 `depends on EXPR` 추가 | 블록 단위 의존성 |
| `source "path/Kconfig"` | 다른 Kconfig 파일 포함 | 트리 전체가 하나로 합쳐짐 |
| `help` | menuconfig의 `?` 설명 | 없음 |

### 1.4 예제 2 — 이 노트의 실험용 Kconfig 만들기

무엇을 확인하나: 복사한 소스 트리에 `drivers/misc/e2/` 디렉터리를 만들고, 위 표의 구문을 거의 다 쓰는 Kconfig를 넣는다. 이후 모든 실험(의존성, 헤더 생성, 빌드, 부팅)이 이 파일로 진행된다. `E2_` 접두어는 같은 VM에서 일하는 다른 노트와 이름이 겹치지 않게 하려는 것이다.

```text
# SPDX-License-Identifier: GPL-2.0-only
menuconfig E2_DEMO
	bool "E2 Kconfig/Kbuild demo drivers"
	help
	  Demo menu for the E2 study note. Say N unless you are reading E2.

if E2_DEMO

config E2_HELLO
	tristate "E2 hello driver (prints a greeting at init)"
	default m if MODULES
	help
	  Prints the Kconfig values it was built with.

config E2_HELLO_GREETING
	string "Greeting text"
	depends on E2_HELLO
	default "hello from Kconfig"

config E2_HELLO_PERIOD_MS
	int "Fake sampling period (ms)"
	depends on E2_HELLO
	range 1 1000
	default 100

config E2_HELLO_STATS
	bool "Link the stats helper into e2_hello"
	depends on E2_HELLO
	default y

choice
	prompt "Log style"
	depends on E2_HELLO
	default E2_STYLE_PLAIN

config E2_STYLE_PLAIN
	bool "plain"

config E2_STYLE_LOUD
	bool "LOUD"

endchoice

comment "dependency semantics demo (configure only)"

config E2_BUS
	bool "E2 fake bus core"

config E2_CODEC
	tristate "E2 codec driver (needs the bus)"
	depends on E2_BUS

config E2_SEL
	bool "forces E2_CODEC on (select)"
	select E2_CODEC

config E2_IMP
	bool "suggests E2_CODEC (imply)"
	imply E2_CODEC

menu "E2 bus tuning"
	visible if E2_BUS

config E2_BUS_TIMEOUT_MS
	int "Bus timeout (ms)"
	default 7

endmenu

endif # E2_DEMO
```

상위 Kconfig에 `source` 한 줄, 상위 Makefile에 디렉터리 한 줄을 넣는다. 랩 원본과의 차이를 `diff`로 확인했다.

```sh
cd /var/tmp/E2/src && mkdir -p drivers/misc/e2 && cp /Users/donh/.elinux_scratch/E2/e2/* drivers/misc/e2/
sed -i '655a source "drivers/misc/e2/Kconfig"' drivers/misc/Kconfig
echo 'obj-$(CONFIG_E2_DEMO)		+= e2/' >> drivers/misc/Makefile
diff -u /var/lab/src/linux-6.18.55/drivers/misc/Makefile drivers/misc/Makefile
diff -u /var/lab/src/linux-6.18.55/drivers/misc/Kconfig drivers/misc/Kconfig
```

```text
--- /var/lab/src/linux-6.18.55/drivers/misc/Makefile	2026-10-03 03:39:03.000000000 -0700
+++ drivers/misc/Makefile	2026-10-10 13:49:50.915919602 -0700
@@ -75,3 +75,4 @@
 obj-y				+= keba/
 obj-y				+= amd-sbi/
 obj-$(CONFIG_MISC_RP1)		+= rp1/
+obj-$(CONFIG_E2_DEMO)		+= e2/
--- /var/lab/src/linux-6.18.55/drivers/misc/Kconfig	2026-10-03 03:39:03.000000000 -0700
+++ drivers/misc/Kconfig	2026-10-10 13:49:50.910919595 -0700
@@ -653,6 +653,7 @@
 source "drivers/misc/vmw_vmci/Kconfig"
 source "drivers/misc/genwqe/Kconfig"
 source "drivers/misc/ocxl/Kconfig"
+source "drivers/misc/e2/Kconfig"
 source "drivers/misc/bcm-vk/Kconfig"
 source "drivers/misc/cardreader/Kconfig"
 source "drivers/misc/uacce/Kconfig"
```

출력에서 볼 것: 트리 안에 드라이버를 추가할 때 고치는 곳은 **딱 세 군데**다 — 새 디렉터리의 Kconfig·Makefile, 부모 Kconfig의 `source`, 부모 Makefile의 `obj-` 한 줄. 부모 Makefile의 `obj-$(CONFIG_E2_DEMO) += e2/`는 "이 심볼이 켜졌을 때만 `e2/`로 내려가라"는 뜻이다(§6.2).

> 주의: 랩 소스(`/var/lab/src`)를 하드링크 복사(`cp -al`)하면 디스크는 아끼지만, `touch`나 제자리 편집이 **랩 원본의 inode를 같이 바꾼다**. 그래서 이 노트는 §8에서 헤더를 `touch`하는 실험 때문에 진짜 복사(`cp -a`)를 썼다.

---

## 2. 의존성 의미론 — `depends on` · `select` · `imply` · `visible if`

### 2.1 직관 — 상한, 강제, 추천

세 키워드는 모두 "A와 B가 관련 있다"를 말하지만 방향과 힘이 다르다.

- `depends on B` (A 쪽에 씀) — **A의 상한**을 B로 묶는다. B가 n이면 A는 존재하지 않는다. B가 m이면 A는 최대 m. 펌웨어로 말하면 `#if !defined(B) #error` 대신 "B가 없으면 메뉴에서 A를 아예 지운다".
- `select B` (A 쪽에 씀) — A가 켜지면 **B의 하한**을 A로 올린다. B의 `depends on`은 **보지 않는다**. "A를 링크하려면 B 라이브러리가 반드시 있어야 한다"를 강제하는 용도.
- `imply B` (A 쪽에 씀) — A가 켜지면 **B의 기본값만** 올린다. B의 depends는 존중하고, 사용자가 B를 n으로 내릴 수 있다. "있으면 좋은 것"을 추천하는 용도.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="113" y="20" text-anchor="middle" font-size="13" font-weight="bold">depends on</text>
  <text x="340" y="20" text-anchor="middle" font-size="13" font-weight="bold">select</text>
  <text x="567" y="20" text-anchor="middle" font-size="13" font-weight="bold">imply</text>
  <rect x="38" y="36" width="150" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="113" y="61" text-anchor="middle" font-size="12">E2_BUS (조건)</text>
  <rect x="38" y="126" width="150" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="113" y="151" text-anchor="middle" font-size="12">E2_CODEC</text>
  <line x1="113" y1="124" x2="113" y2="78" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="122" y="105" font-size="12">상한 = BUS</text>
  <text x="113" y="196" text-anchor="middle" font-size="12">BUS=n → CODEC 사라짐</text>
  <text x="113" y="216" text-anchor="middle" font-size="12">BUS=y → 사용자가 선택</text>
  <rect x="265" y="36" width="150" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="61" text-anchor="middle" font-size="12">E2_SEL</text>
  <rect x="265" y="126" width="150" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="151" text-anchor="middle" font-size="12">E2_CODEC</text>
  <line x1="340" y1="78" x2="340" y2="124" stroke="#d0564a" stroke-width="2" marker-end="url(#ah2)"/>
  <text x="349" y="105" font-size="12">하한 = SEL</text>
  <text x="340" y="196" text-anchor="middle" font-size="12">BUS=n 이어도 CODEC=y</text>
  <text x="340" y="216" text-anchor="middle" font-size="12">사용자가 끌 수 없음</text>
  <rect x="492" y="36" width="150" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="567" y="61" text-anchor="middle" font-size="12">E2_IMP</text>
  <rect x="492" y="126" width="150" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="567" y="151" text-anchor="middle" font-size="12">E2_CODEC</text>
  <line x1="567" y1="78" x2="567" y2="124" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah2)"/>
  <text x="576" y="105" font-size="12">기본값만</text>
  <text x="567" y="196" text-anchor="middle" font-size="12">BUS=n → CODEC=n</text>
  <text x="567" y="216" text-anchor="middle" font-size="12">사용자가 끌 수 있음</text>
  <rect x="38" y="242" width="604" height="44" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="340" y="261" text-anchor="middle" font-size="12">select 함정: 끌려온 심볼의 depends가 거짓이면 "unmet direct dependencies" 경고 후</text>
  <text x="340" y="279" text-anchor="middle" font-size="12">그대로 켜진다 → 의존 대상 없이 컴파일되어 빌드·링크 에러 또는 런타임 오동작</text>
</svg>
```
그림 2 — 세 키워드의 화살표 방향과 힘. `depends on`은 아래에서 위를 올려다보는 "상한", `select`는 위에서 내리꽂는 "강제 하한", `imply`는 "기본값 추천". 아래 빨간 상자가 면접 단골 함정이다.

### 2.2 예제 3 — 여섯 가지 조합을 `olddefconfig`로 판정시키기

무엇을 확인하나: 같은 시작 `.config`에서 사용자 값만 바꿔 넣고 Kconfig 엔진(`olddefconfig`)이 최종 값을 어떻게 정하는지 본다. `scripts/config -u`는 "줄을 아예 지움(사용자 값 없음)", `-d`는 "n으로 명시", `-e`는 "y로 명시"다.

```sh
#!/bin/bash
# deps.sh — depends on / select / imply 의미를 olddefconfig 결과로 확인 (설정만, 빌드 안 함)
S=/var/tmp/E2/src; O=/var/tmp/E2/kdemo; mkdir -p $O; cd $S
try() {  # $1=label, rest = scripts/config args
  local label=$1; shift
  cp /var/tmp/E2/tuned/.config $O/.config
  scripts/config --file $O/.config -d E2_BUS -d E2_SEL -d E2_IMP -u E2_CODEC "$@"
  echo "### $label"
  make -s O=$O olddefconfig 2>&1 | sed 's/^/  [kconfig] /'
  grep -E "E2_(BUS|CODEC|SEL|IMP)[= ]" $O/.config | tr '\n' ' '; echo
}
try "A: user sets CODEC=y, BUS=n (depends on)"   -e E2_CODEC
try "B: SEL=y, BUS=n (select ignores depends)"   -e E2_SEL
try "C: IMP=y, BUS=n (imply respects depends)"   -e E2_IMP
try "D: IMP=y, BUS=y"                            -e E2_IMP -e E2_BUS
try "E: IMP=y, BUS=y, user says CODEC=n"         -e E2_IMP -e E2_BUS -d E2_CODEC
try "F: SEL=y, BUS=y, user says CODEC=n"         -e E2_SEL -e E2_BUS -d E2_CODEC
```

```text
### A: user sets CODEC=y, BUS=n (depends on)
# CONFIG_E2_BUS is not set # CONFIG_E2_SEL is not set # CONFIG_E2_IMP is not set 
### B: SEL=y, BUS=n (select ignores depends)
  [kconfig] 
  [kconfig] WARNING: unmet direct dependencies detected for E2_CODEC
  [kconfig]   Depends on [n]: E2_DEMO [=y] && E2_BUS [=n]
  [kconfig]   Selected by [y]:
  [kconfig]   - E2_SEL [=y] && E2_DEMO [=y]
# CONFIG_E2_BUS is not set CONFIG_E2_CODEC=y CONFIG_E2_SEL=y # CONFIG_E2_IMP is not set 
### C: IMP=y, BUS=n (imply respects depends)
# CONFIG_E2_BUS is not set # CONFIG_E2_CODEC is not set # CONFIG_E2_SEL is not set CONFIG_E2_IMP=y 
### D: IMP=y, BUS=y
CONFIG_E2_BUS=y CONFIG_E2_CODEC=y # CONFIG_E2_SEL is not set CONFIG_E2_IMP=y 
### E: IMP=y, BUS=y, user says CODEC=n
CONFIG_E2_BUS=y # CONFIG_E2_CODEC is not set # CONFIG_E2_SEL is not set CONFIG_E2_IMP=y 
### F: SEL=y, BUS=y, user says CODEC=n
CONFIG_E2_BUS=y CONFIG_E2_CODEC=y CONFIG_E2_SEL=y # CONFIG_E2_IMP is not set 
```

출력에서 볼 것:

- **A** — 사용자가 `CODEC=y`라고 적어도 `BUS=n`이면 CODEC 줄이 **통째로 사라진다**. `.config`에 써 둔 값이 조용히 없어지는 것, 이게 "설정을 넣었는데 안 들어갔다"의 1번 원인이다.
- **B** — `select`는 depends를 무시하고 `CODEC=y`로 만든다. Kconfig는 **경고만** 찍고 계속 간다. 실제 드라이버였다면 CODEC 코드가 BUS 함수 없이 컴파일되어 링크 에러(`undefined reference`)나 런타임 NULL 호출로 터진다.
- **C/D** — `imply`는 BUS가 없으면 아무 일도 하지 않고(C), BUS가 있으면 CODEC 기본값을 y로 올린다(D).
- **E/F** — 사용자가 `CODEC=n`이라고 명시하면 `imply`는 물러나고(E), `select`는 무시하고 다시 y로 만든다(F). "사용자가 끌 수 있느냐"가 둘의 결정적 차이다.

### 2.3 예제 4 — menuconfig 검색 화면은 이 관계를 다 보여 준다

무엇을 확인하나: 대화형 도구인 menuconfig를 `tmux`로 띄우고 키 입력을 보내 화면을 캡처한다(노트에 붙이려고 비대화형으로 돌린 것). `/`(검색)에 심볼 이름을 넣으면 정의 위치, 의존성, **누가 select/imply 하는지**가 나온다. F 조합(SEL=y, BUS=y) 상태의 `.config`에서 찍었다.

```sh
cd /var/tmp/E2/src
tmux -L e2 new-session -d -s e2mc -x 92 -y 30 "TERM=xterm make O=/var/tmp/E2/kdemo menuconfig"
sleep 6; tmux -L e2 send-keys -t e2mc "/"; sleep 1; tmux -L e2 send-keys -t e2mc "E2_CODEC" Enter; sleep 2
tmux -L e2 capture-pane -p -t e2mc; tmux -L e2 kill-server
```

```text
 .config - Linux/arm64 6.18.55 Kernel Configuration
 → Search (E2_CODEC) qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq
  lqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq Search Results qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqk
  x Symbol: E2_CODEC [=y]                                                               x
  x Type  : tristate                                                                    x
  x Defined at drivers/misc/e2/Kconfig:49                                               x
  x   Prompt: E2 codec driver (needs the bus)                                           x
  x   Depends on: E2_DEMO [=y] && E2_BUS [=y]                                           x
  x   Location:                                                                         x
  x     -> Device Drivers                                                               x
  x       -> Misc devices                                                               x
  x         -> E2 Kconfig/Kbuild demo drivers (E2_DEMO [=y])                            x
  x           -> E2 fake bus core (E2_BUS [=y])                                         x
  x (1)         -> E2 codec driver (needs the bus) (E2_CODEC [=y])                      x
  x Selected by [y]:                                                                    x
  x   - E2_SEL [=y] && E2_DEMO [=y]                                                     x
  x Implied by [n]:                                                                     x
  x   - E2_IMP [=n] && E2_DEMO [=y]                                                     x
```

출력에서 볼 것 (`q`, `x`, `l`, `k`는 tmux가 ncurses 선 문자를 ASCII로 찍은 것이다):

- `Depends on: E2_DEMO [=y] && E2_BUS [=y]` — 우리는 `depends on E2_BUS`만 썼는데 `E2_DEMO`가 붙어 있다. `if E2_DEMO … endif` 블록이 안의 모든 항목에 depends를 **자동으로 AND** 한 것이다.
- `Location`에서 CODEC이 `E2 fake bus core` **밑에** 들여쓰기되어 있다. Kconfig는 "바로 앞 심볼에 의존하는 항목"을 그 심볼의 자식처럼 보여 준다(메뉴 구조는 depends로 추론된다).
- `Selected by` / `Implied by` — "왜 이 옵션이 켜져 있지?"를 물을 때 가장 빠른 답이다. 실무에서 "끄려고 해도 안 꺼지는 옵션"의 99%는 누군가 `select`하고 있는 것이다.

### 2.4 실제 드라이버의 정석 패턴 — "버스는 depends, 라이브러리는 select"

센서 키트의 IMU로 자주 쓰는 Bosch BMI270의 실제 Kconfig다.

```sh
sed -n 1,40p drivers/iio/imu/bmi270/Kconfig; cat drivers/iio/imu/bmi270/Makefile
```

```text
# SPDX-License-Identifier: GPL-2.0
#
# BMI270 IMU driver
#

config BMI270
	tristate
	select IIO_BUFFER
	select IIO_TRIGGERED_BUFFER

config BMI270_I2C
	tristate "Bosch BMI270 I2C driver"
	depends on I2C
	select BMI270
	select REGMAP_I2C
	help
	  Enable support for the Bosch BMI270 6-Axis IMU connected to I2C
	  interface.

	  This driver can also be built as a module. If so, the module will be
	  called bmi270_i2c.

config BMI270_SPI
	tristate "Bosch BMI270 SPI driver"
	depends on SPI
	select BMI270
	select REGMAP_SPI
	help
	  Enable support for the Bosch BMI270 6-Axis IMU connected to SPI
	  interface.

	  This driver can also be built as a module. If so, the module will be
	  called bmi270_spi.
# SPDX-License-Identifier: GPL-2.0
#
# Makefile for Bosch BMI270 IMU
#
obj-$(CONFIG_BMI270) += bmi270_core.o
obj-$(CONFIG_BMI270_I2C) += bmi270_i2c.o
obj-$(CONFIG_BMI270_SPI) += bmi270_spi.o
```

출력에서 볼 것:

- 사용자가 고르는 것은 `BMI270_I2C`/`BMI270_SPI`(프롬프트 있음)뿐이다. 코어 `BMI270`은 **프롬프트가 없는 tristate**라 사용자가 직접 켤 수 없고, 버스 드라이버가 `select`해야만 켜진다. 펌웨어로 말하면 "공통 코어 라이브러리 + 버스별 HAL 래퍼" 구조를 Kconfig로 표현한 것이다.
- `REGMAP_I2C`는 `drivers/base/regmap/Kconfig`에서 `tristate` + `depends on I2C`로 정의된 라이브러리다. BMI270_I2C가 **자기 자신도 `depends on I2C`** 를 갖고 있기 때문에 `select REGMAP_I2C`가 안전하다. 이것이 select를 쓰는 규칙이다: **select할 대상의 depends를 내가 먼저 다 만족시켜 둔다.**
- select된 심볼은 select한 쪽의 값 이상이 된다. `BMI270_I2C=m`이면 `BMI270`과 `REGMAP_I2C`는 최소 m, 다른 누군가가 y로 select하면 y.

### 2.5 `visible if`와 `if` 블록 — 숨겨도 값은 남는다

예제 3을 돌리기 전, E2 심볼을 처음 켰을 때의 `.config`(§4.3 예제 7)에 이런 줄이 있었다.

```text
# CONFIG_E2_BUS is not set
CONFIG_E2_BUS_TIMEOUT_MS=7
```

`E2_BUS`가 꺼져서 `menu "E2 bus tuning" visible if E2_BUS`는 메뉴에서 **안 보이는데**, 그 안의 `E2_BUS_TIMEOUT_MS`는 기본값 7로 `.config`에 **들어 있다**. 반면 `depends on E2_BUS`인 `E2_CODEC`은 같은 상황에서 `.config`에 줄 자체가 없다(예제 3의 A). 차이를 표로 정리한다.

| 숨기는 방법 | 메뉴에 보이나 | `.config`에 값이 남나 | 언제 쓰나 |
|---|---|---|---|
| `depends on X` (X=n) | 안 보임 | 안 남음 (심볼 소멸) | 기능 자체가 성립하지 않을 때 |
| `if X` … `endif` | 안 보임 | 안 남음 | 여러 항목에 같은 depends를 한 번에 |
| `menu … visible if X` | 안 보임 | **default로 남음** | 고급 튜닝 값을 숨기되 기본값은 쓰고 싶을 때 |
| `bool "…" if X` (조건부 프롬프트) | 안 보임 | **default로 남음** | 위험한 옵션을 전문가만 바꾸게 (`if EXPERT`) |

---

## 3. `.config`가 코드와 Makefile에 닿는 길 — `auto.conf` · `autoconf.h` · `IS_ENABLED()`

### 3.1 직관 — 한 번 고른 값을 세 가지 언어로 번역한다

`.config`는 사람(그리고 menuconfig)이 읽고 쓰는 파일이다. 빌드가 시작되면 `make`가 `syncconfig`라는 단계를 먼저 돌려서 이 값을 세 가지 형태로 **번역**해 둔다.

| 생성 파일 | 형식 | 누가 읽나 |
|---|---|---|
| `include/config/auto.conf` | `CONFIG_FOO=y` (make 변수 문법) | Kbuild Makefile — `obj-$(CONFIG_FOO)` 전개 |
| `include/generated/autoconf.h` | `#define CONFIG_FOO 1` | C 소스 — 모든 컴파일에 `-include`로 강제 포함 |
| `include/config/<SYMBOL>` | 빈 파일, 값이 바뀔 때만 타임스탬프 갱신 | fixdep — 심볼 단위 증분 빌드(§8) |

```svg
<svg viewBox="0 0 680 290" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="110" width="130" height="60" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="75" y="134" text-anchor="middle" font-size="12">.config</text>
  <text x="75" y="154" text-anchor="middle" font-size="12">E2_HELLO=m</text>
  <rect x="170" y="116" width="120" height="48" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="230" y="136" text-anchor="middle" font-size="12">syncconfig</text>
  <text x="230" y="154" text-anchor="middle" font-size="12">(conf 도구)</text>
  <line x1="140" y1="140" x2="168" y2="140" stroke="currentColor" marker-end="url(#ah3)"/>
  <rect x="320" y="20" width="170" height="52" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="405" y="41" text-anchor="middle" font-size="12">auto.conf</text>
  <text x="405" y="60" text-anchor="middle" font-size="12">CONFIG_E2_HELLO=m</text>
  <rect x="320" y="114" width="170" height="52" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="405" y="135" text-anchor="middle" font-size="12">autoconf.h</text>
  <text x="405" y="154" text-anchor="middle" font-size="12">E2_HELLO_MODULE 1</text>
  <rect x="320" y="208" width="170" height="52" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="405" y="229" text-anchor="middle" font-size="12">include/config/</text>
  <text x="405" y="248" text-anchor="middle" font-size="12">E2_HELLO (빈 파일)</text>
  <line x1="290" y1="130" x2="318" y2="50" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="290" y1="140" x2="318" y2="140" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="290" y1="150" x2="318" y2="230" stroke="currentColor" marker-end="url(#ah3)"/>
  <rect x="520" y="20" width="150" height="52" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="595" y="41" text-anchor="middle" font-size="12">Makefile 전개</text>
  <text x="595" y="60" text-anchor="middle" font-size="12">obj-m += e2_hello.o</text>
  <rect x="520" y="114" width="150" height="52" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="595" y="135" text-anchor="middle" font-size="12">gcc -include</text>
  <text x="595" y="154" text-anchor="middle" font-size="12">IS_ENABLED() 등</text>
  <rect x="520" y="208" width="150" height="52" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="595" y="229" text-anchor="middle" font-size="12">fixdep .cmd 파일</text>
  <text x="595" y="248" text-anchor="middle" font-size="12">심볼 단위 재빌드</text>
  <line x1="490" y1="46" x2="518" y2="46" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="490" y1="140" x2="518" y2="140" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="490" y1="234" x2="518" y2="234" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="75" y="282" text-anchor="middle" font-size="12">사람이 편집</text>
  <text x="595" y="282" text-anchor="middle" font-size="12">빌드가 사용</text>
</svg>
```
그림 3 — `.config` 한 줄이 세 갈래로 번역된다. 위는 "어떤 파일을 빌드하나"(Makefile), 가운데는 "코드 안에서 무엇이 켜졌나"(C 전처리기), 아래는 "무엇을 다시 빌드해야 하나"(fixdep). 생성 파일은 전부 `O=` 출력 디렉터리 안에 생긴다.

### 3.2 예제 5 — E2 심볼이 `autoconf.h`와 `auto.conf`에서 어떻게 보이나

무엇을 확인하나: `E2_HELLO=m`, 문자열·정수·choice 값이 각 생성 파일에 어떤 모양으로 들어가는지, 그리고 `IS_ENABLED()` 계열 매크로가 어떻게 정의되어 있는지 본다.

```sh
cd /var/tmp/E2/tuned
grep E2_ include/generated/autoconf.h; grep E2_ include/config/auto.conf
sed -n "/#define __ARG_PLACEHOLDER_1/,/#define IS_ENABLED/p" /var/tmp/E2/src/include/linux/kconfig.h | grep -v "^ \*\|^/\*\|^$"
```

```text
#define CONFIG_E2_HELLO_GREETING "hi Don"
#define CONFIG_E2_BUS_TIMEOUT_MS 7
#define CONFIG_E2_HELLO_PERIOD_MS 300
#define CONFIG_E2_STYLE_PLAIN 1
#define CONFIG_E2_HELLO_MODULE 1
#define CONFIG_E2_DEMO 1
#define CONFIG_E2_HELLO_STATS 1
CONFIG_E2_HELLO_GREETING=hi Don
CONFIG_E2_BUS_TIMEOUT_MS=7
CONFIG_E2_HELLO_PERIOD_MS=300
CONFIG_E2_STYLE_PLAIN=y
CONFIG_E2_HELLO=m
CONFIG_E2_DEMO=y
CONFIG_E2_HELLO_STATS=y
#define __ARG_PLACEHOLDER_1 0,
#define __take_second_arg(__ignored, val, ...) val
#define __and(x, y)			___and(x, y)
#define ___and(x, y)			____and(__ARG_PLACEHOLDER_##x, y)
#define ____and(arg1_or_junk, y)	__take_second_arg(arg1_or_junk y, 0)
#define __or(x, y)			___or(x, y)
#define ___or(x, y)			____or(__ARG_PLACEHOLDER_##x, y)
#define ____or(arg1_or_junk, y)		__take_second_arg(arg1_or_junk 1, y)
#define __is_defined(x)			___is_defined(x)
#define ___is_defined(val)		____is_defined(__ARG_PLACEHOLDER_##val)
#define ____is_defined(arg1_or_junk)	__take_second_arg(arg1_or_junk 1, 0)
#define IS_BUILTIN(option) __is_defined(option)
#define IS_MODULE(option) __is_defined(option##_MODULE)
#define IS_REACHABLE(option) __or(IS_BUILTIN(option), \
				__and(IS_MODULE(option), __is_defined(MODULE)))
#define IS_ENABLED(option) __or(IS_BUILTIN(option), IS_MODULE(option))
```

출력에서 볼 것:

- `E2_HELLO=m`은 헤더에서 `CONFIG_E2_HELLO`가 **아니라** `CONFIG_E2_HELLO_MODULE`로 정의된다. `n`인 심볼은 헤더에 아예 없다(`E2_BUS`, `E2_STYLE_LOUD`가 없다). choice는 고른 쪽만 `1`.
- `auto.conf`에서는 문자열의 따옴표가 빠져 있다(`CONFIG_E2_HELLO_GREETING=hi Don`). make 변수로 쓰기 위한 형식이다. 헤더 쪽은 C 문자열 리터럴 그대로 `"hi Don"`.
- `IS_BUILTIN()`의 트릭: `CONFIG_FOO`가 `1`로 정의되어 있으면 `__ARG_PLACEHOLDER_##1` → `__ARG_PLACEHOLDER_1` → `0,`이 되어 인자가 하나 늘고, `__take_second_arg`가 `1`을 고른다. 정의되어 있지 않으면 `__ARG_PLACEHOLDER_CONFIG_FOO`라는 의미 없는 토큰 하나뿐이라 두 번째 인자 `0`이 고려된다. 결과적으로 **`#ifdef` 없이 C 표현식 안에서 0/1 상수**를 얻는다.

왜 `#ifdef` 대신 `IS_ENABLED()`를 쓰나? 두 가지 이유다.

1. **모듈 함정** — `#ifdef CONFIG_FOO`는 `FOO=m`일 때 거짓이다. 실습 2(§6.4, §9.3)에서 실제로 `#ifdef=0`이 찍힌다.
2. **양쪽 코드가 다 컴파일된다** — `if (IS_ENABLED(CONFIG_FOO)) foo();`는 꺼져 있어도 `foo()` 호출이 **문법 검사와 타입 검사를 받고** 나서 dead code로 제거된다. `#ifdef`로 가린 코드는 그 설정 조합으로 빌드해 보기 전까지 깨져도 모른다.

`IS_REACHABLE(CONFIG_FOO)`는 "내 코드에서 FOO의 심볼을 부를 수 있나"다. FOO가 m인데 나는 내장(y)이면 부팅 시 FOO 모듈이 아직 없으므로 거짓이 된다. 이걸 피하는 Kconfig 관용구가 `depends on FOO || !FOO`(FOO가 m이면 나도 최대 m)다.

---

## 4. 설정 워크플로 — `.config`를 재현 가능하게 다루기

### 4.1 직관 — `.config`는 산출물, defconfig가 소스다

`.config`는 1만 줄이 넘고(랩 커널 11,748줄), 툴체인 버전 같은 **환경에서 자동 감지한 값**(`CONFIG_CC_VERSION_TEXT` 등)까지 담는다. git에 넣고 리뷰하기에는 너무 크고 시끄럽다. 그래서 실무에서는 **기본값과 다른 줄만 모은 최소 파일(defconfig)** 을 소스로 관리하고, `.config`는 그때그때 만든다. 펌웨어로 말하면 defconfig는 `config_prodA.h`, `.config`는 전처리기가 펼친 최종 결과다.

| 명령 | 입력 | 하는 일 |
|---|---|---|
| `make <board>_defconfig` | `arch/$ARCH/configs/<board>_defconfig` | 최소 파일 + 모든 기본값 → `.config` |
| `make defconfig` | 아키텍처 기본 defconfig | arm64는 `arch/arm64/configs/defconfig` |
| `make savedefconfig` | 현재 `.config` | 기본값과 다른 줄만 → `defconfig` 파일 |
| `make olddefconfig` | 기존 `.config` + 바뀐 Kconfig | 새 심볼은 묻지 않고 기본값으로 채움 |
| `make oldconfig` | 같음 | 새 심볼마다 **물어봄** (대화형) |
| `make listnewconfig` | 같음 | 새 심볼 목록만 출력 (값 안 바꿈) |
| `make menuconfig` / `nconfig` | 현재 `.config` | ncurses 메뉴로 편집 |
| `scripts/config` | `.config` 파일 | 스크립트에서 개별 심볼 켜기/끄기/조회 |
| `scripts/kconfig/merge_config.sh` | 기본 + fragment들 | 여러 조각 파일을 합침 |
| `make tinyconfig` / `allnoconfig` | 없음 | 최소 설정에서 시작 |
| `make localmodconfig` | 현재 `.config` + `lsmod` | 지금 로드된 모듈 외 `=m`을 끔 |

### 4.2 예제 6 — 랩 커널은 "defconfig + 3줄"이다 (`savedefconfig` + `diffconfig`)

무엇을 확인하나: 랩 커널의 11,748줄짜리 `.config`를 `savedefconfig`로 최소화하고, 같은 방식으로 최소화한 arm64 기본 defconfig와 비교한다. 랩 README는 "defconfig + IKCONFIG_PROC, OF_OVERLAY, GPIO_SIM, DYNAMIC_DEBUG, …"라고 적고 있다 — 실제로 무엇이 다른지 확인한다.

```sh
cd /var/tmp/E2/src
mkdir -p /var/tmp/E2/labcfg && cp /var/lab/out/linux-build/.config /var/tmp/E2/labcfg/.config
make -s O=/var/tmp/E2/labcfg savedefconfig
make -s O=/var/tmp/E2/defc defconfig && make -s O=/var/tmp/E2/defc savedefconfig
wc -l /var/tmp/E2/defc/defconfig
diff /var/tmp/E2/defc/defconfig /var/tmp/E2/labcfg/defconfig
scripts/diffconfig /var/tmp/E2/defc/.config /var/tmp/E2/labcfg/.config | head -30
```

```text
1755 /var/tmp/E2/defc/defconfig
664a665
> CONFIG_GPIO_SIM=y
1698c1699
< CONFIG_OVERLAY_FS=m
---
> CONFIG_OVERLAY_FS=y
1740a1742
> CONFIG_DYNAMIC_DEBUG=y

-BT_FEATURE_DEBUG n
-E2_DEMO n
-RUST n
-RUSTC_HAS_COERCE_POINTEE y
-RUSTC_HAS_FILE_AS_C_STR y
-RUSTC_HAS_FILE_WITH_NUL y
-RUSTC_HAS_SPAN_FILE y
-RUSTC_HAS_UNNECESSARY_TRANSMUTES y
-RUST_IS_AVAILABLE y
 DYNAMIC_DEBUG n -> y
 DYNAMIC_DEBUG_CORE n -> y
 GPIO_SIM n -> y
 OVERLAY_FS m -> y
 RUSTC_LLVM_VERSION 210108 -> 0
 RUSTC_VERSION 109301 -> 0
+DEV_SYNC_PROBE y
+FW_LOADER_DEBUG y
+IRQ_SIM y
+TEST_DYNAMIC_DEBUG n
```

출력에서 볼 것:

- 사람이 바꾼 것은 **딱 3줄**(`GPIO_SIM=y`, `OVERLAY_FS=y`, `DYNAMIC_DEBUG=y`)이다. `IKCONFIG_PROC`와 `OF_OVERLAY`는 arm64 defconfig에 이미 켜져 있어서 차이로 안 나온다. 1만 줄 `.config`를 비교했으면 이 사실을 찾기 어려웠을 것이다.
- `diffconfig`(전체 `.config` 비교)에는 그 3줄의 **파생 효과**(`DYNAMIC_DEBUG_CORE`, `IRQ_SIM` 등 select로 끌려온 것)와 **환경 차이**가 섞여 나온다. `RUSTC_VERSION 109301 -> 0`은 랩을 빌드할 때는 VM에 rustc가 없었고, 지금 만든 `defc`에서는 rustc가 감지됐다는 뜻이다. **같은 defconfig라도 툴체인에 따라 `.config`가 달라진다** — `.config`를 툴체인과 함께 버전 관리해야 하는 이유다.
- `E2_DEMO n`은 우리가 소스 트리에 Kconfig를 추가했기 때문에 새로 만든 `defc`에만 있는 심볼이다.

`savedefconfig`의 결과를 보드 defconfig로 저장하는 게 정석이다: `cp $O/defconfig arch/arm64/configs/sensorkit_defconfig` → 이후 `make sensorkit_defconfig`.

### 4.3 예제 7 — 새 Kconfig 심볼이 생겼을 때 (`listnewconfig` → `scripts/config` → `olddefconfig`)

무엇을 확인하나: E2 Kconfig를 추가한 뒤 기존 `.config`에 없는 심볼을 찾고, 스크립트로 값을 넣고, 엔진이 나머지를 채우게 한다. 커널 버전을 올렸을 때(예: 6.6 → 6.18) 하는 일과 똑같다.

```sh
cd /var/tmp/E2/src
cp /var/tmp/E2/tuned/.config /var/tmp/E2/tuned.config.before
make -s O=/var/tmp/E2/tuned listnewconfig
scripts/config --file /var/tmp/E2/tuned/.config -e E2_DEMO -e E2_HELLO \
    --set-str E2_HELLO_GREETING "hi Don" --set-val E2_HELLO_PERIOD_MS 250
scripts/config --file /var/tmp/E2/tuned/.config -s E2_HELLO -s E2_HELLO_GREETING
make -s O=/var/tmp/E2/tuned olddefconfig && grep -E "E2_" /var/tmp/E2/tuned/.config
make -s O=/var/tmp/E2/tuned listnewconfig
scripts/diffconfig /var/tmp/E2/tuned.config.before /var/tmp/E2/tuned/.config
```

```text
### listnewconfig
CONFIG_E2_DEMO=n
### scripts/config
y
hi Don
CONFIG_E2_DEMO=y
CONFIG_E2_HELLO=y
CONFIG_E2_HELLO_GREETING="hi Don"
CONFIG_E2_HELLO_PERIOD_MS=250
CONFIG_E2_HELLO_STATS=y
CONFIG_E2_STYLE_PLAIN=y
# CONFIG_E2_STYLE_LOUD is not set
# CONFIG_E2_BUS is not set
# CONFIG_E2_SEL is not set
# CONFIG_E2_IMP is not set
CONFIG_E2_BUS_TIMEOUT_MS=7
### listnewconfig again
### diffconfig
+E2_BUS n
+E2_BUS_TIMEOUT_MS 7
+E2_DEMO y
+E2_HELLO y
+E2_HELLO_GREETING "hi Don"
+E2_HELLO_PERIOD_MS 250
+E2_HELLO_STATS y
+E2_IMP n
+E2_SEL n
+E2_STYLE_LOUD n
+E2_STYLE_PLAIN y
```

출력에서 볼 것:

- 처음 `listnewconfig`는 `E2_DEMO` **하나만** 보여 준다. 나머지 E2 심볼은 `if E2_DEMO` 안에 있어서, 부모가 n인 동안은 "새 심볼"로 셀 대상조차 아니다.
- `scripts/config`는 Kconfig 규칙을 **모르는** 단순 텍스트 편집기다. `-e E2_HELLO`를 썼지만 의존성 검사는 다음 `olddefconfig`가 한다. 그래서 `scripts/config` 다음에는 **반드시** `olddefconfig`를 돌린다(안 돌리면 다음 make의 syncconfig가 대신 돌리지만, 무엇이 바뀌었는지 못 보고 지나간다).
- `E2_HELLO`는 `default m if MODULES`인데 우리가 `-e`(y)로 명시해서 y가 됐다. 이 값으로 실습 2(§6.4)를 내장 빌드한다.

### 4.4 예제 8 — menuconfig 첫 화면과 심볼 검색

무엇을 확인하나: 예제 4와 같은 방법(tmux 캡처)으로 메인 화면과, `/E2_HELLO_PERIOD` 검색 결과를 본다. 마지막 줄(`range`, 정의 위치)이 실무에서 가장 자주 보는 정보다.

```text
 .config - Linux/arm64 6.18.55 Kernel Configuration
 qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq
  lqqqqqqqqqqqqqqqqqqqqq Linux/arm64 6.18.55 Kernel Configuration qqqqqqqqqqqqqqqqqqqqqqk
  x  Arrow keys navigate the menu.  <Enter> selects submenus ---> (or empty submenus    x
  x  ----).  Highlighted letters are hotkeys.  Pressing <Y> includes, <N> excludes, <M> x
  x  modularizes features.  Press <Esc><Esc> to exit, <?> for Help, </> for Search.     x
  x  Legend: [*] built-in  [ ] excluded  <M> module  < > module capable                 x
  x lqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqk x
  x x         General setup  --->                                                     x x
  x x         Platform selection  --->                                                x x
  x x         Kernel Features  --->                                                   x x
...
  x x     [ ] Virtualization  ----                                                    x x
  x x         General architecture-dependent options  --->                            x x
  x x     [*] Enable loadable module support  --->                                    x x
  x x     [ ] Enable the block layer  ----                                            x x
...
  x x     [ ] Networking support  ----                                                x x
  x x         Device Drivers  --->                                                    x x
...
 → Search (E2_HELLO_PERIOD) qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq
  lqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq Search Results qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqk
  x Symbol: E2_HELLO_PERIOD_MS [=300]                                                   x
  x Type  : integer                                                                     x
  x Range : [1 1000]                                                                    x
  x Defined at drivers/misc/e2/Kconfig:20                                               x
  x   Prompt: Fake sampling period (ms)                                                 x
  x   Depends on: E2_DEMO [=y] && E2_HELLO [=m]                                         x
  x   Location:                                                                         x
  x     -> Device Drivers                                                               x
  x       -> Misc devices                                                               x
  x         -> E2 Kconfig/Kbuild demo drivers (E2_DEMO [=y])                            x
  x           -> E2 hello driver (prints a greeting at init) (E2_HELLO [=m])            x
  x (1)         -> Fake sampling period (ms) (E2_HELLO_PERIOD_MS [=300])                x
```

출력에서 볼 것: 이 화면은 §5의 tinyconfig 기반 설정이라 "Networking support", "Enable the block layer"가 꺼져 있다(`[ ]`). 검색 결과 앞의 `(1)`은 단축키다 — `1`을 누르면 그 항목으로 바로 점프한다. 두 화면 사이의 시점 차이 때문에 값이 `E2_HELLO [=m]`, `[=300]`으로 보인다(§8·§9에서 바꾼 뒤 캡처). `make nconfig`는 같은 기능의 다른 UI이고(F8 = 검색), 둘 다 `libncurses-dev`가 필요하다.

### 4.5 예제 9 — config fragment와 `merge_config.sh`

무엇을 확인하나: "기본 설정 + 내 변경 조각"을 파일로 관리하는 방법. Yocto의 `.cfg`, Buildroot의 `BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES`가 전부 이 방식이다. 실습 1에서 쓸 조각 `e2_boot.config`를 tinyconfig 위에 얹는다.

```text
# e2_boot.config — tinyconfig 위에 얹는 "QEMU virt에서 BusyBox initramfs까지" 최소 조각
CONFIG_PRINTK=y
CONFIG_TTY=y
CONFIG_SERIAL_AMBA_PL011=y
CONFIG_SERIAL_AMBA_PL011_CONSOLE=y
CONFIG_BLK_DEV_INITRD=y
CONFIG_RD_GZIP=y
CONFIG_BINFMT_ELF=y
CONFIG_BINFMT_SCRIPT=y
CONFIG_DEVTMPFS=y
CONFIG_PROC_FS=y
CONFIG_SYSFS=y
CONFIG_MODULES=y
CONFIG_MODULE_UNLOAD=y
```

```sh
cd /var/tmp/E2/src && cp /var/tmp/E2/tiny/.config /var/tmp/E2/tiny.base
scripts/kconfig/merge_config.sh -m -O /var/tmp/E2/tuned /var/tmp/E2/tiny.base /Users/donh/.elinux_scratch/E2/e2_boot.config 2>&1 | tail -15
make O=/var/tmp/E2/tuned olddefconfig
```

```text
Value of CONFIG_PROC_FS is redefined by fragment /Users/donh/.elinux_scratch/E2/e2_boot.config:
Previous value: # CONFIG_PROC_FS is not set
New value: CONFIG_PROC_FS=y

Value of CONFIG_SYSFS is redefined by fragment /Users/donh/.elinux_scratch/E2/e2_boot.config:
Previous value: # CONFIG_SYSFS is not set
New value: CONFIG_SYSFS=y

Value of CONFIG_MODULES is redefined by fragment /Users/donh/.elinux_scratch/E2/e2_boot.config:
Previous value: # CONFIG_MODULES is not set
New value: CONFIG_MODULES=y

#
# merged configuration written to /var/tmp/E2/tuned/.config (needs make)
#
```

`-m`는 "텍스트로만 합치고 Kconfig 검사는 하지 마라"는 뜻이라 뒤에 `olddefconfig`를 돌렸다. 13개 심볼 모두 결과 `.config`에 `=y`로 들어간 것을 grep으로 확인했다.

같은 일을 make 타깃으로 할 수도 있다. `kernel/configs/`나 `arch/$ARCH/configs/`에 있는 `*.config` 파일은 **이름 그대로 make 타깃**이 된다. `make tinyconfig` 자체가 이 방식으로 만들어져 있다.

```sh
grep -n -A3 "^tinyconfig:" scripts/kconfig/Makefile; cat kernel/configs/tiny-base.config kernel/configs/tiny.config
cp /Users/donh/.elinux_scratch/E2/e2_boot.config kernel/configs/e2_boot.config
make O=/var/tmp/E2/frag tinyconfig e2_boot.config 2>&1 | grep -v "^make\[" | tail -3
scripts/diffconfig /var/tmp/E2/tuned.config.before /var/tmp/E2/frag/.config
```

```text
116:tinyconfig:
117-	$(Q)KCONFIG_ALLCONFIG=kernel/configs/tiny-base.config $(MAKE) -f $(srctree)/Makefile allnoconfig
118-	$(Q)$(MAKE) -f $(srctree)/Makefile tiny.config
119-
CONFIG_EXPERT=y
CONFIG_CC_OPTIMIZE_FOR_SIZE=y
CONFIG_KERNEL_XZ=y
CONFIG_SLUB=y
CONFIG_SLUB_TINY=y
CONFIG_LD_DEAD_CODE_DATA_ELIMINATION=y
#
# configuration written to .config
#
+E2_DEMO n
```

출력에서 볼 것: `make tinyconfig e2_boot.config` 한 줄의 결과가 `merge_config.sh` 경로(`tuned.config.before`)와 **완전히 같다**(차이는 그 사이 소스 트리에 추가된 `E2_DEMO` 심볼뿐). 보드별 조각을 `kernel/configs/`에 두는 대신 Yocto/Buildroot에서는 레이어 쪽에 두고 같은 스크립트를 부른다.

### 4.6 함정 — fragment의 값이 조용히 사라진다

무엇을 확인하나: 존재하지 않는 심볼과, 의존성(`SERIAL_AMBA_PL011`)을 빼먹은 심볼을 fragment에 넣으면 어떻게 되나.

```sh
echo "CONFIG_I2C_FOO_DOES_NOT_EXIST=y" > /var/tmp/E2/bad.config
echo "CONFIG_SERIAL_AMBA_PL011_CONSOLE=y" >> /var/tmp/E2/bad.config
cp /var/tmp/E2/tiny/.config /var/tmp/E2/bad/base.config
scripts/kconfig/merge_config.sh -O /var/tmp/E2/bad -m /var/tmp/E2/bad/base.config /var/tmp/E2/bad.config >/dev/null
make -s O=/var/tmp/E2/bad olddefconfig; grep -E "PL011|FOO" /var/tmp/E2/bad/.config
echo "### without -m"
scripts/kconfig/merge_config.sh -O /var/tmp/E2/bad /var/tmp/E2/bad/base.config /var/tmp/E2/bad.config 2>&1 | grep -A3 "Value requested"
```

```text
### without -m
Value requested for CONFIG_I2C_FOO_DOES_NOT_EXIST not in final .config
Requested value:  CONFIG_I2C_FOO_DOES_NOT_EXIST=y
Actual value:     

Value requested for CONFIG_SERIAL_AMBA_PL011_CONSOLE not in final .config
Requested value:  CONFIG_SERIAL_AMBA_PL011_CONSOLE=y
Actual value:     
```

출력에서 볼 것: `-m` + `olddefconfig` 경로는 **아무 말 없이** 두 값을 버렸다(grep 결과 없음). `-m` 없이 돌리면 `merge_config.sh`가 마지막에 "요청한 값이 최종 `.config`에 없다"고 알려 준다. `PL011_CONSOLE`은 `SERIAL_AMBA_PL011`이 꺼져 있어서 사라졌다 — **fragment에는 의존 대상까지 다 적어야 한다.** CI에서는 `-m` 없이 돌리거나, 빌드 후 `.config`에 원하는 줄이 있는지 grep으로 검사하는 단계를 둔다.

### 4.7 예제 10 — tinyconfig와 allnoconfig, 그리고 localmodconfig

무엇을 확인하나: "가장 작은 설정" 두 가지가 왜 다르고, 이미 돌아가는 시스템에서 설정을 줄이는 `localmodconfig`가 무엇을 하는지 본다.

```sh
cd /var/tmp/E2/src && make -s O=/var/tmp/E2/alln allnoconfig
for d in alln tiny tuned defc; do printf "%-6s y=%4d m=%4d lines=%5d\n" $d \
  $(grep -c "=y$" /var/tmp/E2/$d/.config) $(grep -c "=m$" /var/tmp/E2/$d/.config) $(wc -l < /var/tmp/E2/$d/.config); done
scripts/diffconfig /var/tmp/E2/alln/.config /var/tmp/E2/tiny/.config | grep -E "^ (PRINTK|TTY|PROC_FS|SYSFS|EXPERT|MULTIUSER|FUTEX|EPOLL|BLOCK|CC_OPTIMIZE_FOR_SIZE) "
```

```text
alln   y= 489 m=   0 lines= 1713
tiny   y= 427 m=   0 lines= 1590
tuned  y= 510 m=   1 lines= 1868
defc   y=3295 m=1273 lines=11753
 BLOCK y -> n
 CC_OPTIMIZE_FOR_SIZE n -> y
 EPOLL y -> n
 EXPERT n -> y
 FUTEX y -> n
 MULTIUSER y -> n
 PRINTK y -> n
 PROC_FS y -> n
 SYSFS y -> n
 TTY y -> n
```

출력에서 볼 것: **allnoconfig가 tinyconfig보다 크다**(y 489개 vs 427개). allnoconfig는 "보이는 질문에 전부 n"인데, `PRINTK`·`TTY`·`PROC_FS`·`FUTEX` 같은 핵심 기능은 §1.2에서 본 `bool "…" if EXPERT` 형태라 `EXPERT=n`이면 **질문 자체가 안 보이고 기본값 y로 남는다**. tinyconfig는 `tiny-base.config`로 `EXPERT=y`를 먼저 켜서 그 질문들을 드러낸 뒤 전부 n으로 답한다. 그래서 tinyconfig 커널은 printk도 tty도 없어서 **부팅해도 아무것도 출력하지 않는다** — 실습 1에서 조각을 얹는 이유다.

`localmodconfig`는 반대 방향의 도구다. 이미 잘 도는 배포판/BSP 설정에서 출발해서, **지금 `lsmod`에 보이는 모듈만 남기고** 나머지 `=m`을 끈다. 이 VM의 `lsmod`(59줄)를 랩 커널 설정에 적용해 봤다.

```sh
lsmod > /var/tmp/E2/host_lsmod.txt
cp /var/lab/out/linux-build/.config /var/tmp/E2/lmc/.config
yes "" | make O=/var/tmp/E2/lmc LSMOD=/var/tmp/E2/host_lsmod.txt localmodconfig > /var/tmp/E2/logs/lmc.log 2>&1
grep -v "^make\[" /var/tmp/E2/logs/lmc.log | grep -v "^\s*$" | tail -6
printf "before m=%d  after m=%d  after y=%d\n" $(grep -c "=m$" /var/lab/out/linux-build/.config) \
  $(grep -c "=m$" /var/tmp/E2/lmc/.config) $(grep -c "=y$" /var/tmp/E2/lmc/.config)
```

```text
Rust support (RUST) [N/y/?] (NEW) 
*
* E2 Kconfig/Kbuild demo drivers
*
E2 Kconfig/Kbuild demo drivers (E2_DEMO) [N/y/?] (NEW) 
#
# configuration written to .config
before m=1272  after m=28  after y=3068
```

출력에서 볼 것: 모듈이 1,272개에서 28개로 줄었다. 그리고 `localmodconfig`는 내부적으로 `oldconfig`를 돌려서 **새 심볼마다 질문**한다(`(NEW)` 줄). 처음에 `yes ""` 없이 돌렸다가 입력을 기다리며 멈춘 채 2분 타임아웃에 걸렸다 — 스크립트·CI에서 쓸 때는 `yes "" |`로 기본값을 공급하거나 이후 `olddefconfig`를 쓴다. 주의: `localmodconfig`는 "지금 안 꽂혀 있는 장치"의 드라이버도 끄므로, USB 장치를 나중에 꽂으면 드라이버가 없다.

### 4.8 예제 11 — 실행 중인 커널의 설정 꺼내기 (`/proc/config.gz`, `extract-ikconfig`)

무엇을 확인하나: 보드에서 "이 커널이 무슨 설정으로 빌드됐지?"를 알아내는 두 방법. 랩 커널은 `CONFIG_IKCONFIG_PROC=y`로 빌드되어 있다. QEMU 게스트에서 직접 읽는다.

```sh
timeout 120 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -kernel /var/lab/out/Image -initrd /var/lab/out/initramfs.cpio.gz \
  -append 'console=ttyAMA0 quiet LAB_CMD=ls~-l~/proc/config.gz;~zcat~/proc/config.gz~|~head~-5;~zcat~/proc/config.gz~|~grep~-E~"^CONFIG_(IKCONFIG|IKCONFIG_PROC|SERIAL_AMBA_PL011|GPIO_SIM|HZ)=";~zcat~/proc/config.gz~|~wc~-l' 2>&1 | tr -d '\r'
```

```text
lab initramfs: 6.18.55 up 3.71s
-r--r--r--    1 0        0            67903 Oct 10 20:48 /proc/config.gz
#
# Automatically generated file; DO NOT EDIT.
# Linux/arm64 6.18.55 Kernel Configuration
#
CONFIG_CC_VERSION_TEXT="gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0"
CONFIG_IKCONFIG=y
CONFIG_IKCONFIG_PROC=y
CONFIG_HZ=250
CONFIG_SERIAL_AMBA_PL011=y
CONFIG_GPIO_SIM=y
11748
[    5.624333] reboot: Power down
```

`/proc`가 없거나 부팅이 안 되는 이미지라면 파일에서 꺼낸다. `CONFIG_IKCONFIG=y`면 `.config`가 gzip으로 이미지 안에 들어 있다.

```sh
cd /var/tmp/E2/src
scripts/extract-ikconfig /var/lab/out/Image | sed -n 3,4p; scripts/extract-ikconfig /var/lab/out/Image | wc -l
scripts/extract-ikconfig /var/tmp/E2/tuned/arch/arm64/boot/Image; echo rc=$?
```

```text
# Linux/arm64 6.18.55 Kernel Configuration
#
11748
extract-ikconfig: Cannot find kernel config.
rc=1
```

출력에서 볼 것: 게스트의 `/proc/config.gz`(67,903바이트 압축)를 풀면 정확히 빌드 트리의 `.config`와 같은 11,748줄이다. tinyconfig 기반 커널은 IKCONFIG가 꺼져 있어서 꺼낼 수 없다. 양산 보드에서는 보안·크기 때문에 끄기도 하는데, 그러면 **빌드 산출물과 함께 `.config`를 반드시 보관**해야 한다. 디버깅 첫 질문이 거의 항상 "그 커널 `.config` 있어요?"다.

---

## 5. 실습 1 — tinyconfig에서 QEMU 부팅 가능한 최소 커널까지

### 5.1 무엇을 하나

목표: 아무것도 출력하지 않는 tinyconfig에 **최소한의 옵션만** 더해서 랩 QEMU `virt` 머신에서 BusyBox initramfs를 띄운다. 그리고 랩의 defconfig 기반 커널과 빌드 시간·크기·부팅 시간을 비교한다. 이건 실제로 "부팅 시간 2초 이내", "플래시 16 MB 안에 커널+rootfs" 같은 요구를 받았을 때 하는 작업의 축소판이다.

필요한 옵션을 고른 이유는 부팅 경로를 따라가면 나온다(C6, C7).

| 단계 | 필요한 것 | 옵션 |
|---|---|---|
| 커널이 메시지를 낸다 | printk, 콘솔 | `PRINTK`, `TTY`, `SERIAL_AMBA_PL011` + `_CONSOLE` |
| initramfs를 푼다 | initrd 지원, gzip 해제 | `BLK_DEV_INITRD`, `RD_GZIP` |
| `/init`을 실행한다 | ELF 로더, `#!/bin/sh` 스크립트 로더 | `BINFMT_ELF`, `BINFMT_SCRIPT` |
| `/init`이 mount한다 | proc, sysfs, devtmpfs | `PROC_FS`, `SYSFS`, `DEVTMPFS` |
| 실습 2·3용 | 모듈 | `MODULES`, `MODULE_UNLOAD` |

타이머(`ARM_ARCH_TIMER`), 인터럽트 컨트롤러(`ARM_GIC_V3` 등), PSCI(전원 끄기), AMBA 버스, OF(Device Tree)는 `arch/arm64/Kconfig`의 `config ARM64`가 **`select`로 무조건 켠다**. 그래서 조각에 안 적어도 된다.

```sh
grep -n "select ARM_GIC\|select ARM_ARCH_TIMER\|select ARM_AMBA\|select OF$\|select PSCI\|ARM_PSCI_FW" arch/arm64/Kconfig | head
```

```text
125:	select ARM_AMBA
126:	select ARM_ARCH_TIMER
127:	select ARM_GIC
129:	select ARM_GIC_V2M if PCI
130:	select ARM_GIC_V3
131:	select ARM_GIC_V3_ITS if PCI
132:	select ARM_GIC_V5
133:	select ARM_PSCI_FW
267:	select OF
```

랩 initramfs의 `/init`이 무엇을 요구하는지도 확인했다 — `#!/bin/sh` 스크립트이고 proc/sysfs/devtmpfs를 mount한다.

```text
#!/bin/sh
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev 2>/dev/null
mount -t debugfs debugfs /sys/kernel/debug 2>/dev/null
echo "lab initramfs: $(uname -r) up $(cut -d' ' -f1 /proc/uptime)s"
# LAB_CMD=... on the kernel cmdline runs one command then powers off (for scripted examples)
for a in $(cat /proc/cmdline); do case $a in LAB_CMD=*) C="${a#LAB_CMD=}";; esac; done
if [ -n "$C" ]; then sh -c "$(echo "$C" | tr '~' ' ')"; poweroff -f; fi
exec /bin/sh
```

### 5.2 예제 12 — tinyconfig 빌드, 조각 얹어 다시 빌드

무엇을 확인하나: 두 설정의 **처음부터 빌드** 시간과 산출물 크기. tinyconfig는 `make Image`만, tuned는 `make Image modules`를 했다.

```sh
cd /var/tmp/E2/src
make O=/var/tmp/E2/tiny tinyconfig
grep -c "^CONFIG_" /var/tmp/E2/tiny/.config; wc -l /var/tmp/E2/tiny/.config /var/lab/out/linux-build/.config
/usr/bin/time -f "%e s elapsed" make O=/var/tmp/E2/tiny -j4 Image 2>&1 | tail -4
ls -l /var/tmp/E2/tiny/arch/arm64/boot/Image /var/tmp/E2/tiny/vmlinux
# 조각을 얹은 tuned (예제 9의 merge 후)
/usr/bin/time -f "%e s elapsed" make O=/var/tmp/E2/tuned -j4 Image modules > /var/tmp/E2/logs/tuned_build.log 2>&1
tail -3 /var/tmp/E2/logs/tuned_build.log; ls -l /var/tmp/E2/tuned/arch/arm64/boot/Image
```

```text
473
  1590 /var/tmp/E2/tiny/.config
 11748 /var/lab/out/linux-build/.config
 13338 total
  GEN     modules.builtin
  OBJCOPY arch/arm64/boot/Image
make[1]: Leaving directory '/var/tmp/E2/tiny'
254.35 s elapsed
-rwxrwxr-x 1 donh donh 1961992 Oct 10 13:41 /var/tmp/E2/tiny/arch/arm64/boot/Image
-rwxrwxr-x 1 donh donh 2621032 Oct 10 13:41 /var/tmp/E2/tiny/vmlinux
  OBJCOPY arch/arm64/boot/Image
make[1]: Leaving directory '/var/tmp/E2/tuned'
302.66 s elapsed
-rwxrwxr-x 1 donh donh 3434504 Oct 10 13:47 /var/tmp/E2/tuned/arch/arm64/boot/Image
```

출력에서 볼 것: 최소 커널도 처음 빌드는 4~5분 걸린다. 이 중 상당 부분은 설정과 무관한 고정 비용 — 호스트 도구(`scripts/`의 `fixdep`, `modpost`, `dtc`, Kconfig 파서)를 먼저 빌드하고, 생성 헤더를 만들고, 수백 개의 공통 코어 파일(`kernel/`, `mm/`, `lib/`)을 컴파일하는 비용이다. 빌드 트리 규모로 비교하면 차이가 확실하다.

```sh
for d in /var/tmp/E2/tiny /var/tmp/E2/tuned /var/lab/build/linux; do
  printf "%s o=%d ko=%d\n" $d $(find $d -name "*.o" ! -name "*.mod.o" | wc -l) $(find $d -name "*.ko" | wc -l); done
du -sh /var/tmp/E2/tiny /var/tmp/E2/tuned /var/lab/build/linux
```

```text
/var/tmp/E2/tiny o=514 ko=0
/var/tmp/E2/tuned o=672 ko=1
/var/lab/build/linux o=9231 ko=1423
52M	/var/tmp/E2/tiny
79M	/var/tmp/E2/tuned
3.1G	/var/lab/build/linux
```

랩 defconfig 트리는 오브젝트 9,231개 + 모듈 1,423개, 3.1 GB다. 이 VM에서 `-j4`로 처음부터 빌드하면 수십 분 단위가 걸린다(이 노트에서는 다시 빌드하지 않았다 — 랩 트리를 그대로 비교 대상으로 썼다).

### 5.3 예제 13 — 최소 커널을 랩 initramfs로 부팅

무엇을 확인하나: 13줄짜리 조각만 얹은 커널이 실제로 콘솔을 띄우고 BusyBox `/init`을 실행하는지 본다. 부팅 로그는 핵심 줄만 남기고 `...`로 줄였다.

```sh
cd /var/tmp/E2 && time timeout 90 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -kernel tuned/arch/arm64/boot/Image -initrd /var/lab/out/initramfs.cpio.gz \
  -append "console=ttyAMA0 LAB_CMD=uname~-a;~cat~/proc/cmdline;~ls~/dev;~free" > logs/tuned_boot.log 2>&1
```

```text
Booting Linux on physical CPU 0x0000000000 [0x411fd070]
Linux version 6.18.55 (donh@lima-elinux) (gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0, GNU ld (GNU Binutils for Ubuntu) 2.46) #1 SMP Sat Oct 10 13:47:27 PDT 2026
random: crng init done
Machine model: linux,dummy-virt
...
psci: PSCIv1.1 detected in firmware.
...
Kernel command line: console=ttyAMA0 LAB_CMD=uname~-a;~cat~/proc/cmdline;~ls~/dev;~free
...
arch_timer: cp15 timer running at 62.50MHz (virt).
...
Memory: 506792K/524288K available (1856K kernel code, 602K rwdata, 440K rodata, 320K init, 359K bss, 16676K reserved, 0K cma-reserved)
devtmpfs: initialized
...
Serial: AMBA PL011 UART driver
9000000.pl011: ttyAMA0 at MMIO 0x9000000 (irq = 13, base_baud = 0) is a PL011 rev1
printk: console [ttyAMA0] enabled
clocksource: Switched to clocksource arch_sys_counter
workingset: timestamp_bits=62 max_order=17 bucket_order=0
Unpacking initramfs...
Freeing initrd memory: 1180K
clk: Disabling unused clocks
Freeing unused kernel memory: 320K
Run /init as init process
lab initramfs: 6.18.55 up 1.52s
Linux (none) 6.18.55 #1 SMP Sat Oct 10 13:47:27 PDT 2026 aarch64 GNU/Linux
console=ttyAMA0 LAB_CMD=uname~-a;~cat~/proc/cmdline;~ls~/dev;~free
...
Mem:         509112        5544      499784           0        3784      495512
Swap:             0           0           0
reboot: Power down

real	0m2.327s
```

출력에서 볼 것:

- `kernel code 1856K` — 랩 defconfig 커널은 같은 줄이 수십 MB 단위다. 커널 코드 대부분이 드라이버라는 뜻이다.
- 타임스탬프(`[    0.000000]`)가 없다. `PRINTK_TIME`도 tinyconfig에서 꺼졌기 때문이다. 부팅 시간 측정을 하려면 `CONFIG_PRINTK_TIME=y`를 조각에 더한다(C7).
- PL011이 콘솔로 잡히기 **전**의 메시지(`Booting Linux …`)도 보인다. printk 링버퍼에 쌓여 있다가 콘솔이 등록되는 순간 한꺼번에 출력된 것이다(early console 없이도 이렇게 보인다).
- 부팅부터 poweroff까지 2.3초. 같은 방식으로 두 번씩 재 본 벽시계 시간은 아래와 같다(`quiet`, `LAB_CMD=true`).

```text
lab 3.74 s
tuned 1.45 s
lab 4.19 s
tuned 2.48 s
```

TCG 에뮬레이션이라 절대값은 의미가 적지만, 같은 조건에서 defconfig 커널이 1.5~2초 더 걸린다. 수백 개 드라이버의 initcall과 probe 시도 비용이다(C7).

```svg
<svg viewBox="0 0 680 256" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="20" text-anchor="middle" font-size="13" font-weight="bold">arm64 커널 이미지 크기 (MB, 실측)</text>
  <text x="162" y="62" text-anchor="end" font-size="12">tinyconfig</text>
  <rect x="170" y="40" width="18.4" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="194.4" y="52" font-size="12">Image 1.96</text>
  <rect x="170" y="60" width="7.4" height="16" fill="#e08a3c" fill-opacity="0.75"/>
  <text x="183.4" y="72" font-size="12">Image.gz 0.79</text>
  <text x="162" y="120" text-anchor="end" font-size="12">tinyconfig + e2_boot</text>
  <rect x="170" y="98" width="32.1" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="208.1" y="110" font-size="12">Image 3.43</text>
  <rect x="170" y="118" width="11.5" height="16" fill="#e08a3c" fill-opacity="0.75"/>
  <text x="187.5" y="130" font-size="12">Image.gz 1.23</text>
  <text x="162" y="178" text-anchor="end" font-size="12">lab (defconfig+3)</text>
  <rect x="170" y="156" width="400.0" height="16" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="576.0" y="168" font-size="12">Image 42.74</text>
  <rect x="170" y="176" width="142.7" height="16" fill="#e08a3c" fill-opacity="0.75"/>
  <text x="318.7" y="188" font-size="12">Image.gz 15.25</text>
  <line x1="170" y1="34" x2="170" y2="208" stroke="currentColor"/>
  <line x1="170.0" y1="208" x2="170.0" y2="213" stroke="currentColor"/>
  <text x="170.0" y="226" text-anchor="middle" font-size="12">0</text>
  <line x1="263.6" y1="208" x2="263.6" y2="213" stroke="currentColor"/>
  <text x="263.6" y="226" text-anchor="middle" font-size="12">10</text>
  <line x1="357.2" y1="208" x2="357.2" y2="213" stroke="currentColor"/>
  <text x="357.2" y="226" text-anchor="middle" font-size="12">20</text>
  <line x1="450.8" y1="208" x2="450.8" y2="213" stroke="currentColor"/>
  <text x="450.8" y="226" text-anchor="middle" font-size="12">30</text>
  <line x1="544.3" y1="208" x2="544.3" y2="213" stroke="currentColor"/>
  <text x="544.3" y="226" text-anchor="middle" font-size="12">40</text>
  <rect x="440" y="234" width="12" height="12" fill="#4a7bd0" fill-opacity="0.75"/><text x="458" y="244" font-size="12">Image</text>
  <rect x="520" y="234" width="12" height="12" fill="#e08a3c" fill-opacity="0.75"/><text x="538" y="244" font-size="12">Image.gz (gzip -9)</text>
</svg>
```
그림 4 — 같은 6.18.55 소스에서 설정만 바꾼 세 커널의 크기. tuned(부팅 가능한 최소)는 defconfig의 약 8%다. Image.gz는 tuned만 `make Image.gz`로 만들었고, 나머지 둘은 같은 옵션(`gzip -9 -n`)으로 직접 압축해 쟀다. 랩 `vmlinux`는 디버그 정보(`DEBUG_INFO=y`) 때문에 162 MB지만 `Image`에는 디버그 정보가 들어가지 않는다(§10).

| 항목 | tinyconfig | tuned (+13줄) | 랩 (defconfig+3) |
|---|---|---|---|
| `.config` `=y` / `=m` | 427 / 0 | 510 / 1 | 3,296 / 1,272 |
| 오브젝트 / 모듈 | 514 / 0 | 672 / 1 | 9,231 / 1,423 |
| 처음부터 빌드 (`-j4`) | 254 s | 303 s | 측정 안 함 |
| `Image` / `Image.gz` | 1.96 / 0.79 MB | 3.43 / 1.23 MB | 42.74 / 15.25 MB |
| QEMU 부팅→poweroff | 콘솔 없음 | 1.5–2.5 s | 3.7–4.2 s |

---

## 6. Kbuild — 무엇을 어디에 링크할지 선언하는 Makefile

### 6.1 직관 — 드라이버 Makefile은 "목록"만 적는다

일반 Makefile은 규칙(`target: deps` + 명령)을 적는다. Kbuild의 드라이버 Makefile에는 규칙이 **하나도 없다**. 변수 목록만 적는다. "어떻게 컴파일하고 링크하는지"는 `scripts/Makefile.build`가 전부 정한다. 펌웨어로 말하면 "소스 목록 파일 + 공용 빌드 템플릿" 구조다.

| 변수 | 뜻 |
|---|---|
| `obj-y += foo.o` | foo.o를 이 디렉터리의 `built-in.a`에 넣는다 → 결국 `vmlinux`에 링크 |
| `obj-m += foo.o` | foo.o로 모듈 `foo.ko`를 만든다 |
| `obj-$(CONFIG_FOO) += foo.o` | 위 둘을 `.config`가 정하게 한다 (`auto.conf`에서 y/m/빈값으로 전개) |
| `obj-y += sub/` | 하위 디렉터리로 **재귀** — `sub/built-in.a`를 내 `built-in.a`에 포함 |
| `foo-y := a.o b.o` (옛 표기 `foo-objs`) | composite: 여러 `.o`를 합쳐 `foo.o` 하나로 |
| `foo-$(CONFIG_BAR) += c.o` | composite의 일부를 조건부로 |
| `lib-y += x.o` | `lib.a`에 넣는다 (주로 `lib/`, `arch/*/lib/`) |
| `ccflags-y += …` | 이 디렉터리의 모든 C 파일에 컴파일 플래그 추가 |
| `CFLAGS_foo.o += …` | 파일 하나에만 플래그 추가 |
| `subdir-ccflags-y` | 하위 디렉터리까지 상속되는 플래그 |

`obj-$(CONFIG_FOO)` 트릭이 핵심이다. `auto.conf`가 include되면 `CONFIG_FOO=y`면 `obj-y`, `=m`이면 `obj-m`, n이면 변수 자체가 없어서 `obj-`가 되어 **아무도 읽지 않는 변수**가 된다. 조건문 없이 한 줄로 세 경우를 처리한다.

### 6.2 예제 14 — E2 드라이버의 Kbuild 파일과 소스

무엇을 확인하나: composite 모듈(`e2_hello.ko` = `e2_main.o` + 조건부 `e2_stats.o`), 디렉터리 전체 플래그(`ccflags-y`), 파일 하나 플래그(`CFLAGS_e2_stats.o`)를 다 쓰는 작은 드라이버를 만든다. 모듈 이름 `e2_hello`와 같은 이름의 `.c`가 없어야 composite가 된다는 점에 주의한다(`e2_hello.c`가 있으면 Kbuild가 단일 파일 모듈로 해석한다).

```make
# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_E2_HELLO)			+= e2_hello.o
e2_hello-y				:= e2_main.o
e2_hello-$(CONFIG_E2_HELLO_STATS)	+= e2_stats.o
ccflags-y				+= -DE2_BUILD_TAG='"ccflags-y"'
CFLAGS_e2_stats.o			+= -DDEBUG
```

```c
// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include "e2_stats.h"

#ifdef CONFIG_E2_STYLE_LOUD
#define E2_STYLE "LOUD"
#else
#define E2_STYLE "plain"
#endif

#ifdef CONFIG_E2_HELLO		/* the classic trap: false when =m */
#define E2_IFDEF 1
#else
#define E2_IFDEF 0
#endif

static int __init e2_hello_init(void)
{
	pr_info("e2_hello: \"%s\" period=%dms style=%s tag=%s\n",
		CONFIG_E2_HELLO_GREETING, CONFIG_E2_HELLO_PERIOD_MS,
		E2_STYLE, E2_BUILD_TAG);
	pr_info("e2_hello: IS_BUILTIN=%d IS_MODULE=%d IS_ENABLED=%d #ifdef=%d\n",
		IS_BUILTIN(CONFIG_E2_HELLO), IS_MODULE(CONFIG_E2_HELLO),
		IS_ENABLED(CONFIG_E2_HELLO), E2_IFDEF);
	e2_stats_report();
	return 0;
}

static void __exit e2_hello_exit(void)
{
	pr_info("e2_hello: bye\n");
}

module_init(e2_hello_init);
module_exit(e2_hello_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("E2 Kconfig/Kbuild demo");
```

```c
/* SPDX-License-Identifier: GPL-2.0 */
/* e2_stats.h */
#ifndef E2_STATS_H
#define E2_STATS_H
#ifdef CONFIG_E2_HELLO_STATS
extern int e2_stats_count;
void e2_stats_report(void);
#else
static inline void e2_stats_report(void) { }
#endif
#endif
```

```c
// SPDX-License-Identifier: GPL-2.0
/* e2_stats.c */
#include <linux/export.h>
#include <linux/jiffies.h>
#include <linux/printk.h>
#include "e2_stats.h"

int e2_stats_count;
EXPORT_SYMBOL_GPL(e2_stats_count);

void e2_stats_report(void)
{
	e2_stats_count++;
	pr_info("e2_stats: report #%d, HZ=%d\n", e2_stats_count, HZ);
	pr_debug("e2_stats: pr_debug is live because CFLAGS_e2_stats.o has -DDEBUG\n");
}
```

`e2_stats.h`의 "꺼지면 빈 `static inline`" 패턴은 커널 헤더 전체에서 쓰는 관용구다. 호출하는 쪽(`e2_main.c`)에 `#ifdef`가 하나도 필요 없다.

### 6.3 재귀와 링크 순서 — 누가 먼저 링크되나

```svg
<svg viewBox="0 0 680 350" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="250" y="10" width="180" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="35" text-anchor="middle" font-size="12">최상위 Kbuild (obj-y 순서)</text>
  <rect x="10" y="80" width="90" height="34" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="55" y="102" text-anchor="middle" font-size="12">init/</text>
  <rect x="110" y="80" width="90" height="34" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="155" y="102" text-anchor="middle" font-size="12">usr/</text>
  <rect x="210" y="80" width="90" height="34" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="255" y="102" text-anchor="middle" font-size="12">arch/arm64/</text>
  <rect x="310" y="80" width="110" height="34" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="365" y="102" text-anchor="middle" font-size="12">kernel/ mm/ …</text>
  <rect x="430" y="80" width="90" height="34" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="475" y="102" text-anchor="middle" font-size="12">drivers/</text>
  <rect x="530" y="80" width="140" height="34" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="600" y="102" text-anchor="middle" font-size="12">sound/ net/ …</text>
  <line x1="300" y1="50" x2="55" y2="78" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="320" y1="50" x2="255" y2="78" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="380" y1="50" x2="475" y2="78" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="400" y1="50" x2="600" y2="78" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="380" y="150" width="90" height="34" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="425" y="172" text-anchor="middle" font-size="12">tty/serial/</text>
  <rect x="480" y="150" width="90" height="34" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="525" y="172" text-anchor="middle" font-size="12">misc/</text>
  <rect x="480" y="214" width="90" height="34" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="525" y="236" text-anchor="middle" font-size="12">misc/e2/</text>
  <line x1="475" y1="114" x2="425" y2="148" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="485" y1="114" x2="525" y2="148" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="525" y1="184" x2="525" y2="212" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="590" y="228" font-size="12">e2_main.o</text>
  <text x="590" y="244" font-size="12">e2_stats.o</text>
  <rect x="10" y="150" width="340" height="98" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="20" y="172" font-size="12">디렉터리마다 Makefile.build 한 번:</text>
  <text x="20" y="192" font-size="12">obj-y의 .o 와 하위 built-in.a 를 모아</text>
  <text x="20" y="212" font-size="12">자기 built-in.a (thin archive) 생성</text>
  <text x="20" y="232" font-size="12">순서 = Makefile에 적은 순서 그대로</text>
  <rect x="10" y="282" width="150" height="52" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="85" y="304" text-anchor="middle" font-size="12">built-in.a</text>
  <text x="85" y="322" text-anchor="middle" font-size="12">+ lib/lib.a</text>
  <rect x="190" y="282" width="140" height="52" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="260" y="304" text-anchor="middle" font-size="12">vmlinux.a</text>
  <text x="260" y="322" text-anchor="middle" font-size="12">(613개 .o)</text>
  <rect x="360" y="282" width="140" height="52" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="430" y="304" text-anchor="middle" font-size="12">vmlinux.o</text>
  <text x="430" y="322" text-anchor="middle" font-size="12">whole-archive</text>
  <rect x="530" y="282" width="140" height="52" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="600" y="304" text-anchor="middle" font-size="12">vmlinux</text>
  <text x="600" y="322" text-anchor="middle" font-size="12">+ System.map</text>
  <line x1="160" y1="308" x2="188" y2="308" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="330" y1="308" x2="358" y2="308" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="500" y1="308" x2="528" y2="308" stroke="currentColor" marker-end="url(#ah5)"/>
</svg>
```
그림 5 — Kbuild 재귀와 링크 순서. 최상위 `Kbuild` 파일의 `obj-y` 나열 순서(init → usr → arch → kernel/mm/fs … → drivers → sound/net …)가 그대로 `vmlinux.a`의 오브젝트 순서가 되고, 이것이 같은 initcall 레벨 안에서의 **실행 순서**를 정한다.

예제 15 — 무엇을 확인하나: 최상위 순서, 각 단계의 아카이브 내용, 그리고 그 순서가 `System.map`과 initcall에 어떻게 남는지 본다. (E2_HELLO=y로 빌드한 직후 상태.)

```sh
cd /var/tmp/E2/src && grep -n "^obj-" Kbuild
cd /var/tmp/E2/tuned
ar t drivers/misc/e2/built-in.a; ar t vmlinux.a | head -12; ar t vmlinux.a | wc -l
ar t vmlinux.a | grep -n "e2/\|amba-pl011"
grep -n "e2_" System.map
```

```text
90:obj-y			+= init/
91:obj-y			+= usr/
92:obj-y			+= arch/$(SRCARCH)/
93:obj-y			+= $(ARCH_CORE)
94:obj-y			+= kernel/
95:obj-y			+= certs/
96:obj-y			+= mm/
97:obj-y			+= fs/
98:obj-y			+= ipc/
99:obj-y			+= security/
100:obj-y			+= crypto/
101:obj-$(CONFIG_BLOCK)	+= block/
102:obj-$(CONFIG_IO_URING)	+= io_uring/
103:obj-$(CONFIG_RUST)	+= rust/
104:obj-y			+= $(ARCH_LIB)
105:obj-y			+= drivers/
106:obj-y			+= sound/
107:obj-$(CONFIG_SAMPLES)	+= samples/
108:obj-$(CONFIG_NET)	+= net/
109:obj-y			+= virt/
110:obj-y			+= $(ARCH_DRIVERS)
111:obj-$(CONFIG_DRM_HEADER_TEST)	+= include/
drivers/misc/e2/e2_main.o
drivers/misc/e2/e2_stats.o
init/main.o
init/version.o
init/do_mounts.o
init/do_mounts_initrd.o
init/initramfs.o
init/calibrate.o
init/init_task.o
usr/initramfs_data.o
arch/arm64/kernel/debug-monitors.o
arch/arm64/kernel/entry.o
arch/arm64/kernel/irq.o
arch/arm64/kernel/fpsimd.o
613
465:drivers/tty/serial/amba-pl011.o
496:drivers/misc/e2/e2_main.o
497:drivers/misc/e2/e2_stats.o
150:ffff800080017224 T e2_stats_report
16497:ffff8000802391dc r __ksymtab_e2_stats_count
18375:ffff80008027aec8 t e2_hello_init
18489:ffff80008028177c t e2_hello_exit
19044:ffff8000802a5bd0 d __initcall__kmod_e2_hello__412_36_e2_hello_init6
21033:ffff80008039ff90 B e2_stats_count
```

출력에서 볼 것:

- 내장(y)일 때 composite `e2_hello.o`는 **만들어지지 않는다**. 구성 파일 `e2_main.o`, `e2_stats.o`가 그대로 `built-in.a`에 들어간다(모듈일 때는 §9에서 `LD [M] e2_hello.o`가 생긴다).
- `built-in.a`와 `vmlinux.a`는 **thin archive**다 — `.o` 내용을 복사하지 않고 경로만 기록한다(`ar` 옵션 `T`). 수천 개 오브젝트를 매번 복사하지 않으려는 최적화다.
- `vmlinux.a` 안에서 `amba-pl011.o`(465번째)가 `e2_main.o`(496번째)보다 앞이다. `drivers/Makefile`에서 `tty/`가 `misc/`보다 먼저 나오기 때문이다. 둘 다 `module_init`(= `device_initcall`, 레벨 6)이므로 **링크 순서대로** 실행된다 — UART 드라이버가 먼저 probe되고 E2가 나중이다. 부팅 로그에서도 PL011 메시지가 e2_hello보다 먼저 찍힌다(§6.4).
- `__initcall__kmod_e2_hello__412_36_e2_hello_init6` — `module_init()`이 내장 빌드에서 만든 initcall 테이블 항목이다. 끝의 `6`이 레벨이고, `412_36`은 매크로가 붙인 고유 번호와 줄 번호(36번째 줄 `module_init`)다.
- `e2_hello_init`은 소문자 `t`(로컬 텍스트), `.init.text` 섹션에 있어서 부팅 후 `Freeing unused kernel memory`로 해제된다.

`lib/`는 조금 특별하다. 최상위 Makefile이 `libs-y := lib/`를 `lib/lib.a`로 바꿔 `KBUILD_VMLINUX_OBJS`(= `built-in.a lib/lib.a` + `arch/arm64/lib/lib.a`)에 넣고, `scripts/Makefile.vmlinux_o`가 `--whole-archive vmlinux.a`로 링크한다. 옛날 커널에서 `lib-y`는 "필요한 오브젝트만 뽑아 쓰는 진짜 라이브러리"였지만, 6.18에서는 `vmlinux.a`에 합쳐져 결국 전부 링크된다(`ar t vmlinux.a`에 `lib/ctype.o`가 566번째로 들어 있는 것을 확인했다).

### 6.4 실습 2 — 트리 안에 추가한 드라이버를 내장으로 빌드하고 부팅 로그에서 확인

무엇을 확인하나: 예제 7에서 `E2_HELLO=y`로 바꾼 설정을 **증분 빌드**하면 무엇이 다시 만들어지는지, 그리고 부팅 로그에 드라이버 메시지가 나오는지 본다.

```sh
cd /var/tmp/E2/src && /usr/bin/time -f "%e s elapsed" make O=/var/tmp/E2/tuned -j4 Image modules 2>&1 | grep -v "^make\[" | tail -25
```

```text
  SYNC    include/config/auto.conf
  GEN     Makefile
  GEN     Makefile
  CALL    /var/tmp/E2/src/scripts/checksyscalls.sh
  CC      drivers/misc/e2/e2_main.o
  CC      drivers/misc/e2/e2_stats.o
  AR      drivers/misc/e2/built-in.a
  AR      drivers/misc/built-in.a
  AR      drivers/built-in.a
  AR      built-in.a
  AR      vmlinux.a
  LD      vmlinux.o
  MODPOST Module.symvers
  CC      .vmlinux.export.o
  UPD     include/generated/utsversion.h
  CC      init/version-timestamp.o
  LD      vmlinux.unstripped
  NM      System.map
  SORTTAB vmlinux.unstripped
  OBJCOPY vmlinux
  GEN     modules.builtin.modinfo
  GEN     modules.builtin
  OBJCOPY arch/arm64/boot/Image
13.66 s elapsed
```

출력에서 볼 것: `SYNC include/config/auto.conf` — `.config`가 바뀌어서 syncconfig가 먼저 돌았다. 컴파일은 새 파일 2개뿐이고, 그 다음은 **아카이브를 위로 다시 묶고**(`AR` 4단) `vmlinux`를 다시 링크하는 고정 비용이다. `UPD utsversion.h` + `CC init/version-timestamp.o`는 `uname -v`의 빌드 시각을 갱신하는 단계로, 무엇을 바꾸든 매번 돈다.

```sh
cd /var/tmp/E2/tuned && timeout 90 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -kernel arch/arm64/boot/Image -initrd /var/lab/out/initramfs.cpio.gz \
  -append "console=ttyAMA0 LAB_CMD=dmesg~|~grep~e2_;~grep~-c~.~/proc/kallsyms" 2>&1 | tr -d "\r" \
  | grep -E "e2_|Run /init|lab initramfs|^[0-9]"
```

```text
Kernel command line: console=ttyAMA0 LAB_CMD=dmesg~|~grep~e2_;~grep~-c~.~/proc/kallsyms
Unknown kernel command line parameters "LAB_CMD=dmesg~|~grep~e2_;~grep~-c~.~/proc/kallsyms", will be passed to user space.
31824 pages in range for non-PLT usage
523344 pages in range for PLT usage
9000000.pl011: ttyAMA0 at MMIO 0x9000000 (irq = 13, base_baud = 0) is a PL011 rev1
e2_hello: "hi Don" period=250ms style=plain tag=ccflags-y
e2_hello: IS_BUILTIN=1 IS_MODULE=0 IS_ENABLED=1 #ifdef=1
e2_stats: report #1, HZ=250
Run /init as init process
lab initramfs: 6.18.55 up 1.12s
Kernel command line: console=ttyAMA0 LAB_CMD=dmesg~|~grep~e2_;~grep~-c~.~/proc/kallsyms
Unknown kernel command line parameters "LAB_CMD=dmesg~|~grep~e2_;~grep~-c~.~/proc/kallsyms", will be passed to user space.
e2_hello: "hi Don" period=250ms style=plain tag=ccflags-y
e2_hello: IS_BUILTIN=1 IS_MODULE=0 IS_ENABLED=1 #ifdef=1
e2_stats: report #1, HZ=250
e2_stats: pr_debug is live because CFLAGS_e2_stats.o has -DDEBUG
    LAB_CMD=dmesg~|~grep~e2_;~grep~-c~.~/proc/kallsyms
```

(`/proc/kallsyms` 검사는 아무것도 출력하지 않았다 — tuned 커널은 `KALLSYMS`가 꺼져 있어서 그 파일이 없다. 마지막 줄은 커널이 `/init`에 넘긴 환경 변수를 다시 보여 주는 dmesg 줄이 grep에 걸린 것이다.)

출력에서 볼 것:

- `Run /init` 앞의 e2 세 줄은 **부팅 중** 콘솔에 찍힌 것(PL011 다음 — 링크 순서 그대로), 뒤의 줄들은 `/init`이 실행한 `dmesg`가 링버퍼에서 다시 읽은 것이다.
- Kconfig 값이 전부 코드에 들어갔다: 문자열 `"hi Don"`, 정수 250, choice `plain`, Makefile의 `ccflags-y` 태그.
- 내장이므로 `IS_BUILTIN=1`, `#ifdef=1`.
- `pr_debug` 줄은 **dmesg에만** 있고 콘솔에는 없다. `CFLAGS_e2_stats.o += -DDEBUG` 덕분에 `pr_debug`가 `KERN_DEBUG`(레벨 7) printk로 컴파일됐지만, 콘솔 기본 출력 레벨이 그보다 낮은 메시지만 내보내기 때문이다. `e2_main.c`에는 `-DDEBUG`가 없으므로 같은 매크로를 써도 아무것도 안 나온다 — 파일 하나만 디버그 출력을 켜는 고전적 방법이다(요즘은 `DYNAMIC_DEBUG`로 런타임에 켠다).

---

## 7. 빌드 들여다보기 — `V=1`, `V=2`, 단일 타깃

### 7.1 예제 16 — `make V=1`로 진짜 gcc 명령 보기

무엇을 확인하나: 한 파일을 컴파일할 때 Kbuild가 붙이는 플래그 전체를 본다. B4 §7.1에서 out-of-tree 모듈로 일부를 봤으니, 여기서는 **트리 안**, tinyconfig 기반 설정에서의 전체 명령을 줄 단위로 쪼개 본다(105줄 중 핵심만 남기고 `...`로 줄였다).

```sh
cd /var/tmp/E2/src && touch drivers/misc/e2/e2_main.c
make O=/var/tmp/E2/tuned V=1 drivers/misc/e2/e2_main.o 2>&1 | grep -E "gcc .*e2_main" | tr " " "\n" | grep -v "^$"
```

```text
gcc
-Wp,-MMD,drivers/misc/e2/.e2_main.o.d
-nostdinc
-I/var/tmp/E2/src/arch/arm64/include
-I./arch/arm64/include/generated
-I/var/tmp/E2/src/include
-I./include
...
-include
/var/tmp/E2/src/include/linux/compiler-version.h
-include
/var/tmp/E2/src/include/linux/kconfig.h
-include
/var/tmp/E2/src/include/linux/compiler_types.h
-D__KERNEL__
-mlittle-endian
...
-std=gnu11
-fshort-wchar
-funsigned-char
-fno-common
-fno-PIE
-fno-strict-aliasing
-mgeneral-regs-only
...
-mabi=lp64
...
-Os
...
-fno-stack-protector
...
-Wall
-Wextra
-Wundef
-Werror=implicit-function-declaration
...
-Wframe-larger-than=2048
...
-DE2_BUILD_TAG='"ccflags-y"'
-I/var/tmp/E2/src/drivers/misc/e2
-Idrivers/misc/e2
-DKBUILD_MODFILE='"drivers/misc/e2/e2_hello"'
-DKBUILD_BASENAME='"e2_main"'
-DKBUILD_MODNAME='"e2_hello"'
-D__KBUILD_MODNAME=kmod_e2_hello
-c
-o
drivers/misc/e2/e2_main.o
/var/tmp/E2/src/drivers/misc/e2/e2_main.c
```

출력에서 볼 것:

- `-Wp,-MMD,…/.e2_main.o.d` — 헤더 의존성 파일(B4 §3). Kbuild는 이걸 그대로 쓰지 않고 `fixdep`으로 가공한다(§8).
- `-include …/kconfig.h` — 모든 파일에 `autoconf.h`가 강제로 들어가는 지점. 그래서 드라이버는 `CONFIG_*`를 include 없이 쓴다.
- `-I./include`, `-I./arch/arm64/include/generated` — `O=` 빌드라서 생성 헤더는 출력 디렉터리(현재 디렉터리 `.`)에서, 소스 헤더는 `/var/tmp/E2/src`에서 찾는다. 소스 트리를 깨끗하게 유지하는 원리다. 반대로 소스 트리에 `.config`나 생성 파일이 남아 있으면 `O=` 빌드가 "source tree is not clean, please run 'make mrproper'"로 거부한다.
- `-Os` — tinyconfig가 `CC_OPTIMIZE_FOR_SIZE=y`로 바꾼 결과. defconfig는 `-O2`다. Kconfig 값이 **컴파일 플래그까지** 바꾼다.
- `-mgeneral-regs-only`, `-fno-common`, `-Wframe-larger-than=2048` — FP/SIMD 금지, 커널 스택 프레임 2 KB 경고(E4).
- 마지막 부분이 Makefile에서 온 것: `ccflags-y`의 `-DE2_BUILD_TAG`, 그리고 `KBUILD_MODNAME='"e2_hello"'` — composite이라 파일 이름(`e2_main`)이 아니라 **모듈 이름**이 들어간다. `pr_fmt`, `/sys/module/<이름>` 등이 이 값을 쓴다.

`CFLAGS_e2_stats.o`가 정말 그 파일에만 붙는지는 다른 파일을 컴파일시켜 확인했다.

```sh
touch drivers/misc/e2/e2_stats.c
make O=/var/tmp/E2/tuned V=1 drivers/misc/e2/e2_stats.o 2>&1 | grep -o -- " -DDEBUG\| -DE2_BUILD_TAG=[^ ]*"
```

```text
 -DE2_BUILD_TAG='"ccflags-y"'
 -DDEBUG
```

`V=2`는 명령 대신 **왜 다시 빌드하는지**를 알려 준다. "왜 이게 또 컴파일되지?"를 추적할 때 쓴다(이때는 `E2_HELLO=m` 상태).

```sh
touch drivers/misc/e2/e2_main.c; make O=/var/tmp/E2/tuned V=2 modules 2>&1 | grep -E "^  [A-Z]" | head -8
```

```text
  GEN     Makefile - due to target is PHONY
  CALL    /var/tmp/E2/src/scripts/checksyscalls.sh - due to target is PHONY
  CC [M]  drivers/misc/e2/e2_main.o - due to: /var/tmp/E2/src/drivers/misc/e2/e2_main.c
  LD [M]  drivers/misc/e2/e2_hello.o - due to: drivers/misc/e2/e2_main.o
  MODPOST Module.symvers - due to: modules.order
  LD [M]  drivers/misc/e2/e2_hello.ko - due to: drivers/misc/e2/e2_main.o
```

### 7.2 예제 17 — 단일 타깃: `.o`, `.i`, `.s`, `.lst`, 디렉터리, `.ko`

무엇을 확인하나: 커널 전체를 빌드하지 않고 파일 하나의 전처리 결과·어셈블리·혼합 리스트를 만든다. "이 매크로가 실제로 뭘로 펼쳐졌지?", "이 코드가 어떤 명령어가 됐지?"를 볼 때 쓴다. Don이 펌웨어에서 `.lst`/`.map`을 보던 것과 같은 용도다.

```sh
cd /var/tmp/E2/src
make O=/var/tmp/E2/tuned drivers/misc/e2/e2_main.i drivers/misc/e2/e2_main.s drivers/misc/e2/e2_main.lst 2>&1 | grep -E "^  [A-Z]"
cd /var/tmp/E2/tuned && ls -l drivers/misc/e2/ && wc -l drivers/misc/e2/e2_main.i
sed -n 40870,40880p drivers/misc/e2/e2_main.i
sed -n 1,40p drivers/misc/e2/e2_main.lst   # 출력은 .exit.text 부분을 ...로 생략
```

```text
  GEN     Makefile
  CALL    /var/tmp/E2/src/scripts/checksyscalls.sh
  CPP     drivers/misc/e2/e2_main.i
  CC      drivers/misc/e2/e2_main.s
  MKLST   drivers/misc/e2/e2_main.lst
total 2100
-rw-rw-r-- 1 donh donh     212 Oct 10 13:50 built-in.a
-rw-rw-r-- 1 donh donh 2123996 Oct 10 13:51 e2_main.i
-rw-rw-r-- 1 donh donh    2432 Oct 10 13:51 e2_main.lst
-rw-rw-r-- 1 donh donh    3416 Oct 10 13:50 e2_main.o
-rw-rw-r-- 1 donh donh    4205 Oct 10 13:51 e2_main.s
-rw-rw-r-- 1 donh donh    2328 Oct 10 13:51 e2_stats.o
-rw-rw-r-- 1 donh donh       0 Oct 10 13:50 modules.order
40890 drivers/misc/e2/e2_main.i
static int __attribute__((__section__(".init.text"))) __attribute__((__cold__)) e2_hello_init(void)
{
 ({ do {} while (0); _printk("\001" "6" "e2_hello: \"%s\" period=%dms style=%s tag=%s\n", "hi Don", 250, "plain", "ccflags-y"); })

                         ;
 ({ do {} while (0); _printk("\001" "6" "e2_hello: IS_BUILTIN=%d IS_MODULE=%d IS_ENABLED=%d #ifdef=%d\n", 1, 0, 1, 1); })

                                        ;
 e2_stats_report();
 return 0;
}

e2_main.o:     file format elf64-littleaarch64


...

Disassembly of section .init.text:

0000000000000000 <e2_hello_init>:
{
   0:	a9bf7bfd 	stp	x29, x30, [sp, #-16]!
	pr_info("e2_hello: \"%s\" period=%dms style=%s tag=%s\n",
   4:	52801f42 	mov	w2, #0xfa                  	// #250
   8:	90000004 	adrp	x4, 0 <e2_hello_init>
			8: R_AARCH64_ADR_PREL_PG_HI21	.rodata.str1.1+0x11
{
   c:	910003fd 	mov	x29, sp
	pr_info("e2_hello: \"%s\" period=%dms style=%s tag=%s\n",
```

출력에서 볼 것:

- 50줄짜리 `.c`가 전처리 후 **40,890줄, 2.1 MB**가 됐다. 대부분 `module.h`가 끌고 온 헤더다. 커널 빌드가 느린 진짜 이유이고, §8에서 헤더 하나를 건드렸을 때 수백 개 파일이 다시 컴파일되는 이유다.
- `.i`에서 `__init`이 `__section__(".init.text")`로, `pr_info`가 `_printk("\001" "6" …)`(KERN_INFO = SOH + "6")로, `CONFIG_E2_HELLO_GREETING`이 `"hi Don"`으로, `IS_BUILTIN(CONFIG_E2_HELLO)`가 상수 `1`로 펼쳐졌다. 매크로 디버깅은 `.i`를 보는 게 가장 빠르다.
- `.lst`는 소스 줄과 디스어셈블리를 섞은 것이다. `mov w2, #0xfa` = 250 — Kconfig 정수가 즉시값으로 들어갔다. `adrp x4` 밑의 `R_AARCH64_ADR_PREL_PG_HI21 .rodata.str1.1+0x11`은 문자열의 주소가 아직 링크 전이라 재배치 항목으로 남은 것이다(`.o` 단계라 주소가 0).

자주 쓰는 단일 타깃을 `make help`에서 뽑으면 이렇다.

```text
  dir/            - Build all files in dir and below
  dir/file.[ois]  - Build specified target only
  dir/file.lst    - Build specified mixed source/assembly target only
  dir/file.ko     - Build module including final link
```

디렉터리 타깃과 `.ko` 타깃도 실제로 돌려 봤다(`E2_HELLO=m` 상태).

```sh
touch drivers/misc/e2/e2_stats.c && make O=/var/tmp/E2/tuned drivers/misc/e2/ 2>&1 | grep -E "^  [A-Z]"
make O=/var/tmp/E2/tuned drivers/misc/e2/e2_hello.ko 2>&1 | grep -E "^  [A-Z]"
```

```text
  GEN     Makefile
  CALL    /var/tmp/E2/src/scripts/checksyscalls.sh
  CC [M]  drivers/misc/e2/e2_stats.o
  LD [M]  drivers/misc/e2/e2_hello.o
  GEN     Makefile
  CALL    /var/tmp/E2/src/scripts/checksyscalls.sh
  MODPOST Module.symvers
  LD [M]  drivers/misc/e2/e2_hello.ko
```

드라이버 개발 루프에서는 `make drivers/misc/e2/e2_hello.ko` 한 줄이 가장 빠르다. `vmlinux`를 다시 링크하지 않는다.

---

## 8. 실습 3 — 증분 빌드와 fixdep: 무엇을 건드리면 얼마나 다시 빌드되나

### 8.1 직관 — 헤더는 비싸고, Kconfig 값은 싸다

B4 §3에서 `-MMD`로 헤더 의존성을 추적하는 법을 봤다. 커널에는 문제가 하나 더 있다. 모든 파일이 `autoconf.h`(수천 개 `#define`)를 include하므로, 순진하게 하면 **`.config`의 어느 한 줄만 바꿔도 전체가 다시 빌드**된다. Kbuild는 이걸 `fixdep`으로 푼다.

- 컴파일러가 만든 `.e2_main.o.d`(헤더 목록)를 `scripts/basic/fixdep`이 읽는다.
- 소스와 헤더 본문에서 `CONFIG_FOO` 문자열을 찾아, 의존성 목록의 `autoconf.h`를 **`include/config/FOO` 파일들로 바꿔 친다.**
- syncconfig는 값이 **바뀐 심볼의** `include/config/FOO` 파일만 touch한다(`conf_touch_deps()`).
- 결과: `CONFIG_E2_HELLO_PERIOD_MS`를 바꾸면 그 문자열이 들어 있는 파일만 다시 컴파일된다.

### 8.2 예제 18 — `.cmd` 파일 안의 심볼 의존성

무엇을 확인하나: fixdep이 만든 `.e2_main.o.cmd`에 Kconfig 심볼이 의존성으로 들어 있는지 본다.

```sh
cd /var/tmp/E2/tuned
grep -c "" drivers/misc/e2/.e2_main.o.cmd
grep -n "include/config\|wildcard" drivers/misc/e2/.e2_main.o.cmd | head -8
ls include/config | wc -l; ls include/config/E2_HELLO_PERIOD_MS
```

```text
1020
6:    $(wildcard include/config/E2_STYLE_LOUD) \
7:    $(wildcard include/config/E2_HELLO) \
8:    $(wildcard include/config/E2_HELLO_GREETING) \
9:    $(wildcard include/config/E2_HELLO_PERIOD_MS) \
11:    $(wildcard include/config/CC_VERSION_TEXT) \
13:    $(wildcard include/config/CPU_BIG_ENDIAN) \
14:    $(wildcard include/config/BOOGER) \
15:    $(wildcard include/config/FOO) \
570
include/config/E2_HELLO_PERIOD_MS
```

출력에서 볼 것:

- `e2_main.c`가 쓰는 네 심볼(`E2_STYLE_LOUD`, `E2_HELLO`, `GREETING`, `PERIOD_MS`)이 각각 의존성이 됐다. `E2_STYLE_LOUD`는 꺼져 있는데도 들어 있다 — `#ifdef CONFIG_E2_STYLE_LOUD`가 소스에 있으니 그 값이 바뀌면 다시 빌드해야 하기 때문이다. `$(wildcard …)`라서 파일이 없어도(심볼이 n) 에러가 아니다.
- `BOOGER`, `FOO`는 커널 헤더 **주석**에 들어 있는 예시 문자열(`CONFIG_BOOGER`, `CONFIG_FOO`)을 fixdep이 그대로 잡은 것이다. fixdep은 C 파서가 아니라 단순 문자열 스캐너다. 해롭지 않다(그런 심볼은 존재하지 않으니 파일도 안 생긴다).
- `.cmd` 파일에는 이 의존성 외에 **직전 컴파일 명령 전체**도 저장된다. 다음 빌드에서 명령이 한 글자라도 다르면(`ccflags-y` 변경 등) 소스가 그대로여도 다시 컴파일한다(`if_changed` 규칙).

### 8.3 측정 — 일곱 가지 변경의 증분 빌드 비용

무엇을 확인하나: tuned 트리에서 하나씩 바꾸고 `make -j4 Image`의 벽시계 시간과 실제 컴파일 수를 잰다.

```sh
#!/bin/bash
# incr.sh — 증분 빌드 비용 측정 (tuned 트리, -j4, make Image)
S=/var/tmp/E2/src; O=/var/tmp/E2/tuned
cd $S
run() {  # $1 = label
  local t0=$(date +%s.%N)
  make O=$O -j4 Image > /var/tmp/E2/logs/incr_$2.log 2>&1 || { echo FAIL; tail /var/tmp/E2/logs/incr_$2.log; exit 1; }
  local t1=$(date +%s.%N)
  printf "%-44s %7.1f s  CC=%4d\n" "$1" "$(echo "$t1 - $t0" | bc)" "$(grep -c '^  CC ' /var/tmp/E2/logs/incr_$2.log)"
}
make O=$O -j4 Image >/dev/null 2>&1
run "no change (make again)" noop
touch drivers/misc/e2/e2_main.c;                run "touch drivers/misc/e2/e2_main.c" c1
scripts/config --file $O/.config --set-val E2_HELLO_PERIOD_MS 300
run "CONFIG_E2_HELLO_PERIOD_MS 250 -> 300" cfg
touch drivers/tty/serial/amba-pl011.c;          run "touch drivers/tty/serial/amba-pl011.c" c2
touch include/linux/serial_core.h;              run "touch include/linux/serial_core.h" h1
touch include/linux/printk.h;                   run "touch include/linux/printk.h" h2
touch include/linux/kconfig.h;                  run "touch include/linux/kconfig.h" h3
```

```text
no change (make again)                           3.5 s  CC=   0
touch drivers/misc/e2/e2_main.c                  5.3 s  CC=   2
CONFIG_E2_HELLO_PERIOD_MS 250 -> 300            13.7 s  CC=   2
touch drivers/tty/serial/amba-pl011.c            6.8 s  CC=   2
touch include/linux/serial_core.h                9.1 s  CC=   8
touch include/linux/printk.h                   206.1 s  CC= 563
touch include/linux/kconfig.h                  185.0 s  CC= 596
```

설정 변경과 헤더 변경 때 실제로 무엇이 컴파일됐는지 로그에서 뽑았다.

```sh
grep -E "^  (CC|SYNC)" /var/tmp/E2/logs/incr_cfg.log; grep -E "^  CC" /var/tmp/E2/logs/incr_h1.log
```

```text
  SYNC    include/config/auto.conf
  CC      drivers/misc/e2/e2_main.o
  CC      init/version-timestamp.o
  CC      drivers/tty/serial/serial_core.o
  CC      drivers/tty/serial/serial_base_bus.o
  CC      drivers/tty/serial/serial_ctrl.o
  CC      drivers/tty/serial/serial_port.o
  CC      drivers/tty/serial/earlycon.o
  CC      drivers/tty/serial/amba-pl011.o
  CC      drivers/of/fdt.o
  CC      init/version-timestamp.o
```

```svg
<svg viewBox="0 0 680 280" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="20" text-anchor="middle" font-size="13" font-weight="bold">증분 빌드 시간 (tuned 트리, make -j4 Image, 초)</text>
  <text x="182" y="49" text-anchor="end" font-size="12">make 다시 (변경 없음)</text>
  <rect x="190" y="36" width="5.7" height="18" fill="#3f9a6b" fill-opacity="0.75"/>
  <text x="201.7" y="49" font-size="12">3.5 s · CC 0</text>
  <text x="182" y="79" text-anchor="end" font-size="12">touch e2_main.c</text>
  <rect x="190" y="66" width="8.6" height="18" fill="#3f9a6b" fill-opacity="0.75"/>
  <text x="204.6" y="79" font-size="12">5.3 s · CC 1</text>
  <text x="182" y="109" text-anchor="end" font-size="12">CONFIG 값 1개 변경</text>
  <rect x="190" y="96" width="22.2" height="18" fill="#3f9a6b" fill-opacity="0.75"/>
  <text x="218.2" y="109" font-size="12">13.7 s · CC 1</text>
  <text x="182" y="139" text-anchor="end" font-size="12">touch amba-pl011.c</text>
  <rect x="190" y="126" width="11.0" height="18" fill="#3f9a6b" fill-opacity="0.75"/>
  <text x="207.0" y="139" font-size="12">6.8 s · CC 1</text>
  <text x="182" y="169" text-anchor="end" font-size="12">touch serial_core.h</text>
  <rect x="190" y="156" width="14.7" height="18" fill="#e08a3c" fill-opacity="0.75"/>
  <text x="210.7" y="169" font-size="12">9.1 s · CC 7</text>
  <text x="182" y="199" text-anchor="end" font-size="12">touch printk.h</text>
  <rect x="190" y="186" width="333.7" height="18" fill="#d0564a" fill-opacity="0.75"/>
  <text x="529.7" y="199" font-size="12">206.1 s · CC 562</text>
  <text x="182" y="229" text-anchor="end" font-size="12">touch kconfig.h</text>
  <rect x="190" y="216" width="299.5" height="18" fill="#d0564a" fill-opacity="0.75"/>
  <text x="495.5" y="229" font-size="12">185.0 s · CC 595</text>
  <line x1="190" y1="32" x2="190" y2="246" stroke="currentColor"/>
  <line x1="190.0" y1="246" x2="190.0" y2="251" stroke="currentColor"/>
  <text x="190.0" y="264" text-anchor="middle" font-size="12">0</text>
  <line x1="271.0" y1="246" x2="271.0" y2="251" stroke="currentColor"/>
  <text x="271.0" y="264" text-anchor="middle" font-size="12">50</text>
  <line x1="351.9" y1="246" x2="351.9" y2="251" stroke="currentColor"/>
  <text x="351.9" y="264" text-anchor="middle" font-size="12">100</text>
  <line x1="432.9" y1="246" x2="432.9" y2="251" stroke="currentColor"/>
  <text x="432.9" y="264" text-anchor="middle" font-size="12">150</text>
  <line x1="513.8" y1="246" x2="513.8" y2="251" stroke="currentColor"/>
  <text x="513.8" y="264" text-anchor="middle" font-size="12">200</text>
</svg>
```
그림 6 — 증분 빌드 비용(실측). 막대 옆 CC는 실제로 다시 컴파일된 소스 수다(매번 도는 `init/version-timestamp.o` 하나는 뺐다). 초록 = 파일 1개, 주황 = 서브시스템 헤더, 빨강 = 전역 헤더.

출력에서 볼 것:

- **아무것도 안 바꿔도 3.5초**가 든다. 600개 디렉터리를 재귀로 돌며 `.cmd`를 읽고 타임스탬프를 비교하는 비용이다. defconfig 트리(9,231 오브젝트)에서는 이 "빈 make"만 수십 초가 걸린다 — 드라이버 개발 때 `make drivers/…/foo.ko`처럼 단일 타깃을 쓰는 이유다.
- **Kconfig 정수 하나를 바꾸면 `e2_main.o` 하나만** 다시 컴파일된다. 같은 디렉터리의 `e2_stats.o`는 `PERIOD_MS`를 안 쓰므로 그대로다. fixdep이 없었다면 `autoconf.h`가 바뀐 것이므로 600개 전부 다시 빌드됐을 것이다. 13.7초 중 대부분은 syncconfig + vmlinux 재링크다.
- `serial_core.h`처럼 **서브시스템 헤더**는 그 서브시스템(시리얼 드라이버 6개 + `drivers/of/fdt.o`)만 다시 빌드한다.
- `printk.h`, `kconfig.h` 같은 **전역 헤더**를 건드리면 사실상 전체 재빌드(563개, 596개)다. tinyconfig라서 3분이지, defconfig 트리라면 이 VM `-j4`로 수십 분이다. `printk.h`가 `kconfig.h`보다 오래 걸린 것은 컴파일 수 차이가 아니라 측정 중 다른 노트의 빌드와 CPU를 나눠 쓴 잡음이다.

Don의 펌웨어 경험과 연결하면: 공통 헤더(`types.h`, `config.h`)에 디버그 매크로 하나 추가하고 "왜 빌드가 이렇게 오래 걸려?" 하던 그 상황이다. 커널에서는 규모가 100배라서, **드라이버 개발 중에는 공용 헤더를 건드리지 않는다**는 것이 실무 습관이 된다. 헤더를 바꿔야 하면 드라이버 전용 헤더(`drivers/…/foo.h`)를 쓴다.

---

## 9. 모듈 빌드 — modpost, `Module.symvers`, `modules_install`

### 9.1 예제 19 — 같은 드라이버를 `=m`으로 바꿔 빌드

무엇을 확인하나: `E2_HELLO`를 m으로 바꾸면 빌드 단계가 어떻게 달라지는지, modpost가 무엇을 만드는지 본다.

```sh
cd /var/tmp/E2/src && scripts/config --file /var/tmp/E2/tuned/.config -m E2_HELLO && make -s O=/var/tmp/E2/tuned olddefconfig
grep "^CONFIG_E2_HELLO=" /var/tmp/E2/tuned/.config
make O=/var/tmp/E2/tuned -j4 Image modules 2>&1 | grep -E "^  [A-Z]"
cd /var/tmp/E2/tuned && cat modules.order; cat drivers/misc/e2/e2_hello.mod
wc -l Module.symvers; grep -P "\te2_" Module.symvers; grep -P "\t_printk\t" Module.symvers
```

```text
CONFIG_E2_HELLO=m
  SYNC    include/config/auto.conf
  GEN     Makefile
  GEN     Makefile
  CALL    /var/tmp/E2/src/scripts/checksyscalls.sh
  LDS     scripts/module.lds
  AR      drivers/misc/e2/built-in.a
  CC [M]  drivers/misc/e2/e2_main.o
  CC [M]  drivers/misc/e2/e2_stats.o
  LD [M]  drivers/misc/e2/e2_hello.o
  AR      drivers/misc/built-in.a
  AR      drivers/built-in.a
  AR      built-in.a
  AR      vmlinux.a
  LD      vmlinux.o
  MODPOST Module.symvers
  CC [M]  drivers/misc/e2/e2_hello.mod.o
  CC [M]  .module-common.o
  CC      .vmlinux.export.o
  LD [M]  drivers/misc/e2/e2_hello.ko
  UPD     include/generated/utsversion.h
  CC      init/version-timestamp.o
  LD      vmlinux.unstripped
  NM      System.map
  SORTTAB vmlinux.unstripped
  OBJCOPY vmlinux
  GEN     modules.builtin.modinfo
  GEN     modules.builtin
  OBJCOPY arch/arm64/boot/Image
drivers/misc/e2/e2_hello.o
drivers/misc/e2/e2_main.o
drivers/misc/e2/e2_stats.o
3541 Module.symvers
0x00000000	e2_stats_count	drivers/misc/e2/e2_hello	EXPORT_SYMBOL_GPL	
0x00000000	_printk	vmlinux	EXPORT_SYMBOL	
```

출력에서 볼 것:

- `CC [M]` — 같은 파일인데 이번에는 `-DMODULE`이 붙어 컴파일된다(`[M]` 표시). 그래서 `.o`를 내장/모듈 사이에서 재사용할 수 없고 다시 컴파일한다.
- `LD [M] e2_hello.o` — composite가 이번에는 **부분 링크**(`ld -r`)로 하나의 `.o`가 된다. 그다음 modpost가 `e2_hello.mod.c`를 생성하고, 그걸 컴파일해 합친 것이 `e2_hello.ko`다.
- `modules.order` — 최상위에 모든 모듈의 목록이 **Makefile 순서대로** 쌓인다. `modules_install`과 depmod가 이 순서를 쓴다. `e2_hello.mod` — composite 구성 파일 목록.
- `Module.symvers` — `vmlinux`와 모든 모듈이 export한 심볼 3,541개의 표다. 열은 CRC, 심볼, 출처, export 종류. CRC가 전부 `0x00000000`인 이유는 tuned와 랩 둘 다 `CONFIG_MODVERSIONS`가 꺼져 있기 때문이다(켜면 심볼마다 함수 원형의 해시가 들어가서, 타입이 바뀐 커널에 옛 모듈을 넣으면 거부된다). 우리가 `EXPORT_SYMBOL_GPL`한 `e2_stats_count`도 `drivers/misc/e2/e2_hello` 출처로 들어갔다.

modpost가 생성한 `.mod.c`의 핵심 부분이다.

```sh
sed -n 1,40p drivers/misc/e2/e2_hello.mod.c; modinfo drivers/misc/e2/e2_hello.ko
```

```text
#include <linux/module.h>
#include <linux/export-internal.h>
#include <linux/compiler.h>

MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

MODULE_INFO(intree, "Y");

KSYMTAB_DATA(e2_stats_count, "_gpl", "");

MODULE_INFO(depends, "");

filename:       /var/tmp/E2/tuned/drivers/misc/e2/e2_hello.ko
description:    E2 Kconfig/Kbuild demo
license:        GPL
depends:        
intree:         Y
name:           e2_hello
vermagic:       6.18.55 SMP mod_unload aarch64
```

출력에서 볼 것: `struct module __this_module`이 모듈의 "주민등록증"이다. `insmod`할 때 커널이 이 구조체에서 이름과 `init`/`exit` 함수 포인터를 읽는다(E3). `intree: Y`는 커널 트리 안에서 빌드됐다는 표시로, out-of-tree 모듈을 넣으면 커널이 `O`(out-of-tree) taint를 거는 근거다. `vermagic`의 `SMP mod_unload aarch64`는 tuned 설정에서 나온 값 — 랩 커널의 `.ko`와 섞어 쓸 수 없다.

### 9.2 예제 20 — modpost가 잡는 두 가지 에러

무엇을 확인하나: out-of-tree 모듈을 tuned 트리에 대고 빌드하면서, modpost가 (1) 아무도 export하지 않은 심볼, (2) GPL 전용 심볼을 비GPL 모듈이 쓰는 경우를 **빌드 시간에** 잡는 것을 본다. 커널 트리 쪽 `Module.symvers`에 `e2_stats_count`가 이미 있으므로 `KBUILD_EXTRA_SYMBOLS` 없이 해석된다.

```c
// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/printk.h>

extern int e2_stats_count;	/* exported by e2_hello.ko (EXPORT_SYMBOL_GPL) */
void e2_not_exported(void);	/* exists nowhere */

static int __init e2_user_init(void)
{
	pr_info("e2_user: e2_stats_count=%d\n", e2_stats_count);
#ifdef E2_BREAK
	e2_not_exported();
#endif
	return 0;
}
module_init(e2_user_init);
MODULE_LICENSE(E2_LICENSE);
```

```sh
cd /var/tmp/E2/oot   # Kbuild: obj-m := e2_user.o
make -C /var/tmp/E2/tuned M=$PWD ccflags-y='-DE2_LICENSE=\"GPL\"' modules 2>&1 | grep -E "^  [A-Z]|ERROR|WARN"
modinfo -F depends e2_user.ko
make -C /var/tmp/E2/tuned M=$PWD ccflags-y='-DE2_LICENSE=\"GPL\" -DE2_BREAK' modules 2>&1 | grep -E "ERROR|WARN"
make -C /var/tmp/E2/tuned M=$PWD ccflags-y='-DE2_LICENSE=\"Proprietary\"' modules 2>&1 | grep -E "ERROR|WARN"
```

```text
### 1) GPL, all symbols resolvable
  CC [M]  e2_user.o
  MODPOST Module.symvers
WARNING: modpost: missing MODULE_DESCRIPTION() in e2_user.o
  CC [M]  e2_user.mod.o
  CC [M]  .module-common.o
  LD [M]  e2_user.ko
e2_hello
### 2) undefined symbol
WARNING: modpost: missing MODULE_DESCRIPTION() in e2_user.o
ERROR: modpost: "e2_not_exported" [e2_user.ko] undefined!
### 3) proprietary license uses GPL-only symbol
WARNING: modpost: missing MODULE_DESCRIPTION() in e2_user.o
ERROR: modpost: GPL-incompatible module e2_user.ko uses GPL-only symbol 'e2_stats_count'
```

(`###` 줄은 구분용으로 넣은 `echo` 출력이다.)

출력에서 볼 것:

- 1)에서 `depends: e2_hello` — modpost가 `Module.symvers`를 보고 "이 모듈은 `e2_hello`의 심볼을 쓴다"를 `.ko`에 기록했다. 이 값으로 `depmod`가 `modules.dep`을 만들고 `modprobe`가 의존 모듈을 먼저 올린다.
- 2) `undefined!` — 일반 C 프로그램이면 링크 에러가 날 자리인데, `.ko`는 재배치 가능한 오브젝트라 링커는 통과시킨다. 그래서 modpost가 `Module.symvers`와 대조해서 **빌드 시간에** 막는다. 이 검사를 놓치면(예: 다른 커널의 `Module.symvers`로 빌드) `insmod` 때 `Unknown symbol`로 실패한다.
- 3) GPL 전용 export(`EXPORT_SYMBOL_GPL`)를 `MODULE_LICENSE("Proprietary")` 모듈이 쓰면 빌드가 막힌다. 라이선스 문제는 E7에서 다룬다.

### 9.3 `modules_install`과 게스트에서 `modprobe`

무엇을 확인하나: `INSTALL_MOD_PATH`로 루트 파일시스템 디렉터리에 모듈을 설치하면 어떤 구조가 생기는지, 그 디렉터리를 initramfs에 넣어 게스트에서 `modprobe`가 되는지 본다.

```sh
cd /var/tmp/E2/src && rm -rf /var/tmp/E2/modroot
make O=/var/tmp/E2/tuned INSTALL_MOD_PATH=/var/tmp/E2/modroot modules_install 2>&1 | grep -E "^  [A-Z]"
cd /var/tmp/E2/modroot && find . | sort; cat lib/modules/6.18.55/modules.dep; ls -l lib/modules/6.18.55/build
```

```text
  SYMLINK /var/tmp/E2/modroot/lib/modules/6.18.55/build
  INSTALL /var/tmp/E2/modroot/lib/modules/6.18.55/modules.order
  INSTALL /var/tmp/E2/modroot/lib/modules/6.18.55/modules.builtin
  INSTALL /var/tmp/E2/modroot/lib/modules/6.18.55/modules.builtin.modinfo
  INSTALL /var/tmp/E2/modroot/lib/modules/6.18.55/kernel/drivers/misc/e2/e2_hello.ko
  DEPMOD  /var/tmp/E2/modroot/lib/modules/6.18.55
.
./lib
./lib/modules
./lib/modules/6.18.55
./lib/modules/6.18.55/build
./lib/modules/6.18.55/kernel
./lib/modules/6.18.55/kernel/drivers
./lib/modules/6.18.55/kernel/drivers/misc
./lib/modules/6.18.55/kernel/drivers/misc/e2
./lib/modules/6.18.55/kernel/drivers/misc/e2/e2_hello.ko
./lib/modules/6.18.55/modules.alias
./lib/modules/6.18.55/modules.alias.bin
./lib/modules/6.18.55/modules.builtin
./lib/modules/6.18.55/modules.builtin.alias.bin
./lib/modules/6.18.55/modules.builtin.bin
./lib/modules/6.18.55/modules.builtin.modinfo
./lib/modules/6.18.55/modules.dep
./lib/modules/6.18.55/modules.devname
./lib/modules/6.18.55/modules.order
./lib/modules/6.18.55/modules.softdep
./lib/modules/6.18.55/modules.symbols
./lib/modules/6.18.55/modules.symbols.bin
./lib/modules/6.18.55/modules.weakdep
kernel/drivers/misc/e2/e2_hello.ko:
lrwxrwxrwx 1 donh donh 17 Oct 10 13:59 lib/modules/6.18.55/build -> /var/tmp/E2/tuned
```

출력에서 볼 것:

- 경로 규칙은 `$INSTALL_MOD_PATH/lib/modules/<kernelrelease>/kernel/<소스 경로>.ko`다. `<kernelrelease>`(`6.18.55`)는 `uname -r`과 같아야 `modprobe`가 찾는다. `CONFIG_LOCALVERSION`을 바꾸면 이 디렉터리 이름이 바뀐다.
- `modules.builtin` — **내장된** 드라이버 목록. `modprobe foo`를 했는데 foo가 내장이면 에러 없이 성공으로 처리하는 근거다.
- `DEPMOD` — 호스트의 `depmod`가 `-b $INSTALL_MOD_PATH`로 돌아 `modules.dep`, `modules.alias` 등을 만든다. 크로스 빌드에서도 호스트 depmod로 타깃용 색인을 만든다.
- `build` 심볼릭 링크는 out-of-tree 모듈 빌드용(`make -C /lib/modules/$(uname -r)/build M=…`, B4 §7.1)인데, 빌드 머신의 경로를 가리키므로 타깃 rootfs에 넣을 때는 지운다(Yocto/Buildroot도 지운다).

```sh
cd /var/tmp/E2 && rm -rf rootfs && mkdir rootfs && cp -a /var/lab/build/rootfs/. rootfs/
cp -a modroot/lib/modules rootfs/lib/ && rm rootfs/lib/modules/6.18.55/build
(cd rootfs && find . | cpio -o -H newc --quiet | gzip) > e2_initramfs.cpio.gz
timeout 90 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -kernel tuned/arch/arm64/boot/Image -initrd e2_initramfs.cpio.gz \
  -append "console=ttyAMA0 quiet LAB_CMD=modprobe~e2_hello;~lsmod;~cat~/sys/module/e2_hello/initstate;~modprobe~-r~e2_hello;~dmesg~|~grep~e2_" 2>&1 | tr -d "\r"
```

```text
lab initramfs: 6.18.55 up 0.89s
e2_hello 12288 0 - Live 0xffff8000783b0000
live
Kernel command line: console=ttyAMA0 quiet LAB_CMD=modprobe~e2_hello;~lsmod;~cat~/sys/module/e2_hello/initstate;~modprobe~-r~e2_hello;~dmesg~|~grep~e2_
Unknown kernel command line parameters "LAB_CMD=modprobe~e2_hello;~lsmod;~cat~/sys/module/e2_hello/initstate;~modprobe~-r~e2_hello;~dmesg~|~grep~e2_", will be passed to user space.
    LAB_CMD=modprobe~e2_hello;~lsmod;~cat~/sys/module/e2_hello/initstate;~modprobe~-r~e2_hello;~dmesg~|~grep~e2_
e2_hello: "hi Don" period=300ms style=plain tag=ccflags-y
e2_hello: IS_BUILTIN=0 IS_MODULE=1 IS_ENABLED=1 #ifdef=0
e2_stats: report #1, HZ=250
e2_stats: pr_debug is live because CFLAGS_e2_stats.o has -DDEBUG
e2_hello: bye
reboot: Power down
```

출력에서 볼 것:

- `modprobe e2_hello`가 경로를 몰라도 `modules.dep`을 보고 `.ko`를 찾았다. 모듈 주소(`0xffff8000783b0000`)는 실행마다 다르다.
- **같은 소스, 다른 결과**: 내장 때는 `IS_BUILTIN=1 #ifdef=1`, 모듈일 때는 `IS_MODULE=1 #ifdef=0`. `IS_ENABLED`만 양쪽에서 1이다. §3.2의 "모듈 함정"을 실제로 본 것이다.
- `period=300ms` — §8에서 바꾼 Kconfig 값이 모듈에 들어갔다.

---

## 10. 빌드 산출물 지도 — `vmlinux` · `Image` · `Image.gz` · `System.map` · dtbs

### 10.1 예제 21 — 산출물의 정체와 크기

무엇을 확인하나: 각 산출물이 어떤 형식이고 크기가 왜 다른지 `file`, `size`, `xxd`로 본다.

```sh
cd /var/tmp/E2/src && make O=/var/tmp/E2/tuned -j4 Image.gz 2>&1 | grep -E "^  [A-Z]"
for d in /var/tmp/E2/tiny /var/tmp/E2/tuned /var/lab/out/linux-build; do echo "## $d"
  ls -l $d/vmlinux $d/arch/arm64/boot/Image* 2>/dev/null | awk '{print $5, $9}'; size $d/vmlinux | tail -1; done
file /var/lab/out/linux-build/vmlinux /var/tmp/E2/tuned/vmlinux /var/tmp/E2/tuned/arch/arm64/boot/Image /var/tmp/E2/tuned/arch/arm64/boot/Image.gz | sed "s/, BuildID[^,]*//"
xxd -l 64 /var/tmp/E2/tuned/arch/arm64/boot/Image
grep -E "^CONFIG_DEBUG_INFO(_DWARF[A-Z0-9_]*)?=" /var/lab/out/linux-build/.config
```

```text
  GEN     Makefile
  CALL    /var/tmp/E2/src/scripts/checksyscalls.sh
  MODPOST vmlinux.symvers
  GZIP    arch/arm64/boot/Image.gz
## /var/tmp/E2/tiny
1961992 /var/tmp/E2/tiny/arch/arm64/boot/Image
2621032 /var/tmp/E2/tiny/vmlinux
1397116	 351388	 216144	1964648	 1dfa68	/var/tmp/E2/tiny/vmlinux
## /var/tmp/E2/tuned
3434504 /var/tmp/E2/tuned/arch/arm64/boot/Image
1234055 /var/tmp/E2/tuned/arch/arm64/boot/Image.gz
4755976 /var/tmp/E2/tuned/vmlinux
2100431	1029344	 366016	3495791	 35576f	/var/tmp/E2/tuned/vmlinux
## /var/lab/out/linux-build
42740224 /var/lab/out/linux-build/arch/arm64/boot/Image
162255104 /var/lab/out/linux-build/vmlinux
21641971	20961815	 708144	43311930	294e33a	/var/lab/out/linux-build/vmlinux
/var/lab/out/linux-build/vmlinux:           ELF 64-bit LSB pie executable, ARM aarch64, version 1 (SYSV), statically linked, with debug_info, not stripped
/var/tmp/E2/tuned/vmlinux:                  ELF 64-bit LSB executable, ARM aarch64, version 1 (SYSV), statically linked, not stripped
/var/tmp/E2/tuned/arch/arm64/boot/Image:    Linux kernel ARM64 boot executable Image, little-endian, 4K pages
/var/tmp/E2/tuned/arch/arm64/boot/Image.gz: gzip compressed data, max compression, from Unix, original size modulo 2^32 3434504
00000000: 1f20 03d5 2138 0914 0000 0000 0000 0000  . ..!8..........
00000010: 0000 3b00 0000 0000 0a00 0000 0000 0000  ..;.............
00000020: 0000 0000 0000 0000 0000 0000 0000 0000  ................
00000030: 0000 0000 0000 0000 4152 4d64 0000 0000  ........ARMd....
CONFIG_DEBUG_INFO=y
CONFIG_DEBUG_INFO_DWARF_TOOLCHAIN_DEFAULT=y
```

출력에서 볼 것:

- **`vmlinux`** — 링크 결과 ELF. 심볼과 섹션 정보가 있어서 `gdb`, `addr2line`, `objdump`, crash 분석에 쓴다. 부트로더는 이걸 직접 올리지 않는다. 랩 `vmlinux`가 162 MB인 것은 `DEBUG_INFO=y`(DWARF) 때문이고, `size`로 본 실제 메모리 이미지(text+data+bss)는 43 MB다. 랩은 `pie executable`(`RELOCATABLE`, KASLR용)이고 tuned는 tinyconfig가 `RELOCATABLE`을 꺼서 일반 `executable`이다.
- **`Image`** — `objcopy -O binary`로 ELF 헤더와 디버그 정보를 벗긴 **평평한 메모리 이미지**. 펌웨어의 `.bin`과 같다. 파일 크기는 `size`의 text+data에 섹션 정렬 패딩이 더해진 정도이고(tuned: 3.13 MB → 3.43 MB), bss는 0으로 채울 영역이라 파일에 없다. 앞 64바이트가 arm64 부트 헤더다(`Documentation/arch/arm64/booting.rst`): 0x00의 `1f20 03d5`는 `nop`(EFI 스텁이 켜져 있으면 여기가 `MZ`), 0x04는 `b primary_entry` 분기, 0x10의 `0x3b0000`은 bss를 포함한 **메모리상 크기**(3.69 MB, 파일 3.43 MB보다 크다), 0x18의 `0xa`는 플래그(비트0=0 little-endian, 비트1–2=1 4K 페이지, 비트3=1 물리 주소 어디에나 배치 가능 — `file`이 읽은 정보가 이것), 0x38의 `ARMd`(`ARM\x64`)가 매직이다. U-Boot의 `booti`가 이 헤더를 읽는다(C3).
- **`Image.gz`** — gzip 압축본. arm64 커널은 **스스로 압축을 풀지 못한다**(32-bit ARM의 `zImage`와 다르다). 그래서 압축 해제는 부트로더(U-Boot `booti`는 gzip 이미지를 지원)나 FIT 이미지가 맡는다. 플래시 공간과 적재 시간을 줄이려고 쓴다.
- **`System.map`** — `nm vmlinux`의 정렬된 결과. Oops의 주소를 함수 이름으로 바꾸거나(`KALLSYMS`가 꺼진 커널에서 특히), 심볼이 내장인지 확인할 때 쓴다. tuned는 21,327줄, 랩은 224,024줄.

### 10.2 dtbs — 설정이 정하는 또 하나의 산출물

```sh
cd /var/tmp/E2/src && make O=/var/tmp/E2/tuned dtbs 2>&1 | grep -E "^  [A-Z]" | tail -5
find /var/tmp/E2/tuned/arch/arm64/boot/dts -name "*.dtb" 2>/dev/null | wc -l
make O=/var/tmp/E2/tuned arm/juno.dtb 2>&1 | grep -E "^  [A-Z]|rror" | tail -4
ls -l /var/tmp/E2/tuned/arch/arm64/boot/dts/arm/juno.dtb
find /var/lab/out/linux-build/arch/arm64/boot/dts -name "*.dtb" | wc -l; find /var/lab/out/linux-build/arch/arm64/boot/dts -name "*.dtbo" | wc -l
```

```text
0
  DTC     arch/arm64/boot/dts/arm/juno.dtb
-rw-rw-r-- 1 donh donh 27067 Oct 10 14:06 /var/tmp/E2/tuned/arch/arm64/boot/dts/arm/juno.dtb
1577
169
```

출력에서 볼 것: `make dtbs`는 `arch/arm64/boot/dts/<vendor>/Makefile`의 `dtb-$(CONFIG_ARCH_XXX) += board.dtb`를 따른다. tinyconfig에는 SoC 플랫폼(`ARCH_*`)이 하나도 켜져 있지 않아서 **0개**, 랩 defconfig는 1,577개 dtb + 169개 overlay(dtbo)를 만들었다. 특정 보드 하나는 설정과 무관하게 `make <vendor>/<board>.dtb`로 빌드할 수 있다. 보드 DTS를 고치는 루프에서는 이 단일 타깃을 쓴다(D5).

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="160" y="20" text-anchor="middle" font-size="13" font-weight="bold">빌드 머신 (O= 디렉터리)</text>
  <text x="520" y="20" text-anchor="middle" font-size="13" font-weight="bold">타깃 보드</text>
  <rect x="10" y="34" width="300" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="160" y="59" text-anchor="middle" font-size="12">vmlinux — ELF, 디버그 정보 (보관용)</text>
  <rect x="10" y="88" width="300" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="160" y="113" text-anchor="middle" font-size="12">System.map · .config (보관용)</text>
  <rect x="10" y="142" width="300" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="160" y="167" text-anchor="middle" font-size="12">arch/arm64/boot/Image(.gz)</text>
  <rect x="10" y="196" width="300" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="160" y="221" text-anchor="middle" font-size="12">arch/arm64/boot/dts/…/board.dtb</text>
  <rect x="10" y="250" width="300" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="160" y="275" text-anchor="middle" font-size="12">*.ko → modules_install</text>
  <rect x="380" y="142" width="290" height="94" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="525" y="164" text-anchor="middle" font-size="12">/boot 파티션 또는 FIT 이미지</text>
  <text x="525" y="188" text-anchor="middle" font-size="12">Image(.gz) + board.dtb</text>
  <text x="525" y="212" text-anchor="middle" font-size="12">U-Boot booti 가 적재</text>
  <rect x="380" y="250" width="290" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="525" y="275" text-anchor="middle" font-size="12">rootfs /lib/modules/6.18.55/</text>
  <rect x="380" y="34" width="290" height="94" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="525" y="58" text-anchor="middle" font-size="12">보드에는 안 올림</text>
  <text x="525" y="82" text-anchor="middle" font-size="12">Oops 해석 · gdb · addr2line</text>
  <text x="525" y="106" text-anchor="middle" font-size="12">(/proc/config.gz 는 선택)</text>
  <line x1="310" y1="162" x2="378" y2="175" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="310" y1="216" x2="378" y2="203" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="310" y1="270" x2="378" y2="270" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="310" y1="54" x2="378" y2="70" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah7)"/>
  <line x1="310" y1="108" x2="378" y2="92" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah7)"/>
  <text x="340" y="316" text-anchor="middle" font-size="12">INSTALL_MOD_PATH=&lt;rootfs 디렉터리&gt; make modules_install — Buildroot/Yocto가 하는 일</text>
</svg>
```
그림 7 — 산출물이 어디로 가나. 주황(커널+DTB)은 부트 파티션이나 FIT로, 초록(모듈)은 rootfs로 간다. 파랑(`vmlinux`, `System.map`, `.config`)은 보드에 안 올리지만 **반드시 빌드 번호와 함께 보관**한다 — 현장에서 Oops 로그를 받았을 때 그 빌드의 `vmlinux`가 없으면 주소를 해석할 수 없다.

---

## 11. 크로스 컴파일 — `ARCH=`와 `CROSS_COMPILE=`

### 11.1 관례

이 노트의 VM은 arm64라서 arm64 커널은 **네이티브** 빌드였다(`ARCH`, `CROSS_COMPILE` 없음). x86 노트북에서 arm64 보드 커널을 빌드하거나, 같은 arm64 머신에서 32-bit ARM 보드(예: STM32MP1, i.MX6) 커널을 빌드할 때는 두 변수를 준다(B1, B4 §6).

| 변수 | 뜻 | 예 |
|---|---|---|
| `ARCH=` | `arch/<ARCH>/` 디렉터리와 Kconfig 루트 선택 | `arm`, `arm64`, `riscv`, `x86` |
| `CROSS_COMPILE=` | 툴체인 접두어 — `$(CROSS_COMPILE)gcc`, `…ld`, `…objcopy` | `arm-linux-gnueabihf-`, `aarch64-linux-gnu-` |
| `LLVM=1` | gcc 대신 clang/lld (접두어 없이 크로스 가능) | 선택 |

### 11.2 예제 22 — 같은 소스로 32-bit ARM 오브젝트 하나 만들기

무엇을 확인하나: `ARCH=arm multi_v7_defconfig`로 별도 `O=`에 설정하고, PL011 드라이버 오브젝트 하나만 `arm-linux-gnueabihf-`로 빌드한다. 그리고 두 변수 중 하나를 빼먹으면 어떻게 되는지 본다.

```sh
cd /var/tmp/E2/src
make -s ARCH=arm O=/var/tmp/E2/arm32 multi_v7_defconfig
head -4 /var/tmp/E2/arm32/.config | tail -2
grep -E "^CONFIG_(ARM|ARM64|ARCH_MULTI_V7|SERIAL_AMBA_PL011|CC_VERSION_TEXT|ARCH_STM32|ARCH_MXC)=" /var/tmp/E2/arm32/.config
/usr/bin/time -f "%e s" make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- O=/var/tmp/E2/arm32 -j4 drivers/tty/serial/amba-pl011.o > /var/tmp/E2/logs/arm32.log 2>&1
tail -4 /var/tmp/E2/logs/arm32.log
file /var/tmp/E2/arm32/drivers/tty/serial/amba-pl011.o /var/tmp/E2/tuned/drivers/tty/serial/amba-pl011.o
grep CC_VERSION_TEXT /var/tmp/E2/arm32/.config
```

```text
# Linux/arm 6.18.55 Kernel Configuration
#
CONFIG_CC_VERSION_TEXT="gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0"
CONFIG_ARM=y
CONFIG_ARCH_MULTI_V7=y
CONFIG_ARCH_MXC=y
CONFIG_ARCH_STM32=y
CONFIG_SERIAL_AMBA_PL011=y
  CALL    /var/tmp/E2/src/scripts/checksyscalls.sh
  CC      drivers/tty/serial/amba-pl011.o
make[1]: Leaving directory '/var/tmp/E2/arm32'
7.87 s
/var/tmp/E2/arm32/drivers/tty/serial/amba-pl011.o: ELF 32-bit LSB relocatable, ARM, EABI5 version 1 (SYSV), with debug_info, not stripped
/var/tmp/E2/tuned/drivers/tty/serial/amba-pl011.o: ELF 64-bit LSB relocatable, ARM aarch64, version 1 (SYSV), not stripped
CONFIG_CC_VERSION_TEXT="arm-linux-gnueabihf-gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0"
```

출력에서 볼 것:

- 같은 `amba-pl011.c`가 한쪽은 32-bit ARM EABI5, 한쪽은 aarch64 오브젝트가 됐다. 오브젝트 하나지만 Kbuild는 먼저 생성 헤더(`WRAP arch/arm/include/generated/...`)와 호스트 도구를 준비하느라 154줄의 로그를 냈다. 그래도 8초 — 단일 타깃의 힘이다.
- **함정 1**: 설정할 때 `CROSS_COMPILE`을 빼서 `.config`의 `CC_VERSION_TEXT`가 처음엔 호스트 gcc였다. 빌드할 때 크로스 gcc를 주자 Kconfig가 컴파일러가 바뀐 것을 감지해 `SYNC include/config/auto.conf`로 **설정을 다시 계산**했다(마지막 줄). 컴파일러 버전에 따라 켜지는 옵션(`CC_HAS_*`, `RUSTC_*`)이 있으므로, **설정 단계부터 `ARCH`와 `CROSS_COMPILE`을 함께** 주는 것이 정석이다. 셸에 `export ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf-`를 해 두거나 빌드 스크립트에 고정한다.

```sh
mkdir -p /var/tmp/E2/arm32_oops && cp /var/tmp/E2/arm32/.config /var/tmp/E2/arm32_oops/.config
make -s O=/var/tmp/E2/arm32_oops olddefconfig; sed -n 3p /var/tmp/E2/arm32_oops/.config
grep -E "^CONFIG_(ARM|ARM64)=" /var/tmp/E2/arm32_oops/.config; wc -l /var/tmp/E2/arm32/.config /var/tmp/E2/arm32_oops/.config
mkdir -p /var/tmp/E2/arm32_nocc && cp /var/tmp/E2/arm32/.config /var/tmp/E2/arm32_nocc/
make ARCH=arm O=/var/tmp/E2/arm32_nocc -j4 drivers/tty/serial/amba-pl011.o 2>&1 | grep "gcc: error" | head -4
```

```text
# Linux/arm64 6.18.55 Kernel Configuration
CONFIG_ARM64=y
 10868 /var/tmp/E2/arm32/.config
 10370 /var/tmp/E2/arm32_oops/.config
 21238 total
gcc: error: unrecognized argument in option ‘-mtp=cp15’
gcc: error: unrecognized argument in option ‘-mabi=aapcs-linux’
gcc: error: unrecognized command-line option ‘-mfpu=vfp’
gcc: error: unrecognized argument in option ‘-mtp=cp15’
```

출력에서 볼 것:

- **함정 2**: `ARCH=`를 빼고 `olddefconfig`(또는 `menuconfig`)를 한 번 돌리면, 호스트 아키텍처(arm64)의 Kconfig로 32-bit 설정을 **다시 해석해서 덮어쓴다**. 헤더가 `Linux/arm64`로 바뀌고 `CONFIG_ARM`이 사라졌다. 경고도 없다. `ARCH`는 `.config`에 저장되지 않으므로 매 명령에 줘야 한다.
- **함정 3**: `ARCH=arm`인데 `CROSS_COMPILE`을 빼면 호스트(aarch64) gcc가 ARM32 전용 플래그(`-mabi=aapcs-linux`, `-mtp=cp15`, `-mfpu=vfp`)를 이해하지 못해 바로 실패한다. 이건 그래도 즉시 터져서 친절한 편이다.

---

## 12. 커널 안에서는 무슨 일이 일어나나

이 노트의 주제는 대부분 **빌드 시간**에 일어난다. 하지만 그 결과는 실행 시간의 커널 구조로 이어진다. 빌드 도구의 함수와 커널의 함수를 한 장에 놓는다.

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah8" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" font-size="13" font-weight="bold">빌드 시간 (호스트 도구, scripts/)</text>
  <rect x="10" y="32" width="200" height="52" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="110" y="53" text-anchor="middle" font-size="12">parser.y · lexer.l</text>
  <text x="110" y="72" text-anchor="middle" font-size="12">Kconfig → 심볼 트리</text>
  <rect x="240" y="32" width="200" height="52" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="53" text-anchor="middle" font-size="12">sym_calc_value()</text>
  <text x="340" y="72" text-anchor="middle" font-size="12">depends · select · imply</text>
  <rect x="470" y="32" width="200" height="52" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="570" y="53" text-anchor="middle" font-size="12">conf_write_autoconf()</text>
  <text x="570" y="72" text-anchor="middle" font-size="12">conf_touch_deps()</text>
  <line x1="210" y1="58" x2="238" y2="58" stroke="currentColor" marker-end="url(#ah8)"/>
  <line x1="440" y1="58" x2="468" y2="58" stroke="currentColor" marker-end="url(#ah8)"/>
  <rect x="10" y="110" width="200" height="52" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="110" y="131" text-anchor="middle" font-size="12">Makefile.build</text>
  <text x="110" y="150" text-anchor="middle" font-size="12">gcc -MMD → .o.d</text>
  <rect x="240" y="110" width="200" height="52" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="131" text-anchor="middle" font-size="12">fixdep</text>
  <text x="340" y="150" text-anchor="middle" font-size="12">.o.d → .o.cmd</text>
  <rect x="470" y="110" width="200" height="52" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="570" y="131" text-anchor="middle" font-size="12">modpost</text>
  <text x="570" y="150" text-anchor="middle" font-size="12">check_exports() · .mod.c</text>
  <line x1="210" y1="136" x2="238" y2="136" stroke="currentColor" marker-end="url(#ah8)"/>
  <line x1="440" y1="136" x2="468" y2="136" stroke="currentColor" marker-end="url(#ah8)"/>
  <line x1="570" y1="84" x2="340" y2="108" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah8)"/>
  <line x1="10" y1="186" x2="670" y2="186" stroke="currentColor" stroke-dasharray="2 4"/>
  <text x="10" y="210" font-size="13" font-weight="bold">실행 시간 (타깃 커널)</text>
  <rect x="10" y="222" width="300" height="52" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="160" y="243" text-anchor="middle" font-size="12">내장(y): module_init → .initcall6.init</text>
  <text x="160" y="262" text-anchor="middle" font-size="12">링크 순서대로 테이블에 나열</text>
  <rect x="10" y="294" width="300" height="52" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="160" y="315" text-anchor="middle" font-size="12">do_initcalls() → do_initcall_level(6)</text>
  <text x="160" y="334" text-anchor="middle" font-size="12">→ do_one_initcall(e2_hello_init)</text>
  <line x1="160" y1="274" x2="160" y2="292" stroke="currentColor" marker-end="url(#ah8)"/>
  <rect x="370" y="222" width="300" height="52" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="520" y="243" text-anchor="middle" font-size="12">모듈(m): finit_module() → load_module()</text>
  <text x="520" y="262" text-anchor="middle" font-size="12">check_modinfo(): vermagic 비교</text>
  <rect x="370" y="294" width="300" height="52" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="520" y="315" text-anchor="middle" font-size="12">심볼 해석 (커널 export 표)</text>
  <text x="520" y="334" text-anchor="middle" font-size="12">→ do_init_module() → init()</text>
  <line x1="520" y1="274" x2="520" y2="292" stroke="currentColor" marker-end="url(#ah8)"/>
  <line x1="570" y1="162" x2="520" y2="220" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah8)"/>
  <line x1="110" y1="162" x2="160" y2="220" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah8)"/>
</svg>
```
그림 8 — 빌드 도구의 함수(위)와 실행 시간 커널의 함수(아래). 같은 `module_init()` 한 줄이 내장이면 initcall 테이블 항목이 되고, 모듈이면 `struct module`의 `init` 포인터가 된다. modpost가 빌드 시간에 하는 심볼 검사를 `load_module()`이 실행 시간에 한 번 더 한다.

### 12.1 Kconfig 엔진 — `scripts/kconfig/`

- `parser.y`/`lexer.l`이 1,880개 Kconfig 파일을 `source`를 따라가며 읽어 `struct symbol`(심볼)과 `struct menu`(메뉴 트리) 그래프를 만든다. `if` 블록과 `menu`의 의존성은 이때 안의 항목에 AND로 붙는다(예제 4에서 본 `E2_DEMO [=y] && …`).
- `symbol.c`의 `sym_calc_value()`가 심볼 값을 계산한다. 개념적으로 `값 = max(min(사용자값 또는 default, depends 상한), select 하한)`이고, `imply`는 default 쪽에만 더해진다. select 하한이 depends 상한보다 크면 `sym_warn_unmet_dep()`가 예제 3의 "unmet direct dependencies" 경고를 찍는다.
- `confdata.c`의 `conf_write()`가 `.config`를, `conf_write_autoconf()`가 `auto.conf`·`autoconf.h`를 쓰고, `conf_touch_deps()`가 값이 바뀐 심볼의 `include/config/<SYM>` 파일만 갱신한다.
- `conf`(텍스트), `mconf`(menuconfig), `nconf`(nconfig), `qconf`/`gconf`는 같은 엔진 위의 서로 다른 UI다.

### 12.2 Kbuild 엔진 — `scripts/Makefile.*`, `fixdep`, `modpost`

- 최상위 `Makefile`이 `auto.conf`를 include하고, `$(MAKE) -f scripts/Makefile.build obj=<dir>`로 디렉터리마다 내려간다. `Makefile.build`가 그 디렉터리의 `Makefile`/`Kbuild`를 include해서 `obj-y`/`obj-m`/`<name>-y`를 읽고, `built-in.a`, composite `.o`, `modules.order`를 만든다.
- `scripts/basic/fixdep.c` — 주석에도 적혀 있듯 "merely grep" 수준으로 `CONFIG_` 문자열을 찾아 `.cmd` 파일에 `include/config/<SYM>` 의존성을 쓴다(예제 18의 `BOOGER`, `FOO`가 그 증거).
- `scripts/mod/modpost.c` — `read_symbols()`로 각 모듈과 `vmlinux`의 심볼을 읽고, `check_exports()`가 미해결·GPL 위반을 잡고(예제 20), `.mod.c`를 생성하고, `write_dump()`가 `Module.symvers`를 쓴다.

### 12.3 실행 시간 — initcall과 모듈 로더

- 내장일 때 `module_init(fn)`은 `include/linux/module.h`에서 `__initcall(fn)` = `device_initcall(fn)` = `__define_initcall(fn, 6)`이 되어 `.initcall6.init` 섹션에 항목을 하나 넣는다(예제 15의 `__initcall__kmod_e2_hello__…6`). 링커 스크립트가 레벨별로 모은 이 표를 `init/main.c`의 `do_initcalls()` → `do_initcall_level()` → `do_one_initcall()`이 순서대로 부른다. **같은 레벨 안의 순서 = 링크 순서 = Makefile 순서**다. 드라이버 간 probe 순서 문제를 Makefile 순서로 "해결"하는 코드를 가끔 보는데, 올바른 해법은 deferred probe나 device link다(D3 §10, G2).
- 모듈일 때 `insmod`/`modprobe`는 `finit_module()` syscall로 `.ko`를 넘기고, `kernel/module/main.c`의 `load_module()`이 ELF를 재배치하고, `check_modinfo()`가 `vermagic`을 실행 중인 커널과 비교하고, 커널 export 표에서 미해결 심볼을 찾은 뒤, `do_init_module()`이 `init` 함수를 부른다. E3에서 이 경로를 직접 따라간다.

### 12.4 드라이버를 쓸 때 다시 만나는 지점

- **새 드라이버 = Kconfig 항목 + Makefile 한 줄** — 실제 업스트림 패치의 첫 두 hunk가 항상 이것이다(E7, P3).
- `IS_ENABLED()`와 "꺼지면 빈 `static inline`" 헤더 패턴 — 선택 기능(예: 드라이버의 debugfs, PM 콜백)을 `#ifdef` 없이 쓰는 표준 방법(G, H 모듈).
- `EXPORT_SYMBOL_GPL` + `Module.symvers` — 드라이버를 core/bus 모듈로 나눌 때(BMI270 패턴) 반드시 만난다(E3).
- composite 오브젝트 — 실제 드라이버는 대부분 `foo-y := foo_core.o foo_i2c.o …` 형태다.

---

## 13. Embedded 실무에서 다시 보기

### 13.1 장면 1 — "IMU 드라이버를 켰는데 보드 커널에 안 들어가 있다"

센서 키트 BSP에 `CONFIG_BMI270_I2C=y`를 fragment로 추가하고 빌드했는데, 보드에서 `/sys/bus/i2c/drivers/`에 `bmi270`이 없다. 순서대로 확인한다.

1. 보드에서 `zcat /proc/config.gz | grep BMI270` — 실행 중인 커널에 정말 없는지(예제 11). 없다면 빌드 쪽 문제다.
2. 빌드 트리의 `.config`에 `CONFIG_BMI270_I2C`가 있는지. 없으면 **의존성 때문에 버려진 것**이다(예제 3의 A, 예제 9의 함정). menuconfig `/BMI270_I2C`로 `Depends on: I2C [=n]` 같은 줄을 확인한다(예제 4).
3. `.config`에는 있는데 이미지에 없으면, 빌드한 `Image`와 보드에 올라간 `Image`가 다른지(빌드 번호, `uname -v`의 빌드 시각) 확인한다.
4. `=m`이라면 rootfs의 `/lib/modules/$(uname -r)/kernel/drivers/iio/imu/bmi270/`에 `.ko`가 있는지, `modules.dep`이 갱신됐는지(§9.3).

### 13.2 장면 2 — 벤더 BSP defconfig를 리뷰 가능한 상태로 정리하기

벤더가 준 `.config` 1만 줄을 그대로 git에 넣으면 리뷰가 불가능하다. 예제 6처럼 `savedefconfig`로 줄이고, 업스트림 `defconfig`(또는 `imx_v8_defconfig` 같은 SoC defconfig)와 비교해서 **우리 변경만 fragment로 분리**한다. 커널을 올릴 때는 `make olddefconfig` 후 `listnewconfig`/`diffconfig`로 무엇이 새로 생기고 바뀌었는지 리뷰한다(예제 7). Yocto에서는 `bitbake -c menuconfig virtual/kernel` → `bitbake -c diffconfig virtual/kernel`이 fragment를 만들어 준다(K4).

### 13.3 장면 3 — 부팅 시간·플래시 크기 다이어트

"2초 안에 카메라 프레임이 나와야 한다", "SPI-NOR 16 MB에 다 넣어야 한다" 같은 요구가 오면 실습 1의 방법을 쓴다. defconfig에서 출발해 `localmodconfig`로 줄이거나(예제 10), tinyconfig에서 출발해 필요한 것만 fragment로 올린다(예제 12). 측정은 그림 4처럼 `Image`/`Image.gz` 크기와 `PRINTK_TIME` 부팅 로그로 한다(C7). 드라이버를 내장(y)으로 할지 모듈(m)로 할지도 여기서 정한다 — rootfs 마운트 전에 필요한 것(스토리지, 콘솔, 워치독)은 y, 나머지는 m이 기본 전략이다.

### 13.4 장면 4 — "Oops 났는데 주소만 찍혀 있다"

현장 로그에 `pc : 0xffff80008027aec8`만 있고 심볼이 없다(`KALLSYMS=n` 양산 커널). 그 빌드의 `System.map`이나 `vmlinux`가 있으면 바로 해석된다 — 예제 15의 `System.map`에서 `ffff80008027aec8 t e2_hello_init`이 바로 그 주소다. 그래서 CI는 `Image`만이 아니라 `vmlinux`·`System.map`·`.config`를 **빌드 번호와 함께 보관**해야 한다(그림 7).

---

## 14. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| fragment에 의존 대상을 빼먹음 | `.config`에 내 옵션이 없음, 에러 없음 | depends 거짓 → 심볼 소멸 (예제 9) | `merge_config.sh`를 `-m` 없이 돌리거나 빌드 후 grep 검사 |
| `select`로 depends 있는 심볼을 끌어옴 | "unmet direct dependencies" 경고, 이후 링크 에러 | select는 depends 무시 (예제 3 B) | 내가 그 depends를 먼저 가지거나 `depends on`으로 바꿈 |
| `#ifdef CONFIG_FOO`로 기능 분기 | 모듈(m)로 빌드하면 기능이 꺼짐 | m이면 `CONFIG_FOO_MODULE`만 정의 (§9.3) | `IS_ENABLED(CONFIG_FOO)` |
| `scripts/config` 후 `olddefconfig` 생략 | 의도와 다른 값으로 빌드됨 | scripts/config는 의존성 검사 안 함 | 항상 `olddefconfig` → `diffconfig`로 확인 |
| `ARCH=` 없이 menuconfig/olddefconfig | 32-bit 설정이 arm64 설정으로 덮어써짐 | ARCH는 `.config`에 저장 안 됨 (예제 22) | `export ARCH=… CROSS_COMPILE=…`를 스크립트에 고정 |
| 설정과 빌드에 다른 컴파일러 | 매 빌드마다 SYNC, 옵션이 달라짐 | `CC_VERSION_TEXT` 등이 컴파일러에 의존 | 설정 단계부터 `CROSS_COMPILE` 지정 |
| 소스 트리에서 한 번 빌드 후 `O=` 빌드 | "source tree is not clean" | 소스 트리의 `.config`·생성 파일 | `make mrproper` 후 항상 `O=` 사용 |
| 공용 헤더(`printk.h` 등) 수정 | 증분 빌드가 전체 빌드가 됨 | 수백 파일이 include (예제 18: 563개) | 드라이버 전용 헤더 사용 |
| 다른 커널 트리의 `Module.symvers`로 모듈 빌드 | `insmod`: Unknown symbol 또는 vermagic 불일치 | modpost가 엉뚱한 표로 검사 | 보드 커널과 같은 빌드 트리(`make -C <그 O=> M=…`) |
| `localmodconfig`를 CI에서 그대로 실행 | 빌드가 입력을 기다리며 멈춤 | 내부 oldconfig가 새 심볼을 질문 | `yes ""`를 표준 입력으로 공급 (§4.7 코드 참고) |

---

## 15. 면접에서 이렇게 말한다

**Q.** What is the difference between `depends on` and `select` in Kconfig? Why is `select` considered dangerous?

**A.** `depends on`은 내 심볼의 상한이다. 대상이 n이면 내 심볼은 존재하지 않고, m이면 나도 최대 m이다. `select`는 반대로 대상의 하한을 내 값으로 끌어올린다. 문제는 select가 대상의 depends를 보지 않는다는 점이다. 대상이 의존하는 서브시스템이 꺼져 있어도 켜져 버리고, Kconfig는 "unmet direct dependencies" 경고만 찍은 채 진행해서 링크 에러나 런타임 오류로 이어진다. 그래서 select는 프롬프트 없는 라이브러리 심볼에만 쓰고, 내가 그 라이브러리의 depends를 먼저 다 가지고 있게 한다. BMI270 I2C 드라이버가 `depends on I2C`와 `select REGMAP_I2C`를 같이 쓰는 게 정석 예다. 사용자가 끌 수 있어야 하는 추천이라면 `imply`를 쓴다.

> depends on puts an upper bound on my symbol: if the dependency is n my symbol disappears, and if it's m I can be at most m. select does the opposite — it forces the target up to at least my value — and it ignores the target's own dependencies. So it can turn on something whose subsystem is disabled; Kconfig only prints an "unmet direct dependencies" warning and you get link errors or runtime failures later. The rule is to select only non-visible library symbols, and to make sure I already depend on everything the selected symbol depends on — for example an I2C sensor driver says depends on I2C and select REGMAP_I2C. If it's just a recommendation the user should be able to turn off, I use imply.

**꼬리 질문**: "`imply`는요?" → 대상의 기본값만 올리고 대상의 depends는 존중한다. 사용자가 n으로 내리면 그대로 n이다. 직접 olddefconfig로 여섯 조합을 돌려 확인했다.

**Q.** How do you add a new driver to the kernel source tree so it builds as a module or built-in?

**A.** 세 군데를 고친다. 드라이버 디렉터리에 `tristate`로 Kconfig 항목을 만들고 필요한 버스는 `depends on`으로 건다. 같은 디렉터리 Makefile에 `obj-$(CONFIG_FOO) += foo.o`를 쓰고, 파일이 여러 개면 `foo-y := a.o b.o`로 composite를 만든다. 그리고 부모 Kconfig에 `source`, 부모 Makefile에 `obj-$(CONFIG_FOO) += foo/`를 추가한다. `.config`가 y면 `built-in.a`를 거쳐 vmlinux에, m이면 modpost를 거쳐 `foo.ko`가 된다. 같은 소스를 두 방식으로 빌드해 부팅 로그에서 `IS_BUILTIN`/`IS_MODULE` 값이 바뀌는 것까지 확인해 봤다.

> Three places. In the driver's directory I add a tristate Kconfig entry with depends on for the bus it needs, and a Makefile line obj-$(CONFIG_FOO) += foo.o — or a composite foo-y := a.o b.o if there are several files. Then the parent Kconfig sources the new Kconfig and the parent Makefile descends into the directory with obj-$(CONFIG_FOO) += foo/. If the option is y, the objects go into built-in.a and get linked into vmlinux; if it's m, modpost generates the module glue and you get foo.ko. I've built the same driver both ways and watched IS_BUILTIN and IS_MODULE flip in the boot log.

**Q.** Why does the kernel use `IS_ENABLED(CONFIG_FOO)` instead of `#ifdef CONFIG_FOO`?

**A.** 두 가지 이유다. 첫째, tristate가 m이면 `autoconf.h`에는 `CONFIG_FOO_MODULE`만 정의되므로 `#ifdef CONFIG_FOO`는 모듈 빌드에서 거짓이 된다 — 실제로 같은 코드가 내장일 때 1, 모듈일 때 0을 찍었다. `IS_ENABLED`는 y와 m 둘 다 참이다. 둘째, `if (IS_ENABLED(...))`는 일반 C 조건문이라 꺼져 있어도 그 코드가 컴파일러의 문법·타입 검사를 받고 나서 제거된다. `#ifdef`로 가린 코드는 그 설정으로 빌드하기 전까지 깨져도 모른다.

> Two reasons. For a tristate set to m, autoconf.h only defines CONFIG_FOO_MODULE, so #ifdef CONFIG_FOO is false in a modular build — I've seen the same code print 1 built-in and 0 as a module. IS_ENABLED is true for both y and m. Second, IS_ENABLED is a constant expression you can use in a normal if, so the disabled branch is still parsed and type-checked before the compiler drops it, whereas code hidden behind #ifdef can rot unnoticed until someone builds that configuration.

**꼬리 질문**: "`IS_REACHABLE`은?" → 내장 코드가 모듈로 빌드된 기능을 부르려 할 때 거짓이 된다. Kconfig에서 `depends on FOO || !FOO`로 막는 것이 보통이다.

**Q.** What is a defconfig and how do you manage kernel configuration for a product?

**A.** `.config`는 1만 줄이 넘고 툴체인이 감지한 값까지 들어 있어서 소스로 관리하기 나쁘다. 그래서 기본값과 다른 줄만 남긴 defconfig(`make savedefconfig`)를 git에 넣고 `make <board>_defconfig`로 `.config`를 재생성한다. 우리 변경은 보드·기능별 fragment로 나누고 `merge_config.sh`로 합친다. 커널을 올릴 때는 `olddefconfig` 후 `listnewconfig`와 `diffconfig`로 무엇이 바뀌었는지 리뷰한다. 랩 커널을 savedefconfig로 줄여 보니 arm64 defconfig와 정확히 3줄 차이였다 — 1만 줄 diff로는 못 찾는 정보다.

> The full .config is over ten thousand lines and includes values detected from the toolchain, so it's a bad thing to version. Instead I keep a minimal defconfig from make savedefconfig in git and regenerate .config with make board_defconfig. Our own changes live in small fragments per feature, merged with merge_config.sh, and in CI I check that the requested values actually made it into the final .config, because Kconfig silently drops values whose dependencies aren't met. On a kernel upgrade I run olddefconfig, then review listnewconfig and diffconfig output.

**Q.** What's the difference between vmlinux, Image, and Image.gz on arm64? What is System.map?

**A.** `vmlinux`는 링크 결과 ELF로 심볼과 디버그 정보가 있어서 gdb나 Oops 해석에 쓰고, 보드에 올리지 않는다. `Image`는 objcopy로 ELF를 벗긴 평평한 바이너리에 64바이트 arm64 부트 헤더(매직 `ARM\x64`, 메모리상 크기, 플래그)가 붙은 것으로, U-Boot `booti`가 적재한다. `Image.gz`는 그 gzip본인데, arm64 커널은 32-bit의 zImage처럼 스스로 압축을 풀지 못해서 부트로더가 푼다. `System.map`은 심볼 주소표다. 랩 커널은 vmlinux 162 MB(디버그 정보), Image 42.7 MB, Image.gz 15.2 MB였다.

> vmlinux is the linked ELF with symbols and possibly DWARF — you keep it for gdb and for decoding oopses, but you don't flash it. Image is vmlinux run through objcopy into a flat binary, with the 64-byte arm64 boot header — magic, image size including BSS, flags — that U-Boot's booti reads. Image.gz is just that compressed; unlike 32-bit zImage, arm64 has no self-decompressor, so the bootloader or a FIT image handles decompression. System.map is the sorted symbol table you use to turn addresses into function names.

**Q.** You changed one Kconfig option and the rebuild was quick, but touching one header rebuilt everything. Why?

**A.** 모든 파일이 `autoconf.h`를 include하지만, Kbuild의 fixdep이 의존성을 `autoconf.h` 대신 `include/config/<심볼>` 파일 단위로 바꿔 쓴다. syncconfig는 값이 바뀐 심볼의 파일만 touch하므로 그 문자열이 들어 있는 소스만 다시 컴파일된다. 실제로 정수 옵션 하나를 바꾸니 파일 하나만 다시 컴파일됐다. 반면 `printk.h`를 touch하면 그걸 include하는 563개가 전부 다시 컴파일됐다. 헤더 의존성은 파일 단위이기 때문이다.

> Every file includes autoconf.h, but fixdep rewrites the dependency list so that instead of depending on autoconf.h, each object depends on one small file per CONFIG symbol it mentions, under include/config. syncconfig only touches the files for symbols whose value changed, so only the sources that mention that symbol rebuild — I measured one file for an int option. Header dependencies are per file, though, so touching printk.h rebuilt 563 objects in my tiny config.

**Q.** How do you cross-compile the kernel, and what are the common mistakes?

**A.** `ARCH`로 아키텍처 디렉터리와 Kconfig 루트를, `CROSS_COMPILE`로 툴체인 접두어를 정하고, 설정부터 빌드까지 모든 make 명령에 같은 값을 준다. 흔한 실수는 세 가지다. `ARCH` 없이 menuconfig를 돌리면 호스트 아키텍처 Kconfig로 설정이 조용히 덮어써진다. 설정할 때와 빌드할 때 컴파일러가 다르면 `CC_VERSION_TEXT`가 바뀌어 설정이 재계산된다. `CROSS_COMPILE`을 빼면 호스트 gcc가 `-mabi=aapcs-linux` 같은 플래그를 몰라 바로 실패한다. 세 경우 모두 실제로 재현해 봤다.

> ARCH selects the arch directory and Kconfig root, CROSS_COMPILE is the toolchain prefix, and you pass the same values to every make invocation, configuration included — I export them in the build script. The classic mistakes: running menuconfig or olddefconfig without ARCH silently rewrites the config for the host architecture; configuring with one compiler and building with another makes Kconfig resync because compiler-dependent symbols change; and forgetting CROSS_COMPILE makes the host gcc choke on ARM-specific flags.

---

## 16. 직접 해보기

1. **개념** — `config A` / `bool "A" if EXPERT` / `default y`인 심볼이 있다. `make allnoconfig` 후 A의 값은? `make tinyconfig` 후는? 이유를 한 문장으로. — 정답/힌트: allnoconfig에서는 `EXPERT=n`이라 프롬프트가 숨어 default y, tinyconfig에서는 `EXPERT=y`라 보이므로 n(예제 10).
2. **Kconfig 실험** — E2 Kconfig에 `config E2_ROBUST` / `bool "…"` / `depends on E2_CODEC || !E2_CODEC`를 추가하고, CODEC이 y/m/n일 때 ROBUST가 가질 수 있는 최대값을 `scripts/config` + `olddefconfig`로 확인하라. — 정답/힌트: tristate로 만들어야 차이가 보인다. CODEC=m이면 최대 m, y나 n이면 y까지 가능. 이것이 `IS_REACHABLE` 문제를 막는 관용구다.
3. **fragment 검증 스크립트** — fragment 파일의 모든 `CONFIG_X=값` 줄이 최종 `.config`에 그대로 있는지 검사해서, 빠진 줄을 출력하고 실패 코드를 내는 셸 스크립트를 작성하라. `e2_boot.config`와 예제 9의 `bad.config`로 시험하라. — 정답/힌트: `while read l; do grep -qxF "$l" .config 또는 실패 출력; done`. `# CONFIG_X is not set` 줄도 같이 처리한다.
4. **Kbuild 과제** — `e2_stats.c`를 별도 모듈 `e2_stats.ko`로 분리하고(`obj-$(CONFIG_E2_HELLO) += e2_stats.o` 대신 새 tristate 심볼), `e2_hello`가 그것을 쓰게 바꿔라. `modules.dep`에 의존성이 생기는지, `modprobe e2_hello`가 `e2_stats`를 먼저 올리는지 게스트에서 확인하라. — 정답/힌트: `e2_hello`의 Kconfig에 `select E2_STATS` 또는 `depends on E2_STATS`. `modinfo -F depends e2_hello.ko` → `e2_stats`.
5. **단일 타깃 관찰** — `make O=… kernel/printk/printk.i`를 만들고 `wc -l`로 줄 수를 세라. 그다음 `make O=… kernel/printk/printk.lst`에서 `vprintk_emit` 근처를 찾아 Kconfig 값(`CONFIG_LOG_BUF_SHIFT` 등)이 즉시값으로 들어간 곳을 찾아라. — 정답/힌트: tuned 설정은 `PRINTK=y`라 파일이 빌드된다. `.lst`는 디버그 정보가 없으면 소스 줄이 섞이지 않을 수 있다 — 그럴 땐 `.s`를 본다.
6. **fixdep 관찰** — `CONFIG_E2_STYLE_LOUD`를 켜면 다시 컴파일되는 파일이 무엇인지 예측하고, `make V=2`로 이유(`due to: include/config/E2_STYLE_LOUD`)를 확인하라. — 정답/힌트: `e2_main.o`만. `e2_stats.c`는 그 문자열을 안 쓴다.
7. **크로스 + 부팅 없는 검증** — `ARCH=arm multi_v7_defconfig` 트리에서 `make … dtbs` 대신 `make … arm/vexpress-v2p-ca9.dtb`처럼 32-bit 보드 dtb 하나를 만들어 보라(경로는 `arch/arm/boot/dts/`의 실제 위치를 `find`로 먼저 확인). — 정답/힌트: 6.x에서 32-bit ARM dts는 `arch/arm/boot/dts/<vendor>/`로 옮겨졌다. 타깃 경로도 `<vendor>/<board>.dtb`.

---

## 17. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| Kconfig | 커널 설정 언어 | 심볼·타입·의존성·기본값을 정의하는 파일들 |
| 심볼 (symbol) | `config FOO`로 정의된 설정 변수 | 결과에서 `CONFIG_FOO` |
| tristate | y / m / n 세 값 타입 | m = 모듈로 빌드 |
| `depends on` | 의존성 (상한) | 거짓이면 심볼이 사라짐 |
| `select` | 역방향 강제 (하한) | 대상의 depends 무시 |
| `imply` | 약한 역방향 (기본값만) | 대상의 depends 존중, 사용자가 끌 수 있음 |
| `.config` | 설정 결과 파일 | 빌드 트리마다 하나 |
| defconfig | 최소 설정 파일 | 기본값과 다른 줄만, git에 넣는 형태 |
| fragment | 설정 조각 파일 | `merge_config.sh`로 합침 |
| syncconfig | `.config` → 생성 파일 동기화 단계 | 빌드 시작 시 자동 실행 |
| `auto.conf` | make용 설정 | `obj-$(CONFIG_FOO)` 전개에 사용 |
| `autoconf.h` | C용 설정 헤더 | 모든 컴파일에 `-include` |
| `IS_ENABLED()` | y 또는 m이면 1 | `#ifdef` 대신 쓰는 매크로 |
| Kbuild | 커널 Make 프레임워크 | `scripts/Makefile.*` |
| `obj-y` / `obj-m` | 내장 / 모듈 목록 | `built-in.a` / `.ko` |
| composite object | 여러 `.o`를 묶은 하나의 모듈 | `foo-y := a.o b.o` |
| `built-in.a` | 디렉터리별 내장 오브젝트 thin archive | 위로 합쳐져 `vmlinux.a` |
| fixdep | 의존성 후처리 도구 | `CONFIG_` 심볼 단위 증분 빌드 |
| modpost | 모듈 후처리 도구 | 심볼 검사, `.mod.c`, `Module.symvers` |
| `Module.symvers` | export 심볼 표 | 모듈 빌드 시 심볼 해석·CRC 기준 |
| vermagic | 모듈의 커널 버전·설정 지문 | 로드 시 실행 중 커널과 비교 |
| `vmlinux` | 링크된 커널 ELF | 디버그·분석용 |
| `Image` | arm64 부팅 바이너리 | 64바이트 헤더 + 평평한 이미지 |
| `System.map` | 커널 심볼 주소표 | Oops 주소 해석 |
| initcall | 부팅 중 레벨별로 호출되는 init 함수 표 | 내장 드라이버의 `module_init` |
| `O=` | 출력 디렉터리 | 소스 트리를 깨끗하게, 설정별 트리 분리 |
| `ARCH` / `CROSS_COMPILE` | 대상 아키텍처 / 툴체인 접두어 | 크로스 빌드의 두 변수 |
| IKCONFIG | 커널 안에 `.config` 내장 | `/proc/config.gz` (`IKCONFIG_PROC`) |

---

## 18. 요약 & 체크리스트

커널 빌드는 세 층이다. Kconfig는 2만 개가 넘는 심볼의 타입·기본값·의존성을 정의하고, `sym_calc_value()`가 `depends on`(상한)·`select`(depends를 무시하는 강제 하한)·`imply`(기본값 추천)를 계산해 `.config`를 만든다. 빌드 시작 때 syncconfig가 이를 `auto.conf`(make 변수), `autoconf.h`(C 매크로, m이면 `_MODULE` 접미사), `include/config/<SYM>`(심볼 단위 타임스탬프)로 번역한다. Kbuild는 최상위 `Kbuild`의 순서대로 디렉터리를 재귀하며 `obj-y`를 `built-in.a`로 모아 `vmlinux.a` → `vmlinux` → `Image`(→ `Image.gz`)를 만들고, 이 링크 순서가 같은 레벨 initcall의 실행 순서가 된다. `obj-m`은 부분 링크 후 modpost가 심볼 검사·`.mod.c`·`Module.symvers`를 만들어 `.ko`가 되고, `modules_install`이 `lib/modules/<release>/`에 depmod 색인과 함께 설치한다. 설정은 `savedefconfig`로 줄인 defconfig와 fragment로 관리하고, fragment 값이 의존성 때문에 조용히 버려질 수 있으니 검증한다. 실습에서 tinyconfig + 13줄로 3.4 MB짜리 부팅 가능한 커널을 만들었고(defconfig 42.7 MB), Kconfig 정수 하나 변경은 파일 1개, 전역 헤더 하나는 560개 이상을 다시 컴파일시켰다. 크로스 빌드는 모든 명령에 같은 `ARCH`·`CROSS_COMPILE`을 준다.

- [ ] `depends on`/`select`/`imply`의 차이와 select 함정을 예로 설명할 수 있다
- [ ] `visible if`·조건부 프롬프트(`if EXPERT`)가 값을 남기는 이유를 설명할 수 있다
- [ ] `.config`가 `auto.conf`·`autoconf.h`·`include/config/`로 바뀌는 길을 그릴 수 있다
- [ ] `IS_ENABLED`/`IS_BUILTIN`/`IS_MODULE`/`IS_REACHABLE`을 구분해 쓰고, `#ifdef`의 모듈 함정을 설명할 수 있다
- [ ] defconfig/savedefconfig/olddefconfig/listnewconfig/`scripts/config`/fragment로 설정을 재현 가능하게 관리할 수 있다
- [ ] tinyconfig와 allnoconfig의 차이를 설명하고 QEMU에서 부팅 가능한 최소 설정을 만들 수 있다
- [ ] 트리 안에 Kconfig + Makefile + 소스로 드라이버를 추가하고 y/m 둘 다로 빌드할 수 있다
- [ ] composite 오브젝트, `ccflags-y`, `CFLAGS_file.o`를 쓸 수 있다
- [ ] `V=1`, `V=2`, `.i`/`.s`/`.lst`, `dir/`, `.ko` 단일 타깃으로 빌드를 들여다볼 수 있다
- [ ] fixdep이 심볼 단위 증분 빌드를 만드는 원리와 헤더 변경 비용을 설명할 수 있다
- [ ] modpost의 역할, `Module.symvers`, `modules_install` 레이아웃을 설명할 수 있다
- [ ] `vmlinux`/`Image`/`Image.gz`/`System.map`의 차이와 보관해야 할 산출물을 말할 수 있다
- [ ] `ARCH=`/`CROSS_COMPILE=`의 역할과 세 가지 흔한 실수를 말할 수 있다

---

## 참고 자료

- docs.kernel.org — Kconfig Language: https://docs.kernel.org/kbuild/kconfig-language.html
- docs.kernel.org — Kconfig make config (설정 타깃, `KCONFIG_*` 환경 변수): https://docs.kernel.org/kbuild/kconfig.html
- docs.kernel.org — Linux Kernel Makefiles (`obj-y`, composite, `ccflags-y`): https://docs.kernel.org/kbuild/makefiles.html
- docs.kernel.org — Kbuild (출력 파일, 환경 변수): https://docs.kernel.org/kbuild/kbuild.html
- docs.kernel.org — Building External Modules (`M=`, `Module.symvers`): https://docs.kernel.org/kbuild/modules.html
- docs.kernel.org — Booting AArch64 Linux (Image 헤더): https://docs.kernel.org/arch/arm64/booting.html
- 커널 소스 (이 노트에서 인용한 위치, v6.18): `scripts/kconfig/symbol.c`(`sym_calc_value`, `sym_warn_unmet_dep`), `scripts/kconfig/confdata.c`(`conf_write`, `conf_write_autoconf`, `conf_touch_deps`), `scripts/kconfig/Makefile`(tinyconfig), `scripts/kconfig/merge_config.sh`, `scripts/basic/fixdep.c`, `scripts/mod/modpost.c`(`read_symbols`, `check_exports`, `write_dump`), `scripts/Makefile.build`, `scripts/Makefile.vmlinux_o`, `scripts/link-vmlinux.sh`, `include/linux/kconfig.h`, `include/linux/module.h`, `include/linux/init.h`, `init/main.c`(`do_initcalls`), `kernel/module/main.c`(`load_module`, `check_modinfo`, `do_init_module`), `kernel/configs/tiny.config` — https://elixir.bootlin.com/linux/v6.18/source
- Bootlin, "Embedded Linux system development" 및 "Linux kernel and driver development" 슬라이드 (커널 설정·빌드 장): https://bootlin.com/training/
- B4(이 노트 세트) — Make·Kbuild/Kconfig 미리보기, out-of-tree 모듈 빌드
