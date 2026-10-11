# B5. 툴체인 만들기·얻기 — 배포판 패키지 · Arm GNU · Bootlin · crosstool-NG · Buildroot/Yocto SDK

> **이 노트를 다 읽으면**: 툴체인을 얻는 7가지 길(배포판·Arm GNU·Bootlin·벤더 SDK·Yocto/Buildroot SDK·crosstool-NG·Buildroot internal)을 비교해 "우리 팀은 왜 이걸 쓰나"를 설명할 수 있다 · 처음 보는 툴체인의 gcc·libc·커널 헤더 버전과 기본 `-march`/`-mfpu`를 5분 안에 알아낼 수 있다 · 툴체인과 타깃 rootfs가 맞지 않을 때(`GLIBC_2.34 not found`, `ENOSYS`, 로더 없음) 원인을 바로 짚을 수 있다 · Yocto SDK의 `environment-setup-*` 스크립트가 하는 일을 손으로 재현하고 ccache·체크섬·컨테이너로 빌드 환경을 재현 가능하게 만들 수 있다
> **왜 필요한가**: 입사 첫 주에 받는 질문이 "SDK 설치했어요?"이고, 면접에서는 "크로스 툴체인을 어떻게 구했고 타깃과 어떻게 맞췄나요?"가 나온다. 필드에서 "보드에서 실행이 안 돼요"의 절반은 툴체인과 rootfs 불일치다
> **Don 기준 난이도**: 컴파일러 버전을 고정하고 바꿀 때 재검증하는 문화(SSD 펌웨어 양산)는 이미 안다 / Linux 툴체인에만 있는 libc·커널 헤더·sysroot·동적 로더라는 "타깃 rootfs와의 계약"이 새로 배울 부분
> **선행 노트**: B1(크로스 툴체인·triplet·sysroot), B2(glibc vs musl, 동적 로더), B3(ELF·동적 링킹), B4(Make·CMake·pkg-config)

---

## 0. 큰 그림 — "우리 팀은 어떤 툴체인을 쓰나, 왜?"

Don이 SSD 펌웨어 팀에 있을 때를 떠올려 보자. 컴파일러는 아마 Arm Compiler나 `arm-none-eabi-gcc` 특정 버전으로 **고정**되어 있었고, 그 버전을 바꾸는 일은 "전체 회귀 테스트를 다시 돌리는 이벤트"였다. 바이너리는 하나의 이미지로 링크되어 플래시에 그대로 올라갔다. 컴파일러가 만든 코드가 의존하는 것은 `libgcc`와 내가 넣은 startup 코드 정도였다.

Embedded Linux에서는 여기에 **세 가지 계약**이 더 붙는다.

1. **libc 계약**: 내 앱은 타깃 rootfs에 있는 `libc.so`(glibc 또는 musl)를 실행 시점에 불러 쓴다. 빌드할 때 본 libc보다 타깃의 libc가 **오래되면** 실행이 안 된다.
2. **커널 계약**: 툴체인 안의 **커널 헤더**(UAPI 헤더, `linux/*.h`)가 syscall 번호·ioctl 번호·구조체 모양을 정한다. 헤더가 타깃 커널보다 새로우면 "컴파일은 되는데 실행하면 `ENOSYS`"가 된다.
3. **ABI 계약**: float ABI(hard/soft), 명령어 집합(`-march`), 동적 로더 경로(`/lib/ld-linux-armhf.so.3`)가 타깃과 맞아야 한다.

그래서 Linux 팀의 툴체인 선택은 "어떤 gcc 버전이 좋나"가 아니라 **"우리 타깃 rootfs와 계약이 맞는 툴체인을 어디서 얻나"**의 문제다. 선택지는 크게 7가지이고, 위에서 아래로 갈수록 "받아서 그대로 쓰기"에서 "직접 빌드하기"로 옮겨 간다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="235" y="20" text-anchor="middle" font-size="13" font-weight="bold">툴체인 출처 (위: 받아서 그대로 → 아래: 직접 빌드)</text>
  <text x="552" y="20" text-anchor="middle" font-size="13" font-weight="bold">타깃 rootfs와의 일치</text>
  <line x1="22" y1="36" x2="22" y2="384" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah1)"/>
  <text x="14" y="214" font-size="12" transform="rotate(-90 14 214)" text-anchor="middle">통제력·빌드 시간 증가</text>
  <rect x="40" y="32" width="390" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="50" y="49" font-size="13">1. 배포판 패키지</text>
  <text x="50" y="65" font-size="12">apt install gcc-arm-linux-gnueabihf</text>
  <rect x="440" y="32" width="225" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="552" y="56" text-anchor="middle" font-size="12">호스트 배포판의 glibc를 따름</text>
  <rect x="40" y="82" width="390" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="50" y="99" font-size="13">2. Arm GNU Toolchain (옛 Linaro 바이너리)</text>
  <text x="50" y="115" font-size="12">developer.arm.com, 예: 15.2.rel1</text>
  <rect x="440" y="82" width="225" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="552" y="106" text-anchor="middle" font-size="12">Arm이 고른 glibc, 타깃과 별개</text>
  <rect x="40" y="132" width="390" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="50" y="149" font-size="13">3. Bootlin prebuilt</text>
  <text x="50" y="165" font-size="12">toolchains.bootlin.com, arch x libc x stable/bleeding</text>
  <rect x="440" y="132" width="225" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="552" y="156" text-anchor="middle" font-size="12">libc 종류·버전을 내가 고름</text>
  <rect x="40" y="182" width="390" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="50" y="199" font-size="13">4. 벤더 BSP SDK</text>
  <text x="50" y="215" font-size="12">NXP i.MX Yocto SDK, TI Processor SDK, NVIDIA L4T</text>
  <rect x="440" y="182" width="225" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="552" y="206" text-anchor="middle" font-size="12">벤더 rootfs와 짝 — 안전</text>
  <rect x="40" y="232" width="390" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="50" y="249" font-size="13">5. Yocto / Buildroot가 만든 SDK</text>
  <text x="50" y="265" font-size="12">bitbake -c populate_sdk, make sdk</text>
  <rect x="440" y="232" width="225" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="552" y="256" text-anchor="middle" font-size="12">우리 이미지와 같은 빌드 — 보장</text>
  <rect x="40" y="282" width="390" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="50" y="299" font-size="13">6. crosstool-NG</text>
  <text x="50" y="315" font-size="12">ct-ng menuconfig, ct-ng build</text>
  <rect x="440" y="282" width="225" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="552" y="306" text-anchor="middle" font-size="12">모든 버전을 내가 정함 — 내 책임</text>
  <rect x="40" y="332" width="390" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="50" y="349" font-size="13">7. Buildroot internal / Yocto 내부 툴체인</text>
  <text x="50" y="365" font-size="12">이미지 빌드의 첫 단계로 함께 빌드</text>
  <rect x="440" y="332" width="225" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="552" y="356" text-anchor="middle" font-size="12">툴체인이 이미지의 일부</text>
</svg>
```
그림 1 — 툴체인을 얻는 7가지 길. 초록은 "rootfs와 같은 빌드에서 나와 계약이 자동으로 맞는" 경우, 주황은 "툴체인과 rootfs를 사람이 맞춰야 하는" 경우다.

실무에서 이 선택은 대개 **rootfs를 누가 만드느냐**로 결정된다.

- **rootfs를 Yocto로 만드는 팀**(NXP i.MX8, TI AM62 양산 제품에서 흔함): 앱 개발자는 Yocto가 만든 SDK(5번)를 설치해서 쓴다. 커널·부트로더도 같은 툴체인으로 빌드된다.
- **rootfs가 Ubuntu 계열인 팀**(NVIDIA Jetson의 L4T가 대표적): 앱은 보드에서 직접(native) 빌드하거나, 같은 Ubuntu 버전의 배포판 크로스 패키지(1번) 또는 Docker 안의 sysroot로 빌드한다. 커널은 벤더가 지정한 툴체인(4번)으로 크로스 빌드한다.
- **rootfs를 Buildroot로 만드는 팀**(작은 장비, 부팅 시간·크기 중시): Buildroot가 툴체인을 직접 빌드(7번)하거나 Bootlin 툴체인(3번)을 external로 가져다 쓴다.
- **특수 요구가 있는 팀**(오래된 커널·특정 gcc 버전·특이 아키텍처): crosstool-NG(6번)로 직접 만든다.

이 노트는 각 출처를 실제로 받아 보고, 뜯어 보고, 빌드해 보고, 일부러 깨뜨려 본다.

> 실행 환경: 모든 출력은 Lima VM `elinux`(aarch64, `uname -r` = `7.0.0-34-generic`, Ubuntu 26.04, gcc 15.2, Apple Silicon 위 가상화, 4 vCPU를 다른 작업과 공유)에서 실제로 돌린 결과다. 32-bit ARM 바이너리는 `qemu-arm`(QEMU 10.2.1 user mode)으로 실행했다. PID·시간·해시는 실행마다 다르다.

---

## 1. 툴체인을 뜯어 보면 — 4개 부품 + sysroot

### 1.1 직관 — "컴파일러 한 개"가 아니라 "타깃 rootfs의 축소판 + 도구 상자"

B1에서 본 것처럼 크로스 툴체인은 gcc 하나가 아니다. 다음 다섯 덩어리가 한 디렉터리에 묶인 것이다.

| 부품 | 어디서 실행되나 | 하는 일 | 버전이 틀리면 |
|---|---|---|---|
| binutils (`as`, `ld`, `objcopy`, `readelf`) | 호스트 | 어셈블·링크·ELF 조작 | 새 명령어를 어셈블 못 함, 링커 옵션 차이 |
| gcc (`gcc` 드라이버 + `cc1` + `collect2` + `lto1`) | 호스트 | C → 어셈블리, 기본 `-march`/`-mfpu`/float ABI 결정 | 코드 생성 차이, 기본 CPU가 타깃보다 높으면 `SIGILL` |
| C 라이브러리 (glibc / musl / uClibc-ng) | **타깃** (sysroot에 사본) | `printf`, `pthread`, syscall 래퍼, 동적 로더 | `GLIBC_2.xx not found`, 로더 없음 |
| 커널 헤더 (UAPI) | 빌드 시 참조만 | syscall 번호, ioctl 번호, 구조체 정의 | 새 기능 컴파일 불가 또는 런타임 `ENOSYS`/`EINVAL` |
| 호스트 도우미 라이브러리 (gmp, mpfr, mpc, isl, zlib, zstd) | 호스트 | gcc 자체가 쓰는 라이브러리 | 툴체인이 호스트에서 안 뜸 |

여기서 **sysroot**(타깃 루트 파일시스템의 개발용 사본 — 헤더·`.so`·`crt1.o`가 들어 있는 디렉터리)가 핵심이다. 컴파일러는 `#include <stdio.h>`를 호스트 `/usr/include`가 아니라 `<sysroot>/usr/include`에서 찾고, 링커는 `-lc`를 `<sysroot>/lib`에서 찾는다. bare-metal에 비유하면 **"벤더가 준 CMSIS 헤더 + 링커 스크립트 + startup 코드 묶음"이 타깃 OS 전체로 커진 것**이다.

### 1.2 triplet 이름 읽기 — vendor 칸은 장식이다

같은 "32-bit ARM, Linux, hard-float" 타깃이라도 출처마다 이름이 다르다.

| 출처 | triplet (prefix) | 해석 |
|---|---|---|
| Ubuntu/Debian 패키지 | `arm-linux-gnueabihf` | vendor 생략, glibc, EABI hard-float |
| Arm GNU Toolchain | `arm-none-linux-gnueabihf` | vendor = none |
| Bootlin (Buildroot로 빌드) | `arm-buildroot-linux-musleabihf` | vendor = buildroot, musl |
| crosstool-NG 샘플 | `arm-unknown-linux-musleabi` | vendor = unknown, musl, **soft-float ABI** |
| Yocto (Poky) | `arm-poky-linux-gnueabi` + `-mfloat-abi=hard` 옵션 | vendor = poky, float ABI는 `CC` 변수의 옵션으로 지정 |

**vendor 칸(`buildroot`, `none`, `unknown`, `poky`)은 이름일 뿐 ABI에 영향이 없다.** 중요한 것은 마지막 칸(`gnueabihf`, `musleabi`, `gnu`)이 말하는 libc와 float ABI다. 그리고 Yocto처럼 triplet이 `gnueabi`인데 실제로는 `-mfloat-abi=hard` 옵션으로 hard-float를 쓰는 경우도 있으니, **이름을 믿지 말고 §3의 방법으로 직접 확인**한다.

---

## 2. 7가지 출처 비교 — 누가 빌드했고, 무엇과 맞춰져 있나

| 출처 | 누가 빌드 | 호스트 | libc | 버전 통제 | rootfs 일치 | 대표 용도 |
|---|---|---|---|---|---|---|
| 배포판 패키지 | Ubuntu/Debian | 배포판과 같은 arch | 배포판 glibc (예: 26.04는 2.43) | 배포판 릴리스에 묶임 | 같은 배포판 버전 rootfs일 때만 | Ubuntu 기반 보드 앱, 빠른 실험 |
| Arm GNU Toolchain | Arm | x86_64, aarch64 (Linux), 그 외 | Arm이 번들한 glibc | 릴리스 단위 (`14.2.rel1`, `15.2.rel1`) | 사람이 맞춤 | 커널·U-Boot·TF-A 빌드, bare-metal (`arm-none-eabi`) |
| Bootlin prebuilt | Bootlin (Buildroot) | x86_64 | glibc / musl / uClibc-ng 중 선택 | 릴리스 `YYYY.MM-N`, stable/bleeding-edge | 사람이 맞춤 | Buildroot external, 커널 빌드, 벤더가 지정하는 경우 |
| 벤더 BSP SDK | NXP, TI, NVIDIA 등 | 대개 x86_64 | 벤더 rootfs와 같은 것 | 벤더 BSP 릴리스에 묶임 | 벤더 rootfs와 일치 | 평가 보드 bring-up, 초기 개발 |
| Yocto/Buildroot SDK | 우리 빌드 시스템 | 빌드 호스트 arch | 우리 이미지와 같은 것 | 우리 레이어/설정이 정함 | **자동으로 일치** | 양산 제품의 앱 개발 |
| crosstool-NG | 우리 (직접) | 아무 Linux 호스트 | 원하는 것 | 모든 부품 버전을 지정 | 사람이 맞춤 | 특이 요구, 툴체인 자체를 배포하는 팀 |
| Buildroot internal | Buildroot (이미지 빌드 중) | 빌드 호스트 | 설정한 것 | `.config`가 정함 | 자동으로 일치 | 작은 이미지를 처음부터 만드는 팀 |

아래에서 하나씩 본다. 직접 받아 볼 수 있는 것(Bootlin, crosstool-NG, 배포판 패키지)은 실제로 돌리고, 대용량 설치가 필요한 것(Yocto SDK, 벤더 SDK)은 구조를 설명한 뒤 §6에서 핵심 동작을 손으로 재현한다.

### 2.1 배포판 패키지 — `gcc-arm-linux-gnueabihf`

가장 쉬운 길이다. Ubuntu/Debian에서 `apt install gcc-arm-linux-gnueabihf`(64-bit면 `gcc-aarch64-linux-gnu`) 한 줄이면 된다. 이 VM에는 이미 깔려 있다. 어떤 패키지들이 무엇을 들고 오는지 보자.

무엇을 확인하나: 배포판 크로스 툴체인의 구성 패키지와 각 버전.

```sh
limactl shell elinux -- bash -lc 'arm-linux-gnueabihf-gcc --version | head -1; dpkg -S $(readlink -f $(which arm-linux-gnueabihf-gcc)); dpkg-query -W -f="\${Package} \${Version}\n" gcc-arm-linux-gnueabihf libc6-dev-armhf-cross linux-libc-dev-armhf-cross binutils-arm-linux-gnueabihf'
```

```text
arm-linux-gnueabihf-gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0
gcc-15-arm-linux-gnueabihf: /usr/bin/arm-linux-gnueabihf-gcc-15
binutils-arm-linux-gnueabihf 2.46-3ubuntu2
gcc-arm-linux-gnueabihf 4:15.2.0-5ubuntu1
libc6-dev-armhf-cross 2.43-2ubuntu2cross1
linux-libc-dev-armhf-cross 7.0.0-13.13cross1
```

출력에서 볼 것: 툴체인이 **네 개의 배포판 패키지**(gcc, binutils, `libc6-dev-armhf-cross` = glibc 2.43, `linux-libc-dev-armhf-cross` = 커널 7.0 헤더)로 쪼개져 있다. 그리고 sysroot는 `/usr/arm-linux-gnueabihf/`다. 즉 이 툴체인이 만든 바이너리는 **"Ubuntu 26.04 armhf rootfs"를 가정**한다. 타깃이 Ubuntu 26.04 armhf면 완벽하고, 그보다 오래된 glibc를 가진 보드면 §4.1처럼 깨진다.

장점은 설치가 쉽고 보안 업데이트가 배포판을 따라온다는 것, 단점은 **버전을 고를 수 없다**는 것이다. 호스트를 Ubuntu 24.04에서 26.04로 올리는 순간 gcc와 glibc가 함께 바뀐다. 그래서 팀 단위로는 "빌드는 특정 Ubuntu 버전의 Docker 이미지 안에서"로 고정하는 것이 보통이다(§7).

### 2.2 Arm GNU Toolchain — Linaro 바이너리의 후계

Arm이 직접 빌드해 배포하는 GCC 툴체인이다. 예전에는 Linaro가 `gcc-linaro-7.5.0-2019.12-...` 같은 이름으로 바이너리를 냈는데, 이후 Arm이 "GNU Toolchain for the A-profile Architecture"로 이어받았고 지금은 **Arm GNU Toolchain**이라는 이름으로 developer.arm.com에서 받는다. 오래된 BSP 문서에서 "Linaro 툴체인을 받으세요"라는 문장을 보면 지금은 이것을 뜻한다고 생각하면 된다.

파일 이름 규칙은 `arm-gnu-toolchain-<버전>-<호스트>-<타깃>.tar.xz`다. 실제로 어떤 조합이 있는지 HEAD 요청으로 확인했다(받지는 않았다).

```sh
limactl shell elinux -- bash -lc 'for v in 14.2.rel1 14.3.rel1 15.1.rel1 15.2.rel1 15.3.rel1 16.1.rel1; do u=https://developer.arm.com/-/media/Files/downloads/gnu/$v/binrel/arm-gnu-toolchain-$v-aarch64-arm-none-linux-gnueabihf.tar.xz; c=$(curl -s -o /dev/null -w "%{http_code} %{size_download}" -I -L --max-time 20 $u); echo "$v $c"; done'
```

```text
14.2.rel1 200 0
14.3.rel1 200 0
15.1.rel1 404 0
15.2.rel1 200 0
15.3.rel1 404 0
16.1.rel1 404 0
```

```sh
limactl shell elinux -- bash -lc 'u=https://developer.arm.com/-/media/Files/downloads/gnu/15.2.rel1/binrel/arm-gnu-toolchain-15.2.rel1-aarch64-arm-none-linux-gnueabihf.tar.xz; curl -sIL --max-time 20 $u | grep -iE "^HTTP|content-length|content-type"; curl -sL --max-time 20 ${u}.sha256asc | head -3; for h in x86_64 aarch64; do for t in aarch64-none-linux-gnu arm-none-eabi; do c=$(curl -s -o /dev/null -w "%{http_code}" -IL --max-time 20 https://developer.arm.com/-/media/Files/downloads/gnu/15.2.rel1/binrel/arm-gnu-toolchain-15.2.rel1-$h-$t.tar.xz); echo "$h-$t $c"; done; done'
```

```text
HTTP/2 302 
content-length: 269
content-type: text/html; charset=utf-8
x-content-type-options: nosniff
HTTP/1.1 200 OK
Content-Length: 128058412
Content-Type: application/octet-stream
6e5871df39a38b3a4c1374ea06f82fe0fe0c2927ddd2c060401fd60a5d045859  arm-gnu-toolchain-15.2.rel1-aarch64-arm-none-linux-gnueabihf.tar.xz
x86_64-aarch64-none-linux-gnu 200
x86_64-arm-none-eabi 200
aarch64-aarch64-none-linux-gnu 200
aarch64-arm-none-eabi 200
```

출력에서 볼 것:

- 버전 이름은 `<gcc 메이저>.<마이너>.rel<N>` 형식이고, 모든 gcc 릴리스마다 나오는 것은 아니다(15.1은 없다). 2026-10-10에 위 후보들을 찔러 본 범위에서 가장 새 것은 **15.2.rel1**이었다(목록에 없는 버전은 확인하지 않았다).
- 같은 페이지에 `.sha256asc` 체크섬 파일이 있다. 받은 뒤 `sha256sum -c`로 검증하는 것이 기본이다.
- **호스트가 x86_64와 aarch64 둘 다 있다.** Apple Silicon 위 Linux VM이나 Jetson 같은 arm64 호스트에서도 네이티브로 돈다는 뜻이다. 이것이 §2.3의 Bootlin과 다른 점이다.
- 타깃은 Linux용(`aarch64-none-linux-gnu`, `arm-none-linux-gnueabihf`)과 bare-metal용(`arm-none-eabi`, `aarch64-none-elf`)이 한 사이트에 있다. Don이 펌웨어에서 쓰던 `arm-none-eabi-gcc`가 바로 여기서 나온다.

Arm GNU Toolchain은 커널·U-Boot·TF-A처럼 **libc를 쓰지 않는 코드**를 빌드할 때 특히 무난하다. 유저스페이스 앱에 쓸 때는 번들된 glibc 버전이 타깃 rootfs보다 새롭지 않은지 §3 방법으로 꼭 확인한다(이 노트에서는 받지 않았으므로 번들 glibc 버전은 확인하지 않았다).

### 2.3 Bootlin prebuilt — 아키텍처 × libc × 안정도를 고르는 카탈로그

Bootlin(프랑스의 임베디드 Linux 교육·컨설팅 회사, elixir.bootlin.com 운영)은 Buildroot로 수백 개의 툴체인을 빌드해 toolchains.bootlin.com에 공개한다. 장점은 **libc 종류(glibc/musl/uClibc-ng)와 안정도(stable/bleeding-edge)를 골라서** 받을 수 있다는 것이다. 페이지에 박혀 있는 JSON을 파싱해서 최신 릴리스의 조합을 세어 보자.

무엇을 확인하나: Bootlin 카탈로그의 최신 릴리스(2026.08-1)가 몇 개 툴체인을 내는지, 각 툴체인의 부품 버전.

```python
# bootlin_catalog.py <idx.html> : toolchains.bootlin.com 페이지에 박힌 JSON에서 2026.08-1 릴리스 조합을 센다
import re, sys, collections
s = open(sys.argv[1]).read()
names = set(re.findall(r'"name": "([A-Za-z0-9_.+-]+--(?:glibc|musl|uclibc)--(?:stable|bleeding-edge)-2026\.08-1)"', s))
arches = collections.Counter(n.split('--')[0] for n in names)
print("release 2026.08-1: %d toolchains, %d arch variants" % (len(names), len(arches)))
for a in ("armv7-eabihf", "aarch64", "riscv64-lp64d", "x86-64"):
    print("  %-14s" % a, sorted(n.split('--', 1)[1].replace('-2026.08-1', '') for n in names if n.split('--')[0] == a))
for n in ("armv7-eabihf--musl--stable-2026.08-1", "armv7-eabihf--glibc--stable-2026.08-1",
          "aarch64--glibc--stable-2026.08-1", "aarch64--glibc--bleeding-edge-2026.08-1"):
    i = s.find('"name": "%s"' % n)
    m = re.search(r'"summary": (\[\[.*?\]\])', s[i:])
    print("  %-42s %s" % (n, m.group(1).replace('"', '') if m else '?'))
```

```sh
limactl shell elinux -- bash -lc 'mkdir -p /tmp/B5 && cd /tmp/B5 && curl -sL https://toolchains.bootlin.com/ -o idx.html'
limactl shell elinux -- bash -lc 'python3 -I /Users/donh/.elinux_scratch/B5/bootlin_catalog.py /tmp/B5/idx.html'
```

```text
release 2026.08-1: 218 toolchains, 43 arch variants
  armv7-eabihf   ['glibc--bleeding-edge', 'glibc--stable', 'musl--bleeding-edge', 'musl--stable', 'uclibc--bleeding-edge', 'uclibc--stable']
  aarch64        ['glibc--bleeding-edge', 'glibc--stable', 'musl--bleeding-edge', 'musl--stable', 'uclibc--bleeding-edge', 'uclibc--stable']
  riscv64-lp64d  ['glibc--bleeding-edge', 'glibc--stable', 'musl--bleeding-edge', 'musl--stable', 'uclibc--bleeding-edge', 'uclibc--stable']
  x86-64         ['glibc--bleeding-edge', 'glibc--stable', 'musl--bleeding-edge', 'musl--stable', 'uclibc--bleeding-edge', 'uclibc--stable']
  armv7-eabihf--musl--stable-2026.08-1       [[binutils, 2.45.1], [gcc, 15.3.0], [gdb, 16.3], [linux-headers, 5.10.269], [musl, 1.2.6]]
  armv7-eabihf--glibc--stable-2026.08-1      [[binutils, 2.45.1], [gcc, 15.3.0], [gdb, 16.3], [glibc, 2.44-36-g2d5421ffca8893534d5e02ad38c28acd8e778fa3], [linux-headers, 5.10.269]]
  aarch64--glibc--stable-2026.08-1           [[binutils, 2.45.1], [gcc, 15.3.0], [gdb, 16.3], [glibc, 2.44-36-g2d5421ffca8893534d5e02ad38c28acd8e778fa3], [linux-headers, 5.10.269]]
  aarch64--glibc--bleeding-edge-2026.08-1    [[binutils, 2.46.1], [gcc, 16.2.0], [gdb, 17.1], [glibc, 2.44-36-g2d5421ffca8893534d5e02ad38c28acd8e778fa3], [linux-headers, 6.12.108]]
```

출력에서 볼 것:

- 한 릴리스에 **218개** 툴체인(43개 아키텍처 변형 × 최대 3 libc × 2 안정도)이 있다.
- **stable**은 한 단계 앞의 gcc(15.3) + **오래된 커널 헤더(5.10)**, **bleeding-edge**는 최신 gcc(16.2) + 새 헤더(6.12)다. stable의 헤더가 5.10인 것은 의도된 선택이다. 커널 헤더는 "타깃 커널보다 오래되면 안전"하므로(§4.3), 헤더를 낮게 잡아 두면 5.10 이상인 거의 모든 보드에서 쓸 수 있다.
- glibc 버전은 2.44 릴리스 브랜치의 git 스냅샷(`2.44-36-g...`)이다.

이제 실제로 하나 받아 보자. 크기가 작은 `armv7-eabihf--musl--stable`(81 MB)을 고른다.

무엇을 확인하나: 다운로드 + sha256 검증. 파일 이름과 체크섬 URL은 위 JSON의 `tarball-name`과 같은 규칙이다.

```sh
limactl shell elinux -- bash -lc 'cd /tmp/B5 && U=https://toolchains.bootlin.com/downloads/releases/toolchains/armv7-eabihf/tarballs; curl -sfLO $U/armv7-eabihf--musl--stable-2026.08-1.tar.xz && curl -sfLO $U/armv7-eabihf--musl--stable-2026.08-1.sha256; ls -l armv7*; cat armv7*.sha256; sha256sum -c armv7-eabihf--musl--stable-2026.08-1.sha256'
```

```text
-rw-rw-r-- 1 donh donh      110 Oct 10 07:58 armv7-eabihf--musl--stable-2026.08-1.sha256
-rw-rw-r-- 1 donh donh 81278532 Oct 10 07:58 armv7-eabihf--musl--stable-2026.08-1.tar.xz
9147bafae4aa272321a3c6440d04d83b7e23411b2d344f875541d84c4444ba9b  armv7-eabihf--musl--stable-2026.08-1.tar.xz
armv7-eabihf--musl--stable-2026.08-1.tar.xz: OK
```

출력에서 볼 것: `OK`가 나와야 다음 단계로 간다. 팀 스크립트에서는 이 해시 값을 **저장소에 박아 두고**(`sha256sum -c`가 실패하면 빌드 중단) 다운로드 서버가 바뀌거나 파일이 교체되는 것을 막는다(§7).

압축을 풀고 안을 들여다보자.

무엇을 확인하나: 툴체인 tarball의 디렉터리 구조와 크기, 그리고 컴파일러가 **어떤 CPU에서 도는 프로그램인지**.

```sh
limactl shell elinux -- bash -lc 'cd /tmp/B5 && tar xf armv7-eabihf--musl--stable-2026.08-1.tar.xz && T=/tmp/B5/armv7-eabihf--musl--stable-2026.08-1 && du -sh $T && ls $T && du -sh $T/* | sort -h && ls $T/arm-buildroot-linux-musleabihf/bin && file $T/bin/arm-buildroot-linux-musleabihf-gcc.br_real $T/bin/arm-buildroot-linux-musleabihf-gcc'
```

```text
381M	/tmp/B5/armv7-eabihf--musl--stable-2026.08-1
README.txt
arm-buildroot-linux-musleabihf
bin
buildroot.config
etc
include
lib
lib64
libexec
relocate-sdk.sh
share
summary.csv
0	/tmp/B5/armv7-eabihf--musl--stable-2026.08-1/lib64
4.0K	/tmp/B5/armv7-eabihf--musl--stable-2026.08-1/README.txt
4.0K	/tmp/B5/armv7-eabihf--musl--stable-2026.08-1/buildroot.config
4.0K	/tmp/B5/armv7-eabihf--musl--stable-2026.08-1/relocate-sdk.sh
8.0K	/tmp/B5/armv7-eabihf--musl--stable-2026.08-1/etc
12K	/tmp/B5/armv7-eabihf--musl--stable-2026.08-1/summary.csv
252K	/tmp/B5/armv7-eabihf--musl--stable-2026.08-1/include
18M	/tmp/B5/armv7-eabihf--musl--stable-2026.08-1/share
76M	/tmp/B5/armv7-eabihf--musl--stable-2026.08-1/bin
82M	/tmp/B5/armv7-eabihf--musl--stable-2026.08-1/arm-buildroot-linux-musleabihf
96M	/tmp/B5/armv7-eabihf--musl--stable-2026.08-1/lib
111M	/tmp/B5/armv7-eabihf--musl--stable-2026.08-1/libexec
ar
as
ld
ld.bfd
nm
objcopy
objdump
ranlib
readelf
strip
/tmp/B5/armv7-eabihf--musl--stable-2026.08-1/bin/arm-buildroot-linux-musleabihf-gcc.br_real: ELF 64-bit LSB executable, x86-64, version 1 (GNU/Linux), dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2, BuildID[sha1]=2496d4d6827ddd96930fa108c214e090b791dc7e, for GNU/Linux 3.2.0, not stripped
/tmp/B5/armv7-eabihf--musl--stable-2026.08-1/bin/arm-buildroot-linux-musleabihf-gcc:         symbolic link to toolchain-wrapper
```

(실제로는 `ls`와 `du`를 나눠 실행했고 출력은 순서대로 이어 붙였다.)

출력에서 볼 것:

- 81 MB tarball이 풀면 **381 MB**다. 가장 큰 것은 `libexec`(111 MB — `cc1` 하나가 35 MB)와 `lib`(96 MB — gdb용 Python 3.14 포함)다. **타깃으로 가는 것(sysroot)은 36 MB**뿐이다.
- `arm-buildroot-linux-musleabihf/bin/`에 prefix 없는 `as`, `ld`가 또 있다. gcc 드라이버가 내부적으로 부르는 사본이다.
- **gcc가 x86-64 프로그램이다.** 이 VM은 aarch64라서 그대로는 실행되지 않는다. Bootlin 툴체인은 x86_64 호스트용으로 빌드되어 있다(적어도 이 tarball은 그렇다). Apple Silicon Mac이나 arm64 빌드 서버를 쓰는 팀이 실제로 부딪히는 문제다.

이 VM에서는 해결책으로 **qemu-user가 x86-64 프로그램을 에뮬레이션**하게 했다. binfmt_misc에 `qemu-x86_64`가 이미 등록되어 있으니, x86-64용 glibc 런타임만 따로 풀어 주면 된다(`apt install`이 아니라 `.deb`를 받아 `dpkg-deb -x`로 `/tmp/B5/x86root`에 풀었다).

```sh
limactl shell elinux -- bash -lc 'V=$(dpkg-query -W -f="\${Version}" libc6); mkdir -p /tmp/B5/x86root && cd /tmp/B5 && curl -sfLO http://archive.ubuntu.com/ubuntu/pool/main/g/glibc/libc6_${V}_amd64.deb && dpkg-deb -x libc6_${V}_amd64.deb x86root && rm libc6_*.deb && cd x86root && ln -s usr/lib lib && mkdir lib64 && ln -s ../usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2 lib64/ld-linux-x86-64.so.2 && cat /proc/sys/fs/binfmt_misc/qemu-x86_64; export QEMU_LD_PREFIX=/tmp/B5/x86root; time /tmp/B5/armv7-eabihf--musl--stable-2026.08-1/bin/arm-buildroot-linux-musleabihf-gcc --version'
```

```text
enabled
interpreter /usr/bin/qemu-x86_64
flags: POF
offset 0
magic 7f454c4602010100000000000000000002003e00
mask fffffffffffefefcfffffffffffffffffeffffff
arm-buildroot-linux-musleabihf-gcc.br_real (toolchains.bootlin.com 2026.08-1) 15.3.0
Copyright (C) 2025 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.


real	0m0.052s
user	0m0.049s
sys	0m0.003s
```

(실제로는 `.deb` 받기·풀기와 심볼릭 링크 만들기를 두 번에 나눠 실행했고, 출력은 그대로 이었다.)

출력에서 볼 것: 커널이 ELF 헤더의 machine 필드(`3e00` = x86-64)를 보고 `qemu-x86_64`를 끼워 실행한다. `QEMU_LD_PREFIX`는 qemu가 `/lib64/ld-linux-x86-64.so.2` 같은 경로를 찾을 때 앞에 붙이는 루트다. 이제 x86-64 gcc가 aarch64 VM에서 armv7 코드를 만든다 — **에뮬레이션 위의 크로스 컴파일**이다. 단 느리다(§7.3에서 약 11배로 측정).

### 2.4 툴체인 tarball 해부도

```svg
<svg viewBox="0 0 680 470" xmlns="http://www.w3.org/2000/svg">
  <rect x="10" y="12" width="300" height="30" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="20" y="32" font-size="13">armv7-eabihf--musl--stable-2026.08-1/</text>
  <line x1="24" y1="42" x2="24" y2="440" stroke="currentColor"/>
  <line x1="24" y1="66" x2="40" y2="66" stroke="currentColor"/>
  <rect x="40" y="52" width="270" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="50" y="71" font-size="13">bin/ (76M)</text>
  <text x="320" y="64" font-size="12">arm-...-gcc → toolchain-wrapper</text>
  <text x="320" y="79" font-size="12">gcc.br_real, as, ld, gdb (x86-64 ELF)</text>
  <line x1="24" y1="110" x2="40" y2="110" stroke="currentColor"/>
  <rect x="40" y="96" width="270" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="50" y="115" font-size="13">libexec/gcc/.../15.3.0/ (111M)</text>
  <text x="320" y="108" font-size="12">cc1 (35M), cc1plus, lto1, collect2</text>
  <text x="320" y="123" font-size="12">= 진짜 컴파일러 본체, 호스트에서 실행</text>
  <line x1="24" y1="154" x2="40" y2="154" stroke="currentColor"/>
  <rect x="40" y="140" width="270" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="50" y="159" font-size="13">lib/ (96M)</text>
  <text x="320" y="152" font-size="12">gcc/.../15.3.0: libgcc.a, crtbegin.o</text>
  <text x="320" y="167" font-size="12">+ 호스트 라이브러리, gdb용 python3.14</text>
  <line x1="24" y1="198" x2="40" y2="198" stroke="currentColor"/>
  <rect x="40" y="184" width="270" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="50" y="203" font-size="13">arm-buildroot-linux-musleabihf/ (82M)</text>
  <line x1="56" y1="212" x2="56" y2="330" stroke="currentColor"/>
  <line x1="56" y1="236" x2="72" y2="236" stroke="currentColor"/>
  <rect x="72" y="224" width="238" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="82" y="242" font-size="13">bin/ as ld (prefix 없는 사본)</text>
  <line x1="56" y1="276" x2="72" y2="276" stroke="currentColor"/>
  <rect x="72" y="262" width="238" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.2" stroke="#3f9a6b" stroke-width="2"/>
  <text x="82" y="281" font-size="13">sysroot/ (36M) = 타깃 사본</text>
  <text x="320" y="262" font-size="12">lib/ld-musl-armhf.so.1 → libc.so</text>
  <text x="320" y="277" font-size="12">lib/crt1.o crti.o, libatomic.so</text>
  <text x="320" y="292" font-size="12">usr/include/ (libc + 커널 5.10 헤더)</text>
  <line x1="56" y1="318" x2="72" y2="318" stroke="currentColor"/>
  <rect x="72" y="306" width="238" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="82" y="324" font-size="13">include/c++/15.3.0/</text>
  <line x1="24" y1="362" x2="40" y2="362" stroke="currentColor"/>
  <rect x="40" y="348" width="270" height="28" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="50" y="367" font-size="13">share/ (18M), etc/meson/</text>
  <text x="320" y="367" font-size="12">문서, meson cross 파일</text>
  <line x1="24" y1="406" x2="40" y2="406" stroke="currentColor"/>
  <rect x="40" y="392" width="270" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="50" y="411" font-size="13">relocate-sdk.sh</text>
  <text x="320" y="404" font-size="12">옮긴 위치로 내부 경로를 고쳐 씀</text>
  <text x="320" y="419" font-size="12">(share/buildroot/sdk-location 기준)</text>
  <line x1="24" y1="440" x2="40" y2="440" stroke="currentColor"/>
  <rect x="40" y="428" width="270" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="50" y="447" font-size="13">README.txt, buildroot.config</text>
  <text x="320" y="447" font-size="12">summary.csv = 부품 버전·라이선스 목록</text>
</svg>
```
그림 2 — Bootlin 툴체인 tarball 해부도(크기는 위 `du` 실측). 파랑은 **호스트에서 실행되는 것**, 초록은 **타깃으로 가거나 타깃 코드에 링크되는 것**, 주황은 재현·이동용 메타데이터다. 381 MB 중 타깃 쪽은 36 MB뿐이다.

Buildroot 계열 툴체인은 `arm-...-gcc`가 진짜 gcc가 아니라 **`toolchain-wrapper`**라는 작은 프로그램이다. 래퍼는 `--sysroot`와 Buildroot가 정한 보안 플래그를 덧붙여 `gcc.br_real`을 부른다. 환경 변수 `BR2_DEBUG_WRAPPER=1`을 주면 실제로 무엇을 붙이는지 보여 준다.

무엇을 확인하나: 래퍼가 내 명령에 몰래 덧붙이는 옵션.

```sh
limactl shell elinux -- bash -lc 'export QEMU_LD_PREFIX=/tmp/B5/x86root; T=/tmp/B5/armv7-eabihf--musl--stable-2026.08-1; BR2_DEBUG_WRAPPER=1 $T/bin/arm-buildroot-linux-musleabihf-gcc -c -O2 /Users/donh/.elinux_scratch/B5/hello_target.c -o /tmp/B5/h.o 2>&1 | fold -w 150; ls -l $T/bin/arm-linux-gcc $T/bin/arm-buildroot-linux-musleabihf-gcc'
```

```text
Toolchain wrapper was called with: '/tmp/B5/armv7-eabihf--musl--stable-2026.08-1/bin/arm-buildroot-linux-musleabihf-gcc' '-c' '-O2' '/Users/donh/.elin
ux_scratch/B5/hello_target.c' '-o' '/tmp/B5/h.o'
Toolchain wrapper executing: '/tmp/B5/armv7-eabihf--musl--stable-2026.08-1/bin/arm-buildroot-linux-musleabihf-gcc.br_real' '--sysroot' '/tmp/B5/armv7-
eabihf--musl--stable-2026.08-1/arm-buildroot-linux-musleabihf/sysroot' '-fstack-protector-strong' '-fPIE' '-pie' '-Wl,-z,now' '-Wl,-z,relro' '-c' '-O2
' '/Users/donh/.elinux_scratch/B5/hello_target.c' '-o' '/tmp/B5/h.o'
lrwxrwxrwx 1 donh donh 17 Sep  5 08:08 /tmp/B5/armv7-eabihf--musl--stable-2026.08-1/bin/arm-buildroot-linux-musleabihf-gcc -> toolchain-wrapper
lrwxrwxrwx 1 donh donh 17 Sep  5 08:08 /tmp/B5/armv7-eabihf--musl--stable-2026.08-1/bin/arm-linux-gcc -> toolchain-wrapper
```

출력에서 볼 것:

- 래퍼가 **자기 위치 기준으로 계산한 `--sysroot`**를 붙인다. 그래서 tarball을 어느 디렉터리에 풀어도 동작한다(relocatable).
- `-fstack-protector-strong -fPIE -pie -Wl,-z,now -Wl,-z,relro`가 기본으로 들어간다. 이 툴체인으로 만든 바이너리는 내가 지정하지 않아도 PIE + stack protector다. "왜 내 바이너리 크기·성능이 배포판 gcc와 다르지?"의 답이 여기 있을 수 있다.
- `arm-linux-gcc`라는 짧은 이름도 같은 래퍼다. 오래된 Makefile이 `CROSS_COMPILE=arm-linux-`를 가정할 때 쓴다.

> Don의 경험과 연결: 펌웨어 팀에서 "컴파일러가 몰래 넣는 옵션"은 보통 IDE 프로젝트 설정에 숨어 있었다. Linux에서는 래퍼(Buildroot), 스펙 파일(`gcc -dumpspecs`), 환경 변수(`CC`에 붙은 옵션, Yocto)에 숨어 있다. 바이너리가 이상하면 **실제로 실행된 명령줄**부터 확인한다(`-v`, `BR2_DEBUG_WRAPPER=1`, `make V=1`).

### 2.5 Buildroot — internal 툴체인 vs external 툴체인

Buildroot(K 모듈에서 자세히)는 rootfs 이미지를 만드는 빌드 시스템인데, 툴체인을 두 방식으로 다룬다.

| 방식 | 설정 | 동작 | 장단점 |
|---|---|---|---|
| internal (Buildroot toolchain) | `BR2_TOOLCHAIN_BUILDROOT=y` | binutils·gcc·libc를 소스에서 빌드한 뒤 패키지 빌드 시작 | 버전·옵션 완전 통제 / 첫 빌드가 수십 분 더 걸림 |
| external (미리 빌드된 것) | `BR2_TOOLCHAIN_EXTERNAL=y` + `BR2_TOOLCHAIN_EXTERNAL_BOOTLIN` 또는 `..._CUSTOM` | 지정한 tarball을 풀고 libc `.so`를 rootfs로 복사 | 빠름 / 툴체인의 libc·헤더 버전을 받아들여야 함 |

external을 쓸 때 Buildroot는 "이 툴체인의 libc는 glibc인가 musl인가, 커널 헤더는 몇 버전인가, C++/스레드를 지원하나"를 설정으로 알려 달라고 하고, 빌드 시작 때 실제 툴체인과 대조해서 틀리면 멈춘다. §3의 "신원 조회"를 Buildroot가 자동으로 하는 셈이다.

그리고 Buildroot는 `make sdk`로 **자기가 쓴 툴체인 + 지금 만든 rootfs의 sysroot**를 하나의 relocatable tarball로 묶어 준다. Bootlin 툴체인의 `README.txt`가 바로 그 과정이다.

```text
For those who would like to reproduce the toolchain, you can just
follow these steps:

    git clone https://github.com/bootlin/buildroot-toolchains.git buildroot
    cd buildroot
    git checkout toolchains.bootlin.com-2026.08-1

    curl https://toolchains.bootlin.com/downloads/releases/toolchains/armv7-eabihf/build_fragments/armv7-eabihf--musl--stable-2026.08-1.defconfig > .config
    make olddefconfig
    make
    make sdk
```

(위는 tarball 안 `README.txt`의 일부를 그대로 옮긴 것이다.) 즉 Bootlin 툴체인 = "rootfs 없이 툴체인만 만드는 Buildroot 설정"으로 `make sdk`를 한 결과물이고, `relocate-sdk.sh`도 그 SDK 기능에서 온다. 재현 방법(git 태그 + defconfig)까지 적혀 있다는 점이 §7의 재현성 이야기와 이어진다.

### 2.6 Yocto/OpenEmbedded SDK — `populate_sdk`와 `environment-setup-*`

Yocto(K 모듈)를 쓰는 팀에서 앱 개발자가 받는 것은 **SDK 설치 스크립트**다. 이미지 레시피에 대해 다음을 실행하면 만들어진다(이 VM에서는 Yocto 빌드를 돌리지 않았다 — 수십 GB, 수 시간 작업이다. 아래는 Yocto 문서 기준 설명이다).

```sh
bitbake core-image-minimal -c populate_sdk        # 표준 SDK
bitbake core-image-minimal -c populate_sdk_ext    # extensible SDK (devtool 포함)
```

결과는 `tmp/deploy/sdk/` 아래 `poky-glibc-x86_64-core-image-minimal-<tune>-<machine>-toolchain-<ver>.sh` 같은 이름의 **자기 압축 해제 셸 스크립트**다. 설치하면 디렉터리 두 개와 스크립트 하나가 생긴다.

| 설치 결과 | 내용 | 비유 |
|---|---|---|
| `sysroots/<host>-pokysdk-linux/` | 호스트에서 도는 도구: 크로스 gcc, binutils, pkg-config, cmake 래퍼 등 | Bootlin의 `bin/`·`libexec/` |
| `sysroots/<tune>-poky-linux/` | **이미지와 같은 레시피로 빌드된** 타깃 sysroot: 헤더, `.so`, `.pc` 파일 | Bootlin의 `sysroot/`인데 우리 이미지의 라이브러리가 다 들어 있음 |
| `environment-setup-<tune>-poky-linux` | `source`하면 `CC`, `CFLAGS`, `PKG_CONFIG_*` 등을 export | §6에서 손으로 재현 |

이 SDK의 가장 큰 가치는 **타깃 sysroot가 실제 이미지와 같은 빌드에서 나온다**는 것이다. 이미지에 OpenCV 4.10과 glibc 2.39가 들어갔다면 SDK sysroot에도 정확히 그것이 들어 있다. 그래서 "내 PC에서는 링크되는데 보드에서는 안 된다"가 원천적으로 줄어든다. 대신 이미지가 바뀌면(레이어 업데이트) SDK도 다시 뽑아 배포해야 한다.

`environment-setup-*` 스크립트가 export하는 주요 변수는 다음과 같다(정확한 값은 머신 설정마다 다르다).

| 변수 | 예시 형태 | 누가 읽나 |
|---|---|---|
| `SDKTARGETSYSROOT` | `/opt/poky/<ver>/sysroots/cortexa53-poky-linux` | 다른 변수들이 참조 |
| `CC` | `aarch64-poky-linux-gcc -mcpu=cortex-a53 ... --sysroot=$SDKTARGETSYSROOT` | Make, autotools, CMake(첫 configure) |
| `CFLAGS`, `LDFLAGS` | `-O2 -pipe -g ...`, `-Wl,-O1 -Wl,--hash-style=gnu -Wl,--as-needed` | 빌드 시스템 |
| `PKG_CONFIG_SYSROOT_DIR`, `PKG_CONFIG_PATH` | `$SDKTARGETSYSROOT`, `$SDKTARGETSYSROOT/usr/lib/pkgconfig:...` | pkg-config |
| `CONFIGURE_FLAGS` | `--target=... --host=... --build=... --with-libtool-sysroot=...` | `./configure $CONFIGURE_FLAGS` |
| `ARCH`, `CROSS_COMPILE` | `arm64`, `aarch64-poky-linux-` | 커널 Kbuild, U-Boot |
| `OECORE_NATIVE_SYSROOT`, `OECORE_TARGET_SYSROOT` | 두 sysroot 경로 | SDK의 cmake 툴체인 파일 등 |

핵심 관찰: **`CC` 안에 `--sysroot`와 CPU 튜닝 옵션이 들어 있다.** 그래서 SDK 환경에서는 Makefile에 `CC=gcc`를 하드코딩하면 안 되고, `$(CC)`를 그대로 써야 한다. `CC`를 덮어쓰는 순간 sysroot와 `-mcpu`가 사라진다(흔한 실수 표 참고).

### 2.7 벤더 BSP SDK — NXP, TI, NVIDIA

SoC 벤더는 평가 보드용 BSP와 함께 툴체인을 "이걸 쓰라"고 지정한다. 형태는 벤더마다 다르다(아래는 각 벤더 문서를 기억에 기반해 정리한 것이므로, 실제 버전은 사용하는 BSP 릴리스의 문서로 확인해야 한다).

| 벤더 | 툴체인 형태 | 특징 |
|---|---|---|
| NXP i.MX (8M, 8M Plus, 93 등) | Yocto 기반 i.MX BSP에서 `populate_sdk`로 만든 SDK 설치 스크립트 | §2.6과 같은 구조. 벤더 레이어(`meta-imx`)가 들어간 sysroot |
| TI Sitara (AM62x, AM64x 등) | Processor SDK Linux 설치본 안에 Arm GNU Toolchain + Yocto로 만든 `linux-devkit` sysroot | 커널·U-Boot는 Arm GNU, 앱은 devkit의 `environment-setup` |
| NVIDIA Jetson (L4T / JetPack) | rootfs는 Ubuntu. 커널 크로스 빌드용으로 문서가 특정 Bootlin 툴체인을 지정 | 앱은 보드에서 native 빌드하거나 같은 Ubuntu 버전 sysroot로 크로스 |

벤더 SDK의 장점은 **벤더가 검증한 조합**이라는 것, 단점은 **벤더 릴리스 주기에 묶인다**는 것이다. 보통 bring-up은 벤더 SDK로 시작하고, 제품화 단계에서 자체 Yocto 레이어로 옮겨 가며 SDK도 자체 `populate_sdk` 결과물로 바뀐다(P 모듈).

### 2.8 crosstool-NG — 툴체인 공장

crosstool-NG(ct-ng)는 "툴체인만 만드는 빌드 시스템"이다. 커널처럼 `menuconfig`로 아키텍처·gcc·binutils·libc·커널 헤더 버전을 고르고 `ct-ng build`하면 binutils → 커널 헤더 → gcc 1차 → libc → gcc 최종 순서로 전부 소스에서 빌드한다. Buildroot internal 툴체인과 하는 일은 같지만, rootfs를 만들지 않고 **툴체인을 독립 배포물로** 만든다는 점이 다르다. §5에서 실제로 빌드해 본다.

---

## 3. 처음 보는 툴체인 "신원 조회" — 5분 체크리스트

### 3.1 무엇을 알아내야 하나

인수인계받은 프로젝트에 `toolchain/` 디렉터리가 있다. 이것이 무엇인지 알려면 다음 여섯 가지를 확인한다.

1. **triplet** — `gcc -dumpmachine`
2. **gcc 버전** — `gcc -dumpfullversion`
3. **sysroot 위치** — `gcc -print-sysroot`
4. **configure 플래그** — `gcc -v`의 `Configured with:` 줄. 특히 `--with-arch`, `--with-cpu`, `--with-fpu`, `--with-float`, `--with-sysroot`
5. **기본 타깃 옵션** — `gcc -Q --help=target`의 `-march=`, `-mcpu=`, `-mfpu=`, `-mfloat-abi=`
6. **libc 종류·버전과 커널 헤더 버전** — sysroot의 `linux/version.h`, `features.h`, 그리고 libc를 직접 실행

### 3.2 스크립트로 만들기

무엇을 확인하나: 위 여섯 가지를 한 번에 뽑는 스크립트를 Bootlin 툴체인과 Ubuntu 배포판 툴체인에 각각 돌린다.

```sh
#!/bin/bash
# inspect_tc.sh <gcc 경로> : 처음 보는 툴체인의 "신원 조회"
CC=$1
echo "== $CC"
echo "triplet       : $($CC -dumpmachine)"
echo "gcc version   : $($CC -dumpfullversion)"
SR=$($CC -print-sysroot)
echo "sysroot       : ${SR:-(empty: headers/libs in default paths)}"
echo "configured    :"
$CC -v 2>&1 | grep -o -E -- "--(with-(arch|cpu|fpu|float|abi|mode|sysroot|build-sysroot)|enable-(multilib|multiarch)|disable-multilib|with-pkgversion)[^ ]*" | sed 's/^/    /'
echo "target dflts  :"
$CC -Q --help=target 2>/dev/null | grep -E "^ +-m(arch|cpu|fpu|float-abi|tune|abi)=" | sed 's/^ */    /'
# 커널 헤더 버전: 컴파일러가 실제로 찾는 linux/version.h를 쓴다
KV=$(echo '#include <linux/version.h>
KVER LINUX_VERSION_CODE' | $CC -E -P -x c - 2>/dev/null | awk '/^KVER/{print $2}')
echo "kernel hdrs   : $((KV>>16)).$(((KV>>8)&255)).$((KV&255))  (LINUX_VERSION_CODE=$KV)"
VH=$(echo '#include <linux/version.h>' | $CC -E -x c - 2>/dev/null | grep -m1 -o '"[^"]*linux/version.h"')
echo "version.h     : $VH"
echo '#include <features.h>
GLIBC __GLIBC__ __GLIBC_MINOR__' | $CC -E -P -x c - 2>/dev/null | grep "^GLIBC" | sed 's/GLIBC/glibc hdrs    :/'
# libc 런타임 버전: sysroot 안의 libc를 qemu로 직접 실행해 본다 (32-bit ARM만)
if [ -n "$SR" ] && [ "$SR" != "/" ]; then L=$SR; else L=/usr/$($CC -dumpmachine); fi
for f in $L/lib/ld-musl-*.so.1 $L/lib/libc.so.6; do
  [ -e "$f" ] || continue
  echo "libc runtime  : $(qemu-arm -L $L $f 2>&1 | grep -E "musl libc|^Version|GNU C Library" | tr "\n" " ")"
done
```

```sh
limactl shell elinux -- bash -lc 'export QEMU_LD_PREFIX=/tmp/B5/x86root; S=/Users/donh/.elinux_scratch/B5/inspect_tc.sh; $S /tmp/B5/armv7-eabihf--musl--stable-2026.08-1/bin/arm-buildroot-linux-musleabihf-gcc; echo; $S arm-linux-gnueabihf-gcc'
```

```text
== /tmp/B5/armv7-eabihf--musl--stable-2026.08-1/bin/arm-buildroot-linux-musleabihf-gcc
triplet       : arm-buildroot-linux-musleabihf
gcc version   : 15.3.0
sysroot       : /tmp/B5/armv7-eabihf--musl--stable-2026.08-1/arm-buildroot-linux-musleabihf/sysroot
configured    :
    --with-sysroot=/builds/buildroot.org/toolchains-builder/build/armv7-eabihf--musl--stable-2026.08-1/arm-buildroot-linux-musleabihf/sysroot
    --disable-multilib
    --with-pkgversion='toolchains.bootlin.com
    --with-abi=aapcs-linux
    --with-cpu=cortex-a9
    --with-fpu=vfpv3-d16
    --with-float=hard
    --with-mode=arm
target dflts  :
    -mabi=                      		aapcs-linux
    -march=                     		armv7-a+mp+sec+fp
    -mcpu=                      		cortex-a9
    -mfloat-abi=                		hard
    -mfpu=                      		vfpv3-d16
    -mtune=                     		
kernel hdrs   : 5.10.255  (LINUX_VERSION_CODE=330495)
version.h     : "/tmp/B5/armv7-eabihf--musl--stable-2026.08-1/arm-buildroot-linux-musleabihf/sysroot/usr/include/linux/version.h"
glibc hdrs    : __GLIBC__ __GLIBC_MINOR__
libc runtime  : musl libc (armhf) Version 1.2.6 

== arm-linux-gnueabihf-gcc
triplet       : arm-linux-gnueabihf
gcc version   : 15.2.0
sysroot       : /
configured    :
    --with-pkgversion='Ubuntu
    --with-sysroot=/
    --enable-multiarch
    --with-arch=armv7-a+fp
    --with-float=hard
    --with-mode=thumb
target dflts  :
    -mabi=                      		aapcs-linux
    -march=                     		armv7-a+fp
    -mcpu=                      		
    -mfloat-abi=                		hard
    -mfpu=                      		auto
    -mtune=                     		
kernel hdrs   : 7.0.0  (LINUX_VERSION_CODE=458752)
version.h     : "/usr/arm-linux-gnueabihf/include/linux/version.h"
glibc hdrs    : 2 43
libc runtime  : GNU C Library (Ubuntu GLIBC 2.43-2ubuntu2) stable release version 2.43.
```

출력에서 볼 것 — 두 툴체인은 "armv7 hard-float"라는 겉모습은 같지만 속은 꽤 다르다.

| 항목 | Bootlin armv7-eabihf musl | Ubuntu arm-linux-gnueabihf |
|---|---|---|
| 기본 CPU | `-mcpu=cortex-a9` (멀티코어 확장 `+mp`) | `-march=armv7-a+fp` (CPU 지정 없음) |
| FPU | `vfpv3-d16` (D0~D15만 사용) | `auto` (`+fp` = VFPv3-D16 상당) |
| 기본 명령어 모드 | ARM (`--with-mode=arm`) | **Thumb-2** (`--with-mode=thumb`) |
| libc | musl 1.2.6 | glibc 2.43 |
| 커널 헤더 | 5.10 | 7.0 |
| sysroot | 툴체인 안 (relocatable) | `/` + multiarch 경로 `/usr/arm-linux-gnueabihf` |

그리고 두 가지 함정이 숨어 있다.

- **`--with-sysroot`가 빌드 서버 경로**(`/builds/buildroot.org/...`)다. 이 경로는 우리 머신에 없다. 그런데도 동작하는 것은 §2.4의 래퍼가 `--sysroot`를 덮어 주기 때문이다. 래퍼 없이 `gcc.br_real`을 직접 부르면 헤더를 못 찾는다.
- **커널 헤더가 5.10.255로 나오지만 실제는 5.10.269다**(JSON 요약 참고). `LINUX_VERSION_CODE`는 `(major << 16) + (minor << 8) + sublevel`인데 sublevel이 8비트라서, 4.4.256 / 4.9.256 stable 릴리스에서 오버플로 문제가 생긴 뒤로 커널이 sublevel을 **255로 고정(clamp)**해서 넣는다. `LINUX_VERSION_CODE`로는 255 이상의 stable 버전을 구분할 수 없다는 것을 알아 두자.

> 툴체인 하나로 armv7 CPU 전부를 지원하는 것은 아니다. Bootlin의 기본 `-mcpu=cortex-a9`는 Cortex-A7/A8/A9/A15 계열에서 대부분 돌지만, **NEON이 없는 Cortex-A9 변형**(예: 일부 저가 SoC)이나 **VFPv3-D16조차 없는 코어**에서는 `-mfpu`를 다시 지정해야 한다. 기본값을 확인하는 이유가 이것이다.

### 3.3 실제로 빌드하고 실행하기 — sysroot와 동적 로더

무엇을 확인하나: 툴체인이 정한 값(컴파일 타임 매크로)과 타깃이 정한 값(런타임 `uname`)을 한 프로그램에서 같이 찍는다.

```c
/* hello_target: 컴파일 타임 매크로(툴체인이 정한 것)와 런타임 정보(타깃이 정한 것)를 함께 찍는다 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/utsname.h>
#include <linux/version.h>   /* 툴체인 sysroot에 들어 있는 커널 헤더 버전 */

int main(void)
{
    struct utsname u;
    if (uname(&u) < 0) { perror("uname"); exit(1); }

    printf("compiler      : gcc %s\n", __VERSION__);
#if defined(__ARM_ARCH)
    printf("__ARM_ARCH    : %d\n", __ARM_ARCH);
#endif
#if defined(__ARM_PCS_VFP)
    printf("float ABI     : hard (__ARM_PCS_VFP)\n");
#elif defined(__arm__)
    printf("float ABI     : soft/softfp\n");
#endif
#if defined(__GLIBC__)
    printf("libc          : glibc %d.%d (headers)\n", __GLIBC__, __GLIBC_MINOR__);
#else
    printf("libc          : not glibc (musl defines no version macro)\n");
#endif
    printf("kernel headers: %d.%d.%d\n", LINUX_VERSION_CODE >> 16,
           (LINUX_VERSION_CODE >> 8) & 0xff, LINUX_VERSION_CODE & 0xff);
    printf("sizeof(long)  : %zu, sizeof(void *) : %zu\n", sizeof(long), sizeof(void *));
    printf("runtime uname : %s %s %s\n", u.sysname, u.release, u.machine);
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'export QEMU_LD_PREFIX=/tmp/B5/x86root; T=/tmp/B5/armv7-eabihf--musl--stable-2026.08-1; CC=$T/bin/arm-buildroot-linux-musleabihf-gcc; cd /tmp/B5 && time $CC -Wall -Wextra -O2 /Users/donh/.elinux_scratch/B5/hello_target.c -o hello_bootlin && file hello_bootlin && readelf -l hello_bootlin | grep interpreter; echo "--- run without sysroot"; qemu-arm ./hello_bootlin; echo "rc=$?"; echo "--- run with -L sysroot"; SYSROOT=$($CC -print-sysroot); echo SYSROOT=$SYSROOT; qemu-arm -L $SYSROOT ./hello_bootlin'
```

```text
real	0m0.732s
user	0m0.701s
sys	0m0.032s
hello_bootlin: ELF 32-bit LSB pie executable, ARM, EABI5 version 1 (SYSV), dynamically linked, interpreter /lib/ld-musl-armhf.so.1, not stripped
      [Requesting program interpreter: /lib/ld-musl-armhf.so.1]
--- run without sysroot
qemu-arm: Could not open '/lib/ld-musl-armhf.so.1': No such file or directory
rc=255
--- run with -L sysroot
SYSROOT=/tmp/B5/armv7-eabihf--musl--stable-2026.08-1/arm-buildroot-linux-musleabihf/sysroot
compiler      : gcc 15.3.0
__ARM_ARCH    : 7
float ABI     : hard (__ARM_PCS_VFP)
libc          : not glibc (musl defines no version macro)
kernel headers: 5.10.255
sizeof(long)  : 4, sizeof(void *) : 4
runtime uname : Linux 7.0.0-34-generic armv7l
```

출력에서 볼 것:

- 바이너리는 **"내 로더는 `/lib/ld-musl-armhf.so.1`이다"**라고 ELF의 `PT_INTERP`에 적어 둔다(B3). 그 파일이 없으면 커널이(여기서는 qemu가) 실행 자체를 못 한다. 보드에서는 이것이 그 유명한 **`-sh: ./app: not found`**(파일이 있는데 not found) 증상이다.
- `qemu-arm -L <sysroot>`는 "이 디렉터리를 타깃의 `/`로 생각하라"는 뜻이다. **sysroot = 타깃 rootfs의 축소판**이라는 말이 문자 그대로 성립한다.
- 런타임 `uname`은 `7.0.0-34-generic armv7l`이다. qemu-user는 syscall을 호스트 커널로 넘기고 machine 이름만 armv7l로 바꿔 보여 준다. 즉 "빌드는 5.10 헤더로, 실행은 7.0 커널에서" — 헤더 ≤ 커널이므로 안전한 조합이다.

---

## 4. 툴체인 ↔ 타깃 rootfs 호환 규칙 — 일부러 깨뜨려 보기

### 4.1 직관 — "빌드 때 본 것"은 "실행 때 있는 것"보다 새로우면 안 된다

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="127" y="22" text-anchor="middle" font-size="13" font-weight="bold">빌드 때 (툴체인·sysroot)</text>
  <text x="340" y="22" text-anchor="middle" font-size="13" font-weight="bold">규칙</text>
  <text x="552" y="22" text-anchor="middle" font-size="13" font-weight="bold">실행 때 (rootfs·커널·CPU)</text>
  <rect x="15" y="36" width="225" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="127" y="63" text-anchor="middle" font-size="12">sysroot glibc 2.43</text>
  <rect x="250" y="36" width="180" height="44" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="63" text-anchor="middle" font-size="12">빌드 glibc ≤ 타깃 glibc</text>
  <rect x="440" y="36" width="225" height="44" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="552" y="54" text-anchor="middle" font-size="12">rootfs glibc 2.31</text>
  <text x="552" y="71" text-anchor="middle" font-size="12">→ GLIBC_2.34 not found</text>
  <rect x="15" y="96" width="225" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="127" y="123" text-anchor="middle" font-size="12">커널 헤더 5.10 (UAPI)</text>
  <rect x="250" y="96" width="180" height="44" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="123" text-anchor="middle" font-size="12">헤더 ≤ 실행 커널</text>
  <rect x="440" y="96" width="225" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="552" y="114" text-anchor="middle" font-size="12">커널 6.1 / 7.0</text>
  <text x="552" y="131" text-anchor="middle" font-size="12">→ OK (반대면 ENOSYS)</text>
  <rect x="15" y="156" width="225" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="127" y="174" text-anchor="middle" font-size="12">float ABI hard</text>
  <text x="127" y="191" text-anchor="middle" font-size="12">로더 ld-linux-armhf.so.3</text>
  <rect x="250" y="156" width="180" height="44" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="183" text-anchor="middle" font-size="12">ABI·로더 경로 일치</text>
  <rect x="440" y="156" width="225" height="44" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="552" y="174" text-anchor="middle" font-size="12">armel(soft) rootfs</text>
  <text x="552" y="191" text-anchor="middle" font-size="12">→ ./app: not found</text>
  <rect x="15" y="216" width="225" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="127" y="243" text-anchor="middle" font-size="12">기본 -mcpu=cortex-a9</text>
  <rect x="250" y="216" width="180" height="44" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="243" text-anchor="middle" font-size="12">명령어 집합 ≤ 타깃 CPU</text>
  <rect x="440" y="216" width="225" height="44" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="552" y="234" text-anchor="middle" font-size="12">NEON 없는 코어에 NEON 코드</text>
  <text x="552" y="251" text-anchor="middle" font-size="12">→ SIGILL</text>
  <rect x="15" y="276" width="225" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="127" y="303" text-anchor="middle" font-size="12">libc 종류 musl</text>
  <rect x="250" y="276" width="180" height="44" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="303" text-anchor="middle" font-size="12">libc 종류 일치</text>
  <rect x="440" y="276" width="225" height="44" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="552" y="294" text-anchor="middle" font-size="12">glibc rootfs</text>
  <text x="552" y="311" text-anchor="middle" font-size="12">→ 로더 없음 (static은 OK)</text>
  <rect x="15" y="336" width="225" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="127" y="363" text-anchor="middle" font-size="12">libstdc++ (GLIBCXX_3.4.x)</text>
  <rect x="250" y="336" width="180" height="44" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="363" text-anchor="middle" font-size="12">빌드 ≤ 타깃</text>
  <rect x="440" y="336" width="225" height="44" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="552" y="354" text-anchor="middle" font-size="12">오래된 libstdc++.so.6</text>
  <text x="552" y="371" text-anchor="middle" font-size="12">→ GLIBCXX_... not found</text>
  <line x1="240" y1="58" x2="248" y2="58" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="430" y1="58" x2="438" y2="58" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="240" y1="118" x2="248" y2="118" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="430" y1="118" x2="438" y2="118" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="240" y1="178" x2="248" y2="178" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="430" y1="178" x2="438" y2="178" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="240" y1="238" x2="248" y2="238" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="430" y1="238" x2="438" y2="238" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="240" y1="298" x2="248" y2="298" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="430" y1="298" x2="438" y2="298" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="240" y1="358" x2="248" y2="358" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="430" y1="358" x2="438" y2="358" stroke="currentColor" marker-end="url(#ah3)"/>
</svg>
```
그림 3 — 툴체인과 타깃 사이의 여섯 가지 계약. 왼쪽이 빌드 때 툴체인이 정한 것, 오른쪽이 실행 때 타깃이 가진 것이다. 빨강은 이 노트에서 실제로 재현했거나(1, 3행 — §4.2, §5) 실무에서 자주 보는 실패 증상이다.

규칙을 한 줄로 줄이면 **"빌드 때 본 것 ≤ 실행 때 있는 것, 종류(ABI·libc)는 같게"**다. bare-metal에서 "새 HAL 헤더로 빌드했는데 ROM에 구 버전 함수가 박혀 있어 hard fault" 같은 상황과 같은 구조다. 다만 Linux에서는 그 "ROM"이 rootfs의 `.so` 파일들과 커널이라서, **보드마다·이미지 버전마다 다르다**는 것이 차이다.

### 4.2 glibc가 오래된 rootfs — `GLIBC_2.34 not found` 재현

glibc는 **심볼 버전**(symbol versioning)을 쓴다. 함수마다 "이 동작은 glibc 2.34부터"라는 태그(`GLIBC_2.34`)가 붙고, 링크할 때 바이너리에 "나는 `__libc_start_main@GLIBC_2.34`가 필요하다"가 기록된다. 실행 때 타깃 glibc가 그 태그를 모르면 로더가 거부한다. 그래서 **새 glibc로 빌드한 바이너리는 옛 glibc에서 돌지 않지만, 그 반대는 된다**(backward compatible).

무엇을 확인하나: Ubuntu 26.04 크로스 gcc(glibc 2.43)로 만든 바이너리가 요구하는 glibc 버전, 그리고 glibc 2.31 rootfs(Ubuntu 20.04의 armhf `libc6`를 `/tmp/B5/oldroot`에 풀어서 흉내)에서의 실행 결과.

```sh
limactl shell elinux -- bash -lc 'cd /tmp/B5 && arm-linux-gnueabihf-gcc -Wall -Wextra -O2 /Users/donh/.elinux_scratch/B5/hello_target.c -o hello_distro && qemu-arm -L /usr/arm-linux-gnueabihf ./hello_distro && echo "--- GLIBC symbol versions required:" && arm-linux-gnueabihf-objdump -T hello_distro | grep -o "GLIBC_[0-9.]*" | sort -uV && arm-linux-gnueabihf-readelf -d hello_distro | grep NEEDED && readelf -l hello_distro | grep interpreter'
```

```text
compiler      : gcc 15.2.0
__ARM_ARCH    : 7
float ABI     : hard (__ARM_PCS_VFP)
libc          : glibc 2.43 (headers)
kernel headers: 7.0.0
sizeof(long)  : 4, sizeof(void *) : 4
runtime uname : Linux 7.0.0-34-generic armv7l
--- GLIBC symbol versions required:
GLIBC_2.4
GLIBC_2.34
 0x00000001 (NEEDED)                     Shared library: [libc.so.6]
 0x00000001 (NEEDED)                     Shared library: [ld-linux-armhf.so.3]
      [Requesting program interpreter: /lib/ld-linux-armhf.so.3]
```

```sh
limactl shell elinux -- bash -lc 'mkdir -p /tmp/B5/oldroot && cd /tmp/B5 && curl -sfLO http://ports.ubuntu.com/pool/main/g/glibc/libc6_2.31-0ubuntu9_armhf.deb && sha256sum libc6_2.31-0ubuntu9_armhf.deb && dpkg-deb -x libc6_2.31-0ubuntu9_armhf.deb oldroot && rm libc6_2.31*.deb && ls -l oldroot/lib/ld-linux-armhf.so.3'
limactl shell elinux -- bash -lc 'cd /tmp/B5 && echo "[new binary on glibc 2.31 rootfs]"; qemu-arm -L /tmp/B5/oldroot ./hello_distro; echo "rc=$?"; echo "[same source, -static]"; arm-linux-gnueabihf-gcc -Wall -Wextra -O2 -static /Users/donh/.elinux_scratch/B5/hello_target.c -o hello_distro_static; qemu-arm -L /tmp/B5/oldroot ./hello_distro_static | head -3'
```

```text
a1e1950eb3dd55403b2da91a7dc428dff0d3dc1b3ad87be9dbc1dde97711c21f  libc6_2.31-0ubuntu9_armhf.deb
lrwxrwxrwx 1 donh donh 30 Apr 14  2020 oldroot/lib/ld-linux-armhf.so.3 -> arm-linux-gnueabihf/ld-2.31.so
[new binary on glibc 2.31 rootfs]
./hello_distro: /lib/arm-linux-gnueabihf/libc.so.6: version `GLIBC_2.34' not found (required by ./hello_distro)
rc=1
[same source, -static]
compiler      : gcc 15.2.0
__ARM_ARCH    : 7
float ABI     : hard (__ARM_PCS_VFP)
```

출력에서 볼 것:

- 가장 단순한 hello 프로그램도 `GLIBC_2.34`를 요구한다. glibc 2.34에서 프로그램 시작 코드(`crt1.o`가 부르는 방식)가 바뀌며 `__libc_start_main`에 새 심볼 버전이 생겼고, 새 glibc로 링크하면 그 버전에 묶이기 때문이다. 즉 **"Ubuntu 22.04(glibc 2.35) 이상에서 빌드한 바이너리는 Ubuntu 20.04(glibc 2.31) 기반 rootfs에서 거의 다 안 돈다".** 예를 들어 Ubuntu 20.04 기반 Jetson 이미지에 새 PC에서 크로스 빌드한 앱을 올리면 바로 이 메시지를 본다.
- `-static`으로 링크하면 libc가 바이너리 안에 들어가므로 rootfs의 glibc와 무관하게 돈다. 하지만 glibc static은 `getaddrinfo`·NSS·`dlopen`에서 경고나 제약이 있고 크기가 크다(아래). 근본 해결은 **타깃과 같은(또는 더 오래된) glibc를 가진 sysroot로 빌드**하는 것이다 — Yocto SDK나 벤더 SDK를 쓰는 이유다.

무엇을 확인하나: 같은 소스의 동적/정적 × glibc/musl 크기.

```sh
limactl shell elinux -- bash -lc 'export QEMU_LD_PREFIX=/tmp/B5/x86root; cd /tmp/B5 && /tmp/B5/armv7-eabihf--musl--stable-2026.08-1/bin/arm-linux-gcc -Wall -Wextra -O2 -static /Users/donh/.elinux_scratch/B5/hello_target.c -o hello_bootlin_static && ls -l hello_bootlin hello_bootlin_static hello_distro hello_distro_static | awk "{print \$5, \$9}"; file hello_bootlin_static | cut -c1-120; qemu-arm ./hello_bootlin_static | tail -2; arm-linux-gnueabihf-size hello_distro hello_bootlin'
```

```text
7564 hello_bootlin
40480 hello_bootlin_static
69476 hello_distro
490644 hello_distro_static
hello_bootlin_static: ELF 32-bit LSB executable, ARM, EABI5 version 1 (SYSV), statically linked, with debug_info, not st
sizeof(long)  : 4, sizeof(void *) : 4
runtime uname : Linux 7.0.0-34-generic armv7l
   text	   data	    bss	    dec	    hex	filename
   2048	    348	      4	   2400	    960	hello_distro
   2355	    332	     28	   2715	    a9b	hello_bootlin
```

출력에서 볼 것:

- **musl static은 40 KB, glibc static은 490 KB.** "그냥 static으로 빌드해서 libc 문제를 피하자"는 전략은 musl에서 특히 매력적이다. 컨테이너 없는 작은 장비의 진단 도구를 musl static으로 배포하는 팀이 많은 이유다.
- 동적 바이너리 파일 크기 차이(69 KB vs 7.5 KB)는 코드 차이가 아니다(`text` 2048 vs 2355 바이트). Ubuntu 툴체인은 LOAD 세그먼트를 **64 KB 정렬**(`0x10000`, 64K 페이지 커널 대비)로 링크해서 파일에 패딩이 들어가고, Bootlin은 4 KB 정렬이다. 툴체인마다 링커 기본값도 다르다는 예다.

무엇을 확인하나: 두 동적 바이너리의 LOAD 세그먼트 정렬.

```sh
limactl shell elinux -- bash -lc 'cd /tmp/B5 && readelf -lW hello_distro | grep LOAD; readelf -lW hello_bootlin | grep LOAD'
```

```text
  LOAD           0x000000 0x00000000 0x00000000 0x00978 0x00978 R E 0x10000
  LOAD           0x00feac 0x0001feac 0x0001feac 0x0015c 0x00160 RW  0x10000
  LOAD           0x000000 0x00000000 0x00000000 0x00a48 0x00a48 R E 0x1000
  LOAD           0x000eb8 0x00001eb8 0x00001eb8 0x0014c 0x00168 RW  0x1000
```

### 4.3 커널 헤더 — "헤더 ≤ 커널"과 musl의 함정

커널 헤더는 syscall 번호, ioctl 번호, 구조체 레이아웃을 정한다. 리눅스 커널은 **유저 ABI를 깨지 않는다**(새 syscall·필드는 추가만 한다)는 원칙을 지키므로,

- 헤더가 **커널보다 오래되면**: 새 기능을 못 쓸 뿐 안전하다. → Bootlin stable이 5.10 헤더를 쓰는 이유.
- 헤더가 **커널보다 새로우면**: 새 syscall이 컴파일은 되지만, 실행하면 옛 커널이 그 번호를 모르므로 `-ENOSYS`(Function not implemented)가 돌아온다. 새 ioctl 구조체 필드도 옛 드라이버가 무시하거나 `-EINVAL`/`-ENOTTY`를 준다.

무엇을 확인하나: syscall을 번호로 직접 불러서 "커널이 아는 번호"와 "모르는 번호"의 반응 차이를 본다. 그리고 툴체인마다 같은 번호가 헤더에 있는지 본다.

```c
/* newsys: 툴체인 커널 헤더가 아는 syscall 번호 vs 실제 커널이 지원하는 syscall */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <linux/version.h>

static void try(const char *name, long nr)
{
    /* 인자는 일부러 잘못 준다: 커널이 syscall을 알면 EINVAL/EFAULT/EBADF, 모르면 ENOSYS */
    long r = syscall(nr, -1, 0, 0, 0, 0);
    printf("  %-10s nr=%-4ld -> r=%ld errno=%d (%s)\n", name, nr, r, errno, strerror(errno));
}

int main(void)
{
    printf("built against kernel headers 0x%06x\n", LINUX_VERSION_CODE);
    try("getpid", SYS_getpid);
#ifdef __NR_statx            /* Linux 4.11 */
    try("statx", __NR_statx);
#else
    printf("  statx      : not in these headers\n");
#endif
#ifdef __NR_cachestat        /* Linux 6.5 */
    try("cachestat", __NR_cachestat);
#else
    printf("  cachestat  : not in these headers (compile-time)\n");
#endif
    try("nr 999", 999);      /* 존재하지 않는 번호 = 아주 오래된 커널에서 새 syscall을 부른 상황 */
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'export QEMU_LD_PREFIX=/tmp/B5/x86root; T=/tmp/B5/armv7-eabihf--musl--stable-2026.08-1; cd /tmp/B5 && $T/bin/arm-linux-gcc -Wall -Wextra -O2 /Users/donh/.elinux_scratch/B5/newsys.c -o newsys_bootlin && arm-linux-gnueabihf-gcc -Wall -Wextra -O2 /Users/donh/.elinux_scratch/B5/newsys.c -o newsys_distro && echo "[Bootlin, headers 5.10]" && qemu-arm -L $T/arm-buildroot-linux-musleabihf/sysroot ./newsys_bootlin && echo "[Ubuntu cross, headers 7.0]" && qemu-arm -L /usr/arm-linux-gnueabihf ./newsys_distro; qemu-arm --version | head -1'
```

```text
[Bootlin, headers 5.10]
built against kernel headers 0x050aff
  getpid     nr=20   -> r=427717 errno=0 (No error information)
  statx      nr=397  -> r=-1 errno=14 (Bad address)
  cachestat  nr=451  -> r=-1 errno=38 (Function not implemented)
  nr 999     nr=999  -> r=-1 errno=38 (Function not implemented)
[Ubuntu cross, headers 7.0]
built against kernel headers 0x070000
  getpid     nr=20   -> r=427719 errno=0 (Success)
  statx      nr=397  -> r=-1 errno=14 (Bad address)
  cachestat  nr=451  -> r=-1 errno=38 (Function not implemented)
  nr 999     nr=999  -> r=-1 errno=38 (Function not implemented)
```

```text
qemu-arm version 10.2.1 (Debian 1:10.2.1+ds-1ubuntu3.2)
```

출력에서 볼 것 — 예상과 다른 점이 두 가지 있어서 오히려 좋은 공부거리다.

- **5.10 헤더인 Bootlin 툴체인에서도 `__NR_cachestat`(6.5에서 추가)이 정의되어 있다.** 이유: musl은 syscall 번호를 커널 헤더(`asm/unistd.h`)에서 가져오지 않고 **자기 소스(`arch/arm/bits/syscall.h.in`)에 직접 들고 있다.** 그래서 musl 1.2.6이 아는 syscall이면 커널 헤더 버전과 무관하게 `<sys/syscall.h>`에 나온다. glibc는 반대로 커널 헤더의 번호를 쓴다. "커널 헤더 버전 = 쓸 수 있는 syscall 목록"이라는 등식은 **glibc에서만 대략 맞다.**
- `cachestat`은 두 바이너리 모두 `ENOSYS`가 나왔다. 호스트 커널은 7.0이라 cachestat을 지원하지만, 여기서는 **qemu-user(10.2.1)가 커널 역할을 하면서 이 syscall을 번역하지 못해** `ENOSYS`를 돌려준 것이다. 결과적으로 "헤더(또는 libc)는 아는데 실행 환경이 모르는 syscall → `ENOSYS`"라는 상황을 정확히 재현했다. 실제 보드에서는 옛 커널이 같은 역할을 한다.
- 존재하지 않는 번호(999)도 같은 `ENOSYS`다. 커널 입장에서 "모르는 syscall"은 전부 `sys_ni_syscall`로 간다(§8).
- `statx`는 커널이 아는 syscall이므로 잘못된 인자에 `EFAULT`(Bad address)로 반응했다. **`ENOSYS`냐 다른 errno냐**가 "커널이 이 기능을 아는가"를 판별하는 방법이다. 새 syscall을 쓰는 코드는 `ENOSYS`일 때 옛 방식으로 돌아가는 fallback을 둬야 한다(glibc의 `statx` → `fstatat` fallback이 그 예).
- 첫 줄의 `errno=0 (No error information)` vs `(Success)`는 musl과 glibc의 `strerror(0)` 문자열 차이다. 이런 작은 차이도 libc가 바뀌면 로그 파서를 깨뜨릴 수 있다.

### 4.4 ABI와 CPU — float ABI, `-march`, 로더 이름

32-bit ARM에는 float ABI가 세 가지 있다.

| `-mfloat-abi` | FPU 명령 사용 | 함수 인자로 float 전달 | 로더 (glibc) | 로더 (musl) |
|---|---|---|---|---|
| `soft` | 안 씀 (라이브러리 호출) | 정수 레지스터 | `/lib/ld-linux.so.3` | `/lib/ld-musl-arm.so.1` |
| `softfp` | 씀 | 정수 레지스터 (soft와 호환) | `/lib/ld-linux.so.3` | `/lib/ld-musl-arm.so.1` |
| `hard` | 씀 | VFP 레지스터 (`s0`, `d0`) | `/lib/ld-linux-armhf.so.3` | `/lib/ld-musl-armhf.so.1` |

hard와 soft는 **함수 호출 규약이 달라서 섞어 링크할 수 없고**, 그래서 로더 이름까지 다르다. armhf rootfs에 soft-float 바이너리를 올리면 로더가 없어서 `not found`가 나온다. §5에서 crosstool-NG 샘플(`arm-unknown-linux-musleabi`, soft-float)로 만든 바이너리로 이것을 직접 확인한다.

aarch64는 float ABI가 하나뿐이라 이 문제는 없다. 대신 **`-march`/`-mcpu`가 타깃보다 높으면** 문제가 된다. 예를 들어 Cortex-A76용(`-mcpu=cortex-a76`, ARMv8.2)으로 빌드한 코드가 LSE atomics나 dot-product 명령을 쓰면, Cortex-A53(ARMv8.0) 보드에서는 그 명령을 만나는 순간 `SIGILL`로 죽는다. 툴체인의 **기본값**이 무엇인지(§3.2의 `-Q --help=target`) 확인하는 이유다.

## 5. crosstool-NG — 툴체인을 직접 빌드해 보기

### 5.1 직관 — "툴체인 = 닭과 달걀 문제를 단계로 푸는 빌드"

gcc를 타깃용으로 빌드하려면 타깃 libc가 필요하고(`crt1.o`, `libc.so`에 링크하는 `libgcc_s`), libc를 빌드하려면 타깃용 gcc가 필요하다. 닭과 달걀이다. 툴체인 빌더는 이것을 단계로 푼다.

1. **호스트 도우미 라이브러리**(gmp, mpfr, isl, mpc, zlib, zstd) — gcc가 호스트에서 쓰는 수학·압축 라이브러리
2. **binutils** — 타깃용 `as`, `ld`
3. **커널 헤더** — `make headers_install` (§8.1)
4. **core gcc (pass 1)** — libc 없이 C만 컴파일할 수 있는 최소 gcc (`--without-headers`, `libgcc.a`만)
5. **C 라이브러리** — 4번 gcc로 musl/glibc를 빌드해 sysroot에 설치
6. **final gcc** — 이제 libc가 있으니 완전한 gcc(+ `libgcc_s`, 선택한 언어 런타임)
7. **마무리** — strip, 심볼릭 링크, 설정 파일 저장

bare-metal 비유: 부트로더를 만들려면 부트로더가 필요한 상황에서, ROM 코드 → 1차 로더 → 2차 로더로 단계를 쌓는 것과 같은 구조다.

### 5.2 crosstool-NG 설치와 샘플 고르기

crosstool-NG는 그 자체도 소스에서 설치한다(대부분 셸 스크립트 + Kconfig라 빠르다). 릴리스 tarball과 sha512를 받아 검증한다.

무엇을 확인하나: 최신 릴리스 확인 → 다운로드 → 체크섬 → `configure && make && make install`.

```sh
limactl shell elinux -- bash -lc 'curl -s https://api.github.com/repos/crosstool-ng/crosstool-ng/releases/latest | grep -E "tag_name" | head -1'
limactl shell elinux -- bash -lc 'cd /tmp/B5 && V=1.29.0 && curl -sfLO https://github.com/crosstool-ng/crosstool-ng/releases/download/crosstool-ng-$V/crosstool-ng-$V.tar.xz && curl -sfLO https://github.com/crosstool-ng/crosstool-ng/releases/download/crosstool-ng-$V/crosstool-ng-$V.tar.xz.sha512 && cat crosstool-ng-$V.tar.xz.sha512 && sha512sum -c crosstool-ng-$V.tar.xz.sha512 && tar xf crosstool-ng-$V.tar.xz && cd crosstool-ng-$V && (time (./configure --prefix=/tmp/B5/ctng >cfg.log 2>&1 && make -j2 >mk.log 2>&1 && make install >inst.log 2>&1)) 2>&1; echo rc=$?; /tmp/B5/ctng/bin/ct-ng version 2>&1 | head -1'
```

```text
  "tag_name": "crosstool-ng-1.29.0",
04cd24c8ec60d250f4d73bf41e04131a142169cd89a26d49c9725419e248834d455fb9fb5c38c132f314dd5e34687eefb63f7a104f22764bd44ec2b06982acee  crosstool-ng-1.29.0.tar.xz
crosstool-ng-1.29.0.tar.xz: OK

real	0m4.888s
user	0m5.311s
sys	0m1.489s
rc=0
This is crosstool-NG version 1.29.0
```

(GitHub API 응답에서는 `tag_name`과 다운로드 URL 줄만 남겼다.) 빌드 의존성(bison, flex, ncurses, texinfo, help2man, gawk, libtool, autoconf, automake)은 VM에 이미 깔려 있었다.

무엇을 확인하나: 내장 샘플 목록과, 샘플 하나가 어떤 부품 버전을 쓰는지.

```sh
limactl shell elinux -- bash -lc 'export PATH=/tmp/B5/ctng/bin:$PATH; mkdir -p /tmp/B5/ctwork && cd /tmp/B5/ctwork && ct-ng list-samples 2>&1 | wc -l; ct-ng list-samples 2>&1 | head -5; ct-ng list-samples 2>&1 | grep -E "musl|arm-unknown-linux|aarch64-unknown-linux|arm-unknown-eabi|arm-none"; ct-ng list-samples 2>&1 | tail -6'
```

```text
170
Status  Sample name
[G...]   aarch64-ol10u0-linux-gnu
[G...]   aarch64-ol7u9-linux-gnu
[G...]   aarch64-ol8u10-linux-gnu
[G...]   aarch64-ol8u6-linux-gnu
[G...]   aarch64-unknown-linux-gnu
[G...]   aarch64-unknown-linux-musl
[G...]   aarch64-unknown-linux-uclibc
[G...]   aarch64_be-unknown-linux-musl
[G...]   arm-none-eabi
[G...]   arm-unknown-eabi
[G...]   arm-unknown-linux-gnueabi
[G..X]   arm-unknown-linux-musleabi
[G...]   arm-unknown-linux-uclibcgnueabi
[G..X]   arm-unknown-linux-uclibcgnueabihf
[G..X]   loongarch64-unknown-linux-musl
[G..X]   or1k-unknown-linux-musl
[G...]   powerpc64-unknown-linux-musl
[G...]   x86_64-multilib-linux-musl
[G...]   xtensa-fsf-linux-uclibc
 L (Local)       : sample was found in current directory
 G (Global)      : sample was installed with crosstool-NG
 X (EXPERIMENTAL): sample may use EXPERIMENTAL features
 B (BROKEN)      : sample is currently broken
 O (OBSOLETE)    : sample needs to be upgraded
```

```sh
limactl shell elinux -- bash -lc 'export PATH=/tmp/B5/ctng/bin:$PATH; cd /tmp/B5/ctwork && ct-ng show-arm-unknown-linux-musleabi; ct-ng show-aarch64-unknown-linux-musl'
```

```text
[G..X]   arm-unknown-linux-musleabi
    Languages       : C,C++
    OS              : linux-7.1
    Binutils        : binutils-2.47
    Compiler        : gcc-16.2.0
    Linkers         :
    C library       : musl-1.2.6
    Debug tools     : duma-2_5_21 gdb-17.2 strace-7.1
    Companion libs  : expat-2.7.1 gmp-6.3.0 isl-0.27 mpc-1.3.1 mpfr-4.2.2 ncurses-6.5 zlib-1.3.1 zstd-1.5.7
    Companion tools : automake-1.17
[G...]   aarch64-unknown-linux-musl
    Languages       : C,C++
    OS              : linux-7.1
    Binutils        : binutils-2.47
    Compiler        : gcc-16.2.0
    Linkers         :
    C library       : musl-1.2.6
    Debug tools     : gdb-17.2
    Companion libs  : expat-2.7.1 gettext-0.26 gmp-6.3.0 isl-0.27 libiconv-1.18 mpc-1.3.1 mpfr-4.2.2 ncurses-6.5 zlib-1.3.1 zstd-1.5.7
    Companion tools : automake-1.17
```

출력에서 볼 것: 샘플 이름이 곧 triplet이다. `X`는 EXPERIMENTAL 기능을 쓰는 샘플이다. `show-<sample>`은 부품 버전 요약이고, 실제 설정은 `ct-ng <sample>`로 `.config`에 불러온 뒤 `ct-ng menuconfig`로 고친다. 여기서는 32-bit ARM + musl 샘플 `arm-unknown-linux-musleabi`를 고른다(§2.3 Bootlin 툴체인과 비교하기 좋다).

### 5.3 설정 고치기 — `.config`는 커널과 같은 Kconfig

ct-ng의 `.config`는 커널 `.config`와 같은 Kconfig 형식이다(E 모듈에서 다시 본다). 대화형으로는 `ct-ng menuconfig`, 스크립트로는 `sed` + `ct-ng olddefconfig`로 고친다. 이 VM은 `/tmp`가 **2 GB tmpfs(RAM 디스크)를 다른 작업과 공유**하는 환경이라, 샘플 그대로(커널 7.1 전체 소스, C++, gdb, strace)는 들어가지 않는다. 그래서 다음처럼 줄였다.

```sh
#!/bin/bash
# crosstool-NG sample 설정을 /tmp/B5 용으로 줄인다 (C only, 디버그 도구 제외, 손질한 소스 사용)
set -e
cd /tmp/B5/ctwork
sed -i \
 -e 's|^CT_LOCAL_TARBALLS_DIR=.*|CT_LOCAL_TARBALLS_DIR="/tmp/B5/tarballs"|' \
 -e 's|^CT_PREFIX_DIR=.*|CT_PREFIX_DIR="/tmp/B5/x-tools/${CT_TARGET}"|' \
 -e 's|^CT_PARALLEL_JOBS=.*|CT_PARALLEL_JOBS=2|' \
 -e 's|^CT_EXTRA_CFLAGS_FOR_HOST=.*|CT_EXTRA_CFLAGS_FOR_HOST="-g0"|' \
 -e 's|^CT_DEBUG_DUMA=y|# CT_DEBUG_DUMA is not set|' \
 -e 's|^CT_DEBUG_GDB=y|# CT_DEBUG_GDB is not set|' \
 -e 's|^CT_DEBUG_STRACE=y|# CT_DEBUG_STRACE is not set|' \
 -e 's|^CT_CC_LANG_CXX=y|# CT_CC_LANG_CXX is not set|' \
 -e 's|^CT_COMP_TOOLS_AUTOMAKE=y|# CT_COMP_TOOLS_AUTOMAKE is not set|' \
 -e 's|^CT_LINUX_SRC_RELEASE=y|# CT_LINUX_SRC_RELEASE is not set\nCT_LINUX_SRC_CUSTOM=y\nCT_LINUX_CUSTOM_LOCATION="/tmp/B5/trim/linux-6.1.175.tar.gz"|' \
 -e 's|^CT_LINUX_V_7_1=y|CT_LINUX_V_6_1=y|' \
 -e 's|^CT_GCC_SRC_RELEASE=y|# CT_GCC_SRC_RELEASE is not set\nCT_GCC_SRC_CUSTOM=y\nCT_GCC_CUSTOM_LOCATION="/tmp/B5/trim/gcc-16.2.0.tar.gz"|' \
 -e 's|^CT_BINUTILS_SRC_RELEASE=y|# CT_BINUTILS_SRC_RELEASE is not set\nCT_BINUTILS_SRC_CUSTOM=y\nCT_BINUTILS_CUSTOM_LOCATION="/tmp/B5/trim/binutils-2.47.tar.gz"|' \
 .config
ct-ng olddefconfig >/dev/null
grep -E '^CT_(LINUX_SRC_CUSTOM|LINUX_CUSTOM_LOCATION|LINUX_VERSION|GCC_SRC_CUSTOM|BINUTILS_SRC_CUSTOM|BINUTILS_VERSION|GCC_CUSTOM_LOCATION|GCC_VERSION|PARALLEL_JOBS|EXTRA_CFLAGS_FOR_HOST|CC_LANG_CXX|DEBUG_GDB|PREFIX_DIR|LOCAL_TARBALLS_DIR|TARGET_VENDOR|LIBC|ARCH_FLOAT)=' .config
```

```sh
limactl shell elinux -- bash -lc 'export PATH=/tmp/B5/ctng/bin:$PATH; cd /tmp/B5/ctwork && rm -f .config && ct-ng arm-unknown-linux-musleabi >/dev/null && /Users/donh/.elinux_scratch/B5/ctng_tweak.sh 2>&1 | grep -v "^.config:"'
```

```text
CT_LOCAL_TARBALLS_DIR="/tmp/B5/tarballs"
CT_PREFIX_DIR="/tmp/B5/x-tools/${CT_TARGET}"
CT_PARALLEL_JOBS=2
CT_EXTRA_CFLAGS_FOR_HOST="-g0"
CT_ARCH_FLOAT="auto"
CT_TARGET_VENDOR="unknown"
CT_LINUX_SRC_CUSTOM=y
CT_LINUX_CUSTOM_LOCATION="/tmp/B5/trim/linux-6.1.175.tar.gz"
CT_LINUX_VERSION="6.1.175"
CT_BINUTILS_SRC_CUSTOM=y
CT_BINUTILS_VERSION="2.47"
CT_LIBC="musl"
CT_GCC_SRC_CUSTOM=y
CT_GCC_CUSTOM_LOCATION="/tmp/B5/trim/gcc-16.2.0.tar.gz"
CT_GCC_VERSION="16.2.0"
```

바꾼 것의 의미:

| 설정 | 값 | 이유 |
|---|---|---|
| `CT_PARALLEL_JOBS` | 2 | VM의 4 vCPU를 다른 작업과 나눠 씀 |
| `CT_EXTRA_CFLAGS_FOR_HOST` | `-g0` | gcc 자체를 디버그 정보 없이 빌드. core gcc 빌드 디렉터리가 `-g`일 때 737 MB(미완료 시점)에서 `-g0`일 때 432 MB(완료)로 줄었다 |
| `CT_CC_LANG_CXX`, gdb, strace, duma | 끔 | C 툴체인만 필요 |
| `CT_LINUX_V_6_1` + custom 소스 | 6.1.175 | 헤더를 LTS 6.1로 낮춤(§4.3의 "헤더 ≤ 커널"), 헤더 설치에 필요 없는 디렉터리를 뺀 tarball 사용 |
| `CT_GCC_SRC_CUSTOM`, `CT_BINUTILS_SRC_CUSTOM` | 손질한 tarball | 다른 언어 런타임·테스트·번역 파일을 빼서 tmpfs에 맞춤. custom 소스에는 ct-ng가 패치를 적용하지 않으므로 ct-ng 번들 패치를 직접 먼저 적용했다 |
| `CT_PREFIX_DIR` | `/tmp/B5/x-tools/...` | 기본값은 `~/x-tools/<triplet>` |

손질 스크립트 중 gcc 부분은 다음과 같다(sha256은 ct-ng 패키지의 `chksum` 파일 값과 대조했다).

```sh
#!/bin/bash
# trim_gcc.sh : gcc 소스에서 C 전용 크로스 빌드에 필요 없는 런타임 라이브러리·테스트·번역만 뺀다
# (gcc/d, gcc/rust, gcc/jit 의 *-target.def 는 genhooks 가 C 빌드에서도 읽으므로 남긴다)
set -e
cd /tmp/B5/trim
G=gcc-16.2.0
curl -sfLO https://ftp.gnu.org/gnu/gcc/$G/$G.tar.xz
echo "e6738e29597f733270731aa90600f37ffdc045079dfc27ec7e8192cc81085c3e  $G.tar.xz" | sha256sum -c
EX=""
for d in gcc/testsuite libstdc++-v3 libgo libphobos libgfortran libgm2 libgcobol libgrust libga68 gnattools libada \
         libsanitizer libgomp libitm libvtv libquadmath libobjc libffi MD5SUMS; do EX="$EX --exclude=$G/$d"; done
rm -f $G.tar.gz
tar xf $G.tar.xz $EX && tar xf $G.tar.xz $G/gcc/testsuite/selftests && rm $G.tar.xz   # selftests 는 빌드 중 s-selftest-c 가 읽는다
find $G/gcc/po -name "*.po" -delete
(cd $G && for p in /tmp/B5/ctng/share/crosstool-ng/packages/gcc/16.2.0/00[01]*.patch /tmp/B5/ctng/share/crosstool-ng/packages/gcc/16.2.0/002[0-3]*.patch; do patch -p1 -s --forward < $p > /dev/null || echo "FAIL $p"; done)
du -sh $G
tar czf $G.tar.gz $G && rm -rf $G
ls -l
```

이 "소스 다이어트"는 공짜가 아니었다. 실제로 겪은 실패를 그대로 적는다 — ct-ng 로그를 읽는 연습으로 좋다.

| 시도 | 실패 단계 | `build.log`의 핵심 줄 | 원인과 조치 |
|---|---|---|---|
| 1 | GMP 설정 중 (60 s) | (감시 스크립트) `WATCHDOG STOP` | 원본 소스 그대로 풀자 tmpfs 여유가 223 MB로 떨어짐 → 소스 손질 시작 |
| 2 | Installing binutils for host | `cannot find input file: 'testsuite/Makefile.in'` | `*/testsuite`를 다 지웠더니 libiberty의 configure가 요구 → 큰 testsuite만 제외 |
| 3 | Extracting | `Neither file nor directory: .../linux-6.1.175.tar.gz` | 내 정리 스크립트가 **이전 실행의 로그**를 보고 tarball을 지움 → 시작 전에 로그 삭제 |
| 4 | Installing binutils for host | `No rule to make target 'testsuite/gentestdlls.c'` | binutils/testsuite도 빌드에 필요 → gas/testsuite만 제외 |
| 5 | Installing core C gcc compiler | `No rule to make target 'd/d-target.def'` | C만 빌드해도 genhooks가 D·Rust·JIT 언어의 target hook 정의를 읽음 → `gcc/<언어>` 디렉터리 유지 |
| 6 | Installing core C gcc compiler | `No rule to make target '.../gcc/testsuite/selftests'` | gcc가 빌드 중 selftest를 돌림 → selftests만 복원 |
| 7 | Installing core C gcc compiler (381 s) | `WATCHDOG STOP at 381s avail=320MB` | `-O2 -g`로 빌드되는 gcc 오브젝트가 커서 core 빌드 디렉터리만 737 MB → `-g0` |
| 8 | **final gcc까지 완료**, 이후 "Installing binutils for target" | `.../src/binutils/configure: No such file or directory` | 공간 절약용으로 binutils 소스를 host 단계 직후 지웠는데, 마지막에 **타깃에서 돌 binutils**(보드용 `as`/`ld`)를 한 번 더 빌드하는 단계가 있었음 |

교훈: **툴체인 빌드에 필요한 디스크는 "소스 + 가장 큰 gcc 빌드 디렉터리 + 설치본"이고, 기본 설정이면 수 GB다.** 실제 팀에서는 넉넉한 디스크의 빌드 서버에서 돌리고 결과 tarball만 배포한다. 그리고 "테스트 디렉터리는 필요 없겠지"라는 가정은 configure·빌드 규칙에서 자주 깨진다.

### 5.4 빌드 결과 — 단계별 시간과 로그

빌드는 `ctng_run.sh`(백그라운드로 `ct-ng build`를 돌리면서 20초마다 `/tmp` 여유 공간을 기록하고, 끝난 단계의 소스·빌드 디렉터리를 지우고, 여유가 350 MB 아래로 떨어지거나 60분을 넘으면 중단하는 감시 스크립트)로 돌렸다. 아래는 8번째 시도의 단계별 시간이다(`build.log`의 `done in` 줄 그대로).

```sh
limactl shell elinux -- bash -lc 'nohup setsid /Users/donh/.elinux_scratch/B5/ctng_run.sh >/tmp/B5/run.out 2>&1 < /dev/null & disown'
limactl shell elinux -- bash -c 'grep -E "^\[INFO \]  [A-Z].*done in" /tmp/B5/ctng_attempt8_build.log'
```

```text
[INFO ]  Retrieving needed toolchain components' tarballs: done in 0.14s (at 00:01)
[INFO ]  Extracting and patching toolchain components: done in 3.53s (at 00:04)
[INFO ]  Installing zlib for host: done in 0.91s (at 00:05)
[INFO ]  Installing zstd for host: done in 7.56s (at 00:13)
[INFO ]  Installing GMP for host: done in 19.16s (at 00:32)
[INFO ]  Installing MPFR for host: done in 12.75s (at 00:45)
[INFO ]  Installing ISL for host: done in 34.42s (at 01:19)
[INFO ]  Installing MPC for host: done in 3.95s (at 01:23)
[INFO ]  Installing binutils for host: done in 40.80s (at 02:04)
[INFO ]  Installing kernel headers: done in 3.21s (at 02:07)
[INFO ]  Installing core C gcc compiler: done in 405.09s (at 08:52)
[INFO ]  Installing C library: done in 18.95s (at 09:11)
[INFO ]  Installing final gcc compiler: done in 431.56s (at 16:23)
[INFO ]  Checking dynamic linker symlinks: done in 0.05s (at 16:23)
```

그리고 마지막 실패 부분(로그 꼬리, 일부 생략):

```text
[INFO ]  Installing binutils for target
[DEBUG]    Entering '/tmp/B5/ctwork/.build/arm-unknown-linux-musleabi/build/build-binutils-for-target'
[EXTRA]    Configuring binutils for target
[CFG  ]    /usr/bin/bash: /tmp/B5/ctwork/.build/arm-unknown-linux-musleabi/src/binutils/configure: No such file or directory
[ERROR]  >>
[ERROR]  >>  Build failed in step 'Installing binutils for target'
[ERROR]  >>        called in step '(top-level)'
...
[ERROR]  (elapsed: 16:22.71)
```

(`[DEBUG] ==> Executing:` 줄은 길어서 뺐다.)

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
  <text x="20" y="22" font-size="13" font-weight="bold">crosstool-NG 빌드 단계 (arm-unknown-linux-musleabi, 2 jobs) — 실측</text>
  <text x="160" y="54" text-anchor="end" font-size="12">1. 받기·풀기</text>
  <rect x="170.0" y="40" width="2" height="18" fill="#888" fill-opacity="0.5" stroke="#888"/>
  <text x="178.0" y="54" font-size="12">4 s</text>
  <text x="160" y="84" text-anchor="end" font-size="12">2. 호스트 lib</text>
  <rect x="171.9" y="70" width="37.1" height="18" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
  <text x="215.0" y="84" font-size="12">79 s</text>
  <text x="160" y="114" text-anchor="end" font-size="12">3. binutils</text>
  <rect x="209.0" y="100" width="19.3" height="18" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
  <text x="234.3" y="114" font-size="12">41 s</text>
  <text x="160" y="144" text-anchor="end" font-size="12">4. 커널 헤더</text>
  <rect x="228.3" y="130" width="2" height="18" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
  <text x="236.3" y="144" font-size="12">3 s</text>
  <text x="160" y="174" text-anchor="end" font-size="12">5. core gcc</text>
  <rect x="229.7" y="160" width="190.3" height="18" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
  <text x="426.0" y="174" font-size="12">405 s</text>
  <text x="160" y="204" text-anchor="end" font-size="12">6. musl</text>
  <rect x="420.0" y="190" width="8.9" height="18" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
  <text x="434.9" y="204" font-size="12">19 s</text>
  <text x="160" y="234" text-anchor="end" font-size="12">7. final gcc</text>
  <rect x="429.0" y="220" width="203.0" height="18" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
  <text x="423.0" y="234" text-anchor="end" font-size="12">432 s</text>
  <text x="160" y="264" text-anchor="end" font-size="12">8. target binutils</text>
  <rect x="632.0" y="250" width="2" height="18" fill="#d0564a" fill-opacity="0.5" stroke="#d0564a"/>
  <text x="626.0" y="264" text-anchor="end" font-size="12">실패 (소스 삭제됨)</text>
  <line x1="170" y1="284" x2="645" y2="284" stroke="currentColor"/>
  <line x1="170.0" y1="284" x2="170.0" y2="289" stroke="currentColor"/>
  <text x="170.0" y="304" text-anchor="middle" font-size="12">0</text>
  <line x1="264.0" y1="284" x2="264.0" y2="289" stroke="currentColor"/>
  <text x="264.0" y="304" text-anchor="middle" font-size="12">200</text>
  <line x1="358.0" y1="284" x2="358.0" y2="289" stroke="currentColor"/>
  <text x="358.0" y="304" text-anchor="middle" font-size="12">400</text>
  <line x1="452.0" y1="284" x2="452.0" y2="289" stroke="currentColor"/>
  <text x="452.0" y="304" text-anchor="middle" font-size="12">600</text>
  <line x1="546.0" y1="284" x2="546.0" y2="289" stroke="currentColor"/>
  <text x="546.0" y="304" text-anchor="middle" font-size="12">800</text>
  <line x1="640.0" y1="284" x2="640.0" y2="289" stroke="currentColor"/>
  <text x="628" y="304" text-anchor="middle" font-size="12">1000 s</text>
  <text x="20" y="328" font-size="12">core gcc + final gcc = 837 s / 983 s (85%) — 빌드 시간의 대부분은 gcc다</text>
</svg>
```
그림 4 — crosstool-NG 빌드 단계 타임라인(위 로그의 `done in`/`at` 값으로 그림). 1~7번이 §5.1의 단계 그대로이고, 시간의 85%가 gcc 두 번(core, final)에 쓰였다. 8번은 내 정리 스크립트 때문에 실패한 선택 단계다.

출력에서 볼 것:

- 순서가 §5.1의 닭-달걀 해법 그대로다: 호스트 lib → binutils → 커널 헤더 → **core gcc → musl → final gcc**.
- musl 자체는 19 s로 짧다. 툴체인 빌드를 느리게 하는 것은 libc가 아니라 gcc다(glibc라면 libc 단계가 훨씬 길어진다).
- 실패한 것은 "binutils for target"(보드 위에서 돌릴 `as`/`ld`를 만드는 선택 단계)뿐이고, **크로스 툴체인 자체(final gcc + musl)는 이미 `/tmp/B5/x-tools/`에 설치된 뒤**였다. ct-ng의 마지막 정리 단계(strip, 설정 저장, 설치 디렉터리 읽기 전용화)는 돌지 않았다는 점은 알아 두자.
- 2 job 기준 약 16분. 32코어 빌드 서버라면 몇 분이다. 그래서 팀은 툴체인을 매번 빌드하지 않고 **한 번 빌드해서 아티팩트로 배포**한다.

### 5.5 만든 툴체인 써 보기 — 그리고 soft-float의 함정

무엇을 확인하나: §3의 신원 조회 스크립트로 방금 만든 툴체인을 확인한다.

```sh
limactl shell elinux -- bash -lc 'X=/tmp/B5/x-tools/arm-unknown-linux-musleabi; /Users/donh/.elinux_scratch/B5/inspect_tc.sh $X/bin/arm-unknown-linux-musleabi-gcc; file $X/bin/arm-unknown-linux-musleabi-gcc | cut -c1-110; du -sm $X/* | sort -n'
```

```text
== /tmp/B5/x-tools/arm-unknown-linux-musleabi/bin/arm-unknown-linux-musleabi-gcc
triplet       : arm-unknown-linux-musleabi
gcc version   : 16.2.0
sysroot       : /tmp/B5/x-tools/arm-unknown-linux-musleabi/arm-unknown-linux-musleabi/sysroot
configured    :
    --with-sysroot=/tmp/B5/x-tools/arm-unknown-linux-musleabi/arm-unknown-linux-musleabi/sysroot
    --with-pkgversion='crosstool-NG
    --disable-multilib
target dflts  :
    -mabi=                      		aapcs-linux
    -march=                     		armv5t
    -mcpu=                      		arm10tdmi
    -mfloat-abi=                		soft
    -mfpu=                      		auto
    -mtune=                     		
kernel hdrs   : 6.1.175  (LINUX_VERSION_CODE=393647)
version.h     : "/tmp/B5/x-tools/arm-unknown-linux-musleabi/arm-unknown-linux-musleabi/sysroot/usr/include/linux/version.h"
glibc hdrs    : __GLIBC__ __GLIBC_MINOR__
libc runtime  : musl libc (arm) Version 1.2.6 
/tmp/B5/x-tools/arm-unknown-linux-musleabi/bin/arm-unknown-linux-musleabi-gcc: ELF 64-bit LSB executable, ARM 
0	/tmp/B5/x-tools/arm-unknown-linux-musleabi/include
13	/tmp/B5/x-tools/arm-unknown-linux-musleabi/share
23	/tmp/B5/x-tools/arm-unknown-linux-musleabi/lib
36	/tmp/B5/x-tools/arm-unknown-linux-musleabi/arm-unknown-linux-musleabi
57	/tmp/B5/x-tools/arm-unknown-linux-musleabi/bin
82	/tmp/B5/x-tools/arm-unknown-linux-musleabi/libexec
```

(`file`과 `du`는 따로 실행한 결과를 이어 붙였다.)

출력에서 볼 것:

- 이 툴체인의 gcc는 **aarch64 호스트용**이다(`file` 출력의 `ELF 64-bit LSB executable, ARM` — 64-bit ARM = aarch64, 뒤는 `cut`으로 잘랐다). 내가 빌드한 머신에서 네이티브로 돈다 — Bootlin의 x86-64 문제(§2.3)가 없다. 이것이 crosstool-NG를 쓰는 실용적 이유 중 하나다.
- 기본값이 **`-march=armv5t`, `-mfloat-abi=soft`**다. 샘플이 CPU를 지정하지 않아(`CT_ARCH_CPU=""`, `CT_ARCH_FLOAT="auto"`) gcc의 ARM 기본값(ARMv5T, soft-float)이 그대로 들어왔다. 거의 모든 32-bit ARM에서 돌지만 FPU를 안 쓴다. **샘플 이름만 보고 쓰면 이런 기본값을 놓친다.**
- 커널 헤더는 우리가 고른 6.1.175, `--with-sysroot`가 실제 설치 경로(`/tmp/B5/x-tools/...`)다. Buildroot 래퍼가 없으므로 이 툴체인을 다른 경로로 옮기면 sysroot 경로가 어긋날 수 있다(ct-ng 툴체인도 대부분 relocatable하게 동작하지만, 확인이 필요하다).
- 전체 약 210 MB로 Bootlin(381 MB)보다 작다. C++·gdb·Python을 뺐기 때문이다.

무엇을 확인하나: soft-float 툴체인으로 만든 바이너리, 그리고 hard-float로 바꾸려고 할 때 일어나는 일.

```sh
limactl shell elinux -- bash -lc 'X=/tmp/B5/x-tools/arm-unknown-linux-musleabi; cd /tmp/B5 && $X/bin/arm-unknown-linux-musleabi-gcc -Wall -Wextra -O2 /Users/donh/.elinux_scratch/B5/hello_target.c -o hello_ctng && file hello_ctng && readelf -l hello_ctng | grep interpreter && qemu-arm -L $X/arm-unknown-linux-musleabi/sysroot ./hello_ctng && echo "--- -march=armv7-a -mfloat-abi=hard:" && $X/bin/arm-unknown-linux-musleabi-gcc -O2 -march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=hard /Users/donh/.elinux_scratch/B5/hello_target.c -o hello_ctng_hf; echo rc=$?'
```

```text
hello_ctng: ELF 32-bit LSB executable, ARM, EABI5 version 1 (SYSV), dynamically linked, interpreter /lib/ld-musl-arm.so.1, not stripped
      [Requesting program interpreter: /lib/ld-musl-arm.so.1]
compiler      : gcc 16.2.0
__ARM_ARCH    : 5
float ABI     : soft/softfp
libc          : not glibc (musl defines no version macro)
kernel headers: 6.1.175
sizeof(long)  : 4, sizeof(void *) : 4
runtime uname : Linux 7.0.0-34-generic armv7l
--- -march=armv7-a -mfloat-abi=hard:
/tmp/B5/x-tools/arm-unknown-linux-musleabi/arm-unknown-linux-musleabi/bin/ld: error: /tmp/ccOvivoY.o uses VFP register arguments, hello_ctng_hf does not
/tmp/B5/x-tools/arm-unknown-linux-musleabi/arm-unknown-linux-musleabi/bin/ld: failed to merge target specific data of file /tmp/ccOvivoY.o
collect2: error: ld returned 1 exit status
rc=1
```

출력에서 볼 것:

- 로더 이름이 `/lib/ld-musl-arm.so.1`(soft)이다. Bootlin 것은 `ld-musl-armhf.so.1`(hard)이었다. §4.4 표 그대로다.
- `-mfloat-abi=hard`를 명령줄에서 주면 **내 오브젝트는 hard, 툴체인의 musl·crt 파일은 soft**라서 링커가 "uses VFP register arguments, ... does not"로 거부한다. float ABI는 옵션 하나로 바꿀 수 있는 것이 아니라 **툴체인(정확히는 sysroot의 libc) 전체의 속성**이다. hard-float가 필요하면 ct-ng 설정에서 `CT_ARCH_FLOAT_HW`/`CT_ARCH_ARCH`/`CT_ARCH_FPU`를 정해 다시 빌드하거나 `arm-...-musleabihf` 툴체인을 쓴다.

무엇을 확인하나: 두 ABI의 바이너리를 서로의 sysroot(= 서로 다른 rootfs)에서 실행.

```sh
limactl shell elinux -- bash -lc 'B=/tmp/B5/armv7-eabihf--musl--stable-2026.08-1/arm-buildroot-linux-musleabihf/sysroot; C=/tmp/B5/x-tools/arm-unknown-linux-musleabi/arm-unknown-linux-musleabi/sysroot; cd /tmp/B5; echo "[soft-float ct-ng binary on armhf (Bootlin) sysroot]"; qemu-arm -L $B ./hello_ctng; echo rc=$?; echo "[hard-float Bootlin binary on soft-float (ct-ng) sysroot]"; qemu-arm -L $C ./hello_bootlin; echo rc=$?'
```

```text
[soft-float ct-ng binary on armhf (Bootlin) sysroot]
qemu-arm: Could not open '/lib/ld-musl-arm.so.1': No such file or directory
rc=255
[hard-float Bootlin binary on soft-float (ct-ng) sysroot]
qemu-arm: Could not open '/lib/ld-musl-armhf.so.1': No such file or directory
rc=255
```

(실제로는 Bootlin tarball을 다시 받아 sha256 확인 후 풀고 `hello_bootlin`을 다시 빌드한 뒤 실행했다.)

출력에서 볼 것: 같은 musl 1.2.6, 같은 32-bit ARM인데도 **float ABI가 다르면 서로의 rootfs에서 시작조차 못 한다.** 보드 셸에서는 `-sh: ./hello_ctng: not found`로 보인다(그림 3의 3행).

### 5.6 Buildroot internal 툴체인과의 관계

Bootlin tarball 안의 `buildroot.config`를 보면, Bootlin 툴체인이 바로 **Buildroot internal 툴체인 설정 17줄**로 만들어진 것임을 알 수 있다.

```sh
limactl shell elinux -- bash -lc 'cat /tmp/B5/armv7-eabihf--musl--stable-2026.08-1/buildroot.config'
```

```text
BR2_arm=y
BR2_cortex_a9=y
BR2_ARM_CPU_ARMV7A=y
BR2_ARM_ENABLE_VFP=y
BR2_ARM_EABIHF=y
BR2_TOOLCHAIN_BUILDROOT_MUSL=y
BR2_KERNEL_HEADERS_5_10=y
BR2_TOOLCHAIN_BUILDROOT_CXX=y
BR2_INIT_NONE=y
BR2_SYSTEM_BIN_SH_NONE=y
# BR2_PACKAGE_BUSYBOX is not set
# BR2_TARGET_ROOTFS_TAR is not set
BR2_PACKAGE_HOST_GDB=y
BR2_PACKAGE_HOST_GDB_TUI=y
BR2_PACKAGE_HOST_GDB_PYTHON3=y
BR2_PACKAGE_GDB=y
BR2_GCC_ENABLE_OPENMP=y
```

출력에서 볼 것: CPU(`BR2_cortex_a9`), float ABI(`BR2_ARM_EABIHF`), libc(`MUSL`), 커널 헤더(`5_10`) 네 줄이 §3.2에서 조회한 신원과 정확히 대응한다. init·셸·busybox·rootfs 이미지는 꺼서 **"rootfs 없이 툴체인만"** 만든다. 즉 crosstool-NG와 Buildroot internal 툴체인은 같은 일을 하는 두 도구이고, 차이는 Buildroot는 그 툴체인으로 **곧바로 rootfs까지 만든다**는 것이다.

| 비교 | crosstool-NG | Buildroot internal |
|---|---|---|
| 산출물 | 툴체인 디렉터리 (`~/x-tools/<triplet>`) | 툴체인 + rootfs 이미지 (+ `make sdk`) |
| 설정 | `ct-ng menuconfig` (Kconfig) | `make menuconfig`의 Toolchain 메뉴 (Kconfig) |
| rootfs와 libc 일치 | 사람이 맞춤 | 자동 (같은 빌드) |
| 적합한 경우 | 툴체인을 여러 프로젝트에 배포, 특이 조합 | Buildroot로 이미지를 만드는 프로젝트 |

---


## 6. Yocto SDK 흉내 내기 — `environment-setup` 스크립트를 손으로

### 6.1 직관 — "스크립트 하나가 빌드 시스템 전체를 크로스로 바꾼다"

Yocto SDK를 설치하고 `source environment-setup-...`을 하면, 그 셸에서는 평범한 `make`, `cmake`, `./configure`가 갑자기 ARM용 바이너리를 만든다. 마법이 아니라 **환경 변수**다. 빌드 시스템들은 원래 `CC`, `CFLAGS`, `PKG_CONFIG_PATH` 같은 변수를 읽도록 약속되어 있고, SDK 스크립트는 그 변수들을 타깃용으로 채워 줄 뿐이다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="15" y="125" width="170" height="70" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="100" y="152" text-anchor="middle" font-size="13">source</text>
  <text x="100" y="172" text-anchor="middle" font-size="12">env-setup-armv7-musl</text>
  <rect x="225" y="20" width="210" height="290" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="330" y="40" text-anchor="middle" font-size="13" font-weight="bold">export 되는 변수</text>
  <text x="240" y="66" font-size="12">PATH += 툴체인 bin/</text>
  <text x="240" y="92" font-size="12">CC = gcc -mcpu.. --sysroot</text>
  <text x="240" y="118" font-size="12">CFLAGS, LDFLAGS</text>
  <text x="240" y="144" font-size="12">PKG_CONFIG_SYSROOT_DIR</text>
  <text x="240" y="170" font-size="12">PKG_CONFIG_PATH / LIBDIR</text>
  <text x="240" y="196" font-size="12">CMAKE_TOOLCHAIN_FILE</text>
  <text x="240" y="222" font-size="12">CONFIGURE_FLAGS (--host)</text>
  <text x="240" y="248" font-size="12">ARCH, CROSS_COMPILE</text>
  <text x="240" y="274" font-size="12">SDKTARGETSYSROOT</text>
  <line x1="185" y1="160" x2="222" y2="160" stroke="currentColor" marker-end="url(#ah6)"/>
  <rect x="480" y="30" width="185" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="572" y="55" text-anchor="middle" font-size="12">make ($(CC), $(CFLAGS))</text>
  <rect x="480" y="85" width="185" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="572" y="110" text-anchor="middle" font-size="12">cmake (툴체인 파일)</text>
  <rect x="480" y="140" width="185" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="572" y="165" text-anchor="middle" font-size="12">./configure --host=...</text>
  <rect x="480" y="195" width="185" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="572" y="220" text-anchor="middle" font-size="12">pkg-config → sysroot .pc</text>
  <rect x="480" y="250" width="185" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="572" y="275" text-anchor="middle" font-size="12">커널·모듈 Kbuild</text>
  <line x1="435" y1="88" x2="478" y2="50" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="435" y1="196" x2="478" y2="105" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="435" y1="222" x2="478" y2="160" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="435" y1="157" x2="478" y2="215" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="435" y1="248" x2="478" y2="270" stroke="currentColor" marker-end="url(#ah6)"/>
</svg>
```
그림 5 — `environment-setup` 스크립트의 정체. 변수를 export할 뿐이고, 각 빌드 도구가 약속된 변수를 읽어 크로스 빌드로 바뀐다.

### 6.2 스크립트 직접 쓰기

Yocto SDK 대신 §2.3의 Bootlin 툴체인을 쓰되, **타깃 sysroot를 따로 복사**해서 "우리 이미지의 sysroot"처럼 쓴다(`/tmp/B5/sdk-sysroot`). 여기에 "벤더가 준 센서 라이브러리" 역할의 `libimu`를 설치해서 pkg-config까지 확인한다.

```sh
# environment-setup-armv7-musl : Yocto SDK의 environment-setup-* 를 손으로 흉내 낸 것
# 사용: source env-setup-armv7-musl   (실행이 아니라 source!)
export SDK_TC=/tmp/B5/armv7-eabihf--musl--stable-2026.08-1
export SDKTARGETSYSROOT=/tmp/B5/sdk-sysroot          # 타깃 rootfs의 "개발용 복사본"
export PATH="$SDK_TC/bin:$PATH"
export TARGET_PREFIX=arm-buildroot-linux-musleabihf-
export CROSS_COMPILE=$TARGET_PREFIX                   # 커널/U-Boot Makefile이 읽는 변수
export ARCH=arm                                       # 커널 Kbuild가 읽는 변수
TUNE="-mcpu=cortex-a9 -mfpu=vfpv3-d16 -mfloat-abi=hard"
export CC="${TARGET_PREFIX}gcc $TUNE --sysroot=$SDKTARGETSYSROOT"
export CXX="${TARGET_PREFIX}g++ $TUNE --sysroot=$SDKTARGETSYSROOT"
export CPP="${TARGET_PREFIX}gcc -E $TUNE --sysroot=$SDKTARGETSYSROOT"
export AS="${TARGET_PREFIX}as"
export LD="${TARGET_PREFIX}ld --sysroot=$SDKTARGETSYSROOT"
export AR=${TARGET_PREFIX}ar NM=${TARGET_PREFIX}nm STRIP=${TARGET_PREFIX}strip
export OBJCOPY=${TARGET_PREFIX}objcopy OBJDUMP=${TARGET_PREFIX}objdump RANLIB=${TARGET_PREFIX}ranlib
export CFLAGS="-O2 -pipe -g"
export CXXFLAGS="$CFLAGS"
export LDFLAGS="-Wl,-O1 -Wl,--hash-style=gnu -Wl,--as-needed"
# pkg-config가 "호스트 /usr/lib" 대신 타깃 sysroot를 보게 한다
export PKG_CONFIG_SYSROOT_DIR=$SDKTARGETSYSROOT
export PKG_CONFIG_PATH=$SDKTARGETSYSROOT/usr/lib/pkgconfig:$SDKTARGETSYSROOT/usr/share/pkgconfig
export PKG_CONFIG_LIBDIR=$SDKTARGETSYSROOT/usr/lib/pkgconfig   # 호스트 기본 경로 차단
# CMake 3.21+ 은 환경 변수 CMAKE_TOOLCHAIN_FILE 을 읽는다
export CMAKE_TOOLCHAIN_FILE=/Users/donh/.elinux_scratch/B5/armv7-musl.cmake
# configure 스크립트용 (autotools --host 값)
export CONFIGURE_FLAGS="--host=arm-buildroot-linux-musleabihf --with-sysroot=$SDKTARGETSYSROOT"
# --- 이 VM 전용: x86_64 호스트용 Bootlin 툴체인을 arm64 VM에서 돌리기 위한 qemu-x86_64 루트
export QEMU_LD_PREFIX=/tmp/B5/x86root
PS1="[armv7-musl-sdk] $PS1"
```

CMake용 툴체인 파일은 Yocto SDK의 `OEToolchainConfig.cmake`에 해당한다.

```sh
# armv7-musl.cmake : Yocto SDK의 OEToolchainConfig.cmake 역할을 하는 최소 툴체인 파일
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_SYSROOT $ENV{SDKTARGETSYSROOT})
set(CMAKE_C_COMPILER $ENV{SDK_TC}/bin/arm-buildroot-linux-musleabihf-gcc)
set(CMAKE_C_FLAGS_INIT "-mcpu=cortex-a9 -mfpu=vfpv3-d16 -mfloat-abi=hard")
# 헤더·라이브러리·패키지는 sysroot 안에서만, 실행 프로그램(빌드 도구)은 호스트에서만 찾는다
set(CMAKE_FIND_ROOT_PATH $ENV{SDKTARGETSYSROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
```

"벤더 라이브러리" `libimu`(헤더, 소스, `.pc`)는 다음과 같다.

```c
/* imu.h : "벤더가 준 센서 라이브러리" 역할 */
#ifndef IMU_H
#define IMU_H
struct imu_sample { double ax, ay, az; };
int imu_read(struct imu_sample *s, int i);
const char *imu_version(void);
#endif
```

```c
#include <math.h>
#include "imu.h"
int imu_read(struct imu_sample *s, int i)
{
    s->ax = 0.01 * sin(i * 0.1);   /* 가짜 데이터: 진동 + 중력 */
    s->ay = 0.01 * cos(i * 0.1);
    s->az = 9.81;
    return 0;
}
const char *imu_version(void) { return "libimu 1.2.0"; }
```

```text
prefix=/usr
libdir=${prefix}/lib
includedir=${prefix}/include

Name: imu
Description: fake IMU vendor library
Version: 1.2.0
Libs: -L${libdir} -limu
Libs.private: -lm
Cflags: -I${includedir}/imu
```

`.pc` 파일의 `prefix=/usr`가 포인트다. 이 파일은 **타깃 기준 경로**로 쓰여 있다(보드에서 라이브러리는 `/usr/lib`에 있으니까). 호스트에서 크로스 빌드할 때 pkg-config가 그 앞에 sysroot를 붙여 주는 것이 `PKG_CONFIG_SYSROOT_DIR`의 역할이다.

무엇을 확인하나: sysroot 사본에 libimu를 크로스 빌드해서 설치하고, SDK 환경과 일반 환경에서 pkg-config 결과를 비교한다.

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.elinux_scratch/B5; cp -a /tmp/B5/armv7-eabihf--musl--stable-2026.08-1/arm-buildroot-linux-musleabihf/sysroot /tmp/B5/sdk-sysroot && source $S/env-setup-armv7-musl && echo "CC=$CC" && R=$SDKTARGETSYSROOT && mkdir -p $R/usr/include/imu $R/usr/lib/pkgconfig && $CC $CFLAGS -Wall -Wextra -fPIC -shared $S/libimu/imu.c -lm -Wl,-soname,libimu.so.1 -o $R/usr/lib/libimu.so.1 && ln -sf libimu.so.1 $R/usr/lib/libimu.so && cp $S/libimu/imu.h $R/usr/include/imu/ && cp $S/libimu/imu.pc $R/usr/lib/pkgconfig/ && echo "--- pkg-config view:" && pkg-config --cflags --libs imu && pkg-config --modversion imu && echo "--- without SDK env:" && env -u PKG_CONFIG_SYSROOT_DIR -u PKG_CONFIG_PATH -u PKG_CONFIG_LIBDIR pkg-config --libs imu; echo rc=$?'
```

```text
CC=arm-buildroot-linux-musleabihf-gcc -mcpu=cortex-a9 -mfpu=vfpv3-d16 -mfloat-abi=hard --sysroot=/tmp/B5/sdk-sysroot
--- pkg-config view:
-I/tmp/B5/sdk-sysroot/usr/include/imu -L/tmp/B5/sdk-sysroot/usr/lib -limu
1.2.0
--- without SDK env:
Package imu was not found in the pkg-config search path.
Perhaps you should add the directory containing `imu.pc'
to the PKG_CONFIG_PATH environment variable
Package 'imu' not found
rc=1
```

출력에서 볼 것: `.pc` 안에는 `/usr/include/imu`라고 적혀 있지만 pkg-config가 **`/tmp/B5/sdk-sysroot`를 앞에 붙여서** 돌려준다. SDK 변수가 없으면 호스트 경로만 뒤지므로 못 찾는다. 반대로 더 위험한 경우는 **호스트에도 같은 이름의 `.pc`가 있을 때**다. `PKG_CONFIG_LIBDIR`로 호스트 기본 경로를 막지 않으면 호스트(aarch64 또는 x86)용 `-L/usr/lib/...`가 섞여 들어가 "file in wrong format" 링크 에러가 난다.

### 6.3 Make 프로젝트 — 크로스를 모르는 Makefile

앱 소스(`main.c`, `filter.c`, `filter.h`)와 Makefile은 크로스 빌드에 대해 아무것도 모른다. `CC`, `CFLAGS`, `LDFLAGS`, `PKG_CONFIG`를 환경에서 받을 뿐이다. 이것이 좋은 Makefile의 조건이다(B4).

```c
/* imu_app : sysroot 안의 libimu를 pkg-config로 찾아 링크하는 작은 앱 */
#include <math.h>
#include <stdio.h>
#include <imu.h>
#include "filter.h"

int main(void)
{
    struct imu_sample s;
    double lpf = 0.0;
    for (int i = 0; i < 50; i++) {
        if (imu_read(&s, i) < 0) { fprintf(stderr, "imu_read failed\n"); return 1; }
        double mag = sqrt(s.ax * s.ax + s.ay * s.ay + s.az * s.az);
        lpf = lowpass(lpf, mag, 0.2);
    }
    printf("%s, |a| filtered = %.4f m/s^2\n", imu_version(), lpf);
    return 0;
}
```

```c
/* filter.h */
#ifndef FILTER_H
#define FILTER_H
double lowpass(double prev, double x, double alpha);
#endif
/* filter.c */
#include "filter.h"
double lowpass(double prev, double x, double alpha) { return prev + alpha * (x - prev); }
```

```text
# 크로스 여부를 모르는 평범한 Makefile: CC/CFLAGS/LDFLAGS/PKG_CONFIG 는 환경에서 온다
PKG_CONFIG ?= pkg-config
CFLAGS += -Wall -Wextra $(shell $(PKG_CONFIG) --cflags imu)
LDLIBS += $(shell $(PKG_CONFIG) --libs imu) -lm
OUT ?= .
$(OUT)/imu_app: $(OUT)/main.o $(OUT)/filter.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@
$(OUT)/%.o: %.c filter.h
	$(CC) $(CFLAGS) -c $< -o $@
clean:
	rm -f $(OUT)/*.o $(OUT)/imu_app
```

(`filter.h`와 `filter.c`는 실제로는 두 파일이다. 지면을 아끼려고 한 블록에 붙였다.)

무엇을 확인하나: `source` 한 번으로 평범한 `make`가 ARM 바이너리를 만들고, 그 바이너리가 sysroot의 libimu를 물고 실행되는지.

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.elinux_scratch/B5; source $S/env-setup-armv7-musl; mkdir -p /tmp/B5/build-make && make -C $S/proj OUT=/tmp/B5/build-make 2>&1 | sed "s|$S|\$S|g"; file /tmp/B5/build-make/imu_app | cut -c1-110; ${TARGET_PREFIX}readelf -d /tmp/B5/build-make/imu_app | grep NEEDED; qemu-arm -L $SDKTARGETSYSROOT /tmp/B5/build-make/imu_app'
```

```text
make: Entering directory '$S/proj'
arm-buildroot-linux-musleabihf-gcc -mcpu=cortex-a9 -mfpu=vfpv3-d16 -mfloat-abi=hard --sysroot=/tmp/B5/sdk-sysroot -O2 -pipe -g -Wall -Wextra -I/tmp/B5/sdk-sysroot/usr/include/imu -c main.c -o /tmp/B5/build-make/main.o
arm-buildroot-linux-musleabihf-gcc -mcpu=cortex-a9 -mfpu=vfpv3-d16 -mfloat-abi=hard --sysroot=/tmp/B5/sdk-sysroot -O2 -pipe -g -Wall -Wextra -I/tmp/B5/sdk-sysroot/usr/include/imu -c filter.c -o /tmp/B5/build-make/filter.o
arm-buildroot-linux-musleabihf-gcc -mcpu=cortex-a9 -mfpu=vfpv3-d16 -mfloat-abi=hard --sysroot=/tmp/B5/sdk-sysroot -Wl,-O1 -Wl,--hash-style=gnu -Wl,--as-needed /tmp/B5/build-make/main.o /tmp/B5/build-make/filter.o -L/tmp/B5/sdk-sysroot/usr/lib -limu -lm -o /tmp/B5/build-make/imu_app
make: Leaving directory '$S/proj'
/tmp/B5/build-make/imu_app: ELF 32-bit LSB pie executable, ARM, EABI5 version 1 (SYSV), dynamically linked, in
 0x00000001 (NEEDED)                     Shared library: [libimu.so.1]
 0x00000001 (NEEDED)                     Shared library: [libc.so]
libimu 1.2.0, |a| filtered = 9.8099 m/s^2
```

출력에서 볼 것:

- Makefile에는 `arm`이라는 글자가 하나도 없는데, 실행된 명령은 전부 `arm-buildroot-linux-musleabihf-gcc ... --sysroot=...`이다. **크로스 빌드의 책임이 Makefile이 아니라 환경에 있다.**
- `NEEDED`에 `libimu.so.1`(우리가 준 soname)과 `libc.so`(musl의 libc 이름)가 들어갔다. 보드 rootfs에 `libimu.so.1`이 없으면 실행이 안 된다 — 그래서 Yocto는 "앱 레시피의 `RDEPENDS`"로 그 라이브러리를 이미지에 함께 넣는다.
- `-lm`이 링크 명령에 있는데 `NEEDED`에 `libm`이 없다. musl은 libm이 libc 안에 들어 있고, `--as-needed`가 필요 없는 의존성을 빼 준다.

### 6.4 CMake 프로젝트 — 환경 변수 `CMAKE_TOOLCHAIN_FILE`

```text
cmake_minimum_required(VERSION 3.21)
project(imu_app C)
find_package(PkgConfig REQUIRED)
pkg_check_modules(IMU REQUIRED IMPORTED_TARGET imu)
add_executable(imu_app main.c filter.c)
target_compile_options(imu_app PRIVATE -Wall -Wextra)
target_link_libraries(imu_app PRIVATE PkgConfig::IMU m)
```

무엇을 확인하나: `-DCMAKE_TOOLCHAIN_FILE=...`을 명령줄에 쓰지 않아도 환경 변수만으로 CMake가 크로스 설정을 잡는지.

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.elinux_scratch/B5; source $S/env-setup-armv7-musl; cmake --version | head -1; cmake -S $S/proj -B /tmp/B5/build-cmake 2>&1 | grep -v "^--   " | sed "s|$S|\$S|g"; cmake --build /tmp/B5/build-cmake 2>&1 | sed "s|$S|\$S|g"; grep -E "^IMU_LIBRARIES|^IMU_INCLUDE_DIRS" /tmp/B5/build-cmake/CMakeCache.txt; qemu-arm -L $SDKTARGETSYSROOT /tmp/B5/build-cmake/imu_app'
```

```text
cmake version 4.2.3
-- The C compiler identification is GNU 15.3.0
-- Detecting C compiler ABI info
-- Detecting C compiler ABI info - done
-- Check for working C compiler: /tmp/B5/armv7-eabihf--musl--stable-2026.08-1/bin/arm-buildroot-linux-musleabihf-gcc - skipped
-- Detecting C compile features
-- Detecting C compile features - done
-- Found PkgConfig: /usr/bin/pkg-config (found version "2.5.1")
-- Checking for module 'imu'
-- Configuring done (1.6s)
-- Generating done (0.0s)
-- Build files have been written to: /tmp/B5/build-cmake
[ 33%] Building C object CMakeFiles/imu_app.dir/main.c.o
[ 66%] Building C object CMakeFiles/imu_app.dir/filter.c.o
[100%] Linking C executable imu_app
[100%] Built target imu_app
IMU_INCLUDE_DIRS:INTERNAL=/tmp/B5/sdk-sysroot/usr/include/imu
IMU_LIBRARIES:INTERNAL=imu
libimu 1.2.0, |a| filtered = 9.8099 m/s^2
```

```sh
limactl shell elinux -- bash -lc 'cd /tmp/B5/build-cmake && grep -E "C_FLAGS|C_INCLUDES" CMakeFiles/imu_app.dir/flags.make; cat CMakeFiles/imu_app.dir/link.txt'
```

```text
C_INCLUDES = -isystem /tmp/B5/sdk-sysroot/usr/include/imu
C_FLAGS = -O2 -pipe -g -mcpu=cortex-a9 -mfpu=vfpv3-d16 -mfloat-abi=hard -Wall -Wextra
/tmp/B5/armv7-eabihf--musl--stable-2026.08-1/bin/arm-buildroot-linux-musleabihf-gcc --sysroot=/tmp/B5/sdk-sysroot -O2 -pipe -g -mcpu=cortex-a9 -mfpu=vfpv3-d16 -mfloat-abi=hard -Wl,-O1 -Wl,--hash-style=gnu -Wl,--as-needed -Wl,--dependency-file=CMakeFiles/imu_app.dir/link.d CMakeFiles/imu_app.dir/main.c.o CMakeFiles/imu_app.dir/filter.c.o -o imu_app  -lm /tmp/B5/sdk-sysroot/usr/lib/libimu.so
```

출력에서 볼 것:

- "Check for working C compiler ... **skipped**": 크로스 모드라서 CMake가 테스트 프로그램을 **실행**하지 않는다(`CMAKE_SYSTEM_NAME`을 지정하면 `CMAKE_CROSSCOMPILING`이 켜진다). `try_run()`을 쓰는 오래된 CMake 프로젝트가 크로스에서 깨지는 이유다.
- `C_FLAGS` 앞부분 `-O2 -pipe -g`는 **환경 변수 `CFLAGS`**에서 왔고, `-mcpu...`는 툴체인 파일의 `CMAKE_C_FLAGS_INIT`에서 왔다. `--sysroot`는 `CMAKE_SYSROOT`에서 왔다. 세 경로가 합쳐진 것이다.
- pkg-config가 찾은 `libimu.so`가 sysroot 안의 절대 경로로 링크된다. `CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY`가 호스트 `/usr/lib`를 막는다.

> 함정 메모: CMake는 `CC`·`CFLAGS`·`CMAKE_TOOLCHAIN_FILE` 환경 변수를 **첫 configure 때만** 읽고 캐시에 저장한다. SDK 환경을 `source`하지 않고 한 번 configure한 빌드 디렉터리는, 나중에 `source`해도 호스트 컴파일러를 계속 쓴다. 증상이 이상하면 빌드 디렉터리를 지우고 다시 configure한다.

---

## 7. 재현성 — 같은 소스, 같은 바이너리, 같은 팀 환경

### 7.1 왜 중요한가

펌웨어 팀에서 "컴파일러 버전 고정"을 했던 이유는 양산 바이너리를 **6개월 뒤에도 똑같이 다시 만들 수 있어야** 해서다(필드 이슈 재현, 인증, 보안 패치). Linux 툴체인에서도 같다. 다만 고정할 것이 많다.

| 고정 대상 | 방법 | 이 노트에서 한 것 |
|---|---|---|
| 툴체인 바이너리 | 버전 이름 + **sha256 체크섬**을 저장소에 기록, 내부 미러에 보관 | Bootlin `.sha256`, Arm `.sha256asc`, ct-ng `chksum`으로 검증 |
| 툴체인 빌드 방법 | Bootlin: git 태그 + defconfig. ct-ng: `.config`(defconfig) 커밋 | ct-ng `.config` 수정을 스크립트로 남김 (§5) |
| 호스트 환경 | **컨테이너 이미지**(예: `ubuntu:24.04` 기반 Dockerfile)를 digest로 고정 | (이 VM이 그 역할) |
| 빌드 경로 | `-ffile-prefix-map`으로 디버그 정보의 절대 경로 제거 | §7.2 |
| 빌드 속도 | ccache, sstate(Yocto), 미리 받은 다운로드 디렉터리 | §7.3 |

### 7.2 빌드 경로가 바이너리에 새는 문제

무엇을 확인하나: 같은 소스를 다른 디렉터리에서 `-g`로 빌드하면 오브젝트가 달라지는지, `-ffile-prefix-map`으로 같아지는지.

```sh
limactl shell elinux -- bash -lc 'for d in a b; do mkdir -p /tmp/B5/repro/$d/work_$d; cp /Users/donh/.elinux_scratch/B5/hello_target.c /tmp/B5/repro/$d/work_$d/; done; cd /tmp/B5/repro; for d in a b; do (cd $d/work_$d && arm-linux-gnueabihf-gcc -O2 -g -c hello_target.c -o ../h.o); done; echo "[plain -g]"; sha256sum a/h.o b/h.o | cut -c1-16,65-; arm-linux-gnueabihf-readelf --debug-dump=info a/h.o | grep -m1 DW_AT_comp_dir; for d in a b; do (cd $d/work_$d && arm-linux-gnueabihf-gcc -O2 -g -ffile-prefix-map=$PWD=. -c hello_target.c -o ../h2.o); done; echo "[-ffile-prefix-map=\$PWD=.]"; sha256sum a/h2.o b/h2.o | cut -c1-16,65-; arm-linux-gnueabihf-readelf --debug-dump=info a/h2.o | grep -m1 DW_AT_comp_dir'
```

```text
[plain -g]
46670f8a50c1bf9f  a/h.o
1132c4a39687c41a  b/h.o
    <1b>   DW_AT_comp_dir    : (indirect string, offset: 0x87): /tmp/B5/repro/a/work_a
[-ffile-prefix-map=$PWD=.]
ab1c6cd2d600638c  a/h2.o
ab1c6cd2d600638c  b/h2.o
    <1b>   DW_AT_comp_dir    : .
```

출력에서 볼 것: DWARF의 `DW_AT_comp_dir`에 빌드 디렉터리 절대 경로가 박혀서 해시가 달라진다. `-ffile-prefix-map=<빌드경로>=.`을 주면 두 오브젝트가 **비트 단위로 같아진다.** Yocto와 Buildroot는 이 옵션을 기본으로 넣어 재현 가능한 빌드(reproducible builds)를 만든다. ccache 적중률에도 영향을 준다(경로가 다르면 캐시 키가 달라질 수 있다).

### 7.3 ccache — cold vs warm 측정

ccache는 "전처리된 소스 + 컴파일 옵션 + **컴파일러 자체의 해시**"를 키로 오브젝트 파일을 캐시한다. 컴파일러 바이너리가 바뀌면(툴체인 업그레이드) 키가 바뀌므로 잘못된 캐시를 쓰지 않는다.

무엇을 확인하나: zstd 1.5.7 라이브러리(C 파일 26개, ct-ng가 받아 둔 tarball)를 `make -j2`로 빌드할 때, 캐시 없음 / ccache 첫 빌드(cold) / ccache 재빌드(warm) 시간. 세 툴체인(Ubuntu 크로스, Bootlin, §5에서 만든 ct-ng)으로 비교한다.

```text
# bench.mk : zstd 라이브러리 C 파일을 OUT 디렉터리로 컴파일 (ccache 측정용)
SRCS := $(wildcard $(ZSTD)/lib/common/*.c $(ZSTD)/lib/compress/*.c $(ZSTD)/lib/decompress/*.c)
OBJS := $(patsubst $(ZSTD)/lib/%.c,$(OUT)/%.o,$(SRCS))
CFLAGS := -O2 -g -I$(ZSTD)/lib -I$(ZSTD)/lib/common -DZSTD_DISABLE_ASM
all: $(OBJS)
$(OUT)/%.o: $(ZSTD)/lib/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
clean:
	rm -rf $(OUT)
```

```sh
#!/bin/bash
# ccache_bench.sh <label> <compiler> : 캐시 없음 / ccache cold / ccache warm 빌드 시간 (make -j2, zstd lib 26 파일)
LABEL=$1; CCX=$2
export CCACHE_DIR=/tmp/B5/ccache CCACHE_MAXSIZE=300M QEMU_LD_PREFIX=/tmp/B5/x86root
M="make -s -j2 -f /Users/donh/.elinux_scratch/B5/bench.mk ZSTD=/tmp/B5/bench/zstd-1.5.7 OUT=/tmp/B5/bench/out"
t() { local s=$(date +%s.%N); "$@" >/dev/null 2>&1 || echo "build failed"; echo "$(date +%s.%N) - $s" | bc; }
$M clean; $M CC="$CCX" >/dev/null 2>&1; $M clean          # 페이지 캐시 데우기 (측정 제외)
plain=$(t $M CC="$CCX"); $M clean
ccache -C >/dev/null; ccache -z >/dev/null
cold=$(t $M CC="ccache $CCX"); $M clean
warm=$(t $M CC="ccache $CCX")
printf "%-8s plain=%6.2fs  ccache-cold=%6.2fs  ccache-warm=%6.2fs  speedup(plain/warm)=%.1fx\n" \
       $LABEL $plain $cold $warm $(echo "$plain / $warm" | bc -l)
ccache -s | grep -E "Hits|Misses|Cache size"
$M clean
```

```sh
limactl shell elinux -- bash -lc 'mkdir -p /tmp/B5/bench && cd /tmp/B5/bench && tar xf /tmp/B5/tarballs/zstd-1.5.7.tar.gz && ls zstd-1.5.7/lib/{common,compress,decompress}/*.c | wc -l; ccache --version | head -1'
limactl shell elinux -- bash -lc '/Users/donh/.elinux_scratch/B5/ccache_bench.sh distro arm-linux-gnueabihf-gcc'
limactl shell elinux -- bash -lc '/Users/donh/.elinux_scratch/B5/ccache_bench.sh bootlin /tmp/B5/armv7-eabihf--musl--stable-2026.08-1/bin/arm-buildroot-linux-musleabihf-gcc'
limactl shell elinux -- bash -lc '/Users/donh/.elinux_scratch/B5/ccache_bench.sh ctng /tmp/B5/x-tools/arm-unknown-linux-musleabi/bin/arm-unknown-linux-musleabi-gcc'
```

```text
26
ccache version 4.12.3
distro   plain=  8.05s  ccache-cold=  8.22s  ccache-warm=  0.04s  speedup(plain/warm)=216.2x
  Hits:             26 /  52 (50.00%)
  Misses:           26 /  52 (50.00%)
  Cache size (GB): 0.0 / 0.3 ( 0.78%)
  Hits:             26 /  52 (50.00%)
  Misses:           26 /  52 (50.00%)
bootlin  plain= 88.47s  ccache-cold= 91.43s  ccache-warm=  0.04s  speedup(plain/warm)=2473.3x
  Hits:             26 /  52 (50.00%)
  Misses:           26 /  52 (50.00%)
  Cache size (GB): 0.0 / 0.3 ( 0.78%)
  Hits:             26 /  52 (50.00%)
  Misses:           26 /  52 (50.00%)
ctng     plain=  8.74s  ccache-cold=  8.87s  ccache-warm=  0.03s  speedup(plain/warm)=254.2x
  Hits:             26 /  52 (50.00%)
  Misses:           26 /  52 (50.00%)
  Cache size (GB): 0.0 / 0.3 ( 0.80%)
  Hits:             26 /  52 (50.00%)
  Misses:           26 /  52 (50.00%)
```

(`ccache -s`의 Hits/Misses 줄은 ccache 4.12 출력이 두 섹션에 같은 값을 반복해서 두 번씩 나온다. 측정 시 VM의 다른 작업 때문에 load average가 0.6~1.6 정도였다. `speedup` 값은 반올림 전 시간으로 나눈 것이라 그림의 "약 N배"와 조금 다르다. ct-ng 측정은 §5에서 툴체인을 만든 뒤 따로 돌렸다.)

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
  <text x="20" y="22" font-size="13" font-weight="bold">zstd lib 26파일 빌드 시간 (make -j2, 초) — 실측</text>
  <text x="20" y="46" font-size="13">Ubuntu 크로스 gcc 15.2 (aarch64 네이티브)</text>
  <text x="190" y="70" text-anchor="end" font-size="12">캐시 없음</text>
  <rect x="200" y="56" width="35.2" height="18" fill="#888" fill-opacity="0.6" stroke="#888"/>
  <text x="241.2" y="70" font-size="12">8.05 s</text>
  <text x="190" y="94" text-anchor="end" font-size="12">ccache cold</text>
  <rect x="200" y="80" width="36.0" height="18" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="242.0" y="94" font-size="12">8.22 s</text>
  <text x="190" y="118" text-anchor="end" font-size="12">ccache warm</text>
  <rect x="200" y="104" width="2" height="18" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <text x="208" y="118" font-size="12">0.04 s (약 200배)</text>
  <text x="20" y="146" font-size="13">crosstool-NG gcc 16.2 (이 노트에서 빌드, 네이티브)</text>
  <text x="190" y="170" text-anchor="end" font-size="12">캐시 없음</text>
  <rect x="200" y="156" width="38.2" height="18" fill="#888" fill-opacity="0.6" stroke="#888"/>
  <text x="244.2" y="170" font-size="12">8.74 s</text>
  <text x="190" y="194" text-anchor="end" font-size="12">ccache cold</text>
  <rect x="200" y="180" width="38.8" height="18" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="244.8" y="194" font-size="12">8.87 s</text>
  <text x="190" y="218" text-anchor="end" font-size="12">ccache warm</text>
  <rect x="200" y="204" width="2" height="18" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <text x="208" y="218" font-size="12">0.03 s (약 290배)</text>
  <text x="20" y="246" font-size="13">Bootlin gcc 15.3 (x86-64, qemu-x86_64 에뮬레이션)</text>
  <text x="190" y="270" text-anchor="end" font-size="12">캐시 없음</text>
  <rect x="200" y="256" width="387.1" height="18" fill="#888" fill-opacity="0.6" stroke="#888"/>
  <text x="593.1" y="270" font-size="12">88.47 s</text>
  <text x="190" y="294" text-anchor="end" font-size="12">ccache cold</text>
  <rect x="200" y="280" width="400.0" height="18" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="606.0" y="294" font-size="12">91.43 s</text>
  <text x="190" y="318" text-anchor="end" font-size="12">ccache warm</text>
  <rect x="200" y="304" width="2" height="18" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <text x="208" y="318" font-size="12">0.04 s (약 2,200배)</text>
  <line x1="200" y1="334" x2="638" y2="334" stroke="currentColor"/>
  <line x1="200.0" y1="334" x2="200.0" y2="339" stroke="currentColor"/>
  <text x="200.0" y="354" text-anchor="middle" font-size="12">0</text>
  <line x1="287.5" y1="334" x2="287.5" y2="339" stroke="currentColor"/>
  <text x="287.5" y="354" text-anchor="middle" font-size="12">20</text>
  <line x1="375.0" y1="334" x2="375.0" y2="339" stroke="currentColor"/>
  <text x="375.0" y="354" text-anchor="middle" font-size="12">40</text>
  <line x1="462.5" y1="334" x2="462.5" y2="339" stroke="currentColor"/>
  <text x="462.5" y="354" text-anchor="middle" font-size="12">60</text>
  <line x1="550.0" y1="334" x2="550.0" y2="339" stroke="currentColor"/>
  <text x="550.0" y="354" text-anchor="middle" font-size="12">80</text>
  <line x1="637.5" y1="334" x2="637.5" y2="339" stroke="currentColor"/>
  <text x="625" y="354" text-anchor="middle" font-size="12">100 s</text>
</svg>
```
그림 6 — ccache 측정 결과(툴체인마다 1회 측정, 막대 길이는 Python으로 비례 계산). warm 빌드는 세 툴체인 모두 0.03~0.04 s로, 막대가 보이지 않을 정도다.

출력에서 볼 것:

- **cold는 캐시 없음보다 약간 느리다**(8.05 → 8.22 s, 88.47 → 91.43 s). 첫 빌드에서는 해시 계산·캐시 저장 비용만 더해진다. ccache의 이득은 오직 "두 번째부터"다.
- **warm은 0.04 s.** 26개 파일 모두 적중(Hits 26)해서 컴파일러를 한 번도 띄우지 않았다. 브랜치를 오가거나 `make clean`이 잦은 개발 루프, CI의 반복 빌드에서 차이가 크다.
- 같은 gcc 계열인데 **에뮬레이션 툴체인은 네이티브보다 약 11배 느렸다**(88.47 / 8.05). 직접 빌드한 ct-ng gcc 16.2(네이티브)는 8.74 s로 Ubuntu gcc 15.2와 비슷했다. x86 전용 툴체인을 arm64 호스트에서 쓰면 이 비용을 낸다. ccache가 이 비용을 가리는 데 특히 효과적이지만, 근본 해결은 호스트 arch에 맞는 툴체인(Arm GNU의 aarch64 호스트 빌드, §5처럼 직접 빌드한 ct-ng 툴체인, 또는 x86 빌드 서버)을 쓰는 것이다.
- 커널 빌드에도 그대로 쓸 수 있다: `make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- CC="ccache aarch64-linux-gnu-gcc"`.

### 7.4 팀에서 쓰는 재현 가능한 툴체인 세팅 — 실전 패턴

1. 툴체인 tarball을 **내부 아티팩트 저장소**(Artifactory, S3 등)에 올리고 URL + sha256을 저장소의 `toolchain.lock` 같은 파일에 기록한다. 외부 사이트가 사라져도 빌드가 된다.
2. 빌드는 **Docker 이미지 안에서** 한다. Dockerfile에서 툴체인을 받아 sha256 검증 후 `/opt/toolchains/`에 풀고, 이미지는 digest(`@sha256:...`)로 고정한다. 개발자 노트북(Apple Silicon 포함)과 CI가 같은 환경을 쓴다.
3. Yocto 팀은 SDK 설치 스크립트 자체를 아티팩트로 관리하고, 이미지 릴리스 버전과 SDK 버전을 짝지어 태깅한다(`v2.3.0-image` ↔ `v2.3.0-sdk`).
4. 컴파일러 업그레이드는 **별도 PR**로, 전체 테스트 + 바이너리 크기·성능 비교 후 머지한다. Don의 펌웨어 팀 문화와 똑같다.

---

## 8. 커널 안에서는 무슨 일이 일어나나

### 8.1 커널 헤더는 어디서 오나 — `make headers_install`

툴체인 sysroot의 `usr/include/linux/*.h`, `usr/include/asm/*.h`는 커널 소스의 `include/uapi/`와 `arch/<arch>/include/uapi/`에서 온다. 커널 빌드 시스템의 `headers_install` 타깃이 커널 내부 전용 부분을 걷어 내고(`__KERNEL__` 블록 제거 등) 유저용 헤더만 설치한다. crosstool-NG·Buildroot·Yocto 모두 툴체인을 만들 때 이 명령을 부른다.

무엇을 확인하나: §5의 crosstool-NG 빌드를 준비하면서 손질한 커널 6.1 소스에서 `headers_install`이 동작하는지, 결과 `version.h`의 값.

```sh
limactl shell elinux -- bash -lc 'cd /tmp/B5/trim/linux-6.1.175 && make -s ARCH=arm INSTALL_HDR_PATH=/tmp/B5/trim/hdrtest headers_install 2>&1 | tail -5; echo rc=$?; ls /tmp/B5/trim/hdrtest/include | head; grep LINUX_VERSION_CODE /tmp/B5/trim/hdrtest/include/linux/version.h'
```

```text
rc=0
asm
asm-generic
drm
linux
misc
mtd
rdma
scsi
sound
video
#define LINUX_VERSION_CODE 393647
```

출력에서 볼 것: `393647 = 6 × 65536 + 1 × 256 + 175` → 6.1.175. `drivers/`, `fs/`, `net/` 같은 커널 본체를 빼고도 헤더 설치가 된다(이 손질 버전은 §5에서 쓴다). `drm`, `mtd`, `scsi`, `sound`, `video` 같은 디렉터리는 **드라이버가 유저에게 공개하는 ioctl 인터페이스**다. Don이 나중에 드라이버에 ioctl을 추가하면(G5), 그 구조체 정의는 `include/uapi/linux/<내 드라이버>.h`에 두고, 그것이 이 경로를 따라 툴체인 sysroot와 앱으로 흘러간다.

### 8.2 모르는 syscall은 어떻게 `ENOSYS`가 되나

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="15" y="20" width="190" height="46" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="110" y="40" text-anchor="middle" font-size="12">include/uapi/asm-generic</text>
  <text x="110" y="57" text-anchor="middle" font-size="12">unistd.h, syscall.tbl</text>
  <line x1="205" y1="43" x2="243" y2="43" stroke="currentColor" marker-end="url(#ah7)"/>
  <rect x="245" y="20" width="190" height="46" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="40" text-anchor="middle" font-size="12">headers_install</text>
  <text x="340" y="57" text-anchor="middle" font-size="12">→ sysroot/usr/include</text>
  <line x1="435" y1="43" x2="473" y2="43" stroke="currentColor" marker-end="url(#ah7)"/>
  <rect x="475" y="20" width="190" height="46" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="570" y="40" text-anchor="middle" font-size="12">glibc 헤더·앱이 __NR_x 사용</text>
  <text x="570" y="57" text-anchor="middle" font-size="12">(musl은 자체 번호표)</text>
  <text x="340" y="95" text-anchor="middle" font-size="13" font-weight="bold">빌드 때 ↑ / 실행 때 ↓</text>
  <line x1="15" y1="105" x2="665" y2="105" stroke="currentColor" stroke-dasharray="6 4"/>
  <rect x="15" y="125" width="150" height="46" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="90" y="145" text-anchor="middle" font-size="12">앱: syscall(451)</text>
  <text x="90" y="162" text-anchor="middle" font-size="12">svc #0, r7=451</text>
  <line x1="165" y1="148" x2="198" y2="148" stroke="currentColor" marker-end="url(#ah7)"/>
  <rect x="200" y="125" width="200" height="46" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="300" y="145" text-anchor="middle" font-size="12">invoke_syscall()</text>
  <text x="300" y="162" text-anchor="middle" font-size="12">nr &lt; NR_syscalls ?</text>
  <line x1="400" y1="140" x2="448" y2="140" stroke="currentColor" marker-end="url(#ah7)"/>
  <text x="424" y="132" text-anchor="middle" font-size="12">예</text>
  <rect x="450" y="118" width="215" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="557" y="143" text-anchor="middle" font-size="12">sys_call_table[nr] 호출</text>
  <line x1="300" y1="171" x2="300" y2="208" stroke="currentColor" marker-end="url(#ah7)"/>
  <text x="314" y="194" font-size="12">아니오, 또는 표 칸이 비어 있음</text>
  <rect x="200" y="210" width="200" height="46" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="300" y="230" text-anchor="middle" font-size="12">sys_ni_syscall()</text>
  <text x="300" y="247" text-anchor="middle" font-size="12">return -ENOSYS</text>
  <line x1="557" y1="158" x2="557" y2="208" stroke="currentColor" marker-end="url(#ah7)"/>
  <rect x="450" y="210" width="215" height="46" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="557" y="230" text-anchor="middle" font-size="12">기능 있음: 인자 검사</text>
  <text x="557" y="247" text-anchor="middle" font-size="12">-EFAULT, -EINVAL, 결과</text>
  <text x="340" y="285" text-anchor="middle" font-size="12">ENOSYS = "이 커널은 그 번호를 모른다" → 앱은 옛 방식으로 fallback</text>
</svg>
```
그림 7 — 커널 헤더가 빌드 때 syscall 번호를 정하고, 실행 때 커널이 그 번호를 아는지에 따라 실제 함수 또는 `sys_ni_syscall`(`-ENOSYS`)로 간다. arm64 커널에서는 `arch/arm64/kernel/syscall.c`의 `invoke_syscall()`이 이 분기를 한다(32-bit ARM 커널은 `arch/arm/kernel/entry-common.S`의 테이블 점프).

연결 고리:

- 커널 설정에서 기능을 끈 syscall(`CONFIG_` 옵션으로 제외된 것)도 테이블 칸이 `sys_ni_syscall`로 채워진다. 그래서 **같은 커널 버전이라도 `.config`에 따라 `ENOSYS`가 날 수 있다.** "헤더 ≤ 커널" 규칙 + "그 기능이 `.config`에 켜져 있나"까지 봐야 한다(E 모듈).
- §4.3에서 본 `EFAULT`는 커널이 syscall을 알고 `copy_from_user()`로 인자를 읽다 실패한 것이다(A1, E6).

### 8.3 커널과 모듈은 "같은 컴파일러"로

유저 앱은 libc 계약만 맞으면 다른 gcc로 빌드해도 된다. 하지만 **커널 모듈(`.ko`)은 커널을 빌드한 것과 같은 툴체인**으로 빌드하는 것이 원칙이다.

- 커널 `.config`에는 `CONFIG_CC_VERSION_TEXT`(커널을 빌드한 컴파일러 문자열)가 기록된다. 외부 모듈을 빌드할 때 Kbuild는 지금 컴파일러가 그것과 다르면 경고를 낸다.
- gcc 버전에 따라 켜지는 커널 기능(스택 보호, 구조체 레이아웃 랜덤화 플러그인, `-fzero-call-used-regs` 같은 하드닝 옵션)이 달라서, 다른 컴파일러로 만든 모듈은 ABI가 미묘하게 어긋날 수 있다.
- 모듈에는 `vermagic` 문자열(커널 버전, SMP, preempt, `modversions` 등)이 박히고, 맞지 않으면 `insmod`가 거부한다(G1, E 모듈).

그래서 벤더 BSP가 "커널은 이 툴체인으로 빌드하라"고 지정하고(§2.7), Yocto는 커널·모듈·앱을 같은 툴체인으로 빌드한다. Don이 out-of-tree 드라이버를 들고 다른 팀 보드에 올릴 때 첫 질문은 **"이 커널을 어떤 툴체인으로 빌드했나요? 커널 빌드 트리(`/lib/modules/$(uname -r)/build`)는 어디 있나요?"**가 된다.

---

## 9. Embedded 실무에서 다시 보기

### 9.1 Jetson(Ubuntu rootfs) 팀 — "PC에서 빌드한 앱이 보드에서 안 돌아요"

예를 들어 센서 키트 장비가 Ubuntu 22.04 기반 L4T(glibc 2.35)를 쓰는데, 신입이 Ubuntu 24.04 노트북에서 `aarch64-linux-gnu-gcc`(glibc 2.39)로 빌드해서 올렸다. 결과는 §4.2와 같은 `GLIBC_2.38 not found` 류의 에러다. 해결 순서:

1. 보드에서 `ldd --version` 또는 `/lib/aarch64-linux-gnu/libc.so.6` 실행으로 glibc 버전 확인
2. 빌드는 **보드와 같은 Ubuntu 버전 컨테이너**(`ubuntu:22.04` + `gcc-aarch64-linux-gnu`)에서 하거나, 보드 rootfs를 sysroot로 복사해 `--sysroot`로 빌드
3. 장기적으로는 CI의 빌드 이미지를 보드 이미지 버전과 짝지어 관리

### 9.2 i.MX8 + Yocto 팀 — SDK 버전과 이미지 버전 짝 맞추기

이미지에 새 라이브러리(예: `libgpiod` v2)를 추가했는데 앱 팀이 예전 SDK를 쓰고 있으면 `pkg-config`가 `libgpiod`를 못 찾거나 v1 헤더로 빌드된다. 증상은 "보드에서는 v2인데 앱은 v1 API로 컴파일됨 → 링크 또는 실행 에러". 이미지 릴리스 때 `populate_sdk`를 같이 돌려 SDK를 함께 배포하고, 앱 CI는 이미지 버전에 맞는 SDK를 받는다.

### 9.3 작은 진단 도구는 musl static으로

필드 엔지니어가 쓰는 센서 진단 도구(I2C 레지스터 덤프, CAN 프레임 캡처 등)는 여러 세대 보드에 같은 바이너리로 올라가야 한다. §4.2에서 본 것처럼 **musl static 바이너리(40 KB급)**는 rootfs의 libc와 무관하게 돈다. Bootlin `musl--stable` 툴체인이나 ct-ng musl 툴체인을 이 용도로 따로 두는 팀이 많다. 단 `-mcpu`는 가장 오래된 보드 CPU에 맞춘다.

### 9.4 Apple Silicon 노트북 개발자

x86_64 전용 툴체인(Bootlin, 일부 벤더 SDK)은 arm64 호스트에서 그대로 안 돈다(§2.3). 선택지:

| 방법 | 속도 | 비고 |
|---|---|---|
| x86 원격 빌드 서버 / CI | 빠름 | 가장 흔함 |
| arm64 Linux VM + aarch64 호스트용 툴체인 (Arm GNU, 배포판 패키지, 직접 빌드) | 빠름 | 이 노트의 VM 상황 |
| arm64 VM + qemu-user로 x86 툴체인 실행 | 약 11배 느림 (§7.3 실측) | 임시방편, ccache 필수 |
| x86 Docker 이미지 에뮬레이션 실행 | 느림 | 설정은 쉬움 |


---

## 10. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 최신 Ubuntu 호스트의 크로스 gcc로 빌드해 오래된 rootfs에 올림 | `version 'GLIBC_2.34' not found` | 빌드 sysroot의 glibc가 타깃보다 새로움 | 타깃과 같은 glibc의 sysroot(Yocto/벤더 SDK, 같은 버전 컨테이너)로 빌드, 또는 musl static |
| hard-float 툴체인 바이너리를 soft-float rootfs에 (또는 반대) | `-sh: ./app: not found` (파일은 있음) | `PT_INTERP` 로더(`ld-linux-armhf.so.3` 등)가 rootfs에 없음 | `readelf -l`로 interpreter 확인, 툴체인 float ABI를 rootfs와 맞춤 |
| musl 바이너리를 glibc rootfs에 동적 링크로 올림 | `not found` 또는 심볼 에러 | libc 종류가 다름 | 같은 libc 툴체인 사용 또는 `-static` |
| Makefile에 `CC=gcc`를 하드코딩 | SDK 환경에서도 호스트용 바이너리가 나옴, `file`로 보면 aarch64/x86-64 | 환경의 `CC`(sysroot·`-mcpu` 포함)를 덮어씀 | `CC ?=` 또는 `$(CC)`만 사용 |
| `PKG_CONFIG_LIBDIR`/`SYSROOT_DIR` 미설정 | 링크 때 `file in wrong format`, `skipping incompatible` | 호스트용 `.pc`의 `-L/usr/lib/...`가 섞임 | SDK 스크립트처럼 pkg-config 변수 3종 설정 |
| 툴체인 기본 `-mcpu`/`-march`를 확인 안 함 | 일부 보드에서만 `Illegal instruction` | 기본 CPU가 타깃보다 높음 (또는 NEON 가정) | `gcc -Q --help=target`로 확인, `-mcpu`를 가장 낮은 보드에 맞춤 |
| 헤더가 타깃 커널보다 새로운 툴체인으로 새 syscall 사용 | 런타임 `ENOSYS`, 새 ioctl은 `EINVAL`/`ENOTTY` | 커널이 그 기능을 모름 | 헤더 ≤ 커널 툴체인 선택, `ENOSYS` fallback 코드 |
| 커널과 다른 gcc로 out-of-tree 모듈 빌드 | Kbuild 경고, `insmod` 실패(`Invalid module format`), 드물게 오동작 | 컴파일러·플래그·vermagic 불일치 | 커널을 빌드한 툴체인과 커널 빌드 트리 사용 |
| 툴체인 tarball을 체크섬 없이 받음, 버전을 문서에만 적음 | 6개월 뒤 같은 바이너리를 못 만듦 | 다운로드 소스 변경·소실 | URL + sha256 고정, 내부 미러, 컨테이너 digest 고정 |
| x86_64 전용 툴체인을 arm64 호스트에서 실행 | `cannot execute binary file: Exec format error` | 호스트 arch 불일치 | aarch64 호스트용 툴체인, x86 빌드 서버, 또는 qemu-user(느림) |

---

## 11. 면접에서 이렇게 말한다

**Q.** "How did your team obtain the cross toolchain, and why that one?"

**A.** 툴체인은 rootfs를 만드는 쪽을 따라간다. Yocto로 이미지를 만들면 같은 빌드에서 `populate_sdk`로 SDK를 뽑아 쓰고, 그러면 sysroot의 glibc·라이브러리·헤더가 이미지와 정확히 같다. Ubuntu 기반 보드면 같은 Ubuntu 버전의 크로스 패키지나 컨테이너를 쓰고, 커널은 벤더가 지정한 툴체인으로 빌드한다. 체크섬과 컨테이너로 고정한다.

> We pick the toolchain based on who builds the root filesystem. With Yocto we generate the SDK from the same build with populate_sdk, so the sysroot's glibc, libraries and headers exactly match the image. For Ubuntu-based boards we build in a container of the same Ubuntu release, and the kernel is built with the vendor-specified toolchain. Everything is pinned by version and checksum.

**꼬리 질문**: "What if you need a different GCC version than the SDK provides?" → crosstool-NG나 Buildroot로 같은 libc 버전·커널 헤더를 지정해 빌드하고, glibc는 타깃보다 새롭지 않게 맞춘다.

**Q.** "A binary built on a developer laptop fails on the board with 'GLIBC_2.34 not found'. Explain."

**A.** glibc 심볼 버전 때문이다. 바이너리가 링크될 때 빌드 sysroot의 glibc에 있는 `__libc_start_main@GLIBC_2.34` 같은 버전 태그가 기록되는데, 보드의 glibc가 더 오래돼서 그 태그가 없다. glibc는 하위 호환만 보장하므로 타깃과 같거나 오래된 glibc로 빌드해야 한다. `objdump -T`로 요구 버전을 확인하고 SDK sysroot로 다시 빌드하거나, 도구성 바이너리면 musl static으로 만든다.

> glibc uses symbol versioning. At link time the binary records versioned symbols from the build sysroot, like __libc_start_main@GLIBC_2.34, and the board's older glibc doesn't provide that version. glibc is only backward compatible, so you must build against a sysroot whose glibc is the same or older than the target's. I check the required versions with objdump -T and rebuild with the SDK sysroot, or ship a static musl binary for small tools.

**Q.** "What's in a cross toolchain besides the compiler?"

**A.** binutils(어셈블러·링커), gcc 본체(`cc1`)와 `libgcc`, 타깃 C 라이브러리와 동적 로더, 커널 UAPI 헤더, 그리고 그것들을 담은 sysroot다. 호스트 쪽에는 gmp/mpfr/mpc 같은 gcc 의존 라이브러리가 있다. 타깃으로 가는 것은 sysroot 부분뿐이고, 그것이 rootfs와 맞아야 한다.

> Binutils for assembling and linking, the GCC driver and cc1 plus libgcc, the target C library with its dynamic loader, the kernel UAPI headers, all arranged in a sysroot, plus host-side libraries like GMP and MPFR that GCC itself needs. Only the sysroot content relates to the target, and it has to match the root filesystem.

**Q.** "Why do kernel headers in the toolchain need to be older than or equal to the target kernel?"

**A.** 헤더가 syscall 번호, ioctl 번호, 구조체 레이아웃을 정한다. 커널은 유저 ABI를 깨지 않고 추가만 하므로, 옛 헤더로 빌드한 앱은 새 커널에서 잘 돈다. 반대로 새 헤더로 새 syscall을 쓰면 옛 커널은 그 번호를 몰라 `ENOSYS`를 준다. 그래서 Bootlin stable은 일부러 5.10 헤더를 쓴다. 단 musl은 syscall 번호표를 자체적으로 들고 있어서 헤더 버전만 보고 판단하면 안 된다.

> The headers define syscall numbers, ioctl numbers and struct layouts. Linux never breaks the user ABI, it only adds, so an app built with older headers runs fine on a newer kernel. The reverse fails: a new syscall from newer headers returns ENOSYS on an older kernel. That's why prebuilt stable toolchains often ship old headers. One caveat: musl carries its own syscall number table, so header version alone doesn't tell you what's callable.

**Q.** "What does the Yocto SDK environment-setup script actually do?"

**A.** 환경 변수만 export한다. `PATH`에 크로스 도구를 넣고, `CC`에 `--sysroot`와 CPU 튜닝 옵션까지 넣고, `CFLAGS`/`LDFLAGS`, pkg-config가 sysroot를 보도록 `PKG_CONFIG_SYSROOT_DIR`/`PKG_CONFIG_PATH`, 커널용 `ARCH`/`CROSS_COMPILE`, autotools용 `CONFIGURE_FLAGS`, CMake용 툴체인 설정을 준다. 그래서 Makefile은 `$(CC)`를 그대로 써야 하고 덮어쓰면 안 된다.

> It just exports environment variables: PATH to the cross tools, CC including --sysroot and CPU tuning flags, CFLAGS and LDFLAGS, PKG_CONFIG_SYSROOT_DIR and PKG_CONFIG_PATH so pkg-config resolves inside the sysroot, ARCH and CROSS_COMPILE for kernel builds, CONFIGURE_FLAGS for autotools, and a CMake toolchain configuration. That's why Makefiles must use $(CC) rather than hardcoding gcc.

**Q.** "When would you use crosstool-NG instead of a prebuilt toolchain?"

**A.** 미리 빌드된 것으로 안 되는 조합이 필요할 때다. 예를 들어 오래된 커널에 맞춘 낮은 헤더 버전, 특정 glibc 버전, 특이 CPU 튜닝이나 아키텍처, 사내 패치가 들어간 gcc, 또는 aarch64 호스트용 툴체인이 필요할 때. 대신 툴체인 자체가 우리 산출물이 되어 유지보수 책임이 생긴다. 이미지를 Buildroot로 만든다면 Buildroot internal 툴체인이 더 자연스럽다.

> When no prebuilt toolchain has the combination we need: specific kernel header or glibc versions to match an old target, unusual CPU tuning or architecture, patched GCC, or a different host architecture. The cost is that the toolchain becomes our own artifact to maintain. If the image is built with Buildroot, its internal toolchain is usually the more natural choice.

**Q.** "How do you make builds reproducible across the team?"

**A.** 툴체인을 URL + sha256으로 고정하고 내부 미러에 둔다. 빌드는 digest로 고정한 컨테이너 안에서 한다. `-ffile-prefix-map`으로 빌드 경로를 바이너리에서 지운다. 컴파일러 업그레이드는 별도 변경으로 검증한다. ccache는 속도용이지만 키에 컴파일러 해시가 들어가 안전하다.

> Pin the toolchain by URL and SHA-256 and mirror it internally, build inside a container pinned by digest, strip build paths with -ffile-prefix-map so outputs are bit-identical, and treat compiler upgrades as a separate, fully tested change. ccache speeds things up safely because the compiler's own hash is part of the cache key.

---

## 12. 직접 해보기

1. **(개념)** Bootlin `aarch64--glibc--stable-2026.08-1`의 커널 헤더는 5.10, glibc는 2.44다. 이 툴체인으로 빌드한 동적 바이너리를 Ubuntu 22.04(glibc 2.35, 커널 5.15) 보드에 올리면 돌까? 커널 헤더와 glibc 각각에 대해 답하라.
   정답/힌트: 커널 헤더 5.10 ≤ 5.15라 커널 쪽은 안전. 하지만 glibc 2.44 > 2.35라서 `GLIBC_2.3x not found`가 날 가능성이 높다. 동적 링크라면 이 툴체인은 부적합하다.
2. **(관찰)** §3.2의 `inspect_tc.sh`를 VM의 `aarch64-linux-gnu-gcc`에 돌려 보고, 왜 sysroot가 비어 있고 커널 헤더가 `/usr/include`에서 오는지 설명하라.
   정답/힌트: arm64 VM에서 이 이름은 사실상 네이티브 gcc다. multiarch 경로(`/usr/include/aarch64-linux-gnu`)를 쓰고 sysroot는 `/`다.
3. **(코드)** §4.3의 `newsys.c`에 `__NR_mseal`(Linux 6.10)을 추가하고, Bootlin musl 툴체인과 Ubuntu 툴체인에서 각각 정의되어 있는지, qemu-arm에서 어떤 errno가 나오는지 확인하라.
   정답/힌트: musl은 자체 번호표를 쓰므로 musl 버전에 따라 다르다. `#ifdef`로 감싸고, 실행 결과가 `ENOSYS`인지 다른 errno인지로 "실행 환경이 아는가"를 판단한다.
4. **(코드)** §6의 `env-setup-armv7-musl`을 `source`한 셸에서 Makefile의 첫 줄에 `CC = gcc`를 추가하고 빌드해 보라. `file`로 결과를 확인하고 무엇이 잘못됐는지 설명하라.
   정답/힌트: 호스트(aarch64)용 오브젝트가 나오거나, sysroot의 `imu.h`는 찾아도 링크에서 ARM `libimu.so`와 형식이 안 맞아 실패한다.
5. **(관찰)** `readelf -l`로 §5에서 만든 crosstool-NG 툴체인 바이너리와 Bootlin 바이너리의 interpreter를 비교하고, 둘을 서로의 sysroot로 `qemu-arm -L` 실행해 보라.
   정답/힌트: soft-float(`ld-musl-arm.so.1`)와 hard-float(`ld-musl-armhf.so.1`)는 로더 이름부터 다르다. 상대 sysroot에는 그 로더가 없어 실행되지 않는다.
6. **(측정)** §7.3의 벤치마크에서 warm 빌드 직전에 `CFLAGS`에 `-DFOO=1`을 추가하면 적중률이 어떻게 되는지, 헤더 하나(`zstd.h`)를 `touch`만 하면 어떻게 되는지 측정하라.
   정답/힌트: 옵션이 바뀌면 키가 바뀌어 전부 miss. `touch`만 하면 내용 해시가 같으므로 hit(ccache는 mtime이 아니라 내용을 본다. 단 direct mode의 동작 세부는 설정에 따라 다르다).
7. **(설계)** 팀에 i.MX8M Plus(Yocto)와 Jetson Orin(Ubuntu 22.04 L4T) 두 제품이 있다. 공통 센서 라이브러리를 두 제품용으로 빌드하는 CI를 설계하라. 각각 어떤 툴체인·sysroot를 쓰고 무엇을 고정할지 쓴다.
   정답/힌트: i.MX는 이미지 버전에 짝지은 Yocto SDK, Jetson은 `ubuntu:22.04` 기반 컨테이너 + aarch64 크로스 패키지(또는 L4T sysroot). 둘 다 아티팩트 sha256과 컨테이너 digest 고정.

---

## 13. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| toolchain | 툴체인 | binutils + gcc + libc + 커널 헤더 + sysroot 묶음 |
| triplet / tuple | 타깃 이름 | `arch-vendor-os-abi`, 예: `arm-buildroot-linux-musleabihf` |
| sysroot | 타깃 루트 사본 | 크로스 컴파일 때 헤더·라이브러리를 찾는 "타깃의 `/`" |
| UAPI headers | 유저 API 헤더 | 커널이 유저에게 공개하는 syscall·ioctl·구조체 정의 (`headers_install` 결과) |
| symbol versioning | 심볼 버전 | glibc 함수에 `GLIBC_2.34` 같은 버전 태그를 붙여 호환성 관리 |
| PT_INTERP | 로더 경로 | ELF가 지정하는 동적 로더 경로, 없으면 `not found` |
| float ABI | 부동소수 호출 규약 | soft / softfp / hard, hard와 soft는 섞을 수 없음 |
| Arm GNU Toolchain | Arm 배포 GCC | developer.arm.com, 옛 Linaro 바이너리 툴체인의 후계 |
| Bootlin toolchains | 미리 빌드된 툴체인 | toolchains.bootlin.com, Buildroot로 빌드, libc·안정도 선택 |
| crosstool-NG | 툴체인 빌더 | `ct-ng menuconfig` → `ct-ng build`로 툴체인을 소스에서 생성 |
| internal / external toolchain | Buildroot 툴체인 방식 | Buildroot가 직접 빌드 vs 미리 빌드된 것을 가져옴 |
| populate_sdk | Yocto SDK 생성 태스크 | 이미지와 같은 sysroot를 담은 SDK 설치 스크립트 생성 |
| `environment-setup-*` | SDK 환경 스크립트 | `CC`, `CFLAGS`, `PKG_CONFIG_*` 등을 export |
| toolchain-wrapper | Buildroot 래퍼 | `--sysroot`와 기본 플래그를 붙여 진짜 gcc를 호출 |
| relocatable | 위치 독립 | 어느 디렉터리에 풀어도 동작하는 툴체인 |
| ccache | 컴파일 캐시 | 입력·옵션·컴파일러 해시로 오브젝트를 캐시 |
| -ffile-prefix-map | 경로 치환 옵션 | 디버그 정보 등의 빌드 경로를 바꿔 재현 가능한 출력 생성 |
| ENOSYS | errno 38 | "이 syscall을 모른다" — 커널의 `sys_ni_syscall` 반환값 |
| vermagic | 모듈 버전 문자열 | 커널 버전·설정 정보, 맞지 않으면 모듈 로드 거부 |

---

## 14. 요약 & 체크리스트

Embedded Linux 툴체인은 컴파일러 하나가 아니라 **타깃 rootfs와 맺는 계약 묶음**(libc 버전, 커널 헤더, float ABI, 기본 CPU)이다. 그래서 "어떤 툴체인을 쓰나"는 "rootfs를 누가 만드나"로 결정된다. Yocto·Buildroot·벤더 SDK처럼 rootfs와 같은 빌드에서 나온 툴체인은 계약이 자동으로 맞고, 배포판·Arm GNU·Bootlin·crosstool-NG는 사람이 맞춰야 한다. 맞출 때의 규칙은 "빌드 때 본 것 ≤ 실행 때 있는 것, 종류는 같게"다. 처음 보는 툴체인은 `-dumpmachine`, `-v`, `-Q --help=target`, `-print-sysroot`, sysroot의 `version.h`와 libc 실행으로 5분 안에 신원을 조회한다. 팀 환경은 URL + 체크섬 + 컨테이너 + `-ffile-prefix-map`으로 고정하고, ccache로 반복 빌드를 줄인다.

- [ ] 툴체인을 얻는 7가지 길을 나열하고, 각각 rootfs와 일치가 보장되는지 설명할 수 있다
- [ ] 툴체인의 5개 구성 요소와 sysroot가 각각 호스트용인지 타깃용인지 구분할 수 있다
- [ ] triplet의 vendor 칸이 ABI와 무관하다는 것과, float ABI를 직접 확인하는 방법을 말할 수 있다
- [ ] 처음 보는 툴체인의 gcc·libc·커널 헤더 버전과 기본 `-mcpu`/`-mfpu`를 명령어로 알아낼 수 있다
- [ ] `GLIBC_2.xx not found`의 원인(심볼 버전)과 해결책 세 가지를 설명할 수 있다
- [ ] "커널 헤더 ≤ 타깃 커널" 규칙과 `ENOSYS`의 의미, musl의 예외를 설명할 수 있다
- [ ] `-sh: ./app: not found`를 보고 `readelf -l`로 로더를 확인할 수 있다
- [ ] Yocto `environment-setup-*`가 export하는 주요 변수와 그것을 읽는 도구를 짝지을 수 있다
- [ ] 손으로 만든 환경 스크립트로 Make·CMake 프로젝트를 크로스 빌드하고 pkg-config가 sysroot를 보게 할 수 있다
- [ ] crosstool-NG로 샘플을 고르고 설정을 바꿔 빌드하는 절차와 빌드 단계를 설명할 수 있다
- [ ] 툴체인 재현성을 위해 고정해야 할 것(체크섬, 컨테이너, 빌드 경로)을 말할 수 있다
- [ ] 커널 모듈은 커널과 같은 툴체인으로 빌드해야 하는 이유를 설명할 수 있다

---

## 참고 자료

- toolchains.bootlin.com — Bootlin 미리 빌드된 툴체인 카탈로그와 각 tarball의 `README.txt`, `summary.csv`
- [Arm GNU Toolchain 다운로드](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) — 릴리스별 호스트·타깃 조합과 체크섬
- [crosstool-NG 문서](https://crosstool-ng.github.io/docs/) — 설치, `ct-ng` 명령, known issues
- [Buildroot 매뉴얼](https://buildroot.org/downloads/manual/manual.html) — 툴체인 장(internal/external toolchain, `make sdk`)
- [Yocto Project SDK 매뉴얼](https://docs.yoctoproject.org/sdk-manual/) — `populate_sdk`, 설치, `environment-setup` 스크립트
- [docs.kernel.org — Exporting kernel headers for use by userspace](https://docs.kernel.org/kbuild/headers_install.html) — `make headers_install`
- [man7.org — ld.so(8)](https://man7.org/linux/man-pages/man8/ld.so.8.html) — 동적 로더와 `PT_INTERP`
- [man7.org — syscall(2)](https://man7.org/linux/man-pages/man2/syscall.2.html) — 번호로 syscall 부르기, 아키텍처별 호출 규약
- [ccache 매뉴얼](https://ccache.dev/manual/latest.html) — 캐시 키, direct mode, `base_dir`
- [reproducible-builds.org](https://reproducible-builds.org/docs/) — `-ffile-prefix-map`, `SOURCE_DATE_EPOCH`
- Michael Kerrisk, "The Linux Programming Interface" — 41장(공유 라이브러리 기초), 42장(고급 공유 라이브러리: 심볼 버전)
- Bootlin "Embedded Linux system development" 교육 슬라이드 — 크로스 툴체인 장
- elixir.bootlin.com — `arch/arm64/kernel/syscall.c`(`invoke_syscall`), `kernel/sys_ni.c`(`sys_ni_syscall`)
