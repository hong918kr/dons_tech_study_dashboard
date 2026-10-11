# B4. 빌드 시스템 — Make · Kbuild/Kconfig 미리보기 · CMake · Meson · autotools · pkg-config

> **이 노트를 다 읽으면**: Make가 의존성 그래프와 타임스탬프로 무엇을 다시 빌드할지 정하는 원리를 설명하고 `-MMD -MP`로 "헤더 바꿨는데 안 다시 빌드됨" 버그를 막을 수 있다 · `obj-m` 한 줄짜리 Kbuild로 out-of-tree 커널 모듈을 빌드하고 Kconfig가 `CONFIG_` 매크로가 되는 길을 그릴 수 있다 · CMake toolchain file, Meson cross file, `./configure --host`로 같은 코드를 armhf로 크로스 빌드할 수 있다 · pkg-config가 호스트 라이브러리를 타깃 빌드에 새게 만드는 함정을 재현하고 고칠 수 있다
> **왜 필요한가**: 입사 첫 주에 받는 일이 "벤더 SDK로 이 앱을 보드용으로 빌드해 와", "이 드라이버를 우리 커널에 모듈로 붙여 와"다. 면접에서는 "Makefile에서 헤더 의존성을 어떻게 처리하나요?", "크로스 컴파일할 때 CMake에 무엇을 알려 줘야 하나요?"가 나온다
> **Don 기준 난이도**: SSD 펌웨어 빌드용 Makefile·링커 스크립트는 이미 써 봤다 / Linux 생태계의 "메타 빌드 시스템"(CMake·Meson·autotools)과 sysroot·pkg-config로 호스트와 타깃을 분리하는 방식, 커널 전용 Kbuild가 새로 배울 부분
> **선행 노트**: B1(크로스 툴체인·triplet·sysroot), B3(ELF·링킹) — 없어도 읽을 수 있지만 같이 보면 좋다

---

## 0. 큰 그림 — 이게 왜 필요한가

Don이 SSD 펌웨어를 빌드하던 장면을 떠올려 보자. 회사 레포에는 Makefile(또는 그것을 감싼 사내 스크립트) 하나가 있고, `make`를 치면 ARM 크로스 컴파일러가 수백 개 `.c`를 컴파일해서 링커 스크립트대로 하나의 바이너리 이미지를 만든다. 외부 라이브러리는 거의 없고, 있어도 소스째 레포에 들어 있다. 빌드 시스템은 "우리 회사 것" 하나뿐이다.

Embedded Linux는 사정이 다르다. 보드에 올라가는 rootfs 하나에 **수백 개의 오픈소스 패키지**가 들어가고, 각 패키지는 작성자가 고른 빌드 시스템을 쓴다. BusyBox와 커널은 Make + Kconfig, systemd와 GStreamer는 Meson, OpenCV와 ROS 2 패키지는 CMake, 오래된 GNU 도구들(예: `libmodbus`, `can-utils`의 옛 버전)은 autotools다. 엔지니어는 이 모두를 **같은 크로스 툴체인과 같은 sysroot**로 빌드할 줄 알아야 하고, 그걸 수백 번 자동으로 해 주는 것이 Buildroot와 Yocto다(K3, K4).

```svg
<svg viewBox="0 0 680 350" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="14" width="640" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="39" text-anchor="middle" font-size="13">Buildroot / Yocto — 패키지마다 맞는 빌드 시스템을 순서대로 호출 (K3 · K4)</text>
  <line x1="270" y1="54" x2="270" y2="84" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="450" y1="54" x2="450" y2="84" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="85" y="102" text-anchor="middle" font-size="12">입력</text>
  <text x="270" y="102" text-anchor="middle" font-size="12">설정 단계 (configure)</text>
  <text x="450" y="102" text-anchor="middle" font-size="12">빌드 실행</text>
  <text x="595" y="102" text-anchor="middle" font-size="12">산출물</text>
  <rect x="20" y="112" width="130" height="44" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="85" y="139" text-anchor="middle" font-size="13">앱 소스 .c .h</text>
  <rect x="170" y="112" width="200" height="44" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="270" y="139" text-anchor="middle" font-size="12">CMake / Meson / autotools</text>
  <rect x="390" y="112" width="120" height="44" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="450" y="139" text-anchor="middle" font-size="12">Ninja / Make</text>
  <rect x="530" y="112" width="130" height="44" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="595" y="139" text-anchor="middle" font-size="13">ELF 실행 파일</text>
  <rect x="20" y="172" width="130" height="44" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="85" y="199" text-anchor="middle" font-size="13">드라이버 .c</text>
  <rect x="170" y="172" width="200" height="44" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="270" y="199" text-anchor="middle" font-size="12">Kconfig → .config</text>
  <rect x="390" y="172" width="120" height="44" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="450" y="199" text-anchor="middle" font-size="12">Kbuild (make)</text>
  <rect x="530" y="172" width="130" height="44" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="595" y="199" text-anchor="middle" font-size="13">.ko 모듈</text>
  <rect x="20" y="232" width="130" height="44" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="85" y="259" text-anchor="middle" font-size="13">커널 · U-Boot</text>
  <rect x="170" y="232" width="200" height="44" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="270" y="259" text-anchor="middle" font-size="12">make xxx_defconfig</text>
  <rect x="390" y="232" width="120" height="44" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="450" y="259" text-anchor="middle" font-size="12">make -jN</text>
  <rect x="530" y="232" width="130" height="44" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="595" y="259" text-anchor="middle" font-size="13">Image · DTB</text>
  <line x1="150" y1="134" x2="166" y2="134" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="370" y1="134" x2="386" y2="134" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="510" y1="134" x2="526" y2="134" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="150" y1="194" x2="166" y2="194" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="370" y1="194" x2="386" y2="194" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="510" y1="194" x2="526" y2="194" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="150" y1="254" x2="166" y2="254" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="370" y1="254" x2="386" y2="254" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="510" y1="254" x2="526" y2="254" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="130" y="296" width="530" height="40" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="395" y="321" text-anchor="middle" font-size="12">공통 바닥: 크로스 툴체인 + sysroot + pkg-config 경로 (B1 · B2 · B5)</text>
</svg>
```
그림 1 — Embedded Linux 빌드의 세 갈래. 앱은 메타 빌드 시스템(CMake·Meson·autotools)이 Ninja/Make 파일을 생성하고, 드라이버와 커널은 Kconfig + Kbuild가 맡는다. 모두 같은 툴체인과 sysroot 위에 서 있고, Buildroot/Yocto가 이 전체를 호출한다.

이 노트에서 다루는 것을 한 줄씩 정리하면 다음과 같다.

| 빌드 시스템 | 한 줄 정의 | Embedded Linux에서 만나는 곳 |
|---|---|---|
| GNU Make | 타깃·prerequisite·레시피 규칙과 파일 타임스탬프로 "다시 만들 것"을 정하는 실행기 | 커널, U-Boot, BusyBox, 작은 사내 데몬, 모든 것의 밑바닥 |
| Kbuild / Kconfig | 커널 전용 Make 프레임워크 + 설정 언어 | 커널 이미지, 드라이버, out-of-tree 모듈 (E2) |
| CMake | `CMakeLists.txt`를 읽어 Ninja/Make 파일을 **생성**하는 메타 빌드 시스템 | OpenCV, ROS 2, 대부분의 C++ 프로젝트, 벤더 SDK 예제 |
| Meson + Ninja | Python 비슷한 선언형 언어 + 빠른 Ninja 백엔드 | systemd, GStreamer, libcamera, Mesa |
| autotools | `configure` 셸 스크립트가 환경을 검사해 Makefile을 생성 | 오래된 GNU/오픈소스 라이브러리 |
| pkg-config | 라이브러리의 컴파일·링크 플래그를 알려 주는 조회 도구 | 위 네 가지 모두가 의존성을 찾을 때 |

> 실행 환경: 모든 출력은 Lima VM `elinux`(aarch64, `uname -r` = `7.0.0-34-generic`, 4 vCPU, gcc 15.2)에서 실제로 돌린 결과다. 도구 버전: GNU Make 4.4.1, CMake 4.2.3, Meson 1.10.1, Ninja 1.13.2, pkg-config(pkgconf) 2.5.1, autoconf 2.72, automake 1.18.1, qemu-arm 10.2.1. VM 자체가 aarch64이므로 "크로스" 타깃은 32-bit ARM(`arm-linux-gnueabihf`)과 RISC-V를 쓴다. 해시·PID·시간은 실행마다 다르다.

---

## 1. Make 기초 — 규칙, prerequisite, 타임스탬프

### 1.1 직관 — "결과물이 재료보다 오래됐으면 다시 만든다"

Make의 핵심 아이디어는 딱 하나다. **타깃 파일의 수정 시각(mtime)이 재료(prerequisite) 중 하나보다 오래됐거나, 타깃이 아예 없으면 레시피를 실행한다.** 그 외의 모든 기능(변수, 패턴 규칙, 함수)은 이 규칙을 짧게 쓰기 위한 문법이다.

Don에게 익숙한 말로 바꾸면 Make는 **의존성 그래프(DAG)를 DFS 후위 순회(post-order)하면서 "dirty" 노드만 다시 계산하는 스케줄러**다. 펌웨어에서 "NAND 매핑 테이블 중 dirty 비트가 선 블록만 flush"하는 것과 같은 발상이다. dirty 판정 기준이 "mtime 비교"일 뿐이다.

### 1.2 규칙의 문법

```text
target: prerequisite1 prerequisite2 ...
<TAB>recipe line 1
<TAB>recipe line 2
```

- **target**: 만들 파일 이름 (보통 `.o`, 실행 파일).
- **prerequisite**: target을 만드는 데 필요한 파일. 이것들이 먼저 최신이 되도록 Make가 재귀적으로 처리한다.
- **recipe**: 셸 명령. 반드시 **TAB 문자**로 시작한다 (스페이스면 `missing separator` 에러).
- 레시피의 각 줄은 **별도의 셸**에서 실행된다. 그래서 `cd dir` 다음 줄에서는 원래 디렉터리로 돌아와 있다 (`cd dir && cmd`로 한 줄에 써야 한다).

### 1.3 예제 ex1 — 타임스탬프로 다시 빌드할 것을 고른다

무엇을 확인하나: 처음 빌드, 아무것도 안 바꾼 빌드, `.c` 하나 수정, 헤더 수정 각각에서 Make가 어떤 명령만 다시 돌리는지 본다.

```text
# ex1: 규칙 · prerequisite · 타임스탬프 · phony
CC      := gcc
CFLAGS  := -std=gnu11 -Wall -Wextra -O2

imu_app: main.o imu.o
	$(CC) $^ -o $@

main.o: main.c imu.h
	$(CC) $(CFLAGS) -c main.c -o main.o

imu.o: imu.c imu.h
	$(CC) $(CFLAGS) -c imu.c -o imu.o

.PHONY: clean
clean:
	rm -f imu_app *.o
```

`imu.h`에는 `#define IMU_ODR_HZ 400`과 `int imu_read_accel(int axis);`, `imu.c`는 `return 1000 + axis;`, `main.c`는 둘을 출력한다.

```sh
limactl shell elinux -- bash -lc 'cp -r /Users/donh/.elinux_scratch/B4/ex1 /tmp/B4/ && cd /tmp/B4/ex1 &&
  make; ./imu_app; make; touch imu.c; make; touch imu.h; make -n; make --trace'
# 실제 실행에서는 각 단계 앞에 echo "### ..." 구분 줄을 넣었다 (아래 출력의 ### 줄). 다른 예제도 같다.
```

```text
### 1st make
gcc -std=gnu11 -Wall -Wextra -O2 -c main.c -o main.o
gcc -std=gnu11 -Wall -Wextra -O2 -c imu.c -o imu.o
gcc main.o imu.o -o imu_app
ODR=400 Hz, ax=1000
### 2nd make
make: 'imu_app' is up to date.
### touch imu.c
gcc -std=gnu11 -Wall -Wextra -O2 -c imu.c -o imu.o
gcc main.o imu.o -o imu_app
### make -n after touch imu.h
gcc -std=gnu11 -Wall -Wextra -O2 -c main.c -o main.o
gcc -std=gnu11 -Wall -Wextra -O2 -c imu.c -o imu.o
gcc main.o imu.o -o imu_app
### --trace
Makefile:9: update target 'main.o' due to: imu.h
gcc -std=gnu11 -Wall -Wextra -O2 -c main.c -o main.o
Makefile:12: update target 'imu.o' due to: imu.h
gcc -std=gnu11 -Wall -Wextra -O2 -c imu.c -o imu.o
Makefile:6: update target 'imu_app' due to: main.o imu.o
gcc main.o imu.o -o imu_app
```

출력에서 볼 것: `imu.c`만 건드리면 `main.o`는 다시 컴파일하지 않는다. `make -n`(dry-run)은 **실행하지 않고 실행할 명령만** 보여 준다. `make --trace`(GNU Make 4.0+)는 **왜** 각 타깃을 다시 만드는지(`due to: imu.h`)까지 알려 준다 — "왜 이게 매번 다시 빌드되지?" 디버깅의 1번 도구다.

```svg
<svg viewBox="0 0 640 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="240" y="20" width="160" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="320" y="45" text-anchor="middle" font-size="13">imu_app (3)</text>
  <rect x="90" y="130" width="160" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="170" y="155" text-anchor="middle" font-size="13">main.o (1)</text>
  <rect x="390" y="130" width="160" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="470" y="155" text-anchor="middle" font-size="13">imu.o (2)</text>
  <rect x="40" y="240" width="110" height="36" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="95" y="263" text-anchor="middle" font-size="13">main.c</text>
  <rect x="265" y="240" width="110" height="36" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="320" y="263" text-anchor="middle" font-size="13">imu.h (touch)</text>
  <rect x="490" y="240" width="110" height="36" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="545" y="263" text-anchor="middle" font-size="13">imu.c</text>
  <line x1="300" y1="60" x2="190" y2="128" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="340" y1="60" x2="450" y2="128" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="150" y1="170" x2="105" y2="238" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="200" y1="170" x2="300" y2="238" stroke="#d0564a" stroke-width="2" marker-end="url(#ah2)"/>
  <line x1="440" y1="170" x2="340" y2="238" stroke="#d0564a" stroke-width="2" marker-end="url(#ah2)"/>
  <line x1="490" y1="170" x2="535" y2="238" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="20" y="306" font-size="12">화살표 = "이 파일이 필요하다". 괄호 숫자 = 레시피가 실행되는 순서 (post-order).</text>
  <text x="20" y="324" font-size="12">imu.h의 mtime이 main.o · imu.o보다 새로우면 두 .o가 dirty → imu_app도 dirty.</text>
</svg>
```
그림 2 — Make가 걷는 의존성 DAG. 루트(`imu_app`)에서 출발해 잎까지 내려간 뒤, 올라오면서 "타깃이 prerequisite보다 오래됐나"를 검사한다. `imu.h`를 건드리면 빨간 간선을 따라 위쪽 노드가 모두 다시 만들어진다.

### 1.4 phony 타깃 — "파일이 아닌 타깃"

`clean`, `all`, `install`, `test` 같은 타깃은 파일을 만들지 않는다. 그런데 Make는 기본적으로 타깃을 **파일 이름**으로 본다. 그래서 우연히 `clean`이라는 파일이 생기면 "이미 최신"이라며 아무것도 안 한다. `.PHONY:`에 적어 두면 Make는 그 이름의 파일을 보지 않고 항상 레시피를 실행한다.

무엇을 확인하나: `clean`이라는 파일이 있을 때 `.PHONY`가 있는 Makefile과 없는 Makefile의 차이.

```text
# nophony.mk — .PHONY 선언 없음
clean:
	rm -f *.o
```

```text
### ex1 Makefile (.PHONY: clean 있음), 파일 clean 존재
rm -f imu_app *.o
### nophony.mk, 파일 clean 존재
make: 'clean' is up to date.
```

출력에서 볼 것: `.PHONY`가 없으면 "clean is up to date" 한 줄로 끝나고 아무것도 지워지지 않는다. 빌드 산출물을 넣는 디렉터리 이름을 `build`, `test`로 짓는 경우가 많아서 실제로 자주 걸린다.

---

## 2. 변수 · 자동 변수 · 패턴 규칙 · 내장 규칙

### 2.1 네 가지 대입 연산자

| 연산자 | 이름 | 언제 전개되나 | 쓰는 곳 |
|---|---|---|---|
| `=` | recursive (지연) | **참조할 때마다** 다시 전개 | 나중에 정의될 변수를 참조해야 할 때 |
| `:=` | simple (즉시) | **정의하는 그 줄에서** 한 번 전개해 고정 | 기본으로 쓸 것. `$(shell ...)`, `$(wildcard ...)` 결과 |
| `?=` | conditional | 변수가 아직 정의 안 됐을 때만 대입 | `CROSS_COMPILE ?=`처럼 "기본값, 밖에서 덮어쓰기 허용" |
| `+=` | append | 기존 값 뒤에 붙임 (원래 변수의 flavor를 따름) | `CFLAGS += -Wall` |

무엇을 확인하나: `=`와 `:=`의 전개 시점 차이, 환경 변수와 명령행 변수가 `?=`·`+=`와 만났을 때의 우선순위.

```text
# ex2a: = (recursive, 쓸 때 전개) vs := (simple, 정의할 때 전개) vs ?= (없을 때만)
OPT   = -O0
LAZY  = $(OPT) -g          # 참조할 때마다 다시 전개
NOW  := $(OPT) -g          # 이 줄에서 한 번 전개해서 고정
OPT   = -O2
CROSS_COMPILE ?= aarch64-linux-gnu-
CFLAGS += -Wall
$(info LAZY = [$(LAZY)])
$(info NOW  = [$(NOW)])
$(info CROSS_COMPILE = [$(CROSS_COMPILE)])
$(info CFLAGS = [$(CFLAGS)])
all: ;
```

```sh
make -f vars.mk
CROSS_COMPILE=arm-linux-gnueabihf- make -f vars.mk CFLAGS=-O3
```

```text
### vars
LAZY = [-O2 -g          ]
NOW  = [-O0 -g          ]
CROSS_COMPILE = [aarch64-linux-gnu-]
CFLAGS = [-Wall]
make: 'all' is up to date.
### vars with env/cmdline
LAZY = [-O2 -g          ]
NOW  = [-O0 -g          ]
CROSS_COMPILE = [arm-linux-gnueabihf-]
CFLAGS = [-O3]
make: 'all' is up to date.
```

출력에서 볼 것이 세 가지다.

1. `LAZY`는 나중에 바뀐 `OPT = -O2`를 반영했고, `NOW`는 정의 시점의 `-O0`에 고정됐다.
2. 환경 변수 `CROSS_COMPILE`이 있으면 `?=`는 아무 일도 안 한다. 반면 **명령행 변수 `CFLAGS=-O3`는 Makefile 안의 `+= -Wall`까지 무시**하게 만든다 (명령행 변수가 가장 우선). 그래서 커널·패키지 Makefile은 사용자가 덮어쓰는 `CFLAGS`와 자기가 꼭 넣어야 하는 플래그를 다른 변수(`KBUILD_CFLAGS`, `EXTRA_CFLAGS`, `ccflags-y`)로 나눈다. 꼭 붙여야 하면 `override CFLAGS += -Wall`을 쓴다.
3. **줄 끝 주석 앞의 공백이 값에 들어갔다** (`[-O2 -g          ]`). `DIR := build # 출력 폴더` 같은 줄이 `build `가 되어 `$(DIR)/foo`가 `build /foo`로 깨지는 고전적 버그다. 변수 정의 줄에는 줄 끝 주석을 달지 않는다.

### 2.2 자동 변수와 패턴 규칙

파일이 수백 개면 규칙을 하나씩 쓸 수 없다. **패턴 규칙** `%.o: %.c`는 "어떤 `X.o`든 `X.c`에서 만든다"는 뜻이고, 레시피 안에서는 **자동 변수**로 이번에 매칭된 이름을 가리킨다.

| 자동 변수 | 뜻 | ex2b에서 값 |
|---|---|---|
| `$@` | 지금 만드는 타깃 | `build/imu.o` |
| `$<` | 첫 번째 prerequisite | `src/imu.c` |
| `$^` | 모든 prerequisite (중복 제거) | `build/gnss.o build/imu.o ...` |
| `$?` | 타깃보다 새로운 prerequisite만 | (변경된 것만) |
| `$*` | 패턴의 `%`에 매칭된 부분(stem) | `imu` |
| `$(@D)` / `$(@F)` | `$@`의 디렉터리 / 파일 부분 | `build` / `imu.o` |

무엇을 확인하나: `wildcard`·`patsubst`로 소스 목록을 만들고, 패턴 규칙 하나로 모든 `.o`를 빌드하며 자동 변수 값을 직접 찍어 본다. `| build`는 **order-only prerequisite**로, "디렉터리가 존재하기만 하면 되고 mtime은 비교하지 않는다"는 뜻이다(디렉터리 mtime은 안에 파일이 생길 때마다 바뀌어서 일반 prerequisite로 두면 매번 다시 빌드된다).

```text
# ex2b: 패턴 규칙 + 자동 변수
SRCS := $(wildcard src/*.c)
OBJS := $(patsubst src/%.c,build/%.o,$(SRCS))

build/app: $(OBJS)
	@echo "LINK  \$$@=$@  \$$^=$^"
	@$(CC) $^ -o $@

build/%.o: src/%.c src/common.h | build
	@echo "CC    \$$@=$@  \$$<=$<  \$$*=$*"
	@$(CC) -Wall -Wextra -O2 -c $< -o $@

build:
	mkdir -p $@
```

```text
mkdir -p build
CC    $@=build/gnss.o  $<=src/gnss.c  $*=gnss
CC    $@=build/imu.o  $<=src/imu.c  $*=imu
CC    $@=build/lidar.o  $<=src/lidar.c  $*=lidar
CC    $@=build/main.o  $<=src/main.c  $*=main
LINK  $@=build/app  $^=build/gnss.o build/imu.o build/lidar.o build/main.o
```

출력에서 볼 것: 레시피 앞의 `@`는 명령 자체를 출력하지 않게 한다(커널의 `CC [M] foo.o` 같은 짧은 출력이 이 방식이다). `$$`는 셸에 `$` 한 글자를 넘기는 이스케이프다.

### 2.3 내장 규칙 — Makefile이 없어도 빌드된다

Make에는 `%.o: %.c`, `%: %.c` 같은 **내장 규칙(built-in rules)**과 `CC`, `CFLAGS`, `LDLIBS` 같은 기본 변수가 이미 들어 있다. `make -p`(print data base)로 전부 볼 수 있다.

무엇을 확인하나: Makefile 없이 `make hello`가 되는 이유를 `make -p`로 확인한다.

```sh
cd /tmp/B4/ex3 && ls && make hello CFLAGS="-O2 -Wall" && ./hello
make -p -f /dev/null 2>/dev/null | grep -E "^(COMPILE\.c|LINK\.o|LINK\.c|OUTPUT_OPTION|CC) "
make -p -f /dev/null 2>/dev/null | grep -A3 "^%: %\.c$"
make -p -f /dev/null 2>/dev/null | grep -A3 "^%\.o: %\.c$"
rm hello; make -r hello
```

```text
hello.c
cc -O2 -Wall    hello.c   -o hello
no Makefile!
LINK.o = $(CC) $(LDFLAGS) $(TARGET_ARCH)
COMPILE.c = $(CC) $(CFLAGS) $(CPPFLAGS) $(TARGET_ARCH) -c
LINK.c = $(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) $(TARGET_ARCH)
OUTPUT_OPTION = -o $@
CC = cc
%: %.c
#  recipe to execute (built-in):
	$(LINK.c) $^ $(LOADLIBES) $(LDLIBS) -o $@

%.o: %.c
#  recipe to execute (built-in):
	$(COMPILE.c) $(OUTPUT_OPTION) $<

make: *** No rule to make target 'hello'.  Stop.
```

출력에서 볼 것: 내장 규칙 덕분에 `CC=arm-linux-gnueabihf-gcc make hello`만으로도 크로스 빌드가 된다. 관례적인 변수 역할도 여기서 나온다 — `CPPFLAGS`는 전처리기(`-I`, `-D`), `CFLAGS`는 컴파일러, `LDFLAGS`는 링커 옵션(`-L`, `-Wl,...`), `LDLIBS`는 라이브러리(`-lm`). `-r`(`--no-builtin-rules`)을 주면 내장 규칙이 꺼져서 같은 명령이 실패한다. 커널 Kbuild는 예측 가능성을 위해 내장 규칙과 내장 변수를 꺼 놓고(`MAKEFLAGS += -rR`) 모든 규칙을 직접 정의한다.

---

## 3. 헤더 의존성 — "헤더를 바꿨는데 다시 빌드가 안 된다"

### 3.1 문제

Make는 Makefile에 **적힌** 의존성만 안다. `.c`가 `#include`하는 헤더는 컴파일러만 알고 Make는 모른다. 그래서 패턴 규칙을 `%.o: %.c`로만 써 두면, 헤더를 바꿔도 그 헤더를 쓰는 `.o`들이 다시 컴파일되지 않는다. 결과는 단순한 "옛 값"이 아니라 **구조체 레이아웃이 서로 다른 오브젝트가 한 바이너리에 섞이는 ABI 불일치**다. 펌웨어로 치면 공유 메모리 구조체의 정의를 바꿨는데 한쪽 코어 펌웨어만 다시 빌드된 상황이다.

### 3.2 예제 ex4 — 버그 재현과 `-MMD -MP` 수정

무엇을 확인하나: `struct imu_sample` 앞에 타임스탬프 필드를 추가하고 `imu.c`도 함께 고쳤을 때, 헤더 의존성이 없는 Makefile은 `imu.o`만 다시 빌드한다. `main.o`는 옛 12바이트 구조체를 스택에 잡고, 새 `imu.o`는 24바이트 구조체에 쓴다.

```c
/* imu.h v1 */  struct imu_sample { int ax, ay, az; };
/* imu.h v2 */  struct imu_sample { long long ts_ns; int ax, ay, az; };  /* 타임스탬프 필드 추가 */

/* imu.c */
#include "imu.h"
void imu_read(struct imu_sample *s) { s->ax = 11; s->ay = 22; s->az = 33; }

/* main.c */
#include <stdio.h>
#include "imu.h"
int main(void)
{
    struct imu_sample s = {0};
    imu_read(&s);
    printf("sizeof=%zu ax=%d ay=%d az=%d\n", sizeof s, s.ax, s.ay, s.az);
    return 0;
}
```

두 Makefile의 차이는 세 줄이다.

```text
# Makefile.bad — 헤더 의존성이 없는 Makefile (흔한 실수)
CFLAGS := -std=gnu11 -Wall -Wextra -O2
app: main.o imu.o
	$(CC) $^ -o $@
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@
```

```text
# Makefile.good — -MMD: .o 만들면서 .d(헤더 의존성) 생성, -MP: 헤더마다 빈 phony 규칙 추가
CFLAGS := -std=gnu11 -Wall -Wextra -O2 -MMD -MP
OBJS   := main.o imu.o
app: $(OBJS)
	$(CC) $^ -o $@
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@
-include $(OBJS:.o=.d)
```

```sh
cp imu.h.v1 imu.h; make -s -f Makefile.bad && ./app
sleep 1; cp imu.h.v2 imu.h; touch imu.c
make -f Makefile.bad; ./app; echo "exit=$?"
```

```text
sizeof=12 ax=11 ay=22 az=33
cc -std=gnu11 -Wall -Wextra -O2 -c imu.c -o imu.o
cc main.o imu.o -o app
*** stack smashing detected ***: terminated
exit=134
bash: line 1: 427581 Aborted                    ./app
```

같은 순서를 `Makefile.good`으로 하면:

```text
sizeof=12 ax=11 ay=22 az=33
--- after header change:
cc -std=gnu11 -Wall -Wextra -O2 -MMD -MP -c main.c -o main.o
cc -std=gnu11 -Wall -Wextra -O2 -MMD -MP -c imu.c -o imu.o
cc main.o imu.o -o app
sizeof=24 ax=11 ay=22 az=33
===== main.d
main.o: main.c imu.h
imu.h:
```

출력에서 볼 것: 나쁜 Makefile은 경고 하나 없이 빌드에 성공하고, 실행하면 새 `imu_read()`가 `s->az`를 오프셋 16에 써서 `main`의 12바이트 스택 변수를 넘어 stack canary를 깨뜨린다(SIGABRT, exit 134 = 128 + 6). 좋은 Makefile은 컴파일러가 만든 `main.d`(`main.o: main.c imu.h`)를 `-include`로 읽어서 헤더 변경을 안다. "가끔 이상하게 죽는데 `make clean` 하면 고쳐진다"는 증상의 원인은 대개 이것이다.

### 3.3 `-M` 계열 플래그 정리

| 플래그 | 뜻 |
|---|---|
| `-M` | 의존성 규칙만 출력하고 컴파일은 안 함 (시스템 헤더 포함) |
| `-MM` | `-M`과 같되 시스템 헤더(`<stdio.h>`) 제외 |
| `-MD` | 컴파일하면서 부산물로 `.d` 파일 생성 (시스템 헤더 포함) |
| `-MMD` | `-MD`와 같되 시스템 헤더 제외 — 가장 많이 쓴다 |
| `-MP` | 각 헤더에 대해 레시피 없는 빈 규칙(`imu.h:`)을 추가 |
| `-MF file` | `.d` 파일 이름 지정 |
| `-MT target` | `.d` 안의 타깃 이름 지정 (CMake/Meson이 `-MT`·`-MF`를 같이 쓴다) |

`.d` 파일을 컴파일과 **동시에** 만드는 것이 핵심이다. 첫 빌드에는 `.d`가 없지만 어차피 `.o`도 없어서 전부 컴파일되므로 문제없다. `-include`의 `-`는 "파일이 없어도 에러 내지 말라"는 뜻이다.

### 3.4 `-MP`는 왜 필요한가 — 헤더를 지웠을 때

무엇을 확인하나: `imu.c`에서 `#include "old.h"`를 지우고 `old.h` 파일도 삭제했을 때, `-MP` 유무에 따른 차이.

```text
===== CFLAGS has: -MMD -MP
imu.o: imu.c old.h
old.h:
--- after deleting old.h:
cc -std=gnu11 -Wall -Wextra -O2 -MMD -MP -c imu.c -o imu.o
cc main.o imu.o -o app
===== CFLAGS has: -MMD
imu.o: imu.c old.h
--- after deleting old.h:
make: *** No rule to make target 'old.h', needed by 'imu.o'.  Stop.
```

출력에서 볼 것: 옛 `.d`가 아직 "imu.o는 old.h가 필요하다"고 말하고 있는데 `old.h`가 없으니 Make는 그걸 만들 규칙을 찾다 멈춘다. `-MP`가 만든 빈 규칙 `old.h:`가 있으면 "만들 방법은 있고 결과는 항상 새것"으로 취급되어 `imu.o`가 다시 컴파일되고, 새 `.d`에서는 `old.h`가 사라진다. 헤더를 지우거나 이름을 바꾸는 리팩터링 뒤 "No rule to make target xxx.h"가 나오면 `-MP` 누락이다.

---

## 4. 병렬 빌드 `-j` 와 누락 의존성 race

### 4.1 `-j`가 하는 일

`make -jN`은 DAG에서 **서로 의존하지 않는 노드를 최대 N개 동시에** 실행한다. 직렬(`-j1`)일 때 Make는 prerequisite를 **왼쪽부터 차례대로** 처리하기 때문에, 의존성이 빠져 있어도 "우연히 순서가 맞아서" 동작하는 경우가 많다. `-j`는 그 우연을 없앤다. RTOS에서 우선순위 덕분에 우연히 지켜지던 실행 순서가 멀티코어로 옮기자 깨지는 것과 같다.

### 4.2 예제 ex5 — 생성 헤더 의존성 누락

무엇을 확인하나: 빌드 중에 만들어지는 `version.h`(펌웨어 빌드에서 `git describe`로 버전 헤더를 생성하는 흔한 패턴)를 `main.o`의 prerequisite에서 빠뜨리면 `-j1`은 되고 `-j4`는 깨진다.

```text
# ex5: 생성 헤더(version.h)에 대한 의존성 누락 → -j에서만 깨진다
all: version.h app            # 직렬(-j1)이면 왼쪽부터 → 우연히 동작

version.h:
	@sleep 0.3                # git describe 같은 느린 생성 스크립트 흉내
	@echo '#define FW_VERSION "1.4.2"' > $@

app: main.o
	$(CC) $^ -o $@

main.o: main.c                # 버그: version.h가 빠져 있다
	$(CC) -Wall -c $< -o $@

clean: ; rm -f app *.o version.h
.PHONY: all clean
```

```text
### -j1
cc -Wall -c main.c -o main.o
cc main.o -o app
1.4.2
### -j4
cc -Wall -c main.c -o main.o
main.c:2:10: fatal error: version.h: No such file or directory
    2 | #include "version.h"
      |          ^~~~~~~~~~~
compilation terminated.
make: *** [Makefile:12: main.o] Error 1
make: *** Waiting for unfinished jobs....
exit=2
### fixed: main.o: main.c version.h, -j4
cc -Wall -c main.c -o main.o
cc main.o -o app
1.4.2
```

출력에서 볼 것: `-j4`에서는 `version.h` 생성과 `main.o` 컴파일이 동시에 시작돼서 컴파일러가 아직 없는 헤더를 찾는다. 고치는 법은 "순서를 맞추는 것"이 아니라 **진짜 의존성을 적는 것**(`main.o: main.c version.h`)이다. 생성 헤더는 `-MMD`로도 해결되지 않는다는 점에 주의 — `.d`는 첫 컴파일이 **성공한 뒤에야** 생기므로 첫 빌드에서는 아무 도움이 안 된다. 실무 증상은 "CI에서 가끔 빌드가 깨지는데 다시 돌리면 된다"다.

### 4.3 재귀 make vs 비재귀 make

큰 프로젝트를 디렉터리별로 나눌 때 두 방식이 있다.

- **재귀(recursive) make**: 최상위 Makefile이 `$(MAKE) -C subdir`로 하위 디렉터리마다 **별도의 make 프로세스**를 띄운다. 각 make는 자기 디렉터리의 DAG만 안다.
- **비재귀(non-recursive) make**: 최상위 make 하나가 모든 하위 디렉터리의 규칙 조각을 `include`해서 **전체 DAG 하나**를 만든다.

Peter Miller의 1997년 논문 "Recursive Make Considered Harmful"이 지적한 문제는, 재귀 make에서는 디렉터리 경계를 넘는 의존성이 보이지 않는다는 것이다.

무엇을 확인하나: `app/`이 `../lib/libsensor.a`에 의존하는데 최상위에서 `lib`와 `app`을 독립 phony 타깃으로 두면 `-j2`에서 깨진다.

```text
# ex14: 재귀 make — 하위 디렉터리마다 별도의 make 프로세스
SUBDIRS := lib app
all: $(SUBDIRS)
$(SUBDIRS):
	$(MAKE) -C $@
clean:
	for d in $(SUBDIRS); do $(MAKE) -C $$d clean; done
.PHONY: all clean $(SUBDIRS)
```

`lib/Makefile`은 `sensor.o`로 `libsensor.a`를 만들고(느린 단계를 흉내 내려고 `sleep 0.5`), `app/Makefile`은 `app: main.o ../lib/libsensor.a`다.

```text
### -j1
make -C lib
cc    -c -o sensor.o sensor.c
ar rcs libsensor.a sensor.o
make -C app
cc    -c -o main.o main.c
cc main.o ../lib/libsensor.a -o app
3 sensors
### -j2
make -C lib
make -C app
cc    -c -o sensor.o sensor.c
cc    -c -o main.o main.c
make[1]: *** No rule to make target '../lib/libsensor.a', needed by 'app'.  Stop.
make[1]: *** Waiting for unfinished jobs....
make: *** [Makefile:5: app] Error 2
make: *** Waiting for unfinished jobs....
ar rcs libsensor.a sensor.o
exit=2
### fix: app: lib
make -C lib
cc    -c -o sensor.o sensor.c
ar rcs libsensor.a sensor.o
make -C app
cc    -c -o main.o main.c
cc main.o ../lib/libsensor.a -o app
3 sensors
```

출력에서 볼 것: `app` 쪽 make는 `../lib/libsensor.a`를 만드는 규칙을 모르기 때문에 "No rule to make target"으로 멈춘다(그 직후에 lib 쪽 `ar`이 끝나는 것이 보인다). 디렉터리 간 순서(`app: lib`)를 최상위에 적어야 한다. 그리고 재귀 make는 `lib` 안의 파일이 바뀌었는지 `app` 쪽 make가 정확히 판단하지 못하는 문제도 남는다.

커널 Kbuild는 겉보기에는 디렉터리마다 `$(MAKE) -f scripts/Makefile.build obj=<dir>`로 내려가는 재귀 구조지만, 디렉터리 간 의존(링크 순서, `modpost`)을 최상위에서 단계별로 묶어 관리한다. CMake·Meson은 **프로젝트 전체를 하나의 `build.ninja`로 생성**해서 이 문제를 구조적으로 피한다 — 비재귀가 기본이다.

### 4.4 make 디버깅 도구 모음

| 명령 | 언제 쓰나 |
|---|---|
| `make -n` | 실제로 실행하지 않고 어떤 명령이 돌지 확인 |
| `make --trace` | 각 타깃이 **왜** 다시 만들어지는지 (`due to: ...`) |
| `make -d` / `--debug=b` | 의존성 판단 과정 전체 (매우 길다) |
| `make -p` | 변수·규칙 데이터베이스 출력 (내장 규칙 포함) |
| `make V=1` | 커널·CMake(Make 백엔드) 등에서 축약 출력 대신 전체 명령 |
| `make -k` | 에러가 나도 다른 독립 타깃은 계속 빌드 (에러 목록 한 번에 보기) |
| `make -B` | 모든 타깃을 무조건 다시 빌드 |
| `$(info ...)` / `$(warning ...)` | Makefile 파싱 중에 변수 값 찍기 |

---

## 5. 측정 — `-j` 스케일링과 증분 빌드 비용

말로만 하지 않고 숫자로 보자. Python 스크립트로 **200개 모듈**(각 `.c` 약 43줄, 함수 40개짜리) + `main.c`의 프로젝트를 생성했다. 모든 `mod_NNN.c`는 공통 헤더 `common.h`와 자기 헤더 `mod_NNN.h`를 include하고, `main.c`는 200개 `mod_NNN.h`를 모두 include한다. Makefile은 `DEPS=1`이면 `-MMD -MP` + `-include`, `DEPS=0`이면 헤더 의존성 없음이다.

```text
DEPS   ?= 1
SRCS   := $(wildcard src/*.c)
OBJS   := $(SRCS:src/%.c=obj/%.o)
CFLAGS := -std=gnu11 -Wall -Wextra -O2
ifeq ($(DEPS),1)
CFLAGS += -MMD -MP
endif
app: $(OBJS)
	$(CC) $^ -o $@
obj/%.o: src/%.c | obj
	$(CC) $(CFLAGS) -c $< -o $@
obj: ; mkdir -p $@
clean: ; rm -rf obj app
.PHONY: clean
ifeq ($(DEPS),1)
-include $(OBJS:.o=.d)
endif
```

측정 스크립트(핵심부):

```sh
t() { local s=$(date +%s.%N); "$@" > last.log 2>&1; local e=$(date +%s.%N); echo "$e - $s" | bc; }
for j in 1 2 4 8; do for r in 1 2 3; do make -s clean; t make -j$j; done; done
# 증분: 전체 빌드 후 touch src/common.h → make -j4, touch src/mod_017.h → make -j4, 그대로 make -j4
```

VM은 4 vCPU이고 **다른 에이전트가 같은 VM에서 동시에 작업 중**이었다(측정 시작 load average 0.56, 끝 3.15). 그래서 각 조건을 3번 돌려 **중앙값**을 쓴다.

```text
 08:01:33 up 1 day, 17:12,  1 user,  load average: 0.56, 0.43, 0.20
full j=1 run=1 9.898609918
full j=1 run=2 9.673080792
full j=1 run=3 11.729036648
full j=2 run=1 5.055227634
full j=2 run=2 5.191917620
full j=2 run=3 5.131533539
full j=4 run=1 4.038987975
full j=4 run=2 3.883592591
full j=4 run=3 3.910740292
full j=8 run=1 3.393282594
full j=8 run=2 3.448346043
full j=8 run=3 3.442663243
DEPS=1 touch common.h run=1 2.649849006 s, cc lines=200
DEPS=1 touch mod_017.h run=1 .131607007 s, cc lines=2
DEPS=1 noop run=1 .033868760 s
DEPS=1 touch common.h run=2 3.013445123 s, cc lines=200
DEPS=1 touch mod_017.h run=2 .135988546 s, cc lines=2
DEPS=1 noop run=2 .037267332 s
DEPS=1 touch common.h run=3 3.945311267 s, cc lines=200
DEPS=1 touch mod_017.h run=3 .114596772 s, cc lines=2
DEPS=1 noop run=3 .036818870 s
DEPS=0 touch common.h run=1 .007077897 s, cc lines=0
DEPS=0 touch mod_017.h run=1 .005947137 s, cc lines=0
DEPS=0 noop run=1 .005775302 s
DEPS=0 touch common.h run=2 .006873436 s, cc lines=0
DEPS=0 touch mod_017.h run=2 .006146680 s, cc lines=0
DEPS=0 noop run=2 .005771885 s
DEPS=0 touch common.h run=3 .006188430 s, cc lines=0
DEPS=0 touch mod_017.h run=3 .005959469 s, cc lines=0
DEPS=0 noop run=3 .005957428 s
 08:03:12 up 1 day, 17:13,  1 user,  load average: 3.15, 1.37, 0.56
```

```svg
<svg viewBox="0 0 640 310" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="22" text-anchor="middle" font-size="14" font-weight="bold">전체 빌드 시간 vs make -jN (201개 .c, 중앙값 3회)</text>
  <line x1="80" y1="250" x2="600" y2="250" stroke="currentColor"/>
  <line x1="80" y1="40" x2="80" y2="250" stroke="currentColor"/>
  <text x="72" y="254" text-anchor="end" font-size="12">0</text>
  <text x="72" y="201" text-anchor="end" font-size="12">3</text>
  <text x="72" y="149" text-anchor="end" font-size="12">6</text>
  <text x="72" y="96" text-anchor="end" font-size="12">9</text>
  <text x="72" y="44" text-anchor="end" font-size="12">12</text>
  <line x1="80" y1="197.5" x2="600" y2="197.5" stroke="#888" stroke-opacity="0.3"/>
  <line x1="80" y1="145" x2="600" y2="145" stroke="#888" stroke-opacity="0.3"/>
  <line x1="80" y1="92.5" x2="600" y2="92.5" stroke="#888" stroke-opacity="0.3"/>
  <text x="30" y="150" text-anchor="middle" font-size="12" transform="rotate(-90 30 150)">초 (s)</text>
  <rect x="110" y="76.8" width="70" height="173.2" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
  <rect x="240" y="160.2" width="70" height="89.8" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
  <rect x="370" y="181.6" width="70" height="68.4" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
  <rect x="500" y="189.8" width="70" height="60.2" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
  <line x1="230" y1="163.4" x2="320" y2="163.4" stroke="#e08a3c" stroke-width="2" stroke-dasharray="5 3"/>
  <line x1="360" y1="206.7" x2="450" y2="206.7" stroke="#e08a3c" stroke-width="2" stroke-dasharray="5 3"/>
  <line x1="490" y1="228.3" x2="580" y2="228.3" stroke="#e08a3c" stroke-width="2" stroke-dasharray="5 3"/>
  <text x="145" y="70" text-anchor="middle" font-size="12">9.90 s</text>
  <text x="275" y="154" text-anchor="middle" font-size="12">5.13 s (1.93x)</text>
  <text x="405" y="175" text-anchor="middle" font-size="12">3.91 s (2.53x)</text>
  <text x="535" y="183" text-anchor="middle" font-size="12">3.44 s (2.88x)</text>
  <text x="145" y="270" text-anchor="middle" font-size="13">-j1</text>
  <text x="275" y="270" text-anchor="middle" font-size="13">-j2</text>
  <text x="405" y="270" text-anchor="middle" font-size="13">-j4</text>
  <text x="535" y="270" text-anchor="middle" font-size="13">-j8</text>
  <line x1="110" y1="294" x2="140" y2="294" stroke="#e08a3c" stroke-width="2" stroke-dasharray="5 3"/>
  <text x="148" y="298" font-size="12">이상적 선형 확장 (9.90 / N)</text>
  <rect x="360" y="286" width="14" height="12" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="380" y="298" font-size="12">측정값 (4 vCPU, 다른 작업과 공유)</text>
</svg>
```
그림 3 — 같은 200 모듈 프로젝트의 전체 빌드 시간. `-j2`까지는 거의 선형(1.93배)이지만 `-j4`는 2.53배에 그친다. 4 vCPU를 다른 에이전트와 나눠 쓰고 있었고, 마지막 링크 한 번은 병렬화되지 않기 때문이다. `-j8`이 코어 수보다 많은데도 조금 더 빨라진 것은 한 컴파일이 파일 I/O·프로세스 생성을 기다리는 사이 다른 작업이 CPU를 채우기 때문이다.

```svg
<svg viewBox="0 0 660 300" xmlns="http://www.w3.org/2000/svg">
  <text x="330" y="22" text-anchor="middle" font-size="14" font-weight="bold">증분 빌드 시간 (make -j4, 로그 축, 중앙값 3회)</text>
  <line x1="220" y1="230" x2="580" y2="230" stroke="currentColor"/>
  <line x1="220" y1="38" x2="220" y2="230" stroke="currentColor"/>
  <line x1="310" y1="38" x2="310" y2="230" stroke="#888" stroke-opacity="0.3"/>
  <line x1="400" y1="38" x2="400" y2="230" stroke="#888" stroke-opacity="0.3"/>
  <line x1="490" y1="38" x2="490" y2="230" stroke="#888" stroke-opacity="0.3"/>
  <line x1="580" y1="38" x2="580" y2="230" stroke="#888" stroke-opacity="0.3"/>
  <text x="220" y="246" text-anchor="middle" font-size="12">1 ms</text>
  <text x="310" y="246" text-anchor="middle" font-size="12">10 ms</text>
  <text x="400" y="246" text-anchor="middle" font-size="12">100 ms</text>
  <text x="490" y="246" text-anchor="middle" font-size="12">1 s</text>
  <text x="580" y="246" text-anchor="middle" font-size="12">10 s</text>
  <text x="210" y="62" text-anchor="end" font-size="12">-MMD · common.h touch</text>
  <rect x="220" y="48" width="313.1" height="20" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="538" y="62" font-size="12">3.01 s (cc 200)</text>
  <text x="210" y="92" text-anchor="end" font-size="12">-MMD · mod_017.h touch</text>
  <rect x="220" y="78" width="190.7" height="20" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="416" y="92" font-size="12">132 ms (cc 2)</text>
  <text x="210" y="122" text-anchor="end" font-size="12">-MMD · 변경 없음</text>
  <rect x="220" y="108" width="140.9" height="20" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="366" y="122" font-size="12">37 ms (cc 0)</text>
  <text x="210" y="152" text-anchor="end" font-size="12">deps 없음 · common.h touch</text>
  <rect x="220" y="138" width="75.3" height="20" fill="#d0564a" fill-opacity="0.75"/>
  <text x="301" y="152" font-size="12">6.9 ms (cc 0, 틀린 결과)</text>
  <text x="210" y="182" text-anchor="end" font-size="12">deps 없음 · mod_017.h touch</text>
  <rect x="220" y="168" width="69.8" height="20" fill="#d0564a" fill-opacity="0.75"/>
  <text x="296" y="182" font-size="12">6.0 ms (cc 0, 틀린 결과)</text>
  <text x="210" y="212" text-anchor="end" font-size="12">deps 없음 · 변경 없음</text>
  <rect x="220" y="198" width="68.5" height="20" fill="#d0564a" fill-opacity="0.75"/>
  <text x="294" y="212" font-size="12">5.8 ms (cc 0)</text>
  <text x="330" y="276" text-anchor="middle" font-size="12">파랑 = -MMD -MP 의존성 있음 · 빨강 = 헤더 의존성 없음 (빠르지만 다시 빌드해야 할 것을 놓침)</text>
</svg>
```
그림 4 — 헤더 하나를 건드린 뒤의 증분 빌드. 의존성이 있으면 `common.h` 수정은 200개 재컴파일(3.01 s), `mod_017.h` 수정은 그 헤더를 쓰는 `mod_017.c`와 `main.c` 2개만 재컴파일(132 ms)한다. 의존성이 없으면 항상 몇 ms로 끝나지만 **다시 컴파일해야 할 것을 0개** 컴파일한 것이므로 결과물이 틀렸다(§3의 stack smashing).

숫자에서 읽을 것:

- **`-MMD`의 비용은 작다.** 변경 없는 빌드에서 `.d` 201개를 읽고 판단하는 데 37 ms(의존성 없음은 6 ms). 30 ms를 아끼려고 정확성을 버릴 이유가 없다.
- **헤더 설계가 빌드 시간을 정한다.** 모두가 include하는 "god header"(`common.h`) 한 줄 수정 = 전체 재빌드다. 헤더를 잘게 나누고 필요한 것만 include하는 것이 증분 빌드를 빠르게 하는 가장 큰 레버다. 커널이 `#include <linux/kernel.h>` 남용을 줄이려는 이유도 같다.
- **`-j`는 코어 수 근처에서 포화된다.** 관례는 `make -j$(nproc)`, CI에서는 메모리가 허락하면 그보다 조금 크게.

### 5.1 ccache — 같은 컴파일을 다시 하지 않는다

**ccache**는 컴파일러 앞에 끼워 넣는 캐시다. 전처리 입력·컴파일 옵션·컴파일러를 해시해서, 같은 조합이면 저장해 둔 `.o`를 돌려준다. 브랜치를 오가거나 `make clean`을 자주 하는 Yocto/커널 빌드에서 효과가 크다.

무엇을 확인하나: 같은 200 모듈 프로젝트를 `CC="ccache gcc"`로 clean 빌드 두 번.

```sh
export CCACHE_DIR=/tmp/B4/ccache
for r in cold warm; do make -s clean; make -s -j4 CC="ccache gcc"; done; ccache -s
```

```text
cold: 3.957588085 s
warm: .121181997 s
Cacheable calls:    402 / 404 (99.50%)
  Hits:             201 / 402 (50.00%)
    Direct:         201 / 201 (100.0%)
    Preprocessed:     0 / 201 ( 0.00%)
  Misses:           201 / 402 (50.00%)
Uncacheable calls:    2 / 404 ( 0.50%)
Local storage:
  Cache size (GiB): 0.0 / 5.0 ( 0.03%)
```

출력에서 볼 것: 두 번째 clean 빌드는 201개 전부 캐시 hit라서 3.96 s → 0.12 s다. Uncacheable 2개는 링크 호출이다(ccache는 링크를 캐시하지 않는다). 크로스 컴파일에도 그대로 쓴다: `CC="ccache arm-linux-gnueabihf-gcc"`, 커널은 `make CC="ccache aarch64-linux-gnu-gcc"`. Buildroot는 `BR2_CCACHE`, Yocto는 `INHERIT += "ccache"`(또는 sstate 캐시가 더 큰 단위로 같은 일을 한다)로 켠다.

---

## 6. Make로 크로스 컴파일 — `CROSS_COMPILE` 관례

커널, U-Boot, BusyBox, 많은 사내 Makefile이 공유하는 관례가 있다. **툴체인 접두사(prefix)를 변수 하나로 받고, 모든 도구 이름을 거기서 만든다.**

```text
CROSS_COMPILE=arm-linux-gnueabihf-
CC      = $(CROSS_COMPILE)gcc       -> arm-linux-gnueabihf-gcc
LD      = $(CROSS_COMPILE)ld        -> arm-linux-gnueabihf-ld
OBJCOPY = $(CROSS_COMPILE)objcopy   -> arm-linux-gnueabihf-objcopy
STRIP   = $(CROSS_COMPILE)strip     -> arm-linux-gnueabihf-strip
```

접두사 끝의 `-`까지 포함한다는 점이 함정이다(`CROSS_COMPILE=arm-linux-gnueabihf`라고 쓰면 `arm-linux-gnueabihfgcc`를 찾는다). 커널은 여기에 `ARCH=arm64` 같은 아키텍처 변수가 하나 더 필요하다(E2).

### 6.1 예제 ex6 — 한 Makefile로 세 아키텍처

무엇을 확인하나: 같은 소스를 `CROSS_COMPILE`과 출력 디렉터리 `O`만 바꿔 aarch64(네이티브), armhf, riscv64로 빌드하고, `file`과 `qemu-user`로 결과를 검증한다.

```text
# ex6: 커널·U-Boot·BusyBox와 같은 CROSS_COMPILE 관례
CROSS_COMPILE ?=
CC      := $(CROSS_COMPILE)gcc
STRIP   := $(CROSS_COMPILE)strip
CFLAGS  ?= -O2
CFLAGS  += -std=gnu11 -Wall -Wextra
O       ?= build-native

$(O)/sensord: sensord.c | $(O)
	$(CC) $(CFLAGS) $< -o $@
	$(STRIP) $@
$(O): ; mkdir -p $@
```

```c
#include <stdio.h>
#include <sys/utsname.h>
int main(void)
{
    struct utsname u;
    if (uname(&u) != 0) { perror("uname"); return 1; }
    printf("sensord running on machine=%s, sizeof(long)=%zu\n", u.machine, sizeof(long));
    return 0;
}
```

```sh
make
make CROSS_COMPILE=arm-linux-gnueabihf- O=build-armhf
make CROSS_COMPILE=riscv64-linux-gnu- O=build-rv64
file build-*/sensord
./build-native/sensord
qemu-arm -L /usr/arm-linux-gnueabihf build-armhf/sensord
qemu-riscv64 -L /usr/riscv64-linux-gnu build-rv64/sensord
./build-armhf/sensord; echo exit=$?
```

```text
mkdir -p build-native
gcc -O2 -std=gnu11 -Wall -Wextra sensord.c -o build-native/sensord
strip build-native/sensord
mkdir -p build-armhf
arm-linux-gnueabihf-gcc -O2 -std=gnu11 -Wall -Wextra sensord.c -o build-armhf/sensord
arm-linux-gnueabihf-strip build-armhf/sensord
mkdir -p build-rv64
riscv64-linux-gnu-gcc -O2 -std=gnu11 -Wall -Wextra sensord.c -o build-rv64/sensord
riscv64-linux-gnu-strip build-rv64/sensord
build-armhf/sensord:  ELF 32-bit LSB pie executable, ARM, EABI5 version 1 (SYSV), dynamically linked, interpreter /lib/ld-linux-armhf.so.3, BuildID[sha1]=ebddb02bce6f0f874f61b5003b8f08fa4e480d79, for GNU/Linux 3.2.0, stripped
build-native/sensord: ELF 64-bit LSB pie executable, ARM aarch64, version 1 (SYSV), dynamically linked, interpreter /lib/ld-linux-aarch64.so.1, BuildID[sha1]=cd5285a732cdfda57bf8e09ae905d0b4bd5d42cd, for GNU/Linux 3.7.0, stripped
build-rv64/sensord:   ELF 64-bit LSB pie executable, UCB RISC-V, RVC, double-float ABI, version 1 (SYSV), dynamically linked, interpreter /lib/ld-linux-riscv64-lp64d.so.1, BuildID[sha1]=39f59f4aae0e38555a89da268d4d764f0100126e, for GNU/Linux 4.15.0, stripped
sensord running on machine=aarch64, sizeof(long)=8
sensord running on machine=armv7l, sizeof(long)=4
sensord running on machine=riscv64, sizeof(long)=8
### run armhf directly
bash: line 1: ./build-armhf/sensord: cannot execute binary file: Exec format error
exit=126
```

출력에서 볼 것: `file`의 `ELF 32-bit ... ARM, EABI5`와 `interpreter /lib/ld-linux-armhf.so.3`가 "진짜 armhf 바이너리"라는 증거다. `qemu-arm -L <dir>`은 동적 로더와 libc를 `<dir>` 아래에서 찾게 하는 user-mode 에뮬레이터다(armhf에서 `uname`이 `armv7l`을 보고하는 것도 qemu가 흉내 낸 값). 직접 실행하면 `Exec format error`다 — 이 VM(Apple Silicon 위 aarch64)은 AArch32 실행을 지원하지 않고, binfmt_misc에 qemu-arm도 등록돼 있지 않다. 출력 디렉터리를 `O=`로 나누는 것도 커널 관례(`make O=build-imx8`)와 같다: 한 소스 트리로 여러 보드를 동시에 빌드할 수 있다.

---

## 7. Kbuild · Kconfig 미리보기 — 커널은 자기 빌드 시스템을 쓴다

커널은 일반 Makefile이 아니라 **Kbuild**라는 Make 기반 프레임워크로 빌드된다. 드라이버 작성자가 쓰는 Makefile은 "무엇을 빌드할지"만 선언하고, "어떻게"(컴파일 플래그, 링크, `modpost`, 서명, 설치)는 커널 트리의 `scripts/Makefile.*`가 전부 한다. 그리고 "무엇을 켤지"는 **Kconfig**로 정한다. 여기서는 미리보기만 하고, 깊은 내용은 E2(커널 빌드·Kconfig·defconfig)에서 다룬다.

### 7.1 예제 ex7 — out-of-tree 모듈을 VM 커널 헤더로 빌드

**out-of-tree 모듈**은 커널 소스 트리 바깥(내 레포)에 있는 드라이버를 이미 빌드된 커널에 맞춰 `.ko`로 빌드하는 방식이다. 벤더 Wi-Fi 드라이버, 개발 중인 센서 드라이버가 이 형태로 시작한다. 필요한 것은 커널 소스 전체가 아니라 **그 커널을 빌드할 때의 설정과 헤더, Makefile들**이며, Ubuntu에서는 `linux-headers-$(uname -r)` 패키지가 `/lib/modules/$(uname -r)/build` → `/usr/src/linux-headers-...`로 제공한다.

무엇을 확인하나: `obj-m` 한 줄짜리 Kbuild 파일과 위임만 하는 바깥 Makefile로 모듈을 빌드하고, 모듈 파라미터를 주어 로드·언로드한다. 소스에서 `CONFIG_HZ`, `CONFIG_ARM64` 같은 Kconfig 매크로를 그대로 쓸 수 있는지도 본다.

```c
// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

static int odr_hz = 400;
module_param(odr_hz, int, 0444);
MODULE_PARM_DESC(odr_hz, "IMU output data rate in Hz");

static int __init b4_hello_init(void)
{
	pr_info("b4_hello: loaded, odr_hz=%d, CONFIG_HZ=%d\n", odr_hz, CONFIG_HZ);
#ifdef CONFIG_ARM64
	pr_info("b4_hello: CONFIG_ARM64 is set (from autoconf.h)\n");
#endif
	return 0;
}

static void __exit b4_hello_exit(void)
{
	pr_info("b4_hello: unloaded\n");
}

module_init(b4_hello_init);
module_exit(b4_hello_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("B4 out-of-tree Kbuild demo");
```

```text
# Kbuild — Kbuild가 읽는 파일 (Makefile 대신 써도 된다)
obj-m := b4_hello.o
```

```text
# Makefile — 바깥 Makefile: 커널 빌드 시스템(Kbuild)에 위임만 한다
KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules
clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
.PHONY: all clean
```

```sh
limactl shell elinux -- bash -lc 'cp -r /Users/donh/.elinux_scratch/B4/ex7 /tmp/B4/ && cd /tmp/B4/ex7 && make && ls && modinfo ./b4_hello.ko'
```

```text
make -C /lib/modules/7.0.0-34-generic/build M=/tmp/B4/ex7 modules
make[1]: Entering directory '/usr/src/linux-headers-7.0.0-34-generic'
make[2]: Entering directory '/tmp/B4/ex7'
warning: the compiler differs from the one used to build the kernel
  The kernel was built by: aarch64-linux-gnu-gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0
  You are using:           gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0
warning: pahole version differs from the one used to build the kernel
  The kernel was built with: 131
  You are using:             0
  CC [M]  b4_hello.o
  MODPOST Module.symvers
  CC [M]  b4_hello.mod.o
  CC [M]  .module-common.o
  LD [M]  b4_hello.ko
  BTF [M] b4_hello.ko
Skipping BTF generation for b4_hello.ko due to unavailability of vmlinux
make[2]: Leaving directory '/tmp/B4/ex7'
make[1]: Leaving directory '/usr/src/linux-headers-7.0.0-34-generic'
Kbuild
Makefile
Module.symvers
b4_hello.c
b4_hello.ko
b4_hello.mod
b4_hello.mod.c
b4_hello.mod.o
b4_hello.o
modules.order
filename:       /tmp/B4/ex7/./b4_hello.ko
description:    B4 out-of-tree Kbuild demo
license:        GPL
srcversion:     9E7536513249AA27E2C8377
depends:        
name:           b4_hello
vermagic:       7.0.0-34-generic SMP preempt mod_unload modversions aarch64
parm:           odr_hz:IMU output data rate in Hz (int)
```

출력에서 볼 것:

- `make -C $(KDIR)`로 **커널 헤더 트리의 최상위 Makefile**이 실행되고(`Entering directory '/usr/src/linux-headers-...'`), `M=`이 "외부 모듈 디렉터리는 여기"라고 알려 준다. 그러면 Kbuild가 `M` 디렉터리의 `Kbuild`(없으면 `Makefile`)를 읽어 `obj-m`을 찾는다. 우리 Makefile은 사실상 두 번 읽힌다: 한 번은 우리가 `make`를 칠 때, 한 번은 Kbuild가 `obj-m`을 찾을 때. 그래서 Kbuild 파일을 따로 두는 편이 깔끔하다.
- `compiler differs` 경고: 커널은 `aarch64-linux-gnu-gcc`로, 우리는 `gcc`로 빌드했다. 이 VM에서는 같은 gcc 15.2라 문제없지만, **보드 커널과 다른 버전의 컴파일러로 모듈을 빌드하면** 구조체 레이아웃·플러그인 플래그 차이로 로드 실패나 오동작이 날 수 있다. 실무에서는 보드 커널을 빌드한 바로 그 툴체인(Yocto SDK, 벤더 툴체인)을 쓴다.
- `vermagic`: 모듈이 맞춰 빌드된 커널 릴리스와 핵심 설정(`SMP preempt mod_unload modversions aarch64`)이다. 로드할 때 실행 중인 커널과 다르면 거부된다(§13).

이제 로드해 보자. 모듈 이름은 다른 작업과 겹치지 않게 `b4_` 접두사를 쓰고, 끝나면 반드시 내린다.

```sh
sudo insmod ./b4_hello.ko odr_hz=800 && cat /sys/module/b4_hello/parameters/odr_hz && lsmod | grep b4_hello
sudo rmmod b4_hello; sudo dmesg | grep b4_hello | tail -3; lsmod | grep -c b4_hello
```

```text
800
b4_hello               12288  0
[148169.999080] b4_hello: loaded, odr_hz=800, CONFIG_HZ=1000
[148169.999086] b4_hello: CONFIG_ARM64 is set (from autoconf.h)
[148170.010288] b4_hello: unloaded
0
```

출력에서 볼 것: `module_param`이 `/sys/module/b4_hello/parameters/odr_hz`로 노출됐다(0444 = 읽기 전용). `CONFIG_HZ`와 `CONFIG_ARM64`는 우리가 정의한 적이 없는데 쓸 수 있었다 — Kbuild가 모든 커널 소스 컴파일에 Kconfig 결과 헤더를 강제로 include하기 때문이다. `make V=1`로 실제 컴파일 명령을 보면 확인된다(관련 부분만 발췌).

```sh
make V=1 2>&1 | grep -E " -c -o b4_hello.o" | tr " " "\n" | sed -n "14,20p"
grep -n autoconf /lib/modules/$(uname -r)/build/include/linux/kconfig.h
```

```text
-include
/usr/src/linux-headers-7.0.0-34-generic/include/linux/compiler-version.h
-include
/usr/src/linux-headers-7.0.0-34-generic/include/linux/kconfig.h
-I/usr/src/linux-headers-7.0.0-34-generic/ubuntu/include
-include
/usr/src/linux-headers-7.0.0-34-generic/include/linux/compiler_types.h
5:#include <generated/autoconf.h>
55: * autoconf.h.
71: * autoconf.h, while CONFIG_FOO=m results in "#define CONFIG_FOO_MODULE 1".
```

같은 명령줄에는 `-nostdinc`(유저 공간 libc 헤더 금지), `-D__KERNEL__`, `-DMODULE`, `-DKBUILD_MODNAME='"b4_hello"'`, `-mgeneral-regs-only`(커널에서는 FP/SIMD 레지스터 사용 금지)도 들어 있다. 드라이버 코드에서 `<stdio.h>`를 include할 수 없는 이유가 이 `-nostdinc`다.

### 7.2 Kconfig 문법 미리보기

**Kconfig**는 "어떤 기능을 켤 수 있고, 무엇에 의존하고, 기본값이 무엇인가"를 적는 설정 언어다. `make menuconfig`의 메뉴가 이 파일들에서 생성된다.

| 키워드 | 뜻 |
|---|---|
| `config FOO` | 심볼 `FOO` 정의 → 결과는 `CONFIG_FOO` |
| `bool` | y / n 두 값 |
| `tristate` | y(커널에 내장) / m(모듈 .ko) / n(빌드 안 함) |
| `int`, `hex`, `string` | 값 타입 (`range`로 범위 제한) |
| `depends on X` | X가 켜져 있어야 이 항목이 보이고 켤 수 있다 (X=m이면 최대 m) |
| `select Y` | 이 항목을 켜면 Y를 **강제로** 켠다 (Y의 depends는 무시 — 남용 주의) |
| `default` | 기본값 |
| `help` | menuconfig의 설명 |
| `menu` / `choice` / `if` / `source` | 메뉴 묶기, 택일, 조건 블록, 다른 Kconfig 포함 |

무엇을 확인하나: 센서 키트를 흉내 낸 작은 Kconfig를 VM 커널 헤더에 들어 있는 진짜 `scripts/kconfig/conf` 도구로 처리해서 `.config` → `auto.conf` · `autoconf.h`가 생기는 과정을 본다.

```text
mainmenu "B4 sensor-kit Kconfig demo"

config MODULES
	bool "Enable loadable module support"
	default y
	modules

config SENSOR_KIT
	bool "Sensor kit support"
	default y

config IMU_BMI270
	tristate "Bosch BMI270 IMU driver"
	depends on SENSOR_KIT
	select REGMAP_I2C
	default m
	help
	  Driver for the BMI270 6-axis IMU on I2C.

config IMU_ODR_HZ
	int "IMU output data rate (Hz)"
	depends on IMU_BMI270
	range 25 1600
	default 400

config REGMAP_I2C
	tristate

config GNSS_UBLOX
	bool "u-blox GNSS over UART"
	depends on SENSOR_KIT && SERIAL_SUPPORT

config SERIAL_SUPPORT
	bool
```

```sh
B=/lib/modules/$(uname -r)/build; export srctree=/tmp/B4/ex8; cd /tmp/B4/ex8
$B/scripts/kconfig/conf --alldefconfig Kconfig >/dev/null; grep -v '^#' .config | grep .
$B/scripts/kconfig/conf --syncconfig Kconfig; grep define include/generated/autoconf.h; grep -v '^#' include/config/auto.conf
sed -i 's/^CONFIG_IMU_BMI270=m/# CONFIG_IMU_BMI270 is not set/' .config
$B/scripts/kconfig/conf --olddefconfig Kconfig >/dev/null; cat .config | grep -v '^#$' | sed -n '4,$p'
```

```text
### conf --alldefconfig
CONFIG_MODULES=y
CONFIG_SENSOR_KIT=y
CONFIG_IMU_BMI270=m
CONFIG_IMU_ODR_HZ=400
CONFIG_REGMAP_I2C=m
### conf --syncconfig → autoconf.h
#define CONFIG_SENSOR_KIT 1
#define CONFIG_MODULES 1
#define CONFIG_IMU_BMI270_MODULE 1
#define CONFIG_IMU_ODR_HZ 400
#define CONFIG_REGMAP_I2C_MODULE 1
--- auto.conf
CONFIG_SENSOR_KIT=y
CONFIG_MODULES=y
CONFIG_IMU_BMI270=m
CONFIG_IMU_ODR_HZ=400
CONFIG_REGMAP_I2C=m
### disable IMU_BMI270, olddefconfig
CONFIG_SENSOR_KIT=y
# CONFIG_IMU_BMI270 is not set
```

출력에서 볼 것:

- `select REGMAP_I2C` 때문에 사용자가 고를 수 없는(프롬프트 없는) `REGMAP_I2C`가 IMU 드라이버와 같은 `m`으로 켜졌다.
- `GNSS_UBLOX`는 `SERIAL_SUPPORT`가 꺼져 있어서 `.config`에 아예 나타나지 않는다.
- **`=y`는 `#define CONFIG_X 1`, `=m`은 `#define CONFIG_X_MODULE 1`**이 된다. 그래서 C 코드에서 `#ifdef CONFIG_IMU_BMI270`은 모듈로 빌드할 때 **거짓**이다. 이 함정 때문에 커널 코드는 `IS_ENABLED(CONFIG_X)`(y 또는 m이면 참)와 `IS_BUILTIN()`, `IS_MODULE()` 매크로를 쓴다(`include/linux/kconfig.h`).
- IMU 드라이버를 끄고 `olddefconfig`를 돌리면 그것에 의존하던 `IMU_ODR_HZ`와 `select`로 끌려왔던 `REGMAP_I2C`가 함께 사라진다. 처음에 `MODULES` 심볼(`modules` 속성)을 빼고 돌렸을 때는 `default m`이 `y`로 올라갔다 — 모듈 지원이 없는 설정에서 tristate는 bool처럼 동작한다.

실제 커널의 `.config`와 `autoconf.h`도 같은 관계다.

```sh
B=/lib/modules/$(uname -r)/build
grep -E "^CONFIG_(HZ|ARM64|I2C|SPI_SPIDEV|IIO|EXT4_FS|VFAT_FS)=" $B/.config
grep -E "^# CONFIG_USB_DUMMY_HCD " $B/.config
grep -E "define CONFIG_(HZ|ARM64|I2C|IIO_MODULE|SPI_SPIDEV_MODULE|EXT4_FS) " $B/include/generated/autoconf.h
```

```text
CONFIG_ARM64=y
CONFIG_HZ=1000
CONFIG_I2C=y
CONFIG_SPI_SPIDEV=m
CONFIG_IIO=m
CONFIG_EXT4_FS=y
CONFIG_VFAT_FS=y
# CONFIG_USB_DUMMY_HCD is not set
#define CONFIG_ARM64 1
#define CONFIG_IIO_MODULE 1
#define CONFIG_HZ 1000
#define CONFIG_SPI_SPIDEV_MODULE 1
#define CONFIG_EXT4_FS 1
#define CONFIG_I2C 1
```

그리고 Kbuild Makefile 쪽에서는 `auto.conf`의 값이 Make 변수가 되어 이렇게 쓰인다(커널 `drivers/` 아래 Makefile의 전형적인 모양).

```text
obj-$(CONFIG_IMU_BMI270) += bmi270.o      # y → obj-y (vmlinux에 내장), m → obj-m (.ko), n → obj- (무시)
bmi270-y := bmi270_core.o bmi270_i2c.o    # 여러 .c를 묶어 bmi270.o (또는 bmi270.ko) 하나로
ccflags-y += -DDEBUG                       # 이 디렉터리 파일에만 붙는 컴파일 플래그
```

```svg
<svg viewBox="0 0 680 390" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="20" width="150" height="54" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="95" y="43" text-anchor="middle" font-size="13">Kconfig 파일들</text>
  <text x="95" y="62" text-anchor="middle" font-size="12">drivers/*/Kconfig</text>
  <rect x="225" y="20" width="230" height="54" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="43" text-anchor="middle" font-size="12">scripts/kconfig/conf · mconf</text>
  <text x="340" y="62" text-anchor="middle" font-size="12">defconfig · menuconfig 등</text>
  <rect x="510" y="20" width="150" height="54" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="585" y="43" text-anchor="middle" font-size="13">.config</text>
  <text x="585" y="62" text-anchor="middle" font-size="12">CONFIG_IMU_BMI270=m</text>
  <line x1="170" y1="47" x2="221" y2="47" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="455" y1="47" x2="506" y2="47" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="530" y="110" text-anchor="middle" font-size="12">syncconfig (make 시 자동)</text>
  <line x1="560" y1="74" x2="230" y2="126" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="585" y1="74" x2="540" y2="126" stroke="currentColor" marker-end="url(#ah3)"/>
  <rect x="40" y="130" width="290" height="54" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="185" y="153" text-anchor="middle" font-size="12">include/config/auto.conf</text>
  <text x="185" y="172" text-anchor="middle" font-size="12">CONFIG_IMU_BMI270=m (Make 문법)</text>
  <rect x="380" y="130" width="280" height="54" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="520" y="153" text-anchor="middle" font-size="12">include/generated/autoconf.h</text>
  <text x="520" y="172" text-anchor="middle" font-size="12">#define CONFIG_IMU_BMI270_MODULE 1</text>
  <line x1="185" y1="184" x2="185" y2="226" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="520" y1="184" x2="520" y2="226" stroke="currentColor" marker-end="url(#ah3)"/>
  <rect x="40" y="230" width="290" height="54" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="185" y="253" text-anchor="middle" font-size="13">Kbuild Makefile</text>
  <text x="185" y="272" text-anchor="middle" font-size="12">obj-$(CONFIG_IMU_BMI270) += bmi270.o</text>
  <rect x="380" y="230" width="280" height="54" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="520" y="253" text-anchor="middle" font-size="13">모든 .c에 -include kconfig.h</text>
  <text x="520" y="272" text-anchor="middle" font-size="12">#ifdef · IS_ENABLED(CONFIG_X)</text>
  <line x1="185" y1="284" x2="300" y2="326" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="520" y1="284" x2="400" y2="326" stroke="currentColor" marker-end="url(#ah3)"/>
  <rect x="140" y="330" width="400" height="48" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="359" text-anchor="middle" font-size="13">y → vmlinux에 내장 · m → bmi270.ko · n → 빌드 안 함</text>
</svg>
```
그림 5 — Kconfig → `.config` → 두 개의 생성 파일 → Kbuild. 같은 설정이 **Make 쪽**(`auto.conf`: 무엇을 컴파일할지)과 **C 쪽**(`autoconf.h`: 코드 안의 조건부 컴파일)으로 동시에 흘러간다.

---

## 8. 메타 빌드 시스템 — configure → generate → build

Make는 "명령을 실행하는 엔진"이다. 하지만 이식성 있는 프로젝트는 빌드 전에 물어야 할 것이 많다. 컴파일러가 무엇인가? `termios.h`가 있나? `libz`는 어디 있고 어떤 플래그가 필요한가? 크로스 빌드인가? 이 질문에 답하고 그 결과로 **Makefile이나 build.ninja를 생성**하는 도구가 메타 빌드 시스템이다. 세 가지 모두 같은 3단계 구조를 가진다.

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="115" y="22" text-anchor="middle" font-size="14" font-weight="bold">CMake</text>
  <text x="340" y="22" text-anchor="middle" font-size="14" font-weight="bold">Meson</text>
  <text x="565" y="22" text-anchor="middle" font-size="14" font-weight="bold">autotools</text>
  <rect x="15" y="34" width="200" height="50" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="115" y="55" text-anchor="middle" font-size="12">CMakeLists.txt</text>
  <text x="115" y="73" text-anchor="middle" font-size="12">+ toolchain file</text>
  <rect x="240" y="34" width="200" height="50" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="55" text-anchor="middle" font-size="12">meson.build</text>
  <text x="340" y="73" text-anchor="middle" font-size="12">+ cross file (.ini)</text>
  <rect x="465" y="34" width="200" height="50" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="565" y="55" text-anchor="middle" font-size="12">configure.ac · Makefile.am</text>
  <text x="565" y="73" text-anchor="middle" font-size="12">autoreconf -i → configure</text>
  <rect x="15" y="106" width="200" height="50" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="115" y="127" text-anchor="middle" font-size="12">cmake -S . -B build</text>
  <text x="115" y="145" text-anchor="middle" font-size="12">컴파일러 검사 · 캐시</text>
  <rect x="240" y="106" width="200" height="50" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="127" text-anchor="middle" font-size="12">meson setup build</text>
  <text x="340" y="145" text-anchor="middle" font-size="12">--cross-file x.ini</text>
  <rect x="465" y="106" width="200" height="50" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="565" y="127" text-anchor="middle" font-size="12">./configure --host=...</text>
  <text x="565" y="145" text-anchor="middle" font-size="12">conftest → config.log</text>
  <rect x="15" y="178" width="200" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="115" y="199" text-anchor="middle" font-size="12">build.ninja 또는 Makefile</text>
  <text x="115" y="217" text-anchor="middle" font-size="12">CMakeCache.txt</text>
  <rect x="240" y="178" width="200" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="199" text-anchor="middle" font-size="12">build.ninja</text>
  <text x="340" y="217" text-anchor="middle" font-size="12">meson-logs/ · meson-info/</text>
  <rect x="465" y="178" width="200" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="565" y="199" text-anchor="middle" font-size="12">Makefile · config.h</text>
  <text x="565" y="217" text-anchor="middle" font-size="12">config.status</text>
  <rect x="15" y="250" width="200" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="115" y="275" text-anchor="middle" font-size="12">cmake --build build</text>
  <rect x="240" y="250" width="200" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="275" text-anchor="middle" font-size="12">ninja -C build</text>
  <rect x="465" y="250" width="200" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="565" y="275" text-anchor="middle" font-size="12">make -jN</text>
  <line x1="115" y1="84" x2="115" y2="102" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="115" y1="156" x2="115" y2="174" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="115" y1="228" x2="115" y2="246" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="340" y1="84" x2="340" y2="102" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="340" y1="156" x2="340" y2="174" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="340" y1="228" x2="340" y2="246" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="565" y1="84" x2="565" y2="102" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="565" y1="156" x2="565" y2="174" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="565" y1="228" x2="565" y2="246" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="115" y1="290" x2="250" y2="318" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="340" y1="290" x2="340" y2="318" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="565" y1="290" x2="430" y2="318" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="90" y="322" width="500" height="44" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="340" y="349" text-anchor="middle" font-size="13">같은 결과: 크로스 컴파일된 ELF + install (DESTDIR=staging)</text>
</svg>
```
그림 6 — 세 메타 빌드 시스템의 같은 3단계. 입력(파란색)을 설정 단계(주황)가 읽어 시스템을 검사하고, 생성물(초록)로 Ninja/Make 파일을 만든 뒤, 실제 컴파일은 Ninja나 Make가 한다. **크로스 정보(컴파일러, sysroot, 타깃 CPU)는 설정 단계에 한 번만** 넣는다.

핵심 공통점 두 가지:

- **설정과 빌드가 분리된다.** 설정 단계에서 내린 결정(컴파일러 경로, 찾은 라이브러리)은 캐시(`CMakeCache.txt`, `meson-info`, `config.status`)에 저장된다. 그래서 툴체인을 바꿀 때는 빌드 디렉터리를 **지우고 다시 설정**해야 한다. 같은 빌드 디렉터리에서 컴파일러만 바꾸려 하면 옛 캐시가 이긴다.
- **out-of-tree 빌드**: 소스 디렉터리와 빌드 디렉터리를 분리한다. `build-native/`, `build-armhf/`를 나란히 두고 같은 소스를 두 타깃으로 빌드할 수 있다. Meson은 in-tree 빌드를 아예 허용하지 않는다.

---

## 9. CMake — 타깃 중심 빌드와 toolchain file

### 9.1 타깃과 속성 전파

현대 CMake(3.x 이후)의 핵심은 **타깃(target)**이다. `add_library`·`add_executable`로 타깃을 만들고, include 경로·정의·링크 라이브러리를 **타깃의 속성**으로 붙인다. 속성에는 전파 범위가 있다.

| 키워드 | 이 타깃 자신 | 이 타깃을 링크하는 쪽 |
|---|---|---|
| `PRIVATE` | 적용 | 전파 안 됨 |
| `PUBLIC` | 적용 | 전파됨 |
| `INTERFACE` | 적용 안 됨 | 전파됨 (헤더 전용 라이브러리) |

Don의 말로 바꾸면 "헤더에 노출되는 것은 PUBLIC, `.c` 안에서만 쓰는 것은 PRIVATE"이다. 전역 변수 `CMAKE_C_FLAGS`나 `include_directories()`를 쓰는 옛 스타일은 이 전파가 안 되어 큰 프로젝트에서 플래그가 엉킨다.

### 9.2 예제 ex9 — 라이브러리 + 실행 파일, out-of-tree 네이티브 빌드

무엇을 확인하나: `imu` 정적 라이브러리의 PUBLIC include 경로는 `sensord`에도 전파되고, PRIVATE 정의(`IMU_ODR_HZ`)는 `imu.c`에만 붙는지 실제 컴파일 명령으로 확인한다.

```text
cmake_minimum_required(VERSION 3.20)
project(sensorkit VERSION 1.4.2 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
add_compile_options(-Wall -Wextra)

# 라이브러리 타깃: 공개 헤더 경로는 PUBLIC → 링크하는 쪽에 자동 전파
add_library(imu STATIC src/imu.c)
target_include_directories(imu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(imu PRIVATE IMU_ODR_HZ=400)

# 실행 파일 타깃
add_executable(sensord src/main.c)
target_link_libraries(sensord PRIVATE imu m)

install(TARGETS sensord RUNTIME DESTINATION bin)
```

```c
/* include/imu.h */
double imu_pitch_deg(int ax, int az);
int imu_odr(void);

/* src/imu.c */
#include <math.h>
#include "imu.h"
double imu_pitch_deg(int ax, int az) { return atan2(ax, az) * 180.0 / M_PI; }
int imu_odr(void) { return IMU_ODR_HZ; }

/* src/main.c */
#include <stdio.h>
#include "imu.h"
int main(void)
{
    printf("odr=%d Hz pitch=%.1f deg sizeof(void*)=%zu\n",
           imu_odr(), imu_pitch_deg(1000, 1000), sizeof(void *));
    return 0;
}
```

```sh
cmake -S . -B build-native -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-native -- -v
./build-native/sensord; ls
```

```text
-- The C compiler identification is GNU 15.2.0
-- Detecting C compiler ABI info
-- Detecting C compiler ABI info - done
-- Check for working C compiler: /usr/bin/cc - skipped
-- Detecting C compile features
-- Detecting C compile features - done
-- Configuring done (0.1s)
-- Generating done (0.0s)
-- Build files have been written to: /tmp/B4/ex9/build-native
[1/4] /usr/bin/cc  -I/tmp/B4/ex9/include -O3 -DNDEBUG -std=gnu11 -Wall -Wextra -MD -MT CMakeFiles/sensord.dir/src/main.c.o -MF CMakeFiles/sensord.dir/src/main.c.o.d -o CMakeFiles/sensord.dir/src/main.c.o -c /tmp/B4/ex9/src/main.c
[2/4] /usr/bin/cc -DIMU_ODR_HZ=400 -I/tmp/B4/ex9/include -O3 -DNDEBUG -std=gnu11 -Wall -Wextra -MD -MT CMakeFiles/imu.dir/src/imu.c.o -MF CMakeFiles/imu.dir/src/imu.c.o.d -o CMakeFiles/imu.dir/src/imu.c.o -c /tmp/B4/ex9/src/imu.c
[3/4] : && /usr/bin/cmake -E rm -f libimu.a && /usr/bin/ar qc libimu.a  CMakeFiles/imu.dir/src/imu.c.o && /usr/bin/ranlib libimu.a && :
[4/4] : && /usr/bin/cc -O3 -DNDEBUG -Wl,--dependency-file=CMakeFiles/sensord.dir/link.d CMakeFiles/sensord.dir/src/main.c.o -o sensord  libimu.a  -lm && :
odr=400 Hz pitch=45.0 deg sizeof(void*)=8
CMakeLists.txt
build-native
cmake
include
src
```

출력에서 볼 것:

- `main.c`의 컴파일에도 `-I/tmp/B4/ex9/include`가 붙었다(PUBLIC 전파). `-DIMU_ODR_HZ=400`은 `imu.c`에만 있다(PRIVATE).
- CMake도 **`-MD -MT ... -MF ...d`**로 헤더 의존성을 자동 생성한다(§3). 손으로 Makefile을 쓰지 않는 큰 이유다.
- 소스 디렉터리에는 아무것도 생기지 않았다(out-of-tree). `-G Ninja`를 빼면 Unix Makefiles 생성기가 기본이다.

### 9.3 toolchain file — CMake에 "타깃이 무엇인가"를 알려 주는 파일

CMake로 크로스 빌드할 때는 `CMakeLists.txt`를 고치지 않는다. 대신 **toolchain file**을 `-DCMAKE_TOOLCHAIN_FILE=`로 넘긴다. 이 파일은 `project()`가 컴파일러를 검사하기 **전에** 읽힌다.

| 변수 | 뜻 |
|---|---|
| `CMAKE_SYSTEM_NAME` | 타깃 OS (`Linux`, bare-metal이면 `Generic`). 이걸 설정하면 `CMAKE_CROSSCOMPILING`이 TRUE |
| `CMAKE_SYSTEM_PROCESSOR` | 타깃 CPU (`arm`, `aarch64`) — 프로젝트가 분기용으로 참조 |
| `CMAKE_C_COMPILER` | 크로스 컴파일러 (ar·ranlib·strip은 접두사로 자동 추정) |
| `CMAKE_SYSROOT` | 컴파일·링크에 `--sysroot=`로 넘길 타깃 루트 |
| `CMAKE_FIND_ROOT_PATH` | `find_library`·`find_path`·`find_package`가 뒤질 루트들 |
| `CMAKE_FIND_ROOT_PATH_MODE_PROGRAM` | `NEVER`: 실행할 도구는 **호스트**에서 찾는다 |
| `CMAKE_FIND_ROOT_PATH_MODE_LIBRARY` / `_INCLUDE` / `_PACKAGE` | `ONLY`: 라이브러리·헤더·패키지는 **타깃 루트 안에서만** 찾는다 |

첫 시도로 Debian/Ubuntu 크로스 패키지가 설치한 `/usr/arm-linux-gnueabihf`를 그대로 sysroot로 써 봤다.

```text
set(CMAKE_SYSROOT /usr/arm-linux-gnueabihf)   # 첫 시도
```

```text
-- Detecting C compiler ABI info - failed
-- Check for working C compiler: /usr/bin/arm-linux-gnueabihf-gcc
-- Check for working C compiler: /usr/bin/arm-linux-gnueabihf-gcc - broken
CMake Error at /usr/share/cmake-4.2/Modules/CMakeTestCCompiler.cmake:67 (message):
  The C compiler

    "/usr/bin/arm-linux-gnueabihf-gcc"

  is not able to compile a simple test program.
...
    /usr/bin/arm-linux-gnueabihf-ld.bfd: cannot find /usr/arm-linux-gnueabihf/lib/libc.so.6 inside /usr/arm-linux-gnueabihf
    /usr/bin/arm-linux-gnueabihf-ld.bfd: cannot find /usr/arm-linux-gnueabihf/lib/libc_nonshared.a inside /usr/arm-linux-gnueabihf
    /usr/bin/arm-linux-gnueabihf-ld.bfd: cannot find /usr/arm-linux-gnueabihf/lib/ld-linux-armhf.so.3 inside /usr/arm-linux-gnueabihf
    collect2: error: ld returned 1 exit status
```

(중간 출력은 `...`로 생략.) 이 실패가 sysroot의 의미를 잘 보여 준다. `libc.so`는 실제로는 텍스트 **링커 스크립트**이고 안에 `/usr/arm-linux-gnueabihf/lib/libc.so.6` 같은 절대 경로가 적혀 있다. 링커는 sysroot 안에 있는 스크립트의 절대 경로 앞에 sysroot를 붙이므로 `/usr/arm-linux-gnueabihf/usr/arm-linux-gnueabihf/lib/...`를 찾다 실패한다("inside"). 즉 Debian의 `/usr/arm-linux-gnueabihf`는 **타깃 rootfs 모양의 진짜 sysroot가 아니다** — 크로스 gcc에 경로가 내장된 "multiarch 크로스 설치"일 뿐이다.

진짜 sysroot는 **타깃 보드의 `/`를 복사한 것처럼** `usr/include`, `usr/lib`가 있는 디렉터리다. Yocto SDK의 `sysroots/cortexa53-poky-linux/`, Buildroot의 `output/staging/`이 그 모양이다. 여기서는 그 구조를 심볼릭 링크로 흉내 냈다.

```sh
S=/tmp/B4/sysroot-armhf; mkdir -p $S/usr/include $S/usr/lib
ln -s /usr/arm-linux-gnueabihf/include $S/usr/include/arm-linux-gnueabihf
ln -s /usr/arm-linux-gnueabihf/lib     $S/usr/lib/arm-linux-gnueabihf
ln -s /usr/arm-linux-gnueabihf         $S/usr/arm-linux-gnueabihf   # libc.so 스크립트의 절대 경로용
ln -s usr/lib $S/lib
```

최종 toolchain file:

```text
# cmake/armhf-toolchain.cmake — 타깃이 무엇인지 CMake에 알려 주는 파일
set(CMAKE_SYSTEM_NAME      Linux)          # 이걸 설정하면 CMAKE_CROSSCOMPILING=TRUE
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER   arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)

# 타깃 루트 파일시스템 (헤더·라이브러리를 여기서 찾는다)
set(CMAKE_SYSROOT /tmp/B4/sysroot-armhf)
set(CMAKE_FIND_ROOT_PATH ${CMAKE_SYSROOT})

# 프로그램(빌드 도구)은 호스트에서, 라이브러리·헤더·패키지는 타깃 쪽에서만 찾는다
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
```

무엇을 확인하나: 같은 `CMakeLists.txt`를 toolchain file만 바꿔서 armhf로 빌드하고 `file`·`qemu-arm`으로 검증한다.

```sh
cmake -S . -B build-armhf -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/armhf-toolchain.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-armhf -- -v
file build-armhf/sensord
qemu-arm -L /usr/arm-linux-gnueabihf build-armhf/sensord
grep -E "CMAKE_CROSSCOMPILING|CMAKE_SYSTEM_PROCESSOR" build-armhf/CMakeFiles/*/CMakeSystem.cmake
```

```text
-- The C compiler identification is GNU 15.2.0
-- Detecting C compiler ABI info
-- Detecting C compiler ABI info - done
-- Check for working C compiler: /usr/bin/arm-linux-gnueabihf-gcc - skipped
-- Detecting C compile features
-- Detecting C compile features - done
-- Configuring done (0.1s)
-- Generating done (0.0s)
-- Build files have been written to: /tmp/B4/ex9/build-armhf
[1/4] /usr/bin/arm-linux-gnueabihf-gcc --sysroot=/tmp/B4/sysroot-armhf  -I/tmp/B4/ex9/include -O3 -DNDEBUG -std=gnu11 -Wall -Wextra -MD -MT CMakeFiles/sensord.dir/src/main.c.o -MF CMakeFiles/sensord.dir/src/main.c.o.d -o CMakeFiles/sensord.dir/src/main.c.o -c /tmp/B4/ex9/src/main.c
[2/4] /usr/bin/arm-linux-gnueabihf-gcc --sysroot=/tmp/B4/sysroot-armhf -DIMU_ODR_HZ=400 -I/tmp/B4/ex9/include -O3 -DNDEBUG -std=gnu11 -Wall -Wextra -MD -MT CMakeFiles/imu.dir/src/imu.c.o -MF CMakeFiles/imu.dir/src/imu.c.o.d -o CMakeFiles/imu.dir/src/imu.c.o -c /tmp/B4/ex9/src/imu.c
...
[2/2] Linking C executable sensord
build-armhf/sensord: ELF 32-bit LSB pie executable, ARM, EABI5 version 1 (SYSV), dynamically linked, interpreter /lib/ld-linux-armhf.so.3, BuildID[sha1]=3c52352e30de1786fde1cb73e110f0ef676148d3, for GNU/Linux 3.2.0, not stripped
odr=400 Hz pitch=45.0 deg sizeof(void*)=4
set(CMAKE_SYSTEM_PROCESSOR "arm")
set(CMAKE_CROSSCOMPILING "TRUE")
```

출력에서 볼 것: 모든 컴파일에 `--sysroot=`가 자동으로 붙었고, `sizeof(void*)=4`로 32-bit 타깃임이 확인된다. 소스와 `CMakeLists.txt`는 한 글자도 안 바꿨다. 이것이 "toolchain file은 보드 팀이 한 번 만들어 배포하고, 앱 개발자는 `-DCMAKE_TOOLCHAIN_FILE`만 붙인다"는 실무 구조의 이유다.

### 9.4 예제 ex15 — `FIND_ROOT_PATH_MODE`가 막아 주는 것

무엇을 확인하나: 개발자 PC에 호스트용(aarch64) `libbmi.so`가 `/tmp/B4/hostprefix`에, 타깃용(armhf) `libbmi.so`가 sysroot에 있을 때, `CMAKE_PREFIX_PATH`에 호스트 경로가 섞여 들어오면 `find_library`가 무엇을 고르는가. (`libbmi`는 §11에서 만든다.)

```text
cmake_minimum_required(VERSION 3.20)
project(findtest LANGUAGES C)
find_library(ZLIB_LIB z)
find_library(BMI_LIB bmi)
find_program(PYTHON_EXE python3)
message(STATUS "ZLIB_LIB   = ${ZLIB_LIB}")
message(STATUS "BMI_LIB    = ${BMI_LIB}")
message(STATUS "PYTHON_EXE = ${PYTHON_EXE}")
```

```sh
sed "s/MODE_LIBRARY ONLY/MODE_LIBRARY NEVER/" /tmp/B4/ex9/cmake/armhf-toolchain.cmake > never.cmake
for T in /tmp/B4/ex9/cmake/armhf-toolchain.cmake never.cmake; do
  cmake -S . -B b-$(basename $T) -DCMAKE_TOOLCHAIN_FILE=$T -DCMAKE_PREFIX_PATH=/tmp/B4/hostprefix 2>&1 | grep -E "_LIB |_EXE "
done
file -L /tmp/B4/hostprefix/lib/libbmi.so | cut -d, -f1-2
```

```text
### set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)  + CMAKE_PREFIX_PATH=/tmp/B4/hostprefix
-- ZLIB_LIB   = ZLIB_LIB-NOTFOUND
-- BMI_LIB    = /tmp/B4/sysroot-armhf/usr/lib/libbmi.so
-- PYTHON_EXE = /usr/bin/python3
### set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY NEVER)  + CMAKE_PREFIX_PATH=/tmp/B4/hostprefix
-- ZLIB_LIB   = ZLIB_LIB-NOTFOUND
-- BMI_LIB    = /tmp/B4/hostprefix/lib/libbmi.so
-- PYTHON_EXE = /usr/bin/python3
/tmp/B4/hostprefix/lib/libbmi.so: ELF 64-bit LSB shared object, ARM aarch64
```

출력에서 볼 것: `ONLY`일 때는 `CMAKE_PREFIX_PATH`도 sysroot 아래로 다시 뿌리내려서(re-root) armhf 라이브러리를 찾았다. `NEVER`로 바꾸면 **aarch64 라이브러리를 armhf 빌드에 집어 온다** — 설정 단계는 조용히 성공하고 링크 단계에서야 터진다. `PYTHON_EXE`는 두 경우 모두 호스트 것이다(`PROGRAM NEVER`, 빌드 중에 실행할 도구이므로 맞다). 호스트 zlib는 두 경우 모두 못 찾았는데, armhf 컴파일러 기준 라이브러리 아키텍처 디렉터리(`arm-linux-gnueabihf`)만 보기 때문이다. 처음에는 `-DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=NEVER`를 명령행으로 줘 봤는데 결과가 같았다 — toolchain file 안의 `set()`이 일반 변수로 캐시 변수를 가리기 때문이다. toolchain file에 적힌 값은 명령행으로 덮어쓰기 어렵다는 것도 기억해 둔다.

---

## 10. Meson + Ninja — cross file

**Meson**은 Python 비슷한(그러나 튜링 완전하지 않은) 선언형 언어로 빌드를 기술하고 **Ninja** 파일을 생성한다. **Ninja**는 사람이 쓰기보다 생성기가 쓰도록 만든 저수준 빌드 실행기로, 시작이 빠르고 기본으로 `nproc + 2`개쯤 병렬 실행하며, 명령줄이 바뀌어도 다시 빌드한다(Make는 명령줄 변경을 모른다).

Meson은 세 가지 기계를 구분한다. 이 용어는 autotools의 `--build`/`--host`/`--target`과 같다.

| 용어 | 뜻 | 예 |
|---|---|---|
| build machine | 빌드를 **실행하는** 기계 | 개발 PC (이 VM의 aarch64) |
| host machine | 만든 프로그램이 **돌아갈** 기계 | 보드 (armv7) |
| target machine | (컴파일러를 빌드할 때만) 그 컴파일러가 **코드를 생성할** 대상 | 보통 host와 같음 |

### 10.1 예제 ex10 — meson.build와 cross file

무엇을 확인하나: pkg-config 의존성(`bmi`)을 가진 작은 앱을 cross file로 armhf 빌드하고, Meson이 build/host machine을 어떻게 보고하는지 본다.

```text
# meson.build
project('bmi-app', 'c', version : '0.1.0',
        default_options : ['warning_level=2', 'c_std=gnu11'])

bmi_dep = dependency('bmi')                 # pkg-config로 찾는다
m_dep   = meson.get_compiler('c').find_library('m')

executable('bmi-app', 'app.c', dependencies : [bmi_dep, m_dep], install : true)

message('host_machine.cpu_family() = ' + host_machine.cpu_family())
message('build_machine.cpu_family() = ' + build_machine.cpu_family())
```

```text
# armhf-cross.ini — Meson cross file
[constants]
sysroot = '/tmp/B4/sysroot-armhf'

[binaries]
c          = 'arm-linux-gnueabihf-gcc'
ar         = 'arm-linux-gnueabihf-ar'
strip      = 'arm-linux-gnueabihf-strip'
pkg-config = 'pkg-config'
exe_wrapper = ['qemu-arm', '-L', '/usr/arm-linux-gnueabihf', '-E', 'LD_LIBRARY_PATH=' + sysroot + '/usr/lib']

[built-in options]
c_args      = ['--sysroot=' + sysroot]
c_link_args = ['--sysroot=' + sysroot]

[properties]
sys_root          = sysroot
pkg_config_libdir = sysroot + '/usr/lib/pkgconfig'

[host_machine]
system     = 'linux'
cpu_family = 'arm'
cpu        = 'armv7'
endian     = 'little'
```

`app.c`는 `<bmi.h>`를 include하고 `bmi_chip_id()`를 출력한다.

```sh
meson setup build-armhf --cross-file armhf-cross.ini
ninja -C build-armhf -v
file build-armhf/bmi-app | cut -d, -f1-2
qemu-arm -L /usr/arm-linux-gnueabihf -E LD_LIBRARY_PATH=/tmp/B4/sysroot-armhf/usr/lib build-armhf/bmi-app
```

```text
The Meson build system
Version: 1.10.1
Source dir: /tmp/B4/ex10
Build dir: /tmp/B4/ex10/build-armhf
Build type: cross build
Project name: bmi-app
Project version: 0.1.0
C compiler for the host machine: arm-linux-gnueabihf-gcc (gcc 15.2.0 "arm-linux-gnueabihf-gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0")
C linker for the host machine: arm-linux-gnueabihf-gcc ld.bfd 2.46
C compiler for the build machine: /usr/bin/ccache cc (gcc 15.2.0 "cc (Ubuntu 15.2.0-16ubuntu1) 15.2.0")
C linker for the build machine: cc ld.bfd 2.46
Build machine cpu family: aarch64
Build machine cpu: aarch64
Host machine cpu family: arm
Host machine cpu: armv7
Target machine cpu family: arm
Target machine cpu: armv7
Found pkg-config: YES (/usr/bin/pkg-config) 2.5.1
Run-time dependency bmi found: YES 0.3.0
Library m found: YES
Message: host_machine.cpu_family() = arm
Message: build_machine.cpu_family() = aarch64
Build targets in project: 1

bmi-app 0.1.0

  User defined options
    Cross files: armhf-cross.ini

Found ninja-1.13.2 at /usr/bin/ninja
ninja: Entering directory `build-armhf'
[1/2] arm-linux-gnueabihf-gcc -Ibmi-app.p -I. -I.. -I/tmp/B4/sysroot-armhf/usr/include -fdiagnostics-color=always -D_FILE_OFFSET_BITS=64 -Wall -Winvalid-pch -Wextra -std=gnu11 -O0 -g --sysroot=/tmp/B4/sysroot-armhf -MD -MQ bmi-app.p/app.c.o -MF bmi-app.p/app.c.o.d -o bmi-app.p/app.c.o -c ../app.c
[2/2] arm-linux-gnueabihf-gcc  -o bmi-app bmi-app.p/app.c.o -Wl,--as-needed -Wl,--no-undefined --sysroot=/tmp/B4/sysroot-armhf -Wl,--start-group /tmp/B4/sysroot-armhf/usr/lib/libbmi.so -lm -Wl,--end-group
build-armhf/bmi-app: ELF 32-bit LSB pie executable, ARM
BMI270 chip id = 0x24
```

출력에서 볼 것:

- `Build type: cross build`와 build/host machine 구분. Meson은 크로스 빌드 중에도 **build machine용 컴파일러**를 따로 찾는다(빌드 중에 실행할 코드 생성기를 컴파일하기 위해). 그 컴파일러로 `ccache cc`를 자동으로 골랐다 — Meson은 `ccache`가 PATH에 있으면 자동으로 끼운다.
- `Run-time dependency bmi found: YES 0.3.0`: cross file의 `pkg_config_libdir`(§11의 `PKG_CONFIG_LIBDIR`) 덕분에 sysroot 안의 `bmi.pc`를 찾았고, `-I/tmp/B4/sysroot-armhf/usr/include`는 `sys_root`가 `PKG_CONFIG_SYSROOT_DIR` 역할을 해서 붙은 것이다.
- `exe_wrapper`는 크로스 빌드에서 `meson test`나 빌드 중 실행이 필요할 때 qemu로 돌리게 해 준다.
- 기본 빌드 타입은 `debug`(`-O0 -g`)다. 보드용 릴리스는 `--buildtype=release`를 준다.

---

## 11. autotools — `./configure`가 하는 일

### 11.1 직관

**autotools**(autoconf + automake + libtool)는 1990년대 다양한 유닉스 사이에서 이식성을 확보하려고 만들어졌다. 개발자가 쓰는 것은 `configure.ac`(무엇을 검사할지, m4 매크로)와 `Makefile.am`(무엇을 빌드할지)이고, `autoreconf -i`가 이것으로부터 **수천 줄짜리 POSIX 셸 스크립트 `configure`**와 `Makefile.in`을 생성한다. 사용자는 `./configure && make && make install`만 한다.

`./configure`가 실제로 하는 일은 단순하다. **작은 테스트 프로그램(`conftest.c`)을 수십 개 컴파일·링크해 보고**, 성공 여부로 `config.h`의 `#define HAVE_XXX 1`과 Makefile 변수를 결정한다. 펌웨어로 치면 부팅 시 하드웨어 레지스터를 하나씩 probe해서 기능 비트맵을 채우는 것과 같다. 크로스 빌드에서 문제가 되는 건 "**실행해 봐야** 알 수 있는 검사"다 — 타깃 바이너리는 호스트에서 실행할 수 없기 때문이다.

### 11.2 예제 ex11 — 작은 autotools 프로젝트

무엇을 확인하나: `configure.ac`·`Makefile.am`으로 프로젝트를 만들고 `autoreconf -i`, 네이티브 configure, `--host=arm-linux-gnueabihf` 크로스 configure, 그리고 실패한 검사를 `config.log`로 추적한다.

```text
# configure.ac
AC_INIT([gnssd], [0.2.0])
AM_INIT_AUTOMAKE([foreign -Wall -Werror])
AC_PROG_CC
AC_CHECK_HEADERS([termios.h])
AC_CHECK_LIB([m], [cos])
AC_ARG_WITH([zlib], AS_HELP_STRING([--with-zlib], [compress NMEA logs with zlib]))
AS_IF([test "x$with_zlib" = "xyes"],
      [AC_CHECK_LIB([z], [deflate], [], [AC_MSG_ERROR([zlib requested but not found])])])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
```

```text
# Makefile.am
bin_PROGRAMS = gnssd
gnssd_SOURCES = gnssd.c
```

```c
#include "config.h"
#include <stdio.h>
#include <math.h>
int main(void)
{
#ifdef HAVE_TERMIOS_H
    const char *tty = "termios ok";
#else
    const char *tty = "no termios";
#endif
#ifdef HAVE_LIBZ
    const char *z = "zlib on";
#else
    const char *z = "zlib off";
#endif
    printf("%s %s: %s, %s, cos(0)=%.1f\n", PACKAGE_NAME, PACKAGE_VERSION, tty, z, cos(0.0));
    return 0;
}
```

```sh
autoreconf -i; ls
./configure | tail -12; make -s; ./gnssd
grep -E "HAVE_TERMIOS_H|HAVE_LIBM|PACKAGE_VERSION" config.h
```

```text
configure.ac:3: installing './compile'
configure.ac:2: installing './install-sh'
configure.ac:2: installing './missing'
Makefile.am: installing './depcomp'
Makefile.am
Makefile.in
aclocal.m4
autom4te.cache
compile
config.h.in
configure
configure.ac
depcomp
gnssd.c
install-sh
missing
checking for stdint.h... yes
checking for strings.h... yes
checking for sys/stat.h... yes
checking for sys/types.h... yes
checking for unistd.h... yes
checking for termios.h... yes
checking for cos in -lm... yes
checking that generated files are newer than configure... done
configure: creating ./config.status
config.status: creating Makefile
config.status: creating config.h
config.status: executing depfiles commands
gnssd 0.2.0: termios ok, zlib off, cos(0)=1.0
#define HAVE_LIBM 1
#define HAVE_TERMIOS_H 1
#define PACKAGE_VERSION "0.2.0"
```

출력에서 볼 것: 개발자가 쓴 두 파일에서 `configure`, `Makefile.in`, `config.h.in`과 보조 스크립트(`depcomp`는 헤더 의존성 추적용 — §3의 역할)가 생겼다. 각 `checking ...` 줄이 conftest 컴파일 한 번이다.

### 11.3 `--host`로 크로스 configure

autotools의 크로스 빌드는 `--host=<triplet>` 하나로 시작한다. configure는 `<triplet>-gcc`, `<triplet>-strip` 등을 PATH에서 찾는다.

```sh
make -s distclean
./configure --host=arm-linux-gnueabihf | grep -E "checking (for arm|whether we are cross|for cos|for termios|whether the C compiler works)"
make -s; file gnssd | cut -d, -f1-2; qemu-arm -L /usr/arm-linux-gnueabihf ./gnssd
```

```text
checking for arm-linux-gnueabihf-strip... arm-linux-gnueabihf-strip
checking for arm-linux-gnueabihf-gcc... arm-linux-gnueabihf-gcc
checking whether the C compiler works... yes
checking whether we are cross compiling... yes
checking for arm-linux-gnueabihf-gcc option to enable C11 features... none needed
checking for termios.h... yes
checking for cos in -lm... yes
gnssd: ELF 32-bit LSB pie executable, ARM
gnssd 0.2.0: termios ok, zlib off, cos(0)=1.0
```

출력에서 볼 것: `whether we are cross compiling... yes` — configure는 conftest 바이너리를 실행해 보고 실패하면 크로스 빌드로 판단한다. 이후 `AC_RUN_IFELSE` 같은 "실행해야 아는" 검사는 기본값으로 대체되거나 에러가 난다. 그때는 캐시 변수(`ac_cv_xxx=yes ./configure ...`)로 답을 미리 주는데, Buildroot·Yocto 레시피에 `ac_cv_` 변수가 잔뜩 있는 이유가 이것이다.

### 11.4 `config.log`로 실패 원인 찾기

무엇을 확인하나: `--with-zlib`을 요청했지만 armhf용 zlib가 없을 때 configure의 에러 메시지와 `config.log`의 실제 원인.

```sh
make -s distclean; ./configure --host=arm-linux-gnueabihf --with-zlib | tail -3
grep -n -B2 -A6 "checking for deflate in -lz" config.log | head -20
```

```text
checking for cos in -lm... yes
checking for deflate in -lz... no
configure: error: zlib requested but not found
227-configure:4634: $? = 0
228-configure:4646: result: yes
229:configure:4665: checking for deflate in -lz
230-configure:4694: arm-linux-gnueabihf-gcc -o conftest -g -O2   conftest.c -lz  -lm  >&5
231-/usr/bin/arm-linux-gnueabihf-ld.bfd: cannot find -lz: No such file or directory
232-collect2: error: ld returned 1 exit status
233-configure:4694: $? = 1
234-configure: failed program was:
235-| /* confdefs.h */
```

출력에서 볼 것: 화면의 "zlib requested but not found"만으로는 원인을 모른다. `config.log`에는 **실제로 실행한 컴파일 명령과 컴파일러의 에러 메시지**, 실패한 conftest 소스 전체가 남는다. 여기서는 armhf용 `libz.so`가 sysroot에 없다는 것이 명확하다(호스트 aarch64용 zlib는 설치돼 있지만 크로스 링커는 그걸 보지 않는다 — 올바른 동작이다). `config.log` 맨 위에는 configure를 어떤 인자로 실행했는지도 기록된다. 실무 디버그 순서는 "에러 메시지의 `checking ...` 문구로 `config.log`를 검색 → 바로 아래의 컴파일 명령과 에러 확인"이다.

---

## 12. pkg-config와 크로스 빌드 함정

### 12.1 pkg-config는 무엇인가

라이브러리를 쓰려면 `-I`(헤더 경로), `-L`(라이브러리 경로), `-l`(라이브러리 이름), 때로는 `-D`나 의존 라이브러리까지 알아야 한다. 라이브러리가 설치될 때 이 정보를 **`.pc` 파일**로 함께 설치하고, 사용하는 쪽은 `pkg-config --cflags --libs <이름>`으로 조회한다. CMake의 `pkg_check_modules`, Meson의 `dependency()`, autotools의 `PKG_CHECK_MODULES`가 모두 내부적으로 pkg-config를 호출한다. Ubuntu 26.04의 `pkg-config`는 **pkgconf** 구현(2.5.1)이다.

```text
# bmi.pc (sysroot 안에 설치된 타깃용)
prefix=/usr
includedir=${prefix}/include
libdir=${prefix}/lib

Name: bmi
Description: BMI270 IMU helper library
Version: 0.3.0
Cflags: -I${includedir}
Libs: -L${libdir} -lbmi
```

pkg-config가 `.pc`를 찾는 경로를 정하는 환경 변수 세 개가 크로스 빌드의 핵심이다.

| 환경 변수 | 효과 | 크로스 빌드에서 |
|---|---|---|
| `PKG_CONFIG_PATH` | 기본 검색 경로 **앞에 추가** | 호스트 기본 경로(`/usr/lib/aarch64-linux-gnu/pkgconfig` 등)가 여전히 살아 있다 → 누수 위험 |
| `PKG_CONFIG_LIBDIR` | 기본 검색 경로를 **통째로 교체** | sysroot의 pkgconfig 디렉터리만 보게 한다 |
| `PKG_CONFIG_SYSROOT_DIR` | 출력되는 `-I`·`-L` 경로 앞에 sysroot를 붙인다 | `.pc`의 `prefix=/usr`(보드 기준 경로)를 빌드 머신의 실제 경로로 바꿔 준다 |

### 12.2 예제 ex12 — 호스트 `.pc`가 타깃 빌드로 새는 사고와 수정

무엇을 확인하나: 같은 `libbmi`를 호스트(aarch64)용으로 `/tmp/B4/hostprefix`에, 타깃(armhf)용으로 sysroot에 설치한다. 셸에 `PKG_CONFIG_PATH`가 호스트 경로로 남아 있는 개발자가 크로스 빌드를 하면 무슨 일이 생기는지, 그리고 `PKG_CONFIG_LIBDIR` + `PKG_CONFIG_SYSROOT_DIR`로 어떻게 고치는지 본다.

```sh
# usage: mklib.sh <CC> <DESTDIR> <prefix>   — libbmi.so + bmi.pc 를 설치
CC=$1; DEST=$2; PFX=$3; SRC=/Users/donh/.elinux_scratch/B4/ex12
mkdir -p $DEST$PFX/include $DEST$PFX/lib/pkgconfig
$CC -Wall -Wextra -O2 -fPIC -shared $SRC/bmi.c -o $DEST$PFX/lib/libbmi.so
cp $SRC/bmi.h $DEST$PFX/include/
# ... 이어서 위의 bmi.pc를 prefix=$PFX로 써 넣는다
```

`DESTDIR`과 `prefix`를 나눈 것이 핵심이다. 타깃용은 `prefix=/usr`(보드에서의 경로)로 빌드하되 실제 파일은 `DESTDIR=$S`(sysroot) 아래에 놓는다 — `make install DESTDIR=...`의 관례와 같다.

```sh
S=/tmp/B4/sysroot-armhf
bash mklib.sh gcc "" /tmp/B4/hostprefix                  # 호스트(aarch64)용 libbmi
bash mklib.sh arm-linux-gnueabihf-gcc $S /usr            # 타깃(armhf)용 libbmi → sysroot 안에

echo "### [leak] PKG_CONFIG_PATH=hostprefix 가 셸에 남아 있는 개발자"
export PKG_CONFIG_PATH=/tmp/B4/hostprefix/lib/pkgconfig
pkg-config --cflags --libs bmi
arm-linux-gnueabihf-gcc --sysroot=$S app.c $(pkg-config --cflags --libs bmi) -o ex12/app 2>&1
echo "### host zlib also visible?"; pkg-config --modversion zlib

echo "### [fix] PKG_CONFIG_LIBDIR + PKG_CONFIG_SYSROOT_DIR"
unset PKG_CONFIG_PATH
export PKG_CONFIG_LIBDIR=$S/usr/lib/pkgconfig PKG_CONFIG_SYSROOT_DIR=$S
pkg-config --cflags --libs bmi
arm-linux-gnueabihf-gcc --sysroot=$S app.c $(pkg-config --cflags --libs bmi) -o ex12/app && file ex12/app | cut -d, -f1-2
qemu-arm -L /usr/arm-linux-gnueabihf -E LD_LIBRARY_PATH=$S/usr/lib ex12/app
echo "### host zlib now?"; pkg-config --modversion zlib; echo "exit=$?"
```

```text
### [leak] PKG_CONFIG_PATH=hostprefix 가 셸에 남아 있는 개발자
-I/tmp/B4/hostprefix/include -L/tmp/B4/hostprefix/lib -lbmi
/tmp/B4/hostprefix/lib/libbmi.so: file not recognized: file format not recognized
collect2: error: ld returned 1 exit status
### host zlib also visible?
1.3.1
### [fix] PKG_CONFIG_LIBDIR + PKG_CONFIG_SYSROOT_DIR
-I/tmp/B4/sysroot-armhf/usr/include -L/tmp/B4/sysroot-armhf/usr/lib -lbmi
ex12/app: ELF 32-bit LSB pie executable, ARM
BMI270 chip id = 0x24
### host zlib now?
Package zlib was not found in the pkg-config search path.
Perhaps you should add the directory containing `zlib.pc'
to the PKG_CONFIG_PATH environment variable
Package 'zlib' not found
exit=1
```

출력에서 볼 것:

- 누수 상태에서 pkg-config는 **호스트 헤더 경로(`-I/tmp/B4/hostprefix/include`)와 호스트 라이브러리 경로**를 타깃 컴파일러에 넘겼다. 이번에는 aarch64 ELF를 armhf 링커가 읽지 못해서 "file format not recognized"로 바로 터졌으니 운이 좋은 경우다. 더 나쁜 경우는 **헤더만 새는** 것이다: 호스트 헤더의 구조체 정의·매크로가 타깃 라이브러리와 다르면 빌드는 성공하고 보드에서 이상하게 동작한다.
- `PKG_CONFIG_PATH`만 쓰면 호스트 기본 경로가 살아 있어서 호스트 zlib(1.3.1)가 "있다"고 답한다. configure·CMake·Meson은 이걸 믿고 zlib 기능을 켠다.
- 수정 후에는 sysroot 안의 `bmi.pc`만 보이고, `prefix=/usr`가 `PKG_CONFIG_SYSROOT_DIR` 덕분에 `/tmp/B4/sysroot-armhf/usr`로 바뀌어 출력됐다. 호스트 zlib는 "없다"고 정직하게 답한다 — 타깃에 zlib가 필요하면 타깃용으로 빌드해서 sysroot에 넣어야 한다는 신호다.

```svg
<svg viewBox="0 0 680 430" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker>
    <marker id="ah5r" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="#d0564a"/></marker>
  </defs>
  <rect x="10" y="20" width="250" height="220" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="135" y="40" text-anchor="middle" font-size="13" font-weight="bold">빌드 호스트 (aarch64 VM)</text>
  <rect x="20" y="52" width="230" height="46" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="135" y="71" text-anchor="middle" font-size="12">호스트 도구 (cmake · ninja)</text>
  <text x="135" y="89" text-anchor="middle" font-size="12">PROGRAM NEVER → 여기서</text>
  <rect x="20" y="108" width="230" height="56" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="135" y="128" text-anchor="middle" font-size="12">/usr/lib/aarch64-linux-gnu</text>
  <text x="135" y="147" text-anchor="middle" font-size="12">zlib.pc · libz.so (aarch64)</text>
  <rect x="20" y="174" width="230" height="56" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="135" y="194" text-anchor="middle" font-size="12">hostprefix/lib/pkgconfig/bmi.pc</text>
  <text x="135" y="213" text-anchor="middle" font-size="12">libbmi.so (aarch64)</text>
  <rect x="420" y="20" width="250" height="220" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="545" y="40" text-anchor="middle" font-size="13" font-weight="bold">sysroot S = 보드 / 의 복사본</text>
  <rect x="430" y="52" width="230" height="46" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="545" y="71" text-anchor="middle" font-size="12">S/usr/include/bmi.h</text>
  <text x="545" y="89" text-anchor="middle" font-size="12">libc 헤더 (armhf)</text>
  <rect x="430" y="108" width="230" height="56" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="545" y="128" text-anchor="middle" font-size="12">S/usr/lib/libbmi.so (armhf)</text>
  <text x="545" y="147" text-anchor="middle" font-size="12">libc · crt*.o (armhf)</text>
  <rect x="430" y="174" width="230" height="56" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="545" y="194" text-anchor="middle" font-size="12">S/usr/lib/pkgconfig/bmi.pc</text>
  <text x="545" y="213" text-anchor="middle" font-size="12">prefix=/usr (보드 기준 경로)</text>
  <rect x="280" y="120" width="120" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="147" text-anchor="middle" font-size="13">pkg-config</text>
  <line x1="430" y1="200" x2="402" y2="158" stroke="#3f9a6b" stroke-width="2" marker-end="url(#ah5)"/>
  <line x1="250" y1="200" x2="282" y2="160" stroke="#d0564a" stroke-width="2" stroke-dasharray="5 3" marker-end="url(#ah5r)"/>
  <text x="300" y="214" font-size="12">LIBDIR로 차단</text>
  <rect x="190" y="280" width="300" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="307" text-anchor="middle" font-size="12">arm-linux-gnueabihf-gcc --sysroot=S</text>
  <line x1="340" y1="164" x2="340" y2="276" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="348" y="250" font-size="12">-I S/usr/include -L S/usr/lib -lbmi</text>
  <line x1="545" y1="240" x2="470" y2="278" stroke="#4a7bd0" stroke-width="2" marker-end="url(#ah5)"/>
  <line x1="340" y1="324" x2="340" y2="346" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="190" y="350" width="300" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="372" text-anchor="middle" font-size="12">armv7 ELF → 보드 (여기선 qemu-arm)</text>
  <text x="10" y="404" font-size="12">PKG_CONFIG_LIBDIR=S/usr/lib/pkgconfig : 검색 경로를 "교체" (PATH는 "추가"라 샌다)</text>
  <text x="10" y="422" font-size="12">PKG_CONFIG_SYSROOT_DIR=S : .pc의 -I/usr/include 를 -I S/usr/include 로 바꿔 출력</text>
</svg>
```
그림 7 — 크로스 빌드에서 호스트와 타깃의 분리. 빌드 도구는 호스트에서 찾고(초록), 헤더·라이브러리·`.pc`는 sysroot에서만 찾는다(파랑). 빨간 점선이 이 예제에서 재현한 누수 경로이고, `PKG_CONFIG_LIBDIR`이 그 길을 막는다.

pkg-config 쪽 크로스 설정을 하는 또 다른 방법은 **triplet 접두사 래퍼**다. Meson·autotools는 크로스 빌드에서 `arm-linux-gnueabihf-pkg-config`가 PATH에 있으면 그것을 우선 쓰는데, 이 래퍼 안에서 위 환경 변수를 설정해 두면 개발자 셸 환경에 덜 휘둘린다(Yocto·Buildroot가 내부적으로 이런 래퍼나 환경 변수를 준비한다).

---

## 13. Buildroot · Yocto는 이것들을 어떻게 몰고 가나 (K3 · K4 미리보기)

Buildroot와 Yocto는 "빌드 시스템의 빌드 시스템"이다. 패키지마다 위에서 본 빌드 시스템 중 하나를 고르고, **같은 툴체인·sysroot·pkg-config 설정을 주입한 뒤** configure → build → install(`DESTDIR`=staging/sysroot)을 반복한다. 패키지 A가 설치한 헤더와 `.pc`가 sysroot에 들어가면, 그다음 패키지 B가 그것을 찾는다. 위의 예제에서 손으로 한 것을 수백 번 자동으로 하는 셈이다.

| 패키지의 빌드 시스템 | Buildroot (`package/<pkg>/<pkg>.mk`) | Yocto (`<pkg>.bb` 레시피) | 내부에서 주입하는 것 |
|---|---|---|---|
| 손으로 쓴 Makefile | `$(eval $(generic-package))` | 기본 `do_compile`이 `oe_runmake` | `CC`, `CFLAGS`, `LDFLAGS` (sysroot 포함) |
| autotools | `$(eval $(autotools-package))` | `inherit autotools` | `--host`, `--build`, `--prefix=/usr`, `ac_cv_` 캐시 변수 |
| CMake | `$(eval $(cmake-package))` | `inherit cmake` | 자동 생성한 toolchain file |
| Meson | `$(eval $(meson-package))` | `inherit meson` | 자동 생성한 cross file |
| 커널 모듈 | `$(eval $(kernel-module))` | `inherit module` | `KERNEL_SRC`, `ARCH`, `CROSS_COMPILE` |
| pkg-config | 호스트 pkgconf 래퍼 | `inherit pkgconfig` | `PKG_CONFIG_LIBDIR`, `PKG_CONFIG_SYSROOT_DIR` |

Yocto로 만든 SDK(`bitbake <image> -c populate_sdk`)를 설치하면 `environment-setup-<target>` 스크립트가 생긴다. 이를 `source`하면 `CC`(이미 `--sysroot=...`와 CPU 플래그가 붙은), `CONFIGURE_FLAGS`(`--host=...` 포함), `PKG_CONFIG_SYSROOT_DIR`, `PKG_CONFIG_PATH`(SDK sysroot만 가리키게), `OECORE_NATIVE_SYSROOT` 같은 변수가 설정되고, SDK에 CMake toolchain file과 Meson cross file도 들어 있다. 앱 개발자는 그 셸에서 `cmake`/`meson`/`./configure $CONFIGURE_FLAGS`를 치기만 하면 된다. 정확한 변수 목록과 파일 위치는 Yocto 버전에 따라 다르므로 K4·B5에서 실제 SDK로 확인한다.

---

## 14. 커널 안에서는 무슨 일이 일어나나

빌드 시스템은 유저 공간 도구지만, Don의 목표인 **드라이버**에서는 빌드 결과물이 커널과 직접 만난다. ex7에서 만든 `b4_hello.ko`가 빌드되고 로드되는 경로를 따라가 보자.

**빌드 쪽 (Kbuild):**

1. `make -C $(KDIR) M=$PWD modules` → 커널 최상위 `Makefile`이 `M`을 보고 외부 모듈 모드로 들어간다.
2. `scripts/Makefile.build`가 `M` 디렉터리의 `Kbuild`를 읽어 `obj-m := b4_hello.o`를 얻고, 커널과 똑같은 플래그(`-nostdinc`, `-include kconfig.h`, `-D__KERNEL__ -DMODULE`, `-mgeneral-regs-only` …)로 `b4_hello.o`를 컴파일한다.
3. `scripts/Makefile.modpost` → **modpost**가 모듈의 미해결 심볼(`printk` 등)을 커널의 `Module.symvers`(내보낸 심볼과 CRC 목록)와 대조하고, `b4_hello.mod.c`를 생성한다. 여기에 `MODULE_INFO(vermagic, ...)`, 모듈 이름, 의존 모듈, 심볼 CRC(`__versions` 섹션)가 들어간다.
4. `b4_hello.o` + `b4_hello.mod.o`(+ 공통 오브젝트)를 `ld -r`로 묶어 재배치 가능한 ELF인 `b4_hello.ko`를 만든다.

**로드 쪽 (커널):**

1. `insmod`는 파일을 열어 `finit_module(fd, "odr_hz=800", 0)` syscall을 호출한다(구형 경로는 `init_module`로 메모리 버퍼를 넘긴다).
2. 커널의 `load_module()`(`kernel/module/main.c`)이 ELF를 검사하고, `.modinfo`의 **vermagic**을 실행 중인 커널과 비교하고, `CONFIG_MODVERSIONS`가 켜져 있으면 `__versions`의 심볼 CRC를 비교한다. 다르면 `disagrees about version of symbol` 또는 `version magic ... should be ...`를 dmesg에 남기고 거부한다. 서명 검증(`CONFIG_MODULE_SIG`)도 여기서 한다.
3. 섹션을 커널 메모리에 배치하고 재배치(relocation)를 적용한다 — B3에서 본 링커 일을 커널이 런타임에 한다.
4. 모듈 파라미터를 파싱해 `odr_hz`에 800을 넣고, `/sys/module/b4_hello/`를 만들고, `module_init`으로 등록된 `b4_hello_init()`을 호출한다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="20" y="20" font-size="13" font-weight="bold">빌드 (개발 머신 · Kbuild)</text>
  <rect x="20" y="32" width="110" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="75" y="59" text-anchor="middle" font-size="12">b4_hello.c</text>
  <rect x="160" y="32" width="160" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="240" y="51" text-anchor="middle" font-size="12">cc + 커널 플래그</text>
  <text x="240" y="68" text-anchor="middle" font-size="12">-include kconfig.h</text>
  <rect x="350" y="32" width="140" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="420" y="51" text-anchor="middle" font-size="12">modpost</text>
  <text x="420" y="68" text-anchor="middle" font-size="12">Module.symvers 대조</text>
  <rect x="520" y="32" width="140" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="590" y="51" text-anchor="middle" font-size="12">.mod.c 생성</text>
  <text x="590" y="68" text-anchor="middle" font-size="12">vermagic · CRC</text>
  <line x1="130" y1="54" x2="156" y2="54" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="320" y1="54" x2="346" y2="54" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="490" y1="54" x2="516" y2="54" stroke="currentColor" marker-end="url(#ah6)"/>
  <rect x="400" y="104" width="260" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="530" y="129" text-anchor="middle" font-size="12">ld -r → b4_hello.ko</text>
  <line x1="590" y1="76" x2="590" y2="100" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="20" y1="170" x2="660" y2="170" stroke="currentColor" stroke-dasharray="6 4"/>
  <text x="20" y="190" font-size="13" font-weight="bold">로드 (보드 · 커널 공간)</text>
  <rect x="20" y="204" width="180" height="50" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="110" y="225" text-anchor="middle" font-size="12">insmod b4_hello.ko</text>
  <text x="110" y="243" text-anchor="middle" font-size="12">→ finit_module()</text>
  <rect x="230" y="204" width="230" height="50" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="345" y="225" text-anchor="middle" font-size="12">load_module()</text>
  <text x="345" y="243" text-anchor="middle" font-size="12">vermagic · CRC · 서명 검사</text>
  <rect x="490" y="204" width="170" height="50" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="575" y="225" text-anchor="middle" font-size="12">재배치 · 파라미터</text>
  <text x="575" y="243" text-anchor="middle" font-size="12">→ b4_hello_init()</text>
  <line x1="530" y1="144" x2="130" y2="200" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="200" y1="229" x2="226" y2="229" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="460" y1="229" x2="486" y2="229" stroke="currentColor" marker-end="url(#ah6)"/>
  <rect x="230" y="276" width="230" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="345" y="301" text-anchor="middle" font-size="12">불일치 → 거부 (dmesg에 이유)</text>
  <line x1="345" y1="254" x2="345" y2="272" stroke="#d0564a" marker-end="url(#ah6)"/>
</svg>
```
그림 8 — out-of-tree 모듈의 빌드와 로드. 빌드 쪽에서 Kbuild가 심은 vermagic과 심볼 CRC를, 로드 쪽에서 커널의 `load_module()`이 실행 중인 커널과 대조한다. "모듈은 정확히 그 커널의 빌드 트리로 빌드해야 한다"는 규칙의 근거다.

드라이버를 쓸 때 이 지식이 다시 나오는 곳:

- **E2 (커널 빌드)**: `make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- imx_v8_defconfig`, `menuconfig`, `savedefconfig`, 설정 조각(fragment) 병합.
- **E3·E4 (모듈·심볼)**: `EXPORT_SYMBOL_GPL`, `Module.symvers`, 모듈 간 의존, `modprobe`와 `depmod`.
- **G (드라이버 모델)**: 실제 드라이버 디렉터리의 `Kconfig`/`Makefile` 항목 추가 — `obj-$(CONFIG_...) += ...` 한 줄을 넣는 것이 드라이버 업스트림 패치의 일부다.
- **K3·K4**: 같은 모듈을 Buildroot `kernel-module` / Yocto `inherit module`로 이미지에 넣기.

---

## 15. Embedded 실무에서 다시 보기

### 15.1 보드 팀이 준 SDK로 센서 데몬 빌드하기

예를 들어 센서 키트가 i.MX8(aarch64) 보드이고 보드 팀이 Yocto SDK를 준다면, 앱 팀의 일상은 이렇다.

```sh
source /opt/fsl-imx-xwayland/<ver>/environment-setup-armv8a-poky-linux   # 경로는 SDK마다 다르다
echo $CC                     # aarch64-poky-linux-gcc --sysroot=... -mcpu=... 처럼 이미 플래그가 붙어 있다
cmake -S . -B build-imx8      # SDK가 toolchain file을 환경에 걸어 둔다 (SDK 버전에 따라 -DCMAKE_TOOLCHAIN_FILE 필요)
cmake --build build-imx8 -j
file build-imx8/sensord       # 반드시 확인: ELF 64-bit ARM aarch64, interpreter /lib/ld-linux-aarch64.so.1
scp build-imx8/sensord root@board:/usr/bin/
```

`file`로 확인하는 습관이 중요하다. "SDK 환경을 source 안 한 터미널에서 빌드해서 x86 바이너리를 보드에 올렸다"는 사고가 실제로 흔하다(B3의 `Exec format error`).

### 15.2 벤더 드라이버를 보드 커널용 모듈로 빌드하기

벤더가 IMU 드라이버를 out-of-tree 소스로 줬다면, ex7과 같은 Makefile에 **보드 커널의 빌드 디렉터리**와 크로스 변수를 준다.

```sh
make -C ~/work/linux-imx/build-imx8 M=$PWD ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- modules
modinfo bmi270.ko | grep vermagic      # 보드의 uname -r 과 같아야 한다
```

`KDIR`은 **그 보드에서 돌고 있는 커널을 빌드한 바로 그 트리**(최소한 같은 `.config`와 `Module.symvers`)여야 한다. 다른 트리로 빌드하면 로드할 때 `version magic` 불일치나 `disagrees about version of symbol`로 거부된다.

### 15.3 CI에서 빌드 시간 줄이기

§5의 숫자가 그대로 적용된다. 매 커밋마다 커널+모듈+앱을 빌드하는 CI라면 (1) `-j$(nproc)`, (2) ccache 또는 Yocto sstate 캐시를 CI 머신 간 공유, (3) 공통 헤더 남용 줄이기, (4) 생성 헤더 의존성을 정확히 적어서 `-j`에서 가끔 깨지는 빌드(§4)를 없애기. "가끔 깨지는 빌드"는 신뢰를 갉아먹으므로 재현될 때 바로 의존성을 고친다.

### 15.4 "보드에서 실행하면 라이브러리를 못 찾는다"

pkg-config가 새서 호스트의 `-L/usr/local/lib`가 링크에 들어가고, 거기서 RPATH나 잘못된 SONAME이 박혀 보드에서 `error while loading shared libraries`가 나는 경우가 있다. 빌드 로그에 sysroot 밖의 `-I`·`-L` 경로(`/usr/include`, `/usr/local/...`, 홈 디렉터리)가 보이면 §12의 누수를 먼저 의심한다. Yocto는 이런 경우를 QA 검사(호스트 경로 오염 경고)로 잡아 준다.

---

## 16. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 패턴 규칙에 헤더 의존성 없음 | 헤더 수정 후 이상한 크래시, `make clean` 하면 고쳐짐 | Make가 `#include`를 모름 → 일부 `.o`만 새 구조체 정의 | `-MMD -MP` + `-include $(OBJS:.o=.d)` |
| `-MP` 누락 | 헤더 삭제·이름 변경 후 `No rule to make target 'x.h'` | 옛 `.d`가 없는 헤더를 요구 | `-MP` 추가 (또는 `.d` 삭제 후 재빌드) |
| 생성 파일 의존성 누락 | `-j`에서만 가끔 `No such file`, 다시 돌리면 성공 | 직렬 순서에 우연히 의존 | 생성 헤더를 사용하는 `.o`의 prerequisite에 명시 |
| 레시피를 스페이스로 들여씀 | `missing separator. Stop.` | 레시피는 TAB으로 시작해야 함 | 에디터에서 Makefile은 TAB 유지 |
| 변수 줄 끝 주석 | 경로에 공백이 섞여 `build /foo` | 주석 앞 공백이 값에 포함 | 변수 정의 줄에는 주석을 따로 위 줄에 |
| 명령행 `CFLAGS=`가 필수 플래그를 지움 | `-fPIC`·`-D` 누락으로 링크/동작 오류 | 명령행 변수가 Makefile의 `+=`를 이김 | 필수 플래그는 별도 변수나 `override` |
| `CROSS_COMPILE` 끝의 `-` 누락 | `arm-linux-gnueabihfgcc: not found` | 접두사는 `-`까지 포함 | `CROSS_COMPILE=arm-linux-gnueabihf-` |
| 툴체인만 바꾸고 같은 빌드 디렉터리 재사용 | 여전히 호스트 컴파일러로 빌드됨 | `CMakeCache.txt`·`config.status`에 옛 컴파일러 고정 | 빌드 디렉터리를 지우고 다시 설정 |
| 크로스 빌드에서 `PKG_CONFIG_PATH`만 설정 | 호스트 라이브러리가 "있다"고 나와 기능이 켜짐, `file format not recognized` | 호스트 기본 검색 경로가 살아 있음 | `PKG_CONFIG_LIBDIR` + `PKG_CONFIG_SYSROOT_DIR` |
| 다른 커널 트리로 모듈 빌드 | `insmod: Invalid module format`, dmesg에 `version magic` | vermagic·심볼 CRC 불일치 | 보드 커널의 빌드 트리(`KDIR`)와 같은 툴체인 사용 |

---

## 17. 면접에서 이렇게 말한다

**Q.** How does make decide what to rebuild?

**A.** Makefile의 규칙으로 타깃→prerequisite 의존성 그래프를 만들고, 루트에서 DFS로 내려가 잎부터 올라오며 "타깃이 없거나 prerequisite보다 mtime이 오래됐으면" 레시피를 실행한다. 그래서 Makefile에 적히지 않은 의존성(헤더, 생성 파일)은 Make가 모른다.

> make builds a dependency graph from the rules and walks it depth-first. A target is rebuilt if it doesn't exist or if any prerequisite has a newer modification time. The key limitation is that make only knows the dependencies you declare, so header dependencies have to be generated by the compiler with -MMD -MP.

**꼬리 질문**: `.PHONY`는 왜 필요한가? → 파일이 아닌 타깃이 같은 이름의 파일 때문에 "up to date"가 되는 것을 막는다.

**Q.** I changed a header and the binary started crashing until I ran make clean. What happened?

**A.** 헤더 의존성이 없어서 그 헤더를 쓰는 오브젝트 일부만 다시 컴파일됐고, 구조체 레이아웃이 다른 오브젝트가 한 바이너리에 섞였다. 실제로 재현하면 stack smashing으로 죽는다. `-MMD -MP`로 컴파일하면서 `.d` 파일을 만들고 `-include`로 읽게 하면 해결된다. `-MP`는 헤더를 지웠을 때 "No rule to make target"을 막는다.

> Classic missing header dependency. Only some objects were recompiled, so you ended up linking objects that disagree on a struct layout — an ABI mismatch inside one binary. The fix is to have the compiler emit dependency files with -MMD -MP and include them in the Makefile. CMake and Meson do this automatically.

**Q.** Why does a build pass with -j1 but fail randomly with -j8?

**A.** 의존성이 빠져 있는데 직렬 실행 순서가 우연히 맞아서 돌던 것이다. 대표적으로 빌드 중에 생성되는 헤더(version.h, 레지스터 맵 헤더)를 쓰는 `.o`가 그 헤더를 prerequisite로 갖지 않는 경우와, 재귀 make에서 디렉터리 간 의존성이 없는 경우다. 순서를 강제하지 말고 진짜 의존성을 적어야 한다.

> The graph is missing an edge, and serial execution happened to run things in a working order. Typical cases are generated headers not listed as prerequisites, and recursive make where one subdirectory depends on another's output. The fix is to declare the real dependency, not to serialize the build.

**Q.** How do you cross-compile a CMake project for an ARM board?

**A.** CMakeLists.txt는 그대로 두고 toolchain file을 넘긴다. `CMAKE_SYSTEM_NAME`(이걸 설정하면 크로스 모드), 크로스 컴파일러, `CMAKE_SYSROOT`, 그리고 `CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER`, `LIBRARY/INCLUDE/PACKAGE ONLY`로 도구는 호스트에서, 라이브러리와 헤더는 sysroot에서만 찾게 한다. 빌드 후 `file`로 아키텍처를 확인하고 qemu-user로 간단히 실행해 본다.

> You don't touch CMakeLists.txt — you pass a toolchain file. It sets CMAKE_SYSTEM_NAME, the cross compiler, CMAKE_SYSROOT, and the find-root-path modes: programs from the host, libraries, headers and packages only from the sysroot. Then I check the output with file and smoke-test it under qemu-user before deploying.

**꼬리 질문**: sysroot가 무엇인가? → 타깃 보드의 `/`처럼 생긴 디렉터리. 컴파일러는 `--sysroot`로 그 아래의 `usr/include`, `usr/lib`만 본다.

**Q.** What is pkg-config and what goes wrong with it when cross-compiling?

**A.** 라이브러리가 `.pc` 파일로 자기 cflags/libs를 알려 주는 조회 도구다. 크로스 빌드에서 `PKG_CONFIG_PATH`만 설정하면 호스트 기본 경로가 살아 있어서 호스트용 헤더·라이브러리가 타깃 빌드에 들어간다. `PKG_CONFIG_LIBDIR`로 검색 경로를 sysroot로 교체하고, `PKG_CONFIG_SYSROOT_DIR`로 `-I/usr/include` 같은 경로 앞에 sysroot를 붙인다.

> pkg-config reads .pc files to give you compile and link flags for a library. When cross-compiling, PKG_CONFIG_PATH only prepends to the default search path, so host .pc files can leak into the target build. You set PKG_CONFIG_LIBDIR to replace the search path with the sysroot's pkgconfig directory, and PKG_CONFIG_SYSROOT_DIR so the returned -I and -L paths are prefixed with the sysroot.

**Q.** How do you build an out-of-tree kernel module?

**A.** Kbuild 파일에 `obj-m := foo.o`를 쓰고, `make -C <커널 빌드 트리> M=$PWD modules`로 커널의 빌드 시스템에 위임한다. 크로스면 `ARCH`와 `CROSS_COMPILE`을 같이 준다. 빌드 트리는 보드에서 돌고 있는 그 커널을 빌드한 트리여야 한다. 그렇지 않으면 vermagic이나 심볼 CRC 불일치로 로드가 거부된다.

> A Kbuild file with obj-m, and then make -C pointing at the kernel build directory with M set to the module source. For a board you add ARCH and CROSS_COMPILE. The build tree has to match the running kernel's config and Module.symvers, otherwise the loader rejects it on vermagic or symbol CRC mismatch.

**꼬리 질문**: `CONFIG_FOO=m`이면 C 코드에서 `#ifdef CONFIG_FOO`가 참인가? → 아니다. `CONFIG_FOO_MODULE`이 정의된다. 그래서 `IS_ENABLED(CONFIG_FOO)`를 쓴다.

**Q.** Make vs CMake vs Meson — when would you use which?

**A.** Make는 실행기다. 작은 펌웨어·데몬이나 커널처럼 이미 Make 기반인 곳에서 쓴다. CMake와 Meson은 컴파일러·라이브러리 검사와 크로스 설정을 표준화한 메타 빌드 시스템이고, 헤더 의존성·out-of-tree 빌드·Ninja 생성을 기본으로 해 준다. 실무에서는 업스트림 프로젝트가 쓰는 것을 따르고, 새 C/C++ 앱이면 생태계가 넓은 CMake, systemd·GStreamer 계열과 붙는다면 Meson을 고른다.

> Make is the executor; CMake and Meson are generators that standardize toolchain detection, dependency discovery and cross-compilation, and emit Ninja files with header dependency tracking built in. In practice you follow whatever upstream uses. For a new C or C++ application I'd default to CMake for ecosystem reasons, or Meson if it sits next to systemd or GStreamer-style projects.

---

## 18. 직접 해보기

1. **개념**: `=`와 `:=`의 차이를 `$(shell date +%s%N)`를 이용해 보여 주는 Makefile을 쓰고, 한 레시피에서 두 변수를 세 번씩 찍어 보라. 정답/힌트: `=`는 참조할 때마다 `date`를 다시 실행해 값이 매번 달라지고, `:=`는 한 번만 실행돼 같은 값이 나온다. 그래서 무거운 `$(shell ...)`은 `:=`로 받는다.
2. **코드 과제**: ex4의 `Makefile.good`에서 `-include`를 `include`(대시 없음)로 바꾸고 `make clean && make`를 해 보라. 무슨 일이 생기고 왜 그런가? 정답/힌트: 첫 빌드에서 `.d`가 없으니 GNU Make는 그 파일을 만들 규칙을 찾다가 `Makefile.inc:8: main.d: No such file or directory` / `No rule to make target 'main.d'.  Stop.`으로 멈춘다(VM에서 확인). `-include`는 없는 파일을 조용히 넘긴다.
3. **관찰**: §5의 200 모듈 프로젝트에서 `make --trace -j4`로 `mod_017.h`를 touch한 뒤 빌드하고, 재컴파일 이유가 찍힌 두 줄을 찾아라. 정답/힌트: `update target 'obj/mod_017.o' due to: src/mod_017.h`와 `obj/main.o`에 대한 같은 줄이 나온다.
4. **코드 과제**: ex9의 CMake 프로젝트를 RISC-V(`riscv64-linux-gnu-gcc`)용 toolchain file로 빌드하고 `qemu-riscv64 -L /usr/riscv64-linux-gnu`로 실행하라. sysroot는 §9.3처럼 심볼릭 링크로 만든다. 정답/힌트: `CMAKE_SYSTEM_PROCESSOR riscv64`, sysroot 링크는 `arm-linux-gnueabihf`를 `riscv64-linux-gnu`로 바꾸면 된다. `file`에 `UCB RISC-V`가 보여야 한다.
5. **Kconfig 과제**: ex8의 Kconfig에서 `IMU_BMI270`의 `select REGMAP_I2C`를 `depends on REGMAP_I2C`로 바꾸고 `--alldefconfig`를 돌려라. IMU 드라이버는 어떻게 되는가? 정답/힌트: `REGMAP_I2C`는 프롬프트가 없고 기본값도 없어 n이므로 IMU 드라이버도 켤 수 없게 되어 `.config`에서 사라진다. "숨은 라이브러리 심볼은 select, 사용자가 고르는 기능은 depends on"이 관례인 이유다.
6. **autotools 관찰**: ex11에서 `./configure --host=arm-linux-gnueabihf CC=gcc`처럼 일부러 컴파일러를 호스트 것으로 덮어쓰고 configure를 돌려 보라. `whether we are cross compiling` 결과와 `file gnssd`의 결과를 보고, 왜 위험한지 설명하라. 정답/힌트: `CC`가 이기므로 호스트 바이너리가 만들어진다. configure 출력의 컴파일러 이름과 `config.log`의 명령줄을 확인하는 습관이 필요하다.
7. **pkg-config 과제**: §12 상태에서 `PKG_CONFIG_SYSROOT_DIR`만 빼고 `pkg-config --cflags --libs bmi`를 실행하면 어떤 출력이 나오며 빌드는 되는가? 그리고 `bmi.pc`의 `prefix`를 `/opt/sensor`로 바꾸면 어떻게 달라지는가? 정답/힌트: pkgconf는 `/usr/include`, `/usr/lib` 같은 "시스템 디렉터리" 플래그를 기본으로 걸러 내므로 출력은 ` -lbmi`뿐이고(VM에서 확인), `--sysroot` 덕분에 gcc가 sysroot의 `usr/include/bmi.h`를 찾아 빌드는 된다. 하지만 prefix가 `/opt/sensor`면 `-I/opt/sensor/include`처럼 **빌드 머신에 없는(또는 엉뚱한) 경로**가 그대로 나와 실패한다. 벤더 라이브러리는 대개 `/opt` 같은 비표준 prefix를 쓰므로 `PKG_CONFIG_SYSROOT_DIR`은 생략하지 않는다.

---

## 19. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| target | 타깃 | Make 규칙이 만들어 내는 파일(또는 phony 이름) |
| prerequisite | 선행 조건 | 타깃을 만들기 전에 최신이어야 하는 파일 |
| recipe | 레시피 | 타깃을 만드는 셸 명령들 (TAB으로 시작) |
| phony target | 가짜 타깃 | 파일이 아닌 타깃 (`clean`, `all`) — `.PHONY`로 선언 |
| pattern rule | 패턴 규칙 | `%.o: %.c`처럼 여러 파일에 적용되는 규칙 |
| automatic variable | 자동 변수 | `$@`, `$<`, `$^` 등 레시피 안에서 현재 매칭을 가리키는 변수 |
| order-only prerequisite | 순서 전용 선행 조건 | prerequisite 목록에서 파이프 기호 뒤에 적은 것 — 존재만 확인하고 mtime은 비교하지 않음 |
| depfile (.d) | 의존성 파일 | 컴파일러가 `-MMD`로 만드는 "이 .o는 이 헤더들이 필요" 규칙 |
| recursive make | 재귀 make | 하위 디렉터리마다 별도의 make 프로세스를 띄우는 구조 |
| Kbuild | 케이빌드 | 커널 전용 Make 프레임워크 (`obj-y`, `obj-m`) |
| Kconfig | 케이컨피그 | 커널 설정 언어 → `.config` → `autoconf.h`·`auto.conf` |
| tristate | 삼상태 | Kconfig 값 y(내장)·m(모듈)·n(없음) |
| out-of-tree module | 트리 밖 모듈 | 커널 소스 바깥에서 `M=`으로 빌드하는 `.ko` |
| vermagic | 버전 매직 | 모듈에 박힌 커널 릴리스·설정 문자열, 로드 시 비교 |
| meta build system | 메타 빌드 시스템 | 빌드 파일(Makefile, build.ninja)을 생성하는 도구 (CMake, Meson, autotools) |
| toolchain file | 툴체인 파일 | CMake에 크로스 컴파일러·sysroot·검색 정책을 알려 주는 파일 |
| cross file | 크로스 파일 | Meson의 toolchain file에 해당하는 `.ini` |
| build / host / target | 빌드·호스트·타깃 머신 | 빌드하는 기계 / 결과가 도는 기계 / (컴파일러의) 코드 생성 대상 |
| sysroot | 시스루트 | 타깃 `/`의 복사본처럼 생긴 디렉터리, 헤더·라이브러리를 여기서 찾음 |
| DESTDIR | 데스트디어 | 설치 시 앞에 붙이는 임시 루트 (prefix는 보드 기준 경로 유지) |
| pkg-config / .pc | 패키지 설정 조회 | 라이브러리의 cflags·libs를 담은 파일과 그 조회 도구 |
| ccache | 컴파일러 캐시 | 같은 입력의 컴파일 결과를 재사용하는 래퍼 |

---

## 20. 요약 & 체크리스트

Make는 의존성 그래프와 mtime으로 "다시 만들 것"을 고르는 실행기이고, 그래프에 빠진 간선(헤더, 생성 파일, 디렉터리 간 의존)이 빌드 버그의 대부분을 만든다 — `-MMD -MP`와 정확한 prerequisite가 처방이다. 커널은 Make 위에 Kbuild(`obj-y`/`obj-m`)와 Kconfig(`.config` → `auto.conf`/`autoconf.h`)를 얹었고, out-of-tree 모듈은 `make -C KDIR M=$PWD`로 그 시스템을 빌려 쓴다. CMake·Meson·autotools는 모두 "설정 → 생성 → 빌드" 구조의 메타 빌드 시스템이고, 크로스 정보는 설정 단계에 toolchain file·cross file·`--host`로 한 번만 넣는다. 크로스 빌드의 핵심은 호스트와 타깃의 분리이며, sysroot와 `PKG_CONFIG_LIBDIR`·`PKG_CONFIG_SYSROOT_DIR`이 그 경계를 지킨다. 이 노트의 측정에서 200 모듈 빌드는 `-j4`로 2.5배 빨라졌고, 헤더 의존성 추적 비용은 무변경 빌드 기준 약 30 ms였다 — 정확성을 버릴 이유가 없다.

- [ ] Make가 무엇을 다시 빌드할지 정하는 규칙(DAG + mtime)을 그림으로 설명할 수 있다
- [ ] `=`, `:=`, `?=`, `+=`의 차이와 명령행 변수의 우선순위를 설명할 수 있다
- [ ] `$@`, `$<`, `$^`, `$*`와 패턴 규칙으로 디렉터리 구조가 있는 Makefile을 쓸 수 있다
- [ ] "헤더를 바꿨는데 안 다시 빌드됨" 버그를 재현하고 `-MMD -MP`로 고칠 수 있다
- [ ] `-j`에서만 깨지는 빌드의 원인을 찾고 의존성을 고칠 수 있다
- [ ] `CROSS_COMPILE` 관례로 Makefile을 크로스 빌드하고 `file`·`qemu-user`로 검증할 수 있다
- [ ] out-of-tree 모듈의 Kbuild/Makefile을 쓰고 `make -C ... M=...`로 빌드할 수 있다
- [ ] Kconfig의 `bool`/`tristate`/`depends on`/`select`와 `=m`이 `CONFIG_X_MODULE`이 되는 것을 설명할 수 있다
- [ ] CMake toolchain file의 각 줄(`CMAKE_SYSTEM_NAME`, `CMAKE_SYSROOT`, `FIND_ROOT_PATH_MODE_*`)이 무엇을 하는지 설명할 수 있다
- [ ] Meson cross file과 `./configure --host`로 같은 프로젝트를 크로스 빌드하고 `config.log`로 실패를 추적할 수 있다
- [ ] pkg-config 누수를 재현하고 `PKG_CONFIG_LIBDIR`·`PKG_CONFIG_SYSROOT_DIR`로 고칠 수 있다
- [ ] Buildroot/Yocto가 각 빌드 시스템에 무엇을 주입하는지 한 문장씩 말할 수 있다

---

## 참고 자료

- GNU Make Manual — https://www.gnu.org/software/make/manual/make.html (특히 "Automatic Variables", "The Two Flavors of Variables", "Catalogue of Built-In Rules", "Parallel Execution")
- GCC Manual, Preprocessor Options (`-M`, `-MM`, `-MD`, `-MMD`, `-MP`, `-MF`, `-MT`) — https://gcc.gnu.org/onlinedocs/gcc/Preprocessor-Options.html
- Peter Miller, "Recursive Make Considered Harmful" (1997)
- Linux kernel docs, Kbuild — https://docs.kernel.org/kbuild/index.html (특히 "Building External Modules" `modules.html`, "Kconfig Language" `kconfig-language.html`, "Linux Kernel Makefiles" `makefiles.html`)
- 커널 소스: `include/linux/kconfig.h`(`IS_ENABLED`), `scripts/kconfig/`, `scripts/Makefile.build`, `scripts/Makefile.modpost`, `kernel/module/main.c`(`load_module`) — https://elixir.bootlin.com/linux/latest/source
- CMake, cmake-toolchains(7) "Cross Compiling" — https://cmake.org/cmake/help/latest/manual/cmake-toolchains.7.html
- Meson, "Cross compilation" — https://mesonbuild.com/Cross-compilation.html
- GNU Autoconf Manual, "Manual Configuration / Specifying Target Triplets" 및 "config.log" — https://www.gnu.org/software/autoconf/manual/
- pkgconf / pkg-config(1) 매뉴얼 (`PKG_CONFIG_PATH`, `PKG_CONFIG_LIBDIR`, `PKG_CONFIG_SYSROOT_DIR`)
- Bootlin, "Embedded Linux system development" 교육 슬라이드 (크로스 컴파일·빌드 시스템 장) — https://bootlin.com/training/embedded-linux/
- Buildroot user manual, "Adding new packages" (generic/autotools/cmake/meson/kernel-module infrastructure) — https://buildroot.org/downloads/manual/manual.html
- Yocto Project Reference Manual, Classes (`autotools`, `cmake`, `meson`, `module`, `pkgconfig`) — https://docs.yoctoproject.org/ref-manual/classes.html
