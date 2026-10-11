# E4. 커널 프로그래밍 제약 — 커널 코드가 앱 코드와 다른 점

> **이 노트를 다 읽으면**: libc·FPU·큰 스택·sleep·유저 포인터 같은 "앱에서는 당연한 것"이 커널에서 왜 금지되는지 실제 컴파일 에러·Oops 로그로 보여 줄 수 있다 · 커널 스택 16 KiB와 VMAP_STACK guard page, atomic context와 GFP 플래그를 설명할 수 있다 · Oops 로그를 위에서부터 읽고 `faddr2line`으로 소스 줄을 찾을 수 있다 · `checkpatch.pl`·`sparse`·`-Wframe-larger-than`·slab redzone 같은 도구로 이 제약 위반을 미리 잡을 수 있다
> **왜 필요한가**: "What's different about writing kernel code vs userspace code?"는 드라이버 면접의 단골 첫 질문이고, 실무 첫 버그의 대부분(스핀락 안의 `msleep`, 스택 위 큰 버퍼, 유저 포인터 직접 접근)이 이 목록에서 나온다
> **Don 기준 난이도**: libc 없음·FPU 제한·작은 스택·ISR 안에서 블로킹 금지는 bare-metal 펌웨어에서 이미 몸에 익었다 / 새로 배울 부분은 "같은 주소 공간을 수천 개 모듈과 나눠 쓴다", "선점·SMP가 기본", "유저 공간이라는 신뢰할 수 없는 이웃", 그리고 이걸 잡아 주는 커널의 디버그 장치들
> **선행 노트**: B1(커널 빌드 플래그), E1(커널 아키텍처), E3(커널 모듈) — E5(커널 C 관용구)·E6(유저↔커널 경계)·F2/F4(동기화)·F9(메모리 할당)·M3(Oops 분석)으로 이어진다

---

## 0. 큰 그림 — 이게 왜 필요한가

Don이 SSD 펌웨어를 쓸 때의 규칙을 떠올려 보자. `printf`는 UART 링버퍼에 넣는 자체 함수였고, `malloc`은 없거나 부팅 때 한 번만 쓰는 풀 할당자였고, 스택은 링커 스크립트에 `__stack_size = 0x1000`처럼 박혀 있었고, ISR 안에서는 절대 기다리지 않았다. 여기까지는 Linux 커널도 똑같다. **커널 코드는 펌웨어처럼 "맨바닥" 위에서 돈다.** 밑에서 받쳐 주는 libc도, 잘못된 접근을 잡아서 프로세스만 죽여 주는 운영체제도 없다. 커널이 바로 그 운영체제다.

다른 점은 **이웃이 많다**는 것이다. 펌웨어는 한 팀이 쓴 코드 수만 줄이 한 칩을 독점한다. 커널 모듈은 수천 명이 쓴 수백만 줄과 **같은 주소 공간, 같은 특권 레벨(EL1)** 에서 돈다. 내 드라이버가 8바이트를 넘쳐 쓰면 옆 객체(네트워크 스택의 버퍼일 수도, 파일 시스템의 inode일 수도)가 조용히 망가진다. 게다가 CPU가 여러 개고(SMP), 언제든 선점(preemption)되고, 인터럽트가 끼어들고, 위에서는 **믿을 수 없는 유저 프로세스**가 포인터를 넘긴다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="115" y="22" text-anchor="middle" font-size="13" font-weight="bold">Bare-metal 펌웨어</text>
  <text x="340" y="22" text-anchor="middle" font-size="13" font-weight="bold">Linux 유저 앱</text>
  <text x="565" y="22" text-anchor="middle" font-size="13" font-weight="bold">Linux 커널 코드 (드라이버)</text>
  <rect x="15" y="36" width="200" height="34" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="115" y="58" text-anchor="middle" font-size="12">내 코드 (main + ISR)</text>
  <rect x="15" y="84" width="200" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="115" y="106" text-anchor="middle" font-size="12">벤더 HAL · newlib 일부</text>
  <rect x="15" y="132" width="200" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="115" y="154" text-anchor="middle" font-size="12">MPU (선택 사항)</text>
  <rect x="15" y="180" width="200" height="34" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="115" y="202" text-anchor="middle" font-size="12">하드웨어</text>
  <line x1="115" y1="70" x2="115" y2="82" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="115" y1="118" x2="115" y2="130" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="115" y1="166" x2="115" y2="178" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="240" y="36" width="200" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="58" text-anchor="middle" font-size="12">내 코드 (프로세스)</text>
  <rect x="240" y="84" width="200" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="106" text-anchor="middle" font-size="12">glibc · libm · libpthread</text>
  <rect x="240" y="132" width="200" height="34" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="340" y="154" text-anchor="middle" font-size="12">syscall 경계 · MMU 격리</text>
  <rect x="240" y="180" width="200" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="202" text-anchor="middle" font-size="12">커널 (안전망 역할)</text>
  <rect x="240" y="228" width="200" height="34" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="250" text-anchor="middle" font-size="12">하드웨어</text>
  <line x1="340" y1="70" x2="340" y2="82" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="340" y1="118" x2="340" y2="130" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="340" y1="166" x2="340" y2="178" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="340" y1="214" x2="340" y2="226" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="465" y="36" width="200" height="34" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="565" y="58" text-anchor="middle" font-size="12">내 코드 (모듈 .ko)</text>
  <rect x="465" y="84" width="200" height="82" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="565" y="110" text-anchor="middle" font-size="12">나머지 커널 전체</text>
  <text x="565" y="130" text-anchor="middle" font-size="12">같은 주소 공간 · 같은 EL1</text>
  <text x="565" y="150" text-anchor="middle" font-size="12">서로 막아 주는 벽 없음</text>
  <rect x="465" y="180" width="200" height="34" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="565" y="202" text-anchor="middle" font-size="12">하드웨어</text>
  <line x1="565" y1="70" x2="565" y2="82" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="565" y1="166" x2="565" y2="178" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="15" y="290" width="200" height="58" rx="5" fill="#e08a3c" fill-opacity="0.06" stroke="#e08a3c" stroke-dasharray="4 3"/>
  <text x="115" y="314" text-anchor="middle" font-size="12">죽으면: HardFault</text>
  <text x="115" y="334" text-anchor="middle" font-size="12">→ 워치독 리셋</text>
  <rect x="240" y="290" width="200" height="58" rx="5" fill="#4a7bd0" fill-opacity="0.06" stroke="#4a7bd0" stroke-dasharray="4 3"/>
  <text x="340" y="314" text-anchor="middle" font-size="12">죽으면: SIGSEGV</text>
  <text x="340" y="334" text-anchor="middle" font-size="12">→ 그 프로세스만 종료</text>
  <rect x="465" y="290" width="200" height="58" rx="5" fill="#d0564a" fill-opacity="0.06" stroke="#d0564a" stroke-dasharray="4 3"/>
  <text x="565" y="314" text-anchor="middle" font-size="12">죽으면: Oops 또는 panic</text>
  <text x="565" y="334" text-anchor="middle" font-size="12">→ 장비 전체가 위험</text>
  <text x="340" y="384" text-anchor="middle" font-size="13">커널 코드는 펌웨어처럼 맨바닥에 서 있지만, 펌웨어보다 훨씬 많은 이웃과 함께 산다</text>
</svg>
```

그림 1 — 세 세계의 바닥. 유저 앱 밑에는 libc와 커널이라는 두 겹의 안전망이 있다. 커널 코드 밑에는 하드웨어뿐이고, 옆에는 벽 없는 이웃(나머지 커널)이 있다.

### 0.1 세 세계 비교표 — 이 노트의 목차

이 노트는 아래 표의 각 행을 하나씩 **실제 모듈로 재현**한다. "커널" 열이 외울 대상이고, "펌웨어" 열은 Don이 이미 아는 것과의 연결 고리다.

| 제약 | Bare-metal 펌웨어 (Don의 SSD FW) | Linux 유저 앱 | Linux 커널 코드 | 절 |
|---|---|---|---|---|
| 표준 라이브러리 | 없거나 일부(newlib-nano), 직접 만든 `uart_printf` | glibc 전부 | **libc 없음**. `lib/string.c`, `printk`, `kmalloc`, `kstrto*` | §2 |
| 부동소수점·SIMD | FPU 있으면 그냥 씀 (ISR에서 lazy stacking 주의) | 자유 | **금지** (`-mgeneral-regs-only`). 예외는 `kernel_neon_begin/end` | §3 |
| 스택 | 링커 스크립트로 고정, 넘치면 조용히 깨짐 | 8 MiB + 자동 확장 | **태스크당 16 KiB 고정**(arm64), 넘치면 panic | §4 |
| 재귀 | 피함 (MISRA 금지) | 자유 | **사실상 금지** (깊이를 증명할 수 있을 때만) | §4 |
| 기다리기(sleep) | ISR에서 금지, 태스크에서는 RTOS 지연 | 자유 | **atomic context에서 금지** (스핀락·IRQ·softirq) | §5 |
| 메모리 할당 | 부팅 때 정적 풀 | `malloc` 실패는 드묾 (overcommit) | **언제든 실패 가능**, GFP 플래그로 "기다려도 되나"를 알려야 함 | §6 |
| 외부 입력 포인터 | 호스트 명령(NVMe SQ)의 PRP 주소를 검증 후 DMA | 내 주소 공간뿐 | **유저 포인터 직접 역참조 금지**, `copy_from_user` | §7 |
| 메모리 보호 | MPU 쓰면 일부 | 프로세스 간 MMU 격리 | **커널 내부 보호 없음**. 잘못 쓰면 남의 객체가 깨짐 | §8 |
| 에러 처리 | 에러 코드 + assert + 리셋 | 예외·`errno`·프로세스 종료 | **예외 없음**. 음수 errno 반환 + `goto` 정리 | §9 |
| 동시성 | 메인 루프 + ISR, 멀티코어면 공유 메모리 락 | 스레드는 내가 만들 때만 | **언제나** — 선점·SMP·IRQ·softirq | §10 |
| 이식성 | 칩 하나, 엔디언 고정 | 대체로 x86/arm64 | 30여 아키텍처, LE/BE, 32/64비트. `__le32`, `get_unaligned_*` | §11 |
| 긴 busy loop | 워치독이 리셋 | 다른 프로세스가 대신 돈다 | **soft lockup 경고**, 선점 끄고 돌면 CPU 독점 | §12 |
| 코딩 스타일 | 사내 규칙 | 자유 | `Documentation/process/coding-style.rst` + `checkpatch.pl` | §13 |

---

## 1. 실습 준비 — 모듈 하나 = QEMU 부팅 하나

### 1.1 실행 환경

이 노트의 모든 커널 코드는 랩 커널(Linux 6.18.55 arm64, QEMU `virt`)에서 돌렸다. 일부 예제는 **위험한 짓을 일부러 해서 게스트를 panic시키므로**, VM 호스트 커널(`uname -r` → `7.0.0-38-generic`)이 아니라 일회용 QEMU 게스트에만 넣는다.

```sh
uname -r                                          # VM 호스트
grep -E "^(# )?CONFIG_(VMAP_STACK|FRAME_WARN|DEBUG_ATOMIC_SLEEP|SOFTLOCKUP_DETECTOR|ARM64_PAN|PREEMPT|KERNEL_MODE_NEON|INIT_STACK_ALL_ZERO|CPU_LITTLE_ENDIAN)[= ]" \
    /var/lab/out/linux-build/.config
```

```text
7.0.0-38-generic
CONFIG_PREEMPT=y
CONFIG_KERNEL_MODE_NEON=y
CONFIG_CPU_LITTLE_ENDIAN=y
CONFIG_ARM64_PAN=y
CONFIG_VMAP_STACK=y
CONFIG_INIT_STACK_ALL_ZERO=y
CONFIG_FRAME_WARN=2048
# CONFIG_SOFTLOCKUP_DETECTOR is not set
# CONFIG_DEBUG_ATOMIC_SLEEP is not set
```

출력에서 볼 것: 랩 커널(arm64 defconfig 기반)은 **`DEBUG_ATOMIC_SLEEP`과 `SOFTLOCKUP_DETECTOR`가 꺼져 있다.** 양산 커널도 흔히 그렇다. §5와 §12는 이 차이 자체가 교재다 — 같은 버그가 디버그 옵션이 없으면 어떻게 숨어 버리는지 먼저 보고, 그다음 **두 옵션만 켠 커널 복사본**(`/var/tmp/E4/kbuild`, `LOCALVERSION=-e4dbg`)을 따로 빌드해서 같은 모듈을 다시 돌린다.

```sh
# 디버그 커널 복사본 (랩 소스는 읽기만, 출력은 내 디렉터리)
cp /var/lab/out/linux-build/.config /var/tmp/E4/kbuild/.config
cd /var/lab/src/linux-6.18.55
scripts/config --file /var/tmp/E4/kbuild/.config -e DEBUG_ATOMIC_SLEEP -e SOFTLOCKUP_DETECTOR --set-str LOCALVERSION "-e4dbg"
make O=/var/tmp/E4/kbuild olddefconfig
make O=/var/tmp/E4/kbuild -j5 Image modules_prepare
```

### 1.2 예제 1 — 게스트 실행 스크립트

무엇을 확인하나: `.ko`와 테스트 스크립트를 랩 initramfs 복사본에 넣고, 커널 인자 `LAB_CMD=sh~/t.sh`로 스크립트 하나를 돌린 뒤 자동으로 꺼지는 실행기. 이 노트의 모든 게스트 출력은 이 스크립트로 얻었다.

```sh
#!/bin/bash
# usage: run_guest.sh <tag> <test.sh> [extra kernel args] ; env KIMG, FILES, SMP, CPU, TMO
set -e
tag=$1; t=$2; extra=$3
W=/var/tmp/E4/run_$tag
rm -rf $W && mkdir -p $W/r
cp -a /var/lab/build/rootfs/. $W/r/
for f in $FILES; do cp $f $W/r/; done
cp $t $W/r/t.sh
(cd $W/r && find . | cpio -o -H newc --quiet | gzip) > $W/i.gz
KIMG=${KIMG:-/var/lab/out/Image}
timeout ${TMO:-120} qemu-system-aarch64 -machine virt -cpu ${CPU:-cortex-a57} -smp ${SMP:-2} -m 512M -nographic -no-reboot \
  -kernel $KIMG -initrd $W/i.gz -append "console=ttyAMA0 quiet $extra LAB_CMD=sh~/t.sh" 2>&1 | tr -d '\r'
```

모듈은 전부 같은 방식으로 빌드한다 (`Kbuild` 한 줄 + 랩 빌드 트리):

```sh
mkdir -p /var/tmp/E4/e4_nolibc && cd /var/tmp/E4/e4_nolibc
echo "obj-m += e4_nolibc.o" > Kbuild
make -C /var/lab/out/linux-build M=$PWD modules
```

출력에서 볼 것: `-no-reboot`가 중요하다. 이 노트에는 일부러 panic을 내는 예제가 있는데, `panic=-1`로 재부팅되면 initramfs가 같은 `LAB_CMD`를 또 실행해서 무한 반복된다. `-no-reboot`면 QEMU가 그냥 종료한다. `quiet`라서 콘솔에는 `pr_info`가 안 보이므로 각 테스트 스크립트는 마지막에 `dmesg | grep`으로 로그를 꺼낸다 (Oops·panic은 우선순위가 높아서 `quiet`여도 콘솔에 바로 찍힌다).

---

## 2. libc가 없다 — 그럼 무엇을 쓰나

### 2.1 직관

유저 앱의 `printf`·`malloc`·`strtol`은 glibc 함수이고, glibc는 결국 `write()`·`mmap()` 같은 **syscall을 부른다**. 커널은 syscall을 **받는** 쪽이니 glibc를 쓸 수 없다(자기 자신을 부르는 셈). 펌웨어에서 newlib의 `_write` stub을 직접 UART로 연결했던 것과 같은 상황인데, 커널은 아예 libc를 링크하지 않고 **필요한 함수를 커널 안에 다시 구현해 두었다**: `lib/string.c`(문자열), `lib/vsprintf.c`(`snprintf`), `lib/kstrtox.c`(숫자 파싱), `lib/sort.c`(정렬), `mm/slub.c`(`kmalloc`).

### 2.2 예제 2 — `#include <stdio.h>`는 어디서 막히나

무엇을 확인하나: 커널 모듈에서 libc 헤더를 include하면 무슨 일이 생기는지.

```c
// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <stdio.h>

static int __init e4_stdio_init(void)
{
	printf("hello from kernel?\n");
	return 0;
}
module_init(e4_stdio_init);
MODULE_LICENSE("GPL");
```

```text
  CC [M]  e4_stdio.o
e4_stdio.c:3:10: fatal error: stdio.h: No such file or directory
    3 | #include <stdio.h>
      |          ^~~~~~~~~
compilation terminated.
make: *** [/var/lab/src/linux-6.18.55/Makefile:248: __sub-make] Error 2
```

출력에서 볼 것: **링크 단계가 아니라 전처리 단계**에서 막힌다. VM에는 `/usr/include/stdio.h`가 분명히 있는데도 못 찾는다. 이유는 다음 예제의 `-nostdinc`다.

### 2.3 예제 3 — Kbuild가 실제로 넘기는 플래그 (`V=1`)

무엇을 확인하나: 모듈 `.c` 하나를 컴파일할 때 Kbuild가 gcc에 넘기는 플래그 중 "커널은 앱이 아니다"를 강제하는 것들.

```sh
cd /var/tmp/E4/e4_nolibc && rm -f e4_nolibc.o
make -C /var/lab/out/linux-build M=$PWD V=1 modules 2>&1 | grep -- "-o e4_nolibc.o" > cc.txt
tr " " "\n" < cc.txt | grep -E "^-(nostdinc|mgeneral|ffreestanding|std=|Wframe-larger|fstack-protector|mstack-protector|ftrivial|fno-common|fno-strict-aliasing|mbranch|D__KERNEL__|DMODULE|Werror)" | sort | uniq
```

```text
-DMODULE
-D__KERNEL__
-Werror=date-time
-Werror=designated-init
-Werror=implicit-function-declaration
-Werror=implicit-int
-Werror=incompatible-pointer-types
-Werror=return-type
-Werror=strict-prototypes
-Wframe-larger-than=2048
-fno-common
-fno-strict-aliasing
-fstack-protector-strong
-ftrivial-auto-var-init=zero
-mbranch-protection=pac-ret
-mgeneral-regs-only
-mstack-protector-guard-offset=1304
-mstack-protector-guard-reg=sp_el0
-mstack-protector-guard=sysreg
-nostdinc
-std=gnu11
```

출력에서 볼 것 — 이 노트의 제약 대부분이 여기 플래그로 이미 박혀 있다:

| 플래그 | 뜻 | 관련 제약 |
|---|---|---|
| `-nostdinc` | 시스템 include 경로(`/usr/include`)를 검색하지 않음. 커널 `include/`·`arch/arm64/include/`만 | libc 없음 (§2) |
| `-mgeneral-regs-only` | 정수 레지스터(x0~x30)만 쓰는 코드 생성. FP/SIMD 레지스터(v0~v31) 사용 금지 | FPU 금지 (§3) |
| `-Wframe-larger-than=2048` | 함수 하나의 스택 프레임이 2048 B 넘으면 경고 (`CONFIG_FRAME_WARN`) | 작은 스택 (§4) |
| `-fstack-protector-strong` + `-mstack-protector-guard=sysreg` | 스택 카나리. 카나리 값을 `sp_el0`(현재 `task_struct`)에서 읽음 | 스택 깨짐 감지 |
| `-ftrivial-auto-var-init=zero` | 초기화 안 한 지역 변수를 0으로 (`CONFIG_INIT_STACK_ALL_ZERO`) | 정보 유출 방지 |
| `-std=gnu11` | C11 + GNU 확장(문장 표현식, `typeof`, 지정 초기화) | E5 관용구 |
| `-Werror=implicit-function-declaration` | 선언 안 된 함수 호출은 에러 | 헤더 누락이 경고가 아니라 빌드 실패가 된다 (예제 12 작성 중 `linux/mm.h`를 빠뜨렸을 때 `is_vmalloc_addr`에서 실제로 걸렸다) |
| `-D__KERNEL__` | 헤더가 커널용 정의를 고르게 함 | uapi 헤더 공유 |

**B1에서 확인했듯 arm64 커널의 일반 플래그에 `-ffreestanding`은 없다.** 커널 루트 `Makefile`이 `NOSTDINC_FLAGS += -nostdinc`를 넣고, `arch/arm64/Makefile`이 `KBUILD_CFLAGS += $(CC_FLAGS_NO_FPU)`(= `-mgeneral-regs-only`)를 넣는다. `-ffreestanding`은 arm64에서는 **FPU 번역 단위 전용 플래그**(`CC_FLAGS_FPU`)에만 들어 있다 — `<arm_neon.h>`를 쓰려면 필요해서다(§3.4). 그래서 gcc는 커널을 "hosted" 환경으로 보고 `memcpy`·`strlen` 같은 builtin 최적화를 그대로 하는데, 커널이 같은 이름의 함수를 직접 제공하므로 문제가 없다.

### 2.4 예제 4 — libc 대체 함수 모음 (`e4_nolibc`)

무엇을 확인하나: 센서 설정 문자열 `"rate=400,range=0x10"`을 파싱하는 앱 코드를 커널식으로 바꾸면 어떤 함수를 쓰게 되는지.

```c
// SPDX-License-Identifier: GPL-2.0
/* e4_nolibc: libc가 없을 때 커널이 대신 주는 것들 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/sort.h>

static char *cfg = "rate=400,range=16";
module_param(cfg, charp, 0444);

static int e4_cmp(const void *a, const void *b)
{
	return *(const int *)a - *(const int *)b;
}

static int __init e4_nolibc_init(void)
{
	char buf[32], *dup, *s, *tok;
	int samples[] = { 42, -7, 1000, 3 };
	unsigned int val;
	int ret;

	dup = kstrdup(cfg, GFP_KERNEL);		/* strdup -> kstrdup + GFP */
	if (!dup)
		return -ENOMEM;
	s = dup;
	while ((tok = strsep(&s, ",")) != NULL) {	/* strtok -> strsep */
		char *eq = strchr(tok, '=');

		if (!eq)
			continue;
		*eq = '\0';
		ret = kstrtouint(eq + 1, 0, &val);	/* atoi/strtoul -> kstrto* */
		pr_info("key=%s val=%u ret=%d\n", tok, val, ret);
	}
	ret = kstrtouint("12abc", 0, &val);
	pr_info("kstrtouint(\"12abc\") = %d (%pe)\n", ret, ERR_PTR(ret));

	sort(samples, ARRAY_SIZE(samples), sizeof(int), e4_cmp, NULL); /* qsort */
	snprintf(buf, sizeof(buf), "%d,%d,%d,%d",	/* snprintf: lib/vsprintf.c */
		 samples[0], samples[1], samples[2], samples[3]);
	pr_info("sorted=%s len=%zu\n", buf, strlen(buf));

	ret = strscpy(buf, "this string is longer than 32 bytes", sizeof(buf));
	pr_info("strscpy ret=%d buf=\"%s\"\n", ret, buf);
	kfree(dup);
	return 0;
}

static void __exit e4_nolibc_exit(void) { }
module_init(e4_nolibc_init);
module_exit(e4_nolibc_exit);
MODULE_DESCRIPTION("E4: libc replacements in the kernel");
MODULE_LICENSE("GPL");
```

```sh
# 게스트 안 (t.sh)
insmod /e4_nolibc.ko cfg="rate=400,range=0x10,odr=fast"
dmesg | grep e4_nolibc
```

```text
lab initramfs: 6.18.55 up 5.41s
[    6.932616] e4_nolibc: loading out-of-tree module taints kernel.
[    6.948865] e4_nolibc: key=rate val=400 ret=0
[    6.949084] e4_nolibc: key=range val=16 ret=0
[    6.949157] e4_nolibc: key=odr val=16 ret=-22
[    6.949207] e4_nolibc: kstrtouint("12abc") = -22 (-EINVAL)
[    6.951543] e4_nolibc: sorted=-7,3,42,1000 len=12
[    6.951600] e4_nolibc: strscpy ret=-7 buf="this string is longer than 32 b"
```

출력에서 볼 것:

- `kstrtouint`는 `atoi`와 달리 **"12abc"를 거부**한다(-EINVAL). `atoi("12abc")`는 12를 돌려주고 에러를 알려 주지 않는다. 커널은 유저가 sysfs·모듈 파라미터로 넣는 문자열을 믿지 않으므로 엄격한 쪽만 제공한다. `base=0`이면 `0x10`도 16으로 읽는다.
- `odr=fast` 줄: 실패하면 `val`은 **바뀌지 않는다** — 직전 값 16이 그대로 찍혔다. 반환값을 무시하고 `val`을 쓰면 엉뚱한 값을 쓰게 된다(§9).
- `strscpy`는 잘리면 `-E2BIG`(-7)을 돌려주고 항상 NUL로 끝낸다. `strcpy`/`strncpy`/`strlcpy`는 새 커널 코드에서 쓰지 않는다(`checkpatch`가 경고).
- `%pe`는 커널 전용 포맷: 에러 포인터를 `-EINVAL` 같은 이름으로 찍는다. `%pOF`, `%pR`, `%ph`, `%px`처럼 커널 `printk`에는 포인터 확장 포맷이 많다(`Documentation/core-api/printk-formats.rst`).

### 2.5 이 함수들은 어디서 오나 — `nm -u`

```sh
nm -u /var/tmp/E4/e4_nolibc/e4_nolibc.ko
```

```text
                 U __stack_chk_fail
                 U _printk
                 U kfree
                 U kstrdup
                 U kstrtouint
                 U param_ops_charp
                 U sized_strscpy
                 U snprintf
                 U sort
                 U strchr
                 U strlen
                 U strsep
```

출력에서 볼 것: `.ko`는 이 심볼들을 **미해결(U)** 로 들고 있다가, `insmod` 때 커널이 `EXPORT_SYMBOL`로 공개한 심볼 표에서 주소를 채운다(E3). libc의 동적 링커가 하는 일을 커널 모듈 로더가 한다. `strchr`·`strlen`은 arm64가 어셈블리로 최적화한 버전(`arch/arm64/lib/strchr.S` 등, `asm/string.h`의 `__HAVE_ARCH_STRCHR`)이고, 나머지는 `lib/string.c`의 C 구현이다. `pr_info`는 매크로라서 실제로는 `_printk`가 남는다.

| 앱에서 쓰던 것 | 커널에서 쓰는 것 | 정의 위치 | 차이점 |
|---|---|---|---|
| `printf` | `pr_info`/`pr_err`, 장치가 있으면 `dev_info` | `kernel/printk/` | 로그 레벨 필수, 링버퍼(`dmesg`)로 감 |
| `malloc`/`free` | `kmalloc`/`kfree`, `kzalloc`, `vmalloc`, `devm_kzalloc` | `mm/slub.c`, `mm/vmalloc.c` | **GFP 플래그 필수** (§6) |
| `strdup` | `kstrdup(s, GFP_KERNEL)` | `mm/util.c` | GFP 필요 |
| `atoi`/`strtoul` | `kstrtoint`/`kstrtouint`/`kstrtoul` | `lib/kstrtox.c` | 엄격, 에러 반환 |
| `strcpy`/`strncpy` | `strscpy` | `lib/string.c` | 항상 NUL 종료, 잘리면 `-E2BIG` |
| `strtok` | `strsep` | `lib/string.c` | 재진입 가능 |
| `qsort`/`bsearch` | `sort`/`bsearch` | `lib/sort.c`, `lib/bsearch.c` | heapsort (최악도 O(n log n), 재귀 없음) |
| `sleep`/`usleep` | `msleep`/`usleep_range`/`udelay` | `kernel/time/` | sleep vs busy-wait 구분 (§5) |
| `assert` | `WARN_ON`/`BUG_ON` | `include/asm-generic/bug.h` | `BUG_ON`은 최후 수단 |
| `pthread_mutex` | `mutex`, `spinlock_t` | `kernel/locking/` | F2 |
| `errno` | 음수 반환값 (`-EINVAL`) | `include/uapi/asm-generic/errno-base.h` | 전역 errno 없음 |

> 함정: 예제 4를 처음 빌드할 때 비교 함수 이름을 `cmp_int`로 지었더니 `expected identifier or '(' before 'const'` 에러가 났다. `include/linux/sort.h`에 `#define cmp_int(l, r) ...` 매크로가 이미 있어서다. 커널은 **하나의 거대한 전역 네임스페이스**다 — 모듈 안의 함수·변수는 `static`으로 숨기고, 이름에는 드라이버 접두어(`e4_`, `bmi270_`)를 붙인다.

---

## 3. 부동소수점·NEON은 기본 금지

### 3.1 직관 — 왜 금지인가

유저 프로세스가 syscall이나 인터럽트로 커널에 들어올 때 커널은 **정수 레지스터(x0~x30)만** 저장한다. FP/SIMD 레지스터(v0~v31, 각 128비트 = 512 B, SVE면 더 큼)는 저장하지 않는다. 커널이 그 레지스터를 안 건드린다고 약속했기 때문이고, 그 덕분에 syscall 진입·탈출이 빠르다. 커널 코드가 `float`를 쓰면 컴파일러가 `v0`~`v31`을 쓰는 명령을 내고, **유저 프로세스의 FP 레지스터 값이 몰래 바뀐다** — 유저 앱의 `double` 계산 결과가 가끔 틀리는 최악의 버그다.

펌웨어 비유: Cortex-M4F에서 ISR이 FPU를 쓰면 lazy stacking으로 S0~S15를 자동 저장한다. 커널은 이 "자동 저장"을 일부러 하지 않는 대신 **FPU를 쓰지 않겠다는 계약**을 컴파일러 플래그(`-mgeneral-regs-only`)로 강제한다.

### 3.2 예제 5 — `float` 한 줄이면 컴파일 에러

무엇을 확인하나: 센서 raw 값을 m/s² 로 바꾸는 "앱이라면 평범한" 코드가 커널 빌드에서 어떻게 막히는지.

```c
// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>

static int raw = 157;			/* 센서 raw 값 (1 LSB = 0.01 g) */
module_param(raw, int, 0444);

static noinline int e4_to_mps2_x100(int r)
{
	float g = r / 100.0f;		/* 앱이라면 아무 문제 없는 한 줄 */

	return (int)(g * 981.0f);
}

static int __init e4_float_init(void)
{
	pr_info("accel=%d (x0.01 m/s^2)\n", e4_to_mps2_x100(raw));
	return 0;
}
module_init(e4_float_init);
MODULE_LICENSE("GPL");
```

```text
  CC [M]  e4_float.o
e4_float.c: In function ‘e4_to_mps2_x100’:
e4_float.c:11:16: error: ‘-mgeneral-regs-only’ is incompatible with the use of floating-point types
   11 |         return (int)(g * 981.0f);
      |                ^~~~~~~~~~~~~~~~~
e4_float.c:11:16: error: ‘-mgeneral-regs-only’ is incompatible with the use of floating-point types
...  (같은 줄이 여러 번 반복 — 생략)
```

출력에서 볼 것: arm64에서는 **경고가 아니라 에러**다. x86 커널도 `arch/x86/Makefile`에서 `-mno-sse -mno-mmx -mno-sse2 -mno-avx`(64비트는 `-mno-80387`도)를 넣어 같은 계약을 강제한다. 처음 시도에서 `noinline`이 없었을 때는 gcc가 함수를 `pr_info` 호출 안으로 인라인한 뒤 **에러 위치를 `printk.h` 매크로로 찍었다** — 에러 메시지가 엉뚱한 헤더를 가리키면 인라인을 의심하라.

커널식 해법은 **고정소수점(fixed-point)** 이다. Don이 펌웨어에서 늘 하던 것과 같다: `accel_x100 = raw * 981 / 100;`. IIO 서브시스템(I2)은 아예 정수 raw 값 + `scale`(정수부·소수부 두 정수)만 커널이 내보내고 **실수 변환은 유저 공간**이 하도록 설계돼 있다.

### 3.3 그래도 FP/SIMD가 꼭 필요하면 — 커널 FPU 구간

암호(AES·SHA·CRC), RAID6 패리티, XOR 같은 곳은 NEON이 몇 배 빠르므로 커널도 쓴다. 규칙(`Documentation/core-api/floating-point.rst`):

1. FP 코드는 **별도의 `.c` 파일**에 둔다.
2. 그 파일만 `CFLAGS_<file>.o += $(CC_FLAGS_FPU)`, `CFLAGS_REMOVE_<file>.o += $(CC_FLAGS_NO_FPU)`로 컴파일한다.
3. 호출하는 쪽(일반 파일)에서 `kernel_fpu_begin()`/`kernel_fpu_end()`로 감싼다. arm64에서는 `asm/fpu.h`가 이를 `kernel_neon_begin()`/`kernel_neon_end()`로 정의한다.

### 3.4 예제 6 — FPU 전용 번역 단위 + `kernel_fpu_begin/end`

무엇을 확인하나: 위 규칙대로 하면 같은 float 계산이 빌드되고 돌아가는지, FP 명령이 어느 오브젝트에만 들어가는지.

```c
// e4_neon_fp.c
// SPDX-License-Identifier: GPL-2.0
/* FP 전용 번역 단위: 이 파일만 -mgeneral-regs-only 없이 컴파일된다 */
#include <linux/types.h>
#include "e4_neon.h"

int e4_accel_sum_milli(const s16 *raw, int n, int lsb_per_g)
{
	float sum = 0.0f;
	int i;

	for (i = 0; i < n; i++)
		sum += (float)raw[i] / (float)lsb_per_g * 9.80665f;
	return (int)(sum * 1000.0f);		/* 결과는 정수로 돌려준다 */
}
```

```c
// e4_neon_main.c
// SPDX-License-Identifier: GPL-2.0
/* e4_neon: FP/SIMD는 kernel_fpu_begin()/end() 구간 안에서만 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/preempt.h>
#include <asm/fpu.h>
#include <asm/simd.h>
#include "e4_neon.h"

static const s16 raw[8] = { 2048, 2050, 2047, 2049, 2048, 2046, 2051, 2048 };

static int __init e4_neon_init(void)
{
	int milli;

	if (!kernel_fpu_available() || !may_use_simd())
		return -ENODEV;
	kernel_fpu_begin();			/* = kernel_neon_begin() on arm64 */
	milli = e4_accel_sum_milli(raw, ARRAY_SIZE(raw), 2048);
	kernel_fpu_end();
	pr_info("sum of 8 samples = %d.%03d m/s^2 (preempt_count=%#x)\n",
		milli / 1000, milli % 1000, preempt_count());
	return 0;
}

static void __exit e4_neon_exit(void) { }
module_init(e4_neon_init);
module_exit(e4_neon_exit);
MODULE_DESCRIPTION("E4: kernel-mode NEON/FP section");
MODULE_LICENSE("GPL");
```

```c
// e4_neon.h
/* SPDX-License-Identifier: GPL-2.0 */
int e4_accel_sum_milli(const s16 *raw, int n, int lsb_per_g);
```

```make
obj-m += e4_neon.o
e4_neon-y := e4_neon_main.o e4_neon_fp.o
CFLAGS_e4_neon_fp.o += $(CC_FLAGS_FPU)
CFLAGS_REMOVE_e4_neon_fp.o += $(CC_FLAGS_NO_FPU)
```

```sh
make -C /var/lab/out/linux-build M=$PWD modules
objdump -d e4_neon_fp.o   | grep -cE "\b(fmul|fdiv|fadd|scvtf|fcvtzs|fmadd)\b"
objdump -d e4_neon_main.o | grep -cE "\b(fmul|fdiv|fadd|scvtf|fcvtzs)\b"
objdump -d e4_neon_fp.o   | grep -E "\b(fmul|fdiv|fadd|scvtf|fcvtzs)\b" | head -5
```

```text
  CC [M]  e4_neon_main.o
  CC [M]  e4_neon_fp.o
  LD [M]  e4_neon.o
  MODPOST Module.symvers
  CC [M]  e4_neon.mod.o
  CC [M]  .module-common.o
  LD [M]  e4_neon.ko
6
0
   c:	1e22005d 	scvtf	s29, w2
  24:	1e22005f 	scvtf	s31, w2
  28:	1e3d1bff 	fdiv	s31, s31, s29
  40:	1e3f0bde 	fmul	s30, s30, s31
  44:	1e3803c0 	fcvtzs	w0, s30
```

```text
[    4.794383] e4_neon: sum of 8 samples = 78.467 m/s^2 (preempt_count=0x0)
```

출력에서 볼 것: FP 명령(`scvtf`·`fdiv`·`fmul`, 레지스터 `s29`~`s31`)은 `e4_neon_fp.o`에만 있고 `e4_neon_main.o`에는 0개다. 계산값 16387/2048 × 9.80665 = 78.467이 맞다.

`kernel_neon_begin()`(`arch/arm64/kernel/fpsimd.c`)이 하는 일: `may_use_simd()`가 거짓이면 `BUG_ON`, 현재 유저 태스크의 FPSIMD 상태를 메모리에 저장(`fpsimd_save_user_state()`), `TIF_KERNEL_FPSTATE` 플래그를 세운다. 6.18의 arm64는 이 플래그 덕분에 **FPU 구간 안에서도 선점될 수 있다**(커널 FP 상태도 컨텍스트 스위치 때 저장) — 그래서 출력의 `preempt_count`가 0이다. 예전 커널(대략 6.8 이전)은 구간 전체에서 선점을 껐다. 아키텍처·버전마다 다르니 "FPU 구간은 짧게, 그 안에서 잠들지 말 것"을 기본 규칙으로 기억한다.

---

## 4. 작고 고정된 커널 스택 — 16 KiB, 넘치면 panic

### 4.1 직관

유저 프로세스의 메인 스레드 스택은 기본 8 MiB(`ulimit -s`)이고 모자라면 page fault로 자동으로 늘어난다. 커널은 **스레드(태스크)마다 스택을 하나씩** 따로 갖는데, 수천 개 태스크가 각각 메가바이트 단위 스택을 가지면 메모리가 남아나지 않는다. 그래서 커널 스택은 작고, **고정 크기이며, 늘어나지 않는다.** 펌웨어의 링커 스크립트 스택(`__stack_size = 0x1000`)과 같은 처지다. 다른 점은 태스크 수가 동적이라는 것, 그리고 넘쳤을 때 잡아 주는 장치(guard page)가 있다는 것이다.

### 4.2 정의 — arm64의 `THREAD_SIZE`는 어디서 정해지나

랩 소스 `arch/arm64/include/asm/memory.h`:

```c
#define MIN_THREAD_SHIFT	(14 + KASAN_THREAD_SHIFT)

/*
 * VMAP'd stacks are allocated at page granularity, so we must ensure that such
 * stacks are a multiple of page size.
 */
#if (MIN_THREAD_SHIFT < PAGE_SHIFT)
#define THREAD_SHIFT		PAGE_SHIFT
#else
#define THREAD_SHIFT		MIN_THREAD_SHIFT
#endif
...
#define THREAD_SIZE		(UL(1) << THREAD_SHIFT)

/*
 * By aligning VMAP'd stacks to 2 * THREAD_SIZE, we can detect overflow by
 * checking sp & (1 << THREAD_SHIFT), which we can do cheaply in the entry
 * assembly.
 */
#define THREAD_ALIGN		(2 * THREAD_SIZE)

#define IRQ_STACK_SIZE		THREAD_SIZE

#define OVERFLOW_STACK_SIZE	SZ_4K
```

읽는 법:

- 기본 `1 << 14` = **16 KiB**. KASAN을 켜면 `KASAN_THREAD_SHIFT`가 1이 되어 32 KiB(계측 코드가 스택을 더 먹으므로). 64K 페이지 커널이면 `PAGE_SHIFT`(16)가 더 커서 64 KiB.
- `IRQ_STACK_SIZE`도 16 KiB이고 **CPU마다** 하나씩 따로 있다. 하드웨어 인터럽트와 softirq는 이 IRQ 스택에서 돈다 (태스크 스택을 더 깊게 파먹지 않도록).
- `OVERFLOW_STACK_SIZE` 4 KiB — 스택이 넘쳤을 때 **panic 메시지를 찍기 위해서만** 쓰는 CPU별 비상 스택.
- 32비트 ARM은 8 KiB(`THREAD_SIZE_ORDER` 1), x86-64는 16 KiB(KASAN 시 더 큼)다. 면접에서는 "아키텍처마다 다르고 보통 8~16 KiB"라고 답한다.

### 4.3 예제 7 — 내 코드가 돌 때 스택이 얼마나 남았나 (`e4_stack`)

무엇을 확인하나: 실제 `THREAD_SIZE`, 스택이 vmalloc 영역에 있는지(VMAP_STACK), `module_init`이 불렸을 때 이미 쓴 스택, 타이머 콜백(softirq)은 어느 스택에서 도는지 — 그리고 `depth` 파라미터를 주면 재귀로 스택을 소진한다.

```c
// SPDX-License-Identifier: GPL-2.0
/* e4_stack: 커널 스택 크기·사용량·오버플로 관찰 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/sched/task_stack.h>
#include <linux/mm.h>
#include <linux/timer.h>
#include <linux/delay.h>

static int depth;			/* >0 이면 그 깊이까지 재귀 */
module_param(depth, int, 0444);
static struct timer_list e4_timer;

static unsigned long stack_used(void)
{
	unsigned long top = (unsigned long)task_stack_page(current) + THREAD_SIZE;
	unsigned long sp = current_stack_pointer;

	if (sp < top && sp >= top - THREAD_SIZE)
		return top - sp;
	return 0;			/* 태스크 스택이 아님 (IRQ 스택 등) */
}

static noinline int e4_recurse(int n)
{
	volatile char pad[512];		/* 프레임 하나 ≈ 512 B + α */

	pad[0] = (char)n;
	if (n % 4 == 0)
		pr_info("depth=%d used=%lu left=%lu\n", n, stack_used(),
			THREAD_SIZE - stack_used());
	if (n >= depth)
		return pad[0];
	return e4_recurse(n + 1) + pad[0];
}

static void e4_timer_fn(struct timer_list *t)
{
	pr_info("timer(softirq): in_task_stack=%s sp=%px\n",
		stack_used() ? "yes" : "no", (void *)current_stack_pointer);
}

static int __init e4_stack_init(void)
{
	void *base = task_stack_page(current);

	pr_info("THREAD_SIZE=%lu PAGE_SIZE=%lu\n", THREAD_SIZE, PAGE_SIZE);
	pr_info("stack base=%px vmalloc=%d used_at_init=%lu\n",
		base, is_vmalloc_addr(base), stack_used());
	timer_setup(&e4_timer, e4_timer_fn, 0);
	mod_timer(&e4_timer, jiffies + 1);
	msleep(20);
	if (depth > 0)
		pr_info("recurse result %d\n", e4_recurse(1));
	return 0;
}

static void __exit e4_stack_exit(void)
{
	timer_delete_sync(&e4_timer);
}
module_init(e4_stack_init);
module_exit(e4_stack_exit);
MODULE_DESCRIPTION("E4: kernel stack size and overflow");
MODULE_LICENSE("GPL");
```

```sh
# 게스트 안 (t.sh)
insmod /e4_stack.ko
dmesg | grep e4_stack
rmmod e4_stack
echo "== depth=8"
insmod /e4_stack.ko depth=8
dmesg | grep "e4_stack: depth\|recurse"
rmmod e4_stack
```

```text
lab initramfs: 6.18.55 up 5.69s
[    7.398883] e4_stack: loading out-of-tree module taints kernel.
[    7.506464] e4_stack: THREAD_SIZE=16384 PAGE_SIZE=4096
[    7.506603] e4_stack: stack base=ffff8000804b0000 vmalloc=1 used_at_init=1280
[    7.512878] e4_stack: timer(softirq): in_task_stack=no sp=ffff80008000be00
== depth=8
[    7.945102] e4_stack: depth=4 used=3520 left=12864
[    7.945203] e4_stack: depth=8 used=5760 left=10624
[    7.945291] e4_stack: recurse result 36
```

출력에서 볼 것 (주소는 실행마다 다르다):

- `THREAD_SIZE=16384` — 소스에서 읽은 16 KiB가 맞다. `vmalloc=1` — 스택이 vmalloc 영역(`0xffff8000...`)에 있다. 이게 `CONFIG_VMAP_STACK`이다 (§4.5).
- `used_at_init=1280` — `insmod` syscall → `load_module` → `do_init_module` → `do_one_initcall` → 내 `init`까지 오는 데 이미 1.25 KiB를 썼다. 드라이버의 `read()`는 VFS를 거쳐 오고, 블록 드라이버는 파일 시스템 → 블록 계층을 거쳐 오므로 **내 함수가 시작할 때 이미 수 KiB가 쓰여 있을 수 있다.** 내가 쓸 수 있는 예산은 16 KiB가 아니다.
- 타이머 콜백은 `in_task_stack=no` — softirq는 **CPU별 IRQ 스택**에서 돌았다 (sp가 태스크 스택 범위 밖).
- 재귀 4단계마다 2240 B씩 늘었다 → **프레임 하나에 560 B** (512 B 배열 + 저장 레지스터·카나리·정렬).

### 4.4 예제 8 — 재귀로 스택을 넘치게 하면 (`depth=100`)

무엇을 확인하나: 16 KiB를 넘는 순간 커널이 어떻게 반응하는지. 게스트가 죽으므로 마지막 명령으로 둔다.

```sh
echo "== depth=100 (overflow)"
insmod /e4_stack.ko depth=100
```

```text
== depth=100 (overflow)
[    8.244150] e4_stack: depth=4 used=3520 left=12864
[    8.244206] e4_stack: depth=8 used=5760 left=10624
[    8.244216] e4_stack: depth=12 used=8000 left=8384
[    8.244222] e4_stack: depth=16 used=10240 left=6144
[    8.244228] e4_stack: depth=20 used=12480 left=3904
[    8.244236] e4_stack: depth=24 used=14720 left=1664
[    8.244440] Insufficient stack space to handle exception!
[    8.244465] ESR: 0x0000000096000047 -- DABT (current EL)
[    8.244500] FAR: 0xffff8000804afff8
[    8.244512] Task stack:     [0xffff8000804b0000..0xffff8000804b4000]
[    8.244523] IRQ stack:      [0xffff800080000000..0xffff800080004000]
[    8.244530] Overflow stack: [0xffff00001feb8140..0xffff00001feb9140]
[    8.244920] CPU: 0 UID: 0 PID: 78 Comm: insmod Tainted: G           O        6.18.55 #1 PREEMPT 
...
[    8.245201] pstate: 00000005 (nzcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
[    8.245248] pc : __pi_memset_generic+0x50/0x188
[    8.245677] lr : e4_recurse+0x34/0xc0 [e4_stack]
[    8.245978] sp : ffff8000804afff0
...
[    8.247828] x20: ffff000004143540 x19: 000000000000001b x18: 000000000000000a
...
[    8.248173] Kernel panic - not syncing: kernel stack overflow
[    8.259577] CPU: 0 UID: 0 PID: 78 Comm: insmod Tainted: G           O        6.18.55 #1 PREEMPT 
[    8.262960] Call trace:
...
[    8.270828]  vpanic+0xf8/0x2dc
[    8.270892]  abort+0x0/0x4
[    8.270955]  add_taint+0x0/0xbc
[    8.271040]  panic_bad_stack+0x108/0x120
[    8.271128]  handle_bad_stack+0x34/0x40
[    8.271207]  __bad_stack+0x80/0x84
[    8.271320]  __pi_memset_generic+0x50/0x188 (P)
[    8.271403]  e4_recurse+0x8c/0xc0 [e4_stack]
[    8.271486]  e4_recurse+0x8c/0xc0 [e4_stack]
...  (e4_recurse 26줄 — 생략)
[    8.280791]  e4_stack_init+0xac/0xf94 [e4_stack]
[    8.280944]  do_one_initcall+0x60/0x1e0
[    8.281027]  do_init_module+0x54/0x240
[    8.281100]  load_module+0x18c0/0x1eb0
[    8.281168]  __do_sys_init_module+0x178/0x220
...
[    8.282088] SMP: stopping secondary CPUs
```

출력에서 볼 것:

- **FAR `0xffff8000804afff8`** 은 Task stack 범위 `[...804b0000..804b4000]`의 시작보다 **8바이트 아래** — 스택 바로 밑 guard page를 건드렸다.
- `x19 = 0x1b` = 27 — 27번째 재귀에서 죽었다. 24단계에서 1664 B 남았고 프레임당 560 B이므로 25, 26단계까지 들어가고 27단계에서 넘친다. 계산과 정확히 맞는다.
- `pc : __pi_memset_generic` — 죽은 곳이 내 코드가 아니라 `memset`이다. `CONFIG_INIT_STACK_ALL_ZERO`(`-ftrivial-auto-var-init=zero`)가 지역 배열 `pad[512]`를 0으로 채우는 `memset`을 넣었고, 그 `memset`이 guard page에 첫 쓰기를 했다. **Oops의 `pc`가 라이브러리 함수면 `lr`과 Call trace에서 진짜 범인을 찾는다.**
- `Insufficient stack space to handle exception!` → 예외 진입 코드가 sp를 검사하고 CPU별 **Overflow stack**(4 KiB)으로 갈아탄 뒤 `handle_bad_stack()` → `panic_bad_stack()` → `panic("kernel stack overflow")`. Oops가 아니라 **바로 panic**이다 — 스택이 깨진 태스크는 정리조차 할 수 없으니까.

```svg
<svg viewBox="0 0 680 470" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="250" y="20" text-anchor="middle" font-size="13" font-weight="bold">insmod 태스크의 커널 스택 (VMAP_STACK, 16 KiB)</text>
  <text x="140" y="46" text-anchor="end" font-size="12">0xffff8000804b4000</text>
  <rect x="150" y="34" width="200" height="34" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="250" y="56" text-anchor="middle" font-size="12">pt_regs (유저 레지스터)</text>
  <rect x="150" y="68" width="200" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="250" y="86" text-anchor="middle" font-size="12">el0_svc → init_module</text>
  <text x="250" y="101" text-anchor="middle" font-size="12">→ do_one_initcall</text>
  <rect x="150" y="108" width="200" height="26" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="250" y="126" text-anchor="middle" font-size="12">e4_stack_init (48 B)</text>
  <rect x="150" y="134" width="200" height="150" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="250" y="190" text-anchor="middle" font-size="12">e4_recurse × 24</text>
  <text x="250" y="210" text-anchor="middle" font-size="12">프레임당 560 B</text>
  <text x="250" y="230" text-anchor="middle" font-size="12">(512 B 배열 + 저장 레지스터)</text>
  <rect x="150" y="284" width="200" height="34" fill="#d0564a" fill-opacity="0.18" stroke="#d0564a"/>
  <text x="250" y="306" text-anchor="middle" font-size="12">25~27단계: 남은 1664 B</text>
  <text x="140" y="322" text-anchor="end" font-size="12">0xffff8000804b0000</text>
  <rect x="150" y="318" width="200" height="60" fill="#d0564a" fill-opacity="0.05" stroke="#d0564a" stroke-dasharray="5 4"/>
  <text x="250" y="342" text-anchor="middle" font-size="12">guard page (매핑 안 됨)</text>
  <text x="250" y="362" text-anchor="middle" font-size="12">FAR = 0x...804afff8</text>
  <line x1="365" y1="44" x2="365" y2="300" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="375" y="170" font-size="12">스택은 아래로 자란다</text>
  <rect x="425" y="300" width="245" height="96" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="547" y="322" text-anchor="middle" font-size="12">1. guard page 접근 → DABT</text>
  <text x="547" y="342" text-anchor="middle" font-size="12">2. 예외 진입 시 sp 검사 실패</text>
  <text x="547" y="362" text-anchor="middle" font-size="12">3. per-CPU overflow 스택 전환</text>
  <text x="547" y="382" text-anchor="middle" font-size="12">4. panic: kernel stack overflow</text>
  <line x1="355" y1="345" x2="423" y2="345" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="425" y="44" width="245" height="74" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="547" y="66" text-anchor="middle" font-size="12">THREAD_SIZE = 1 &lt;&lt; 14</text>
  <text x="547" y="86" text-anchor="middle" font-size="12">THREAD_ALIGN = 2 × THREAD_SIZE</text>
  <text x="547" y="106" text-anchor="middle" font-size="12">IRQ 스택은 CPU마다 따로 16 KiB</text>
  <text x="340" y="420" text-anchor="middle" font-size="12">VMAP_STACK이 없으면 guard page도 없다: 스택 아래의 다른 객체를 조용히 덮어쓴다</text>
  <text x="340" y="444" text-anchor="middle" font-size="12">(주소는 예제 8의 실제 실행 값, 실행마다 다르다)</text>
</svg>
```

그림 2 — 예제 8의 커널 스택. VMAP_STACK은 스택을 vmalloc 영역에 페이지 단위로 매핑하고 그 아래를 비워 둔다(guard page). 넘치는 첫 접근이 곧바로 data abort가 되어 "어디서 넘쳤는지"가 로그에 남는다.

### 4.5 VMAP_STACK이 왜 중요한가

`CONFIG_VMAP_STACK`(4.9에 x86, arm64는 4.14부터 지원)이 없던 시절 커널 스택은 물리적으로 연속인 페이지(`alloc_pages`)였고, 바로 아래에는 다른 커널 데이터가 있었다. 넘치면 그 데이터를 **조용히 덮어쓰고** 한참 뒤 전혀 다른 곳에서 이상한 증상으로 터졌다 — 펌웨어에서 스택이 `.bss` 끝을 덮어 전역 변수가 이상해지는 버그와 똑같다. 옛 커널은 `thread_info`를 스택 맨 아래에 두었기 때문에 넘치면 `thread_info`부터 깨졌다(`CONFIG_THREAD_INFO_IN_TASK`로 `task_struct` 안으로 옮긴 이유 중 하나). Don이 펌웨어에서 스택 끝에 `0xDEADBEEF` 패턴을 심어 두고 주기적으로 검사했던 것을, VMAP_STACK은 **MMU로 하드웨어 수준에서 즉시** 잡는다.

남은 디버그 장치:

| 장치 | 무엇을 하나 | 랩 커널 |
|---|---|---|
| `CONFIG_VMAP_STACK` | guard page로 overflow 즉시 감지 → panic | `=y` |
| `CONFIG_FRAME_WARN` | 빌드 때 함수 프레임이 N 바이트 넘으면 경고 | `2048` |
| `-fstack-usage` (`ccflags-y`) | 함수별 정적 스택 사용량을 `.su` 파일로 | 직접 켬 |
| `CONFIG_DEBUG_STACK_USAGE` | 태스크 종료 시 "used greatest stack depth" 기록 | 꺼짐 |
| `CONFIG_SCHED_STACK_END_CHECK` | 스케줄할 때 스택 끝 magic 검사 | 꺼짐 |
| `CONFIG_STACKPROTECTOR_STRONG` | 함수 반환 전 카나리 검사 (버퍼 overrun 감지) | `=y` |

### 4.6 예제 9 — 빌드 때 잡기: `-Wframe-larger-than`과 `-fstack-usage`

무엇을 확인하나: 앱 습관대로 "버퍼는 스택에" 4 KiB를 잡으면 빌드에서 경고가 나는지, 함수별 스택 사용량을 숫자로 뽑을 수 있는지.

```c
// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/string.h>

static uint len = 64;
module_param(len, uint, 0444);

static noinline int e4_parse_frame(u8 seed, size_t n)
{
	u8 frame[4096];			/* 앱 습관: "버퍼는 스택에" */
	size_t i;

	n = min(n, sizeof(frame));
	for (i = 0; i < n; i++)
		frame[i] = seed + i;
	return frame[n / 2];
}

static int __init e4_bigframe_init(void)
{
	pr_info("mid=%d\n", e4_parse_frame(0xa5, len));
	return 0;
}
module_init(e4_bigframe_init);
MODULE_DESCRIPTION("E4: frame size warning");
MODULE_LICENSE("GPL");
```

```make
obj-m += e4_bigframe.o
ccflags-y += -fstack-usage
```

```sh
make -C /var/lab/out/linux-build M=$PWD modules
cat e4_bigframe.su ../e4_stack/e4_stack.su
```

```text
  CC [M]  e4_bigframe.o
e4_bigframe.c: In function ‘e4_parse_frame.constprop’:
e4_bigframe.c:18:1: warning: the frame size of 4112 bytes is larger than 2048 bytes [-Wframe-larger-than=]
   18 | }
      | ^
  MODPOST Module.symvers
  LD [M]  e4_bigframe.ko
e4_bigframe.c:9:21:e4_parse_frame.constprop	4144	static
e4_bigframe.c:20:19:e4_bigframe_init	16	static
e4_stack.c:14:22:stack_used	0	static
e4_stack.c:37:13:e4_timer_fn	16	static
e4_stack.c:24:21:e4_recurse	560	static
e4_stack.c:43:19:e4_stack_init	48	static
e4_stack.c:58:20:e4_stack_exit	16	static
```

출력에서 볼 것:

- 경고 한 줄이 4 KiB 지역 배열을 잡았다. 2048은 `CONFIG_FRAME_WARN`(arm64 64비트 기본값)에서 온다. 커널 메인라인에 이 경고가 있는 패치는 리뷰에서 거의 확실히 거절된다. 고치는 법: `kmalloc`으로 힙에 잡거나(§6), 장치 구조체(`struct e4_priv`) 안에 버퍼를 둔다.
- `.su` 파일: `e4_recurse`가 **560 B** — 예제 7에서 런타임으로 잰 프레임 크기(2240 B / 4)와 정확히 같다. `static`은 "프레임 크기가 컴파일 때 고정"이라는 뜻이다 (`alloca`·VLA가 있으면 `dynamic`).
- 처음 시도에서 `len` 대신 상수 8을 넘겼더니 gcc가 배열을 통째로 최적화해 없애 버려 경고가 안 났다 (`.su`에 0). 정적 분석은 **최적화 후 코드** 기준이다.
- 재미있는 점: 바로 이 예제 자체가 "경고 0개" 규칙을 일부러 어긴 반례다. 실제 드라이버 빌드에서는 `make W=1`을 켜고 경고를 0으로 유지한다.

### 4.7 재귀는 사실상 금지 — 대신 무엇을 쓰나

재귀 자체가 문법적으로 금지된 것은 아니다. 커널에도 깊이가 증명된 재귀(예: 디렉터리 트리 depth 제한이 있는 경로)는 있다. 하지만 **입력에 따라 깊이가 정해지는 재귀**(DT 노드 트리, 유저가 준 중첩 데이터, 링크드 리스트 길이)는 16 KiB 예산에서 언젠가 터진다. 예제 8에서 겨우 560 B 프레임 27개로 죽었다.

대안: (1) 명시적 스택/큐를 힙에 두는 반복문, (2) 깊이 상한을 두고 넘으면 `-ELOOP`/`-E2BIG` 반환, (3) 트리 순회는 커널이 제공하는 반복자(`for_each_child_of_node`, `list_for_each_entry`, `rb_first`/`rb_next`) 사용. MISRA C의 "재귀 금지" 규칙을 지켜 온 Don에게는 익숙한 규칙이다.

---

## 5. atomic context에서는 잠들 수 없다

### 5.1 직관

펌웨어 규칙 "ISR 안에서 `vTaskDelay()`나 세마포어 대기를 하지 마라"의 커널판이다. 다만 커널에는 ISR 말고도 **잠들면 안 되는 곳**이 더 많다: 스핀락을 잡고 있을 때, 선점을 끈 구간, softirq·tasklet·타이머 콜백. 이런 곳을 통틀어 **atomic context**라고 부른다. 반대로 syscall 핸들러·`probe()`·kthread·workqueue는 **process context**이고 잠들 수 있다.

"잠든다(sleep)"는 정확히 말해 **`schedule()`을 불러 CPU를 다른 태스크에 넘기는 것**이다. `msleep`, `mutex_lock`(경합 시), `wait_event`, `kmalloc(GFP_KERNEL)`(메모리 회수가 필요할 때), `copy_from_user`(page fault 처리 시), I2C/SPI 동기 전송이 전부 잠들 수 있다.

왜 atomic에서 잠들면 안 되나:

- 스핀락을 잡은 채 잠들면, 같은 CPU에서 다음에 도는 태스크가 그 락을 잡으려 할 때 **영원히 스핀**한다 (락 주인은 잠들어 있고, 이 CPU는 선점이 꺼져 있어 주인을 깨워 돌릴 수 없다).
- 인터럽트 핸들러는 "태스크"가 아니라서 재울 대상(`task_struct`)이 없다. 끼어든 태스크가 대신 잠들어 버린다.

```svg
<svg viewBox="0 0 680 430" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="10" width="320" height="300" rx="8" fill="#3f9a6b" fill-opacity="0.06" stroke="#3f9a6b"/>
  <text x="170" y="34" text-anchor="middle" font-size="13" font-weight="bold">잠들어도 되는 곳 (process context)</text>
  <rect x="350" y="10" width="320" height="300" rx="8" fill="#d0564a" fill-opacity="0.06" stroke="#d0564a"/>
  <text x="510" y="34" text-anchor="middle" font-size="13" font-weight="bold">잠들면 안 되는 곳 (atomic context)</text>
  <rect x="25" y="48" width="290" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="170" y="67" text-anchor="middle" font-size="12">open / read / write / ioctl 핸들러</text>
  <rect x="25" y="82" width="290" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="170" y="101" text-anchor="middle" font-size="12">probe() · remove() · module_init</text>
  <rect x="25" y="116" width="290" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="170" y="135" text-anchor="middle" font-size="12">kthread · workqueue 작업</text>
  <rect x="25" y="150" width="290" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="170" y="169" text-anchor="middle" font-size="12">threaded IRQ 핸들러 (irq thread)</text>
  <text x="170" y="204" text-anchor="middle" font-size="12" font-weight="bold">쓸 수 있는 것</text>
  <text x="170" y="226" text-anchor="middle" font-size="12">mutex_lock · msleep · wait_event</text>
  <text x="170" y="246" text-anchor="middle" font-size="12">kmalloc(GFP_KERNEL) · vmalloc</text>
  <text x="170" y="266" text-anchor="middle" font-size="12">copy_from_user · i2c/spi 전송</text>
  <text x="170" y="286" text-anchor="middle" font-size="12">+ atomic 쪽 API도 전부</text>
  <rect x="365" y="48" width="290" height="28" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="510" y="67" text-anchor="middle" font-size="12">hardirq 핸들러 (top half)</text>
  <rect x="365" y="82" width="290" height="28" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="510" y="101" text-anchor="middle" font-size="12">softirq · tasklet · timer_list 콜백</text>
  <rect x="365" y="116" width="290" height="28" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="510" y="135" text-anchor="middle" font-size="12">spin_lock() ~ spin_unlock() 사이</text>
  <rect x="365" y="150" width="290" height="28" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="510" y="169" text-anchor="middle" font-size="12">preempt_disable · local_irq_disable</text>
  <text x="510" y="204" text-anchor="middle" font-size="12" font-weight="bold">쓸 수 있는 것</text>
  <text x="510" y="226" text-anchor="middle" font-size="12">spin_lock · atomic_t · READ_ONCE</text>
  <text x="510" y="246" text-anchor="middle" font-size="12">kmalloc(GFP_ATOMIC) · udelay</text>
  <text x="510" y="266" text-anchor="middle" font-size="12">wake_up · complete · queue_work</text>
  <text x="510" y="286" text-anchor="middle" font-size="12">(mutex · msleep · GFP_KERNEL 금지)</text>
  <line x1="315" y1="120" x2="363" y2="120" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="363" y1="140" x2="317" y2="140" stroke="currentColor" marker-end="url(#ah3)"/>
  <rect x="10" y="326" width="660" height="92" rx="6" fill="#4a7bd0" fill-opacity="0.08" stroke="#4a7bd0"/>
  <text x="340" y="348" text-anchor="middle" font-size="12">판정 기준: preempt_count ≠ 0 이거나 IRQ가 꺼져 있으면 atomic</text>
  <text x="340" y="370" text-anchor="middle" font-size="12">preempt_count = [preempt 8bit] [softirq 8bit] [hardirq 4bit] [NMI 4bit]</text>
  <text x="340" y="392" text-anchor="middle" font-size="12">atomic 쪽에서 오래 걸리는 일은 queue_work()로 넘겨 process context에서 한다</text>
</svg>
```

그림 3 — 커널 실행 문맥 지도. 같은 드라이버 함수라도 `spin_lock()`을 잡는 순간 오른쪽 세계로 넘어가고(→), 오른쪽에서 잠들어야 하는 일은 workqueue로 왼쪽에 넘긴다(←). 경계 판정은 `preempt_count`(`include/linux/preempt.h`) 하나로 한다.

### 5.2 예제 10 — 스핀락 안에서 `msleep`/`GFP_KERNEL` (랩 커널, 디버그 옵션 없음)

무엇을 확인하나: 스핀락을 잡고 (1) `GFP_ATOMIC` 할당, (2) `GFP_KERNEL` 할당, (3) `msleep`을 하면 디버그 옵션이 꺼진 커널은 각각 어떻게 반응하는지.

```c
// SPDX-License-Identifier: GPL-2.0
/* e4_atomic: atomic context에서 잠들면 무슨 일이 생기나 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/slab.h>

static int mode;	/* 1: msleep under lock, 2: GFP_KERNEL under lock, 3: GFP_ATOMIC */
module_param(mode, int, 0444);
static DEFINE_SPINLOCK(e4_lock);

static int __init e4_atomic_init(void)
{
	void *p = NULL;

	spin_lock(&e4_lock);
	pr_info("mode=%d in_atomic=%d preempt_count=0x%x\n",
		mode, in_atomic(), preempt_count());
	switch (mode) {
	case 1:
		msleep(10);			/* 잠든다 → 금지 */
		break;
	case 2:
		p = kmalloc(64, GFP_KERNEL);	/* 잠들 수 있다 → 금지 */
		break;
	case 3:
		p = kmalloc(64, GFP_ATOMIC);	/* 잠들지 않는다 → OK */
		break;
	}
	spin_unlock(&e4_lock);
	pr_info("mode=%d done p=%s\n", mode, p ? "ok" : "NULL");
	kfree(p);
	return 0;
}

static void __exit e4_atomic_exit(void) { }
module_init(e4_atomic_init);
module_exit(e4_atomic_exit);
MODULE_DESCRIPTION("E4: sleeping in atomic context");
MODULE_LICENSE("GPL");
```

```sh
# 게스트 안 (t.sh)
for m in 3 2 1; do
  echo "== mode=$m"
  insmod /e4_atomic.ko mode=$m
  rmmod e4_atomic
done
dmesg | grep -v "^\[ *0\.\|^\[ *[1-4]\." | grep -A40 "e4_atomic"
```

```text
lab initramfs: 6.18.55 up 6.04s
== mode=3
== mode=2
== mode=1
[    7.766298] BUG: scheduling while atomic: insmod/7
** replaying previous printk message **
[    7.766298] BUG: scheduling while atomic: insmod/74/0x00000002
[    7.276373] e4_atomic: loading out-of-tree module taints kernel.
[    7.310545] e4_atomic: mode=3 in_atomic=1 preempt_count=0x1
[    7.310665] e4_atomic: mode=3 done p=ok
[    7.428293] e4_atomic: mode=2 in_atomic=1 preempt_count=0x1
[    7.428409] e4_atomic: mode=2 done p=ok
[    7.766131] e4_atomic: mode=1 in_atomic=1 preempt_count=0x1
[    7.766298] BUG: scheduling while atomic: insmod/74/0x00000002
[    7.766363] Modules linked in: e4_atomic(O+) [last unloaded: e4_atomic(O)]
[    7.768458] CPU: 1 UID: 0 PID: 74 Comm: insmod Tainted: G           O        6.18.55 #1 PREEMPT 
[    7.769495] Call trace:
...
[    7.773516]  __schedule_bug+0x50/0x68
[    7.773556]  __schedule+0x75c/0x968
[    7.773615]  schedule+0x34/0xd4
[    7.773662]  schedule_timeout+0x80/0x104
[    7.773687]  msleep+0x28/0x40
[    7.773719]  e4_atomic_init+0x68/0xfa0 [e4_atomic]
[    7.774158]  do_one_initcall+0x60/0x1e0
...
[    7.782367] e4_atomic: mode=1 done p=NULL
...
[    7.782584] initcall e4_atomic_init+0x0/0xfa0 [e4_atomic] returned with preemption imbalance 
[    7.782987] WARNING: CPU: 1 PID: 74 at init/main.c:1256 do_one_initcall+0x158/0x1e0
...
```

(`== mode=1` 아래 세 줄은 콘솔 출력이다: `BUG:`는 `quiet`여도 KERN_ERR라 콘솔에 바로 찍혔고, 다른 출력과 섞여 끊긴 줄을 printk가 `replaying previous printk message`로 다시 찍었다. 그 아래가 `dmesg` 사본이다.)

출력에서 볼 것:

- `spin_lock()` 하나로 `preempt_count`가 0x1, `in_atomic()`이 1이 됐다. 단일 CPU 커널이 아니어도, 스핀락은 **잡은 CPU의 선점을 끈다**.
- **mode=2 (`GFP_KERNEL` under lock): 아무 일도 없었다.** 메모리가 넉넉해서 slab 빠른 경로가 잠들지 않고 바로 돌려줬기 때문이다. 메모리가 부족한 현장 장비에서만 가끔 잠들고 그때 데드락·크래시가 난다 — **가장 위험한 종류의 잠복 버그**다.
- mode=1 (`msleep`): 실제로 `schedule()`까지 갔고, 스케줄러의 `schedule_debug()`가 `in_atomic_preempt_off()`를 보고 `__schedule_bug()`를 불렀다. 이 검사는 디버그 옵션 없이도 **항상** 켜져 있다. `0x00000002`는 그 순간의 `preempt_count`(스핀락 1 + `schedule()` 진입 시 1).
- 그 뒤 `preempt_count_set(PREEMPT_DISABLED)`로 카운트를 강제로 바로잡기 때문에, 내 `spin_unlock()`이 카운트를 하나 더 내려 **preemption imbalance** WARNING이 이어진다. 버그 하나가 엉뚱한 곳에 두 번째 경고를 남기는 전형적인 모습이다.

### 5.3 `might_sleep()` — 잠들 수도 있는 함수가 미리 외치게 하기

mode=2 같은 잠복 버그를 잡으려면 "실제로 잠들었을 때"가 아니라 "**잠들 수 있는 함수를 atomic에서 불렀을 때**" 경고해야 한다. 그게 `might_sleep()` 주석이다. `include/linux/kernel.h`(랩 소스):

```c
#ifdef CONFIG_DEBUG_ATOMIC_SLEEP
extern void __might_resched(const char *file, int line, unsigned int offsets);
extern void __might_sleep(const char *file, int line);
...
# define might_sleep() \
	do { __might_sleep(__FILE__, __LINE__); might_resched(); } while (0)
...
#else
...
# define might_sleep() do { might_resched(); } while (0)
```

`kmalloc(GFP_KERNEL)`·`mutex_lock`·`msleep`·`copy_from_user` 같은 함수 입구에는 `might_sleep()`(또는 `might_alloc(gfp)`)이 박혀 있다. `CONFIG_DEBUG_ATOMIC_SLEEP=y`면 `__might_resched()`(`kernel/sched/core.c`)가 `preempt_count`·`irqs_disabled()`를 검사해서 문제가 있으면 다음을 찍는다:

```c
	pr_err("BUG: sleeping function called from invalid context at %s:%d\n",
	       file, line);
	pr_err("in_atomic(): %d, irqs_disabled(): %d, non_block: %d, pid: %d, name: %s\n",
	       in_atomic(), irqs_disabled(), current->non_block_count,
	       current->pid, current->comm);
	pr_err("preempt_count: %x, expected: %x\n", preempt_count(),
	       offsets & MIGHT_RESCHED_PREEMPT_MASK);
```

옵션이 꺼져 있으면 `might_sleep()`은 `might_resched()`(PREEMPT_VOLUNTARY일 때의 선점 지점)만 남고 **검사는 사라진다.** 랩 커널(arm64 defconfig)이 바로 그 상태다.

### 5.4 예제 11 — 같은 모듈을 `DEBUG_ATOMIC_SLEEP` 커널에서

무엇을 확인하나: §1.1에서 만든 디버그 커널 복사본(`6.18.55-e4dbg`)에서는 mode=2의 잠복 버그가 드러나는지. 모듈은 그 커널 트리로 다시 빌드한다(`vermagic`이 다르므로).

```sh
cp /var/tmp/E4/kbuild/vmlinux.symvers /var/tmp/E4/kbuild/Module.symvers   # 모듈 링크용 심볼 표
make -C /var/tmp/E4/kbuild M=/var/tmp/E4/dbg_e4_atomic modules
modinfo /var/tmp/E4/dbg_e4_atomic/e4_atomic.ko | grep vermagic
# 게스트: KIMG=/var/tmp/E4/kbuild/arch/arm64/boot/Image, 모드 사이에 sleep 2 (경고는 1초에 한 번으로 rate-limit)
```

```text
vermagic:       6.18.55-e4dbg SMP preempt mod_unload aarch64
```

```text
lab initramfs: 6.18.55-e4dbg up 1.37s
6.18.55-e4dbg
== mode=3
== mode=2
[    3.626736] BUG: sleeping function called from invalid context at include/linux/sched/mm.h:321
[    3.626769] in_atomic(): 1, irqs_disabled(): 0, non_block: 0, pid: 74, name: insmod
[    3.626786] preempt_count: 1, expected: 0
[    3.626797] RCU nest depth: 0, expected: 0
== mode=1
[    5.690642] BUG: scheduling while atomic: insmod/77/0x00000002
...
[    3.626617] e4_atomic: mode=2 in_atomic=1 preempt_count=0x1
[    3.626736] BUG: sleeping function called from invalid context at include/linux/sched/mm.h:321
[    3.626769] in_atomic(): 1, irqs_disabled(): 0, non_block: 0, pid: 74, name: insmod
[    3.626786] preempt_count: 1, expected: 0
[    3.626797] RCU nest depth: 0, expected: 0
[    3.627284] CPU: 1 UID: 0 PID: 74 Comm: insmod Tainted: G           O        6.18.55-e4dbg #1 PREEMPT 
[    3.627579] Call trace:
...
[    3.628413]  __might_resched+0x150/0x164
[    3.628428]  __might_sleep+0x44/0x80
[    3.628442]  __kmalloc_cache_noprof+0x184/0x4d0
[    3.628458]  e4_atomic_init+0x84/0xfa0 [e4_atomic]
[    3.628798]  do_one_initcall+0x60/0x1e0
...
[    3.630472] e4_atomic: mode=2 done p=ok
[    5.690514] e4_atomic: mode=1 in_atomic=1 preempt_count=0x1
[    5.690642] BUG: scheduling while atomic: insmod/77/0x00000002
...
[    5.691368]  __schedule_bug+0x50/0x68
[    5.691384]  __schedule+0x7a8/0x9c0
[    5.691401]  schedule+0x34/0xdc
[    5.691414]  schedule_timeout+0x80/0x110
[    5.691426]  msleep+0x38/0x50
[    5.691439]  e4_atomic_init+0x68/0xfa0 [e4_atomic]
...
```

출력에서 볼 것:

- **mode=2가 이제 잡힌다.** 할당은 여전히 성공했지만(`p=ok`), `kmalloc` 입구의 `might_alloc(GFP_KERNEL)` → `might_sleep_if()` → `__might_resched()`가 "이 문맥에서 잠들 **수도** 있는 함수를 불렀다"고 외쳤다. 위치 `include/linux/sched/mm.h:321`이 바로 `might_alloc()`의 `might_sleep_if(gfpflags_allow_blocking(gfp_mask))` 줄이다.
- `preempt_count: 1, expected: 0` — 스핀락 하나를 잡고 있다는 뜻. `irqs_disabled(): 0`이므로 `spin_lock_irqsave`가 아닌 `spin_lock`이었다는 것까지 알 수 있다.
- mode=1(`msleep`)은 디버그 커널에서도 "sleeping function called…"가 아니라 "scheduling while atomic"이었다. `msleep()` 경로 앞부분에는 `might_sleep()` 주석이 없고 실제 `schedule()`에서야 걸린 것이다. 두 메시지의 차이: **"scheduling while atomic" = 실제로 잠들었다(사고 발생)**, **"sleeping function called from invalid context" = 잠들 수 있는 함수를 불렀다(사고 예고)**.
- 결론: 드라이버 개발·CI 커널에는 `CONFIG_DEBUG_ATOMIC_SLEEP=y`(+ lockdep `CONFIG_PROVE_LOCKING`)를 켠다. 양산 커널은 성능 때문에 끄는 경우가 많다.

---

## 6. 메모리 할당은 실패할 수 있고, "기다려도 되나"를 알려야 한다

### 6.1 직관

유저 공간의 `malloc`은 Linux의 overcommit 때문에 거의 실패하지 않는다(대신 나중에 OOM killer가 온다). 커널 할당은 다르다. (1) 커널은 자기 메모리를 스왑할 수 없고, (2) `kmalloc`은 **물리적으로 연속**인 메모리를 줘야 하므로 메모리가 조각나면 큰 크기는 실패하며, (3) 호출자가 atomic context에 있으면 메모리 회수를 기다릴 수도 없다. 그래서 커널 할당 API는 크기와 함께 **GFP 플래그**("Get Free Pages" 플래그)로 "이 할당을 위해 무엇을 해도 되는가"를 받는다.

| 플래그 | 잠들어도 되나 | 쓰는 곳 |
|---|---|---|
| `GFP_KERNEL` | 예 (reclaim, I/O, FS 콜백 가능) | process context 기본값, `probe()`, ioctl |
| `GFP_ATOMIC` | 아니오. 비상 예비분까지 사용 | IRQ 핸들러, 스핀락 안, softirq |
| `GFP_NOWAIT` | 아니오. 비상 예비분은 안 씀 | 실패해도 되는 기회성 할당 |
| `GFP_NOIO` / `GFP_NOFS` | 예, 단 블록 I/O / FS 재진입 금지 | 블록 드라이버·FS 내부 (요즘은 `memalloc_noio_save()` 범위 API 권장) |
| `GFP_DMA` / `GFP_DMA32` | 플래그 조합 | 주소 제한 있는 오래된 DMA 장치 (보통은 DMA API 사용) |
| `__GFP_ZERO` | 수식자 | 0으로 채움 (`kzalloc` = `kmalloc` + 이것) |
| `__GFP_NOWARN` | 수식자 | 실패 경고 출력 안 함 |

더 깊은 내용(slab 캐시, 페이지 할당자, CMA)은 F9에서 다룬다.

### 6.2 예제 12 — 실패 경로와 할당자별 비용 (`e4_alloc`)

무엇을 확인하나: (1) `kmalloc`에는 최대 크기가 있고 그보다 크면 무조건 실패하는지, (2) `init`이 실패를 돌려주면 유저가 무엇을 보는지, (3) `kmalloc`/`kzalloc`/`vmalloc` 비용이 크기에 따라 어떻게 다른지.

```c
// SPDX-License-Identifier: GPL-2.0
/* e4_alloc: 할당은 실패할 수 있다 + kmalloc/kzalloc/vmalloc 비용 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/ktime.h>

static bool fail;		/* true: init이 일부러 -ENOMEM 경로로 */
module_param(fail, bool, 0444);

static u64 bench(int kind, size_t sz)	/* 1000회 평균, 6회 중 최솟값 */
{
	u64 best = U64_MAX, t0;
	void *p;
	int r, i;

	for (r = 0; r < 6; r++) {		/* r=0은 warm-up 겸 */
		t0 = ktime_get_ns();
		for (i = 0; i < 1000; i++) {
			p = kind == 0 ? kmalloc(sz, GFP_KERNEL) :
			    kind == 1 ? kzalloc(sz, GFP_KERNEL) : vmalloc(sz);
			if (!p)
				return 0;
			((volatile char *)p)[0] = 1;
			kind == 2 ? vfree(p) : kfree(p);
		}
		best = min(best, (ktime_get_ns() - t0) / 1000);
	}
	return best;
}

static int __init e4_alloc_init(void)
{
	static const size_t sizes[] = { 64, 1024, 4096, 32768, 131072 };
	void *big = kmalloc(8 << 20, GFP_KERNEL | __GFP_NOWARN);
	void *v = vmalloc(8 << 20);
	int i;

	pr_info("kmalloc(8 MiB) = %s, KMALLOC_MAX_SIZE=%lu\n",
		big ? "ok" : "NULL", KMALLOC_MAX_SIZE);
	pr_info("vmalloc(8 MiB) = %s, is_vmalloc_addr=%d\n",
		v ? "ok" : "NULL", is_vmalloc_addr(v));
	kfree(big);
	vfree(v);
	if (fail) {
		pr_info("simulating allocation failure\n");
		return -ENOMEM;
	}
	for (i = 0; i < ARRAY_SIZE(sizes); i++)
		pr_info("size=%6zu kmalloc=%llu kzalloc=%llu vmalloc=%llu ns\n",
			sizes[i], bench(0, sizes[i]), bench(1, sizes[i]),
			bench(2, sizes[i]));
	return 0;
}

static void __exit e4_alloc_exit(void) { }
module_init(e4_alloc_init);
module_exit(e4_alloc_exit);
MODULE_DESCRIPTION("E4: allocation can fail; allocator costs");
MODULE_LICENSE("GPL");
```

```sh
# 게스트 안 (t.sh)
insmod /e4_alloc.ko fail=1
echo "insmod exit=$?"
insmod /e4_alloc.ko
dmesg | grep e4_alloc:
```

```text
lab initramfs: 6.18.55 up 1.22s
insmod: can't insert '/e4_alloc.ko': Cannot allocate memory
insmod exit=1
[    1.356513] e4_alloc: loading out-of-tree module taints kernel.
[    1.364744] e4_alloc: kmalloc(8 MiB) = NULL, KMALLOC_MAX_SIZE=4194304
[    1.365336] e4_alloc: vmalloc(8 MiB) = ok, is_vmalloc_addr=1
[    1.366965] e4_alloc: simulating allocation failure
[    1.399832] e4_alloc: kmalloc(8 MiB) = NULL, KMALLOC_MAX_SIZE=4194304
[    1.399891] e4_alloc: vmalloc(8 MiB) = ok, is_vmalloc_addr=1
[    1.447669] e4_alloc: size=    64 kmalloc=279 kzalloc=316 vmalloc=6268 ns
[    1.492915] e4_alloc: size=  1024 kmalloc=307 kzalloc=828 vmalloc=4905 ns
[    1.565653] e4_alloc: size=  4096 kmalloc=974 kzalloc=2377 vmalloc=5223 ns
[    1.760353] e4_alloc: size= 32768 kmalloc=2297 kzalloc=16167 vmalloc=10087 ns
[    2.315327] e4_alloc: size=131072 kmalloc=3222 kzalloc=58699 vmalloc=23602 ns
```

출력에서 볼 것:

- **`kmalloc(8 MiB)`는 메모리가 남아도 실패한다.** `KMALLOC_MAX_SIZE` = `1 << (MAX_PAGE_ORDER + PAGE_SHIFT)` = 2^(10+12) = 4 MiB가 상한이다(`include/linux/slab.h`, `CONFIG_ARCH_FORCE_MAX_ORDER=10`). `__GFP_NOWARN`을 빼면 커널이 경고 스택을 찍는다. `vmalloc`은 물리 페이지를 낱장으로 모아 가상으로만 이어 붙이므로 8 MiB도 된다.
- `fail=1`: `init`이 `-ENOMEM`을 돌려주자 `insmod`가 `Cannot allocate memory`로 실패했다(§9).
- 비용(그림 5): 작은 크기에서 `kmalloc`은 수백 ns(slab per-CPU 캐시에서 꺼내기), `vmalloc`은 수 µs(페이지 할당 + 페이지 테이블 매핑 + 해제 시 TLB 처리)로 **10~20배** 비싸다. 8 KiB(`KMALLOC_SHIFT_HIGH` = `PAGE_SHIFT + 1`)를 넘는 `kmalloc`은 slab이 아니라 **페이지 할당자로 직행**해서 µs 단위가 된다. `kzalloc`의 추가 비용은 순수하게 `memset`이라 크기에 비례한다 — 128 KiB에서는 `vmalloc`(이 벤치는 페이지를 0으로 채우지 않음)보다도 비싸다.
- 이 숫자는 **QEMU TCG(명령어 번역 에뮬레이션)** 위의 값이라 실제 Cortex-A 하드웨어보다 수 배 느리고, 특히 `memset` 같은 메모리 집약 코드가 과장된다. 4 KiB `kmalloc`은 같은 모듈의 이전 실행 두 번에서 294 ns, 303 ns였다 — 974 ns는 TCG 잡음이다. **상대적인 순서**(kmalloc < vmalloc, kzalloc ∝ 크기)만 믿는다.

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="22" text-anchor="middle" font-size="13" font-weight="bold">할당+해제 1회 비용 (ns, 로그 축, 6회 중 최솟값) — QEMU TCG 게스트</text>
  <line x1="80" y1="280.0" x2="660" y2="280.0" stroke="#888" stroke-opacity="0.35" stroke-dasharray="3 3"/>
  <text x="74" y="284.0" text-anchor="end" font-size="12">100 ns</text>
  <line x1="80" y1="203.3" x2="660" y2="203.3" stroke="#888" stroke-opacity="0.35" stroke-dasharray="3 3"/>
  <text x="74" y="207.3" text-anchor="end" font-size="12">1 µs</text>
  <line x1="80" y1="126.7" x2="660" y2="126.7" stroke="#888" stroke-opacity="0.35" stroke-dasharray="3 3"/>
  <text x="74" y="130.7" text-anchor="end" font-size="12">10 µs</text>
  <line x1="80" y1="50.0" x2="660" y2="50.0" stroke="#888" stroke-opacity="0.35" stroke-dasharray="3 3"/>
  <text x="74" y="54.0" text-anchor="end" font-size="12">100 µs</text>
  <line x1="80" y1="280" x2="660" y2="280" stroke="currentColor"/>
  <line x1="80" y1="40" x2="80" y2="280" stroke="currentColor"/>
  <rect x="95.0" y="245.8" width="26" height="34.2" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
  <text x="108.0" y="241.8" text-anchor="middle" font-size="12">279</text>
  <rect x="125.0" y="241.7" width="26" height="38.3" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
  <text x="138.0" y="237.7" text-anchor="middle" font-size="12">316</text>
  <rect x="155.0" y="142.2" width="26" height="137.8" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
  <text x="168.0" y="138.2" text-anchor="middle" font-size="12">6.3k</text>
  <text x="138.0" y="298" text-anchor="middle" font-size="12">64 B</text>
  <rect x="211.0" y="242.7" width="26" height="37.3" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
  <text x="224.0" y="238.7" text-anchor="middle" font-size="12">307</text>
  <rect x="241.0" y="209.6" width="26" height="70.4" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
  <text x="254.0" y="205.6" text-anchor="middle" font-size="12">828</text>
  <rect x="271.0" y="150.4" width="26" height="129.6" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
  <text x="284.0" y="146.4" text-anchor="middle" font-size="12">4.9k</text>
  <text x="254.0" y="298" text-anchor="middle" font-size="12">1 KiB</text>
  <rect x="327.0" y="204.2" width="26" height="75.8" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
  <text x="340.0" y="200.2" text-anchor="middle" font-size="12">974</text>
  <rect x="357.0" y="174.5" width="26" height="105.5" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
  <text x="370.0" y="170.5" text-anchor="middle" font-size="12">2.4k</text>
  <rect x="387.0" y="148.3" width="26" height="131.7" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
  <text x="400.0" y="144.3" text-anchor="middle" font-size="12">5.2k</text>
  <text x="370.0" y="298" text-anchor="middle" font-size="12">4 KiB</text>
  <rect x="443.0" y="175.6" width="26" height="104.4" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
  <text x="456.0" y="171.6" text-anchor="middle" font-size="12">2.3k</text>
  <rect x="473.0" y="110.7" width="26" height="169.3" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
  <text x="486.0" y="106.7" text-anchor="middle" font-size="12">16k</text>
  <rect x="503.0" y="126.4" width="26" height="153.6" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
  <text x="516.0" y="122.4" text-anchor="middle" font-size="12">10k</text>
  <text x="486.0" y="298" text-anchor="middle" font-size="12">32 KiB</text>
  <rect x="559.0" y="164.4" width="26" height="115.6" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
  <text x="572.0" y="160.4" text-anchor="middle" font-size="12">3.2k</text>
  <rect x="589.0" y="67.7" width="26" height="212.3" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
  <text x="602.0" y="63.7" text-anchor="middle" font-size="12">59k</text>
  <rect x="619.0" y="98.1" width="26" height="181.9" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
  <text x="632.0" y="94.1" text-anchor="middle" font-size="12">24k</text>
  <text x="602.0" y="298" text-anchor="middle" font-size="12">128 KiB</text>
  <rect x="190" y="308" width="12" height="12" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
  <text x="208" y="318" font-size="12">kmalloc</text>
  <rect x="300" y="308" width="12" height="12" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
  <text x="318" y="318" font-size="12">kzalloc</text>
  <rect x="410" y="308" width="12" height="12" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
  <text x="428" y="318" font-size="12">vmalloc</text>
  <text x="340" y="346" text-anchor="middle" font-size="12">8 KiB 초과 kmalloc은 slab이 아니라 페이지 할당자로 간다 · kzalloc의 추가 비용 = memset</text>
</svg>
```

그림 5 — 예제 12에서 직접 잰 할당+해제 비용 (로그 축). 작은 객체는 `kmalloc`, 크고 물리 연속이 필요 없으면 `vmalloc`(또는 `kvmalloc`: 먼저 `kmalloc`을 시도하고 실패하면 `vmalloc`), 0 초기화가 꼭 필요할 때만 `kzalloc`. DMA 버퍼는 셋 다 아니고 DMA API(H8)다.

### 6.3 할당 규칙 요약

- **반환값은 항상 검사한다.** `kmalloc`이 `NULL`을 돌려줄 수 있다는 전제에서 코드를 쓴다. `checkpatch`는 `kmalloc` 실패 후의 `pr_err`("out of memory" 메시지)를 오히려 지적한다 — 할당자가 이미 경고를 찍기 때문이다.
- 문맥에 맞는 GFP: process context면 `GFP_KERNEL`, atomic이면 `GFP_ATOMIC`. atomic에서 큰 할당이 필요해 보이면 **설계를 바꾼다**: `probe()`에서 미리 할당하거나, 메모리 풀(`mempool_create`)을 쓰거나, 일을 workqueue로 넘긴다.
- 드라이버는 가능하면 `devm_kzalloc(&pdev->dev, ...)` — `probe()` 실패나 `remove()` 때 자동 해제(G6).
- `vmalloc` 메모리는 물리적으로 연속이 아니므로 **DMA에 그대로 쓰지 못한다**(F9, H8).

---

## 7. 유저 포인터는 절대 직접 역참조하지 않는다

### 7.1 직관

`write(fd, buf, len)`의 `buf`는 **유저 프로세스의 가상 주소**다. 커널 입장에서 그 값은 "남이 적어 준 주소 쪽지"일 뿐이다: 매핑이 안 돼 있을 수도(NULL+16), 스왑아웃돼 있을 수도, 아예 **커널 주소**일 수도 있다(악의적인 유저가 커널 비밀을 읽거나 덮어쓰게 하려고). Don의 SSD 펌웨어가 호스트 NVMe 명령의 PRP 주소를 그대로 DMA에 쓰지 않고 범위·정렬을 먼저 검사했던 것과 같은 원리다.

그래서 유저 메모리는 반드시 `copy_from_user`/`copy_to_user`/`get_user`/`put_user`로만 접근한다. 이 함수들은 (1) `access_ok()`로 주소가 유저 영역인지 검사하고, (2) 접근 중 page fault가 나면 **exception table**(`__ex_table`)에 등록된 복구 지점으로 점프해 "복사 못 한 바이트 수"를 돌려준다. Oops 대신 `-EFAULT`가 되는 비결이다. 자세한 경로는 E6.

### 7.2 예제 13 — misc device로 `copy_from_user` vs 직접 역참조 (`e4_uptr`)

무엇을 확인하나: 같은 `write` 핸들러에서 `copy_from_user`를 쓰면 잘못된 포인터가 `-EFAULT`로 끝나는지, 유저 포인터를 그냥 역참조하면 어떻게 되는지.

```c
// SPDX-License-Identifier: GPL-2.0
/* e4_uptr: 유저 포인터를 직접 읽으면? copy_from_user 와 비교 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

static bool direct;		/* true: 유저 포인터를 그냥 역참조 (버그) */
module_param(direct, bool, 0644);

static ssize_t e4_write(struct file *f, const char __user *ubuf,
			size_t len, loff_t *pos)
{
	char kbuf[16] = "";
	size_t n = min(len, sizeof(kbuf) - 1);

	if (direct) {
		/* 컴파일러는 막지 않는다. sparse는 경고한다. */
		kbuf[0] = *ubuf;
		pr_info("direct read: '%c'\n", kbuf[0]);
		return len;
	}
	if (copy_from_user(kbuf, ubuf, n)) {
		pr_info("copy_from_user(%px) failed -> -EFAULT\n", ubuf);
		return -EFAULT;
	}
	pr_info("copied %zu bytes: \"%s\"\n", n, kbuf);
	return len;
}

static const struct file_operations e4_fops = {
	.owner = THIS_MODULE,
	.write = e4_write,
};
static struct miscdevice e4_misc = {
	.minor = MISC_DYNAMIC_MINOR, .name = "e4_uptr", .fops = &e4_fops,
};
module_misc_device(e4_misc);
MODULE_DESCRIPTION("E4: never dereference user pointers");
MODULE_LICENSE("GPL");
```

유저 쪽 테스트 프로그램 (VM에서 `gcc -static`으로 빌드해 initramfs에 넣었다):

```c
/* uwrite: e4_uptr에 정상/잘못된 포인터를 write()로 넘긴다 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void try(int fd, const char *what, const void *p, size_t n)
{
	ssize_t r = write(fd, p, n);

	printf("%-28s ret=%zd errno=%s\n", what, r, r < 0 ? strerror(errno) : "-");
}

int main(void)
{
	int fd = open("/dev/e4_uptr", O_WRONLY);

	if (fd < 0) {
		perror("open");
		return 1;
	}
	try(fd, "valid user buffer", "imu0:ok", 7);
	try(fd, "NULL+16 (unmapped)", (const void *)16, 4);
	try(fd, "kernel address", (const void *)0xffff800080000000UL, 4);
	close(fd);
	return 0;
}
```

```sh
gcc -static -std=gnu11 -Wall -Wextra -O2 uwrite.c -o /var/tmp/E4/uwrite
# 게스트 안 (t.sh)
insmod /e4_uptr.ko
grep -o "Features.*" /proc/cpuinfo | head -1
/uwrite
dmesg | grep e4_uptr:
echo "== direct=1"
echo 1 > /sys/module/e4_uptr/parameters/direct
echo hello > /dev/e4_uptr
echo "echo exit=$?"
dmesg | grep "e4_uptr: direct"
```

**첫 번째 실행: `-cpu cortex-a57` (랩 기본, ARMv8.0)**

```text
lab initramfs: 6.18.55 up 5.83s
Features	: fp asimd evtstrm aes pmull sha1 sha2 crc32 cpuid
valid user buffer            ret=7 errno=-
NULL+16 (unmapped)           ret=-1 errno=Bad address
kernel address               ret=-1 errno=Bad address
[    6.933704] e4_uptr: loading out-of-tree module taints kernel.
[    7.086199] e4_uptr: copied 7 bytes: "imu0:ok"
[    7.094506] e4_uptr: copy_from_user(0000000000000010) failed -> -EFAULT
== direct=1
echo exit=0
[    7.205280] e4_uptr: direct read: 'h'
```

출력에서 볼 것:

- `copy_from_user` 경로: 정상 버퍼는 복사, NULL+16은 `-EFAULT`(유저에게 "Bad address"). 커널은 멀쩡하다.
- **커널 주소를 넘긴 경우에는 내 드라이버 로그가 아예 없다.** `vfs_write()`가 드라이버를 부르기 전에 `access_ok(buf, count)`로 먼저 걸러 `-EFAULT`를 돌려줬다. 그래도 드라이버는 이 검사에 기대지 말고 늘 `copy_from_user`를 쓴다 (ioctl 인자 안의 포인터처럼 VFS가 모르는 포인터가 많다).
- **`direct=1`인데도 "잘 동작"했다.** Cortex-A57(ARMv8.0)에는 PAN이 없어서, 커널 모드(EL1)에서 유저 페이지를 그냥 읽을 수 있다. 이 버그는 개발 보드에서는 영원히 숨어 있다가, ARMv8.1 이상 칩이나 x86(SMAP)에서 터진다. 다만 유저가 매핑 안 된 주소를 넘기면 A57에서도 Oops가 난다 — 직접 역참조한 명령에는 exception table 복구 지점이 없기 때문이다(§18 문제 4).

**두 번째 실행: `-cpu max` (PAN 지원) — 같은 커널, 같은 모듈**

```text
== direct=1
[    7.304864] Unable to handle kernel access to user memory outside uaccess routines at virtual address 000000001e4d5aa0
[    7.304957] Mem abort info:
[    7.304971]   ESR = 0x000000009600000f
...
[    7.305101]   FSC = 0x0f: level 3 permission fault
...
[    7.307690] user pgtable: 4k pages, 52-bit VAs, pgdp=0000000043577300
[    7.307730] [000000001e4d5aa0] pgd=080000004357c403, p4d=0800000043589403, pud=08000000435a1403, pmd=0800000043590403, pte=00e8000042504c43
[    7.319332] Internal error: Oops: 000000009600000f [#1]  SMP
[    7.330217] Modules linked in: e4_uptr(O)
[    7.331326] CPU: 0 UID: 0 PID: 69 Comm: sh Tainted: G           O        6.18.55 #1 PREEMPT 
...
[    7.332459] pstate: 81402005 (Nzcv daif +PAN -UAO -TCO +DIT -SSBS BTYPE=--)
[    7.332772] pc : e4_write+0x124/0x144 [e4_uptr]
[    7.338203] lr : vfs_write+0xd0/0x380
[    7.338610] sp : ffff800080493cf0
...
[    7.348738] x20: 000000001e4d5aa0 x19: 0000000000000006 x18: 0000000000000000
...
[    7.353266] Call trace:
[    7.353516]  e4_write+0x124/0x144 [e4_uptr] (P)
[    7.353745]  vfs_write+0xd0/0x380
[    7.353837]  ksys_write+0x64/0x100
...
[    7.359164] Code: 17ffffe4 f9400021 36d7fa41 17ffffcf (39400281) 
[    7.360912] ---[ end trace 0000000000000000 ]---
Segmentation fault
```

출력에서 볼 것:

- **PAN (Privileged Access Never, ARMv8.1)**: `pstate`의 `+PAN`. PAN 비트가 켜져 있으면 EL1이 유저 접근 가능(EL0) 페이지를 읽기만 해도 permission fault가 난다. `copy_from_user`는 복사하는 동안만 PAN을 잠깐 끄거나(`uaccess_enable_privileged`), 유저 권한으로 접근하는 `LDTR`/`STTR` 명령을 쓴다. 그래서 메시지가 "**outside uaccess routines**"다 — uaccess 함수 밖에서 유저 메모리를 만졌다는 뜻. x86의 같은 기능이 **SMAP**이다.
- `pte=...`가 있으니 페이지는 **멀쩡히 매핑돼 있었다.** 매핑 문제가 아니라 권한 문제(`FSC = 0x0f: level 3 permission fault`)다.
- 유저 쪽은 `echo`를 실행하던 셸이 `Segmentation fault`로 죽었다. 커널 Oops가 **process context에서** 나면 커널은 그 태스크를 죽이고(`make_task_dead`) 시스템은 계속 돈다 — 하지만 그 태스크가 잡고 있던 락은 영원히 안 풀린다. 이후 시스템 상태는 믿을 수 없다(`Tainted`에 `D`가 붙는다).
- 랩 커널은 `CONFIG_ARM64_SW_TTBR0_PAN`이 꺼져 있다. 이 옵션은 PAN이 없는 ARMv8.0에서 TTBR0(유저 페이지 테이블)를 커널 실행 중 비워 두는 소프트웨어 PAN 흉내다. 켰다면 A57에서도 직접 역참조가 fault가 났을 것이다.

### 7.3 예제 14 — sparse는 빌드 때 잡는다 (`make C=1`)

무엇을 확인하나: `__user` 주석이 붙은 포인터를 역참조하는 코드를 정적 분석기 sparse가 잡는지.

```sh
cd /var/tmp/E4/e4_uptr && touch e4_uptr.c
make -C /var/lab/out/linux-build M=$PWD C=1 modules
```

```text
  CC [M]  e4_uptr.o
  CHECK   e4_uptr.c
e4_uptr.c:20:28: warning: dereference of noderef expression
  MODPOST Module.symvers
  LD [M]  e4_uptr.ko
```

출력에서 볼 것: gcc는 아무 말도 안 했지만 sparse는 20번째 줄(`kbuf[0] = *ubuf;`)을 정확히 짚었다. `__user`는 gcc에게는 빈 매크로이고, sparse에게만 `__attribute__((noderef, address_space(__user)))`로 보인다(`include/linux/compiler_types.h`). 드라이버를 쓰면 **`make C=1`을 습관으로** 한다(E5).

---

## 8. 커널 안에는 메모리 보호가 없다

### 8.1 직관

유저 공간에서 버퍼 overflow는 내 프로세스만 망친다. 커널에서는 **모든 커널 코드가 같은 주소 공간을 쓴다.** 내 드라이버의 `kmalloc(32)` 버퍼 바로 옆 32바이트는 USB 스택의 객체일 수도, 다른 프로세스의 파일 구조체일 수도 있다. 넘쳐 쓰면 **fault도 경고도 없이** 남의 데이터가 바뀐다. 증상은 한참 뒤 전혀 다른 서브시스템에서 나타난다. 펌웨어에서 전역 배열 overflow가 바로 다음 전역 변수를 덮던 것과 같지만, 이웃이 수천 배 많다.

### 8.2 예제 15 — 8바이트 overflow가 이웃 객체를 바꾼다 (`e4_corrupt`)

무엇을 확인하나: `kmalloc-32` 캐시에서 메모리상 연속한 두 객체를 찾아, 앞 객체를 8바이트 넘쳐 쓰면 뒤 객체가 어떻게 되는지.

```c
// SPDX-License-Identifier: GPL-2.0
/* e4_corrupt: 커널 안에는 보호 경계가 없다 — 8바이트 overflow 실험 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>

#define N 64
struct e4_rec { u64 id; char name[24]; };	/* 32 B → kmalloc-32 */

static int __init e4_corrupt_init(void)
{
	struct e4_rec *r[N] = { };
	int i, j, a = -1, b = -1;

	for (i = 0; i < N; i++) {
		r[i] = kmalloc(sizeof(*r[i]), GFP_KERNEL);
		if (!r[i])
			goto out;
		r[i]->id = 0x1111111100000000ULL + i;
		strscpy(r[i]->name, "sensor", sizeof(r[i]->name));
	}
	for (i = 0; i < N && a < 0; i++)	/* 메모리상 바로 뒤 이웃 찾기 */
		for (j = 0; j < N; j++)
			if ((char *)r[j] == (char *)r[i] + 32) {
				a = i; b = j;
				break;
			}
	if (a < 0) {				/* slab_debug: 사이에 redzone */
		pr_info("no adjacent object (redzone?) - overflow r[0]\n");
		memset(r[0]->name, 'X', 24 + 8);
		goto out;
	}
	pr_info("r[%d]=%px  r[%d]=%px (next object)\n", a, r[a], b, r[b]);
	pr_info("before: r[%d]->id=%#llx\n", b, r[b]->id);
	memset(r[a]->name, 'X', 24 + 8);	/* off-by-8: name[24] 넘어서 씀 */
	pr_info("after : r[%d]->id=%#llx  (no fault, no warning)\n",
		b, r[b]->id);
out:
	for (i = 0; i < N && r[i]; i++)
		kfree(r[i]);
	return 0;
}

static void __exit e4_corrupt_exit(void) { }
module_init(e4_corrupt_init);
module_exit(e4_corrupt_exit);
MODULE_DESCRIPTION("E4: no memory protection inside the kernel");
MODULE_LICENSE("GPL");
```

```sh
# 게스트 안 (t.sh)
insmod /e4_corrupt.ko
dmesg | grep -A40 "e4_corrupt: r\[\|e4_corrupt: no adj"
```

**기본 부팅 (slab 디버그 없음)**

```text
lab initramfs: 6.18.55 up 6.93s
[    7.899190] e4_corrupt: r[0]=ffff0000035b1fa0  r[1]=ffff0000035b1fc0 (next object)
[    7.899344] e4_corrupt: before: r[1]->id=0x1111111100000001
[    7.902930] e4_corrupt: after : r[1]->id=0x5858585858585858  (no fault, no warning)
```

출력에서 볼 것: `r[1]`의 `id`가 `'X'`(0x58) 8개로 바뀌었다. 커널은 아무것도 몰랐다. 이번에는 `r[1]`이 내 객체였지만, 실제로는 **다른 서브시스템의 객체**일 확률이 더 높다. 더 나쁜 경우 해제된 객체 안의 SLUB freelist 포인터를 덮어 **나중에 전혀 무관한 `kmalloc`이 이상한 주소를 돌려주는** 형태로 터진다.

**커널 인자 `slab_debug=FZ,kmalloc-32` 추가 (같은 모듈)**

```text
lab initramfs: 6.18.55 up 6.50s
[    8.993784] e4_corrupt: no adjacent object (redzone?) - overflow r[0]
[    8.993893] [Right Redzone overwritten] 0xffff0000035abf40-0xffff0000035abf47 @offset=3904. First byte 0x58 instead of 0xcc
[    8.994260] =============================================================================
[    8.994657] BUG kmalloc-32 (Tainted: G           O       ): Object corrupt
[    8.994713] -----------------------------------------------------------------------------
[    8.994713] 
[    8.994914] Slab 0xfffffdffc00d6ac0 objects=42 used=42 fp=0x0000000000000000 flags=0x3fffe0000000000(node=0|zone=0|lastcpupid=0x1ffff)
[    8.995295] Object 0xffff0000035abf20 @offset=3872 fp=0x5858585858585858
[    8.995295] 
[    9.001012] Redzone  ffff0000035abf00: cc cc cc cc cc cc cc cc cc cc cc cc cc cc cc cc  ................
[    9.001129] Redzone  ffff0000035abf10: cc cc cc cc cc cc cc cc cc cc cc cc cc cc cc cc  ................
[    9.001224] Object   ffff0000035abf20: 00 00 00 00 11 11 11 11 58 58 58 58 58 58 58 58  ........XXXXXXXX
[    9.001240] Object   ffff0000035abf30: 58 58 58 58 58 58 58 58 58 58 58 58 58 58 58 58  XXXXXXXXXXXXXXXX
[    9.001272] Redzone  ffff0000035abf40: 58 58 58 58 58 58 58 58                          XXXXXXXX
[    9.001348] Padding  ffff0000035abf48: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
[    9.001362] Padding  ffff0000035abf58: 00 00 00 00 00 00 00 00                          ........
...
[    9.005334] WARNING: CPU: 1 PID: 70 at mm/slub.c:1234 object_err+0x1f8/0x210
...
[    9.053063]  check_bytes_and_report+0x10c/0x150
[    9.053374]  check_object+0x258/0x3b0
```

출력에서 볼 것:

- `slab_debug`(옛 이름 `slub_debug`, 둘 다 받는다)를 켜면 객체마다 앞뒤에 **redzone**(0xcc로 채운 빈칸)이 생긴다. 그래서 32바이트 간격으로 붙은 객체가 없어졌고(첫 줄), `kfree()` 때 SLUB가 redzone을 검사해서 **정확히 어느 8바이트가 덮였는지** 덤프한다. 펌웨어에서 버퍼 끝에 canary를 심던 것과 같은 원리를 할당자가 자동으로 한다.
- 덤프의 첫 8바이트 `00 00 00 00 11 11 11 11` = `id` 0x1111111100000000 (리틀 엔디언으로 저장). 이어지는 `XXXX...`가 `name[24]`, 그리고 Redzone 첫 8바이트까지 `X`.
- 이 옵션은 메모리와 속도를 희생하므로 양산에서는 끄고, **재현 테스트 때 커널 인자로만** 켠다. 더 강력한 도구는 KASAN(M7 예정)으로, 접근하는 **순간** 잡는다(redzone은 해제할 때 잡는다).

### 8.3 Oops 해부 (M3 미리보기)

§7의 PAN Oops를 위에서부터 읽는 법이다. Oops는 "커널이 자기 코드에서 처리할 수 없는 예외를 만났다"는 보고서이고, 펌웨어의 HardFault 핸들러가 찍던 레지스터 덤프 + 스택 추적의 커널판이다.

```svg
<svg viewBox="0 0 680 452" xmlns="http://www.w3.org/2000/svg">
  <text x="200" y="20" text-anchor="middle" font-size="13" font-weight="bold">Oops 로그 (예제 13, 요약)</text>
  <text x="560" y="20" text-anchor="middle" font-size="13" font-weight="bold">읽는 법</text>
  <rect x="10" y="32" width="380" height="40" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="20" y="50" font-size="12">Unable to handle kernel access to user</text>
  <text x="20" y="66" font-size="12">memory outside uaccess routines at 1e4d5aa0</text>
  <rect x="10" y="80" width="380" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="20" y="98" font-size="12">ESR = 0x9600000f  EC = 0x25: DABT</text>
  <text x="20" y="114" font-size="12">FSC = 0x0f: level 3 permission fault</text>
  <rect x="10" y="128" width="380" height="24" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="20" y="145" font-size="12">Internal error: Oops: 9600000f [#1] SMP</text>
  <rect x="10" y="160" width="380" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="20" y="178" font-size="12">Modules linked in: e4_uptr(O)</text>
  <text x="20" y="194" font-size="12">Comm: sh Tainted: G O 6.18.55 #1 PREEMPT</text>
  <rect x="10" y="208" width="380" height="24" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="20" y="225" font-size="12">pstate: 81402005 (... +PAN ...)</text>
  <rect x="10" y="240" width="380" height="24" rx="4" fill="#d0564a" fill-opacity="0.18" stroke="#d0564a"/>
  <text x="20" y="257" font-size="12">pc : e4_write+0x124/0x144 [e4_uptr]</text>
  <rect x="10" y="272" width="380" height="24" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="20" y="289" font-size="12">x20: 000000001e4d5aa0  (x0 ~ x29 레지스터)</text>
  <rect x="10" y="304" width="380" height="72" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="20" y="322" font-size="12">Call trace:</text>
  <text x="20" y="338" font-size="12">  e4_write+0x124/0x144 [e4_uptr] (P)</text>
  <text x="20" y="354" font-size="12">  vfs_write+0xd0/0x380</text>
  <text x="20" y="370" font-size="12">  ksys_write → ... → el0_svc</text>
  <rect x="10" y="384" width="380" height="24" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="20" y="401" font-size="12">Code: ... 17ffffcf (39400281)</text>
  <text x="400" y="56" font-size="12">① 무엇을 · 어느 주소에서</text>
  <text x="400" y="104" font-size="12">② ARM 예외 원인 (ESR 해독)</text>
  <text x="400" y="145" font-size="12">③ Oops 번호 — [#1]이 진짜 원인</text>
  <text x="400" y="178" font-size="12">④ 의심 모듈 · taint 플래그</text>
  <text x="400" y="194" font-size="12">    (O = out-of-tree 모듈)</text>
  <text x="400" y="225" font-size="12">⑤ CPU 상태 — PAN이 켜져 있었다</text>
  <text x="400" y="257" font-size="12">⑥ 죽은 위치 = 함수+오프셋/크기</text>
  <text x="400" y="289" font-size="12">⑦ 레지스터 — 문제 주소가 x20에</text>
  <text x="400" y="338" font-size="12">⑧ 누가 불렀나 (아래가 먼저)</text>
  <text x="400" y="401" font-size="12">⑨ 괄호 안 = 죽은 명령어 (ldrb)</text>
  <text x="340" y="434" text-anchor="middle" font-size="12">⑥ + ⑨ 로 소스 줄을 찾는다: faddr2line e4_uptr.ko e4_write+0x124/0x144 (M3)</text>
</svg>
```

그림 4 — Oops 해부도. 위에서 아래로 "무엇이 → 왜(ESR) → 몇 번째 → 누구(모듈·taint) → CPU 상태 → 어디서(pc) → 데이터(레지스터) → 경로(Call trace) → 명령어(Code)" 순서다. 여러 Oops가 이어지면 항상 `[#1]`부터 본다.

### 8.4 예제 16 — `pc`에서 소스 줄까지 (`faddr2line`, `objdump`)

무엇을 확인하나: Oops의 `pc : e4_write+0x124/0x144`를 소스 파일의 줄 번호와 명령어로 되돌릴 수 있는지.

```sh
cd /var/tmp/E4/e4_uptr
/var/lab/src/linux-6.18.55/scripts/faddr2line e4_uptr.ko e4_write+0x124/0x144
objdump -d e4_uptr.ko | grep -B1 -A1 "39400281"
```

```text
e4_write+0x124/0x144:
e4_write at /var/tmp/E4/e4_uptr/e4_uptr.c:20
 120:	17ffffcf 	b	5c <e4_write+0x5c>
 124:	39400281 	ldrb	w1, [x20]
 128:	90000000 	adrp	x0, 0 <e4_write>
```

출력에서 볼 것: 20번째 줄 `kbuf[0] = *ubuf;` — sparse가 경고한 바로 그 줄이다. 명령어 `ldrb w1, [x20]`은 Oops의 `Code:` 괄호 안 `39400281`과 같고, 주소 레지스터 `x20 = 0x1e4d5aa0`이 Oops 첫 줄의 fault 주소다. 랩 커널은 `CONFIG_DEBUG_INFO=y`라서 `.ko`에 DWARF가 있어 줄 번호까지 나온다. 양산 커널에서 디버그 정보를 빼더라도 **같은 빌드의 디버그 심볼(`vmlinux`, `.ko`)은 반드시 보관**해야 현장 Oops를 해석할 수 있다.

---

## 9. 예외는 없다 — 모든 에러를 손으로 처리한다

### 9.1 규약

커널 C에는 C++ 예외도, 전역 `errno`도, 프로세스 종료로 자원을 회수해 주는 OS도 없다. 규약은 세 가지다.

1. 실패하면 **음수 errno**를 돌려준다(`return -ENOMEM;`). 성공은 0 또는 양수(읽은 바이트 수 등). 유저 공간으로 돌아갈 때 syscall 층이 `-ret`를 `errno`에 넣고 `-1`을 돌려준다.
2. 포인터를 돌려주는 함수는 실패를 `NULL` 또는 **`ERR_PTR(-ENOMEM)`** 으로 표현한다. 받는 쪽은 `IS_ERR()`/`PTR_ERR()`로 검사한다. 어느 쪽인지는 함수마다 다르므로 문서·소스를 확인한다 (`kmalloc`은 NULL, `kthread_create`·`devm_ioremap_resource`는 `ERR_PTR`).
3. 여러 자원을 차례로 얻다가 중간에 실패하면 **얻은 역순으로** 풀어 준다 — `goto` 레이블 사다리(E5에서 자세히). 요즘 드라이버는 `devm_*` 관리 자원으로 이 사다리 자체를 줄인다(G6).

```c
/* 패턴 (E5에서 실행 예제로 다시 본다) */
static int e4_setup(struct e4_priv *p)
{
	int ret;

	p->buf = kzalloc(4096, GFP_KERNEL);
	if (!p->buf)
		return -ENOMEM;
	p->wq = alloc_workqueue("e4", 0, 0);
	if (!p->wq) {
		ret = -ENOMEM;
		goto err_free_buf;
	}
	ret = e4_hw_init(p);
	if (ret)
		goto err_destroy_wq;
	return 0;

err_destroy_wq:
	destroy_workqueue(p->wq);
err_free_buf:
	kfree(p->buf);
	return ret;
}
```

### 9.2 `init`이 실패하면 유저는 무엇을 보나

예제 12(`e4_alloc`)의 `fail=1` 실행에서 본 출력이다 — `module_init`이 `-ENOMEM`을 돌려주면:

```text
insmod: can't insert '/e4_alloc.ko': Cannot allocate memory
insmod exit=1
```

출력에서 볼 것: `init_module(2)` syscall이 `-ENOMEM`을 그대로 유저에게 전달했고, BusyBox `insmod`가 `strerror(ENOMEM)`을 찍었다. 커널은 실패한 모듈을 **자동으로 언로드**한다 — `init`이 실패하면 `exit`는 불리지 않으므로, `init` 안에서 이미 얻은 자원은 **`init`이 직접** 풀어야 한다. 이걸 빠뜨린 것이 가장 흔한 모듈 누수 버그다. 예제 4의 `kstrtouint`처럼 **반환값을 확인하지 않은 실패**도 같은 부류다.

---

## 10. 동시성은 어디에나 있다

### 10.1 직관

유저 앱은 스레드를 만들지 않으면 동시성이 없다. 커널 코드는 **내가 아무것도 안 해도** 동시에 실행된다:

- **SMP**: 두 프로세스가 동시에 같은 `/dev/e4`에 `write()`하면 내 `.write` 핸들러가 CPU 두 개에서 **동시에** 돈다.
- **선점(preemption)**: `CONFIG_PREEMPT=y`(랩 커널)면 내 코드가 아무 명령 사이에서나 멈추고 다른 태스크가 같은 코드를 돈다.
- **인터럽트·softirq**: 내 IRQ 핸들러가 내 `read()` 한가운데 끼어든다.

펌웨어에서 "메인 루프와 ISR이 공유하는 변수는 `volatile` + 인터럽트 끄고 접근"이 규칙이었다면, 커널에서는 **모든 공유 데이터에 대해 "누가 동시에 만질 수 있나"를 먼저 묻는다.** `volatile`은 답이 아니다(F5).

### 10.2 예제 17 — `int++`는 원자적이지 않다 (`e4_race`)

무엇을 확인하나: kthread 4개가 CPU마다 하나씩 공유 `int`를 100만 번씩 증가시키면 몇 번이 사라지는지, `atomic_t`와 비교.

```c
// SPDX-License-Identifier: GPL-2.0
/* e4_race: SMP에서 int++ 는 원자적이지 않다 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/atomic.h>
#include <linux/completion.h>

#define NTHREADS 4
#define LOOPS 1000000
static int plain;			/* 보호 없는 공유 카운터 */
static atomic_t safe = ATOMIC_INIT(0);
static DECLARE_COMPLETION(e4_done);
static atomic_t running = ATOMIC_INIT(NTHREADS);
static atomic_t go = ATOMIC_INIT(0);

static int e4_worker(void *arg)
{
	int i;

	while (!atomic_read(&go))		/* 모두 동시에 출발 */
		cpu_relax();
	for (i = 0; i < LOOPS; i++) {
		WRITE_ONCE(plain, READ_ONCE(plain) + 1);	/* load → add → store */
		atomic_inc(&safe);
	}
	if (atomic_dec_and_test(&running))
		complete(&e4_done);
	return 0;
}

static int __init e4_race_init(void)
{
	struct task_struct *t;
	int i;

	for (i = 0; i < NTHREADS; i++) {
		t = kthread_create(e4_worker, NULL, "e4_race/%d", i);
		if (IS_ERR(t))
			return PTR_ERR(t);	/* 실제 코드라면 앞서 만든 스레드 정리 필요 */
		kthread_bind(t, i % num_online_cpus());	/* CPU마다 하나씩 */
		wake_up_process(t);
	}
	atomic_set(&go, 1);
	wait_for_completion(&e4_done);
	pr_info("cpus=%u expected=%d plain=%d atomic=%d lost=%d\n",
		num_online_cpus(), NTHREADS * LOOPS, plain,
		atomic_read(&safe), NTHREADS * LOOPS - plain);
	return 0;
}

static void __exit e4_race_exit(void) { }
module_init(e4_race_init);
module_exit(e4_race_exit);
MODULE_DESCRIPTION("E4: concurrency is everywhere");
MODULE_LICENSE("GPL");
```

```sh
SMP=4 FILES=/var/tmp/E4/e4_race/e4_race.ko ./run_guest.sh race t_race.sh   # t_race.sh: insmod + dmesg | grep
SMP=1 FILES=/var/tmp/E4/e4_race/e4_race.ko ./run_guest.sh race t_race.sh
```

```text
lab initramfs: 6.18.55 up 3.88s
[    4.484702] e4_race: loading out-of-tree module taints kernel.
[    4.664102] e4_race: cpus=4 expected=4000000 plain=3617758 atomic=4000000 lost=382242
lab initramfs: 6.18.55 up 2.79s
[    3.206433] e4_race: loading out-of-tree module taints kernel.
[    3.343459] e4_race: cpus=1 expected=4000000 plain=4000000 atomic=4000000 lost=0
```

출력에서 볼 것:

- CPU 4개: **38만 번(약 9.6%)의 증가가 사라졌다.** 두 CPU가 같은 값을 읽고 각자 +1을 써서 하나가 덮인다. `atomic_inc`는 정확히 400만 (arm64에서는 LSE `stadd` 또는 LL/SC 루프).
- CPU 1개: 0개 손실. 그렇다고 단일 코어면 안전한 것은 아니다 — 선점이 load와 store 사이에 끼면 똑같이 잃는다. 확률이 낮아 이 실행에서 안 보였을 뿐이다.
- 처음에는 `kthread_run`으로 그냥 띄웠더니 `lost=0`이었다: 각 스레드가 너무 빨리 끝나 실제로는 겹치지 않았다. 레이스는 **"테스트에서 안 나왔다"가 "없다"를 뜻하지 않는다.** 그래서 커널은 lockdep·KCSAN 같은 도구로 증명하려 든다(F2, F4, M7).
- `READ_ONCE`/`WRITE_ONCE`는 컴파일러가 로드·스토어를 합치거나 쪼개지 않게 할 뿐 **원자성을 주지 않는다**(F5).

---

## 11. 이식성 — 엔디언·크기·정렬을 타입으로 강제한다

### 11.1 직관

Don의 SSD 펌웨어는 칩 하나(리틀 엔디언 ARM)에서만 돌았다. 그래도 NVMe 스펙의 구조체는 리틀 엔디언으로 정의돼 있고, 호스트가 빅 엔디언 POWER 서버일 때를 대비해 필드를 `le32_to_cpu` 류로 다뤘을 것이다. Linux 드라이버는 **같은 소스가 30여 개 아키텍처, LE/BE, 32/64비트에서 컴파일된다.** 센서 레지스터·네트워크 패킷·디스크 포맷처럼 "바깥에서 정해진 바이트 배열"은 항상 명시적 변환을 거친다.

| 도구 | 용도 |
|---|---|
| `u8/u16/u32/u64`, `s16` … | 커널 내부 고정 폭 정수 (`linux/types.h`) |
| `__u32`, `__le32`, `__be16` | uapi 헤더(유저와 공유)·외부 포맷 필드 |
| `le32_to_cpu`, `cpu_to_be16` … | 엔디언 변환 (LE CPU에서 `le*`는 no-op) |
| `get_unaligned_le32(p)` | 정렬 안 된 바이트 배열에서 읽기 (`linux/unaligned.h`) |
| `__packed` | 패딩 없는 wire format 구조체 |
| `sizeof(long)` | 32비트에서 4, 64비트에서 8 — 포맷 정의에 `long` 금지 |

### 11.2 예제 18 — wire format 파싱과 sparse의 endian 검사 (`e4_endian`)

무엇을 확인하나: 1바이트 어긋난 위치에 있는 리틀 엔디언 IMU 헤더를 안전하게 파싱하는 법, 그리고 변환을 빠뜨리면 sparse가 잡는지.

```c
// SPDX-License-Identifier: GPL-2.0
/* e4_endian: 바이트 순서·정렬·크기를 타입으로 강제하기 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/types.h>
#include <linux/unaligned.h>

struct e4_imu_hdr {			/* 센서가 LE로 보내는 헤더 (wire format) */
	__le16 magic;
	__le16 len;
	__le32 timestamp_us;
} __packed;

static const u8 wire[] = { 0xff, 0x34, 0x12, 0x08, 0x00,
			   0x78, 0x56, 0x34, 0x12 };	/* 1바이트 어긋난 위치 */

static int __init e4_endian_init(void)
{
	const struct e4_imu_hdr *h = (const void *)(wire + 1);
	u32 ts_ok = le32_to_cpu(h->timestamp_us);
#ifdef E4_BUGGY
	u32 ts_bad = h->timestamp_us;		/* 변환 누락: sparse가 잡는다 */
#else
	u32 ts_bad = (__force u32)h->timestamp_us;
#endif

	pr_info("sizeof: int=%zu long=%zu void*=%zu u64=%zu hdr=%zu\n",
		sizeof(int), sizeof(long), sizeof(void *), sizeof(u64),
		sizeof(struct e4_imu_hdr));
	pr_info("magic=%#x len=%u ts=%#x\n", le16_to_cpu(h->magic),
		le16_to_cpu(h->len), ts_ok);
	pr_info("get_unaligned_le32(wire+5)=%#x  be32=%#x\n",
		get_unaligned_le32(wire + 5), get_unaligned_be32(wire + 5));
	pr_info("cpu_to_be32(0x12345678) bytes in memory: %*ph\n", 4,
		&(__be32){ cpu_to_be32(0x12345678) });
	pr_info("ts_bad=%#x (big-endian CPU였다면 틀렸을 값)\n", ts_bad);
	return 0;
}

static void __exit e4_endian_exit(void) { }
module_init(e4_endian_init);
module_exit(e4_endian_exit);
MODULE_DESCRIPTION("E4: endianness and portability");
MODULE_LICENSE("GPL");
```

```sh
make -C /var/lab/out/linux-build M=$PWD C=1 modules                       # 정상판
touch e4_endian.c
make -C /var/lab/out/linux-build M=$PWD C=1 ccflags-y=-DE4_BUGGY modules  # 변환 누락판
```

```text
  CC [M]  e4_endian.o
  CHECK   e4_endian.c
  MODPOST Module.symvers
  CC [M]  e4_endian.mod.o
  CC [M]  .module-common.o
  LD [M]  e4_endian.ko
  CC [M]  e4_endian.o
  CHECK   e4_endian.c
e4_endian.c:22:23: warning: incorrect type in initializer (different base types)
e4_endian.c:22:23:    expected unsigned int [usertype] ts_bad
e4_endian.c:22:23:    got restricted __le32 const [usertype] timestamp_us
  MODPOST Module.symvers
  CC [M]  e4_endian.mod.o
  CC [M]  .module-common.o
  LD [M]  e4_endian.ko
```

```text
[    4.749571] e4_endian: sizeof: int=4 long=8 void*=8 u64=8 hdr=8
[    4.749634] e4_endian: magic=0x1234 len=8 ts=0x12345678
[    4.749654] e4_endian: get_unaligned_le32(wire+5)=0x12345678  be32=0x78563412
[    4.749674] e4_endian: cpu_to_be32(0x12345678) bytes in memory: 12 34 56 78
[    4.749756] e4_endian: ts_bad=0x12345678 (big-endian CPU였다면 틀렸을 값)
```

출력에서 볼 것:

- `__le32`는 sparse에게 **"restricted" 타입**이다. 평범한 `u32`에 바로 넣으면 경고, `le32_to_cpu()`를 거치거나 `__force`로 "알고 한다"고 표시해야 조용하다. gcc는 둘 다 같은 `unsigned int`로 본다.
- `ts_bad`가 리틀 엔디언 arm64에서는 우연히 맞다. **테스트로는 절대 못 잡는 버그** — 빅 엔디언 MIPS/PowerPC 보드로 포팅하는 날 터진다. 그래서 타입 + sparse로 잡는다.
- `hdr=8` — `__packed`라 패딩이 없다. `wire + 1`처럼 정렬 안 된 주소를 구조체로 캐스팅해도 `__packed` 구조체 필드는 컴파일러가 정렬을 가정하지 않고 접근한다(정렬 안 된 로드를 못 하는 아키텍처에서는 바이트 단위 로드로 쪼갠다). 일반 구조체를 정렬 안 된 주소로 캐스팅하면, arm64는 일반 메모리에서는 대체로 괜찮지만 디바이스 메모리(MMIO)나 일부 아키텍처에서는 alignment fault가 난다 — 바이트 배열에서는 `get_unaligned_*`가 정석이다.
- `%*ph`는 버퍼를 16진 바이트로 찍는 커널 포맷이다. `cpu_to_be32` 후 메모리에 `12 34 56 78`(네트워크 순서)로 놓였다.

---

## 12. 끝없는 busy loop 금지 — soft lockup detector

### 12.1 직관

펌웨어에서 `while (!(REG & READY));`는 흔하다. 칩이 응답하지 않으면 하드웨어 워치독이 보드를 리셋해 준다. 커널에서 같은 루프를 **선점을 끈 채**(스핀락 안, `preempt_disable()` 구간) 돌면 그 CPU는 스케줄러를 영영 못 만난다. 다른 태스크·RCU·워크큐가 그 CPU에서 멈추고, 결국 시스템 전체가 굳는다. 선점이 켜져 있어도 IRQ를 끄고 돌면 더 나쁘다(hard lockup).

커널의 소프트웨어 워치독:

- **soft lockup**: CPU가 `2 × watchdog_thresh`초(기본 10 → **20초**) 동안 한 번도 스케줄되지 않음. CPU마다 hrtimer가 주기적으로 돌며 "그 CPU의 stopper 스레드가 최근에 타임스탬프를 갱신했나"를 확인한다(`kernel/watchdog.c`, 임계값 = `get_softlockup_thresh()` = `watchdog_thresh * 2`).
- **hard lockup**: IRQ까지 꺼져서 hrtimer조차 못 돎. perf NMI 또는 buddy CPU 방식으로 감지(`CONFIG_HARDLOCKUP_DETECTOR`).
- **RCU stall**: 한 CPU가 RCU grace period를 너무 오래(기본 21초) 막음. 별도 감지기지만 같은 버그에서 같이 뜬다.

### 12.2 예제 19 — 선점을 끄고 15초 폴링 (`e4_lockup`)

무엇을 확인하나: 센서 ready 비트 폴링을 흉내 낸 `mdelay(1)` 루프를 선점 허용/금지 두 경우로 돌렸을 때 soft lockup detector가 반응하는지. 랩 커널은 `SOFTLOCKUP_DETECTOR`가 꺼져 있으므로 디버그 커널 복사본에서 돌리고, 빨리 보기 위해 커널 인자 `watchdog_thresh=5`(임계값 10초)를 준다.

```c
// SPDX-License-Identifier: GPL-2.0
/* e4_lockup: 선점을 끈 채 오래 돌면 soft lockup detector가 잡는다 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/preempt.h>
#include <linux/jiffies.h>

static int secs = 25;
module_param(secs, int, 0444);
static bool nopreempt = true;	/* false: 선점 허용 상태로 같은 루프 */
module_param(nopreempt, bool, 0444);

static int __init e4_lockup_init(void)
{
	unsigned long end = jiffies + secs * HZ;
	unsigned long loops = 0;

	pr_info("spinning %d s, preemption %s\n", secs,
		nopreempt ? "DISABLED" : "enabled");
	if (nopreempt)
		preempt_disable();
	while (time_before(jiffies, end)) {	/* 센서 ready 비트 폴링 흉내 */
		mdelay(1);
		loops++;
	}
	if (nopreempt)
		preempt_enable();
	pr_info("done after %lu loops\n", loops);
	return 0;
}

static void __exit e4_lockup_exit(void) { }
module_init(e4_lockup_init);
module_exit(e4_lockup_exit);
MODULE_DESCRIPTION("E4: soft lockup demo");
MODULE_LICENSE("GPL");
```

```sh
# 게스트 안 (t.sh) — 커널: 6.18.55-e4dbg, 커널 인자 watchdog_thresh=5
uname -r
cat /proc/sys/kernel/watchdog_thresh /proc/sys/kernel/soft_watchdog
insmod /e4_lockup.ko secs=15 nopreempt=0
rmmod e4_lockup
insmod /e4_lockup.ko secs=15
echo "insmod exit=$?"
dmesg | sed -n '/e4_lockup: loading/,$p' | grep -v "^\[.*\] x[0-9]"
```

```text
lab initramfs: 6.18.55-e4dbg up 1.39s
6.18.55-e4dbg
5
1
[   30.131959] watchdog: BUG: soft lockup - CPU#1 stuck for 13s! [insmod:73]
insmod exit=0
[    1.587263] e4_lockup: loading out-of-tree module taints kernel.
[    1.592705] e4_lockup: spinning 15 s, preemption enabled
[   16.591484] e4_lockup: done after 13961 loops
[   16.713976] e4_lockup: spinning 15 s, preemption DISABLED
[   30.131959] watchdog: BUG: soft lockup - CPU#1 stuck for 13s! [insmod:73]
[   30.132363] Modules linked in: e4_lockup(O+) [last unloaded: e4_lockup(O)]
[   30.133728] CPU: 1 UID: 0 PID: 73 Comm: insmod Tainted: G           O        6.18.55-e4dbg #1 PREEMPT 
...
[   30.134131] pstate: 80000005 (Nzcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
[   30.134190] pc : arch_timer_read_cntvct_el0+0x8/0xc
[   30.134767] lr : __delay_cycles+0x50/0x94
[   30.134801] sp : ffff80008048ba70
[   30.135177] Call trace:
[   30.135348]  arch_timer_read_cntvct_el0+0x8/0xc (P)
[   30.135463]  __delay+0x54/0xd0
[   30.135512]  __const_udelay+0x2c/0x38
[   30.135526]  e4_lockup_init+0x98/0xfc4 [e4_lockup]
[   30.135940]  do_one_initcall+0x60/0x1e0
[   30.135977]  do_init_module+0x54/0x240
[   30.135993]  load_module+0x18cc/0x1ef0
[   30.136003]  __do_sys_init_module+0x1b0/0x264
...
[   31.711824] e4_lockup: done after 14336 loops
```

출력에서 볼 것:

- **선점 허용(`nopreempt=0`)**: 15초 동안 똑같이 바쁘게 돌았지만 경고가 없다. `CONFIG_PREEMPT=y` 커널에서는 타이머 인터럽트에서 돌아올 때 더 높은 우선순위의 stopper 스레드가 끼어들어 타임스탬프를 갱신할 수 있었다. (CPU 시간은 여전히 낭비다 — 다른 일을 못 했다.)
- **선점 금지**: 루프 시작(16.7초) 후 약 13초 시점에 `watchdog: BUG: soft lockup - CPU#1 stuck for 13s!`. 임계값 10초 + hrtimer 주기(임계값의 1/5 = 2초 간격) 때문에 정확히 10초가 아니라 그다음 검사 시점에 찍힌다. Call trace의 `__const_udelay` ← `e4_lockup_init`이 범인을 가리킨다.
- soft lockup은 **경고일 뿐 루프를 멈추지 않는다** — 루프는 끝까지 돌았다(`done after 14336 loops`). 양산 장비에서 멈춤 대신 재부팅을 원하면 `softlockup_panic=1`(커널 인자 또는 `kernel.softlockup_panic` sysctl) + `panic=N`을 쓴다.
- `mdelay(1)` 한 번이 TCG에서 약 1.07 ms(15000 jiffies-ms / 14000 루프)였다. `mdelay`/`udelay`는 **잠들지 않는 busy-wait**다. 수 ms 이상 기다릴 때는 process context에서 `usleep_range()`/`msleep()`을 쓴다.

처음에는 기본 임계값(20초)과 25초 루프로 돌렸는데 soft lockup 대신 **RCU stall**이 먼저(약 21초) 떴고, soft lockup 메시지는 끝내 안 나왔다:

```text
[   26.698134] e4_lockup: spinning 25 s, preemption DISABLED
[   46.778994] hrtimer: interrupt took 13471088 ns
[   47.705047] rcu: INFO: rcu_preempt self-detected stall on CPU
[   47.707413] rcu: 	1-....: (5189 ticks this GP) idle=52e4/1/0x4000000000000000 softirq=199/199 fqs=2595
[   47.708134] rcu: 	(t=5251 jiffies g=-819 q=9 ncpus=2)
...
[   47.713112]  e4_lockup_init+0x98/0xfc4 [e4_lockup]
...
[   51.703631] e4_lockup: done after 24224 loops
```

RCU stall 보고 경로가 스택을 덤프하면서 워치독 타임스탬프를 갱신(`touch_nmi_watchdog()` → `touch_softlockup_watchdog()`)하기 때문에 soft lockup 카운트가 리셋된 것으로 보인다. 현장 로그에서 둘 중 무엇이 찍히든 **같은 버그**(선점 없이 너무 오래 도는 코드)다. 랩 기본 커널처럼 `SOFTLOCKUP_DETECTOR`가 꺼져 있어도 RCU stall 감지기는 남아 있어서, 긴 루프는 결국 이런 로그를 남긴다.

### 12.3 고치는 법

```c
/* 나쁨: 원자 문맥에서 무한 폴링 */
while (!(readl(base + STATUS) & READY))
	cpu_relax();

/* 좋음: 타임아웃 + 잠들며 폴링 (process context) — linux/iopoll.h */
ret = readl_poll_timeout(base + STATUS, val, val & READY,
			 100 /* us, 사이사이 usleep_range */, 10000 /* us 타임아웃 */);
if (ret)
	return dev_err_probe(dev, ret, "sensor not ready\n");	/* -ETIMEDOUT */

/* 원자 문맥에서 꼭 기다려야 하면: 짧은 상한 + _atomic 변형 */
ret = readl_poll_timeout_atomic(base + STATUS, val, val & READY, 1, 100);
```

긴 계산 루프를 process context에서 돌려야 하면 루프 안에 `cond_resched()`를 넣어 `PREEMPT_NONE`/`VOLUNTARY` 커널에서도 스케줄 기회를 준다.

---

## 13. 커널 코딩 스타일과 `checkpatch.pl`

### 13.1 왜 스타일까지 제약인가

커널은 수천 명이 같은 코드를 읽고 고친다. 그래서 스타일이 "취향"이 아니라 **리뷰를 통과하기 위한 조건**이다(`Documentation/process/coding-style.rst`). 핵심만:

- 들여쓰기는 **탭(8칸 폭)**, 한 줄 100열 이하(80열 권장). 들여쓰기가 3단계를 넘으면 함수를 나누라는 신호.
- 함수의 여는 중괄호 `{`는 **다음 줄**, `if`/`for`/`struct`의 `{`는 같은 줄.
- `typedef`로 구조체를 숨기지 않는다 (`struct e4_priv`를 그대로 쓴다). 예외는 불투명 타입·고정 폭 정수.
- 이름은 `snake_case`, 전역 심볼에는 드라이버 접두어. CamelCase 금지.
- `printk`에는 로그 레벨, 가능하면 `pr_*`/`dev_*`.
- 지역 변수 선언 뒤 빈 줄 하나.

### 13.2 예제 20 — 앱 스타일로 쓴 모듈에 `checkpatch.pl` 돌리기

무엇을 확인하나: 앱 개발자가 흔히 쓰는 스타일이 커널 기준으로 몇 개의 지적을 받는지, 그리고 이 노트의 모듈은 어떤지.

```c
// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/slab.h>

typedef struct { int Rate; int range; } imu_cfg_t;

static int __init e4_style_init(void) {
    imu_cfg_t *cfg = kmalloc(sizeof(imu_cfg_t), GFP_KERNEL);
    if (cfg == NULL) { printk("no mem\n"); return -1; }
    cfg->Rate = 400; cfg->range = 16;
    kfree(cfg);
    return 0;
}
module_init(e4_style_init);
MODULE_LICENSE("GPL");
```

```sh
CP=/var/lab/src/linux-6.18.55/scripts/checkpatch.pl
$CP --no-tree --terse -f e4_style.c; echo "exit=$?"
$CP --no-tree --terse --strict -f e4_style.c 2>/dev/null | grep "CHECK\|total"
$CP --no-tree --terse -f e4_atomic.c; echo "exit=$?"
```

```text
e4_style.c:5: WARNING: do not add new typedefs
e4_style.c:7: ERROR: open brace '{' following function definitions go on the next line
e4_style.c:8: WARNING: please, no spaces at the start of a line
e4_style.c:9: WARNING: Missing a blank line after declarations
e4_style.c:9: WARNING: please, no spaces at the start of a line
e4_style.c:9: WARNING: printk() should include KERN_<LEVEL> facility level
e4_style.c:9: ERROR: trailing statements should be on next line
e4_style.c:10: WARNING: please, no spaces at the start of a line
e4_style.c:11: WARNING: please, no spaces at the start of a line
e4_style.c:12: WARNING: please, no spaces at the start of a line
total: 2 errors, 8 warnings, 15 lines checked
exit=1
e4_style.c:5: CHECK: Avoid CamelCase: <Rate>
e4_style.c:9: CHECK: Comparison to NULL could be written "!cfg"
total: 2 errors, 8 warnings, 2 checks, 15 lines checked
e4_atomic.c:19: WARNING: use of in_atomic() is incorrect outside core kernel code
e4_atomic.c:22: WARNING: msleep < 20ms can sleep for up to 20ms; see function description of msleep().
total: 0 errors, 2 warnings, 41 lines checked
exit=1
```

출력에서 볼 것:

- 15줄에 10개 지적. `--strict`(일부 서브시스템 메인테이너가 요구)를 켜면 CamelCase와 `== NULL` 비교까지 잡는다. 하지만 **`return -1`은 아무도 안 잡는다** — `-1`은 `-EPERM`이라 유저에게 "Operation not permitted"가 간다. 의미 있는 errno(`-ENOMEM`)를 고르는 것은 사람 몫이다.
- 이 노트의 `e4_atomic.c`도 경고 2개를 받았다: (1) `in_atomic()`은 드라이버에서 "지금 잠들어도 되나"를 판단하는 데 쓰면 안 된다(`CONFIG_PREEMPT_COUNT`가 없는 커널에서는 스핀락 안에서도 0을 돌려줄 수 있다). 여기서는 관찰용이라 일부러 썼다. (2) `msleep(10)`은 jiffy 단위라 실제로는 최대 20 ms까지 잔다 — 짧은 지연은 `usleep_range()`. **checkpatch는 스타일만이 아니라 흔한 API 오용도 잡는다.**
- `-f`는 패치가 아닌 파일 전체 검사, `--no-tree`는 커널 트리 밖 파일을 검사할 때. 실제 업스트림 작업에서는 `git format-patch` 결과에 돌린다(E7).

---

## 14. 커널 안에서는 무슨 일이 일어나나

지금까지의 제약이 커널의 어떤 구조체·함수에서 나오는지 한 번에 정리한다.

| 제약 | 커널 쪽 실체 | 소스 위치 (6.18) | 드라이버에서 다시 만나는 곳 |
|---|---|---|---|
| libc 없음 | `-nostdinc`, `EXPORT_SYMBOL` 표, 모듈 로더의 심볼 해석 | `Makefile`, `kernel/module/main.c`, `lib/` | E3, E5 |
| FPU 금지 | `-mgeneral-regs-only`, `kernel_neon_begin()` → `fpsimd_save_user_state()` | `arch/arm64/Makefile`, `arch/arm64/kernel/fpsimd.c` | 암호·체크섬 가속 |
| 작은 스택 | `THREAD_SIZE`, `task_stack_page()`, VMAP 스택 할당, `handle_bad_stack()` | `arch/arm64/include/asm/memory.h`, `kernel/fork.c`, `arch/arm64/kernel/entry-common.c` | 큰 버퍼는 `priv` 구조체로 (G3) |
| atomic에서 sleep 금지 | `preempt_count`, `schedule_debug()`, `__might_resched()` | `include/linux/preempt.h`, `kernel/sched/core.c` | IRQ 핸들러(H4), 락 선택(F2) |
| 할당 실패·GFP | `gfp_t` 비트, slab/page allocator, `might_alloc()` | `include/linux/gfp_types.h`, `mm/slub.c`, `mm/page_alloc.c` | F9, `devm_kzalloc` (G6) |
| 유저 포인터 | `__user`, `access_ok()`, `copy_from_user()`, exception table, PAN | `include/linux/uaccess.h`, `arch/arm64/mm/fault.c` | `.read`/`.write`/`.unlocked_ioctl` (E6, G4) |
| 보호 없음 | 단일 커널 주소 공간, SLUB redzone, KASAN | `mm/slub.c`, `mm/kasan/` | M3, M7 |
| 에러 처리 | 음수 errno, `ERR_PTR`, `goto` 정리 | `include/linux/err.h` | 모든 `probe()` (G3) |
| 동시성 | 선점 모델, SMP, `atomic_t`, 락 | `kernel/locking/`, `include/linux/atomic/` | F2, F4, F5 |
| 이식성 | `__le32`/`__be32`, `__bitwise`, `get_unaligned_*` | `include/uapi/linux/types.h`, `include/linux/unaligned.h` | 레지스터·패킷 파싱 (H5, I1) |
| 긴 루프 | watchdog hrtimer + per-CPU stopper 스레드 | `kernel/watchdog.c` | 폴링 루프 → `readx_poll_timeout` (H1) |

핵심 구조 하나만 기억한다면 **`preempt_count`** 다. 태스크마다(arm64는 `thread_info` 안) 있는 32비트 정수로, 하위 8비트는 `preempt_disable()` 중첩 수, 다음 8비트는 softirq, 그다음 4비트는 hardirq, 그다음 4비트는 NMI 중첩 수다. `spin_lock()`은 이 값을 +1 하고, 인터럽트 진입은 hardirq 필드를 +1 한다. `in_atomic()`, `in_interrupt()`, `in_task()`가 전부 이 값을 읽는다. §5의 "잠들어도 되나"와 §12의 "선점될 수 있나"가 같은 정수 하나로 결정된다.

---

## 15. Embedded 실무에서 다시 보기

### 15.1 장면 1 — IMU IRQ 핸들러에서 I2C로 FIFO를 읽고 싶다

센서 키트의 IMU가 "FIFO watermark" 인터럽트를 올린다. 펌웨어 습관대로 hardirq 핸들러 안에서 `i2c_transfer()`로 FIFO를 읽으면 §5의 버그다 — I2C 전송은 컨트롤러 완료 인터럽트를 **기다리며 잠든다.** 정답은 `devm_request_threaded_irq(dev, irq, NULL, e4_imu_thread_fn, IRQF_ONESHOT, ...)`: 핸들러가 **irq thread(process context)** 에서 돌므로 I2C·`mutex`·`GFP_KERNEL`을 다 쓸 수 있다. IIO 드라이버(I2) 대부분이 이 구조다.

### 15.2 장면 2 — 카메라 드라이버의 큰 버퍼

V4L2 드라이버가 센서 레지스터 초기화 테이블(수 KiB)이나 메타데이터 버퍼를 함수 지역 배열로 잡으면 §4.6의 `-Wframe-larger-than` 경고가 난다. 경고를 무시한 채 다른 큰 프레임 함수에서 부르면 현장에서 가끔 스택 overflow panic이 난다. 정답: 상수 테이블은 `static const`(`.rodata`), 작업 버퍼는 `probe()`에서 `devm_kzalloc`으로 한 번 잡아 `priv` 구조체에 둔다. 프레임 버퍼 같은 대용량은 DMA 버퍼(`dma_alloc_coherent`, videobuf2 — H8, Q1).

### 15.3 장면 3 — ioctl로 받은 구조체 안의 포인터

CAN 모터 컨트롤러 설정 ioctl이 `struct e4_cfg { __u32 n; __u64 table_ptr; }`를 받는다. `copy_from_user(&cfg, argp, sizeof(cfg))`로 구조체는 안전하게 가져왔어도, **`table_ptr`는 여전히 유저 주소다.** 또 한 번 `copy_from_user(kbuf, u64_to_user_ptr(cfg.table_ptr), ...)`를 해야 하고, `n`은 상한 검사(`if (cfg.n > E4_MAX) return -EINVAL;`)를 먼저 해야 한다. 32비트 유저/64비트 커널 조합을 위해 포인터 필드를 `__u64`로 정의하는 것이 uapi 규칙이다(E6, G4).

### 15.4 장면 4 — "가끔 부팅 중 멈춘다" → soft lockup 로그

장비가 가끔 부팅 중 30초씩 멈춘다는 현장 보고. 콘솔 로그에 `watchdog: BUG: soft lockup - CPU#1 stuck for 22s!`와 함께 Call trace가 남아 있다면 범인은 거의 확실히 **선점이나 인터럽트를 끈 채 하드웨어 ready 비트를 무한 폴링하는 드라이버**다(§12). 고치는 법은 `readl_poll_timeout()`(타임아웃 + `usleep_range`)으로 바꾸고, 타임아웃이면 `-ETIMEDOUT`을 돌려 `probe()`를 실패시키는 것. 양산 커널에 `SOFTLOCKUP_DETECTOR`를 켜 두는 이유가 이 로그 한 줄 때문이다.

---

## 16. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 스핀락 안에서 `msleep`/`mutex_lock`/`i2c_transfer` | `BUG: scheduling while atomic` 또는 `sleeping function called from invalid context`, 가끔 데드락 | atomic context에서 `schedule()` | 락을 mutex로 바꾸거나, 잠드는 일을 락 밖·workqueue로 |
| 스핀락·IRQ 안에서 `kmalloc(GFP_KERNEL)` | 평소엔 정상, 메모리 부족 때만 위와 같은 BUG | 메모리 회수 경로에서 잠듦 | `GFP_ATOMIC`, 또는 미리 할당 |
| 큰 지역 배열 (수 KiB) | 빌드 경고 `-Wframe-larger-than`, 현장에서 stack overflow panic | 16 KiB 고정 스택 | `kmalloc`/`devm_kzalloc`, `priv` 구조체 |
| 입력 의존 재귀 | `Insufficient stack space` → `kernel stack overflow` panic | 깊이 무제한 | 반복문 + 명시적 스택, 깊이 상한 |
| `float`/`double` 사용 | `-mgeneral-regs-only is incompatible` 빌드 에러 | FPU 레지스터 미저장 계약 | 고정소수점, 또는 FPU 전용 파일 + `kernel_fpu_begin/end` |
| 유저 포인터 직접 역참조 | 개발 보드(PAN 없음)에서는 정상, 신형 칩에서 `access to user memory outside uaccess routines` Oops | PAN/SMAP, 미검증 주소 | `copy_from_user`/`get_user`, `make C=1` |
| `kmalloc` 반환값 미검사 | 메모리 부족 때 NULL 역참조 Oops | 할당은 실패할 수 있다 | `if (!p) return -ENOMEM;` |
| `init` 실패 경로에서 자원 미해제 | 모듈 재로드 시 `-EEXIST`/`-EBUSY`, 메모리 누수 | 실패 시 `exit`가 안 불림 | `goto` 정리 사다리, `devm_*` |
| 공유 카운터를 그냥 `++` | 통계·참조 카운트가 가끔 틀림 | SMP·선점 레이스 | `atomic_t`, `refcount_t`, 락 |
| 선점 끄고 ready 비트 무한 폴링 | `watchdog: BUG: soft lockup`, 시스템 멈춤 | 스케줄러가 CPU를 못 얻음 | `readl_poll_timeout`, `usleep_range`, 타임아웃 |

---

## 17. 면접에서 이렇게 말한다

**Q.** What are the main differences between writing kernel code and writing a userspace application?

**A.** 커널은 libc가 없고, FP를 못 쓰고, 스택이 16 KiB 고정이고, 잠들 수 없는 문맥이 있고, 할당이 실패할 수 있고, 유저 포인터를 믿으면 안 되고, 내부 메모리 보호가 없다. 실수 하나가 프로세스가 아니라 시스템 전체를 죽인다는 게 근본 차이다. 펌웨어 경험으로 보면 bare-metal 규칙에 "수많은 이웃 + 선점 + SMP + 신뢰할 수 없는 유저"가 더해진 환경이다.

> Kernel code runs in a single privileged address space with no safety net. There's no libc — the kernel has its own string, printk and allocation APIs. Floating point is off-limits by default because the kernel doesn't save user FP registers on entry. Each task has a small fixed stack, 16 KiB on arm64, so no big locals and no unbounded recursion. Some contexts — interrupt handlers, code holding a spinlock — must never sleep. Allocations can fail and need GFP flags. User pointers must go through copy_from_user. And since there's no memory protection between kernel components, a single bad write can corrupt unrelated subsystems and take down the whole system, not just one process.

**꼬리 질문**: "Coming from bare-metal firmware, which of these were new to you?" → 선점·SMP가 기본이라는 것과 유저 포인터 검증. 펌웨어에서는 ISR과 메인 루프만 생각하면 됐다.

**Q.** Why can't you use floating point in the Linux kernel? What if you really need SIMD?

**A.** 커널 진입 때 FP/SIMD 레지스터를 저장하지 않으므로, 커널이 쓰면 유저 프로세스의 FP 상태가 깨진다. arm64는 `-mgeneral-regs-only`로 컴파일해서 `float`를 쓰면 빌드 에러가 난다. 꼭 필요하면 FP 코드를 별도 파일로 분리해 `CC_FLAGS_FPU`로 컴파일하고, 호출 쪽을 `kernel_neon_begin/end`(일반 API로는 `kernel_fpu_begin/end`)로 감싼다. 이 구간에서 유저 FP 상태를 저장·복원한다.

> The kernel doesn't save and restore user FP/SIMD state on syscall or interrupt entry, to keep entry cheap, so kernel code touching those registers would silently corrupt user state. On arm64 the kernel is built with -mgeneral-regs-only, so using float is a compile error. When SIMD is really worth it — crypto, checksums, RAID — you put that code in its own translation unit compiled with CC_FLAGS_FPU and bracket the call with kernel_neon_begin and kernel_neon_end, which save the user state first. For sensor math in drivers, use fixed-point and let userspace do the floating-point conversion, like IIO does with raw plus scale.

**Q.** How big is the kernel stack, and what happens if you overflow it?

**A.** arm64는 태스크당 16 KiB(`THREAD_SIZE`, KASAN이면 32 KiB), 인터럽트는 CPU별 IRQ 스택 16 KiB. `CONFIG_VMAP_STACK`이면 스택이 vmalloc 영역에 있고 아래에 guard page가 있어서, 넘치는 순간 data abort → "Insufficient stack space" → "kernel stack overflow" panic이다. VMAP_STACK이 없으면 옆 메모리를 조용히 덮는다. 예방은 빌드 때 `-Wframe-larger-than`(`CONFIG_FRAME_WARN`)과 `-fstack-usage`로 한다.

> On arm64 it's 16 KiB per task, defined by THREAD_SIZE, with a separate per-CPU IRQ stack. With CONFIG_VMAP_STACK the stack lives in vmalloc space with an unmapped guard page below it, so an overflow faults immediately; the entry code detects the bad stack pointer, switches to a small per-CPU overflow stack and panics with "kernel stack overflow". Without VMAP_STACK you'd silently corrupt adjacent memory. I keep locals small, avoid input-dependent recursion, and watch the -Wframe-larger-than warnings; I've reproduced the overflow panic in QEMU with a 560-byte frame recursing 27 deep.

**Q.** What does "sleeping in atomic context" mean? How would you detect it?

**A.** atomic context는 스핀락을 잡았거나 선점·IRQ가 꺼졌거나 인터럽트·softirq 안이라서 `schedule()`을 부르면 안 되는 상태다. 여기서 `msleep`, `mutex_lock`, `GFP_KERNEL` 할당, I2C 전송처럼 잠들 수 있는 함수를 부르면 데드락이나 크래시가 난다. 실제로 잠들면 스케줄러가 "scheduling while atomic"을 찍지만, `GFP_KERNEL`처럼 가끔만 잠드는 경우는 `CONFIG_DEBUG_ATOMIC_SLEEP`을 켜야 `might_sleep()`이 "sleeping function called from invalid context"로 미리 잡는다.

> Atomic context means the current code can't call schedule(): holding a spinlock, preemption or interrupts disabled, or running in hardirq or softirq context — it's all tracked in preempt_count. If you call something that may sleep there, like msleep, mutex_lock, a GFP_KERNEL allocation or an I2C transfer, you can deadlock. If it actually schedules, the scheduler prints "scheduling while atomic". But many functions only sleep occasionally, so in development I enable CONFIG_DEBUG_ATOMIC_SLEEP: might_sleep() annotations then flag the call every time, even when it doesn't actually sleep. The fix is usually a threaded IRQ, a mutex instead of a spinlock, GFP_ATOMIC, or deferring to a workqueue.

**꼬리 질문**: "Can you take a mutex in an interrupt handler?" → 아니다. hardirq는 잠들 수 없다. threaded IRQ 핸들러에서는 된다.

**Q.** Why can't a driver just dereference a pointer it got from userspace?

**A.** 그 주소는 매핑이 안 돼 있을 수도, 스왑아웃돼 있을 수도, 커널 주소일 수도 있다. `copy_from_user`는 `access_ok`로 유저 영역인지 검사하고 fault를 exception table로 복구해서 `-EFAULT`를 돌려준다. ARMv8.1 PAN이나 x86 SMAP이 켜진 CPU에서는 직접 역참조가 바로 Oops다. 저는 QEMU에서 같은 모듈이 Cortex-A57(PAN 없음)에서는 조용히 동작하고 `-cpu max`에서는 Oops 나는 걸 직접 확인했다.

> A user pointer is just a number supplied by an untrusted process. It might be unmapped, paged out, or deliberately point into kernel memory. copy_from_user checks it with access_ok, and if the access faults, the exception table turns that into a short copy so the driver returns -EFAULT instead of oopsing. On CPUs with PAN on arm64 or SMAP on x86, a direct dereference faults immediately with "access to user memory outside uaccess routines". On older cores without PAN it may appear to work, which is exactly why it's dangerous — sparse catches it at build time through the __user annotation.

**Q.** What happens when you write past the end of a kmalloc buffer in the kernel?

**A.** 커널 안에는 보호 경계가 없어서 fault 없이 옆 객체가 깨진다. 증상은 나중에 전혀 다른 곳에서 나온다. 재현할 때는 `slab_debug=FZ`로 redzone을 켜서 free 때 잡거나, KASAN으로 접근하는 순간 잡는다.

> Nothing stops you — there's no protection between kernel components, so you silently corrupt whatever object is next in the slab, possibly belonging to a different subsystem, and the crash shows up later somewhere unrelated. To catch it I'd boot with slab_debug to add redzones, which reports "Right Redzone overwritten" on free, or use a KASAN build, which catches the bad access as it happens with a precise report.

**Q.** How do you handle errors in kernel code without exceptions?

**A.** 음수 errno를 반환하고, 포인터 반환 함수는 NULL 또는 `ERR_PTR`을 쓴다. 여러 자원을 얻는 함수는 실패 지점에서 얻은 역순으로 해제하는 `goto` 사다리를 쓰거나, 드라이버라면 `devm_*`으로 자동 해제한다. 모든 반환값을 검사한다 — `kstrto*`가 실패하면 출력 변수는 안 바뀐다.

> By convention functions return 0 or a negative errno, and pointer-returning functions return NULL or an ERR_PTR that you test with IS_ERR. When a function acquires several resources, failures jump to goto labels that release them in reverse order; in drivers I prefer devm-managed resources so the cleanup is automatic when probe fails. And every return value gets checked — for instance kstrtouint leaves the output untouched on failure, so ignoring the return code means using stale data.

**Q.** What is a soft lockup?

**A.** 한 CPU에서 watchdog 스레드가 `2 × watchdog_thresh`(기본 20초) 동안 스케줄되지 못한 상태다. 보통 선점을 끄고 무한 폴링하는 코드가 원인이다. `CONFIG_SOFTLOCKUP_DETECTOR`가 hrtimer로 감시해서 "BUG: soft lockup - CPU#N stuck for Xs!"와 스택을 찍는다. 인터럽트까지 꺼져 있으면 hard lockup이고 NMI·perf 기반 detector가 잡는다.

> A soft lockup means a CPU has been stuck in kernel mode without scheduling for more than twice watchdog_thresh — 20 seconds by default. The detector uses a per-CPU hrtimer to check whether the per-CPU watchdog task got to run; if not, it prints "BUG: soft lockup - CPU#N stuck for Xs" with a backtrace. The usual cause is busy-polling hardware with preemption disabled. A hard lockup is the same thing with interrupts also off, detected via NMI or a perf counter. The fix is to poll with a timeout and sleep between reads, like readl_poll_timeout.

---

## 18. 직접 해보기

1. **개념**: `kmalloc(GFP_KERNEL)`이 "잠들 수도 있다"는 것은 정확히 어떤 상황인가? 그리고 예제 10의 mode=2가 랩 커널에서 아무 경고 없이 지나간 이유는? — 정답/힌트: 빈 slab 객체가 없고 페이지 할당자가 메모리 회수(reclaim)·compaction을 해야 할 때 잠든다. 랩 게스트는 메모리가 넉넉해 빠른 경로로 끝났고, `DEBUG_ATOMIC_SLEEP`이 꺼져 있어 `might_alloc()` 검사도 없었다.
2. **코드**: 예제 9의 `e4_parse_frame`을 `kmalloc`을 쓰도록 고치고, `-fstack-usage`로 프레임이 몇 바이트로 줄었는지 확인하라. 에러 경로(`-ENOMEM`)도 넣을 것. — 정답/힌트: `.su`에서 수십 바이트 수준으로 줄고 `-Wframe-larger-than` 경고가 사라진다. `kfree`를 모든 반환 경로에서 부를 것.
3. **코드**: 예제 7의 `e4_recurse`에서 `pad[512]`를 `pad[128]`로 바꾸면 몇 번째 깊이에서 넘칠지 먼저 계산하고, 게스트에서 확인하라. — 정답/힌트: 프레임 크기를 `.su`로 먼저 확인한다 (128 + 고정분 48 정도). (16384 − 시작 시 사용량) ÷ 프레임 크기로 예측한다.
4. **관찰**: 예제 13의 게스트를 `-cpu cortex-a57`로 두고 커널 인자에 아무것도 바꾸지 말고 `direct=1`일 때 `echo` 대신 **매핑 안 된 주소**(예: `(void *)16`)를 `write()`로 넘기도록 `uwrite.c`를 고쳐 보라. A57에서도 Oops가 나는가? — 정답/힌트: 매핑 안 된 유저 주소(예: `(void *)16`)를 직접 역참조하면 PAN이 없어도 translation fault → Oops다 (exception table 항목이 없으므로).
5. **코드**: 예제 17의 `e4_race`에서 `plain` 증가를 `spin_lock`/`spin_unlock`으로 감싸고 `lost=0`이 되는지, 실행 시간이 어떻게 변하는지 `ktime_get_ns()`로 재라. — 정답/힌트: `lost=0`. 락 경합 때문에 `atomic_inc`보다 느리다 — F2의 "락 vs 원자 연산" 비용 비교의 출발점.
6. **관찰**: 예제 19를 `nopreempt=0`으로 다시 돌려 soft lockup이 나지 않는 것을 확인하고, 이유를 `CONFIG_PREEMPT=y`와 연결해 설명하라. — 정답/힌트: 선점 가능 커널에서는 타이머 인터럽트가 돌아올 때 watchdog 스레드(최고 우선순위 stopper)가 끼어들 수 있다. `CONFIG_PREEMPT_NONE` 커널이라면 `cond_resched()`가 필요하다.
7. **도구**: 이 노트의 모든 `e4_*.c`에 `checkpatch.pl --strict`와 `make C=1`을 돌리고, 남은 지적을 고쳐라. — 정답/힌트: `in_atomic()`·`msleep(10)` 경고는 의도적인 데모 코드라 남을 수 있다. 실제 드라이버라면 `usleep_range(10000, 11000)`.

---

## 19. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| freestanding / hosted | C 표준의 두 실행 환경 | hosted는 표준 라이브러리 전체를 가정. arm64 커널은 일반 코드에 `-ffreestanding`을 쓰지 않고 `-nostdinc`로 헤더만 차단한다 |
| `-mgeneral-regs-only` | 정수 레지스터만 쓰는 코드 생성 | arm64 커널의 FPU 금지를 컴파일러가 강제 |
| `THREAD_SIZE` | 태스크당 커널 스택 크기 | arm64 기본 16 KiB |
| VMAP_STACK | vmalloc 영역에 매핑한 커널 스택 | 아래 guard page로 overflow 즉시 감지 |
| guard page | 일부러 매핑하지 않은 페이지 | 넘친 접근을 fault로 바꾼다 |
| atomic context | 잠들 수 없는 실행 문맥 | 스핀락·IRQ·softirq·선점 비활성 구간 |
| process context | 태스크를 대신해 도는 문맥 | syscall, kthread, workqueue — 잠들 수 있다 |
| `preempt_count` | 태스크별 중첩 카운터 | preempt/softirq/hardirq/NMI 필드로 문맥을 판정 |
| `might_sleep()` | "잠들 수 있음" 주석 매크로 | `DEBUG_ATOMIC_SLEEP`일 때 atomic에서 부르면 BUG 출력 |
| GFP flags | 할당 시 허용 동작 비트 | `GFP_KERNEL`은 잠들어도 됨, `GFP_ATOMIC`은 안 됨 |
| `__user` | 유저 주소 공간 포인터 주석 | sparse가 직접 역참조를 경고 |
| PAN / SMAP | 특권 모드의 유저 메모리 접근 금지 기능 | arm64 v8.1 / x86. uaccess 함수 밖 접근을 fault로 |
| exception table | fault 복구 지점 표 | `copy_from_user` 중 fault를 `-EFAULT`로 바꾼다 |
| Oops | 커널이 처리 못 한 예외 보고 | 태스크를 죽이고 계속 갈 수도, panic일 수도 |
| taint | 커널 오염 플래그 | O(트리 밖 모듈), W(경고 발생), D(Oops 발생) 등 |
| redzone | 객체 경계의 감시 바이트 | `slab_debug=Z`가 넣고 free 때 검사 |
| soft lockup | 한 CPU가 오랫동안 스케줄 안 됨 | 기본 20초, watchdog이 경고 |
| `checkpatch.pl` | 커널 스타일·API 오용 검사기 | 패치 제출 전 필수 |
| sparse | 커널용 정적 분석기 | `make C=1`, 주소 공간·엔디언 타입 검사 |

---

## 20. 요약 & 체크리스트

커널 코드는 펌웨어처럼 맨바닥에서 돌지만, 수많은 이웃과 같은 주소 공간을 쓰고, 선점·SMP·인터럽트가 항상 켜져 있고, 위에서는 믿을 수 없는 유저 공간이 포인터를 던진다. 그래서 libc 대신 커널 라이브러리(`lib/`, `kstrto*`, `strscpy`, `kmalloc`)를 쓰고, FP는 `-mgeneral-regs-only`로 막혀 있으며(필요하면 `kernel_neon_begin/end` 구간), 스택은 16 KiB 고정이라 큰 지역 변수와 재귀를 피하고(VMAP_STACK이 넘침을 panic으로 잡는다), atomic context에서는 절대 잠들지 않고(`DEBUG_ATOMIC_SLEEP`이 잠복 버그를 드러낸다), 모든 할당·호출의 실패를 음수 errno로 처리하며, 유저 메모리는 `copy_from_user`로만 만지고(PAN이 직접 접근을 Oops로 만든다), 공유 데이터는 원자 연산·락으로 보호하고, 외부 포맷은 `__le32`·`get_unaligned_*`로 다루고, 하드웨어 폴링에는 타임아웃을 둔다. 이 규칙들은 대부분 **도구로 확인할 수 있다**: 컴파일러 플래그, `-Wframe-larger-than`, sparse, checkpatch, slab_debug, 디버그 Kconfig.

- [ ] 커널 코드와 유저 앱 코드의 차이를 7가지 이상 영어로 30초 안에 말할 수 있다
- [ ] arm64 Kbuild의 `-nostdinc`, `-mgeneral-regs-only`, `-Wframe-larger-than` 이 각각 어떤 제약을 강제하는지 설명할 수 있다
- [ ] libc 함수(`printf`, `malloc`, `atoi`, `strcpy`, `qsort`)의 커널 대체 함수를 댈 수 있다
- [ ] FP 코드를 별도 파일 + `CC_FLAGS_FPU` + `kernel_fpu_begin/end` 로 빌드할 수 있다
- [ ] `THREAD_SIZE`가 어디서 정해지는지, VMAP_STACK이 overflow를 어떻게 잡는지 그림으로 설명할 수 있다
- [ ] `-fstack-usage`와 `.su` 파일로 함수별 스택 사용량을 잴 수 있다
- [ ] atomic context와 process context를 구분하고, 각각에서 쓸 수 있는 API를 말할 수 있다
- [ ] "scheduling while atomic"과 "sleeping function called from invalid context"의 차이와 `CONFIG_DEBUG_ATOMIC_SLEEP`의 역할을 설명할 수 있다
- [ ] `GFP_KERNEL`과 `GFP_ATOMIC`을 문맥에 맞게 고를 수 있다
- [ ] 유저 포인터를 직접 역참조하면 왜 위험한지, PAN이 무엇인지 설명하고 `copy_from_user`로 고칠 수 있다
- [ ] Oops 로그에서 원인·모듈·`pc`·Call trace를 읽고 `faddr2line`으로 소스 줄을 찾을 수 있다
- [ ] soft lockup이 무엇이고 하드웨어 폴링 코드를 어떻게 고쳐야 하는지 말할 수 있다

## 참고 자료

- Linux kernel documentation — Floating-point API: https://docs.kernel.org/core-api/floating-point.html
- Linux kernel documentation — Linux kernel coding style: https://docs.kernel.org/process/coding-style.html
- Linux kernel documentation — Memory Allocation Guide (GFP 플래그): https://docs.kernel.org/core-api/memory-allocation.html
- Linux kernel documentation — Short users guide for SLUB (`slab_debug`): https://docs.kernel.org/mm/slub.html
- Linux kernel documentation — Softlockup detector and hardlockup detector (aka nmi_watchdog): https://docs.kernel.org/admin-guide/lockup-watchdogs.html
- Linux kernel documentation — How to get printk format specifiers right: https://docs.kernel.org/core-api/printk-formats.html
- Linux kernel documentation — Sparse: https://docs.kernel.org/dev-tools/sparse.html
- Linux kernel documentation — Checkpatch: https://docs.kernel.org/dev-tools/checkpatch.html
- Jonathan Corbet, Alessandro Rubini, Greg Kroah-Hartman, "Linux Device Drivers, 3rd ed." 2장 (Building and Running Modules — "Kernel Modules Versus Applications" 절, 오래된 판이지만 개념은 그대로)
- Robert Love, "Linux Kernel Development, 3rd ed." 2장 (A Beast of a Different Nature)
- 랩 소스 (elixir.bootlin.com에서도 같은 경로): `arch/arm64/include/asm/memory.h`(THREAD_SIZE), `arch/arm64/Makefile`(CC_FLAGS_FPU/NO_FPU), `arch/arm64/kernel/fpsimd.c`(kernel_neon_begin), `kernel/sched/core.c`(schedule_debug, __might_resched), `kernel/watchdog.c`, `arch/arm64/mm/fault.c`(PAN fault 메시지), `include/linux/preempt.h`

