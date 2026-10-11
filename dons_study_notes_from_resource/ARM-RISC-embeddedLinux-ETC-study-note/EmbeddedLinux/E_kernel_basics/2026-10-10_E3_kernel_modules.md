# E3. 커널 모듈 (LKM) — 만들고, 넣고, 빼고, 서로 부르기

> **이 노트를 다 읽으면**: hello 모듈을 화이트보드에 쓰고 out-of-tree Makefile로 빌드해 QEMU 게스트에서 `insmod`/`rmmod`할 수 있다 · `modinfo`의 vermagic·depends·parm·alias가 어디서 오는지, `insmod`와 `modprobe`(depmod 색인)가 어떻게 다른지 설명할 수 있다 · 두 모듈 사이에 `EXPORT_SYMBOL_GPL`/네임스페이스로 함수를 내보내고, 로드 순서·refcount·"in use"·"Unknown symbol"을 재현하고 고칠 수 있다 · `finit_module` → `load_module` → `do_init_module` 경로와 taint 플래그(O, P, E)를 소스 이름 그대로 말할 수 있다
> **왜 필요한가**: 드라이버 면접의 첫 실습 질문은 거의 항상 "hello world 모듈을 써 보라"이고, 입사 첫 주에 벤더 BSP의 `.ko`가 "Unknown symbol"·"version magic"·"Module is in use"로 안 올라가는 장면을 반드시 만난다
> **Don 기준 난이도**: 링커·심볼·재배치·init 순서는 펌웨어에서 이미 안다 / 새로 배울 부분은 "실행 중인 커널에 링크를 런타임에 한다"는 점, 그 링크를 검사하는 장치들(vermagic, Module.symvers, GPL 심볼, 네임스페이스), 그리고 refcount로 언로드를 막는 규칙
> **선행 노트**: B3(ELF와 링킹), C6(커널 시작·initcall), E1(커널 구조), E2(설정과 빌드 — Kbuild `obj-m`) — E4(커널 프로그래밍 제약)·E5(커널 C 관용구)·G3(platform driver)로 이어진다

---

## 0. 큰 그림 — 이게 왜 필요한가

Don의 SSD 펌웨어를 떠올려 보자. `main.c`, `nand_drv.c`, `nvme.c`를 전부 컴파일해서 링커 스크립트로 **한 번에** 링크하면 `firmware.elf`가 나온다. 모든 함수 주소가 빌드 때 확정된다. 기능 하나를 고치려면 다시 빌드하고 다시 플래시한다.

Linux 커널도 기본은 똑같다. `vmlinux`(= `Image`)는 수천 개의 `.o`를 한 번에 링크한 거대한 펌웨어다. 그런데 Linux에는 **실행 중인 커널에 새 코드를 끼워 넣는 장치**가 하나 더 있다. 그게 **로더블 커널 모듈(LKM, Loadable Kernel Module)** 이고 파일 확장자는 `.ko`(kernel object)다.

- `.ko`는 **아직 링크가 덜 끝난 ELF 오브젝트**다. 커널 함수(`printk`, `kmalloc` …)를 부르는 자리가 "미해결 심볼 + 재배치 항목"으로 남아 있다.
- `insmod`가 이 파일을 커널에 넘기면, 커널 안의 **모듈 로더**(`kernel/module/main.c`)가 메모리를 잡고, 심볼을 커널 심볼 표에서 찾아 주소를 채우고(재배치), `init` 함수를 부른다. **링커가 하던 일을 커널이 런타임에 한다.**
- 올라간 모듈은 커널과 **같은 주소 공간, 같은 특권(EL1)** 에서 돈다. 유저 프로세스처럼 격리된 게 아니다. 모듈의 NULL 역참조는 곧 커널 Oops다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="165" y="24" text-anchor="middle" font-size="13" font-weight="bold">펌웨어 — 빌드 때 한 번 링크</text>
  <text x="510" y="24" text-anchor="middle" font-size="13" font-weight="bold">Linux — 실행 중에 기능을 링크</text>
  <line x1="335" y1="10" x2="335" y2="255" stroke="#888" stroke-dasharray="4 4"/>
  <rect x="30" y="44" width="120" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="90" y="66" text-anchor="middle" font-size="12">main.c</text>
  <rect x="170" y="44" width="120" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="230" y="66" text-anchor="middle" font-size="12">nand_drv.c</text>
  <line x1="90" y1="78" x2="140" y2="108" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="230" y1="78" x2="180" y2="108" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="30" y="110" width="260" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="160" y="132" text-anchor="middle" font-size="12">ld + linker script</text>
  <line x1="160" y1="144" x2="160" y2="174" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="30" y="176" width="260" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="160" y="198" text-anchor="middle" font-size="12">firmware.elf — 모든 주소 확정</text>
  <text x="160" y="240" text-anchor="middle" font-size="12">기능 추가 = 다시 빌드 · 다시 플래시</text>
  <rect x="360" y="44" width="140" height="34" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="430" y="66" text-anchor="middle" font-size="12">vmlinux (실행 중)</text>
  <rect x="520" y="44" width="140" height="34" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="590" y="66" text-anchor="middle" font-size="12">e3_user.ko</text>
  <line x1="430" y1="78" x2="470" y2="108" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="590" y1="78" x2="550" y2="108" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="395" y="98" text-anchor="middle" font-size="12">심볼 표</text>
  <text x="630" y="98" text-anchor="middle" font-size="12">insmod</text>
  <rect x="360" y="110" width="300" height="50" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="510" y="130" text-anchor="middle" font-size="12">module loader (kernel/module/main.c)</text>
  <text x="510" y="150" text-anchor="middle" font-size="12">심볼 해석 + 재배치 + init 호출</text>
  <line x1="510" y1="160" x2="510" y2="184" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="360" y="186" width="300" height="34" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="510" y="208" text-anchor="middle" font-size="12">커널 일부가 된 모듈 (같은 주소 공간·특권)</text>
  <text x="510" y="246" text-anchor="middle" font-size="12">rmmod로 다시 뺀다 — 재부팅 없음</text>
  <text x="340" y="284" text-anchor="middle" font-size="12">둘 다 "같은 주소 공간, 같은 특권"이다. 차이는 링크 시점뿐이다.</text>
</svg>
```
그림 1 — 왼쪽은 Don이 아는 펌웨어 빌드, 오른쪽은 Linux 모듈. 모듈 로더는 "커널 안에 들어 있는 링커 + 로더"다.

모듈을 쓰는 이유는 실무적이다.

| 이유 | 장면 |
|---|---|
| 개발 속도 | 드라이버 하나 고칠 때마다 커널 전체를 다시 굽고 재부팅하지 않는다. `rmmod` → 빌드 → `insmod`로 몇 초 만에 반복한다 |
| 이미지 크기·부팅 시간 | 배포판 커널은 수천 개 드라이버를 `=m`으로 두고 장치가 있을 때만 올린다 (D3 §9의 modalias 자동 로딩) |
| 하드웨어 변형 | 같은 rootfs를 IMU 칩이 다른 보드 리비전 두 개에 쓴다. 맞는 모듈만 로드된다 |
| 벤더 코드 | GPU·Wi-Fi·NPU 벤더는 드라이버를 out-of-tree `.ko`로 준다. 이걸 우리 커널에 맞춰 빌드하는 게 BSP 엔지니어의 일상이다 |

이 노트에서는 실제로 모듈 9개(`e3_hello`, `e3_multi`, `e3_param`, `e3_fail`, `e3_hold`, `e3_lib`, `e3_user`, `e3_prop`, 실패용 `e3_nolic`)를 만들어 **QEMU 게스트 커널에 넣고 빼면서** 모든 규칙을 눈으로 확인한다.

> 실행 환경: 빌드는 Lima VM `elinux`(aarch64, 호스트 커널 `uname -r` = `7.0.0-38-generic`)에서 랩의 커널 빌드 트리 `/var/lab/out/linux-build`(Linux **6.18.55**, `Module.symvers` 있음)를 대상으로 했다. 실행은 전부 QEMU `virt`(cortex-a57, 512 MB, TCG)로 부팅한 **게스트 커널 6.18.55** + BusyBox 1.37 initramfs 안이다. VM 호스트 커널(7.0)에는 모듈을 하나도 넣지 않았다. 커널 소스 인용은 `/var/lab/src/linux-6.18.55`에서 grep으로 확인했다. 모듈 주소·타임스탬프·`umod=` 포인터는 실행마다 다르다.

### 0.1 실습 도구 — 빌드하고 QEMU에 넣는 두 스크립트

모든 예제는 같은 방식으로 돈다. VM의 `/var/tmp/E3/` 아래에서 모듈을 빌드하고, 랩 rootfs를 복사해 `.ko`와 게스트용 스크립트를 넣고, `LAB_CMD=sh~/e3/tN.sh`로 부팅하면 스크립트 하나를 실행하고 자동으로 꺼진다.

```sh
# mkinitrd.sh — 랩 rootfs 복사 → .ko·스크립트 추가 → cpio로 다시 묶기
W=/var/tmp/E3; R=$W/rootfs
rm -rf $R && mkdir -p $R && cp -a /var/lab/build/rootfs/. $R/
mkdir -p $R/e3 $R/lib/modules/6.18.55/extra
cp $W/base/*.ko $W/lib/e3_lib.ko $W/user/e3_user.ko $W/prop/e3_prop.ko \
   $W/patched/*.ko /var/lab/build/linux/lib/crc/crc-ccitt.ko $R/e3/
cp $W/lib/e3_lib.ko $W/user/e3_user.ko $W/base/e3_param.ko $W/base/e3_hello.ko \
   $R/lib/modules/6.18.55/extra/
cp /Users/donh/.elinux_scratch/E3/guest/*.sh $R/e3/
(cd $R && find . | cpio -o -H newc --quiet | gzip) > $W/i.gz
```

```sh
# boot.sh <게스트 스크립트> [추가 커널 인자]
S=$1; shift
timeout 120 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -kernel /var/lab/out/Image -initrd /var/tmp/E3/i.gz \
  -append "console=ttyAMA0 quiet $* LAB_CMD=sh~/e3/$S" 2>&1 | tr -d '\r' \
  | sed -n '/lab initramfs/,$p' | grep -v "reboot: Power down"
```

`quiet`라서 콘솔에는 `pr_warn`/`pr_err`급 메시지만 섞여 나오고, `pr_info`는 스크립트 안에서 `dmesg`로 꺼내 본다. 게스트에는 `/lib/modules`가 비어 있고(랩 README: 랩 커널의 `.ko`는 initramfs에 설치되지 않음), 우리가 넣은 것만 있다.

---

## 1. 모듈의 해부 — hello 모듈 한 개

### 1.1 직관

펌웨어의 드라이버 모듈은 보통 `xxx_init()`과 (가끔) `xxx_deinit()`을 가진다. `main()`이 순서대로 init을 부른다. 커널 모듈도 같다. 차이는 **누가 init을 부르느냐**다. 모듈은 `main()`이 없고, **"로드될 때 이걸 불러 줘", "빠질 때 이걸 불러 줘"** 라고 두 함수를 커널에 등록만 한다.

### 1.2 예제 1 — `e3_hello.c`와 out-of-tree Makefile

무엇을 확인하나: 모듈이 가져야 할 최소 구성 — init/exit 함수, `module_init`/`module_exit` 등록, `__init`/`__exit` 섹션 표시, 메타데이터 매크로 — 와 커널 트리 밖에서 빌드하는 Makefile.

```c
// SPDX-License-Identifier: GPL-2.0
/* e3_hello: 가장 작은 모듈 — init/exit, __init/__exit, 메타데이터 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>

static int __init e3_hello_init(void)
{
	/* init 실행 중에는 MODULE_STATE_COMING(1) 상태다 */
	pr_info("hello from init, state=%d, fn=%ps\n",
		THIS_MODULE->state, e3_hello_init);
	return 0;		/* 0 = 성공. 음수 errno = 로드 실패 */
}

static void __exit e3_hello_exit(void)
{
	pr_info("bye from exit, state=%d\n", THIS_MODULE->state);
}

module_init(e3_hello_init);
module_exit(e3_hello_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Don <don@example.com>");
MODULE_DESCRIPTION("E3 minimal hello module");
MODULE_VERSION("1.0");
```

```make
obj-m := e3_hello.o
KDIR ?= /var/lab/out/linux-build

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules
clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
```

```sh
mkdir -p /var/tmp/E3/hello && cp ~/.elinux_scratch/E3/hello/* /var/tmp/E3/hello/
cd /var/tmp/E3/hello && make
ls
```

```text
make -C /var/lab/out/linux-build M=/var/tmp/E3/hello modules
make[1]: Entering directory '/var/lab/build/linux'
make[2]: Entering directory '/var/tmp/E3/hello'
  CC [M]  e3_hello.o
  MODPOST Module.symvers
  CC [M]  e3_hello.mod.o
  CC [M]  .module-common.o
  LD [M]  e3_hello.ko
make[2]: Leaving directory '/var/tmp/E3/hello'
make[1]: Leaving directory '/var/lab/build/linux'
Makefile
Module.symvers
e3_hello.c
e3_hello.ko
e3_hello.mod
e3_hello.mod.c
e3_hello.mod.o
e3_hello.o
modules.order
```

출력에서 볼 것: 경고 0개. `CROSS_COMPILE`을 안 줬는데도 되는 것은 VM이 arm64이고 대상 커널도 arm64이기 때문이다(x86 PC에서 보드용으로 빌드하면 `ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-`가 필요하다, B1). 빌드는 4단계다: 내 `.c` → `.o`, **modpost**(심볼·라이선스 검사, `.mod.c` 생성), `.mod.c` → `.mod.o`, 그리고 셋을 묶어 `.ko`.

코드 한 줄씩:

| 요소 | 뜻 | 빠뜨리면 |
|---|---|---|
| `module_init(fn)` | "로드되면 `fn`을 불러라". 모듈로 빌드되면 `fn`의 별칭 `init_module`을 만든다 | init이 안 불린다(로드는 됨) |
| `module_exit(fn)` | "언로드할 때 `fn`을 불러라". 별칭 `cleanup_module` | **rmmod 불가** — `[permanent]` (§4) |
| `__init` | 함수를 `.init.text` 섹션에 넣는다 → init이 끝나면 메모리를 버린다 | 동작은 같고 메모리만 낭비 |
| `__exit` | `.exit.text`에 넣는다. built-in이면 아예 버린다(쓸 일이 없으니까) | 동작은 같다 |
| `MODULE_LICENSE` | 라이선스 문자열. GPL 호환이면 GPL 전용 심볼을 쓸 수 있다 | **빌드 에러** (6.18 modpost, §9) |
| `MODULE_DESCRIPTION` | 설명 | modpost **경고** (아래) |
| `MODULE_AUTHOR`, `MODULE_VERSION` | 메타데이터. `VERSION`이 있으면 `srcversion`도 붙는다 | 상관없음 |
| `pr_fmt` | 모든 `pr_*()` 앞에 `"e3_hello: "`를 붙인다 | 로그에서 모듈을 구분하기 어렵다 |

> 함정: 처음 base 디렉터리를 빌드할 때 `e3_fail.c`에 설명을 안 넣었더니 modpost가 `WARNING: modpost: missing MODULE_DESCRIPTION() in e3_fail.o`를 냈다. 6.x 최근 커널은 설명 없는 모듈을 경고한다. "경고 0개" 규칙을 지키려면 `MODULE_DESCRIPTION`도 필수로 생각하자.

### 1.3 out-of-tree 빌드는 어떻게 굴러가나

`make -C $(KDIR) M=$(CURDIR) modules`는 "커널 빌드 트리로 가서(`-C`), 외부 디렉터리(`M=`)의 모듈을 빌드해라"다. 커널의 최상위 Makefile이 우리 디렉터리의 `Makefile`(또는 `Kbuild` 파일)을 **Kbuild 문법으로 다시 읽어서** `obj-m` 목록을 얻는다. 그래서 같은 파일 안에 "사람이 치는 `make` 규칙(`all:`)"과 "Kbuild가 읽는 `obj-m`"이 같이 있다. 큰 프로젝트는 둘을 나눠 `Kbuild` 파일에 `obj-m`만 두기도 한다.

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="340" y="28" text-anchor="middle" font-size="13" font-weight="bold">make -C /var/lab/out/linux-build M=$PWD modules</text>
  <rect x="10" y="50" width="118" height="54" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="69" y="72" text-anchor="middle" font-size="12">e3_hello.c</text>
  <text x="69" y="92" text-anchor="middle" font-size="12">obj-m := x.o</text>
  <rect x="146" y="50" width="118" height="54" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="205" y="72" text-anchor="middle" font-size="12">CC [M]</text>
  <text x="205" y="92" text-anchor="middle" font-size="12">e3_hello.o</text>
  <rect x="282" y="50" width="118" height="54" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="341" y="72" text-anchor="middle" font-size="12">MODPOST</text>
  <text x="341" y="92" text-anchor="middle" font-size="12">심볼·라이선스 검사</text>
  <rect x="418" y="50" width="118" height="54" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="477" y="72" text-anchor="middle" font-size="12">e3_hello.mod.o</text>
  <text x="477" y="92" text-anchor="middle" font-size="12">+ module-common</text>
  <rect x="554" y="50" width="118" height="54" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="613" y="72" text-anchor="middle" font-size="12">LD [M]</text>
  <text x="613" y="92" text-anchor="middle" font-size="12">e3_hello.ko</text>
  <line x1="128" y1="77" x2="144" y2="77" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="264" y1="77" x2="280" y2="77" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="400" y1="77" x2="416" y2="77" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="536" y1="77" x2="552" y2="77" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="146" y="150" width="390" height="48" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="341" y="170" text-anchor="middle" font-size="12">커널 빌드 트리 (/var/lab/out/linux-build)</text>
  <text x="341" y="189" text-anchor="middle" font-size="12">헤더 · .config · Kbuild 규칙 · Module.symvers · vermagic</text>
  <line x1="205" y1="150" x2="205" y2="106" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah2)"/>
  <line x1="341" y1="150" x2="341" y2="106" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah2)"/>
  <line x1="477" y1="150" x2="477" y2="106" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah2)"/>
  <text x="340" y="230" text-anchor="middle" font-size="12">커널 쪽 Kbuild가 내 Makefile을 다시 읽어 obj-m 목록을 빌드한다</text>
</svg>
```
그림 2 — out-of-tree 빌드. 모듈은 **반드시 대상 커널의 빌드 트리**(같은 `.config`, 같은 헤더, 같은 `Module.symvers`)로 빌드해야 한다. 다른 트리로 빌드한 `.ko`는 §10의 vermagic 검사에서 튕긴다.

빌드 트리에 무엇이 있어야 하나: 전체 커널 빌드 결과까지는 필요 없고 `make modules_prepare`(설정 헤더·`scripts/mod/modpost` 같은 도구 생성)면 빌드는 된다. 다만 `Module.symvers`가 없으면 modpost가 커널 심볼의 존재·GPL 여부를 검사하지 못해 경고가 쏟아진다. 랩은 `make modules`까지 돌려서 `Module.symvers`(20,778줄)가 있다. 데스크톱 배포판에서는 이 역할을 `/lib/modules/$(uname -r)/build` 심볼릭 링크(→ `linux-headers` 패키지)가 한다.

---

## 2. `.ko` 안에는 무엇이 들어 있나

### 2.1 예제 2 — `modinfo`와 `.mod.c`

무엇을 확인하나: `modinfo`가 보여 주는 키들이 소스의 어느 매크로에서, 또는 빌드 시스템의 어느 단계에서 왔는지.

```sh
cd /var/tmp/E3/base && modinfo e3_hello.ko
strings -a e3_hello.ko | grep -E "^(license|author|description|version|srcversion|depends|name|vermagic)="
head -30 e3_hello.mod.c
```

```text
filename:       /var/tmp/E3/base/e3_hello.ko
version:        1.0
description:    E3 minimal hello module
author:         Don <don@example.com>
license:        GPL
srcversion:     FF3FE46F7386678160F9951
depends:        
name:           e3_hello
vermagic:       6.18.55 SMP preempt mod_unload aarch64
version=1.0
description=E3 minimal hello module
author=Don <don@example.com>
license=GPL
srcversion=FF3FE46F7386678160F9951
depends=
name=e3_hello
vermagic=6.18.55 SMP preempt mod_unload aarch64
```

```c
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


MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "FF3FE46F7386678160F9951");
```

출력에서 볼 것: `modinfo`는 마법이 아니라 `.modinfo` 섹션의 `key=value\0` 문자열들을 읽어서 보여 줄 뿐이다(`strings`로도 그대로 보인다). 각 키의 출처:

| 키 | 누가 넣나 | 쓰임 |
|---|---|---|
| `license`, `author`, `description`, `version` | 내 소스의 `MODULE_*` 매크로 | license는 커널이 GPL 심볼 허용·taint 판단에 쓴다 |
| `parm` | `MODULE_PARM_DESC` + `module_param`의 타입 | 사람용 문서 (§5) |
| `alias` | `MODULE_DEVICE_TABLE` | modprobe 자동 로딩 (D3 §9) |
| `name` | `.mod.c`의 `MODULE_INFO(name, KBUILD_MODNAME)` | 모듈 이름 = 파일 이름에서 `-`를 `_`로 |
| `depends` | modpost가 "다른 **모듈**에서 가져다 쓰는 심볼"을 보고 계산 | depmod가 의존성 색인에 씀 (§11) |
| `srcversion` | modpost가 소스 파일들의 해시로 계산 (`MODULE_VERSION`이 있거나 `CONFIG_MODULE_SRCVERSION_ALL`일 때) | "같은 버전 문자열인데 소스가 다른" 빌드를 구분 |
| `vermagic` | `scripts/module-common.c`의 `MODULE_INFO(vermagic, VERMAGIC_STRING)` → `.module-common.o` | **로드 때 커널과 비교** (§10) |
| `intree` | 커널 트리 안에서 빌드될 때만 `Y` | 없으면 taint `O` (§9) |

`.mod.c`가 핵심이다. modpost가 만들어 주는 이 C 파일이 `struct module __this_module`을 `.gnu.linkonce.this_module` 섹션에 정의하고, 그 `.init`/`.exit` 필드에 `init_module`/`cleanup_module`(= `module_init`/`module_exit`가 만든 별칭)을 꽂는다. 커널 로더는 이 섹션을 찾아 `struct module`로 쓴다. 즉 **`THIS_MODULE`은 이 `__this_module`의 주소**다.

vermagic의 `6.18.55 SMP preempt mod_unload aarch64`는 커널 버전 + 핵심 설정(SMP, 선점 모델, 언로드 지원, 아키텍처)이다. 이 값들이 다르면 `struct module` 배치나 락 구현이 달라져서 모듈이 커널 자료구조를 잘못 읽게 된다.

### 2.2 예제 3 — `__init`이 정말 다른 섹션에 들어가나

무엇을 확인하나: `__init`/`__exit`가 붙은 함수가 어느 섹션에 있고, `init_module`이 정말 같은 함수의 별칭인지.

```sh
readelf -sW e3_hello.ko | grep -E "e3_hello|__this_module|init_module|cleanup_module"
readelf -SW e3_hello.ko | grep -E "init.text|exit.text|modinfo|this_module|\.text "
```

```text
    29: 0000000000000000     0 FILE    LOCAL  DEFAULT  ABS e3_hello.c
    32: 0000000000000000    56 FUNC    LOCAL  DEFAULT   10 e3_hello_init
    34: 0000000000000000    44 FUNC    LOCAL  DEFAULT   12 e3_hello_exit
    36: 0000000000000000     8 OBJECT  LOCAL  DEFAULT   14 __UNIQUE_ID___addressable_cleanup_module414
    38: 0000000000000000     8 OBJECT  LOCAL  DEFAULT   16 __UNIQUE_ID___addressable_init_module413
    44: 0000000000000000     0 FILE    LOCAL  DEFAULT  ABS e3_hello.mod.c
    54: 0000000000000000  1152 OBJECT  GLOBAL DEFAULT   35 __this_module
    55: 0000000000000000    44 FUNC    GLOBAL DEFAULT   12 cleanup_module
    56: 0000000000000000    56 FUNC    GLOBAL DEFAULT   10 init_module
  [ 4] .init.text.ftrace_trampoline PROGBITS        0000000000000000 000043 000001 00   A  0   0  1
  [ 6] .text             PROGBITS        0000000000000000 000044 000000 00  AX  0   0  1
  [10] .init.text        PROGBITS        0000000000000000 0000a0 000038 00  AX  0   0  4
  [11] .rela.init.text   RELA            0000000000000000 005800 0000a8 18   I 39  10  8
  [12] .exit.text        PROGBITS        0000000000000000 0000d8 00002c 00  AX  0   0  4
  [13] .rela.exit.text   RELA            0000000000000000 0058a8 000078 18   I 39  12  8
  [18] .modinfo          PROGBITS        0000000000000000 000118 0000c3 00   A  0   0  1
  [35] .gnu.linkonce.this_module PROGBITS        0000000000000000 004c48 000480 00  WA  0   0 64
  [36] .rela.gnu.linkonce.this_module RELA            0000000000000000 008a28 000030 18   I 39  35  8
```

출력에서 볼 것:

- `e3_hello_init`(56바이트)은 섹션 10 = `.init.text`, `e3_hello_exit`(44바이트)은 섹션 12 = `.exit.text`. 일반 `.text`는 크기 0이다(이 모듈엔 다른 함수가 없다).
- `init_module`과 `e3_hello_init`이 **같은 섹션·같은 크기**다 — `module_init()` 매크로가 만든 별칭이다. 6.18 `include/linux/module.h`(모듈 빌드 쪽 정의)는 이렇다:

```c
#define module_init(initfn)					\
	static inline initcall_t __maybe_unused __inittest(void)		\
	{ return initfn; }					\
	int init_module(void) __copy(initfn)			\
		__attribute__((alias(#initfn)));		\
	___ADDRESSABLE(init_module, __initdata);
```

`__inittest`는 "initfn이 `int (*)(void)` 타입인가"를 컴파일 때 검사하는 장치이고, `___ADDRESSABLE`이 위 심볼 표의 `__UNIQUE_ID___addressable_init_module413`을 만든다. 모듈 빌드에서는 `late_initcall(fn)`·`subsys_initcall(fn)`도 전부 `module_init(fn)`으로 바뀐다(같은 헤더) — 모듈에는 initcall 레벨이 없다.

- 모든 주소가 0이다. 아직 링크(재배치)가 안 끝난 relocatable ELF라서다. `.rela.init.text`가 "이 위치에 `_printk` 주소를 채워 넣어라" 같은 재배치 목록이다. 이 빈칸을 채우는 게 커널 로더의 일이다(§13).
- `.gnu.linkonce.this_module`이 0x480 = 1152바이트 = `sizeof(struct module)`이다.

### 2.3 `.ko` 크기 — 파일 38 KB 중 실제 메모리에 올라가는 것은?

`readelf -SW` 섹션 크기를 Python으로 분류하고(`.debug_*`와 그 `.rela`를 묶음), `strip --strip-debug` 결과와 게스트의 `/sys/module/e3_hello/coresize`를 같이 놓았다.

```sh
ls -l e3_hello.ko                                       # 38520
cp e3_hello.ko /var/tmp/E3/e3_hello_s.ko && strip --strip-debug /var/tmp/E3/e3_hello_s.ko
ls -l /var/tmp/E3/e3_hello_s.ko                         # 5432
readelf -SW e3_hello.ko | python3 -I -c "$CLASSIFY"     # 섹션 이름으로 범주를 나눠 합산 (.debug_* 와 .rela.debug_* → debug, 나머지 .rela.* → rela, .text/.init.text/.exit.text/.plt → code …)
```

```text
-rw-rw-r-- 1 donh donh 38520 Oct 10 13:39 e3_hello.ko
-rw-rw-r-- 1 donh donh 5432 Oct 10 13:41 /var/tmp/E3/e3_hello_s.ko
{'other': 370, 'code': 104, 'rela': 384, 'modinfo': 195, 'debug(+rela)': 31364, 'this_module': 1152, 'symtab/strtab': 2174} 35743
```

```svg
<svg viewBox="0 0 640 380" xmlns="http://www.w3.org/2000/svg">
  <text x="20" y="22" font-size="13" font-weight="bold">e3_hello.ko 파일 구성 (바이트, 합계 38,520)</text>
  <line x1="225" y1="34" x2="225" y2="232" stroke="currentColor"/>
  <text x="215" y="54" text-anchor="end" font-size="12">디버그 정보 .debug_*</text>
  <rect x="225" y="42" width="360" height="16" fill="#4a7bd0"/>
  <text x="580" y="54" text-anchor="end" font-size="12" fill="#fff">31,364</text>
  <text x="215" y="78" text-anchor="end" font-size="12">ELF·섹션 헤더, 정렬</text>
  <rect x="225" y="66" width="31.9" height="16" fill="#888"/>
  <text x="262" y="78" font-size="12">2,777</text>
  <text x="215" y="102" text-anchor="end" font-size="12">.symtab · .strtab</text>
  <rect x="225" y="90" width="25" height="16" fill="#888"/>
  <text x="256" y="102" font-size="12">2,174</text>
  <text x="215" y="126" text-anchor="end" font-size="12">.gnu.linkonce.this_module</text>
  <rect x="225" y="114" width="13.2" height="16" fill="#e08a3c"/>
  <text x="244" y="126" font-size="12">1,152</text>
  <text x="215" y="150" text-anchor="end" font-size="12">재배치 .rela.*</text>
  <rect x="225" y="138" width="4.4" height="16" fill="#888"/>
  <text x="236" y="150" font-size="12">384</text>
  <text x="215" y="174" text-anchor="end" font-size="12">기타 (.rodata, .note)</text>
  <rect x="225" y="162" width="4.3" height="16" fill="#888"/>
  <text x="236" y="174" font-size="12">370</text>
  <text x="215" y="198" text-anchor="end" font-size="12">.modinfo</text>
  <rect x="225" y="186" width="2.2" height="16" fill="#e08a3c"/>
  <text x="234" y="198" font-size="12">195</text>
  <text x="215" y="222" text-anchor="end" font-size="12">코드 (.init/.exit.text)</text>
  <rect x="225" y="210" width="1.2" height="16" fill="#3f9a6b"/>
  <text x="232" y="222" font-size="12">104</text>
  <text x="20" y="268" font-size="13" font-weight="bold">같은 모듈의 세 가지 크기</text>
  <line x1="225" y1="278" x2="225" y2="362" stroke="currentColor"/>
  <text x="215" y="296" text-anchor="end" font-size="12">.ko 파일</text>
  <rect x="225" y="284" width="360" height="16" fill="#4a7bd0"/>
  <text x="580" y="296" text-anchor="end" font-size="12" fill="#fff">38,520</text>
  <text x="215" y="322" text-anchor="end" font-size="12">strip --strip-debug 후</text>
  <rect x="225" y="310" width="50.8" height="16" fill="#3f9a6b"/>
  <text x="282" y="322" font-size="12">5,432</text>
  <text x="215" y="348" text-anchor="end" font-size="12">메모리 coresize (게스트)</text>
  <rect x="225" y="336" width="114.8" height="16" fill="#e08a3c"/>
  <text x="346" y="348" font-size="12">12,288 = 4 KB 페이지 3장</text>
</svg>
```
그림 3 — `e3_hello.ko`의 81%는 디버그 정보다. 실제 코드는 104바이트. 메모리에는 디버그 정보가 안 올라가지만, `CONFIG_STRICT_MODULE_RWX=y`라서 코드(RX)·읽기 전용(RO)·데이터(RW)를 **페이지 단위로 따로** 잡아 최소 3페이지 = 12,288바이트가 된다(`coresize`는 §3 예제 4의 실제 값).

실무 연결: 제품 rootfs에 모듈을 넣을 때는 `make INSTALL_MOD_STRIP=1 modules_install`로 디버그 정보를 빼서 넣는다(Yocto·Buildroot가 기본으로 한다). 크래시 분석용으로는 strip 안 한 `.ko`를 따로 보관한다 — 펌웨어에서 `.elf`는 보관하고 `.bin`만 굽는 것과 같다.

---

## 3. 넣고, 보고, 빼기 — `insmod` · `lsmod` · `rmmod`

### 3.1 예제 4 — QEMU 게스트에서 hello 모듈의 한 생애

무엇을 확인하나: 로드 후 `/sys/module/<이름>/`에 생기는 것들, `/proc/kallsyms`에서 `__init` 함수가 사라지는 것, `lsmod` = `/proc/modules`, 언로드 후 흔적.

```sh
# guest t1.sh
cd /e3
echo "== insmod"; insmod e3_hello.ko; echo "insmod rc=$?"
echo "== /sys/module/e3_hello"; ls /sys/module/e3_hello
for f in initstate refcnt taint version srcversion coresize initsize; do echo "$f: $(cat /sys/module/e3_hello/$f)"; done
echo "== sections"; ls -a /sys/module/e3_hello/sections
echo "== kallsyms"; grep e3_hello /proc/kallsyms
echo "== lsmod"; lsmod
echo "== /proc/modules"; cat /proc/modules
echo "== tainted: $(cat /proc/sys/kernel/tainted)"
echo "== rmmod"; rmmod e3_hello; echo "rmmod rc=$?"; ls /sys/module/e3_hello 2>&1
dmesg | grep e3_hello
```

```text
lab initramfs: 6.18.55 up 3.94s
== insmod
insmod rc=0
== /sys/module/e3_hello
coresize    initsize    notes       sections    taint       version
holders     initstate   refcnt      srcversion  uevent
initstate: live
refcnt: 0
taint: O
version: 1.0
srcversion: FF3FE46F7386678160F9951
coresize: 12288
initsize: 0
== sections
.                             .note.Linux
..                            .note.gnu.build-id
.exit.data                    .note.gnu.property
.exit.text                    .plt
.gnu.linkonce.this_module     .rodata.str1.8
.init.data                    .strtab
.init.plt                     .symtab
.init.text                    .text.ftrace_trampoline
.init.text.ftrace_trampoline
== kallsyms
ffffbd1160755000 r $d	[e3_hello]
ffffbd1160751024 t $x	[e3_hello]
ffffbd1160751024 t e3_hello_exit	[e3_hello]
ffffbd1160753000 d $d	[e3_hello]
ffffbd1160753000 d __UNIQUE_ID___addressable_cleanup_module414	[e3_hello]
ffffbd1160755058 r $d	[e3_hello]
ffffbd1160753040 d $d	[e3_hello]
ffffbd116075509c r $d	[e3_hello]
ffffbd116075509c r _note_19	[e3_hello]
ffffbd11607550b4 r _note_18	[e3_hello]
ffffbd1160753040 d __this_module	[e3_hello]
ffffbd1160751024 t cleanup_module	[e3_hello]
== lsmod
e3_hello 12288 0 - Live 0xffffbd1160751000 (O)
== /proc/modules
e3_hello 12288 0 - Live 0xffffbd1160751000 (O)
== tainted: 4096
== rmmod
rmmod rc=0
ls: /sys/module/e3_hello: No such file or directory
[    4.322404] e3_hello: loading out-of-tree module taints kernel.
[    4.345492] e3_hello: hello from init, state=1, fn=e3_hello_init [e3_hello]
[   13.106091] e3_hello: bye from exit, state=2
```

(`ls`의 색 코드는 지웠다. 이후 스크립트는 `ls --color=never`를 쓴다.)

출력에서 볼 것:

- **init 중 state=1, exit 중 state=2**. `enum module_state`는 `LIVE=0, COMING=1, GOING=2, UNFORMED=3`이다(`include/linux/module.h`). init이 도는 동안은 아직 LIVE가 아니다.
- **`initsize: 0`, kallsyms에 `e3_hello_init`이 없다**. `do_init_module()`이 init 성공 직후 `.init.*` 영역을 해제하고 kallsyms를 "core 심볼만" 쓰도록 바꾼다. `sections/` 디렉터리에는 `.init.text` 항목이 남아 있지만 그 메모리는 이미 반납됐다. 그래서 **`__init` 함수를 init 이후에 부르면(예: 콜백으로 등록해 두면) 해제된 메모리로 점프**한다. 커널은 빌드 때 modpost의 section mismatch 검사로 이것을 잡아 준다.
- `taint: O`, `tainted: 4096` = 비트 12 = `O`(out-of-tree). 첫 out-of-tree 모듈이 올라오는 순간 `loading out-of-tree module taints kernel.`이 한 번 찍힌다. §9에서 해독한다.
- `lsmod`의 BusyBox 출력은 `/proc/modules`를 그대로 보여 준다. 칸의 뜻: 이름, 크기(coresize), **refcount**, 사용하는 모듈 목록(`-` = 없음), 상태(`Live`/`Loading`/`Unloading`), 기준 주소, taint 플래그. 데스크톱 kmod `lsmod`는 같은 파일을 `Module Size Used by` 3칸으로 예쁘게 보여 줄 뿐이다.
- `rmmod` 후 `/sys/module/e3_hello`가 사라졌다. 그런데 **taint는 남는다**. 한 번 오염된 커널은 재부팅 전까지 계속 `O`다.

### 3.2 모듈의 생애 — 상태 전이

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="40" width="130" height="50" rx="8" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="85" y="61" text-anchor="middle" font-size="13" font-weight="bold">.ko 파일</text>
  <text x="85" y="80" text-anchor="middle" font-size="12">디스크의 ELF</text>
  <rect x="270" y="40" width="130" height="50" rx="8" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="335" y="61" text-anchor="middle" font-size="13" font-weight="bold">UNFORMED</text>
  <text x="335" y="80" text-anchor="middle" font-size="12">복사·배치·재배치</text>
  <rect x="530" y="40" width="130" height="50" rx="8" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="595" y="61" text-anchor="middle" font-size="13" font-weight="bold">COMING</text>
  <text x="595" y="80" text-anchor="middle" font-size="12">init() 실행 중</text>
  <rect x="530" y="170" width="130" height="50" rx="8" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="595" y="191" text-anchor="middle" font-size="13" font-weight="bold">LIVE</text>
  <text x="595" y="210" text-anchor="middle" font-size="12">정상 동작</text>
  <rect x="270" y="170" width="130" height="50" rx="8" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="335" y="191" text-anchor="middle" font-size="13" font-weight="bold">GOING</text>
  <text x="335" y="210" text-anchor="middle" font-size="12">exit() 실행 중</text>
  <rect x="20" y="170" width="130" height="50" rx="8" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="85" y="191" text-anchor="middle" font-size="13" font-weight="bold">해제</text>
  <text x="85" y="210" text-anchor="middle" font-size="12">free_module()</text>
  <line x1="150" y1="65" x2="268" y2="65" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="209" y="57" text-anchor="middle" font-size="12">load_module</text>
  <line x1="400" y1="65" x2="528" y2="65" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="465" y="57" text-anchor="middle" font-size="12">complete_formation</text>
  <line x1="595" y1="90" x2="595" y2="168" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="588" y="134" text-anchor="end" font-size="12">init() == 0</text>
  <line x1="530" y1="195" x2="402" y2="195" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="465" y="187" text-anchor="middle" font-size="12">rmmod</text>
  <text x="465" y="212" text-anchor="middle" font-size="12">refcnt 0</text>
  <line x1="530" y1="90" x2="402" y2="170" stroke="#d0564a" marker-end="url(#ah3)"/>
  <text x="420" y="122" text-anchor="end" font-size="12" font-weight="bold">init() 실패</text>
  <line x1="270" y1="90" x2="152" y2="170" stroke="#d0564a" marker-end="url(#ah3)"/>
  <text x="200" y="122" text-anchor="end" font-size="12" font-weight="bold">심볼·재배치 실패</text>
  <line x1="270" y1="195" x2="152" y2="195" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="211" y="187" text-anchor="middle" font-size="12">exit 후</text>
  <text x="20" y="250" font-size="12">vermagic·서명 검사는 UNFORMED 전에 끝난다 (실패하면 그냥 거부).</text>
  <text x="20" y="272" font-size="12">/proc/modules 상태 칸: Loading = COMING, Live = LIVE, Unloading = GOING</text>
</svg>
```
그림 4 — `struct module.state`의 전이. 빨간 화살표는 실패 경로다. init이 실패하면 `do_init_module()`의 `fail:` 라벨이 상태를 GOING으로 바꾸고 바로 `free_module()`한다 — **exit 함수는 불리지 않는다.** 그래서 init 안에서 스스로 정리해야 한다(§6).

### 3.3 `insmod`가 실제로 부르는 syscall

| 도구 | syscall | 특징 |
|---|---|---|
| BusyBox(이 랩, `MODPROBE_SMALL=y`) | `init_module(buf, len, args)` | 파일을 유저 메모리로 읽어 통째로 넘긴다. §7 예제 9의 dyndbg 로그에 `init_module: umod=..., len=...`이 찍힌다 |
| kmod(데스크톱·Yocto의 `insmod`/`modprobe`) | `finit_module(fd, args, flags)` 먼저 | 커널이 fd에서 직접 읽는다. 어느 파일에서 왔는지 알 수 있어 LoadPin·IMA 같은 보안 정책이 파일 출처를 검사할 수 있다 |
| 언로드 (둘 다) | `delete_module(name, O_NONBLOCK)` | 사용 중이면 기다리지 않고 바로 실패 |

두 syscall 모두 `CAP_SYS_MODULE` 권한이 필요하고(`may_init_module()`), `/proc/sys/kernel/modules_disabled`를 1로 쓰면 그 뒤로는 아무도 모듈을 못 넣는다(보안 강화 장비에서 부팅 끝에 쓰는 기법).

---

## 4. 여러 파일로 된 모듈, 그리고 뺄 수 없는 모듈

### 4.1 예제 5 — `e3_multi` = `e3_main.c` + `e3_util.c`, exit 없음

무엇을 확인하나: 소스 두 개를 한 `.ko`로 묶는 Kbuild 문법(`<모듈>-y`), 그리고 `module_exit`이 없는 모듈은 영영 못 뺀다는 것.

```c
// SPDX-License-Identifier: GPL-2.0
/* e3_util.c — 같은 모듈 안의 두 번째 .c: static이 아니면 같은 .ko 안에서는 그냥 링크된다.
 * e3_util.h에는 선언 한 줄: int e3_util_sum(const int *v, int n); */
#include "e3_util.h"

int e3_util_sum(const int *v, int n)
{
	int i, s = 0;

	for (i = 0; i < n; i++)
		s += v[i];
	return s;
}
```

```c
// SPDX-License-Identifier: GPL-2.0
/* e3_main.c — e3_multi = e3_main.o + e3_util.o. 일부러 module_exit이 없다 → rmmod 불가 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include "e3_util.h"

static int __init e3_multi_init(void)
{
	static const int v[] = { 1, 2, 3, 4 };

	pr_info("sum=%d (e3_util_sum from another .c)\n",
		e3_util_sum(v, ARRAY_SIZE(v)));
	return 0;
}
module_init(e3_multi_init);
/* module_exit 없음! */
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("E3 multi-file module without exit");
```

`base/` 디렉터리의 Makefile은 모듈 다섯 개를 한 번에 빌드한다:

```make
# out-of-tree 모듈: 커널 빌드 트리의 Kbuild가 이 파일을 다시 읽는다
obj-m += e3_hello.o e3_param.o e3_fail.o e3_hold.o
obj-m += e3_multi.o
e3_multi-y := e3_main.o e3_util.o

KDIR ?= /var/lab/out/linux-build

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules
clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
```

```sh
cd /var/tmp/E3/base && make
cat e3_multi.mod; nm e3_multi.ko | grep -E "e3_|init_module|cleanup"
```

```text
...
  CC [M]  e3_main.o
  CC [M]  e3_util.o
  LD [M]  e3_multi.o
  MODPOST Module.symvers
...
  LD [M]  e3_multi.ko
./e3_main.o
./e3_util.o
0000000000000000 d __UNIQUE_ID___addressable_init_module413
0000000000000000 t e3_multi_init
0000000000000000 T e3_util_sum
0000000000000000 T init_module
```

(빌드 로그는 `e3_multi` 관련 줄만 남겼다(`...`). 다섯 모듈 모두 경고 0개.)

게스트에서(t3.sh 앞부분):

```sh
insmod e3_multi.ko; lsmod; rmmod e3_multi; echo "rc=$?"
dmesg | grep e3_multi
```

```text
e3_multi 12288 0 [permanent], Live 0xffffc1924c8a3000 (O)
rmmod: remove 'e3_multi': Device or resource busy
rc=0
[    6.271831] e3_multi: loading out-of-tree module taints kernel.
[    6.302522] e3_multi: sum=10 (e3_util_sum from another .c)
```

출력에서 볼 것:

- **모듈 이름과 소스 이름이 같으면 안 된다.** `e3_multi-y := e3_main.o e3_util.o`에서 `e3_multi.c`라는 파일이 있으면 Kbuild가 헷갈린다. 그래서 `e3_main.c`로 이름을 달리했다. `cleanup_module`이 `nm`에 없다 — exit을 안 만들었으니까.
- `e3_util_sum`은 `T`(전역)지만 **다른 모듈은 이걸 못 쓴다.** `EXPORT_SYMBOL`을 안 했기 때문이다. 같은 `.ko` 안의 링크는 일반 링커(`LD [M] e3_multi.o`)가 끝낸다. 모듈 경계를 넘는 링크만 커널 심볼 표를 거친다(§7).
- `[permanent]`: init은 있는데 exit이 없는 모듈. `delete_module()`의 `if (mod->init && !mod->exit)` 검사가 `-EBUSY`를 돌려준다. 실무에서는 "한 번 올리면 끝"인 모듈이 아니라면 실수다. 개발 중에 이걸 만들면 재부팅해야 새 버전을 올릴 수 있다.
- **BusyBox `rmmod`는 실패해도 종료 코드가 0이었다**(이 빌드의 modprobe-small 구현). 스크립트에서 `rmmod x && echo ok`로 성공을 판정하면 속는다. `grep -q '^x ' /proc/modules`로 다시 확인하자. kmod `rmmod`는 실패하면 1을 돌려준다.

---

## 5. 모듈 파라미터 — 로드할 때 값을 넘기기

### 5.1 직관

펌웨어에서 "보드마다 다른 값"은 `#define`이나 EEPROM 설정 블록으로 넘긴다. 모듈은 **로드할 때 `이름=값`** 으로 넘긴다. `insmod e3_param.ko count=5`. 그리고 권한을 주면 **실행 중에 `/sys/module/<m>/parameters/<p>` 파일로 읽고 쓸 수 있다.** 디버그 레벨, 폴링 주기, 기능 on/off 같은 값에 쓴다.

### 5.2 예제 6 — 타입·권한·배열·콜백 파라미터

무엇을 확인하나: `module_param`의 타입(int, bool, charp, uint), 배열 파라미터, 권한 비트(0644/0444/0), 범위 검사를 하는 콜백 파라미터(`module_param_cb`), `MODULE_PARM_DESC`가 `modinfo`에 보이는 모습.

```c
// SPDX-License-Identifier: GPL-2.0
/* e3_param: module_param 종류·권한·콜백 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>

static int count = 1;
module_param(count, int, 0644);			/* root rw */
MODULE_PARM_DESC(count, "samples per read (int, rw)");
static bool verbose;
module_param(verbose, bool, 0444);		/* 읽기 전용 */
MODULE_PARM_DESC(verbose, "extra logs (bool, ro)");
static char *name = "imu0";
module_param(name, charp, 0444);
MODULE_PARM_DESC(name, "sensor name (charp)");
static int addrs[4];
static int n_addrs;
module_param_array(addrs, int, &n_addrs, 0444);
MODULE_PARM_DESC(addrs, "up to 4 I2C addresses");
static unsigned int secret = 42;
module_param(secret, uint, 0);			/* sysfs에 안 보임 */

static int rate_hz = 100;
static int rate_set(const char *val, const struct kernel_param *kp)
{
	int v, ret = kstrtoint(val, 0, &v);

	if (ret)
		return ret;
	if (v < 1 || v > 1000)
		return -EINVAL;			/* 범위 검사 */
	pr_info("rate_hz %d -> %d\n", *(int *)kp->arg, v);
	*(int *)kp->arg = v;
	return 0;
}
static const struct kernel_param_ops rate_ops = {
	.set = rate_set,
	.get = param_get_int,
};
module_param_cb(rate_hz, &rate_ops, &rate_hz, 0644);
MODULE_PARM_DESC(rate_hz, "ODR in Hz, 1..1000 (validated)");

static int __init e3_param_init(void)
{
	pr_info("count=%d verbose=%d name=%s n_addrs=%d addrs[0]=0x%x secret=%u rate_hz=%d\n",
		count, verbose, name, n_addrs, addrs[0], secret, rate_hz);
	return 0;
}
static void __exit e3_param_exit(void)
{
	pr_info("exit: count=%d rate_hz=%d\n", count, rate_hz);
}
module_init(e3_param_init);
module_exit(e3_param_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("E3 module parameters");
```

먼저 VM에서 `modinfo`:

```text
filename:       /var/tmp/E3/base/e3_param.ko
description:    E3 module parameters
license:        GPL
depends:        
name:           e3_param
vermagic:       6.18.55 SMP preempt mod_unload aarch64
parm:           count:samples per read (int, rw) (int)
parm:           verbose:extra logs (bool, ro) (bool)
parm:           name:sensor name (charp) (charp)
parm:           addrs:up to 4 I2C addresses (array of int)
parm:           secret:uint
parm:           rate_hz:ODR in Hz, 1..1000 (validated)
```

게스트 스크립트(t3.sh 뒷부분):

```sh
insmod e3_param.ko count=5 name=bmi088 addrs=0x68,0x69 verbose=1 secret=7
P=/sys/module/e3_param/parameters; ls -l --color=never $P
for f in $P/*; do echo "${f##*/} = $(cat $f)"; done
echo 10 > $P/count; echo "count -> $(cat $P/count)"
echo 200 > $P/rate_hz; echo "rate_hz -> $(cat $P/rate_hz)"
echo 5000 > $P/rate_hz; echo "write 5000 rc=$?, rate_hz=$(cat $P/rate_hz)"
echo N > $P/verbose; echo "write verbose rc=$?"
rmmod e3_param
for a in bogus=1 rate_hz=5000 count=abc; do echo "-- insmod e3_param.ko $a"; insmod e3_param.ko $a; echo "rc=$?"; grep -q e3_param /proc/modules && rmmod e3_param; done
dmesg | grep -E "e3_param"
```

```text
total 0
-r--r--r--    1 0        0             4096 Oct 10 20:44 addrs
-rw-r--r--    1 0        0             4096 Oct 10 20:44 count
-r--r--r--    1 0        0             4096 Oct 10 20:44 name
-rw-r--r--    1 0        0             4096 Oct 10 20:44 rate_hz
-r--r--r--    1 0        0             4096 Oct 10 20:44 verbose
addrs = 104,105
count = 5
name = bmi088
rate_hz = 100
verbose = Y
count -> 10
rate_hz -> 200
sh: write error: Invalid argument
write 5000 rc=1, rate_hz=200
/e3/t3.sh: line 9: can't create /sys/module/e3_param/parameters/verbose: Permission denied
write verbose rc=1
-- insmod e3_param.ko bogus=1
rc=0
-- insmod e3_param.ko rate_hz=5000
[    7.645653] e3_param: `5000' invalid for parameter `rate_hz'
insmod: can't insert 'e3_param.ko': invalid parameter
rc=1
-- insmod e3_param.ko count=abc
[    7.742125] e3_param: `abc' invalid for parameter `count'
insmod: can't insert 'e3_param.ko': invalid parameter
rc=1
[    6.602616] e3_param: count=5 verbose=1 name=bmi088 n_addrs=2 addrs[0]=0x68 secret=7 rate_hz=100
[    7.198082] e3_param: rate_hz 100 -> 200
[    7.358366] e3_param: exit: count=10 rate_hz=200
[    7.428741] e3_param: unknown parameter 'bogus' ignored
[    7.433074] e3_param: count=1 verbose=0 name=imu0 n_addrs=0 addrs[0]=0x0 secret=42 rate_hz=100
[    7.566557] e3_param: exit: count=1 rate_hz=100
[    7.645653] e3_param: `5000' invalid for parameter `rate_hz'
[    7.742125] e3_param: `abc' invalid for parameter `count'
```

출력에서 볼 것:

- **권한 비트가 그대로 sysfs 파일 권한**이 된다. `0644` → `-rw-r--r--`, `0444` → 읽기 전용, `0`인 `secret`은 **파일이 아예 없다**(로드 때만 줄 수 있는 값). `0444` 파일에는 root도 못 쓴다(`Permission denied`) — sysfs는 쓰기 비트가 없는 속성의 쓰기 open을 root에게도 거절한다.
- `count`를 10으로 바꾸자 exit에서 `count=10`이 찍혔다. sysfs 쓰기는 **모듈의 전역 변수를 직접 바꾼다.** 드라이버 코드는 이 값이 언제든 바뀔 수 있다고 가정해야 한다(읽을 때 `READ_ONCE`, 여러 필드를 같이 바꿔야 하면 콜백에서 락).
- `rate_hz`는 콜백이라 **범위 밖 값(5000)을 `-EINVAL`로 거절**했고 값은 200 그대로다. 같은 콜백이 로드 때도 불려서 `insmod rate_hz=5000`이 로드 자체를 실패시켰다. 타입이 안 맞는 `count=abc`도 같다.
- **모르는 파라미터(`bogus=1`)는 경고만 하고 로드는 된다**(`unknown parameter 'bogus' ignored`). 오타 낸 파라미터가 조용히 무시되는 셈이다. dmesg를 꼭 본다.
- 배열 `addrs`는 `0x68,0x69`를 받아 `n_addrs=2`가 됐고 sysfs에는 **10진수 `104,105`** 로 보인다. 16진수로 넣었다고 16진수로 나오지 않는다.
- `modinfo`의 `parm:` 줄: `module_param`은 타입을 `(int)`처럼 붙여 주지만 `module_param_cb`는 타입 정보가 없어 설명만 나온다. `secret`처럼 `MODULE_PARM_DESC`가 없으면 타입만 나온다.

| 매크로 | 쓰임 |
|---|---|
| `module_param(var, type, perm)` | 기본. type: `bool` `invbool` `int` `uint` `long` `ulong` `short` `ushort` `byte` `charp` `hexint` |
| `module_param_named(name, var, type, perm)` | 외부 이름과 C 변수 이름을 다르게 |
| `module_param_array(arr, type, &n, perm)` | 콤마로 구분된 배열, 받은 개수가 `n`에 |
| `module_param_string(name, buf, len, perm)` | 고정 크기 버퍼로 문자열 복사 (`charp`는 포인터만 저장) |
| `module_param_cb(name, &ops, &var, perm)` | `.set`/`.get` 콜백으로 검증·부수효과 |
| `MODULE_PARM_DESC(name, "설명")` | `modinfo`용 문서 |

> 함정: 권한에 `0666`이나 `0777`처럼 **others 쓰기 비트**를 주면 컴파일이 안 된다(`include/linux/moduleparam.h`의 `VERIFY_OCTAL_PERMISSIONS`가 빌드 타임에 막는다). 일반 사용자가 커널 변수를 바꾸게 둘 수는 없다.

### 5.3 커널 명령줄로 넘기기 — built-in일 때

같은 드라이버를 `=y`(built-in)로 빌드하면 `insmod`할 일이 없다. 그때는 **커널 명령줄에 `<모듈이름>.<파라미터>=값`** 을 쓴다. 예: `printk.time=0`, `usbcore.autosuspend=-1`. `.ko`로 빌드된 모듈의 명령줄 파라미터는 커널이 무시하고, **modprobe가** `/proc/cmdline`을 읽어 적용한다(kmod 기준). §12에서 랩으로 확인한다.

---

## 6. init이 실패하면 — `goto` 사다리로 거꾸로 정리

### 6.1 직관

펌웨어 초기화에서 "클럭 켜고 → DMA 채널 잡고 → 인터럽트 등록"하다가 3단계에서 실패하면, 1·2단계에서 잡은 것을 **역순으로** 놓는다. 커널도 같다. 그리고 그림 4에서 봤듯 **init이 실패하면 exit은 불리지 않는다.** 정리는 init이 직접 해야 한다. 커널은 이걸 `goto` 라벨 사다리로 쓴다(E5에서 다시 나온다).

### 6.2 예제 7 — `e3_fail`: 어느 단계에서 실패해도 새는 것이 없다

무엇을 확인하나: 1단계(메모리), 2단계(misc 장치 등록), 3단계(가짜 HW probe)에서 각각 실패시켰을 때 정리 순서, 그리고 init이 돌려준 음수 errno가 `insmod`의 에러 메시지가 되는 것.

```c
// SPDX-License-Identifier: GPL-2.0
/* e3_fail: init 실패 경로 — goto로 거꾸로 정리 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>

static int fail_at;			/* 0=성공, 1/2/3=그 단계에서 실패 */
module_param(fail_at, int, 0444);
static void *buf;
static const struct file_operations e3_fail_fops = { .owner = THIS_MODULE };
static struct miscdevice e3_fail_misc = {
	.minor = MISC_DYNAMIC_MINOR, .name = "e3_fail", .fops = &e3_fail_fops,
};

static int e3_fake_hw_probe(void) { return fail_at == 3 ? -ENODEV : 0; }

static int __init e3_fail_init(void)
{
	int ret;

	buf = fail_at == 1 ? NULL : kzalloc(4096, GFP_KERNEL);	/* 1단계 */
	if (!buf) {
		ret = -ENOMEM;
		goto err_buf;
	}
	ret = fail_at == 2 ? -EBUSY : misc_register(&e3_fail_misc);	/* 2단계 */
	if (ret)
		goto err_free;
	ret = e3_fake_hw_probe();				/* 3단계 */
	if (ret)
		goto err_misc;
	pr_info("init ok\n");
	return 0;

err_misc:
	misc_deregister(&e3_fail_misc);
	pr_info("undo: misc_deregister\n");
err_free:
	kfree(buf);
	pr_info("undo: kfree\n");
err_buf:
	pr_err("init failed at step %d: %d\n", fail_at, ret);
	return ret;
}
static void __exit e3_fail_exit(void)
{
	misc_deregister(&e3_fail_misc);
	kfree(buf);
	pr_info("exit\n");
}
module_init(e3_fail_init);
module_exit(e3_fail_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("E3 init failure path");
```

```sh
# guest t4.sh 일부
for n in 3 2 1; do insmod e3_fail.ko fail_at=$n; done
ls /dev/e3_fail 2>&1; lsmod
insmod e3_fail.ko; ls --color=never /dev/e3_fail; rmmod e3_fail
dmesg | grep -E "e3_fail"
```

```text
[    7.000844] e3_fail: init failed at step 3: -19
insmod: can't insert 'e3_fail.ko': No such device
[    7.088558] e3_fail: init failed at step 2: -16
insmod: can't insert 'e3_fail.ko': Device or resource busy
[    7.237352] e3_fail: init failed at step 1: -12
insmod: can't insert 'e3_fail.ko': Cannot allocate memory
ls: /dev/e3_fail: No such file or directory
/dev/e3_fail
[    7.000629] e3_fail: undo: misc_deregister
[    7.000781] e3_fail: undo: kfree
[    7.000844] e3_fail: init failed at step 3: -19
[    7.088405] e3_fail: undo: kfree
[    7.088558] e3_fail: init failed at step 2: -16
[    7.237352] e3_fail: init failed at step 1: -12
[    7.642824] e3_fail: init ok
[    7.753579] e3_fail: exit
```

(`pr_err`는 `quiet`에서도 콘솔에 나오므로 위쪽에 섞여 보인다.)

출력에서 볼 것:

- init의 반환값 `-ENODEV(-19)`, `-EBUSY(-16)`, `-ENOMEM(-12)`이 그대로 `insmod`의 `strerror` 메시지("No such device" 등)가 됐다. **음수 errno를 돌려주는 게 커널 규약**이다(E6). `-1`을 돌려주면 유저는 "Operation not permitted"(EPERM)를 보게 된다 — 엉뚱한 메시지다.
- 단계 3 실패: 등록했던 misc 장치를 먼저 해제하고 버퍼를 해제했다. 단계 2 실패: 버퍼만. 단계 1: 아무것도. **라벨은 획득의 역순으로 놓이고, 실패 지점은 "자기 바로 전 단계까지 되돌리는" 라벨로 점프한다.** 실패 후 `lsmod`가 비어 있고 `/dev/e3_fail`이 없다 — 새는 것이 없다.
- 실패한 로드도 이미 taint `O`를 남긴다. taint는 `load_module()` 초반(`module_augment_kernel_taints()`)에 찍히기 때문이다(§13).

> 함정: init이 **양수**를 돌려주면? 커널은 `do_init_module()`에서 `'%s'->init suspiciously returned %d, it should follow 0/-E convention`이라고 경고하고 성공으로 취급한다(6.18 `kernel/module/main.c`). 펌웨어 습관대로 `return 1;`(성공)을 쓰면 안 된다.

### 6.3 devm이라는 출구

드라이버(G3)에서는 `probe()` 안에서 `devm_kzalloc`, `devm_request_irq`처럼 **장치에 수명이 묶인 자원**을 쓰면 실패·제거 때 커널이 역순으로 자동 해제한다. `goto` 사다리가 짧아진다. 하지만 모듈 init 자체(드라이버 등록 전)에는 장치가 없으니 여전히 손으로 정리한다. 그리고 면접관은 `goto` 패턴을 직접 써 보라고 한다.

---

## 7. 모듈끼리 함수 부르기 — `EXPORT_SYMBOL`

### 7.1 직관

펌웨어에서는 `nand_drv.c`가 `nand_read()`를 정의하면 다른 `.c`가 그냥 부른다. 링커가 같은 이미지 안에서 이름을 찾아 준다. 커널 모듈은 **각자 따로 로드되는 별개의 링크 단위**다. 그래서 "남이 불러도 되는 함수"를 **명시적으로 공개**해야 한다. 이게 `EXPORT_SYMBOL`이다. 공개된 심볼은 커널의 **심볼 표**(`__ksymtab` 섹션)에 이름과 주소가 등록되고, 나중에 로드되는 모듈의 미해결 심볼을 로더가 이 표에서 찾는다.

| 매크로 | 누가 쓸 수 있나 |
|---|---|
| `EXPORT_SYMBOL(f)` | 모든 모듈 (라이선스 무관) |
| `EXPORT_SYMBOL_GPL(f)` | GPL 호환 라이선스 모듈만. 커널 내부 API 대부분이 이쪽이다 |
| `EXPORT_SYMBOL_NS(f, "NS")` / `EXPORT_SYMBOL_NS_GPL(f, "NS")` | `MODULE_IMPORT_NS("NS")`를 선언한 모듈만 (+ GPL 조건) |

**네임스페이스**는 심볼 이름 공간을 서브시스템별로 나눠 "아무 드라이버나 내부 헬퍼를 가져다 쓰는 것"을 막는 장치다(예: `USB_STORAGE`, `CRYPTO_INTERNAL`). 6.13부터 매크로 인자가 **문자열 리터럴**로 바뀌었다. 6.18 랩 소스(`grep -n` 결과, 경로 앞의 `/var/lab/src/linux-6.18.55/`는 생략):

```text
include/linux/export.h:91:#define EXPORT_SYMBOL_NS(sym, ns)	__EXPORT_SYMBOL(sym, "", ns)
include/linux/export.h:92:#define EXPORT_SYMBOL_NS_GPL(sym, ns)	__EXPORT_SYMBOL(sym, "GPL", ns)
include/linux/module.h:307:#define MODULE_IMPORT_NS(ns)	MODULE_INFO(import_ns, ns)
```

`MODULE_INFO(import_ns, ns)`이니 `ns`는 문자열이어야 한다. 예전 문법 `MODULE_IMPORT_NS(E3_LIB)`(따옴표 없음)로 빌드하면 6.18에서는 이렇게 깨진다:

```text
e3_user.c:23:18: error: expected ‘,’ or ‘;’ before ‘E3_LIB’
```

오래된 BSP 드라이버를 6.13+로 옮길 때 자주 만나는 컴파일 에러다.

### 7.2 예제 8 — `e3_lib`(내보내기)와 `e3_user`(가져다 쓰기) 빌드

무엇을 확인하나: 심볼 세 개를 서로 다른 방식으로 내보내고, 다른 디렉터리의 모듈이 그걸 쓰려면 빌드 때 무엇이 필요한지.

공개 API는 `lib/e3_lib.h`에 선언한다(`int e3_lib_add(int a, int b);`, `int e3_lib_plain(void);`, `int e3_lib_secret(void);` 세 줄과 include guard).

```c
// SPDX-License-Identifier: GPL-2.0
/* e3_lib: 다른 모듈에게 함수를 내보내는 "라이브러리 모듈" */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/export.h>
#include "e3_lib.h"

int e3_lib_add(int a, int b)
{
	return a + b;
}
EXPORT_SYMBOL_GPL(e3_lib_add);		/* GPL 호환 모듈만 */

int e3_lib_plain(void)
{
	return 7;
}
EXPORT_SYMBOL(e3_lib_plain);		/* 라이선스 무관 */

int e3_lib_secret(void)
{
	return 0x5eed;
}
EXPORT_SYMBOL_NS_GPL(e3_lib_secret, "E3_LIB");	/* 네임스페이스 */

static int __init e3_lib_init(void)
{
	pr_info("loaded, exporting 3 symbols\n");
	return 0;
}
static void __exit e3_lib_exit(void)
{
	pr_info("unloaded\n");
}
module_init(e3_lib_init);
module_exit(e3_lib_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("E3 symbol provider");
```

```c
// SPDX-License-Identifier: GPL-2.0
/* e3_user: e3_lib의 심볼을 부르는 모듈 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include "e3_lib.h"

static int __init e3_user_init(void)
{
	pr_info("add(2,3)=%d plain=%d secret=0x%x\n",
		e3_lib_add(2, 3), e3_lib_plain(), e3_lib_secret());
	return 0;
}
static void __exit e3_user_exit(void)
{
	pr_info("bye\n");
}
module_init(e3_user_init);
module_exit(e3_user_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("E3 symbol consumer");
#ifndef E3_NO_IMPORT
MODULE_IMPORT_NS("E3_LIB");		/* 6.13+: 문자열 리터럴 */
#endif
```

```make
# user/Makefile
obj-m := e3_user.o
ccflags-y += -I/var/tmp/E3/lib $(E3_FLAGS)
# e3_lib의 Module.symvers를 알려 줘야 modpost가 심볼을 찾는다
KBUILD_EXTRA_SYMBOLS := /var/tmp/E3/lib/Module.symvers
KDIR ?= /var/lab/out/linux-build
all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules
```

`e3_lib`를 먼저 빌드하면 그 디렉터리에 `Module.symvers`가 생긴다:

```sh
cd /var/tmp/E3/lib && make && cat Module.symvers
```

```text
  CC [M]  e3_lib.o
  MODPOST Module.symvers
  LD [M]  e3_lib.ko
0x00000000	e3_lib_add	e3_lib	EXPORT_SYMBOL_GPL	
0x00000000	e3_lib_plain	e3_lib	EXPORT_SYMBOL	
0x00000000	e3_lib_secret	e3_lib	EXPORT_SYMBOL_GPL	E3_LIB
```

칸: CRC(아래), 심볼, 소속 모듈, 내보내기 종류, 네임스페이스. 첫 칸이 0인 건 이 커널이 `CONFIG_MODVERSIONS`가 꺼져 있어서다(§10).

> 함정: 처음엔 `e3_lib.h` 없이 빌드했더니 `warning: no previous prototype for ‘e3_lib_add’ [-Wmissing-prototypes]`가 세 번 나왔다. 커널 빌드는 `-Wmissing-prototypes`를 켠다. **내보내는 함수는 반드시 헤더에 선언**하고 정의하는 쪽도 그 헤더를 include한다. 사용자 모듈 안에 `int e3_lib_add(int, int);`를 직접 적는 것은 시그니처가 바뀌어도 아무도 모르는 지름길이다.

이제 `e3_user`를 세 가지로 빌드해 본다:

```sh
cd /var/tmp/E3/user; K=/var/lab/out/linux-build
echo "=== (a) KBUILD_EXTRA_SYMBOLS 없이";  make -C $K M=$PWD KBUILD_EXTRA_SYMBOLS= modules
echo "=== (b) MODULE_IMPORT_NS 빼고";      make E3_FLAGS=-DE3_NO_IMPORT
echo "=== (c) 정상";                       make; modinfo e3_user.ko
```

```text
=== (a) KBUILD_EXTRA_SYMBOLS 없이
  CC [M]  e3_user.o
  MODPOST Module.symvers
ERROR: modpost: "e3_lib_add" [e3_user.ko] undefined!
ERROR: modpost: "e3_lib_plain" [e3_user.ko] undefined!
ERROR: modpost: "e3_lib_secret" [e3_user.ko] undefined!
make[1]: *** [/var/lab/src/linux-6.18.55/Makefile:248: __sub-make] Error 2
=== (b) MODULE_IMPORT_NS 빼고
  CC [M]  e3_user.o
  MODPOST Module.symvers
ERROR: modpost: module e3_user uses symbol e3_lib_secret from namespace E3_LIB, but does not import it.
=== (c) 정상
  CC [M]  e3_user.o
  MODPOST Module.symvers
  CC [M]  e3_user.mod.o
  CC [M]  .module-common.o
  LD [M]  e3_user.ko
filename:       /var/tmp/E3/user/e3_user.ko
import_ns:      E3_LIB
description:    E3 symbol consumer
license:        GPL
depends:        e3_lib
name:           e3_user
vermagic:       6.18.55 SMP preempt mod_unload aarch64
```

(각 빌드 사이에 `make clean`을 했고, (a)의 make 에러 줄은 마지막 한 줄만 남겼다.)

출력에서 볼 것:

- (a) modpost는 `vmlinux`의 `Module.symvers`(빌드 트리)와 **`KBUILD_EXTRA_SYMBOLS`로 알려 준 파일**만 본다. 다른 out-of-tree 모듈의 심볼은 알려 주지 않으면 "undefined"다. 이건 런타임 에러를 **빌드 타임에 미리 잡는** 장치다. 예전 커널은 이걸 경고로만 냈지만 6.x는 에러다(급하면 `KBUILD_MODPOST_WARN=1`로 경고로 낮출 수 있지만, 그러면 로드 때 터진다).
- (b) 네임스페이스 검사도 modpost가 빌드 때 한다. 런타임에도 `verify_namespace_is_imported()`가 한 번 더 본다.
- (c) `depends: e3_lib` — modpost가 "e3_user가 e3_lib 소유 심볼을 쓴다"를 `Module.symvers`의 소속 칸에서 알아내 `.mod.c`에 `MODULE_INFO(depends, "e3_lib");`를 써 넣었다. depmod·modprobe는 이 한 줄로 로드 순서를 정한다(§11).
- 대안: 두 모듈을 **같은 디렉터리의 같은 Makefile**(`obj-m := e3_lib.o e3_user.o`)로 빌드하면 modpost가 한 번에 둘을 보므로 `KBUILD_EXTRA_SYMBOLS`가 필요 없다. 벤더 드라이버 묶음이 보통 이렇게 한다.

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="20" y="22" font-size="13" font-weight="bold">빌드 때 — modpost가 검사</text>
  <rect x="20" y="34" width="200" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="120" y="53" text-anchor="middle" font-size="12">e3_lib.c</text>
  <text x="120" y="70" text-anchor="middle" font-size="12">EXPORT_SYMBOL_GPL(...)</text>
  <rect x="250" y="34" width="190" height="44" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="345" y="53" text-anchor="middle" font-size="12">lib/Module.symvers</text>
  <text x="345" y="70" text-anchor="middle" font-size="12">e3_lib_add → e3_lib, GPL</text>
  <rect x="250" y="96" width="190" height="44" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="345" y="115" text-anchor="middle" font-size="12">vmlinux Module.symvers</text>
  <text x="345" y="132" text-anchor="middle" font-size="12">커널 심볼 20,778개</text>
  <rect x="470" y="34" width="190" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="565" y="53" text-anchor="middle" font-size="12">modpost (e3_user)</text>
  <text x="565" y="70" text-anchor="middle" font-size="12">undefined·NS·GPL 검사</text>
  <rect x="430" y="140" width="230" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="545" y="159" text-anchor="middle" font-size="12">e3_user.ko (depends=e3_lib)</text>
  <text x="545" y="176" text-anchor="middle" font-size="12">import_ns=E3_LIB</text>
  <line x1="220" y1="56" x2="248" y2="56" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="440" y1="56" x2="468" y2="56" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="440" y1="118" x2="500" y2="80" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="565" y1="78" x2="565" y2="138" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="458" y="98" font-size="12">EXTRA_SYMBOLS</text>
  <line x1="20" y1="205" x2="660" y2="205" stroke="#888" stroke-dasharray="5 4"/>
  <text x="20" y="230" font-size="13" font-weight="bold">로드 때 — 커널 모듈 로더가 연결</text>
  <rect x="20" y="244" width="200" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="120" y="263" text-anchor="middle" font-size="12">insmod e3_lib.ko</text>
  <text x="120" y="280" text-anchor="middle" font-size="12">__ksymtab · _gpl 등록</text>
  <rect x="250" y="244" width="190" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="345" y="263" text-anchor="middle" font-size="12">insmod e3_user.ko</text>
  <text x="345" y="280" text-anchor="middle" font-size="12">resolve_symbol()</text>
  <rect x="460" y="244" width="200" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="560" y="263" text-anchor="middle" font-size="12">ref_module(user, lib)</text>
  <text x="560" y="280" text-anchor="middle" font-size="12">e3_lib refcnt 1, holders</text>
  <line x1="250" y1="266" x2="222" y2="266" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="236" y="306" text-anchor="middle" font-size="12">찾기</text>
  <line x1="440" y1="266" x2="458" y2="266" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="340" y="340" text-anchor="middle" font-size="12">순서가 바뀌면 resolve 실패 → "Unknown symbol e3_lib_add (err -2)"</text>
</svg>
```
그림 5 — 심볼 내보내기·가져오기. 위 칸은 **빌드 때** 정적 검사(modpost + `Module.symvers`), 아래 칸은 **로드 때** 실제 연결(커널 심볼 표 + `resolve_symbol()` + 의존 관계 기록). 둘은 따로 일한다 — 빌드가 통과해도 로드 순서가 틀리면 실패한다.

### 7.3 예제 9 — 로드 순서를 틀리면 "Unknown symbol", 그리고 로더 내부 로그

무엇을 확인하나: `e3_lib` 없이 `e3_user`를 넣었을 때의 실패, 순서대로 넣었을 때의 성공, 그리고 dynamic debug로 `kernel/module/main.c`의 `pr_debug`를 켜서 로더가 하는 일을 직접 보기.

```sh
# guest t2.sh 앞부분
echo 'file kernel/module/main.c +p' > /sys/kernel/debug/dynamic_debug/control
dmesg -c >/dev/null
echo "== 1) e3_user 먼저"; insmod e3_user.ko; echo "rc=$?"; dmesg -c
echo "== 2) e3_lib 다음 e3_user"; insmod e3_lib.ko; insmod e3_user.ko; echo "rc=$?"; dmesg -c
echo "== kallsyms"; grep -E " e3_lib_| __ksymtab_e3" /proc/kallsyms
echo "== lsmod"; lsmod
echo "lib refcnt=$(cat /sys/module/e3_lib/refcnt)"; ls /sys/module/e3_lib/holders
```

```text
== 1) e3_user 먼저
insmod: can't insert 'e3_user.ko': unknown symbol in module or invalid parameter
rc=1
[    5.569740] init_module: umod=0000000023383683, len=38328, uargs=0000000012c72290
[    5.586505] Core section allocation order for e3_user:
[    5.586814] 	.plt
[    5.586887] 	.text.ftrace_trampoline
...
[    5.590490] Init section allocation order for e3_user:
...
[    5.594966] Final section addresses for e3_user:
[    5.596279] 	0xffffd8861ff82000 0x00000018 .plt
...
[    5.596558] 	0xffffd8861ff88048 0x0000004c .init.text
[    5.596571] 	0xffffd8861ff82024 0x00000024 .exit.text
...
[    5.600218] e3_user: loading out-of-tree module taints kernel.
[    5.610584] Absolute symbol: 0x00000000 e3_user.c
[    5.610661] Absolute symbol: 0x00000000 e3_user.mod.c
[    5.610674] Absolute symbol: 0x00000000 module-common.c
[    5.611048] Failed to find symbol e3_lib_secret
[    5.613858] e3_user: Unknown symbol e3_lib_secret (err -2)
[    5.613914] Failed to find symbol e3_lib_add
[    5.613928] e3_user: Unknown symbol e3_lib_add (err -2)
[    5.614085] Failed to find symbol e3_lib_plain
[    5.614099] e3_user: Unknown symbol e3_lib_plain (err -2)
== 2) e3_lib 다음 e3_user
rc=0
[    6.119010] init_module: umod=0000000088d9636f, len=39976, uargs=0000000012c72290
[    6.122651] Core section allocation order for e3_lib:
...
[    6.122710] 	__ksymtab
[    6.122715] 	__ksymtab_gpl
...
[    6.135681] Failed to find symbol e3_lib_plain
[    6.135764] Failed to find symbol e3_lib_add
[    6.135777] Failed to find symbol e3_lib_secret
[    6.151688] e3_lib: loaded, exporting 3 symbols
[    6.224780] init_module: umod=00000000a1eabaf6, len=38328, uargs=0000000012c72290
...
[    6.234560] e3_user does not use e3_lib!
[    6.234777] Allocating new usage for e3_user.
[    6.245325] e3_user: add(2,3)=5 plain=7 secret=0x5eed
== kallsyms
ffffd8861ff86010 r __ksymtab_e3_lib_add	[e3_lib]
ffffd8861ff86000 r __ksymtab_e3_lib_plain	[e3_lib]
ffffd8861ff8601c r __ksymtab_e3_lib_secret	[e3_lib]
ffffd8861ff82058 t e3_lib_exit	[e3_lib]
ffffd8861ff82050 t e3_lib_secret	[e3_lib]
ffffd8861ff82040 t e3_lib_add	[e3_lib]
ffffd8861ff82048 T e3_lib_plain	[e3_lib]
== lsmod
e3_user 12288 0 - Live 0xffffd8861ff88000 (O)
e3_lib 12288 1 e3_user, Live 0xffffd8861ff82000 (O)
lib refcnt=1
e3_user
```

(`...`는 섹션 목록 줄을 생략한 곳이다. 원래 로그는 섹션마다 한 줄씩 수십 줄이다.)

출력에서 볼 것:

- **로더의 순서가 로그에 그대로 보인다**: `init_module`(BusyBox는 파일을 메모리로 읽어 넘긴다) → 섹션 배치(Core 영역과 Init 영역을 따로) → 최종 주소 → taint → 심볼 해석(`Absolute symbol`, `Failed to find symbol`) → 실패면 `Unknown symbol ... (err -2)`. -2 = `-ENOENT`.
- 실패한 `e3_user`가 받은 주소(`0xffffd8861ff82000`)를 성공한 `e3_lib`가 그대로 다시 받았다. 실패한 로드의 메모리는 깨끗이 반납됐다.
- `e3_lib`를 로드할 때도 `Failed to find symbol e3_lib_*`가 찍혔다. 에러가 아니다. `complete_formation()` → `verify_exported_symbols()`가 "내가 내보낼 이름이 이미 다른 모듈에 있나?"를 확인한 것이다 — **없어야** 정상이다. 같은 이름을 두 모듈이 내보내면 두 번째 로드는 `exports duplicate symbol`로 실패한다.
- `e3_user does not use e3_lib!` → `Allocating new usage for e3_user.`: `ref_module()`이 "아직 의존 기록이 없으니 새로 만든다"는 뜻이다. 이 순간 `e3_lib`의 refcount가 1이 되고 `holders/e3_user`가 생긴다.
- kallsyms에서 `e3_lib_plain`만 `T`(전역)이고 나머지는 `t`다. 내보내기 여부는 `T`/`t`가 아니라 **`__ksymtab_<이름>` 항목의 존재**로 판단한다. `/proc/kallsyms`는 디버깅용 전체 심볼 목록이고, 다른 모듈이 링크할 수 있는 것은 `__ksymtab`에 있는 것뿐이다.

> 실무 팁: `/proc/kallsyms`의 주소는 root가 아니면 0으로 보인다(`kernel.kptr_restrict`). 그리고 `kallsyms_lookup_name()`은 5.7부터 모듈에 export되지 않는다 — "kallsyms로 주소 찾아서 비공개 함수 부르기"는 막혔다.

---

## 8. 언로드를 막는 장치 — refcount

### 8.1 직관

`e3_lib`를 `e3_user`가 쓰는 중에 빼면? `e3_user`가 다음에 `e3_lib_add`를 부르는 순간 **해제된 메모리로 점프**한다. 열린 `/dev/e3_hold`가 있는데 모듈을 빼면? 다음 `read()`가 사라진 `.read` 함수로 간다. 커널은 이걸 **모듈 refcount**로 막는다. 펌웨어 RTOS에서 "태스크가 쓰는 중인 드라이버 객체는 삭제 금지" 카운터를 두는 것과 같다.

refcount를 올리는 길은 세 가지다:

| 누가 | 어떻게 | 보이는 곳 |
|---|---|---|
| 다른 모듈이 내 심볼을 씀 | 로드 때 `resolve_symbol()` → `ref_module()` → `strong_try_module_get()` | `lsmod`의 Used by 칸에 모듈 이름, `/sys/module/<m>/holders/` |
| 유저가 내 장치 파일을 open | VFS가 `fops_get()` = `try_module_get(fops->owner)` | Used by 숫자만 오른다 |
| 내 코드가 명시적으로 | `try_module_get(owner)` / `module_put(owner)` | 숫자 |

`fops_get`의 6.18 정의(`include/linux/fs.h`):

```c
#define fops_get(fops) ({						\
	const struct file_operations *_fops = (fops);			\
	(((_fops) && try_module_get((_fops)->owner) ? (_fops) : NULL));	\
})
```

그래서 드라이버의 `file_operations`에 `.owner = THIS_MODULE` **한 줄**이 있어야 한다. 이게 없으면(owner가 NULL) `try_module_get(NULL)`은 그냥 성공하고 아무것도 잡지 않는다 — 파일이 열려 있어도 모듈이 빠진다.

### 8.2 예제 10 — 사용 중인 모듈 빼 보기

무엇을 확인하나: 심볼 의존(`e3_user` → `e3_lib`)과 열린 파일(`/dev/e3_hold`) 두 경우 모두 `rmmod`가 거절되고, 원인을 없애면 빠지는 것.

```c
// SPDX-License-Identifier: GPL-2.0
/* e3_hold: fd가 열려 있는 동안 모듈 refcount가 올라가는 것을 본다 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>

static int e3_hold_open(struct inode *inode, struct file *f)
{
	/* 여기 들어왔을 때는 이미 VFS가 try_module_get(fops->owner)를 했다 */
	pr_info("open: refcnt=%d\n", module_refcount(THIS_MODULE));
	return 0;
}
static int e3_hold_release(struct inode *inode, struct file *f)
{
	pr_info("release: refcnt=%d (put comes after)\n",
		module_refcount(THIS_MODULE));
	return 0;
}
static const struct file_operations e3_hold_fops = {
	.owner   = THIS_MODULE,		/* 이 한 줄이 rmmod를 막는다 */
	.open    = e3_hold_open,
	.release = e3_hold_release,
};
static struct miscdevice e3_hold_misc = {
	.minor = MISC_DYNAMIC_MINOR, .name = "e3_hold", .fops = &e3_hold_fops,
};
module_misc_device(e3_hold_misc);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("E3 refcount demo");
```

`module_misc_device(x)`는 init에서 `misc_register(&x)`, exit에서 `misc_deregister(&x)`를 만들어 주는 매크로다(`module_platform_driver`와 같은 계열, D3).

```sh
# t2.sh 중간 — 심볼 의존
rmmod e3_lib; echo "rc=$?"
rmmod e3_user; rmmod e3_lib; echo "rc=$?"; dmesg -c
# t4.sh 앞부분 — 열린 파일
insmod e3_hold.ko; ls -l --color=never /dev/e3_hold
exec 3</dev/e3_hold
lsmod; echo "refcnt=$(cat /sys/module/e3_hold/refcnt)"
rmmod e3_hold; echo "rc=$?"; lsmod
exec 3<&-
echo "after close: refcnt=$(cat /sys/module/e3_hold/refcnt)"; rmmod e3_hold; lsmod; echo "(lsmod empty)"
dmesg | grep e3_hold
```

```text
rmmod: remove 'e3_lib': Resource temporarily unavailable
rc=0
rc=0
[   10.500888] hrtimer: interrupt took 8808928 ns
[   18.377879] e3_user: bye
[   18.402702] e3_user unusing e3_lib
[   18.500049] e3_lib: unloaded
crw-------    1 0        0          10, 258 Oct 10 20:45 /dev/e3_hold
e3_hold 12288 1 - Live 0xffffcd72db41f000 (O)
refcnt=1
rmmod: remove 'e3_hold': Resource temporarily unavailable
rc=0
e3_hold 12288 1 - Live 0xffffcd72db41f000 (O)
after close: refcnt=0
(lsmod empty)
[    6.246443] e3_hold: loading out-of-tree module taints kernel.
[    6.400269] e3_hold: open: refcnt=1
[    6.652518] e3_hold: release: refcnt=1 (put comes after)
```

출력에서 볼 것:

- `hrtimer: interrupt took ...` 줄은 TCG 에뮬레이션이 느려서 생긴 무관한 경고다.
- 두 경우 모두 `Resource temporarily unavailable` = `EAGAIN`(= `EWOULDBLOCK`). `rmmod`가 `O_NONBLOCK`으로 `delete_module()`을 부르므로 "기다리지 않고 바로 거절"한다. 데스크톱 kmod `rmmod`는 syscall 전에 refcount를 직접 읽어 보고 `ERROR: Module e3_lib is in use by: e3_user` 같은 친절한 메시지를 낸다. 인터뷰에서 말하는 **"Module is in use"** 가 이것이다.
- `open:` 시점에 이미 refcnt=1이다. `misc_open()`이 `fops_get()`으로 우리 모듈을 잡은 **뒤에** 우리 `.open`을 부르기 때문이다. `release:`에서도 1이다 — `module_put()`은 `.release`가 끝난 **뒤** VFS가 `__fput()`에서 한다. 우리 코드가 실행되는 동안 모듈이 사라질 수 없게 하는 순서다.
- `e3_user unusing e3_lib`: `e3_user`가 빠질 때 `module_unload_free()`가 의존 기록을 지우며 `module_put(e3_lib)`을 한다. 그래서 그다음 `rmmod e3_lib`는 성공한다.
- 여기서도 BusyBox `rmmod`의 rc는 실패인데 0이었다(§4 함정).

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="30" width="170" height="44" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="105" y="49" text-anchor="middle" font-size="12">셸 프로세스</text>
  <text x="105" y="66" text-anchor="middle" font-size="12">fd 3 → /dev/e3_hold</text>
  <rect x="260" y="30" width="150" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="335" y="49" text-anchor="middle" font-size="12">e3_hold</text>
  <text x="335" y="66" text-anchor="middle" font-size="12">refcnt 1</text>
  <line x1="190" y1="52" x2="258" y2="52" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="224" y="94" text-anchor="middle" font-size="12">fops_get()</text>
  <rect x="20" y="120" width="170" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="105" y="139" text-anchor="middle" font-size="12">e3_user</text>
  <text x="105" y="156" text-anchor="middle" font-size="12">e3_lib_add() 호출</text>
  <rect x="260" y="120" width="150" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="335" y="139" text-anchor="middle" font-size="12">e3_lib refcnt 1</text>
  <text x="335" y="156" text-anchor="middle" font-size="12">holders/e3_user</text>
  <line x1="190" y1="142" x2="258" y2="142" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="224" y="184" text-anchor="middle" font-size="12">ref_module()</text>
  <rect x="20" y="210" width="170" height="44" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="105" y="229" text-anchor="middle" font-size="12">e3_multi</text>
  <text x="105" y="246" text-anchor="middle" font-size="12">module_exit 없음</text>
  <rect x="260" y="210" width="150" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="335" y="237" text-anchor="middle" font-size="12">[permanent]</text>
  <line x1="190" y1="232" x2="258" y2="232" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="450" y="20" width="215" height="250" rx="8" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="557" y="42" text-anchor="middle" font-size="13" font-weight="bold">delete_module() 검사</text>
  <text x="462" y="70" font-size="12">1. holders 있음 → EAGAIN</text>
  <text x="462" y="96" font-size="12">2. 상태 LIVE 아님 → EBUSY</text>
  <text x="462" y="122" font-size="12">3. init만 있고 exit 없음</text>
  <text x="482" y="140" font-size="12">→ EBUSY</text>
  <text x="462" y="166" font-size="12">4. refcnt > 0 → EAGAIN</text>
  <text x="462" y="192" font-size="12">5. 통과 → GOING</text>
  <text x="482" y="210" font-size="12">exit() 호출</text>
  <text x="462" y="236" font-size="12">6. free_module()</text>
  <line x1="410" y1="52" x2="448" y2="160" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah5)"/>
  <line x1="410" y1="142" x2="448" y2="70" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah5)"/>
  <line x1="410" y1="232" x2="448" y2="130" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah5)"/>
  <text x="20" y="288" font-size="12">세 모듈 모두 rmmod가 거절된다. 왼쪽 원인을 없애야(fd 닫기, e3_user 먼저 빼기) 빠진다.</text>
</svg>
```
그림 6 — 모듈을 붙잡는 세 가지와 `delete_module()`(6.18 `kernel/module/main.c`)의 검사 순서. 1번은 `list_empty(&mod->source_list)`, 4번은 `try_stop_module()` → `try_release_module_ref()`다.

### 8.3 명시적 `try_module_get` — 언제 직접 쓰나

대부분의 드라이버는 `.owner = THIS_MODULE`로 충분하다. 직접 쓰는 경우는 **"남의 모듈이 준 함수 포인터를 나중에 부를" 때**다. 예를 들어 센서 허브 코어 모듈이 하위 드라이버 모듈이 등록한 `ops`를 보관했다가 부른다면:

```c
/* 코어 모듈 쪽 — 하위 드라이버의 ops를 쓰는 동안 그 모듈을 잡는다 (개념 코드) */
if (!try_module_get(ops->owner))
	return -ENODEV;		/* 그 모듈이 빠지는 중(GOING)이면 실패 */
ret = ops->read_sample(dev, buf);
module_put(ops->owner);
```

`try_module_get()`은 모듈이 GOING이면 **실패**한다(그래서 이름에 try가 있다). `__module_get()`은 무조건 올린다. 그리고 **자기 자신(`THIS_MODULE`)을 잡는 것은 대개 잘못**이다. 자기 코드가 실행 중이라는 사실이 이미 "누군가 나를 불렀다"는 뜻이고, 그 호출자가 잡아야 한다. 특히 자기 workqueue 끝에서 `module_put(THIS_MODULE)`을 하면 put 직후 모듈이 해제되어 함수의 나머지 몇 명령이 해제된 메모리에서 돈다. 이런 경우를 위해 `module_put_and_kthread_exit()` 같은 전용 헬퍼가 있다.

---

## 9. 라이선스, taint, 서명

### 9.1 예제 11 — `MODULE_LICENSE`가 없거나 GPL이 아니면

무엇을 확인하나: 라이선스가 없을 때(빌드), GPL이 아닌데 GPL 전용 심볼을 쓸 때(빌드와 로드), GPL이 아니지만 일반 심볼만 쓸 때(로드 + taint P).

먼저 빌드 단계:

`e3_nolic.c`는 빈 init 하나와 `module_init`만 있고 `MODULE_LICENSE`가 없는 4줄짜리다.

```c
// SPDX-License-Identifier: GPL-2.0
/* e3_prop: GPL이 아닌 라이선스를 선언한 모듈 (taint P 실험용) */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include "e3_lib.h"

static int __init e3_prop_init(void)
{
#ifdef E3_USE_GPL
	pr_info("add=%d\n", e3_lib_add(1, 1));	/* GPL-only 심볼 */
#else
	pr_info("plain=%d\n", e3_lib_plain());	/* EXPORT_SYMBOL */
#endif
	return 0;
}
static void __exit e3_prop_exit(void) { }
module_init(e3_prop_init);
module_exit(e3_prop_exit);
MODULE_LICENSE("Proprietary");
MODULE_DESCRIPTION("E3 non-GPL module");
```

```sh
cd /var/tmp/E3/nolic && make
cd /var/tmp/E3/prop
echo "=== prop GPL-only"; make E3_FLAGS=-DE3_USE_GPL
echo "=== prop GPL-only + KBUILD_MODPOST_WARN=1"; make E3_FLAGS=-DE3_USE_GPL KBUILD_MODPOST_WARN=1
echo "=== prop plain"; make            # 일반 심볼만
```

```text
  CC [M]  e3_nolic.o
  MODPOST Module.symvers
ERROR: modpost: missing MODULE_LICENSE() in e3_nolic.o
WARNING: modpost: missing MODULE_DESCRIPTION() in e3_nolic.o
make[4]: *** [/var/lab/src/linux-6.18.55/scripts/Makefile.modpost:147: Module.symvers] Error 1
=== prop GPL-only
  CC [M]  e3_prop.o
  MODPOST Module.symvers
ERROR: modpost: GPL-incompatible module e3_prop.ko uses GPL-only symbol 'e3_lib_add'
=== prop GPL-only + KBUILD_MODPOST_WARN=1
  CC [M]  e3_prop.o
  MODPOST Module.symvers
ERROR: modpost: GPL-incompatible module e3_prop.ko uses GPL-only symbol 'e3_lib_add'
=== prop plain
  CC [M]  e3_prop.o
  MODPOST Module.symvers
  CC [M]  e3_prop.mod.o
  CC [M]  .module-common.o
  LD [M]  e3_prop.ko
```

(`make[N]: Entering/Leaving` 줄과 nolic의 재귀 make 에러 줄은 첫 줄만 남기고 뺐다. prop 빌드 사이에는 `make clean`을 했다.)

출력에서 볼 것: 6.18에서는 **라이선스 누락이 빌드 에러**다. 그리고 GPL 전용 심볼 사용은 `KBUILD_MODPOST_WARN=1`로도 경고로 낮아지지 않는 **진짜 에러**다. 자주 듣는 "라이선스가 없으면 로드는 되는데 taint된다"는 옛 커널(또는 빌드 검사를 건너뛴 `.ko`) 이야기다. 런타임 동작을 보려면 빌드 검사를 우회한 `.ko`가 필요하다. 그래서 정상 빌드한 `e3_user.ko`의 `.modinfo`에서 `license=GPL`을 같은 길이의 `license=BSD`로 바이트 단위로 바꿨다(BSD 단독은 GPL 호환 목록에 없다). 실무에서 이런 `.ko`는 "다른 빌드 시스템으로 만든 벤더 바이너리"의 모습이다.

```python
#!/usr/bin/env python3
"""patch_ko.py — 같은 길이의 문자열을 .ko 안에서 바꿔치기 (빌드 검사를 우회한 모듈 재현용)"""
import sys
src, dst, old, new = sys.argv[1:5]
data = open(src, 'rb').read()
assert len(old) == len(new) and data.count(old.encode()) == 1, "need exactly one same-length match"
open(dst, 'wb').write(data.replace(old.encode(), new.encode()))
print(f"{dst}: '{old}' -> '{new}'")
```

```sh
cd /var/tmp/E3 && mkdir -p patched
python3 -I patch_ko.py user/e3_user.ko patched/e3_user_bsd.ko "license=GPL" "license=BSD"
modinfo patched/e3_user_bsd.ko | grep -E "license|vermagic"
```

```text
patched/e3_user_bsd.ko: 'license=GPL' -> 'license=BSD'
license:        BSD
vermagic:       6.18.55 SMP preempt mod_unload aarch64
```

게스트(t2.sh 뒷부분):

```sh
insmod e3_lib.ko; insmod e3_prop.ko; echo "rc=$?"
insmod e3_user_bsd.ko; echo "rc=$?"; dmesg -c
lsmod
t=$(cat /proc/sys/kernel/tainted); echo "tainted=$t"
for b in 0:P 1:F 2:S 3:R 4:M 5:B 6:U 7:D 8:A 9:W 10:C 11:I 12:O 13:E 14:L 15:K 16:X 17:T 18:N 19:J; do
  n=${b%%:*}; [ $(( (t >> n) & 1 )) -eq 1 ] && echo "  bit $n = ${b#*:}"; done
```

```text
rc=0
insmod: can't insert 'e3_user_bsd.ko': unknown symbol in module or invalid parameter
rc=1
[   18.838047] e3_lib: loaded, exporting 3 symbols
[   18.929622] e3_prop: module license 'Proprietary' taints kernel.
[   18.929840] Disabling lock debugging due to kernel taint
[   18.929930] e3_prop: module license taints kernel.
[   18.931073] e3_prop: plain=7
[   18.999850] e3_user: Unknown symbol e3_lib_secret (err -2)
[   18.999911] e3_user: Unknown symbol e3_lib_add (err -2)
e3_prop 12288 0 - Live 0xffffd8861ff88000 (PO)
e3_lib 12288 1 e3_prop, Live 0xffffd8861ff82000 (O)
tainted=4097
  bit 0 = P
  bit 12 = O
```

출력에서 볼 것:

- `e3_prop`("Proprietary")은 **로드된다**. `EXPORT_SYMBOL`인 `e3_lib_plain`만 쓰니까. 대신 커널이 `P` taint를 받고 **lock debugging(lockdep)이 꺼진다** — 소스를 볼 수 없는 코드가 섞였으니 커널 개발자들은 이 커널의 버그 리포트를 받지 않는다는 뜻이다. `lsmod`에 `(PO)`.
- 라이선스만 바꾼 `e3_user_bsd`(내부 이름은 `e3_user`)는 **GPL 전용인 `e3_lib_add`와 `e3_lib_secret`만** `Unknown symbol`로 실패했고, `EXPORT_SYMBOL`인 `e3_lib_plain`은 문제없이 찾았다. 런타임에는 "GPL 위반" 같은 메시지가 따로 없고 그냥 **그 심볼이 안 보이는 것처럼** 처리된다(`resolve_symbol()`이 `gplok = false`로 찾는다). 벤더 바이너리 `.ko`가 "Unknown symbol"로 실패하면 라이선스도 의심하라.
- `tainted=4097` = 0x1001 = 비트 0(P) + 비트 12(O). 위의 해독 루프는 6.18 `kernel/panic.c`의 `taint_flags[]` 순서를 그대로 옮긴 것이다.

### 9.2 taint 플래그 읽는 법

`/proc/sys/kernel/tainted`는 비트마스크이고, Oops 메시지의 `Tainted: P   O` 줄과 `lsmod`의 괄호도 같은 정보다. 모듈과 관련된 것만 추리면:

| 비트 | 문자 | 뜻 | 언제 |
|---|---|---|---|
| 0 | P | proprietary module | GPL 호환이 아닌 라이선스(없음 포함) |
| 1 | F | forced load | `modprobe --force`로 vermagic·버전 검사를 무시 |
| 7 | D | died | Oops/BUG가 났다 |
| 9 | W | warning | `WARN_ON`이 터졌다 |
| 10 | C | staging | `drivers/staging`의 모듈 |
| 12 | O | out-of-tree | `.modinfo`에 `intree=Y`가 없음 |
| 13 | E | unsigned | `CONFIG_MODULE_SIG=y`인데 서명이 없거나 검증 실패 |
| 18 | N | test | KUnit 등 테스트 모듈 |

비교를 위해 랩 커널 트리 안에서 빌드된 모듈(`crc-ccitt.ko`, `intree: Y`)을 넣어 봤다(t7.sh):

```text
tainted(start)=0
crc_ccitt 12288 0 - Live 0xffffd456cc97e000
taint file: ''  tainted=0
ffffd456cc97e020 T crc_ccitt	[crc_ccitt]
e3_hello 12288 0 - Live 0xffffd456cc984000 (O)
crc_ccitt 12288 0 - Live 0xffffd456cc97e000
tainted=4096
```

in-tree 모듈은 taint를 남기지 않는다. 우리 `e3_hello`가 들어오는 순간 4096(O)이 된다. **taint는 "고장"이 아니라 "디버깅할 때 알아야 할 이력"** 이다. 커널 메일링 리스트에 Oops를 올리면 맨 먼저 이 줄을 본다.

### 9.3 모듈 서명 — 이 랩에서는 꺼져 있다

```sh
grep -E "CONFIG_MODULE_SIG|CONFIG_MODVERSIONS|CONFIG_MODULE_FORCE" /var/lab/out/linux-build/.config
```

```text
# CONFIG_MODULE_FORCE_LOAD is not set
# CONFIG_MODULE_FORCE_UNLOAD is not set
# CONFIG_MODVERSIONS is not set
# CONFIG_MODULE_SIG is not set
```

`CONFIG_MODULE_SIG=y`이면 빌드가 `.ko` 끝에 서명(PKCS#7)과 `~Module signature appended~` 꼬리표를 붙이고, 커널은 로드 때 `module_sig_check()`에서 내장 키링(`certs/`)으로 검증한다.

| 설정 | 서명이 없거나 틀리면 |
|---|---|
| `MODULE_SIG=y`, `MODULE_SIG_FORCE=n` | 로드는 되고 taint `E` + `module verification failed: signature and/or required key missing - tainting kernel` |
| `MODULE_SIG_FORCE=y` (또는 부팅 인자 `module.sig_enforce=1`) | **로드 거부** (`-EKEYREJECTED` 등) |
| Secure Boot + lockdown | 강제와 같은 효과 |

이 랩은 서명이 꺼져 있어서 E taint가 생기지 않는다(그래서 위 출력에 E가 없다). 실무 장비(보안 부팅을 하는 드론·차량)에서는 **서명 키를 빌드 서버에 두고 모든 `.ko`를 서명**한다. out-of-tree 벤더 모듈도 우리 키로 다시 서명해야 하며(`scripts/sign-file`), 이걸 빠뜨리면 현장 장비에서 드라이버가 안 올라온다. 보안 부팅 체인은 C5와 L5에서 다룬다.

---

## 10. vermagic — "다른 커널용으로 빌드했다"

### 10.1 예제 12 — vermagic만 바꾼 `.ko` 넣어 보기

무엇을 확인하나: 커널 버전 문자열이 다른 모듈이 로드 때 어떻게 거절되는지. 다른 커널 트리로 빌드하는 대신, §9와 같은 바꿔치기로 vermagic의 `6.18.55`를 `6.18.99`로 고쳤다.

```sh
python3 -I patch_ko.py base/e3_hello.ko patched/e3_hello_badmagic.ko "vermagic=6.18.55" "vermagic=6.18.99"
# guest
insmod e3_hello_badmagic.ko; dmesg | tail -1
```

```text
patched/e3_hello_badmagic.ko: 'vermagic=6.18.55' -> 'vermagic=6.18.99'
[    8.000621] e3_hello: version magic '6.18.99 SMP preempt mod_unload aarch64' should be '6.18.55 SMP preempt mod_unload aarch64'
insmod: can't insert 'e3_hello_badmagic.ko': invalid module format
[    8.000621] e3_hello: version magic '6.18.99 SMP preempt mod_unload aarch64' should be '6.18.55 SMP preempt mod_unload aarch64'
```

(첫 줄 dmesg는 `pr_err`급이라 콘솔에도 찍혀 두 번 보인다.)

출력에서 볼 것: `invalid module format` = `-ENOEXEC`. **유저에게 보이는 메시지는 애매하고, 진짜 이유는 dmesg에만 있다.** "invalid module format"을 보면 무조건 `dmesg | tail`부터 본다. 이 검사는 `early_mod_check()` → `check_modinfo()` → `same_magic()`이 하고, 메모리를 할당하기 **전**이다(그림 4의 "UNFORMED 전 거부").

### 10.2 vermagic과 modversions(CRC)의 차이

| 장치 | 무엇을 비교하나 | 이 랩 |
|---|---|---|
| vermagic | 커널 릴리스 문자열 + SMP/preempt/mod_unload/arch | 켜짐(항상) |
| modversions (`CONFIG_MODVERSIONS`) | **심볼마다** 함수 시그니처·구조체 배치의 CRC (`Module.symvers` 첫 칸, `.ko`의 `__versions` 섹션) | 꺼짐 → CRC 칸이 `0x00000000` |

modversions가 켜진 커널(배포판 대부분)은 릴리스 문자열이 같아도 구조체 하나가 바뀌면 `disagrees about version of symbol xxx`로 거절한다. 반대로 vermagic만 있는 이 랩 같은 커널은 **같은 `6.18.55`인데 `.config`가 다른 트리로 빌드한 모듈이 vermagic을 통과해 버릴 수 있다.** 그러면 구조체 오프셋이 달라 엉뚱한 메모리를 건드린다. 결론은 하나다: **모듈은 장비에 올라간 그 커널의 빌드 트리로 빌드한다.**

`CONFIG_LOCALVERSION`(예: `-imx`)과 `CONFIG_LOCALVERSION_AUTO`(git 해시 `-g1a2b3c`)도 릴리스 문자열에 붙는다. 벤더 커널에 패치 한 줄 넣고 다시 빌드했더니 `uname -r`이 `6.6.23-g4f1e…-dirty`로 바뀌어 기존 모듈이 전부 안 올라가는 사고가 흔하다.

---

## 11. `modprobe`와 `depmod` — 의존성을 알아서

### 11.1 직관

`insmod`는 **경로를 받아 그 파일 하나**를 넣는다. 의존 모듈은 신경 쓰지 않는다(그래서 §7에서 순서를 틀리자 실패했다). `modprobe`는 **이름을 받아** `/lib/modules/$(uname -r)/`에서 파일을 찾고, 의존 모듈을 **먼저** 넣는다. 그러려면 "누가 누구에게 의존하는가" 색인이 필요하다. 그 색인을 만드는 도구가 `depmod`다. 펌웨어로 치면 링커 맵에서 init 순서를 미리 계산해 표로 만들어 두는 것이다.

### 11.2 예제 13 — BusyBox `depmod`/`modprobe`로 의존 모듈 자동 로드

무엇을 확인하나: `/lib/modules/6.18.55/extra/`에 `.ko`를 두고 `depmod` → `modprobe e3_user` 한 번에 `e3_lib`까지 올라오는지, `modprobe -r`이 의존 모듈까지 내리는지, 그리고 커널 명령줄의 모듈 옵션을 BusyBox modprobe가 읽는지.

```sh
# t5.sh — 커널 인자에 e3_param.count=7 printk.time=0 추가
K=/lib/modules/$(uname -r)
echo "== before depmod"; ls --color=never -R $K
depmod; echo "depmod rc=$?"; ls --color=never $K; echo "-- modules.dep.bb"; cat $K/modules.dep.bb
echo "== modprobe e3_user"; modprobe e3_user; lsmod
echo "== modprobe -r e3_user"; modprobe -r e3_user; lsmod; echo "(end lsmod)"
echo "== modprobe e3_param (cmdline e3_param.count=7)"; modprobe e3_param; cat /sys/module/e3_param/parameters/count; rmmod e3_param
echo "== insmod e3_param (cmdline is ignored)"; insmod /e3/e3_param.ko; cat /sys/module/e3_param/parameters/count; rmmod e3_param
```

```text
lab initramfs: 6.18.55 up 3.66s
== cmdline: console=ttyAMA0 quiet e3_param.count=7 printk.time=0 LAB_CMD=sh~/e3/t5.sh
== before depmod
/lib/modules/6.18.55:
extra           modules.dep.bb

/lib/modules/6.18.55/extra:
e3_hello.ko  e3_lib.ko    e3_param.ko  e3_user.ko
depmod rc=0
extra           modules.dep.bb
-- modules.dep.bb
extra/e3_param.ko

extra/e3_user.ko
e3_lib

extra/e3_hello.ko

extra/e3_lib.ko symbol:e3_lib_add symbol:e3_lib_plain symbol:e3_lib_secret

== modprobe e3_user
e3_user 12288 0 - Live 0xffffa814037eb000 (O)
e3_lib 12288 1 e3_user, Live 0xffffa814037e5000 (O)
== modprobe -r e3_user
(end lsmod)
== modprobe e3_param (cmdline e3_param.count=7)
1
== insmod e3_param (cmdline is ignored)
1
```

출력에서 볼 것:

- `modprobe e3_user` 한 줄에 `e3_lib`가 먼저 올라왔다. `modprobe -r e3_user`는 쓰는 모듈이 없어진 `e3_lib`까지 내렸다(lsmod가 비었다).
- BusyBox의 색인은 `modules.dep.bb` 한 파일이다(이 BusyBox는 `CONFIG_MODPROBE_SMALL=y`). 모듈마다 "파일 경로 [alias/symbol 목록]" 줄 + "의존 모듈 이름" 줄 + 빈 줄. `e3_lib` 줄의 `symbol:e3_lib_add`는 "이 심볼이 필요하면 이 모듈을 넣어라"는 색인이다.
- `depmod` 전에 이미 `modules.dep.bb`가 있었다. 이 rootfs에는 우리가 만든 적이 없으니, 부팅 중 커널이 `request_module()`로 `/sbin/modprobe`(= `/proc/sys/kernel/modprobe`)를 부른 적이 있고, modprobe-small이 색인이 없을 때 전체를 스캔하며 만들어 둔 것으로 보인다(BusyBox `modprobe-small.c` 주석: "This will generate modules.dep.bb as a side effect").
- **커널 명령줄의 `e3_param.count=7`은 BusyBox modprobe가 적용하지 않았다**(count=1). kmod `modprobe`는 `/proc/cmdline`에서 `<모듈>.<파라미터>=` 항목을 찾아 옵션으로 붙여 준다. 그리고 `insmod`는 어느 쪽이든 명령줄을 보지 않는다. 장비에서 "명령줄에 넣었는데 안 먹어요"의 흔한 원인이다. 확실한 방법은 `/etc/modprobe.d/*.conf`의 `options e3_param count=7`(kmod)이다.

### 11.3 데스크톱·Yocto의 kmod `depmod`는 무엇을 만드나

비교를 위해 같은 `.ko`들을 VM 호스트의 kmod `depmod`(kmod 34.2)로 별도 디렉터리에 색인했다. 이건 파일만 읽고 쓰는 작업이라 호스트 커널에 아무것도 넣지 않는다.

```sh
T=/var/tmp/E3/kmodtree; mkdir -p $T/lib/modules/6.18.55/extra
cp /var/tmp/E3/lib/e3_lib.ko /var/tmp/E3/user/e3_user.ko /var/tmp/E3/base/e3_param.ko $T/lib/modules/6.18.55/extra/
cp /var/lab/out/linux-build/modules.order /var/lab/out/linux-build/modules.builtin \
   /var/lab/out/linux-build/modules.builtin.modinfo $T/lib/modules/6.18.55/
depmod --version | head -1; depmod -b $T 6.18.55; echo rc=$?
cd $T/lib/modules/6.18.55; ls; cat modules.dep; cat modules.symbols
modprobe -d $T -S 6.18.55 --show-depends e3_user
modprobe -d $T -S 6.18.55 -n -v virtio_blk
```

```text
kmod version 34.2
rc=0
extra
modules.alias
modules.alias.bin
modules.builtin
...
modules.dep
modules.dep.bin
modules.devname
modules.order
modules.softdep
modules.symbols
modules.symbols.bin
modules.weakdep
extra/e3_user.ko: extra/e3_lib.ko
extra/e3_param.ko:
extra/e3_lib.ko:
# Aliases for symbols, used by symbol_request().
alias symbol:e3_lib_plain e3_lib
alias symbol:e3_lib_add e3_lib
alias symbol:E3_LIB e3_lib
alias symbol:e3_lib_secret e3_lib
insmod /var/tmp/E3/kmodtree/lib/modules/6.18.55/extra/e3_lib.ko 
insmod /var/tmp/E3/kmodtree/lib/modules/6.18.55/extra/e3_user.ko 
builtin virtio_blk
```

출력에서 볼 것:

- `modules.dep`의 형식은 `모듈: 의존1 의존2`. `--show-depends`가 실제로 할 `insmod` 순서를 보여 준다 — **의존 모듈이 먼저**. (`ls` 목록의 `...`는 `modules.builtin.*` 세 줄을 생략한 곳이다.)
- `modules.alias`(D3 §9의 modalias 자동 로딩), `modules.symbols`(`symbol_request()`용), `modules.builtin`(built-in 목록) 등 파일이 여러 개고 `.bin`은 빠른 조회용 트라이 색인이다. `alias symbol:E3_LIB` 줄은 네임스페이스 이름이 심볼처럼 잡힌 것으로, 이 조합(kmod 34.2 + 6.18)에서 관찰된 그대로 옮겼다(무해).
- `virtio_blk`는 `modules.builtin`에 있어서 `builtin virtio_blk`로 답하고 아무것도 하지 않는다. **built-in 드라이버를 `modprobe`해도 에러가 아니다** — udev가 modalias로 modprobe를 불러도 조용히 넘어가게 하려는 장치다.
- 실무에서는 `make modules_install INSTALL_MOD_PATH=<rootfs>`가 `.ko`를 `lib/modules/<버전>/kernel/...`(out-of-tree 모듈은 `M=`로 설치할 때 기본 `updates/`, 6.18 `scripts/Makefile.modinst`의 `INSTALL_MOD_DIR ?= updates`)에 복사하고 마지막에 `depmod`를 돌린다. Yocto·Buildroot는 타깃 rootfs를 만들 때 호스트 쪽 `depmod -b`를 돌린다(위 명령과 같은 방식).

| | BusyBox (이 랩, modprobe-small) | kmod (데스크톱·Yocto) |
|---|---|---|
| 색인 | `modules.dep.bb` 하나 | `modules.dep`, `.alias`, `.symbols`, `.builtin`, `.bin` |
| insmod syscall | `init_module` (메모리 버퍼) | `finit_module` (fd) |
| 명령줄 `mod.param=` | 적용 안 함(관찰) | `/proc/cmdline`에서 읽어 적용 |
| `/etc/modprobe.d` (options, blacklist, softdep) | 이 빌드에서 블랙리스트 기능 꺼짐 | 지원 |
| rmmod 실패 종료 코드 | 0 (관찰) | 1 |
| 사용 중 메시지 | `Resource temporarily unavailable` | `Module X is in use by: Y` |

---

## 12. built-in(`=y`) vs 모듈(`=m`)

### 12.1 같은 소스, 두 가지 운명

Kconfig에서 `tristate` 항목은 `y`(커널 이미지에 포함), `m`(`.ko`), `n`(빌드 안 함) 중 하나다(E2). **같은 드라이버 소스가 설정에 따라 둘 중 하나가 된다.** 그래서 매크로들이 두 가지로 정의돼 있다:

| | `=m` (모듈) | `=y` (built-in) |
|---|---|---|
| `module_init(fn)` | `init_module` 별칭 → insmod 때 불림 | `__initcall(fn)` = `device_initcall(fn)` → 부팅 중 레벨 6에서 불림 (C6) |
| `module_exit(fn)` | `cleanup_module` 별칭 → rmmod 때 | 불릴 일이 없다. arm64는 `.exit.text`를 init 영역(`__init_begin`~`__init_end`)에 두었다가 부팅 후 버린다 |
| `__init` 코드 | 모듈 init 후 해제 (`initsize: 0`) | 부팅 끝에 `Freeing unused kernel memory`로 해제 |
| 파라미터 넘기기 | `insmod m.ko p=v`, `/etc/modprobe.d` | 커널 명령줄 `m.p=v` |
| `/sys/module/m/` | `initstate`, `refcnt`, `holders` … 전부 | 파라미터가 있을 때만 `parameters/`와 `uevent` |
| `THIS_MODULE` | `&__this_module` | `NULL` |
| `lsmod` | 보인다 | 안 보인다 (`modules.builtin`에만) |
| 장점 | 필요할 때만, 교체·디버깅 쉬움 | 부팅 초기에 필요(루트 FS·콘솔·인터럽트 컨트롤러), rootfs 전에 동작 |

C6 노트의 표 그대로, `include/linux/module.h`의 built-in 쪽 정의는 `#define module_init(x) __initcall(x);`이고 `include/linux/init.h`의 `#define __initcall(fn) device_initcall(fn)`이다. 그래서 built-in 드라이버의 init 순서는 링크 순서(Makefile 순서)와 initcall 레벨로 정해진다.

### 12.2 예제 14 — built-in 모듈의 흔적과 명령줄 파라미터

무엇을 확인하나: built-in인 `virtio_blk`·`printk`도 `/sys/module/`에 나타나지만 `initstate`가 없다는 것, 명령줄 `printk.time=0`이 built-in 파라미터를 바꾸는 것.

```sh
# t5.sh 끝 (커널 인자 printk.time=0)
for d in /sys/module/*/parameters; do m=${d%/parameters}; [ -e $m/initstate ] || echo -n "${m##*/} "; done; echo
cat /sys/module/printk/parameters/time
# t6.sh / t7.sh
ls --color=never /sys/module/printk/parameters /sys/module/module/parameters
ls /sys/module/printk/initstate
ls --color=never /sys/module/virtio_blk /sys/module/virtio_blk/parameters
```

```text
== built-in modules with parameters
8250 8250_core acpi acpiphp ahci ahci_ceva amd_xgbe arm_smmu arm_smmu_v3 armmmci auth_rpcgss battery blk_cgroup block bq27xxx_battery button clk_imx8mm clk_imx8mn clk_imx8mp clk_imx8mq clk_imx93 cpufreq cpuidle cros_charge_control cros_ec_debugfs cryptomgr dns_resolver dynamic_debug e1000 e1000e edac_core efi_pstore ehci_hcd fb firmware_class fsl_dpa gpiolib_acpi grant_table hdma hdma_mgmt hibernate hid hid_apple hisi_rng hisi_sas_main hisi_sas_v3_hw hns3 i2c_algo_bit i2c_qup igb kernel keyboard kvm libahci libata lockd loop megaraid_sas memory_hotplug mmcblk module musb_hdrc mvneta mvpp2 netfs nfs nfs_layout_flexfiles nfs_layout_nfsv41_files nfsv4 nmi_backtrace nvmem_qfprom overlay page_reporting pci_hotplug pcie_aspm phy_xgene pinctrl_bcm2835 pnp printk processor psmouse pstore qcom_scm random rcupdate rcutree rng_core sata_sil24 sbsa_gwdt scsi_mod sdhci secretmem slab_common smc91x snps_udc_core snps_udc_plat spurious srcutree sunrpc sysrq tcp_cubic thermal tpm ufshcd_core usb_storage usbcore usbhid vfio_iommu_type1 vfio_pci virtio_blk virtio_net virtio_pci vt watchdog workqueue xen xen_blkfront xen_gntalloc xen_gntdev xen_kbdfront xen_netfront xen_privcmd xhci_hcd xilinx_uartps zynqmp_ipi_mailbox 
== printk.time
N
/sys/module/module/parameters:
async_probe

/sys/module/printk/parameters:
always_kmsg_dump         console_suspend          time
console_no_auto_verbose  ignore_loglevel
ls: /sys/module/printk/initstate: No such file or directory
/sys/module/virtio_blk:
parameters  uevent

/sys/module/virtio_blk/parameters:
num_request_queues  poll_queues         queue_depth
```

출력에서 볼 것:

- arm64 defconfig라 i.MX8·Xen·Qualcomm까지 built-in이 아주 많다. 이들은 `lsmod`에는 없지만 파라미터 때문에 `/sys/module/<이름>/parameters/`가 있다. 장비에서 "이 드라이버 모듈이야, built-in이야?"는 `/sys/module/<이름>/initstate` 존재 여부로 바로 안다(또는 `modules.builtin`에서 grep).
- `printk.time=0`을 명령줄에 넣었더니 `time`이 `N`이 됐다(이 부팅의 dmesg에는 타임스탬프가 없었다). `printk`는 진짜 "모듈"이 아니지만 `module_param`을 쓰는 built-in 코드는 모두 `KBUILD_MODNAME`(여기서는 `printk`)을 접두어로 쓴다.
- `/sys/module/module/parameters/async_probe`: 모듈 로더 자신도 파라미터를 가진다(`module.async_probe=1`이면 모든 모듈 드라이버의 probe를 비동기로).

---

## 13. 커널 안에서는 무슨 일이 일어나나

### 13.1 `insmod`부터 `LIVE`까지 — 6.18의 함수 이름 그대로

```svg
<svg viewBox="0 0 680 470" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="16" width="300" height="44" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="170" y="35" text-anchor="middle" font-size="12">insmod (BusyBox, 이 랩)</text>
  <text x="170" y="52" text-anchor="middle" font-size="12">init_module(buf, len, args)</text>
  <rect x="360" y="16" width="300" height="44" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="510" y="35" text-anchor="middle" font-size="12">insmod / modprobe (kmod)</text>
  <text x="510" y="52" text-anchor="middle" font-size="12">finit_module(fd, args, flags)</text>
  <rect x="20" y="80" width="300" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="99" text-anchor="middle" font-size="12">copy_module_from_user()</text>
  <text x="170" y="116" text-anchor="middle" font-size="12">유저 버퍼를 커널로 복사</text>
  <rect x="360" y="80" width="300" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="510" y="99" text-anchor="middle" font-size="12">idempotent_init_module()</text>
  <text x="510" y="116" text-anchor="middle" font-size="12">init_module_from_file()</text>
  <line x1="170" y1="60" x2="170" y2="78" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="510" y1="60" x2="510" y2="78" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="170" y1="124" x2="280" y2="148" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="510" y1="124" x2="400" y2="148" stroke="currentColor" marker-end="url(#ah6)"/>
  <rect x="170" y="150" width="340" height="30" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="170" text-anchor="middle" font-size="13" font-weight="bold">load_module(info, uargs, flags)</text>
  <line x1="340" y1="180" x2="175" y2="196" stroke="currentColor" marker-end="url(#ah6)"/>
  <rect x="20" y="198" width="310" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="30" y="216" font-size="12">1. module_sig_check — 서명 (랩: 꺼짐)</text>
  <rect x="20" y="234" width="310" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="30" y="252" font-size="12">2. elf_validity_cache_copy — ELF 검증</text>
  <rect x="20" y="270" width="310" height="26" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="30" y="288" font-size="12">3. early_mod_check — vermagic 비교</text>
  <rect x="20" y="306" width="310" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="30" y="324" font-size="12">4. layout_and_allocate — 영역 할당·복사</text>
  <rect x="20" y="342" width="310" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="30" y="360" font-size="12">5. add_unformed_module — UNFORMED 등록</text>
  <rect x="20" y="378" width="310" height="26" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="30" y="396" font-size="12">6. module_augment_kernel_taints — O·P</text>
  <rect x="350" y="198" width="310" height="26" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="360" y="216" font-size="12">7. simplify_symbols — 심볼 해석</text>
  <rect x="350" y="234" width="310" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="360" y="252" font-size="12">8. apply_relocations — 주소 패치</text>
  <rect x="350" y="270" width="310" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="360" y="288" font-size="12">9. complete_formation — RO/NX, COMING</text>
  <rect x="350" y="306" width="310" height="26" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="360" y="324" font-size="12">10. parse_args — 모듈 파라미터</text>
  <rect x="350" y="342" width="310" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="360" y="360" font-size="12">11. mod_sysfs_setup — /sys/module/&lt;m&gt;</text>
  <rect x="350" y="378" width="310" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="360" y="396" font-size="12">12. do_init_module — init() → LIVE</text>
  <line x1="330" y1="391" x2="348" y2="211" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah6)"/>
  <text x="20" y="430" font-size="12">빨간 칸 = 이 노트에서 실패를 재현한 지점 (vermagic §10, Unknown symbol §7·§9, 파라미터 §5).</text>
  <text x="20" y="452" font-size="12">각 단계 실패 시 load_module 끝의 goto 라벨들이 역순으로 정리한다 — 커널도 §6의 패턴을 쓴다.</text>
</svg>
```
그림 7 — 6.18 `kernel/module/main.c`의 로드 경로(왼쪽 열 다음 오른쪽 열). 함수 이름은 랩 소스에서 grep으로 확인했다. 예제 9의 dyndbg 로그(섹션 배치 → taint → `Absolute symbol`/`Failed to find symbol`)가 이 순서와 일치한다.

단계별 설명(펌웨어 링커·로더와 대응시켜):

| 단계 | 하는 일 | 펌웨어로 치면 |
|---|---|---|
| syscall 입구 | `may_init_module()`: `CAP_SYS_MODULE`·`modules_disabled` 검사. `finit_module`은 `idempotent_init_module()`로 **같은 파일(inode)의 동시 로드를 한 번으로 합친다**(udev가 같은 모듈을 여러 번 요청하는 부팅 폭주 대책) | 부트로더가 이미지 헤더 확인 |
| `module_sig_check` | 서명 검증 (`CONFIG_MODULE_SIG`) | secure boot 서명 검증 |
| `elf_validity_cache_copy` | ELF 헤더·섹션 표·`.modinfo`·`.gnu.linkonce.this_module` 존재 확인, 이름 확정 | ELF 파서 |
| `early_mod_check` | 블랙리스트, `check_modstruct_version`, `check_modinfo`(vermagic, livepatch), 같은 이름 모듈이 이미 있나 | 버전 호환 확인 |
| `layout_and_allocate` | core(text·rodata·data)와 init 영역을 **따로** 배치하고 메모리 할당(`execmem`), 섹션 복사 | 링커 스크립트의 섹션 배치 |
| `add_unformed_module` | 전역 `modules` 리스트에 UNFORMED로 등록 (중복 로드 방지) | — |
| `module_augment_kernel_taints` | `intree` 없으면 O, license가 GPL 호환이 아니면 P, staging이면 C | — |
| `find_module_sections` / `setup_modinfo` | `__ksymtab`, `__param`, `__ex_table` 등 특수 섹션 위치 기록, modinfo 속성 | 링커 맵 읽기 |
| `simplify_symbols` | 미해결 심볼마다 `resolve_symbol()` → 커널+모듈 심볼 표 검색, GPL·네임스페이스 검사, `ref_module()`로 의존 기록 | 링커의 심볼 해석 |
| `apply_relocations` | `.rela.*` 항목대로 명령어·데이터에 주소를 써 넣는다(arm64: `apply_relocate_add()`, ADRP/BL 범위 밖이면 PLT 경유 — 그래서 `.plt` 섹션이 있다) | 링커의 재배치 |
| `post_relocation` / `complete_formation` | 퍼센트(per-cpu) 데이터 복사, **중복 export 검사**, text를 RO+X, rodata를 RO, data를 NX로(`STRICT_MODULE_RWX`), 상태 COMING | MPU 영역 권한 설정 |
| `prepare_coming_module` / `parse_args` | 모듈 notifier(`MODULE_STATE_COMING`), 파라미터 파싱(타입·콜백 검사 실패면 로드 실패) | — |
| `mod_sysfs_setup` | `/sys/module/<m>/` 생성 | — |
| `do_init_module` | `do_one_initcall(mod->init)` → 성공이면 LIVE, 초기 참조 해제, init 영역 해제(워크큐로 지연), kallsyms를 core 심볼로 교체 | `main()`이 `xxx_init()` 호출 |

언로드는 `delete_module` syscall → 그림 6의 검사 → `try_stop_module()`(GOING) → `mod->exit()` → `free_module()`(sysfs 제거, 의존 기록 해제, 메모리 반납)이다.

### 13.2 핵심 구조체

| 구조체 / 변수 | 위치 | 이 노트에서 본 모습 |
|---|---|---|
| `struct module` | `include/linux/module.h` | `.gnu.linkonce.this_module` 1152바이트, `THIS_MODULE` |
| `mod->state` | `enum module_state` | init 중 1(COMING), exit 중 2(GOING) |
| `mod->refcnt` | `atomic_t`, `module_refcount()` = 값 − `MODULE_REF_BASE` | `/sys/module/<m>/refcnt`, lsmod 3번째 칸 |
| `mod->source_list` / `target_list` | `struct module_use` 리스트 | `holders/`, lsmod "Used by" 이름 |
| `mod->mem[MOD_TEXT … MOD_INIT_TEXT …]` | 영역별 메모리 | `coresize`, `initsize` |
| `mod->kp`, `num_kp` | `struct kernel_param` 배열 (`__param` 섹션) | `/sys/module/<m>/parameters/` |
| `struct kernel_symbol` | `__ksymtab*` 섹션 | `/proc/kallsyms`의 `__ksymtab_e3_lib_add` |
| `mod->taints` | 비트마스크 | lsmod의 `(PO)` |

### 13.3 드라이버를 쓸 때 다시 만나는 지점

- **`.owner = THIS_MODULE`** — `file_operations`(G4 문자 디바이스, G7 misc device), `struct platform_driver`의 `driver.owner`(`platform_driver_register` 매크로가 자동으로 채운다, G3), `struct i2c_driver`(H5) 등. 이 필드가 refcount 보호의 시작점이다.
- **`module_platform_driver()` / `module_i2c_driver()` / `module_misc_device()`** — init/exit 보일러플레이트를 만드는 매크로. 속은 이 노트의 `module_init`/`module_exit`다(D3, G3).
- **`MODULE_DEVICE_TABLE`** — `alias:`를 만들어 depmod·modprobe 자동 로딩으로 이어진다(D3 §9, G9).
- **`EXPORT_SYMBOL_GPL`** — 드라이버가 쓰는 커널 API(`devm_*`, `regmap_*`, `iio_*`) 대부분이 GPL 전용이다. 그래서 제품 드라이버는 사실상 GPL이어야 한다(E7).
- **에러 경로** — `probe()`의 `goto`/`devm` 정리(E5, G3), `-EPROBE_DEFER`(D3 §10).
- **`__init`/`__initdata`** — probe에서 쓰는 함수에 `__init`을 붙이면 안 된다. 핫플러그나 deferred probe로 init 이후에 probe가 불릴 수 있다. modpost의 section mismatch 경고가 이걸 잡는다.

---

## 14. Embedded 실무에서 다시 보기

### 14.1 장면 1 — 벤더 IMU 드라이버를 BSP 커널에 맞춰 빌드

센서 키트 보드(i.MX8)에 벤더가 IMU 드라이버를 `bmi_vendor/` 소스로 줬다. 순서는 이 노트 그대로다.

```sh
# Yocto SDK 또는 BSP 커널 빌드 트리를 대상으로
make -C ~/bsp/build/tmp/work-shared/imx8mp/kernel-build-artifacts \
     M=$PWD ARCH=arm64 CROSS_COMPILE=aarch64-poky-linux- modules
modinfo bmi_vendor.ko | grep -E "vermagic|depends|license|alias"
```

`vermagic`이 보드의 `uname -r`과 같은지, `depends`에 보드에 없는 모듈이 있는지(`industrialio`, `regmap-i2c` 등은 보드 커널에 `=m`인지 `=y`인지), `license`가 GPL인지(아니면 GPL 전용 API를 쓸 수 없다), `alias`에 우리 DT의 `compatible`이 있는지를 확인한다. Yocto라면 이걸 `inherit module` 레시피로 만들어 `modules_install`·`depmod`·서명까지 빌드 시스템이 하게 한다(K 모듈).

### 14.2 장면 2 — "현장 장비에서 드라이버가 안 올라와요"

| 증상 (dmesg) | 원인 | 대응 |
|---|---|---|
| `version magic '6.6.23-g1a2b' should be '6.6.23-g9f8e'` | 커널을 다시 빌드했는데 모듈은 옛 것 | 커널과 모듈을 한 빌드에서 함께 배포 (OTA 패키지에 묶기) |
| `Unknown symbol regmap_init_i2c (err -2)` | 의존 모듈을 안 넣었거나 `insmod`로 순서를 틀림 | `modprobe` 사용, `depmod` 재실행 |
| `Unknown symbol` + 라이선스가 GPL 아님 | 벤더 바이너리 `.ko`가 GPL 전용 API를 씀 | 벤더에 GPL 빌드 요청 |
| `module verification failed` 또는 로드 거부 | 서명 키 불일치 | 빌드 서버 키로 재서명 |
| `disagrees about version of symbol` | modversions CRC 불일치 = 커널 구조체가 바뀜 | 같은 빌드 트리로 재빌드 |

### 14.3 장면 3 — 파라미터로 현장 튜닝

드론의 IMU 드라이버에 `module_param_cb(odr_hz, …, 0644)`를 두면, 비행 로그를 보면서 `echo 400 > /sys/module/bmi_vendor/parameters/odr_hz`로 재부팅 없이 샘플링 주기를 바꿔 볼 수 있다. 단 콜백에서 범위를 검사하고(§5의 `rate_hz`), 값이 바뀌면 하드웨어 레지스터에 실제로 반영하는 코드까지 있어야 한다 — 전역 변수만 바꾸고 레지스터는 그대로인 "가짜 튜닝"이 흔한 버그다. 오래 쓸 설정이면 파라미터보다 sysfs 장치 속성(G8)이나 IIO 속성(I 모듈)이 맞는 자리다.

### 14.4 장면 4 — 부팅 시간과 built-in 선택

카메라(V4L2) 드라이버를 `=m`으로 두면 부팅 후 udev가 modalias로 올리는 데 수백 ms가 더 걸린다. 차량용 후방 카메라처럼 "전원 켜고 2초 안에 영상"이 요구되면 그 드라이버만 `=y`로 바꾸고, 나머지는 모듈로 둔다. 반대로 루트 파일시스템이 있는 eMMC 드라이버를 `=m`으로 두면 rootfs를 못 읽어 initramfs가 필요해진다(C7). 결정 기준: **rootfs 마운트 전에 필요하거나 부팅 시간 예산 안에 꼭 동작해야 하면 `=y`, 나머지는 `=m`.**

---

## 15. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `module_exit` 빠뜨림 | lsmod에 `[permanent]`, rmmod `Device or resource busy` | init만 있고 exit 없으면 `-EBUSY` | exit 함수 추가 (재부팅 필요) |
| init에서 실패 시 정리 안 함 | 두 번째 insmod에서 `misc_register` `-EBUSY`, 메모리 누수 | init 실패 시 exit은 안 불린다 | `goto` 사다리로 역순 정리 |
| init이 `1` 반환 | `suspiciously returned 1` 경고 후 성공 처리 | 0/-errno 규약 위반 | 성공 0, 실패 `-ENODEV` 등 |
| 의존 모듈 먼저 안 넣음 | `Unknown symbol xxx (err -2)` | 심볼 표에 아직 없음 | `modprobe` + `depmod` |
| `KBUILD_EXTRA_SYMBOLS` 누락 | 빌드 때 `ERROR: modpost: "xxx" undefined!` | modpost가 다른 out-of-tree 모듈의 Module.symvers를 모름 | 경로 지정 또는 같은 Makefile로 빌드 |
| 6.13+에서 `MODULE_IMPORT_NS(NS)` | `expected ‘,’ or ‘;’ before ‘NS’` | 인자가 문자열 리터럴로 바뀜 | `MODULE_IMPORT_NS("NS")` |
| 다른 트리로 빌드 | `invalid module format` + dmesg `version magic` | vermagic 불일치 | 대상 커널 빌드 트리로 재빌드 |
| `fops.owner` 빠뜨림 | 파일이 열려 있어도 rmmod 성공 → 다음 read에서 Oops | refcount가 안 오름 | `.owner = THIS_MODULE` |
| 파라미터 오타 | 조용히 기본값으로 동작 | `unknown parameter ignored` 경고만 | dmesg 확인, 중요한 값은 콜백에서 검증 |
| `__init` 함수를 콜백으로 등록 | init 이후 호출 시 Oops (해제된 메모리) | init 영역은 로드 직후 해제 | `__init` 제거, section mismatch 경고 확인 |

---

## 16. 면접에서 이렇게 말한다

**Q.** Write a minimal "hello world" kernel module on the whiteboard and explain each line.

**A.** 헤더 두 개(`linux/module.h`, `linux/init.h`), `__init`이 붙은 `static int` init 함수가 `pr_info` 후 0을 반환, `__exit`이 붙은 `static void` exit 함수, `module_init`/`module_exit`로 등록, `MODULE_LICENSE("GPL")`과 `MODULE_DESCRIPTION`. 그리고 `obj-m := hello.o`와 `make -C <커널 빌드 트리> M=$PWD modules` Makefile까지 쓴다. 각 줄의 의미: `__init`은 로드 후 메모리 해제, 반환값은 0 또는 음수 errno, 라이선스는 GPL 전용 심볼 접근과 taint를 결정한다.

```c
#include <linux/module.h>
#include <linux/init.h>

static int __init hello_init(void)
{
	pr_info("hello\n");
	return 0;
}

static void __exit hello_exit(void)
{
	pr_info("bye\n");
}

module_init(hello_init);
module_exit(hello_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("hello");
```

> Two headers, an init function marked __init that returns 0 on success or a negative errno, an exit function marked __exit, module_init and module_exit to register them, and MODULE_LICENSE. The Makefile is just obj-m := hello.o, and I build it with make -C pointing at the target kernel's build tree and M equal to the current directory. __init puts the function in .init.text, which the kernel frees right after init succeeds. The license matters: without a GPL-compatible one, modpost refuses GPL-only symbols and the kernel is tainted. On a 6.18 kernel a missing MODULE_LICENSE is actually a build error.

**꼬리 질문**: "exit을 안 쓰면?" → 로드는 되지만 `[permanent]`가 되어 rmmod가 `-EBUSY`로 거절된다.

**Q.** What is the difference between insmod and modprobe?

**A.** `insmod`는 파일 경로 하나를 받아 그 모듈만 넣는다. 의존성을 모른다. `modprobe`는 이름(또는 alias)을 받아 `/lib/modules/$(uname -r)`에서 찾고, depmod가 만든 `modules.dep`을 보고 의존 모듈을 먼저 넣는다. `/etc/modprobe.d`의 options·blacklist도 적용하고, kmod 버전은 커널 명령줄의 `mod.param=`도 읽는다. udev 자동 로딩도 modalias로 modprobe를 부른다.

> insmod takes a path and loads exactly that file; it knows nothing about dependencies, so if a symbol provider isn't loaded you get "Unknown symbol". modprobe takes a module name or an alias, looks it up under /lib/modules for the running kernel, and uses the modules.dep index built by depmod to load dependencies first. It also applies options and blacklists from /etc/modprobe.d. udev uses modprobe with the device's modalias for automatic loading.

**Q.** What happens inside the kernel when you insmod a module?

**A.** `finit_module`(또는 `init_module`) → `load_module()`. 서명 검사, ELF 검증, vermagic 비교, core·init 영역을 따로 할당해 섹션 복사, UNFORMED로 목록 등록, taint 판정, 미해결 심볼을 커널·모듈 심볼 표에서 해석(GPL·네임스페이스 검사, 의존 기록), 재배치 적용, 메모리 권한 설정(text RO+X, data NX), 파라미터 파싱, sysfs 생성, 마지막으로 `do_init_module()`이 init을 부르고 성공하면 LIVE로 바꾸고 init 영역을 해제한다. 요컨대 커널 안에 있는 런타임 링커 + 로더다.

> The syscall goes into load_module. It checks the signature if enabled, validates the ELF, compares vermagic, lays out and allocates separate core and init regions, adds the module to the list as UNFORMED, applies taints, resolves undefined symbols against the kernel and other modules' export tables — checking GPL-only and namespaces and recording the dependency — applies relocations, sets memory permissions, parses parameters, creates the sysfs directory, and finally do_init_module calls the init function. On success the state becomes LIVE and the init sections are freed. It's basically a runtime linker and loader inside the kernel.

**Q.** EXPORT_SYMBOL vs EXPORT_SYMBOL_GPL — and what are symbol namespaces?

**A.** 둘 다 심볼을 커널 심볼 표에 올려 다른 모듈이 링크하게 한다. `_GPL`은 GPL 호환 라이선스 모듈만 쓸 수 있다. 빌드 때 modpost가 에러로 막고, 런타임에도 비GPL 모듈에는 그 심볼이 안 보여 `Unknown symbol`이 된다 — 직접 확인했다. 네임스페이스는 `EXPORT_SYMBOL_NS_GPL(f, "NS")`로 내보내고 쓰는 쪽이 `MODULE_IMPORT_NS("NS")`를 선언해야 하는 장치로, 서브시스템 내부 API가 아무 드라이버에서나 쓰이는 걸 막는다. 6.13부터 인자가 문자열 리터럴이다.

> Both put the symbol into the kernel's export table so other modules can link against it. The GPL variant is only usable by modules with a GPL-compatible license — modpost rejects it at build time, and at load time the symbol is simply invisible to a non-GPL module, so you get Unknown symbol. Namespaces add another gate: the exporter uses EXPORT_SYMBOL_NS_GPL with a namespace string, and the user must declare MODULE_IMPORT_NS with the same string, otherwise modpost fails. Since 6.13 the namespace argument is a string literal.

**Q.** How does the kernel prevent a module from being unloaded while it's in use?

**A.** 모듈 refcount다. 다른 모듈이 내 심볼을 쓰면 로드 때 `ref_module()`이 refcount를 올리고 holders에 기록한다. 장치 파일을 열면 VFS가 `fops_get()`으로 `try_module_get(fops->owner)`를 한다 — 그래서 `.owner = THIS_MODULE`이 중요하다. `delete_module()`은 holders가 있거나 refcount가 0이 아니면 `EAGAIN`, exit 함수가 없으면 `EBUSY`로 거절한다. kmod rmmod는 이걸 "Module is in use"로 보여 준다.

> With the module reference count. When another module links against my symbols, ref_module takes a reference and records it in holders. When user space opens my device file, the VFS calls try_module_get on fops->owner — which is why .owner = THIS_MODULE matters. delete_module refuses with EAGAIN if there are holders or a non-zero refcount, and with EBUSY if the module has an init but no exit. If I hold a function pointer from another module for later use, I wrap the call in try_module_get and module_put on that module's owner.

**꼬리 질문**: "`try_module_get`이 실패하는 경우는?" → 그 모듈이 GOING(언로드 중)일 때. 그래서 try다.

**Q.** Your module's init fails halfway through. What happens, and how do you write it?

**A.** init이 음수 errno를 돌려주면 커널은 상태를 GOING으로 바꾸고 바로 free한다. **exit은 불리지 않는다.** 그래서 init 안에서 이미 획득한 자원을 역순으로 풀어야 하고, 커널 관용구는 `goto` 라벨 사다리다. 반환한 errno는 insmod 메시지가 된다(`-ENODEV` → "No such device"). 드라이버 probe에서는 devm 자원으로 사다리를 줄일 수 있다.

> The kernel marks it GOING and frees it immediately, and the exit function is not called. So init must undo whatever it already acquired, in reverse order. The idiom is a goto ladder: each failure jumps to the label that releases everything acquired before it. The negative errno propagates to user space, so -ENODEV shows up as "No such device" in insmod. In probe functions I prefer devm-managed resources, which the driver core releases automatically.

**Q.** What does "tainted kernel" mean? What are O, P and E?

**A.** 커널이 "지원 범위 밖의 일이 있었다"고 기록하는 비트마스크로, `/proc/sys/kernel/tainted`와 Oops의 `Tainted:` 줄에 나온다. O는 트리 밖에서 빌드된 모듈(`intree` 없음), P는 GPL 호환이 아닌 라이선스(P가 붙으면 lockdep도 꺼진다), E는 서명 검증이 켜진 커널에서 서명 없는 모듈. 고장이라는 뜻이 아니라, 버그 리포트를 볼 때 "재현 가능한 소스만으로 이뤄진 커널인가"를 알려 주는 이력이다.

> It's a bitmask recording that something outside the supported envelope happened, shown in /proc/sys/kernel/tainted and in the Tainted line of an oops. O means an out-of-tree module was loaded, P a module with a non-GPL-compatible license — which also disables lock debugging — and E an unsigned module on a kernel with signature checking. It doesn't mean the kernel is broken; it tells whoever reads the bug report that code without available source or outside mainline was involved.

**Q.** Built-in versus loadable module — how do you decide, and what changes in the code?

**A.** 코드는 같다. `=y`면 `module_init`이 `device_initcall`이 되어 부팅 중에 불리고, exit 코드는 버려지며, 파라미터는 커널 명령줄 `name.param=`으로 준다. `=m`이면 insmod 때 불리고 rmmod로 뺄 수 있다. rootfs 마운트 전에 필요하거나(스토리지, 콘솔, 인터럽트 컨트롤러) 부팅 시간 예산 안에 꼭 떠야 하는 건 built-in, 나머지는 모듈로 둬서 이미지를 작게 하고 개발 중 교체를 쉽게 한다.

> The source is the same. Built in, module_init becomes a device_initcall that runs during boot, the exit path is discarded, and parameters come from the kernel command line as module.param. As a module, init runs at insmod and the code can be unloaded and replaced. Anything needed before the root filesystem is mounted — storage, console, the interrupt controller — or anything with a hard boot-time deadline goes built in; the rest stays modular for a smaller image and faster iteration.

---

## 17. 직접 해보기

1. (개념) `e3_hello`를 로드한 뒤 `/sys/module/e3_hello/initsize`가 0인 이유와 kallsyms에 `e3_hello_init`이 없는 이유를 한 문단으로 설명하라. 정답/힌트: `do_init_module()`이 init 성공 후 init 영역을 해제하고 kallsyms를 core 심볼로 바꾼다.
2. (코드) `e3_param`에 `module_param_named(debug, e3_dbg, int, 0644)`를 추가하고, `echo 2 > /sys/module/e3_param/parameters/debug`로 바꾼 값이 exit 로그에 보이게 하라. 이름과 변수 이름이 다른 것을 `modinfo`로 확인하라. 정답/힌트: `parm: debug:...`로 보이고 C 변수는 `e3_dbg`다.
3. (코드) `e3_lib`와 `e3_user`를 한 디렉터리·한 Makefile(`obj-m := e3_lib.o e3_user.o`)로 옮겨 `KBUILD_EXTRA_SYMBOLS` 없이 빌드되는지 확인하라. 정답/힌트: modpost가 한 번에 두 모듈을 보므로 된다. `modinfo e3_user.ko`의 `depends`는 그대로 `e3_lib`다.
4. (관찰) `e3_hold`의 `.owner = THIS_MODULE` 줄을 지우고 다시 빌드해, fd 3을 연 상태에서 `rmmod e3_hold`가 성공하는지 보라. 그다음 `exec 3<&-`를 하면 무슨 일이 생기나? 정답/힌트: rmmod는 성공하고, 닫을 때 해제된 모듈의 `.release`를 부르며 Oops가 날 수 있다. 이게 owner 필드가 있는 이유다.
5. (관찰) 같은 이름의 심볼(`e3_lib_add`)을 내보내는 두 번째 모듈 `e3_lib2`를 만들어 `e3_lib` 다음에 넣어 보라. dmesg 메시지와 errno는? 정답/힌트: `complete_formation()` → `verify_exported_symbols()`가 `e3_lib2: exports duplicate symbol e3_lib_add (owned by e3_lib)`를 찍고 `-ENOEXEC`(insmod: invalid module format)로 실패시킨다.
6. (dyndbg) 예제 9처럼 `file kernel/module/main.c +p`를 켜고 `rmmod e3_user`를 해서 `unusing` 줄을 찾아라. 그 줄을 찍는 함수는? 정답/힌트: `module_unload_free()`가 의존 기록마다 `pr_debug("%s unusing %s\n")` 후 `module_put()`한다.
7. (설계) 센서 허브 코어 모듈이 하위 IMU 드라이버 모듈의 `ops`를 보관하는 구조를 그려 보고, 어디서 `try_module_get`/`module_put`을 해야 하는지 표시하라. 정답/힌트: ops를 **호출하는 구간**에서 `ops->owner`를 잡는다. 하위 드라이버가 자신을 잡는 것은 틀린 설계다.

---

## 18. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| LKM | Loadable Kernel Module | 실행 중인 커널에 런타임 링크되는 `.ko` |
| `.ko` | kernel object | 재배치 가능한 ELF + `.modinfo` + `__this_module` |
| out-of-tree | 커널 소스 밖 | `M=`로 빌드한 모듈. taint O |
| Kbuild | 커널 빌드 시스템 | `obj-m`, `<m>-y` 문법을 읽는다 |
| modpost | 모듈 후처리 도구 | 심볼·라이선스·네임스페이스 검사, `.mod.c`와 `Module.symvers` 생성 |
| `Module.symvers` | 내보낸 심볼 목록 | CRC, 심볼, 모듈, 종류, 네임스페이스 |
| `KBUILD_EXTRA_SYMBOLS` | 추가 symvers 경로 | 다른 out-of-tree 모듈 심볼을 modpost에 알림 |
| vermagic | 버전 매직 문자열 | 커널 릴리스 + 핵심 설정. 다르면 로드 거부 |
| modversions | 심볼별 CRC | `CONFIG_MODVERSIONS`. 구조체 변경을 잡는다 |
| `EXPORT_SYMBOL(_GPL)` | 심볼 공개 | `__ksymtab(_gpl)`에 등록 |
| symbol namespace | 심볼 이름 공간 | `EXPORT_SYMBOL_NS` + `MODULE_IMPORT_NS("NS")` |
| `__init` / `__exit` | 섹션 표시 | init 후 해제 / built-in이면 버림 |
| refcount | 모듈 참조 수 | 0이 아니면 언로드 불가 |
| holders | 나를 쓰는 모듈 | `/sys/module/<m>/holders/`, lsmod Used by |
| `try_module_get` | 참조 획득 시도 | GOING이면 실패 |
| taint | 오염 플래그 | O(트리 밖), P(비GPL), E(무서명) 등 |
| depmod | 의존성 색인 생성기 | `modules.dep`(kmod), `modules.dep.bb`(BusyBox) |
| modprobe | 이름으로 로드 | 의존 모듈 먼저, options/blacklist 적용 |
| `finit_module` | fd로 모듈 로드 syscall | kmod가 쓴다. `init_module`은 버퍼로 |
| built-in | `=y` | `module_init` → `device_initcall`, `lsmod`에 없음 |

---

## 19. 요약 & 체크리스트

모듈은 "실행 중인 커널에 런타임 링크되는 재배치 가능한 ELF"다. 소스는 init/exit 두 함수와 메타데이터 매크로로 이뤄지고, 대상 커널의 빌드 트리로 `make -C <KDIR> M=$PWD`해서 만든다. 빌드 때 modpost가 심볼 존재·GPL·네임스페이스·라이선스를 미리 검사하고, 로드 때 `load_module()`이 vermagic·서명을 확인한 뒤 메모리를 잡고 심볼을 해석·재배치하고 `init`을 부른다. 모듈 사이 함수 호출은 `EXPORT_SYMBOL(_GPL/_NS)`로만 되며, 의존 관계와 열린 파일은 refcount로 언로드를 막는다. init이 실패하면 exit은 불리지 않으니 `goto` 사다리로 스스로 정리한다. `modprobe`는 depmod 색인으로 의존 모듈을 먼저 넣는다. 같은 소스를 `=y`로 빌드하면 `module_init`은 initcall이 되고 파라미터는 커널 명령줄로 간다.

- [ ] hello 모듈과 Makefile을 보지 않고 쓸 수 있다
- [ ] `modinfo`의 vermagic, depends, srcversion, parm, alias가 어디서 오는지 설명할 수 있다
- [ ] `__init`/`__exit`이 어느 섹션에 들어가고 언제 해제되는지 말할 수 있다
- [ ] 모듈 파라미터를 타입·권한·콜백으로 정의하고 sysfs로 바꿀 수 있다
- [ ] init 실패 경로를 `goto` 사다리로 짜고, 실패 시 exit이 안 불린다는 것을 설명할 수 있다
- [ ] 두 모듈 사이에 `EXPORT_SYMBOL_GPL`/`EXPORT_SYMBOL_NS_GPL`로 함수를 공유하고 `KBUILD_EXTRA_SYMBOLS`로 빌드할 수 있다
- [ ] "Unknown symbol", "invalid module format", "Module is in use"를 각각 원인별로 진단할 수 있다
- [ ] `.owner = THIS_MODULE`과 `try_module_get`/`module_put`이 왜 필요한지 설명할 수 있다
- [ ] `/proc/sys/kernel/tainted`를 비트로 해독하고 O, P, E의 의미를 말할 수 있다
- [ ] `insmod`와 `modprobe`, BusyBox와 kmod의 차이를 말할 수 있다
- [ ] `finit_module` → `load_module` → `do_init_module`의 주요 단계를 순서대로 말할 수 있다
- [ ] 드라이버를 `=y`로 할지 `=m`으로 할지 근거를 들어 정할 수 있다

## 참고 자료

- docs.kernel.org — Building External Modules: https://docs.kernel.org/kbuild/modules.html
- docs.kernel.org — Symbol Namespaces: https://docs.kernel.org/core-api/symbol-namespaces.html
- docs.kernel.org — Tainted kernels: https://docs.kernel.org/admin-guide/tainted-kernels.html
- docs.kernel.org — Kernel module signing facility: https://docs.kernel.org/admin-guide/module-signing.html
- man7.org — init_module(2) / finit_module(2): https://man7.org/linux/man-pages/man2/init_module.2.html
- man7.org — delete_module(2): https://man7.org/linux/man-pages/man2/delete_module.2.html
- 랩 소스 `/var/lab/src/linux-6.18.55`: `kernel/module/main.c`(load_module, do_init_module, delete_module), `kernel/module/signing.c`, `include/linux/module.h`, `include/linux/moduleparam.h`, `include/linux/export.h`, `kernel/panic.c`(taint_flags), `scripts/mod/modpost.c`, `scripts/module-common.c` — 같은 파일을 https://elixir.bootlin.com/linux/v6.18/source 에서 볼 수 있다
- BusyBox 1.37 `modutils/modprobe-small.c` (랩 `/var/lab/src/busybox-1.37.0`)
- Bootlin, "Linux kernel and driver development" 교육 슬라이드 — 모듈 장: https://bootlin.com/doc/training/linux-kernel/
- The Linux Kernel Module Programming Guide (sysprog21): https://sysprog21.github.io/lkmpg/
