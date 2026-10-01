# F7. 크로스 빌드와 모델 배치 — 툴체인, CMake, 링커 스크립트로 가중치·arena를 원하는 메모리에

> **이 노트를 다 읽으면**: target triple·float ABI·sysroot·libc를 보고 "이 오브젝트가 이 펌웨어에 링크될 수 있나"를 판단할 수 있다 · `.tflite` 모델을 `xxd -i` / `.incbin` / CMake 세 가지 방법으로 정렬을 지켜 바이너리에 넣고, 오브젝트 파일의 섹션·심볼로 확인할 수 있다 · 가중치는 QSPI XIP 또는 SRAM 복사본, arena는 DTCM, scratch는 SRAM2에 두는 링커 스크립트를 쓰고 부팅 copy-down 비용을 계산할 수 있다 · Cortex-M용 CMake toolchain file과 Android NDK 빌드 명령을 쓰고, `--gc-sections`·맵 파일·`size`로 flash/RAM 예산을 검증할 수 있다
> **JD 연결**: "Bring up toolchains, SDKs and new accelerator platforms", "Integrate models into firmware (C/C++/Rust)", "Optimizing models for MCUs & edge processors" · study_prep_list F7 행 — GCC/Clang 크로스 컴파일, CMake, **링커 스크립트로 모델·arena 배치** (flash / SRAM / TCM 섹션), Android NDK 빌드 (+ F2 "모델을 C 배열로 flash에 넣기", J1 "정적 메모리, arena 배치", J5 모델 업데이트)
> **Don 기준 난이도**: bare-metal 툴체인, 링커 스크립트, startup 코드, Trace32 디버깅은 SSD 펌웨어에서 매일 하던 일이라 **이미 강함** / 새로 배울 것은 "ML 산출물(`.tflite`, 수백 KB 가중치 blob, tensor arena)을 그 지식에 얹는 법", CMake toolchain file, Android NDK, 모델 파일의 정렬·버전 관리
> **선행 노트**: D2 (메모리 계산, 6.2절 섹션 읽기, 8절 예산 워크시트), E2 (1.4절 기능 매크로, 6절 자동 벡터화, 8~9절 float ABI·M-profile 플래그), E7 (2.1절 TCM, 5~6절 DMA·캐시), F2 (TFLite Micro — 병렬 작성 중, arena와 op resolver)

---

## 0. 큰 그림 — 이게 왜 필요한가

Don이 SSD 컨트롤러 펌웨어를 빌드할 때의 흐름은 이랬다: 소스 → 크로스 컴파일러 → 오브젝트 → 링커 스크립트로 TCM·SRAM·DRAM에 배치 → ELF → 바이너리 → 다운로드 → Trace32로 확인. edge ML 펌웨어도 **흐름은 완전히 같다**. 달라지는 것은 단 하나, 입력에 **사람이 쓰지 않은 거대한 데이터 덩어리**가 끼어든다는 점이다.

- `model.tflite` — 수십 KB~수 MB의 가중치 + 그래프 정의(FlatBuffer). 코드가 아니라 **데이터**지만 펌웨어 이미지에 들어가야 한다.
- tensor arena — 수십~수백 KB의 활성값 작업 공간. 초기값은 없지만 **가장 빠른 RAM**에 있어야 추론이 빠르다(D2, E7 2.1절).
- 커널 라이브러리(TFLM, CMSIS-NN) — 수백 개 op 중 모델이 쓰는 몇 개만 남겨야 flash가 남는다.

그래서 edge ML에서 "툴체인 bring-up"이라는 말은 결국 이 세 질문이다.

1. **어떻게 빌드하나?** — 올바른 triple·`-mcpu`·float ABI·libc로 런타임을 크로스 빌드 (1, 6, 7절)
2. **모델을 어떻게 넣나?** — 정렬과 섹션을 지키며 바이너리에 포함 (2, 3절)
3. **어디에 두나?** — 링커 스크립트로 가중치·arena·scratch를 메모리 영역에 배치하고, 크기를 검증 (4, 5, 8절)

```svg
<svg viewBox="0 0 680 260" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="f7a1" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs> <rect x="10" y="30" width="110" height="40" fill="#4a7bd0" fill-opacity="0.3" stroke="#4a7bd0"/> <text x="65" y="47" font-size="12" text-anchor="middle">model.tflite</text> <text x="65" y="63" font-size="12" text-anchor="middle">(가중치 blob)</text> <rect x="150" y="30" width="160" height="40" fill="none" stroke="#4a7bd0"/> <text x="230" y="47" font-size="12" text-anchor="middle">xxd -i · .incbin · CMake</text>
<text x="230" y="63" font-size="12" text-anchor="middle">→ .c 배열 / .S (3절)</text> <rect x="10" y="110" width="110" height="40" fill="#888" fill-opacity="0.25" stroke="#888"/> <text x="65" y="127" font-size="12" text-anchor="middle">main.c · TFLM</text> <text x="65" y="143" font-size="12" text-anchor="middle">CMSIS-NN 소스</text> <rect x="150" y="110" width="160" height="40" fill="none" stroke="currentColor"/> <text x="230" y="127" font-size="12" text-anchor="middle">clang/gcc --target -mcpu</text> <text x="230" y="143" font-size="12" text-anchor="middle">-mfloat-abi -O2 (1, 7절)</text>
<line x1="120" y1="50" x2="148" y2="50" stroke="currentColor" stroke-width="1.5" marker-end="url(#f7a1)"/> <line x1="120" y1="130" x2="148" y2="130" stroke="currentColor" stroke-width="1.5" marker-end="url(#f7a1)"/> <rect x="340" y="70" width="100" height="60" fill="none" stroke="currentColor"/> <text x="390" y="92" font-size="12" text-anchor="middle">*.o (ELF)</text> <text x="390" y="108" font-size="12" text-anchor="middle">입력 섹션들</text> <text x="390" y="124" font-size="12" text-anchor="middle">ar → lib*.a</text>
<polyline points="310,50 325,50 325,85 338,85" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#f7a1)"/> <polyline points="310,130 325,130 325,115 338,115" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#f7a1)"/> <rect x="465" y="10" width="95" height="40" fill="#e08a3c" fill-opacity="0.3" stroke="#e08a3c"/> <text x="512" y="27" font-size="12" text-anchor="middle">kws.ld</text> <text x="512" y="43" font-size="12" text-anchor="middle">MEMORY·SECTIONS</text> <rect x="465" y="70" width="95" height="60" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c"/>
<text x="512" y="92" font-size="12" text-anchor="middle">링커 ld/lld</text> <text x="512" y="108" font-size="12" text-anchor="middle">--gc-sections</text> <text x="512" y="124" font-size="12" text-anchor="middle">(4, 8절)</text> <line x1="440" y1="100" x2="463" y2="100" stroke="currentColor" stroke-width="1.5" marker-end="url(#f7a1)"/> <line x1="512" y1="50" x2="512" y2="68" stroke="currentColor" stroke-width="1.5" marker-end="url(#f7a1)"/> <rect x="585" y="70" width="85" height="60" fill="none" stroke="currentColor"/> <text x="627" y="92" font-size="12" text-anchor="middle">fw.elf</text>
<text x="627" y="108" font-size="12" text-anchor="middle">+ fw.map</text> <text x="627" y="124" font-size="12" text-anchor="middle">(심볼 포함)</text> <line x1="560" y1="100" x2="583" y2="100" stroke="currentColor" stroke-width="1.5" marker-end="url(#f7a1)"/> <rect x="585" y="190" width="85" height="40" fill="none" stroke="currentColor"/> <text x="627" y="207" font-size="12" text-anchor="middle">objcopy</text> <text x="627" y="223" font-size="12" text-anchor="middle">.bin / .hex</text> <line x1="627" y1="130" x2="627" y2="188" stroke="currentColor" stroke-width="1.5" marker-end="url(#f7a1)"/>
<rect x="460" y="190" width="105" height="40" fill="none" stroke="currentColor"/> <text x="512" y="207" font-size="12" text-anchor="middle">J-Link · Trace32</text> <text x="512" y="223" font-size="12" text-anchor="middle">+ QSPI loader</text> <line x1="585" y1="210" x2="567" y2="210" stroke="currentColor" stroke-width="1.5" marker-end="url(#f7a1)"/> <rect x="340" y="190" width="100" height="40" fill="#3f9a6b" fill-opacity="0.3" stroke="#3f9a6b"/> <text x="390" y="207" font-size="12" text-anchor="middle">MCU flash</text>
<text x="390" y="223" font-size="12" text-anchor="middle">QSPI · OTA 슬롯</text> <line x1="460" y1="210" x2="442" y2="210" stroke="currentColor" stroke-width="1.5" marker-end="url(#f7a1)"/> <rect x="10" y="190" width="300" height="40" fill="none" stroke="#888" stroke-dasharray="4 3"/> <text x="160" y="207" font-size="12" text-anchor="middle">CMake toolchain file (6절)</text> <text x="160" y="223" font-size="12" text-anchor="middle">위 전체의 컴파일러·플래그·ar·링커를 정한다</text>
</svg>
```

그림 1 — edge ML 펌웨어의 빌드 파이프라인. 아래 줄(소스 → 컴파일 → 링크 → ELF → 다운로드)은 Don이 아는 그대로다. 새로 생긴 것은 위 줄의 **모델 blob 경로**(파랑)와, 링커 스크립트가 가중치·arena를 **어느 메모리에** 놓을지 정하는 부분(주황)이다.

### 이 노트의 측정 환경과 정직한 한계

이 Mac(Apple clang 21, arm64 macOS)에는 **ARM용 링커가 없다**. 확인한 도구 상황:

| 도구 | 있나 | 이 노트에서 한 일 |
|---|---|---|
| Apple clang 21 (`--target=thumbv7em-none-eabihf`, `thumbv8.1m.main-none-eabihf`, `aarch64-linux-android30`) | ✅ | ARM ELF 오브젝트 `-c` 컴파일, `.S` 어셈블 |
| `xcrun llvm-objdump` / `llvm-nm` / `llvm-size` | ✅ | ELF 섹션·심볼·디스어셈블·크기 |
| `llvm-readelf`, `llvm-ar`, `ld.lld`, `arm-none-eabi-*` | ❌ | — (섹션 타입·정렬은 Python 20줄로 ELF 헤더를 직접 읽음) |
| `/usr/bin/ar`, `ranlib` (Apple cctools) | ✅ | ELF 멤버를 **버린다** — 6.3절에서 실측 |
| cmake 4.4.3 (`.venv/bin/cmake`), make, `xxd` | ✅ | toolchain file로 Cortex-M4 정적 라이브러리 실제 빌드 |
| Android NDK, Bazel | ❌ | 명령만 제시, "실행 안 함" 표기 |

그래서 **오브젝트(.o)와 아카이브(.a)까지는 전부 실측**, 링크 이후(링커 스크립트, 맵 파일)는 **문법을 신중히 쓴 예시이고 실제 링크로 검증하지 않았다**. 링크 단계의 동작(copy-down, NOLOAD, gc)은 호스트 C 시뮬레이션과 macOS 링커(`-dead_strip`)로 대신 확인한다. 각 블록에 어느 쪽인지 표시한다.

---

## 1. 크로스 컴파일 복습 — triple, ABI, sysroot, libc

Don에게는 복습이다. ML 런타임을 빌드할 때 자주 헷갈리는 부분만 짚는다.

### 1.1 host와 target

GNU 용어는 build(컴파일러가 도는 기계) / host(산출물이 도는 기계) / target(컴파일러가 코드를 만들 대상)이지만, 일상에서는 "host = 내 PC, target = 기기"로 부른다. 이 노트도 일상 용어를 쓴다.

### 1.2 target triple 읽기

triple은 `<arch><sub>-<vendor>-<os>-<abi>` 꼴이다. 빠진 칸은 생략하거나 `unknown`.

| triple | arch | os | abi | 어디서 |
|---|---|---|---|---|
| `thumbv6m-none-eabi` | Armv6-M (Thumb) | 없음(bare-metal) | EABI, soft float | Cortex-M0/M0+ |
| `thumbv7em-none-eabihf` | Armv7E-M (DSP 확장) | 없음 | EABI + **hard float** | Cortex-M4F/M7 |
| `thumbv8m.main-none-eabihf` | Armv8-M Mainline | 없음 | EABI hf | Cortex-M33 |
| `thumbv8.1m.main-none-eabihf` | Armv8.1-M (+MVE 가능) | 없음 | EABI hf | Cortex-M55/M85 |
| `arm-none-eabi` | (GCC 이름) 32-bit Arm | 없음 | EABI | arm-none-eabi-gcc의 접두어 |
| `aarch64-linux-android30` | AArch64 | Android, API 30 | (bionic) | 폰의 Cortex-A, NDK |
| `aarch64-linux-gnu` | AArch64 | Linux | glibc | Linux SBC, Yocto |

말로 하면: **triple의 앞부분이 명령어 집합, 뒷부분이 "누구의 규칙으로 함수를 부르고 어떤 라이브러리를 쓰나"**다. `-mcpu=cortex-m4`는 그 안에서 스케줄링·확장(DSP, FPU)을 고르는 더 구체적인 스위치다(E2 1.4절, 9.2절).

### 1.3 float ABI — eabi vs eabihf (E2 8절 요약 + 실측, 예제 1)

`eabi`와 `eabihf`의 차이는 **float 인자를 어느 레지스터로 넘기느냐**다. 같은 함수를 세 가지 `-mfloat-abi`로 컴파일해 보면 한눈에 보인다.

무엇을 확인하는 코드인지: `x × s + b` 한 줄이 soft / softfp / hard에서 어떤 명령이 되는지 본다.

```sh
cat > abi.c <<'EOF'
float scale_add(float x, float s, float b) { return x * s + b; }
EOF
for fa in soft softfp hard; do
  cc --target=arm-none-eabi -mcpu=cortex-m4 -mthumb -mfloat-abi=$fa -O2 -c abi.c -o abi_$fa.o
  xcrun llvm-objdump -dr --no-show-raw-insn abi_$fa.o | sed -n '/<scale_add>/,$p'
done
```

```text
== -mfloat-abi=soft
00000000 <scale_add>:
       0:      	push	{r4, lr}
       2:      	mov	r4, r2
       4:      	bl	0x4 <scale_add+0x4>     @ imm = #-0x4
			00000004:  R_ARM_THM_CALL	__aeabi_fmul
       8:      	mov	r1, r4
       a:      	bl	0xa <scale_add+0xa>     @ imm = #-0x4
			0000000a:  R_ARM_THM_CALL	__aeabi_fadd
       e:      	pop	{r4, pc}
== -mfloat-abi=softfp
00000000 <scale_add>:
       0:      	vmov	s2, r1
       4:      	vmov	s4, r0
       8:      	vmov	s0, r2
       c:      	vmul.f32	s2, s4, s2
      10:      	vadd.f32	s0, s2, s0
      14:      	vmov	r0, s0
      18:      	bx	lr
== -mfloat-abi=hard
00000000 <scale_add>:
       0:      	vmul.f32	s0, s0, s1
       4:      	vadd.f32	s0, s0, s2
       8:      	bx	lr
```

출력에서 볼 것:

- **soft**: FPU를 아예 안 쓰고 `__aeabi_fmul`/`__aeabi_fadd` 라이브러리 함수를 부른다(재배치 `R_ARM_THM_CALL`). 이 함수들은 링크 때 libgcc/compiler-rt가 채운다.
- **softfp**: FPU 명령은 쓰지만 인자는 정수 레지스터(r0~r2)로 받아 `vmov`로 옮긴다 — 호출마다 이동 비용.
- **hard**(= `eabihf`): 인자가 처음부터 s0~s2에 있다. 3 명령. **한 펌웨어 안의 모든 오브젝트·라이브러리가 같은 쪽이어야 링크된다** — 벤더가 준 미리 빌드된 `libcmsis-nn.a`가 softfp인데 앱이 hard면 GNU ld가 "uses VFP register arguments" 류 에러를 낸다.

ML 쪽 함의: 전처리(mel, 정규화)와 dequantize가 float이므로 soft float로 잘못 빌드하면 **결과는 맞는데 수십 배 느리다**. 증상이 "정확도 OK, latency만 이상"이라 찾기 어렵다.

### 1.4 sysroot와 libc — "컴파일러는 있는데 `string.h`가 없다"

컴파일러 자체가 가진 헤더(freestanding 헤더)와, C 라이브러리가 주는 헤더는 다르다. 실측으로 확인한다.

무엇을 확인하는 코드인지: sysroot 없이 Cortex-M55 triple로 각 헤더를 include해 본다.

```sh
for h in stdint.h stddef.h arm_acle.h arm_mve.h string.h stdio.h; do
  printf "%-10s " $h
  echo "#include <$h>" | cc --target=thumbv8.1m.main-none-eabihf -mcpu=cortex-m55 -x c -c - -o /dev/null 2>&1 |
    head -1 | grep -q error && echo "없음 (libc 헤더 필요)" || echo "OK (컴파일러 내장)"
done
```

```text
stdint.h   OK (컴파일러 내장)
stddef.h   OK (컴파일러 내장)
arm_acle.h OK (컴파일러 내장)
arm_mve.h  OK (컴파일러 내장)
string.h   없음 (libc 헤더 필요)
stdio.h    없음 (libc 헤더 필요)
```

출력에서 볼 것: `stdint.h`, `arm_mve.h`(Helium intrinsics) 같은 것은 **컴파일러 리소스 디렉터리**에 있다. `string.h`, `stdio.h`는 libc가 줘야 한다. 그래서 크로스 툴체인은 항상 "컴파일러 + **sysroot**(libc 헤더·라이브러리·startup 파일 묶음)" 짝이다. CMake에서는 `CMAKE_SYSROOT`, clang에서는 `--sysroot=`, GCC 툴체인은 설치 경로 안에 sysroot가 내장되어 있다.

임베디드용 libc 선택지:

| libc | 특징 | 어디서 |
|---|---|---|
| **newlib** | 오래된 표준. 큰 편 | Arm GNU Toolchain 기본 |
| **newlib-nano** | newlib의 작은 구성 (`--specs=nano.specs`). `printf`의 float 지원이 기본 꺼짐 (`-u _printf_float`로 켬) | Arm GNU Toolchain에 같이 들어 있음 |
| **picolibc** | newlib에서 갈라진 작은 libc, 스레드 로컬·스택 사용이 작음 | LLVM 계열 Arm 임베디드 툴체인, Zephyr 옵션 |
| 벤더 libc | Arm Compiler의 microlib, IAR DLIB 등 | 상용 툴체인 |

`--specs=nosys.specs`(syscall을 빈 stub으로)와 `--specs=rdimon.specs`(semihosting으로 `printf`를 디버거에 보냄, 9절)도 Arm GNU 툴체인의 자주 쓰는 링크 옵션이다.

### 1.5 툴체인 지도 (이름은 바뀌니 확인할 것)

| 툴체인 | 컴파일러 | 비고 |
|---|---|---|
| **Arm GNU Toolchain** | `arm-none-eabi-gcc` | 가장 흔함. TFLM Makefile의 기본(`TOOLCHAIN=gcc`) |
| **Arm Compiler for Embedded** (AC6) | `armclang` (LLVM 기반, 상용) | `armlink`와 **scatter file**(GNU 링커 스크립트와 다른 문법). TFLM에 `TOOLCHAIN=armclang` 경로가 있다 |
| **LLVM 기반 Arm 임베디드 툴체인** | `clang` + `lld` + picolibc | 과거 "LLVM Embedded Toolchain for Arm"이라는 이름으로 GitHub에 공개, 최근에는 "Arm Toolchain for Embedded (ATfE)"로 이어지는 것으로 알고 있다 — 이름·배포 형태는 확인 필요 |
| 업스트림 clang + lld | `clang --target=...` | 이 노트에서 쓴 방식 (단, 이 Mac은 lld 없음) |
| IAR, Segger, TI, Synopsys(ARC), Cadence(Xtensa) | 벤더 컴파일러 | DSP·NPU 벤더 SDK는 대개 자기 툴체인을 강제한다(F8) |

GCC와 Clang은 플래그 대부분이 같지만(`-mcpu -mfloat-abi -ffunction-sections`), **링커 스크립트 해석의 세부**(lld는 GNU ld 문법을 대부분 지원하지만 일부 차이)와 **기본 float ABI**가 다를 수 있다. 그래서 E2 9.2절의 결론 그대로 — `-mcpu -mthumb -mfpu -mfloat-abi` 네 개를 **항상 명시**한다.

### 1.6 Android 쪽 triple — 같은 컴파일러, 다른 세계

무엇을 확인하는 코드인지: 같은 Apple clang으로 Android triple을 주면 무엇이 달라지나.

```sh
for t in aarch64-linux-android30 aarch64-linux-gnu; do
  cc --target=$t -O2 -march=armv8.2-a+dotprod -c dot.c -o a64.o &&
  echo "$t: $(xcrun llvm-objdump -f a64.o | grep format)  sdot=$(xcrun llvm-objdump -d a64.o | grep -c sdot)"
done
echo | cc --target=aarch64-linux-android30 -dM -E - | grep -E "MIN_SDK|__PIC__ "
echo '#include <stdio.h>' | cc --target=aarch64-linux-android30 -x c -c - -o /dev/null 2>&1 | head -1
```

```text
aarch64-linux-android30: a64.o:	file format elf64-littleaarch64  sdot=2
aarch64-linux-gnu: a64.o:	file format elf64-littleaarch64  sdot=2
#define __ANDROID_MIN_SDK_VERSION__ 30
#define __PIC__ 2
<stdin>:1:10: fatal error: 'stdio.h' file not found
```

출력에서 볼 것: (`dot.c`는 7.2절의 int8/fp32 내적 파일이다.) 코드 생성은 Linux와 같다(SDOT 2개). Android triple은 API 레벨(`30`)을 매크로로 박고, **기본이 PIC**(`__PIC__ 2`)다 — Android 앱의 네이티브 코드는 `.so`로 로드되니까. 그리고 bionic libc 헤더는 NDK sysroot에만 있으므로 `stdio.h`가 없다. 실제로는 NDK의 `android.toolchain.cmake`가 이 sysroot·triple·API 레벨을 다 맞춰 준다(6.5절).

---

## 2. 섹션 — 가중치·arena는 오브젝트 파일 어디에 들어가나

D2 6.2절에서는 Mach-O(`size -m`)로 `const → __const`, 초기값 있는 전역 → `__data`, 초기값 없는 전역 → `__bss`를 확인했다. 여기서는 **진짜 Cortex-M ELF 오브젝트**로, 섹션 속성(`section`, `aligned`)이 어떻게 반영되는지까지 본다.

### 2.1 섹션의 세 가지 성격

| 출력 섹션 | ELF 타입 | 플래그 | flash 이미지에 바이트가 있나 | RAM을 쓰나 | ML 예 |
|---|---|---|---|---|---|
| `.text` | PROGBITS | AX (할당·실행) | 있음 | XIP면 아니오 | 커널 코드 |
| `.rodata` | PROGBITS | A (할당·읽기 전용) | 있음 | 아니오 | 가중치, LUT, 모델 blob |
| `.data` | PROGBITS | AW (할당·쓰기) | 있음 (초기값) | **예** (부팅 때 복사) | requant 표를 실수로 non-const로 |
| `.bss` | NOBITS | AW | **없음** | 예 (부팅 때 0) | tensor arena |
| `NOLOAD` 출력 섹션 | (링커가 NOBITS로) | AW | 없음 | 예 (초기화 안 함) | scratch, DMA 버퍼 |

말로 하면: **PROGBITS = 이미지에 바이트가 있다, NOBITS = 크기만 있다.** `.data`는 flash에 원본, RAM에 사본 — 두 번 센다(D2 6.2절).

### 2.2 실측 — `const`, `section`, `aligned`, `.bss` (예제 2)

무엇을 확인하는 코드인지: 가중치 두 개(기본 / 섹션 지정 + 16 B 정렬), requant 표, arena, scratch를 Cortex-M4 오브젝트로 만들고 섹션과 심볼을 본다.

```c
#include <stdint.h>
#define W_BYTES 3000
#define ARENA   (16 * 1024)

const int8_t g_w_plain[W_BYTES] = {1, 2, 3};                    /* 기본: const → .rodata */
__attribute__((section(".model_weights"), aligned(16)))
const int8_t g_w_placed[W_BYTES] = {4, 5, 6};                   /* 이름 붙인 섹션 + 16B 정렬 */
int32_t g_requant[8] = {1073741824, 1};                         /* 초기값 있는 RW → .data */
__attribute__((aligned(16))) static uint8_t g_arena[ARENA];     /* 초기값 없음 → .bss */
__attribute__((section(".noinit_scratch"), aligned(16)))
static uint8_t g_scratch[4096];                                 /* 별도 섹션 (NOLOAD 후보) */

uint8_t *arena_base(void) { return g_arena; }
uint8_t *scratch_base(void) { return g_scratch; }
int run(int i) { return g_w_plain[i] + g_w_placed[i] + g_requant[i & 7]; }
```

```sh
cc --target=thumbv7em-none-eabihf -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -O2 -Wall -Wextra \
   -c placement.c -o placement.o
xcrun llvm-objdump -h placement.o | grep -E "Idx| \.(text|rodata|model_weights|data|bss|noinit_scratch) "
xcrun llvm-objdump -t placement.o | grep -E "g_|run|_base"
```

```text
Idx Name            Size     VMA      Type
  2 .text           00000042 00000000 TEXT
  6 .rodata         00000bb8 00000000 DATA
  7 .model_weights  00000bb8 00000000 DATA
  8 .data           00000020 00000000 DATA
  9 .bss            00004000 00000000 BSS
 10 .noinit_scratch 00001000 00000000 DATA
00000000 l     O .bss	00004000 g_arena
00000000 l     O .noinit_scratch	00001000 g_scratch
00000000 g     F .text	0000000a arena_base
0000000c g     F .text	0000000a scratch_base
00000018 g     F .text	0000002a run
00000000 g     O .rodata	00000bb8 g_w_plain
00000000 g     O .model_weights	00000bb8 g_w_placed
00000000 g     O .data	00000020 g_requant
```

출력에서 볼 것:

- `0xbb8` = 3000 B. 두 가중치가 각각 `.rodata`와 `.model_weights`에 정확히 들어갔다. `__attribute__((section(...)))` 하나로 **입력 섹션 이름**을 바꿨고, 이 이름을 링커 스크립트가 잡아서 원하는 메모리에 보낸다(4절).
- `VMA`가 전부 0 — 오브젝트 파일은 아직 주소가 없다(재배치 가능). 주소는 링커가 정한다.
- `.noinit_scratch`의 Type이 **`DATA`** 다. `.bss`(BSS)가 아니다! 다음 절에서 이게 왜 문제인지 본다.

### 2.3 `llvm-readelf`가 없으니 ELF 헤더를 직접 읽는다 (예제 3)

`llvm-objdump -h`는 정렬(`sh_addralign`)과 NOBITS 여부를 정확히 보여 주지 않는다. `readelf -S`가 없으니 섹션 헤더 표를 Python으로 직접 읽는다 — ELF32 섹션 헤더는 32-bit 워드 10개짜리 구조체 배열일 뿐이다. 펌웨어 엔지니어에게는 레지스터 맵 파싱과 같은 일이다.

무엇을 확인하는 코드인지: 섹션별 타입·플래그·크기·정렬, 그리고 **.o 파일 안에 실제로 바이트를 차지하는지**를 출력한다.

```python
import struct, sys
TYPES = {1: "PROGBITS", 8: "NOBITS", 3: "STRTAB", 2: "SYMTAB", 9: "REL", 0x70000001: "ARM_EXIDX", 0x70000003: "ARM_ATTR"}
def flags(f):
    return "".join(c for b, c in ((2, "A"), (1, "W"), (4, "X")) if f & b) or "-"
d = open(sys.argv[1], "rb").read()
assert d[:4] == b"\x7fELF" and d[4] == 1          # 32-bit ELF만 (Cortex-M)
shoff, = struct.unpack_from("<I", d, 0x20)
shentsize, shnum, shstrndx = struct.unpack_from("<HHH", d, 0x2E)
secs = [struct.unpack_from("<10I", d, shoff + i * shentsize) for i in range(shnum)]
strtab = secs[shstrndx]
name = lambda off: d[strtab[4] + off:].split(b"\0")[0].decode()
want = sys.argv[2:] if len(sys.argv) > 2 else None
print(f"{'section':16s} {'type':9s} {'flags':5s} {'size':>6s} {'align':>5s} {'bytes in .o':>11s}")
for s in secs:
    n = name(s[0])
    if want and n not in want: continue
    t = TYPES.get(s[1], hex(s[1]))
    in_file = 0 if s[1] == 8 else s[5]
    print(f"{n:16s} {t:9s} {flags(s[2]):5s} {s[5]:6d} {s[8]:5d} {in_file:11d}")
```

```sh
.venv/bin/python elfsec.py placement.o .text .rodata .model_weights .data .bss .noinit_scratch
```

```text
section          type      flags   size align bytes in .o
.text            PROGBITS  AX        66     4          66
.rodata          PROGBITS  A       3000     1        3000
.model_weights   PROGBITS  A       3000    16        3000
.data            PROGBITS  AW        32     4          32
.bss             NOBITS    AW     16384    16           0
.noinit_scratch  PROGBITS  AW      4096    16        4096
```

출력에서 볼 것:

- **`.rodata`의 정렬이 1**이다. `int8_t` 배열이니 컴파일러는 1 B 정렬이면 충분하다고 본다. 모델 blob을 그냥 `const uint8_t model[]`로 두면 **16 B 정렬이 보장되지 않는다**(3.4절에서 왜 문제인지).
- `aligned(16)`을 준 `.model_weights`는 정렬 16. 이 정렬은 링커가 출력 섹션을 만들 때도 지켜 준다.
- `.bss` 16 KB arena는 .o 파일에 **0 바이트**. 반면 0으로 초기화된 4 KB scratch를 `.noinit_scratch`라는 이름으로 옮기자 **PROGBITS가 되어 4096 바이트의 0을 실제로 저장**한다. 링커 스크립트에서 이 출력 섹션에 `NOLOAD`를 붙이지 않으면 flash 4 KB가 0으로 낭비되고, `.data`처럼 복사 대상이 될 수도 있다.

### 2.4 섹션 이름이 타입을 바꾼다 (예제 4)

무엇을 확인하는 코드인지: 초기값 없는 배열을 세 가지 이름의 섹션에 넣고 타입을 비교한다.

```c
#include <stdint.h>
__attribute__((section(".noinit_scratch"))) uint8_t a_scratch[4096];
__attribute__((section(".bss.scratch2")))  uint8_t b_scratch[4096];
__attribute__((section(".noinit")))        uint8_t c_scratch[4096];
```

```text
section          type      flags   size align bytes in .o
.noinit_scratch  PROGBITS  AW      4096     1        4096
.bss.scratch2    NOBITS    AW      4096     1           0
.noinit          PROGBITS  AW      4096     1        4096
```

출력에서 볼 것: clang은 이름이 **`.bss.`로 시작할 때만** NOBITS로 만들었다. `.noinit`도 PROGBITS다(GCC는 버전에 따라 `.noinit`을 특별 취급하기도 한다 — 툴체인마다 확인). 실무 규칙 두 개:

1. 큰 zero 버퍼를 별도 섹션에 넣고 싶으면 이름을 `.bss.<무엇>`으로 짓거나,
2. 링커 스크립트의 출력 섹션에 **`(NOLOAD)`** 를 붙인다. NOLOAD는 입력이 PROGBITS여도 이미지에 바이트를 넣지 않는다(4.4절).

그리고 오브젝트를 만들 때마다 `size -A`(또는 위 스크립트)로 **"bytes in .o"가 의도대로인지** 확인하는 습관 — flash 예산이 갑자기 64 KB 늘었다면 대개 이 문제다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="f7a4" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs> <text x="70" y="18" font-size="13" text-anchor="middle">소스</text> <text x="300" y="18" font-size="13" text-anchor="middle">입력 섹션 (.o, -ffunction-sections)</text> <text x="580" y="18" font-size="13" text-anchor="middle">출력 섹션 (ELF) → 메모리</text>
<rect x="10" y="40" width="120" height="90" fill="none" stroke="#888"/><text x="70" y="80" font-size="12" text-anchor="middle">fw_main.c</text><text x="70" y="96" font-size="12" text-anchor="middle">(infer, LUT, arena)</text> <rect x="10" y="150" width="120" height="70" fill="none" stroke="#888"/><text x="70" y="182" font-size="12" text-anchor="middle">kernels.c</text><text x="70" y="198" font-size="12" text-anchor="middle">(op 커널 3개)</text>
<rect x="10" y="250" width="120" height="40" fill="none" stroke="#4a7bd0"/><text x="70" y="274" font-size="12" text-anchor="middle">model.S (.incbin)</text> <rect x="220" y="30" width="160" height="20" fill="#888" fill-opacity="0.2" stroke="#888"/><text x="300" y="45" font-size="12" text-anchor="middle">.text.infer 68</text> <rect x="220" y="55" width="160" height="20" fill="#3f9a6b" fill-opacity="0.25" stroke="#3f9a6b"/><text x="300" y="70" font-size="12" text-anchor="middle">.data.g_mel_lut 160</text>
<rect x="220" y="80" width="160" height="20" fill="#4a7bd0" fill-opacity="0.2" stroke="#4a7bd0"/><text x="300" y="95" font-size="12" text-anchor="middle">.rodata.g_window 1024</text> <rect x="220" y="105" width="160" height="20" fill="#e08a3c" fill-opacity="0.3" stroke="#e08a3c"/><text x="300" y="120" font-size="12" text-anchor="middle">.bss.g_arena 49152</text> <rect x="220" y="130" width="160" height="20" fill="#e08a3c" fill-opacity="0.3" stroke="#e08a3c"/><text x="300" y="145" font-size="12" text-anchor="middle">.bss.scratch 16384</text>
<rect x="220" y="160" width="160" height="20" fill="#888" fill-opacity="0.2" stroke="#888"/><text x="300" y="175" font-size="12" text-anchor="middle">.text.dot_s8 34</text> <rect x="220" y="185" width="160" height="20" fill="none" stroke="#d0564a" stroke-dasharray="4 3"/><text x="300" y="200" font-size="12" text-anchor="middle">.text.relu_s32 6</text> <rect x="220" y="210" width="160" height="20" fill="none" stroke="#d0564a" stroke-dasharray="4 3"/><text x="300" y="225" font-size="12" text-anchor="middle">.text.unused_kernel 26</text>
<rect x="220" y="260" width="160" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/><text x="300" y="275" font-size="12" text-anchor="middle">.model_weights 98308</text> <line x1="130" y1="60" x2="218" y2="40" stroke="#888"/><line x1="130" y1="110" x2="218" y2="140" stroke="#888"/> <line x1="130" y1="170" x2="218" y2="170" stroke="#888"/><line x1="130" y1="200" x2="218" y2="220" stroke="#888"/> <line x1="130" y1="270" x2="218" y2="270" stroke="#888"/>
<rect x="510" y="30" width="140" height="30" fill="none" stroke="currentColor"/><text x="580" y="50" font-size="12" text-anchor="middle">.text → FLASH</text> <rect x="510" y="70" width="140" height="30" fill="none" stroke="currentColor"/><text x="580" y="90" font-size="12" text-anchor="middle">.rodata → FLASH</text> <rect x="510" y="110" width="140" height="30" fill="none" stroke="currentColor"/><text x="580" y="130" font-size="12" text-anchor="middle">.data → SRAM AT> FLASH</text>
<rect x="510" y="150" width="140" height="30" fill="none" stroke="currentColor"/><text x="580" y="170" font-size="12" text-anchor="middle">.arena_dtcm → DTCM</text> <rect x="510" y="190" width="140" height="30" fill="none" stroke="currentColor"/><text x="580" y="210" font-size="12" text-anchor="middle">.scratch → SRAM2</text> <rect x="510" y="230" width="140" height="30" fill="none" stroke="#d0564a" stroke-dasharray="4 3"/><text x="580" y="250" font-size="12" text-anchor="middle">gc로 제거됨</text>
<rect x="510" y="270" width="140" height="30" fill="none" stroke="#4a7bd0"/><text x="580" y="290" font-size="12" text-anchor="middle">.model_xip → QSPI</text> <line x1="380" y1="40" x2="508" y2="45" stroke="currentColor" marker-end="url(#f7a4)"/> <line x1="380" y1="170" x2="508" y2="50" stroke="currentColor" marker-end="url(#f7a4)"/> <line x1="380" y1="90" x2="508" y2="85" stroke="currentColor" marker-end="url(#f7a4)"/> <line x1="380" y1="65" x2="508" y2="125" stroke="currentColor" marker-end="url(#f7a4)"/>
<line x1="380" y1="115" x2="508" y2="165" stroke="currentColor" marker-end="url(#f7a4)"/> <line x1="380" y1="140" x2="508" y2="205" stroke="currentColor" marker-end="url(#f7a4)"/> <line x1="380" y1="195" x2="508" y2="242" stroke="#d0564a" stroke-dasharray="4 3" marker-end="url(#f7a4)"/> <line x1="380" y1="220" x2="508" y2="248" stroke="#d0564a" stroke-dasharray="4 3" marker-end="url(#f7a4)"/> <line x1="380" y1="270" x2="508" y2="285" stroke="#4a7bd0" marker-end="url(#f7a4)"/> <text x="10" y="320" font-size="12">숫자 = 8.2절에서 실측한 바이트. 빨간 점선 = 아무도 참조하지 않아 --gc-sections가 버리는 입력 섹션</text>
</svg>
```

그림 2 — 섹션의 흐름. 컴파일러는 `-ffunction-sections -fdata-sections`로 함수·변수마다 입력 섹션을 따로 만들고, 링커 스크립트는 입력 섹션 이름 패턴(`.text.*`, `.bss.*`, `.model_weights`)으로 출력 섹션에 모아 메모리 영역에 놓는다. 참조되지 않는 입력 섹션은 gc로 사라진다(7.3절).

---

## 3. 모델을 바이너리에 넣는 세 가지 방법

파일 시스템이 없는 MCU에서 모델은 **펌웨어 이미지의 일부**이거나, 별도 flash 영역에 따로 구운 blob이다. 이미지에 넣는 대표적인 방법 세 가지를 실제 TFLM 예제 모델(`hello_world_int8.tflite`, 2704 B — `.tools/tflite-micro` 안의 실제 파일)로 해 본다.

### 3.1 방법 1 — `xxd -i` (가장 흔함, 함정도 가장 많음) (예제 5)

무엇을 확인하는 코드인지: `xxd -i`가 만드는 C 파일의 모양과, 그 결과가 어느 섹션에 들어가는지 본다.

```sh
cp .tools/tflite-micro/tensorflow/lite/micro/examples/hello_world/models/hello_world_int8.tflite model.tflite
xxd -i model.tflite > model_data.cc
head -4 model_data.cc; echo ...; tail -3 model_data.cc; wc -l model_data.cc
xxd -l 16 model.tflite
```

```text
unsigned char model_tflite[] = {
  0x28, 0x00, 0x00, 0x00, 0x54, 0x46, 0x4c, 0x33, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00, 0x20, 0x00,
  0x04, 0x00, 0x08, 0x00, 0x0c, 0x00, 0x10, 0x00, 0x14, 0x00, 0x00, 0x00,
...
  0x09, 0x00, 0x00, 0x00
};
unsigned int model_tflite_len = 2704;
     229 model_data.cc
00000000: 2800 0000 5446 4c33 0000 0000 0000 0000  (...TFL3........
```

출력에서 볼 것:

- 바이트 4~7이 `TFL3` — TFLite FlatBuffer의 **file identifier**다. 첫 4바이트(`0x28` = 40)는 루트 테이블까지의 오프셋. 펌웨어에서 모델을 받으면 이 4바이트를 먼저 확인하면 "엉뚱한 blob"을 빨리 거른다(9.4절).
- `xxd -i`는 **`const`가 없고 정렬도 없다.** 그대로 컴파일하면:

```sh
cc --target=thumbv7em-none-eabihf -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -O2 -x c++ -c model_data.cc -o raw.o
.venv/bin/python elfsec.py raw.o .data .rodata
```

```text
section          type      flags   size align bytes in .o
.data            PROGBITS  AW      2708     4        2708
```

모델 2704 B + 길이 변수 4 B가 **`.data`** 로 갔다. 즉 flash에 원본 + RAM에 사본 — 모델 크기만큼 SRAM을 **공짜로 날린다**. 300 KB 모델이면 MCU SRAM 전부다. 고친 버전(앞에 `extern` 선언 추가, `alignas(16) const`):

```sh
# sed로 선언부를 고쳐 model_fixed.cc 생성 (명령 생략), 결과 확인:
head -2 model_fixed.cc; tail -2 model_fixed.cc
cc ... -x c++ -c model_fixed.cc -o fixed.o && .venv/bin/python elfsec.py fixed.o .data .rodata && xcrun llvm-nm -S fixed.o
```

```text
extern const unsigned char g_model[];
alignas(16) const unsigned char g_model[] = {
extern const unsigned int g_model_len;
const unsigned int g_model_len = 2704;
section          type      flags   size align bytes in .o
.rodata          PROGBITS  A       2708    16        2708
00000000 00000a90 R g_model
00000a90 00000004 R g_model_len
```

이제 `.rodata`, 정렬 16, 심볼 타입 `R`(읽기 전용). 

**C++ 함정 하나**: `extern` 선언을 빼고 처음 컴파일했을 때 `fixed.o`에는 `g_model_len`(4 B)만 남고 **`g_model`이 통째로 사라졌다**. C++에서 namespace 범위의 `const` 변수는 **internal linkage**(파일 안에서만 보임, C의 `static`과 같음)라서, 그 파일 안에서 아무도 안 쓰면 컴파일러가 버린다. 그러면 다른 파일의 `extern const unsigned char g_model[];`이 링크 에러("undefined reference")가 된다. 해결: 헤더의 `extern` 선언을 `.cc`에서 include하거나, 정의에 `extern`을 붙인다. TFLM의 생성기도 헤더(`extern const ...`)와 `.cc`(정의)를 한 쌍으로 만든다.

### 3.2 TFLM이 실제로 쓰는 방식 — 확인

`.tools/tflite-micro`(이 노트 작성 시점의 체크아웃 `06f0b46`)에서 직접 확인한 사실:

- `tensorflow/lite/micro/tools/generate_cc_arrays.py`는 모델을 `alignas(16) const <type> <name>[] = {...};` 로 쓰고, 헤더에 `constexpr unsigned int <name>_size` 와 `extern const <type> <name>[];` 을 쓴다.
- `micro_arena_constants.h`: "We align tensor buffers to 16-byte boundaries, since this is a common requirement for SIMD extensions." — `MicroArenaBufferAlignment()`가 16을 돌려준다.
- `micro_allocator.h` 주석: tensor arena는 `alignas(16)`으로 두라, "otherwise some head room will be wasted"(정렬 안 되면 앞부분 일부를 버려서 맞춘다). `micro_interpreter.h`: 정렬 안 된 arena면 필요한 크기가 `arena_used_bytes() + 16`이 된다.

### 3.3 방법 2 — 어셈블러 `.incbin` (예제 6)

C 배열로 바꾸지 않고, 어셈블러가 **파일 바이트를 그대로** 섹션에 붙이게 한다. 수 MB 모델에서 `xxd -i` 소스(바이트당 약 6글자)가 컴파일러를 느리게 만드는 문제가 없다.

무엇을 확인하는 코드인지: `.incbin`으로 만든 오브젝트의 섹션·정렬·심볼, 그리고 바이트가 원본과 같은지.

```text
    .section .model_weights, "a", %progbits   @ "a" = allocatable, 쓰기 불가(읽기 전용)
    .balign 16                                @ TFLM 생성기와 같은 16 B 정렬
    .global g_model, g_model_end
g_model:
    .incbin "model.tflite"                    @ 파일 바이트를 그대로 붙인다
g_model_end:
    .balign 4
    .global g_model_size
g_model_size:
    .word g_model_end - g_model               @ 크기를 링크 타임 상수로
```

```sh
cc --target=thumbv7em-none-eabihf -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -c model_incbin.S -o incbin.o
.venv/bin/python elfsec.py incbin.o .model_weights
xcrun llvm-nm -n -S incbin.o
xcrun llvm-objdump -s -j .model_weights incbin.o | sed -n "4,5p;\$p"
```

```text
section          type      flags   size align bytes in .o
.model_weights   PROGBITS  A       2708    16        2708
00000000 00000000 R g_model
00000a90 00000000 R g_model_end
00000a90 00000000 R g_model_size
 0000 28000000 54464c33 00000000 00000000  (...TFL3........
 0010 00000000 14002000 04000800 0c001000  ...... .........
 0a90 900a0000                             ....
```

출력에서 볼 것:

- 섹션 크기 2708 = 모델 2704 + 크기 워드 4. 마지막 워드 `900a0000`(little-endian) = `0x0a90` = 2704 — 어셈블러가 `g_model_end - g_model`을 계산해 넣었다.
- 심볼 크기 열이 0이다(`.size` 지시어를 안 써서). 디버거·맵 파일에서 크기를 보고 싶으면 `.type g_model, %object` 와 `.size g_model, g_model_end - g_model` 을 추가한다.
- C 쪽 선언: `extern const uint8_t g_model[]; extern const uint32_t g_model_size;`
- `.incbin`의 파일 경로는 어셈블러의 `-I` 경로에서도 찾는다(6.2절의 CMake 빌드에서 `target_include_directories`로 통과). **모델이 바뀌어도 `.S`는 안 바뀌므로**, 빌드 시스템에 의존성을 명시해야 재빌드된다 — 흔한 "새 모델 넣었는데 옛 모델이 돈다" 버그.

### 3.4 방법 3 — CMake `file(READ ... HEX)`로 빌드 때 생성 (예제 7)

Python·xxd 없이 CMake만으로 C 배열을 생성한다. Windows CI까지 같은 스크립트로 돈다는 장점이 있다. SHA-256을 주석에 박아 두면 "이 펌웨어에 어떤 모델이 들어갔나"를 소스만으로 추적할 수 있다.

무엇을 확인하는 코드인지: `cmake -P`로 스크립트를 실행해 C 파일을 만들고, 결과 바이트가 원본·`.incbin`과 같은지 비교한다.

```text
# 사용: cmake -DIN=model.tflite -DOUT=model_data.c -DSYM=g_model -P embed_model.cmake
file(READ "${IN}" hex HEX)                                   # 파일 전체 → "28000000544..." 16진 문자열
string(LENGTH "${hex}" nhex)
math(EXPR nbytes "${nhex} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," body "${hex}")   # 2글자마다 0x..,
string(REGEX REPLACE "(0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,)" "\\1\n  " body "${body}")
file(SHA256 "${IN}" digest)
file(WRITE "${OUT}"
  "/* generated from ${IN} — do not edit. sha256=${digest} */\n"
  "#include <stdint.h>\n"
  "#include <stddef.h>\n"
  "__attribute__((section(\".model_weights\"), aligned(16)))\n"
  "const uint8_t ${SYM}[${nbytes}] = {\n  ${body}\n};\n"
  "const size_t ${SYM}_len = ${nbytes};\n")
message(STATUS "embedded ${IN}: ${nbytes} bytes -> ${OUT}")
```

```sh
.venv/bin/cmake -DIN=model.tflite -DOUT=model_cmake.c -DSYM=g_model -P embed_model.cmake
head -6 model_cmake.c; tail -3 model_cmake.c
cc --target=thumbv7em-none-eabihf -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -O2 -std=c11 -Wall -Wextra -c model_cmake.c -o mc.o
.venv/bin/python elfsec.py mc.o .model_weights .rodata; xcrun llvm-nm -S mc.o
```

```text
-- embedded model.tflite: 2704 bytes -> model_cmake.c
/* generated from model.tflite — do not edit. sha256=505ee4fae7fa46ab67bea4c08b4969eb3eb8b9114c50595ec4a29d9a27993202 */
#include <stdint.h>
#include <stddef.h>
__attribute__((section(".model_weights"), aligned(16)))
const uint8_t g_model[2704] = {
  0x28,0x00,0x00,0x00,0x54,0x46,0x4c,0x33,0x00,0x00,0x00,0x00,
  0x09,0x00,0x00,0x00,
};
const size_t g_model_len = 2704;
section          type      flags   size align bytes in .o
.model_weights   PROGBITS  A       2704    16        2704
.rodata          PROGBITS  A          4     4           4
00000000 00000a90 R g_model
00000000 00000004 R g_model_len
```

세 방법의 바이트가 정말 같은지, `.o`의 섹션 내용을 꺼내 원본 파일과 비교했다(ELF 섹션 오프셋으로 잘라 내는 Python 10줄, 2.3절 스크립트와 같은 방식):

```text
2704 2708 True True 900a0000
```

출력에서 볼 것: CMake 버전 2704 B, `.incbin` 버전 2708 B. 둘 다 원본과 바이트 단위로 일치(`True True`), `.incbin` 뒤 4 B는 크기 워드 `900a0000`. 실제 프로젝트에서는 이 스크립트를 `add_custom_command(OUTPUT model_data.c COMMAND ${CMAKE_COMMAND} -P ... DEPENDS model.tflite)`로 걸어서 **모델이 바뀌면 자동 재생성**되게 한다.

### 3.5 세 방법 비교와 정렬 요구

| 방법 | 장점 | 단점 | 언제 |
|---|---|---|---|
| `xxd -i` | 어디서나 됨, 디버거에서 배열로 보임 | `const`·정렬 없음(직접 고쳐야), 큰 모델이면 소스 수십 MB·컴파일 느림 | 작은 모델, 예제 |
| `.incbin` | 빠름, 바이트 그대로, 섹션·정렬 제어 쉬움 | 어셈블러 문법(GNU/armclang 차이), 의존성 직접 명시 | 수백 KB~MB 모델 |
| CMake `file(READ HEX)` / TFLM `generate_cc_arrays.py` | 빌드 시스템에 통합, 해시 기록 | 큰 파일은 CMake 정규식이 느림 | 빌드 재현성이 중요한 제품 |
| 별도 파티션 (펌웨어와 분리) | 모델만 OTA 가능(J5), 펌웨어 재빌드 불필요 | 런타임에 주소·헤더·CRC 검증 필요(9.4절) | 제품 |

**정렬은 왜 16 B인가** — 세 겹의 이유가 있다.

1. **SIMD 로드**: TFLM 주석 그대로, 16 B는 128-bit 벡터(Helium `vldrw`, NEON `ld1`) 한 줄이다. 정렬된 주소면 한 번의 버스 트랜잭션으로 읽힌다.
2. **FlatBuffer 내부의 스칼라**: `.tflite` 안의 int32 bias, float scale, 오프셋 테이블은 **버퍼 시작 기준으로** 정렬되어 직렬화된다. 버퍼 시작이 홀수 주소면 그 안의 int32도 비정렬이 된다. Cortex-M0/M0+(Armv6-M)는 비정렬 워드 접근에서 **HardFault**, M3/M4/M7도 `LDRD`/`LDM`은 비정렬을 허용하지 않는다. Cortex-A에서도 일부 SIMD 경로는 정렬을 가정한다.
3. **arena 낭비**: arena가 비정렬이면 TFLM은 앞부분을 잘라 맞추므로 최대 15 B를 잃는다(3.2절 주석). 작지만, 정렬된 arena를 기대하는 벤더 커널(NPU DMA)은 아예 실패할 수 있다.

NPU용 모델(예: Ethos-U용 Vela 출력, F2)은 **더 큰 정렬**(DMA burst·캐시 라인 32~64 B)을 요구할 수 있다 — 벤더 문서를 확인한다. 그래서 링커 스크립트에서 모델 섹션은 `ALIGN(16)` 이상, NPU가 직접 읽으면 그 요구에 맞춘다.

---

## 4. 링커 스크립트로 배치 — 가중치는 QSPI·SRAM, arena는 DTCM

### 4.1 배치 전략 고르기

D2 1.3절에서 "XIP로 flash에서 바로 읽을까, RAM에 복사할까"를 계산으로 봤다. 링커 스크립트로 옮기면 선택지는 이렇다.

| 무엇 | 어디 | 링커 스크립트 표현 | 왜 |
|---|---|---|---|
| 큰 가중치 (대부분 layer) | 외부 QSPI/OSPI NOR, **XIP** | `> QSPI` | 내부 flash가 모자람. 읽기 전용이라 복사 불필요. 캐시가 hit하면 꽤 빠름 |
| 자주 읽는 작은 가중치 (첫 conv, 반복 호출 layer) | SRAM에 **복사본** | `> SRAM1 AT> QSPI` | XIP 대역폭(D2 1.3절)이 병목일 때. 부팅 비용(5절)과 RAM을 대가로 |
| tensor arena | **DTCM** | `(NOLOAD) > DTCM` | 0-wait, 캐시 없음 → 지연이 결정적(E7 2.1절). 초기값 불필요 |
| im2col·scratch, 마이크 DMA 버퍼 | SRAM2 (다른 bank) | `(NOLOAD) > SRAM2` | CPU가 arena를 읽는 동안 DMA가 다른 bank에 써서 **bank 충돌** 회피 |
| hot 커널 코드 (CMSIS-NN conv 내부 루프) | ITCM | `> ITCM AT> FLASH` | flash wait-state·캐시 miss 제거 |
| 런타임·나머지 코드, LUT | 내부 flash | `> FLASH` | 기본 |

**주의**: DTCM은 대개 **DMA가 접근할 수 없거나**(칩마다 다름) 느리다. NPU·DMA가 읽어야 하는 버퍼는 DMA 가능 영역(SRAM1/2)에 둔다. 또 캐시가 있는 SRAM에 DMA 버퍼를 두면 clean/invalidate가 필요하다(E7 6절). "arena는 DTCM"이 항상 정답은 아니다 — **누가 읽고 쓰느냐**로 정한다.

```svg
<svg viewBox="0 0 680 390" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="f7a2" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs> <text x="60" y="18" font-size="13" text-anchor="middle">QSPI NOR</text><text x="60" y="34" font-size="12" text-anchor="middle">0x9000_0000</text> <text x="172" y="18" font-size="13" text-anchor="middle">내부 FLASH</text><text x="172" y="34" font-size="12" text-anchor="middle">0x0800_0000</text>
<text x="284" y="18" font-size="13" text-anchor="middle">ITCM</text><text x="284" y="34" font-size="12" text-anchor="middle">0x0000_0000</text> <text x="396" y="18" font-size="13" text-anchor="middle">DTCM</text><text x="396" y="34" font-size="12" text-anchor="middle">0x2000_0000</text> <text x="508" y="18" font-size="13" text-anchor="middle">SRAM1 (AXI)</text><text x="508" y="34" font-size="12" text-anchor="middle">0x2400_0000</text> <text x="620" y="18" font-size="13" text-anchor="middle">SRAM2</text><text x="620" y="34" font-size="12" text-anchor="middle">0x3000_0000</text>
<rect x="10" y="45" width="100" height="110" fill="#4a7bd0" fill-opacity="0.45" stroke="#4a7bd0"/><text x="60" y="92" font-size="12" text-anchor="middle">.model_xip</text><text x="60" y="108" font-size="12" text-anchor="middle">큰 가중치</text><text x="60" y="124" font-size="12" text-anchor="middle">(XIP로 읽음)</text> <rect x="10" y="155" width="100" height="40" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0" stroke-dasharray="4 3"/><text x="60" y="172" font-size="12" text-anchor="middle">.model_hot</text><text x="60" y="187" font-size="12" text-anchor="middle">원본 (LMA)</text>
<rect x="10" y="195" width="100" height="40" fill="none" stroke="#888" stroke-dasharray="4 3"/><text x="60" y="219" font-size="12" text-anchor="middle">모델 슬롯 B</text> <rect x="122" y="45" width="100" height="22" fill="none" stroke="#888"/><text x="172" y="61" font-size="12" text-anchor="middle">.isr_vector</text> <rect x="122" y="67" width="100" height="60" fill="#888" fill-opacity="0.25" stroke="#888"/><text x="172" y="92" font-size="12" text-anchor="middle">.text</text><text x="172" y="108" font-size="12" text-anchor="middle">TFLM·드라이버</text>
<rect x="122" y="127" width="100" height="30" fill="#4a7bd0" fill-opacity="0.2" stroke="#4a7bd0"/><text x="172" y="147" font-size="12" text-anchor="middle">.rodata LUT</text> <rect x="122" y="157" width="100" height="26" fill="none" stroke="#888" stroke-dasharray="4 3"/><text x="172" y="175" font-size="12" text-anchor="middle">.data 원본</text> <rect x="122" y="183" width="100" height="26" fill="none" stroke="#888" stroke-dasharray="4 3"/><text x="172" y="201" font-size="12" text-anchor="middle">.itcm 원본</text>
<rect x="234" y="45" width="100" height="50" fill="#888" fill-opacity="0.35" stroke="#888"/><text x="284" y="66" font-size="12" text-anchor="middle">.itcm_text</text><text x="284" y="82" font-size="12" text-anchor="middle">hot 커널</text> <rect x="346" y="45" width="100" height="100" fill="#e08a3c" fill-opacity="0.45" stroke="#e08a3c"/><text x="396" y="88" font-size="12" text-anchor="middle">.arena_dtcm</text><text x="396" y="104" font-size="12" text-anchor="middle">NOLOAD</text>
<rect x="346" y="145" width="100" height="40" fill="#888" fill-opacity="0.25" stroke="#888"/><text x="396" y="169" font-size="12" text-anchor="middle">.stack</text> <rect x="458" y="45" width="100" height="26" fill="#888" fill-opacity="0.25" stroke="#888"/><text x="508" y="63" font-size="12" text-anchor="middle">.data</text> <rect x="458" y="71" width="100" height="30" fill="#888" fill-opacity="0.25" stroke="#888"/><text x="508" y="91" font-size="12" text-anchor="middle">.bss · heap</text>
<rect x="458" y="101" width="100" height="40" fill="#4a7bd0" fill-opacity="0.45" stroke="#4a7bd0"/><text x="508" y="118" font-size="12" text-anchor="middle">.model_hot</text><text x="508" y="134" font-size="12" text-anchor="middle">(복사본, VMA)</text> <rect x="570" y="45" width="100" height="70" fill="#3f9a6b" fill-opacity="0.4" stroke="#3f9a6b"/><text x="620" y="70" font-size="12" text-anchor="middle">.scratch</text><text x="620" y="86" font-size="12" text-anchor="middle">NOLOAD</text><text x="620" y="102" font-size="12" text-anchor="middle">im2col</text>
<rect x="570" y="115" width="100" height="40" fill="#3f9a6b" fill-opacity="0.4" stroke="#3f9a6b"/><text x="620" y="132" font-size="12" text-anchor="middle">마이크 DMA</text><text x="620" y="147" font-size="12" text-anchor="middle">ping-pong</text> <polyline points="60,235 60,290 508,290 508,143" fill="none" stroke="#4a7bd0" stroke-width="1.5" marker-end="url(#f7a2)"/> <text x="250" y="285" font-size="12">부팅 때 복사 (QSPI → SRAM1)</text> <polyline points="172,209 172,260 480,260 480,73" fill="none" stroke="currentColor" stroke-width="1.2" stroke-dasharray="3 3" marker-end="url(#f7a2)"/>
<text x="250" y="255" font-size="12">.data 복사</text> <polyline points="200,209 200,240 284,240 284,97" fill="none" stroke="currentColor" stroke-width="1.2" stroke-dasharray="3 3" marker-end="url(#f7a2)"/> <text x="290" y="232" font-size="12">ITCM 복사</text> <rect x="346" y="320" width="100" height="30" fill="none" stroke="currentColor"/><text x="396" y="340" font-size="12" text-anchor="middle">Cortex-M CPU</text> <line x1="110" y1="100" x2="344" y2="330" stroke="#4a7bd0" stroke-width="1.2" marker-end="url(#f7a2)"/> <text x="110" y="330" font-size="12">XIP 읽기 (캐시 경유)</text>
<text x="10" y="378" font-size="12">주소는 STM32H7 계열과 비슷하게 잡은 가상의 예 — 실제 값은 칩 reference manual의 memory map에서</text>
</svg>
```

그림 3 — 메모리 지도 위의 모델 배치. 파랑 = 가중치(대부분 QSPI에서 XIP, 일부는 SRAM1 복사본), 주황 = arena(DTCM), 초록 = scratch·DMA 버퍼(다른 bank인 SRAM2). 점선 상자는 "이미지에만 있고 실행 중엔 복사본을 쓰는" 로드 원본이다.

### 4.2 LMA와 VMA — "어디에 구워지나" vs "어디서 실행되나"

Don은 이미 아는 개념이지만, 모델 배치에서는 이게 핵심이니 정리한다.

- **VMA**(Virtual/Run Memory Address): 실행 중 코드가 그 심볼을 **접근하는 주소**. `> SRAM1`.
- **LMA**(Load Memory Address): 이미지 안에서 그 바이트가 **저장되는 주소**. `AT> FLASH`.
- 둘이 같으면(`.text > FLASH`) 복사가 필요 없다. 다르면 startup 코드가 LMA → VMA로 복사해야 한다. 링커는 복사해 주지 않는다 — **심볼만 준다**(`LOADADDR(.data)`, `_sdata`, `_edata`).

```svg
<svg viewBox="0 0 640 300" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="f7a3" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs> <text x="120" y="20" font-size="13" text-anchor="middle">로드 이미지 (LMA) — 전원 꺼져도 남음</text> <text x="500" y="20" font-size="13" text-anchor="middle">실행 중 (VMA) — main() 진입 시점</text> <rect x="40" y="35" width="160" height="40" fill="#888" fill-opacity="0.25" stroke="#888"/><text x="120" y="59" font-size="12" text-anchor="middle">.text · .rodata</text>
<rect x="40" y="75" width="160" height="30" fill="none" stroke="#888"/><text x="120" y="95" font-size="12" text-anchor="middle">_sidata: .data 원본</text> <rect x="40" y="105" width="160" height="30" fill="none" stroke="#888"/><text x="120" y="125" font-size="12" text-anchor="middle">__itcm_load</text> <text x="20" y="80" font-size="12" text-anchor="middle" transform="rotate(-90 20 80)">FLASH</text> <rect x="40" y="160" width="160" height="40" fill="#4a7bd0" fill-opacity="0.2" stroke="#4a7bd0"/><text x="120" y="184" font-size="12" text-anchor="middle">__model_hot_load</text>
<rect x="40" y="200" width="160" height="50" fill="#4a7bd0" fill-opacity="0.45" stroke="#4a7bd0"/><text x="120" y="229" font-size="12" text-anchor="middle">.model_xip (복사 안 함)</text> <text x="20" y="205" font-size="12" text-anchor="middle" transform="rotate(-90 20 205)">QSPI</text> <rect x="420" y="35" width="160" height="30" fill="#888" fill-opacity="0.25" stroke="#888"/><text x="500" y="55" font-size="12" text-anchor="middle">ITCM: .itcm_text</text>
<rect x="420" y="75" width="160" height="30" fill="#888" fill-opacity="0.25" stroke="#888"/><text x="500" y="95" font-size="12" text-anchor="middle">SRAM1: .data</text> <rect x="420" y="105" width="160" height="30" fill="none" stroke="#888"/><text x="500" y="125" font-size="12" text-anchor="middle">SRAM1: .bss = 0</text> <rect x="420" y="135" width="160" height="40" fill="#4a7bd0" fill-opacity="0.45" stroke="#4a7bd0"/><text x="500" y="159" font-size="12" text-anchor="middle">SRAM1: .model_hot</text>
<rect x="420" y="185" width="160" height="40" fill="#e08a3c" fill-opacity="0.45" stroke="#e08a3c"/><text x="500" y="204" font-size="12" text-anchor="middle">DTCM: arena</text><text x="500" y="219" font-size="12" text-anchor="middle">NOLOAD — 쓰레기값</text> <rect x="420" y="235" width="160" height="30" fill="#3f9a6b" fill-opacity="0.4" stroke="#3f9a6b"/><text x="500" y="255" font-size="12" text-anchor="middle">SRAM2: scratch NOLOAD</text>
<line x1="200" y1="90" x2="418" y2="90" stroke="currentColor" stroke-width="1.5" marker-end="url(#f7a3)"/><text x="310" y="84" font-size="12" text-anchor="middle">① copy</text> <line x1="200" y1="120" x2="418" y2="52" stroke="currentColor" stroke-width="1.5" marker-end="url(#f7a3)"/><text x="300" y="72" font-size="12" text-anchor="middle">③ copy</text> <line x1="200" y1="180" x2="418" y2="157" stroke="#4a7bd0" stroke-width="1.5" marker-end="url(#f7a3)"/><text x="310" y="160" font-size="12" text-anchor="middle">⑤ copy (QSPI 켠 뒤)</text>
<text x="310" y="128" font-size="12" text-anchor="middle">② zero (원본 없음)</text> <line x1="380" y1="120" x2="418" y2="120" stroke="currentColor" stroke-width="1.2" stroke-dasharray="3 3" marker-end="url(#f7a3)"/> <line x1="200" y1="225" x2="418" y2="210" stroke="#4a7bd0" stroke-width="1.2" stroke-dasharray="2 4"/><text x="310" y="235" font-size="12" text-anchor="middle">XIP: CPU가 QSPI 주소로 직접</text> <text x="20" y="290" font-size="12">번호 = 4.5절 Reset_Handler의 순서. 링커는 주소 심볼만 주고, 복사는 startup 코드가 한다</text>
</svg>
```

그림 4 — LMA/VMA copy-down. 왼쪽은 flash·QSPI에 구워진 이미지, 오른쪽은 `main()` 진입 시점의 RAM 상태. `AT>`가 붙은 섹션만 복사 대상이고, `.bss`는 원본 없이 0으로 채우며, NOLOAD 섹션은 아무도 건드리지 않는다.

### 4.3 완전한 예시 링커 스크립트 (GNU ld 문법 — 이 환경에서 링크 검증 못 함)

> 이 스크립트는 GNU ld 매뉴얼의 문법에 맞춰 신중히 썼지만, 이 Mac에는 ARM 링커(`arm-none-eabi-ld`, `ld.lld`)가 없어서 **실제 링크로 검증하지 않았다**. 주소·크기는 STM32H7 계열과 비슷한 **가상의 예**다. 실제 칩에서는 벤더 BSP의 `.ld`를 출발점으로 이 절의 ML 관련 블록만 추가하는 것이 현실적이다.

```text
/* kws_m7.ld — 예시: Cortex-M7급 MCU + 외부 QSPI NOR (memory-mapped/XIP) */
ENTRY(Reset_Handler)

__stack_size = 8K;                         /* 스택 크기 — stack painting으로 검증 (D2 6.3절) */
__arena_min  = 96K;                        /* 모델이 요구하는 arena 크기 (F2: arena_used_bytes()로 측정) */

MEMORY
{
  ITCM  (rx)  : ORIGIN = 0x00000000, LENGTH = 64K
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K
  DTCM  (rw)  : ORIGIN = 0x20000000, LENGTH = 128K
  SRAM1 (rwx) : ORIGIN = 0x24000000, LENGTH = 512K
  SRAM2 (rw)  : ORIGIN = 0x30000000, LENGTH = 256K
  QSPI  (rx)  : ORIGIN = 0x90000000, LENGTH = 16M
}

SECTIONS
{
  /* ---------- 내부 FLASH: 벡터 테이블, 코드, 상수 ---------- */
  .isr_vector : ALIGN(4)
  {
    KEEP(*(.isr_vector))                   /* 아무도 참조 안 해도 gc에서 지킨다 */
  } > FLASH

  .text : ALIGN(4)
  {
    *(.text .text.*)
    *(.rodata .rodata.*)                   /* LUT, 작은 상수 */
    KEEP(*(.init)) KEEP(*(.fini))
    . = ALIGN(4);
  } > FLASH

  /* ---------- hot 커널: FLASH에 구워 ITCM에서 실행 ---------- */
  .itcm_text : ALIGN(4)
  {
    __itcm_start = .;
    *(.itcm_text .itcm_text.*)             /* __attribute__((section(".itcm_text"))) 붙인 함수 */
    . = ALIGN(4);
    __itcm_end = .;
  } > ITCM AT> FLASH
  __itcm_load = LOADADDR(.itcm_text);

  /* ---------- .data: FLASH에 원본, SRAM1에서 실행 ---------- */
  .data : ALIGN(4)
  {
    _sdata = .;
    *(.data .data.*)
    . = ALIGN(4);
    _edata = .;
  } > SRAM1 AT> FLASH
  _sidata = LOADADDR(.data);

  /* ---------- 모델 ①: 큰 가중치 — QSPI에 두고 XIP로 읽는다 ---------- */
  .model_xip : ALIGN(16)
  {
    __model_xip_start = .;
    KEEP(*(.model_weights))                /* 런타임이 심볼로만 찾아도 지워지지 않게 */
    KEEP(*(.model_weights.*))
    . = ALIGN(16);
    __model_xip_end = .;
  } > QSPI

  /* ---------- 모델 ②: hot 가중치 — QSPI에 구워 SRAM1로 복사 ---------- */
  .model_hot : ALIGN(16)
  {
    __model_hot_start = .;
    KEEP(*(.model_hot .model_hot.*))
    . = ALIGN(16);
    __model_hot_end = .;
  } > SRAM1 AT> QSPI
  __model_hot_load = LOADADDR(.model_hot);

  /* ---------- RAM: 0 초기화 ---------- */
  .bss (NOLOAD) : ALIGN(4)
  {
    _sbss = .;
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } > SRAM1

  /* ---------- tensor arena: DTCM, 초기화 안 함 ---------- */
  .arena_dtcm (NOLOAD) : ALIGN(16)
  {
    __arena_start = .;
    KEEP(*(.tensor_arena))                 /* alignas(16) uint8_t arena[] __attribute__((section(".tensor_arena"))) */
    . = ALIGN(16);
    __arena_end = .;
  } > DTCM

  .stack (NOLOAD) : ALIGN(8)
  {
    . = . + __stack_size;
    __stack_top = .;                       /* 벡터 테이블 0번 엔트리(MSP 초기값)로 */
  } > DTCM

  /* ---------- scratch · DMA 버퍼: SRAM2, 초기화 안 함 ---------- */
  .scratch_sram2 (NOLOAD) : ALIGN(32)      /* 32 B = Cortex-M7 D-cache 라인 */
  {
    *(.scratch_sram2 .scratch_sram2.*)
  } > SRAM2

  /* ---------- 빌드 타임 검사: 어기면 링크 실패 ---------- */
  ASSERT(__arena_end - __arena_start >= __arena_min, "tensor arena smaller than model needs")
  ASSERT((__model_xip_start % 16) == 0, "model blob is not 16-byte aligned")
}
```

블록별로 Don이 이미 아는 것과 새로운 것:

- **`MEMORY`**: 늘 쓰던 그것. 새로운 것은 `QSPI` 같은 **외부 메모리 영역**이 들어간다는 점뿐. 단, QSPI는 **부팅 직후엔 안 보인다** — QSPI 컨트롤러를 memory-mapped 모드로 설정하기 전에는 그 주소를 읽으면 bus fault다. 그래서 복사·XIP 접근은 반드시 BSP의 QSPI 초기화 뒤에(4.5절).
- **`KEEP`**: `--gc-sections`는 "아무도 참조하지 않는 입력 섹션"을 지운다. 모델 blob을 `__model_xip_start` 같은 **링커 심볼로만** 찾는 구조이면, 코드가 `g_model`을 직접 참조하지 않으니 통째로 지워질 수 있다. 벡터 테이블과 같은 이유로 `KEEP`.
- **`AT>`**: `> SRAM1 AT> QSPI` 는 "VMA는 SRAM1, LMA는 QSPI". `LOADADDR(.model_hot)`이 LMA를 돌려준다.
- **`(NOLOAD)`**: 이미지에 바이트를 넣지 않고 startup이 초기화하지도 않는다. **C 표준이 보장하는 "static 변수는 0"이 깨진다**는 뜻이다 — arena·scratch처럼 매번 덮어쓰는 버퍼에만 쓴다. TFLM arena는 0 초기화를 요구하지 않는다(런타임이 텐서를 쓰기 전에 채움).
- **`ALIGN(16)` + 섹션 끝의 `. = ALIGN(16)`**: 시작 정렬은 모델 정렬 요구(3.5절), 끝 정렬은 다음 섹션·복사 루프를 워드 단위로 맞추기 위함.
- **`ASSERT`**: 펌웨어 엔지니어가 가장 좋아할 기능. "arena가 모델 요구보다 작다"를 **런타임 `AllocateTensors()` 실패가 아니라 빌드 실패**로 만든다. `__arena_min`은 호스트에서 측정한 `arena_used_bytes()`에 여유를 더한 값으로 CI가 생성하면 된다.
- 입력 섹션 매칭 순서: GNU ld는 스크립트에 **먼저 나온** 패턴이 이긴다. `.bss.*`가 `.bss` 출력 섹션에 잡히므로, arena를 `.bss.arena` 이름으로 두고 나중의 `.arena_dtcm`에서 잡으려 하면 이미 `.bss`에 들어가 버린다. 그래서 arena 섹션 이름은 `.tensor_arena`처럼 `.bss.` 접두어를 피했다(대신 PROGBITS가 되지만 출력 섹션이 NOLOAD라 이미지에 안 들어감 — 2.4절).

### 4.4 C 쪽에서 섹션 지정하기

```c
#include <stdint.h>
#define SECTION(s) __attribute__((section(s)))

/* 모델: QSPI XIP (생성 파일에서) */
SECTION(".model_weights") __attribute__((aligned(16))) const uint8_t g_kws_model[] = { 0x28, 0x00 /* ... */ };
/* arena: DTCM, NOLOAD */
SECTION(".tensor_arena") __attribute__((aligned(16))) uint8_t g_arena[96 * 1024];
/* im2col scratch + 마이크 DMA: SRAM2, 캐시 라인 정렬 */
SECTION(".scratch_sram2") __attribute__((aligned(32))) int8_t  g_im2col[16 * 1024];
SECTION(".scratch_sram2") __attribute__((aligned(32))) int16_t g_mic_dma[2][512];
/* hot 커널: ITCM */
SECTION(".itcm_text") void conv_inner_s8(const int8_t *in, const int8_t *w, int32_t *acc, int n);
```

이 조각을 Cortex-M4용으로 `-Wall -Wextra -c` 컴파일(경고 0)하고 2.3절 스크립트로 섹션을 보면:

```text
section          type      flags   size align bytes in .o
.model_weights   PROGBITS  A          2    16           2
.tensor_arena    PROGBITS  AW     98304    16       98304
.scratch_sram2   PROGBITS  AW     18432    32       18432
```

96 KB arena와 18 KB scratch가 **.o 안에 0으로 실제 저장**되어 있다(2.4절 — 커스텀 이름 섹션은 PROGBITS). 4.3절 스크립트에서 두 출력 섹션에 `(NOLOAD)`를 붙인 이유가 이것이다. 붙이지 않으면 flash 이미지가 114 KB 커진다. 정렬은 각각 16, 32로 기록되어 링커가 지킨다.

C++ 런타임(TFLM)은 arena 포인터만 받으므로(`tflite::MicroInterpreter interpreter(model, resolver, g_arena, sizeof(g_arena));`), **arena 위치는 순수하게 링커 스크립트가 정한다**. TFLM 소스를 고칠 필요가 없다. 가중치도 `tflite::GetModel(g_kws_model)`에 포인터만 넘긴다 — 그 포인터가 QSPI 주소든 SRAM 주소든 TFLM은 모른다. 이 "주소 투명성" 덕분에 배치를 링커 스크립트에서만 바꿔 가며 latency를 비교할 수 있다(F2 참고).

Arm Compiler(`armlink`)는 같은 개념을 **scatter file**로 쓴다: load region(LMA) 안에 execution region(VMA)을 나열하고, `UNINIT`이 NOLOAD에 해당하며, copy-down·zero-init 표는 링커가 만들어 C 라이브러리의 `__main`이 처리한다(GNU 쪽은 startup 코드가 직접 한다).

### 4.5 startup 코드 — 링커 심볼을 쓰는 쪽 (예제 8, 컴파일만)

무엇을 확인하는 코드인지: 4.3절 스크립트의 심볼로 copy-down·zero-init을 하는 Reset_Handler가 Cortex-M4 오브젝트로 컴파일되는지, 그리고 **어떤 외부 심볼을 요구하는지** 본다.

```c
#include <stdint.h>
/* 링커 스크립트가 정의한 심볼 — 값이 아니라 "주소"가 의미다 */
extern uint32_t _sidata[], _sdata[], _edata[], _sbss[], _ebss[];
extern uint32_t __model_hot_load[], __model_hot_start[], __model_hot_end[];
extern uint32_t __itcm_load[], __itcm_start[], __itcm_end[];
extern void qspi_enable_memory_mapped(void);   /* BSP: QSPI를 XIP 모드로 */
extern int main(void);

static void copy32(uint32_t *dst, uint32_t *end, const uint32_t *src) { while (dst < end) *dst++ = *src++; }
static void zero32(uint32_t *dst, uint32_t *end) { while (dst < end) *dst++ = 0; }

__attribute__((noreturn)) void Reset_Handler(void) {
    /* (클럭·FPU(CPACR)·MPU·캐시 설정은 SystemInit에서 — 생략) */
    copy32(_sdata, _edata, _sidata);                      /* 1. .data       FLASH → SRAM1 */
    zero32(_sbss, _ebss);                                 /* 2. .bss        0으로 */
    copy32(__itcm_start, __itcm_end, __itcm_load);        /* 3. hot 커널     FLASH → ITCM */
    qspi_enable_memory_mapped();                          /* 4. QSPI가 보여야 아래 복사·XIP 가능 */
    copy32(__model_hot_start, __model_hot_end, __model_hot_load); /* 5. hot 가중치 QSPI → SRAM1 */
    /* .arena_dtcm, .scratch_sram2는 NOLOAD — 손대지 않는다 */
    main();
    for (;;) { }
}
```

```sh
cc --target=thumbv7em-none-eabihf -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -O2 -std=c11 -Wall -Wextra -c startup.c -o startup.o
xcrun llvm-nm -u startup.o | awk '{print $2}' | paste -sd' ' -
```

```text
__aeabi_memclr4 __aeabi_memcpy4 __itcm_end __itcm_load __itcm_start __model_hot_end __model_hot_load __model_hot_start _ebss _edata _sbss _sdata _sidata main qspi_enable_memory_mapped
```

출력에서 볼 것:

- 링커 스크립트가 제공할 심볼 11개가 undefined(`U`)로 나온다 — 링크 때 스크립트의 `_sdata = .;` 등이 채운다. 이 목록과 스크립트의 심볼이 하나라도 다르면 "undefined reference"로 바로 드러난다.
- **`__aeabi_memcpy4` / `__aeabi_memclr4`** 가 생겼다! `-O2`의 loop idiom 인식이 손으로 쓴 복사 루프를 `memcpy`/`memset` 호출로 바꿨다. 보통은 괜찮지만, (1) libc가 없는 빌드면 링크 실패, (2) `memcpy`를 ITCM에 올려 두었다면 ITCM 복사 **전에** ITCM의 memcpy를 부르는 셈이 된다. 같은 파일을 `-ffreestanding` 또는 `-fno-builtin`으로 다시 컴파일하자 `aeabi_mem` 참조가 **0개**가 됐다(실측). startup 파일은 freestanding으로 빌드하는 것이 안전하다(GCC는 `-fno-tree-loop-distribute-patterns`가 같은 역할이라고 알려져 있다 — 확인 필요).
- 순서 4 → 5가 중요하다. QSPI를 켜기 전에 `.model_hot`을 복사하면 bus fault.

---

## 5. 부팅 때 일어나는 일 — copy-down 비용을 숫자로 (예제 9)

ARM 링커가 없으니, **링커 심볼을 흉내 낸 포인터**와 가짜 메모리 배열로 호스트에서 같은 startup 로직을 돌린다. 진짜 Reset_Handler와 같은 루프이고, 메모리만 배열이다.

무엇을 확인하는 코드인지: (1) copy / zero / NOLOAD가 의도대로 동작하는지, (2) 각 섹션의 부팅 비용을 "바이트 ÷ 대역폭"으로 계산한다.

```c
#include <stdint.h>
#include <stdio.h>
#include <string.h>
/* 가짜 메모리 영역 4개 — 실제 칩에서는 링커 스크립트의 MEMORY 블록 */
static uint32_t FLASH[16 * 1024], QSPI[128 * 1024], SRAM[64 * 1024], DTCM[32 * 1024];
#define KB(x) ((x) * 1024u / 4u)            /* 워드 개수 */
/* ---- 링커가 정의해 줄 심볼을 흉내 (실제로는 extern uint32_t _sdata[]; 등) ---- */
uint32_t *_sidata = FLASH,        *_sdata = SRAM,            *_edata = SRAM + KB(2);
uint32_t *_siweights = QSPI,      *_sweights = SRAM + KB(4), *_eweights = SRAM + KB(4 + 150);
uint32_t *_sbss = SRAM + KB(160), *_ebss = SRAM + KB(184);
uint32_t *_sarena = DTCM,         *_earena = DTCM + KB(96);
uint32_t *_sscratch = SRAM + KB(200);                         /* NOLOAD: 아무도 건드리지 않음 */

static void copy_words(uint32_t *dst, uint32_t *end, const uint32_t *src) { while (dst < end) *dst++ = *src++; }
static void zero_words(uint32_t *dst, uint32_t *end) { while (dst < end) *dst++ = 0; }

void Reset_Handler_sim(void) {                  /* crt0가 main 전에 하는 일 그대로 */
    copy_words(_sdata, _edata, _sidata);        /* .data: flash(LMA) → SRAM(VMA) */
    copy_words(_sweights, _eweights, _siweights);/* .model_weights_ram: QSPI → SRAM */
    zero_words(_sbss, _ebss);                   /* .bss */
    zero_words(_sarena, _earena);               /* .arena (DTCM) — 선택: 안 지워도 TFLM은 동작 */
}
int main(void) {
    for (unsigned i = 0; i < sizeof QSPI / 4; i++) QSPI[i] = i * 2654435761u;  /* 모델 바이트 */
    memset(FLASH, 0x5A, sizeof FLASH);
    memset(SRAM, 0xEE, sizeof SRAM); memset(DTCM, 0xEE, sizeof DTCM);         /* 리셋 직후 쓰레기 */
    Reset_Handler_sim();
    printf("weights copied ok=%d  data[0]=%08X  bss[0]=%08X  arena[0]=%08X  scratch[0]=%08X\n",
           memcmp(_sweights, _siweights, (size_t)(_eweights - _sweights) * 4) == 0,
           _sdata[0], _sbss[0], _sarena[0], _sscratch[0]);
    struct { const char *n; long bytes; double MBps; } c[] = {        /* 부팅 비용 = bytes / 대역폭 */
        {".data      (int flash ~200 MB/s)", (_edata - _sdata) * 4L, 200},
        {".weights   (QSPI 4bit@80MHz=40 MB/s)", (_eweights - _sweights) * 4L, 40},
        {".bss       (4 B/clk @100MHz)", (_ebss - _sbss) * 4L, 400},
        {".arena     (4 B/clk @100MHz)", (_earena - _sarena) * 4L, 400}};
    double tot = 0;
    for (int i = 0; i < 4; i++) { double us = c[i].bytes / c[i].MBps; tot += us;
        printf("%-38s %7ld B %8.1f us\n", c[i].n, c[i].bytes, us); }
    printf("total %.2f ms  (NOLOAD scratch 48 KB = 0 us)\n", tot / 1000);
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 boot_sim.c -o boot_sim && ./boot_sim
```

```text
weights copied ok=1  data[0]=5A5A5A5A  bss[0]=00000000  arena[0]=00000000  scratch[0]=EEEEEEEE
.data      (int flash ~200 MB/s)          2048 B     10.2 us
.weights   (QSPI 4bit@80MHz=40 MB/s)    153600 B   3840.0 us
.bss       (4 B/clk @100MHz)             24576 B     61.4 us
.arena     (4 B/clk @100MHz)             98304 B    245.8 us
total 4.16 ms  (NOLOAD scratch 48 KB = 0 us)
```

출력에서 볼 것:

- `scratch[0]=EEEEEEEE`: NOLOAD 영역은 리셋 직후 쓰레기값 그대로다. 그 영역을 "0일 것"이라 가정하는 코드가 있으면 버그 — 전원 사이클마다 다르게 터지는 종류다.
- 비용 계산은 **1 MB/s = 1 B/µs** 이므로 `bytes / MBps = µs`. 손으로: 150 KB(153,600 B) ÷ 40 B/µs = 3,840 µs ≈ **3.8 ms**. 대역폭 가정은 표기한 대로 근사치다 — QSPI 80 MHz × 4 bit = 320 Mbit/s = 40 MB/s는 명령·주소 오버헤드를 뺀 이상값이고, DDR 모드·OSPI(8 bit)면 2~4배 빨라진다. 내부 flash 200 MB/s도 wait-state·가속기 설정에 따라 다르다.
- 전체 부팅 지연의 **92%** 가 모델 복사(3.84 / 4.16 ms)다. wake word 기기에서 "전원 버튼 → 첫 추론" 시간이 중요하면, 복사할 hot 가중치를 줄이거나, DMA로 복사를 백그라운드로 돌리면서 다른 초기화를 하거나, 처음부터 XIP로 시작하는 선택을 한다.
- arena zero-init 246 µs는 **안 해도 되는 일**이다(TFLM은 요구하지 않음). NOLOAD로 두면 0이다.

```svg
<svg viewBox="0 0 640 200" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="20" font-size="13">부팅 init 비용 (예제 9 실측 계산, 총 4.16 ms)</text> <line x1="140" y1="35" x2="140" y2="165" stroke="currentColor"/> <text x="135" y="54" font-size="12" text-anchor="end">.data 2 KB</text><rect x="140" y="42" width="1.2" height="18" fill="#888"/><text x="148" y="56" font-size="12">10 µs</text> <text x="135" y="84" font-size="12" text-anchor="end">hot 가중치 150 KB</text><rect x="140" y="72" width="460.8" height="18" fill="#4a7bd0"/><text x="595" y="86" font-size="12" text-anchor="end">3840 µs</text>
<text x="135" y="114" font-size="12" text-anchor="end">.bss 24 KB</text><rect x="140" y="102" width="7.4" height="18" fill="#888"/><text x="154" y="116" font-size="12">61 µs</text> <text x="135" y="144" font-size="12" text-anchor="end">arena 96 KB (선택)</text><rect x="140" y="132" width="29.5" height="18" fill="#e08a3c"/><text x="176" y="146" font-size="12">246 µs</text> <line x1="140" y1="165" x2="620" y2="165" stroke="currentColor"/>
<text x="140" y="180" font-size="12" text-anchor="middle">0</text><text x="260" y="180" font-size="12" text-anchor="middle">1000</text><text x="380" y="180" font-size="12" text-anchor="middle">2000</text><text x="500" y="180" font-size="12" text-anchor="middle">3000</text><text x="620" y="180" font-size="12" text-anchor="middle">4000 µs</text> <line x1="260" y1="165" x2="260" y2="169" stroke="currentColor"/><line x1="380" y1="165" x2="380" y2="169" stroke="currentColor"/><line x1="500" y1="165" x2="500" y2="169" stroke="currentColor"/>
<text x="10" y="196" font-size="12">QSPI 40 MB/s · 내부 flash 200 MB/s · RAM 쓰기 400 MB/s 가정 — 칩마다 다르다</text>
</svg>
```

그림 5 — 예제 9의 부팅 비용. 모델 복사가 압도적이다. "가중치를 SRAM으로 복사할까"는 추론 latency 이득(D2 1.3절)과 이 부팅 비용·SRAM 사용량을 같이 놓고 정한다.

---

## 6. CMake로 크로스 빌드

### 6.1 toolchain file — 크로스 빌드의 "보드 정의"

CMake는 처음 configure할 때 컴파일러가 동작하는지 **작은 프로그램을 빌드·링크**해 본다. bare-metal에서는 startup·링커 스크립트 없이 링크가 실패하므로, 이 검사를 정적 라이브러리로 바꾸는 것이 핵심 한 줄이다(`CMAKE_TRY_COMPILE_TARGET_TYPE`).

```text
# Cortex-M4F 크로스 빌드 toolchain file (Apple clang / LLVM clang 공용)
set(CMAKE_SYSTEM_NAME      Generic)        # OS 없음(bare-metal)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER   clang)
set(CMAKE_ASM_COMPILER clang)
set(CMAKE_C_COMPILER_TARGET   thumbv7em-none-eabihf)   # → --target=...
set(CMAKE_ASM_COMPILER_TARGET thumbv7em-none-eabihf)

set(MCU_FLAGS "-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb")
set(CMAKE_C_FLAGS_INIT   "${MCU_FLAGS} -ffunction-sections -fdata-sections")
set(CMAKE_ASM_FLAGS_INIT "${MCU_FLAGS}")

# 링커가 없어도/startup 코드가 없어도 configure가 통과하도록:
# 컴파일러 확인용 test 프로그램을 실행 파일이 아니라 static lib로 만든다.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# 호스트 라이브러리·헤더를 찾지 않도록 (sysroot가 있으면 CMAKE_SYSROOT도 지정)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# [이 Mac 한정 우회] Apple ar/ranlib은 ELF 멤버를 버린다(본문 6.3절).
# 실제 프로젝트에서는 CMAKE_AR을 llvm-ar 또는 arm-none-eabi-ar로 지정하면 된다.
set(CMAKE_C_ARCHIVE_CREATE "<CMAKE_AR> qcS <TARGET> <OBJECTS>")   # S = 심볼 인덱스 생략
set(CMAKE_C_ARCHIVE_APPEND "<CMAKE_AR> qS <TARGET> <OBJECTS>")
set(CMAKE_C_ARCHIVE_FINISH "")                                     # ranlib 호출 안 함
```

GCC 툴체인이면 컴파일러 줄이 `set(CMAKE_C_COMPILER arm-none-eabi-gcc)` 로 바뀌고 `_COMPILER_TARGET`이 필요 없다(GCC는 triple이 실행 파일 이름에 박혀 있다). 링크 단계에는 `CMAKE_EXE_LINKER_FLAGS_INIT`에 `-T${CMAKE_SOURCE_DIR}/kws_m7.ld -Wl,--gc-sections -Wl,-Map=fw.map --specs=nano.specs` 같은 것을 넣는다.

### 6.2 실제 빌드 — Cortex-M4용 `libkws.a` (예제 10)

무엇을 확인하는 코드인지: 위 toolchain file로 커널 C 파일 + `.incbin` 모델 어셈블리를 Cortex-M4 정적 라이브러리로 실제 configure·빌드한다.

```text
cmake_minimum_required(VERSION 3.20)
project(kws_model C ASM)

add_library(kws STATIC
  src/kernels.c            # int8 커널
  src/model_incbin.S)      # .incbin으로 모델 넣기

# 모델이 바뀌면 .S도 다시 어셈블되도록 의존성 명시
set_source_files_properties(src/model_incbin.S PROPERTIES
  OBJECT_DEPENDS ${CMAKE_SOURCE_DIR}/src/model.tflite)
target_include_directories(kws PRIVATE src)
target_compile_options(kws PRIVATE $<$<COMPILE_LANGUAGE:C>:-O2 -std=c11 -Wall -Wextra>)
```

`src/kernels.c`에는 `dot_s8`, `relu_s32`, `unused_kernel` 세 함수가 있다(7.3절에서 gc 대상으로 다시 쓴다).

```sh
.venv/bin/cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cortex_m4_clang.cmake
.venv/bin/cmake --build build
ar t build/libkws.a
xcrun llvm-size -A build/libkws.a
grep -h "C_FLAGS\|ASM_FLAGS" build/CMakeFiles/kws.dir/flags.make
```

```text
-- The C compiler identification is Clang 21.0.0
-- The ASM compiler identification is AppleClang
-- Check for working C compiler: /usr/bin/clang - skipped
-- Build files have been written to: /private/tmp/claude-501/f7/proj/build
[ 33%] Building C object CMakeFiles/kws.dir/src/kernels.c.obj
[ 66%] Building ASM object CMakeFiles/kws.dir/src/model_incbin.S.obj
[100%] Linking C static library libkws.a
[100%] Built target kws
kernels.c.obj
model_incbin.S.obj
kernels.c.obj   (ex build/libkws.a):
section                           size   addr
.text                                0      0
.text.dot_s8                       180      0
.text.relu_s32                       6      0
.text.unused_kernel                138      0
model_incbin.S.obj   (ex build/libkws.a):
section             size   addr
.text                  0      0
.model_weights      2708      0
ASM_FLAGS = -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb
C_FLAGS = -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -ffunction-sections -fdata-sections -O2 -std=c11 -Wall -Wextra
```

(`llvm-size -A` 출력에서 `.ARM.attributes`, `.comment` 등 메타데이터 줄은 생략했다.)

출력에서 볼 것:

- `Check for working C compiler: ... - skipped` — `CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY` 덕분에 링크 검사를 건너뛰었다. 이 줄이 없으면 bare-metal에서 configure가 "The C compiler is not able to compile a simple test program"으로 멈추는 일이 흔하다.
- 아카이브 멤버 둘 다 ARM ELF(`llvm-objdump -f`로 `elf32-littlearm` 확인). `-ffunction-sections` 덕분에 커널마다 `.text.<이름>` 섹션이 따로 있다 — 링커가 함수 단위로 버릴 수 있는 상태.
- `dot_s8`이 `-O2`에서 180 B다(8.2절의 `-Os` 빌드에서는 34 B). 커널 라이브러리의 최적화 레벨은 크기에 큰 영향을 준다 — 7.2절.

### 6.3 실측된 함정 — Apple `ar`/`ranlib`이 ELF를 조용히 버린다

위 toolchain file의 마지막 세 줄(우회) 없이 처음 빌드했을 때 실제로 일어난 일:

```text
[100%] Linking C static library libkws.a
ranlib: warning: archive member 'kernels.c.obj' not a mach-o file
[100%] Built target kws
```

```sh
ls -l build/libkws.a; ar -tv build/libkws.a
```

```text
-rw-r--r--@ 1 donh  wheel  96 Sep 30 18:37 build/libkws.a
rw-r--r--     501/20            8 Sep 30 18:37 2026 __.SYMDEF SORTED
```

빌드는 "Built target"으로 성공했는데 라이브러리는 **96 바이트, 멤버 0개**다. macOS의 cctools `ar`는 `q`로 만들 때 심볼 표를 만들려고 ranlib 로직을 돌리고, Mach-O가 아닌 멤버를 **경고 한 줄 남기고 버린다**. `S`(심볼 표 생략) 플래그를 주면 멤버가 남는다(위 우회). 다만 심볼 인덱스가 없는 아카이브는 GNU ld·lld가 "archive has no index; run ranlib" 류로 거부할 수 있으므로, **실제 크로스 빌드에서는 반드시 타깃용 `ar`**(`arm-none-eabi-ar`, `llvm-ar`)를 `CMAKE_AR`로 지정한다.

교훈 (Don에게 익숙한 종류): **"빌드 성공"은 산출물 검증이 아니다.** CI에 `ar t`/`size`로 멤버·크기를 확인하는 한 줄을 넣는다. 이 버그는 링크 단계에서 "undefined reference to dot_s8"로 나타나서, 원인이 아카이브 생성 단계라는 걸 찾는 데 시간이 걸리는 유형이다.

### 6.4 TFLM 런타임 자체를 Cortex-M용으로 빌드 (명령만, 실행 안 함)

TFLM은 자체 Makefile 빌드가 기본이다(`.tools/tflite-micro`에서 타깃 이름·옵션을 확인함).

```sh
# 실행 안 함 — arm-none-eabi-gcc를 Makefile이 내려받는다
make -f tensorflow/lite/micro/tools/make/Makefile \
     TARGET=cortex_m_generic TARGET_ARCH=cortex-m4+fp \
     OPTIMIZED_KERNEL_DIR=cmsis_nn microlite
# 결과: gen/.../lib/libtensorflow-microlite.a
```

확인한 사실: `cortex_m_generic_makefile.inc`가 `TARGET_ARCH`로 `cortex-m0`~`cortex-m85`(`+fp`, `+nodsp` 변형 포함)를 받고, `TOOLCHAIN=gcc`(기본, 접두어 `arm-none-eabi-`)와 `armclang`을 지원한다. 공통 플래그에 `-ffunction-sections -fdata-sections -fno-rtti -fno-exceptions -fno-threadsafe-statics`와 `-DTF_LITE_STATIC_MEMORY`가 들어 있고, 예제 링크에는 `-Wl,--gc-sections`를 쓴다. 즉 TFLM 자체가 **"op 수백 개를 다 컴파일해 두고 링커가 안 쓰는 것을 버리는"** 구조다(7.3절). 제품에서는 이 `.a`를 받아 위 CMake 프로젝트에 `target_link_libraries`로 붙이거나, TFLM의 project generation으로 소스를 뽑아 CMake에 직접 넣는다.

### 6.5 Android NDK로 런타임 크로스 빌드 (명령만, 실행 안 함 — NDK 미설치)

Hark 같은 기기의 큰 SoC 쪽(Qualcomm, Android 또는 임베디드 Linux 추정)에서 llama.cpp·ONNX Runtime·TFLite를 빌드할 때의 표준형:

```sh
# 실행 안 함 — NDK가 이 Mac에 없다
export ANDROID_NDK=$HOME/Library/Android/sdk/ndk/<버전>
cmake -S . -B build-android \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-30 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-android -j
adb push build-android/bin/my_bench /data/local/tmp/
adb shell /data/local/tmp/my_bench
```

- `android.toolchain.cmake`는 NDK에 들어 있는 toolchain file이다. 6.1절에서 직접 쓴 것과 같은 일(컴파일러 경로, triple `aarch64-linux-android30`, sysroot, 기본 플래그)을 해 준다.
- `ANDROID_ABI`: `arm64-v8a`(64-bit, 현재 주류), `armeabi-v7a`(32-bit), `x86_64`(에뮬레이터). `ANDROID_PLATFORM`은 최소 API 레벨 = 1.6절의 `__ANDROID_MIN_SDK_VERSION__`.
- 특정 CPU 기능(dotprod, i8mm)을 쓰려면 `-march=armv8.2-a+dotprod` 등을 추가하되, **기기가 지원하는지 런타임에 확인**해야 한다(E2 4.6절). llama.cpp 같은 프로젝트는 이를 CMake 옵션으로 노출한다.
- C++ 표준 라이브러리: `ANDROID_STL=c++_shared`(앱에 `libc++_shared.so`를 같이 넣음) vs `c++_static`. 여러 `.so`가 각자 static으로 넣으면 문제가 생길 수 있어 공식 문서가 주의를 준다.
- 최근 Android는 **16 KB 페이지 크기** 기기를 지원하기 시작해서, 네이티브 `.so`를 16 KB 정렬로 링크하라는 요구(`-Wl,-z,max-page-size=16384`, 최신 NDK는 기본값으로 처리하는 것으로 알려져 있음)가 생겼다 — 배포 전 NDK 버전과 Play 정책 확인 필요.
- QNN·SNPE 같은 벤더 SDK는 자체 `.so`와 헤더를 주고, 앱은 위 방식으로 빌드한 뒤 링크한다(F4, F8).

**Bazel**: TensorFlow/TFLite, MediaPipe, XNNPACK은 Bazel이 1차 빌드 시스템이다. 크로스 빌드는 "toolchain + platform" 개념과 `--config=` 묶음으로 한다(예: TFLite Android 빌드용 `--config=android_arm64` — 버전마다 다르니 문서 확인). 실무에서는 Bazel로 `.so`/`.a`를 뽑아 제품 빌드(CMake/Make/Yocto)에 넣는 경우가 많다.

---

## 7. ML 커널에 중요한 빌드 플래그

### 7.1 요약 표

| 플래그 | 효과 | ML에서의 주의 |
|---|---|---|
| `-O2` | 일반 최적화, 자동 벡터화(clang) | 커널 라이브러리 기본값 |
| `-O3` | 공격적 unroll·벡터화 | 코드 크기 증가. 측정해서 이득이 있을 때만 |
| `-Os` / `-Oz` | 크기 우선 | 앱 코드에는 좋지만 **CMSIS-NN·TFLM 커널은 느려질 수 있다** → 라이브러리별로 다른 레벨 (E2 9.3절) |
| `-ffast-math` | 부동소수점 결합법칙·NaN/Inf 무시 허용 | float 누산 벡터화가 켜지지만 **결과가 bit-exact하지 않다**. golden vector 테스트(J6)와 충돌. NaN 검사 코드가 사라질 수 있음 |
| `-mcpu/-mfpu/-mfloat-abi` | 명령 집합·FPU·호출 규약 | 잘못 주면 조용히 느린 C 경로(E2 1.4절) 또는 링크 불가(1.3절) |
| `-flto` | 링크 타임 최적화 — 파일 경계를 넘어 인라인·제거 | 크기·속도 이득. 단 **섹션 속성·`used` 표시가 없는 심볼이 사라지거나**, 디버깅 심볼이 덜 직관적. 벤더 미리 빌드된 `.a`와는 LTO 불가 |
| `-ffunction-sections -fdata-sections` + `-Wl,--gc-sections` | 함수·변수별 섹션 + 링커 gc | **안 쓰는 op 커널 제거**의 기본 (7.3절) |
| `-ffreestanding` / `-fno-builtin` | libc 가정 끔 | startup 코드에 (4.5절) |
| `-fno-exceptions -fno-rtti` | C++ 예외·RTTI 끔 | TFLM이 기본으로 사용. 코드·`.ARM.exidx` 감소 |
| `-g` | 디버그 정보 | **flash 크기에 영향 없음**(ELF의 별도 섹션). 항상 켜 두고 `.bin`에서 빠진다 |

### 7.2 실측 — 최적화 레벨과 `-ffast-math`가 Cortex-M55 커널에 미치는 영향 (예제 11)

E2 6.3절에서 "fp32 누산은 `-ffast-math` 없이는 벡터화되지 않는다"를 봤다. 여기서는 **크기**까지 같이 본다.

무엇을 확인하는 코드인지: int8 내적과 fp32 내적을 `-Os/-O2/-O3/-O3 -ffast-math`로 Cortex-M55용 컴파일해 함수 크기와 Helium 명령을 센다.

```c
#include <stdint.h>
int32_t dot_s8(const int8_t *a, const int8_t *b, int n) {
    int32_t acc = 0;
    for (int i = 0; i < n; i++) acc += a[i] * b[i];
    return acc;
}
float dot_f32(const float *a, const float *b, int n) {
    float acc = 0.f;
    for (int i = 0; i < n; i++) acc += a[i] * b[i];
    return acc;
}
```

```sh
M55="--target=thumbv8.1m.main-none-eabihf -mcpu=cortex-m55 -mfloat-abi=hard"
for o in -Os -O2 -O3 "-O3 -ffast-math"; do
  cc $M55 $o -ffunction-sections -c dot.c -o d.o
  s8=$(xcrun llvm-size -A d.o | awk '$1==".text.dot_s8"{print $2}')
  f=$(xcrun llvm-size -A d.o | awk '$1==".text.dot_f32"{print $2}')
  # ... llvm-objdump -d로 함수별 벡터 명령을 grep (전체 스크립트는 생략)
done
```

```text
flags            s8_bytes f32_bytes s8 vector insns        f32 vector insns      
-Os              42       44       vmlava.s8              vfma.f32×1           
-O2              44       208      vmlava.s8              vfma.f32×7           
-O3              44       212      vmlava.s8              vfma.f32×7           
-O3 -ffast-math  44       56       vmlava.s8              vadd.f32×3 vfma.f32×1
```

`vfma.f32`가 스칼라(s 레지스터)인지 벡터(q 레지스터)인지는 디스어셈블로 확인해야 한다:

```text
== -O2  (dot_f32 일부)
      40:      	vldr	s2, [r4]
      44:      	vldr	s10, [r5]
      48:      	vldr	s4, [r4, #4]
      4c:      	vldr	s12, [r5, #4]
      50:      	vfma.f32	s0, s2, s10
== -O3 -ffast-math  (dot_f32 일부)
      10:      	dlstp.32	lr, r2
      14:      	vldrw.u32	q1, [r0], #16
      18:      	vldrw.u32	q2, [r1], #16
      1c:      	vfma.f32	q0, q2, q1
      20:      	letp	lr, 0x14 <dot_f32+0x14> @ imm = #-0x10
      24:      	vadd.f32	s2, s2, s3
      28:      	vadd.f32	s0, s0, s1
      2c:      	vadd.f32	s0, s0, s2
```

출력에서 볼 것:

- **int8은 모든 레벨에서 같은 4명령 루프**(`dlstp.8` / `vldrb.u8` ×2 / `vmlava.s8` / `letp`, 42~44 B). 정수 덧셈은 순서를 바꿔도 결과가 같으니 컴파일러가 마음대로 벡터화한다. 양자화 모델이 MCU에서 유리한 또 하나의 이유.
- **fp32는 `-O2`/`-O3`에서 스칼라 `vfma.f32 s0, ...`를 7번 펼친 208 B 코드**. 순서를 지켜야 하니(IEEE 결합법칙 불성립) 벡터화를 못 하고 unroll만 했다. `-ffast-math`를 주자 `q` 레지스터 4-lane FMA + tail-predicated 루프(`dlstp.32`/`letp`) + 마지막에 lane 4개를 더하는 `vadd` 3개, **56 B**로 줄었다. 대신 덧셈 순서가 바뀌어 결과의 마지막 비트가 달라진다(E2 6.3절에서 측정).
- 실무 결론: `-ffast-math`는 **파일 단위로** 필요한 전처리 코드에만 주고(CMake `set_source_files_properties(... COMPILE_OPTIONS -ffast-math)`), 검증 기준과 비교하는 코드·NaN 검사가 있는 코드에는 주지 않는다. bit-exact가 필요하면 int8/고정소수점 경로로 간다.

### 7.3 안 쓰는 커널을 바이너리에서 빼기 — sections + gc (예제 12)

TFLM은 op 커널을 수십 개 갖고 있고, CMSIS-NN도 마찬가지다. KWS 모델은 그중 5~8개만 쓴다. 나머지를 빼는 장치는 두 겹이다.

1. **소스 수준**: TFLM의 `MicroMutableOpResolver<N>`에 필요한 op만 `AddConv2D()` 식으로 등록한다(`AllOpsResolver`를 쓰면 모든 커널이 참조되어 gc가 못 지운다). F2에서 자세히.
2. **링커 수준**: `-ffunction-sections -fdata-sections`로 함수·변수마다 섹션을 만들고, `-Wl,--gc-sections`로 참조되지 않은 섹션을 버린다.

무엇을 확인하는 코드인지: (a) ARM 오브젝트에서 섹션이 함수별로 쪼개지는지, (b) 링크 gc가 안 쓰는 커널과 그 LUT를 실제로 버리는지 — (b)는 ARM 링커가 없으니 macOS 링커의 `-dead_strip`(같은 개념, Mach-O판)으로 확인한다.

```c
#include <stdint.h>
/* "op 커널 라이브러리" 흉내: 6개 커널 + 각자의 LUT. 앱은 이 중 2개만 쓴다. */
#define K(name, lutval) \
    const int8_t name##_lut[256] = {lutval}; \
    void name(int8_t *p, int n) { for (int i = 0; i < n; i++) p[i] = (int8_t)(name##_lut[(uint8_t)p[i]] + p[i] / 3); }
K(op_conv2d, 1) K(op_dwconv, 2) K(op_fc, 3) K(op_softmax, 4) K(op_lstm, 5) K(op_gelu, 6)
```

(a) ARM 오브젝트의 섹션 수:

```text
flags=''  sections: text=1 rodata=1
    .text 0x00000166
    .rodata 0x00000600
flags='-ffunction-sections -fdata-sections'  sections: text=7 rodata=6
    .text 0x00000000
    .text.op_conv2d 0x0000003a
    .text.op_dwconv 0x0000003a
    .text.op_fc 0x0000003a
```

(b) 호스트 링크, `app.c`는 `op_conv2d`와 `op_fc`만 호출:

```sh
for ds in "" "-Wl,-dead_strip"; do
  cc -std=c11 -Wall -Wextra -Os app.c kern_lib.c $ds -o app_gc &&
  echo "link flags='$ds': op symbols=$(nm app_gc | grep -c ' T _op_')  luts=$(nm app_gc | grep -c '_lut$')  $(size -m app_gc | grep -E '__text|__const' | tr -s '\t ' ' ' | tr '\n' ' ')"
done
```

```text
link flags='': op symbols=6  luts=6   Section __text: 544  Section __const: 1536 
link flags='-Wl,-dead_strip': op symbols=2  luts=2   Section __text: 272  Section __const: 512 
```

출력에서 볼 것:

- (a) 옵션 없이는 6개 커널이 `.text` 하나(0x166 = 358 B), LUT 6개가 `.rodata` 하나(0x600 = 1536 B)에 뭉쳐 있다. 이러면 링커는 **섹션 단위로만** 버릴 수 있으니 하나라도 쓰면 전부 남는다. 옵션을 주면 커널 6개 + 빈 `.text` = 7개, LUT 6개로 쪼개진다.
- (b) gc 후 커널 6 → 2, LUT 6 → 2, `__const` 1536 → 512 B(정확히 LUT 2개 × 256 B). **커널 코드뿐 아니라 그 커널만 쓰는 상수 테이블까지** 같이 사라진다. TFLM에서는 op마다 이런 상수가 붙어 있어서 효과가 크다.
- Mach-O는 기본적으로 심볼 단위로 쪼개져 있어서(`subsections_via_symbols`) `-ffunction-sections` 없이도 `-dead_strip`이 된다. **ELF에서는 컴파일 플래그와 링크 플래그가 둘 다 있어야** 한다.
- gc 결과를 확인하는 법(ELF): `-Wl,--print-gc-sections`로 버려진 섹션 목록을 출력하거나, 맵 파일의 "Discarded input sections" 부분을 본다(8.1절).

---

## 8. 맵 파일과 크기 예산

### 8.1 GNU ld 맵 파일 읽기 (예시 발췌 — 실제 링크 출력 아님)

`-Wl,-Map=fw.map`(필요하면 `-Wl,--cref`로 교차 참조 추가)가 만드는 맵 파일은 크게 네 부분이다: **Archive member included**(어느 `.a` 멤버가 왜 끌려왔나), **Discarded input sections**(gc로 버려진 것), **Memory Configuration**(MEMORY 블록 그대로 — 아래 발췌에서는 생략), **Linker script and memory map**(출력 섹션별 입력 섹션·심볼·주소). 4.3절 스크립트로 8.2절 오브젝트를 링크했다면(arena는 4.4절처럼 `.tensor_arena` 섹션에 넣었다고 가정) 이런 모양일 것이다. 형식을 보여 주려는 **예시**이며, 숫자는 8.2절 실측 크기로 맞췄다:

```text
Discarded input sections

 .text.relu_s32 0x00000000        0x6 kernels.o
 .text.unused_kernel
                0x00000000       0x1a kernels.o

.model_xip      0x90000000    0x18010
                0x90000000                __model_xip_start = .
 *(.model_weights)
 .model_weights
                0x90000000    0x18004 model.o
                0x90000000                g_model
                0x90018000                g_model_end
                0x90018000                g_model_size
 *(.model_weights.*)
                0x90018010                . = ALIGN (0x10)
 *fill*         0x90018004        0xc 
                0x90018010                __model_xip_end = .

.arena_dtcm     0x20000000     0xc000
                0x20000000                __arena_start = .
 *(.tensor_arena)
 .tensor_arena  0x20000000     0xc000 fw_main.o
                0x2000c000                __arena_end = .
```

읽는 법:

- 들여쓰기 없는 줄(`.model_xip 0x90000000 0x18010`)이 **출력 섹션**: VMA와 크기. LMA가 다르면 `load address 0x...`가 붙는다.
- `*(.model_weights)` 는 스크립트의 패턴, 바로 아래 줄이 그 패턴에 걸린 **입력 섹션**(어느 `.o`의 몇 바이트).
- `*fill*` 은 정렬 패딩. 0x18004 → 0x18010으로 맞추느라 12 B. 패딩이 큰 곳이 보이면 정렬 요구를 재검토한다.
- "Discarded input sections"에 `relu_s32`, `unused_kernel`이 보이면 gc가 일한 것. 반대로 **써야 할 것이 여기 있으면** 그게 "모델이 통째로 사라졌다" 버그의 원인이다(`KEEP` 누락, 4.3절).

맵 파일은 사람이 읽기 지루하므로 스크립트로 집계한다. Google의 **Bloaty**는 ELF를 섹션·심볼·컴파일 유닛 단위로 분해해 보여 주는 도구이고, 임베디드 쪽에서는 맵 파일을 HTML로 보여 주는 오픈소스 도구(예: puncover)도 쓰인다. 가장 확실한 것은 `nm --size-sort`와 `size -A`다.

### 8.2 실측 — 미니 KWS 펌웨어의 오브젝트 크기 보고 (예제 13)

모델 96 KB(가짜 바이트로 만든 98,304 B 파일을 `.incbin`), arena 48 KB, im2col 16 KB, Hann 창 LUT, mel 표를 가진 미니 펌웨어를 Cortex-M4용으로 컴파일하고 크기를 본다.

무엇을 확인하는 코드인지: `llvm-size -A`와 `llvm-nm --size-sort`로 **무엇이 몇 바이트이고 flash/RAM 어느 쪽인지** 가른다.

```c
#include <stdint.h>
/* libc 없음: string.h 대신 builtin */
extern const uint8_t g_model[];
int32_t dot_s8(const int8_t *a, const int8_t *b, int n);
#define ARENA_BYTES (48 * 1024)
__attribute__((aligned(16))) static uint8_t g_arena[ARENA_BYTES];          /* .bss */
__attribute__((section(".bss.scratch"), aligned(16))) static int8_t g_im2col[16 * 1024];
static int16_t g_audio_ring[2 * 512];                                      /* .bss: 마이크 ping-pong */
int32_t g_mel_lut[40] = {1, 2, 3};                                        /* .data */
const int16_t g_window[512] = {0, 1, 2};                                  /* .rodata: Hann 창 */
int infer(void) {
    __builtin_memcpy(g_im2col, g_model + 64, 256);
    g_arena[0] = (uint8_t)g_audio_ring[0];
    return dot_s8(g_im2col, (const int8_t *)g_arena, 256) + g_mel_lut[1] + g_window[3];
}
```

```sh
M4="--target=thumbv7em-none-eabihf -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -O2"
cc $M4 -Os -ffunction-sections -fdata-sections -c fw_main.c -o fw_main.o
cc $M4 -Os -ffunction-sections -fdata-sections -c kernels.c -o kernels.o
cc $M4 -c model.S -o model.o
xcrun llvm-size -A fw_main.o kernels.o model.o      # (메타데이터 섹션 줄 생략)
echo ---
xcrun llvm-nm --size-sort -S --defined-only fw_main.o
```

```text
fw_main.o  :
.text                         0      0
.text.infer                  68      0
.data.g_mel_lut             160      0
.rodata.g_window           1024      0
.bss.scratch              16384      0
.bss.g_arena              49152      0
kernels.o  :
.text                                0      0
.text.dot_s8                        34      0
.text.relu_s32                       6      0
.text.unused_kernel                 26      0
model.o  :
.text                   0      0
.model_weights      98308      0
---
00000000 00000044 T infer
00000000 000000a0 D g_mel_lut
00000000 00000400 R g_window
00000000 00004000 b g_im2col
00000000 0000c000 b g_arena
```

출력에서 볼 것:

- 처음에는 `#include <string.h>`로 썼다가 `fatal error: 'string.h' file not found`가 났다(1.4절 — sysroot 없음). `__builtin_memcpy`로 바꿨지만 재배치를 보면 결국 **`__aeabi_memcpy` 호출**이 생긴다 — 링크 때 libc나 compiler-rt가 필요하다.
- `g_audio_ring`이 목록에 없다. 0으로 초기화된 static이고 쓰는 곳이 없으니 컴파일러가 "항상 0"으로 상수 접어 버렸다. 실제 펌웨어에서는 DMA가 쓰니 `volatile` 또는 DMA 드라이버가 주소를 가져가서 남는다 — "크기 보고에 내 버퍼가 없다"면 이런 이유다.
- `nm`의 타입 글자: `T` 코드, `R` 읽기 전용 데이터, `D` 초기값 있는 데이터, `b` bss(소문자 = 파일 로컬). 이 한 글자로 flash/RAM 귀속을 판단한다.
- 심볼 크기 0인 `g_model`(3.3절에서 본 `.size` 누락) — 크기 보고에서는 섹션 크기(98308)를 써야 한다.

```svg
<svg viewBox="0 0 680 220" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="20" font-size="13">미니 KWS 펌웨어 크기 (예제 13 실측, gc 후 가정 · libc·startup 제외)</text> <text x="112" y="62" font-size="12" text-anchor="end">FLASH</text> <rect x="120" y="45" width="518.4" height="26" fill="#4a7bd0" fill-opacity="0.75"/> <rect x="638.4" y="45" width="5.4" height="26" fill="#3f9a6b"/> <rect x="643.8" y="45" width="0.84" height="26" fill="#e08a3c"/> <rect x="644.7" y="45" width="0.6" height="26" fill="#888"/> <text x="380" y="62" font-size="12" text-anchor="middle">모델 98,308 B (98.7%)</text> <text x="112" y="122" font-size="12" text-anchor="end">RAM</text>
<rect x="120" y="105" width="259.2" height="26" fill="#e08a3c" fill-opacity="0.75"/> <rect x="379.2" y="105" width="86.4" height="26" fill="#3f9a6b" fill-opacity="0.75"/> <rect x="465.6" y="105" width="0.84" height="26" fill="#888"/> <text x="250" y="122" font-size="12" text-anchor="middle">arena 49,152 B</text> <text x="422" y="122" font-size="12" text-anchor="middle">im2col 16,384</text> <line x1="120" y1="145" x2="660" y2="145" stroke="currentColor"/>
<line x1="120" y1="145" x2="120" y2="150" stroke="currentColor"/><line x1="255" y1="145" x2="255" y2="150" stroke="currentColor"/><line x1="390" y1="145" x2="390" y2="150" stroke="currentColor"/><line x1="525" y1="145" x2="525" y2="150" stroke="currentColor"/><line x1="660" y1="145" x2="660" y2="150" stroke="currentColor"/>
<text x="120" y="164" font-size="12" text-anchor="middle">0</text><text x="255" y="164" font-size="12" text-anchor="middle">25 KB</text><text x="390" y="164" font-size="12" text-anchor="middle">50 KB</text><text x="525" y="164" font-size="12" text-anchor="middle">75 KB</text><text x="660" y="164" font-size="12" text-anchor="end">100 KB</text> <text x="10" y="190" font-size="12">FLASH 나머지: Hann LUT 1,024 (초록) · .data 원본 160 (주황) · 코드 102 = infer 68 + dot_s8 34 (회색)</text> <text x="10" y="208" font-size="12">RAM 나머지: .data 160 · gc가 relu_s32 6 + unused_kernel 26 B를 제거한다고 가정</text>
</svg>
```

그림 6 — 예제 13의 숫자로 그린 flash·RAM 구성. ML 펌웨어의 전형적인 모양이다: **flash는 모델이, RAM은 arena가 지배**하고, 코드 크기는 상대적으로 작다(실제 제품에서는 TFLM 런타임 + 커널 + RTOS + BLE 스택이 수십~수백 KB 더해진다).

### 8.3 크기 예산 표 (D2 8절 워크시트에 연결)

D2 8절의 예산 워크시트는 **추정값**으로 채웠다. 빌드가 생기면 같은 표를 **실측값**으로 다시 채운다. 이 예제의 숫자와 가상의 칩(4.3절 MEMORY)으로:

| 항목 | 영역 | 실측 / 예산 | 출처 | 여유 |
|---|---|---|---|---|
| 모델 blob | QSPI 16 MB | 98,308 B | `size -A` `.model_weights` | 99% 남음 — 다른 모델·OTA 슬롯 B 가능 |
| 코드 + 상수 | 내부 FLASH 1 MB | 1,126 B (+ TFLM·RTOS 미포함) | `size -A` `.text.*` + `.rodata.*` | TFLM+CMSIS-NN 수십~100 KB대(빌드 구성 따라) — 실측 필요 |
| arena | DTCM 128 KB | 49,152 B / 요구치 `arena_used_bytes()` | `nm` + F2 | 스택 8 KB 포함 후 남는 양 확인 |
| scratch + 마이크 DMA | SRAM2 256 KB | 16,384 + 2,048 B | `nm` | 넉넉 |
| `.data` | SRAM1 + flash 원본 | 160 B ×2 | `size -A` | — |
| 부팅 copy-down | — | 4.16 ms (hot 가중치 150 KB 가정) | 예제 9 | wake-to-ready 예산과 비교 |

규칙: **예산표의 모든 숫자에 "어느 명령의 어느 줄에서 왔는지"를 적는다.** Don이 margin sign-off 문서에 측정 조건을 적던 것과 같다. CI에서 이 숫자를 매 빌드 뽑아 이전 빌드와 diff하면 "모델 바꿨더니 RAM 12 KB 늘었다"를 PR 단계에서 잡는다.

---

## 9. 디버그·트레이스 워크플로 — Don의 영역에 ML을 얹기

Trace32·J-Link·GDB는 Don이 더 잘 안다. 여기서는 **ML 펌웨어에서 달라지는 점**만 적는다.

### 9.1 심볼로 모델과 arena 보기

- ELF에 `-g`로 심볼이 있으면 디버거에서 `g_model`, `g_arena`를 바로 볼 수 있다. Trace32: `Data.dump g_arena`, `Var.View g_mel_lut`, 메모리 비교는 `Data.COMPare`. GDB: `x/16xb &g_model`, `p sizeof(g_arena)`, `info symbol 0x90000000`.
- **arena 사용 high-water**: 부팅 때 arena를 `0xA5`로 칠하고(stack painting과 같은 기법, D2 6.3절) 추론 후 덤프해서 칠이 남은 경계를 찾는다. TFLM의 `arena_used_bytes()`와 교차 확인.
- 활성값 텐서 덤프: 디버거로 arena의 특정 오프셋(텐서 주소는 TFLM이 로그로 줄 수 있다)을 파일로 저장 → 호스트 Python에서 numpy로 읽어 PC 결과와 비교. "layer N의 출력이 PC와 다르다"를 이렇게 이분 탐색한다(C8, F8 정확도 불일치 디버깅).

### 9.2 외부 flash에 모델 쓰기

ELF에 `0x90000000`(QSPI) 섹션이 있으면, 다운로드 도구가 **그 외부 flash를 프로그램하는 방법(flash loader/algorithm)** 을 알아야 한다. Trace32는 외부 flash용 `FLASH.CFI`/스크립트, J-Link는 지원 칩의 QSPI 설정 또는 사용자 정의 flash loader, 벤더 도구(예: STM32CubeProgrammer)는 "external loader"를 쓴다. 이게 안 되면 내부 flash만 써지고 QSPI는 옛 모델 그대로인 **조용한 불일치**가 생긴다 → 9.4절의 CRC 검사가 필요한 이유.

### 9.3 printf 경로 — semihosting, RTT, ITM

| 방식 | 원리 | 속도·부작용 | ML에서 |
|---|---|---|---|
| semihosting | `BKPT 0xAB`로 디버거에 syscall 위임 (`--specs=rdimon.specs`) | **매우 느림**(호출마다 코어 정지), 디버거 없으면 HardFault | 초기 bring-up에서 텐서 덤프를 파일로 바로 쓰기 |
| SEGGER RTT | RAM의 링버퍼를 J-Link가 백그라운드로 읽음 | 빠름, 코어 정지 없음 | 추론 시간 로그, layer별 cycle 수 |
| ITM/SWO | Cortex-M 트레이스 포트로 스트림 | 빠름, SWO 핀 필요(M0+ 없음) | DWT cycle counter와 같이 프로파일링(K 모듈) |
| UART | 평범한 시리얼 | 느리고 ISR 타이밍 바꿈 | 현장·공장 로그 |

semihosting printf를 추론 루프에 넣고 latency를 재면 **printf 시간을 재는 것**이 된다. 측정 구간 안에서는 DWT `CYCCNT`만 읽고, 출력은 끝난 뒤에.

### 9.4 모델 무결성 — 헤더 + CRC로 readback 검증 (예제 14)

모델을 펌웨어와 따로 굽거나 OTA로 바꾸면(J5), 부팅 때 "이 flash에 있는 모델이 온전하고, 이 런타임과 호환되나"를 확인해야 한다. 최소 구성은 **고정 크기 헤더 + CRC**다.

무엇을 확인하는 코드인지: 32 B 헤더(매직·버전·길이·CRC·arena 요구량)를 모델 앞에 붙이고, readback 검증과 비트 하나 오류 검출을 확인한다.

```c
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct {                 /* flash 모델 슬롯 맨 앞 32 B — 부트로더·앱·OTA가 공유 */
    uint32_t magic;              /* 'MDL1' */
    uint16_t hdr_version, runtime_abi;   /* 헤더 형식 / 요구 런타임(TFLM 스키마·op 세트) 버전 */
    uint32_t model_version;      /* 0x00010203 = 1.2.3 */
    uint32_t payload_len;        /* .tflite 바이트 수 */
    uint32_t payload_crc32;      /* CRC-32 (IEEE, zlib과 같은 다항식) */
    uint32_t arena_bytes;        /* 이 모델에 필요한 arena — 부팅 때 미리 확인 */
    uint32_t reserved[2];
} model_hdr_t;
_Static_assert(sizeof(model_hdr_t) == 32, "header must stay 32 bytes");

static uint32_t crc32(const uint8_t *p, size_t n) {          /* 비트 단위 — 작고 느린 참조 구현 */
    uint32_t c = 0xFFFFFFFFu;
    while (n--) { c ^= *p++; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & -(c & 1u)); }
    return ~c;
}
static uint8_t slot[32 + 4096] __attribute__((aligned(16)));  /* "flash 슬롯" */
int main(void) {
    FILE *f = fopen("model.tflite", "rb"); size_t n = fread(slot + 32, 1, 4096, f); fclose(f);
    model_hdr_t h = {0x314C444Du, 1, 3, 0x00010203u, (uint32_t)n, crc32(slot + 32, n), 2048, {0, 0}};
    memcpy(slot, &h, sizeof h);                                /* "OTA가 쓴 이미지" */
    const model_hdr_t *r = (const model_hdr_t *)slot;           /* 부팅 때 readback 검증 */
    printf("magic ok=%d len=%u crc=%08X recomputed=%08X id=%.4s payload%%16=%u\n",
           r->magic == 0x314C444Du, r->payload_len, r->payload_crc32,
           crc32(slot + 32, r->payload_len), (const char *)slot + 32 + 4,
           (unsigned)((uintptr_t)(slot + 32) % 16));
    slot[32 + 1000] ^= 0x04;                                   /* 비트 하나 뒤집힘 (flash 열화·OTA 중단) */
    printf("after bit flip: recomputed=%08X -> %s\n", crc32(slot + 32, r->payload_len),
           crc32(slot + 32, r->payload_len) == r->payload_crc32 ? "OK" : "REJECT, fall back to slot A");
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 model_hdr.c -o model_hdr && ./model_hdr
.venv/bin/python -c "import zlib; print(hex(zlib.crc32(open('model.tflite','rb').read())))"
```

```text
magic ok=1 len=2704 crc=012010D9 recomputed=012010D9 id=TFL3 payload%16=0
after bit flip: recomputed=7CC7C7BE -> REJECT, fall back to slot A
0x12010d9
```

출력에서 볼 것:

- C 구현의 CRC `012010D9`가 Python `zlib.crc32`와 같다 — 호스트 빌드 스크립트가 zlib으로 헤더를 만들고 펌웨어가 검증하는 구조가 성립한다.
- 헤더를 **32 B**(16의 배수)로 잡아서 페이로드 시작이 여전히 16 B 정렬이다(`payload%16=0`). 헤더 크기를 20 B 같은 값으로 잡으면 3.5절의 정렬이 깨진다. `_Static_assert`로 고정.
- `id=TFL3`까지 확인하면 "CRC는 맞는데 엉뚱한 포맷"도 거른다. `runtime_abi`는 "이 모델이 요구하는 op·스키마를 이 펌웨어의 런타임이 지원하나"를 나타내는 우리만의 번호다 — 펌웨어가 지원 범위를 모르면 새 모델이 `AllocateTensors()`에서야 실패한다(J5).
- 실제 MCU에서는 CRC 하드웨어 가속기나 테이블 방식 CRC를 쓴다. 비트 단위 구현은 바이트당 8회 루프라 수백 KB 모델이면 수 ms~수십 ms가 걸릴 수 있다 — 부팅 예산(5절)에 넣는다. 위변조까지 막으려면 CRC가 아니라 서명(secure boot 체인)이 필요하다.

---

## 10. 임베디드 관점에서 다시 보기

이 노트의 내용을 **빌드 타임에 강제되는 규칙**으로 바꾸면 이렇다. Don이 SSD 펌웨어에서 링커 ASSERT와 `_Static_assert`로 레이아웃을 지키던 방식 그대로다.

```c
#include <stdint.h>
#include <stddef.h>
extern const uint8_t g_model[];                 /* .model_weights, 16 B 정렬이어야 함 */
extern uint8_t __arena_start[], __arena_end[];  /* 링커 스크립트 심볼 */
#define MODEL_ARENA_BYTES  (90 * 1024)          /* 호스트에서 측정한 arena_used_bytes() + 여유 */

int model_preflight(void) {
    if (((uintptr_t)g_model & 15u) != 0)                      return -1;  /* 정렬 (3.5절) */
    if (g_model[4] != 'T' || g_model[5] != 'F' ||
        g_model[6] != 'L' || g_model[7] != '3')               return -2;  /* FlatBuffer id */
    if ((size_t)(__arena_end - __arena_start) < MODEL_ARENA_BYTES) return -3;  /* 링커 ASSERT의 런타임 사본 */
    return 0;
}
```

- 정렬·크기는 가능한 한 **링크 타임**(`ASSERT`)에, 모델 내용(id, CRC, 버전)은 **부팅 타임**에 검사한다. 런타임 `AllocateTensors()` 실패는 마지막 방어선이어야 한다.
- 같은 펌웨어로 모델만 바꾸는 구조(J5)를 원하면, 모델을 이미지에 넣지 말고 **고정 주소 파티션 + 헤더**(9.4절)로 두고 링커 스크립트에는 그 파티션의 시작 심볼만 둔다.
- 배치를 바꿔 가며 latency를 재는 실험(가중치 XIP vs SRAM, arena DTCM vs SRAM)은 **링커 스크립트만 바꾸면 되도록** 섹션 이름을 처음부터 분리해 둔다(4.4절의 "주소 투명성").
- Hexagon DSP·NPU 쪽에서는 이 모든 것이 벤더 툴(QNN context binary, Vela 출력의 메모리 영역 지정)로 이동하지만, 개념(읽기 전용 가중치 / 읽기·쓰기 scratch / 정렬 / 누가 DMA로 접근하나)은 그대로다(E5, F4).

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `xxd -i` 출력을 그대로 사용 | RAM이 모델 크기만큼 부족, 링크 시 SRAM overflow | `const` 없음 → `.data` (flash + RAM 둘 다) | `alignas(16) const` 추가, 맵 파일에서 `.rodata`/모델 섹션 확인 |
| 모델 배열 정렬 안 함 | M0+에서 HardFault, M4에서 가끔 느림, NPU DMA 에러 | `int8` 배열은 1 B 정렬 → FlatBuffer 내부 int32 비정렬 | `aligned(16)`, 링커 `ALIGN(16)`, 부팅 때 주소 검사 |
| C++에서 `const` 모델 배열에 `extern` 선언 없음 | "undefined reference to g_model" | namespace 범위 `const` = internal linkage, 쓰지 않으면 제거 | 헤더의 `extern` 선언을 include |
| `--gc-sections`인데 `KEEP` 누락 | 모델·벡터 테이블이 통째로 사라짐, `GetModel()`이 쓰레기 읽음 | 링커 심볼로만 접근하는 섹션은 "참조 없음" | `KEEP(*(.model_weights))`, 맵 파일 Discarded 확인 |
| 큰 zero 버퍼를 커스텀 섹션에 넣고 NOLOAD 안 붙임 | flash 이미지가 수십 KB 커짐 | 커스텀 이름 섹션은 PROGBITS로 0을 저장 | 출력 섹션에 `(NOLOAD)` 또는 이름을 `.bss.*`로 |
| QSPI 초기화 전에 모델 접근/복사 | 부팅 직후 BusFault | memory-mapped 모드 전에는 주소가 안 보임 | Reset_Handler 순서: QSPI init → copy → main |
| float ABI 혼용 (hard 앱 + softfp 라이브러리) | 링크 에러 또는 float 인자 쓰레기 | 호출 규약 불일치 | 모든 `.a`를 같은 `-mfloat-abi`로, `.ARM.attributes` 확인 |
| `AllOpsResolver` 사용 | flash가 예상보다 100 KB 이상 큼 | 모든 커널이 참조되어 gc 불가 | `MicroMutableOpResolver`로 필요한 op만 (F2) |
| 모델 파일 바꿨는데 재빌드 안 됨 | 옛 모델로 추론, 정확도 이상 | `.incbin` 의존성이 빌드 시스템에 없음 | `OBJECT_DEPENDS`/`add_custom_command DEPENDS`, 헤더 해시·버전 로그 |
| 호스트 `ar`로 ARM 아카이브 생성 (macOS) | "Built target" 성공인데 링크 때 undefined reference | Apple ranlib이 ELF 멤버 삭제 | `CMAKE_AR`을 `llvm-ar`/`arm-none-eabi-ar`로, CI에서 `ar t` 확인 |
| 전체에 `-ffast-math` | golden vector 테스트 불일치, NaN 검사 무력화 | 결합법칙 재배열, finite-math 가정 | 필요한 파일만, int8 경로 우선 |

---

## 12. 면접에서 이렇게 말한다

**Q.** How do you place model weights in external flash but keep the tensor arena in TCM?

**A.** 링커 스크립트의 MEMORY에 QSPI와 DTCM 영역을 정의하고, 가중치 배열에 `section(".model_weights")`와 16 B 정렬을 붙여 `> QSPI` 출력 섹션에 `KEEP`으로 모은다. arena는 `.tensor_arena` 섹션에 넣고 `(NOLOAD) > DTCM`으로 둔다. 런타임은 포인터만 받으니 코드 변경이 없다. QSPI는 부팅 때 memory-mapped 모드를 먼저 켜야 하고, XIP 대역폭이 병목이면 hot layer만 `> SRAM AT> QSPI`로 복사본을 둔다. 링커 `ASSERT`로 arena 크기와 모델 정렬을 빌드 타임에 검사한다.

> "I give the weights and the arena their own input sections and let the linker script decide where they live. The weights go into a KEEP'd output section in the memory-mapped QSPI region, aligned to 16 bytes, and are read in place through XIP; the arena is a NOLOAD section in DTCM, so it costs no flash and no boot-time init and gets zero-wait-state access. If XIP bandwidth turns out to be the bottleneck, I move the hot layers to a section with its VMA in SRAM and its LMA in QSPI and copy them in the reset handler after the QSPI controller is in memory-mapped mode. Linker ASSERTs check the arena size and model alignment at build time."

**Q.** What alignment does the model buffer need, and why?

**A.** TFLM은 16 B를 쓴다 — 생성기가 `alignas(16)`으로 배열을 만들고, arena 버퍼 정렬 상수도 16이다. 이유는 셋: 128-bit SIMD 로드 한 줄, FlatBuffer 안의 int32·float 필드가 버퍼 시작 기준으로 정렬돼 있어서 시작이 비정렬이면 내부 필드도 비정렬(M0+는 HardFault, LDRD/LDM은 M4에서도 fault), 그리고 arena가 비정렬이면 앞부분을 버린다. NPU가 DMA로 직접 읽으면 벤더가 더 큰 정렬을 요구할 수 있다.

> "Sixteen bytes for TFLite Micro — its array generator emits alignas(16), and the arena allocator aligns tensor buffers to 16 because that's a common SIMD requirement. It matters because the flatbuffer's scalars are aligned relative to the start of the buffer, so a misaligned base makes int32 bias and scale fields misaligned too: that's a HardFault on Cortex-M0+ and on LDRD or LDM on M4. If an NPU reads the model directly over DMA I check the vendor's alignment, which can be a cache line or larger."

**Q.** How do you keep unused kernels out of the binary?

**A.** 두 단계. 소스에서 TFLM `MicroMutableOpResolver`로 모델이 쓰는 op만 등록해 나머지 커널의 참조를 끊고, 빌드에서 `-ffunction-sections -fdata-sections` + `--gc-sections`로 참조 없는 함수와 그 상수 테이블을 링커가 버리게 한다. 직접 실험(커널 6개 중 2개 사용)에서 코드 544 → 272 B, LUT 1536 → 512 B로 줄었다. 맵 파일의 Discarded input sections와 `nm --size-sort`로 확인하고, 반대로 지워지면 안 되는 모델·벡터 테이블은 `KEEP`.

> "At the source level I register only the ops the model uses with MicroMutableOpResolver, so the other kernels aren't referenced. At build level everything is compiled with function and data sections and linked with gc-sections, so unreferenced kernels and their lookup tables are dropped. I verify with the map file's discarded-sections list and a size-sorted symbol dump, and I KEEP anything that is only reached through linker symbols, like the vector table or a model blob."

**Q.** How do you cross-compile an inference runtime for Android?

**A.** NDK의 `android.toolchain.cmake`를 `CMAKE_TOOLCHAIN_FILE`로 주고 `ANDROID_ABI=arm64-v8a`, `ANDROID_PLATFORM=android-<최소 API>`, Release로 configure한다. 그게 triple(`aarch64-linux-android<API>`), bionic sysroot, PIC 기본값을 맞춘다. dotprod·i8mm 같은 확장은 빌드 옵션으로 켜고 런타임에 CPU 기능을 확인한다. `adb push`로 벤치마크를 돌리고, 벤더 SDK(QNN 등)의 `.so`는 같은 ABI로 링크한다. STL 선택(c++_shared/static)과 16 KB 페이지 정렬 같은 배포 요구도 확인한다.

> "I configure CMake with the NDK's android.toolchain.cmake, ABI arm64-v8a, a minimum platform level and a Release build. That sets the aarch64-linux-android triple, the bionic sysroot and PIC defaults. CPU extensions like dot-product are enabled through build options and checked at runtime, since not every device has them. I push a benchmark binary with adb to measure on the real device, link vendor libraries like QNN against the same ABI, and check packaging details such as the STL choice and page-size alignment."

**Q.** Your build succeeded but the firmware is 64 KB bigger than expected. How do you find out why?

**A.** 이전 빌드와 `size -A` 섹션별 diff를 먼저 본다. `.data`가 늘었으면 `const` 빠진 배열(예: `xxd -i` 결과), PROGBITS 섹션이 늘었으면 NOLOAD 없는 zero 버퍼, `.text`면 `nm --size-sort`로 새로 끌려온 커널·라이브러리 멤버를 찾는다. 맵 파일의 Archive member included가 "왜 끌려왔나"를 알려 준다.

> "I diff the per-section sizes against the previous build first. Growth in .data usually means an array lost its const, growth in a PROGBITS output section can be a zero-filled buffer that isn't NOLOAD, and growth in .text I chase with a size-sorted symbol list and the map file's archive-member section, which tells me which reference pulled a new object in."

---

## 13. 직접 해보기

1. **손계산**: 400 KB 모델 중 hot layer 120 KB를 OSPI(8-bit, 100 MHz SDR, 오버헤드 무시)에서 SRAM으로 복사한다. 부팅 비용은 몇 ms인가? 정답: 8 bit × 100 MHz = 100 MB/s → 122,880 B ÷ 100 B/µs ≈ 1.23 ms.

2. **손계산**: 헤더 20 B + `.tflite`로 모델 슬롯을 만들고, 슬롯 시작이 0x90010000이다. 페이로드 시작 주소의 16 B 정렬 여부는? 헤더를 몇 B로 바꿔야 하나? 정답: 0x90010014 → 16으로 나눈 나머지 4, 비정렬. 헤더를 32 B(또는 16의 배수)로.

3. **코드**: 2.2절 `placement.c`에서 `g_requant`의 `const`를 추가·제거하며 `elfsec.py`로 `.data` 크기 변화를 확인하라. 그리고 `g_scratch`의 섹션 이름을 `.bss.noinit_scratch`로 바꿔 "bytes in .o"가 0이 되는지 확인하라. 힌트: 2.4절 — `.bss.` 접두어면 NOBITS.

4. **코드**: 7.3절 `kern_lib.c`를 `-ffunction-sections`만(`-fdata-sections` 없이) 컴파일해 LUT 섹션 개수를 세어라. gc가 커널은 지우는데 LUT는 못 지우는 상황이 왜 생기는지 설명하라. 정답: 데이터 섹션이 쪼개지지 않아 LUT 6개가 `.rodata` 하나에 남고, 그 섹션이 쓰이는 LUT 때문에 통째로 유지된다.

5. **코드**: 9.4절 `model_hdr.c`의 CRC를 256 엔트리 테이블 방식으로 바꾸고, 같은 CRC 값이 나오는지·호스트에서 몇 배 빨라지는지 재 보라. 힌트: `table[i]` = 비트 단위 루프를 바이트 `i`에 8번 돌린 값, 본 루프는 `c = table[(c ^ b) & 0xFF] ^ (c >> 8)`.

6. **설계**: Hark 같은 웨어러블(추정)에서 wake word 모델(60 KB)과 IMU 제스처 모델(20 KB)을 같은 MCU에 넣되, 둘 다 OTA로 독립 교체하고 싶다. 4.3절 스크립트를 어떻게 바꾸겠나? 힌트: 모델별 고정 파티션(헤더 + 페이로드) 두 개, 각 시작 주소를 심볼로, A/B 슬롯이면 부트로더가 유효한 쪽 주소를 넘김. arena는 두 모델이 동시에 안 돌면 하나를 공유(F2, J1).

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| target triple | 대상 플랫폼 이름 | `arch-vendor-os-abi`, 예: `thumbv7em-none-eabihf` |
| sysroot | 대상용 헤더·라이브러리 묶음 | libc 헤더(`string.h`)와 `.a`, startup 파일이 있는 루트 디렉터리 |
| EABI / eabihf | Arm 임베디드 호출 규약 | hf = float 인자를 FPU 레지스터(s0~)로 전달 |
| newlib / newlib-nano / picolibc | 임베디드 libc | 크기·기능이 다른 C 라이브러리 구현 |
| freestanding | libc 없는 환경 | `-ffreestanding`: 컴파일러가 libc 함수를 가정·생성하지 않게 |
| PROGBITS / NOBITS | ELF 섹션 타입 | 바이트가 파일에 있음 / 크기만 있음(.bss) |
| LMA / VMA | load / virtual(run) address | 이미지에 저장된 주소 / 실행 중 접근하는 주소 |
| `AT>` | 링커 스크립트 LMA 지정 | `> SRAM AT> FLASH` = SRAM에서 실행, FLASH에 저장 |
| NOLOAD | 초기화·저장 안 하는 출력 섹션 | 이미지 바이트 0, 부팅 init 0 |
| `KEEP` | gc 예외 | 참조 없어도 섹션을 남김 |
| `--gc-sections` | 링크 타임 섹션 gc | 참조되지 않는 입력 섹션 제거 |
| XIP | execute in place | flash(QSPI)를 메모리처럼 직접 읽기·실행 |
| `.incbin` | 어셈블러 지시어 | 파일 바이트를 그대로 섹션에 삽입 |
| toolchain file | CMake 크로스 설정 | 컴파일러·triple·플래그·sysroot를 한 파일에 |
| `CMAKE_TRY_COMPILE_TARGET_TYPE` | CMake 변수 | 컴파일러 검사를 static lib로 해서 bare-metal 링크 실패 회피 |
| NDK | Android Native Development Kit | Android용 clang·sysroot·`android.toolchain.cmake` |
| map file | 링커 맵 | 섹션·심볼·주소·버려진 섹션 목록 |
| scatter file | Arm Compiler 링커 설정 | GNU 링커 스크립트에 해당 |

---

## 15. 요약 & 체크리스트

edge ML 펌웨어의 빌드는 Don이 아는 bare-metal 빌드에 **모델 blob과 arena**를 얹은 것이다. 모델은 `alignas(16) const`로(또는 `.incbin`·CMake 생성으로) 자기 섹션에 넣고, 링커 스크립트가 그 섹션을 QSPI(XIP) 또는 SRAM 복사본(`AT>`)에 놓는다. arena와 scratch는 `NOLOAD`로 DTCM·다른 SRAM bank에 둔다. CMake toolchain file은 triple·float ABI·`ar`까지 정하고, `-ffunction-sections -fdata-sections --gc-sections`와 op resolver가 안 쓰는 커널을 뺀다. 결과는 `size -A`·`nm --size-sort`·맵 파일로 실측해 예산표를 채우고, 정렬·크기는 링커 `ASSERT`로, 모델 무결성은 헤더+CRC로 부팅 때 검사한다. 이 Mac에서는 오브젝트·아카이브까지 실측했고, 링크 단계는 시뮬레이션과 예시로 대신했다.

- [ ] triple 하나(`thumbv8.1m.main-none-eabihf`)를 보고 arch·OS·float ABI를 말할 수 있다
- [ ] soft / softfp / hard float ABI의 차이를 디스어셈블로 설명할 수 있다
- [ ] `xxd -i` 결과가 왜 `.data`로 가는지, 어떻게 고치는지 말할 수 있다
- [ ] `.incbin` 어셈블리 파일을 쓰고 섹션·정렬·크기 심볼을 확인할 수 있다
- [ ] TFLM이 16 B 정렬을 요구하는 이유 세 가지를 말할 수 있다
- [ ] `> SRAM AT> QSPI`, `(NOLOAD)`, `KEEP`, `ASSERT`를 써서 모델·arena 배치 링커 스크립트를 쓸 수 있다
- [ ] copy-down 부팅 비용을 bytes ÷ 대역폭으로 손계산할 수 있다
- [ ] bare-metal용 CMake toolchain file의 핵심 줄(`Generic`, `_COMPILER_TARGET`, `TRY_COMPILE_TARGET_TYPE`, `CMAKE_AR`)을 쓸 수 있다
- [ ] Android NDK로 `arm64-v8a` 빌드를 configure하는 명령을 쓸 수 있다
- [ ] `-ffunction-sections` + `--gc-sections`로 커널이 빠지는 것을 `nm`/맵 파일로 확인할 수 있다

## 참고 자료

- GNU ld 매뉴얼 — Linker Scripts (MEMORY, SECTIONS, `AT>`, `NOLOAD`, `KEEP`, `ASSERT`): https://sourceware.org/binutils/docs/ld/
- GNU as 매뉴얼 — `.incbin`, `.section`, `.balign`: https://sourceware.org/binutils/docs/as/
- CMake 문서 — cmake-toolchains(7) (Cross Compiling, `CMAKE_TRY_COMPILE_TARGET_TYPE`): https://cmake.org/cmake/help/latest/manual/cmake-toolchains.7.html
- Android NDK — CMake 가이드 (`android.toolchain.cmake`, `ANDROID_ABI`, `ANDROID_PLATFORM`): https://developer.android.com/ndk/guides/cmake
- TensorFlow Lite Micro 소스 (`micro_arena_constants.h`, `micro_allocator.h`, `tools/generate_cc_arrays.py`, `tools/make/targets/cortex_m_generic_makefile.inc`): https://github.com/tensorflow/tflite-micro
- Arm GNU Toolchain 다운로드 페이지: https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads
- LLVM Embedded Toolchain for Arm (GitHub, ARM-software): https://github.com/ARM-software/LLVM-embedded-toolchain-for-Arm
- picolibc: https://github.com/picolibc/picolibc
- Bloaty (Google, 바이너리 크기 분석): https://github.com/google/bloaty
- Clang 문서 — Cross-compilation using Clang: https://clang.llvm.org/docs/CrossCompilation.html
- 이 노트 세트: D2(메모리 계산·예산), E2(Arm 플래그·float ABI·벡터화), E7(TCM·DMA·캐시), F2(TFLite Micro), J1(추론 통합), J5(모델 업데이트)
