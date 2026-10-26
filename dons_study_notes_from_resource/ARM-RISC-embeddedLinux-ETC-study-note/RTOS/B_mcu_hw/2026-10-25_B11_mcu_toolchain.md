# B11. MCU 툴체인 특화 — arm-none-eabi · libc · 최적화 · map 분석으로 "플래시 256 KB에 넣기"

> **이 노트를 다 읽으면**: arm-none-eabi 툴체인이 multilib·specs 파일로 libc와 시작 파일을 고르는 과정을 `-###`로 직접 보여 줄 수 있다 · newlib / newlib-nano / picolibc / libc 없음을 같은 프로그램으로 재서 "printf 하나가 몇 KB인가"를 숫자로 답할 수 있다 · syscall stub(`_write`, `_sbrk` …)과 semihosting이 printf를 핀까지 보내는 길을 설명하고, 없을 때 나는 링크 에러를 읽을 수 있다 · `-O` 레벨·LTO·`--gc-sections`·`-fstack-usage`·map 파일로 코드 크기와 타이밍을 측정하고 줄일 수 있다
> **왜 필요한가**: "Our image is 2 KB over flash — what do you do?", "Why does my delay loop disappear at -O2?", "newlib vs newlib-nano?", "What is semihosting?"는 펌웨어 면접 단골이고, 입사 첫 달에 반드시 한 번은 "링커가 `region FLASH overflowed`라고 한다"를 만난다.
> **Don 기준 난이도**: 링커 스크립트·map 파일·`-O2`에서만 깨지는 버그는 SSD 펌웨어에서 이미 겪었다 / libc 세 종류의 실제 크기 차이, specs 파일이라는 GCC 내부 장치, LTO가 asm에서만 부르는 함수를 지우는 함정, picolibc의 `printf_float`, Zephyr의 툴체인 변형(gnuarmemb vs Zephyr SDK)이 새롭다
> **선행 노트**: A3(구성 요소 footprint — printf `%f` 13.7 KB, `setvbuf` 3 KB), B5(리셋부터 main까지, 링커 스크립트), EL B1(크로스 툴체인·triplet), EL B2(C 라이브러리), EL B3(ELF·링킹). 이어지는 노트: F3(스택 크기 산정), F7(재진입성과 C 라이브러리), G2(포트 레이어), H2(Zephyr 빌드), K6(딕셔너리 로깅)
> 🔁 **Bare-Metal 겹침**: BM F5(C 런타임 · libc) · BM F6(최적화와 코드 크기) — 이 노트가 주인. Bare-Metal 노트는 이 노트를 링크하고 "RTOS 없이 crt0부터 직접 쓰는 최소 런타임(자체 `memcpy`·`printf` 구현)" 관점만 더한다

---

## 0. 큰 그림 — 이게 왜 필요한가

SSD 펌웨어를 하던 Don에게 툴체인은 "벤더가 준 Makefile 안의 한 줄"이었을 가능성이 크다. 그런데 RTOS 펌웨어 엔지니어의 일상에는 툴체인 문제가 생각보다 자주 나온다.

- 드론 비행 제어기(STM32H7, FLASH 2 MB)에서는 크기는 여유가 있지만 **`-O2`로 바꾸자 IMU 드라이버의 지연 루프가 사라져서** 센서 초기화가 실패한다.
- 로봇 관절 모터 컨트롤러(Cortex-M4, FLASH 128 KB)에서는 디버그 로그에 `printf("%f")` 하나를 넣자 **링크가 `region FLASH overflowed by 9 KB`로 실패**한다.
- 센서 허브 MCU(Zephyr)에서는 CI가 Zephyr SDK로, 개발자 PC는 Ubuntu의 `arm-none-eabi-gcc`로 빌드해서 **바이너리 크기와 타이밍이 미묘하게 다르다**.
- 새 보드 bring-up 때 printf가 아무것도 안 찍는다 — 알고 보니 **`--specs=nosys.specs`로 링크해서 `_write`가 "항상 실패"하는 가짜 함수**였다.

이 노트는 이 네 장면을 전부 직접 재현하고 숫자로 잰다. 큰 흐름은 "**툴체인이 무엇을 링크하나 → libc를 무엇으로 고르나 → 최적화가 크기와 시간을 어떻게 바꾸나 → map 파일로 어떻게 찾아내나**"다.

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="#888"/></marker></defs>
<text x="340" y="20" font-size="14" text-anchor="middle">arm-none-eabi-gcc 한 번 = 드라이버가 specs를 읽고 도구 다섯 개를 부른다</text>
<rect x="10" y="40" width="120" height="52" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="70" y="62" font-size="13" text-anchor="middle">소스</text>
<text x="70" y="80" font-size="12" text-anchor="middle">main.c, port.c</text>
<rect x="160" y="40" width="150" height="52" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="235" y="62" font-size="13" text-anchor="middle">gcc 드라이버</text>
<text x="235" y="80" font-size="12" text-anchor="middle">specs 파일을 읽는다</text>
<rect x="340" y="40" width="130" height="52" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="405" y="62" font-size="13" text-anchor="middle">cc1 → as</text>
<text x="405" y="80" font-size="12" text-anchor="middle">파일마다 .o</text>
<rect x="500" y="40" width="170" height="52" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="585" y="62" font-size="13" text-anchor="middle">ld (collect2)</text>
<text x="585" y="80" font-size="12" text-anchor="middle">app.elf + app.map</text>
<line x1="130" y1="66" x2="158" y2="66" stroke="#888" marker-end="url(#ah1)"/>
<line x1="310" y1="66" x2="338" y2="66" stroke="#888" marker-end="url(#ah1)"/>
<line x1="470" y1="66" x2="498" y2="66" stroke="#888" marker-end="url(#ah1)"/>
<rect x="140" y="112" width="350" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="315" y="137" font-size="12" text-anchor="middle">multilib: -mcpu, -mfloat-abi → thumb/v7-m/nofp</text>
<line x1="235" y1="92" x2="235" y2="110" stroke="#888" marker-end="url(#ah1)"/>
<text x="20" y="190" font-size="13">링커 입력</text>
<rect x="10" y="202" width="200" height="52" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="110" y="224" font-size="12" text-anchor="middle">시작 파일: crt0.o, crti.o …</text>
<text x="110" y="242" font-size="12" text-anchor="middle">-nostartfiles가 뺀다</text>
<rect x="225" y="202" width="215" height="52" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="332" y="224" font-size="12" text-anchor="middle">libc · libgloss · libgcc</text>
<text x="332" y="242" font-size="12" text-anchor="middle">-nodefaultlibs가 뺀다</text>
<rect x="455" y="202" width="215" height="52" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="562" y="224" font-size="12" text-anchor="middle">링커 스크립트 -T app.ld</text>
<text x="562" y="242" font-size="12" text-anchor="middle">항상 내가 준다 (B5)</text>
<line x1="110" y1="202" x2="110" y2="168" stroke="#888" stroke-dasharray="4 3"/>
<line x1="332" y1="202" x2="332" y2="168" stroke="#888" stroke-dasharray="4 3"/>
<line x1="562" y1="202" x2="562" y2="168" stroke="#888" stroke-dasharray="4 3"/>
<line x1="110" y1="168" x2="600" y2="168" stroke="#888" stroke-dasharray="4 3"/>
<line x1="600" y1="168" x2="600" y2="94" stroke="#888" stroke-dasharray="4 3" marker-end="url(#ah1)"/>
<rect x="10" y="274" width="660" height="52" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="340" y="296" font-size="12" text-anchor="middle">-nostdlib = 둘 다 뺀다 · -ffreestanding은 컴파일 단계 옵션(링크와 무관)</text>
<text x="340" y="314" font-size="12" text-anchor="middle">nano.specs → -lc_nano · nosys.specs → -lnosys · rdimon.specs → -lrdimon + rdimon-crt0.o</text>
</svg>
```
그림 1 — `arm-none-eabi-gcc` 명령 하나 뒤에서 일어나는 일. 드라이버가 specs 파일과 multilib 규칙으로 어떤 libc·시작 파일을 링크할지 정하고, `-nostartfiles` / `-nodefaultlibs` / `-nostdlib`는 그 재료를 빼는 스위치다.

Don의 SSD 경험으로 비유하면 이렇다. SSD 컨트롤러 펌웨어에서 "코드 영역이 ITCM 256 KB에 안 들어간다"는 문제를 풀 때 map 파일을 열어 큰 함수를 찾고, 디버그 문자열을 빼고, `-Os`로 일부 파일만 다시 빌드했을 것이다. RTOS MCU에서도 방법은 같다. 다른 점은 **libc가 이미지의 가장 큰 덩어리일 때가 많다는 것**(A3에서 FreeRTOS 커널 2.4 KB 옆에 printf가 7.7 KB였다)과, **RTOS 포트 코드(PendSV 핸들러 같은 asm)가 최적화·LTO와 부딪히는 지점**이 있다는 것이다.

### 0.1 이 노트의 지도

| 절 | 질문 | 예제 |
|---|---|---|
| 1 | arm-none-eabi 안에는 무엇이 있고, 무엇을 링크하나 | 1, 2 |
| 2 | `-ffreestanding` · `-nostdlib`는 정확히 무엇을 빼나 | 3 |
| 3 | libc 고르기 — newlib / nano / picolibc / 없음 | 4, 5 |
| 4 | syscall stub과 semihosting | 6, 7 |
| 5 | `-O` 레벨 — 크기와 속도 | 8, 9 |
| 6 | 최적화가 타이밍을 바꾼다 — 지연 루프와 `volatile` | 10 |
| 7 | LTO | 11 |
| 8 | `--gc-sections`와 섹션 앵커 | 12 |
| 9 | 스택 사용량 — `-fstack-usage`, puncover | 13 |
| 10 | map 파일·크기 분석 — "내 bloaty" | 14 |
| 11 | GCC vs Clang/LLD, ATfE | 15 |
| 12 | RTOS가 툴체인에 요구하는 것 — FreeRTOS 포트, Zephyr 툴체인 변형 | 16 |

### 실습 환경 한 줄

Lima VM `elinux`(aarch64 Ubuntu 26.04)의 RTOS 랩: QEMU `mps2-an385`(Cortex-M3 @ 25 MHz) 위에 bare-metal · FreeRTOS V11.3.2(ARM_CM3 포트) · Zephyr v4.4.2. 툴체인은 Ubuntu 패키지 `gcc-arm-none-eabi` 14.2.1(newlib 4.6.0, newlib-nano 포함) · `picolibc-arm-none-eabi` 1.8.10 · Ubuntu clang/LLD 21.1.8 · Zephyr SDK 1.0.1의 `arm-zephyr-eabi-gcc` 14.3.0. 시간 숫자는 모두 `ICOUNT=1`(명령 1개 = 32 ns 가상 시간, 매번 같은 값)로 쟀다 — **QEMU icount 기준이라 명령 개수에 비례할 뿐, 캐시·플래시 wait state·버스·파이프라인을 모델링하지 않으므로 실제 칩과 절대값은 다르다.** 크기 숫자(바이트)는 에뮬레이션과 무관하게 실제 칩 빌드와 같다. 소스는 Mac `~/.rtos_scratch/B11/`, 빌드 산출물은 VM `/var/tmp/rtos/B11/`.

랩 템플릿을 그대로 쓰지 않고 자기 폴더에 복사해서 쓴 부분이 있다: libc 비교에서는 `setvbuf`를 부르지 않는 최소 보드 파일 `mb.c`를, LTO 실험에서는 `board_hardfault`에 `used` 속성을 붙인 `common/` 복사본을 썼다(이유는 7절).

---

## 1. arm-none-eabi 해부 — triplet 다음 이야기

> 🔁 이 절은 Bare-Metal BM F5와 겹친다 (주인: 이 노트).

triplet `arm-none-eabi`의 뜻(arch-vendor-os-abi, "OS 없음 + Arm EABI")과 크로스 툴체인의 부품 목록(cc1, as, ld, binutils, sysroot)은 EL B1에서 했다. 여기서는 **MCU에서만 중요한 두 장치 — multilib과 specs 파일** — 을 본다.

### 1.1 multilib — 같은 libc를 CPU마다 따로 빌드해 둔 선반

직관: 툴체인 패키지 안에는 libc가 한 벌이 아니라 **수십 벌** 들어 있다. Cortex-M0+(Armv6-M, Thumb-1만)용, Cortex-M3(Armv7-M)용, FPU 있는 Cortex-M4F(hard float)용 … 각각 다른 명령어와 다른 부동소수점 호출 규약으로 컴파일된 `libc.a`가 서로 다른 폴더에 있다. 이걸 **multilib**이라고 한다. 컴파일러 드라이버는 `-mcpu` · `-march` · `-mfloat-abi` · `-mfpu` 조합을 보고 그중 한 폴더를 고른다.

```sh
arm-none-eabi-gcc -print-multi-lib | head -3          # 선반 목록 (폴더;옵션)
arm-none-eabi-gcc -print-multi-lib | wc -l
```

```text
.;
arm/v5te/softfp;@marm@march=armv5te+fp@mfloat-abi=softfp
arm/v5te/hard;@marm@march=armv5te+fp@mfloat-abi=hard
39
```

39개 중 Cortex-M에 쓰이는 것은 `thumb/v6-m/nofp`, `thumb/v7-m/nofp`, `thumb/v7e-m/...`, `thumb/v8-m.base/...`, `thumb/v8-m.main/...`, `thumb/v8.1-m.main+mve/hard`, `+pacbti` 계열이다.

#### 예제 1 — 옵션 조합이 어느 선반을 고르나

무엇을 확인하나: 같은 툴체인에 CPU 옵션만 바꿔 주면 어떤 multilib 폴더(= 어떤 libc.a, libgcc.a)를 쓰는지 본다.

```sh
for f in "-mcpu=cortex-m0plus" "-mcpu=cortex-m3" "-mcpu=cortex-m4" \
         "-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard" \
         "-mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard" \
         "-mcpu=cortex-m33 -mfloat-abi=hard -mfpu=fpv5-sp-d16" \
         "-mcpu=cortex-m55" "-mcpu=cortex-r52"; do
  printf "%-52s -> %s\n" "$f" "$(arm-none-eabi-gcc -mthumb $f -print-multi-directory)"
done
```

```text
-mcpu=cortex-m0plus                                  -> thumb/v6-m/nofp
-mcpu=cortex-m3                                      -> thumb/v7-m/nofp
-mcpu=cortex-m4                                      -> thumb/v7e-m/nofp
-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard   -> thumb/v7e-m+fp/hard
-mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard      -> thumb/v7e-m+dp/hard
-mcpu=cortex-m33 -mfloat-abi=hard -mfpu=fpv5-sp-d16  -> thumb/v8-m.main+fp/hard
-mcpu=cortex-m55                                     -> thumb/v8-m.main/nofp
-mcpu=cortex-r52                                     -> thumb/v7/nofp
```

출력에서 볼 것: (1) `-mcpu=cortex-m4`만 주면 **FPU가 있는 칩인데도 `nofp`** 선반을 쓴다 — `-mfloat-abi=hard`(그리고 `-mfpu`)를 줘야 FPU 명령과 하드 float ABI로 빌드된 libc가 붙는다. 드론 FC의 자세 제어 루프가 이유 없이 느리다면 제일 먼저 볼 곳이다. (2) Cortex-M55도 `-mfloat-abi`를 안 주면 `nofp`다(Helium·FPU 미사용). (3) **Cortex-R52는 맞는 선반이 없어서 범용 `thumb/v7/nofp`로 떨어졌다.** Armv8-R 전용 multilib이 이 패키지에 없기 때문이다. 링크는 되지만 R52의 기능을 쓰지 못하는 libc다 — 이런 "조용한 fallback"은 에러가 나지 않으니 `-print-multi-directory`로 꼭 확인한다(B9 Cortex-R).

함정: 오브젝트마다 `-mfloat-abi`가 다르면 링커가 `uses VFP register arguments, ... does not`로 거부한다(EL B1 6절). MCU에서는 **컴파일 옵션과 링크 옵션에 같은 CPU 플래그를 넘겨야** 링크 때도 같은 선반을 고른다. Makefile에서 링크 줄에 `-mthumb -mcpu=`를 빼먹으면 어떻게 되는지 직접 해 봤다(오브젝트는 Cortex-M3용으로 컴파일, 링크만 CPU 플래그 없이):

```sh
arm-none-eabi-gcc *.o -T mps2_m3.ld -nostartfiles --specs=nano.specs -Wl,--gc-sections -o app.elf
```

```text
ld: <lib>/libc_nano.a(libc_a-memcpy-stub.o)(memcpy): Unknown destination type (ARM/Thumb) in startup.o
startup.o: in function `Reset_Handler':
startup.c:(.text.Reset_Handler+0x18): dangerous relocation: unsupported relocation
ld: <lib>/libc_nano.a(libc_a-memset.o)(memset): Unknown destination type (ARM/Thumb) in startup.o
startup.c:(.text.Reset_Handler+0x32): dangerous relocation: unsupported relocation
ld: <lib>/libc_nano.a(libc_a-printf.o)(printf): Unknown destination type (ARM/Thumb) in app.o
...
```

(`<lib>` = `/usr/lib/gcc/arm-none-eabi/14.2.1/../../../arm-none-eabi/lib/` — `thumb/v7-m/nofp/`가 아닌 **기본 선반 `.`**, 즉 ARM 모드 libc다.) Thumb 전용 코어용 오브젝트가 ARM 명령 libc를 부르려 해서 링커가 거부했다. 다행히 이 경우는 링크 에러로 드러난다. **"Unknown destination type (ARM/Thumb)"과 "dangerous relocation"은 multilib 불일치의 신호**로 기억해 두자. 2.2절의 `-nostdlib` 에러에도 같은 문구가 나오는데, 그때는 심볼 자체가 없어서 링커가 대상의 명령 집합을 모를 때 나온다.

### 1.2 specs 파일 — 드라이버의 "링크 레시피"

직관: `gcc` 드라이버는 "컴파일러"가 아니라 **명령 조립기**다(EL B1 3.3의 `-###`). 어떤 시작 파일(crt0.o)과 어떤 라이브러리를 링크 명령에 붙일지는 GCC 안에 내장된 **specs**라는 작은 규칙 언어로 정해진다. `--specs=파일`은 이 규칙을 덮어쓰는 파일을 추가로 읽으라는 뜻이다. MCU 세계에서 자주 보는 것:

| specs 파일 | 하는 일 (실제 파일 내용 기준) | 언제 쓰나 |
|---|---|---|
| `nano.specs` | `-lc` → `-lc_nano`, `-lg` → `-lg_nano`로 바꾸고 헤더 경로에 `/usr/include/newlib/nano` 추가 | newlib-nano를 쓸 때 (거의 항상) |
| `nosys.specs` | `-lnosys` 추가 — "항상 실패하는" syscall 가짜 구현 | 링크만 통과시키고 싶을 때 (위험) |
| `rdimon.specs` | `-lrdimon` 추가 + 시작 파일을 `rdimon-crt0.o`로 | semihosting으로 printf·파일 I/O |
| `picolibc.specs` | 헤더·라이브러리 경로를 picolibc로, `-T` 없으면 `picolibc.ld`, `--gc-sections` 자동, `--printf=` 옵션 | picolibc를 쓸 때 |

`nano.specs`의 핵심 줄(VM의 `/usr/lib/arm-none-eabi/newlib/nano.specs`):

```text
*cpp_unique_options:
-isystem /usr/include/newlib/nano %(nano_cpp_unique_options)

*link:
%(nano_link) %:replace-outfile(-lc -lc_nano) %:replace-outfile(-lg -lg_nano) ...
```

`picolibc.specs`에는 `%{-printf=*:--defsym=vfprintf=__%*_vfprintf}` 같은 줄이 있다. `--printf=i`라고 쓰면 링커에게 "`vfprintf`라는 이름은 `__i_vfprintf`(정수 전용판)를 가리켜라"고 시키는 것이다. 이게 3절에서 2 KB짜리 printf를 만드는 장치다.

#### 예제 2 — 옵션마다 링커에 실제로 넘어가는 재료

무엇을 확인하나: `-###`로 드라이버가 `collect2`(ld)에 넘기는 명령을 뽑아서 **시작 파일(.o)·라이브러리(-l)·링커 스크립트(-T)만** 비교한다.

```sh
#!/bin/bash
# linkline.sh — 같은 t.c를 옵션별로 링크할 때 collect2(ld)에 넘어가는 시작 파일·라이브러리만 뽑는다
cd /var/tmp/rtos/B11
for sp in "--specs=nano.specs --specs=nosys.specs" "--specs=rdimon.specs" "--specs=picolibc.specs" \
          "-nostartfiles --specs=nano.specs" "-nodefaultlibs" "-nostdlib"; do
  printf "%-40s : " "$sp"
  arm-none-eabi-gcc -### -mthumb -mcpu=cortex-m3 $sp t.c -o t.elf 2>&1 | grep collect2 | tr -d '"' | tr ' ' '\n' |
    grep -E '\.o$|^-l|^-T|^--(start|end)-group' | grep -v '^/tmp/' | sed 's|.*/||' | tr '\n' ' '
  echo
done
```

```text
--specs=nano.specs --specs=nosys.specs   : crti.o crtbegin.o crt0.o --start-group -lgcc -lc_nano --end-group --start-group -lgcc -lc_nano -lnosys --end-group --start-group -lgcc -lc_nano -lnosys --end-group crtend.o crtn.o 
--specs=rdimon.specs                     : crti.o crtbegin.o rdimon-crt0.o --start-group -lgcc -lc --end-group --start-group -lgcc -lc -lrdimon --end-group crtend.o crtn.o 
--specs=picolibc.specs                   : -Tpicolibc.ld crt0.o --start-group -lgcc --start-group -lgcc -lc --end-group --end-group 
-nostartfiles --specs=nano.specs         : --start-group -lgcc -lc_nano --end-group --start-group -lgcc -lc_nano --end-group 
-nodefaultlibs                           : crti.o crtbegin.o crt0.o crtend.o crtn.o 
-nostdlib                                :
```

출력에서 볼 것:

- `nano.specs`가 `-lc`를 `-lc_nano`로 **바꿔치기**했다. 라이브러리 그룹이 여러 번 반복되는 것은 specs 파일이 겹쳐 쌓이면서 생긴 것이고 해롭지 않다(`--start-group`은 서로 부르는 아카이브를 여러 번 훑게 하는 장치 — EL B3 4절).
- `rdimon.specs`는 시작 파일까지 `rdimon-crt0.o`로 바꾼다. 우리 랩처럼 **자기 startup.c를 쓰면서 `-nostartfiles`를 주면 이 crt0이 빠지므로**, semihosting 초기화 함수 `initialise_monitor_handles()`를 직접 불러야 한다(예제 7).
- `picolibc.specs`는 `-Tpicolibc.ld`(picolibc의 기본 링커 스크립트)를 붙인다. 우리가 `-T`를 주면 specs의 `%{!T:...}` 조건 때문에 빠진다.
- 랩 템플릿의 조합 `-nostartfiles --specs=nano.specs`는 **시작 파일 없음 + newlib-nano + libgcc**다. `crt0.o`를 뺀 대신 `startup.c`의 `Reset_Handler`가 .data 복사·.bss 0 채우기를 한다(B5).
- `-nodefaultlibs`는 라이브러리만, `-nostdlib`는 시작 파일과 라이브러리를 **둘 다** 뺀다. `-nostdlib`이면 **libgcc까지 빠진다**는 점이 중요하다 — 다음 절.

---

## 2. `-ffreestanding` · `-nostdlib`는 정확히 무엇을 빼나

### 2.1 직관 — 컴파일 단계 옵션 vs 링크 단계 옵션

두 종류를 섞어서 오해하는 일이 많다.

| 옵션 | 단계 | 정확한 뜻 |
|---|---|---|
| `-ffreestanding` | 컴파일 | "표준 라이브러리가 있다고 가정하지 마라" — `-fno-builtin`을 포함한다. `main`이 특별하지 않다. `printf("x\n")` → `puts` 같은 **라이브러리 의미에 기댄 변환을 하지 않는다** |
| `-fno-builtin` | 컴파일 | `memcpy`·`strlen` 같은 이름을 컴파일러 내장 함수로 취급하지 않는다 |
| `-nostartfiles` | 링크 | crt0.o·crti.o·crtbegin.o 등 시작 파일을 빼라 |
| `-nodefaultlibs` | 링크 | libc·libgcc 등 기본 라이브러리를 빼라 |
| `-nostdlib` | 링크 | 위 둘 다 |

그리고 GCC 문서가 분명히 적어 두었지만 잘 안 읽히는 문장이 있다: **freestanding 환경에서도 GCC는 `memcpy`, `memmove`, `memset`, `memcmp`가 있다고 가정한다.** 구조체 대입·큰 배열 초기화를 컴파일러가 이 함수 호출로 바꿀 수 있기 때문이다. 커널(Linux)도 이 네 함수를 직접 제공한다(EL B1 11절).

### 2.2 예제 3 — 내가 부르지 않은 `memcpy`가 생긴다

무엇을 확인하나: 소스에 `memcpy`·`memset`·`puts`가 하나도 없는데, 컴파일러가 만든 오브젝트는 무엇을 부르는가. 옵션별로 `objdump`에서 분기 대상만 뽑는다.

```c
/* fs.c — "-ffreestanding이면 libc 호출이 안 생긴다"는 오해 확인 */
#include <stdint.h>
int printf(const char *, ...);
struct frame { uint32_t w[32]; };          /* 128 B 구조체 */
static struct frame g_a, g_b;
static uint8_t g_buf[256];

void copy_frame(void)  { g_b = g_a; }      /* 구조체 대입 */
void clear_buf(void)   { for (int i = 0; i < 256; i++) g_buf[i] = 0; }  /* 0 채우기 루프 */
void hello(void)       { printf("hello\n"); }
int  read_b(int i)     { return (int)g_b.w[i & 31] + g_buf[i & 255]; }
```

```sh
for f in "" "-ffreestanding" "-ffreestanding -fno-tree-loop-distribute-patterns"; do
  printf "%-55s calls: " "-O2 $f"
  arm-none-eabi-gcc -mthumb -mcpu=cortex-m3 -O2 $f -Wall -Wextra -c fs.c -o fs.o &&
  arm-none-eabi-objdump -d fs.o | grep -oE "b(l|\.w)?\s+[0-9a-f]+ <[a-z_]+>" | sed "s/.*<//; s/>//" | sort | uniq | tr "\n" " "
  echo
done
```

```text
-O2                                                     calls: memcpy memset puts 
-O2 -ffreestanding                                      calls: memcpy printf 
-O2 -ffreestanding -fno-tree-loop-distribute-patterns   calls: memcpy printf 
```

출력에서 볼 것:

- 보통(hosted) `-O2`: 구조체 대입 → `memcpy`, 0 채우기 루프 → `memset`(GCC의 **loop distribution** 최적화가 "이 루프는 memset이다"를 알아본다), `printf("hello\n")` → `puts`(인자가 개행으로 끝나는 상수 문자열이면 같은 동작).
- `-ffreestanding`: `puts` 변환은 사라졌고(라이브러리 의미를 가정 안 함), 이 GCC 14에서는 루프 → `memset` 변환도 꺼졌다. 그러나 **구조체 대입의 `memcpy`는 그대로**다.

그래서 `-nostdlib`로 링크하면 이렇게 된다. 랩 `startup.c`의 `.data` 복사 루프와 `.bss` 0 채우기 루프(`while (dst < &_edata) *dst++ = *src++;`)를 `-O2`(hosted)로 컴파일하고 libc 없이 링크했을 때의 실제 출력이다:

```sh
arm-none-eabi-gcc -mthumb -mcpu=cortex-m3 -O2 ... -DNOLIBC=1 app.c mb.c startup.c \
  -T mps2_m3.ld -nostartfiles -Wl,--gc-sections -nostdlib -lgcc -o app.elf
```

```text
.../arm-none-eabi/bin/ld: /tmp/ccXFfeo8.o: in function `Reset_Handler':
/var/rtoslab/templates/common/startup.c:42:(.text.Reset_Handler+0x18): undefined reference to `memcpy'
.../arm-none-eabi/bin/ld: (memcpy): Unknown destination type (ARM/Thumb) in /tmp/ccXFfeo8.o
/var/rtoslab/templates/common/startup.c:42:(.text.Reset_Handler+0x18): dangerous relocation: unsupported relocation
.../arm-none-eabi/bin/ld: /var/rtoslab/templates/common/startup.c:43:(.text.Reset_Handler+0x32): undefined reference to `memset'
...
collect2: error: ld returned 1 exit status
```

(긴 경로는 `...`로 줄였다.) **소스 42행은 손으로 쓴 복사 루프인데 링커는 `memcpy`가 없다고 한다.** 이게 "-O0에서는 링크되는데 -O2에서는 memcpy undefined"의 정체다. 고치는 법은 세 가지다: (1) `memcpy`/`memset`을 직접 구현해서 넣는다(Linux 커널 방식, Bare-Metal 노트의 최소 런타임), (2) 시작 코드만 `-fno-tree-loop-distribute-patterns`(또는 `-ffreestanding`)로 컴파일한다, (3) libc를 링크한다. 위 빌드에 (2)를 적용하면 링크가 되고, libc 없이 **848 B**짜리 이미지가 나온다(예제 4의 맨 위 행).

함정 하나 더: 직접 만든 `memset` 안의 0 채우기 루프를 `-O2`로 컴파일하면 GCC가 그 루프를 **`memset` 호출로 바꿔서 무한 재귀**가 된다. 자기 `mem*` 구현 파일에는 반드시 `-fno-tree-loop-distribute-patterns`(또는 `-ffreestanding`)를 준다.

### 2.3 Don 경험과 연결

SSD 펌웨어에서 ROM 코드나 부트로더를 만들 때 "libc 없이" 빌드했다면, 아마 벤더 SDK가 `memcpy`를 이미 넣어 두었을 것이다. 그 `memcpy`가 왜 필요한지 — 내가 안 불러도 컴파일러가 부르기 때문 — 가 이 절의 요점이다. 그리고 **`-nostdlib`는 libgcc까지 뺀다**: Cortex-M0의 정수 나눗셈(`__aeabi_uidiv`), 64비트 시프트(`__aeabi_llsl`), 소프트 float(`__aeabi_dmul`)는 libgcc에 있으므로, `-nostdlib`를 쓰면 `-lgcc`를 다시 붙여야 한다(위 명령에도 붙였다).

---

## 3. libc 고르기 — newlib · newlib-nano · picolibc · 없음

> 🔁 이 절은 Bare-Metal BM F5와 겹친다 (주인: 이 노트).

### 3.1 직관 — MCU libc는 "printf 엔진 + malloc + 문자열 함수 묶음"

Linux의 glibc는 syscall 래퍼·스레드·로케일·동적 로더까지 품은 거대한 층이었다(EL B2). MCU의 libc는 훨씬 단순하다. 실제로 이미지 크기를 좌우하는 것은 세 가지뿐이다.

1. **포맷 엔진** — `printf`·`snprintf`가 공유하는 `vfprintf`. 정수만이냐, `%f`(double → 10진 변환 `dtoa`)까지냐에 따라 2 KB에서 20 KB 이상까지 벌어진다.
2. **stdio 버퍼와 FILE 구조체** — newlib은 `stdout`에 버퍼를 `malloc`으로 붙인다. 그래서 printf가 malloc을 끌고 온다(아래에서 확인).
3. **malloc** — newlib(full)은 dlmalloc 계열(빈 목록 테이블 `__malloc_av_` 1 KB), newlib-nano는 단순 free list, picolibc는 자체 작은 할당기.

그리고 OS가 없으니 libc가 "아래층"에 기대는 부분 — 문자 하나를 어디로 내보내나, 힙 메모리를 어디서 얻나 — 은 **내가 채운다.** 이게 4절의 syscall stub이다.

| libc | 출신 · 라이선스 | 특징 | 누가 기본으로 쓰나 |
|---|---|---|---|
| newlib (full) | Red Hat, BSD 계열 | 기능 완전, `_reent` 구조체로 재진입, 크다 | GNU Arm 툴체인 기본(`-lc`) |
| newlib-nano | 같은 소스를 크기 옵션으로 다시 빌드 | printf에서 float 기본 제외(`-u _printf_float`로 켬), 작은 malloc | STM32CubeIDE·MCUXpresso 등 벤더 IDE 기본(`nano.specs`) |
| picolibc | Keith Packard, newlib + AVR libc 기반 | tinystdio(버퍼 없는 stdio), TLS 기반 errno, printf 수준 선택 | Zephyr 기본 libc, ATfE 기본 libc |
| LLVM-libc | LLVM 프로젝트 | 새로 쓰는 libc, 임베디드 지원 진행 중 | ATfE에 평가용 오버레이로 제공(11절) |
| 없음 (자체 구현) | — | 필요한 함수만 직접 (`memcpy`·작은 포맷터) | 부트로더, 아주 작은 MCU |

### 3.2 예제 4 — 같은 프로그램, libc 다섯 가지

무엇을 확인하나: 기능을 하나씩 켜는 같은 `app.c`를 libc만 바꿔 빌드해서 FLASH(text)·data·bss를 잰다. 보드 파일은 libc를 전혀 부르지 않는 `mb.c`(UART·us 시계·종료만)를 써서 **순수하게 libc 차이만** 보이게 했다(A3에서 본 랩 `board.c`의 `setvbuf` 3 KB 잡음을 없앴다).

```c
/* app.c — libc 비교용 같은 프로그램. 기능을 매크로로 하나씩 켠다 */
#include "board.h"
#if !NOLIBC
#include <stdio.h>
#include <stdlib.h>
#endif
volatile float g_temp = 36.5f;
int main(void)
{
#if NOLIBC
    board_printf_raw("hello %d\n", 42);
#else
    printf("hello %d\n", 42);
#endif
#if USE_SNPRINTF
    char buf[32];
    snprintf(buf, sizeof buf, "id=%04x", 0xBEEFu);
    printf("%s\n", buf);
#endif
#if USE_MALLOC
    char *p = malloc(100);
    printf("malloc %s\n", p ? "ok" : "fail");
    free(p);
#endif
#if USE_FLOAT
    printf("temp=%.2f\n", (double)g_temp);
#endif
    return 0;
}
```

picolibc용 출력 연결(`pico_io.c`) — tinystdio에는 `_write`가 없고 **`stdout`이라는 FILE 객체를 내가 정의**해야 printf가 링크된다(랩 README의 경고 그대로다):

```c
/* picolibc(tinystdio)용: stdout을 UART에 묶는다. syscall stub가 필요 없다 */
#include <stdio.h>
#include "board.h"
static int uart_put(char c, FILE *f) { (void)f; board_putc(c); return (unsigned char)c; }
static FILE uart_file = FDEV_SETUP_STREAM(uart_put, NULL, NULL, _FDEV_SETUP_WRITE);
FILE *const stdout = &uart_file;
```

빌드 스크립트의 libc별 링크 줄(공통 플래그 `-mthumb -mcpu=cortex-m3 -O2 -ffunction-sections -fdata-sections`, `-T mps2_m3.ld -nostartfiles -Wl,--gc-sections`):

```sh
#!/bin/bash
# build.sh <name> <libc> [CFLAGS...] — app.c를 libc별로 빌드하고 크기를 잰다 (핵심 줄만)
case $L in
  none)   arm-none-eabi-gcc $CF -DNOLIBC=1 -fno-tree-loop-distribute-patterns "$@" $SRC $LD -nostdlib -lgcc -o $O/app.elf ;;
  newlib) arm-none-eabi-gcc $CF "$@" $SRC $S/stubs_newlib.c $LD -o $O/app.elf ;;
  nano)   arm-none-eabi-gcc $CF "$@" $SRC $S/stubs_newlib.c $LD --specs=nano.specs -o $O/app.elf ;;
  nanof)  arm-none-eabi-gcc $CF "$@" $SRC $S/stubs_newlib.c $LD --specs=nano.specs -u _printf_float -o $O/app.elf ;;
  pico)   arm-none-eabi-gcc --specs=picolibc.specs $CF "$@" $SRC $S/pico_io.c $LD -o $O/app.elf ;;
  pico-i) arm-none-eabi-gcc --specs=picolibc.specs --printf=i $CF "$@" $SRC $S/pico_io.c $LD -o $O/app.elf ;;
esac
arm-none-eabi-size $O/app.elf | tail -1 | awk -v n=$N '{printf "%-22s text=%6d data=%5d bss=%5d\n", n, $1, $2, $3}'
```

```sh
B=/Users/donh/.rtos_scratch/B11/libc/build.sh
$B none none && rtos-run-mps2 /var/tmp/rtos/B11/libc/none/app.elf
for L in newlib nano pico pico-i; do
  $B ${L}_P $L; $B ${L}_PS $L -DUSE_SNPRINTF=1; $B ${L}_PSM $L -DUSE_SNPRINTF=1 -DUSE_MALLOC=1
done
for L in newlib nanof pico; do $B ${L}_PSMF $L -DUSE_SNPRINTF=1 -DUSE_MALLOC=1 -DUSE_FLOAT=1; done
```

```text
none                   text=   848 data=    0 bss=    0
hello 42
[exit 0]
newlib_P               text= 29916 data= 1724 bss=  372
newlib_PS              text= 37428 data= 1724 bss=  372
newlib_PSM             text= 37524 data= 1724 bss=  372
nano_P                 text=  7068 data=   96 bss=  328
nano_PS                text=  8544 data=   96 bss=  328
nano_PSM               text=  8640 data=   96 bss=  328
pico_P                 text=  7872 data=   16 bss=    0
pico_PS                text=  8072 data=   16 bss=    0
pico_PSM               text=  8664 data=   20 bss=   24
pico-i_P               text=  2152 data=   16 bss=    0
pico-i_PS              text=  2352 data=   16 bss=    0
pico-i_PSM             text=  2944 data=   20 bss=   24
newlib_PSMF            text= 37556 data= 1724 bss=  372
nanof_PSMF             text= 22016 data=  464 bss=  328
pico_PSMF              text=  9592 data=   24 bss=   28
```

모든 변형을 QEMU에서 실행해서 출력이 같은지도 확인했다(float 변형의 실행 출력):

```text
hello 42
id=beef
malloc ok
temp=36.50
[exit 0]
```

```svg
<svg viewBox="0 0 660 360" xmlns="http://www.w3.org/2000/svg">
<text x="330" y="24" font-size="14" text-anchor="middle">같은 프로그램(printf + snprintf + malloc)의 FLASH(text) — libc별</text>
<text x="222" y="66" font-size="12" text-anchor="end">libc 없음 (자체 포맷터)</text>
<rect x="230" y="50" width="8.1" height="24" fill="#888" fill-opacity="0.75" stroke="#888"/>
<text x="244.1" y="66" font-size="12">848 B</text>
<text x="222" y="102" font-size="12" text-anchor="end">picolibc --printf=i</text>
<rect x="230" y="86" width="28.0" height="24" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="264.0" y="102" font-size="12">2,944 B</text>
<text x="222" y="138" font-size="12" text-anchor="end">newlib-nano</text>
<rect x="230" y="122" width="82.1" height="24" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="318.1" y="138" font-size="12">8,640 B</text>
<text x="222" y="174" font-size="12" text-anchor="end">picolibc (기본, double)</text>
<rect x="230" y="158" width="82.3" height="24" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="318.3" y="174" font-size="12">8,664 B</text>
<text x="222" y="210" font-size="12" text-anchor="end">picolibc + %f 사용</text>
<rect x="230" y="194" width="91.1" height="24" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="327.1" y="210" font-size="12">9,592 B</text>
<text x="222" y="246" font-size="12" text-anchor="end">newlib-nano + _printf_float</text>
<rect x="230" y="230" width="209.2" height="24" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="445.2" y="246" font-size="12">22,016 B</text>
<text x="222" y="282" font-size="12" text-anchor="end">newlib (full)</text>
<rect x="230" y="266" width="356.5" height="24" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="592.5" y="282" font-size="12">37,524 B</text>
<line x1="230" y1="302" x2="610" y2="302" stroke="currentColor"/>
<line x1="230.0" y1="302" x2="230.0" y2="307" stroke="currentColor"/>
<text x="230.0" y="321" font-size="12" text-anchor="middle">0 KB</text>
<line x1="325.0" y1="302" x2="325.0" y2="307" stroke="currentColor"/>
<text x="325.0" y="321" font-size="12" text-anchor="middle">10 KB</text>
<line x1="420.0" y1="302" x2="420.0" y2="307" stroke="currentColor"/>
<text x="420.0" y="321" font-size="12" text-anchor="middle">20 KB</text>
<line x1="515.0" y1="302" x2="515.0" y2="307" stroke="currentColor"/>
<text x="515.0" y="321" font-size="12" text-anchor="middle">30 KB</text>
<line x1="610.0" y1="302" x2="610.0" y2="307" stroke="currentColor"/>
<text x="610.0" y="321" font-size="12" text-anchor="middle">40 KB</text>
<text x="330" y="344" font-size="12" text-anchor="middle">arm-none-eabi-gcc 14.2 · Cortex-M3 · -O2 · gc-sections · 보드/스타트업 코드 포함</text>
</svg>
```
그림 2 — 같은 기능(printf + snprintf + malloc)을 libc만 바꿔 빌드한 FLASH. 보드·스타트업 코드(약 0.8 KB)가 모두에 포함되어 있다. 측정: 예제 4.

출력에서 볼 것 — 다섯 가지 발견:

1. **newlib(full)은 printf 하나로 29.9 KB**다. full newlib의 `vfprintf`는 float을 항상 포함하므로, `%f`를 안 써도 `_dtoa_r`(4 KB)과 소프트 double 연산이 딸려 온다. `.data` 1,724 B는 `_impure_data`(재진입 구조체 `struct _reent`)와 `__malloc_av_`(1 KB짜리 malloc 빈 테이블)다. RAM 2 KB가 printf 하나로 사라진다.
2. **newlib-nano는 7.1 KB, picolibc 기본은 7.9 KB로 비슷하다.** 그러나 picolibc 기본 printf는 이미 **double까지 지원**하는 판(`__d_vfprintf`)이고 `%f`를 실제로 써도 9.6 KB다. 같은 일을 newlib-nano로 하려면 `-u _printf_float`로 22.0 KB가 된다. **float 로그가 필요하면 picolibc가 12 KB 이상 작다.**
3. **picolibc `--printf=i`(정수 전용)는 2.2 KB**다. 같은 printf API를 그대로 쓰면서 newlib-nano의 1/3이다. `%f`를 넘기면 숫자 대신 `*float*`라고 찍는다(아래 예제 5에서 확인).
4. **newlib/nano는 malloc을 켜도 96 B만 늘었다.** 이미 printf가 malloc을 끌고 왔기 때문이다(바로 아래 `nm`).
5. **picolibc는 bss 0**이다. tinystdio는 stdio 버퍼가 없어서 printf가 RAM을 먹지 않는다. newlib-nano의 bss 328 B 중 312 B는 `__sf`(FILE 구조체 3개)다.

무엇이 그렇게 끌려오는지 `nm`으로 확인한다(newlib-nano, printf만 쓴 빌드):

```sh
arm-none-eabi-nm -S --size-sort nano_P/app.elf | grep -iE "malloc|sbrk|impure|__sf|_reent"
```

```text
00001918 00000002 T __malloc_lock
0000191c 00000002 T __malloc_unlock
00000f0c 00000002 T __sfp_lock_acquire
00000f10 00000002 T __sfp_lock_release
200001a0 00000004 B __malloc_free_list
2000019c 00000004 B __malloc_sbrk_start
20000004 00000004 D _impure_ptr
00001970 00000010 T _malloc_usable_size_r
0000031c 00000020 T _sbrk
00001ab0 00000024 T _sbrk_r
0000050c 00000026 t __sfputc_r
00000534 0000002e T __sfputs_r
20000008 0000004c D _impure_data
00001358 0000004c t sbrk_aligned
00000e48 000000a4 T __sfp
000013a4 00000100 T _malloc_r
00000f3c 00000110 T __sflush_r
20000060 00000138 B __sf
0000158c 000002d8 T __sfvwrite_r
```

`malloc`을 부르지 않았는데 `_malloc_r`·`_sbrk`가 있다. newlib의 stdio는 **처음 출력할 때 `__smakebuf_r`가 `stdout` 버퍼를 `_malloc_r`로 할당**하기 때문이다. 그리고 `__malloc_lock`·`__sfp_lock_acquire`가 **2바이트**(= `bx lr` 하나)다 — 기본 구현은 아무것도 안 하는 빈 함수라는 뜻이다. RTOS에서 여러 태스크가 printf·malloc을 동시에 부르면 이 빈 락 때문에 힙이 깨질 수 있다. 이것이 F7(재진입성과 C 라이브러리)의 출발점이다: FreeRTOS는 `configUSE_NEWLIB_REENTRANT`(태스크마다 `struct _reent`를 TCB에 둠, `include/newlib-freertos.h`)와 `configUSE_PICOLIBC_TLS`(`include/picolibc-freertos.h`)를 제공하고, 락 함수는 내가 뮤텍스로 구현해야 한다. 자세한 것은 F7.

### 3.3 예제 5 — picolibc의 printf 수준과 `printf_float` 함정

picolibc는 printf 구현을 다섯 수준으로 갖고 있고 링크 때 `vfprintf`라는 이름을 어느 것에 묶을지 고른다(1.2절의 `--defsym`).

| 옵션 | 묶이는 함수 | 지원 |
|---|---|---|
| (기본) 또는 `--printf=d` | `__d_vfprintf` | double까지 전부 |
| `--printf=f` | `__f_vfprintf` | float(32비트)만 — 인자를 `printf_float()`으로 감싸야 한다 |
| `--printf=l` | `__l_vfprintf` | 정수 + long long, float 없음 (Zephyr 기본이 이것) |
| `--printf=i` | `__i_vfprintf` | 정수만 |
| `--printf=m` | `__m_vfprintf` | 최소 (폭·정밀도 일부만) |

무엇을 확인하나: `--printf=f`(float 전용)에서 C의 기본 규칙대로 double을 넘기면 무슨 일이 생기나.

```c
/* picolibc --printf=f 에서 float 인자를 printf_float()으로 감싸야 하는지 확인 */
#include <stdio.h>
#include "board.h"
volatile float g_temp = 36.5f;
int main(void)
{
    printf("raw    temp=%.2f\n", (double)g_temp);          /* 잘못: double을 넘김 */
    printf("macro  temp=%.2f\n", printf_float(g_temp));    /* 맞음: picolibc 매크로 */
    return 0;
}
```

```sh
arm-none-eabi-gcc --specs=picolibc.specs --printf=f -mthumb -mcpu=cortex-m3 -O2 -std=gnu11 -Wall -Wextra \
  -ffunction-sections -fdata-sections -I$S -I$T app_pf.c mb.c startup.c pico_io.c \
  -T mps2_m3.ld -nostartfiles -Wl,--gc-sections -o app.elf && arm-none-eabi-size app.elf && rtos-run-mps2 app.elf
grep -n "printf_float" /usr/lib/picolibc/arm-none-eabi/include/stdio.h | head -5
```

```text
   text	   data	    bss	    dec	    hex	filename
   8072	     20	      4	   8096	   1fa0	/var/tmp/rtos/B11/libc/pf/app.elf
raw    temp=3.04
macro  temp=36.50
[exit 0]
496:__printf_float(float f)
508:# define printf_float(x) ((double) (x))
516:# define printf_float(x) ((double) (x))
530:# define printf_float(x) ((double) (x))
542:# define printf_float(x) __printf_float(x)
```

출력에서 볼 것: **컴파일 경고 없이 36.50이 3.04로 찍혔다.** C의 가변 인자에서 float은 항상 double(64비트)로 올라가는데, `__f_vfprintf`는 32비트 float 비트 패턴을 기대한다. `printf_float(x)`는 `--printf=f`일 때만 float을 32비트 정수 슬롯에 담아 넘기는 함수(`__printf_float`)가 되고, 다른 수준에서는 그냥 `(double)` 캐스트다(헤더 508~542행). 그래서 **picolibc 코드에서는 float 출력을 항상 `printf_float()`으로 감싸 두면** 수준을 바꿔도 안전하다. 그런데 이 수준은 8.1 KB로 기본(double) 9.6 KB와 1.5 KB 차이밖에 안 난다. Cortex-M4F처럼 single FPU만 있는 칩에서 double 소프트 연산을 피하는 것이 진짜 이점이다.

### 3.4 흔한 함정 (libc)

- newlib-nano에서 `printf("%f")`가 **아무것도 안 찍거나 빈칸**이 나온다 → `-u _printf_float`(scanf는 `-u _scanf_float`)을 링크 옵션에 추가. 크기가 13 KB 늘어나는 것을 각오한다.
- `nano.specs`를 링크 단계에만 주고 컴파일 단계에는 안 준다 → 헤더(`newlib.h`의 구성 매크로)는 full newlib 것, 라이브러리는 nano — 대부분 동작하지만 `_REENT_SMALL` 등 구조체 배치가 달라질 수 있다. **컴파일·링크 양쪽에 같은 specs**를 준다.
- picolibc로 바꾸고 printf가 "undefined reference to `stdout`" → `pico_io.c`처럼 `stdout`을 정의한다.
- picolibc의 `errno`는 **TLS 변수**다(`-ftls-model=local-exec`가 specs에 들어 있다). 자기 링커 스크립트·시작 코드를 쓰면 `.tdata`/`.tbss` 배치와 TLS 포인터 설정(`_set_tls`)을 직접 챙겨야 한다. 이 노트의 예제는 errno를 쓰는 경로(malloc 실패 등)를 타지 않아서 문제가 드러나지 않았을 뿐이다. Zephyr는 이것을 `arch_tls_stack_setup`으로 처리한다(예제 16에서 SDK 빌드에만 이 함수가 보인다).

---

## 4. syscall stub과 semihosting — printf는 어떻게 핀까지 가나

> 🔁 이 절은 Bare-Metal BM F5와 겹친다 (주인: 이 노트).

### 4.1 직관 — libc의 "아래층 콘센트"

newlib은 OS 없는 환경을 위해 **운영체제가 해 줄 일을 함수 이름 몇 개로 추상화**했다. `write(2)` 대신 `_write`, `brk(2)` 대신 `_sbrk`, `exit` 대신 `_exit` … 이 함수들을 libc 안에는 두지 않고 "누군가 제공하라"고 남겨 둔다. 이게 **syscall stub**이다(Linux의 진짜 syscall이 아니라 이름만 빌린 콘센트). 제공하는 쪽이 libgloss(`libnosys`, `librdimon`) 또는 **내 코드**다.

| stub | 누가 부르나 | 최소 구현 |
|---|---|---|
| `_write(fd, p, n)` | printf, puts, fwrite → `__sfvwrite_r` → `_write_r` | UART에 n바이트 보내고 n 반환 |
| `_sbrk(inc)` | malloc, stdio 버퍼 | 링커 스크립트의 `__heap_start`~`__heap_end`를 bump |
| `_read` | scanf, getchar | 0 반환(입력 없음) 또는 UART 수신 |
| `_close`, `_lseek`, `_fstat`, `_isatty` | stdio 초기화, fflush | `_isatty`는 1 → stdout이 line buffered |
| `_exit` | exit, abort | 무한 루프 또는 리셋 |
| `_kill`, `_getpid` | abort, assert | -1 / 1 |

랩 `board.c`에 들어 있는 구현이 정확히 이것이다(`_write`가 `board_putc`를 부르고, `_sbrk`가 `__heap_start`부터 올라간다).

```svg
<svg viewBox="0 0 680 350" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="#888"/></marker></defs>
<text x="340" y="20" font-size="14" text-anchor="middle">printf 한 줄이 핀까지 가는 세 가지 길</text>
<text x="115" y="46" font-size="13" text-anchor="middle">newlib-nano + 내 stub</text>
<rect x="10" y="58" width="210" height="36" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="115" y="81" font-size="12" text-anchor="middle">printf → _vfprintf_r</text>
<line x1="115" y1="94" x2="115" y2="108" stroke="#888" marker-end="url(#ah2)"/>
<rect x="10" y="110" width="210" height="36" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="115" y="133" font-size="12" text-anchor="middle">__sfvwrite_r (stdio 버퍼)</text>
<line x1="115" y1="146" x2="115" y2="160" stroke="#888" marker-end="url(#ah2)"/>
<rect x="10" y="162" width="210" height="36" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="115" y="185" font-size="12" text-anchor="middle">_write_r → _write</text>
<line x1="115" y1="198" x2="115" y2="212" stroke="#888" marker-end="url(#ah2)"/>
<rect x="10" y="214" width="210" height="36" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="115" y="237" font-size="12" text-anchor="middle">내 stub: board_putc()</text>
<line x1="115" y1="250" x2="115" y2="264" stroke="#888" marker-end="url(#ah2)"/>
<rect x="10" y="266" width="210" height="36" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="115" y="289" font-size="12" text-anchor="middle">UART0 DATA 레지스터</text>
<text x="340" y="46" font-size="13" text-anchor="middle">rdimon (semihosting)</text>
<rect x="235" y="58" width="210" height="36" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340" y="81" font-size="12" text-anchor="middle">printf → _vfprintf_r</text>
<line x1="340" y1="94" x2="340" y2="108" stroke="#888" marker-end="url(#ah2)"/>
<rect x="235" y="110" width="210" height="36" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340" y="133" font-size="12" text-anchor="middle">__sfvwrite_r (stdio 버퍼)</text>
<line x1="340" y1="146" x2="340" y2="160" stroke="#888" marker-end="url(#ah2)"/>
<rect x="235" y="162" width="210" height="36" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340" y="185" font-size="12" text-anchor="middle">librdimon의 _write</text>
<line x1="340" y1="198" x2="340" y2="212" stroke="#888" marker-end="url(#ah2)"/>
<rect x="235" y="214" width="210" height="36" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="340" y="237" font-size="12" text-anchor="middle">bkpt 0xab (SYS_WRITE)</text>
<line x1="340" y1="250" x2="340" y2="264" stroke="#888" marker-end="url(#ah2)"/>
<rect x="235" y="266" width="210" height="36" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340" y="289" font-size="12" text-anchor="middle">디버거·QEMU → 호스트</text>
<text x="565" y="46" font-size="13" text-anchor="middle">picolibc (tinystdio)</text>
<rect x="460" y="58" width="210" height="36" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="565" y="81" font-size="12" text-anchor="middle">printf → vfprintf</text>
<line x1="565" y1="94" x2="565" y2="108" stroke="#888" marker-end="url(#ah2)"/>
<rect x="460" y="110" width="210" height="36" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="565" y="133" font-size="12" text-anchor="middle">FILE의 put 콜백</text>
<line x1="565" y1="146" x2="565" y2="160" stroke="#888" marker-end="url(#ah2)"/>
<rect x="460" y="162" width="210" height="36" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="565" y="185" font-size="12" text-anchor="middle">내 uart_put()</text>
<line x1="565" y1="198" x2="565" y2="212" stroke="#888" marker-end="url(#ah2)"/>
<rect x="460" y="214" width="210" height="36" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="565" y="237" font-size="12" text-anchor="middle">board_putc()</text>
<line x1="565" y1="250" x2="565" y2="264" stroke="#888" marker-end="url(#ah2)"/>
<rect x="460" y="266" width="210" height="36" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="565" y="289" font-size="12" text-anchor="middle">UART0 DATA 레지스터</text>
<text x="340" y="326" font-size="12" text-anchor="middle">빨간 칸: 디버거가 없으면 bkpt가 HardFault가 된다 (예제 7에서 QEMU Lockup으로 재현)</text>
<text x="340" y="343" font-size="12" text-anchor="middle">picolibc에는 _write가 없다 — stdout이라는 FILE 객체를 내가 정의한다</text>
</svg>
```
그림 3 — printf 한 줄이 UART 핀(또는 호스트 터미널)에 도착하는 세 가지 길. newlib은 `_write` stub, semihosting은 `bkpt 0xab` 트랩, picolibc는 FILE 구조체의 put 콜백을 쓴다.

### 4.2 예제 6 — stub이 없을 때, 그리고 `nosys.specs`의 조용한 실패

무엇을 확인하나: (1) stub 없이 newlib-nano printf+malloc을 링크하면 무슨 에러가 나나, (2) `--specs=nosys.specs`로 "해결"하면 어떻게 되나.

```sh
CF="-mthumb -mcpu=cortex-m3 -O2 -std=gnu11 -Wall -Wextra -ffunction-sections -fdata-sections -I$S -I$T -DUSE_MALLOC=1"
LD="-T $T/mps2_m3.ld -nostartfiles -Wl,--gc-sections --specs=nano.specs"
echo "=== (1) stub 없음"
arm-none-eabi-gcc $CF app.c mb.c startup.c $LD -o a1.elf 2>&1 | grep -E "undefined|error"
echo "=== (2) nosys.specs"
arm-none-eabi-gcc $CF app.c mb.c startup.c $LD --specs=nosys.specs -o a2.elf 2>&1 | grep warning | sort -u
arm-none-eabi-size a2.elf; rtos-run-mps2 a2.elf
```

```text
=== (1) stub 없음
.../newlib/libc/reent/readr.c:49:(.text+0x14): undefined reference to `_read'
.../newlib/libc/reent/closer.c:47:(.text+0xc): undefined reference to `_close'
.../newlib/libc/reent/lseekr.c:49:(.text+0x14): undefined reference to `_lseek'
.../newlib/libc/reent/writer.c:49:(.text+0x14): undefined reference to `_write'
.../newlib/libc/reent/isattyr.c:52:(.text+0xc): undefined reference to `_isatty'
.../newlib/libc/reent/sbrkr.c:51:(.text+0xc): undefined reference to `_sbrk'
.../newlib/libc/reent/fstatr.c:55:(.text+0xe): undefined reference to `_fstat'
collect2: error: ld returned 1 exit status
=== (2) nosys.specs
.../newlib/libc/reent/closer.c:47:(.text+0xc): warning: _close is not implemented and will always fail
.../newlib/libc/reent/fstatr.c:55:(.text+0xe): warning: _fstat is not implemented and will always fail
.../newlib/libc/reent/isattyr.c:52:(.text+0xc): warning: _isatty is not implemented and will always fail
.../newlib/libc/reent/lseekr.c:49:(.text+0x14): warning: _lseek is not implemented and will always fail
.../newlib/libc/reent/readr.c:49:(.text+0x14): warning: _read is not implemented and will always fail
.../newlib/libc/reent/writer.c:49:(.text+0x14): warning: _write is not implemented and will always fail
   text	   data	    bss	    dec	    hex	filename
   7180	     92	    332	   7604	   1db4	/var/tmp/rtos/B11/stubs/a2.elf
[exit 0]
```

(`/build/newlib-Ipb6mw/newlib-4.6.0.20260123/build_nano/arm-none-eabi/thumb/v7-m/nofp/newlib/../../../../../../`로 시작하는 긴 경로를 `...`로 줄였다.)

출력에서 볼 것:

- (1) 에러 메시지의 파일 이름이 힌트다: `reent/writer.c`의 `_write_r`가 `_write`를 찾는다. **`_r`이 붙은 재진입 래퍼가 errno를 `struct _reent`에 담고 진짜 stub을 부르는 구조**다. 이 7개를 구현하면 끝난다. `_exit`이 목록에 없는 것은 이 프로그램이 `exit`를 부르지 않기 때문이다(시작 코드가 `board_exit`를 직접 부른다).
- (2) `nosys.specs`는 7개 모두를 "`errno = ENOSYS; return -1;`"로 채운다. **링크는 경고만 내고 성공하며, 실행하면 printf 세 줄이 하나도 안 찍힌다.** `[exit 0]`은 printf가 아니라 `board_exit`의 자체 포맷터가 찍은 것이다. 새 보드 bring-up에서 "printf가 안 나온다"의 가장 흔한 원인 중 하나가 이것이다. 링커 경고 `will always fail`을 CI에서 에러로 취급하자(`-Wl,--fatal-warnings`).
- `_sbrk`는 nosys에도 경고가 없다 — libnosys의 `_sbrk`는 링커 심볼 `end`부터 힙을 키우는 **진짜 구현**이다. 그래서 malloc은 성공했지만 결과를 찍을 길이 없었다. 그리고 그 `_sbrk`는 **스택과 충돌하는지 검사하지 않는다.** 랩 `board.c`의 `_sbrk`처럼 `__heap_end`를 넘으면 `(void *)-1`을 돌려주는 검사를 직접 넣는 것이 좋다.

### 4.3 예제 7 — semihosting(rdimon): 디버거를 통해 호스트로 printf

직관: **semihosting**은 타깃 CPU가 `bkpt 0xab` 명령(Thumb; Armv7-M)으로 멈추면, 붙어 있는 디버거(또는 QEMU)가 r0의 "명령 번호"(0x05 = SYS_WRITE, 0x18 = SYS_EXIT …)와 r1의 인자 블록을 읽고 **호스트 PC에서 대신 실행해 주는** 규약이다. UART 배선 없이 printf와 **호스트 파일 열기**까지 된다. 랩의 `board_exit()`도 SYS_EXIT(0x18)으로 QEMU를 끈다.

무엇을 확인하나: UART stub 없이 `--specs=rdimon.specs`로 printf와 호스트 파일 쓰기를 하고, 디버거가 없는 상황(semihosting 꺼진 QEMU)도 재현한다.

```c
/* 같은 printf를 semihosting(rdimon)으로 내보낸다 — UART stub 없음 */
#include <stdio.h>
#include <stdlib.h>
#include "board.h"
extern void initialise_monitor_handles(void);   /* librdimon: fd 0/1/2를 호스트에 연다 */
int main(void)
{
    initialise_monitor_handles();
    uint32_t t0 = board_now_us();
    for (int i = 0; i < 20; i++)
        printf("semihost line %02d\n", i);
    uint32_t t1 = board_now_us();
    FILE *f = fopen("/var/tmp/rtos/B11/stubs/host.txt", "w");   /* 호스트 파일에 직접 쓴다 */
    if (f) { fprintf(f, "written by Cortex-M3 at t=%lu us\n", (unsigned long)t1); fclose(f); }
    printf("20 lines took %lu us (virtual)\n", (unsigned long)(t1 - t0));
    return 0;
}
```

```sh
arm-none-eabi-gcc $CF app_rd.c mb.c startup.c -T mps2_m3.ld -nostartfiles -Wl,--gc-sections \
  --specs=nano.specs --specs=rdimon.specs -o rd.elf && arm-none-eabi-size rd.elf
ICOUNT=1 rtos-run-mps2 rd.elf | tail -4; cat /var/tmp/rtos/B11/stubs/host.txt
echo "--- semihosting 없이 실행 (디버거 없는 실칩 흉내)"
timeout 3 qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -nographic -monitor none -serial stdio -kernel rd.elf; echo "rc=$?"
```

```text
   text	   data	    bss	    dec	    hex	filename
  10724	    104	    504	  11332	   2c44	/var/tmp/rtos/B11/stubs/rd.elf
semihost line 18
semihost line 19
20 lines took 743 us (virtual)
[exit 0]
written by Cortex-M3 at t=764 us
--- semihosting 없이 실행 (디버거 없는 실칩 흉내)
[HardFault] PC=0x257e
[exit 255]
qemu: fatal: Lockup: can't escalate 3 to HardFault (current priority -1)

R00=00000018 R01=00020023 R02=00000000 R03=00002951
...
XPSR=21000003 --C- T handler
FPSCR: 00000000
rc=134
```

출력에서 볼 것:

- **Cortex-M3 펌웨어가 호스트(VM)의 파일 `/var/tmp/rtos/B11/stubs/host.txt`를 만들었다.** 테스트 벡터를 호스트에서 읽고 결과를 파일로 쓰는 HIL·CI 테스트에 semihosting이 쓰이는 이유다(M9).
- 크기는 UART stub판(nano, 7~8 KB)보다 약 3 KB 크다 — librdimon에 파일 핸들 테이블과 semihosting 호출 코드가 있다.
- "20 lines took 743 us"는 **QEMU에서만** 그렇다. QEMU는 semihosting 처리 시간을 가상 시간에 넣지 않는다. 실제 칩에서는 `bkpt`마다 CPU가 **정지(halt)**하고, 디버그 프로브가 USB로 메모리를 읽고, 호스트가 처리한 뒤 재개한다 — 호출당 대략 밀리초 단위다(프로브·소프트웨어에 따라 다르며 이 랩에서는 잴 수 없다). **실시간 태스크 안에서 semihosting printf를 쓰면 deadline이 깨진다.** 그동안 인터럽트도 멈춘다.
- semihosting을 끄고 실행하자 **첫 `bkpt`에서 HardFault**가 났다. Armv7-M에서 디버거가 없을 때 `bkpt`는 HardFault로 승격된다. 그런데 HardFault 핸들러 안의 `board_exit()`가 또 `bkpt`를 실행해서 **Lockup**(HardFault 안에서 또 fault — 더 높일 우선순위가 없음)이 됐다. 실제 칩에서는 lockup → 보통 리셋(칩에 따라 다름)이다. **"디버거를 붙이면 되는데 떼면 부팅이 안 된다"의 고전적 원인**이다. 양산 빌드에서는 semihosting을 반드시 빼고, 꼭 남겨야 하면 `CoreDebug->DHCSR`의 C_DEBUGEN 비트로 디버거 연결 여부를 확인한 뒤에만 `bkpt`를 실행한다.

### 4.4 UART retarget vs semihosting vs SWO — 언제 무엇을

| 방법 | 배선 | 속도 · 실시간성 | 양산 이미지 | 비고 |
|---|---|---|---|---|
| UART retarget (`_write` → UART) | TX 핀 1개 | 115200 bps면 1문자 약 87 us, 폴링이면 그동안 CPU 점유 | 남겨도 됨 | 랩 `board.c` 방식. RTOS에서는 큐 + TX 인터럽트/DMA로 (J 모듈) |
| semihosting (rdimon) | 디버그 프로브 필수 | 호출마다 CPU 정지 | **빼야 함** (디버거 없으면 HardFault) | 파일 I/O 가능, CI·테스트용 |
| SWO / ITM | SWO 핀 + 프로브 | 비침투에 가까움 (M6) | 남겨도 됨 | Cortex-M3/M4/M7/M33. M0+에는 ITM 없음 |
| RTT (SEGGER) | J-Link | RAM 링버퍼, 매우 빠름 | 남겨도 됨 | 메모리 백도어 방식 |
| 딕셔너리 로깅 (K6) | UART 등 | 포맷을 호스트에서 | 남겨도 됨 | printf 엔진 자체를 없앤다 |

---

## 5. `-O` 레벨 — 크기와 속도는 한 축이 아니다

> 🔁 이 절은 Bare-Metal BM F6와 겹친다 (주인: 이 노트).

### 5.1 직관과 정의

`-O` 레벨은 "최적화 패스 묶음"의 이름이다. MCU에서 의미 있는 차이만 정리하면:

| 레벨 | 무엇을 켜나 (MCU에서 체감되는 것) | 쓰는 곳 |
|---|---|---|
| `-O0` | 거의 없음. 모든 변수가 스택 메모리에 있고 매 줄마다 load/store | 디버그 빌드 (줄 단위 스텝이 정확) |
| `-Og` | `-O1` 일부 + 디버그 정보 보존 | 개발 빌드 권장 (GCC 문서) |
| `-O1` | 레지스터 할당, 상수 전파, 간단한 루프 최적화 | — |
| `-O2` | `-O1` + 인라인, 스케줄링, loop distribution(루프 → `memset`), 정렬 | 일반 양산 (랩 템플릿 기본) |
| `-O3` | `-O2` + 벡터화·루프 unroll·더 공격적 인라인 | 핫 루프 파일만 |
| `-Os` | `-O2`에서 크기를 늘리는 것을 끔 (정렬 패딩, unroll, 일부 인라인) | 플래시가 빠듯한 MCU 양산 |
| `-Oz` | `-Os`보다 더 공격적으로 크기 (GCC 12부터 지원) | 극단적 크기 제약 |

Zephyr는 이것을 Kconfig choice로 고른다: `CONFIG_SIZE_OPTIMIZATIONS`(기본, `-Os`), `CONFIG_SIZE_OPTIMIZATIONS_AGGRESSIVE`(`-Oz`), `CONFIG_SPEED_OPTIMIZATIONS`(`-O2`), `CONFIG_DEBUG_OPTIMIZATIONS`(`-Og`), `CONFIG_NO_OPTIMIZATIONS`(`-O0`) — `Kconfig.zephyr` 523~555행과 `cmake/compiler/gcc/compiler_flags.cmake`의 `optimization_size` 등 속성이 대응한다. Zephyr 기본이 `-Os`라는 점이 FreeRTOS 데모 프로젝트들(보통 `-O2` 또는 벤더 IDE 기본 `-O0`/`-Og`)과 다르다.

### 5.2 예제 8 — 커널 두 개: CRC32와 바이트 복사

무엇을 확인하나: 같은 C 함수를 6개 레벨로 빌드해서 **함수 크기(`nm -S`)**, 전체 text, **ICOUNT 시간**을 잰다.

```c
/* kern.c — -O 레벨 비교용 커널 두 개: 비트 단위 CRC32, 바이트 복사 루프 */
#include <stddef.h>
#include <stdint.h>
#include "kern.h"
uint32_t crc32_bitwise(const uint8_t *p, size_t n)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
    }
    return ~crc;
}
void byte_copy(uint8_t *d, const uint8_t *s, size_t n)
{
    for (size_t i = 0; i < n; i++)
        d[i] = s[i];
}
```

```c
/* main.c — 커널을 4 KB 버퍼에 돌리고 ICOUNT 가상 시간(us)을 잰다 */
#include "board.h"
#include "kern.h"
#define N 4096
static uint8_t src[N], dst[N];
int main(void)
{
    for (int i = 0; i < N; i++) src[i] = (uint8_t)(i * 7 + 3);
    uint32_t t0 = board_now_us();
    uint32_t c = crc32_bitwise(src, N);
    uint32_t t1 = board_now_us();
    for (int r = 0; r < 8; r++) byte_copy(dst, src, N);     /* 32 KB 복사 */
    uint32_t t2 = board_now_us();
    board_printf_raw("crc=0x%x crc_us=%u copy_us=%u chk=%u\n", c, t1 - t0, t2 - t1, dst[N - 1]);
    return 0;
}
```

```sh
#!/bin/bash
# run_opt.sh — 같은 소스를 -O 레벨별로 빌드: 함수 크기(nm) · 전체 text · ICOUNT 시간
S=/Users/donh/.rtos_scratch/B11/opt; M=/var/rtoslab/templates/baremetal-mps2/Makefile
for O in -O0 -O1 -O2 -O3 -Os -Oz; do
  D=/var/tmp/rtos/B11/opt/b$O
  make -s -B -f $M SRCS="$S/main.c $S/kern.c" OUT=$D EXTRA_CFLAGS="$O" >/dev/null
  fs() { arm-none-eabi-nm -S -t d $D/app.elf | awk -v s=$1 '$4==s{print $2+0}'; }
  txt=$(arm-none-eabi-size $D/app.elf | awk 'NR==2{print $1}')
  out=$(ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 $D/app.elf | head -1)
  printf "%-4s crc_fn=%4sB copy_fn=%4sB text=%5s | %s\n" $O "$(fs crc32_bitwise)" "$(fs byte_copy)" $txt "$out"
done
```

```text
-O0  crc_fn= 108B copy_fn=  60B text= 4660 | crc=0x5e4e1995 crc_us=18483 copy_us=15736 chk=252
-O1  crc_fn=  64B copy_fn=  24B text= 4100 | crc=0x5e4e1995 crc_us=5899 copy_us=4198 chk=252
-O2  crc_fn=  60B copy_fn=  24B text= 4340 | crc=0x5e4e1995 crc_us=5899 copy_us=4198 chk=252
-O3  crc_fn= 116B copy_fn= 100B text= 5280 | crc=0x5e4e1995 crc_us=3671 copy_us=1054 chk=252
-Os  crc_fn=  48B copy_fn=  18B text= 3948 | crc=0x5e4e1995 crc_us=6030 copy_us=6294 chk=252
-Oz  crc_fn=  48B copy_fn=  18B text= 3948 | crc=0x5e4e1995 crc_us=6030 copy_us=6294 chk=252
```

(템플릿 Makefile의 `-O2` 뒤에 `EXTRA_CFLAGS`가 붙으므로 마지막 `-O`가 이긴다.) 무엇이 달라졌는지 `byte_copy`의 기계어를 본다:

```sh
for O in -O1 -O2 -O3 -Os; do echo "== $O"
  arm-none-eabi-objdump -d --no-show-raw-insn /var/tmp/rtos/B11/opt/b$O/app.elf | awk "/<byte_copy>:/,/^$/" | head -14
done
```

```text
== -O2
00000174 <byte_copy>:
     174:	cbz	r2, 18a <byte_copy+0x16>
     176:	add	r2, r1
     178:	subs	r0, #1
     17a:	subs	r1, #1
     17c:	subs	r2, #1
     17e:	ldrb.w	r3, [r1, #1]!
     182:	cmp	r1, r2
     184:	strb.w	r3, [r0, #1]!
     188:	bne.n	17e <byte_copy+0xa>
     18a:	bx	lr
== -O3
000001ac <byte_copy>:
     1ac:	cbz	r2, 20e <byte_copy+0x62>
     1ae:	subs	r3, r2, #1
     1b0:	cmp	r3, #5
     1b2:	bls.n	1f8 <byte_copy+0x4c>
     1b4:	adds	r3, r1, #1
     1b6:	subs	r3, r0, r3
     1b8:	cmp	r3, #2
     1ba:	bls.n	1f8 <byte_copy+0x4c>
     1bc:	mov	r3, r1
     1be:	mov	ip, r0
     1c0:	push	{r4, r5, r6, lr}
     1c2:	bic.w	r5, r2, #3
     1c6:	lsrs	r6, r2, #2
== -Os
0000015c <byte_copy>:
 15c:	movs	r3, #0
 15e:	push	{r4, lr}
 160:	cmp	r3, r2
 162:	bne.n	166 <byte_copy+0xa>
 164:	pop	{r4, pc}
 166:	ldrb	r4, [r1, r3]
 168:	strb	r4, [r0, r3]
 16a:	adds	r3, #1
 16c:	b.n	160 <byte_copy+0x4>
```

(`-O1`은 `-O2`와 같은 4명령 루프라서 생략했다.)

출력에서 볼 것:

- `-O0` → `-O1`만으로 **3~4배 빨라진다.** `-O0`은 루프 변수 하나 바꿀 때마다 스택에 저장하고 다시 읽는다. 디버그 빌드의 타이밍으로 실시간 설계를 하면 안 되는 이유다.
- `-O2`의 루프는 **포스트 인덱스 주소 지정**(`ldrb.w r3, [r1, #1]!`)으로 4명령, `-Os`는 인덱스 레지스터 방식으로 5명령(+분기 위치가 나빠 매 반복 `b.n`)이다. 6바이트 작은 대신 **50% 느리다**.
- `-O3`는 **런타임 겹침 검사**(`subs r3, r0, r3; cmp r3, #2`)를 넣고, 겹치지 않으면 4바이트씩 복사하는 경로로 간다(`bic.w r5, r2, #3` = n을 4의 배수로). 100 B로 커졌지만 **4배 빠르다.** `d`와 `s`가 겹칠 수 있다고 가정해야 하므로 GCC는 이 루프를 `memcpy`로 바꾸지 않았다 — `restrict`를 붙이면 바꿀 수 있다(직접 해보기 2).
- CRC는 `-O3`에서 안쪽 8회 루프를 펼쳐서 38% 빨라졌다. `-O1`과 `-O2`는 이 커널에서 같은 코드다.
- GCC 14에서 이 코드는 **`-Oz`가 `-Os`와 바이트 단위로 같았다.** `-Oz`의 차이는 주로 함수 호출 순서·상수 로드 방식 같은 곳에서 나오고, 코드에 따라 0~수 % 수준이다(Clang에서는 차이가 컸다 — 11절).

### 5.3 예제 9 — FreeRTOS 데모 전체

커널 하나가 아니라 **RTOS 이미지 전체**에서는 어떨까. 큐 길이 1로 2,000개를 주고받는 생산자-소비자(매번 컨텍스트 스위치) 데모다.

```c
/* frt/main.c — FreeRTOS 데모: 큐로 2,000개 주고받기 + 컨텍스트 스위치. -O 레벨·LTO 비교용 */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "board.h"
#define ITEMS 2000
static QueueHandle_t q;
static uint32_t t_start;

static void producer(void *arg)
{
    (void)arg;
    t_start = board_now_us();
    for (uint32_t i = 0; i < ITEMS; i++)
        xQueueSend(q, &i, portMAX_DELAY);       /* 큐 길이 1 → 매번 소비자에게 전환 */
    vTaskSuspend(NULL);
}
static void consumer(void *arg)
{
    (void)arg;
    uint32_t v, sum = 0;
    for (uint32_t i = 0; i < ITEMS; i++) { xQueueReceive(q, &v, portMAX_DELAY); sum += v; }
    uint32_t us = board_now_us() - t_start;
    board_printf_raw("sum=%u items=%u time_us=%u\n", sum, ITEMS, us);
    board_exit(0);
}
int main(void)
{
    q = xQueueCreate(1, sizeof(uint32_t));
    xTaskCreate(consumer, "cons", 256, NULL, 2, NULL);
    xTaskCreate(producer, "prod", 256, NULL, 1, NULL);
    vTaskStartScheduler();
    return 1;
}
```

```sh
#!/bin/bash
# run_frt.sh — FreeRTOS 데모를 -O 레벨(+LTO)별로 빌드: text/data/bss · ICOUNT 시간
S=/Users/donh/.rtos_scratch/B11/frt; M=/var/rtoslab/templates/freertos-mps2/Makefile
for O in -O0 -O1 -O2 -O3 -Os -Oz "-O2 -flto" "-Os -flto"; do
  D=/var/tmp/rtos/B11/frt/b$(echo $O | tr -d ' ')
  make -s -B -f $M SRCS="$S/main.c" OUT=$D EXTRA_CFLAGS="$O" >/dev/null 2>$D.err || { echo "$O BUILD FAIL"; head -5 $D.err; continue; }
  sz=$(arm-none-eabi-size $D/app.elf | awk 'NR==2{printf "text=%5d data=%4d bss=%6d",$1,$2,$3}')
  out=$(ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 $D/app.elf | head -1)
  printf "%-10s %s | %s\n" "$O" "$sz" "$out"
done
```

```text
-O0        text=17572 data= 100 bss=135404 | sum=1999000 items=2000 time_us=106728
-O1        text=12964 data= 100 bss=135404 | sum=1999000 items=2000 time_us=60939
-O2        text=12600 data= 100 bss=135408 | sum=1999000 items=2000 time_us=57108
-O3        text=14624 data= 100 bss=135408 | sum=1999000 items=2000 time_us=56719
-Os        text=11724 data= 100 bss=135404 | sum=1999000 items=2000 time_us=57287
-Oz        text=11724 data= 100 bss=135404 | sum=1999000 items=2000 time_us=57287
-O2 -flto BUILD FAIL
/usr/lib/gcc/arm-none-eabi/14.2.1/../../../arm-none-eabi/bin/ld: /tmp/ccoAeqVr.ltrans0.ltrans.o: in function `HardFault_Handler':
/var/rtoslab/templates/common/startup.c:59:(.text.HardFault_Handler+0xe): undefined reference to `board_hardfault'
/usr/lib/gcc/arm-none-eabi/14.2.1/../../../arm-none-eabi/bin/ld: (board_hardfault): Unknown destination type (ARM/Thumb) in /tmp/ccoAeqVr.ltrans0.ltrans.o
/var/rtoslab/templates/common/startup.c:59:(.text.HardFault_Handler+0xe): dangerous relocation: unsupported relocation
-Os -flto BUILD FAIL
...
```

출력에서 볼 것:

- RTOS 이미지 전체에서 **`-O0` → `-O2`가 크기 −28%, 시간 −46%**다. 반면 `-O2` → `-O3`는 크기 +16%에 시간은 0.7%밖에 줄지 않았다. 커널 코드는 분기·리스트 조작이 대부분이라 벡터화·unroll할 루프가 거의 없기 때문이다.
- **`-Os`는 `-O2`보다 7% 작고 시간은 0.3% 차이**다. RTOS 커널에서는 `-Os`가 거의 공짜다. 그래서 Zephyr 기본이 `-Os`다. 핫 루프(필터·FOC)만 따로 `-O2`/`-O3`로 컴파일하는 것이 실무 정석이다(GCC는 `__attribute__((optimize("O3")))`도 있지만 GCC 문서가 디버그용이라고 권하지 않는다 — 파일 단위 플래그가 낫다).
- **LTO는 랩 템플릿 그대로는 링크가 실패**했다. 7절의 주제다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<text x="340" y="22" font-size="14" text-anchor="middle">-O 레벨별 크기(가로) 대 실행 시간(세로) — 왼쪽 아래가 좋다</text>
<text x="140.0" y="42" font-size="12" text-anchor="middle">CRC32 함수 (B · us)</text>
<line x1="50" y1="250" x2="230" y2="250" stroke="currentColor"/>
<line x1="50" y1="50" x2="50" y2="250" stroke="currentColor"/>
<text x="50.0" y="266" font-size="12" text-anchor="middle">0</text>
<text x="140.0" y="266" font-size="12" text-anchor="middle">65</text>
<text x="230.0" y="266" font-size="12" text-anchor="middle">130</text>
<text x="46" y="254.0" font-size="12" text-anchor="end">0k</text>
<text x="46" y="154.0" font-size="12" text-anchor="end">10k</text>
<text x="46" y="54.0" font-size="12" text-anchor="end">20k</text>
<circle cx="199.5" cy="65.2" r="5" fill="#d0564a" fill-opacity="0.8" stroke="#d0564a"/>
<text x="206.5" y="61.2" font-size="12">-O0</text>
<circle cx="138.6" cy="191.0" r="5" fill="#888" fill-opacity="0.8" stroke="#888"/>
<text x="145.6" y="187.0" font-size="12">-O1</text>
<circle cx="133.1" cy="191.0" r="5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<text x="140.1" y="211.0" font-size="12">-O2</text>
<circle cx="210.6" cy="213.3" r="5" fill="#e08a3c" fill-opacity="0.8" stroke="#e08a3c"/>
<text x="217.6" y="209.3" font-size="12">-O3</text>
<circle cx="116.5" cy="189.7" r="5" fill="#3f9a6b" fill-opacity="0.8" stroke="#3f9a6b"/>
<text x="123.5" y="185.7" font-size="12">-Os/z</text>
<text x="360.0" y="42" font-size="12" text-anchor="middle">바이트 복사 함수 (B · us)</text>
<line x1="270" y1="250" x2="450" y2="250" stroke="currentColor"/>
<line x1="270" y1="50" x2="270" y2="250" stroke="currentColor"/>
<text x="270.0" y="266" font-size="12" text-anchor="middle">0</text>
<text x="360.0" y="266" font-size="12" text-anchor="middle">55</text>
<text x="450.0" y="266" font-size="12" text-anchor="middle">110</text>
<text x="266" y="254.0" font-size="12" text-anchor="end">0k</text>
<text x="266" y="154.0" font-size="12" text-anchor="end">8k</text>
<text x="266" y="54.0" font-size="12" text-anchor="end">17k</text>
<circle cx="368.2" cy="64.9" r="5" fill="#d0564a" fill-opacity="0.8" stroke="#d0564a"/>
<text x="375.2" y="60.9" font-size="12">-O0</text>
<circle cx="309.3" cy="200.6" r="5" fill="#888" fill-opacity="0.8" stroke="#888"/>
<text x="316.3" y="196.6" font-size="12">-O1</text>
<circle cx="309.3" cy="200.6" r="5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<text x="316.3" y="220.6" font-size="12">-O2</text>
<circle cx="433.6" cy="237.6" r="5" fill="#e08a3c" fill-opacity="0.8" stroke="#e08a3c"/>
<text x="440.6" y="233.6" font-size="12">-O3</text>
<circle cx="299.5" cy="176.0" r="5" fill="#3f9a6b" fill-opacity="0.8" stroke="#3f9a6b"/>
<text x="306.5" y="172.0" font-size="12">-Os/z</text>
<text x="580.0" y="42" font-size="12" text-anchor="middle">FreeRTOS 데모 전체 (KB · ms)</text>
<line x1="490" y1="250" x2="670" y2="250" stroke="currentColor"/>
<line x1="490" y1="50" x2="490" y2="250" stroke="currentColor"/>
<text x="490.0" y="266" font-size="12" text-anchor="middle">10</text>
<text x="580.0" y="266" font-size="12" text-anchor="middle">14</text>
<text x="670.0" y="266" font-size="12" text-anchor="middle">19</text>
<text x="486" y="254.0" font-size="12" text-anchor="end">40</text>
<text x="486" y="154.0" font-size="12" text-anchor="end">75</text>
<text x="486" y="54.0" font-size="12" text-anchor="end">110</text>
<circle cx="641.4" cy="59.3" r="5" fill="#d0564a" fill-opacity="0.8" stroke="#d0564a"/>
<text x="648.4" y="55.3" font-size="12">-O0</text>
<circle cx="549.3" cy="190.2" r="5" fill="#888" fill-opacity="0.8" stroke="#888"/>
<text x="556.3" y="186.2" font-size="12">-O1</text>
<circle cx="542.0" cy="201.1" r="5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<text x="549.0" y="221.1" font-size="12">-O2</text>
<circle cx="582.5" cy="202.2" r="5" fill="#e08a3c" fill-opacity="0.8" stroke="#e08a3c"/>
<text x="589.5" y="198.2" font-size="12">-O3</text>
<circle cx="524.5" cy="200.6" r="5" fill="#3f9a6b" fill-opacity="0.8" stroke="#3f9a6b"/>
<text x="531.5" y="196.6" font-size="12">-Os/z</text>
<text x="340" y="290" font-size="12" text-anchor="middle">시간 = QEMU icount 가상 시간(명령 1개 = 32 ns) · 데이터 4 KB·32 KB · 큐 2,000회</text>
<text x="340" y="308" font-size="12" text-anchor="middle">CRC·복사 세로축 단위 k = 1,000 us · FreeRTOS 가로 KB · 세로 ms</text>
</svg>
```
그림 4 — `-O` 레벨별 크기(가로) 대 시간(세로). 작은 커널에서는 `-O3`가 "크고 빠르고", `-Os`가 "작고 느리다". RTOS 이미지 전체에서는 `-O2`·`-O3`·`-Os`가 시간으로는 거의 같은 자리에 모이고 `-Os`만 왼쪽(작은 쪽)으로 간다. 측정: 예제 8, 9.

---

## 6. 최적화가 타이밍을 바꾼다 — 지연 루프와 `volatile`

> 🔁 이 절은 Bare-Metal BM F6와 겹친다 (주인: 이 노트).

### 6.1 직관 — 컴파일러에게 "시간"은 관찰 가능한 동작이 아니다

C 표준이 정의하는 "관찰 가능한 동작"은 `volatile` 접근, I/O(라이브러리 호출), 프로그램 종료 시점의 파일 상태뿐이다. **"이 루프가 1 ms 걸린다"는 관찰 가능한 동작이 아니다.** 그러니 아무 효과 없는 빈 루프는 지워도 된다. 이게 "`-O2`에서 센서 초기화 지연이 사라졌다"의 정체다.

### 6.2 예제 10 — 지연 함수 네 가지를 레벨별로

```c
/* dly.c — 바쁜 대기(busy-wait) 지연이 -O 레벨에 따라 어떻게 변하나 */
#include "board.h"
#define NI __attribute__((noinline))
NI void delay_bad(uint32_t n)   { for (uint32_t i = 0; i < n; i++) { } }          /* 빈 루프 */
NI void delay_vol(uint32_t n)   { for (volatile uint32_t i = 0; i < n; i++) { } } /* volatile 카운터 */
NI void delay_nop(uint32_t n)   { for (uint32_t i = 0; i < n; i++) __asm volatile("nop"); }
NI void delay_timer(uint32_t us)                                                  /* 하드웨어 시계 기준 */
{
    uint32_t t0 = board_now_us();
    while (board_now_us() - t0 < us) { }
}
static void run(const char *name, void (*f)(uint32_t), uint32_t arg)
{
    uint32_t t0 = board_now_us();
    f(arg);
    board_printf_raw("  %s(%u) = %u us\n", name, arg, board_now_us() - t0);
}
int main(void)
{
    run("delay_bad  ", delay_bad, 10000);
    run("delay_vol  ", delay_vol, 10000);
    run("delay_nop  ", delay_nop, 10000);
    run("delay_timer", delay_timer, 1000);
    return 0;
}
```

```sh
for O in -O0 -O1 -O2 -Os; do echo "== $O"
  make -s -B -f $M SRCS=$S/dly.c OUT=/var/tmp/rtos/B11/dly$O EXTRA_CFLAGS=$O >/dev/null &&
  ICOUNT=1 rtos-run-mps2 /var/tmp/rtos/B11/dly$O/app.elf | grep -v board_exit
done
arm-none-eabi-objdump -d --no-show-raw-insn /var/tmp/rtos/B11/dly-O2/app.elf | awk "/<delay_bad>:/,/^$/; /<delay_vol>:/,/^$/"
```

```text
== -O0
  delay_bad  (10000) = 2241 us
  delay_vol  (10000) = 2241 us
  delay_nop  (10000) = 2561 us
  delay_timer(1000) = 1002 us
== -O1
  delay_bad  (10000) = 960 us
  delay_vol  (10000) = 1920 us
  delay_nop  (10000) = 1281 us
  delay_timer(1000) = 1000 us
== -O2
  delay_bad  (10000) = 0 us
  delay_vol  (10000) = 1921 us
  delay_nop  (10000) = 1280 us
  delay_timer(1000) = 1000 us
== -Os
  delay_bad  (10000) = 0 us
  delay_vol  (10000) = 2240 us
  delay_nop  (10000) = 1600 us
  delay_timer(1000) = 1000 us
000000c0 <delay_bad>:
      c0:	bx	lr
      c2:	nop

000000c4 <delay_vol>:
      c4:	movs	r3, #0
      c6:	sub	sp, #8
      c8:	str	r3, [sp, #4]
      ca:	ldr	r3, [sp, #4]
      cc:	cmp	r0, r3
      ce:	bls.n	dc <delay_vol+0x18>
      d0:	ldr	r3, [sp, #4]
      d2:	adds	r3, #1
      d4:	str	r3, [sp, #4]
      d6:	ldr	r3, [sp, #4]
      d8:	cmp	r3, r0
      da:	bcc.n	d0 <delay_vol+0xc>
      dc:	add	sp, #8
      de:	bx	lr
```

출력에서 볼 것:

- `delay_bad`: `-O0` 2,241 us → `-O1` 960 us → **`-O2` 0 us(함수 본문이 `bx lr` 하나)**. 같은 소스가 레벨에 따라 2.3배 차이 나다가 결국 사라진다. 이 함수로 "센서 전원 켜고 10 ms 대기"를 했다면, 디버그 빌드에서는 되고 릴리스 빌드에서는 센서가 응답하지 않는다.
- `delay_vol`: 지워지지는 않지만 **스택 메모리 load/store 루프**가 된다(매 반복 `ldr`·`str` 2쌍). `-O1`/`-O2` 1,920 us, `-Os` 2,240 us로 **레벨마다 여전히 다르다.** 그리고 실제 칩에서는 이 load/store가 SRAM·캐시·wait state 영향을 받아 QEMU 숫자와 또 다르다. `volatile`은 "지우지 마라"이지 "정확한 시간"이 아니다.
- `delay_nop`: `asm volatile("nop")`은 지워지지 않지만 루프 오버헤드는 레벨마다 다르다(1,280~2,561 us). 그리고 Armv7-M 아키텍처 매뉴얼은 NOP에 대해 "실행 시간을 늘린다는 보장이 없다(늘리거나, 그대로이거나, 줄일 수도 있다)"고 적어 두었다 — **`nop`은 지연용이 아니다.**
- `delay_timer`: **모든 레벨에서 1,000~1,002 us.** 하드웨어 시계(여기서는 TIMER1, 실칩에서는 DWT CYCCNT·SysTick·HW 타이머)를 읽는 지연만이 최적화와 무관하다. RTOS에서는 ms 단위면 `vTaskDelay`/`k_msleep`(CPU를 양보), us 단위면 타이머 폴링 또는 Zephyr `k_busy_wait()`(`kernel/busy_wait.c`에서 `k_cycle_get_32()`를 읽으며 돈다)를 쓴다.

### 6.3 `volatile`이 필요한 다른 곳, 필요 없는 곳

같은 원리가 RTOS 코드에서 세 군데 나온다.

| 상황 | `-O2`에서 일어나는 일 | 올바른 방법 |
|---|---|---|
| ISR이 세우는 플래그를 태스크가 `while (!flag);`로 기다림 | load를 루프 밖으로 끌어올려 무한 루프 | `volatile` + 가능하면 세마포어/알림으로 블로킹 (D 모듈) |
| 주변장치 레지스터 읽기·쓰기 | 같은 레지스터 두 번 쓰기가 한 번으로 합쳐짐 | 레지스터 포인터는 항상 `volatile` (랩 `REG32` 매크로) |
| 태스크 간 공유 변수 (락으로 보호) | 락 함수 호출이 컴파일러 배리어 역할을 하므로 문제 없음 | `volatile`은 원자성·순서를 주지 않는다 — 뮤텍스나 C11 atomics (D8) |

FreeRTOS도 이 원리를 쓴다: `pxCurrentTCB`는 `TCB_t * volatile`로 선언되어 있고(`tasks.c` 465행), PendSV asm이 메모리에서 직접 읽는다.

---

## 7. LTO — 파일 경계를 넘는 최적화와 그 함정

> 🔁 이 절은 Bare-Metal BM F6와 겹친다 (주인: 이 노트).

### 7.1 직관 — "링크 때 한 번 더 컴파일"

보통 컴파일은 파일 하나씩 한다. `queue.c`를 컴파일할 때 컴파일러는 `tasks.c`의 `xTaskRemoveFromEventList`가 얼마나 짧은지 모르니 인라인할 수 없다. **LTO(Link Time Optimization, `-flto`)**는 `.o`에 기계어 대신(또는 함께) GCC 중간 표현(GIMPLE)을 넣어 두고, 링크 때 **프로그램 전체를 한 번에 다시 최적화**한다. 효과: 파일을 넘는 인라인, 안 쓰는 함수·인자 제거, 상수 전파(`.constprop` 복제본).

대가: (1) **asm 문자열 안에서만 이름이 불리는 함수는 "아무도 안 부른다"고 판단해 지운다.** (2) 빌드 시간·메모리 증가. (3) 디버깅 시 함수가 다른 함수 안으로 녹아들어 백트레이스가 낯설어진다. (4) 링커 스크립트에서 파일 이름으로 섹션을 배치하는 규칙(`*foo.o(.text*)`)이 LTO 뒤에는 파일 이름이 `ltrans0.ltrans.o`로 바뀌어 맞지 않는다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="#888"/></marker>
<marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="#d0564a"/></marker></defs>
<text x="340" y="20" font-size="14" text-anchor="middle">LTO는 C 호출 그래프만 본다 — asm 문자열 속 이름은 보이지 않는다</text>
<rect x="10" y="40" width="190" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="105" y="61" font-size="12" text-anchor="middle">g_vectors[] (used)</text>
<text x="105" y="79" font-size="12" text-anchor="middle">벡터 테이블 = 뿌리</text>
<rect x="245" y="40" width="190" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340" y="61" font-size="12" text-anchor="middle">HardFault_Handler</text>
<text x="340" y="79" font-size="12" text-anchor="middle">naked, 본문은 asm</text>
<rect x="480" y="40" width="190" height="50" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="575" y="61" font-size="12" text-anchor="middle">board_hardfault()</text>
<text x="575" y="79" font-size="12" text-anchor="middle">LTO가 지운다</text>
<line x1="200" y1="65" x2="243" y2="65" stroke="#888" marker-end="url(#ah4)"/>
<line x1="435" y1="65" x2="478" y2="65" stroke="#d0564a" stroke-dasharray="4 3" marker-end="url(#ah5)"/>
<text x="457" y="108" font-size="12" text-anchor="middle">"b board_hardfault" (문자열)</text>
<rect x="10" y="130" width="660" height="44" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="340" y="157" font-size="12" text-anchor="middle">링크 결과: undefined reference to `board_hardfault' (예제 11, 랩 템플릿 그대로 -flto)</text>
<rect x="10" y="194" width="325" height="92" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="172" y="216" font-size="13" text-anchor="middle">고치는 법 (내 코드)</text>
<text x="22" y="240" font-size="12">__attribute__((used))를 붙인다</text>
<text x="22" y="260" font-size="12">또는 asm 피연산자로 넘긴다</text>
<text x="22" y="278" font-size="12">또는 -Wl,--undefined=이름</text>
<rect x="345" y="194" width="325" height="92" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="507" y="216" font-size="13" text-anchor="middle">FreeRTOS는 이미 막아 두었다</text>
<text x="357" y="240" font-size="12">pxCurrentTCB: portDONT_DISCARD</text>
<text x="357" y="260" font-size="12">vTaskSwitchContext: 시작 함수 끝에서</text>
<text x="357" y="278" font-size="12">일부러 한 번 C로 호출 (port.c)</text>
</svg>
```
그림 5 — LTO 실패의 구조. `HardFault_Handler`는 naked 함수라 본문이 asm 문자열(`b board_hardfault`)뿐이다. LTO는 C 호출 그래프만 보므로 `board_hardfault`를 "아무도 안 부른다"고 지우고, 링커는 asm이 남긴 참조를 풀 수 없다.

### 7.2 예제 11 — LTO를 고치고, 크기·속도를 잰다

예제 9의 실패를 세 가지로 고칠 수 있다. 랩 템플릿은 읽기 전용이므로 `common/`을 내 폴더에 복사해서 `board.h`의 선언에 `used`를 붙였다:

```c
void     board_hardfault(uint32_t *frame) __attribute__((noreturn, used)); /* B11: asm에서만 불림 → LTO가 지우지 않게 used */
```

템플릿을 고치지 않고 링크 옵션으로 붙잡아 두는 방법도 확인했다:

```sh
make -s -B -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=$S/main.c OUT=/var/tmp/rtos/B11/ltou \
     EXTRA_CFLAGS="-O2 -flto" EXTRA_LDFLAGS="-Wl,--undefined=board_hardfault" &&
ICOUNT=1 rtos-run-mps2 /var/tmp/rtos/B11/ltou/app.elf
```

```text
   text	   data	    bss	    dec	    hex	filename
  13060	    100	 135416	 148576	  24460	/var/tmp/rtos/B11/ltou/app.elf
sum=1999000 items=2000 time_us=51712
[board_exit] code=0
```

`--undefined=이름`(= `-u`)은 "이 심볼은 뿌리다"라고 링커에게 알리고, GCC의 LTO 플러그인은 그 정보를 받아 함수를 남긴다.

이제 파일마다 `.o`를 만들고 링크하는 내 빌드 스크립트 `bld.sh`(map 파일에 오브젝트 이름이 남는다)로 `-O`·LTO·gc 조합을 잰다:

```sh
#!/bin/bash
# bld.sh <OUT> <bm|frt> <gcc|clang> "<app srcs>" — 파일마다 .o를 만들고 링크 (핵심 줄만)
#   env: OPT(-O2) LTO(0/1) GC(1/0) XCF(추가 CFLAGS) XLD(추가 LDFLAGS)
CF="$OPT -g -std=gnu11 -Wall -Wextra $INC $XCF"
[ $GC = 1 ] && CF="$CF -ffunction-sections -fdata-sections"
[ $LTO = 1 ] && CF="$CF -flto"
for s in $SRCS; do arm-none-eabi-gcc -mthumb -mcpu=cortex-m3 $CF -c $s -o $OUT/obj/$(basename ${s%.c}).o; done
arm-none-eabi-gcc -mthumb -mcpu=cortex-m3 $OPT $([ $LTO = 1 ] && echo -flto) $OUT/obj/*.o -T $C/mps2_m3.ld -nostartfiles \
  --specs=nano.specs -Wl,-Map=$OUT/app.map $([ $GC = 1 ] && echo -Wl,--gc-sections) $XLD -o $OUT/app.elf
```

```sh
for v in "OPT=-O2" "OPT=-O2 LTO=1" "OPT=-Os" "OPT=-Os LTO=1" "OPT=-O2 GC=0" "OPT=-Os GC=0"; do
  printf "%-16s " "$v"; env $v $B /var/tmp/rtos/B11/x/$(echo $v|tr -d " =") frt gcc $S/frt/main.c | tr "\n" " "
  ICOUNT=1 $R /var/tmp/rtos/B11/x/$(echo $v|tr -d " =")/app.elf | head -1
done
```

```text
OPT=-O2          text= 12600 data=  100 bss= 135408 sum=1999000 items=2000 time_us=57108
OPT=-O2 LTO=1    text= 12504 data=  100 bss= 135408 sum=1999000 items=2000 time_us=49393
OPT=-Os          text= 11724 data=  100 bss= 135404 sum=1999000 items=2000 time_us=57287
OPT=-Os LTO=1    text= 10328 data=  100 bss= 135396 sum=1999000 items=2000 time_us=51000
OPT=-O2 GC=0     text= 30432 data=  100 bss= 135408 sum=1999000 items=2000 time_us=55227
OPT=-Os GC=0     text= 27800 data=  100 bss= 135404 sum=1999000 items=2000 time_us=55228
```

출력에서 볼 것:

- **`-O2` + LTO: 크기는 거의 같고(−0.8%) 시간은 −13.5%.** 큐 send/receive 경로의 `tasks.c` 함수들이 `queue.c` 안으로 인라인되어 호출·복귀 명령이 줄었다.
- **`-Os` + LTO: 크기 −12%(11,724 → 10,328 B), 시간 −11%.** 크기와 속도를 동시에 얻는 드문 경우다. LTO가 쓰지 않는 인자·분기를 전체 프로그램 기준으로 제거하기 때문이다.
- `--gc-sections`를 끄면(`GC=0`) 30 KB로 2.4배가 된다. 8절에서 자세히. 그런데 **gc를 끈 쪽이 오히려 3% 빠르다**는 이상한 숫자가 있다 — 이것도 8절에서 원인을 찾는다.

LTO가 실제로 무엇을 했는지 bloaty의 diff로 본다(bloaty는 10절에서 소개):

```sh
bloaty --domain=vm -d symbols -n 10 /var/tmp/rtos/B11/x/OPT-OsLTO1/app.elf -- /var/tmp/rtos/B11/x/OPT-O2/app.elf
```

```text
     VM SIZE    
 -------------- 
  +777%    +684    Reset_Handler
  +762%    +488    producer
  +938%    +300    xTaskIncrementTick
  [NEW]    +224    prvCopyDataToQueue.constprop.0
  [DEL]    -208    xTaskPriorityDisinherit
  [DEL]    -252    xTaskRemoveFromEventList
  [DEL]    -268    xPortStartScheduler
  [DEL]    -280    vTaskSuspend
  [DEL]    -404    xQueueGenericSend
  [DEL]    -412    xTaskIncrementTick.part.0
 -101.5% -2.11Ki    [144 Others]
  -1.5% -2.23Ki    TOTAL
```

`xQueueGenericSend`(404 B)가 사라지고 `producer`가 488 B 커졌다 — **큐 송신 함수가 태스크 함수 안으로 통째로 인라인**됐다. `main`·`vTaskStartScheduler`·`xPortStartScheduler`는 `Reset_Handler` 안으로 녹았다(+684 B). `.constprop.0`은 "항상 같은 인자로 불리니 그 상수를 박아 넣은 복제본"이다. 디버거에서 `xQueueGenericSend`에 중단점을 걸면 안 걸리는 이유가 이것이다.

### 7.3 FreeRTOS는 LTO를 어떻게 견디나

PendSV 핸들러(`portable/GCC/ARM_CM3/port.c`의 `xPortPendSVHandler`, naked)는 asm 안에서 `bl vTaskSwitchContext`를 부르고 `ldr r3, =pxCurrentTCB`로 TCB 포인터를 읽는다. 둘 다 asm 문자열 안의 이름이다. 그런데도 FreeRTOS + LTO는 링크됐다. 소스를 보면 두 가지 장치가 있다.

```c
/* portable/GCC/ARM_CM3/portmacro.h 81행 */
#define portDONT_DISCARD      __attribute__( ( used ) )

/* tasks.c 465행 */
portDONT_DISCARD PRIVILEGED_DATA TCB_t * volatile pxCurrentTCB = NULL;

/* port.c xPortStartScheduler() 끝부분 (400~410행) */
    prvPortStartFirstTask();
    /* ... Call vTaskSwitchContext() so link time optimisation does not remove the
     * symbol. */
    vTaskSwitchContext();
    prvTaskExitError();
```

`pxCurrentTCB`는 `used`로, `vTaskSwitchContext`는 **절대 실행되지 않는 자리에서 C로 한 번 불러서** LTO가 보게 만든다. 우리 랩의 `board_hardfault`에는 이런 장치가 없었을 뿐이다. 새 포트·새 핸들러를 쓸 때 **asm에서만 부르는 C 함수에는 `used`를 붙인다**가 규칙이다.

Zephyr는 Kconfig `CONFIG_LTO`로 켠다. 조건이 있다: `depends on !(GEN_ISR_TABLES ... ) || ISR_TABLES_LOCAL_DECLARATION` — ISR 테이블을 빌드 후 단계에서 생성하는 기본 방식은 LTO와 맞지 않아서 `CONFIG_ISR_TABLES_LOCAL_DECLARATION=y`를 함께 줘야 한다(`Kconfig.zephyr` 557행). 예제 16의 앱으로 재 보았다:

```sh
rtos-zbuild mps2/an385 $S/zapp $O/v_SIZE                       # 기본 -Os
rtos-zbuild mps2/an385 $S/zapp $O/v_LTO -- -DCONFIG_ISR_TABLES_LOCAL_DECLARATION=y -DCONFIG_LTO=y
```

```text
== SIZE
           FLASH:       13564 B         4 MB      0.32%
sum=499500 cycles=889027 stdc=201710 gcc=14.2.1 20241119
== LTO
           FLASH:       12400 B         4 MB      0.30%
sum=499500 cycles=770035 stdc=201710 gcc=14.2.1 20241119
```

Zephyr에서도 LTO가 **FLASH −8.6%, msgq 1,000회 −13%**였다(같은 빌드에서 `-O2`인 `CONFIG_SPEED_OPTIMIZATIONS`는 17,076 B / 793,028 cycles로, LTO+`-Os`가 크기·속도 모두 이겼다).

```svg
<svg viewBox="0 0 680 370" xmlns="http://www.w3.org/2000/svg">
<text x="340" y="22" font-size="14" text-anchor="middle">gc-sections · LTO가 FLASH와 실행 시간에 주는 효과</text>
<text x="610" y="42" font-size="12" text-anchor="middle">시간(ms)</text>
<text x="222" y="63" font-size="12" text-anchor="end">FreeRTOS -O2, gc 없음</text>
<rect x="230" y="48" width="285.3" height="22" fill="#d0564a" fill-opacity="0.75" stroke="#d0564a"/>
<text x="521.3" y="63" font-size="12">30,432 B</text>
<text x="610" y="63" font-size="12" text-anchor="middle">55.2</text>
<text x="222" y="95" font-size="12" text-anchor="end">FreeRTOS -O2, gc(함수+데이터)</text>
<rect x="230" y="80" width="118.1" height="22" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="354.1" y="95" font-size="12">12,600 B</text>
<text x="610" y="95" font-size="12" text-anchor="middle">57.1</text>
<text x="222" y="127" font-size="12" text-anchor="end">FreeRTOS -O2, gc(함수만)</text>
<rect x="230" y="112" width="115.4" height="22" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="351.4" y="127" font-size="12">12,308 B</text>
<text x="610" y="127" font-size="12" text-anchor="middle">55.2</text>
<text x="222" y="159" font-size="12" text-anchor="end">FreeRTOS -O2, gc + LTO</text>
<rect x="230" y="144" width="117.2" height="22" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="353.2" y="159" font-size="12">12,504 B</text>
<text x="610" y="159" font-size="12" text-anchor="middle">49.4</text>
<text x="222" y="191" font-size="12" text-anchor="end">FreeRTOS -Os, gc</text>
<rect x="230" y="176" width="109.9" height="22" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="345.9" y="191" font-size="12">11,724 B</text>
<text x="610" y="191" font-size="12" text-anchor="middle">57.3</text>
<text x="222" y="223" font-size="12" text-anchor="end">FreeRTOS -Os, gc + LTO</text>
<rect x="230" y="208" width="96.8" height="22" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="332.8" y="223" font-size="12">10,328 B</text>
<text x="610" y="223" font-size="12" text-anchor="middle">51.0</text>
<text x="222" y="255" font-size="12" text-anchor="end">Zephyr -Os</text>
<rect x="230" y="240" width="127.2" height="22" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="363.2" y="255" font-size="12">13,564 B</text>
<text x="610" y="255" font-size="12" text-anchor="middle">35.6</text>
<text x="222" y="287" font-size="12" text-anchor="end">Zephyr -Os + CONFIG_LTO</text>
<rect x="230" y="272" width="116.2" height="22" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="352.2" y="287" font-size="12">12,400 B</text>
<text x="610" y="287" font-size="12" text-anchor="middle">30.8</text>
<line x1="230" y1="304" x2="530" y2="304" stroke="currentColor"/>
<text x="230.0" y="320" font-size="12" text-anchor="middle">0 KB</text>
<text x="305.0" y="320" font-size="12" text-anchor="middle">8 KB</text>
<text x="380.0" y="320" font-size="12" text-anchor="middle">16 KB</text>
<text x="455.0" y="320" font-size="12" text-anchor="middle">24 KB</text>
<text x="530.0" y="320" font-size="12" text-anchor="middle">32 KB</text>
<text x="340" y="344" font-size="12" text-anchor="middle">FreeRTOS: 큐 2,000회 · Zephyr: msgq 1,000회 (Zephyr는 printk·드라이버 포함) · QEMU icount 기준</text>
</svg>
```
그림 6 — gc-sections와 LTO의 효과(FLASH 막대, 오른쪽은 ICOUNT 시간). 가장 큰 절약은 gc-sections(−59%), 그다음이 `-Os`+LTO다. 측정: 예제 11, 12.

---

## 8. `--gc-sections` — 안 쓰는 함수 버리기, 그리고 섹션 앵커

> 🔁 이 절은 Bare-Metal BM F6와 겹친다 (주인: 이 노트).

### 8.1 직관

링커가 버릴 수 있는 최소 단위는 **입력 섹션**이다. 보통 `tasks.c`의 모든 함수는 `.text` 섹션 하나에 들어가므로, `xTaskCreate` 하나만 써도 `tasks.c` 전체가 따라온다. `-ffunction-sections`는 함수마다 `.text.함수이름` 섹션을, `-fdata-sections`는 전역 변수마다 `.data.이름`/`.bss.이름`을 만든다. 그다음 `-Wl,--gc-sections`가 **뿌리(ENTRY 심볼, `KEEP()`된 섹션 — 벡터 테이블)에서 참조를 따라가며 닿지 않는 섹션을 버린다.** 링커 스크립트에서 벡터 테이블을 `KEEP(*(.isr_vector))`로 감싸는 이유가 이것이다(B5).

### 8.2 예제 12 — 무엇이 버려졌나, 그리고 이상한 3%

```sh
XLD="-Wl,--print-gc-sections" bld.sh /var/tmp/rtos/B11/gcp frt gcc $S/frt/main.c > gcp.log 2>&1
L=gcp.log
echo "removed sections: $(grep -c "removing unused section" $L)"
grep -o "section .*" $L | grep -E "tasks.o|queue.o|timers.o" | head -8
echo "-- by file"; grep -o "in file .*" $L | sed "s|.*/||; s|in file ||; s|(.*||; s|'||g" | sort | uniq -c | sort -rn | head -10
```

```text
removed sections: 289
section '.text.xQueueGenericGetStaticBuffers' in file '/var/tmp/rtos/B11/gcp/obj/queue.o'
section '.text.xQueueCreateCountingSemaphoreStatic' in file '/var/tmp/rtos/B11/gcp/obj/queue.o'
section '.text.xQueueCreateCountingSemaphore' in file '/var/tmp/rtos/B11/gcp/obj/queue.o'
section '.text.xQueueCreateMutex' in file '/var/tmp/rtos/B11/gcp/obj/queue.o'
section '.text.xQueueGiveMutexRecursive' in file '/var/tmp/rtos/B11/gcp/obj/queue.o'
section '.text.xQueueCreateMutexStatic' in file '/var/tmp/rtos/B11/gcp/obj/queue.o'
section '.text.xQueueGenericSendFromISR' in file '/var/tmp/rtos/B11/gcp/obj/queue.o'
section '.text.xQueueGiveFromISR' in file '/var/tmp/rtos/B11/gcp/obj/queue.o'
-- by file
     96 libc_nano.a
     57 tasks.o
     39 stream_buffer.o
     32 queue.o
     27 event_groups.o
     22 timers.o
      7 heap_4.o
      5 board.o
      2 port.o
      2 hooks.o
```

출력에서 볼 것: 289개 섹션이 버려졌다. `event_groups.o`·`stream_buffer.o`는 통째로(이 앱은 안 쓴다), `queue.o`에서는 뮤텍스·세마포어·ISR 버전 함수들이 빠졌다. **FreeRTOS를 "필요한 파일만 골라 빌드"할 필요가 없는 이유**가 이것이다 — 다 넣고 gc에게 맡기면 된다. libc_nano.a에서도 96개가 빠졌다(newlib은 이미 함수 단위 섹션으로 빌드되어 있다).

이제 예제 11의 이상한 숫자 — gc를 끄면 3% 빠르다 — 를 풀어 보자. 가설: `-fdata-sections`가 ARM의 **섹션 앵커(section anchors)** 최적화를 끈다. 섹션 앵커는 한 파일의 전역 변수들을 한 블록으로 보고 기준 주소 하나를 레지스터에 올린 뒤 `[r3, #오프셋]`으로 접근하는 최적화다. 변수마다 섹션이 따로면 링커가 순서를 바꿀 수 있으므로 오프셋을 고정할 수 없어서, 변수마다 주소를 리터럴 풀에서 따로 읽어야 한다.

```sh
for v in "GC=0" "GC=0 XCF=-fno-section-anchors" \
         "GC=0 XCF=-ffunction-sections XLD=-Wl,--gc-sections" "GC=0 XCF=-fdata-sections XLD=-Wl,--gc-sections"; do
  printf "%-55s " "$v"; eval "$v bld.sh $D frt gcc $S/frt/main.c" | tr "\n" " "; ICOUNT=1 $R $D/app.elf | head -1
done
```

```text
GC=0                                                    text= 30432 data=  100 bss= 135408 sum=1999000 items=2000 time_us=55227
GC=0 XCF=-fno-section-anchors                           text= 31016 data=  100 bss= 135408 sum=1999000 items=2000 time_us=57108
GC=0 XCF=-ffunction-sections XLD=-Wl,--gc-sections      text= 12308 data=  100 bss= 135408 sum=1999000 items=2000 time_us=55227
GC=0 XCF=-fdata-sections XLD=-Wl,--gc-sections          text= 26792 data=  100 bss= 135408 sum=1999000 items=2000 time_us=57108
```

출력에서 볼 것: 가설이 맞다. `-fno-section-anchors`만 줘도 **정확히 같은 57,108 us**가 나오고 코드가 584 B 커진다. `-fdata-sections`를 켠 빌드는 모두 57,108 us, 끈 빌드는 모두 55,227 us다. 그리고 **`-ffunction-sections`만 쓰고 `-fdata-sections`를 뺀 빌드가 가장 작고(12,308 B) 빠르다.** 이 앱에서는 버릴 전역 변수가 거의 없어서 `-fdata-sections`의 이득(안 쓰는 변수 제거)보다 손해(앵커 상실)가 컸다. 일반론은 아니다 — 안 쓰는 큰 테이블·버퍼가 많은 코드(벤더 HAL, 프로토콜 스택)에서는 `-fdata-sections`가 RAM·FLASH를 크게 줄인다. 결론: **둘 다 켜는 것이 안전한 기본값이지만, 크기·속도가 중요하면 두 조합을 재 보라.**

### 8.3 gc의 함정

- **`KEEP` 없는 벡터 테이블**: gc가 `.isr_vector`를 버려서 리셋 벡터가 비고, 칩이 부팅하지 않는다. `KEEP(*(.isr_vector))` 필수.
- **링커 스크립트 배열 패턴**(Zephyr의 iterable section, initcall 테이블처럼 `__start_xxx`/`__stop_xxx` 사이를 순회하는 섹션): 아무도 이름으로 부르지 않으므로 `KEEP`이 없으면 사라진다. EL B3 11.2의 initcall 흉내와 같은 원리다.
- **`__attribute__((used))`는 링커 gc를 막지 못한다.** `used`는 "컴파일러야 지우지 마라"이고, 링커의 gc는 섹션 참조만 본다. GCC 11에는 링커 gc까지 막는 `retain` 속성(ELF 섹션 플래그 `SHF_GNU_RETAIN`)이 생겼지만, 이 arm-none-eabi 툴체인에서 확인해 보니 받아들이지 않았다:

```c
/* r.c — used / retain 속성이 --gc-sections에서 살아남나 */
__attribute__((used))   void keep_used(void)   { __asm volatile("nop"); }
__attribute__((retain, used)) void keep_retain(void) { __asm volatile("nop"); }
void Reset_Handler(void) { for (;;) { } }
```

```sh
arm-none-eabi-gcc -mthumb -mcpu=cortex-m3 -O2 -Wall -Wextra -ffunction-sections -nostdlib \
  -Wl,--gc-sections -Wl,-e,Reset_Handler r.c -o r.elf && arm-none-eabi-nm r.elf | grep keep
```

```text
r.c:3:1: warning: 'retain' attribute ignored [-Wattributes]
    3 | __attribute__((retain, used)) void keep_retain(void) { __asm volatile("nop"); }
      | ^~~~~~~~~~~~~
```

`nm`이 아무것도 찍지 않았다 — **두 함수 모두 gc로 사라졌고**, `retain`은 무시됐다(경고는 이 확인의 일부라 일부러 남겼다). 그러니 MCU에서 "참조 없이 남겨야 하는 것"(벡터 테이블, 버전 문자열, 부트로더가 읽는 헤더, iterable 테이블)은 **링커 스크립트의 `KEEP()`** 이나 `-Wl,--undefined=이름`으로 붙잡는다.

---

## 9. 스택 사용량 — `-fstack-usage`, `-Wstack-usage=`, 그리고 정적 분석이 못 보는 것

> 🔁 이 절은 Bare-Metal BM F6와 겹친다 (주인: 이 노트). 태스크 스택 크기 산정 전체(high water mark, 카나리, MPU 가드, PSPLIM)는 F3이 주인이다 — 여기서는 툴체인이 주는 도구만 본다.

### 9.1 직관

RTOS에서는 태스크마다 스택을 `xTaskCreate(..., 256, ...)`(word 단위)나 `K_THREAD_DEFINE(..., 1024, ...)`(바이트)로 **미리 정해 준다.** 너무 작으면 오버플로(옆 TCB·힙을 덮어씀), 너무 크면 RAM 낭비다. GCC는 함수마다 "이 함수의 프레임은 몇 바이트"를 계산해서 알려 줄 수 있다.

| 옵션 | 결과 |
|---|---|
| `-fstack-usage` | 오브젝트 옆에 `파일.su` — 함수별 `바이트  static/dynamic/bounded` |
| `-Wstack-usage=N` | 프레임이 N 바이트를 넘거나 상한이 없는(VLA·alloca) 함수에 경고 |
| `-fcallgraph-info=su` | `파일.ci` — 호출 그래프 + 프레임 크기 (VCG 형식). 경로 합산의 재료 |

중요한 한계: **`.su`는 함수 하나의 프레임**이다. 호출 경로 전체(main → fmt → snprintf → vfprintf …)를 더해야 태스크 스택이 되고, **libc는 `-fstack-usage` 없이 빌드되어 있어서 그 부분은 비어 있다.** 그리고 인터럽트가 그 위에 쌓는 하드웨어 프레임(Cortex-M 8워드 = 32 B, FPU 사용 시 26워드 = 104 B — B2, B6)도 들어가지 않는다.

### 9.2 예제 13 — `.su` 읽기, 그리고 칠해서 재기

```c
/* su.c — -fstack-usage / -Wstack-usage= 확인용: 큰 지역 버퍼, VLA, printf 호출 */
#include <stdio.h>
#include <string.h>
#include "board.h"
int fmt_telemetry(char *out, int n, int alt_cm, int roll_cdeg)
{
    char tmp[96];                                   /* 96 B 지역 배열 */
    int len = snprintf(tmp, sizeof tmp, "ALT=%d ROLL=%d", alt_cm, roll_cdeg);
    size_t m = (size_t)(len < n ? len : n - 1);
    memcpy(out, tmp, m);
    out[m] = '\0';
    return len;
}
int checksum_vla(int n)
{
    unsigned char buf[n];                           /* VLA → 크기가 런타임에 정해진다 */
    for (int i = 0; i < n; i++) buf[i] = (unsigned char)i;
    int s = 0;
    for (int i = 0; i < n; i++) s += buf[i];
    return s;
}
int main(void)
{
    char line[40];
    int len = fmt_telemetry(line, sizeof line, 1234, -56);
    printf("%s len=%d vla=%d\n", line, len, checksum_vla(32));
    return 0;
}
```

```sh
arm-none-eabi-gcc -mthumb -mcpu=cortex-m3 -O2 -std=gnu11 -Wall -Wextra -ffunction-sections -fdata-sections \
  -fstack-usage -Wstack-usage=100 -fcallgraph-info=su -I$C -c su.c -o su.o
cat su.su
arm-none-eabi-gcc ... su.o startup.c board.c -T mps2_m3.ld -nostartfiles --specs=nano.specs -Wl,--gc-sections -o app.elf && rtos-run-mps2 app.elf
```

```text
su.c: In function 'fmt_telemetry':
su.c:5:5: warning: stack usage is 120 bytes [-Wstack-usage=]
    5 | int fmt_telemetry(char *out, int n, int alt_cm, int roll_cdeg)
      |     ^~~~~~~~~~~~~
su.c: In function 'checksum_vla':
su.c:14:5: warning: stack usage might be unbounded [-Wstack-usage=]
   14 | int checksum_vla(int n)
      |     ^~~~~~~~~~~~
su.c:5:5:fmt_telemetry	120	static
su.c:14:5:checksum_vla	8	dynamic
su.c:22:5:main	48	static
ALT=1234 ROLL=-56 len=17 vla=496
[board_exit] code=0
```

(경고 두 개는 `-Wstack-usage=100`이 일부러 잡게 한 것이다. 경로 앞의 `/Users/donh/.rtos_scratch/B11/su/`는 줄였다.) `.ci` 파일의 일부:

```text
node: { title: "fmt_telemetry" label: "fmt_telemetry\n/Users/donh/.rtos_scratch/B11/su/su.c:5:5\n120 bytes (static)" }
node: { title: "snprintf" label: "snprintf\n/usr/include/newlib/stdio.h:272:5" shape : ellipse }
edge: { sourcename: "fmt_telemetry" targetname: "snprintf" label: "/Users/donh/.rtos_scratch/B11/su/su.c:8:5" }
...
edge: { sourcename: "main" targetname: "fmt_telemetry" label: "/Users/donh/.rtos_scratch/B11/su/su.c:23:15" }
```

출력에서 볼 것: `fmt_telemetry`는 96 B 배열 + 저장 레지스터·정렬로 **120 B**, VLA 함수는 `dynamic`(상한 없음 — 인증 코드에서 VLA를 금지하는 이유, O 모듈 MISRA). `.ci`에서 `snprintf`는 **타원(ellipse) = 크기를 모르는 외부 함수**로 표시된다. 정적 분석은 "main 48 + fmt 120 + snprintf ?"까지만 말해 준다.

그 "?"를 재는 가장 단순한 방법이 **스택 칠하기(stack painting)**다: 스택 영역을 알려진 무늬(0xA5A5A5A5)로 채우고, 함수를 돌린 뒤 무늬가 지워진 깊이를 본다. FreeRTOS의 `uxTaskGetStackHighWaterMark()`와 Zephyr의 `CONFIG_THREAD_ANALYZER`가 정확히 이 방식이다(F3, M4).

```c
/* su_paint.c — 정적 분석이 못 보는 libc 스택을 "칠하고 재기"로 측정 */
#include <stdint.h>
#include "board.h"
int fmt_telemetry(char *out, int n, int alt_cm, int roll_cdeg);   /* su.c */
#define PAINT 2048
static uint32_t used_below(uint32_t *lo, uint32_t *hi)
{
    uint32_t *p = lo;
    while (p < hi && *p == 0xA5A5A5A5u) p++;        /* 아직 칠한 값이면 안 쓰인 곳 */
    return (uint32_t)((char *)hi - (char *)p);
}
int main(void)
{
    uint32_t sp; __asm volatile("mov %0, sp" : "=r"(sp));
    uint32_t *hi = (uint32_t *)(sp - 64), *lo = hi - PAINT / 4;   /* 현재 SP 아래 2 KB */
    for (uint32_t *p = lo; p < hi; p++) *p = 0xA5A5A5A5u;
    char line[40];
    fmt_telemetry(line, sizeof line, 1234, -56);
    board_printf_raw("fmt_telemetry() 경로 실제 스택 사용 = %u B (정적 .su = 120 B)\n", used_below(lo, hi) + 64);
    return 0;
}
```

```sh
arm-none-eabi-gcc ... -Dmain=su_main -c su.c -o su_lib.o      # su.c의 main은 이름을 바꿔 둔다
arm-none-eabi-gcc ... -c su_paint.c -o p.o
arm-none-eabi-gcc ... p.o su_lib.o startup.c board.c -T mps2_m3.ld -nostartfiles --specs=nano.specs -Wl,--gc-sections -o paint.elf
rtos-run-mps2 paint.elf
```

```text
fmt_telemetry() 경로 실제 스택 사용 = 532 B (정적 .su = 120 B)
[board_exit] code=0
```

출력에서 볼 것: **실제로는 532 B, 정적 숫자의 4.4배**다(현재 SP부터 잰 값, `line[40]` 등 main 쪽 일부 포함). 차이 대부분은 newlib-nano `snprintf` → `_svfprintf_r` → `_printf_i` 경로의 프레임이다. 텔레메트리 태스크 스택을 `.su`만 보고 256 B로 잡았다면 오버플로다. **규칙: 정적 분석(경로 합) + 칠해서 재기 + 인터럽트 프레임 여유**, 셋 다 본다(F3).

### 9.3 puncover — `.su` + ELF로 경로 합산까지

puncover(Python 패키지)는 ELF의 심볼·디스어셈블리와 `.su` 파일을 합쳐 **함수별 크기, 정적 변수, 호출 경로 기준 최대 스택**을 웹 페이지로 보여 준다. pip로 자기 venv에 설치되었다(버전 0.8.0). 0.8.0에는 웹 서버 없이 JSON을 쓰는 옵션이 있어서 CI에 넣기 좋다.

```sh
python3 -m venv /var/tmp/rtos/B11/venv && /var/tmp/rtos/B11/venv/bin/pip install -q puncover
XCF=-fstack-usage bld.sh /var/tmp/rtos/B11/pc frt gcc $S/frt/main.c
cd /var/tmp/rtos/B11/pc && puncover --elf_file app.elf --build_dir obj --src_root / --non-interactive --generate-report \
  --report-max-static-stack-usage producer:::1024 --report-max-static-stack-usage consumer:::1024 \
  --report-max-static-stack-usage prvTimerTask:::512 --report-max-static-stack-usage prvIdleTask:::512
python3 -c "import json; r=json.load(open('report.json'))['no_tag']['stack_report']
for k,v in r.items(): print(k, v['max_static_stack_size'], '/', v['max_stack_size'], ' -> '.join(c['name'] for c in v['call_stack'][:4]))"
```

```text
producer 208 / 1024 producer -> xQueueGenericSend -> prvUnlockQueue -> prvNotifyQueueSetContainer
consumer 216 / 1024 consumer -> xQueueReceive -> prvUnlockQueue -> prvNotifyQueueSetContainer
prvIdleTask 176 / 512 prvIdleTask -> prvCheckTasksWaitingTermination -> vPortFree -> xTaskResumeAll
prvTimerTask 248 / 512 prvTimerTask -> xQueueReceive -> prvUnlockQueue -> prvNotifyQueueSetContainer
```

(report.json 원문은 함수별 `stack_size`가 달린 긴 JSON이라 요약 한 줄로 바꿔 찍었다.) 출력에서 볼 것: 태스크마다 **최악 정적 경로**와 그 합(producer 208 B)이 나온다. 경로 끝은 `configASSERT` → `board_exit` → `board_printf_raw` → `put_u`처럼 실패 경로까지 포함한다(정적 분석은 "실제로 가나"를 모른다 — 보수적). 여기에 PendSV가 태스크 스택에 저장하는 r4–r11(32 B)과 하드웨어 프레임(32 B)을 더하면 producer는 약 272 B — 우리가 준 1 KB(256 word)는 4배 여유다. **libc를 부르는 태스크는 libc 부분이 빠져 있다는 것**을 9.2에서 봤으니, printf를 쓰는 태스크는 칠하기로 다시 잰다.

---

## 10. map 파일과 크기 분석 — "내 bloaty" 만들기

> 🔁 이 절은 Bare-Metal BM F6와 겹친다 (주인: 이 노트).

### 10.1 도구 지도

| 도구 | 보여 주는 것 | 한계 |
|---|---|---|
| `arm-none-eabi-size` (Berkeley) | text / data / bss 세 숫자 | FLASH = text + data, RAM = data + bss 계산은 내가 |
| `size -A` (SysV) | 출력 섹션별 크기·주소 | 누가 큰지는 안 보임 |
| `nm --size-sort -S` | 심볼별 크기 | 정적 함수·별칭·링커 심볼 섞임, 오브젝트 출처 없음 |
| map 파일 (`-Wl,-Map=`) | 입력 섹션 하나하나의 주소·크기·**출처 오브젝트**(아카이브 멤버까지), 버려진 섹션, 채움(`*fill*`) | 사람이 읽기엔 길다 → 스크립트 |
| bloaty | 섹션·심볼·컴파일 단위별 크기, 두 ELF diff | 빌드해야 함(아래) |
| puncover | 크기 + 스택 + 웹 UI/JSON | pip 설치 |
| Zephyr `west build -t rom_report` / `ram_report` | 소스 트리 구조로 묶은 크기 | Zephyr 전용 |

### 10.2 예제 14 — `size -A`, `nm`, 그리고 map 파서

FreeRTOS 데모(`-O2`, gc)를 세 단계로 본다.

```sh
E=/var/tmp/rtos/B11/x/OPT-O2/app.elf
arm-none-eabi-size -A -d $E | grep -vE "debug|comment|ARM.attr"
arm-none-eabi-nm --size-sort -S -t d $E | grep -iE " [tTrRdDbB] " | tail -10
```

```text
/var/tmp/rtos/B11/x/OPT-O2/app.elf  :
section             size        addr
.isr_vector          192           0
.text              12400         192
.ARM.exidx             8       12592
.data                100   536870912
.bss              135408   536871016
.noinit                0   537006424
.heap                  0   537006424
Total             378264

00009912 00000364 T setvbuf
00000768 00000388 T pvPortMalloc
00003380 00000404 T xQueueGenericSend
00005352 00000412 t xTaskIncrementTick.part.0
00006176 00000428 T xTaskResumeAll
00008292 00000592 t prvTimerTask
537004344 00001024 b uxIdleTaskStack.2
537002200 00002048 b uxTimerTaskStack.0
536871044 00131072 b ucHeap
541065216 -4058792 B _estack
```

출력에서 볼 것: FLASH = `.isr_vector` + `.text` + `.ARM.exidx` + `.data`(초기값) = 192 + 12,400 + 8 + 100 = **12,700 B**. RAM의 대부분은 `ucHeap`(heap_4의 128 KB 배열, `configTOTAL_HEAP_SIZE`)이고 그다음이 커널이 정적으로 잡은 timer/idle 태스크 스택이다. `nm`의 마지막 줄 `_estack`의 **음수 크기**는 링커 스크립트가 정의한 심볼(크기 없음)에 `nm`이 다음 심볼까지의 거리를 붙인 엉터리 값이다 — `nm` 결과를 스크립트로 합산할 때 걸러야 한다. 그리고 `Total 378264`는 디버그 섹션까지 더한 것이라 의미가 없다.

`nm`은 "누가 데려왔나"를 말해 주지 않는다. map 파일은 말해 준다:

```text
.text           0x000000c0     0x3070
 *(.text*)
 .text.put_u    0x000000c0       0x44 /var/tmp/rtos/B11/x/OPT-O2/obj/board.o
 .text.board_init
                0x00000104       0x38 /var/tmp/rtos/B11/x/OPT-O2/obj/board.o
                0x00000104                board_init
```

입력 섹션 이름이 길면 주소·크기가 **다음 줄로 넘어간다**는 점이 파서를 쓸 때 첫 번째 함정이다. 이걸 처리하는 100줄 미만 파서:

```python
#!/usr/bin/env python3
"""mapsize.py — GNU ld map 파일로 '내 bloaty': 오브젝트·아카이브·입력 섹션(≈함수)별 FLASH/RAM 순위"""
import re, sys, collections
FLASH_OUT = {'.isr_vector', '.text', '.ARM.exidx', '.rodata'}
RAM_OUT = {'.bss', '.noinit'}
BOTH_OUT = {'.data'}                                   # FLASH(초기값)와 RAM 둘 다 먹는다
def short(path):
    m = re.match(r'.*/(lib[^/]+\.a)\((.+)\)$', path)
    if m: return m.group(1), m.group(1) + '(' + m.group(2).replace('libc_a-', '') + ')'
    return re.sub(r'.*/', '', path), re.sub(r'.*/', '', path)
def parse(fn):
    lines = open(fn).read().split('\n')
    lines = lines[lines.index('Linker script and memory map'):]
    out, pend, rows = None, None, []
    for ln in lines:
        m = re.match(r'^(\.\S+)\s+0x[0-9a-f]+\s+0x[0-9a-f]+', ln) or re.match(r'^(\.\S+)$', ln)
        if m: out = m.group(1); pend = None; continue
        if out not in FLASH_OUT | RAM_OUT | BOTH_OUT: continue
        m = re.match(r'^ (\S+)$', ln)                  # 이름이 길어 다음 줄로 넘어간 경우
        if m and not ln.startswith(' *('): pend = m.group(1); continue
        m = re.match(r'^ (\S+)?\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$', ln)
        if m:
            sec = m.group(1) or pend; pend = None
            if sec is None or sec.startswith('*('): continue
            rows.append((out, sec, int(m.group(3), 16), m.group(4).strip()))
            continue
        m = re.match(r'^ \*fill\*\s+0x[0-9a-f]+\s+0x([0-9a-f]+)', ln)
        if m: rows.append((out, '*fill*', int(m.group(1), 16), '(fill)'))
    return rows
def main(fn, top=12):
    rows = parse(fn)
    obj, arc, sec = collections.Counter(), collections.Counter(), collections.Counter()
    ram = collections.Counter(); fl = rm = 0
    for out, s, size, f in rows:
        a, o = short(f)
        if out in FLASH_OUT | BOTH_OUT: obj[o] += size; arc[a] += size; sec[(s, o)] += size; fl += size
        if out in RAM_OUT | BOTH_OUT: ram[o] += size; rm += size
    print(f'FLASH total {fl} B   RAM(static) total {rm} B')
    print(f'\n-- FLASH by archive/object group (top {top})')
    for k, v in arc.most_common(top): print(f'{v:7d}  {100*v/fl:5.1f}%  {k}')
    print(f'\n-- FLASH by input section (~function) (top {top})')
    for (s, o), v in sec.most_common(top): print(f'{v:7d}  {s:<34.34s} {o}')
    print(f'\n-- RAM by object (top 5)')
    for k, v in ram.most_common(5): print(f'{v:7d}  {k}')
if __name__ == '__main__':
    main(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 12)
```

```sh
python3 mapsize.py /var/tmp/rtos/B11/x/OPT-O2/app.map 12
```

```text
FLASH total 12700 B   RAM(static) total 135508 B

-- FLASH by archive/object group (top 12)
   3592   28.3%  tasks.o
   3140   24.7%  libc_nano.a
   1752   13.8%  queue.o
   1136    8.9%  board.o
    952    7.5%  timers.o
    712    5.6%  port.o
    676    5.3%  heap_4.o
    324    2.6%  startup.o
    232    1.8%  main.o
    144    1.1%  list.o
     40    0.3%  hooks.o

-- FLASH by input section (~function) (top 12)
    592  .text.prvTimerTask                 timers.o
    556  .rodata.put_u.str1.4               board.o
    548  .text                              libc_nano.a(findfp.o)
    428  .text.xTaskResumeAll               tasks.o
    412  .text.xTaskIncrementTick.part.0    tasks.o
    404  .text.xQueueGenericSend            queue.o
    404  .text                              libc_nano.a(fflush.o)
    388  .text.pvPortMalloc                 heap_4.o
    364  .text                              libc_nano.a(setvbuf.o)
    352  .text.xQueueReceive                queue.o
    332  .text                              libc_nano.a(mallocr.o)
    312  .text.prvAddNewTaskToReadyList     tasks.o

-- RAM by object (top 5)
 131100  heap_4.o
   3592  tasks.o
    328  libc_nano.a(findfp.o)
    304  timers.o
     80  libc_nano.a(impure.o)
```

출력에서 볼 것:

- 검증: FLASH 합계 12,700 B가 `size -A`의 합과 **정확히 같다.** RAM 135,508 = data 100 + bss 135,408. 파서를 믿기 전에 항상 이 대조를 한다.
- **printf를 한 번도 안 부르는데 libc_nano가 3,140 B(25%)**다. 범인은 `findfp.o`(548 B, stdio FILE 초기화), `fflush.o`, `setvbuf.o`, `mallocr.o` — A3에서 찾은 랩 `board.c`의 `setvbuf(stdout, ...)` 한 줄이다. 아카이브 멤버 단위로 보니 **어떤 libc 기능이 끌려왔는지** 바로 보인다. `nm`만으로는 이게 안 보인다.
- `.rodata.put_u.str1.4` 556 B는 `board.o`의 **병합된 문자열 상수**(`-fmerge-constants`가 한 섹션으로 모은 포맷 문자열들)다. 로그 문자열은 생각보다 크다 — 딕셔너리 로깅(K6)이 문자열을 FLASH에서 아예 빼는 이유다.
- RAM은 `tasks.o` 3,592 B가 의외로 크다: `configKERNEL_PROVIDED_STATIC_MEMORY`로 커널이 직접 잡은 idle 스택(1 KB)·timer 스택(2 KB)·TCB다.

### 10.3 bloaty와 puncover 상태

- **puncover**: `pip install puncover`로 VM의 자기 venv에 0.8.0이 설치되어 바로 동작했다(9.3). `--report-schema` 옵션은 `pydantic` 패키지가 없어 실패했다(별도 설치 필요).
- **bloaty**: Ubuntu 26.04 VM에 패키지가 없어서(apt 설치는 랩 규칙상 하지 않음) GitHub 소스(`google/bloaty`, 커밋 f1a83e6, 2026-09-24)를 `/var/tmp/rtos/B11/`에 clone해 CMake + Ninja로 빌드했다. CMake 설정만 378.8초, 이어서 Ninja 644단계 빌드(6코어, protobuf·abseil·capstone 포함 — 한 번에 성공), 결과 `bloaty --version` = `Bloaty McBloatface 1.1`. 실행 결과:

```sh
bloaty --domain=vm -d symbols -n 10 /var/tmp/rtos/B11/x/OPT-O2/app.elf
bloaty -d compileunits -n 8 /var/tmp/rtos/B11/x/OPT-O2/app.elf
```

```text
     VM SIZE    
 -------------- 
  88.5%   128Ki    ucHeap
   7.3%  10.6Ki    [184 Others]
   1.4%  2.00Ki    uxTimerTaskStack.0
   0.7%    1024    uxIdleTaskStack.2
   0.4%     592    prvTimerTask
   0.4%     555    [section .text]
   0.3%     428    xTaskResumeAll
   0.3%     412    xTaskIncrementTick.part.0
   0.3%     404    xQueueGenericSend
   0.3%     388    pvPortMalloc
   0.2%     364    setvbuf
 100.0%   144Ki    TOTAL
    FILE SIZE        VM SIZE    
 --------------  -------------- 
   1.9%  6.92Ki  89.0%   128Ki    /var/rtoslab/src/FreeRTOS-Kernel/portable/MemMang/heap_4.c
  31.6%   115Ki   0.0%       0    [Unmapped]
  24.9%  90.8Ki   3.0%  4.32Ki    [38 Others]
  14.5%  53.0Ki   4.9%  7.02Ki    /var/rtoslab/src/FreeRTOS-Kernel/tasks.c
   9.3%  34.1Ki   0.0%       0    [section .debug_loclists]
   8.1%  29.6Ki   1.3%  1.83Ki    /var/rtoslab/src/FreeRTOS-Kernel/queue.c
   4.7%  17.1Ki   0.8%  1.23Ki    /var/rtoslab/src/FreeRTOS-Kernel/timers.c
   2.9%  10.5Ki   0.4%     606    /Users/donh/.rtos_scratch/B11/common/board.c
   2.1%  7.68Ki   0.6%     876    ../../../../../../newlib/libc/stdio/findfp.c
 100.0%   365Ki 100.0%   144Ki    TOTAL
```

bloaty 읽는 법: **VM SIZE = 메모리를 차지하는 크기**(FLASH와 RAM을 구분하지 않고 합친다 — 그래서 `.bss`의 `ucHeap`이 88%로 나온다), **FILE SIZE = ELF 파일 안의 크기**(디버그 정보 포함, MCU에서는 의미 없음). MCU에서는 `--domain=vm`과 `-d sections,symbols`처럼 섹션으로 먼저 나눠 FLASH(`.text`·`.rodata`·`.data`)만 보는 습관이 좋다. `compileunits` 보기는 DWARF를 읽어서 **소스 파일 단위**로 묶어 주므로(newlib 소스 경로 `findfp.c`까지 보인다), 내 map 파서의 "오브젝트별" 보기와 같은 질문에 답한다. bloaty의 진짜 강점은 7.2에서 쓴 **두 ELF diff**(`bloaty new.elf -- old.elf`)다 — CI에서 PR마다 "이 변경이 FLASH를 몇 바이트 늘렸나"를 찍는 데 쓴다.

---

## 11. GCC vs Clang/LLD, 그리고 Arm Toolchain for Embedded

### 11.1 Clang으로 Cortex-M 빌드하기

Clang은 하나의 바이너리가 모든 타깃을 지원한다(EL B1 8절). Cortex-M에는 `--target=thumbv7m-none-eabi -mcpu=cortex-m3 -mfloat-abi=soft`를 주고, **libc와 libgcc는 GCC 툴체인 것을 빌려 쓴다**(Ubuntu clang에는 bare-metal Arm용 libc·compiler-rt가 없다). 링커는 LLD(`ld.lld`)를 직접 부르고 우리 링커 스크립트를 그대로 쓴다.

```sh
# bld.sh의 clang 경로 (핵심 줄만)
clang --target=thumbv7m-none-eabi -mcpu=cortex-m3 -mfloat-abi=soft -isystem /usr/include/newlib/nano \
      -isystem /usr/lib/arm-none-eabi/include -Wno-unused-command-line-argument $CF -c $s -o $o
ld.lld $OUT/obj/*.o -T $C/mps2_m3.ld --gc-sections -Map=$OUT/app.map \
       /usr/lib/arm-none-eabi/newlib/thumb/v7-m/nofp/libc_nano.a \
       /usr/lib/gcc/arm-none-eabi/14.2.1/thumb/v7-m/nofp/libgcc.a -o $OUT/app.elf
```

LLD는 GNU ld와 달리 **아카이브 순서·`--start-group`이 필요 없다**(한 번 본 아카이브의 심볼을 기억한다). GNU ld 문법의 링커 스크립트(`MEMORY`, `> RAM AT > FLASH`, `NOLOAD`, `ASSERT`)는 이 정도 수준이면 그대로 동작했다.

### 11.2 예제 15 — 같은 소스, GCC 14.2 vs Clang 21.1

```sh
for tc in gcc clang; do for O in -O2 -Os -Oz; do
  D=/var/tmp/rtos/B11/cl/$tc$O
  printf "%-6s %-4s kern " $tc $O; OPT=$O bld.sh $D bm $tc "$S/opt/main.c $S/opt/kern.c" | tr "\n" " "; ICOUNT=1 $R $D/app.elf | head -1
  printf "%-6s %-4s frt  " $tc $O; OPT=$O bld.sh $D-frt frt $tc $S/frt/main.c | tr "\n" " "; ICOUNT=1 $R $D-frt/app.elf | head -1
done; done
```

```text
gcc    -O2  kern text=  4340 data=   96 bss=   8520 crc=0x5e4e1995 crc_us=5899 copy_us=4198 chk=252
gcc    -O2  frt  text= 12600 data=  100 bss= 135408 sum=1999000 items=2000 time_us=57108
gcc    -Os  kern text=  3952 data=   96 bss=   8520 crc=0x5e4e1995 crc_us=6030 copy_us=6294 chk=252
gcc    -Os  frt  text= 11724 data=  100 bss= 135404 sum=1999000 items=2000 time_us=57287
gcc    -Oz  kern text=  3952 data=   96 bss=   8520 crc=0x5e4e1995 crc_us=6030 copy_us=6294 chk=252
gcc    -Oz  frt  text= 11724 data=  100 bss= 135404 sum=1999000 items=2000 time_us=57287
clang  -O2  kern text=  6716 data=   96 bss=   8520 crc=0x5e4e1995 crc_us=4327 copy_us=3676 chk=252
clang  -O2  frt  text= 16096 data=  100 bss= 135404 sum=1999000 items=2000 time_us=60955
clang  -Os  kern text=  4380 data=   96 bss=   8520 crc=0x5e4e1995 crc_us=8127 copy_us=4197 chk=252
clang  -Os  frt  text= 13140 data=  100 bss= 135404 sum=1999000 items=2000 time_us=60442
clang  -Oz  kern text=  3956 data=   96 bss=   8520 crc=0x5e4e1995 crc_us=9700 copy_us=5246 chk=252
clang  -Oz  frt  text= 11580 data=  100 bss= 135404 sum=1999000 items=2000 time_us=55736
```

(Clang 빌드도 `-Wall -Wextra` 경고 0개였다. FreeRTOS의 GCC 포트 — naked 함수 + GNU inline asm — 를 Clang이 그대로 컴파일했고 정상 실행됐다.)

출력에서 볼 것:

- **Clang `-O2`는 크고(FreeRTOS +28%) 이 데모에서는 오히려 느렸다(+7%)**. 작은 커널(CRC·복사)에서는 Clang `-O2`가 GCC `-O2`보다 빨랐다(CRC −27%) — 루프 unroll을 더 적극적으로 한다.
- **Clang에서는 `-Os`와 `-Oz`가 크게 다르다**(FreeRTOS 13,140 → 11,580 B). GCC 14는 둘이 같았다. Clang `-Oz`의 FreeRTOS는 GCC `-Os`보다 1.2% 작고, 시간도 가장 짧았다(55,736 us). 반대로 CRC는 `-Oz`가 가장 느렸다(9,700 us).
- 결론: **"어느 컴파일러가 낫다"는 코드와 레벨에 따라 뒤집힌다.** 툴체인을 바꿀 때는 자기 이미지로 크기·핵심 경로 시간을 재야 한다. 시간 숫자는 QEMU icount(명령 수 비례)라 실제 Cortex-M3의 파이프라인·플래시 wait state 효과는 빠져 있다.

### 11.3 Arm Toolchain for Embedded (ATfE)

Arm은 오랫동안 GCC 기반 "GNU Arm Embedded Toolchain"(이 랩 패키지의 원류, 버전 이름 `14.2.rel1`)과 상용 armclang(Arm Compiler 6)을 냈다. 2025년부터는 **LLVM 기반의 Arm Toolchain for Embedded(ATfE)** 를 공개 배포한다. 2026-10 웹 확인 내용:

- ATfE는 Clang/LLD/LLVM binutils + **picolibc(C 라이브러리)** + LLVM libc++로 구성된다. 무료·커뮤니티 지원판이 ATfE, 같은 기능에 Arm 상용 지원이 붙은 것이 **ATfEP**(Professional)다.
- GitHub `arm/arm-toolchain` 릴리스 페이지 기준 최신은 **`release-23.1.0-ATfE`(9월 10일, "now based on LLVM 23.1.0")**, 그 전이 22.1.0(3월 23일), 21.1.1, 21.1.0, 20.1.0이다. 페이지에 연도가 표시되지 않지만 LLVM 22/23 릴리스 시기로 보아 22.1.0이 2026년 3월, 23.1.0이 2026년 9월이다(연도는 추정).
- **ATfEP 22.1.0**이 Arm Development Studio 2026.0에 포함되었다(Arm 커뮤니티 블로그, 2026-06-19 게시) — Bare-Metal 나침반 §5의 기록과 일치한다. 같은 글에서 **LLVM-libc는 "평가용 선택 오버레이"**로 제공된다.
- Arm 블로그(2026-03-09)는 LLVM-libc가 앞으로 ATfE의 **기본 C 라이브러리가 될 예정**이며, picolibc를 쓰는 프로젝트는 일부 이전 작업이 불가피하니 미리 옮겨 보라고 권한다. 전환 시점(어느 버전부터인지)은 확인 못 함.
- Zephyr 문서에는 ATfE를 툴체인 변형으로 쓰는 페이지(`develop/toolchains/arm_toolchain_for_embedded.rst`)가 v4.4.2 소스 트리에 있다.

이 VM에는 ATfE가 설치되어 있지 않아서 **직접 실행하지 않았다.** Ubuntu Clang 21 + newlib-nano 조합(예제 15)이 "LLVM 계열 컴파일러로 Cortex-M을 빌드하면 크기·속도가 어떻게 달라지나"의 대리 실험이다.

---

## 12. RTOS가 툴체인에 요구하는 것

### 12.1 FreeRTOS — 포트 디렉터리가 컴파일러별로 나뉜 이유

FreeRTOS 커널의 C 코드(`tasks.c`, `queue.c` …)는 표준 C라서 어느 컴파일러로도 된다. **컴파일러마다 다른 것은 포트**다: 컨텍스트 스위치(PendSV)·첫 태스크 시작(SVC)·크리티컬 섹션은 어셈블리이고, 어셈블리를 C에 섞는 문법이 컴파일러마다 다르기 때문이다. 소스 트리 `portable/` 아래 폴더가 컴파일러 이름(`GCC`, `IAR`, `RVDS`, `ARMClang`, `Keil`, `CCS`, `Tasking` …)인 이유다.

| 포트 폴더 (ARM_CM3) | 파일 | asm을 쓰는 방식 |
|---|---|---|
| `GCC/ARM_CM3` | `port.c`, `portmacro.h` | `__attribute__((naked))` 함수 안의 GNU 확장 inline asm (`__asm volatile("..." :: "i"(...))`) — Clang도 받아들인다(예제 15) |
| `IAR/ARM_CM3` | `port.c`, `portasm.s`, `portmacro.h` | 핸들러를 별도 어셈블리 파일 `portasm.s`에 IAR 어셈블러 문법으로 |
| `RVDS/ARM_CM3` | `port.c`, `portmacro.h` | 옛 Arm Compiler 5(armcc)의 `__asm void 함수() { ... }` 임베디드 어셈블러 |

실무 함의: (1) Keil/armclang(Arm Compiler 6)은 Clang 기반이라 **GCC 포트를 쓴다** — `portable/ARMClang/` 폴더에는 코드 없이 `Use-the-GCC-ports.txt` 한 장만 있고 "The FreeRTOS GCC port layer also builds and works with the ARMClang compiler"라고 적혀 있다. `RVDS` 포트는 옛 armcc 5 전용이다. (2) 포트를 고르면 `portmacro.h`의 `portDONT_DISCARD`, `portFORCE_INLINE` 같은 컴파일러 속성 매크로도 같이 정해진다. LTO를 견디게 하는 `used`가 여기서 온다(7.3). (3) 새 컴파일러로 옮길 때 가장 먼저 깨지는 것은 PendSV asm의 **레지스터 제약·문법**이고, 그다음이 링커 스크립트 문법이다(IAR `.icf`, Keil scatter file은 GNU ld 스크립트와 완전히 다르다).

### 12.2 Zephyr — 툴체인 변형(toolchain variant)

Zephyr는 CMake 변수 `ZEPHYR_TOOLCHAIN_VARIANT`로 툴체인을 고른다. 소스의 `cmake/toolchain/` 아래 폴더가 그 목록이다: `zephyr`(Zephyr SDK), `gnuarmemb`(외부 GNU Arm Embedded, `GNUARMEMB_TOOLCHAIN_PATH`로 위치 지정), `armclang`, `iar`, `host`(native_sim용, `host/llvm` 포함), `cross-compile`, `arcmwdt`, `xcc`, `xt-clang`, `oneApi`. 랩의 `rtos-zbuild`는 mps2 보드에 기본으로 `gnuarmemb`(Ubuntu `arm-none-eabi-gcc`)를 쓰고, `ZTC=zephyr`면 SDK의 `arm-zephyr-eabi-gcc`를 쓴다.

#### 예제 16 — 같은 Zephyr 앱, 두 툴체인

```c
/* B11 zapp — 같은 Zephyr 앱을 두 툴체인(gnuarmemb / Zephyr SDK)으로 빌드해 비교 */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
K_MSGQ_DEFINE(q, sizeof(uint32_t), 4, 4);
static void producer(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    for (uint32_t i = 0; i < 1000; i++) k_msgq_put(&q, &i, K_FOREVER);
}
K_THREAD_DEFINE(prod, 1024, producer, NULL, NULL, NULL, 5, 0, 0);
int main(void)
{
    uint32_t v, sum = 0, t0 = k_cycle_get_32();
    for (int i = 0; i < 1000; i++) { k_msgq_get(&q, &v, K_FOREVER); sum += v; }
    printk("sum=%u cycles=%u stdc=%ld gcc=%s\n", sum, k_cycle_get_32() - t0,
           (long)__STDC_VERSION__, __VERSION__);
    return 0;
}
```

`prj.conf`는 `CONFIG_PRINTK=y`, `CONFIG_SIZE_OPTIMIZATIONS=y` 두 줄이다.

```sh
rtos-zbuild mps2/an385 $S $O/gnuarmemb
ZTC=zephyr rtos-zbuild mps2/an385 $S $O/sdk
for t in gnuarmemb sdk; do echo "== $t"
  grep -E "^CMAKE_C_COMPILER:|ZEPHYR_TOOLCHAIN_VARIANT:" $O/$t/CMakeCache.txt
  grep -o "\-std=[a-z0-9]*" $O/$t/compile_commands.json | sort | uniq -c
  arm-none-eabi-size $O/$t/zephyr/zephyr.elf | tail -1
  timeout 10 qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -nographic -monitor none -serial stdio \
     -icount shift=5,align=off,sleep=off -kernel $O/$t/zephyr/zephyr.elf | grep sum
done
```

```text
           FLASH:       13564 B         4 MB      0.32%
             RAM:        7320 B         4 MB      0.17%
           FLASH:       13228 B         4 MB      0.32%
             RAM:        7328 B         4 MB      0.17%
== gnuarmemb
CMAKE_C_COMPILER:STRING=/usr/bin/arm-none-eabi-gcc
ZEPHYR_TOOLCHAIN_VARIANT:STRING=gnuarmemb
    102 -std=c17
  13352	    208	   7111	  20671	   50bf	/var/tmp/rtos/B11/z/gnuarmemb/zephyr/zephyr.elf
sum=499500 cycles=889027 stdc=201710 gcc=14.2.1 20241119
== sdk
CMAKE_C_COMPILER:STRING=/var/rtoslab/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc
ZEPHYR_TOOLCHAIN_VARIANT:STRING=zephyr
    103 -std=c17
  13020	    208	   7119	  20347	   4f7b	/var/tmp/rtos/B11/z/sdk/zephyr/zephyr.elf
sum=499500 cycles=901040 stdc=201710 gcc=14.3.0
```

(QEMU를 `timeout`으로 끝낼 때 찍히는 `terminating on signal 15` 줄은 지웠다.) 크기가 336 B 다르다. 무엇 때문인지 `.config`와 심볼 diff로 본다:

```sh
K="PICOLIBC_USE_MODULE|PICOLIBC_USE_TOOLCHAIN|PICOLIBC_IO_POS_ARGS|TOOLCHAIN_SUPPORTS_THREAD_LOCAL_STORAGE|LIBC_ERRNO|TOOLCHAIN_GNUARMEMB"
for t in gnuarmemb sdk; do echo "$t: $(grep -E "^CONFIG_($K)=" $O/$t/zephyr/.config | tr "\n" " ")"; done
python3 symdiff.py $O/gnuarmemb/zephyr/zephyr.elf $O/sdk/zephyr/zephyr.elf 10
```

```text
gnuarmemb: CONFIG_PICOLIBC_USE_MODULE=y CONFIG_PICOLIBC_IO_POS_ARGS=y CONFIG_TOOLCHAIN_GNUARMEMB=y 
sdk: CONFIG_LIBC_ERRNO=y CONFIG_TOOLCHAIN_SUPPORTS_THREAD_LOCAL_STORAGE=y CONFIG_PICOLIBC_USE_TOOLCHAIN=y 
FLASH symbols: A=12004 B=11810 diff=-194
  -284  A=  284 B=    0  skip_to_arg
  -176  A= 1332 B= 1156  __l_vfprintf
  +128  A=   64 B=  192  _vector_table
   +64  A=    0 B=   64  arch_tls_stack_setup
   +32  A=   20 B=   52  z_thread_entry
   +28  A=   96 B=  124  z_setup_new_thread
   -18  A=  192 B=  174  __ultoa_invert
   +12  A=   72 B=   84  z_arm_pendsv
   +12  A=    0 B=   12  __aeabi_read_tp
    +8  A=   48 B=   56  arch_switch_to_main_thread
```

`symdiff.py`는 두 ELF의 `nm` 결과를 같은 주소의 별칭은 한 번만, 링커 심볼(1 MB 이상의 엉터리 크기)은 빼고 합쳐서 차이 순으로 찍는 18줄짜리 스크립트다(bloaty diff의 흉내).

출력에서 볼 것 — **"툴체인을 바꾸면 컴파일러만 바뀌는 게 아니다"**:

1. **C 라이브러리의 출처가 바뀐다.** gnuarmemb에서는 Zephyr가 picolibc를 **모듈 소스에서 직접 빌드**하고(`PICOLIBC_USE_MODULE`, 위치 인자 `%1$d` 지원 `IO_POS_ARGS` 켬 → `skip_to_arg` 284 B), SDK에서는 **툴체인에 미리 빌드된 picolibc**를 쓴다(`PICOLIBC_USE_TOOLCHAIN`, POS_ARGS 없음). printf 엔진이 460 B 차이 난다.
2. **TLS 지원이 바뀐다.** SDK 툴체인은 thread-local storage를 지원해서 errno가 TLS 변수가 되고, 스레드 생성 때 `arch_tls_stack_setup`이, 접근 때 `__aeabi_read_tp`가 들어온다. PendSV(`z_arm_pendsv`)도 TLS 포인터 교체 때문에 12 B 커졌다. 3.4절의 picolibc TLS 이야기가 여기서 다시 나온다.
3. 컴파일러 버전(14.2.1 vs 14.3.0)과 성능 차이는 1.4%(cycles 889,027 vs 901,040)로 작다.
4. 두 빌드 모두 **`-std=c17`** 이고 `__STDC_VERSION__ = 201710`이다. Zephyr 4.4 마이그레이션 가이드: "Zephyr now officially defaults to C17 (ISO/IEC 9899:2018) as its minimum required C standard version", 그보다 낮은 `CONFIG_STD_C99`·`CONFIG_STD_C11`은 deprecated(`Kconfig.zephyr`에서 `select DEPRECATED`). 오래된 벤더 툴체인(C11까지만 지원)을 쓰던 프로젝트가 4.4로 올릴 때 걸리는 부분이다. 참고로 GCC 15부터는 C 기본값이 `gnu23`이므로, Zephyr처럼 `-std=`를 명시하지 않는 빌드는 GCC 15로 올릴 때 `bool`·`true`가 키워드가 되는 등의 변화를 만난다(BM F1의 메모).

실무 교훈: 팀에서 **CI와 개발자 PC의 툴체인 변형을 하나로 맞춘다**(보통 Zephyr SDK). 다르면 크기·printf 기능·errno 구현이 달라서 "내 PC에서는 들어가는데 CI에서는 FLASH 넘침"이 생긴다. Zephyr 4.4가 처음 지원하는 SDK 1.0은 GNU 툴체인을 올렸고 **Clang/LLVM을 실험적으로** 포함한다(릴리스 노트). 이 랩의 SDK에는 `gnu/`만 설치되어 있어서 LLVM 변형은 시험하지 못했다.

### 12.3 Bare-Metal 대비 RTOS가 툴체인에 더 요구하는 것 — 요약

| 요구 | 이유 | 이 노트 |
|---|---|---|
| asm에서만 부르는 C 심볼 보존 (`used`, 일부러 한 번 호출) | PendSV·SVC·HardFault 핸들러가 naked asm | 7.3 |
| libc 재진입 (`_reent`·락·TLS) | 여러 태스크가 printf·malloc·errno 공유 | 3.2, F7 |
| 태스크별 스택 크기 정보 | 스택이 태스크 수만큼 | 9 |
| 같은 ABI의 multilib | FPU 컨텍스트 저장 여부가 `-mfloat-abi`와 연결 (B6) | 1.1 |
| 링커 스크립트 확장 (Zephyr iterable section, `KEEP`) | 커널 객체·init 테이블을 링크 시점에 모음 | 8.3, B5 |
| 표준 버전 (Zephyr C17) | 커널 헤더가 C11/C17 기능 사용 | 12.2 |

---

## 13. RTOS 커널 안에서는 무슨 일이 일어나나

툴체인 이야기는 커널 소스 곳곳에 흔적을 남긴다. 이 노트에서 만난 것을 소스 위치로 모으면:

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<text x="340" y="20" font-size="14" text-anchor="middle">RTOS 소스에서 툴체인과 맞닿는 지점</text>
<rect x="10" y="34" width="210" height="34" rx="6" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/>
<text x="115" y="56" font-size="13" text-anchor="middle">FreeRTOS</text>
<rect x="10" y="80" width="210" height="36" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="115" y="103" font-size="12" text-anchor="middle">GCC/ARM_CM3/port.c</text>
<rect x="10" y="126" width="210" height="36" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="115" y="149" font-size="12" text-anchor="middle">naked PendSV · SVC (asm)</text>
<rect x="10" y="172" width="210" height="36" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="115" y="195" font-size="12" text-anchor="middle">portDONT_DISCARD (used)</text>
<rect x="10" y="218" width="210" height="36" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="115" y="241" font-size="12" text-anchor="middle">newlib-freertos.h (_reent)</text>
<rect x="10" y="264" width="210" height="36" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="115" y="287" font-size="12" text-anchor="middle">picolibc-freertos.h (TLS)</text>
<rect x="235" y="34" width="210" height="34" rx="6" fill="#3f9a6b" fill-opacity="0.25" stroke="#3f9a6b"/>
<text x="340" y="56" font-size="13" text-anchor="middle">Zephyr</text>
<rect x="235" y="80" width="210" height="36" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="340" y="103" font-size="12" text-anchor="middle">cmake/toolchain/gnuarmemb</text>
<rect x="235" y="126" width="210" height="36" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="340" y="149" font-size="12" text-anchor="middle">cmake/compiler/gcc/</text>
<rect x="235" y="172" width="210" height="36" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="340" y="195" font-size="12" text-anchor="middle">Kconfig: LTO · STD_C17</text>
<rect x="235" y="218" width="210" height="36" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="340" y="241" font-size="12" text-anchor="middle">lib/libc/picolibc (module)</text>
<rect x="235" y="264" width="210" height="36" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="340" y="287" font-size="12" text-anchor="middle">arch_tls_stack_setup()</text>
<rect x="460" y="34" width="210" height="34" rx="6" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
<text x="565" y="56" font-size="13" text-anchor="middle">링커 단계</text>
<rect x="460" y="80" width="210" height="36" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="565" y="103" font-size="12" text-anchor="middle">KEEP(.isr_vector)</text>
<rect x="460" y="126" width="210" height="36" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="565" y="149" font-size="12" text-anchor="middle">iterable section 테이블</text>
<rect x="460" y="172" width="210" height="36" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="565" y="195" font-size="12" text-anchor="middle">--gc-sections 뿌리</text>
<rect x="460" y="218" width="210" height="36" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="565" y="241" font-size="12" text-anchor="middle">-Map → 크기 분석</text>
<rect x="460" y="264" width="210" height="36" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="565" y="287" font-size="12" text-anchor="middle">-u 이름 (LTO 뿌리)</text>
<text x="340" y="322" font-size="12" text-anchor="middle">컴파일러 문법(asm·속성)과 C 라이브러리 연결이 RTOS 포트·설정의 일부다</text>
</svg>
```
그림 7 — FreeRTOS·Zephyr 소스에서 툴체인과 맞닿는 지점. 왼쪽 두 칸은 컴파일러 문법·속성, 아래 칸들은 C 라이브러리 연결이다.

FreeRTOS V11.3.2:

- `portable/GCC/ARM_CM3/port.c` — `xPortPendSVHandler`, `vPortSVCHandler`, `prvPortStartFirstTask`가 `__attribute__((naked))`(123~130행). 본문은 GNU inline asm이고, `bl vTaskSwitchContext`·`ldr r3, =pxCurrentTCB`처럼 **C 심볼을 asm 문자열로 참조**한다. `xPortStartScheduler()` 끝의 `vTaskSwitchContext();`(410행)는 LTO용 안전장치다(7.3). 컨텍스트 스위치 자체는 C3·G2.
- `portable/GCC/ARM_CM3/portmacro.h` 81행 `portDONT_DISCARD = __attribute__((used))` — `include/FreeRTOS.h` 2767행에서 포트가 정의하지 않으면 빈 매크로로 기본값을 둔다.
- `include/FreeRTOS.h` 121~141행 `configUSE_NEWLIB_REENTRANT`, `configUSE_PICOLIBC_TLS` → `include/newlib-freertos.h`, `include/picolibc-freertos.h`. 태스크를 바꿀 때 libc의 "현재 태스크 상태"(`_impure_ptr` 또는 TLS 포인터)도 바꾼다. F7에서 깊게.
- `portable/ARMClang/Use-the-GCC-ports.txt` — 컴파일러별 포트 폴더 구조의 예외(12.1).

Zephyr v4.4.2:

- `cmake/toolchain/<변형>/generic.cmake`·`target.cmake` — 컴파일러 경로·접두사, `cmake/compiler/gcc/compiler_flags.cmake` — `optimization_size`(-Os)·`optimization_size_aggressive`(-Oz)·`dialect_c17` 같은 **추상 속성을 실제 플래그로** 바꾸는 표. 그래서 Zephyr 앱은 `-Os`를 직접 쓰지 않고 Kconfig로 고른다(H2).
- `Kconfig.zephyr` — `STD_C` choice(기본 `STD_C17`, C99/C11은 deprecated), 최적화 choice(523~555행), `LTO`(557행, ISR 테이블 조건).
- `lib/libc/picolibc/` — Zephyr가 picolibc에 붙이는 훅(락, `stdout` 연결 = `printk`와 같은 콘솔). `CONFIG_PICOLIBC_USE_MODULE`이면 `modules/lib/picolibc` 소스를 같은 컴파일러로 빌드한다(예제 16).
- `arch/arm/core/tls.c`의 `arch_tls_stack_setup()` — TLS 지원 툴체인일 때 새 스레드 스택 꼭대기에 TLS 블록을 만든다(예제 16에서 SDK 빌드에만 64 B로 등장).
- 링커 쪽: `include/zephyr/linker/` 아래 링커 스크립트 조각과 iterable section 매크로(`STRUCT_SECTION_ITERABLE`) — 커널 객체·init 함수 테이블을 `KEEP`으로 모아 gc에서 지킨다(8.3, B5, H2).

연결: 컨텍스트 스위치 asm은 C3/G2, 첫 태스크 시작은 C3, 링커 스크립트·시작 코드는 B5, 재진입 libc는 F7, 스택 산정은 F3, Zephyr 빌드 시스템은 H2.

---

## 14. 실무에서 다시 보기

### 14.1 로봇 관절 모터 컨트롤러 — 128 KB FLASH에 FOC + CAN + FreeRTOS + 로그

증상: 디버그용 `printf("iq=%.3f\n", iq)` 하나를 넣었더니 `region FLASH overflowed by 9 KB`.

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="#888"/></marker></defs>
<text x="340" y="20" font-size="14" text-anchor="middle">"플래시가 넘쳤다" — 효과가 큰 순서대로</text>
<rect x="10" y="36" width="660" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="22" y="57" font-size="13">1. 무엇이 큰지 먼저 잰다</text>
<text x="658" y="57" font-size="12" text-anchor="end">측정 없이 고치지 않는다</text>
<text x="40" y="76" font-size="12">size -A · nm --size-sort -S · mapsize.py · puncover</text>
<line x1="340" y1="86" x2="340" y2="96" stroke="#888" marker-end="url(#ah3)"/>
<rect x="10" y="98" width="660" height="50" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="22" y="119" font-size="13">2. libc와 printf를 의심한다</text>
<text x="658" y="119" font-size="12" text-anchor="end">newlib 37.5 → 8.6 / 2.9 KB</text>
<text x="40" y="138" font-size="12">newlib → newlib-nano 또는 picolibc, %f 빼기</text>
<line x1="340" y1="148" x2="340" y2="158" stroke="#888" marker-end="url(#ah3)"/>
<rect x="10" y="160" width="660" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="22" y="181" font-size="13">3. 안 쓰는 코드를 버린다</text>
<text x="658" y="181" font-size="12" text-anchor="end">FreeRTOS 30.4 → 12.3 KB</text>
<text x="40" y="200" font-size="12">-ffunction-sections + -Wl,--gc-sections</text>
<line x1="340" y1="210" x2="340" y2="220" stroke="#888" marker-end="url(#ah3)"/>
<rect x="10" y="222" width="660" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="22" y="243" font-size="13">4. 컴파일러에게 맡긴다</text>
<text x="658" y="243" font-size="12" text-anchor="end">12.6 → 11.7 → 10.3 KB</text>
<text x="40" y="262" font-size="12">-Os (GCC 14에서는 -Oz도 같았다), 그다음 -flto</text>
<line x1="340" y1="272" x2="340" y2="282" stroke="#888" marker-end="url(#ah3)"/>
<rect x="10" y="284" width="660" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="22" y="305" font-size="13">5. 기능과 설정을 줄인다</text>
<text x="658" y="305" font-size="12" text-anchor="end">타이머·trace·로그 끄기</text>
<text x="40" y="324" font-size="12">FreeRTOSConfig.h · Kconfig 조각 (A3)</text>
<text x="340" y="350" font-size="12" text-anchor="middle">숫자는 Cortex-M3 · arm-none-eabi-gcc 14.2 실측 (예제 4, 9, 11, 12)</text>
</svg>
```
그림 8 — "플래시가 넘쳤다"를 푸는 순서. 위에서부터 효과가 크고 부작용이 적다. 숫자는 이 노트의 측정.

이 노트의 숫자로 순서를 정하면: (1) `mapsize.py`로 상위 아카이브를 본다 → libc_nano가 1위일 것이다. (2) `-u _printf_float`를 빼고 정수 고정소수점으로 바꾸거나(`printf("iq=%d.%03d", ...)`), picolibc로 옮기면 float를 써도 newlib-nano+float보다 12 KB 작다(예제 4). (3) `--gc-sections`가 켜져 있는지 확인(벤더 IDE 프로젝트를 Makefile로 옮기다 빠지는 일이 흔하다 — 2.4배 차이). (4) `-Os` + LTO로 다시 12%. 그래도 안 되면 (5) FreeRTOS 기능(타이머 태스크, trace, stream buffer)을 끈다(A3). FOC 전류 루프 파일은 `-O2`로 따로 컴파일해서 `-Os`의 속도 손해를 막는다.

### 14.2 드론 비행 제어기 — `-O2`로 바꾸자 IMU 초기화 실패

증상: 디버그 빌드(`-O0`)는 정상, 릴리스(`-O2`)에서 IMU WHO_AM_I 읽기 실패. 원인 후보 1순위는 6절의 **빈 지연 루프**(데이터시트의 "power-on 후 10 ms 대기"를 `for` 루프로 구현) — `-O2`에서 0 us가 된다. 2순위는 레지스터 포인터에 `volatile`이 빠진 드라이버, 3순위는 배리어 누락(J5). 확인법: `objdump -d`로 지연 함수가 `bx lr` 하나인지 본다. 수정: `vTaskDelay(pdMS_TO_TICKS(10))`(스케줄러 시작 후) 또는 HW 타이머 기반 `delay_us()`.

### 14.3 센서 허브 MCU (Zephyr) — CI와 내 PC의 바이너리가 다르다

예제 16 그대로다. CI는 Zephyr SDK, 개발자는 `gnuarmemb`로 빌드해서 336 B 차이와 errno 구현(TLS 여부) 차이가 났다. 해결: `west build`가 쓰는 변형을 `ZEPHYR_TOOLCHAIN_VARIANT=zephyr`로 저장소에 고정하고, CI에서 `bloaty new.elf -- main.elf`(또는 `symdiff.py`)로 PR마다 크기 변화를 댓글로 단다.

### 14.4 SSD 펌웨어 경험을 RTOS 말로

SSD 컨트롤러 펌웨어는 대개 벤더 SDK의 자체 런타임(작은 `printf`, 자체 `memcpy`)을 쓰고 libc를 거의 링크하지 않는다 — 이 노트의 "libc 없음(848 B)" 행이다. 면접에서 "우리 SSD FW는 newlib을 안 썼고, 로그는 포맷 문자열 ID만 내보내는 방식이었다"라고 말하면 Zephyr의 딕셔너리 로깅(K6)과 정확히 같은 아이디어다. 또 SSD에서 ITCM/DTCM 예산을 map 파일로 관리했던 경험은 10절의 map 파서와 같은 일이다.

---

## 15. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `-mcpu=cortex-m4`만 주고 `-mfloat-abi=hard` 생략 | FPU가 있는데 float 연산이 느림 | `thumb/v7e-m/nofp` multilib 선택 | `-mfpu=fpv4-sp-d16 -mfloat-abi=hard`를 컴파일·링크 양쪽에 (예제 1) |
| `--specs=nosys.specs`로 링크 에러를 "해결" | printf가 아무것도 안 나옴 | `_write`가 ENOSYS로 항상 실패 | 진짜 `_write` stub 작성, `-Wl,--fatal-warnings` (예제 6) |
| semihosting printf를 양산 빌드에 남김 | 디버거 떼면 부팅 안 됨 (HardFault/Lockup) | `bkpt 0xab`가 디버거 없이 HardFault | 양산에서 rdimon 제거, UART/RTT로 (예제 7) |
| newlib-nano에서 `%f` 사용 | 빈칸 출력 또는 13 KB 증가 | float printf가 기본 제외 | `-u _printf_float` 또는 정수 고정소수점, picolibc (예제 4) |
| picolibc `--printf=f`에서 `(double)x` 전달 | 36.50 대신 3.04 (경고 없음) | float 전용 엔진은 32비트 인자 기대 | `printf_float(x)` 매크로 (예제 5) |
| 빈 `for` 루프로 지연 | `-O2`에서 지연 0 | 관찰 불가능한 동작 → 삭제 | HW 타이머·`vTaskDelay`·`k_busy_wait` (예제 10) |
| `-nostdlib`로 링크 | `undefined reference to memcpy/memset` (내가 안 불렀는데) | 구조체 대입·루프를 컴파일러가 mem 함수로 변환 | `mem*` 제공 또는 `-fno-tree-loop-distribute-patterns`, `-lgcc` (예제 3) |
| `-flto` 추가 | `undefined reference to board_hardfault` | asm에서만 부르는 함수를 LTO가 삭제 | `__attribute__((used))`, `-Wl,--undefined=` (예제 9, 11) |
| `--gc-sections` 켜고 `KEEP` 누락 | 리셋 벡터가 비어 부팅 안 함, init 테이블 사라짐 | 참조 없는 섹션 삭제 | 링커 스크립트 `KEEP(*(.isr_vector))` (8.3) |
| 태스크 스택을 `.su` 값만 보고 정함 | printf 쓰는 태스크가 가끔 HardFault | libc 프레임·ISR 프레임 미포함 | 칠해서 재기 + high water mark (예제 13, F3) |

---

## 16. 면접에서 이렇게 말한다

**Q.** "Our firmware is 2 KB over the flash limit. Walk me through what you would do."

**A.** 측정부터 한다. map 파일을 오브젝트·아카이브 단위로 합쳐서 상위 항목을 보면 대개 libc(printf·float)가 1위다. 그다음 `--gc-sections`가 켜져 있는지, `-Os`와 LTO를 써 봤는지 확인한다. 직접 재 보니 FreeRTOS 데모에서 gc만으로 30 KB → 12.6 KB, `-Os`+LTO로 다시 12%가 줄었다. 그래도 모자라면 기능을 끄거나 로그를 딕셔너리 방식으로 바꾼다.

> I start by measuring, not guessing. I generate the map file and rank input sections by object and archive — in practice the C library, especially printf with float support, is usually at the top. Then I check the cheap global switches: function sections with --gc-sections, -Os, and LTO. On a FreeRTOS demo I measured, gc-sections alone took the image from 30 KB to 12.6 KB, and -Os plus LTO saved another 12%. Only after that do I trim features or move logging to a dictionary-based scheme where format strings stay on the host.

**꼬리 질문**: "Why not just use -O3?" → 커널·제어 코드에서는 크기만 16% 늘고 시간은 1% 미만 줄었다(예제 9). 핫 루프 파일만 따로.

**Q.** "What's the difference between newlib, newlib-nano and picolibc?"

**A.** 셋 다 bare-metal용 C 라이브러리다. newlib은 기능이 완전하지만 printf 하나에 30 KB, RAM 2 KB. nano는 같은 소스를 크기 위주로 빌드한 것으로 float printf를 기본에서 뺐다(7 KB). picolibc는 버퍼 없는 tinystdio라 RAM을 거의 안 쓰고, 정수 전용 printf는 2 KB, double printf도 nano+float보다 12 KB 작다. Zephyr와 Arm의 새 LLVM 툴체인이 picolibc를 기본으로 쓴다.

> All three are C libraries for bare-metal targets. Full newlib is feature-complete but heavy: a single printf cost me about 30 KB of flash and 2 KB of RAM on a Cortex-M3. Newlib-nano is the same code built for size, with float formatting removed by default, so printf is around 7 KB. Picolibc uses tinystdio with no stdio buffers, so it uses almost no RAM; its integer-only printf is about 2 KB, and even full double printf is 12 KB smaller than nano with float enabled. Zephyr and Arm's LLVM-based embedded toolchain both default to picolibc.

**꼬리 질문**: "Is newlib thread-safe under an RTOS?" → 락 훅(`__malloc_lock`)이 기본으로 빈 함수(2바이트)다. FreeRTOS `configUSE_NEWLIB_REENTRANT` + 락 구현이 필요하다(F7).

**Q.** "What are syscall stubs and what is semihosting?"

**A.** newlib은 출력·힙 같은 OS 기능을 `_write`, `_sbrk` 같은 함수 이름으로 남겨 두고 내가 구현하게 한다. `_write`를 UART에 연결하면 printf가 UART로 나간다. semihosting은 `bkpt 0xab`로 CPU를 멈추고 디버거가 호스트에서 대신 I/O를 해 주는 방식이라 배선 없이 파일까지 쓸 수 있지만, 호출마다 CPU가 정지하고 디버거가 없으면 HardFault가 난다 — 실제로 QEMU에서 Lockup까지 재현했다. 그래서 테스트 빌드 전용이다.

> Newlib leaves the OS-dependent pieces — writing bytes, growing the heap, exiting — as a small set of functions like _write and _sbrk that the firmware provides. If _write sends bytes to a UART, printf goes to the UART. Semihosting instead executes a BKPT 0xAB instruction; an attached debugger services the request on the host, so you even get host file I/O with no wiring. But each call halts the core, and without a debugger the breakpoint escalates to a HardFault — I reproduced that as a lockup in QEMU. So semihosting is for test builds only.

**Q.** "Why does my busy-wait delay loop disappear at -O2, and is volatile the fix?"

**A.** 시간은 C의 관찰 가능한 동작이 아니라서 빈 루프는 지워도 된다. 실제로 `-O2`에서 함수가 `bx lr` 하나가 됐다. `volatile` 카운터는 지워지지는 않지만 스택 load/store 루프가 되어 레벨마다 시간이 다르다(1,920~2,241 us). 정답은 하드웨어 시계를 읽는 지연이나 RTOS 지연 API다 — 이건 모든 레벨에서 1,000 us로 같았다.

> Elapsed time is not observable behavior in C, so an empty loop is dead code and the compiler may delete it — at -O2 my delay function compiled to a single return. Making the counter volatile keeps the loop but turns it into memory loads and stores whose cost still changes with optimization level and memory speed. The robust fix is to wait on a hardware timer or cycle counter, or use the RTOS delay so the CPU can run other tasks; my timer-based delay measured the same at every optimization level.

**Q.** "What can go wrong when you enable LTO on RTOS firmware?"

**A.** LTO는 C 호출 그래프만 보기 때문에 asm 문자열에서만 부르는 함수를 지운다. 랩 템플릿에 `-flto`를 켜자 HardFault 핸들러가 부르는 C 함수가 사라져 링크가 실패했다. FreeRTOS는 `pxCurrentTCB`에 `used`를 붙이고 `vTaskSwitchContext`를 C에서 한 번 일부러 불러서 이 문제를 피한다. 고친 뒤에는 `-Os`+LTO로 크기 −12%, 시간 −11%였다. 또 함수가 인라인되어 사라지니 디버깅 중단점 위치가 바뀐다.

> LTO only sees the C-level call graph, so a function referenced only from an inline-assembly string looks unused and gets dropped. When I turned on -flto for a lab template, the C function called from the naked HardFault handler disappeared and the link failed. FreeRTOS avoids this by marking pxCurrentTCB as used and calling vTaskSwitchContext once from C in the port. After fixing it with the used attribute, -Os plus LTO gave 12% less flash and 11% faster queue round trips. The other cost is debuggability: functions get inlined away, so breakpoints and backtraces look different.

**Q.** "How do you size a task stack?"

**A.** 세 가지를 합친다. `-fstack-usage`와 호출 그래프(puncover)로 정적 최악 경로, 스택 칠하기나 `uxTaskGetStackHighWaterMark`로 실측, 그리고 인터럽트·FPU 프레임 여유. 정적 분석은 libc를 모른다 — snprintf 하나 부르는 함수가 정적 120 B, 실측 532 B였다.

> I combine three sources: static analysis with -fstack-usage plus the call graph — puncover does the path sum — runtime measurement by painting the stack or reading the high-water mark, and a margin for exception frames, especially the 104-byte FPU frame. Static analysis can't see into a precompiled libc: a function that called snprintf showed 120 bytes statically but used 532 bytes when I measured it.

**Q.** "GCC or Clang for Cortex-M?"

**A.** 코드와 레벨에 따라 뒤집힌다. 같은 FreeRTOS 데모에서 Clang `-O2`는 GCC보다 28% 컸지만 Clang `-Oz`는 GCC `-Os`보다 조금 작고 빨랐다. CRC 커널에서는 Clang `-O2`가 27% 빨랐다. 그래서 툴체인을 바꿀 때는 자기 이미지로 크기와 핵심 경로를 잰다. Arm은 LLVM 기반 ATfE(picolibc 포함)를 배포하고 있고, FreeRTOS GCC 포트는 Clang으로도 그대로 빌드된다.

> It depends on the code and the level, and the answer flips. On the same FreeRTOS demo, Clang at -O2 was 28% larger than GCC, but Clang at -Oz was slightly smaller and faster than GCC at -Os, while on a CRC kernel Clang -O2 was 27% faster. So when switching toolchains I measure my own image — size and the critical paths. Arm now ships the LLVM-based Arm Toolchain for Embedded with picolibc, and the FreeRTOS GCC port builds unchanged with Clang.

**꼬리 질문**: "What does Zephyr require?" → 4.4부터 C17이 최소 표준(C99/C11은 deprecated). 툴체인 변형에 따라 libc 빌드 방식과 TLS 지원이 달라진다(예제 16).

---

## 17. 직접 해보기

1. **개념** — `-ffreestanding`으로 컴파일하고 `-nostdlib`로 링크했는데 `undefined reference to memcpy`가 났다. 소스에는 `memcpy`가 없다. 가능한 원인 두 가지와 고치는 법은? 정답/힌트: 구조체 대입·큰 지역 배열 초기화가 `memcpy`/`memset`이 된다(freestanding에서도). `mem*`를 직접 넣거나 libc 링크. 2.2절.
2. **코드** — 예제 8의 `byte_copy`를 `void byte_copy(uint8_t *restrict d, const uint8_t *restrict s, size_t n)`로 바꾸고 `-O2`에서 `objdump`를 보라. 무엇으로 바뀌나, 시간은? 정답/힌트: 겹침이 없다고 약속했으므로 GCC가 `memcpy` 호출로 바꿀 수 있다. newlib-nano `memcpy`가 v7-M용 최적화판인지 시간으로 확인.
3. **측정** — 예제 4에 `USE_SCANF`(`sscanf("12 34", "%d %d", ...)`)를 추가해 newlib-nano와 picolibc(`--printf=i --scanf=i`)를 비교하라. 정답/힌트: picolibc specs의 `--scanf=` 옵션이 `vfscanf`를 바꾼다(1.2절의 specs 줄).
4. **측정** — 예제 11의 FreeRTOS 데모를 `-ffunction-sections`만(데이터 섹션 없이) + `-Os` + LTO로 빌드해 지금까지의 최소(10,328 B)를 깨 보라. 정답/힌트: 8.2절의 섹션 앵커 효과가 LTO와 겹치는지 본다. 결과를 bloaty diff로 설명.
5. **코드** — `_sbrk`가 스택과 충돌하는지 검사하도록 고쳐라: 현재 MSP와 비교해서 힙 끝이 `MSP - 256`을 넘으면 `errno = ENOMEM; return (void *)-1;`. RTOS가 돌 때 이 검사가 왜 의미 없어지는가? 정답/힌트: RTOS 시작 후 태스크는 PSP로 자기 스택을 쓰고, MSP는 인터럽트 스택이다. FreeRTOS에서는 `heap_x`를 쓰고 libc malloc을 막거나 `configUSE_NEWLIB_REENTRANT` + 락을 쓴다(F1, F7).
6. **측정** — 예제 16의 Zephyr 앱에서 `CONFIG_PICOLIBC_IO_POS_ARGS=n`을 gnuarmemb 빌드에 주면 SDK 빌드와 크기 차이가 얼마나 줄어드나? 정답/힌트: `skip_to_arg` 284 B가 사라지는지 `symdiff.py`로 확인. 남는 차이는 TLS.
7. **개념** — semihosting printf가 들어간 이미지를 디버거 없이 실칩에서 돌리면 어떻게 되나? 디버거 연결 여부를 런타임에 확인하는 레지스터는? 정답/힌트: `bkpt` → HardFault(디버그 비활성 시). Cortex-M `CoreDebug->DHCSR`의 C_DEBUGEN 비트.

---

## 18. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| arm-none-eabi | OS 없는 Arm EABI 툴체인 | MCU용 GCC 툴체인의 triplet (EL B1) |
| multilib | CPU·FPU 조합별로 미리 빌드한 라이브러리 묶음 | `-print-multi-directory`로 어느 것을 쓰는지 확인 |
| specs 파일 | GCC 드라이버의 명령 조립 규칙 | `--specs=nano.specs`가 `-lc`를 `-lc_nano`로 바꾼다 |
| newlib / newlib-nano | Red Hat 계열 임베디드 libc / 그 크기 최적화 빌드 | nano는 float printf 기본 제외 |
| picolibc | newlib + AVR libc 기반 작은 libc | tinystdio, TLS errno, Zephyr·ATfE 기본 |
| LLVM-libc | LLVM 프로젝트의 새 C 라이브러리 | ATfE 평가용 오버레이 |
| libgloss | newlib의 보드 지원 계층 | `libnosys`, `librdimon` |
| syscall stub | libc가 부르는 OS 대체 함수 | `_write`, `_sbrk`, `_exit` … |
| semihosting | `bkpt 0xab`로 디버거에게 I/O를 맡기는 규약 | 디버거 없으면 HardFault |
| libgcc | 컴파일러 보조 루틴 | 나눗셈·64비트·소프트 float (`__aeabi_*`) |
| freestanding | 표준 라이브러리 가정 없는 C 환경 | 그래도 `mem*` 4개는 필요 |
| loop distribution | 루프를 `memset`/`memcpy` 호출로 바꾸는 최적화 | `-ftree-loop-distribute-patterns` |
| `-Os` / `-Oz` | 크기 최적화 / 더 공격적인 크기 최적화 | GCC 14에서는 이 코드에서 같았다 |
| LTO | 링크 시점 전체 프로그램 최적화 | asm 참조 함수 삭제 주의 |
| `.constprop` | 상수 인자를 박아 넣은 함수 복제본 | LTO·IPA가 만든다 |
| `--gc-sections` | 참조 없는 입력 섹션 삭제 | `-ffunction-sections`와 짝 |
| `KEEP()` | 링커 스크립트에서 gc 금지 | 벡터 테이블·테이블 섹션 |
| section anchor | 한 블록 기준 주소 + 오프셋으로 전역 접근 | `-fdata-sections`가 끈다 |
| `.su` 파일 | `-fstack-usage` 출력 | 함수 하나의 프레임 크기 |
| stack painting | 스택을 무늬로 채우고 지워진 깊이로 측정 | high water mark의 원리 |
| map 파일 | 링커의 배치 보고서 | 입력 섹션·출처 오브젝트·fill |
| bloaty | Google의 바이너리 크기 분석기 | 섹션·심볼·컴파일 단위, diff |
| puncover | 크기·스택 분석기 (Python) | `.su` + ELF, JSON 보고서 |
| ATfE / ATfEP | Arm Toolchain for Embedded (Professional) | LLVM 기반, picolibc |
| toolchain variant | Zephyr의 툴체인 선택 | `zephyr`, `gnuarmemb`, `armclang`, `iar` … |
| TLS | thread-local storage | 스레드마다 다른 errno |

---

## 19. 요약 & 체크리스트

MCU 툴체인은 "컴파일러"가 아니라 **드라이버 + specs + multilib + libc + libgloss + libgcc + 링커 스크립트**의 묶음이고, `-###`·`-print-multi-directory`로 무엇이 실제로 링크되는지 확인할 수 있다. 같은 printf 프로그램이 libc에 따라 0.8 KB(자체 포맷터)·2.9 KB(picolibc 정수)·8.6 KB(newlib-nano)·22 KB(nano + float)·37.5 KB(newlib)로 45배까지 벌어졌다. libc의 아래층은 syscall stub(`_write`·`_sbrk`)이고, `nosys.specs`는 조용히 실패하며 semihosting은 디버거 없이 HardFault가 된다. 최적화에서는 RTOS 이미지 전체가 `-O0` → `-O2`로 크기 −28%·시간 −46%, `-Os`는 거의 공짜, `--gc-sections`가 가장 큰 절약(−59%), `-Os` + LTO가 크기·속도를 함께 12%·11% 줄였다. 대신 LTO는 asm에서만 부르는 함수를 지우고(`used`로 막는다), 최적화는 빈 지연 루프를 지운다(HW 시계로 기다린다). 크기는 map 파일을 오브젝트·아카이브 단위로 합쳐서(`mapsize.py`, bloaty, puncover) 찾고, 스택은 `.su` + 실측으로 정한다. Zephyr에서는 툴체인 변형이 libc 빌드 방식과 TLS까지 바꾸므로 CI와 PC를 맞춘다.

- [ ] `-print-multi-directory`로 내 CPU 옵션이 어느 multilib을 고르는지 확인할 수 있다 (FPU 칩에 `nofp`가 나오면 이상하다는 것을 안다)
- [ ] `nano.specs` / `nosys.specs` / `rdimon.specs` / `picolibc.specs`가 링크 명령을 어떻게 바꾸는지 `-###`로 보여 줄 수 있다
- [ ] `-ffreestanding`(컴파일)과 `-nostartfiles`/`-nodefaultlibs`/`-nostdlib`(링크)의 차이와, freestanding에서도 `memcpy`가 필요한 이유를 설명할 수 있다
- [ ] newlib / newlib-nano / picolibc / 자체 구현의 printf 크기를 대략 숫자로 말할 수 있다 (30 / 7 / 8 또는 2 / 1 KB)
- [ ] `-u _printf_float`와 picolibc `--printf=` 수준, `printf_float()` 함정을 안다
- [ ] newlib stub 7개를 쓰고, `nosys.specs`의 "will always fail" 경고를 CI에서 잡을 수 있다
- [ ] semihosting의 동작(`bkpt 0xab`, r0 명령 번호)과 양산에서 빼야 하는 이유를 설명할 수 있다
- [ ] `-O0/-O2/-O3/-Os/-Oz`의 크기·속도 경향을 RTOS 이미지와 핫 루프로 나눠 말할 수 있다
- [ ] 빈 지연 루프가 사라지는 이유와 `volatile`이 정답이 아닌 이유를 설명할 수 있다
- [ ] LTO 링크 실패(asm 참조 심볼)를 `used`·`--undefined`로 고치고, FreeRTOS가 어떻게 막았는지 짚을 수 있다
- [ ] `--print-gc-sections`로 버려진 것을 보고, `-fdata-sections`와 섹션 앵커의 트레이드오프를 안다
- [ ] map 파일을 오브젝트·아카이브 단위로 합산하는 스크립트를 쓰고 `size -A` 합과 대조할 수 있다

---

## Bare-Metal 노트와의 관계

| BM ID | BM 주제 | 이 노트의 해당 절 | Bare-Metal 쪽에 남길 내용 |
|---|---|---|---|
| BM F5 | C 런타임 · libc (newlib / nano / picolibc / LLVM-libc, syscall stub, semihosting, printf 크기) | 1(multilib·specs), 2(freestanding), 3(libc 비교·picolibc 수준), 4(stub·semihosting), 11.3(LLVM-libc·ATfE) | RTOS 없이 crt0을 직접 쓰는 최소 런타임: 자체 `memcpy`/`memset`(loop distribution 함정 포함), 100줄 정수 printf, `_sbrk` 없는 정적 할당 설계 |
| BM F6 | 최적화와 코드 크기 (-O 레벨, -Os/-Oz, LTO, gc-sections, map 분석 bloaty/puncover, 최적화가 타이밍을 바꿈) | 5(-O 레벨), 6(지연 루프·volatile), 7(LTO), 8(gc·섹션 앵커), 9(스택), 10(map·bloaty·puncover) | RTOS 커널이 없는 순수 루프 펌웨어에서의 사이클 단위 측정(DWT CYCCNT가 있는 실칩), 부트로더처럼 몇 KB 예산 안에서의 극단적 크기 기법 |
| BM F1 | 툴체인 지형 (GNU Arm, ATfE, IAR, Keil, GCC 15 gnu23) | 11(GCC vs Clang, ATfE), 12.1(컴파일러별 FreeRTOS 포트), 12.2(C17) | 툴체인 선택표 자체는 BM F1이 주인 — 이 노트는 RTOS 관점(포트 문법, Zephyr 변형)만 다뤘다 |

---

## 참고 자료

- GCC 문서 — Optimize Options(`-Os`, `-Oz`, `-flto`, `-ftree-loop-distribute-patterns`, `-fsection-anchors`), Developer Options(`-fstack-usage`, `-fcallgraph-info`), C Dialect Options(`-ffreestanding`, `-fno-builtin`), Link Options(`-nostartfiles`, `-nodefaultlibs`, `-nostdlib`, `-u`), Spec Files — [gcc.gnu.org/onlinedocs/gcc/](https://gcc.gnu.org/onlinedocs/gcc/)
- GNU ld 문서 — `--gc-sections`, `--print-gc-sections`, `KEEP`, `-Map` — [sourceware.org/binutils/docs/ld/](https://sourceware.org/binutils/docs/ld/)
- newlib (libc, libgloss, nano 빌드) — [sourceware.org/newlib/](https://sourceware.org/newlib/) · VM의 `/usr/lib/arm-none-eabi/newlib/nano.specs`, `thumb/v7-m/nofp/nosys.specs`, `rdimon.specs` (newlib 4.6.0.20260123)
- picolibc — [github.com/picolibc/picolibc](https://github.com/picolibc/picolibc) · VM의 `/usr/lib/gcc/arm-none-eabi/14.2.1/picolibc.specs`, `/usr/lib/picolibc/arm-none-eabi/include/stdio.h`(`printf_float`, 496~542행) (picolibc 1.8.10)
- Arm, "Semihosting for AArch32 and AArch64" 명세 (SYS_WRITE 0x05, SYS_EXIT 0x18, `BKPT 0xAB`) — [github.com/ARM-software/abi-aa](https://github.com/ARM-software/abi-aa)
- Arm, Armv7-M Architecture Reference Manual — NOP·BKPT 동작, 디버그 비활성 시 BKPT → HardFault, DHCSR
- Arm Toolchain for Embedded 릴리스 — [github.com/arm/arm-toolchain/releases](https://github.com/arm/arm-toolchain/releases) (2026-10 확인: 23.1.0, 22.1.0 …)
- Arm Community Blog, Arm Development Studio 2026.0 (ATfEP 22.1.0, LLVM-libc 오버레이, 2026-06-19) — [developer.arm.com/…/developmentstudio20260](https://developer.arm.com/community/arm-community-blogs/b/tools-software-ides-blog/posts/developmentstudio20260)
- Arm Community Blog, "Arm Toolchain for Embedded: migration from picolibc to LLVM libc" (2026-03-09) — [developer.arm.com/…/arm-toolchain-for-embedded-migration-from-picolibc-to-llvm-libc](https://developer.arm.com/community/arm-community-blogs/b/tools-software-ides-blog/posts/arm-toolchain-for-embedded-migration-from-picolibc-to-llvm-libc)
- Zephyr 문서 — Arm Toolchain for Embedded 페이지 [docs.zephyrproject.org/latest/develop/toolchains/arm_toolchain_for_embedded.html](https://docs.zephyrproject.org/latest/develop/toolchains/arm_toolchain_for_embedded.html), GNU Arm Embedded(`gnu_arm_embedded.rst`), Zephyr SDK(`zephyr_sdk.rst`) — v4.4.2 소스 트리 `doc/develop/toolchains/`
- Zephyr v4.4.2 소스: `Kconfig.zephyr`(STD_C choice, 최적화 choice 523~555행, LTO 557행), `cmake/compiler/gcc/compiler_flags.cmake`, `cmake/toolchain/`, `lib/libc/picolibc/`, `arch/arm/core/tls.c`, `kernel/busy_wait.c`, `doc/releases/migration-guide-4.4.rst`(C17 기본), `doc/releases/release-notes-4.4.rst`(SDK 1.0, Clang/LLVM 실험 지원)
- FreeRTOS-Kernel V11.3.2 소스: `portable/GCC/ARM_CM3/port.c`·`portmacro.h`, `portable/IAR/ARM_CM3/portasm.s`, `portable/RVDS/ARM_CM3/port.c`, `portable/ARMClang/Use-the-GCC-ports.txt`, `include/FreeRTOS.h`, `include/newlib-freertos.h`, `include/picolibc-freertos.h`, `tasks.c` — [github.com/FreeRTOS/FreeRTOS-Kernel](https://github.com/FreeRTOS/FreeRTOS-Kernel)
- bloaty — [github.com/google/bloaty](https://github.com/google/bloaty) (커밋 f1a83e6을 소스 빌드, 1.1)
- puncover — [github.com/HBehrens/puncover](https://github.com/HBehrens/puncover) · PyPI 0.8.0
- Embedded Linux 노트: EL B1(크로스 툴체인·`-###`·multilib vs multiarch), EL B2(C 라이브러리), EL B3(ELF·정적 링크 순서). RTOS 노트: A3(footprint), B5(링커 스크립트·시작 코드)
