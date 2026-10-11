# E5. 커널 C 관용구 — container_of · list_head · ERR_PTR · goto 정리 · __iomem/__user · sparse

> **이 노트를 다 읽으면**: `container_of`를 `offsetof`에서 유도해 화이트보드에 쓰고 커널판(타입 검사 포함)과 비교할 수 있다 · `list_head` intrusive 리스트를 직접 구현하고 `list_for_each_entry_safe`가 왜 필요한지 oops 주소(`dead0000000000fc`)로 설명할 수 있다 · `ERR_PTR`/`IS_ERR`/`PTR_ERR`의 인코딩을 숫자로 말하고 `goto` 사다리 → `devm_*` → `__free()`/`guard()`로 이어지는 에러 정리 방식을 고를 수 있다 · `BIT`/`GENMASK`/`FIELD_PREP`, 엔디안 헬퍼, `__iomem`/`__user`/`__bitwise`를 쓰고 `make C=2`(sparse)로 검사할 수 있다
> **왜 필요한가**: 커널 코딩 면접은 "container_of 직접 구현해 보세요", "링크드 리스트 순회하면서 지우는 코드 써 보세요", "probe 에러 처리 어떻게 하나요?"로 시작한다. 실무에서 드라이버 코드 한 함수를 읽으려면 이 어휘를 전부 알아야 한다
> **Don 기준 난이도**: 포인터 산술, 구조체 레이아웃, 링크드 리스트, 비트 마스크, 엔디안, 에러 코드 전파는 SSD 펌웨어에서 매일 했다 / 새로 배울 부분은 커널이 이걸 **어떤 이름과 어떤 매크로로 표준화했는지**, GCC 확장(`typeof`, statement expression, `cleanup` 속성)을 어떻게 쓰는지, sparse라는 별도 검사기
> **선행 노트**: E3(커널 모듈 빌드·로드), E4(커널 프로그래밍 제약) — 모듈을 빌드해 본 적이 없다면 E3을 먼저 본다. D8의 `_scoped` 매크로가 이 노트 §4의 `__free()`와 같은 원리다

---

## 0. 큰 그림 — 이게 왜 필요한가

Don이 SSD 펌웨어에서 쓰던 C와 커널 C는 같은 언어다. 그런데 커널 드라이버 한 파일을 처음 열면 `container_of(...)`, `list_for_each_entry_safe(...)`, `if (IS_ERR(p)) return PTR_ERR(p);`, `goto err_free;`, `FIELD_PREP(...)`, `void __iomem *` 같은 낯선 "단어"가 줄마다 나온다. 이건 새 문법이 아니라 **커널 커뮤니티가 30년 동안 표준화한 관용구(idiom)** 다. 펌웨어 팀마다 `GET_FIELD()`, `LIST_FOR_EACH()`, `RET_IF_ERR()` 같은 사내 매크로가 있었을 텐데, 커널은 그걸 전부 하나로 통일해 두었다고 보면 된다.

이 관용구들은 네 무리로 나뉜다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="22" text-anchor="middle" font-size="13">bare-metal C + 커널 관용구 = 드라이버 코드를 읽고 쓰는 어휘</text>
  <rect x="10" y="40" width="325" height="110" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="172" y="64" text-anchor="middle" font-size="14">① 객체 잇기 (§1, §2)</text>
  <text x="172" y="90" text-anchor="middle" font-size="12">container_of · list_head · hlist · kref</text>
  <text x="172" y="112" text-anchor="middle" font-size="12">멤버 포인터 하나로 바깥 구조체를 찾는다</text>
  <text x="172" y="134" text-anchor="middle" font-size="12">링크를 객체 안에 내장 (intrusive)</text>
  <rect x="345" y="40" width="325" height="110" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="507" y="64" text-anchor="middle" font-size="14">② 에러 전달과 정리 (§3, §4)</text>
  <text x="507" y="90" text-anchor="middle" font-size="12">ERR_PTR · IS_ERR · PTR_ERR</text>
  <text x="507" y="112" text-anchor="middle" font-size="12">goto 사다리 · devm_* · __free() · guard()</text>
  <text x="507" y="134" text-anchor="middle" font-size="12">획득의 역순으로 해제</text>
  <rect x="10" y="160" width="325" height="110" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="172" y="184" text-anchor="middle" font-size="14">③ 레지스터와 데이터 (§5)</text>
  <text x="172" y="210" text-anchor="middle" font-size="12">BIT · GENMASK · FIELD_PREP · FIELD_GET</text>
  <text x="172" y="232" text-anchor="middle" font-size="12">cpu_to_le32 · ARRAY_SIZE · min/clamp</text>
  <text x="172" y="254" text-anchor="middle" font-size="12">likely · READ_ONCE · BUILD_BUG_ON</text>
  <rect x="345" y="160" width="325" height="110" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="507" y="184" text-anchor="middle" font-size="14">④ 정적 검사 (§6)</text>
  <text x="507" y="210" text-anchor="middle" font-size="12">__iomem · __user · __bitwise · __force</text>
  <text x="507" y="232" text-anchor="middle" font-size="12">make C=2 → sparse</text>
  <text x="507" y="254" text-anchor="middle" font-size="12">gcc가 못 보는 주소 공간·엔디안 실수</text>
  <text x="340" y="292" text-anchor="middle" font-size="12">모두 런타임 비용 0에 가깝다 — 대부분 컴파일 타임에 상수나 검사로 사라진다</text>
</svg>
```
그림 1 — 이 노트가 다루는 커널 C 관용구의 네 무리. 드라이버 `probe()` 하나에 네 무리가 모두 등장한다.

얼마나 자주 나오는지 랩 소스(Linux 6.18.55)의 `drivers/` 디렉터리에서 세어 봤다.

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/src/linux-6.18.55; for w in "container_of(" "list_for_each_entry(" "list_for_each_entry_safe(" "IS_ERR(" "goto err" "FIELD_PREP(" "__free(" "guard("; do printf "%-28s %s\n" "$w" "$(grep -rF --include=*.c "$w" drivers | wc -l)"; done'
```

```text
container_of(                16021
list_for_each_entry(         6233
list_for_each_entry_safe(    3300
IS_ERR(                      25193
goto err                     39595
FIELD_PREP(                  12563
__free(                      712
guard(                       3006
```

출력에서 볼 것: 숫자는 "그 문자열이 들어간 줄 수"다. `goto err...`와 `IS_ERR(`가 수만 줄 — 드라이버 코드의 상당 부분이 에러 처리다. `__free(`와 `guard(`(여기엔 `scoped_guard(`도 포함)는 6.5에서 들어온 새 방식이라 아직 적지만 빠르게 늘고 있다.

> 실행 환경: 유저스페이스 예제는 Lima VM `elinux`(호스트 커널 `uname -r` = `7.0.0-38-generic`, gcc 15.2, `-std=gnu11 -Wall -Wextra -O2`)에서, 커널 모듈은 공용 랩의 **Linux 6.18.55** 빌드 트리로 컴파일해 QEMU `virt`(cortex-a57, 512 MB, TCG) 게스트에서 insmod했다. sparse는 0.6.4. 아래 출력은 전부 실제 실행 결과이고 주소·타임스탬프는 실행마다 다르다. 모듈은 gcc 경고 **0개**로 빌드했다(§6의 sparse 경고와 §5.8의 컴파일 에러는 일부러 낸 것이다).

### 0.1 실습 준비 — 모듈 빌드와 QEMU 부팅 스크립트

모든 커널 예제는 같은 방식으로 돌린다. 소스는 Mac의 `~/.elinux_scratch/E5/m/`, 빌드는 VM의 `/var/tmp/E5/mod/`. Kbuild 파일 한 줄:

```make
obj-m := e5_cof.o e5_list.o e5_err.o e5_cleanup.o e5_regs.o e5_sparse_ok.o
```

```sh
limactl shell elinux -- bash -lc 'rm -rf /var/tmp/E5/mod && mkdir -p /var/tmp/E5/mod && cp ~/.elinux_scratch/E5/m/* /var/tmp/E5/mod/ && cd /var/tmp/E5/mod && make -C /var/lab/out/linux-build M=$PWD modules'
```

```text
make: Entering directory '/var/lab/build/linux'
make[1]: Entering directory '/var/tmp/E5/mod'
  CC [M]  e5_cof.o
  CC [M]  e5_list.o
  CC [M]  e5_err.o
  CC [M]  e5_cleanup.o
  CC [M]  e5_regs.o
  CC [M]  e5_sparse_ok.o
  MODPOST Module.symvers
  CC [M]  e5_cof.mod.o
  CC [M]  .module-common.o
  LD [M]  e5_cof.ko
  ...
  LD [M]  e5_sparse_ok.ko
make[1]: Leaving directory '/var/tmp/E5/mod'
make: Leaving directory '/var/lab/build/linux'
```

경고 줄이 하나도 없다(중간 `.mod.o`/`.ko` 줄은 생략). 부팅 스크립트는 랩 rootfs를 복사해 `.ko`와 게스트 스크립트 `/e5.sh`를 넣고, `LAB_CMD=sh~/e5.sh`로 그 스크립트를 실행한 뒤 자동으로 전원을 끈다.

```sh
#!/bin/sh
# boot.sh guest_script.sh -> lab kernel 부팅, 스크립트 실행, 콘솔 출력
set -e
W=/var/tmp/E5
rm -rf $W/r && mkdir -p $W/r && cp -a /var/lab/build/rootfs/. $W/r/
cp $W/mod/*.ko $W/r/
cp "$1" $W/r/e5.sh
(cd $W/r && find . | cpio -o -H newc --quiet | gzip) > $W/i.gz
timeout 120 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -kernel /var/lab/out/Image -initrd $W/i.gz \
  -append "console=ttyAMA0 quiet LAB_CMD=sh~/e5.sh" | tr -d '\r'
```

---

## 1. container_of — 멤버 포인터에서 바깥 구조체 찾기

### 1.1 직관: "필드 주소에서 오프셋을 빼면 구조체 시작 주소"

Don이 SSD 펌웨어에서 NAND 명령 큐를 만들 때를 떠올려 보자. 명령 디스크립터 `struct nand_cmd` 안에 큐 링크 필드가 있고, 큐에서 꺼낸 건 링크 필드의 주소다. 이걸 다시 `struct nand_cmd *`로 바꾸려면? 링크 필드가 구조체 첫 멤버라면 그냥 캐스트하면 되고, 아니라면 "링크 필드 주소 − 링크 필드의 오프셋"을 계산해야 한다. FreeRTOS는 이 문제를 `ListItem_t` 안에 `pvOwner`라는 **역포인터**를 하나 더 둬서 풀었다(포인터 8바이트를 더 쓰는 대신 계산이 없다). 리눅스는 역포인터 없이 **컴파일 타임 상수 오프셋 빼기**로 푼다. 그게 `container_of`다.

정의를 한 문장으로: `container_of(ptr, type, member)`는 "`ptr`이 `type` 구조체 안의 `member` 필드를 가리킨다면, 그 `type` 구조체의 시작 주소"를 돌려준다.

### 1.2 offsetof에서 유도하기

`offsetof(type, member)`는 C 표준(`<stddef.h>`) 매크로로, 구조체 시작에서 멤버까지의 바이트 수를 컴파일 타임 상수로 준다. 그러면:

```text
&obj.member  ==  (char *)&obj + offsetof(type, member)
=>  &obj     ==  (char *)&obj.member - offsetof(type, member)
```

`char *`로 바꾸는 이유는 포인터 산술 단위를 1바이트로 만들기 위해서다(`struct device *`에서 8을 빼면 `8 × sizeof(struct device)`바이트가 빠진다). 화이트보드용 최소판은 한 줄이다.

```c
#define container_of(ptr, type, member) \
	((type *)((char *)(ptr) - offsetof(type, member)))
```

```svg
<svg viewBox="0 0 680 260" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="340" y="22" text-anchor="middle" font-size="13">container_of = 멤버 주소 − 멤버 오프셋 (오프셋은 컴파일 타임 상수)</text>
  <rect x="150" y="40" width="220" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="158" y="59" font-size="12">+0</text>
  <text x="270" y="59" text-anchor="middle" font-size="13">u32 odr_hz</text>
  <rect x="150" y="68" width="220" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="158" y="87" font-size="12">+4</text>
  <text x="270" y="87" text-anchor="middle" font-size="13">u8 range_g + pad</text>
  <rect x="150" y="96" width="220" height="84" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="158" y="115" font-size="12">+8</text>
  <text x="270" y="134" text-anchor="middle" font-size="13">struct device dev</text>
  <text x="270" y="154" text-anchor="middle" font-size="12">(16바이트, 내장)</text>
  <rect x="150" y="180" width="220" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="158" y="199" font-size="12">+24</text>
  <text x="270" y="199" text-anchor="middle" font-size="13">int irq</text>
  <rect x="150" y="208" width="220" height="20" fill="#888" fill-opacity="0.06" stroke="#888"/>
  <text x="158" y="222" font-size="12">+28</text>
  <text x="270" y="222" text-anchor="middle" font-size="12">pad (총 32)</text>
  <text x="140" y="59" text-anchor="end" font-size="12">0xfffffd8bb898</text>
  <text x="140" y="115" text-anchor="end" font-size="12">0xfffffd8bb8a0</text>
  <rect x="425" y="100" width="245" height="36" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="547" y="123" text-anchor="middle" font-size="13">콜백이 받은 것: struct device *d</text>
  <line x1="425" y1="112" x2="374" y2="100" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="425" y="160" width="245" height="56" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="547" y="182" text-anchor="middle" font-size="13">d − offsetof(imu_priv, dev)</text>
  <text x="547" y="204" text-anchor="middle" font-size="13">= …8a0 − 8 = …898</text>
  <path d="M425,188 C395,188 400,44 374,44" fill="none" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="547" y="240" text-anchor="middle" font-size="12">arm64 코드: sub x0, x0, #0x8 한 줄</text>
</svg>
```
그림 2 — 예제 1의 실제 레이아웃과 주소. 콜백은 내장된 `dev`의 주소(`…8a0`)만 받지만, `dev`의 오프셋 8을 빼면 바깥 `struct imu_priv`(`…898`)가 나온다.

### 1.3 예제 1 — userspace에서 container_of 만들기 (타입 검사 포함)

무엇을 확인하나: 커널 정의를 흉내 낸 `container_of`로 콜백이 받은 `struct device *`에서 드라이버의 private 구조체를 되찾고, 타입이 틀리면 컴파일이 실패하는지 본다.

```c
/* cof.c — container_of 를 offsetof 로부터 직접 만들기 (userspace) */
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#define __same_type(a, b) __builtin_types_compatible_p(typeof(a), typeof(b))
#define container_of(ptr, type, member) ({                          \
	void *__mptr = (void *)(ptr);                                   \
	_Static_assert(__same_type(*(ptr), ((type *)0)->member),        \
		       "pointer type mismatch in container_of()");      \
	((type *)((char *)__mptr - offsetof(type, member))); })

struct device { const char *name; int id; };      /* 흉내낸 struct device */

struct imu_priv {                 /* 드라이버의 private 구조체 */
	uint32_t odr_hz;
	uint8_t  range_g;
	struct device dev;        /* 포인터가 아니라 '내장(embedded)' */
	int      irq;
};

static void show(struct device *d)  /* 콜백은 struct device * 만 받는다 */
{
	struct imu_priv *p = container_of(d, struct imu_priv, dev);
	printf("callback got dev=%p -> priv=%p odr=%u irq=%d\n",
	       (void *)d, (void *)p, p->odr_hz, p->irq);
}

int main(void)
{
	struct imu_priv imu = { .odr_hz = 1000, .range_g = 8,
				.dev = { "imu0", 0 }, .irq = 42 };
	printf("sizeof(struct imu_priv) = %zu\n", sizeof(imu));
	printf("offsetof(odr_hz)=%zu range_g=%zu dev=%zu irq=%zu\n",
	       offsetof(struct imu_priv, odr_hz), offsetof(struct imu_priv, range_g),
	       offsetof(struct imu_priv, dev), offsetof(struct imu_priv, irq));
	printf("&imu=%p &imu.dev=%p diff=%td\n", (void *)&imu, (void *)&imu.dev,
	       (char *)&imu.dev - (char *)&imu);
	show(&imu.dev);
#ifdef BAD
	int *ip = &imu.irq;
	struct imu_priv *q = container_of(ip, struct imu_priv, dev); /* 틀린 타입 */
	(void)q;
#endif
	return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'mkdir -p /var/tmp/E5/u && cd /var/tmp/E5/u && gcc -std=gnu11 -Wall -Wextra -O2 ~/.elinux_scratch/E5/u/cof.c -o cof && ./cof; echo "--- BAD build"; gcc -std=gnu11 -Wall -Wextra -O2 -DBAD ~/.elinux_scratch/E5/u/cof.c -o cofbad'
```

```text
sizeof(struct imu_priv) = 32
offsetof(odr_hz)=0 range_g=4 dev=8 irq=24
&imu=0xfffffd8bb898 &imu.dev=0xfffffd8bb8a0 diff=8
callback got dev=0xfffffd8bb8a0 -> priv=0xfffffd8bb898 odr=1000 irq=42
--- BAD build
/Users/donh/.elinux_scratch/E5/u/cof.c: In function ‘main’:
/Users/donh/.elinux_scratch/E5/u/cof.c:9:9: error: static assertion failed: "pointer type mismatch in container_of()"
    9 |         _Static_assert(__same_type(*(ptr), ((type *)0)->member),        \
      |         ^~~~~~~~~~~~~~
/Users/donh/.elinux_scratch/E5/u/cof.c:42:30: note: in expansion of macro ‘container_of’
   42 |         struct imu_priv *q = container_of(ip, struct imu_priv, dev); /* 틀린 타입 */
      |                              ^~~~~~~~~~~~
```

출력에서 볼 것: `range_g`는 오프셋 4, `dev`는 8 — `struct device`가 포인터를 담고 있어 8바이트 정렬이 필요하므로 `range_g` 뒤에 3바이트 패딩이 들어갔다. 콜백은 `…8a0`만 받았는데 `…898`을 정확히 되찾았다. `int *`를 `struct device` 멤버 자리에 넘기자 **컴파일 에러**가 났다 — 최소판 한 줄 매크로라면 조용히 틀린 주소를 만들었을 것이다.

매크로에 쓰인 GCC 확장 세 가지:

| 확장 | 하는 일 | 여기서 쓰는 이유 |
|---|---|---|
| `({ ... })` statement expression | 블록의 마지막 식이 전체 값 | 임시 변수(`__mptr`)를 두고 정적 검사도 넣기 위해 |
| `typeof(x)` | 식의 타입 | 멤버 타입과 `*ptr` 타입 비교 |
| `__builtin_types_compatible_p(T1, T2)` | 두 타입이 같으면 1 (컴파일 타임) | `static_assert`의 조건 |

`((type *)0)->member`는 "0번지에 `type`이 있다고 치고 그 멤버"라는 식인데, `typeof` 안에서만 쓰이므로 실제로 0번지를 읽지 않는다(평가되지 않는 문맥).

### 1.4 예제 2 — 런타임 비용은 빼기 한 번

무엇을 확인하나: `container_of`가 기계어로 무엇이 되는지 본다.

```c
/* cofasm.c — container_of 는 런타임에 '상수 빼기' 한 번이다 */
#include <stddef.h>
#define container_of(p, T, m) ((T *)((char *)(p) - offsetof(T, m)))
struct device { const char *name; int id; };
struct imu_priv { unsigned odr_hz; unsigned char range_g; struct device dev; int irq; };

struct imu_priv *to_imu(struct device *d) { return container_of(d, struct imu_priv, dev); }
int irq_of(struct device *d) { return container_of(d, struct imu_priv, dev)->irq; }
```

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/E5/u && gcc -std=gnu11 -Wall -Wextra -O2 -c ~/.elinux_scratch/E5/u/cofasm.c -o cofasm.o && objdump -d --no-show-raw-insn cofasm.o | sed -n "/<to_imu>/,\$p"'
```

```text
0000000000000000 <to_imu>:
   0:	sub	x0, x0, #0x8
   4:	ret

0000000000000008 <irq_of>:
   8:	ldr	w0, [x0, #16]
   c:	ret
```

출력에서 볼 것: `to_imu`는 `sub x0, x0, #0x8` 한 줄. `irq_of`는 빼기조차 없다 — 컴파일러가 "−8(dev 오프셋) + 24(irq 오프셋) = +16"을 미리 계산해 load 명령의 즉시 오프셋에 넣었다. 커널이 역포인터 대신 `container_of`를 쓰는 이유가 이것이다: **메모리 0바이트, 명령 0~1개.**

### 1.5 커널의 진짜 정의 — 랩 소스에서 보기

```sh
limactl shell elinux -- bash -lc 'sed -n 8,40p /var/lab/src/linux-6.18.55/include/linux/container_of.h'
```

```text
#define typeof_member(T, m)	typeof(((T*)0)->m)

/**
 * container_of - cast a member of a structure out to the containing structure
 * @ptr:	the pointer to the member.
 * @type:	the type of the container struct this is embedded in.
 * @member:	the name of the member within the struct.
 *
 * WARNING: any const qualifier of @ptr is lost.
 * Do not use container_of() in new code.
 */
#define container_of(ptr, type, member) ({				\
	void *__mptr = (void *)(ptr);					\
	static_assert(__same_type(*(ptr), ((type *)0)->member) ||	\
		      __same_type(*(ptr), void),			\
		      "pointer type mismatch in container_of()");	\
	((type *)(__mptr - offsetof(type, member))); })

/**
 * container_of_const - cast a member of a structure out to the containing
 *			structure and preserve the const-ness of the pointer
 * @ptr:		the pointer to the member
 * @type:		the type of the container struct this is embedded in.
 * @member:		the name of the member within the struct.
 *
 * Always prefer container_of_const() instead of container_of() in new code.
 */
#define container_of_const(ptr, type, member)				\
	_Generic(ptr,							\
		const typeof(*(ptr)) *: ((const type *)container_of(ptr, type, member)),\
		default: ((type *)container_of(ptr, type, member))	\
	)
```

한 줄씩 읽으면:

- `void *__mptr = (void *)(ptr);` — `ptr`을 **한 번만** 평가한다(매크로 인자에 `p++` 같은 부작용이 있어도 안전). 커널은 GNU C라서 `void *` 산술이 1바이트 단위로 허용되므로 `char *` 대신 `void *`에서 바로 뺀다.
- `static_assert(__same_type(...) || __same_type(*(ptr), void), ...)` — 예제 1에서 본 타입 검사. `void *`를 넘기는 것은 예외로 허용한다(콜백의 `void *data`에서 꺼낼 때). `__same_type`은 `include/linux/compiler_types.h`에 `__builtin_types_compatible_p(typeof(a), typeof(b))`로 정의돼 있다.
- 주석 "WARNING: any const qualifier of @ptr is lost" — `const struct device *`를 넣어도 `struct imu_priv *`(const 아님)가 나온다. 그래서 6.18 소스는 "새 코드에서는 `container_of_const()`를 써라"라고 적어 두었다. `_Generic`으로 입력이 const면 const 포인터를 돌려준다. 실제로 6.18의 `kobj_to_dev()`는 `container_of_const(__kobj, struct device, kobj)`로 정의돼 있다(§7).

### 1.6 예제 3 — 커널 안의 container_of: platform_device, 내 구조체, kref

무엇을 확인하나: (1) 커널이 만든 `struct platform_device` 안에 `struct device dev`가 내장돼 있고 `to_platform_device()`가 `container_of`라는 것, (2) 드라이버가 자기 구조체에 `struct device`와 `struct kref`를 내장하면 코어가 넘겨주는 `struct device *`/`struct kref *`에서 자기 구조체를 되찾는다는 것.

```c
// e5_cof.c — container_of 실전: platform_device 안의 dev, 내 구조체 안의 dev·kref
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/kref.h>

struct e5_sensor {                    /* 드라이버가 만든 '내 디바이스' 객체 */
	u32 odr_hz;
	struct kref users;            /* 참조 카운트 (내장) */
	struct device dev;            /* 디바이스 모델 객체 (내장, 포인터 아님) */
};
#define to_e5_sensor(d) container_of(d, struct e5_sensor, dev)

static void e5_dev_release(struct device *dev)    /* 코어는 struct device * 만 준다 */
{
	struct e5_sensor *s = to_e5_sensor(dev);
	pr_info("e5_cof: release dev=%px -> sensor=%px, kfree\n", dev, s);
	kfree(s);
}
static void e5_users_release(struct kref *k)      /* kref 도 같은 패턴 */
{
	pr_info("e5_cof: last user gone, kref=%px -> sensor=%px\n",
		k, container_of(k, struct e5_sensor, users));
}

static int __init e5_init(void)
{
	struct platform_device *pdev;
	struct e5_sensor *s;

	pdev = platform_device_register_simple("e5_cof", -1, NULL, 0);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);
	pr_info("e5_cof: pdev=%px &pdev->dev=%px offsetof(platform_device, dev)=%zu\n",
		pdev, &pdev->dev, offsetof(struct platform_device, dev));
	pr_info("e5_cof: to_platform_device(&pdev->dev) == pdev ? %d\n",
		to_platform_device(&pdev->dev) == pdev);
	platform_device_unregister(pdev);

	s = kzalloc(sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;
	s->odr_hz = 1000;
	kref_init(&s->users);                         /* users = 1 */
	device_initialize(&s->dev);                   /* dev refcount = 1 */
	s->dev.release = e5_dev_release;
	pr_info("e5_cof: sensor=%px &s->dev=%px offsetof(e5_sensor, dev)=%zu odr=%u\n",
		s, &s->dev, offsetof(struct e5_sensor, dev), to_e5_sensor(&s->dev)->odr_hz);
	kref_get(&s->users);                          /* users = 2 */
	pr_info("e5_cof: kref_read=%u\n", kref_read(&s->users));
	kref_put(&s->users, e5_users_release);        /* 1 */
	kref_put(&s->users, e5_users_release);        /* 0 -> release 콜백 */
	put_device(&s->dev);                          /* 0 -> e5_dev_release -> kfree */
	return 0;
}
static void __exit e5_exit(void) { }
module_init(e5_init);
module_exit(e5_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("E5: container_of with embedded struct device and kref");
```

게스트 스크립트는 `insmod /e5_cof.ko && dmesg | grep e5_` 한 줄이다.

```sh
limactl shell elinux -- bash -lc 'sh ~/.elinux_scratch/E5/boot.sh ~/.elinux_scratch/E5/g1.sh'
```

```text
lab initramfs: 6.18.55 up 4.20s
[    4.851234] e5_cof: loading out-of-tree module taints kernel.
[    4.871141] e5_cof: pdev=ffff000003e2f000 &pdev->dev=ffff000003e2f010 offsetof(platform_device, dev)=16
[    4.871308] e5_cof: to_platform_device(&pdev->dev) == pdev ? 1
[    4.872233] e5_cof: sensor=ffff000003e2f000 &s->dev=ffff000003e2f008 offsetof(e5_sensor, dev)=8 odr=1000
[    4.872320] e5_cof: kref_read=2
[    4.872414] e5_cof: last user gone, kref=ffff000003e2f004 -> sensor=ffff000003e2f000
[    4.872454] e5_cof: release dev=ffff000003e2f008 -> sensor=ffff000003e2f000, kfree
[    5.059965] reboot: Power down
```

출력에서 볼 것:

- `struct platform_device`는 `name`(8) + `id`(4) + `id_auto`(1, 패딩)를 지나 오프셋 16에 `dev`가 있다. `to_platform_device()`는 `include/linux/platform_device.h`에 `#define to_platform_device(x) container_of((x), struct platform_device, dev)`로 정의돼 있다. 플랫폼 드라이버의 `probe(struct platform_device *pdev)`에서 `&pdev->dev`를 쓰고, 반대로 `struct device *`만 받는 콜백(PM 콜백, sysfs `show`)에서는 `to_platform_device(dev)`로 돌아온다.
- `kref`(오프셋 4)와 `dev`(오프셋 8) 두 멤버에서 각각 같은 `sensor` 주소(`…f000`)를 되찾았다. **한 객체가 여러 "부품"을 내장하고, 각 부품을 받는 콜백에서 부품마다 다른 `member` 이름으로 돌아온다.**
- `sensor` 주소가 직전에 해제된 `pdev` 주소와 같다(`…f000`). slab 할당자가 방금 해제된 같은 크기 클래스의 객체를 재사용한 것이다 — 값은 실행마다 다를 수 있다.
- `put_device()`가 마지막 참조를 내리자 코어가 우리 `release`를 불렀고, 거기서 `container_of`로 바깥 구조체를 찾아 `kfree`했다. **`struct device`를 내장한 객체의 메모리는 반드시 `release`에서 해제한다** — 이게 디바이스 모델의 수명 규칙이다(§7).

### 1.7 함정

| 함정 | 무슨 일이 생기나 |
|---|---|
| 멤버가 **포인터**인데 container_of (`struct device *dev;`를 멤버로 두고 `container_of(dev, ...)`) | 포인터가 가리키는 다른 객체 주소에서 오프셋을 빼므로 엉뚱한 주소. 6.x는 타입 검사로 대부분 컴파일 에러가 난다(`struct device *` 대 `struct device`) |
| `member` 이름을 다른 같은 타입 멤버로 씀 (`list_head`가 두 개인 구조체에서 `node` 대신 `alarm`) | 타입이 같아 검사를 통과한다. 오프셋 차이만큼 어긋난 주소 → 조용한 메모리 손상 |
| const 포인터에 container_of | const가 사라진다. 새 코드는 `container_of_const` |
| 내장 객체를 정적 변수나 스택에 두고 `release`에서 `kfree` | 디바이스 모델 객체는 동적 할당 + `release`가 정석. 스택 객체에 `put_device`하면 해제 후 사용 |
| `struct device`를 내장해 놓고 `release`를 비워 둠 | 코어가 `WARN`으로 "Device '...' does not have a release() function, it is broken and must be fixed."를 찍는다(`drivers/base/core.c`의 `device_release`). 빈 release로 경고만 지우면 누수 |

---

## 2. list_head — 링크를 객체 안에 넣는 intrusive 리스트

### 2.1 직관: "링크 필드를 구조체에 박아 두고, 리스트는 링크끼리만 잇는다"

교과서 링크드 리스트(그리고 C++ `std::list`)는 **리스트 노드가 데이터를 품거나 가리킨다**: `struct node { struct node *next; void *data; }`. 커널은 반대로 **데이터가 링크를 품는다**: 데이터 구조체 안에 `struct list_head { struct list_head *next, *prev; }`를 멤버로 두고, 리스트는 그 `list_head`끼리만 잇는다. 링크에서 데이터로 돌아갈 때는 §1의 `container_of`를 쓴다. 이걸 **intrusive list**(침투형 리스트)라고 한다.

Don의 펌웨어 경험으로 치면, NAND 명령 디스크립터 풀(pool)에서 디스크립터 안에 `next` 필드를 두고 free list / pending list / done list 사이를 옮기던 방식이 정확히 이것이다. 리스트에 넣는다고 메모리를 새로 할당하지 않는다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" font-size="13">intrusive — 링크가 객체 안에 있다 (커널 list_head)</text>
  <rect x="10" y="70" width="90" height="36" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="55" y="92" text-anchor="middle" font-size="12">LIST_HEAD q</text>
  <rect x="130" y="40" width="160" height="100" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="210" y="58" text-anchor="middle" font-size="12">struct e5_sample</text>
  <text x="210" y="78" text-anchor="middle" font-size="12">seq · accel_mg</text>
  <rect x="140" y="88" width="140" height="22" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="210" y="104" text-anchor="middle" font-size="12">node (next, prev)</text>
  <rect x="140" y="114" width="140" height="20" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="210" y="128" text-anchor="middle" font-size="12">hnode (해시용)</text>
  <rect x="310" y="40" width="160" height="100" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="390" y="58" text-anchor="middle" font-size="12">struct e5_sample</text>
  <text x="390" y="78" text-anchor="middle" font-size="12">seq · accel_mg</text>
  <rect x="320" y="88" width="140" height="22" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="390" y="104" text-anchor="middle" font-size="12">node (next, prev)</text>
  <rect x="320" y="114" width="140" height="20" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="390" y="128" text-anchor="middle" font-size="12">hnode (해시용)</text>
  <rect x="490" y="40" width="160" height="100" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="570" y="58" text-anchor="middle" font-size="12">struct e5_sample</text>
  <text x="570" y="78" text-anchor="middle" font-size="12">seq · accel_mg</text>
  <rect x="500" y="88" width="140" height="22" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="570" y="104" text-anchor="middle" font-size="12">node (next, prev)</text>
  <rect x="500" y="114" width="140" height="20" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="570" y="128" text-anchor="middle" font-size="12">hnode (해시용)</text>
  <line x1="100" y1="88" x2="138" y2="99" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="280" y1="99" x2="318" y2="99" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="460" y1="99" x2="498" y2="99" stroke="currentColor" marker-end="url(#ah3)"/>
  <path d="M640,99 L662,99 L662,152 L55,152 L55,108" fill="none" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="340" y="172" text-anchor="middle" font-size="12">next/prev는 객체 시작이 아니라 node 멤버를 가리킨다 → container_of로 바깥 객체 복원</text>
  <text x="10" y="200" font-size="13">non-intrusive — 리스트 노드가 객체를 가리킨다 (일반 링크드 리스트)</text>
  <rect x="10" y="212" width="90" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="55" y="232" text-anchor="middle" font-size="12">head</text>
  <rect x="130" y="212" width="110" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="185" y="232" text-anchor="middle" font-size="12">next · data</text>
  <rect x="310" y="212" width="110" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="365" y="232" text-anchor="middle" font-size="12">next · data</text>
  <rect x="490" y="212" width="110" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="545" y="232" text-anchor="middle" font-size="12">next · data</text>
  <rect x="130" y="268" width="110" height="32" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="185" y="288" text-anchor="middle" font-size="12">sample</text>
  <rect x="310" y="268" width="110" height="32" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="365" y="288" text-anchor="middle" font-size="12">sample</text>
  <rect x="490" y="268" width="110" height="32" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="545" y="288" text-anchor="middle" font-size="12">sample</text>
  <line x1="100" y1="228" x2="128" y2="228" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="240" y1="228" x2="308" y2="228" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="420" y1="228" x2="488" y2="228" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="185" y1="244" x2="185" y2="266" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="365" y1="244" x2="365" y2="266" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="545" y1="244" x2="545" y2="266" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="340" y="322" text-anchor="middle" font-size="12">노드 할당이 N번 더 필요 · 포인터를 한 번 더 따라감 · 리스트마다 노드가 따로</text>
</svg>
```
그림 3 — 위: 커널 방식. 리스트 머리(`LIST_HEAD`)와 각 객체의 `node`가 원형 이중 연결 리스트를 이룬다(`prev` 화살표는 생략). 같은 객체가 `hnode`로 해시 테이블에도 동시에 들어 있다. 아래: 교과서 방식. 노드와 데이터가 따로 할당된다.

왜 커널은 intrusive를 고집하나:

| 이유 | 설명 |
|---|---|
| `list_add`가 **실패하지 않는다** | 넣을 때 메모리 할당이 없다. spinlock을 잡은 상태나 인터럽트 컨텍스트에서도 `-ENOMEM` 걱정 없이 넣는다. 드라이버에서 결정적이다 |
| 한 객체를 여러 리스트에 | `list_head` 멤버를 여러 개 두면 된다(예제 4의 `node`와 `alarm`, 커널의 `struct task_struct`에는 `list_head`가 여러 개 있다) |
| 객체 포인터만 있으면 O(1) 삭제 | `list_del(&obj->node)` — 리스트를 처음부터 찾을 필요가 없다(이중 연결) |
| 타입에 무관한 한 벌의 코드 | 리스트 코드는 `list_head`만 안다. C++ 템플릿 없이 모든 타입에 같은 함수 |
| 캐시 | 데이터와 링크가 같은 캐시 라인에 있을 가능성이 높다(§2.8에서 측정) |

### 2.2 API 한 장 정리

| API | 하는 일 | 비고 |
|---|---|---|
| `LIST_HEAD(name)` | 빈 리스트 머리 선언+초기화 | 빈 리스트 = `next`와 `prev`가 자기 자신 |
| `INIT_LIST_HEAD(&h)` | 런타임 초기화 | 구조체 안의 머리, kmalloc한 머리 |
| `list_add(new, head)` | 머리 **바로 뒤**에 삽입 | 스택(LIFO) |
| `list_add_tail(new, head)` | 머리 **바로 앞**(=끝)에 삽입 | 큐(FIFO) |
| `list_del(entry)` | 빼고 `next`/`prev`에 poison 값 기록 | 뺀 뒤 그 링크를 쓰면 바로 터지게 |
| `list_del_init(entry)` | 빼고 자기 자신을 빈 리스트로 | 뺀 뒤 `list_empty(entry)`로 검사 가능 |
| `list_move(_tail)(entry, head)` | 다른 리스트로 옮김 | free list → busy list |
| `list_empty(head)` | 비었나 | |
| `list_first_entry(head, type, member)` | 첫 원소의 바깥 객체 | 비어 있으면 안 됨 → `_or_null` 버전 |
| `list_for_each_entry(pos, head, member)` | 바깥 객체 단위로 순회 | 순회 중 `pos` 삭제 금지 |
| `list_for_each_entry_safe(pos, n, head, member)` | 다음 원소를 `n`에 미리 저장하며 순회 | 순회 중 `pos` 삭제 가능 |
| `list_splice(_init)(list, head)` | 리스트 통째로 붙이기 | 락 안에서 떼어 와 락 밖에서 처리하는 패턴 |

랩 소스에서 핵심 두 함수와 두 매크로를 보자.

```sh
limactl shell elinux -- bash -lc 'S=/var/lab/src/linux-6.18.55/include/linux/list.h; sed -n 157,169p $S; sed -n 242,247p $S; sed -n 788,791p $S; sed -n 875,879p $S'
```

```text
static __always_inline void __list_add(struct list_head *new,
				       struct list_head *prev,
				       struct list_head *next)
{
	if (!__list_add_valid(new, prev, next))
		return;

	next->prev = new;
	new->next = next;
	new->prev = prev;
	WRITE_ONCE(prev->next, new);
}

static inline void list_del(struct list_head *entry)
{
	__list_del_entry(entry);
	entry->next = LIST_POISON1;
	entry->prev = LIST_POISON2;
}
#define list_for_each_entry(pos, head, member)				\
	for (pos = list_first_entry(head, typeof(*pos), member);	\
	     !list_entry_is_head(pos, head, member);			\
	     pos = list_next_entry(pos, member))
#define list_for_each_entry_safe(pos, n, head, member)			\
	for (pos = list_first_entry(head, typeof(*pos), member),	\
		n = list_next_entry(pos, member);			\
	     !list_entry_is_head(pos, head, member); 			\
	     pos = n, n = list_next_entry(n, member))
```

읽을 점:

- `__list_add`의 마지막 줄만 `WRITE_ONCE`다. 새 노드를 완성한 뒤 **마지막에** `prev->next`를 한 번에 써서, 락 없이 앞으로만 읽는 RCU 독자가 반쯤 만든 노드를 보지 않게 한다(§5.6 미리보기). `__list_add_valid`는 `CONFIG_LIST_HARDENED`/`CONFIG_DEBUG_LIST`가 꺼져 있으면 항상 true인 인라인이다(랩 커널은 둘 다 꺼짐).
- `list_del`은 뺀 노드의 `next`/`prev`를 `LIST_POISON1`/`LIST_POISON2`로 덮는다. `include/linux/poison.h`에서 `((void *) 0x100 + POISON_POINTER_DELTA)`, `0x122 + ...`이고 arm64는 `CONFIG_ILLEGAL_POINTER_VALUE=0xdead000000000000`이므로 실제 값은 `0xdead000000000100`과 `0xdead000000000122`다. 유저 공간도 커널 공간도 아닌 주소라서 접근하면 **반드시** fault가 난다. 예제 6에서 이 숫자를 oops에서 다시 만난다.
- `list_for_each_entry`는 `pos`를 바깥 객체 포인터로 두고, 다음 단계에서 `pos->member.next`를 읽는다. 그러니 몸통에서 `pos`를 지우면 다음 단계가 poison(또는 해제된 메모리)을 읽는다. `_safe`는 다음 원소를 `n`에 **미리** 빼 둔다.
- 종료 조건 `list_entry_is_head`는 "`&pos->member == head`"다. 머리는 바깥 객체가 아니므로 `pos`는 마지막에 "머리를 바깥 객체로 착각한 가짜 포인터"가 된다 — 루프가 끝난 뒤 `pos`를 쓰면 안 되는 이유다.

### 2.3 예제 4 — userspace로 list_head 다시 만들기 (화이트보드용)

무엇을 확인하나: 커널 `list.h`의 핵심(머리 초기화, add, add_tail, del + poison, for_each_entry, _safe)을 60줄 안에 다시 만들고, 센서 샘플 6개를 큐에 넣은 뒤 음수 샘플을 지운다. 같은 객체를 두 번째 리스트(`alarm`)에도 넣는다. 인자 `bug`를 주면 non-safe 순회로 지운다.

```c
/* ilist.c — 커널 list_head 를 userspace 로 다시 만들기 (화이트보드용 최소판) */
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

struct list_head { struct list_head *next, *prev; };
#define LIST_HEAD_INIT(n) { &(n), &(n) }
#define container_of(p, T, m) ((T *)((char *)(p) - offsetof(T, m)))
#define list_entry(p, T, m) container_of(p, T, m)
#define list_for_each_entry(pos, head, m)                                   \
	for (pos = list_entry((head)->next, typeof(*pos), m); &pos->m != (head); \
	     pos = list_entry(pos->m.next, typeof(*pos), m))
#define list_for_each_entry_safe(pos, n, head, m)                           \
	for (pos = list_entry((head)->next, typeof(*pos), m),               \
	     n = list_entry(pos->m.next, typeof(*pos), m);                  \
	     &pos->m != (head);                                             \
	     pos = n, n = list_entry(n->m.next, typeof(*n), m))

static void __list_add(struct list_head *new, struct list_head *prev,
		       struct list_head *next)
{ next->prev = new; new->next = next; new->prev = prev; prev->next = new; }
static void list_add(struct list_head *new, struct list_head *head)
{ __list_add(new, head, head->next); }            /* 머리 뒤 = 스택 */
static void list_add_tail(struct list_head *new, struct list_head *head)
{ __list_add(new, head->prev, head); }            /* 머리 앞 = 큐 */
static void list_del(struct list_head *e)
{
	e->next->prev = e->prev; e->prev->next = e->next;
	e->next = (void *)0x100; e->prev = (void *)0x122;   /* poison */
}

struct sample {                      /* 센서 샘플 하나 */
	unsigned seq; int accel_mg;
	struct list_head node;       /* 전체 큐 */
	struct list_head alarm;      /* 같은 객체가 두 번째 리스트에도 */
};

int main(int argc, char **argv)
{
	struct list_head q = LIST_HEAD_INIT(q), alarms = LIST_HEAD_INIT(alarms);
	struct sample *s, *tmp;
	int bug = argc > 1 && !strcmp(argv[1], "bug");

	for (unsigned i = 0; i < 6; i++) {
		s = malloc(sizeof(*s));
		if (!s) { perror("malloc"); exit(1); }
		s->seq = i; s->accel_mg = (int)(i * 700) - 1500;
		list_add_tail(&s->node, &q);
		if (s->accel_mg > 1000) list_add(&s->alarm, &alarms);
	}
	printf("queue :"); list_for_each_entry(s, &q, node) printf(" %u(%d)", s->seq, s->accel_mg);
	printf("\nalarms:"); list_for_each_entry(s, &alarms, alarm) printf(" %u", s->seq);
	printf("\n");
	if (bug) {                    /* 틀린 코드: non-safe 순회 중 삭제 */
		list_for_each_entry(s, &q, node)
			if (s->accel_mg < 0) { list_del(&s->node); free(s); }
	} else {
		list_for_each_entry_safe(s, tmp, &q, node)
			if (s->accel_mg < 0) { list_del(&s->node); free(s); }
	}
	printf("after :"); list_for_each_entry(s, &q, node) printf(" %u", s->seq);
	printf("\n");
	return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/E5/u && gcc -std=gnu11 -Wall -Wextra -O2 ~/.elinux_scratch/E5/u/ilist.c -o ilist && ./ilist; echo "--- bug mode"; ./ilist bug; echo "exit=$?"'
```

```text
queue : 0(-1500) 1(-800) 2(-100) 3(600) 4(1300) 5(2000)
alarms: 5 4
after : 3 4 5
--- bug mode
exit=139
bash: line 1: 478243 Segmentation fault         (core dumped) ./ilist bug
```

출력에서 볼 것:

- `queue`는 `list_add_tail`이라 넣은 순서(FIFO), `alarms`는 `list_add`라 역순(LIFO: 5가 먼저).
- `_safe` 버전은 음수 3개(0, 1, 2)를 지우고 3, 4, 5만 남겼다.
- `bug` 모드는 첫 삭제 직후 **segfault**(종료 코드 139 = 128 + SIGSEGV 11). 어디서 죽었는지 gdb로 보자.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/E5/u && gdb -q -batch -ex run -ex "info registers x0 x1 x2 x3" -ex "x/i \$pc" -ex "p \$_siginfo._sifields._sigfault.si_addr" --args ./ilist bug 2>&1 | tail -8'
```

```text
Program received signal SIGSEGV, Segmentation fault.
0x0000aaaaaaaa0b64 in main ()
x0             0xb341b06e44c68180  -5529944879787507328
x1             0xaaaaaaac2148      187649984569672
x2             0x30                48
x3             0xaaaaaaac1010      187649984565264
=> 0xaaaaaaaa0b64 <main+612>:	ldr	w0, [x20, #4]
$1 = (void *) 0xb341b06e44c6817c
```

출력에서 볼 것: fault 주소가 우리가 넣은 poison `0x100` 근처가 **아니다**. `x0 = 0xb341…8180`은 무작위처럼 보이는 값이고, fault 주소 `…817c` = `x0 − 8(node 오프셋) + 4(accel_mg 오프셋)`이다. 즉 "다음 원소"를 `x0`에서 계산했다. 그런데 `x0`는 어디서 왔나? 버그 코드는 `list_del` → `free(s)` → (루프 증가식에서) `s->node.next` 읽기 순서다. glibc의 `free`는 해제한 청크의 앞 16바이트에 tcache 메타데이터(다음 free 청크 포인터와 **무작위 tcache key**)를 쓰는데, `node.next`가 정확히 오프셋 8 — key 자리 — 라서 poison이 key로 덮였다. **해제 후 사용(use-after-free)은 무엇을 읽을지 할당자에 달려 있다** — 이번엔 즉시 죽었지만 다른 할당자·다른 크기라면 그럴듯한 포인터를 읽고 조용히 계속 갈 수도 있다. 커널에서 같은 일을 하면 어떻게 되는지 예제 6에서 본다(그쪽은 `kfree` 없이 `list_del`만 해서 poison이 그대로 보이게 했다).

화이트보드에서 외울 것은 네 줄이다: `__list_add`의 포인터 4개 대입 순서, `list_add = __list_add(new, head, head->next)`, `list_add_tail = __list_add(new, head->prev, head)`, `list_del = prev->next = next; next->prev = prev`. 나머지는 이 넷과 `container_of`의 조합이다.

### 2.4 예제 5 — 커널 모듈: 센서 샘플 큐 + hlist 해시

무엇을 확인하나: 커널의 진짜 `list.h`와 `hashtable.h`로 같은 일을 한다. 샘플을 `kmalloc`해 큐(`list_head`)와 seq 해시(`hlist`)에 동시에 넣고, 해시로 seq=4를 찾고, `_safe`로 음수 샘플을 지운 뒤, `rmmod` 때 전부 해제한다. 모듈 파라미터 `bug=1`이면 non-safe 순회로 지운다.

```c
// e5_list.c — list_head 로 센서 샘플 큐, _safe 삭제, hlist 해시, 그리고 bug=1 이면 non-safe 삭제
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/hashtable.h>

static bool bug;
module_param(bug, bool, 0444);

struct e5_sample {
	u32 seq;
	s32 accel_mg;
	struct list_head node;        /* 큐 연결 */
	struct hlist_node hnode;      /* seq 해시 연결 (같은 객체, 두 번째 컨테이너) */
};
static LIST_HEAD(e5_q);
static DEFINE_HASHTABLE(e5_by_seq, 2);           /* 버킷 4개 = hlist_head[4] */

static int __init e5_init(void)
{
	struct e5_sample *s, *tmp;
	u32 i;

	pr_info("e5_list: sizeof list_head=%zu hlist_head=%zu hlist_node=%zu sample=%zu\n",
		sizeof(struct list_head), sizeof(struct hlist_head),
		sizeof(struct hlist_node), sizeof(struct e5_sample));
	for (i = 0; i < 6; i++) {
		s = kmalloc(sizeof(*s), GFP_KERNEL);
		if (!s)
			return -ENOMEM;         /* (데모: 실패 시 이미 넣은 것 누수) */
		s->seq = i;
		s->accel_mg = i * 700 - 1500;
		list_add_tail(&s->node, &e5_q);       /* 큐: 뒤에 붙임 */
		hash_add(e5_by_seq, &s->hnode, s->seq);
	}
	list_for_each_entry(s, &e5_q, node)
		pr_info("e5_list: q seq=%u accel=%d\n", s->seq, s->accel_mg);
	hash_for_each_possible(e5_by_seq, s, hnode, 4)
		if (s->seq == 4)
			pr_info("e5_list: hash lookup seq=4 -> %px accel=%d\n", s, s->accel_mg);

	if (bug) {                                     /* 틀린 코드 */
		list_for_each_entry(s, &e5_q, node)
			if (s->accel_mg < 0) {
				list_del(&s->node);
				pr_info("e5_list: deleted %u, node.next now %px\n",
					s->seq, s->node.next);
			}
	} else {                                       /* 올바른 코드 */
		list_for_each_entry_safe(s, tmp, &e5_q, node)
			if (s->accel_mg < 0) {
				list_del(&s->node);
				hash_del(&s->hnode);
				kfree(s);
			}
	}
	list_for_each_entry(s, &e5_q, node)
		pr_info("e5_list: kept seq=%u\n", s->seq);
	return 0;
}
static void __exit e5_exit(void)
{
	struct e5_sample *s, *tmp;
	list_for_each_entry_safe(s, tmp, &e5_q, node) {
		list_del(&s->node);
		kfree(s);
	}
	pr_info("e5_list: freed all, empty=%d\n", list_empty(&e5_q));
}
module_init(e5_init);
module_exit(e5_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("E5: list_head, hlist and the non-safe deletion bug");
```

게스트 스크립트: `insmod /e5_list.ko && rmmod e5_list && dmesg | grep e5_`

```text
lab initramfs: 6.18.55 up 4.55s
[    5.483223] e5_list: loading out-of-tree module taints kernel.
[    5.497913] e5_list: sizeof list_head=16 hlist_head=8 hlist_node=16 sample=40
[    5.498058] e5_list: q seq=0 accel=-1500
[    5.498086] e5_list: q seq=1 accel=-800
[    5.498092] e5_list: q seq=2 accel=-100
[    5.498096] e5_list: q seq=3 accel=600
[    5.498100] e5_list: q seq=4 accel=1300
[    5.498104] e5_list: q seq=5 accel=2000
[    5.498132] e5_list: hash lookup seq=4 -> ffff000003724180 accel=1300
[    5.498369] e5_list: kept seq=3
[    5.498392] e5_list: kept seq=4
[    5.498396] e5_list: kept seq=5
[    5.532540] e5_list: freed all, empty=1
[    5.688498] reboot: Power down
```

출력에서 볼 것: `struct e5_sample`은 `seq`(4) + `accel_mg`(4) + `node`(16) + `hnode`(16) = 40바이트 — 리스트 두 개에 들어가는 비용이 객체 안 32바이트뿐이고 추가 할당이 없다. `hlist_head`는 **8바이트**(포인터 하나)라서 `list_head`(16)의 절반이다. `rmmod` 때 `_safe`로 전부 지워 `list_empty`가 1.

### 2.5 예제 6 — 순회 중 삭제 버그를 커널에서 터뜨리고 oops 읽기

무엇을 확인하나: 같은 모듈을 `bug=1`로 넣으면 커널이 어떻게 죽는지, 그리고 oops의 주소만 보고 원인을 추론할 수 있는지.

게스트 스크립트: `insmod /e5_list.ko bug=1; echo "insmod exit=$?"; dmesg | grep -E "e5_list|Unable|pc :|lr :|Internal error|Call trace|list_" | head -20` (콘솔에 찍힌 oops 전체 중 핵심만 남기고 레지스터 덤프 일부를 생략했다)

```text
[    6.132415] Unable to handle kernel paging request at virtual address dead0000000000fc
[    6.132450] Mem abort info:
[    6.132460]   ESR = 0x0000000096000004
[    6.132487]   EC = 0x25: DABT (current EL), IL = 32 bits
...
[    6.132525]   FSC = 0x04: level 0 translation fault
...
[    6.132611] [dead0000000000fc] address between user and kernel address ranges
[    6.132756] Internal error: Oops: 0000000096000004 [#1]  SMP
[    6.137931] Modules linked in: e5_list(O+)
[    6.143060] CPU: 0 UID: 0 PID: 63 Comm: insmod Tainted: G           O        6.18.55 #1 PREEMPT 
...
[    6.146868] pc : e5_init+0x150/0xfc4 [e5_list]
[    6.147626] lr : e5_init+0x184/0xfc4 [e5_list]
...
[    6.148680] x26: 0000000000000000 x25: ffff80008045bd40 x24: 0000000061c88647
...
[    6.150578] x20: ffffd484b78f7000 x19: dead0000000000f8 x18: 0000000000000010
...
[    6.154272] x2 : 0000000000000000 x1 : ffff000003e50000 x0 : dead000000000100
[    6.154512] Call trace:
[    6.154733]  e5_init+0x150/0xfc4 [e5_list] (P)
[    6.154946]  do_one_initcall+0x60/0x1e0
[    6.158483]  do_init_module+0x54/0x240
[    6.158583]  load_module+0x18c0/0x1eb0
...
[    6.159910] ---[ end trace 0000000000000000 ]---
Segmentation fault
insmod exit=139
...
[    6.129970] e5_list: deleted 0, node.next now dead000000000100
```

이 oops를 Don이 JTAG로 data abort를 분석하듯 읽어 보자.

1. **fault 주소 `dead0000000000fc`** — `dead…`로 시작하면 거의 항상 poison 값이다. `LIST_POISON1 = 0xdead000000000100`에서 4가 모자란다.
2. **`x0 = dead000000000100`** — 레지스터에 `LIST_POISON1` 그 자체가 있다. 방금 `list_del`한 노드의 `next`를 읽어 온 값이다(마지막 dmesg 줄 "node.next now dead000000000100"과 일치).
3. **`x19 = dead0000000000f8`** — `container_of(0xdead…100, struct e5_sample, node)` = `0x…100 − 8`(`node`의 오프셋). 즉 다음 `pos`.
4. 그 `pos`의 `accel_mg`(오프셋 4)를 읽으면 `0x…0f8 + 4 = 0x…0fc` — fault 주소와 정확히 일치. **"삭제한 노드의 next를 따라가서 다음 원소의 필드를 읽다 죽었다."**
5. `ESR`의 `EC = 0x25: DABT (current EL)` — 커널 모드(EL1)에서 난 data abort. `FSC = 0x04: level 0 translation fault` — 페이지 테이블 최상위 단계에서 이미 매핑이 없다. "address between user and kernel address ranges"는 커널이 친절하게 붙인 해설이다.
6. `insmod`는 Segmentation fault로 끝났다(`CONFIG_PANIC_ON_OOPS`가 꺼져 있어 커널은 살아 있지만, 이 모듈은 반쯤 초기화된 상태로 남아 이후 `rmmod`도 못 한다 — 실제 장비라면 재부팅).

> 덤: `x24 = 0000000061c88647`은 `include/linux/hash.h`의 `GOLDEN_RATIO_32`(`hash_32()`가 곱하는 상수)다. 직전의 `hash_add`/`hash_for_each_possible`이 레지스터에 남긴 값으로 보인다.

`kfree`까지 했다면(예제 4처럼) poison 대신 해제된 메모리를 읽어서 **가끔만** 터지는 더 나쁜 버그가 된다. `list_del`이 poison을 쓰는 이유가 그 "가끔"을 "항상"으로 바꾸기 위해서다.

### 2.6 hlist — 해시 버킷용 반쪽 리스트

해시 테이블은 버킷이 수천 개이고 대부분 비어 있다. 버킷마다 `list_head`(16바이트)를 두면 낭비라서 커널은 머리가 포인터 하나인 `hlist`를 쓴다.

```sh
limactl shell elinux -- bash -lc 'sed -n 199,209p /var/lab/src/linux-6.18.55/include/linux/types.h'
```

```text
struct list_head {
	struct list_head *next, *prev;
};

struct hlist_head {
	struct hlist_node *first;
};

struct hlist_node {
	struct hlist_node *next, **pprev;
};
```

- `hlist_head`는 `first` 하나 → 8바이트. 대신 꼬리로 바로 갈 수 없다(`add_tail` O(1) 불가) — 해시 버킷엔 필요 없다.
- `hlist_node`의 `pprev`는 "이전 노드" 가 아니라 **"나를 가리키는 포인터의 주소"**(`struct hlist_node **`)다. 첫 노드면 `&head->first`, 아니면 `&prev->next`. 그래서 첫 노드인지 아닌지 분기 없이 `*n->pprev = n->next`로 삭제된다. "포인터의 포인터로 머리 특수 경우를 없앤다"는 이 기법은 단일 연결 리스트 삭제 면접 문제의 모범 답이기도 하다(§10 드릴 2의 꼬리 질문).
- `DEFINE_HASHTABLE(name, bits)`는 `struct hlist_head name[1 << bits]`, `hash_add`/`hash_del`/`hash_for_each_possible`이 그 위의 얇은 래퍼다(예제 5).

### 2.7 kref — 내장 참조 카운트

`struct kref`는 `refcount_t` 하나뿐인 구조체다(`include/linux/kref.h`). 이것도 intrusive다: 객체 안에 내장하고, `kref_put(&obj->ref, release)`가 0이 되면 `release(struct kref *)`를 부르고, release는 `container_of`로 객체를 찾아 해제한다(예제 3의 `e5_users_release`).

| 규칙 | 이유 |
|---|---|
| 포인터를 남에게 넘기거나 저장하면 `kref_get` | 내가 쓰는 동안 남이 마지막 put을 해도 살아 있게 |
| 다 쓰면 `kref_put(ref, release)` | release는 마지막 put에서 정확히 한 번 |
| `kref_init`은 1로 시작 | 만든 사람이 첫 참조를 가진다 |
| release 안에서 객체를 리스트에서 빼야 한다면 락과 함께 | "0이 됐는데 리스트로 누가 찾아서 get" 경쟁 → `kref_get_unless_zero`, `kref_put_mutex`/`kref_put_lock` |
| `refcount_t`는 0에서 증가·오버플로를 감지해 WARN하고 포화(saturate)시킨다 | use-after-free를 누수로 바꿔 안전하게 |

`struct device`(내부의 `kobject` → `kref`)도 결국 이 구조다(§7).

### 2.8 예제 7 — 순회 비용 측정: intrusive vs non-intrusive

무엇을 확인하나: 원소 2^20개(약 100만)를 순회하며 값을 더하는 시간을 두 방식으로 잰다. non-intrusive의 data는 노드와 다른 시점에 할당해 떨어진 곳에 둔다(실제로 객체가 먼저 있고 나중에 리스트에 넣는 상황). `s` 인자를 주면 링크 순서를 무작위로 섞어 캐시·프리페치가 못 돕게 한다. 7회 반복 중 최솟값을 출력한다.

```c
/* bench.c — 순회 비용: intrusive(list_head 내장) vs non-intrusive(노드가 data 를 가리킴) */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stddef.h>

#define N (1 << 20)
struct list_head { struct list_head *next; };
struct sample_i { long val; char pad[48]; struct list_head node; };   /* intrusive */
struct sample_n { long val; char pad[48]; };                         /* data 만 */
struct nnode    { struct nnode *next; struct sample_n *data; };      /* 별도 노드 */

static double now(void)
{ struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e9 + t.tv_nsec; }

int main(int argc, char **argv)
{
	(void)argv;
	int shuffle = argc > 1;                 /* 인자 있으면 링크 순서를 섞는다 */
	unsigned *ord = malloc(N * sizeof(*ord));
	struct sample_i **pi = malloc(N * sizeof(*pi));
	struct nnode **pn = malloc(N * sizeof(*pn));
	if (!ord || !pi || !pn) { perror("malloc"); exit(1); }
	srand(1);
	for (unsigned i = 0; i < N; i++) {       /* intrusive 객체, non-intrusive 노드 */
		ord[i] = i;
		pi[i] = malloc(sizeof(**pi)); pn[i] = malloc(sizeof(**pn));
		if (!pi[i] || !pn[i]) { perror("malloc"); exit(1); }
		pi[i]->val = i;
	}
	for (unsigned i = 0; i < N; i++) {       /* non-intrusive data 는 다른 곳에 */
		pn[i]->data = malloc(sizeof(struct sample_n));
		if (!pn[i]->data) { perror("malloc"); exit(1); }
		pn[i]->data->val = i;
	}
	if (shuffle)
		for (unsigned i = N - 1; i > 0; i--) {
			unsigned j = rand() % (i + 1), t = ord[i]; ord[i] = ord[j]; ord[j] = t;
		}
	struct list_head head; struct list_head *li = &head;
	struct nnode nhead; struct nnode *ln = &nhead;
	for (unsigned i = 0; i < N; i++) {
		li->next = &pi[ord[i]]->node; li = li->next;
		ln->next = pn[ord[i]]; ln = ln->next;
	}
	li->next = NULL; ln->next = NULL;
	double b1 = 1e30, b2 = 1e30; long s1 = 0, s2 = 0;
	for (int rep = 0; rep < 7; rep++) {      /* 7회 중 최솟값 (VM 잡음 제거) */
		s1 = s2 = 0; double t0 = now();
		for (struct list_head *p = head.next; p; p = p->next)
			s1 += ((struct sample_i *)((char *)p - offsetof(struct sample_i, node)))->val;
		double t1 = now();
		for (struct nnode *p = nhead.next; p; p = p->next)
			s2 += p->data->val;
		double t2 = now();
		if (t1 - t0 < b1) b1 = t1 - t0;
		if (t2 - t1 < b2) b2 = t2 - t1;
	}
	printf("%s intrusive %.2f ns/elem, non-intrusive %.2f ns/elem (sum %ld %ld)\n",
	       shuffle ? "shuffled  " : "sequential", b1 / N, b2 / N, s1, s2);
	return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/E5/u && gcc -std=gnu11 -Wall -Wextra -O2 ~/.elinux_scratch/E5/u/bench.c -o bench && for i in 1 2 3; do ./bench; ./bench s; done; uptime'
```

```text
sequential intrusive 9.51 ns/elem, non-intrusive 11.23 ns/elem (sum 549755289600 549755289600)
shuffled   intrusive 241.09 ns/elem, non-intrusive 478.18 ns/elem (sum 549755289600 549755289600)
sequential intrusive 8.90 ns/elem, non-intrusive 17.05 ns/elem (sum 549755289600 549755289600)
shuffled   intrusive 259.34 ns/elem, non-intrusive 418.11 ns/elem (sum 549755289600 549755289600)
sequential intrusive 3.04 ns/elem, non-intrusive 5.97 ns/elem (sum 549755289600 549755289600)
shuffled   intrusive 156.64 ns/elem, non-intrusive 159.65 ns/elem (sum 549755289600 549755289600)
 13:46:48 up  3:49,  1 user,  load average: 18.15, 14.52, 7.79
```

같은 명령을 조금 전에 한 번 더 돌렸을 때는 sequential (7.49, 9.11) (5.88, 10.82) (3.77, 4.73), shuffled (335.78, 328.54) (235.42, 187.97) (395.83, 366.20)이 나왔다(괄호 = intrusive, non-intrusive). 두 번의 3회씩, 총 6회를 모아 최솟값과 중앙값을 그렸다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <text x="190" y="24" text-anchor="middle" font-size="13">sequential (링크 순서 = 할당 순서)</text>
  <line x1="70" y1="50" x2="70" y2="230" stroke="currentColor"/>
  <line x1="70" y1="230" x2="310" y2="230" stroke="currentColor"/>
  <line x1="66" y1="230.0" x2="70" y2="230.0" stroke="currentColor"/>
  <text x="63" y="234.0" text-anchor="end" font-size="12">0</text>
  <line x1="66" y1="170.0" x2="70" y2="170.0" stroke="currentColor"/>
  <text x="63" y="174.0" text-anchor="end" font-size="12">4</text>
  <line x1="70" y1="170.0" x2="310" y2="170.0" stroke="#888" stroke-opacity="0.3"/>
  <line x1="66" y1="110.0" x2="70" y2="110.0" stroke="currentColor"/>
  <text x="63" y="114.0" text-anchor="end" font-size="12">8</text>
  <line x1="70" y1="110.0" x2="310" y2="110.0" stroke="#888" stroke-opacity="0.3"/>
  <line x1="66" y1="50.0" x2="70" y2="50.0" stroke="currentColor"/>
  <text x="63" y="54.0" text-anchor="end" font-size="12">12</text>
  <line x1="70" y1="50.0" x2="310" y2="50.0" stroke="#888" stroke-opacity="0.3"/>
  <rect x="95" y="184.4" width="36" height="45.6" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <text x="113" y="179.4" text-anchor="middle" font-size="12">3.04</text>
  <rect x="137" y="159.1" width="36" height="71.0" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
  <text x="155" y="154.1" text-anchor="middle" font-size="12">4.73</text>
  <text x="134" y="248" text-anchor="middle" font-size="12">최솟값</text>
  <rect x="210" y="129.6" width="36" height="100.4" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <text x="228" y="124.6" text-anchor="middle" font-size="12">6.69</text>
  <rect x="252" y="80.4" width="36" height="149.6" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
  <text x="270" y="75.4" text-anchor="middle" font-size="12">9.97</text>
  <text x="249" y="248" text-anchor="middle" font-size="12">중앙값</text>
  <text x="520" y="24" text-anchor="middle" font-size="13">shuffled (링크 순서 무작위)</text>
  <line x1="400" y1="50" x2="400" y2="230" stroke="currentColor"/>
  <line x1="400" y1="230" x2="640" y2="230" stroke="currentColor"/>
  <line x1="396" y1="230.0" x2="400" y2="230.0" stroke="currentColor"/>
  <text x="393" y="234.0" text-anchor="end" font-size="12">0</text>
  <line x1="396" y1="185.0" x2="400" y2="185.0" stroke="currentColor"/>
  <text x="393" y="189.0" text-anchor="end" font-size="12">100</text>
  <line x1="400" y1="185.0" x2="640" y2="185.0" stroke="#888" stroke-opacity="0.3"/>
  <line x1="396" y1="140.0" x2="400" y2="140.0" stroke="currentColor"/>
  <text x="393" y="144.0" text-anchor="end" font-size="12">200</text>
  <line x1="400" y1="140.0" x2="640" y2="140.0" stroke="#888" stroke-opacity="0.3"/>
  <line x1="396" y1="95.0" x2="400" y2="95.0" stroke="currentColor"/>
  <text x="393" y="99.0" text-anchor="end" font-size="12">300</text>
  <line x1="400" y1="95.0" x2="640" y2="95.0" stroke="#888" stroke-opacity="0.3"/>
  <line x1="396" y1="50.0" x2="400" y2="50.0" stroke="currentColor"/>
  <text x="393" y="54.0" text-anchor="end" font-size="12">400</text>
  <line x1="400" y1="50.0" x2="640" y2="50.0" stroke="#888" stroke-opacity="0.3"/>
  <rect x="425" y="159.5" width="36" height="70.5" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <text x="443" y="154.5" text-anchor="middle" font-size="12">156.64</text>
  <rect x="467" y="158.2" width="36" height="71.8" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
  <text x="485" y="153.2" text-anchor="middle" font-size="12">159.65</text>
  <text x="464" y="248" text-anchor="middle" font-size="12">최솟값</text>
  <rect x="540" y="117.4" width="36" height="112.6" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <text x="558" y="112.4" text-anchor="middle" font-size="12">250.2</text>
  <rect x="582" y="73.7" width="36" height="156.3" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
  <text x="600" y="68.7" text-anchor="middle" font-size="12">347.4</text>
  <text x="579" y="248" text-anchor="middle" font-size="12">중앙값</text>
  <text x="20" y="140" text-anchor="middle" font-size="12" transform="rotate(-90 20 140)">ns / 원소</text>
  <rect x="170" y="268" width="14" height="14" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <text x="190" y="280" font-size="12">intrusive (list_head 내장)</text>
  <rect x="380" y="268" width="14" height="14" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
  <text x="400" y="280" font-size="12">non-intrusive (노드 → data)</text>
</svg>
```
그림 4 — 원소 하나 순회 비용(ns, 6회 측정의 최솟값과 중앙값). 왼쪽과 오른쪽의 세로축 단위가 30배 이상 다르다.

해석(정직하게):

- **측정 잡음이 크다.** `load average 18`(6코어 VM에서 다른 노트 작업이 동시에 돌았다)이라 같은 측정이 2~3배 흔들린다. 그래서 최솟값(간섭이 가장 적었던 회)을 같이 봤다.
- sequential: intrusive가 최솟값 기준 약 1.6배(3.04 vs 4.73 ns), 중앙값 기준 약 1.5배 빠르다. non-intrusive는 원소마다 **떨어진 곳의 data를 한 번 더 읽어야** 해서 캐시 라인을 두 배로 쓴다.
- shuffled: 둘 다 원소당 150~480 ns — 캐시 미스와 TLB 미스가 지배하는 포인터 추적(pointer chasing)이다. 최솟값은 거의 같은데(156.64 vs 159.65), 이유는 `p->data` 읽기와 `p->next` 읽기가 서로 의존하지 않아 CPU가 두 미스를 **겹쳐서** 기다릴 수 있기 때문이다(memory-level parallelism). 임계 경로는 `next` 사슬 하나뿐이다. 중앙값은 non-intrusive가 1.4배 느리다.
- 결론: intrusive의 **결정적인** 장점은 속도보다 §2.1의 표 — 추가 할당 없음(실패 불가), 다중 소속, O(1) 삭제 — 이다. 속도 이점은 "있지만 상황에 따라 0~2배" 정도로 말하는 게 정확하다. 그리고 리스트 순회 자체가 캐시에 나쁘다는 사실이 더 크다(배열이 가능하면 배열).

### 2.9 함정

- **리스트는 스레드 안전하지 않다.** `list_add`/`list_del`/순회 전부 호출자가 락(spinlock/mutex)을 잡아야 한다. 락 없이 읽으려면 RCU 버전(`list_add_rcu`, `list_for_each_entry_rcu`, `list_del_rcu` + `synchronize_rcu`/`kfree_rcu`) — M 모듈에서 다룬다.
- **`LIST_HEAD` 초기화를 빼먹음.** `kzalloc`한 구조체의 `list_head`는 `{NULL, NULL}`이라 빈 리스트가 아니다. `list_empty`가 false를 돌려주고 순회가 NULL을 따라간다 → 반드시 `INIT_LIST_HEAD`.
- **`list_del` 후 `list_empty(&obj->node)`로 "이미 빠졌나" 검사.** poison이라 false다. 그런 용도면 `list_del_init`.
- **루프가 끝난 뒤 `pos` 사용.** 찾지 못하고 끝났으면 `pos`는 머리를 바깥 객체로 착각한 가짜 포인터다. 찾았는지는 별도 변수(`found = pos; break;`)로 기록한다.
- **`_safe`는 "내가 `pos`를 지우는 것"만 안전하다.** 다른 스레드가 `n`을 지우는 것은 막지 못한다 — 그건 락의 일이다.

---

## 3. ERR_PTR / IS_ERR / PTR_ERR — 포인터 하나에 "결과 또는 이유"를 싣기

### 3.1 직관: NULL은 "왜" 실패했는지 말하지 못한다

포인터를 돌려주는 함수가 실패하면 보통 NULL을 돌려준다. 그런데 드라이버의 `probe()`는 실패 **이유**에 따라 다르게 행동해야 한다. 예를 들어 클럭을 가져오다 실패했는데 그게 "클럭 컨트롤러가 아직 probe되지 않음(`-EPROBE_DEFER`)"이면 나중에 다시 시도해야 하고, "DT에 clocks 속성이 없음(`-ENOENT`)"이면 에러로 끝내야 한다. 펌웨어라면 `int get_clk(struct clk **out)`처럼 out-parameter + 에러 코드 반환으로 풀었을 것이다. 커널은 **포인터 값 자체에 에러 코드를 심는다.**

아이디어: 커널 주소 공간의 **맨 꼭대기 4095바이트**(`0xfffffffffffff001` ~ `0xffffffffffffffff`)에는 유효한 객체가 절대 없다. 그러니 `-1` ~ `-4095`를 그대로 `unsigned long`으로 바꾼 값(2의 보수라서 바로 그 꼭대기 영역)을 "에러 포인터"로 쓰자. errno는 전부 4095 이하이므로(`MAX_ERRNO`) 다 들어간다.

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
  <text x="40" y="56" font-size="12">0x0</text>
  <text x="325" y="56" text-anchor="middle" font-size="12">0xdead000000000100</text>
  <text x="640" y="56" text-anchor="end" font-size="12">0xffffffffffffffff</text>
  <rect x="40" y="70" width="15" height="36" fill="#d0564a" fill-opacity="0.3" stroke="#d0564a"/>
  <rect x="55" y="70" width="175" height="36" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="142" y="93" text-anchor="middle" font-size="12">user space</text>
  <rect x="230" y="70" width="190" height="36" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="325" y="93" text-anchor="middle" font-size="12">non-canonical (안 씀)</text>
  <rect x="420" y="70" width="180" height="36" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="510" y="93" text-anchor="middle" font-size="12">kernel space</text>
  <rect x="600" y="70" width="40" height="36" fill="#d0564a" fill-opacity="0.3" stroke="#d0564a"/>
  <text x="620" y="93" text-anchor="middle" font-size="12">ERR</text>
  <line x1="325" y1="62" x2="325" y2="106" stroke="#d0564a" stroke-dasharray="3,3"/>
  <text x="47" y="124" text-anchor="middle" font-size="12">NULL</text>
  <text x="325" y="124" text-anchor="middle" font-size="12">LIST_POISON1 (§2)</text>
  <line x1="600" y1="106" x2="310" y2="138" stroke="#d0564a" stroke-dasharray="4,3"/>
  <line x1="640" y1="106" x2="670" y2="138" stroke="#d0564a" stroke-dasharray="4,3"/>
  <text x="20" y="160" font-size="12">NULL = 0 : 실패했지만 이유 없음</text>
  <text x="20" y="182" font-size="12">ERR_PTR(e) = (void *)e  (e는 -1..-4095)</text>
  <text x="20" y="204" font-size="12">IS_ERR(p) : p ≥ (unsigned long)-4095</text>
  <text x="20" y="226" font-size="12">PTR_ERR(p) = (long)p</text>
  <rect x="310" y="138" width="360" height="102" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="325" y="157" font-size="12">-1</text>
  <text x="380" y="157" font-size="12">EPERM</text>
  <text x="500" y="157" font-size="12">0xffffffffffffffff</text>
  <text x="325" y="176" font-size="12">-2</text>
  <text x="380" y="176" font-size="12">ENOENT</text>
  <text x="500" y="176" font-size="12">0xfffffffffffffffe</text>
  <text x="325" y="195" font-size="12">-22</text>
  <text x="380" y="195" font-size="12">EINVAL</text>
  <text x="500" y="195" font-size="12">0xffffffffffffffea</text>
  <text x="325" y="214" font-size="12">-517</text>
  <text x="380" y="214" font-size="12">EPROBE_DEFER</text>
  <text x="500" y="214" font-size="12">0xfffffffffffffdfb</text>
  <text x="325" y="233" font-size="12">-4095</text>
  <text x="380" y="233" font-size="12">MAX_ERRNO</text>
  <text x="500" y="233" font-size="12">0xfffffffffffff001</text>
</svg>
```
그림 5 — 64비트 가상 주소 공간(비율 무시). 0번지(NULL)와 꼭대기 4095바이트(ERR 창)는 유효한 객체가 올 수 없는 곳이라 "특수 값"으로 쓴다. list poison(`0xdead…`)은 그 사이의 non-canonical 구멍을 쓴다. 오른쪽 아래는 ERR 창 안의 실제 값(예제 8, 9에서 출력한 숫자).

### 3.2 예제 8 — userspace에서 인코딩을 숫자로 보기

무엇을 확인하나: 커널 `err.h`와 같은 정의로 errno가 어떤 주소가 되는지, 경계(-4095와 -4096)에서 `IS_ERR`가 어떻게 바뀌는지, NULL과 에러 포인터를 구분하는 법.

```c
/* errptr.c — ERR_PTR 인코딩을 숫자로 보기 (커널 err.h 와 같은 정의) */
#include <stdio.h>
#include <stdbool.h>
#include <errno.h>

#define MAX_ERRNO 4095
#define IS_ERR_VALUE(x) ((unsigned long)(void *)(x) >= (unsigned long)-MAX_ERRNO)
static inline void *ERR_PTR(long error) { return (void *)error; }
static inline long PTR_ERR(const void *ptr) { return (long)ptr; }
static inline bool IS_ERR(const void *ptr) { return IS_ERR_VALUE((unsigned long)ptr); }
static inline bool IS_ERR_OR_NULL(const void *ptr) { return !ptr || IS_ERR(ptr); }

static int real_obj = 7;
static void *get_thing(int mode)   /* 커널 API 흉내: 포인터 또는 에러 */
{
	switch (mode) {
	case 0:  return &real_obj;
	case 1:  return ERR_PTR(-ENOENT);
	case 2:  return ERR_PTR(-517 /* EPROBE_DEFER (커널 내부 errno) */);
	default: return NULL;
	}
}

int main(void)
{
	long codes[] = { -1, -EINVAL, -ENOMEM, -ENOENT, -MAX_ERRNO, -MAX_ERRNO - 1 };
	printf("%-8s %-20s %s\n", "errno", "ERR_PTR(errno)", "IS_ERR");
	for (unsigned i = 0; i < sizeof(codes) / sizeof(codes[0]); i++)
		printf("%-8ld %#-20lx %d\n", codes[i], (unsigned long)ERR_PTR(codes[i]),
		       IS_ERR(ERR_PTR(codes[i])));
	printf("first error address : %#lx\n", (unsigned long)-MAX_ERRNO);
	printf("error window size   : %lu bytes (top of 64-bit space)\n",
	       0UL - (unsigned long)-MAX_ERRNO);
	for (int m = 0; m < 4; m++) {
		void *p = get_thing(m);
		if (IS_ERR(p))
			printf("mode %d: error %ld\n", m, PTR_ERR(p));
		else if (!p)
			printf("mode %d: NULL (IS_ERR=%d, IS_ERR_OR_NULL=%d)\n", m,
			       IS_ERR(p), IS_ERR_OR_NULL(p));
		else
			printf("mode %d: ok, value %d\n", m, *(int *)p);
	}
	return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/E5/u && gcc -std=gnu11 -Wall -Wextra -O2 ~/.elinux_scratch/E5/u/errptr.c -o errptr && ./errptr'
```

```text
errno    ERR_PTR(errno)       IS_ERR
-1       0xffffffffffffffff   1
-22      0xffffffffffffffea   1
-12      0xfffffffffffffff4   1
-2       0xfffffffffffffffe   1
-4095    0xfffffffffffff001   1
-4096    0xfffffffffffff000   0
first error address : 0xfffffffffffff001
error window size   : 4095 bytes (top of 64-bit space)
mode 0: ok, value 7
mode 1: error -2
mode 2: error -517
mode 3: NULL (IS_ERR=0, IS_ERR_OR_NULL=1)
```

출력에서 볼 것: `ERR_PTR`은 **아무 계산도 하지 않는 캐스트**다(`-22` → `0x…ffea`는 2의 보수 그대로). `IS_ERR`는 비교 한 번. `-4096`(`0x…f000`)부터는 에러가 아니다 — 꼭대기 페이지의 시작 주소는 제외된다. NULL은 `IS_ERR`로 잡히지 않는다. `-517`(`EPROBE_DEFER`)은 커널 내부 전용 errno라 유저 헤더 `<errno.h>`에는 없다(그래서 숫자로 썼다).

### 3.3 커널의 정의

```sh
limactl shell elinux -- bash -lc 'S=/var/lab/src/linux-6.18.55/include/linux/err.h; sed -n 18p $S; sed -n 28p $S; sed -n 39,42p $S; sed -n 55,58p $S; sed -n 68,71p $S; sed -n 82,85p $S'
```

```text
#define MAX_ERRNO	4095
#define IS_ERR_VALUE(x) unlikely((unsigned long)(void *)(x) >= (unsigned long)-MAX_ERRNO)
static __always_inline void * __must_check ERR_PTR(long error)
{
	return (void *) error;
}
static __always_inline long __must_check PTR_ERR(__force const void *ptr)
{
	return (long) ptr;
}
static __always_inline bool __must_check IS_ERR(__force const void *ptr)
{
	return IS_ERR_VALUE((unsigned long)ptr);
}
static __always_inline bool __must_check IS_ERR_OR_NULL(__force const void *ptr)
{
	return unlikely(!ptr) || IS_ERR_VALUE((unsigned long)ptr);
}
```

- `__must_check` — 반환값을 무시하면 gcc 경고(`warn_unused_result`). `IS_ERR(p);`만 쓰고 결과를 안 보면 잡힌다.
- `unlikely(...)` — 에러는 드문 경로라고 컴파일러에 알려 정상 경로를 직선 코드로 배치하게 한다(§5.5).
- `__force` — sparse에게 "주소 공간 표시(`__iomem`, `__user` 등)가 붙은 포인터를 넘겨도 경고하지 마라"(§6). 그래서 `IS_ERR()`는 `void __iomem *`에도 쓸 수 있다. 실제로 `devm_ioremap_resource()`는 `IOMEM_ERR_PTR()`(= `(__force void __iomem *)ERR_PTR(err)`)로 에러를 돌려준다.

### 3.4 예제 9 — 커널 모듈: 내 함수와 실제 커널 API의 ERR_PTR

무엇을 확인하나: "여러 실패 이유"를 가진 lookup 함수를 `ERR_PTR`로 짜고, `%pe` printk 포맷으로 에러 이름을 찍고, 실제 커널 API(`filp_open`)가 돌려주는 에러 포인터도 같은 방식으로 다룬다.

```c
// e5_err.c — ERR_PTR / IS_ERR / PTR_ERR / IS_ERR_OR_NULL / PTR_ERR_OR_ZERO / %pe
#include <linux/module.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/slab.h>

struct e5_cal { int gain; };
static struct e5_cal e5_table[2] = { { 100 }, { 250 } };

/* 실패 이유가 여러 가지인 lookup: NULL 대신 ERR_PTR 로 '왜'를 돌려준다 */
static struct e5_cal *e5_get_cal(int id)
{
	if (id < 0)
		return ERR_PTR(-EINVAL);
	if (id >= ARRAY_SIZE(e5_table))
		return ERR_PTR(-ENODEV);
	if (id == 1)
		return ERR_PTR(-EPROBE_DEFER);    /* "아직 준비 안 됨, 나중에 다시" */
	return &e5_table[id];
}
static struct e5_cal *e5_find_optional(int id)  /* optional: 없으면 NULL */
{
	return id == 0 ? &e5_table[0] : NULL;
}

static int __init e5_init(void)
{
	struct file *f;
	int id;

	pr_info("e5_err: -MAX_ERRNO as pointer = %px\n", ERR_PTR(-MAX_ERRNO));
	for (id = -1; id <= 2; id++) {
		struct e5_cal *c = e5_get_cal(id);
		if (IS_ERR(c))
			pr_info("e5_err: id=%d ptr=%px err=%ld (%pe)\n",
				id, c, PTR_ERR(c), c);
		else
			pr_info("e5_err: id=%d ok gain=%d\n", id, c->gain);
	}
	pr_info("e5_err: optional id=5 -> IS_ERR=%d IS_ERR_OR_NULL=%d\n",
		IS_ERR(e5_find_optional(5)), IS_ERR_OR_NULL(e5_find_optional(5)));
	pr_info("e5_err: PTR_ERR_OR_ZERO(ok)=%d PTR_ERR_OR_ZERO(err)=%d\n",
		PTR_ERR_OR_ZERO(e5_get_cal(0)), PTR_ERR_OR_ZERO(e5_get_cal(9)));

	f = filp_open("/e5_no_such_file", O_RDONLY, 0);   /* 실제 커널 API */
	if (IS_ERR(f))
		pr_info("e5_err: filp_open -> %px = %pe\n", f, f);
	else
		filp_close(f, NULL);
	return 0;
}
static void __exit e5_exit(void) { }
module_init(e5_init);
module_exit(e5_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("E5: ERR_PTR family");
```

```text
lab initramfs: 6.18.55 up 4.00s
[    4.829425] e5_err: loading out-of-tree module taints kernel.
[    4.837148] e5_err: -MAX_ERRNO as pointer = fffffffffffff001
[    4.837285] e5_err: id=-1 ptr=ffffffffffffffea err=-22 (-EINVAL)
[    4.837501] e5_err: id=0 ok gain=100
[    4.837521] e5_err: id=1 ptr=fffffffffffffdfb err=-517 (-EPROBE_DEFER)
[    4.837559] e5_err: id=2 ptr=ffffffffffffffed err=-19 (-ENODEV)
[    4.837579] e5_err: optional id=5 -> IS_ERR=0 IS_ERR_OR_NULL=1
[    4.837599] e5_err: PTR_ERR_OR_ZERO(ok)=0 PTR_ERR_OR_ZERO(err)=-19
[    4.837920] e5_err: filp_open -> fffffffffffffffe = -ENOENT
[    4.981972] reboot: Power down
```

출력에서 볼 것: `%pe`는 에러 포인터를 받아 `-EINVAL`, `-EPROBE_DEFER` 같은 **이름**을 찍는다(`CONFIG_SYMBOLIC_ERRNAME=y`일 때. 꺼져 있으면 숫자). 드라이버 로그에 `dev_err(dev, "failed to get clock: %pe\n", clk)` 패턴이 흔한 이유다. 정수 에러를 찍고 싶으면 `%pe`에 `ERR_PTR(ret)`를 넘긴다. `filp_open`도 실패하면 NULL이 아니라 `ERR_PTR(-ENOENT)`(`0x…fffe`)를 돌려줬다. `PTR_ERR_OR_ZERO(p)`는 "에러면 에러 코드, 아니면 0" — `return PTR_ERR_OR_ZERO(p);` 한 줄로 끝내는 관용구.

### 3.5 규칙 — API마다 실패 표현이 다르다

**함수마다 NULL을 돌려주는지 ERR_PTR을 돌려주는지 다르고, 이걸 헷갈리는 게 가장 흔한 버그다.** 문서(kernel-doc 주석의 `Return:` 줄)를 꼭 확인한다.

| API | 실패 시 | 검사 |
|---|---|---|
| `kmalloc`, `kzalloc`, `devm_kzalloc` | NULL | `if (!p) return -ENOMEM;` |
| `devm_ioremap_resource`, `devm_platform_ioremap_resource` | ERR_PTR | `IS_ERR` |
| `devm_ioremap` | NULL | `!p` |
| `devm_clk_get`, `devm_regulator_get`, `devm_gpiod_get` | ERR_PTR | `IS_ERR` (`-EPROBE_DEFER` 가능) |
| `devm_gpiod_get_optional`, `devm_clk_get_optional` | 없으면 NULL, 진짜 에러면 ERR_PTR | `IS_ERR`만 검사하고 NULL은 "없음"으로 통과 |
| `filp_open`, `kthread_create`, `platform_device_register_simple` | ERR_PTR | `IS_ERR` |
| `debugfs_create_dir` | ERR_PTR (그러나 결과를 검사하지 말라는 것이 debugfs 방침) | 검사 안 함 |

보조 함수:

- `ERR_CAST(p)` — 에러 포인터를 다른 타입의 포인터로 그대로 전달(`return ERR_CAST(clk);`). 캐스트 경고를 피하고 "에러를 그대로 넘긴다"는 의도를 드러낸다.
- `IS_ERR_OR_NULL(p)` — 쓸 일이 드물다. 이게 필요하다면 보통 API 설계가 모호하다는 신호다. 예외: `_optional` API처럼 NULL과 ERR이 둘 다 의미가 있을 때 정리(cleanup) 코드에서 "둘 다 아니면 해제".
- `dev_err_probe(dev, err, fmt, ...)` — 에러 로그 + 반환을 한 번에. `-EPROBE_DEFER`면 로그를 찍지 않고 deferred 사유로만 기록한다(D8 참고).

---

## 4. 에러 정리 — goto 사다리, `devm_*`, cleanup.h

### 4.1 직관: 자원은 쌓은 역순으로 내린다

Don이 SSD 펌웨어 초기화 코드에서 "클럭 켜기 → 전원 켜기 → PHY 초기화 → 인터럽트 등록"을 하다가 세 번째에서 실패하면, 앞의 두 개를 **역순으로** 끄고 나가야 했다. 리눅스 드라이버 `probe()`가 정확히 이 구조다. 방법은 세 가지가 있고, 모두 "획득의 역순 해제(LIFO)"라는 같은 원칙을 누가 기억하느냐만 다르다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="110" y="22" text-anchor="middle" font-size="13">goto 사다리 (사람이 기억)</text>
  <text x="340" y="22" text-anchor="middle" font-size="13">devm_* (디바이스 수명)</text>
  <text x="570" y="22" text-anchor="middle" font-size="13">cleanup.h (스코프 수명)</text>
  <rect x="10" y="40" width="200" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="110" y="62" text-anchor="middle" font-size="12">clk_prepare_enable() ok</text>
  <rect x="10" y="90" width="200" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="110" y="112" text-anchor="middle" font-size="12">regulator_enable() ok</text>
  <rect x="10" y="140" width="200" height="34" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="110" y="162" text-anchor="middle" font-size="12">request_irq() 실패</text>
  <rect x="10" y="190" width="200" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="110" y="212" text-anchor="middle" font-size="12">err_reg: 레귤레이터 off</text>
  <rect x="10" y="240" width="200" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="110" y="262" text-anchor="middle" font-size="12">err_clk: 클럭 off · return</text>
  <line x1="110" y1="74" x2="110" y2="88" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="110" y1="124" x2="110" y2="138" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="110" y1="174" x2="110" y2="188" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="110" y1="224" x2="110" y2="238" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="240" y="40" width="200" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="62" text-anchor="middle" font-size="12">devm A → devres push</text>
  <rect x="240" y="90" width="200" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="112" text-anchor="middle" font-size="12">devm B → devres push</text>
  <rect x="240" y="140" width="200" height="34" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="340" y="162" text-anchor="middle" font-size="12">probe 실패 또는 remove</text>
  <rect x="240" y="190" width="200" height="84" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="215" text-anchor="middle" font-size="12">드라이버 코어가</text>
  <text x="340" y="237" text-anchor="middle" font-size="12">devres 스택을 pop</text>
  <text x="340" y="259" text-anchor="middle" font-size="12">B → A (역순)</text>
  <line x1="340" y1="74" x2="340" y2="88" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="340" y1="124" x2="340" y2="138" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="340" y1="174" x2="340" y2="188" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="470" y="40" width="200" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="570" y="62" text-anchor="middle" font-size="12">buf __free(kfree) = kzalloc</text>
  <rect x="470" y="90" width="200" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="570" y="112" text-anchor="middle" font-size="12">guard(mutex)(&amp;lock)</text>
  <rect x="470" y="140" width="200" height="34" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="570" y="162" text-anchor="middle" font-size="12">return (어디서든)</text>
  <rect x="470" y="190" width="200" height="84" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="570" y="215" text-anchor="middle" font-size="12">컴파일러가 넣은 호출</text>
  <text x="570" y="237" text-anchor="middle" font-size="12">mutex_unlock → kfree</text>
  <text x="570" y="259" text-anchor="middle" font-size="12">(선언의 역순)</text>
  <line x1="570" y1="74" x2="570" y2="88" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="570" y1="124" x2="570" y2="138" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="570" y1="174" x2="570" y2="188" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="340" y="305" text-anchor="middle" font-size="12">공통점: 획득의 역순으로 해제 · 차이: 순서를 누가 기억하나 (사람 / devres 리스트 / 컴파일러)</text>
</svg>
```
그림 6 — 에러 정리 세 방식. 왼쪽은 레이블을 내려가며 떨어지는(fall-through) 사다리, 가운데는 디바이스에 매달린 devres 스택, 오른쪽은 변수 스코프에 매달린 cleanup 함수.

### 4.2 goto 사다리 규칙

커널 코딩 스타일 문서(`Documentation/process/coding-style.rst` 7장 "Centralized exiting of functions")가 권하는 형태다.

```c
static int imu_probe(struct platform_device *pdev)
{
	int ret;

	ret = clk_prepare_enable(priv->clk);
	if (ret)
		return ret;                 /* 아직 아무것도 안 잡았으면 그냥 return */
	ret = regulator_enable(priv->vdd);
	if (ret)
		goto err_clk;
	ret = request_irq(priv->irq, imu_isr, 0, "imu", priv);
	if (ret)
		goto err_reg;
	return 0;

err_reg:                                    /* 레이블 = "여기부터 되돌린다" */
	regulator_disable(priv->vdd);
err_clk:
	clk_disable_unprepare(priv->clk);
	return ret;
}
```

(위는 형태를 보여 주는 조각이라 단독 실행하지 않았다. 실행 가능한 같은 구조가 예제 10과 예제 11의 `e5_init`이다.)

- 레이블은 **무엇을 되돌리는지**로 이름 짓는다(`err_reg`, `err_clk`, 또는 `out_free_buf`). `err1`, `err2`는 중간에 단계를 끼워 넣을 때 번호가 꼬인다.
- 레이블은 획득의 **역순**으로 위에서 아래로 놓고, 아래로 떨어지며(fall-through) 앞 단계들을 차례로 되돌린다.
- 한 함수에 `return` 경로가 여러 개면 정리 코드가 복제되고, 복제는 언젠가 어긋난다 — 사다리는 정리 코드를 **한 곳**에 모은다. Don의 펌웨어에서 `do { ... } while (0)` + `break`로 하던 것과 같은 목적이다.
- 성공 경로는 `return 0;`으로 사다리 **위에서** 끝난다(정상 경로가 레이블을 지나가지 않게).

### 4.3 예제 10 — userspace: goto 사다리 vs cleanup 속성, 실패 주입

무엇을 확인하나: 4단계 초기화를 `goto` 사다리로 짜고 `fail_at`으로 원하는 단계에서 실패시켜 되돌림 순서를 본다. 같은 일을 GCC `__attribute__((cleanup(fn)))` — 커널 `__free()`/`guard()`의 기반 — 으로 짜서 비교한다.

```c
/* ladder.c — goto 정리 사다리 vs __attribute__((cleanup)) (fail_at 로 실패 주입) */
#include <stdio.h>
#include <stdlib.h>

static int fail_at;                                   /* 몇 번째 단계에서 실패시킬지 */
static int acquire(const char *what, int step)
{
	if (step == fail_at) { printf("  %s: FAIL\n", what); return -1; }
	printf("  %s: ok\n", what); return 0;
}
static void release(const char *what) { printf("  undo %s\n", what); }

static int probe_goto(void)                           /* 커널 전통 방식 */
{
	int ret;
	ret = acquire("clk_enable", 1);   if (ret) goto out;
	ret = acquire("regulator_on", 2); if (ret) goto err_clk;
	ret = acquire("request_irq", 3);  if (ret) goto err_reg;
	ret = acquire("register_dev", 4); if (ret) goto err_irq;
	return 0;
err_irq: release("request_irq");
err_reg: release("regulator_on");
err_clk: release("clk_enable");
out:     return ret;
}

/* cleanup 속성: 변수가 스코프를 벗어날 때 함수 호출 (커널 __free()/guard() 의 기반) */
static void free_buf(char **p) { if (*p) { printf("  auto free buf\n"); free(*p); } }
static void unlock(int **l)    { if (*l) printf("  auto unlock\n"); }

static int probe_cleanup(void)
{
	static int lock;
	int *held __attribute__((cleanup(unlock))) = &lock;      /* guard(mutex) 흉내 */
	printf("  lock\n");
	char *buf __attribute__((cleanup(free_buf))) = malloc(64); /* __free(kfree) 흉내 */
	if (!buf) return -1;
	if (acquire("parse_config", 2)) return -1;      /* goto 없이 그냥 return */
	printf("  work done\n");
	return 0;
}

int main(int argc, char **argv)
{
	fail_at = argc > 1 ? atoi(argv[1]) : 0;
	printf("[goto ladder, fail_at=%d]\n", fail_at);
	printf("  -> ret=%d\n", probe_goto());
	printf("[cleanup attr, fail_at=%d]\n", fail_at);
	printf("  -> ret=%d\n", probe_cleanup());
	return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/E5/u && gcc -std=gnu11 -Wall -Wextra -O2 ~/.elinux_scratch/E5/u/ladder.c -o ladder && ./ladder 3 && ./ladder 2 && ./ladder 0'
```

```text
[goto ladder, fail_at=3]
  clk_enable: ok
  regulator_on: ok
  request_irq: FAIL
  undo regulator_on
  undo clk_enable
  -> ret=-1
[cleanup attr, fail_at=3]
  lock
  parse_config: ok
  work done
  auto free buf
  auto unlock
  -> ret=0
[goto ladder, fail_at=2]
  clk_enable: ok
  regulator_on: FAIL
  undo clk_enable
  -> ret=-1
[cleanup attr, fail_at=2]
  lock
  parse_config: FAIL
  auto free buf
  auto unlock
  -> ret=-1
[goto ladder, fail_at=0]
  clk_enable: ok
  regulator_on: ok
  request_irq: ok
  register_dev: ok
  -> ret=0
[cleanup attr, fail_at=0]
  lock
  parse_config: ok
  work done
  auto free buf
  auto unlock
  -> ret=0
```

출력에서 볼 것: 사다리는 실패 지점 **바로 앞 단계부터** 역순으로 되돌린다(fail_at=3 → regulator, clk). cleanup 버전은 `goto`가 하나도 없는데, 실패든 성공이든 함수를 빠져나갈 때 **선언의 역순**(`buf` 먼저, 그다음 `held`)으로 정리 함수가 불렸다. 성공 경로에서도 `buf`가 해제된 점에 주의 — 성공 시 버퍼를 호출자에게 넘기려면 커널의 `no_free_ptr()`/`return_ptr()`로 "소유권을 빼 간다"고 표시해야 한다.

### 4.4 `devm_*` — 디바이스에 수명을 맡기기

`devm_`(device-managed) 접두어 API는 자원을 할당하면서 그 해제 함수를 `struct device`의 **devres 리스트**에 등록한다. `probe()`가 실패하거나 드라이버가 unbind(`remove()` 이후)될 때 드라이버 코어가 그 리스트를 **역순으로** 실행한다. 그래서 devm만 쓴 `probe()`에는 정리 코드가 없다.

- 대표: `devm_kzalloc`, `devm_ioremap_resource`/`devm_platform_ioremap_resource`, `devm_clk_get_enabled`, `devm_regulator_get_enable`, `devm_request_irq`/`devm_request_threaded_irq`, `devm_gpiod_get`, `devm_mutex_init`.
- devm 버전이 없는 자원은 `devm_add_action_or_reset(dev, fn, data)`로 직접 등록한다. 등록 자체가 실패하면 `fn(data)`를 즉시 불러 주므로("or_reset") 누수가 없다.
- 해제 시점이 `remove()`가 **끝난 뒤**라는 것이 핵심 함정이다. 예를 들어 `devm_request_irq`로 받은 IRQ는 `remove()`에서 하드웨어를 꺼도 아직 살아 있다 → IRQ 핸들러가 꺼진 하드웨어나 해제된 자원을 만질 수 있다. devm 자원 순서를 "획득 순서 = 역순 해제"에 맞게 짜야 한다.

### 4.5 cleanup.h — 6.18에 있는가? 있다

`include/linux/cleanup.h`는 6.5에 들어왔고(Peter Zijlstra), 6.18 랩 소스에 다음이 있다.

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/src/linux-6.18.55 && grep -n "define DEFINE_FREE\|define __free\|define no_free_ptr\|define return_ptr\|define guard\|define scoped_guard" include/linux/cleanup.h; grep -n "DEFINE_FREE(kfree," include/linux/slab.h; grep -n "DEFINE_GUARD(mutex," include/linux/mutex.h; grep -n "DEFINE_LOCK_GUARD_1(spinlock_irqsave," include/linux/spinlock.h; sed -n 210,213p include/linux/cleanup.h'
```

```text
210:#define DEFINE_FREE(_name, _type, _free) \
213:#define __free(_name)	__cleanup(__free_##_name)
227:#define no_free_ptr(p) \
230:#define return_ptr(p)	return no_free_ptr(p)
413:#define guard(_name) \
441:#define scoped_guard(_name, args...)	\
513:DEFINE_FREE(kfree, void *, if (!IS_ERR_OR_NULL(_T)) kfree(_T))
228:DEFINE_GUARD(mutex, struct mutex *, mutex_lock(_T), mutex_unlock(_T))
585:DEFINE_LOCK_GUARD_1(spinlock_irqsave, spinlock_t,
#define DEFINE_FREE(_name, _type, _free) \
	static inline void __free_##_name(void *p) { _type _T = *(_type *)p; _free; }

#define __free(_name)	__cleanup(__free_##_name)
```

구조가 보인다: `DEFINE_FREE(kfree, void *, ...)`가 `__free_kfree(void *p)`라는 정리 함수를 만들고, `__free(kfree)`는 `__cleanup(__free_kfree)` = `__attribute__((__cleanup__(__free_kfree)))`로 펼쳐진다 — 예제 10에서 손으로 쓴 것과 같다. `kfree` 정리 함수가 `IS_ERR_OR_NULL`을 검사하므로 NULL이나 에러 포인터인 채로 스코프를 나가도 안전하다. `guard(mutex)(&lock)`는 생성 시 `mutex_lock`, 스코프 끝에서 `mutex_unlock`을 부르는 숨은 변수를 만든다(C++ `std::lock_guard`와 같은 RAII). `scoped_guard(spinlock_irqsave, &lock) { ... }`는 블록 하나만 잠근다.

### 4.6 예제 11 — 커널 모듈: goto 사다리 + devm + __free/guard, 실패 주입

무엇을 확인하나: 플랫폼 드라이버 `probe()`에서 devm 액션 두 개를 등록하고, 그 안의 `e5_parse()`는 `__free(kfree)`와 `guard(mutex)`를 쓴다. 모듈 파라미터 `fail_at`(1=A, 2=B, 3=parse)으로 실패를 주입해 정리 순서를 본다. 모듈 init 자체는 전통적인 goto 사다리다.

```c
// e5_cleanup.c — goto 사다리 vs devm_* vs cleanup.h(__free, guard). fail_at=N 으로 실패 주입
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/cleanup.h>

static int fail_at;
module_param(fail_at, int, 0444);
static DEFINE_MUTEX(e5_lock);

static int step(const char *what, int n)
{
	if (n == fail_at) { pr_info("e5_cleanup:   %s FAIL\n", what); return -EIO; }
	pr_info("e5_cleanup:   %s ok\n", what);
	return 0;
}
static void e5_undo(void *what) { pr_info("e5_cleanup:   devm undo %s\n", (char *)what); }

static int e5_parse(void)            /* cleanup.h: 스코프 끝에서 자동 kfree / unlock */
{
	char *buf __free(kfree) = kzalloc(64, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	guard(mutex)(&e5_lock);
	pr_info("e5_cleanup:   parse: locked=%d\n", mutex_is_locked(&e5_lock));
	return step("parse", 3);     /* 실패든 성공이든 buf 해제 + unlock 자동 */
}

static int e5_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret;

	pr_info("e5_cleanup: probe (fail_at=%d)\n", fail_at);
	ret = step("res A", 1);
	if (ret) return ret;
	ret = devm_add_action_or_reset(dev, e5_undo, "A");
	if (ret) return ret;
	ret = step("res B", 2);
	if (ret) return ret;                 /* A 는 devres 가 자동 해제 */
	ret = devm_add_action_or_reset(dev, e5_undo, "B");
	if (ret) return ret;
	ret = e5_parse();
	pr_info("e5_cleanup:   after parse: locked=%d\n", mutex_is_locked(&e5_lock));
	return ret;                          /* 실패면 B, A 순으로 자동 undo */
}
static void e5_remove(struct platform_device *pdev)
{
	pr_info("e5_cleanup: remove (devm undo runs after this)\n");
}
static struct platform_driver e5_drv = {
	.probe = e5_probe, .remove = e5_remove,
	.driver = { .name = "e5_cleanup" },
};
static struct platform_device *e5_pdev;
static int __init e5_init(void)
{
	int ret = platform_driver_register(&e5_drv);
	if (ret)
		goto err;
	e5_pdev = platform_device_register_simple("e5_cleanup", -1, NULL, 0);
	ret = PTR_ERR_OR_ZERO(e5_pdev);
	if (ret)
		goto err_drv;
	return 0;
err_drv:
	platform_driver_unregister(&e5_drv);
err:
	return ret;
}
static void __exit e5_exit(void)
{
	platform_device_unregister(e5_pdev);
	platform_driver_unregister(&e5_drv);
}
module_init(e5_init);
module_exit(e5_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("E5: goto ladder, devm and scope-based cleanup");
```

게스트 스크립트(매번 `dmesg -c`로 비우고 insmod → rmmod):

```sh
for f in 0 2 3; do
  echo "== fail_at=$f"; dmesg -c > /dev/null
  insmod /e5_cleanup.ko fail_at=$f && rmmod e5_cleanup
  dmesg | grep -E "e5_cleanup" | sed 's/^\[[ 0-9.]*\] //'
done
```

```text
lab initramfs: 6.18.55 up 3.30s
== fail_at=0
e5_cleanup: loading out-of-tree module taints kernel.
e5_cleanup: probe (fail_at=0)
e5_cleanup:   res A ok
e5_cleanup:   res B ok
e5_cleanup:   parse: locked=1
e5_cleanup:   parse ok
e5_cleanup:   after parse: locked=0
e5_cleanup: remove (devm undo runs after this)
e5_cleanup:   devm undo B
e5_cleanup:   devm undo A
== fail_at=2
[    4.512021] e5_cleanup e5_cleanup: probe with driver e5_cleanup failed with error -5
e5_cleanup: probe (fail_at=2)
e5_cleanup:   res A ok
e5_cleanup:   res B FAIL
e5_cleanup e5_cleanup: probe with driver e5_cleanup failed with error -5
e5_cleanup:   devm undo A
== fail_at=3
[    4.812480] e5_cleanup e5_cleanup: probe with driver e5_cleanup failed with error -5
e5_cleanup: probe (fail_at=3)
e5_cleanup:   res A ok
e5_cleanup:   res B ok
e5_cleanup:   parse: locked=1
e5_cleanup:   parse FAIL
e5_cleanup:   after parse: locked=0
e5_cleanup e5_cleanup: probe with driver e5_cleanup failed with error -5
e5_cleanup:   devm undo B
e5_cleanup:   devm undo A
[    5.025322] reboot: Power down
```

출력에서 볼 것:

- **fail_at=0**: 정상 probe. `guard(mutex)` 안에서는 `locked=1`, 함수를 빠져나오자 `locked=0` — unlock 호출이 소스 어디에도 없다. `rmmod` 때 `remove()`가 먼저 불리고 **그 뒤에** devm 액션이 B → A 역순으로 실행됐다.
- **fail_at=2**: B에서 실패 → `return ret` 한 줄뿐인데 이미 등록된 A가 자동으로 되돌려졌다. 드라이버 코어가 "probe with driver e5_cleanup failed with error -5"(-EIO)를 경고 레벨로 찍었다(그래서 타임스탬프가 붙은 줄이 콘솔에도 한 번 더 보인다).
- **fail_at=3**: `e5_parse()`가 실패해도 mutex는 풀렸고(`locked=0`) `buf`는 해제됐다. 그다음 devm이 B, A를 되돌렸다.
- probe가 실패해도 `insmod`는 성공한다 — 모듈 init은 드라이버 **등록**만 하고, probe는 디바이스와 매칭될 때 따로 돈다. 그래서 `rmmod`도 정상.

### 4.7 함정 — 섞어 쓸 때

| 함정 | 설명 |
|---|---|
| devm 자원을 사다리에서 또 해제 | 이중 해제. devm으로 받은 것은 손대지 않는다 |
| 같은 함수에서 `goto`와 `__free` 혼용 | cleanup.h 주석이 명시적으로 금지: "usage of "goto" and cleanup helpers is never mixed in the same function". 아래 예제 12 |
| `__free` 변수를 함수 맨 위에 `= NULL`로 몰아서 선언 | 해제 순서가 선언 순서의 역순이라, 의존 관계가 있는 자원 순서가 뒤집힐 수 있다. "선언과 할당을 한 문장에" 가 권장 |
| 성공 경로에서 소유권 이전을 잊음 | 스코프 끝에서 해제돼 호출자가 해제된 메모리를 받는다. `return_ptr(p)` 또는 `ptr = no_free_ptr(p)` |
| devm 해제가 remove 이후라는 것을 잊음 | IRQ·workqueue가 remove 이후에도 돈다. 필요하면 remove에서 `disable_irq`/`cancel_work_sync` 또는 devm 순서를 조정 |

### 4.8 예제 12 — goto가 cleanup 변수의 선언을 건너뛰면

무엇을 확인하나: `goto`로 `__attribute__((cleanup))` 변수의 초기화를 건너뛰면 무슨 일이 일어나는지. 일부러 경고가 나는 코드다.

```c
/* gotoscope.c — goto 가 cleanup 변수 선언을 건너뛰면? */
#include <stdio.h>
#include <stdlib.h>
static void free_buf(char **p) { printf("free_buf(%p)\n", (void *)*p); free(*p); }
int f(int fail)
{
	if (fail)
		goto out;
	char *buf __attribute__((cleanup(free_buf))) = malloc(16);
	if (!buf)
		return -1;
	return 0;
out:
	return -1;
}
int main(void) { return f(1) + 1; }
```

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/E5/u && gcc -std=gnu11 -Wall -Wextra -O2 ~/.elinux_scratch/E5/u/gotoscope.c -o gotoscope; echo rc=$?; ./gotoscope; echo exit=$?'
```

```text
In function ‘free_buf’,
    inlined from ‘f’ at /Users/donh/.elinux_scratch/E5/u/gotoscope.c:9:8:
/Users/donh/.elinux_scratch/E5/u/gotoscope.c:4:72: warning: ‘buf’ may be used uninitialized [-Wmaybe-uninitialized]
    4 | static void free_buf(char **p) { printf("free_buf(%p)\n", (void *)*p); free(*p); }
      |                                                                        ^~~~~~~~
/Users/donh/.elinux_scratch/E5/u/gotoscope.c: In function ‘f’:
/Users/donh/.elinux_scratch/E5/u/gotoscope.c:9:15: note: ‘buf’ was declared here
    9 |         char *buf __attribute__((cleanup(free_buf))) = malloc(16);
      |               ^~~
...
rc=0
free_buf((nil))
exit=0
```

출력에서 볼 것: gcc는 **에러가 아니라 경고**만 내고 빌드를 끝냈다(rc=0, 중간의 `printf` 인라인 경고 몇 줄은 생략). 실행하면 `goto out`이 `buf` 선언을 건너뛰었는데도 스코프를 나갈 때 `free_buf`가 **초기화되지 않은 값**으로 불렸다. 이번엔 스택 값이 우연히 0이라 `free(NULL)`로 무사했지만 정의되지 않은 동작이다. (clang은 이 패턴을 컴파일 에러로 막는다.) 커널 빌드 로그 수천 줄 속에서 이런 경고 하나는 묻히기 쉽다 — 그래서 "한 함수에서는 둘 중 하나만" 규칙이 있다.

---

## 5. 작은 매크로들 — Don의 레지스터 세계

### 5.1 BIT · GENMASK · FIELD_PREP · FIELD_GET

Don이 데이터시트의 레지스터 표를 코드로 옮길 때 `#define CTRL1_ODR_SHIFT 4`, `#define CTRL1_ODR_MASK (0xF << 4)`, `reg = (reg & ~MASK) | ((val << SHIFT) & MASK)`를 썼을 것이다. 커널은 이걸 **마스크 하나만 정의**하면 되도록 표준화했다. shift는 마스크에서 컴파일러가 계산한다(`__builtin_ctzll`).

| 매크로 | 뜻 | 예 |
|---|---|---|
| `BIT(n)` | `1UL << n` | `BIT(0)` = `0x1` |
| `GENMASK(h, l)` | 비트 h..l이 1인 마스크 (h ≥ l, **높은 비트 먼저**) | `GENMASK(7, 4)` = `0xf0` |
| `GENMASK_ULL`, `BIT_ULL` | 64비트 버전 (32비트 커널에서도 64비트) | |
| `FIELD_PREP(mask, val)` | val을 mask 위치로 shift (+ 범위 검사) | `FIELD_PREP(GENMASK(7,4), 5)` = `0x50` |
| `FIELD_GET(mask, reg)` | reg에서 mask 필드를 꺼내 0번 비트로 내림 | `FIELD_GET(GENMASK(7,4), 0x5b)` = `5` |
| `FIELD_MAX(mask)`, `FIELD_FIT(mask, val)` | 필드 최댓값, 들어가는지 검사 | |

가상의 IMU `CTRL1` 레지스터(8비트)로 그려 보면:

```
CTRL1 (8-bit)       bit:  7   6   5   4 | 3   2 |  1  |  0
                        [   ODR[3:0]    ]  FS[1:0]  BDU   EN
FIELD_PREP(ODR, 5)  = 0101 0000 = 0x50
FIELD_PREP(FS, 2)   = 0000 1000 = 0x08
BDU | EN            = 0000 0011 = 0x03
CTRL1               = 0101 1011 = 0x5b
RMW: ODR := 7       = 0111 1011 = 0x7b
```

`FIELD_PREP`의 가치는 **검사**에 있다: 마스크가 상수가 아니거나, 0이거나, 상수 값이 필드보다 크면 **컴파일 에러**가 난다(예제 14의 CASE 5). 손으로 쓴 shift/mask에서 가장 흔한 버그 — 값이 넘쳐서 옆 필드를 덮는 것 — 를 빌드 단계에서 막는다.

### 5.2 ARRAY_SIZE · BUILD_BUG_ON

- `ARRAY_SIZE(arr)` = `sizeof(arr) / sizeof((arr)[0]) + __must_be_array(arr)`. 뒤의 `__must_be_array`는 `arr`가 포인터면 컴파일 에러를 내는 0짜리 식이다. 배열이 함수 인자로 넘어오면 포인터로 바뀌므로(decay) 그 자리에서 `ARRAY_SIZE`를 쓰면 8/2 = 4 같은 엉터리가 나오는데, 이걸 막는다(예제 14의 CASE 2).
- `BUILD_BUG_ON(cond)` — cond가 컴파일 타임에 참이면 빌드 실패. 런타임 비용 0인 `assert`. 펌웨어에서 `typedef char check[(cond) ? -1 : 1];` 같은 트릭으로 하던 것. 예: 레지스터 맵 구조체 크기 검사, 표 길이와 비트 폭 일치 검사(예제 13). 메시지를 붙이려면 `BUILD_BUG_ON_MSG`, 파일 스코프에서는 `static_assert(cond, "msg")`.

### 5.3 min · max · clamp — 타입 안전

C 교과서의 `#define MIN(a,b) ((a) < (b) ? (a) : (b))`에는 두 가지 문제가 있다. (1) 인자를 두 번 평가한다(`MIN(i++, j)`), (2) signed와 unsigned를 비교하면 C의 일반 산술 변환으로 `-1`이 `0xffffffff`가 되어 `MIN(10u, -1)`이 10이 된다. 커널 `min()`은 각 인자를 `__auto_type` 임시 변수에 **한 번만** 담고, 부호가 섞이면 `BUILD_BUG_ON_MSG`로 "signedness error"를 낸다(예제 14의 CASE 4). 6.x의 규칙은 `include/linux/minmax.h` 주석대로 "부호 사용이 호환돼야 한다 — 단, 컴파일 타임에 음수가 아님이 확실한 signed 값은 unsigned와 비교해도 된다"이다.

- 타입이 정말 다르면 `min_t(type, a, b)` — 둘 다 `type`으로 캐스트 후 비교(어떤 타입으로 비교할지 **명시**하는 것이 핵심).
- `clamp(val, lo, hi)` — 범위 제한. DT나 사용자 입력으로 받은 ODR 값을 하드웨어 범위로 자를 때.
- `umin`/`umax`(둘 다 unsigned로 비교)도 있다.

### 5.4 엔디안 — cpu_to_le32 · le32_to_cpu · __le32

장치 레지스터, DMA 디스크립터, 네트워크/USB 패킷 필드는 **엔디안이 정해져 있다**(NVMe는 little-endian, 네트워크·DT는 big-endian). CPU의 엔디안과 무관하게 같은 바이트가 나와야 한다. Don이 NVMe 명령 구조체를 다룰 때 Cortex-R이 LE라서 변환 없이 썼을 텐데, 리눅스 드라이버는 BE CPU(일부 PowerPC, MIPS 설정)에서도 같은 코드가 돌아야 하므로 변환을 **항상 쓴다** — LE CPU에서 `cpu_to_le32`는 아무 일도 안 하는 매크로라 비용은 0이다.

| 함수 | 방향 |
|---|---|
| `cpu_to_le32(x)` / `cpu_to_be32(x)` | CPU 값 → LE/BE 저장 형식 (`__le32`/`__be32` 반환) |
| `le32_to_cpu(x)` / `be32_to_cpu(x)` | 저장 형식 → CPU 값 |
| `le32_to_cpup(p)`, `get_unaligned_le32(p)` | 포인터에서 읽기 (정렬 안 된 버퍼는 `get_unaligned_*`) |
| `le32_add_cpu(&x, n)` | 저장 형식 필드에 더하기 |

타입 `__le32`, `__be32`는 `include/uapi/linux/types.h`에서 `typedef __u32 __bitwise __le32;`로 정의된다. gcc에게는 그냥 `u32`지만, **sparse에게는 `u32`와 섞으면 경고가 나는 별개 타입**이다(§6). 구조체 필드를 `__le32`로 선언해 두는 것이 문서이자 검사다.

참고로 MMIO 접근자 `readl()`/`writel()`은 **little-endian 장치 레지스터**를 가정하고 내부에서 `le32_to_cpu`를 해 준다. BE 레지스터 장치면 `ioread32be()`.

### 5.5 likely · unlikely

`likely(x)` = `__builtin_expect(!!(x), 1)`. 분기 예측기에 직접 힌트를 주는 게 아니라(arm64엔 그런 명령이 없다), **컴파일러가 코드 배치를 바꾼다**: 예상 경로를 fall-through 직선 코드로, 드문 경로를 함수 뒤쪽으로 보내 I-cache와 분기 예측기를 돕는다. 규칙: **측정했거나 명백할 때만** — 에러 경로(`if (unlikely(!p))`), fast path 검사. 틀리게 쓰면 오히려 느려진다. `IS_ERR()`가 이미 내부에 `unlikely`를 품고 있으므로 `if (unlikely(IS_ERR(p)))`는 중복이다. (`CONFIG_PROFILE_ANNOTATED_BRANCHES`를 켜면 힌트가 맞았는지 통계를 볼 수 있다.)

### 5.6 READ_ONCE · WRITE_ONCE — 미리보기

컴파일러는 "다른 스레드가 이 변수를 바꿀 수 있다"는 것을 모른다. 그래서 루프 안의 `while (!flag)`를 레지스터에 한 번 읽고 무한 루프로 만들거나, 두 번의 쓰기를 합치거나, 64비트 쓰기를 두 번의 32비트 쓰기로 쪼갤(tearing) 수 있다. `READ_ONCE(x)`/`WRITE_ONCE(x, v)`는 **정확히 한 번, 쪼개지 않고** 접근하라는 표시다(내부적으로 `volatile` 접근). 펌웨어에서 ISR과 공유하는 변수에 `volatile`을 붙이던 것과 같은 목적인데, 커널은 변수 선언이 아니라 **접근 지점**에 붙인다.

중요: `READ_ONCE`는 **메모리 배리어가 아니다**. 다른 CPU에서 순서가 보장되지 않는다. 순서가 필요하면 `smp_load_acquire`/`smp_store_release`나 락. 락으로 보호된 변수에는 필요 없다. 자세한 건 M 모듈(동시성)에서.

### 5.7 예제 13 — 커널 모듈: 레지스터 필드, 엔디안, min/clamp, 그리고 진짜 MMIO

무엇을 확인하나: 가상 IMU 레지스터로 `FIELD_PREP`/`FIELD_GET`/RMW를 하고, 엔디안 변환 바이트를 찍고, QEMU `virt` 보드의 진짜 장치 PL031 RTC(`pl031@9010000`, `virt.dts`에 있음)를 `ioremap`해서 PrimeCell 주변장치 ID 레지스터(`0xFE0`~`0xFEC`, 각 레지스터의 하위 8비트가 ID 1바이트)를 읽어 `FIELD_GET`으로 해석한다.

```c
// e5_regs.c — BIT/GENMASK/FIELD_*, ARRAY_SIZE, min/clamp, 엔디안, READ_ONCE, 실제 MMIO(PL031)
#include <linux/module.h>
#include <linux/bitfield.h>
#include <linux/io.h>
#include <linux/minmax.h>
#include <linux/build_bug.h>

/* 가상의 IMU CTRL1 레지스터: [7:4] ODR, [3:2] FS, [1] BDU, [0] EN */
#define CTRL1_ODR	GENMASK(7, 4)
#define CTRL1_FS	GENMASK(3, 2)
#define CTRL1_BDU	BIT(1)
#define CTRL1_EN	BIT(0)
static const u16 odr_hz[] = { 0, 13, 26, 52, 104, 208, 416, 833 };

#define PL031_BASE	0x09010000      /* QEMU virt RTC (virt.dts 의 pl031@9010000) */
#define PL031_DR	0x000
#define PL031_PERIPHID0	0xfe0

static int __init e5_init(void)
{
	void __iomem *rtc;
	u32 reg = 0, id = 0, want = 900, flag = 0;
	__le32 wire;
	int i;

	BUILD_BUG_ON(ARRAY_SIZE(odr_hz) != 8);        /* 표와 비트 폭이 맞는지 컴파일 때 검사 */
	reg |= FIELD_PREP(CTRL1_ODR, 5) | FIELD_PREP(CTRL1_FS, 2) | CTRL1_BDU | CTRL1_EN;
	pr_info("e5_regs: ODR mask=%#lx FS mask=%#lx -> CTRL1=%#04x\n", CTRL1_ODR, CTRL1_FS, reg);
	pr_info("e5_regs: decode ODR=%lu (%u Hz) FS=%lu EN=%d\n", FIELD_GET(CTRL1_ODR, reg),
		odr_hz[FIELD_GET(CTRL1_ODR, reg)], FIELD_GET(CTRL1_FS, reg), !!(reg & CTRL1_EN));
	reg &= ~CTRL1_ODR;                             /* read-modify-write: 필드만 교체 */
	reg |= FIELD_PREP(CTRL1_ODR, 7);
	pr_info("e5_regs: RMW ODR=7 -> CTRL1=%#04x, ARRAY_SIZE=%zu\n", reg, ARRAY_SIZE(odr_hz));
	pr_info("e5_regs: min(want,833)=%u clamp(want,13,833)=%u min_t(int,-1,5)=%d\n",
		min(want, 833U), clamp(want, 13U, 833U), min_t(int, -1, 5));

	wire = cpu_to_le32(0x11223344);                /* 장치/패킷이 LE 로 정해진 필드 */
	pr_info("e5_regs: cpu_to_le32 bytes=%*ph back=%#x be32 bytes=%*ph\n",
		4, &wire, le32_to_cpu(wire), 4, &(__be32){ cpu_to_be32(0x11223344) });

	WRITE_ONCE(flag, 1);                           /* 컴파일러가 접근을 합치거나 지우지 못함 */
	if (unlikely(READ_ONCE(flag) != 1))
		return -EIO;

	rtc = ioremap(PL031_BASE, 0x1000);
	if (!rtc)
		return -ENOMEM;
	for (i = 0; i < 4; i++)                        /* PeriphID0..3: 각 레지스터의 [7:0] */
		id |= FIELD_GET(GENMASK(7, 0), readl(rtc + PL031_PERIPHID0 + 4 * i)) << (8 * i);
	pr_info("e5_regs: PL031 periphid=%#010x part=%#05lx designer=%#lx rev=%lu, RTCDR=%u\n",
		id, FIELD_GET(GENMASK(11, 0), id), FIELD_GET(GENMASK(19, 12), id),
		FIELD_GET(GENMASK(23, 20), id), readl(rtc + PL031_DR));
	iounmap(rtc);
	return 0;
}
static void __exit e5_exit(void) { }
module_init(e5_init);
module_exit(e5_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("E5: register field helpers, endian, READ_ONCE, MMIO");
```

게스트 스크립트: `insmod /e5_regs.ko && insmod /e5_sparse_ok.ko && dmesg | grep -E "e5_regs|e5_sparse"; date +%s` (뒤의 `e5_sparse_ok` 줄은 §6에서 설명한다)

```text
lab initramfs: 6.18.55 up 4.22s
[    5.050522] e5_regs: loading out-of-tree module taints kernel.
[    5.074143] e5_regs: ODR mask=0xf0 FS mask=0xc -> CTRL1=0x5b
[    5.074212] e5_regs: decode ODR=5 (208 Hz) FS=2 EN=1
[    5.074229] e5_regs: RMW ODR=7 -> CTRL1=0x7b, ARRAY_SIZE=8
[    5.074239] e5_regs: min(want,833)=833 clamp(want,13,833)=833 min_t(int,-1,5)=-1
[    5.074254] e5_regs: cpu_to_le32 bytes=44 33 22 11 back=0x11223344 be32 bytes=11 22 33 44
[    5.074471] e5_regs: PL031 periphid=0x00141031 part=0x031 designer=0x41 rev=1, RTCDR=1791664981
[    5.175840] e5_sparse_ok: RTCDR=1791664981 payload_len=64
[    5.175901] e5_sparse_ok: raw bits=0x40
1791664981
[    5.440788] reboot: Power down
```

출력에서 볼 것:

- `CTRL1=0x5b`, RMW 후 `0x7b` — §5.1의 비트 그림과 정확히 같다. RMW는 `reg &= ~MASK; reg |= FIELD_PREP(MASK, v);` 두 줄이 정석이다(커널엔 `FIELD_MODIFY`/`u32_replace_bits()` 같은 헬퍼도 있다).
- `cpu_to_le32(0x11223344)`의 메모리 바이트는 `44 33 22 11`(arm64는 LE라 변환 없음), `cpu_to_be32`는 `11 22 33 44`(바이트 스왑). `%*ph`는 버퍼를 hex 바이트로 찍는 printk 포맷이다.
- PL031: `periphid = 0x00141031` → part number `0x031`(PL**031**), designer `0x41`(= ASCII `'A'`, ARM), revision 1. 데이터시트의 "PeriphID 레지스터는 4개로 나뉘어 각 하위 8비트만 유효"를 `FIELD_GET(GENMASK(7, 0), ...)`로 그대로 옮겼다. `RTCDR`(현재 시각, 초)은 게스트의 `date +%s`와 같은 1791664981이다(2026-10-10 20:43:01 UTC).
- `min_t(int, -1, 5)`는 -1. 같은 비교를 `min(u32 변수, int 변수)`로 하면 컴파일이 안 된다(다음 예제).

### 5.8 예제 14 — 컴파일 타임 검사 5종을 일부러 터뜨리기

무엇을 확인하나: `BUILD_BUG_ON`, `ARRAY_SIZE`, `container_of`, `min`, `FIELD_PREP`이 각각 어떤 실수를 **빌드 단계에서** 막는지. `KCFLAGS=-DCASE=n`으로 한 번에 한 실수만 켠다. CASE=0은 정상 코드(빌드 성공 확인용, 로드하지 않음).

```c
// e5_bad.c — 컴파일 타임 검사가 막아 주는 실수 5종 (빌드 실패가 정상)
#include <linux/module.h>
#include <linux/bitfield.h>
#include <linux/minmax.h>
#include <linux/list.h>

struct e5_s { int irq; struct list_head node; };

static int __init e5_init(void)
{
	static const u16 tbl[8];
	const u16 *p = tbl;
	struct e5_s s = { 0 };
	int *ip = &s.irq;
	u32 reg, u = 10;
	int neg = -1;

	reg = FIELD_PREP(GENMASK(7, 4), 2) + u + neg + p[0] + *ip;   /* 정상 코드 */
#if CASE == 1
	BUILD_BUG_ON(sizeof(tbl) != 8);                        /* 16 != 8 */
#elif CASE == 2
	reg = ARRAY_SIZE(p);                                   /* 포인터에 ARRAY_SIZE */
#elif CASE == 3
	reg = container_of(ip, struct e5_s, node)->irq;        /* 타입 불일치 */
#elif CASE == 4
	reg = min(u, neg);                                     /* unsigned vs signed */
#elif CASE == 5
	reg = FIELD_PREP(GENMASK(7, 4), 20);                   /* 20 은 4비트 필드에 안 들어감 */
#endif
	return reg ? 0 : -EINVAL;
}
module_init(e5_init);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("E5: compile-time checks");
```

```sh
limactl shell elinux -- bash -lc 'mkdir -p /var/tmp/E5/bad && cp ~/.elinux_scratch/E5/bad/* /var/tmp/E5/bad/ && cd /var/tmp/E5/bad && for c in 0 1 2 3 4 5; do echo "##### CASE=$c"; rm -f e5_bad.o; make -C /var/lab/out/linux-build M=$PWD KCFLAGS=-DCASE=$c modules 2>&1 | grep -E "error:|warning:|LD \[M\]" ; done'
```

```text
##### CASE=0
  LD [M]  e5_bad.ko
##### CASE=1
/var/lab/src/linux-6.18.55/include/linux/compiler_types.h:610:45: error: call to ‘__compiletime_assert_418’ declared with attribute error: BUILD_BUG_ON failed: sizeof(tbl) != 8
##### CASE=2
/var/lab/src/linux-6.18.55/include/linux/array_size.h:11:38: warning: division ‘sizeof (const u16 * {aka const short unsigned int *}) / sizeof (u16 {aka const short unsigned int})’ does not compute the number of array elements [-Wsizeof-pointer-div]
/var/lab/src/linux-6.18.55/include/linux/compiler.h:197:67: error: static assertion failed: "must be array"
##### CASE=3
/var/lab/src/linux-6.18.55/include/linux/build_bug.h:78:41: error: static assertion failed: "pointer type mismatch in container_of()"
##### CASE=4
/var/lab/src/linux-6.18.55/include/linux/compiler_types.h:610:45: error: call to ‘__compiletime_assert_420’ declared with attribute error: min(u, neg) signedness error
##### CASE=5
/var/lab/src/linux-6.18.55/include/linux/compiler_types.h:610:45: error: call to ‘__compiletime_assert_420’ declared with attribute error: FIELD_PREP: value too large for the field
```

출력에서 볼 것: CASE=0은 경고 없이 링크까지 갔다. 나머지는 모두 빌드 실패이고 메시지가 원인을 그대로 말한다. 두 종류의 메커니즘이 보인다 — `static assertion failed`(C11 `_Static_assert`, 파서 단계에서 바로 판정: `ARRAY_SIZE`, `container_of`)와 `call to '__compiletime_assert_NNN' declared with attribute error`(조건이 최적화 후에야 상수가 되는 경우를 위해, 조건이 참이면 "호출하면 에러"인 함수 호출을 남기는 트릭: `BUILD_BUG_ON`, `min`, `FIELD_PREP`). 처음에 다섯 실수를 한 파일에 다 넣었더니 앞의 두 `static_assert`만 보고하고 뒤의 셋은 보고되지 않았다 — 두 번째 종류는 앞 단계 에러가 없어야 판정되기 때문이다. 그래서 CASE로 나눴다.

---

## 6. sparse — gcc가 못 보는 실수를 잡는 주석들

### 6.1 직관: 같은 `u32 *`라도 "어느 주소 공간의 포인터인가"가 다르다

Don의 SoC 세계에서 주소는 하나의 공간이 아니다. 같은 `0x9010000`이라도 CPU의 물리 주소, IOMMU 뒤의 DMA 주소, 커널 가상 주소가 다르다. 리눅스 커널 코드에는 성질이 다른 포인터가 섞여 다닌다:

| 주석 | 의미 | 직접 역참조 | 올바른 접근 |
|---|---|---|---|
| (없음) `__kernel` | 커널 가상 주소의 일반 메모리 | 가능 | `*p` |
| `__user` | **유저 프로세스** 주소. 커널 페이지 테이블에선 의미가 다르거나 매핑이 없을 수 있음, 악의적일 수 있음 | 금지 | `copy_from_user`, `get_user` (E6) |
| `__iomem` | `ioremap`한 **장치 레지스터** | 금지 | `readl`/`writel`, `ioread32` |
| `__percpu` | per-CPU 변수의 오프셋 | 금지 | `this_cpu_ptr`, `per_cpu_ptr` |
| `__rcu` | RCU로 보호된 포인터 | 금지 | `rcu_dereference` |
| `__bitwise` 타입 (`__le32`, `__be32`, `gfp_t`, `__poll_t`) | 정수지만 다른 정수와 섞으면 안 되는 타입 | 산술 금지 | 변환 함수 |
| `__force` | "이 캐스트는 의도한 것" — 위 검사를 끄는 표시 | | 최소한으로 |

gcc에게는 이 주석들이 **아무 의미 없다.** `include/linux/compiler_types.h`를 보면 `__CHECKER__`가 정의된 경우(= sparse가 돌 때)에만 속성이 붙는다.

```sh
limactl shell elinux -- bash -lc 'sed -n 42,53p /var/lab/src/linux-6.18.55/include/linux/compiler_types.h; sed -n 66p /var/lab/src/linux-6.18.55/include/linux/compiler_types.h; sed -n 38,39p /var/lab/src/linux-6.18.55/include/uapi/linux/types.h'
```

```text
/* sparse defines __CHECKER__; see Documentation/dev-tools/sparse.rst */
#ifdef __CHECKER__
/* address spaces */
# define __kernel	__attribute__((address_space(0)))
# define __user		__attribute__((noderef, address_space(__user)))
# define __iomem	__attribute__((noderef, address_space(__iomem)))
# define __percpu	__attribute__((noderef, address_space(__percpu)))
# define __rcu		__attribute__((noderef, address_space(__rcu)))
static inline void __chk_user_ptr(const volatile void __user *ptr) { }
static inline void __chk_io_ptr(const volatile void __iomem *ptr) { }
/* other */
# define __force	__attribute__((force))
# define __iomem
typedef __u32 __bitwise __le32;
typedef __u32 __bitwise __be32;
```

- `address_space(X)` — 포인터에 "주소 공간 꼬리표"를 단다. 꼬리표가 다른 포인터끼리 대입·전달하면 경고.
- `noderef` — 이 포인터는 `*p`, `p->x`로 직접 역참조하면 경고.
- `__bitwise` — 정수 타입에 "제한(restricted)" 꼬리표. 같은 꼬리표끼리 `&`, `|`, `==`는 되지만 일반 정수와 섞거나 산술하면 경고.
- 마지막 `# define __iomem`(66행)은 `#else` 쪽 — gcc가 볼 때는 빈 매크로다.

왜 `__iomem` 포인터를 직접 역참조하면 안 되나? (1) 컴파일러가 접근을 합치거나 순서를 바꾸거나 쪼갤 수 있다(레지스터는 읽기 자체가 부수 효과 — read-to-clear). (2) 아키텍처에 따라 MMIO는 특수 명령이나 배리어가 필요하다(`readl`은 arm64에서 load 뒤에 배리어를 넣어 이후 DMA 버퍼 읽기와 순서를 맞춘다). (3) 일부 아키텍처(x86의 port I/O)는 메모리 명령으로 접근할 수 없다. `readl`/`writel`은 이 모든 것을 처리하고 LE 변환까지 한다.

### 6.2 예제 15 — 일부러 틀린 모듈에 make C=2

무엇을 확인하나: gcc로는 경고 0개인 코드에서 sparse가 세 종류의 실수를 잡는 것을 본다. `make C=1`은 다시 컴파일되는 파일만, `C=2`는 모든 소스 파일을 검사한다. 이 모듈은 로드하지 않는다.

```c
// e5_sparse.c — gcc 는 조용하지만 sparse 가 잡는 실수 3종 (로드하지 않는다)
#include <linux/module.h>
#include <linux/io.h>
#include <linux/uaccess.h>

struct e5_pkt_hdr {               /* 와이어 포맷: little-endian 으로 정해진 헤더 */
	__le32 magic;
	__le32 len;
};

/* 실수 1: __iomem 포인터를 직접 역참조 */
static __maybe_unused u32 e5_read_status(void __iomem *regs)
{
	u32 __iomem *r = regs;
	return *r;
}

/* 실수 2: 커널 버퍼를 __user 자리에 넘김 (유저 버퍼와 커널 버퍼를 바꿔 씀) */
static __maybe_unused long e5_copy_out(char __user *ubuf, size_t n)
{
	char kbuf[16] = "imu-left";
	return copy_to_user(kbuf, ubuf, min(n, sizeof(kbuf)));
}

/* 실수 3: __le32 와 CPU 정수를 섞음 */
static __maybe_unused u32 e5_payload_len(const struct e5_pkt_hdr *h)
{
	u32 len = h->len;                 /* 변환 없이 대입 */
	if (h->magic == 0xE5E5E5E5)       /* 변환 없이 비교 */
		return len;
	return 0;
}
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("E5: deliberate sparse mistakes");
```

Kbuild: `obj-m := e5_sparse.o e5_sparse_ok.o` (고친 버전을 같이 넣어 비교)

```sh
limactl shell elinux -- bash -lc 'mkdir -p /var/tmp/E5/sp && cp ~/.elinux_scratch/E5/sp/* /var/tmp/E5/sp/ && cd /var/tmp/E5/sp && make -C /var/lab/out/linux-build M=$PWD C=2 modules'
```

```text
make: Entering directory '/var/lab/build/linux'
make[1]: Entering directory '/var/tmp/E5/sp'
  CC [M]  e5_sparse.o
  CHECK   e5_sparse.c
e5_sparse.c:22:29: warning: incorrect type in argument 1 (different address spaces)
e5_sparse.c:22:29:    expected void [noderef] __user *to
e5_sparse.c:22:29:    got char *
e5_sparse.c:22:35: warning: incorrect type in argument 2 (different address spaces)
e5_sparse.c:22:35:    expected void const *from
e5_sparse.c:22:35:    got char [noderef] __user *ubuf
e5_sparse.c:28:20: warning: incorrect type in initializer (different base types)
e5_sparse.c:28:20:    expected unsigned int [usertype] len
e5_sparse.c:28:20:    got restricted __le32 const [usertype] len
e5_sparse.c:29:14: warning: restricted __le32 degrades to integer
e5_sparse.c:15:17: warning: dereference of noderef expression
  CC [M]  e5_sparse_ok.o
  CHECK   e5_sparse_ok.c
  MODPOST Module.symvers
  CC [M]  e5_sparse.mod.o
  CC [M]  .module-common.o
  LD [M]  e5_sparse.ko
  CC [M]  e5_sparse_ok.mod.o
  LD [M]  e5_sparse_ok.ko
make[1]: Leaving directory '/var/tmp/E5/sp'
make: Leaving directory '/var/lab/build/linux'
```

출력에서 볼 것:

- `CC [M] e5_sparse.o` 줄 뒤에 **gcc 경고가 없다.** 세 실수 모두 gcc에겐 정상 코드다. 그리고 `.ko`까지 만들어졌다 — sparse 경고는 빌드를 멈추지 않는다.
- **실수 1**(15행, `return *r;`): "dereference of noderef expression" — `__iomem` 포인터 직접 역참조.
- **실수 2**(22행): `copy_to_user(to, from, n)`의 인자 순서를 뒤집었다. 1번 인자 자리는 `void __user *`인데 `char *`(커널 스택 버퍼)가 왔고, 2번 자리에는 `__user` 포인터가 왔다. 이 버그가 런타임에 가면 커널 스택에 유저가 고른 데이터를 쓰는 **보안 취약점**이다 — 그런데 gcc는 아무 말도 안 한다.
- **실수 3**(28, 29행): `__le32`를 `u32`에 바로 대입("different base types"), `__le32`를 정수 상수와 비교("restricted __le32 degrades to integer"). LE CPU에서는 우연히 맞게 동작하므로 x86/arm64에서 테스트로는 절대 안 잡히고, BE 플랫폼에서야 터진다.
- `[usertype]`은 typedef 이름이 있는 타입이라는 sparse의 표시다.

### 6.3 예제 16 — 고친 버전: sparse 경고 0, QEMU에서 실행

```c
// e5_sparse_ok.c — 같은 코드를 sparse 경고 0 으로 고친 버전 (QEMU 에서 실행)
#include <linux/module.h>
#include <linux/io.h>
#include <linux/uaccess.h>

struct e5_pkt_hdr {
	__le32 magic;
	__le32 len;
};

static u32 e5_read_status(void __iomem *regs)
{
	return readl(regs);                       /* 고침 1: 접근자 사용 */
}

static __maybe_unused long e5_copy_out(char __user *ubuf, size_t n)
{
	char kbuf[16] = "imu-left";
	return copy_to_user(ubuf, kbuf, min(n, sizeof(kbuf)));   /* 고침 2: to, from 순서 */
}

static u32 e5_payload_len(const struct e5_pkt_hdr *h)
{
	if (h->magic != cpu_to_le32(0xE5E5E5E5)) /* 고침 3: 상수를 LE 로 (컴파일 때 계산) */
		return 0;
	return le32_to_cpu(h->len);               /* 고침 3: 값을 CPU 엔디안으로 */
}

static int __init e5_init(void)
{
	static const u8 wire[8] = { 0xe5, 0xe5, 0xe5, 0xe5, 0x40, 0x00, 0x00, 0x00 };
	const struct e5_pkt_hdr *h = (const void *)wire;   /* 패킷 바이트를 헤더로 해석 */
	void __iomem *rtc = ioremap(0x09010000, 0x1000);
	u32 raw;

	if (!rtc)
		return -ENOMEM;
	pr_info("e5_sparse_ok: RTCDR=%u payload_len=%u\n",
		e5_read_status(rtc), e5_payload_len(h));
	raw = (__force u32)h->len;                /* 의도적 재해석은 __force 로 표시 */
	pr_info("e5_sparse_ok: raw bits=%#x\n", raw);
	iounmap(rtc);
	return 0;
}
static void __exit e5_exit(void) { }
module_init(e5_init);
module_exit(e5_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("E5: sparse-clean version");
```

위 §6.2 빌드 출력에서 `CHECK e5_sparse_ok.c` 뒤에 경고가 한 줄도 없다. 이 노트의 모든 커널 모듈에도 `C=2`를 돌렸다.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/E5/mod && make -C /var/lab/out/linux-build M=$PWD C=2 modules 2>&1 | grep -v "^make"'
```

```text
  CHECK   e5_cof.c
  CHECK   e5_list.c
  CHECK   e5_err.c
  CHECK   e5_cleanup.c
  CHECK   e5_regs.c
  CHECK   e5_sparse_ok.c
```

출력에서 볼 것: 이미 빌드된 상태라 `CC` 없이 `CHECK`만 돌았고(`C=2`는 컴파일 여부와 상관없이 검사), 경고 0개다. 예제 13의 실행 출력에서 `e5_sparse_ok: RTCDR=1791664981 payload_len=64`(와이어 바이트 `40 00 00 00`을 LE로 해석한 64)와 `raw bits=0x40`을 이미 봤다.

고친 방법 정리:

1. `*r` → `readl(regs)`. `__iomem`은 접근자로만.
2. `copy_to_user(ubuf, kbuf, n)` — 목적지가 앞. sparse가 순서 실수를 타입으로 잡아 준다.
3. 값은 `le32_to_cpu(h->len)`로 꺼내고, 상수 비교는 **상수 쪽을** `cpu_to_le32(0xE5E5E5E5)`로 바꿨다. 상수 변환은 컴파일 타임에 계산되므로 런타임 비용이 0이다(값 쪽을 변환하면 BE CPU에서 매번 바이트 스왑).
4. 비트 패턴을 의도적으로 재해석할 때(로그 출력용 raw 값 등)는 `(__force u32)h->len`. `__force`는 "검토했다"는 서명이다 — 리뷰어는 `__force`가 보이면 그 줄을 다시 본다.

### 6.4 sparse를 언제 돌리나

- 패치를 메일링 리스트에 보내기 전에 `make C=1` (바뀐 파일만) — 새 sparse 경고는 리뷰에서 지적받는다. 0-day 봇(kernel test robot)이 자동으로 돌려 경고를 메일로 보낸다(E7).
- 기존 코드에도 sparse 경고가 많은 파일이 있다. 내 패치가 **새 경고를 추가하지 않는지**를 본다(`C=2` 출력을 패치 전후로 diff).
- `__bitwise`는 내 드라이버 타입에도 쓸 수 있다: `typedef u32 __bitwise imu_reg_t;` 처럼 정의해 두면 "레지스터 값"과 "그냥 숫자"가 섞이는 것을 sparse가 잡는다. 커널의 `gfp_t`, `blk_opf_t`, `__poll_t`가 이렇게 만들어진 타입이다.

---

## 7. 커널 안에서는 무슨 일이 일어나나

이 노트의 관용구들은 커널 자료구조의 뼈대 그 자체다. 가장 좋은 예가 디바이스 모델이다. `struct device` 하나를 해제하는 경로를 따라가면 `container_of`가 네 번, `kref`가 한 번 나온다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="30" width="330" height="250" rx="6" fill="#888" fill-opacity="0.10" stroke="#888"/>
  <text x="22" y="50" font-size="12">platform_object (pdev + name[])</text>
  <rect x="25" y="62" width="300" height="200" rx="6" fill="#3f9a6b" fill-opacity="0.10" stroke="#3f9a6b"/>
  <text x="37" y="82" font-size="12">struct platform_device pdev</text>
  <rect x="40" y="96" width="270" height="150" rx="6" fill="#e08a3c" fill-opacity="0.10" stroke="#e08a3c"/>
  <text x="52" y="116" font-size="12">struct device dev</text>
  <rect x="55" y="130" width="240" height="100" rx="6" fill="#4a7bd0" fill-opacity="0.10" stroke="#4a7bd0"/>
  <text x="67" y="150" font-size="12">struct kobject kobj</text>
  <rect x="70" y="170" width="210" height="44" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="175" y="197" text-anchor="middle" font-size="13">struct kref kref</text>
  <rect x="370" y="30" width="300" height="30" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="520" y="50" text-anchor="middle" font-size="12">put_device(dev)</text>
  <rect x="370" y="74" width="300" height="30" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="520" y="94" text-anchor="middle" font-size="12">kobject_put(&amp;dev-&gt;kobj)</text>
  <rect x="370" y="118" width="300" height="30" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="520" y="138" text-anchor="middle" font-size="12">kref_put(&amp;kobj-&gt;kref, kobject_release)</text>
  <rect x="370" y="162" width="300" height="30" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="520" y="182" text-anchor="middle" font-size="12">kobject_release(): kref → kobj</text>
  <rect x="370" y="206" width="300" height="30" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="520" y="226" text-anchor="middle" font-size="12">device_release(): kobj → dev</text>
  <rect x="370" y="250" width="300" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="520" y="270" text-anchor="middle" font-size="12">dev-&gt;release(): dev → pa, kfree(pa)</text>
  <line x1="520" y1="60" x2="520" y2="72" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="520" y1="104" x2="520" y2="116" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="520" y1="148" x2="520" y2="160" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="520" y1="192" x2="520" y2="204" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="520" y1="236" x2="520" y2="248" stroke="currentColor" marker-end="url(#ah5)"/>
</svg>
```
그림 7 — 왼쪽: 객체가 마트료시카처럼 내장(embed)된다. 오른쪽: 마지막 `put_device()`에서 시작해 안쪽 `kref`가 0이 되면, 각 층의 release가 `container_of`로 **한 층씩 바깥으로** 올라가며 최종적으로 가장 바깥 객체를 `kfree`한다. "→"는 모두 `container_of`(또는 그것을 감싼 매크로)다.

랩 소스(6.18.55)에서 한 단계씩 확인한 위치:

| 단계 | 파일 | 코드 |
|---|---|---|
| `kobject_put` | `lib/kobject.c` | `kref_put(&kobj->kref, kobject_release);` |
| `kobject_release` | `lib/kobject.c` | `struct kobject *kobj = container_of(kref, struct kobject, kref);` |
| `device_release` | `drivers/base/core.c` | `struct device *dev = kobj_to_dev(kobj);` → `dev->release(dev)` 또는 `dev->type->release(dev)` (없으면 WARN) |
| `kobj_to_dev` | `include/linux/device.h` | `container_of_const(__kobj, struct device, kobj)` |
| `platform_device_release` | `drivers/base/platform.c` | `container_of(dev, struct platform_object, pdev.dev)` → `kfree(pa)` |

`container_of`의 `member`에 `pdev.dev`처럼 **중첩 멤버 경로**를 쓸 수 있다는 점도 보자(`offsetof`가 허용한다). `platform_object`는 `platform_device_alloc()`이 이름 문자열을 같은 할당 안에 붙이기 위해 만든 래퍼(`struct platform_device pdev; char name[];`)다 — 예제 3의 `pdev`를 `kfree`한 것도 결국 이 경로였다.

같은 패턴이 커널 전체에 반복된다:

- **버스·드라이버 코어**: `to_platform_device`, `to_i2c_client`, `to_spi_device`, `to_usb_device`, `to_pci_dev` — 전부 `container_of(dev, ..., dev)`. 버스 콜백은 `struct device *`를 주고 각 버스가 자기 타입으로 되돌린다. F 모듈(드라이버 모델)과 G 모듈(버스 드라이버)에서 매일 쓴다.
- **file_operations**: `open(struct inode *inode, struct file *filp)`에서 `container_of(inode->i_cdev, struct my_dev, cdev)`로 내 디바이스 구조체를 찾아 `filp->private_data`에 저장한다(H 모듈 char 드라이버의 첫 줄). `miscdevice`면 `container_of(filp->private_data, struct my_dev, misc)`.
- **work와 timer**: `INIT_WORK(&priv->work, fn)`의 `fn(struct work_struct *w)`는 `container_of(w, struct my_priv, work)`로, 타이머 콜백은 `timer_container_of(priv, t, timer)`(예전 커널의 `from_timer`가 6.16에서 이 이름으로 바뀌었다 — 6.16 이전 커널용 드라이버는 `#if LINUX_VERSION_CODE < KERNEL_VERSION(6,16,0)`로 `#define timer_container_of from_timer`를 두기도 한다)로 자기 구조체를 찾는다(I 모듈).
- **리스트 사용처**: 드라이버 코어의 `bus->p->klist_devices`, 커널 타이머, 워크큐, 프로세스 목록(`task_struct.tasks`), 블록 I/O 요청 큐 — 전부 intrusive 리스트다. `/proc`의 많은 파일은 그 리스트를 순회해서 만든다.
- **ERR_PTR**: `devm_*_get()` 계열 전부, `clk`/`regulator`/`gpiod`/`phy`/`reset` 프레임워크, `kthread_run`, `debugfs`. `-EPROBE_DEFER`가 ERR_PTR로 전파되는 것이 deferred probe의 근간이다(F 모듈).
- **devm/devres**: `struct device`의 `devres_head` 리스트(이것도 `list_head`)에 해제 함수와 데이터를 쌓아 두고, probe 실패나 unbind 때 `drivers/base/dd.c`의 `device_unbind_cleanup()` → `devres_release_all()`이 역순으로 실행한다.
- **sparse 주석**: `copy_to_user`의 인자는 `void __user *`(E6), `devm_ioremap_resource`의 반환은 `void __iomem *`(G·H 모듈의 MMIO), DMA 디스크립터 필드는 `__le32`/`__le64`(K 모듈).

---

## 8. Embedded 실무에서 다시 보기

### 8.1 센서 키트 IMU 드라이버의 probe — 관용구 총출동

예를 들어 SPI IMU 드라이버라면 `probe()`의 뼈대는 이렇다(형태를 보여 주는 조각이며 실행하지 않았다).

```c
struct imu_priv {
	struct spi_device *spi;
	struct mutex lock;                     /* 레지스터 접근 직렬화 */
	struct list_head fifo_q;               /* 처리 대기 샘플 (intrusive) */
	struct work_struct drain_work;         /* container_of로 priv를 찾는다 */
	u32 odr_hz;
};

static int imu_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct imu_priv *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;                            /* kzalloc 계열: NULL */
	ret = devm_regulator_get_enable(dev, "vdd");
	if (ret)
		return dev_err_probe(dev, ret, "vdd\n");   /* -EPROBE_DEFER면 조용히 */
	ret = devm_mutex_init(dev, &priv->lock);
	if (ret)
		return ret;
	INIT_LIST_HEAD(&priv->fifo_q);                 /* kzalloc 했어도 필수 */
	priv->odr_hz = 1000;
	device_property_read_u32(dev, "odr-hz", &priv->odr_hz);  /* D8: optional */
	priv->odr_hz = clamp(priv->odr_hz, 13U, 833U);
	ret = imu_write(priv, IMU_CTRL1, FIELD_PREP(CTRL1_ODR, odr_to_code(priv->odr_hz)) |
				      CTRL1_BDU | CTRL1_EN);
	if (ret)
		return ret;                                /* 정리 코드 없음 — 전부 devm */
	spi_set_drvdata(spi, priv);
	return 0;
}
```

`goto`가 하나도 없다. 새 드라이버는 devm으로 probe를 "자원을 선언하는 목록"처럼 쓰고, 함수 안에서만 쓰는 임시 버퍼·락은 `__free`/`guard`로 처리한다. 반면 **기존 벤더 BSP 드라이버**(4.x, 5.x 시절 코드)는 대부분 goto 사다리이므로 둘 다 읽을 줄 알아야 한다. 백포트할 때 cleanup.h 코드를 6.5 이전 커널로 가져가면 컴파일이 안 된다는 점도 기억하자.

### 8.2 패킷 헤더와 DMA 디스크립터 — `__le32`로 선언하기

LiDAR UDP 패킷이나 카메라 메타데이터, 또는 SoC DMA 엔진의 디스크립터는 필드 엔디안이 정해져 있다. 구조체를 `__le32`/`__be16`으로 선언하고 `le32_to_cpu`로만 꺼내면 sparse가 실수를 잡는다(예제 15). 레이아웃은 `__packed`와 `static_assert(sizeof(struct desc) == 16);`로 고정한다(§5.2).

### 8.3 레지스터 맵 — 마스크만 정의하고 shift는 잊기

데이터시트의 레지스터 표를 `#define REG_X_FIELD GENMASK(h, l)`로 옮기고 `FIELD_PREP`/`FIELD_GET`만 쓴다. 필드 폭을 넘는 상수는 빌드가 막아 준다(예제 14 CASE 5). regmap을 쓰면 `regmap_update_bits(map, REG, MASK, FIELD_PREP(MASK, v))`가 RMW를 락과 함께 해 준다(G 모듈).

### 8.4 oops에서 `dead…`와 `fffffffffffff…`를 알아보기

현장에서 받은 oops 로그의 fault 주소가

- `dead000000000100`/`…122` 근처 → **list_del 후 사용**(예제 6).
- `fffffffffffffffe`, `ffffffffffffffea`, `fffffffffffffdfb` 근처 → **ERR_PTR을 IS_ERR 검사 없이 역참조**. 주소에서 errno를 바로 읽을 수 있다(`…fdfb` + 멤버 오프셋이면 -517, 아직 준비 안 된 자원을 그냥 썼다).
- `0000000000000010` 같은 작은 값 → NULL 포인터의 멤버 접근(오프셋 0x10의 필드).
- `6b6b6b6b6b6b6b6b` → SLUB 디버그(`slub_debug=P`)가 해제된 객체를 채우는 poison `POISON_FREE`(0x6b) — kfree 후 사용.

이 네 가지 패턴을 아는 것만으로 현장 크래시의 절반은 첫 화면에서 분류된다.

---

## 9. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `list_for_each_entry` 안에서 `list_del`/`kfree` | oops, fault 주소 `dead0000000000xx` 또는 간헐적 크래시 | 증가식이 지운 노드의 `next`를 읽음 | `list_for_each_entry_safe` |
| `kzalloc`한 구조체의 `list_head`를 초기화 안 함 | 첫 `list_add`나 순회에서 NULL 역참조 | `{NULL, NULL}`은 빈 리스트가 아님 | `INIT_LIST_HEAD` |
| ERR_PTR 반환 API를 `if (!p)`로 검사 | `ffffffffffffffxx` 근처 주소에서 oops | NULL이 아니라 에러 포인터 | API 문서의 `Return:` 확인, `IS_ERR` |
| NULL 반환 API를 `IS_ERR`로 검사 | NULL 역참조 oops | `kmalloc`, `devm_ioremap`은 NULL | `if (!p)` |
| `container_of`에 잘못된 member (같은 타입 다른 멤버) | 조용한 메모리 손상 | 타입 검사를 통과 | 멤버 이름 확인, 리스트마다 다른 이름 |
| goto 레이블 순서가 획득 순서와 어긋남 | 에러 경로에서 이중 해제 또는 누수 (정상 경로 테스트로는 안 보임) | 사다리를 손으로 관리 | 실패 주입 테스트(예제 11의 `fail_at`), devm 전환 |
| devm 자원을 수동으로도 해제 | 이중 해제 WARN/oops (rmmod 시) | devres가 한 번 더 해제 | devm 자원은 손대지 않음 |
| `__iomem` 포인터 직접 역참조 | x86에선 동작, 다른 아키텍처에서 이상 동작·순서 문제 | 배리어·엔디안·접근 폭 보장 없음 | `readl`/`writel`, sparse로 검사 |
| `__le32` 필드를 변환 없이 사용 | LE 보드에선 정상, BE 보드에서 값이 바이트 스왑됨 | 엔디안 무시 | `le32_to_cpu`, sparse |
| `min(u32, int)` 류 비교 | 6.x에선 빌드 에러, 옛 매크로면 `-1`이 최댓값이 됨 | 부호 섞인 비교 | `min_t(type, ...)`, 타입 정리 |

---

## 10. 면접에서 이렇게 말한다

**Q.** What does container_of do, and how is it implemented?

**A.** 구조체 안에 내장된 멤버의 포인터를 받아서 그 멤버를 품은 바깥 구조체의 포인터를 돌려준다. 구현은 멤버 주소에서 `offsetof(type, member)`를 빼는 것 — 오프셋이 컴파일 타임 상수라서 런타임엔 빼기 한 번(arm64에서 `sub` 한 줄)이다. 커널판은 statement expression과 `static_assert(__same_type(*ptr, ((type *)0)->member))`로 포인터 타입이 멤버 타입과 같은지 컴파일 때 검사한다. 새 코드에서는 const를 보존하는 `container_of_const`를 권장한다.

> container_of takes a pointer to a member embedded in a struct and returns a pointer to the enclosing struct. It subtracts offsetof(type, member) from the member's address — the offset is a compile-time constant, so at runtime it's a single subtract. The kernel version wraps it in a statement expression with a static_assert that the pointer type matches the member type, so passing the wrong pointer is a build error. For new code there's container_of_const, which preserves const.

**꼬리 질문**: "왜 역포인터(back-pointer)를 두지 않나?" → 객체마다 8바이트를 아끼고, 포인터 초기화를 잊는 버그가 없고, 하나의 객체가 여러 부품(device, kref, list_head, work)을 내장해도 각각에서 돌아올 수 있다.

**Q.** Why does the kernel use intrusive linked lists instead of a generic list that stores pointers?

**A.** 링크가 객체 안에 있으니 리스트에 넣을 때 할당이 없어서 `list_add`가 실패할 수 없다 — spinlock을 잡거나 인터럽트 컨텍스트에서도 쓸 수 있다. `list_head`를 여러 개 두면 한 객체가 여러 리스트에 동시에 있을 수 있고, 객체 포인터만 있으면 O(1)로 뺄 수 있다. 리스트 코드는 `list_head`만 알아서 타입마다 다시 쓸 필요가 없고, `container_of`로 바깥 객체로 돌아온다. 캐시에도 대체로 유리한데, 측정해 보면 무작위 순서 순회에선 차이가 작다.

> Because the links live inside the object, adding to a list never allocates, so list_add can't fail — that matters under a spinlock or in interrupt context. An object can sit on several lists by embedding several list_heads, and removal is O(1) given the object. The list code only knows about list_head and is reused for every type; container_of gets you back to the object. It's usually a bit more cache-friendly too, although for random-order traversal the difference is small.

**Q.** How do you delete entries while iterating a kernel list?

**A.** `list_for_each_entry_safe(pos, n, head, member)`를 쓴다. 다음 원소를 `n`에 미리 저장해 두므로 몸통에서 `pos`를 `list_del` + `kfree`해도 된다. 그냥 `list_for_each_entry`로 지우면 증가식이 지운 노드의 `next`를 읽는데, `list_del`이 거기에 `LIST_POISON1`(`0xdead000000000100`)을 써 놓았기 때문에 그 근처 주소에서 oops가 난다. `_safe`는 내가 지우는 것만 안전하고, 동시 수정은 락이나 RCU로 막아야 한다.

> I use list_for_each_entry_safe, which caches the next entry in a second cursor, so I can list_del and kfree the current one. With the plain iterator, the increment step reads the deleted node's next pointer, which list_del has poisoned to 0xdead000000000100, so you oops at an address near that. The _safe variant only protects against my own deletion — concurrent modification still needs a lock or RCU.

**Q.** How does ERR_PTR work?

**A.** 커널 주소 공간의 맨 위 4095바이트에는 유효한 객체가 없다는 점을 이용한다. `ERR_PTR(-ENOENT)`은 -2를 그대로 포인터로 캐스트한 것(`0xfffffffffffffffe`)이고, `IS_ERR`는 `(unsigned long)p >= (unsigned long)-4095` 비교 한 번, `PTR_ERR`는 다시 `long`으로 캐스트한다. 이걸로 한 반환값에 "객체 또는 실패 이유"를 담는다 — 특히 `-EPROBE_DEFER`를 구분해야 하는 probe 경로에서 중요하다. NULL은 `IS_ERR`가 잡지 않으니 API마다 NULL을 주는지 ERR_PTR을 주는지 확인해야 한다.

> The top 4095 bytes of the address space never hold a valid object, so a negative errno cast to a pointer — ERR_PTR(-ENOENT) is 0xfffffffffffffffe — can't be confused with a real pointer. IS_ERR is a single unsigned compare against -MAX_ERRNO, and PTR_ERR casts it back to long. That lets one return value carry either the object or the reason it failed, which matters for things like -EPROBE_DEFER. IS_ERR doesn't catch NULL, so you have to know which convention each API uses.

**꼬리 질문**: "로그에 에러 이름을 찍으려면?" → `%pe` 포맷에 에러 포인터(정수면 `ERR_PTR(ret)`)를 넘긴다.

**Q.** How do you handle errors and cleanup in a probe function?

**A.** 전통적으로는 goto 사다리 — 자원을 잡을 때마다 실패하면 "직전까지 잡은 것을 되돌리는 레이블"로 점프하고, 레이블은 획득의 역순으로 아래로 떨어지며 해제한다. 지금은 가능한 한 `devm_*`를 써서 드라이버 코어가 probe 실패나 unbind 때 역순으로 해제하게 하고, devm 버전이 없는 건 `devm_add_action_or_reset`으로 등록한다. 함수 안에서만 쓰는 버퍼와 락은 6.5부터 있는 `__free(kfree)`, `guard(mutex)` 같은 scope 기반 정리를 쓴다. 한 함수에서 goto와 cleanup 헬퍼를 섞지 않는다. devm 해제는 remove 이후에 일어나므로 IRQ 같은 자원의 순서에 주의한다.

> Classically with a goto ladder: each acquisition jumps on failure to a label that undoes everything acquired before it, with labels in reverse order falling through. In new code I use devm_ helpers so the driver core releases resources in reverse order on probe failure or unbind, and devm_add_action_or_reset for anything without a devm variant. For function-local buffers and locks I use the scope-based helpers from cleanup.h — __free(kfree), guard(mutex) — available since 6.5. I don't mix goto with cleanup helpers in one function, and I keep in mind that devm teardown runs after remove, which matters for IRQs.

**Q.** What are `__iomem` and `__user`, and what checks them?

**A.** sparse용 주소 공간 주석이다. gcc에게는 빈 매크로라 아무 효과가 없고, `make C=1`/`C=2`로 sparse를 돌리면 다른 주소 공간 포인터를 섞거나 직접 역참조할 때 경고한다. `__iomem`은 ioremap한 장치 레지스터 — 반드시 `readl`/`writel`로 접근한다(배리어, 접근 폭, 엔디안). `__user`는 유저 공간 포인터 — `copy_from_user`/`get_user`로만 접근한다(검증, fault 처리). `__bitwise`는 `__le32` 같은 타입을 일반 정수와 섞지 못하게 하고, 의도적인 변환은 `__force`로 표시한다.

> They're sparse annotations — address-space tags. To gcc they expand to nothing; running sparse via make C=1 or C=2 warns when pointers from different address spaces are mixed or dereferenced directly. `__iomem` marks ioremapped registers, which must be accessed with readl/writel so you get the right access width, barriers and endianness. `__user` marks userspace pointers, which go through copy_from_user or get_user. `__bitwise` makes types like `__le32` incompatible with plain integers, and `__force` marks an intentional cast.

**꼬리 질문**: "sparse가 실제로 잡은 버그 예?" → `copy_to_user`의 인자 순서를 바꾸면 gcc는 조용하지만 sparse는 "different address spaces"로 잡는다 — 그대로 두면 유저가 커널 스택을 덮는 취약점.

**Q.** How do you define and update a register field in kernel code?

**A.** 데이터시트의 필드를 `GENMASK(h, l)`(단일 비트는 `BIT(n)`)로 마스크만 정의하고, 쓰기는 `FIELD_PREP(mask, val)`, 읽기는 `FIELD_GET(mask, reg)`. shift는 마스크에서 계산되고, 상수 값이 필드보다 크면 빌드 에러다. RMW는 `reg &= ~mask; reg |= FIELD_PREP(mask, v);` — regmap이면 `regmap_update_bits`가 락과 함께 해 준다. 레지스터 접근은 `readl`/`writel`로.

> I define each field as a mask with GENMASK(high, low) or BIT(n), then use FIELD_PREP to place a value and FIELD_GET to extract it. The shift is derived from the mask, and a constant that doesn't fit the field fails the build. A read-modify-write is clear the mask, OR in FIELD_PREP — or regmap_update_bits, which also takes the lock. The access itself goes through readl and writel.

**Q.** Why does the kernel's min() refuse to compare an unsigned int with an int?

**A.** C의 일반 산술 변환 때문에 `-1`이 unsigned로 바뀌어 최댓값이 되므로, `min(10u, -1)`이 10이 되는 버그가 난다. 커널 `min()`은 각 인자를 한 번만 평가하고(부작용 안전) 부호가 호환되지 않으면 컴파일 에러를 낸다. 의도한 비교 타입이 있으면 `min_t(type, a, b)`로 명시한다. 컴파일 타임에 음수가 아님이 확실한 signed 상수는 허용된다.

> Because of the usual arithmetic conversions, -1 becomes UINT_MAX when compared with an unsigned value, so a naive MIN would return the wrong one. The kernel's min evaluates each argument once and fails the build when the signedness is incompatible; if I really want a particular comparison type I use min_t and name it. Signed values that are provably non-negative at compile time are allowed.

### 10.1 화이트보드 드릴 3개

면접관이 "펜 들고 써 보세요"라고 할 때를 대비해, 손으로 5~10분 안에 쓸 수 있는 분량으로 연습한다. 모범 답안은 이 노트의 예제에서 실제로 컴파일·실행한 코드의 핵심만 뽑은 것이다.

**드릴 1 — container_of를 구현하고, 콜백에서 써라.**
"`struct sensor { int id; struct device dev; };`가 있다. `void on_event(struct device *d)` 콜백 안에서 `id`를 출력하라. 라이브러리 없이 `container_of`부터."

모범 답안:

```c
#include <stddef.h>   /* offsetof */

#define container_of(ptr, type, member) \
	((type *)((char *)(ptr) - offsetof(type, member)))

void on_event(struct device *d)
{
	struct sensor *s = container_of(d, struct sensor, dev);
	printf("id=%d\n", s->id);
}
```

말로 덧붙일 것: (1) `char *`로 바꾸는 이유 — 바이트 단위 산술. (2) 오프셋은 컴파일 타임 상수라 런타임 비용은 빼기 한 번. (3) 커널판은 `({ ... })` 안에 `static_assert(__same_type(*(ptr), ((type *)0)->member))`를 넣어 타입을 검사하고 `ptr`을 한 번만 평가한다. (4) `dev`가 포인터 멤버라면 쓸 수 없다 — 내장돼 있어야 한다. 시간이 남으면 예제 1처럼 타입 검사 버전을 쓴다.

**드릴 2 — intrusive 이중 연결 리스트: add_tail, del, 그리고 조건부 삭제.**
"센서 샘플 구조체에 `list_head`를 넣고, 큐에 넣는 함수와 `accel < 0`인 샘플을 모두 지우는 함수를 써라."

모범 답안(예제 4의 핵심):

```c
struct list_head { struct list_head *next, *prev; };
#define LIST_HEAD_INIT(n) { &(n), &(n) }

static void __list_add(struct list_head *new, struct list_head *prev,
		       struct list_head *next)
{
	next->prev = new;
	new->next = next;
	new->prev = prev;
	prev->next = new;
}
static void list_add_tail(struct list_head *new, struct list_head *head)
{
	__list_add(new, head->prev, head);
}
static void list_del(struct list_head *e)
{
	e->next->prev = e->prev;
	e->prev->next = e->next;
	e->next = e->prev = NULL;      /* 커널은 poison 값 */
}

struct sample { int seq, accel; struct list_head node; };

void drop_negative(struct list_head *q)
{
	struct list_head *p, *n;
	for (p = q->next, n = p->next; p != q; p = n, n = p->next) {
		struct sample *s = container_of(p, struct sample, node);
		if (s->accel < 0) {
			list_del(&s->node);
			free(s);
		}
	}
}
```

말로 덧붙일 것: (1) 원형 + 머리 노드(sentinel) 덕에 빈 리스트·첫 원소·끝 원소에 특수 경우가 없다. (2) 삭제 루프에서 `n`을 **먼저** 저장하는 것이 `list_for_each_entry_safe`의 본질 — 저장 안 하면 해제된 노드의 `next`를 읽는다(예제 4의 gdb, 예제 6의 oops). (3) 커널에선 리스트 조작을 락으로 보호한다.

꼬리 질문: "단일 연결 리스트에서 값이 x인 노드를 모두 지워라(머리 특수 처리 없이)." → 포인터의 포인터.

```c
struct node { int val; struct node *next; };

void remove_all(struct node **head, int x)
{
	struct node **pp = head;          /* "현재 노드를 가리키는 링크"의 주소 */
	while (*pp) {
		struct node *cur = *pp;
		if (cur->val == x) {
			*pp = cur->next;          /* 링크를 건너뛰게 고침 */
			free(cur);
		} else {
			pp = &cur->next;
		}
	}
}
```

이게 `hlist_node.pprev`의 아이디어다(§2.6).

**드릴 3 — 자원 4개를 잡는 probe를 에러 처리까지 써라. 그다음 devm으로 다시 써라.**
"클럭 enable, 레귤레이터 enable, 버퍼 kmalloc, IRQ 요청. 어느 단계에서 실패해도 누수가 없어야 한다. 버퍼 조회 함수 `get_buf()`는 실패 시 ERR_PTR을 돌려준다."

모범 답안(goto 버전):

```c
static int imu_probe(struct platform_device *pdev)
{
	struct imu *imu = platform_get_drvdata(pdev);
	int ret;

	ret = clk_prepare_enable(imu->clk);
	if (ret)
		return ret;
	ret = regulator_enable(imu->vdd);
	if (ret)
		goto err_clk;
	imu->buf = get_buf(imu);
	if (IS_ERR(imu->buf)) {
		ret = PTR_ERR(imu->buf);
		goto err_reg;
	}
	ret = request_irq(imu->irq, imu_isr, 0, "imu", imu);
	if (ret)
		goto err_buf;
	return 0;

err_buf:
	put_buf(imu->buf);
err_reg:
	regulator_disable(imu->vdd);
err_clk:
	clk_disable_unprepare(imu->clk);
	return ret;
}
```

devm 버전으로 다시 쓰면:

```c
static int imu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct imu *imu = platform_get_drvdata(pdev);
	int ret;

	imu->clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(imu->clk))
		return dev_err_probe(dev, PTR_ERR(imu->clk), "clk\n");
	ret = devm_regulator_get_enable(dev, "vdd");
	if (ret)
		return dev_err_probe(dev, ret, "vdd\n");
	imu->buf = get_buf(imu);
	if (IS_ERR(imu->buf))
		return PTR_ERR(imu->buf);
	ret = devm_add_action_or_reset(dev, put_buf_action, imu->buf);
	if (ret)
		return ret;
	return devm_request_irq(dev, imu->irq, imu_isr, 0, "imu", imu);
}
```

(드릴 답안은 손으로 쓰는 형태를 보여 주는 조각이라 단독 빌드하지 않았다. 같은 구조가 예제 10, 11에서 실행됐다.)

말로 덧붙일 것: (1) 레이블은 "무엇을 되돌리는지"로 이름 짓고 획득 역순으로 놓는다. (2) `request_irq`는 마지막 — IRQ가 들어오는 순간 핸들러가 다른 자원을 쓰므로 모든 준비가 끝난 뒤에 켠다. (3) devm 버전의 IRQ는 remove 이후에 해제되므로, remove에서 하드웨어를 먼저 끄면 그 사이 IRQ가 올 수 있다 — 필요하면 remove 처음에 `disable_irq`. (4) `dev_err_probe`는 `-EPROBE_DEFER`면 로그를 남기지 않는다. (5) 테스트는 예제 11처럼 단계별 실패 주입으로.

---

## 11. 직접 해보기

1. (개념) `struct a { char c; struct list_head l; int x; };`에서 `offsetof(struct a, l)`과 `offsetof(struct a, x)`는 arm64에서 각각 얼마인가? `container_of(&obj.l, struct a, l)`은 어떤 명령 하나로 컴파일되는가?
   정답/힌트: 8과 24(`list_head`가 8바이트 정렬). `sub x0, x0, #0x8`.

2. (코드) 예제 4의 `ilist.c`에 `list_move_tail(entry, head)`와 `list_splice_tail_init(list, head)`를 추가하고, "음수 샘플을 지우는 대신 `bad` 리스트로 옮긴 뒤 마지막에 한 번에 해제"하도록 바꿔라.
   정답/힌트: `list_move_tail = list_del + list_add_tail`. 이동도 순회 중이면 `_safe`가 필요하다.

3. (관찰) 예제 6을 `list_del` 대신 `list_del_init`으로 바꾸면 oops가 나는가? 무엇이 출력되는가? 이유를 `list_del_init`의 정의로 설명하라.
   정답/힌트: 지운 노드가 자기 자신을 가리키게 되므로 증가식이 `pos`를 다시 `pos`로 만든다 — 같은 원소를 무한히 도는 **hang**(soft lockup)이 된다. poison이 "크래시로 빨리 드러내기"를 위한 선택임을 확인하라.

4. (코드) 예제 9의 `e5_get_cal()`을 쓰는 쪽에서 `if (!c)`로 검사하도록 일부러 틀리고 `id=-1`일 때 `c->gain`을 읽어 보라. oops의 fault 주소는 얼마일 것인가? 예측 후 QEMU에서 확인하라.
   정답/힌트: `ERR_PTR(-EINVAL)` = `0xffffffffffffffea`에 `gain`의 오프셋 0을 더한 주소. 주소의 하위 바이트에서 errno(-22)를 읽어 낼 수 있다.

5. (코드) 예제 11에 세 번째 devm 액션 C를 `e5_parse()` 뒤에 추가하고 `fail_at=0`으로 넣었다 뺐을 때 출력 순서를 예측하라. 이어서 `e5_probe()`를 devm 없이 goto 사다리로 다시 쓰고 `fail_at` 1~3에서 출력이 같은지 비교하라.
   정답/힌트: remove → C → B → A. goto 버전은 remove에서 A, B, C를 직접 역순으로 해제해야 하므로 코드가 두 곳(에러 경로, remove)에 생긴다 — devm의 가치가 이것.

6. (관찰) 예제 15의 `e5_sparse.c`에 `void __user *p = (void __force __user *)kbuf;` 같은 줄을 넣으면 sparse 경고가 사라지는가? 그게 왜 위험한지 설명하라. 그리고 `make C=1`과 `C=2`를 두 번씩 연속 실행해 차이를 보라.
   정답/힌트: `__force`가 검사를 끈다 — 경고만 사라지고 버그는 그대로. `C=1`은 두 번째 실행에서 재컴파일이 없으니 `CHECK`도 안 돈다.

7. (설계) 센서 키트의 GNSS UART 패킷 헤더(sync `0xB562`(BE), class, id, length(LE16))를 C 구조체로 정의하라. 필드 타입에 `__be16`/`__le16`을 쓰고 `static_assert`로 크기를 고정하고, 길이를 읽는 함수를 sparse 경고 없이 작성하라.
   정답/힌트: `struct ubx_hdr { __be16 sync; u8 cls; u8 id; __le16 len; } __packed; static_assert(sizeof(struct ubx_hdr) == 6);`, `if (h->sync != cpu_to_be16(0xB562)) return -EINVAL; return le16_to_cpu(h->len);`.

---

## 12. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| `container_of` | 멤버 → 바깥 구조체 | 멤버 주소 − `offsetof(type, member)` + 타입 검사 |
| `container_of_const` | const 보존판 | `_Generic`으로 입력이 const면 const 포인터 반환. 새 코드 권장 |
| embedded (내장) | 포인터가 아니라 값으로 포함 | `struct device dev;` — container_of의 전제 |
| intrusive list | 침투형 리스트 | 링크(`list_head`)가 데이터 객체 안에 있는 리스트 |
| `list_head` | 이중 원형 리스트 링크 | `next`, `prev` 두 포인터(16바이트) |
| sentinel (머리 노드) | 데이터 없는 리스트 머리 | 빈 리스트 = 머리가 자기 자신을 가리킴. 특수 경우 제거 |
| `hlist` | 해시 버킷용 리스트 | 머리 8바이트(`first`), 노드는 `next`와 `pprev` |
| `LIST_POISON1/2` | list_del 후 기록 값 | arm64에서 `0xdead000000000100`/`…122` |
| `kref` | 내장 참조 카운트 | `refcount_t` 하나. 0이 되면 release 콜백 |
| `ERR_PTR` / `IS_ERR` / `PTR_ERR` | 에러 포인터 인코딩 | errno를 주소 공간 최상위 4095바이트에 실어 반환 |
| `MAX_ERRNO` | 4095 | 에러 포인터로 쓸 수 있는 errno의 최댓값 |
| `%pe` | printk 포맷 | 에러 포인터를 `-ENOENT` 같은 이름으로 출력 |
| goto 사다리 | 중앙 집중 에러 정리 | 실패 시 레이블로 점프, 레이블은 획득 역순 |
| devm / devres | 디바이스 관리 자원 | `struct device`에 해제 함수를 쌓아 unbind·probe 실패 시 역순 실행 |
| `__free()` / `guard()` | scope 기반 정리 (cleanup.h) | GCC `cleanup` 속성. 스코프를 나갈 때 자동 해제·unlock |
| `no_free_ptr` / `return_ptr` | 소유권 이전 | `__free` 변수의 자동 해제를 취소하고 값을 넘김 |
| `GENMASK` / `BIT` | 비트 마스크 | `GENMASK(h, l)`은 비트 h..l, `BIT(n)`은 `1UL << n` |
| `FIELD_PREP` / `FIELD_GET` | 필드 넣기/꺼내기 | 마스크에서 shift를 계산, 범위 컴파일 검사 |
| `BUILD_BUG_ON` | 컴파일 타임 assert | 조건이 참이면 빌드 실패 |
| `__le32` / `__be32` | 엔디안 지정 정수 | `__bitwise` 타입. `le32_to_cpu` 등으로만 변환 |
| `likely` / `unlikely` | 분기 힌트 | `__builtin_expect`. 코드 배치 최적화 |
| `READ_ONCE` / `WRITE_ONCE` | 한 번의 쪼개지지 않는 접근 | 컴파일러 최적화 방지. 배리어 아님 |
| sparse | 커널 정적 검사기 | `make C=1/C=2`. 주소 공간·bitwise·락 주석 검사 |
| `__iomem` / `__user` | 주소 공간 주석 | MMIO 포인터 / 유저 공간 포인터. 직접 역참조 금지 |
| `__force` | 검사 해제 캐스트 | "이 변환은 의도했다"는 표시 |
| `__bitwise` | 제한된 정수 타입 | 다른 정수와 섞으면 sparse 경고 |

---

## 13. 요약 & 체크리스트

커널 C 관용구는 새 문법이 아니라 Don이 펌웨어에서 손으로 하던 일을 표준화한 것이다. `container_of`는 "멤버 주소 − 상수 오프셋"으로 내장된 부품에서 바깥 객체를 찾고, 커널판은 타입까지 검사한다. 디바이스 모델·kref·work·리스트가 모두 이 위에 서 있다. `list_head`는 링크를 객체 안에 넣어 할당 없는(실패 없는) 삽입, 다중 소속, O(1) 삭제를 얻고, 순회 중 삭제는 반드시 `_safe`로 한다 — 틀리면 `dead…` 주소 oops가 난다. `ERR_PTR`은 주소 공간 꼭대기 4095바이트에 errno를 실어 "객체 또는 이유"를 하나의 포인터로 돌려주며, API마다 NULL/ERR_PTR 관례가 다르다는 것이 가장 큰 함정이다. 에러 정리는 "획득의 역순"이라는 한 원칙을 goto 사다리(사람), devm(드라이버 코어), cleanup.h(컴파일러)가 각각 기억한다. `GENMASK`/`FIELD_PREP`/엔디안 헬퍼는 레지스터·패킷 필드를 안전하게 다루고 상당수 실수를 빌드 단계에서 막는다. gcc가 못 보는 주소 공간·엔디안 실수는 `__iomem`/`__user`/`__bitwise` 주석과 `make C=2`(sparse)가 잡는다.

- [ ] `container_of`를 `offsetof`에서 유도해 화이트보드에 쓰고, 커널판의 타입 검사와 `container_of_const`를 설명할 수 있다
- [ ] `to_platform_device`, release 콜백, `kref` release에서 `container_of`가 어떻게 쓰이는지 말할 수 있다
- [ ] `list_head`로 큐를 만들고 `list_add`/`list_add_tail`/`list_del`/`list_for_each_entry(_safe)`를 직접 구현할 수 있다
- [ ] 순회 중 삭제 버그의 oops 주소(`dead0000000000fc`)를 오프셋 계산으로 해석할 수 있다
- [ ] intrusive 리스트의 장점(할당 없음, 다중 소속, O(1) 삭제)과 hlist의 `pprev`를 설명할 수 있다
- [ ] `ERR_PTR(-ENOENT)`의 실제 값과 `IS_ERR`의 비교식을 말하고, API별 NULL/ERR_PTR 관례를 구분할 수 있다
- [ ] goto 사다리를 레이블 이름·순서 규칙에 맞게 쓰고, 같은 함수를 devm으로 다시 쓸 수 있다
- [ ] `__free(kfree)`, `guard(mutex)`, `no_free_ptr`/`return_ptr`의 동작과 "goto와 섞지 않기" 규칙을 설명할 수 있다
- [ ] `GENMASK`/`FIELD_PREP`/`FIELD_GET`으로 레지스터 필드 RMW 코드를 쓸 수 있다
- [ ] `cpu_to_le32`/`le32_to_cpu`와 `__le32` 타입을 써서 엔디안 안전한 구조체를 정의할 수 있다
- [ ] `min()`의 signedness 검사, `ARRAY_SIZE`의 배열 검사, `BUILD_BUG_ON`이 무엇을 막는지 말할 수 있다
- [ ] `make C=2`로 sparse를 돌리고 "different address spaces", "dereference of noderef", "restricted __le32" 경고를 고칠 수 있다

---

## 참고 자료

- docs.kernel.org — [Linux kernel coding style](https://docs.kernel.org/process/coding-style.html) (7장 "Centralized exiting of functions")
- docs.kernel.org — [Scope-based Cleanup Helpers](https://docs.kernel.org/core-api/cleanup.html) (`__free`, `guard`, `DEFINE_FREE`)
- docs.kernel.org — [Devres - Managed Device Resource](https://docs.kernel.org/driver-api/driver-model/devres.html)
- docs.kernel.org — [Sparse](https://docs.kernel.org/dev-tools/sparse.html) (`__bitwise`, `__force`, `make C=1/C=2`)
- docs.kernel.org — [Adding reference counters (krefs) to kernel objects](https://docs.kernel.org/core-api/kref.html)
- docs.kernel.org — [Linked Lists in Linux](https://docs.kernel.org/core-api/list.html)
- docs.kernel.org — [How to get printk format specifiers right](https://docs.kernel.org/core-api/printk-formats.html) (`%pe`, `%px`, `%*ph`)
- 랩 소스 (Linux 6.18.55, VM의 `/var/lab/src/linux-6.18.55`): `include/linux/container_of.h`, `include/linux/list.h`, `include/linux/types.h`(`list_head`, `hlist_*`), `include/linux/poison.h`, `include/linux/hashtable.h`, `include/linux/kref.h`, `include/linux/err.h`, `include/linux/cleanup.h`, `include/linux/slab.h`(`DEFINE_FREE(kfree, ...)`), `include/linux/mutex.h`(`DEFINE_GUARD(mutex, ...)`), `include/linux/bitfield.h`, `include/linux/bits.h`, `include/linux/minmax.h`, `include/linux/array_size.h`, `include/linux/build_bug.h`, `include/linux/compiler_types.h`(sparse 주석), `include/uapi/linux/types.h`(`__le32`), `lib/kobject.c`(`kobject_release`), `drivers/base/core.c`(`device_release`), `drivers/base/platform.c`(`platform_device_release`)
- elixir.bootlin.com — 같은 파일을 버전별로 볼 때: [include/linux/list.h](https://elixir.bootlin.com/linux/v6.18/source/include/linux/list.h), [include/linux/cleanup.h](https://elixir.bootlin.com/linux/v6.18/source/include/linux/cleanup.h)
- Robert Love, "Linux Kernel Development" 3판 6장 (Kernel Data Structures — 링크드 리스트와 `container_of`)
- Bootlin, "Linux kernel and driver development" 교육 슬라이드 — 커널 코딩 관용구, devm, 리스트 ([bootlin.com/training/kernel](https://bootlin.com/training/kernel/))
