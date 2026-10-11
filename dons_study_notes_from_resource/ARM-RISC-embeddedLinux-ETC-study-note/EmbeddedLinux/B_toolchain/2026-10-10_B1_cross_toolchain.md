# B1. 크로스 컴파일 툴체인 — triplet·sysroot·binutils·ABI, "내 PC에서 보드용 바이너리를 만드는 원리"

> **이 노트를 다 읽으면**: `arm-linux-gnueabihf` 같은 triplet을 한 글자씩 해독할 수 있다 · `gcc` 한 번이 cc1·as·collect2/ld와 sysroot의 어떤 파일을 쓰는지 직접 꺼내 볼 수 있다 · 크로스 바이너리를 qemu-user로 돌리고 "Exec format error / not found / Illegal instruction"을 원인별로 가를 수 있다 · `arm-none-eabi`(Don의 bare-metal 세계)와 `arm-linux-gnueabihf`의 차이를 스택 단위로 설명할 수 있다
> **왜 필요한가**: 입사 첫 주에 받는 일이 "BSP SDK 깔고 앱·커널·모듈 크로스 빌드 환경 세팅"이고, 면접에서는 "What is a sysroot?", "hard-float vs soft-float?", "보드에서 바이너리가 안 돈다, 어떻게 보나?"가 단골이다
> **Don 기준 난이도**: `arm-none-eabi-gcc`, 링커 스크립트, `-mcpu`/`-mfloat-abi`, `objdump`는 이미 손에 익었다 / 그 위에 "OS·libc·동적 로더·커널 헤더"가 얹힌 Linux 툴체인의 구조와 sysroot 개념이 새로 배울 부분
> **선행 노트**: A1 (syscall 경계, ELF 실행), A10 (셸 도구) — B3(ELF와 링킹)와 짝으로 읽으면 좋다

---

## 0. 큰 그림 — 이게 왜 필요한가

Don의 SSD 펌웨어 빌드를 떠올려 보자. x86 리눅스 서버(또는 Windows PC)에서 `arm-none-eabi-gcc`로 Cortex-R 이미지를 만들고, 그 `.elf`를 JTAG으로 보드에 올렸다. **컴파일러가 도는 기계(PC)와 결과물이 도는 기계(SSD 컨트롤러)가 다른 것** — 이것이 크로스 컴파일(cross compilation)이다. Don은 이미 8년 동안 크로스 컴파일을 해 왔다.

Embedded Linux에서도 똑같다. 센서 키트의 i.MX8이나 Jetson에서 직접 `gcc`를 돌릴 수도 있지만(native build), 보드는 느리고 메모리가 작고 rootfs에 컴파일러가 없는 경우가 대부분이다. 그래서 개발 PC에서 **보드용 바이너리**를 만든다. 달라지는 점은 하나다: 결과물이 OS 위에서 돌기 때문에, 툴체인이 **타깃 OS의 C 라이브러리(glibc/musl), 동적 로더, 커널 헤더**까지 함께 들고 있어야 한다. 이 묶음이 **sysroot**(타깃 루트 파일시스템의 개발용 축소판)다.

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="150" y="22" text-anchor="middle" font-size="14" font-weight="bold">빌드 머신 (개발 PC)</text>
  <rect x="20" y="32" width="260" height="270" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="150" y="54" text-anchor="middle" font-size="13">Mac → Lima VM (aarch64)</text>
  <rect x="35" y="66" width="230" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="150" y="91" text-anchor="middle" font-size="12">gcc (native aarch64)</text>
  <rect x="35" y="114" width="230" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="150" y="139" text-anchor="middle" font-size="12">arm-linux-gnueabihf-gcc</text>
  <rect x="35" y="162" width="230" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="150" y="187" text-anchor="middle" font-size="12">riscv64-linux-gnu-gcc</text>
  <rect x="35" y="210" width="230" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="150" y="235" text-anchor="middle" font-size="12">arm-none-eabi-gcc + newlib</text>
  <text x="150" y="284" text-anchor="middle" font-size="12">+ sysroot (libc · 헤더 · crt)</text>
  <line x1="265" y1="86" x2="395" y2="66" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="265" y1="134" x2="395" y2="134" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="265" y1="182" x2="395" y2="202" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="265" y1="230" x2="395" y2="270" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="330" y="112" text-anchor="middle" font-size="12">ELF 복사</text>
  <rect x="400" y="40" width="260" height="52" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="530" y="62" text-anchor="middle" font-size="13">i.MX8M · Jetson Orin</text>
  <text x="530" y="81" text-anchor="middle" font-size="12">aarch64 Linux</text>
  <rect x="400" y="108" width="260" height="52" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="530" y="130" text-anchor="middle" font-size="13">AM335x · i.MX6 (Cortex-A)</text>
  <text x="530" y="149" text-anchor="middle" font-size="12">32-bit ARM Linux (armhf)</text>
  <rect x="400" y="176" width="260" height="52" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="530" y="198" text-anchor="middle" font-size="13">RISC-V SoC 보드</text>
  <text x="530" y="217" text-anchor="middle" font-size="12">riscv64 Linux</text>
  <rect x="400" y="244" width="260" height="52" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="530" y="266" text-anchor="middle" font-size="13">모터 MCU (Cortex-M4)</text>
  <text x="530" y="285" text-anchor="middle" font-size="12">OS 없음 (bare-metal)</text>
</svg>
```
그림 1 — 하나의 빌드 머신에 타깃별 툴체인이 여러 벌 깔린다. Linux 타깃용(초록)은 libc·동적 로더·커널 헤더를 담은 sysroot가 필요하고, MCU용(주황)은 newlib만 있으면 된다.

Embedded Linux 엔지니어가 툴체인을 만나는 장면은 이렇다.

- **센서 데몬 빌드**: IMU를 읽는 C 데몬을 PC에서 `aarch64-linux-gnu-gcc`로 빌드해 `scp`로 보드에 올린다. 보드에서 `./imud`가 `not found`로 실패한다 → 동적 로더 경로·glibc 버전 문제 (§5, §12.1).
- **커널·U-Boot 빌드**: `make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-` — 이 두 변수가 무엇인지 모르면 커널 빌드 첫 줄부터 막힌다 (§10, C3, E2).
- **성능 튜닝**: 같은 코드가 Cortex-A53 보드에서 느리다 → `-mcpu`를 맞췄는지, NEON이 쓰였는지 `objdump`로 확인 (§7).
- **이종 코어 SoC**: i.MX8M의 Cortex-A53(Linux)과 Cortex-M4(펌웨어)를 한 프로젝트에서 빌드 → `aarch64-linux-gnu`와 `arm-none-eabi` 두 툴체인을 동시에 쓴다 (§4, O 모듈).

> 실행 환경: 모든 출력은 Lima VM `elinux`(aarch64, `uname -r` = `7.0.0-34-generic`, Ubuntu 26.04)에서 실제로 돌린 결과다. 크로스 툴체인은 Ubuntu 패키지 `gcc-arm-linux-gnueabihf`·`gcc-riscv64-linux-gnu`·`gcc-x86-64-linux-gnu`(GCC 15.2), `gcc-arm-none-eabi`(14.2.rel1) + `libnewlib-arm-none-eabi`, `clang`/`lld` 21.1.8, `qemu-user` 10.2.1. 아래 명령은 VM 안에서 `cd /tmp/B1; S=/Users/donh/.elinux_scratch/B1`(소스 위치)를 한 상태로 친 것이다. 해시·주소·임시 파일 이름은 실행마다 다르다.

---

## 1. 크로스 컴파일이란 — build / host / target

### 1.1 직관 — "컴파일러가 도는 곳"과 "결과물이 도는 곳"

컴파일러 하나에는 기계가 세 개 얽혀 있다. GNU 빌드 시스템(autoconf, GCC의 `configure`)은 이것을 정확한 용어로 부른다.

| 용어 | 뜻 | Don의 SSD FW 예 |
|---|---|---|
| **build** | 컴파일러(또는 프로그램) **자체를 빌드한** 기계 | Arm이 GNU toolchain 패키지를 빌드한 서버 |
| **host** | 그 컴파일러가 **실행되는** 기계 | Don의 x86 리눅스 서버 |
| **target** | 그 컴파일러가 **만든 코드가 실행되는** 기계 | Cortex-R SSD 컨트롤러 |

일반 프로그램(예: 센서 데몬)을 `configure`로 빌드할 때는 target이 의미가 없고 build와 host만 쓴다: `./configure --host=aarch64-linux-gnu`는 "이 프로그램은 aarch64 Linux에서 돌 것이다"라는 뜻이다. target이 의미를 갖는 것은 **컴파일러·binutils·디버거처럼 "코드를 만드는 프로그램"**을 빌드할 때다.

### 1.2 세 가지 조합

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="200" y="20" text-anchor="middle" font-size="13" font-weight="bold">build</text>
  <text x="400" y="20" text-anchor="middle" font-size="13" font-weight="bold">host</text>
  <text x="600" y="20" text-anchor="middle" font-size="13" font-weight="bold">target</text>
  <text x="14" y="62" font-size="13">native</text>
  <rect x="120" y="40" width="160" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="200" y="65" text-anchor="middle" font-size="12">aarch64-linux-gnu</text>
  <rect x="320" y="40" width="160" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="400" y="65" text-anchor="middle" font-size="12">aarch64-linux-gnu</text>
  <rect x="520" y="40" width="150" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="595" y="65" text-anchor="middle" font-size="12">aarch64-linux-gnu</text>
  <line x1="280" y1="60" x2="318" y2="60" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="480" y1="60" x2="518" y2="60" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="400" y="98" text-anchor="middle" font-size="12">예: 이 VM의 gcc — 셋이 모두 같다</text>
  <text x="14" y="142" font-size="13">cross</text>
  <rect x="120" y="120" width="160" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="200" y="145" text-anchor="middle" font-size="12">x86_64-linux-gnu</text>
  <rect x="320" y="120" width="160" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="400" y="145" text-anchor="middle" font-size="12">x86_64-linux-gnu</text>
  <rect x="520" y="120" width="150" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="595" y="145" text-anchor="middle" font-size="12">arm-linux-gnueabihf</text>
  <line x1="280" y1="140" x2="318" y2="140" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="480" y1="140" x2="518" y2="140" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="400" y="178" text-anchor="middle" font-size="12">예: PC에 깐 Ubuntu gcc-arm-linux-gnueabihf — target만 다르다</text>
  <text x="14" y="222" font-size="13">Canadian</text>
  <rect x="120" y="200" width="160" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="200" y="225" text-anchor="middle" font-size="12">x86_64-linux-gnu</text>
  <rect x="320" y="200" width="160" height="40" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="400" y="225" text-anchor="middle" font-size="12">x86_64-w64-mingw32</text>
  <rect x="520" y="200" width="150" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="595" y="225" text-anchor="middle" font-size="12">arm-none-eabi</text>
  <line x1="280" y1="220" x2="318" y2="220" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="480" y1="220" x2="518" y2="220" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="400" y="258" text-anchor="middle" font-size="12">예: Linux 서버에서 "Windows용 arm-none-eabi-gcc.exe"를 빌드</text>
  <text x="340" y="288" text-anchor="middle" font-size="12">화살표 = "이 기계에서 만든 것이 다음 기계에서 돈다"</text>
</svg>
```
그림 2 — native는 셋이 같고, cross는 target만 다르고, Canadian cross는 셋이 모두 다르다.

- **native**: build = host = target. 지금 VM의 `gcc`가 그렇다.
- **cross**: build = host ≠ target. Embedded Linux 개발의 기본형.
- **Canadian cross**: build ≠ host ≠ target. 예를 들어 Arm이 Windows용 `arm-none-eabi-gcc.exe`를 배포하려면, Linux 빌드 서버(build)에서 MinGW 크로스 컴파일러로, Windows(host)에서 돌고, Cortex-M(target)용 코드를 내는 GCC를 빌드해야 한다. 이름은 당시 캐나다에 정당이 셋이었던 데서 왔다는 이야기가 전해진다. 실무에서 직접 할 일은 드물고, crosstool-NG나 Yocto의 `nativesdk`(다른 호스트용 SDK 만들기)에서 이 개념을 만난다 (B5, K).

### 1.3 코드로 확인 — 컴파일러에게 "너는 누구냐" 묻기

무엇을 확인하나: 각 gcc가 스스로 어떤 target용인지(`-dumpmachine`), Debian식 multiarch 이름이 무엇인지, multilib(§9)가 몇 벌인지 본다.

```sh
for c in gcc arm-linux-gnueabihf-gcc riscv64-linux-gnu-gcc x86_64-linux-gnu-gcc arm-none-eabi-gcc; do
  printf "%-26s -dumpmachine=%-22s multiarch=%-22s multilibs=%s\n" $c \
    "$($c -dumpmachine)" "$($c -print-multiarch)" "$($c -print-multi-lib | wc -l)"
done
```

```text
gcc                        -dumpmachine=aarch64-linux-gnu      multiarch=aarch64-linux-gnu      multilibs=1
arm-linux-gnueabihf-gcc    -dumpmachine=arm-linux-gnueabihf    multiarch=arm-linux-gnueabihf    multilibs=1
riscv64-linux-gnu-gcc      -dumpmachine=riscv64-linux-gnu      multiarch=riscv64-linux-gnu      multilibs=1
x86_64-linux-gnu-gcc       -dumpmachine=x86_64-linux-gnu       multiarch=x86_64-linux-gnu       multilibs=3
arm-none-eabi-gcc          -dumpmachine=arm-none-eabi          multiarch=                       multilibs=39
```

출력에서 볼 것: 그냥 `gcc`도 사실 `aarch64-linux-gnu`용 컴파일러다 — native 컴파일러는 "target이 자기 자신인 크로스 컴파일러"일 뿐이다. `arm-none-eabi`는 multiarch가 비어 있고(OS가 없으니 Debian 라이브러리 경로 개념도 없다) multilib이 39벌이나 된다 (§9).

---

## 2. Target triplet 해독

### 2.1 구조 — arch-vendor-os-abi

툴체인 이름 앞에 붙는 `arm-linux-gnueabihf-` 같은 접두어가 **target triplet**이다. 이름은 "세 쌍"이지만 실제로는 네 칸까지 쓴다.

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="22" text-anchor="middle" font-size="14" font-weight="bold">정식형: arch - vendor - os(kernel) - abi(libc+ABI)</text>
  <rect x="40" y="38" width="110" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="95" y="63" text-anchor="middle" font-size="14">arm</text>
  <rect x="170" y="38" width="130" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="235" y="63" text-anchor="middle" font-size="14">unknown</text>
  <rect x="320" y="38" width="110" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="375" y="63" text-anchor="middle" font-size="14">linux</text>
  <rect x="450" y="38" width="190" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="545" y="63" text-anchor="middle" font-size="14">gnueabihf</text>
  <text x="95" y="98" text-anchor="middle" font-size="12">CPU 아키텍처</text>
  <text x="235" y="98" text-anchor="middle" font-size="12">벤더 (흔히 생략)</text>
  <text x="375" y="98" text-anchor="middle" font-size="12">OS 커널</text>
  <text x="545" y="98" text-anchor="middle" font-size="12">libc=GNU, EABI, hard-float</text>
  <line x1="20" y1="114" x2="660" y2="114" stroke="currentColor" stroke-opacity="0.4"/>
  <text x="30" y="136" font-size="12" font-weight="bold">triplet</text>
  <text x="245" y="136" font-size="12" font-weight="bold">arch</text>
  <text x="345" y="136" font-size="12" font-weight="bold">os</text>
  <text x="425" y="136" font-size="12" font-weight="bold">libc / ABI</text>
  <text x="30" y="162" font-size="12">aarch64-linux-gnu</text>
  <text x="245" y="162" font-size="12">AArch64</text>
  <text x="345" y="162" font-size="12">Linux</text>
  <text x="425" y="162" font-size="12">glibc, LP64</text>
  <text x="30" y="188" font-size="12">arm-linux-gnueabihf</text>
  <text x="245" y="188" font-size="12">ARMv7-A</text>
  <text x="345" y="188" font-size="12">Linux</text>
  <text x="425" y="188" font-size="12">glibc, EABI, float를 VFP 레지스터로</text>
  <text x="30" y="214" font-size="12">arm-linux-gnueabi</text>
  <text x="245" y="214" font-size="12">ARMv5T~</text>
  <text x="345" y="214" font-size="12">Linux</text>
  <text x="425" y="214" font-size="12">glibc, EABI, soft-float (armel)</text>
  <text x="30" y="240" font-size="12">arm-none-eabi</text>
  <text x="245" y="240" font-size="12">ARM 전체</text>
  <text x="345" y="240" font-size="12">없음</text>
  <text x="425" y="240" font-size="12">newlib, EABI (bare-metal)</text>
  <text x="30" y="266" font-size="12">riscv64-linux-gnu</text>
  <text x="245" y="266" font-size="12">RV64GC</text>
  <text x="345" y="266" font-size="12">Linux</text>
  <text x="425" y="266" font-size="12">glibc, lp64d</text>
  <text x="30" y="292" font-size="12">x86_64-linux-gnu</text>
  <text x="245" y="292" font-size="12">x86-64</text>
  <text x="345" y="292" font-size="12">Linux</text>
  <text x="425" y="292" font-size="12">glibc, SysV AMD64</text>
  <text x="30" y="318" font-size="12">aarch64-linux-musl</text>
  <text x="245" y="318" font-size="12">AArch64</text>
  <text x="345" y="318" font-size="12">Linux</text>
  <text x="425" y="318" font-size="12">musl libc (Alpine, 일부 Buildroot)</text>
  <text x="340" y="348" text-anchor="middle" font-size="12">vendor 칸은 대부분 생략: aarch64-linux-gnu = aarch64-unknown-linux-gnu</text>
</svg>
```
그림 3 — triplet은 "어떤 CPU에서, 어떤 OS 위에서, 어떤 C 라이브러리·호출 규약으로 도는 코드인가"를 한 줄로 적은 것이다.

칸별 의미를 하나씩 보자.

- **arch**: `aarch64`, `arm`, `armv7a`, `riscv64`, `x86_64`. 32-bit ARM은 그냥 `arm`이라 쓰고 세부 아키텍처는 컴파일러 기본값(`--with-arch`)과 `-march`로 정한다.
- **vendor**: `unknown`, `pc`, `none`, `poky`(Yocto), `buildroot` 등. 코드 생성에는 거의 영향이 없고 "누가 만든 툴체인인가" 표시다. 그래서 Debian/Ubuntu는 아예 생략한다. 단, `arm-none-eabi`의 `none`은 위치상 vendor 칸이지만 관례적으로 "OS 없음"으로 읽힌다 (정식형으로 쓰면 `arm-unknown-none-eabi`에 가깝다).
- **os**: `linux`, `none`(없음), `elf`, `w64`(Windows) 등. 이것이 바뀌면 **syscall 방식, 실행 파일 형식의 세부, 시작 코드**가 바뀐다.
- **abi(환경)**: `gnu`(glibc), `musl`, `gnueabi`(glibc + ARM EABI, soft-float), `gnueabihf`(glibc + EABI + **h**ard-**f**loat), `eabi`(bare-metal EABI). 이것이 바뀌면 **함수 호출 규약과 C 라이브러리**가 바뀐다.

> 함정 메모: Clang은 triplet을 내부적으로 정규화한다. `--target=arm-linux-gnueabihf`를 주면 내부 triple은 `armv7-unknown-linux-gnueabihf`로 바뀐다(§8의 출력). 반대로 GCC는 triplet 하나당 **컴파일러 바이너리 한 벌**이 따로 설치된다. 이 차이가 §8의 주제다.

### 2.2 코드로 확인 — 한 소스, 네 아키텍처

무엇을 확인하나: 같은 `hello.c`를 네 툴체인으로 빌드하고 `file`과 `readelf -h`로 ELF 헤더가 어떻게 다른지 본다.

```c
/* hello.c: 어떤 아키텍처용으로 빌드됐는지 스스로 말하는 hello */
#include <stdio.h>
#include <sys/utsname.h>

int main(void)
{
    struct utsname u;
    if (uname(&u) < 0) { perror("uname"); return 1; }
#if defined(__aarch64__)
    const char *arch = "aarch64";
#elif defined(__arm__)
    const char *arch = "arm (32-bit)";
#elif defined(__riscv)
    const char *arch = "riscv";
#elif defined(__x86_64__)
    const char *arch = "x86_64";
#else
    const char *arch = "unknown";
#endif
    printf("hello: built for %s, sizeof(long)=%zu, uname -m says %s\n",
           arch, sizeof(long), u.machine);
    return 0;
}
```

```sh
gcc                     -Wall -Wextra -O2 $S/hello.c -o hello-arm64
arm-linux-gnueabihf-gcc -Wall -Wextra -O2 $S/hello.c -o hello-armhf
riscv64-linux-gnu-gcc   -Wall -Wextra -O2 $S/hello.c -o hello-riscv64
x86_64-linux-gnu-gcc    -Wall -Wextra -O2 $S/hello.c -o hello-x86_64
file hello-*
for f in hello-*; do echo "== $f"; readelf -h $f | grep -E "Class|Data|Machine|Flags|Entry"; done
```

```text
hello-arm64:   ELF 64-bit LSB pie executable, ARM aarch64, version 1 (SYSV), dynamically linked, interpreter /lib/ld-linux-aarch64.so.1, BuildID[sha1]=5fe4d7d8c70332449b110b5142d1d1ca9c49d99e, for GNU/Linux 3.7.0, not stripped
hello-armhf:   ELF 32-bit LSB pie executable, ARM, EABI5 version 1 (SYSV), dynamically linked, interpreter /lib/ld-linux-armhf.so.3, BuildID[sha1]=e4366d01843fbdb0aa3ecbb481a23e1f26af7d81, for GNU/Linux 3.2.0, not stripped
hello-riscv64: ELF 64-bit LSB pie executable, UCB RISC-V, RVC, double-float ABI, version 1 (SYSV), dynamically linked, interpreter /lib/ld-linux-riscv64-lp64d.so.1, BuildID[sha1]=784dd6e3e1ec38818aced56eae54edeb10cd4006, for GNU/Linux 4.15.0, not stripped
hello-x86_64:  ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV), dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2, BuildID[sha1]=ed6886b984941f11670526b13f98086e0e17cde0, for GNU/Linux 3.2.0, not stripped
== hello-arm64
  Class:                             ELF64
  Data:                              2's complement, little endian
  Machine:                           AArch64
  Entry point address:               0x8c0
  Flags:                             0x0
== hello-armhf
  Class:                             ELF32
  Data:                              2's complement, little endian
  Machine:                           ARM
  Entry point address:               0x579
  Flags:                             0x5000400, Version5 EABI, hard-float ABI
== hello-riscv64
  Class:                             ELF64
  Data:                              2's complement, little endian
  Machine:                           RISC-V
  Entry point address:               0x770
  Flags:                             0x5, RVC, double-float ABI
== hello-x86_64
  Class:                             ELF64
  Data:                              2's complement, little endian
  Machine:                           Advanced Micro Devices X86-64
  Entry point address:               0x1160
  Flags:                             0x0
```

출력에서 볼 것 (네 가지):

1. **interpreter**: 동적 링크된 실행 파일마다 "나를 메모리에 올려 줄 동적 로더(dynamic loader) 경로"가 박혀 있다. 아키텍처·ABI마다 이름이 다르다 (`ld-linux-armhf.so.3`, `ld-linux-riscv64-lp64d.so.1`). §5에서 이게 왜 중요한지 본다.
2. **ELF e_flags**: ARM은 `Version5 EABI, hard-float ABI`, RISC-V는 `RVC, double-float ABI`처럼 **ABI가 헤더 플래그에 기록**된다. 링커는 이 플래그가 다른 오브젝트를 섞지 않는다 (§6).
3. **armhf 엔트리 `0x579`가 홀수**: Ubuntu armhf 툴체인은 기본이 Thumb-2(`--with-mode=thumb`)라서, 주소 최하위 비트 1 = "Thumb 상태로 점프"라는 Cortex-M 시절의 그 규칙이 그대로 보인다.
4. **`for GNU/Linux 3.2.0`**: glibc가 요구하는 **최소 커널 버전**(ELF note `NT_GNU_ABI_TAG`)이다. riscv64는 4.15부터 지원되므로 4.15.0이다. 커널 헤더 이야기(§9)와 이어진다.

---

## 3. 툴체인 안에는 무엇이 있나

### 3.1 부품 목록

"툴체인"은 컴파일러 하나가 아니라 **여러 프로그램과 라이브러리의 묶음**이다. Don이 쓰던 `arm-none-eabi` 패키지도 마찬가지였다 — 다만 Linux용에는 libc와 커널 헤더가 더 무겁게 들어간다.

| 부품 | 무엇 | 이 VM에서의 위치 (armhf) | bare-metal 대응 |
|---|---|---|---|
| gcc driver | 옵션을 해석해 아래 프로그램들을 차례로 부르는 "지휘자" | `/usr/bin/arm-linux-gnueabihf-gcc` | 같음 |
| cc1 | 진짜 C 컴파일러 (전처리 + C → 어셈블리) | `/usr/libexec/gcc-cross/arm-linux-gnueabihf/15/cc1` | 같음 |
| as, ld, objdump, readelf, nm, objcopy, strip … | **binutils** — 어셈블러, 링커, ELF 도구 | `/usr/arm-linux-gnueabihf/bin/` + `/usr/bin/arm-linux-gnueabihf-*` | 같음 |
| collect2 | ld 앞단 래퍼 (C++ 생성자 수집 등) | `/usr/libexec/gcc-cross/.../collect2` | 같음 |
| libgcc | 컴파일러 보조 루틴 (64-bit 나눗셈, soft-float `__aeabi_fadd`, 예외 unwind) | `/usr/lib/gcc-cross/arm-linux-gnueabihf/15/libgcc.a` | 같음 |
| crt 파일 | `crt1.o`(`_start`), `crti.o`/`crtn.o`, `crtbegin.o`/`crtend.o` | sysroot `lib/` + gcc 디렉터리 | Don이 직접 쓰던 `startup.s` |
| C 라이브러리 | glibc (`libc.so.6`, `libc.a`) + 동적 로더 `ld-linux-armhf.so.3` | `/usr/arm-linux-gnueabihf/lib/` | newlib (`libc.a`) |
| libc 헤더 | `stdio.h`, `pthread.h`, `bits/…` | `/usr/arm-linux-gnueabihf/include/` | newlib 헤더 |
| Linux 커널 헤더 (UAPI) | `linux/*.h`, `asm/*.h` — syscall 번호, ioctl 구조체 | 같은 `include/` (패키지 `linux-libc-dev-armhf-cross`) | **없음** |
| C++ 런타임 | `libstdc++`, `libsupc++` | g++ 패키지 | 보통 안 씀 |

이 중 **libc + libc 헤더 + 커널 헤더 + crt 파일**을 "타깃 루트 파일시스템처럼 생긴 디렉터리"에 모아 둔 것이 **sysroot**다. 링커와 컴파일러는 헤더·라이브러리를 호스트의 `/usr/include`, `/usr/lib`가 아니라 sysroot에서 찾아야 한다.

### 3.2 코드로 확인 — gcc에게 부품 위치를 물어보기

무엇을 확인하나: `-v`로 빌드 설정을, `-print-*` 옵션으로 각 부품의 실제 경로를 꺼낸다.

```sh
G=arm-linux-gnueabihf-gcc
$G -v 2>&1 | grep -E "^Target|^Configured|^Thread|^gcc version" | sed "s/--/\n  --/g" \
  | grep -E "Target|gcc version|with-arch|with-fpu|with-float|with-mode|sysroot|prefix=|program-prefix|includedir"
echo ===; $G -print-sysroot; echo "[sysroot above]"
$G -print-libgcc-file-name
$G -print-prog-name=cc1;  $G -print-prog-name=as
$G -print-prog-name=collect2;  $G -print-prog-name=ld
$G -print-file-name=libc.so.6;  $G -print-file-name=crt1.o
```

```text
Target: arm-linux-gnueabihf
  --prefix=/usr 
  --with-sysroot=/ 
  --with-arch=armv7-a+fp 
  --with-float=hard 
  --with-mode=thumb 
  --program-prefix=arm-linux-gnueabihf- 
  --includedir=/usr/arm-linux-gnueabihf/include 
gcc version 15.2.0 (Ubuntu 15.2.0-16ubuntu1) 
===
/
[sysroot above]
/usr/lib/gcc-cross/arm-linux-gnueabihf/15/libgcc.a
/usr/libexec/gcc-cross/arm-linux-gnueabihf/15/cc1
/usr/lib/gcc-cross/arm-linux-gnueabihf/15/../../../../arm-linux-gnueabihf/bin/as
/usr/libexec/gcc-cross/arm-linux-gnueabihf/15/collect2
/usr/lib/gcc-cross/arm-linux-gnueabihf/15/../../../../arm-linux-gnueabihf/bin/ld
/usr/lib/gcc-cross/arm-linux-gnueabihf/15/../../../../arm-linux-gnueabihf/lib/libc.so.6
/usr/lib/gcc-cross/arm-linux-gnueabihf/15/../../../../arm-linux-gnueabihf/lib/crt1.o
```

출력에서 볼 것:

- **기본값이 configure 때 박힌다**: `--with-arch=armv7-a+fp --with-float=hard --with-mode=thumb`. 그래서 `-march`를 안 줘도 ARMv7-A + VFP + Thumb-2 + hard-float 코드가 나온다. 벤더 SDK 툴체인은 여기에 보드 CPU(`--with-cpu=cortex-a53` 등)를 박아 두는 경우가 많다.
- **sysroot가 `/`**: Debian/Ubuntu 크로스 패키지는 특이하게 sysroot를 `/`로 두고, 타깃 파일을 `/usr/arm-linux-gnueabihf/{include,lib}`라는 **multiarch 디렉터리**에 넣는다. Buildroot·Yocto·Arm GNU Toolchain 같은 일반적인 툴체인은 sysroot가 별도 디렉터리(예: `.../arm-buildroot-linux-gnueabihf/sysroot`)다. 원리는 같고 레이아웃만 다르다 (B5, K).
- 실제 파일 경로를 따라가면 **cc1·collect2는 gcc 쪽(`libexec/gcc-cross`), as·ld는 binutils 쪽(`/usr/arm-linux-gnueabihf/bin`), libc·crt1.o는 sysroot 쪽**이다. 세 출처가 합쳐진 것이 툴체인이다.

### 3.3 코드로 확인 — `-###`로 driver가 실제로 부르는 명령 보기

무엇을 확인하나: `gcc -###`는 아무것도 실행하지 않고 "실행할 명령"만 출력한다. Don이 bare-metal Makefile에서 직접 쓰던 `as`·`ld` 호출이 driver 뒤에 숨어 있는 것을 확인한다.

```sh
$G -### -O2 $S/hello.c -o h 2>&1 \
  | grep -v "^COLLECT\|^Using\|^Target\|^Configured\|^Thread\|^Supported\|^gcc version\|^OFFLOAD" | cut -c1-330
```

```text
 /usr/libexec/gcc-cross/arm-linux-gnueabihf/15/cc1 -quiet -imultilib . -imultiarch arm-linux-gnueabihf /Users/donh/.elinux_scratch/B1/hello.c "-D_TIME_BITS=64" "-D_FILE_OFFSET_BITS=64" "-D_FORTIFY_SOURCE=3" -quiet -dumpdir h- -dumpbase hello.c -dumpbase-ext .c "-mfloat-abi=hard" "-mtls-dialect=gnu" -mthumb "-mlibarch=armv7-a+fp"
 /usr/lib/gcc-cross/arm-linux-gnueabihf/15/../../../../arm-linux-gnueabihf/bin/as "-march=armv7-a+fp" "-mfloat-abi=hard" "-meabi=5" -o /tmp/cc1uoI60.o /tmp/cclzZ0cN.s
COMPILER_PATH=/usr/libexec/gcc-cross/arm-linux-gnueabihf/15/:/usr/libexec/gcc-cross/arm-linux-gnueabihf/15/:/usr/libexec/gcc-cross/arm-linux-gnueabihf/:/usr/lib/gcc-cross/arm-linux-gnueabihf/15/:/usr/lib/gcc-cross/arm-linux-gnueabihf/:/usr/lib/gcc-cross/arm-linux-gnueabihf/15/../../../../arm-linux-gnueabihf/bin/
LIBRARY_PATH=/usr/lib/gcc-cross/arm-linux-gnueabihf/15/:/usr/lib/gcc-cross/arm-linux-gnueabihf/15/../../../../arm-linux-gnueabihf/lib/:/lib/arm-linux-gnueabihf/:/lib/:/usr/lib/arm-linux-gnueabihf/:/usr/lib/
 /usr/libexec/gcc-cross/arm-linux-gnueabihf/15/collect2 -plugin /usr/libexec/gcc-cross/arm-linux-gnueabihf/15/liblto_plugin.so "-plugin-opt=/usr/libexec/gcc-cross/arm-linux-gnueabihf/15/lto-wrapper" "-plugin-opt=-fresolution=/tmp/ccdYjUZm.res" "-plugin-opt=-pass-through=-lgcc" "-plugin-opt=-pass-through=-lgcc_s" "-plugin-opt=-pa
```

collect2 줄이 너무 길어서, LTO 플러그인 옵션과 `-L` 경로를 빼고 다시 보자.

```sh
$G -### -O2 $S/hello.c -o h 2>&1 | grep collect2 | tr " " "\n" | grep -vE "plugin|^\"?-L" | tr "\n" " "
```

```text
 /usr/libexec/gcc-cross/arm-linux-gnueabihf/15/collect2 "--sysroot=/" --build-id --eh-frame-hdr -dynamic-linker /lib/ld-linux-armhf.so.3 -X "--hash-style=gnu" --as-needed -m armelf_linux_eabi -pie -z now -z relro -o h /usr/lib/gcc-cross/arm-linux-gnueabihf/15/../../../../arm-linux-gnueabihf/lib/Scrt1.o /usr/lib/gcc-cross/arm-linux-gnueabihf/15/../../../../arm-linux-gnueabihf/lib/crti.o /usr/lib/gcc-cross/arm-linux-gnueabihf/15/crtbeginS.o /tmp/ccBoGZQD.o -lgcc --push-state --as-needed -lgcc_s --pop-state -lc -lgcc --push-state --as-needed -lgcc_s --pop-state /usr/lib/gcc-cross/arm-linux-gnueabihf/15/crtendS.o /usr/lib/gcc-cross/arm-linux-gnueabihf/15/../../../../arm-linux-gnueabihf/lib/crtn.o
```

출력에서 볼 것:

- **cc1에 몰래 붙는 매크로**: `-D_TIME_BITS=64 -D_FILE_OFFSET_BITS=64`는 Ubuntu가 32-bit ARM에서 `time_t`·`off_t`를 64-bit로 바꾼 정책(2038년 문제 대비)이다. `-D_FORTIFY_SOURCE=3`은 배포판 보안 기본값이다. "같은 GCC 15인데 Buildroot 툴체인과 결과가 다르다"는 일이 이런 배포판 기본값 때문에 생긴다.
- **링크 줄 = Don의 bare-metal 링크 + OS 부품**: `Scrt1.o`(여기 `_start`가 있다, PIE용 S 버전) → `crti.o` → `crtbeginS.o` → **내 오브젝트** → `-lgcc -lc -lgcc` → `crtendS.o` → `crtn.o`. 그리고 `-dynamic-linker /lib/ld-linux-armhf.so.3` — §2.2의 "interpreter" 문자열이 여기서 박힌다. bare-metal에서는 이 자리에 Don의 `startup.s`와 링커 스크립트(`-T`)가 있었다.

### 3.4 컴파일 4단계 — `-save-temps`

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="40" width="120" height="300" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="80" y="170" text-anchor="middle" font-size="13">gcc driver</text>
  <text x="80" y="190" text-anchor="middle" font-size="12">(옵션 해석,</text>
  <text x="80" y="207" text-anchor="middle" font-size="12">순서대로 호출)</text>
  <rect x="170" y="20" width="220" height="30" rx="15" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="280" y="40" text-anchor="middle" font-size="12">hello.c</text>
  <rect x="170" y="66" width="220" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="280" y="84" text-anchor="middle" font-size="12">cc1: 전처리 (-E)</text>
  <text x="280" y="99" text-anchor="middle" font-size="12">→ hello.i</text>
  <rect x="170" y="120" width="220" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="280" y="138" text-anchor="middle" font-size="12">cc1: 컴파일 (-S)</text>
  <text x="280" y="153" text-anchor="middle" font-size="12">→ hello.s</text>
  <rect x="170" y="174" width="220" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="280" y="192" text-anchor="middle" font-size="12">as: 어셈블 (-c)</text>
  <text x="280" y="207" text-anchor="middle" font-size="12">→ hello.o</text>
  <rect x="170" y="228" width="220" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="280" y="246" text-anchor="middle" font-size="12">collect2 → ld: 링크</text>
  <text x="280" y="261" text-anchor="middle" font-size="12">→ hello (ELF)</text>
  <rect x="170" y="290" width="220" height="44" rx="15" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="280" y="309" text-anchor="middle" font-size="12">hello</text>
  <text x="280" y="325" text-anchor="middle" font-size="12">INTERP: ld-linux-armhf.so.3</text>
  <line x1="280" y1="50" x2="280" y2="64" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="280" y1="106" x2="280" y2="118" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="280" y1="160" x2="280" y2="172" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="280" y1="214" x2="280" y2="226" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="280" y1="268" x2="280" y2="288" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="140" y1="86" x2="168" y2="86" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah3)"/>
  <line x1="140" y1="194" x2="168" y2="194" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah3)"/>
  <line x1="140" y1="248" x2="168" y2="248" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah3)"/>
  <rect x="430" y="56" width="236" height="60" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="548" y="76" text-anchor="middle" font-size="12">sysroot/include</text>
  <text x="548" y="92" text-anchor="middle" font-size="12">glibc 헤더 + linux/asm UAPI</text>
  <text x="548" y="108" text-anchor="middle" font-size="12">+ gcc 내장 (stddef.h, arm_neon.h)</text>
  <line x1="430" y1="86" x2="392" y2="86" stroke="currentColor" marker-end="url(#ah3)"/>
  <rect x="430" y="200" width="236" height="96" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="548" y="220" text-anchor="middle" font-size="12">sysroot: Scrt1.o crti.o crtn.o</text>
  <text x="548" y="238" text-anchor="middle" font-size="12">libc.so.6 · libc.a · libm …</text>
  <text x="548" y="256" text-anchor="middle" font-size="12">gcc: crtbeginS.o · libgcc.a</text>
  <text x="548" y="274" text-anchor="middle" font-size="12">-dynamic-linker 경로</text>
  <text x="548" y="290" text-anchor="middle" font-size="12">(bare-metal: startup.s + -T)</text>
  <line x1="430" y1="248" x2="392" y2="248" stroke="currentColor" marker-end="url(#ah3)"/>
</svg>
```
그림 4 — 왼쪽 driver가 cc1·as·collect2/ld를 차례로 부르고, 오른쪽 sysroot가 헤더(컴파일 단계)와 crt·libc(링크 단계)를 공급한다.

무엇을 확인하나: `-save-temps`로 각 단계의 중간 산출물을 남기고, 단계마다 무엇이 생기는지 본다.

```sh
mkdir -p /tmp/B1/st && cd /tmp/B1/st
arm-linux-gnueabihf-gcc -O2 -save-temps $S/hello.c -o hello && ls -l
grep -c "" hello.i; grep -n "^int main\|printf (const" hello.i | head
sed -n "/^main:/,/\.size\tmain/p" hello.s | head -40
file hello.o; arm-linux-gnueabihf-nm hello.o
```

```text
total 60
-rwxrwxr-x 1 donh donh 69408 Oct 10 07:58 hello
-rw-rw-r-- 1 donh donh 39307 Oct 10 07:58 hello.i
-rw-rw-r-- 1 donh donh  1556 Oct 10 07:58 hello.o
-rw-rw-r-- 1 donh donh  1696 Oct 10 07:58 hello.s

1240
490:extern int printf (const char *__restrict __format, ...);
505:extern int vprintf (const char *__restrict __format, __gnuc_va_list __arg);
1061:printf (const char *__restrict __fmt, ...)
1067:vprintf (const char * __restrict __fmt,
1222:int main(void)
```

```text
main:
	@ args = 0, pretend = 0, frame = 400
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L9
	ldr	r3, .L9+4
.LPIC4:
	add	r2, pc
	push	{lr}
	sub	sp, sp, #412
	ldr	r3, [r2, r3]
	add	r0, sp, #12
	ldr	r3, [r3]
	str	r3, [sp, #404]
	mov	r3, #0
	bl	uname(PLT)
	cmp	r0, #0
	blt	.L7
	ldr	r2, .L9+8
	add	ip, sp, #272
	ldr	r1, .L9+12
	movs	r3, #4
.LPIC1:
	add	r2, pc
	movs	r0, #2
.LPIC2:
	add	r1, pc
	str	ip, [sp]
	bl	__printf_chk(PLT)
	movs	r0, #0
...
hello.o: ELF 32-bit LSB relocatable, ARM, EABI5 version 1 (SYSV), not stripped
00000000 r .LC0
00000008 r .LC1
00000018 r .LC2
         U _GLOBAL_OFFSET_TABLE_
         U __printf_chk
         U __stack_chk_fail
         U __stack_chk_guard
00000000 T main
         U perror
         U uname
```

(`hello.s`는 `main`의 앞부분만 남기고 `...`로 줄였다. `head -40` 출력의 일부다.)

출력에서 볼 것:

- **hello.i (1240줄, 39KB)**: 23줄짜리 소스가 sysroot의 `stdio.h`·`sys/utsname.h`를 펼쳐 1240줄이 됐다. 여기 들어간 헤더가 **타깃용**이어야 한다. 호스트 헤더가 섞이면 구조체 크기가 틀어진다 (§12.2의 zlib 예).
- **hello.s**: `bl uname(PLT)`, `bl __printf_chk(PLT)` — 라이브러리 호출이 PLT(동적 링크 점프 테이블, B3)를 거친다. `printf`가 `__printf_chk`로 바뀐 것은 `_FORTIFY_SOURCE` 효과다. `sp, #404`에 넣는 값은 stack protector canary다.
- **hello.o의 `U` 심볼**: 아직 해결 안 된 외부 참조 (`uname`, `__printf_chk`). 링커가 sysroot의 libc에서 찾아 연결한다. `_start`는 아직 없다 — `Scrt1.o`가 링크 때 붙인다.

---

## 4. arm-none-eabi vs arm-linux-gnueabihf — Don의 세계와 새 세계

### 4.1 같은 ARM, 다른 "아래층"

두 툴체인 모두 ARM 코드를 내고, 둘 다 EABI(AAPCS 호출 규약)를 따른다. 차이는 **`main()` 아래에 무엇이 있느냐**다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="170" y="22" text-anchor="middle" font-size="14" font-weight="bold">arm-none-eabi (bare-metal)</text>
  <text x="510" y="22" text-anchor="middle" font-size="14" font-weight="bold">arm-linux-gnueabihf (Linux)</text>
  <rect x="30" y="36" width="280" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="170" y="61" text-anchor="middle" font-size="13">main() → printf()</text>
  <rect x="370" y="36" width="280" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="510" y="61" text-anchor="middle" font-size="13">main() → printf()</text>
  <rect x="30" y="86" width="280" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="111" text-anchor="middle" font-size="13">newlib libc.a (정적 링크만)</text>
  <rect x="370" y="86" width="280" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="510" y="111" text-anchor="middle" font-size="13">glibc libc.so.6 (동적 링크)</text>
  <rect x="30" y="136" width="280" height="56" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="170" y="158" text-anchor="middle" font-size="13">_write · _sbrk · _close … 스텁</text>
  <text x="170" y="178" text-anchor="middle" font-size="12">nosys / rdimon(semihosting) / 직접 구현</text>
  <rect x="370" y="136" width="280" height="56" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="510" y="158" text-anchor="middle" font-size="13">write() 래퍼: r7=번호, svc #0</text>
  <text x="510" y="178" text-anchor="middle" font-size="12">ld-linux-armhf.so.3 가 먼저 로드</text>
  <rect x="30" y="202" width="280" height="56" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="170" y="224" text-anchor="middle" font-size="13">startup.s + 링커 스크립트</text>
  <text x="170" y="244" text-anchor="middle" font-size="12">벡터 테이블 · .data 복사 · .bss 0</text>
  <rect x="370" y="202" width="280" height="56" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="510" y="224" text-anchor="middle" font-size="13">Linux 커널 (EL1 / SVC 모드)</text>
  <text x="510" y="244" text-anchor="middle" font-size="12">sys_write → VFS → tty 드라이버</text>
  <rect x="30" y="268" width="280" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="170" y="293" text-anchor="middle" font-size="13">UART 레지스터 직접 쓰기</text>
  <rect x="370" y="268" width="280" height="40" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="510" y="293" text-anchor="middle" font-size="13">드라이버가 UART 레지스터 접근</text>
  <rect x="30" y="318" width="620" height="34" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="340" text-anchor="middle" font-size="13">하드웨어: Cortex 코어 · UART · 메모리</text>
  <line x1="170" y1="76" x2="170" y2="84" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="170" y1="126" x2="170" y2="134" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="510" y1="76" x2="510" y2="84" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="510" y1="126" x2="510" y2="134" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="510" y1="192" x2="510" y2="200" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="170" y="380" text-anchor="middle" font-size="12">모두 한 이미지, 한 특권 레벨</text>
  <text x="510" y="380" text-anchor="middle" font-size="12">초록·파랑·주황 = 유저, 빨강 = 커널</text>
</svg>
```
그림 5 — 왼쪽은 printf의 끝이 "내가 채워야 하는 스텁"이고, 오른쪽은 printf의 끝이 `svc #0`으로 커널에 넘어간다. 툴체인 이름의 `none`과 `linux`가 이 차이다.

| 항목 | arm-none-eabi | arm-linux-gnueabihf |
|---|---|---|
| OS | 없음 (또는 RTOS를 내가 링크) | Linux 커널 |
| C 라이브러리 | newlib / newlib-nano (정적) | glibc (동적이 기본) |
| 시작 코드 | 내가 쓴 `startup.s`, `Reset_Handler` | `crt1.o`의 `_start` → `__libc_start_main` |
| 링커 스크립트 | 내가 쓴다 (FLASH/RAM 주소) | 툴체인 기본 스크립트, 주소는 커널이 정한다(PIE) |
| I/O의 끝 | `_write` 스텁 → UART 또는 semihosting | `write()` syscall → 커널 드라이버 |
| 메모리 할당 | `_sbrk`가 링커 심볼 `end`부터 힙을 늘림 | `brk`/`mmap` syscall |
| 동적 로더 | 없음 | `/lib/ld-linux-armhf.so.3` |
| 커널 헤더 | 없음 | `linux/*.h` (UAPI) |
| 기본 float ABI | soft (multilib로 hard 선택) | hard (이름의 `hf`) |
| 출력 형식 | ELF → `objcopy -O binary`로 플래시 | ELF 그대로 실행 |

### 4.2 코드로 확인 — newlib printf의 끝은 어디인가

무엇을 확인하나: `arm-none-eabi-gcc`로 printf를 쓰는 프로그램을 (1) 스텁 없이, (2) `nosys.specs`로, (3) `rdimon.specs`(semihosting)로 링크해 보고, 마지막 것을 qemu-arm으로 실제 실행한다.

```c
/* bm_hello.c: arm-none-eabi + newlib. OS가 없으니 printf의 끝은 "syscalls" 스텁이다 */
#include <stdio.h>

int main(void)
{
    printf("hello from bare-metal newlib, no OS below me\n");
    return 0;
}
```

```sh
N=arm-none-eabi-gcc
echo === nostub; $N -mcpu=cortex-m4 -mthumb -O2 $S/bm_hello.c -o bm_nostub.elf 2>&1 \
  | grep -oE "undefined reference to .[_a-z]+.|collect2: .*" | sort -u | head
echo === nosys;  $N -mcpu=cortex-m4 -mthumb -O2 --specs=nosys.specs $S/bm_hello.c -o bm_nosys.elf 2>&1 \
  | grep -oE "warning: [_a-z]+ is not implemented" | sort -u
```

```text
=== nostub
collect2: error: ld returned 1 exit status
undefined reference to `_close'
undefined reference to `_exit'
undefined reference to `_fstat'
undefined reference to `_isatty'
undefined reference to `_lseek'
undefined reference to `_read'
undefined reference to `_sbrk'
undefined reference to `_write'
=== nosys
warning: _close is not implemented
warning: _fstat is not implemented
warning: _isatty is not implemented
warning: _lseek is not implemented
warning: _read is not implemented
warning: _write is not implemented
```

출력에서 볼 것: newlib은 OS 기능 8개(`_write`, `_sbrk`, `_read` …)를 **외부에 맡긴다**. Don이 펌웨어에서 `_write`를 UART 송신 함수로 구현해 printf를 살렸던 그 지점이다. `nosys.specs`는 "전부 실패를 반환하는 가짜"를 넣어 링크만 되게 한다. Linux 툴체인에서는 이 8개 자리에 **syscall**이 있고, 그 너머에 커널이 있다.

이번에는 semihosting(디버거·에뮬레이터가 호스트의 파일·콘솔을 대신 처리해 주는 ARM 규약) 버전을 만들어 실제로 돌린다.

```sh
$N -mcpu=cortex-a7 -marm -O2 --specs=rdimon.specs $S/bm_hello.c -o bm_rdimon.elf && file bm_rdimon.elf \
  && readelf -h bm_rdimon.elf | grep -E "Flags|Entry|OS/ABI"; readelf -l bm_rdimon.elf | grep -c INTERP
qemu-arm ./bm_rdimon.elf; echo "exit=$?"
cat /proc/sys/vm/mmap_min_addr
$N -mcpu=cortex-a7 -marm -O2 --specs=rdimon.specs -Wl,-Ttext-segment=0x400000 $S/bm_hello.c -o bm_rdimon.elf \
  && readelf -l bm_rdimon.elf | grep LOAD
qemu-arm ./bm_rdimon.elf; echo "exit=$?"
arm-none-eabi-objdump -d bm_rdimon.elf | grep -E "svc|bkpt" | head -3
arm-none-eabi-nm bm_rdimon.elf | grep -E " _write$| _sbrk$| _swiwrite"
```

```text
bm_rdimon.elf: ELF 32-bit LSB executable, ARM, EABI5 version 1 (SYSV), statically linked, with debug_info, not stripped
  OS/ABI:                            UNIX - System V
  Entry point address:               0x8195
  Flags:                             0x5000200, Version5 EABI, soft-float ABI
0
qemu-arm: /tmp/B1/bm_rdimon.elf: Error mapping file: Operation not permitted
exit=255
32768
  LOAD           0x010000 0x00400000 0x00400000 0x034bc 0x034bc R E 0x10000
  LOAD           0x0134bc 0x004134bc 0x004134bc 0x00680 0x00a54 RW  0x10000
hello from bare-metal newlib, no OS below me
exit=0
  40019c:	dfab      	svc	171	@ 0xab
  4001de:	dfab      	svc	171	@ 0xab
  4029e8:	dfab      	svc	171	@ 0xab
00402d60 W _sbrk
00402ba4 T _swiwrite
00402be0 W _write
```

출력에서 볼 것:

- **`soft-float ABI`, INTERP 0개, statically linked**: none-eabi 기본값은 soft-float이고, 동적 로더가 없다. 같은 ARM ELF라도 헤더 플래그가 Linux용(`hard-float ABI`)과 다르다.
- **첫 실행 실패**: 기본 링커 스크립트가 코드를 `0x8000`에 놓는데, 이 VM의 `vm.mmap_min_addr`가 32768(=0x8000)이라 유저 프로세스가 그 주소를 매핑할 수 없다 (NULL 근처 매핑 금지, 커널 보안 설정). `-Ttext-segment=0x400000`으로 옮기자 돌았다. bare-metal 이미지의 주소 감각과 Linux 프로세스의 주소 규칙이 부딪친 장면이다.
- **`svc 0xab`**: newlib의 `_write` → `_swiwrite`가 Thumb semihosting 호출(`svc 0xab`)을 한다. qemu-arm(user 모드)이 이것을 잡아 호스트 콘솔에 찍어 줬다. 진짜 보드에서는 JTAG 디버거(OpenOCD 등)가 같은 일을 한다. Cortex-M에서는 `bkpt 0xab`를 쓴다.

> 함정 메모: 이 실험은 "none-eabi ELF를 Linux 유저 프로세스처럼 실행"한 것이라 qemu가 관대하게 처리해 준 결과다. 실제 MCU 펌웨어는 벡터 테이블·링커 스크립트가 있어야 하고 `qemu-system-arm -M <보드>`나 실물 보드에서 돌린다.

---

## 5. 크로스 바이너리 실행하기 — qemu-user와 sysroot

### 5.1 직관 — "명령어 번역기 + syscall 중계기"

qemu-user(`qemu-arm`, `qemu-riscv64` …)는 **다른 아키텍처의 유저 프로세스 하나**를 돌린다. 명령어는 동적 번역(TCG)으로 호스트 명령어로 바꾸고, syscall은 **번호·구조체를 호스트 커널 형식으로 변환해서** 호스트 커널에 넘긴다. 커널까지 에뮬레이션하는 `qemu-system-*`과 달리 커널은 호스트 것을 그대로 쓴다. 그래서 가볍고 빠르지만 드라이버·`/dev` 실험은 못 한다.

남은 문제가 하나 있다. 동적 링크된 armhf 바이너리는 실행되자마자 `/lib/ld-linux-armhf.so.3`를 찾고, 그 로더는 `libc.so.6`을 찾는다. 이 파일들은 **호스트의 `/lib`에 없다** — sysroot에 있다. 그래서 qemu에게 "타깃의 `/`는 여기다"라고 알려 줘야 한다: `-L <sysroot>` 또는 환경 변수 `QEMU_LD_PREFIX`.

### 5.2 코드로 확인 — 네 가지 실패와 성공

무엇을 확인하나: §2.2의 네 바이너리를 그냥 실행했을 때와 `-L`을 줬을 때를 비교한다.

```sh
for f in hello-arm64 hello-armhf hello-riscv64 hello-x86_64; do echo "\$ ./$f"; ./$f; echo "exit=$?"; done
echo ---; qemu-arm ./hello-armhf; echo "exit=$?"
qemu-arm -L /usr/arm-linux-gnueabihf ./hello-armhf; echo "exit=$?"
cat /proc/sys/fs/binfmt_misc/qemu-riscv64 | head -5
ls /proc/sys/fs/binfmt_misc | grep -c qemu
```

```text
$ ./hello-arm64
hello: built for aarch64, sizeof(long)=8, uname -m says aarch64
exit=0
$ ./hello-armhf
bash: line 1: ./hello-armhf: cannot execute binary file: Exec format error
exit=126
$ ./hello-riscv64
qemu-riscv64: Could not open '/lib/ld-linux-riscv64-lp64d.so.1': No such file or directory
exit=255
$ ./hello-x86_64
qemu-x86_64: Could not open '/lib64/ld-linux-x86-64.so.2': No such file or directory
exit=255
---
qemu-arm: Could not open '/lib/ld-linux-armhf.so.3': No such file or directory
exit=255
hello: built for arm (32-bit), sizeof(long)=4, uname -m says armv7l
exit=0
enabled
interpreter /usr/bin/qemu-riscv64
flags: POF
offset 0
magic 7f454c460201010000000000000000000200f300
31
```

출력에서 볼 것:

- **riscv64·x86_64는 `./`만으로 qemu가 떴다**: `binfmt_misc`(커널이 ELF 헤더의 magic 바이트를 보고 "이 형식은 이 인터프리터로 실행하라"고 정하는 기능)에 qemu가 31개 등록돼 있다. `magic`의 `f300`은 ELF `e_machine` = 0xF3(RISC-V)이다. 커널의 `execve`가 이 표를 보고 `qemu-riscv64`를 대신 실행한다.
- **armhf만 `Exec format error`**: binfmt 목록에 `qemu-arm`이 없다. Debian 계열은 arm64 호스트에서 qemu-arm을 등록하지 않는다 — 많은 arm64 CPU가 32-bit ARM(AArch32) 유저 코드를 하드웨어로 직접 돌릴 수 있기 때문이다. 그런데 **Apple Silicon은 AArch32를 지원하지 않아서** 커널이 거절한다. 그래서 `qemu-arm`을 명시적으로 불러야 한다.
- **`Could not open '/lib/ld-linux-...'`**: 번역은 됐지만 **동적 로더를 호스트 `/lib`에서 찾다가 실패**했다. `-L /usr/arm-linux-gnueabihf`를 주자 성공했다. 이 메시지가 바로 "sysroot가 왜 필요한가"의 실행 시점 버전이다.
- **`uname -m says armv7l`**: qemu가 `uname` syscall 결과의 machine 필드를 타깃 것으로 바꿔 준다. 커널 버전(`release`)은 호스트 것 그대로다 (§9에서 다시 본다).

### 5.3 코드로 확인 — QEMU_LD_PREFIX와 정적 링크

무엇을 확인하나: binfmt로 자동 실행될 때는 `-L` 대신 환경 변수로 sysroot를 알려 준다. 정적 링크하면 로더가 필요 없다는 것도 확인한다.

```sh
QEMU_LD_PREFIX=/usr/riscv64-linux-gnu ./hello-riscv64
QEMU_LD_PREFIX=/usr/x86_64-linux-gnu  ./hello-x86_64
arm-linux-gnueabihf-gcc -O2 -static $S/hello.c -o hello-armhf-static && file hello-armhf-static && qemu-arm ./hello-armhf-static
ls -l /usr/arm-linux-gnueabihf/lib/ld-linux-armhf.so.3 /usr/arm-linux-gnueabihf/lib/libc.so.6
readelf -d hello-armhf | grep NEEDED; readelf -l hello-armhf | grep -A1 INTERP
```

```text
hello: built for riscv, sizeof(long)=8, uname -m says riscv64
hello: built for x86_64, sizeof(long)=8, uname -m says x86_64
hello-armhf-static: ELF 32-bit LSB executable, ARM, EABI5 version 1 (GNU/Linux), statically linked, BuildID[sha1]=bdb89113cae72f27a8ae784cf7697ea1a6b56d90, for GNU/Linux 3.2.0, not stripped
hello: built for arm (32-bit), sizeof(long)=4, uname -m says armv7l
-rwxr-xr-x 1 root root 1115736 Apr 10 23:34 /usr/arm-linux-gnueabihf/lib/ld-linux-armhf.so.3
-rwxr-xr-x 1 root root 1188008 Apr 10 23:34 /usr/arm-linux-gnueabihf/lib/libc.so.6
 0x00000001 (NEEDED)                     Shared library: [libc.so.6]
 0x00000001 (NEEDED)                     Shared library: [ld-linux-armhf.so.3]
  INTERP         0x000198 0x00000198 0x00000198 0x00019 0x00019 R   0x1
      [Requesting program interpreter: /lib/ld-linux-armhf.so.3]
```

출력에서 볼 것: 정적 바이너리는 `-L` 없이 돌았다 — INTERP가 없으니 찾을 파일이 없다. 동적 바이너리는 `PT_INTERP`(로더 경로)와 `NEEDED`(필요한 공유 라이브러리) 두 가지를 sysroot에서 찾아야 한다. **보드 rootfs가 바로 이 sysroot의 "실물"**이다. 보드에서 같은 문제가 나면 qemu 메시지 대신 셸이 `./hello: not found`처럼 아주 헷갈리는 메시지를 낸다 (파일은 있는데 INTERP가 없어서). 자세한 디버깅은 B2, B3.

---

## 6. ABI — EABI, hard-float, soft-float

### 6.1 직관 — "함수를 부를 때 인자를 어디에 두기로 약속했나"

ABI(Application Binary Interface)는 **이미 컴파일된 코드끼리** 지켜야 하는 약속이다: 인자를 어느 레지스터에 넣나, 구조체는 어떻게 정렬하나, 스택은 몇 바이트 정렬인가, `long`은 몇 비트인가. Don이 Cortex-R 펌웨어에서 어셈블리 함수와 C 함수를 섞을 때 "r0~r3에 인자, r0에 리턴, r4~r11은 callee-saved"를 지켰던 것이 ARM의 **AAPCS**(Procedure Call Standard for the Arm Architecture)이고, 이 AAPCS를 포함한 ARM의 바이너리 규약 묶음이 **EABI**(Embedded ABI)다. `arm-none-eabi`와 `arm-linux-gnueabihf` 둘 다 EABI를 따른다 — 그래서 둘 다 이름에 `eabi`가 들어간다.

32-bit ARM에서 가장 자주 문제가 되는 ABI 차이는 **float 인자를 어디에 넣는가**다.

```svg
<svg viewBox="0 0 680 270" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="115" y="22" text-anchor="middle" font-size="14" font-weight="bold">-mfloat-abi=soft</text>
  <text x="340" y="22" text-anchor="middle" font-size="14" font-weight="bold">-mfloat-abi=softfp</text>
  <text x="565" y="22" text-anchor="middle" font-size="14" font-weight="bold">-mfloat-abi=hard</text>
  <rect x="20" y="36" width="190" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="115" y="61" text-anchor="middle" font-size="12">인자: r0, r1 (정수 레지스터)</text>
  <rect x="245" y="36" width="190" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="61" text-anchor="middle" font-size="12">인자: r0, r1 (정수 레지스터)</text>
  <rect x="470" y="36" width="190" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="565" y="61" text-anchor="middle" font-size="12">인자: s0, s1 (VFP 레지스터)</text>
  <rect x="20" y="96" width="190" height="56" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="115" y="118" text-anchor="middle" font-size="12">bl __aeabi_fadd</text>
  <text x="115" y="138" text-anchor="middle" font-size="12">(libgcc 정수 연산으로 흉내)</text>
  <rect x="245" y="96" width="190" height="56" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="118" text-anchor="middle" font-size="12">vmov s14, r0 / s15, r1</text>
  <text x="340" y="138" text-anchor="middle" font-size="12">vadd.f32 → vmov r0, s15</text>
  <rect x="470" y="96" width="190" height="56" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="565" y="118" text-anchor="middle" font-size="12">vadd.f32 s0, s0, s1</text>
  <text x="565" y="138" text-anchor="middle" font-size="12">(복사 없음)</text>
  <line x1="115" y1="76" x2="115" y2="94" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="340" y1="76" x2="340" y2="94" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="565" y1="76" x2="565" y2="94" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="115" y="176" text-anchor="middle" font-size="12">FPU 없는 코어용</text>
  <text x="340" y="176" text-anchor="middle" font-size="12">FPU 사용 + soft 호출 규약</text>
  <text x="565" y="176" text-anchor="middle" font-size="12">FPU 사용 + VFP 호출 규약</text>
  <line x1="20" y1="196" x2="435" y2="196" stroke="#4a7bd0"/>
  <text x="228" y="216" text-anchor="middle" font-size="12">서로 링크 가능 (호출 규약이 같다) — Debian armel</text>
  <line x1="470" y1="196" x2="660" y2="196" stroke="#3f9a6b"/>
  <text x="565" y="216" text-anchor="middle" font-size="12">Debian armhf (gnueabihf)</text>
  <text x="340" y="252" text-anchor="middle" font-size="13">soft·softfp ↔ hard 는 섞으면 링커가 거부한다</text>
</svg>
```
그림 6 — 32-bit ARM의 세 가지 float ABI. softfp는 FPU 명령을 쓰지만 인자는 정수 레지스터로 주고받아 soft와 호환되고, hard는 VFP 레지스터로 직접 넘긴다.

- **soft**: FPU 명령을 아예 안 쓴다. float 연산은 libgcc의 `__aeabi_fadd` 같은 함수가 정수 연산으로 흉내 낸다. FPU 없는 Cortex-M0/M3, ARM9 시절의 기본.
- **softfp**: FPU 명령(`vadd.f32`)은 쓰되, 함수 경계에서는 soft와 똑같이 r0~r3으로 주고받는다. soft로 빌드된 라이브러리와 섞을 수 있다는 장점, 레지스터 복사(`vmov`) 비용이 단점.
- **hard**: float 인자를 s0~s15/d0~d7로 바로 넘긴다. 가장 빠르다. 이것이 `gnueabihf`의 `hf`이고, Debian/Ubuntu `armhf` 포트, Raspberry Pi OS 32-bit, 대부분의 Cortex-A 32-bit 보드 BSP가 이것이다.

Don이 Cortex-M4F 펌웨어에서 `-mfloat-abi=hard -mfpu=fpv4-sp-d16`을 썼다면 이미 같은 선택을 해 본 것이다. 차이는 **Linux에서는 이 선택이 "배포판 전체"에 걸린다**는 것이다. rootfs의 모든 라이브러리가 한 ABI로 빌드되어 있어서, 앱 하나만 다른 ABI로 빌드할 수 없다.

### 6.2 코드로 확인 — 세 ABI의 기계어와 섞었을 때의 실패

무엇을 확인하나: 같은 `fadd()`를 세 float ABI로 컴파일해 기계어와 ELF 속성을 비교하고, hard와 softfp를 섞어 링크해 본다.

```c
/* fadd.c: float 인자를 받는 함수 — 인자가 s0/s1(VFP)로 오나, r0/r1로 오나 */
float fadd(float a, float b) { return a + b; }
```

```c
/* fmain.c: fadd()를 부르는 쪽 */
#include <stdio.h>
float fadd(float a, float b);
int main(void) { printf("%.1f\n", (double)fadd(1.5f, 2.0f)); return 0; }
```

```sh
G=arm-linux-gnueabihf-gcc
echo "=== hard (default)"; $G -O2 -c $S/fadd.c -o fadd_hard.o && arm-linux-gnueabihf-objdump -d fadd_hard.o | grep -A4 "<fadd>:" | tail -3
echo "=== softfp"; $G -O2 -mfloat-abi=softfp -c $S/fadd.c -o fadd_softfp.o && arm-linux-gnueabihf-objdump -d fadd_softfp.o | grep -A6 "<fadd>:" | tail -5
echo "=== soft"; $G -O2 -mfloat-abi=soft -c $S/fadd.c -o fadd_soft.o && arm-linux-gnueabihf-objdump -d fadd_soft.o | grep -A4 "<fadd>:" | tail -3
for o in fadd_hard.o fadd_softfp.o fadd_soft.o; do echo "-- $o"; readelf -h $o | grep Flags; readelf -A $o | grep -E "VFP_args|FP_arch"; done
echo "=== link hard main + softfp fadd"; $G -O2 $S/fmain.c fadd_softfp.o -o mix 2>&1 | head -4
echo "=== whole program soft"; $G -O2 -mfloat-abi=soft $S/fmain.c -o allsoft 2>&1 | tail -4
$G -O2 $S/fmain.c fadd_hard.o -o ok && qemu-arm -L /usr/arm-linux-gnueabihf ./ok
```

```text
=== hard (default)
   0:	ee30 0a20 	vadd.f32	s0, s0, s1
   4:	4770      	bx	lr
   6:	bf00      	nop
=== softfp
   4:	ee07 1a90 	vmov	s15, r1
   8:	ee77 7a27 	vadd.f32	s15, s14, s15
   c:	ee17 0a90 	vmov	r0, s15
  10:	4770      	bx	lr
  12:	bf00      	nop
=== soft
   0:	b508      	push	{r3, lr}
   2:	f7ff fffe 	bl	0 <__aeabi_fadd>
   6:	bd08      	pop	{r3, pc}
-- fadd_hard.o
  Flags:                             0x5000000, Version5 EABI
  Tag_FP_arch: VFPv3-D16
  Tag_ABI_VFP_args: VFP registers
-- fadd_softfp.o
  Flags:                             0x5000000, Version5 EABI
  Tag_FP_arch: VFPv3-D16
-- fadd_soft.o
  Flags:                             0x5000000, Version5 EABI
=== link hard main + softfp fadd
/usr/bin/arm-linux-gnueabihf-ld.bfd: error: mix uses VFP register arguments, fadd_softfp.o does not
/usr/bin/arm-linux-gnueabihf-ld.bfd: failed to merge target specific data of file fadd_softfp.o
collect2: error: ld returned 1 exit status
=== whole program soft
/usr/arm-linux-gnueabihf/include/gnu/stubs.h:7:11: fatal error: gnu/stubs-soft.h: No such file or directory
    7 | # include <gnu/stubs-soft.h>
      |           ^~~~~~~~~~~~~~~~~~
compilation terminated.
3.5
```

(softfp 디스어셈블은 `tail`로 잘려 첫 줄 `0: vmov s14, r0`이 빠졌다. 따로 보면 `ee07 0a10 vmov s14, r0`이다.)

출력에서 볼 것:

- **기계어가 ABI를 그대로 보여 준다**: hard는 `vadd.f32 s0, s0, s1` 한 줄, softfp는 `vmov`로 r↔s 복사가 앞뒤로 붙고, soft는 `__aeabi_fadd` 호출이다.
- **ABI는 ELF 속성(`.ARM.attributes`)에 기록된다**: `Tag_ABI_VFP_args: VFP registers`가 hard의 표시다. 링커는 이 태그가 다른 오브젝트를 만나면 `uses VFP register arguments, fadd_softfp.o does not`으로 **거부**한다. 고마운 실패다 — 그냥 링크됐다면 실행 중 float 인자가 엉뚱한 레지스터에서 읽혀 쓰레기 값이 나왔을 것이다.
- **soft로 통째 빌드도 안 된다**: sysroot의 glibc가 hard로 빌드되어 있어서, glibc 헤더가 `gnu/stubs-soft.h`를 요구하다가 실패한다. "ABI는 툴체인+sysroot 단위로 정해진다"는 증거다. soft-float가 필요하면 `arm-linux-gnueabi`(armel) 툴체인과 armel rootfs를 통째로 써야 한다.

### 6.3 다른 아키텍처의 ABI 이름

| 아키텍처 | ABI 이름 | 뜻 | 확인 위치 |
|---|---|---|---|
| AArch64 | LP64 (AAPCS64) | `long`·포인터 64-bit, 인자 x0~x7 / v0~v7 | 64-bit ARM은 FPU·NEON 필수라 soft/hard 구분이 없다 |
| ARM 32 | EABI + hard/soft | 위 표 | `readelf -h`의 Flags, `readelf -A` |
| RISC-V 64 | lp64d | `long` 64-bit + double을 FP 레지스터로 | `file` 출력의 `double-float ABI`, 로더 이름 `ld-linux-riscv64-lp64d.so.1` |
| x86-64 | SysV AMD64 (`-mx32`, `-m32` multilib) | 인자 rdi, rsi, rdx … | §9.4의 x86_64 multilib 3벌 |

> 함정 메모: AArch64에는 hard/soft 구분이 없지만 그 대신 **커널은 FP/SIMD 레지스터를 쓰지 않도록** `-mgeneral-regs-only`로 빌드된다 (§11). 유저 공간의 ABI와 커널의 컴파일 규칙은 다른 이야기다.

---

## 7. -march / -mcpu / -mtune / -mfpu — 같은 triplet, 다른 코드

### 7.1 직관 — triplet은 "가족", -mcpu는 "개인"

`arm-linux-gnueabihf`는 "32-bit ARM Linux 가족" 전체를 뜻한다. 그 안에서도 Cortex-A7, A9, A15, A53(32-bit 모드)은 쓸 수 있는 명령어(NEON, 정수 나눗셈 `sdiv`, crypto)와 파이프라인 특성이 다르다. 그것을 고르는 것이 이 옵션들이다.

| 옵션 | 정하는 것 | 결과 바이너리가 도는 범위 | 예 |
|---|---|---|---|
| `-march=` | **쓸 수 있는 명령어 집합** (ISA + 확장) | 그 아키텍처 이상의 모든 코어 | `-march=armv8-a`, `-march=armv8.2-a+dotprod` |
| `-mtune=` | **명령어 스케줄링·비용 모델**만 (명령어 집합은 그대로) | `-march` 범위 그대로 | `-mtune=cortex-a53` |
| `-mcpu=` | `-march` + `-mtune`를 그 코어에 맞게 한 번에 | 그 코어(와 상위 호환 코어) | `-mcpu=cortex-a72` |
| `-mfpu=` (ARM 32만) | FPU/NEON 종류 | 그 FPU가 있는 코어 | `-mfpu=neon-vfpv4`, 기본 `auto` = `-mcpu`에서 추론 |

규칙 하나: **`-march`(또는 `-mcpu`)를 실제 보드보다 높게 잡으면 SIGILL**, 낮게 잡으면 느릴 뿐 돈다. `-mtune`은 틀려도 절대 죽지 않는다. 그래서 배포판은 낮은 `-march` + 흔한 코어 `-mtune`으로 빌드하고, 보드 전용 이미지(Yocto `DEFAULTTUNE`, Buildroot `BR2_cortex_a53`)는 `-mcpu`를 정확히 맞춘다.

### 7.2 코드로 확인 — `-mcpu`가 켜는 기능 매크로

무엇을 확인하나: `-mcpu`를 바꾸면 컴파일러가 정의하는 ACLE 기능 매크로(`__ARM_NEON`, `__ARM_FEATURE_*`)가 어떻게 바뀌는지 본다. 소스의 `#ifdef __ARM_NEON` 분기가 이것으로 결정된다.

```sh
G=arm-linux-gnueabihf-gcc
for f in "" "-mcpu=cortex-a7" "-mcpu=cortex-a7 -mfpu=neon-vfpv4"; do
  echo "[$G $f]"; echo | $G $f -dM -E - \
    | grep -E "__ARM_ARCH |__ARM_NEON |__ARM_FP |__ARM_PCS_VFP|__ARM_ARCH_PROFILE|__thumb2__ |__ARM_FEATURE_IDIV " | sort | tr "\n" ";"; echo
done
for f in "" "-mcpu=cortex-a53" "-mcpu=cortex-a72" "-mcpu=cortex-a78"; do
  echo "[gcc $f] $(echo | gcc $f -dM -E - | grep -oE "__ARM_ARCH [0-9]+|__ARM_FEATURE_(CRC32|ATOMICS|DOTPROD|CRYPTO|AES) 1" | tr "\n" ";")"
done
gcc -mcpu=cortex-a72 -Q --help=target | grep -E "^\s+-m(cpu|tune|arch)="
```

```text
[arm-linux-gnueabihf-gcc ]
#define __ARM_ARCH 7;#define __ARM_ARCH_PROFILE 65;#define __ARM_FP 12;#define __ARM_PCS_VFP 1;#define __thumb2__ 1;
[arm-linux-gnueabihf-gcc -mcpu=cortex-a7]
#define __ARM_ARCH 7;#define __ARM_ARCH_PROFILE 65;#define __ARM_FEATURE_IDIV 1;#define __ARM_FP 14;#define __ARM_NEON 1;#define __ARM_PCS_VFP 1;#define __thumb2__ 1;
[arm-linux-gnueabihf-gcc -mcpu=cortex-a7 -mfpu=neon-vfpv4]
#define __ARM_ARCH 7;#define __ARM_ARCH_PROFILE 65;#define __ARM_FEATURE_IDIV 1;#define __ARM_FP 14;#define __ARM_NEON 1;#define __ARM_PCS_VFP 1;#define __thumb2__ 1;
[gcc ] __ARM_ARCH 8;
[gcc -mcpu=cortex-a53] __ARM_ARCH 8;__ARM_FEATURE_CRC32 1;
[gcc -mcpu=cortex-a72] __ARM_ARCH 8;__ARM_FEATURE_CRC32 1;
[gcc -mcpu=cortex-a78] __ARM_FEATURE_ATOMICS 1;__ARM_FEATURE_DOTPROD 1;__ARM_ARCH 8;__ARM_FEATURE_CRC32 1;
  -march=                     		
  -mcpu=                      		cortex-a72
  -mtune=                     		
```

출력에서 볼 것:

- **ARM 32 기본값에는 NEON이 없다** (`__ARM_FP 12` = 단정·배정밀 VFP만, `__ARM_NEON` 없음). `-mcpu=cortex-a7`만 줘도 `__ARM_NEON`과 하드웨어 정수 나눗셈(`IDIV`)이 켜진다 — 최신 GCC는 `-mfpu=auto`가 기본이라 `-mcpu`에서 FPU를 추론한다. 예전 튜토리얼처럼 `-mfpu=neon-vfpv4`를 꼭 같이 쓸 필요는 없다(같이 써도 결과는 같다).
- **AArch64 기본값(armv8-a)에는 CRC32도 LSE atomics도 없다**. Cortex-A53/A72는 ARMv8.0이라 CRC32만, Cortex-A78(ARMv8.2)은 LSE atomics(`__ARM_FEATURE_ATOMICS`)와 dot product까지 켜진다. Jetson Orin의 Cortex-A78AE가 이쪽이다.

### 7.3 코드로 확인 — NEON 자동 벡터화 (ARM 32)

무엇을 확인하나: 정수 덧셈 루프와 float saxpy 루프가 옵션에 따라 NEON으로 벡터화되는지 `-fopt-info-vec-optimized`(컴파일러가 "이 루프를 벡터화했다"고 보고하는 옵션)로 확인한다.

```c
/* vec.c: 자동 벡터화 대상 루프 두 개 (int, float) */
#include <stdint.h>
void add_i32(int32_t *restrict d, const int32_t *restrict a,
             const int32_t *restrict b, int n)
{
    for (int i = 0; i < n; i++) d[i] = a[i] + b[i];
}
void saxpy(float *restrict y, const float *restrict x, float k, int n)
{
    for (int i = 0; i < n; i++) y[i] = k * x[i] + y[i];
}
```

```sh
G=arm-linux-gnueabihf-gcc
for f in "-O2" "-O3" "-O3 -mcpu=cortex-a7 -mfpu=vfpv4" "-O3 -mcpu=cortex-a7" "-O3 -mcpu=cortex-a7 -ffast-math"; do
  echo "[$f]"; $G $f -fopt-info-vec-optimized -c $S/vec.c -o v.o 2>&1 | sed "s|.*/vec.c|  vec.c|"
done
```

```text
[-O2]
[-O3]
[-O3 -mcpu=cortex-a7 -mfpu=vfpv4]
[-O3 -mcpu=cortex-a7]
  vec.c:6:23: optimized: loop vectorized using 16 byte vectors
[-O3 -mcpu=cortex-a7 -ffast-math]
  vec.c:6:23: optimized: loop vectorized using 16 byte vectors
  vec.c:10:23: optimized: loop vectorized using 16 byte vectors
```

```sh
$G -O3 -mcpu=cortex-a7 -mfpu=neon-vfpv4 -ffast-math -c $S/vec.c -o v.o
arm-linux-gnueabihf-objdump -d --no-show-raw-insn v.o | grep -E "^\s+[0-9a-f]+:\s+v" | head -20
```

```text
  20:	vld1.32	{d16-d17}, [ip]!
  24:	vld1.32	{d18-d19}, [r3]!
  2a:	vadd.i32	q8, q8, q9
  2e:	vst1.32	{d16-d17}, [lr]!
  96:	vdup.32	q10, d0[0]
  a0:	vld1.32	{d16-d17}, [r3]
  a4:	vld1.32	{d18-d19}, [ip]!
  a8:	vfma.f32	q8, q9, q10
  ae:	vst1.32	{d16-d17}, [r3]!
  cc:	vldr	s15, [r4]
  d0:	vldr	s14, [r5]
  d6:	vfma.f32	s15, s14, s0
  da:	vstr	s15, [r4]
  ec:	vldr	s15, [r4]
  f0:	vldr	s14, [r3]
  f6:	vfma.f32	s15, s0, s14
  fa:	vstr	s15, [r4]
 10a:	vldr	s15, [r0]
 10e:	vldr	s14, [r3]
 112:	vfma.f32	s15, s0, s14
```

출력에서 볼 것:

- 기본 armhf(`-O2`, `-O3`)는 NEON이 없어서 **아무것도 벡터화하지 않는다**. `-mfpu=vfpv4`처럼 NEON 없는 FPU를 명시해도 마찬가지다.
- `-mcpu=cortex-a7`이면 int 루프가 `vld1.32`/`vadd.i32 q8`(128-bit, 4개씩)로 바뀐다. **float 루프는 `-ffast-math`를 줘야만** 벡터화된다. ARMv7 NEON의 float 연산은 IEEE 754를 완전히 따르지 않기 때문(denormal을 0으로 처리)에, GCC는 "정확도를 포기해도 된다"(`-funsafe-math-optimizations`, `-ffast-math`에 포함)는 허락이 있어야 NEON float를 쓴다.
- 벡터 루프(`q` 레지스터, 4개씩) 뒤에 남은 1~3개를 처리하는 스칼라 꼬리(`vldr s15`, `vfma.f32 s15`)가 붙는다. Don이 DMA 버스트 뒤에 나머지 바이트를 따로 처리하던 것과 같은 구조다.

### 7.4 코드로 확인 — AArch64에서는

무엇을 확인하나: 같은 `vec.c`를 AArch64 native gcc로, `-O` 레벨과 `-mcpu`를 바꿔 가며 컴파일하고 saxpy 안의 벡터 FMA(`fmla v.4s`) 개수와 `.text` 크기를 센다.

```sh
for f in "-O1" "-O2" "-O3" "-O3 -mcpu=cortex-a53" "-O3 -mcpu=cortex-a72"; do
  gcc $f -c $S/vec.c -o a.o
  n=$(objdump -d a.o | grep -A80 "<saxpy>:" | grep -cE "fmla\s+v[0-9]+\.4s")
  sz=$(size -A a.o | awk "/^.text/{print \$2}")
  echo "[$f] saxpy fmla.4s=$n  .text=$sz bytes"
done
gcc -O3 -c $S/vec.c -o a.o; objdump -d --no-show-raw-insn a.o | grep -A40 "<saxpy>:" | grep -E "ldr\s+q|fmla|str\s+q" | head -4
gcc -O3 -mcpu=cortex-a53 -S $S/vec.c -o a53.s; gcc -O3 -mcpu=cortex-a72 -S $S/vec.c -o a72.s
diff <(sed -n "/^saxpy:/,/size/p" a53.s) <(sed -n "/^saxpy:/,/size/p" a72.s) | head -30
```

```text
[-O1] saxpy fmla.4s=0  .text=100 bytes
[-O2] saxpy fmla.4s=0  .text=96 bytes
[-O3] saxpy fmla.4s=1  .text=336 bytes
[-O3 -mcpu=cortex-a53] saxpy fmla.4s=1  .text=348 bytes
[-O3 -mcpu=cortex-a72] saxpy fmla.4s=1  .text=356 bytes
  c8:	ldr	q5, [x0, x3]
  cc:	ldr	q6, [x1, x3]
  d0:	fmla	v5.4s, v6.4s, v0.s[0]
  d4:	str	q5, [x0, x3]
5c5
< 	ble	.L9
---
> 	ble	.L17
8c8
< 	bls	.L14
---
> 	bls	.L24
13c13
< .L12:
---
> .L20:
20,31c20,22
< 	bne	.L12
< 	and	w4, w2, -4
< 	cmp	w2, w4
< 	beq	.L9
< .L11:
< 	uxtw	x3, w4
< 	lsl	x6, x3, 2
< 	ldr	s4, [x1, x3, lsl 2]
< 	ldr	s3, [x0, x3, lsl 2]
< 	fmadd	s3, s4, s0, s3
< 	str	s3, [x0, x3, lsl 2]
< 	add	w3, w4, 1
---
> 	bne	.L20
> 	and	w3, w2, -4
> 	mov	w4, w3
33,44c24,50
```

(diff는 30줄에서 잘랐다.)

출력에서 볼 것:

- AArch64에서는 **`-ffast-math` 없이도 float가 벡터화된다** (`fmla v5.4s`). AArch64 Advanced SIMD는 IEEE 754를 따르기 때문이다. ARM 32와의 큰 차이다.
- `-O2`(이 GCC에서는 아주 보수적인 "very cheap" 벡터화 비용 모델)는 반복 횟수 `n`을 모르는 이 루프를 벡터화하지 않았다. `-O3`에서 코드 크기가 96 → 336 바이트로 3.5배가 됐다 — 벡터 본체 + 스칼라 꼬리 + 겹침 검사. 플래시가 작은 보드에서는 이 크기도 비용이다.
- `-mcpu=cortex-a53`(in-order 코어)과 `cortex-a72`(out-of-order 코어)는 **같은 명령어 집합에서 순서·꼬리 처리 방식만** 다르게 골랐다. 이것이 `-mtune` 효과다.

### 7.5 코드로 확인 — `-march`를 너무 높게 잡으면: SIGILL

무엇을 확인하나: ARMv8.1의 LSE atomic(`ldadd`)을 쓰도록 빌드한 바이너리를 qemu로 **Cortex-A53/A72(ARMv8.0)** 흉내를 내며 돌려 본다. Jetson(A78)용으로 빌드한 바이너리를 i.MX8M(A53)에 올리는 실수의 재현이다.

```c
/* lse.c: -march를 타깃보다 높게 잡으면? (ARMv8.1 LSE atomic → Cortex-A53에는 없음) */
#include <stdio.h>
static long counter;
int main(void)
{
    long old = __atomic_fetch_add(&counter, 5, __ATOMIC_SEQ_CST);
    printf("atomic ok: old=%ld new=%ld\n", old, counter);
    return 0;
}
```

```sh
gcc -O2 -Wall -Wextra -static $S/lse.c -o lse_default
gcc -O2 -Wall -Wextra -static -march=armv8.1-a $S/lse.c -o lse_v81
objdump -d lse_default | grep -A30 "<main>:" | grep -E "bl\s" | head -3
objdump -d lse_default | grep -A12 "<__aarch64_ldadd8_acq_rel>:" | head -14
for c in cortex-a53 cortex-a72 cortex-a76; do echo -n "$c: "; qemu-aarch64 -cpu $c ./lse_v81 2>&1 | head -1; done
for c in cortex-a53 cortex-a72; do echo -n "$c (default build): "; qemu-aarch64 -cpu $c ./lse_default; done
```

```text
  4004f8:	940000a2 	bl	400780 <__aarch64_ldadd8_acq_rel>
  400510:	94002f74 	bl	40c2e0 <___printf_chk>
0000000000400780 <__aarch64_ldadd8_acq_rel>:
  400780:	b0000510 	adrp	x16, 4a1000 <_IO_2_1_stderr_+0x78>
  400784:	39644210 	ldrb	w16, [x16, #2320]
  400788:	34000070 	cbz	w16, 400794 <__aarch64_ldadd8_acq_rel+0x14>
  40078c:	f8e00020 	ldaddal	x0, x0, [x1]
  400790:	d65f03c0 	ret
  400794:	aa0003f0 	mov	x16, x0
  400798:	c85ffc20 	ldaxr	x0, [x1]
  40079c:	8b100011 	add	x17, x0, x16
  4007a0:	c80ffc31 	stlxr	w15, x17, [x1]
  4007a4:	35ffffaf 	cbnz	w15, 400798 <__aarch64_ldadd8_acq_rel+0x18>
  4007a8:	d65f03c0 	ret
  4007ac:	d503201f 	nop
cortex-a53: qemu: uncaught target signal 4 (Illegal instruction) - core dumped
cortex-a72: qemu: uncaught target signal 4 (Illegal instruction) - core dumped
cortex-a76: atomic ok: old=0 new=5
cortex-a53 (default build): atomic ok: old=0 new=5
cortex-a72 (default build): atomic ok: old=0 new=5
```

출력에서 볼 것:

- `-march=armv8.1-a` 빌드는 `ldaddal`을 직접 박아서, ARMv8.0인 A53·A72에서 **SIGILL**(signal 4)로 죽고 ARMv8.2인 A76에서만 돈다. 보드에서는 `Illegal instruction`으로 보인다.
- 기본 빌드는 `__aarch64_ldadd8_acq_rel`이라는 **outline atomics** 도우미를 부른다. 시작 시 CPU 기능(HWCAP)을 보고 플래그를 세워 두고, LSE가 있으면 `ldaddal`, 없으면 `ldaxr/stlxr` 루프(Don이 아는 LDREX/STREX 방식)로 간다. 런타임 디스패치로 "낮은 `-march`의 호환성 + 높은 코어의 성능"을 둘 다 얻는 장치다.

### 7.6 측정 — native vs qemu-user, 옵션별 실행 시간

무엇을 확인하나: 같은 saxpy 벤치마크를 native(-O0/-O2/-O3/-mcpu)와 qemu-user(armhf, x86_64, riscv64)로 돌려 시간을 잰다. "qemu로 기능 테스트는 되지만 성능 측정은 안 된다"를 숫자로 본다.

```c
/* bench.c: 같은 C 코드를 아키텍처·옵션별로 빌드해 시간을 잰다 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define N 4096
#define REPS 20000
static float x[N], y[N];

__attribute__((noinline))
static void saxpy(float *restrict yy, const float *restrict xx, float k, int n)
{
    for (int i = 0; i < n; i++) yy[i] = k * xx[i] + yy[i];
}

int main(void)
{
    struct timespec t0, t1;
    for (int i = 0; i < N; i++) { x[i] = (float)(i % 7); y[i] = 0.0f; }
    if (clock_gettime(CLOCK_MONOTONIC, &t0) < 0) { perror("clock_gettime"); exit(1); }
    for (int r = 0; r < REPS; r++) saxpy(y, x, 1e-6f, N);
    if (clock_gettime(CLOCK_MONOTONIC, &t1) < 0) { perror("clock_gettime"); exit(1); }
    double ms = (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
    printf("%8.1f ms  (%.3f ns/elem)  check=%.3f\n", ms, ms * 1e6 / ((double)N * REPS), (double)y[5]);
    return 0;
}
```

```sh
#!/bin/bash
# runbench.sh: bench.c를 여러 툴체인/옵션으로 빌드하고 각 3회 실행해 중앙값을 보인다
S=/Users/donh/.elinux_scratch/B1; mkdir -p /tmp/B1/bench; cd /tmp/B1/bench || exit 1
build() { "$@" -Wall -Wextra "$S/bench.c" -o "$OUT" || exit 1; }
OUT=n_O0  build gcc -O0
OUT=n_O2  build gcc -O2
OUT=n_O3  build gcc -O3
OUT=n_O3_a72 build gcc -O3 -mcpu=cortex-a72
OUT=a_O2  build arm-linux-gnueabihf-gcc -O2
OUT=a_O3neon build arm-linux-gnueabihf-gcc -O3 -mcpu=cortex-a7 -mfpu=neon-vfpv4 -ffast-math
OUT=r_O3  build riscv64-linux-gnu-gcc -O3
OUT=x_O3  build x86_64-linux-gnu-gcc -O3
run() { local name=$1; shift; for i in 1 2 3; do "$@" 2>&1; done | sort -n | sed -n 2p | sed "s/^/$name: /"; }
run "native aarch64 -O0       " ./n_O0
run "native aarch64 -O2       " ./n_O2
run "native aarch64 -O3       " ./n_O3
run "native aarch64 -O3 a72   " ./n_O3_a72
run "qemu-arm armhf -O2       " qemu-arm -L /usr/arm-linux-gnueabihf ./a_O2
run "qemu-arm armhf -O3 NEON  " qemu-arm -L /usr/arm-linux-gnueabihf ./a_O3neon
run "qemu-riscv64 -O3         " qemu-riscv64 -L /usr/riscv64-linux-gnu ./r_O3
run "qemu-x86_64 -O3          " qemu-x86_64 -L /usr/x86_64-linux-gnu ./x_O3
```

```text
native aarch64 -O0       :    202.8 ms  (2.476 ns/elem)  check=0.100
native aarch64 -O2       :      6.4 ms  (0.078 ns/elem)  check=0.100
native aarch64 -O3       :      6.4 ms  (0.079 ns/elem)  check=0.100
native aarch64 -O3 a72   :      6.4 ms  (0.078 ns/elem)  check=0.100
qemu-arm armhf -O2       :    541.1 ms  (6.605 ns/elem)  check=0.100
qemu-arm armhf -O3 NEON  :    375.1 ms  (4.579 ns/elem)  check=0.100
qemu-riscv64 -O3         :   2059.7 ms  (25.143 ns/elem)  check=0.100
qemu-x86_64 -O3          :    475.3 ms  (5.802 ns/elem)  check=0.100
```

```sh
cd /tmp/B1/bench; objdump -d n_O2 | grep -E "^[0-9a-f]+ <saxpy"; objdump -d n_O2 | grep -cE "fmla\s+v[0-9]+\.4s"
```

```text
0000000000000b40 <saxpy.constprop.0>:
1
```

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="20" text-anchor="middle" font-size="14" font-weight="bold">saxpy 4096 x 20000회 실행 시간 (ms, 로그 스케일, 3회 중앙값)</text>
  <line x1="230.0" y1="34" x2="230.0" y2="296" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="230.0" y="312" text-anchor="middle" font-size="12">1</text>
  <line x1="325.0" y1="34" x2="325.0" y2="296" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="325.0" y="312" text-anchor="middle" font-size="12">10</text>
  <line x1="420.0" y1="34" x2="420.0" y2="296" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="420.0" y="312" text-anchor="middle" font-size="12">100</text>
  <line x1="515.0" y1="34" x2="515.0" y2="296" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="515.0" y="312" text-anchor="middle" font-size="12">1000</text>
  <line x1="610.0" y1="34" x2="610.0" y2="296" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="610.0" y="312" text-anchor="middle" font-size="12">10000</text>
  <text x="222" y="58" text-anchor="end" font-size="12">native aarch64 -O0</text>
  <rect x="230" y="44" width="219.2" height="20" fill="#888" fill-opacity="0.75" stroke="#888"/>
  <text x="455.2" y="58" font-size="12">202.8</text>
  <text x="222" y="90" text-anchor="end" font-size="12">native aarch64 -O2</text>
  <rect x="230" y="76" width="76.6" height="20" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
  <text x="312.6" y="90" font-size="12">6.4</text>
  <text x="222" y="122" text-anchor="end" font-size="12">native aarch64 -O3</text>
  <rect x="230" y="108" width="76.6" height="20" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
  <text x="312.6" y="122" font-size="12">6.4</text>
  <text x="222" y="154" text-anchor="end" font-size="12">native -O3 -mcpu=cortex-a72</text>
  <rect x="230" y="140" width="76.6" height="20" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
  <text x="312.6" y="154" font-size="12">6.4</text>
  <text x="222" y="186" text-anchor="end" font-size="12">qemu-arm armhf -O2</text>
  <rect x="230" y="172" width="259.7" height="20" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
  <text x="495.7" y="186" font-size="12">541.1</text>
  <text x="222" y="218" text-anchor="end" font-size="12">qemu-arm armhf -O3 NEON</text>
  <rect x="230" y="204" width="244.5" height="20" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
  <text x="480.5" y="218" font-size="12">375.1</text>
  <text x="222" y="250" text-anchor="end" font-size="12">qemu-x86_64 -O3</text>
  <rect x="230" y="236" width="254.3" height="20" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
  <text x="490.3" y="250" font-size="12">475.3</text>
  <text x="222" y="282" text-anchor="end" font-size="12">qemu-riscv64 -O3 (rv64gc)</text>
  <rect x="230" y="268" width="314.8" height="20" fill="#d0564a" fill-opacity="0.75" stroke="#d0564a"/>
  <text x="550.8" y="282" font-size="12">2059.7</text>
  <line x1="230" y1="34" x2="230" y2="296" stroke="currentColor"/>
  <text x="420.0" y="330" text-anchor="middle" font-size="12">ms (작을수록 빠름) — Apple Silicon 위 Lima VM, qemu 10.2.1</text>
</svg>
```
그림 7 — 직접 측정한 실행 시간. native 벡터 코드(초록)와 qemu-user(주황·파랑·빨강) 사이에 60~320배 차이가 난다. 로그 축이다.

출력에서 볼 것:

- **native -O2 = -O3**: 앞 절에서 -O2는 saxpy를 벡터화하지 않았는데 여기서는 같은 속도다. `saxpy.constprop.0`이 답이다 — 이 프로그램에서는 `n`이 항상 4096이라 GCC가 상수를 넣은 복제본을 만들었고, 반복 횟수를 알게 되자 -O2에서도 벡터화했다(`fmla .4s` 1개). "최적화 결과는 호출 맥락에 따라 달라진다", 그래서 **추측하지 말고 objdump로 확인**해야 한다.
- **-O0는 32배 느리다**. 디버그 빌드를 보드에 올려 놓고 "보드가 느리다"고 결론 내리는 실수가 흔하다.
- **qemu-user는 native보다 60~320배 느리다**. 같은 qemu-arm 안에서는 NEON 버전이 1.4배 빨랐지만, 이 비율도 실제 Cortex-A7과는 무관하다 (qemu가 NEON 명령을 호스트 명령으로 번역하는 비용이 섞여 있다). riscv64는 기본 `rv64gc`에 벡터 확장(RVV)이 없어 스칼라 코드라 가장 느렸다. **qemu는 "돌아가나?"를 보는 도구지, "얼마나 빠른가?"를 재는 도구가 아니다.** 성능은 실물 보드에서 잰다 (M, N 모듈).

---

## 8. Clang — 컴파일러 하나로 모든 타깃

### 8.1 GCC와 Clang의 구조 차이

GCC는 **configure할 때 target이 하나로 고정**된다. 그래서 `arm-linux-gnueabihf-gcc`, `riscv64-linux-gnu-gcc`가 각각 다른 실행 파일이다. Clang(LLVM)은 **모든 백엔드를 한 바이너리에 넣고** 실행할 때 `--target=`으로 고른다. 대신 Clang은 C 라이브러리·crt 파일·libgcc를 직접 갖고 있지 않아서, **sysroot와 (보통은) GCC 크로스 설치본을 빌려 쓴다**.

| 항목 | GCC | Clang/LLVM |
|---|---|---|
| target 선택 | 툴체인 이름(접두어)으로 | `--target=<triple>` 옵션으로 |
| 설치 | target마다 한 벌 | 하나로 전부 (`clang -print-targets`) |
| 링커 | GNU ld (bfd), gold | `ld.lld` (`-fuse-ld=lld`), GNU ld도 가능 |
| libc·crt·런타임 | 툴체인에 포함 | sysroot + GCC 설치본에서 빌림 (또는 compiler-rt) |
| 커널 빌드 | `CROSS_COMPILE=` | `LLVM=1` (+ `ARCH=`) |
| 강점 | 커널·U-Boot·벤더 SDK의 기준 컴파일러 | 진단 메시지, sanitizer, clang-tidy, 한 바이너리로 다중 타깃 CI |

### 8.2 코드로 확인 — clang 하나로 세 타깃

무엇을 확인하나: 같은 `clang`으로 armhf·riscv64·x86_64 hello를 만들고, 내부적으로 어떤 triple·링커·헤더 경로를 쓰는지 본다.

```sh
clang -print-targets | head -3; clang -print-targets | grep -cE "^\s+\S+\s+-"
clang -print-targets | grep -E "^\s+(aarch64|arm|riscv64|x86-64|thumb) "
for t in arm-linux-gnueabihf riscv64-linux-gnu x86_64-linux-gnu; do
  clang --target=$t -O2 -Wall -Wextra -fuse-ld=lld $S/hello.c -o clang-$t && file clang-$t | cut -c1-110
done
QEMU_LD_PREFIX=/usr/riscv64-linux-gnu ./clang-riscv64-linux-gnu
clang --target=arm-linux-gnueabihf -O2 -fuse-ld=lld -### $S/hello.c -o x 2>&1 \
  | grep -oE "\"-triple\" \"[^\"]+\"|\"-target-cpu\" \"[^\"]+\"|\"-mfloat-abi\" \"[^\"]+\"|ld.lld|\"-internal-externc-isystem\" \"[^\"]+\"|\"-dynamic-linker\" \"[^\"]+\"" | sort -u
readelf -p .comment clang-arm-linux-gnueabihf | grep -E "clang|GCC|Linker"
```

```text
  Registered Targets:
    aarch64     - AArch64 (little endian)
47
    aarch64     - AArch64 (little endian)
    arm         - ARM
    riscv64     - 64-bit RISC-V
    thumb       - Thumb
    x86-64      - 64-bit X86: EM64T and AMD64
clang-arm-linux-gnueabihf: ELF 32-bit LSB pie executable, ARM, EABI5 version 1 (SYSV), dynamically linked, int
clang-riscv64-linux-gnu: ELF 64-bit LSB pie executable, UCB RISC-V, RVC, double-float ABI, version 1 (SYSV), d
clang-x86_64-linux-gnu: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV), dynamically linked, interpret
hello: built for riscv, sizeof(long)=8, uname -m says riscv64
"-dynamic-linker" "/lib/ld-linux-armhf.so.3"
"-internal-externc-isystem" "/include"
"-internal-externc-isystem" "/usr/include"
"-mfloat-abi" "hard"
"-target-cpu" "generic"
"-triple" "armv7-unknown-linux-gnueabihf"
ld.lld
  [     1]  Linker: Ubuntu LLD 21.1.8
  [    1b]  GCC: (Ubuntu 15.2.0-16ubuntu1) 15.2.0
  [    41]  Ubuntu clang version 21.1.8 (6ubuntu1)
```

```sh
clang --target=arm-linux-gnueabihf -v -c $S/hello.c -o /dev/null 2>&1 \
  | sed -n "/Selected GCC/p;/Candidate multilib/p;/search starts here:/,/End of search/p"
echo ===; clang --target=arm-linux-gnueabihf --sysroot=/nonexistent -c $S/hello.c -o /dev/null 2>&1 | head -3
```

```text
Selected GCC installation: /usr/lib/gcc-cross/arm-linux-gnueabihf/15
Candidate multilib: .;@m32
#include "..." search starts here:
#include <...> search starts here:
 /usr/lib/llvm-21/lib/clang/21/include
 /usr/local/include
 /usr/lib/gcc-cross/arm-linux-gnueabihf/15/../../../../arm-linux-gnueabihf/include
 /usr/include
End of search list.
===
/Users/donh/.elinux_scratch/B1/hello.c:2:10: fatal error: 'stdio.h' file not found
    2 | #include <stdio.h>
      |          ^~~~~~~~~
```

출력에서 볼 것:

- clang 한 바이너리에 47개 타깃이 들어 있다. `--target`만 바꿔 세 아키텍처 바이너리를 만들었고, riscv64 결과물이 qemu에서 돌았다.
- 내부 triple은 `armv7-unknown-linux-gnueabihf`로 정규화됐고, float ABI(hard)와 동적 로더 경로도 triple에서 유도됐다.
- **`Selected GCC installation`**: clang이 GCC 크로스 설치본을 찾아 crt 파일·libgcc·헤더 경로를 빌렸다. `.comment` 섹션에 `GCC: (Ubuntu 15.2.0…)`이 섞여 있는 것이 그 흔적이다(crt 오브젝트가 GCC로 빌드된 것). 링커는 `LLD 21.1.8`이다.
- `--sysroot=/nonexistent`를 주자 `stdio.h`를 못 찾았다 — **clang에게 sysroot는 필수 입력**이다. Yocto/Buildroot SDK에서 clang을 쓸 때 `--sysroot=$SDKTARGETSYSROOT`를 반드시 넘기는 이유다.

---

## 9. 커널 헤더·glibc 버전·multilib — sysroot 안의 "버전" 문제

### 9.1 커널 헤더(UAPI)는 무엇이고 왜 툴체인에 있나

유저 프로그램은 커널과 **syscall 번호, ioctl 번호, 구조체 레이아웃**으로 대화한다(A1, A3). 이 정의가 커널 소스의 `include/uapi/`와 `arch/<arch>/include/uapi/`에 있고, `make headers_install`로 뽑아낸 것이 **커널 헤더(UAPI headers)**다. Debian/Ubuntu에서는 `linux-libc-dev`(native), `linux-libc-dev-armhf-cross`(크로스) 패키지다. glibc는 이 헤더를 보고 빌드되고, 앱도 `#include <linux/i2c-dev.h>`, `<linux/spi/spidev.h>`, `<linux/videodev2.h>`로 직접 쓴다 — 센서 키트 코드의 핵심 헤더들이다.

규칙은 하나다: **툴체인의 커널 헤더 버전 ≤ 보드에서 실제로 도는 커널 버전**. 커널은 옛 ABI를 유지하므로(새 syscall·필드는 추가만 한다) 옛 헤더로 빌드한 앱은 새 커널에서 돈다. 반대로 헤더가 더 새로우면 앱이 커널에 없는 syscall이나 ioctl을 써서 `ENOSYS`, `EINVAL`, `ENOTTY`가 난다. 벤더 BSP 커널이 5.10인데 최신 배포판 툴체인(헤더 7.0)으로 빌드하면 이 위험이 있다. 그래서 Yocto/Buildroot는 **보드 커널과 같은 버전의 헤더**로 툴체인을 만든다(Buildroot의 "Kernel Headers" 옵션).

### 9.2 코드로 확인 — 헤더 버전과 syscall 번호

무엇을 확인하나: 각 sysroot의 `LINUX_VERSION_CODE`와 syscall 번호를 출력해, 아키텍처마다 syscall 번호가 다르다는 것과 "헤더 버전 ≠ 실행 커널 버전"을 본다.

```c
/* khdr.c: 툴체인에 들어 있는 "커널 헤더"의 버전과 syscall 번호를 출력 */
#include <stdio.h>
#include <linux/version.h>
#include <sys/syscall.h>
#include <sys/utsname.h>

int main(void)
{
    struct utsname u;
    if (uname(&u) < 0) { perror("uname"); return 1; }
    printf("headers: %d.%d.%d  running kernel: %s  __NR_openat=%d  __NR_getpid=%d\n",
           LINUX_VERSION_CODE >> 16, (LINUX_VERSION_CODE >> 8) & 0xff,
           LINUX_VERSION_CODE & 0xff, u.release, (int)__NR_openat, (int)__NR_getpid);
    return 0;
}
```

```sh
gcc -Wall -Wextra $S/khdr.c -o k-arm64 && ./k-arm64
arm-linux-gnueabihf-gcc -Wall -Wextra $S/khdr.c -o k-armhf && qemu-arm -L /usr/arm-linux-gnueabihf ./k-armhf
riscv64-linux-gnu-gcc -Wall -Wextra $S/khdr.c -o k-rv && qemu-riscv64 -L /usr/riscv64-linux-gnu ./k-rv
x86_64-linux-gnu-gcc -Wall -Wextra $S/khdr.c -o k-x86 && qemu-x86_64 -L /usr/x86_64-linux-gnu ./k-x86
dpkg -S /usr/arm-linux-gnueabihf/include/linux/version.h /usr/include/linux/version.h
```

```text
headers: 7.0.14  running kernel: 7.0.0-34-generic  __NR_openat=56  __NR_getpid=172
headers: 7.0.0  running kernel: 7.0.0-34-generic  __NR_openat=322  __NR_getpid=20
headers: 7.0.0  running kernel: 7.0.0-34-generic  __NR_openat=56  __NR_getpid=172
headers: 7.0.0  running kernel: 7.0.0-34-generic  __NR_openat=257  __NR_getpid=39
linux-libc-dev-armhf-cross: /usr/arm-linux-gnueabihf/include/linux/version.h
linux-libc-dev:arm64: /usr/include/linux/version.h
```

출력에서 볼 것:

- **syscall 번호는 아키텍처마다 다르다**: `openat`이 arm64·riscv64는 56(둘 다 새 "generic" 번호표 사용), 32-bit ARM은 322, x86_64는 257. 그래서 커널 헤더는 반드시 **타깃 아키텍처용**이어야 한다. 호스트 헤더로 빌드한 ARM 바이너리는 엉뚱한 syscall을 부른다.
- **헤더 버전은 패키지마다 다르다**: native `linux-libc-dev`는 7.0.14, 크로스 패키지는 7.0.0. 둘 다 실행 커널(7.0.0-34)과 별개로 설치된 파일이다. 보드에서는 "툴체인 헤더 버전"과 "보드 커널 `uname -r`"을 따로 확인해야 한다.
- qemu-user 아래의 `running kernel`은 호스트 커널이다 — qemu-user는 커널을 흉내 내지 않는다(§5).

### 9.3 glibc 버전 — "GLIBC_2.xx not found"의 정체

무엇을 확인하나: 우리 armhf hello가 요구하는 glibc 심볼 버전과 sysroot의 glibc 버전을 본다. 보드에서 가장 자주 보는 에러 `version 'GLIBC_2.38' not found`의 원인이다.

```sh
arm-linux-gnueabihf-objdump -T hello-armhf | grep -oE "GLIBC_[0-9.]+" | sort -uV
qemu-arm -L /usr/arm-linux-gnueabihf /usr/arm-linux-gnueabihf/lib/libc.so.6 | head -1
```

```text
GLIBC_2.4
GLIBC_2.34
GNU C Library (Ubuntu GLIBC 2.43-2ubuntu2) stable release version 2.43.
```

출력에서 볼 것: sysroot의 glibc는 2.43인데, 바이너리가 실제로 요구하는 심볼 버전은 최대 `GLIBC_2.34`다 (`__libc_start_main@GLIBC_2.34`). 보드 rootfs의 glibc가 2.34 이상이면 돌고, 2.31이면 동적 로더가 `version 'GLIBC_2.34' not found`로 거부한다. 규칙: **sysroot의 glibc ≤ 보드의 glibc**. 그래서 "최신 Ubuntu 툴체인으로 빌드해서 옛 BSP rootfs에 올리는 것"이 위험하고, 팀은 보드 rootfs와 짝이 맞는 SDK(Yocto `populate_sdk`, Buildroot `make sdk`)를 쓴다 (B2, B5). `libc.so.6`을 실행 파일처럼 돌리면 버전을 알려 주는 것도 기억해 둘 요령이다.

### 9.4 multilib vs multiarch

- **multilib**(GCC 개념): 컴파일러 **하나**가 옵션 조합별로 미리 빌드된 라이브러리 **여러 벌**을 갖고, `-m` 옵션에 맞는 디렉터리를 골라 링크한다. `arm-none-eabi`는 Cortex-M0(v6-m, soft)부터 M4F(v7e-m+fp, hard), Cortex-A까지 39벌이다. x86_64 gcc는 64-bit, `-m32`, `-mx32` 3벌.
- **multiarch**(Debian 개념): 한 시스템에 **여러 아키텍처의 라이브러리를 나란히** 설치하는 디렉터리 규칙(`/usr/lib/aarch64-linux-gnu`, `/usr/lib/arm-linux-gnueabihf`). 이 VM의 크로스 패키지 레이아웃(`/usr/arm-linux-gnueabihf/`)도 같은 이름 규칙을 쓴다.

무엇을 확인하나: arm-none-eabi가 `-mcpu`·`-mfloat-abi` 조합에 따라 어느 newlib 디렉터리를 고르는지 본다.

```sh
arm-none-eabi-gcc -print-multi-lib | head -20
arm-none-eabi-gcc -mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16 -print-multi-directory
arm-none-eabi-gcc -mcpu=cortex-m0 -print-multi-directory
arm-none-eabi-gcc -mcpu=cortex-a7 -print-multi-directory
x86_64-linux-gnu-gcc -print-multi-lib
```

```text
.;
arm/v5te/softfp;@marm@march=armv5te+fp@mfloat-abi=softfp
arm/v5te/hard;@marm@march=armv5te+fp@mfloat-abi=hard
thumb/nofp;@mthumb@mfloat-abi=soft
thumb/v7/nofp;@mthumb@march=armv7@mfloat-abi=soft
thumb/v7+fp/softfp;@mthumb@march=armv7+fp@mfloat-abi=softfp
thumb/v7+fp/hard;@mthumb@march=armv7+fp@mfloat-abi=hard
thumb/v7-r+fp.sp/softfp;@mthumb@march=armv7-r+fp.sp@mfloat-abi=softfp
thumb/v7-r+fp.sp/hard;@mthumb@march=armv7-r+fp.sp@mfloat-abi=hard
thumb/v7-a/nofp;@mthumb@march=armv7-a@mfloat-abi=soft
thumb/v7-a+fp/softfp;@mthumb@march=armv7-a+fp@mfloat-abi=softfp
thumb/v7-a+fp/hard;@mthumb@march=armv7-a+fp@mfloat-abi=hard
thumb/v7-a+simd/softfp;@mthumb@march=armv7-a+simd@mfloat-abi=softfp
thumb/v7-a+simd/hard;@mthumb@march=armv7-a+simd@mfloat-abi=hard
thumb/v7ve+simd/softfp;@mthumb@march=armv7ve+simd@mfloat-abi=softfp
thumb/v7ve+simd/hard;@mthumb@march=armv7ve+simd@mfloat-abi=hard
thumb/v8-a/nofp;@mthumb@march=armv8-a@mfloat-abi=soft
thumb/v8-a+simd/softfp;@mthumb@march=armv8-a+simd@mfloat-abi=softfp
thumb/v8-a+simd/hard;@mthumb@march=armv8-a+simd@mfloat-abi=hard
thumb/v6-m/nofp;@mthumb@march=armv6s-m@mfloat-abi=soft
thumb/v7e-m+fp/hard
thumb/v6-m/nofp
thumb/v7-a/nofp
.;
32;@m32
x32;@mx32
```

출력에서 볼 것: Cortex-M4F + hard는 `thumb/v7e-m+fp/hard`의 newlib을, M0는 `thumb/v6-m/nofp`를 쓴다. Don이 펌웨어 Makefile에서 `-mcpu`와 `-mfloat-abi`를 **컴파일과 링크 양쪽에** 똑같이 줘야 했던 이유가 이것이다 — 링크 때 안 주면 기본(`.`) 라이브러리가 섞여 들어가 ABI 불일치가 난다. 반면 Linux 크로스 툴체인은 대개 multilib 1벌이고, 다른 ABI가 필요하면 **툴체인과 sysroot를 통째로** 바꾼다.

---

## 10. 빌드 시스템과의 접점 — CROSS_COMPILE, ARCH, --host

### 10.1 CROSS_COMPILE 관례

커널, U-Boot, BusyBox, 그리고 많은 펌웨어 Makefile이 쓰는 관례: **툴 이름 앞에 붙일 접두어를 변수 하나로 받는다.** 커널 최상위 `Makefile`에는 실제로 이렇게 적혀 있다 (이 VM의 `/lib/modules/$(uname -r)/build/Makefile`에서 확인).

```text
CC		= $(CROSS_COMPILE)gcc
LD		= $(CROSS_COMPILE)ld
AR		= $(CROSS_COMPILE)ar
NM		= $(CROSS_COMPILE)nm
OBJCOPY		= $(CROSS_COMPILE)objcopy
OBJDUMP		= $(CROSS_COMPILE)objdump
READELF		= $(CROSS_COMPILE)readelf
STRIP		= $(CROSS_COMPILE)strip
```

그리고 `LLVM=1`이면 같은 자리에 `clang`, `ld.lld`, `llvm-objcopy`가 들어가는 분기가 있다. **`ARCH=`**는 컴파일러가 아니라 **커널 소스의 어느 `arch/<ARCH>/` 디렉터리를 쓸지**를 정한다 (`arm64`, `arm`, `riscv`, `x86`). 그래서 커널 빌드는 항상 둘을 짝으로 준다.

```sh
# 커널 (E2, C3에서 실제로 해 본다)
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- defconfig
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j$(nproc) Image dtbs modules
# U-Boot (C3)
make CROSS_COMPILE=aarch64-linux-gnu- <board>_defconfig && make CROSS_COMPILE=aarch64-linux-gnu-
```

(위 커널·U-Boot 명령은 형식 소개다. 이 노트에서는 실행하지 않았다.)

무엇을 확인하나: 같은 관례를 쓰는 최소 Makefile로 접두어 하나만 바꿔 세 아키텍처를 빌드한다.

```text
# Makefile.demo: 커널·U-Boot와 같은 CROSS_COMPILE 관례를 흉내 낸 최소 Makefile
CROSS_COMPILE ?=
CC      := $(CROSS_COMPILE)gcc
OBJDUMP := $(CROSS_COMPILE)objdump
OUT     ?= /tmp/B1/mk

$(OUT)/hello: hello.c
	@mkdir -p $(OUT)
	$(CC) -O2 -Wall -Wextra $< -o $@
	@file $@ | cut -d, -f1-2
```

```sh
cd $S
make -s -f Makefile.demo OUT=/tmp/B1/mk-native
make -s -f Makefile.demo CROSS_COMPILE=arm-linux-gnueabihf- OUT=/tmp/B1/mk-armhf
make -s -f Makefile.demo CROSS_COMPILE=riscv64-linux-gnu- OUT=/tmp/B1/mk-rv
make -f Makefile.demo CROSS_COMPILE=arm-linux-gnueabihf- OUT=/tmp/B1/mk-armhf2
```

```text
/tmp/B1/mk-native/hello: ELF 64-bit LSB pie executable, ARM aarch64
/tmp/B1/mk-armhf/hello: ELF 32-bit LSB pie executable, ARM
/tmp/B1/mk-rv/hello: ELF 64-bit LSB pie executable, UCB RISC-V
arm-linux-gnueabihf-gcc -O2 -Wall -Wextra hello.c -o /tmp/B1/mk-armhf2/hello
/tmp/B1/mk-armhf2/hello: ELF 32-bit LSB pie executable, ARM
```

출력에서 볼 것: 접두어가 **하이픈으로 끝난다**(`arm-linux-gnueabihf-`). `CROSS_COMPILE=arm-linux-gnueabihf`(하이픈 없음)로 주면 `arm-linux-gnueabihfgcc`를 찾다가 실패한다 — 처음 커널 빌드할 때 누구나 한 번 한다.

빌드 시스템별로 크로스 설정을 주는 방법은 이렇다 (자세한 건 B4).

| 빌드 시스템 | 크로스 설정 방법 | 예 |
|---|---|---|
| 커널, U-Boot, BusyBox | `ARCH=` + `CROSS_COMPILE=` | `make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-` |
| 일반 Makefile | `CC=` 등 변수 덮어쓰기 | `make CC=aarch64-linux-gnu-gcc` |
| autotools | `--host=<triplet>` | `./configure --host=aarch64-linux-gnu --prefix=/usr` |
| CMake | toolchain 파일 (`CMAKE_SYSTEM_NAME`, `CMAKE_C_COMPILER`, `CMAKE_SYSROOT`) | `cmake -DCMAKE_TOOLCHAIN_FILE=aarch64.cmake ..` |
| Meson | cross file (`[binaries]`, `[host_machine]`) | `meson setup --cross-file aarch64.txt build` |
| Yocto SDK | `source environment-setup-<triplet>` → `$CC`에 `--sysroot`까지 포함 | `$CC -O2 app.c -o app` |

### 10.2 C++와 C 라이브러리 선택이 결과물에 주는 영향

무엇을 확인하나: C++ 크로스 빌드는 `libstdc++`도 타깃용이 있어야 한다는 것과, C 라이브러리·링크 방식에 따라 크기가 얼마나 달라지는지 본다.

```c
// hellopp.cpp: C++ 크로스 빌드 — libstdc++도 타깃용이 필요하다
#include <iostream>
#include <vector>
int main() { std::vector<int> v{1, 2, 3}; std::cout << "C++ on armhf, v.size()=" << v.size() << "\n"; }
```

```sh
arm-linux-gnueabihf-g++ -O2 -Wall -Wextra $S/hellopp.cpp -o hpp && qemu-arm -L /usr/arm-linux-gnueabihf ./hpp && readelf -d hpp | grep NEEDED
```

```text
C++ on armhf, v.size()=3
 0x00000001 (NEEDED)                     Shared library: [libstdc++.so.6]
 0x00000001 (NEEDED)                     Shared library: [libc.so.6]
 0x00000001 (NEEDED)                     Shared library: [ld-linux-armhf.so.3]
```

C++ 바이너리는 보드 rootfs에 **같은 GCC 계열의 `libstdc++.so.6`**이 있어야 돈다. `GLIBCXX_3.4.xx not found`가 glibc 버전 문제의 C++ 판이다.

이제 크기. musl은 aarch64 native용 `musl-gcc` 래퍼만 설치돼 있어서 크기 비교는 aarch64에서 한다 (`fs`는 §11의 freestanding 프로그램).

```sh
mkdir -p /tmp/B1/sz && cd /tmp/B1/sz
gcc -O2 $S/hello.c -o dyn && strip -o dyn.s dyn
gcc -O2 -static $S/hello.c -o glibc_static && strip -o glibc_static.s glibc_static
musl-gcc -O2 -static -Wall -Wextra $S/hello.c -o musl_static && strip -o musl_static.s musl_static && ./musl_static.s
cp ../fs2 fs && strip -o fs.s fs
arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -O2 --specs=nosys.specs --specs=nano.specs $S/bm_hello.c -o bm_nano.elf 2>/dev/null
arm-none-eabi-size bm_nano.elf
size dyn.s glibc_static.s musl_static.s fs.s
```

```text
hello: built for aarch64, sizeof(long)=8, uname -m says aarch64
   text	   data	    bss	    dec	    hex	filename
   4292	    108	    504	   4904	   1328	bm_nano.elf
   text	   data	    bss	    dec	    hex	filename
   2224	    688	      8	   2920	    b68	dyn.s
 566473	  23324	  21744	 611541	  954d5	glibc_static.s
  33478	    688	   1616	  35782	   8bc6	musl_static.s
   1308	      0	      0	   1308	    51c	fs.s
```

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="20" text-anchor="middle" font-size="14" font-weight="bold">hello 한 개의 .text 크기 (bytes, 로그 스케일, strip 후)</text>
  <line x1="230.0" y1="34" x2="230.0" y2="200" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="230.0" y="216" text-anchor="middle" font-size="12">1K</text>
  <line x1="356.7" y1="34" x2="356.7" y2="200" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="356.7" y="216" text-anchor="middle" font-size="12">10K</text>
  <line x1="483.3" y1="34" x2="483.3" y2="200" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="483.3" y="216" text-anchor="middle" font-size="12">100K</text>
  <line x1="610.0" y1="34" x2="610.0" y2="200" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="610.0" y="216" text-anchor="middle" font-size="12">1M</text>
  <text x="222" y="58" text-anchor="end" font-size="12">freestanding (-nostdlib)</text>
  <rect x="230" y="44" width="14.8" height="20" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
  <text x="250.8" y="58" font-size="12">1,308</text>
  <text x="222" y="90" text-anchor="end" font-size="12">dynamic glibc (-O2)</text>
  <rect x="230" y="76" width="44.0" height="20" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
  <text x="280.0" y="90" font-size="12">2,224</text>
  <text x="222" y="122" text-anchor="end" font-size="12">arm-none-eabi nano (M4)</text>
  <rect x="230" y="108" width="80.1" height="20" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
  <text x="316.1" y="122" font-size="12">4,292</text>
  <text x="222" y="154" text-anchor="end" font-size="12">static musl</text>
  <rect x="230" y="140" width="193.1" height="20" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
  <text x="429.1" y="154" font-size="12">33,478</text>
  <text x="222" y="186" text-anchor="end" font-size="12">static glibc</text>
  <rect x="230" y="172" width="348.7" height="20" fill="#d0564a" fill-opacity="0.75" stroke="#d0564a"/>
  <text x="584.7" y="186" font-size="12">566,473</text>
  <line x1="230" y1="34" x2="230" y2="200" stroke="currentColor"/>
  <text x="420.0" y="234" text-anchor="middle" font-size="12">size(1)의 text 열 — aarch64, M4만 arm-none-eabi</text>
</svg>
```
그림 8 — 같은 hello의 코드 크기. 정적 glibc는 정적 musl의 17배다. 동적 glibc가 작아 보이는 것은 libc 본체(`libc.so.6`, 1MB대)가 rootfs에 따로 있기 때문이다.

출력에서 볼 것: **"작은 정적 바이너리가 필요하면 musl"**이 숫자로 보인다(33KB vs 566KB). 정적 glibc는 printf 하나에도 locale·NSS·스레드 지원이 딸려 온다. newlib-nano(Cortex-M4)는 4KB — Don이 펌웨어에서 `--specs=nano.specs`를 쓰던 이유다. C 라이브러리 비교는 B2에서 깊게 한다.

---


## 11. 커널 안에서는 무슨 일이 일어나나 — 같은 컴파일러, libc 없는 세계

### 11.1 커널은 "OS가 없는 프로그램"이다

이 노트 전체에서 Linux 툴체인은 "libc + crt + 동적 로더"를 얹은 bare-metal 툴체인이라고 설명했다. 그런데 **커널 자신은 그 위가 아니라 아래에 있다**. 커널이 부팅할 때 그 아래에는 glibc도, `crt1.o`도, `ld-linux.so`도 없다. 그래서 커널은 **같은 `aarch64-linux-gnu-gcc`로 빌드하되, 유저 공간용 부품을 전부 빼고** 빌드한다. Don에게는 익숙한 세계로 돌아가는 셈이다 — 커널 빌드는 `arm-none-eabi`로 펌웨어를 빌드하던 방식에 훨씬 가깝다.

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="200" y="14" width="280" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="34" text-anchor="middle" font-size="13">aarch64-linux-gnu-gcc 15.2</text>
  <text x="340" y="51" text-anchor="middle" font-size="12">(같은 cc1 · as · ld)</text>
  <line x1="270" y1="58" x2="170" y2="88" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="410" y1="58" x2="510" y2="88" stroke="currentColor" marker-end="url(#ah6)"/>
  <rect x="20" y="90" width="300" height="236" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="112" text-anchor="middle" font-size="14" font-weight="bold">유저 프로그램 (센서 데몬)</text>
  <text x="36" y="140" font-size="12">헤더: sysroot glibc + linux UAPI</text>
  <text x="36" y="164" font-size="12">시작: Scrt1.o → __libc_start_main</text>
  <text x="36" y="188" font-size="12">라이브러리: libc.so.6, libgcc</text>
  <text x="36" y="212" font-size="12">FP/NEON: 자유롭게 사용</text>
  <text x="36" y="236" font-size="12">링크: 기본 스크립트, -pie</text>
  <text x="36" y="260" font-size="12">로드: 커널 execve + ld-linux</text>
  <text x="36" y="284" font-size="12">미해결 심볼: 실행 시 libc가 해결</text>
  <text x="36" y="308" font-size="12">EL0</text>
  <rect x="360" y="90" width="300" height="236" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="510" y="112" text-anchor="middle" font-size="14" font-weight="bold">커널 · 모듈 (.ko)</text>
  <text x="376" y="140" font-size="12">헤더: -nostdinc, 커널 include/ 만</text>
  <text x="376" y="164" font-size="12">시작: head.S (부트로더가 점프)</text>
  <text x="376" y="188" font-size="12">라이브러리: 없음, lib/ 에 자체 구현</text>
  <text x="376" y="212" font-size="12">FP/NEON: -mgeneral-regs-only</text>
  <text x="376" y="236" font-size="12">링크: vmlinux.lds / module.lds</text>
  <text x="376" y="260" font-size="12">로드: 부트로더 / insmod</text>
  <text x="376" y="284" font-size="12">미해결 심볼: 커널 심볼 테이블</text>
  <text x="376" y="308" font-size="12">EL1</text>
</svg>
```
그림 9 — 같은 컴파일러로 두 세계를 빌드한다. 오른쪽(커널)은 libc·crt·동적 로더 없이 자기가 필요한 것을 직접 갖춘다. Don의 bare-metal 빌드와 같은 모양이다.

### 11.2 코드로 확인 — libc 없이 도는 유저 프로그램 (커널 빌드 흉내)

무엇을 확인하나: `-ffreestanding -nostdlib`로 libc·crt 없이 빌드하면 무엇이 빠지는지, 그리고 **컴파일러가 스스로 `memcpy`를 부를 수 있다**는 것을 확인한다. 커널이 `lib/string.c`에 `memcpy`·`memset`을 직접 갖고 있는 이유다.

```c
/* freestand.c: libc 없이(-nostdlib) aarch64 Linux에서 도는 최소 프로그램.
 * 커널 코드처럼 "C 언어 + 내가 준비한 것"만 쓴다. */
typedef unsigned long size_t;

static long sys3(long nr, long a, long b, long c)
{
    register long x8 asm("x8") = nr;
    register long x0 asm("x0") = a;
    register long x1 asm("x1") = b;
    register long x2 asm("x2") = c;
    asm volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
    return x0;
}

struct big { char buf[1024]; };

#ifdef NEED_MEMFUNCS
void *memcpy(void *d, const void *s, size_t n)  /* 커널: lib/string.c + arch/arm64/lib/memcpy.S */
{
    unsigned char *dp = d; const unsigned char *sp = s;
    while (n--) *dp++ = *sp++;
    return d;
}
void *memset(void *s, int c, size_t n)
{
    unsigned char *p = s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}
#endif

void _start(void)
{
    struct big b = { "freestanding: no libc, no crt1.o, just svc #0\n" };
    size_t n = 0;
    while (b.buf[n]) n++;
    sys3(64 /* __NR_write */, 1, (long)b.buf, (long)n);
    sys3(93 /* __NR_exit */, 0, 0, 0);
    for (;;) { }
}
```

```sh
gcc -O2 -Wall -Wextra -ffreestanding -nostdlib -static $S/freestand.c -o fs1 2>&1 | head -3
echo ---
gcc -O2 -Wall -Wextra -ffreestanding -nostdlib -static -DNEED_MEMFUNCS $S/freestand.c -o fs2 && ./fs2; echo "exit=$?"
file fs2 | cut -c1-80; nm fs2 | grep " T "
```

```text
/usr/bin/aarch64-linux-gnu-ld.bfd: /tmp/cc93tTGf.o: in function `_start':
freestand.c:(.text+0x1c): undefined reference to `memcpy'
collect2: error: ld returned 1 exit status
---
freestanding: no libc, no crt1.o, just svc #0
exit=0
fs2: ELF 64-bit LSB executable, ARM aarch64, version 1 (SYSV), statically linked
0000000000400160 T _start
0000000000400120 T memcpy
0000000000400140 T memset
```

출력에서 볼 것:

- 소스에는 `memcpy` 호출이 한 줄도 없는데 링커가 `memcpy`를 찾았다. 1KB 구조체를 문자열 리터럴로 초기화하는 코드를 **컴파일러가 `memcpy`(+`memset`) 호출로 바꿨기** 때문이다. `-ffreestanding`은 "printf 같은 libc 함수의 의미를 가정하지 마라"는 뜻이지, "memcpy를 절대 부르지 마라"는 뜻이 아니다. GCC 문서도 freestanding 환경에서 `memcpy`, `memmove`, `memset`, `memcmp`는 제공되어야 한다고 적고 있다.
- 직접 제공하자 링크되고 실행됐다. 결과물은 심볼 3개, `.text` 1,308바이트(§10.2 차트)다. `_start`가 진입점이고, `svc #0`(A1에서 본 그 syscall)으로 직접 커널을 불렀다. Don의 펌웨어에서 `Reset_Handler`가 진입점이고 레지스터를 직접 만지던 것과 같은 구조다.

### 11.3 코드로 확인 — 실제 커널 모듈은 어떤 플래그로 빌드되나

무엇을 확인하나: 가장 작은 out-of-tree 모듈을 이 VM 커널의 빌드 트리(`linux-headers-7.0.0-34-generic`)로 빌드하면서, Kbuild가 gcc에 주는 플래그와 링크 방식을 꺼내 본다. 처음엔 접두어 없이, 다음엔 `ARCH`/`CROSS_COMPILE`을 줘서 빌드한다.

```c
// hello_mod.c: 커널 모듈도 같은 gcc로 빌드된다 — 플래그만 다르다
#include <linux/module.h>
#include <linux/init.h>

static int __init hello_init(void) { pr_info("B1: hello from kernel\n"); return 0; }
static void __exit hello_exit(void) { }
module_init(hello_init);
module_exit(hello_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("B1 toolchain demo");
```

Makefile은 한 줄이다: `obj-m := hello_mod.o`.

```sh
K=/lib/modules/$(uname -r)/build; D=/tmp/B1/kmod
make -C $K M=$D modules 2>&1 | grep -vE "^make|^\s*$" | head -12
```

```text
warning: the compiler differs from the one used to build the kernel
  The kernel was built by: aarch64-linux-gnu-gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0
  You are using:           gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0
warning: pahole version differs from the one used to build the kernel
  The kernel was built with: 131
  You are using:             0
  CC [M]  hello_mod.o
  MODPOST Module.symvers
  CC [M]  hello_mod.mod.o
  LD [M]  hello_mod.ko
  BTF [M] hello_mod.ko
Skipping BTF generation for hello_mod.ko due to unavailability of vmlinux
```

```sh
make -C $K M=$D clean >/dev/null 2>&1
make -C $K M=$D ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- modules 2>&1 | grep -vE "^make|^\s*$"
make -C $K M=$D clean >/dev/null 2>&1
make -C $K M=$D ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- V=1 modules > $D/build.log 2>&1
grep -m1 -E "gcc .*hello_mod.c" $D/build.log | tr " " "\n" \
  | grep -E "^-(nostdinc|ffreestanding|fno-builtin|mgeneral-regs-only|fno-common|fno-PIE|mbranch|std=|O2|D__KERNEL__|DMODULE|mstack-protector-guard=|mabi)" | sort -u | tr "\n" " "; echo
grep -m1 -oE "\S*ld -r .*-o hello_mod.ko" $D/build.log | cut -c1-200
readelf -h $D/hello_mod.ko | grep -E "Type|Machine"; modinfo $D/hello_mod.ko | grep -E "vermagic"; nm -u $D/hello_mod.ko
```

```text
warning: pahole version differs from the one used to build the kernel
  The kernel was built with: 131
  You are using:             0
  CC [M]  hello_mod.o
  MODPOST Module.symvers
  CC [M]  hello_mod.mod.o
  CC [M]  .module-common.o
  LD [M]  hello_mod.ko
  BTF [M] hello_mod.ko
Skipping BTF generation for hello_mod.ko due to unavailability of vmlinux
-DMODULE -D__KERNEL__ -O2 -fno-PIE -fno-builtin-wcslen -fno-common -mabi=lp64 -mbranch-protection=pac-ret -mgeneral-regs-only -mstack-protector-guard=sysreg -nostdinc -std=gnu11 
aarch64-linux-gnu-ld -r -EL  -maarch64elf -z noexecstack --no-warn-rwx-segments --build-id=sha1  -T /usr/src/linux-headers-7.0.0-34-generic/scripts/module.lds -o hello_mod.ko
  Type:                              REL (Relocatable file)
  Machine:                           AArch64
vermagic:       7.0.0-34-generic SMP preempt mod_unload modversions aarch64
                 U _printk
```

출력에서 볼 것:

- **"The kernel was built by: aarch64-linux-gnu-gcc"**: Ubuntu의 arm64 커널조차 triplet 접두어가 붙은 이름의 gcc로 빌드됐다. 접두어 없이 `gcc`로 빌드하자 Kbuild가 "컴파일러가 다르다"고 경고했고, `CROSS_COMPILE=aarch64-linux-gnu-`을 주자 경고가 사라졌다(`/usr/bin/aarch64-linux-gnu-gcc`는 같은 GCC 15의 다른 이름이다). **모듈은 커널을 빌드한 것과 같은 컴파일러로** 빌드해야 한다 — 플러그인·구조체 레이아웃 옵션이 맞아야 하기 때문이다. 보드용 모듈이면 BSP 커널을 빌드한 그 크로스 툴체인이다.
- **커널 플래그**: `-nostdinc`(시스템·sysroot 헤더 금지, 커널 `include/`만), `-mgeneral-regs-only`(FP/SIMD 레지스터 금지 — 커널은 진입 시 유저의 FP 상태를 저장하지 않으므로, NEON이 필요하면 `kernel_neon_begin()`/`kernel_neon_end()`로 감싼다), `-fno-PIE`, `-mstack-protector-guard=sysreg`(canary를 libc의 TLS가 아니라 `sp_el0`가 가리키는 현재 태스크에서 읽음), `-D__KERNEL__`. **`-ffreestanding`은 arm64 공통 플래그에 없다** — arm64 `Makefile`은 NEON intrinsic을 쓰는 파일에만 `CC_FLAGS_FPU := -ffreestanding`을 붙인다(이 VM의 `arch/arm64/Makefile` 36~41행에서 확인). 링크는 libc 없이 `ld -r`(재배치 가능 오브젝트) + `module.lds` 스크립트다.
- **`.ko`는 `REL` 파일이고 미해결 심볼이 `_printk`**다. 유저 프로그램의 `U printf`를 실행 시 동적 로더가 libc에서 해결했다면, 모듈의 `U _printk`는 `insmod` 때 **커널이 자기 심볼 테이블(EXPORT_SYMBOL)에서** 해결한다. `vermagic`이 커널 버전·설정과 맞아야 로드된다 (E4, E5).

무엇을 확인하나 (실패 예): arm64용으로 configure된 커널 빌드 트리에 32-bit ARM 컴파일러를 들이대면?

```sh
rm -rf /tmp/B1/kmod2 && mkdir -p /tmp/B1/kmod2 && cp $S/kmod/hello_mod.c $S/kmod/Makefile /tmp/B1/kmod2/
make -C $K M=/tmp/B1/kmod2 ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- modules 2>&1 | grep -vE "^make" | head -8
```

```text
warning: the compiler differs from the one used to build the kernel
  The kernel was built by: aarch64-linux-gnu-gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0
  You are using:           arm-linux-gnueabihf-gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0
warning: pahole version differs from the one used to build the kernel
  The kernel was built with: 131
  You are using:             0
  CC [M]  hello_mod.o
cc1: error: plugin arm_ssp_per_task_plugin should be specified before ‘-fplugin-arg-arm_ssp_per_task_plugin-offset=1912’ in the command line
```

출력에서 볼 것: 커널 빌드 트리(`.config`, 생성된 헤더, 플러그인 설정)는 **한 아키텍처·한 컴파일러에 묶여 있다**. 모듈만 다른 툴체인으로 빌드하는 것은 불가능하다. 보드용 모듈은 보드 커널 소스 트리 + 그 커널을 빌드한 크로스 툴체인으로 빌드한다 (E4, S 모듈의 out-of-tree 모듈 실습).

### 11.4 드라이버를 쓸 때 다시 만나는 지점

| 이 노트의 개념 | 드라이버·커널 작업에서 다시 만나는 곳 | 모듈 ID |
|---|---|---|
| `CROSS_COMPILE`, `ARCH` | 커널·U-Boot·모듈 빌드 명령의 첫 두 인자 | C3, E2, E4 |
| 커널 헤더(UAPI) | 드라이버의 ioctl 구조체를 `include/uapi/`에 두면 유저가 `linux-libc-dev`로 받는다 | G4, H7 |
| `-mgeneral-regs-only` | 드라이버에서 float 금지, NEON은 `kernel_neon_begin()` | E6, N |
| freestanding, 자체 `memcpy` | 커널 `lib/`, `memcpy_toio`/`memcpy_fromio`(MMIO는 일반 memcpy 금지) | F, H1 |
| `.ko` = REL + 미해결 심볼 | `EXPORT_SYMBOL`, modpost, `vermagic`, `modprobe` 의존성 | E4, E5 |
| 동일 툴체인 원칙 | BSP SDK 툴체인으로 out-of-tree 모듈 빌드 | E4, P |

---

## 12. Embedded 실무에서 다시 보기

### 12.1 "보드에서 `./imud: not found`" — 파일은 분명히 있는데

센서 키트 보드에 PC에서 빌드한 데몬을 올렸더니 셸이 `not found`라고 한다. 파일은 `ls`로 보인다. 원인은 거의 항상 **INTERP 경로의 동적 로더가 보드 rootfs에 없는 것**이다 (§5에서 qemu가 `Could not open '/lib/ld-linux-armhf.so.3'`라고 친절하게 말해 준 그 상황). 예를 들어 armhf로 빌드했는데 보드가 aarch64 rootfs이거나, glibc로 빌드했는데 보드가 musl(Alpine, 일부 Buildroot) rootfs인 경우다. 보는 순서:

```sh
file ./imud                                   # 아키텍처·ABI·interpreter
readelf -l ./imud | grep interpreter          # 보드에 이 경로가 있나?
readelf -d ./imud | grep NEEDED               # 필요한 .so가 보드에 있나?
arm-linux-gnueabihf-objdump -T ./imud | grep -oE "GLIBC_[0-9.]+" | sort -uV | tail -1   # 보드 glibc보다 높나?
```

| 보드에서 보이는 증상 | 원인 | 이 노트 |
|---|---|---|
| `Exec format error` | 아키텍처가 다름 (x86 바이너리, AArch32 미지원 코어) | §5.2 |
| `not found` (파일은 있음) | INTERP 동적 로더 없음 (ABI·libc 불일치) | §5.3 |
| `version 'GLIBC_2.34' not found` | sysroot glibc > 보드 glibc | §9.3 |
| `Illegal instruction` | `-march`/`-mcpu`가 보드 CPU보다 높음 | §7.5 |
| float 값이 쓰레기 | ABI 불일치 오브젝트가 (링커 검사를 우회해) 섞임 | §6.2 |
| ioctl이 `ENOTTY`/`EINVAL` | 툴체인 커널 헤더 > 보드 커널 | §9.1 |

### 12.2 호스트 라이브러리가 몰래 섞이는 문제

무엇을 확인하나: 호스트(arm64)에만 설치된 zlib을 armhf 크로스 빌드에서 쓰면 무슨 일이 일어나는지 본다. Debian 크로스 툴체인은 sysroot가 `/`라서 include 경로 맨 끝에 호스트 `/usr/include`가 들어 있다.

```c
/* usez.c: 호스트(arm64)에만 설치된 라이브러리를 크로스 빌드에서 쓰면? */
#include <stdio.h>
#include <zlib.h>
int main(void) { printf("zlib %s\n", zlibVersion()); return 0; }
```

```sh
ls /usr/include/zlib.h /usr/arm-linux-gnueabihf/include/zlib.h
gcc -Wall -Wextra $S/usez.c -lz -o z-native && ./z-native
arm-linux-gnueabihf-gcc -Wall -Wextra $S/usez.c -lz -o z-armhf 2>&1 | tail -3
arm-linux-gnueabihf-gcc -H -fsyntax-only $S/usez.c 2>&1 | grep zlib
echo | arm-linux-gnueabihf-gcc -xc -E -v - 2>&1 | sed -n "/<...> search starts/,/End of search/p"
```

```text
/usr/include/zlib.h
ls: cannot access '/usr/arm-linux-gnueabihf/include/zlib.h': No such file or directory
zlib 1.3.1
/usr/bin/arm-linux-gnueabihf-ld.bfd: cannot find -lz: No such file or directory
collect2: error: ld returned 1 exit status
. /usr/include/zlib.h
#include <...> search starts here:
 /usr/lib/gcc-cross/arm-linux-gnueabihf/15/include
 /usr/lib/gcc-cross/arm-linux-gnueabihf/15/../../../../arm-linux-gnueabihf/include
 /usr/include
End of search list.
```

출력에서 볼 것: **컴파일은 통과했다** — armhf 크로스 컴파일러가 호스트의 `/usr/include/zlib.h`를 집어 왔다(`-H` 출력). 링크에서야 `cannot find -lz`로 걸렸다. 이번엔 운 좋게 링크 단계에서 멈췄지만, 호스트 헤더의 구조체 레이아웃·매크로가 타깃과 다르면 **링크는 되고 런타임에 깨지는** 최악의 경우가 생긴다. 해법은 **타깃 라이브러리를 sysroot에 설치**하는 것(Debian이면 `libz-dev:armhf` 같은 multiarch 패키지, Yocto/Buildroot면 레시피·패키지 추가)이고, `pkg-config`도 `PKG_CONFIG_SYSROOT_DIR`/`PKG_CONFIG_LIBDIR`로 sysroot를 보게 해야 한다 (B4). Buildroot·Yocto 툴체인이 sysroot를 별도 디렉터리로 두는 이유 중 하나가 이 오염 방지다.

### 12.3 이종 코어 SoC — 툴체인 두 벌을 한 저장소에서

i.MX8M Mini 같은 SoC는 Cortex-A53(Linux)과 Cortex-M4(실시간 모터·IMU 처리)를 함께 갖는다. 실무 저장소는 이렇게 생긴다.

- `linux-app/`: `aarch64-linux-gnu-gcc` (또는 Yocto SDK의 `$CC`), glibc, `/dev/rpmsg*`로 M4와 통신.
- `m4-firmware/`: `arm-none-eabi-gcc -mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16 --specs=nano.specs`, 링커 스크립트는 M4의 TCM 주소.
- `kernel/`: `make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-`, remoteproc 드라이버가 M4 펌웨어 ELF를 로드.

Don은 이 세 디렉터리 중 두 번째는 이미 전문가다. 이 노트는 첫 번째와 세 번째의 툴체인 감각을 맞추는 것이고, M4↔A53 통신은 O 모듈(remoteproc, RPMsg)에서 다룬다.

### 12.4 qemu-user를 CI에 쓰기

보드 없이 PR마다 "크로스 빌드 + 기본 동작"을 확인하고 싶다면 qemu-user가 좋다. 단위 테스트(파서, 프로토콜 처리, 링버퍼 로직)를 크로스 빌드해 `qemu-aarch64 -L <sysroot>`로 돌린다. `-cpu cortex-a53`을 주면 §7.5처럼 "보드 CPU에 없는 명령어를 썼다"까지 잡는다. 단, §7.6의 측정처럼 **성능 회귀 테스트로는 쓸 수 없고**, `/dev`·ioctl·실제 드라이버 동작은 확인할 수 없다(그건 `qemu-system-aarch64` + 커널 이미지 또는 실물 보드, M·S 모듈).

---

## 13. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `CROSS_COMPILE=aarch64-linux-gnu` (끝 하이픈 누락) | `aarch64-linux-gnugcc: not found` | 접두어에 `gcc`를 그대로 이어 붙인다 | 접두어는 `-`로 끝나게 |
| `ARCH=` 없이 커널 크로스 빌드 | 호스트 아키텍처 `.config`가 만들어지거나 이상한 에러 | `ARCH`는 컴파일러가 아니라 `arch/` 디렉터리 선택 | `ARCH`와 `CROSS_COMPILE`을 항상 짝으로 |
| 최신 배포판 툴체인으로 옛 BSP rootfs용 앱 빌드 | 보드에서 `GLIBC_2.xx not found` | sysroot glibc > 보드 glibc | 보드 rootfs와 짝인 SDK 사용, 또는 정적 링크(musl) |
| soft-float 라이브러리와 hard-float 앱을 섞음 | 링크 에러 `uses VFP register arguments` | ELF 속성 `Tag_ABI_VFP_args` 불일치 | 툴체인·sysroot·라이브러리를 한 ABI로 통일 |
| Jetson용 `-mcpu=cortex-a78`로 빌드해 A53 보드에 배포 | 특정 기능에서만 `Illegal instruction` | LSE·dotprod 등 상위 ISA 명령 | 배포 대상 중 가장 낮은 코어로 `-march`, 필요하면 런타임 디스패치 |
| 호스트 `/usr/include`·`-L/usr/lib`가 크로스 빌드에 섞임 | 링크 실패 또는 런타임 구조체 깨짐 | sysroot가 `/`인 툴체인, 하드코딩된 `-I/usr/include` | 타깃 라이브러리를 sysroot에 설치, `pkg-config` sysroot 설정 |
| 툴체인 커널 헤더가 보드 커널보다 새것 | 새 ioctl·syscall이 `ENOTTY`/`ENOSYS` | UAPI는 하위 호환만 보장 | 헤더 버전 ≤ 보드 커널, Buildroot/Yocto에서 헤더 버전 고정 |
| qemu-user로 성능 측정 | "NEON이 1.4배밖에 안 빨라졌다" 같은 엉터리 결론 | TCG 번역 비용이 결과를 지배 | 성능은 실물 보드 + `perf`로 |
| 모듈을 커널과 다른 툴체인으로 빌드 | 경고 무시 → 로드 실패나 이상 동작 | 플러그인·구조체 옵션 불일치 | 커널을 빌드한 그 툴체인 + 같은 커널 트리 |
| arm-none-eabi 링크 때 `-mcpu`/`-mfloat-abi`를 빠뜨림 | ABI 불일치 링크 에러 또는 잘못된 newlib | 링크 때 multilib 선택이 기본값으로 | 컴파일·링크 플래그를 같은 변수로 |

---

## 14. 면접에서 이렇게 말한다

**Q.** "What is a cross compiler, and why do we use one for embedded Linux?"

**A.** 컴파일러가 도는 기계(host)와 결과물이 도는 기계(target)가 다른 컴파일러다. 보드는 느리고 메모리·저장공간이 작고 rootfs에 개발 도구가 없어서, 빠른 PC에서 보드용 바이너리를 만든다. Linux 타깃이면 컴파일러만이 아니라 타깃용 libc, 동적 로더, 커널 헤더가 담긴 sysroot까지 툴체인에 포함되어야 한다는 점이 bare-metal 크로스 컴파일과 다르다.

> A cross compiler runs on one machine — the host — and generates code for a different target. We use it because the target board is slow, memory-limited and usually has no compiler in its rootfs, so we build on a fast workstation. For a Linux target the toolchain isn't just gcc and binutils: it also needs a sysroot with the target's C library, dynamic loader, crt files and kernel UAPI headers, so the binary links against exactly what will be on the board.

**꼬리 질문**: "What's a Canadian cross?" → build, host, target이 모두 다른 경우. 예: Linux 서버에서 Windows에서 도는 arm-none-eabi-gcc를 빌드.

**Q.** "Decode the triplet arm-linux-gnueabihf. How is it different from arm-none-eabi?"

**A.** arch=arm(32-bit), os=linux, 환경=gnueabihf — glibc + ARM EABI + hard-float(float 인자를 VFP 레지스터로). arm-none-eabi는 OS가 없어서 newlib을 정적으로 링크하고, printf의 끝이 `_write` 같은 스텁이라 내가 UART나 semihosting으로 채워야 한다. Linux 쪽은 printf가 `write()` syscall로 커널에 넘어가고, 동적 로더가 libc.so를 붙여 준다.

> It's arm, Linux, GNU libc, ARM EABI with the hard-float calling convention — floating-point arguments go in VFP registers. arm-none-eabi targets no OS: you link newlib statically, provide your own startup code and linker script, and the bottom of printf is a stub like _write that you implement over a UART or semihosting. With the Linux triplet, printf ends in a write system call into the kernel, and the program is dynamically linked against glibc via ld-linux-armhf.so.3.

**Q.** "What is a sysroot and what goes wrong if it's wrong?"

**A.** 타깃 루트 파일시스템의 개발용 축소판이다 — 타깃 libc 헤더·라이브러리, crt 파일, 커널 헤더, 그리고 앱이 쓰는 타깃 라이브러리. 컴파일러가 헤더와 라이브러리를 호스트가 아닌 여기서 찾게 하는 기준 경로다. 틀리면 호스트 헤더가 섞여 링크 실패나 런타임 구조체 불일치가 나고, sysroot의 glibc가 보드보다 새로우면 보드에서 `GLIBC_2.xx not found`가 난다.

> The sysroot is a directory that looks like the target's root filesystem, trimmed for development: the target libc headers and libraries, crt objects, kernel UAPI headers and any target libraries the app depends on. The compiler and linker resolve includes and libraries relative to it instead of the host's /usr. If it's wrong you get host headers leaking in, link failures like "cannot find -lz", or — if the sysroot's glibc is newer than the board's — "GLIBC_2.34 not found" at runtime. That's why teams use the SDK generated from the same Yocto or Buildroot build as the image.

**Q.** "Explain hard-float vs soft-float on ARM. Can you mix them?"

**A.** soft는 FPU 명령 없이 libgcc 함수로 float를 흉내 내고, softfp는 FPU 명령은 쓰되 인자는 정수 레지스터로 넘겨 soft와 호환되며, hard는 float 인자를 VFP 레지스터로 직접 넘긴다. soft/softfp와 hard는 호출 규약이 달라서 섞을 수 없고, 링커가 ELF 속성을 보고 거부한다. Linux에서는 배포판 전체가 한 ABI로 빌드되므로 armhf rootfs면 모든 것을 hard로 빌드해야 한다.

> Soft-float emulates floating point in libgcc with no FPU instructions; softfp uses the FPU but still passes float arguments in core registers, so it's link-compatible with soft; hard-float passes them in VFP registers, which is faster. Soft and softfp can't be mixed with hard because the calling convention differs — the linker checks the Tag_ABI_VFP_args build attribute and refuses with "uses VFP register arguments". On Linux the whole distribution picks one: armhf means every library is hard-float, so your app must be too.

**Q.** "What's the difference between -march, -mcpu and -mtune?"

**A.** `-march`는 쓸 수 있는 명령어 집합을 정하고, `-mtune`은 명령어 집합은 그대로 두고 스케줄링만 특정 코어에 맞추고, `-mcpu`는 둘을 그 코어에 맞게 한 번에 정한다. `-march`/`-mcpu`를 보드보다 높게 잡으면 Illegal instruction이 나고, `-mtune`은 틀려도 느려질 뿐이다. 실제로 ARMv8.1 LSE atomic을 쓰도록 빌드한 바이너리가 Cortex-A53 에뮬레이션에서 SIGILL로 죽는 것을 확인했다.

> -march sets which instructions the compiler may emit; -mtune keeps the instruction set but optimises scheduling and cost decisions for a particular core; -mcpu sets both for that core. Overshooting -march or -mcpu gives you SIGILL on older cores — for example, -march=armv8.1-a inlines LSE atomics like ldaddal, which a Cortex-A53 doesn't have. A wrong -mtune only costs performance. For a fleet with mixed cores you build for the lowest -march and rely on runtime dispatch, like GCC's outline atomics on AArch64.

**Q.** "The kernel headers in your toolchain are newer than the kernel on the board. Is that a problem?"

**A.** 문제가 될 수 있다. 커널 UAPI는 하위 호환만 보장해서, 옛 헤더로 빌드한 앱은 새 커널에서 돌지만 반대로 새 헤더로 빌드한 앱은 보드 커널에 없는 syscall·ioctl·구조체 필드를 써서 ENOSYS나 EINVAL이 날 수 있다. 그래서 툴체인의 커널 헤더 버전은 보드 커널 이하로 맞춘다. 또 syscall 번호는 아키텍처마다 달라서(openat이 arm64 56, arm 322) 헤더는 반드시 타깃 아키텍처용이어야 한다.

> It can be. The kernel only guarantees backward compatibility of its user ABI, so binaries built against older headers run on newer kernels, but headers newer than the running kernel let the program use syscalls, ioctls or struct fields the board's kernel doesn't know, which surface as ENOSYS, EINVAL or ENOTTY. The rule is toolchain headers less than or equal to the oldest kernel you deploy on — Buildroot and Yocto pin the header version to the BSP kernel for that reason. The headers must also be for the target architecture: syscall numbers differ, openat is 56 on arm64 and 322 on 32-bit ARM.

**Q.** "How is building a kernel module different from building a user-space program?"

**A.** 같은 크로스 gcc를 쓰지만 libc·crt·동적 로더가 전혀 없다. Kbuild가 `-nostdinc`로 커널 헤더만 쓰게 하고, arm64에서는 `-mgeneral-regs-only`로 FP/SIMD를 막고, `ld -r`과 `module.lds`로 재배치 가능한 `.ko`를 만든다. 미해결 심볼은 insmod 때 커널의 export 심볼 테이블로 해결된다. 그리고 반드시 커널을 빌드한 것과 같은 툴체인과 같은 커널 트리로 빌드해야 한다.

> It uses the same cross compiler, but there's no libc, no crt and no dynamic loader. Kbuild passes -nostdinc so only kernel headers are visible, -mgeneral-regs-only on arm64 so the compiler never touches FP or SIMD registers, and links with ld -r and module.lds to produce a relocatable .ko. Its undefined symbols, like _printk, are resolved at insmod time against the kernel's exported symbols, and vermagic has to match. You must build it against the same configured kernel tree and with the same toolchain that built the kernel.

**꼬리 질문**: "Can you use floating point in a driver?" → 기본은 금지. arm64에서는 `kernel_neon_begin()`/`kernel_neon_end()` 구간 안에서만, 그 파일은 `CC_FLAGS_FPU`로 빌드.

**Q.** "GCC or Clang for cross compiling — what's the practical difference?"

**A.** GCC는 target마다 컴파일러 바이너리가 따로 있고, Clang은 한 바이너리가 `--target`으로 모든 아키텍처를 지원한다. 대신 Clang은 libc·crt·libgcc를 sysroot와 GCC 설치본에서 빌려 쓰므로 `--sysroot`가 필수다. 커널은 둘 다 지원하며 Clang은 `LLVM=1`로 빌드한다. 벤더 BSP와 U-Boot는 아직 GCC가 기준인 경우가 많다.

> GCC is configured for one target per installation, so you get arm-linux-gnueabihf-gcc, aarch64-linux-gnu-gcc and so on. Clang is a single binary with all backends and picks the target with --target, but it doesn't ship a C library or crt files — it borrows them from the sysroot and usually from an installed GCC cross toolchain, so you must pass --sysroot. The kernel builds with both; for Clang you use LLVM=1. In practice vendor BSPs and U-Boot are still GCC-first, while Clang is handy for sanitizers, static analysis and one-compiler CI.

---

## 15. 직접 해보기

1. **개념**: `aarch64-poky-linux`, `arm-buildroot-linux-musleabihf`, `riscv32-unknown-elf`를 각각 arch / vendor / os / 환경으로 나누고, 어떤 C 라이브러리와 어떤 실행 환경을 뜻하는지 말하라.
   정답/힌트: Yocto(poky) aarch64 Linux glibc · Buildroot 32-bit ARM Linux musl hard-float · RISC-V 32 bare-metal(newlib, `elf` = OS 없음).
2. **코드**: §2.2의 `hello.c`에 `#include <linux/version.h>`를 추가해 riscv64 sysroot의 헤더 버전을 출력하고, `qemu-riscv64 -L`로 실행해 보라. `uname()`이 돌려주는 `release`가 왜 호스트 커널인지 설명하라.
   정답/힌트: qemu-user는 syscall을 호스트 커널에 넘긴다. 헤더 버전(7.0.0)과 실행 커널(7.0.0-34-generic)은 별개다 (§9.2).
3. **ABI 관찰**: `fadd.c`를 `-mfloat-abi=softfp`로, `fmain.c`도 `-mfloat-abi=softfp`로 컴파일(`-c`)한 뒤 두 `.o`의 `readelf -A`를 비교하라. 이 둘을 armhf glibc와 링크하면 어떻게 되나?
   정답/힌트: 두 `.o`에는 `Tag_ABI_VFP_args`가 없다. glibc(hard)와 링크하면 §6.2와 같은 `uses VFP register arguments` 계열 에러가 난다.
4. **qemu로 CPU 호환성 검사**: `-march=armv8.2-a+dotprod`로 `__builtin` 없이 int8 dot product 루프를 `-O3`로 빌드해 `sdot` 명령이 나오는지 `objdump`로 확인하고, `qemu-aarch64 -cpu cortex-a53`과 `-cpu cortex-a76`에서 돌려 보라.
   정답/힌트: 벡터화되면 `sdot`이 보이고 A53에서는 SIGILL. 벡터화가 안 되면 루프 형태(`int8_t` 곱의 `int32_t` 누적)를 확인하라.
5. **clang 크로스**: `clang --target=aarch64-linux-gnu -mcpu=cortex-a53 -O3 -c vec.c`의 결과를 `llvm-objdump -d`로 보고 GCC `-O3 -mcpu=cortex-a53`과 saxpy 루프의 명령 수·언롤 정도를 비교하라.
   정답/힌트: 이 VM에서 clang 결과에는 `fmla .4s`가 2개(언롤 2배) 나왔고 GCC는 1개였다. 컴파일러마다 벡터화 전략이 다르다.
6. **커널 빌드 미리보기**: 이 VM의 커널 빌드 트리에서 `grep -n "CROSS_COMPILE\|LLVM" /lib/modules/$(uname -r)/build/Makefile | head -30`을 실행하고, `LLVM=1`일 때 `CC`, `LD`가 무엇으로 바뀌는지 찾아라.
   정답/힌트: `CC = $(LLVM_PREFIX)clang$(LLVM_SUFFIX)`, `LD = $(LLVM_PREFIX)ld.lld$(LLVM_SUFFIX)`. E2에서 실제 커널을 빌드한다.
7. **strace로 관찰**: `strace -f -e trace=execve arm-linux-gnueabihf-gcc -O2 hello.c -o h 2>&1 | grep execve`로 driver가 실제로 exec하는 프로그램 목록을 뽑고, §3.3의 `-###` 출력과 비교하라.
   정답/힌트: `cc1` → `as` → `collect2` → `ld`(collect2가 다시 exec) 순서가 보여야 한다. `-###`에는 collect2까지만, strace에는 그 뒤의 `ld`까지 보인다.

---

## 16. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| cross compiler | 크로스 컴파일러 | host와 다른 target용 코드를 만드는 컴파일러 |
| build / host / target | 빌드·호스트·타깃 기계 | 컴파일러를 빌드한 곳 / 컴파일러가 도는 곳 / 결과물이 도는 곳 |
| Canadian cross | 캐나디안 크로스 | build, host, target이 모두 다른 툴체인 빌드 |
| target triplet | 타깃 트리플릿 | `arch-vendor-os-abi` 형식의 타깃 이름 (`arm-linux-gnueabihf`) |
| toolchain | 툴체인 | 컴파일러 + binutils + libc + 헤더 + 런타임 라이브러리 묶음 |
| binutils | GNU 바이너리 도구 | `as`, `ld`, `objdump`, `readelf`, `nm`, `objcopy`, `strip` |
| gcc driver | 컴파일러 드라이버 | 옵션을 해석해 cc1·as·collect2/ld를 차례로 실행하는 `gcc` 명령 |
| cc1 | C 컴파일러 본체 | 전처리 + C → 어셈블리 |
| collect2 | 링커 래퍼 | GCC가 ld 앞에 두는 프로그램 |
| libgcc | 컴파일러 런타임 | 64-bit 나눗셈, soft-float, unwind 등 컴파일러가 부르는 보조 함수 |
| crt1.o / Scrt1.o | C runtime 시작 파일 | `_start`가 있는 오브젝트, `__libc_start_main` 호출 |
| sysroot | 시스루트 | 타깃 루트 파일시스템의 개발용 축소판 (헤더 + 라이브러리) |
| dynamic loader | 동적 로더 | `ld-linux-*.so`, 실행 시 공유 라이브러리를 로드·연결 |
| PT_INTERP | 인터프리터 프로그램 헤더 | 실행 파일이 요구하는 동적 로더 경로 |
| UAPI headers | 커널 유저 API 헤더 | syscall 번호·ioctl·구조체 정의 (`linux-libc-dev`) |
| EABI / AAPCS | ARM 임베디드 ABI / 호출 규약 | 레지스터 사용·스택 정렬·타입 크기 규약 |
| hard-float / softfp / soft | float ABI | float 인자를 VFP 레지스터로 / 정수 레지스터로(FPU 사용) / FPU 미사용 |
| armhf / armel | Debian ARM 포트 | 32-bit ARM hard-float / soft-float(EABI) |
| `-march` / `-mtune` / `-mcpu` | 아키텍처·튜닝·코어 옵션 | 명령어 집합 / 스케줄링 / 둘 다 |
| outline atomics | 아웃라인 원자 연산 | AArch64에서 LSE 유무를 런타임에 판단하는 atomic 도우미 함수 |
| multilib | 멀티립 | 한 컴파일러가 옵션 조합별 라이브러리 여러 벌을 갖는 것 |
| multiarch | 멀티아치 | Debian의 아키텍처별 라이브러리 디렉터리 규칙 |
| newlib / newlib-nano | 임베디드 C 라이브러리 | bare-metal용 libc / 크기 최적화판 |
| semihosting | 세미호스팅 | 타깃이 `svc 0xab`/`bkpt 0xab`로 디버거·에뮬레이터에 I/O를 맡기는 ARM 규약 |
| qemu-user | QEMU 유저 모드 | 다른 아키텍처의 유저 프로세스 하나를 번역 실행, syscall은 호스트 커널로 |
| binfmt_misc | 바이너리 형식 등록 | 커널이 ELF magic을 보고 qemu 같은 인터프리터로 실행하게 하는 기능 |
| `CROSS_COMPILE` / `ARCH` | 커널 빌드 변수 | 툴 접두어 / `arch/` 디렉터리 선택 |
| freestanding | 프리스탠딩 환경 | libc 없이 컴파일러만으로 도는 C 환경 (커널, 펌웨어) |
| vermagic | 모듈 버전 매직 | 모듈이 빌드된 커널 버전·설정 문자열, 로드 시 검사 |

---

## 17. 요약 & 체크리스트

크로스 컴파일은 Don이 이미 하던 일이다. 새로운 것은 Linux 타깃이 **OS 위에서 돈다**는 사실 하나에서 나온다. 결과물이 타깃 libc와 동적 로더에 링크되므로 툴체인은 **sysroot**(타깃 libc·crt·커널 UAPI 헤더)를 함께 들고 있어야 한다. triplet은 그 조합(arch, OS, libc, ABI)의 이름이다. 실행 문제는 네 갈래로 나뉜다. 아키텍처가 다르면 `Exec format error`, 로더나 libc가 없으면 `not found`·`GLIBC_x not found`, `-march`가 너무 높으면 `Illegal instruction`, ABI가 다르면 링크 거부다. 32-bit ARM의 hard/soft-float는 배포판 단위로 정해진다. `-mcpu`는 NEON·LSE 같은 기능과 코드 모양을 바꾸고, 그 결과는 objdump로 직접 확인한다. Clang은 컴파일러 하나로 모든 타깃을 다루지만 sysroot를 빌려 쓴다. 커널과 모듈은 같은 크로스 gcc로 빌드하되 libc 없이(`-nostdinc`, `-mgeneral-regs-only`, `ld -r`) 빌드하고, `ARCH`와 `CROSS_COMPILE`로 툴체인을 고른다. bare-metal에 가장 가까운 Linux 코드가 바로 커널이다.

- [ ] build / host / target을 정의하고 native, cross, Canadian cross를 예로 설명할 수 있다
- [ ] `arm-linux-gnueabihf`, `aarch64-linux-gnu`, `arm-none-eabi`, `riscv64-linux-gnu`의 칸별 의미를 말할 수 있다
- [ ] `-v`, `-###`, `-print-sysroot`, `-print-prog-name`, `-print-file-name`으로 툴체인 부품 위치를 찾을 수 있다
- [ ] `-save-temps`로 .i/.s/.o를 만들고 각 단계에서 무엇이 바뀌는지 설명할 수 있다
- [ ] arm-none-eabi와 arm-linux-gnueabihf의 차이를 printf의 끝(스텁 vs syscall)으로 설명할 수 있다
- [ ] 크로스 바이너리를 `qemu-<arch> -L <sysroot>` 또는 `QEMU_LD_PREFIX`로 실행하고, `-L`이 왜 필요한지 말할 수 있다
- [ ] hard-float / softfp / soft의 기계어 차이와 섞었을 때의 링크 에러를 재현할 수 있다
- [ ] `-march`/`-mtune`/`-mcpu`의 차이를 말하고 너무 높은 `-march`가 SIGILL을 내는 것을 qemu `-cpu`로 보여 줄 수 있다
- [ ] objdump로 NEON 벡터화 여부를 확인하고, ARMv7에서 float 벡터화에 `-ffast-math`가 필요한 이유를 말할 수 있다
- [ ] 툴체인 커널 헤더·glibc 버전이 보드와 어떤 관계여야 하는지 설명할 수 있다
- [ ] `make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-`의 두 변수가 각각 무엇을 정하는지 말할 수 있다
- [ ] 커널 모듈 빌드 플래그(`-nostdinc`, `-mgeneral-regs-only`, `ld -r`)가 유저 프로그램과 왜 다른지 설명할 수 있다

## 참고 자료

- GCC 매뉴얼: [ARM Options](https://gcc.gnu.org/onlinedocs/gcc/ARM-Options.html) (`-mfloat-abi`, `-mfpu=auto`, `-mcpu`), [AArch64 Options](https://gcc.gnu.org/onlinedocs/gcc/AArch64-Options.html) (`-march`, `-mtune`, `-moutline-atomics`, `-mgeneral-regs-only`), [Directory Options](https://gcc.gnu.org/onlinedocs/gcc/Directory-Options.html) (`--sysroot`), [Developer Options](https://gcc.gnu.org/onlinedocs/gcc/Developer-Options.html) (`-save-temps`, `-print-*`), [C Dialect Options](https://gcc.gnu.org/onlinedocs/gcc/C-Dialect-Options.html) (`-ffreestanding`)
- GNU Autoconf 매뉴얼 "Specifying Target Triplets" / "Hosts and Cross-Compilation": [https://www.gnu.org/software/autoconf/manual/autoconf.html](https://www.gnu.org/software/autoconf/manual/autoconf.html)
- Clang: [Cross-compilation using Clang](https://clang.llvm.org/docs/CrossCompilation.html)
- docs.kernel.org: [Building Linux with Clang/LLVM](https://docs.kernel.org/kbuild/llvm.html), [Exporting kernel headers for use by userspace](https://docs.kernel.org/kbuild/headers_install.html), [Building External Modules](https://docs.kernel.org/kbuild/modules.html)
- QEMU: [User Mode Emulation](https://www.qemu.org/docs/master/user/main.html)
- Arm ABI 문서 저장소 (AAPCS, AAPCS64, ELF for the Arm Architecture): [https://github.com/ARM-software/abi-aa](https://github.com/ARM-software/abi-aa)
- Debian Wiki: [ArmHardFloatPort](https://wiki.debian.org/ArmHardFloatPort), [Multiarch](https://wiki.debian.org/Multiarch)
- newlib: [https://sourceware.org/newlib/](https://sourceware.org/newlib/)
- Bootlin, "Embedded Linux system development" 교육 슬라이드 — Cross-compiling toolchains 장: [https://bootlin.com/doc/training/embedded-linux/](https://bootlin.com/doc/training/embedded-linux/)
- Chris Simmonds & Frank Vasquez, "Mastering Embedded Linux Programming" (3rd ed.) — 2장 Learning About Toolchains
- 이 VM에서 직접 본 소스: `/lib/modules/$(uname -r)/build/Makefile` (`CROSS_COMPILE`, `LLVM` 분기), `arch/arm64/Makefile` (`CC_FLAGS_FPU`, `-mgeneral-regs-only`), `scripts/module.lds`
