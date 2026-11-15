---
id: TOOL-B14
time: 60분
updated: 2026-11-15
---
# TOOL-B14 맥에서 크로스 컴파일 첫걸음

## 학습 목표

- 타깃 트리플(target triple) `arch-vendor-os-env` 을 읽고, 같은 `target.c` 를 Mac clang 하나로 `aarch64-linux-gnu`, `thumbv7em-none-eabi`(Cortex-M4 hard/soft float), `x86_64` 로, VM clang 으로 `riscv32-unknown-elf` 로 **오브젝트까지** 만든다.
- `readelf -h` 의 Class / Data / Machine / Flags 와 디스어셈블리를 비교해 "이 `.o` 는 어느 CPU · 어떤 float ABI 용인가" 를 말한다. 특히 **ARM `.o` 의 e_flags 에는 float ABI 비트가 없다**(실측) — 그 정보는 `.ARM.attributes` 에 있다.
- `clang --target=... main.c` **링크**가 Mac 에서 왜 실패하는지(ELF 링커 · crt0/crt1 · libc · 헤더 · compiler-rt 가 없음) 실제 오류 문장으로 설명하고, sysroot · `--gcc-toolchain` · newlib(`--specs=nano.specs --specs=nosys.specs`)이 각각 무엇을 채우는지 안다.
- ELF 헤더(52/64 B)만 읽는 `elf_target_info()` 를 짜서 실제 오브젝트·실행 파일 16개(Mach-O 2개 포함)의 readelf 답과 맞추고, 헤더만으로 "같이 링크해도 되나" 를 판정하는 CI 게이트를 만든다.

## 1. 문제

펌웨어 빌드는 **전부 크로스 컴파일**이다 — 노트북(호스트)에서 Cortex-M / RISC-V MCU / SSD 컨트롤러 코어(타깃)용 바이너리를 만든다. 면접에서 "arm-none-eabi 가 뭐의 약자냐", "sysroot 가 뭐냐", "벤더가 준 `.a` 를 링크했더니 `uses VFP register arguments` 에러가 난다, 왜?" 는 툴체인을 실제로 만져 본 사람인지 가르는 질문이다. 이 레슨은 Apple clang 하나가 이미 여러 타깃의 **컴파일러**라는 것, 그런데 **툴체인**은 아니라는 것을 실측으로 확인한다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">타깃 트리플 = arch(+sub) - vendor - os - env/abi   (clang 이 정규화한 이름, 실측)</text>
  <rect x="10" y="32" width="170" height="26" class="f-acc" stroke="currentColor"/><text x="18" y="50" fill="currentColor">arch: thumbv7em</text>
  <rect x="180" y="32" width="150" height="26" class="f-mut" stroke="currentColor"/><text x="188" y="50" fill="currentColor">vendor: unknown</text>
  <rect x="330" y="32" width="120" height="26" class="f-mut" stroke="currentColor"/><text x="338" y="50" fill="currentColor">os: none</text>
  <rect x="450" y="32" width="110" height="26" class="f-ok" stroke="currentColor"/><text x="458" y="50" fill="currentColor">env: eabi</text>
  <text x="572" y="50" fill="currentColor" font-size="12">← thumbv7em-none-eabi</text>
  <rect x="10" y="64" width="170" height="26" class="f-acc" stroke="currentColor"/><text x="18" y="82" fill="currentColor">arch: aarch64</text>
  <rect x="180" y="64" width="150" height="26" class="f-mut" stroke="currentColor"/><text x="188" y="82" fill="currentColor">vendor: unknown</text>
  <rect x="330" y="64" width="120" height="26" class="f-warn" stroke="currentColor"/><text x="338" y="82" fill="currentColor">os: linux</text>
  <rect x="450" y="64" width="110" height="26" class="f-ok" stroke="currentColor"/><text x="458" y="82" fill="currentColor">env: gnu</text>
  <text x="572" y="82" fill="currentColor" font-size="12">← aarch64-linux-gnu</text>
  <rect x="10" y="96" width="170" height="26" class="f-acc" stroke="currentColor"/><text x="18" y="114" fill="currentColor">arch: riscv32</text>
  <rect x="180" y="96" width="150" height="26" class="f-mut" stroke="currentColor"/><text x="188" y="114" fill="currentColor">vendor: unknown</text>
  <rect x="330" y="96" width="120" height="26" class="f-mut" stroke="currentColor"/><text x="338" y="114" fill="currentColor">os: unknown</text>
  <rect x="450" y="96" width="110" height="26" class="f-ok" stroke="currentColor"/><text x="458" y="114" fill="currentColor">env: elf</text>
  <text x="572" y="114" fill="currentColor" font-size="12">← riscv32-unknown-elf</text>
  <text x="10" y="142" fill="currentColor" font-size="12">CPU 세부는 트리플 밖의 플래그로: -mcpu -mfpu -mfloat-abi (ARM) / -march -mabi (RISC-V)</text>
  <text x="10" y="172" fill="currentColor" font-weight="bold">"크로스 툴체인" = 아래 칸이 모두 같은 타깃용으로 있어야 한다 — 이 Mac 에는?</text>
  <rect x="10" y="184" width="100" height="58" class="f-ok" stroke="currentColor"/><text x="18" y="204" fill="currentColor">컴파일러</text><text x="18" y="222" fill="currentColor" font-size="12">ARM/AArch64</text><text x="18" y="238" fill="currentColor" font-size="12">x86 있음</text>
  <rect x="115" y="184" width="100" height="58" class="f-warn" stroke="currentColor"/><text x="123" y="204" fill="currentColor">RISC-V</text><text x="123" y="222" fill="currentColor" font-size="12">백엔드</text><text x="123" y="238" fill="currentColor" font-size="12">없음</text>
  <rect x="220" y="184" width="100" height="58" class="f-ok" stroke="currentColor"/><text x="228" y="204" fill="currentColor">어셈블러</text><text x="228" y="222" fill="currentColor" font-size="12">clang 내장</text><text x="228" y="238" fill="currentColor" font-size="12">있음</text>
  <rect x="325" y="184" width="100" height="58" class="f-warn" stroke="currentColor"/><text x="333" y="204" fill="currentColor">ELF 링커</text><text x="333" y="222" fill="currentColor" font-size="12">ld64 = Mach-O</text><text x="333" y="238" fill="currentColor" font-size="12">lld 없음</text>
  <rect x="430" y="184" width="100" height="58" class="f-warn" stroke="currentColor"/><text x="438" y="204" fill="currentColor">crt0 / crt1</text><text x="438" y="222" fill="currentColor" font-size="12">시작 코드</text><text x="438" y="238" fill="currentColor" font-size="12">없음</text>
  <rect x="535" y="184" width="100" height="58" class="f-warn" stroke="currentColor"/><text x="543" y="204" fill="currentColor">libc + 헤더</text><text x="543" y="222" fill="currentColor" font-size="12">stdio.h 등</text><text x="543" y="238" fill="currentColor" font-size="12">없음</text>
  <rect x="640" y="184" width="95" height="58" class="f-warn" stroke="currentColor"/><text x="647" y="204" fill="currentColor">builtins</text><text x="647" y="222" fill="currentColor" font-size="12">libgcc·rt</text><text x="647" y="238" fill="currentColor" font-size="12">없음</text>
  <text x="10" y="270" fill="currentColor" font-size="12">→ Mac 에서는 -c (오브젝트) 까지만 된다. 링크하려면 sysroot(헤더+libc+crt) 와 ELF 링커가 필요하다.</text>
  <text x="10" y="292" fill="currentColor" font-size="12">→ arm-none-eabi-gcc 는 이 묶음 전체를 준다 (newlib 포함). Linux 타깃 예: riscv64-linux-gnu-gcc + sysroot</text>
  <text x="10" y="314" fill="currentColor" font-size="12">초록 = 이 Mac 에 있음, 주황 = 없음 (5. 풀이 Part B 의 실제 오류 문장으로 확인)</text>
</svg>
```

실험 재료 (모두 이 디렉터리):

| 파일 | 역할 |
|---|---|
| `target.c` | 크로스 컴파일할 freestanding 소스: `struct sample`, float 곱셈 `scale()`, 정수 루프 `checksum()`. 헤더는 `<stdint.h>` 하나 |
| `main.c` | 링크 실험용 `main()` — `checksum({1,2,3,4}) = 2` 를 종료 코드로 |
| `demo_cross.sh` | Mac 빌드 + Lima VM(`elinux`) 빌드·`readelf`·링크 실험을 한 번에. 결과를 `cross_*.txt` 와 `objs/*.o` 로 남김 |
| `objs/` | 실제 오브젝트 15개 (각 424 ~ 1072 B) |
| `cross_mac.txt` `cross_disasm.txt` `cross_readelf_h.txt` `cross_arm_attr.txt` `cross_link.txt` `cross_hdr64.txt` | 아래 풀이에 붙인 출력의 원본 |

<!-- code: target.c -->

### Part A — 같은 C 파일, 여러 타깃: 헤더와 명령어 비교

- `bash demo_cross.sh` 로 오브젝트를 만든다: Mac clang 으로 `aarch64-linux-gnu`, `aarch64_be-linux-gnu`, `x86_64-linux-gnu`, `thumbv7em-none-eabi -mcpu=cortex-m4` (hard / soft float), Mach-O `arm64` / `x86_64`; VM clang 으로 `riscv32` (`rv32imac/ilp32`, `rv32imafc/ilp32f`, `rv32ima`, `rv32ec/ilp32e`, `rv32imac_ztso`), `riscv64` (`rv64gc/lp64d`); VM `arm-none-eabi-gcc` 로 Cortex-M4 오브젝트와 **링크된** `.elf` 두 개.
- `readelf -h` 의 Class / Data / Machine / Flags 와 `scale()` · `checksum()` 디스어셈블리를 표로 비교한다 (FPU 명령 vs 라이브러리 호출, 회전 명령 유무).
- 코딩: `rd16()` / `rd32()` (엔디언을 지키는 바이트 조립), `machine_name()` (EM_ARM 40 / EM_X86_64 62 / EM_AARCH64 183 / EM_RISCV 243). 주어진 `elf_target_info()` 가 ELF32 / ELF64 의 **e_flags 위치(36 / 48)** 를 고르는 이유를 설명한다.

### Part B — 왜 링크가 안 되나 + 헤더가 아닌 입력 걸러내기

- Mac 에서 `clang --target=aarch64-linux-gnu main.c target.c` 와 `--target=thumbv7em-none-eabi` 링크, `#include <stdio.h>`, `-fuse-ld=lld`, 그리고 VM 의 riscv32 / newlib 링크를 실제로 실패시키고 **무엇이 빠졌는지** 오류 문장으로 짚는다. 무엇을 넣으면 성공하는지도 (`--specs=nano.specs --specs=nosys.specs`, `riscv64-linux-gnu-gcc` + `qemu-riscv64`).
- 코딩: `elf_check_ident()` — 짧은 버퍼, NULL, 엉뚱한 매직, **Mach-O 매직(0xFEEDFACF)은 "ELF 아님" 으로 따로**, 잘못된 EI_CLASS / EI_DATA, ELF32 는 52 B · ELF64 는 64 B 미만이면 거부. 큰 엔디언 `aarch64_be` 오브젝트도 바르게 읽어야 한다.

### Part C — float ABI: 헤더가 말해 주는 것과 못 하는 것

- `decode_flags()` — ARM: EABI 버전(`e_flags >> 24`), `EF_ARM_ABI_FLOAT_HARD 0x400` / `SOFT 0x200`, 둘 다 없으면 **UNKNOWN** (`.o` 가 그렇다). RISC-V: `EF_RISCV_RVC 0x1`, float ABI `(e_flags & 0x6)` 0 soft / 2 single / 4 double / 6 quad.
- `link_check(a, b)` — machine → class → endian → float ABI 순서로 판정. 실제 GNU ld / ld.lld 결과(`can't link soft-float modules with single-float modules`, `uses VFP register arguments`)와 맞춘다.
- `ci_gate()` — 펌웨어 이미지에 같이 들어갈 벤더 오브젝트 목록을 검사: 확실히 깨지는 수와 "헤더로는 모름(attributes 를 봐야 함)" 수를 따로 센다.

## 2. Problem (English)

> **Cross-compiling on a Mac: identify the target of an object file.**
>
> Your CI builds firmware for a Cortex-M4F (hard-float) image and a RISC-V `rv32imafc/ilp32f` image on macOS runners. A vendor drops prebuilt objects into `libs/`. Builds sometimes fail at link time with `uses VFP register arguments, X does not` or `can't link soft-float modules with single-float modules`, and someone accidentally committed a Mach-O object.
>
> 1. Compile the same `target.c` for `aarch64-linux-gnu`, `thumbv7em-none-eabi -mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16` (and `soft`), and `riscv32-unknown-elf -march=rv32imac -mabi=ilp32`. Compare `readelf -h` and the disassembly of `scale()`. Explain why `clang --target=... main.c target.c` fails to **link** on the Mac.
> 2. Implement, reading **only the ELF header** (no section parsing):
>
> ```c
> int elf_target_info(const uint8_t *buf, size_t n, struct target_info *out);
> /* 0 on success; TI_ERR_SHORT / TI_ERR_MAGIC / TI_ERR_MACHO / TI_ERR_CLASS / TI_ERR_DATA */
> enum link_verdict link_check(const struct target_info *a, const struct target_info *b);
> int ci_gate(const struct blob *objs, int n, const struct target_info *want, int *unknown);
> ```
>
> Constraints: no pointer casts to header structs (alignment / endianness), big-endian ELF must work, Mach-O (`cf fa ed fe`) must be reported as "not ELF", ELF32 needs 52 bytes and ELF64 needs 64.
>
> Example: `m4_hard.elf` → `ELF32 LSB EXEC ARM, EABI5, hard-float`; `m4_hard.o` → `ELF32 LSB REL ARM, EABI5, float ABI unknown`; `rv32imac.o` + `rv32imafc.o` → `LINK_ERR_FLOAT`.
>
> Follow-ups: what is a sysroot; why does `arm-none-eabi-gcc` ship newlib; what goes wrong if you mix `-mfloat-abi=hard` and `softfp`; why is `.o` e_flags not enough for ARM.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/TOOL-B14_s && /tmp/TOOL-B14_s
bash demo_cross.sh            # 실험 재현 (VM 'elinux' 필요, 약 3초) — objs/ 와 cross_*.txt 를 다시 만든다
```

처음 실행 결과 (실제 출력, 44개 FAIL 중 앞뒤만):

```text
FAIL starter.c:220  rd16((const uint8_t[]){0x34, 0x12}, 0) == 0x1234 && rd16((const uint8_t[]){0x34, 0x12}, 1) == 0x3412
FAIL starter.c:221  rd32((const uint8_t[]){0x00, 0x00, 0x00, 0x05}, 0) == 0x05000000u
FAIL starter.c:222  rd32((const uint8_t[]){0x00, 0x00, 0x00, 0x05}, 1) == 0x5u
FAIL starter.c:223  strcmp(machine_name(40), "ARM") == 0 && strcmp(machine_name(183), "AArch64") == 0
…
FAIL starter.c:243  INFO(H_arm64_macho_o, &t) == TI_ERR_MACHO
FAIL starter.c:244  INFO(H_x86_64_macho_o, &t) == TI_ERR_MACHO
…
FAIL starter.c:282  INFO(H_rv32imafc_o, &want) == 0 && ci_gate(rv_libs, 5, &want, &unk) == 4 && unk == 0
FAIL starter.c:288  INFO(H_m4_hard_elf, &want) == 0 && ci_gate(m4_libs, 5, &want, &unk) == 3 && unk == 2
2/46 passed
```

- 테스트의 바이트 배열 `H_*` 는 `demo_cross.sh` 가 만든 실제 파일의 첫 64 바이트(`cross_hdr64.txt`, VM `od -tx1` / Mac `od`)를 그대로 옮긴 것이다. 주석의 `readelf: Flags …` 가 기대값의 출처다.
- `elf_target_info()` 자체는 주어진 코드다 — 네가 채울 TODO(`rd16`, `rd32`, `machine_name`, `elf_check_ident`, `decode_flags`, `link_check`, `ci_gate`)를 엮는다. Part A 의 `INFO(...)` 체크는 `elf_check_ident` 가 성공 경로(52/64)를 돌려줘야 통과하므로, 성공 경로부터 먼저 짜도 된다.

<!-- code: demo_cross.sh -->

## 4. 힌트

<details>
<summary>힌트 1 — 헤더를 struct 로 캐스팅하지 말 것</summary>

`(const Elf32_Ehdr *)buf` 는 세 가지로 틀린다: (1) 버퍼 정렬이 보장되지 않는다(Cortex-M0 에서 비정렬 `ldr` = HardFault, ARCH 트랙), (2) 큰 엔디언 파일을 작은 엔디언 호스트에서 읽으면 값이 뒤집힌다, (3) 엄격한 별칭 규칙(strict aliasing) 위반. 바이트를 하나씩 읽어 `(uint32_t)p[3] << 24 | …` 로 조립하면 셋 다 해결된다. 시프트 전에 `uint32_t` 로 올리는 이유: `uint8_t` 는 `int` 로 승격되고(C-B02), `0x80 << 24` 는 `int` 오버플로(UB)다.

</details>

<details>
<summary>힌트 2 — ELF32 와 ELF64 는 0x18 부터 어긋난다</summary>

앞 24 바이트(`e_ident` 16 + `e_type` 2 + `e_machine` 2 + `e_version` 4)는 같다. 그 뒤 `e_entry`, `e_phoff`, `e_shoff` 가 주소 크기(4 / 8 B)라서 `e_flags` 는 ELF32 에서 0x24(36), ELF64 에서 0x30(48). 헤더 전체는 52 / 64 B (`readelf: Size of this header`).

</details>

<details>
<summary>힌트 3 — 검사 순서가 오류 코드를 결정한다</summary>

길이 4 미만 → SHORT, 그다음 매직. Mach-O 64비트 오브젝트의 첫 4 바이트는 파일에 `cf fa ed fe` (리틀 엔디언으로 쓴 0xFEEDFACF, `otool -hv` 의 `MH_MAGIC_64`). ELF 매직을 본 **뒤에야** "16 바이트도 안 된다" 를 SHORT 로 말할 수 있고, 클래스를 본 뒤에야 52/64 를 안다.

</details>

<details>
<summary>힌트 4 — ARM .o 에 float 비트가 없는 이유</summary>

`cross_readelf_h.txt` 에서 `m4_hard.o` 와 `m4_soft.o` 는 둘 다 `Flags: 0x5000000, Version5 EABI` 다(clang 도 gcc 도). `0x400`/`0x200` 은 **링커가** 입력의 `.ARM.attributes` (`Tag_ABI_VFP_args: VFP registers`)를 합쳐 실행 파일에 찍는다. 그래서 헤더만 보는 함수는 `.o` 에 대해 "모름" 이라고 정직하게 답해야 한다. RISC-V 는 컴파일러가 `.o` 에 바로 넣는다.

</details>

## 5. 풀이

### 0. 실험 결과 한눈에 — `demo_cross.sh`

Mac (Apple clang 21, 호스트 트리플 `arm64-apple-darwin27.2.0`) 쪽 `file(1)` 결과 (`cross_mac.txt`):

```text
aarch64_be.o:    ELF 64-bit MSB relocatable, ARM aarch64, version 1 (SYSV), not stripped
aarch64_linux.o: ELF 64-bit LSB relocatable, ARM aarch64, version 1 (SYSV), not stripped
arm64_macho.o:   Mach-O 64-bit object arm64
m4_hard.o:       ELF 32-bit LSB relocatable, ARM, EABI5 version 1 (SYSV), not stripped
m4_soft.o:       ELF 32-bit LSB relocatable, ARM, EABI5 version 1 (SYSV), not stripped
x86_64_linux.o:  ELF 64-bit LSB relocatable, x86-64, version 1 (SYSV), not stripped
x86_64_macho.o:  Mach-O 64-bit object x86_64
```

`--target` 하나 바꿨을 뿐인데 **Mach-O 와 ELF 가 갈린다** — 오브젝트 형식은 트리플의 os 부분이 정한다(darwin → Mach-O, linux / none / elf → ELF). RISC-V 는 Mac clang 에 백엔드가 없어 컴파일 단계에서 끝난다 (실제 출력):

```text
$ clang --target=riscv32-unknown-elf -march=rv32imac -mabi=ilp32 -ffreestanding -Os -c target.c -o …/rv.o
error: unable to create target: 'No available targets are compatible with triple "riscv32-unknown-unknown-elf"'
```

`clang -print-targets` 의 목록도 aarch64 / aarch64_32 / aarch64_be / arm / arm64 / arm64_32 / armeb / thumb / thumbeb / x86 / x86-64 뿐이다. Mac `/usr/bin/objdump` 도 RISC-V 를 **디스어셈블하지 못한다** (`can't find target: unable to get target for 'riscv32--'`) — 헤더(`-f`)는 읽는다. 그래서 RISC-V 는 VM(Ubuntu clang 21.1.8)에서 만들었다.

VM `readelf -h` 요약 (`cross_readelf_h.txt`, 모든 줄 실측):

| 파일 | 만든 것 | Class | Data | Type | Machine | Flags (readelf 그대로) |
|---|---|---|---|---|---|---|
| `aarch64_linux.o` | Mac clang | ELF64 | little | REL | AArch64 | `0x0` |
| `aarch64_be.o` | Mac clang | ELF64 | **big** | REL | AArch64 | `0x0` |
| `x86_64_linux.o` | Mac clang | ELF64 | little | REL | Advanced Micro Devices X86-64 | `0x0` |
| `m4_hard.o` / `m4_soft.o` | Mac clang | ELF32 | little | REL | ARM | `0x5000000, Version5 EABI` (**둘이 같다**) |
| `gcc_m4_hard.o` / `gcc_m4_soft.o` | VM arm-none-eabi-gcc 14.2.1 | ELF32 | little | REL | ARM | `0x5000000, Version5 EABI` |
| `rv32imac.o` | VM clang | ELF32 | little | REL | RISC-V | `0x1, RVC, soft-float ABI` |
| `rv32imafc.o` | VM clang | ELF32 | little | REL | RISC-V | `0x3, RVC, single-float ABI` |
| `rv32ima.o` | VM clang | ELF32 | little | REL | RISC-V | `0x0` |
| `rv64gc.o` | VM clang | ELF64 | little | REL | RISC-V | `0x5, RVC, double-float ABI` |
| `rv32ec.o` / `rv32imac_tso.o` | VM clang | ELF32 | little | REL | RISC-V | `0x9, RVC, RVE, soft-float ABI` / `0x11, RVC, TSO, soft-float ABI` |
| `m4_hard.elf` | VM gcc 링크 | ELF32 | little | **EXEC** | ARM | `0x5000400, Version5 EABI, hard-float ABI` |
| `m4_soft.elf` | VM gcc 링크 | ELF32 | little | **EXEC** | ARM | `0x5000200, Version5 EABI, soft-float ABI` |

OS/ABI 는 전부 `UNIX - System V`(0) — Linux 용 오브젝트도 0 이다. 이 바이트로 "Linux 용인가 bare-metal 용인가" 는 구별되지 않는다(GNU 확장 기능을 쓸 때만 3 이 된다고 알려져 있음, 확인 권장).

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">ELF 헤더 = 앞 52 B (ELF32) / 64 B (ELF64) — m4_hard.elf 의 실제 바이트</text>
  <text x="10" y="42" fill="currentColor" font-family="monospace">0x00</text>
  <rect x="50" y="28" width="120" height="22" class="f-acc" stroke="currentColor"/><text x="56" y="43" fill="currentColor" font-family="monospace">7f 45 4c 46</text>
  <rect x="170" y="28" width="40" height="22" class="f-ok" stroke="currentColor"/><text x="180" y="43" fill="currentColor" font-family="monospace">01</text>
  <rect x="210" y="28" width="40" height="22" class="f-ok" stroke="currentColor"/><text x="220" y="43" fill="currentColor" font-family="monospace">01</text>
  <rect x="250" y="28" width="40" height="22" class="f-mut" stroke="currentColor"/><text x="260" y="43" fill="currentColor" font-family="monospace">01</text>
  <rect x="290" y="28" width="40" height="22" class="f-mut" stroke="currentColor"/><text x="300" y="43" fill="currentColor" font-family="monospace">00</text>
  <rect x="330" y="28" width="250" height="22" class="f-mut" stroke="currentColor"/><text x="340" y="43" fill="currentColor" font-family="monospace">00 × 8 (ABI 버전 + 패딩)</text>
  <text x="56" y="66" fill="currentColor">매직 \x7fELF</text><text x="166" y="66" fill="currentColor">CLASS</text><text x="210" y="66" fill="currentColor">DATA</text><text x="246" y="66" fill="currentColor">VER</text><text x="288" y="66" fill="currentColor">OSABI</text>
  <text x="10" y="96" fill="currentColor" font-family="monospace">0x10</text>
  <rect x="50" y="82" width="60" height="22" class="f-acc" stroke="currentColor"/><text x="58" y="97" fill="currentColor" font-family="monospace">02 00</text>
  <rect x="110" y="82" width="60" height="22" class="f-acc" stroke="currentColor"/><text x="118" y="97" fill="currentColor" font-family="monospace">28 00</text>
  <rect x="170" y="82" width="110" height="22" class="f-mut" stroke="currentColor"/><text x="178" y="97" fill="currentColor" font-family="monospace">01 00 00 00</text>
  <rect x="280" y="82" width="110" height="22" class="f-ok" stroke="currentColor"/><text x="288" y="97" fill="currentColor" font-family="monospace">b1 80 00 00</text>
  <rect x="390" y="82" width="110" height="22" class="f-mut" stroke="currentColor"/><text x="398" y="97" fill="currentColor" font-family="monospace">34 00 00 00</text>
  <text x="50" y="120" fill="currentColor">e_type=2 EXEC</text><text x="128" y="134" fill="currentColor">e_machine=0x28=40 ARM</text><text x="290" y="120" fill="currentColor">e_entry=0x80b1</text><text x="400" y="120" fill="currentColor">e_phoff</text>
  <text x="10" y="162" fill="currentColor" font-family="monospace">0x20</text>
  <rect x="50" y="148" width="110" height="22" class="f-mut" stroke="currentColor"/><text x="58" y="163" fill="currentColor" font-family="monospace">7c b2 01 00</text>
  <rect x="160" y="148" width="110" height="22" class="f-warn" stroke="currentColor"/><text x="168" y="163" fill="currentColor" font-family="monospace">00 04 00 05</text>
  <rect x="270" y="148" width="60" height="22" class="f-mut" stroke="currentColor"/><text x="278" y="163" fill="currentColor" font-family="monospace">34 00</text>
  <text x="50" y="186" fill="currentColor">e_shoff</text><text x="160" y="186" fill="currentColor">e_flags @0x24 = 0x05000400</text><text x="272" y="200" fill="currentColor">e_ehsize=52</text>
  <text x="160" y="214" fill="currentColor">(EABI 5 | 0x400 hard-float)</text>
  <rect x="440" y="140" width="290" height="80" class="f-acc" stroke="currentColor"/>
  <text x="450" y="160" fill="currentColor" font-weight="bold">ELF64 는 0x18 부터 넓어진다</text>
  <text x="450" y="178" fill="currentColor" font-family="monospace">e_entry 8B @0x18, phoff @0x20</text>
  <text x="450" y="196" fill="currentColor" font-family="monospace">shoff @0x28, e_flags @0x30</text>
  <text x="450" y="214" fill="currentColor" font-family="monospace">e_ehsize @0x34 = 64</text>
  <text x="10" y="246" fill="currentColor">aarch64_be.o 의 e_machine 자리는 "00 b7" — 큰 엔디언이라 앞 바이트가 상위 (= 183).</text>
  <text x="10" y="266" fill="currentColor">m4_hard.o (REL) 의 0x24 는 "00 00 00 05" = 0x05000000 — float 비트 없음.</text>
  <text x="10" y="286" fill="currentColor">ELF32 .o 는 0x34(52) 부터 바로 .text 바이트다 (01 88 = ldrh r1, [r0]).</text>
  <text x="10" y="306" fill="currentColor">이 레슨은 헤더까지만 읽는다. 섹션 표·심볼은 TOOL-A11, 섹션 의미는 TOOL-B04.</text>
</svg>
```

### Part A 풀이 — 같은 소스, 다른 명령어

`scale()` (`(float)s->raw * s->gain`) 의 실제 디스어셈블리 (`cross_disasm.txt` 발췌, Mac `/usr/bin/objdump -d` / VM `llvm-objdump -d`, 모두 `-Os`):

```text
aarch64_linux.o                       m4_hard.o (Cortex-M4, -mfloat-abi=hard)
 0: ldr   h0, [x0]                     0: ldrh  r1, [r0]
 4: ldr   s1, [x0, #0x4]               2: vldr  s2, [r0, #4]
 8: ucvtf s0, s0                       6: vmov  s0, r1
 c: fmul  s0, s1, s0                   a: vcvt.f32.u32 s0, s0
10: ret                               e: vmul.f32 s0, s2, s0
                                      12: bx    lr
m4_soft.o (-mfloat-abi=soft)          rv32imac.o (F 확장 없음)
 0: push  {r4, lr}                     0: addi  sp, sp, -0x10
 2: mov   r4, r0                       … (ra, s0 저장)
 4: ldrh  r0, [r0]
 6: bl    0x6 <scale+0x6>   ← ui2f     8: lhu   a0, 0x0(a0)
 a: ldr   r1, [r4, #0x4]               c: auipc ra, 0x0 / 10: jalr ra  ← __floatunsisf
 … (r0/r1 교환)                       14: lw    a1, 0x4(s0)
12: bl    0x12 <scale+0x12> ← fmul    1c: auipc ra, 0x0 / 20: jalr ra  ← __mulsf3
16: pop   {r4, pc}                     … 2a: ret
rv32imafc.o (ilp32f)                  x86_64_macho.o
 0: lhu   a1, 0x0(a0)                  0: pushq %rbp ; movq %rsp, %rbp
 4: flw   fa5, 0x4(a0)                 4: movzwl (%rdi), %eax
 6: fcvt.s.wu fa4, a1                  7: cvtsi2ss %eax, %xmm0
 a: fmul.s fa0, fa5, fa4               b: mulss 0x4(%rdi), %xmm0
 e: ret                               10: popq %rbp ; 11: retq
```

| 타깃 | `scale` 크기 | float 곱셈은 | 결과 반환 레지스터 | 미해결 심볼 (`nm`) |
|---|---|---|---|---|
| aarch64 | 0x14 = 20 B | `fmul` (FP 명령) | `s0` | 없음 |
| Cortex-M4 hard | 0x14 = 20 B | `vmul.f32` (FPv4-SP) | `s0` — **hard-float 호출 규약** | 없음 |
| Cortex-M4 soft | 0x18 = 24 B | `bl __aeabi_fmul` | `r0` — 정수 레지스터 | `U __aeabi_fmul`, `U __aeabi_ui2f` |
| rv32imac | 0x2c = 44 B | `call __mulsf3` (auipc+jalr) | `a0` | `U __floatunsisf`, `U __mulsf3` |
| rv32imafc / ilp32f | 0x10 = 16 B | `fmul.s` | `fa0` | 없음 |
| x86-64 Mach-O | 0x12 = 18 B | `mulss` (SSE) | `xmm0` | 없음 |

읽을거리:

- **soft-float 는 "FPU 를 안 쓴다" 이상으로 함수 호출 규약이 다르다.** hard 는 float 인자·반환을 `s0` 로, soft 는 `r0` 로 주고받는다. 그래서 둘을 섞어 링크하면 값이 엉뚱한 레지스터에 있게 된다 → Part C. (`-mfloat-abi=softfp` 는 FPU 명령을 쓰되 호출 규약은 soft: `gcc_m4_softfp.o` 는 `Tag_FP_arch: VFPv4-D16` 은 있고 `Tag_ABI_VFP_args` 는 없었다 — `cross_arm_attr.txt`.)
- soft-float 도우미 이름이 아키텍처마다 다르다: ARM EABI 는 `__aeabi_fmul` / `__aeabi_ui2f`, RISC-V 는 libgcc 표준 이름 `__mulsf3` / `__floatunsisf`. 이 함수들은 **libgcc 또는 compiler-rt builtins** 에 있다 → Part B 의 "builtins 없음" 이 바로 이것.
- `bl 0x6 <scale+0x6>` 처럼 자기 자신을 가리키는 호출은 `.o` 의 **재배치 구멍** 이다 (TOOL-B04). `objdump -r objs/m4_soft.o` 실측: `00000006 R_ARM_THM_CALL __aeabi_ui2f`, `00000012 R_ARM_THM_CALL __aeabi_fmul`.
- `checksum()` 의 회전 `(sum << 1 | sum >> 31)`: AArch64 `eor w0, w10, w0, ror #31`, Thumb-2 `eor.w r0, r3, r0, ror #31` (회전이 eor 의 피연산자에 접힘), x86 `roll %esi`, RISC-V 기본 ISA 는 회전이 없어 `srli` / `slli` / `or` 세 개 (Zbb 확장에 `ror`/`rori` 가 있다 — 확인 권장). Cortex-M4 루프 앞의 `nop` 은 루프 시작(0x1c)을 4 B 경계에 맞추는 패딩으로 보인다.
- 크기 비교는 명령어 밀도 감각만 준다(ARM-B12 의 Thumb 밀도 참고). x86 Mach-O 는 Darwin 기본값으로 프레임 포인터(`push rbp`)를 남겼다.

**코드 — `rd16` / `rd32` / `machine_name`.** 핵심은 "파일의 엔디언(EI_DATA)대로 조립" 이다:

```c
return big ? (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]
           : (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
```

같은 바이트 `00 00 00 05` 가 LSB 로는 `0x05000000` (ARM e_flags, EABI 5), MSB 로는 `5` — 체크 2·3번이 이것을 확인한다. 주어진 `elf_target_info` 는 `e_type`@16, `e_machine`@18 을 `rd16` 으로, `e_flags` 를 `bits == 64 ? 48 : 36` 에서 `rd32` 로 읽는다. ELF64 인 `rv64gc.o` 의 0x30 바이트가 `05 00 00 00` 이라 Flags 0x5 와 맞는다 (36 에서 읽으면 `e_shoff` 의 상위 절반 0 이 나온다).

### Part B 풀이 — 링크가 실패하는 이유, 하나씩

컴파일(`-c`)은 "이 소스 → 이 ISA 의 명령어" 로 끝나지만, 링크는 **타깃용 파일들**을 모아야 한다:

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">펌웨어 링크에 들어가는 것 (arm-none-eabi-gcc + nano / nosys specs)</text>
  <rect x="10" y="34" width="150" height="34" class="f-warn" stroke="currentColor"/><text x="18" y="50" fill="currentColor">crt0.o</text><text x="18" y="63" fill="currentColor" font-size="11">시작 코드 → main</text>
  <rect x="10" y="74" width="150" height="34" class="f-ok" stroke="currentColor"/><text x="18" y="90" fill="currentColor">main.o, target.o</text><text x="18" y="103" fill="currentColor" font-size="11">내 코드 (Mac 에서도 됨)</text>
  <rect x="10" y="114" width="150" height="34" class="f-warn" stroke="currentColor"/><text x="18" y="130" fill="currentColor">libc_nano.a</text><text x="18" y="143" fill="currentColor" font-size="11">newlib-nano (+ 헤더)</text>
  <rect x="10" y="154" width="150" height="34" class="f-warn" stroke="currentColor"/><text x="18" y="170" fill="currentColor">libgcc.a / builtins</text><text x="18" y="183" fill="currentColor" font-size="11">__aeabi_fmul 등</text>
  <rect x="10" y="194" width="150" height="34" class="f-warn" stroke="currentColor"/><text x="18" y="210" fill="currentColor">libnosys.a</text><text x="18" y="223" fill="currentColor" font-size="11">_sbrk _write … 스텁</text>
  <path d="M160 51 L300 120 M160 91 L300 120 M160 131 L300 125 M160 171 L300 130 M160 211 L300 132" stroke="currentColor" fill="none"/>
  <rect x="300" y="100" width="150" height="44" class="f-acc" stroke="currentColor"/><text x="310" y="118" fill="currentColor">ELF 링커</text><text x="310" y="134" fill="currentColor" font-size="11">GNU ld / ld.lld</text>
  <path d="M450 122 L520 122" stroke="currentColor" stroke-width="2"/>
  <rect x="520" y="100" width="210" height="44" class="f-ok" stroke="currentColor"/><text x="530" y="118" fill="currentColor">m4_hard.elf (EXEC)</text><text x="530" y="134" fill="currentColor" font-size="11">text 3356 data 108 bss 504</text>
  <text x="300" y="172" fill="currentColor" font-size="11">메모리 배치 = 링커 스크립트 (TOOL-M01)</text>
  <text x="300" y="200" fill="currentColor">주황 = Mac 에 타깃용이 없는 것.</text>
  <text x="300" y="218" fill="currentColor">sysroot = 이 중 헤더·libc·crt 를 담은 "타깃의 / 디렉터리".</text>
  <text x="300" y="236" fill="currentColor">Mac 의 ld64 는 Mach-O 만 만든다 → 링커 칸부터 막힌다.</text>
</svg>
```

실제로 하나씩 막아 본 결과 (`cross_link.txt`, Mac = Apple clang 21, VM = Ubuntu clang 21.1.8 / arm-none-eabi-gcc 14.2.1):

| # | 시도 (어디서) | 실제 오류 문장 | 빠진 것 |
|---|---|---|---|
| 1 | `clang --target=aarch64-linux-gnu main.c target.c` (Mac) | `ld: unknown options: -EL --hash-style=gnu --eh-frame-hdr -dynamic-linker --as-needed …` | **ELF 링커**. 드라이버는 GNU ld 용 옵션으로 Apple `ld`(ld64)를 불렀다 |
| 2 | `#include <stdio.h>` + `--target=aarch64-linux-gnu -c` (Mac) | `fatal error: 'stdio.h' file not found` | 타깃 **libc 헤더** (sysroot) |
| 3 | `clang --target=thumbv7em-none-eabi … main.c target.c` (Mac) | `ld: unknown options: -Bstatic -EL --start-group --end-group --target2=rel` | ELF 링커 + `crt0.o` + `-lc` + `libclang_rt.builtins.a` |
| 4 | 위 + `-fuse-ld=lld` (Mac) | `clang: error: invalid linker name in argument '-fuse-ld=lld'` | Apple CLT 에 lld 가 없다 |
| 5 | `ls …/lib/clang/21/lib/` (Mac) | `darwin` `macho_embedded` 뿐 | thumbv7em 용 compiler-rt 없음 |
| 6 | `clang --target=riscv32-unknown-elf … main.c target.c` (VM) | `/usr/bin/aarch64-linux-gnu-ld.bfd: unrecognised emulation mode: elf32lriscv` | 호스트 GNU ld 는 RISC-V 를 모른다 |
| 7 | 위 + `-fuse-ld=lld` (VM) | `ld.lld: error: cannot open crt0.o` / `unable to find library -lgcc` / `-lc` | 링커는 생겼고, 런타임·libc 가 없다 |
| 8 | 위 + `-nostdlib` (VM) | `ld.lld: error: undefined symbol: __floatunsisf` / `__mulsf3` `>>> referenced by target.c … (scale)` | soft-float **builtins** (Part A 의 그 두 함수) |
| 9 | `arm-none-eabi-gcc -mcpu=cortex-m4 … main.c target.c` (VM, specs 없음) | ``undefined reference to `_exit'`` (`_sbrk`, `_read`, `_write`, `_close`, `_lseek` 도) | newlib 이 기대하는 **시스템 콜 스텁** — 보드 지원 코드의 몫 |
| 10 | 위 + `--specs=nano.specs --specs=nosys.specs` | 성공 (경고: `_close is not implemented and will always fail` 등 4개) | — |
| 13 | `riscv64-linux-gnu-gcc main.c target.c` → `qemu-riscv64 -L /usr/riscv64-linux-gnu ./rv64.elf` | 성공, 종료 코드 2 | — (완전한 Linux 크로스 툴체인) |

읽는 법:

- **1·3 은 같은 실패다.** `-v` 로 본 실제 링크 명령: `"/Library/Developer/CommandLineTools/usr/bin/ld" -EL --hash-style=gnu --eh-frame-hdr -m aarch64linux -pie -dynamic-linker /lib/ld-linux-aarch64.so.1 -o … Scrt1.o crti.o crtbeginS.o -L/usr/lib … -lgcc --as-needed -lgcc_s --no-as-needed -lc -lgcc … crtendS.o crtn.o`. 드라이버는 **Linux 용 링크 명령을 정확히 만들었다** — `Scrt1.o`, `crti.o`, `-lc`, 동적 로더 경로까지. 다만 그 명령을 받을 GNU 호환 링커도, 그 파일들도 Mac 에 없다. ld64 가 첫 옵션에서 멈춰서 "파일 없음" 오류까지 가지도 못했다.
- **sysroot** = 타깃 시스템의 루트 디렉터리 사본(`usr/include`, `usr/lib`, `crt*.o`, `libc.so/.a`). `--sysroot=<dir>` 를 주면 드라이버가 헤더·라이브러리를 그 아래에서 찾는다. `--gcc-toolchain=<prefix>` 는 clang 에게 "GCC 설치(crtbegin.o, libgcc, libstdc++)는 여기" 라고 알려 준다. VM 에서는 `clang --target=riscv64-linux-gnu` 가 플래그 없이도 링크됐다(#14) — `-###` 를 보면 `"/usr/bin/riscv64-linux-gnu-ld"`, `…/gcc-cross/riscv64-linux-gnu/15/…/Scrt1.o`, `-L/usr/lib/gcc-cross/riscv64-linux-gnu/15` 를 스스로 찾았다(Debian 의 cross 패키지 경로 자동 탐지).
- **arm-none-eabi-gcc 가 newlib 을 싣고 오는 이유**: `none` = OS 가 없으니 "시스템 libc" 도 없다. 그래서 툴체인이 MCU 용 C 라이브러리(newlib / newlib-nano)를 **multilib** 으로 — 코어·FPU·float ABI 조합마다 따로 빌드해 — 들고 온다. 실측: `-mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16 -print-multi-directory` → `thumb/v7e-m+fp/hard`, soft 는 `thumb/v7e-m/nofp/libc_nano.a`. 이 Debian 패키지는 `-print-sysroot` 가 빈 문자열이고 prefix 기준 상대 경로(`/usr/lib/gcc/arm-none-eabi/14.2.1/../../../arm-none-eabi/lib/…`)로 찾는다. 반면 `riscv64-linux-gnu-gcc -print-sysroot` 는 `/`.
- newlib 은 I/O 를 `_write`, 힙을 `_sbrk` 같은 **보드가 구현할 함수**로 미룬다. `nosys.specs` 는 그걸 "항상 실패" 스텁(libnosys)으로 채워 링크만 되게 한다 — 실제 보드에선 UART 로 가는 `_write`, 링커 스크립트의 힙 심볼을 쓰는 `_sbrk` 를 직접 쓴다 (TOOL-M01, RTOS-B 트랙의 printf 리타깃).
- 13번이 "완전한 툴체인" 의 모습이다: 크로스 컴파일러 + 타깃 sysroot(`/usr/riscv64-linux-gnu`) + 에뮬레이터. `./rv64.elf` 를 바로 실행하면 binfmt 가 qemu 를 부르지만 `Could not open '/lib/ld-linux-riscv64-lp64d.so.1'` 로 실패했다(실측) — 동적 로더를 sysroot 에서 찾으라고 `-L` 을 줘야 한다.

**코드 — `elf_check_ident()`.** 판정 순서가 곧 사양이다:

| 입력 (테스트) | 반환 | 이유 |
|---|---|---|
| `NULL`, `n = 0`, `n = 3` | `TI_ERR_SHORT` | 매직조차 못 본다 |
| `objs/arm64_macho.o`, `x86_64_macho.o` (`cf fa ed fe …`) | `TI_ERR_MACHO` | 친절한 오류: "Mac 기본 타깃으로 빌드된 것" — CI 에서 가장 흔한 실수 |
| 1번 바이트를 `'e'` 로 | `TI_ERR_MAGIC` | |
| ELF 매직 + `n = 8` | `TI_ERR_SHORT` | 매직은 맞지만 e_ident 16 B 미만 |
| EI_CLASS = 3 / EI_DATA = 0 | `TI_ERR_CLASS` / `TI_ERR_DATA` | 유효 값은 1, 2 뿐 |
| `rv32imac.o` 51 B / 52 B | SHORT / 성공(52) | ELF32 헤더 52 B |
| `rv64gc.o` 63 B | SHORT | ELF64 헤더 64 B |
| `aarch64_be.o` | 성공(64), `big_endian = 1`, `e_machine = 183` | `00 b7` 을 MSB 로 |

### Part C 풀이 — float ABI, 그리고 헤더의 한계

**ARM: `.o` 에는 float 비트가 없다 (실측).** 위 표에서 clang 이든 gcc 든 hard / soft `.o` 의 Flags 는 `0x5000000` 로 같았다. 차이는 `.ARM.attributes` 섹션에 있다 (`cross_arm_attr.txt`, VM `readelf -A`):

```text
=== m4_hard.o                          === m4_soft.o
  Tag_CPU_name: "cortex-m4"              Tag_CPU_name: "cortex-m4"
  Tag_CPU_arch: v7E-M                    Tag_CPU_arch: v7E-M
  Tag_CPU_arch_profile: Microcontroller  Tag_CPU_arch_profile: Microcontroller
  Tag_FP_arch: VFPv4-D16                 (FP 관련 태그 없음)
  Tag_ABI_HardFP_use: SP only
  Tag_ABI_VFP_args: VFP registers
```

GNU ld 는 이 태그를 합치면서 결과 실행 파일에 `0x400`(hard) / `0x200`(soft)을 찍는다 — 그래서 `m4_hard.elf` = `0x5000400`, `m4_soft.elf` = `0x5000200`. 같은 프로그램인데 soft 쪽은 `text 4236` vs hard `3356` B (`arm-none-eabi-size`, 실측): float 도우미 함수가 약 880 B 를 더 가져왔다.

**섞으면 어떻게 되나 (실제 출력):**

```text
$ arm-none-eabi-ld main_m4_soft.o gcc_m4_hard.o -e main -o mix.elf
arm-none-eabi-ld: error: gcc_m4_hard.o uses VFP register arguments, mix.elf does not
arm-none-eabi-ld: failed to merge target specific data of file gcc_m4_hard.o
[rc=1]
$ ld.lld main_m4_soft.o gcc_m4_hard.o -e main -o mix_lld.elf
[rc=0]
$ riscv64-linux-gnu-ld -m elf32lriscv -r main_rv_f.o rv32imac.o -o mixrv.o
/usr/bin/riscv64-linux-gnu-ld.bfd: rv32imac.o: can't link soft-float modules with single-float modules
$ ld.lld -r main_rv_f.o rv32imac.o -o mixrv2.o
ld.lld: error: rv32imac.o: cannot link object files with different floating-point ABI from main_rv_f.o
```

- GNU ld 는 ARM attributes 로 거부했다. **ld.lld 21 은 이 ARM 조합을 오류 없이 링크했다**(실측, 결과 Flags `0x5000400`) — lld 가 `Tag_ABI_VFP_args` 불일치를 검사하지 않는 것으로 보인다(버전별 차이 가능, 확인 권장). lld 로 넘어가는 프로젝트라면 이 검사를 CI 에 따로 두는 것이 안전하다 — Part C 의 `ci_gate` 가 그 자리다.
- RISC-V 는 float ABI 가 **헤더에 있으니** 두 링커 모두 거부했다. 반면 RVC 차이(`rv32ima.o` 0x0 + `rv32imac.o` 0x1)는 둘 다 링크됐고 결과 Flags 는 `0x1, RVC, soft-float ABI` — 플래그를 OR 한 것이다.

**코드 — `decode_flags` / `link_check` / `ci_gate`:**

| 쌍 (테스트) | `link_check` | 실제 링커 |
|---|---|---|
| `rv32imac.o` + `rv32imafc.o` | `LINK_ERR_FLOAT` | bfd·lld 모두 오류 |
| `rv32ima.o` + `rv32imac.o` | `LINK_OK` | 둘 다 성공 |
| `gcc_m4_hard.o` + `m4_soft.o` | `LINK_UNKNOWN` | bfd 오류, lld 통과 → 헤더만으로는 모름 |
| `m4_hard.elf` + `m4_soft.elf` | `LINK_ERR_FLOAT` | (실행 파일끼리는 링크하지 않지만 ABI 가 다름을 헤더가 증명) |
| `aarch64_linux.o` + `aarch64_be.o` | `LINK_ERR_ENDIAN` | |
| `rv32imac.o` + `rv64gc.o` | `LINK_ERR_CLASS` | |
| `m4_hard.o` + `rv32imac.o` | `LINK_ERR_MACHINE` | |

비교 순서(machine → class → endian → float)는 "가장 근본적인 차이부터 보고" 하려는 것이다. ARM 쪽 `FABI_UNKNOWN` 이 하나라도 있으면 `LINK_UNKNOWN` — 헤더가 모르는 것을 OK 라고 하지 않는 것이 이 함수의 핵심이다. `ci_gate` 는 Cortex-M4 hard 이미지 기준으로 `m4_hard.o`, `gcc_m4_hard.o` 를 "모름 2", `m4_soft.elf`(float), `aarch64_linux.o`, `rv32imac.o`(machine)를 "깨짐 3" 으로 센다. RISC-V 이미지(rv32imafc) 기준으로는 Mach-O 를 포함해 4개가 깨짐, 모름 0. 실전에서는 UNKNOWN 이 나오면 `readelf -A` 로 `Tag_ABI_VFP_args` 를 확인하는 다음 단계로 넘긴다(attributes 파싱은 TOOL-A11 의 섹션 파서 위에서).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/TOOL-B14_x && /tmp/TOOL-B14_x     # 46/46 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o /tmp/TOOL-B14_xa && /tmp/TOOL-B14_xa   # 46/46, 보고 없음
```

## 7. 자주 하는 실수

**1. `-c` 가 되니까 크로스 컴파일이 된다고 생각한다.** 컴파일러 백엔드만 있으면 `.o` 는 나온다. 링크에는 ELF 링커 + crt + libc + builtins 가 필요하고, Mac 에는 하나도 없다(Part B 표 1 ~ 5). → 오브젝트만 Mac 에서, 링크는 arm-none-eabi-gcc / 크로스 툴체인(또는 Docker·VM)에서.

**2. 헤더를 구조체 포인터로 읽는다.** `((Elf32_Ehdr *)buf)->e_flags` 는 정렬·엔디언·별칭 규칙에 걸린다. `aarch64_be.o` 를 Mac(LSB)에서 그렇게 읽으면 `e_machine` 이 `0xb700` 이 된다. → 바이트 단위 조립 (`rd16` / `rd32`).

**3. ELF64 의 e_flags 를 36 에서 읽는다.** ELF32 오프셋을 그대로 쓰면 `rv64gc.o` 에서 0 이 나와 "soft-float, RVC 없음" 으로 오판한다. → `bits == 64 ? 48 : 36`, 헤더 크기 검사도 52 / 64 로 따로.

**4. ARM `.o` 의 `0x400` 비트로 hard-float 여부를 판단한다.** `.o` 는 clang·gcc 모두 비트가 없다(실측 `0x5000000`). 없다고 soft 로 단정하면 hard 라이브러리를 soft 로 분류한다. → `.o` 는 UNKNOWN, 확인은 `readelf -A` (`Tag_ABI_VFP_args: VFP registers`).

**5. 툴체인마다 float ABI 를 다르게 준다.** 앱은 `-mfloat-abi=hard`, 벤더 DSP 라이브러리는 `softfp` 로 빌드되면 `uses VFP register arguments` 로 링크가 깨지거나(GNU ld) **lld 에선 조용히 링크돼 float 인자가 엉뚱한 레지스터로 간다**. → CPU·FPU·float ABI 플래그를 한 변수(`ARCH_FLAGS`)로 모든 타깃과 라이브러리에 같이 넘기고, CI 에서 검사.

**6. `--target` 만 바꾸고 `-mcpu` / `-march` / `-mabi` 를 빼먹는다.** `thumbv7em-none-eabi` 만으로는 FPU 가 정해지지 않고, `riscv32` 는 `-march` 기본값이 툴체인마다 다르다. 결과 `.o` 를 `readelf -h` / `-A` 로 한 번 확인하는 습관이 가장 싸다.

## 8. 면접 Q&A

<details>
<summary>Q1. arm-none-eabi 의 각 부분은 무슨 뜻인가? aarch64-linux-gnu 와 무엇이 다른가?</summary>

트리플은 arch-vendor-os-env. `arm` = 32비트 ARM, `none` = OS 없음(bare-metal) — vendor 는 생략, `eabi` = ARM Embedded ABI(호출 규약·자료형 크기·정렬 규칙). `aarch64-linux-gnu` 는 64비트 ARM, OS = Linux, env = glibc 의 GNU ABI. 차이는 libc 와 시작 코드에서 드러난다: Linux 타깃은 `Scrt1.o crti.o … -lc` 와 동적 로더(`/lib/ld-linux-aarch64.so.1`)를 쓰고, `none` 은 `crt0.o` + newlib 을 정적으로 쓴다(Part B `-v` 실측). clang 은 이름을 정규화한다 — `thumbv7em-unknown-none-eabi`, `riscv32-unknown-unknown-elf` (실측 경로·오류문).

</details>

<details>
<summary>Q2. sysroot 가 무엇이고, 없으면 무엇이 실패하나?</summary>

타깃 시스템의 루트 파일시스템 중 빌드에 필요한 부분(헤더, libc, crt 오브젝트, 동적 로더)의 사본. `--sysroot=` 로 지정하면 드라이버가 `<sysroot>/usr/include`, `<sysroot>/usr/lib` 를 기준으로 찾는다. 없으면 컴파일 단계에서 `'stdio.h' file not found`, 링크 단계에서 crt / `-lc` 를 못 찾는다(Mac 실측 #2, VM lld 실측 #7). freestanding 코드(`<stdint.h>` 같은 컴파일러 제공 헤더만)는 sysroot 없이 `-c` 까지 된다 — 이 레슨의 `target.c` 처럼.

</details>

<details>
<summary>Q3. 크로스 컴파일러가 있는데 왜 libgcc / compiler-rt 가 또 필요한가?</summary>

컴파일러가 직접 명령으로 못 만드는 연산을 함수 호출로 바꾸기 때문이다. Cortex-M4 soft-float 의 `float *` → `__aeabi_fmul`, `unsigned → float` → `__aeabi_ui2f`, RISC-V rv32imac 은 `__mulsf3` / `__floatunsisf` (실측 `nm` 의 `U`). M 확장 없는 RISC-V 나 Cortex-M0 의 나눗셈(`__aeabi_uidiv`), 64비트 시프트 등도 같다. 이 함수들은 libc 가 아니라 컴파일러 런타임 라이브러리에 있다 — `-nostdlib` 로 빼면 `undefined symbol: __floatunsisf` 가 난다(실측 #8). 그래서 펌웨어 링크 줄에 `-lgcc` 가 붙는다.

</details>

<details>
<summary>Q4. -mfloat-abi=soft / softfp / hard 의 차이와, 섞으면 생기는 일은?</summary>

soft: FPU 명령 안 씀 + float 인자는 정수 레지스터. softfp: FPU 명령은 씀 + 호출 규약은 soft(정수 레지스터) — 그래서 soft 와 링크 호환. hard: FPU 명령 + float 인자를 `s0..s15` 로. hard 와 soft/softfp 를 섞으면 호출자는 `r0` 에, 피호출자는 `s0` 에서 값을 찾는다. GNU ld 는 `.ARM.attributes` 를 보고 `X uses VFP register arguments, Y does not` 로 거부한다(실측). ld.lld 21 은 같은 조합을 통과시켰다(실측, 확인 권장) — 그러면 런타임에 엉뚱한 값이 나온다. 벤더 바이너리 라이브러리(예: 블루투스 스택, DSP 라이브러리)를 받을 때 첫 확인 항목이다.

</details>

<details>
<summary>Q5. `.o` 파일이 어느 타깃용인지 30초 안에 어떻게 확인하나?</summary>

`file x.o` (형식·비트·엔디언·머신), `readelf -h x.o` (Machine, Flags), ARM 이면 `readelf -A x.o` (Tag_CPU_name, Tag_FP_arch, Tag_ABI_VFP_args). Mac 에선 readelf 대신 `/usr/bin/objdump -f` 로 아키텍처는 보이지만 Flags 해석은 없다. 아카이브면 `readelf -h lib.a` 가 멤버마다(`File: t.a(rv32imac.o)` …) 출력한다(VM 실측). RISC-V 는 Flags 에 RVC·float ABI 가 바로 나온다(`0x3, RVC, single-float ABI`). 이 레슨의 `elf_target_info` 가 그 첫 두 단계를 코드로 한 것.

</details>

<details>
<summary>Q6. arm-none-eabi-gcc 는 왜 newlib 을 같이 배포하나? --specs=nano.specs / nosys.specs 는?</summary>

OS 가 없는 타깃엔 "시스템 libc" 가 없으니 툴체인이 들고 와야 한다. 그리고 코어·FPU·float ABI 조합마다 따로 빌드한 multilib 으로 온다(`thumb/v7e-m+fp/hard` 실측). `nano.specs` 는 크기 최적화판 newlib-nano(`libc_nano.a`) 로 바꾸고, `nosys.specs` 는 `_write` / `_sbrk` / `_exit` 같은 시스템 콜 자리를 "실패하는 스텁" 으로 채운다. 이 둘 없이 링크하면 ``undefined reference to `_exit'`` 등이 난다(실측 #9). 실제 제품에선 `_write` 를 UART 로, `_sbrk` 를 링커 스크립트 힙으로 직접 구현한다.

</details>

<details>
<summary>Q7. Mac 에서 펌웨어를 빌드하는 CI 를 꾸린다면?</summary>

선택지: (1) Arm GNU Toolchain(arm-none-eabi-gcc, macOS 용 배포판) — 컴파일러·binutils·newlib 일체, 가장 흔함. (2) LLVM 계열 — clang + ld.lld + 타깃용 libc/compiler-rt 를 묶은 배포판(예: Arm 의 LLVM Embedded Toolchain, 확인 권장). Apple CLT 의 clang 만으로는 링크가 안 된다(Part B). (3) Docker / Linux VM 에서 빌드. 어떤 경우든 `ARCH_FLAGS` 를 한곳에서 관리하고, 산출물 `.elf` 의 `readelf -h` Flags 와 `-A` 를 CI 에서 검사하고, 툴체인 버전을 고정한다(TOOL-M05 툴체인 파일, TOOL-A06 재현 가능한 빌드).

</details>

<details>
<summary>Q8. e_entry 가 0x80b1 인데 홀수다. 잘못된 것 아닌가?</summary>

ARM/Thumb 에서 주소 비트 0 은 "이 주소로 점프할 때 Thumb 상태" 표시다. Cortex-M 은 Thumb 만 실행하므로 엔트리와 벡터 테이블의 핸들러 주소는 **항상 홀수**여야 한다 — 짝수 주소로 `bx` 하면 Cortex-M 은 UsageFault(INVSTATE)를 낸다. 실제 명령은 0x80b0 에 있다. 벡터 테이블을 손으로 채울 때 `| 1` 을 빼먹는 버그가 고전이다(BM-B08). 추가 연습 1 이 이것을 디코드한다.

</details>

## 9. 추가 연습

### 연습 1 — 엔트리 주소와 Thumb 비트

`elf_entry(buf, n, &addr, &thumb)`: ELF32 / ELF64 의 `e_entry`(0x18, 4 / 8 B)를 읽고 ARM 이면 비트 0 을 떼어 `thumb` 로 돌려줘라. `m4_hard.elf` → `0x80b0`, thumb 1. `.o` 는 0.

<details>
<summary>답 보기</summary>

64비트는 `rd32` 두 번을 엔디언에 맞게 합친다 (`rd64`). Thumb 비트는 ARM 에서만 의미가 있다 — AArch64·RISC-V 의 홀수 엔트리는 그냥 이상한 값이다. readelf 는 `Entry point address: 0x80b1` 을 그대로 보여 준다(실측). 이 값을 디버거 브레이크포인트 주소로 쓸 때는 0x80b0.

</details>

### 연습 2 — Mach-O 면 어떤 CPU 인가

`macho_cpu(buf, n, &filetype)`: 64비트 Mach-O(`0xFEEDFACF`, 리틀 엔디언)면 `cputype` 을 `"arm64"` (0x0100000C) / `"x86_64"` (0x01000007) 로, `filetype` 을 돌려준다(1 = MH_OBJECT). Mac `otool -hv objs/*.o` 실측: `MH_MAGIC_64 ARM64 ALL 0x00 OBJECT 4 280 SUBSECTIONS_VIA_SYMBOLS`, `MH_MAGIC_64 X86_64 …`.

<details>
<summary>답 보기</summary>

Mach-O 헤더: magic(4) · cputype(4) · cpusubtype(4) · filetype(4) · ncmds · sizeofcmds · flags · reserved. cputype 의 0x01000000 = `CPU_ARCH_ABI64`. 바이트 `0c 00 00 01` = 0x0100000C. CI 의 "누가 Mach-O 를 커밋했나" 메시지를 "x86_64 Mac 용 오브젝트(Rosetta 빌드?)" 처럼 구체적으로 만들 수 있다.

</details>

### 연습 3 — readelf 의 Flags 줄을 그대로 재현

`readelf_flags(t, out, sz)`: `cross_readelf_h.txt` 의 9가지 Flags 문자열(`0x0`, `0x5000000, Version5 EABI`, `…, hard-float ABI`, `0x9, RVC, RVE, soft-float ABI`, `0x11, RVC, TSO, soft-float ABI` …)과 **바이트 단위로 같게** 만들어라.

<details>
<summary>답 보기</summary>

함정 둘. (1) `printf("%#x", 0)` 은 `0` 을 찍는다(`0x` 가 안 붙음, 셸 `printf '%#x|%#x' 0 5` 실측 `0|0x5`, C 의 printf 도 같은 규칙) → `"0x%x"`. (2) readelf 는 e_flags 가 0 이면 아무 해석도 붙이지 않는다 — `rv32ima.o` 는 soft-float 인데 `0x0` 만 나온다(실측). RISC-V 순서는 RVC → RVE → TSO → float ABI (실측 두 줄과 일치). 스크립트에서 readelf 출력과 내 도구 출력을 `diff` 로 맞추는 회귀 테스트의 기본기.

</details>

### 연습 4 — 헤더에서 -mabi 되짚기

`riscv_mabi(t)`: RISC-V 헤더에서 컴파일 때 준 `-mabi` 를 복원하라 — `rv32imac.o` → `ilp32`, `rv32imafc.o` → `ilp32f`, `rv64gc.o` → `lp64d`, `rv32ec.o` → `ilp32e`. ARM 이면 NULL.

<details>
<summary>답 보기</summary>

`EF_RISCV_RVE`(0x8)면 `ilp32e`/`lp64e`, 아니면 클래스(32/64) × float ABI(0/2/4/6 → 없음/f/d/q). 거꾸로는 안 된다: `-march` 의 확장 목록(m, a, zbb …)은 헤더에 없고 `.riscv.attributes` 의 `Tag_RISCV_arch` 문자열에 있다(확인 권장, 섹션 파싱 = TOOL-A11). 즉 ABI 는 헤더로, ISA 는 attributes 로.

</details>

### 연습 5 — 헤더 자기 일관성 검사

`elf_sanity(buf, n)`: `EI_VERSION == 1`, `e_version == 1`(0x14), `e_ehsize`(ELF32 0x28 / ELF64 0x34)가 52 / 64 와 일치하는지. 깨진 사본 3개(ehsize 64 인 ELF32, e_version 2, EI_VERSION 0)를 거부하라.

<details>
<summary>답 보기</summary>

실측 `Size of this header: 52 (bytes)` / `64 (bytes)`. 이 검사는 "잘린 다운로드·잘못된 오프셋으로 읽은 버퍼" 를 싸게 걸러 낸다 — 플래시에 올라간 이미지 헤더를 부트로더가 검증할 때(매직 + 버전 + 헤더 크기 + CRC)와 같은 생각이다(SIV 트랙의 이미지 검증).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/TOOL-B14_p && /tmp/TOOL-B14_p      # 30/30 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined practice.c -o /tmp/TOOL-B14_pa && /tmp/TOOL-B14_pa   # 30/30, 보고 없음
```

## 10. 핵심 정리

- 트리플 = arch-vendor-os-env (`thumbv7em-none-eabi`, `aarch64-linux-gnu`, `riscv32-unknown-elf`). CPU 세부는 `-mcpu` / `-mfpu` / `-mfloat-abi` / `-march` / `-mabi`. os 가 darwin 이면 Mach-O, 그 외 ELF.
- Apple clang 은 ARM / AArch64 / x86 **컴파일러**(RISC-V 없음)지만 **툴체인은 아니다**: ELF 링커(ld64 는 Mach-O 전용, lld 없음) · crt · libc · 헤더 · compiler-rt 가 없어 `-c` 까지만 된다. 실제 오류: `ld: unknown options: -EL --hash-style=gnu …`.
- 링크 재료 = crt0/crt1 + 내 `.o` + libc + libgcc/builtins (+ 링커 스크립트). sysroot 는 헤더·libc·crt 를 담은 타깃 루트, `--gcc-toolchain` 은 GCC 설치 위치. arm-none-eabi-gcc 는 newlib 을 multilib 으로 싣고 오고, `--specs=nano.specs --specs=nosys.specs` 가 libc 크기와 시스템 콜 스텁을 정한다.
- ELF 헤더: 매직 `7f 45 4c 46`, EI_CLASS(1/2) · EI_DATA(1 LSB / 2 MSB), e_type@16, e_machine@18 (ARM 40, x86-64 62, AArch64 183, RISC-V 243), e_flags@36 (ELF32) / @48 (ELF64), 헤더 52 / 64 B. 바이트 조립으로 읽는다.
- ARM e_flags: 상위 8비트 = EABI 5. `0x400` hard / `0x200` soft 는 **링크된 실행 파일에만** — `.o` 는 `.ARM.attributes` (`Tag_ABI_VFP_args`). RISC-V e_flags: 0x1 RVC, 0x6 float ABI(0/2/4/6), 0x8 RVE, 0x10 TSO — `.o` 에도 있다.
- float ABI 혼합: GNU ld 는 거부(`uses VFP register arguments` / `can't link soft-float modules with single-float modules`), ld.lld 21 은 ARM 혼합을 통과시켰다(실측). CI 에서 헤더 + attributes 로 검사.
- soft-float 비용(실측): Cortex-M4 `scale()` 이 `vmul.f32` 대신 `bl __aeabi_fmul`, 링크 결과 text 3356 → 4236 B.

## 참고자료

- System V ABI gABI, ELF Header 장 (e_ident, e_type, e_machine, e_flags, e_ehsize) — <https://refspecs.linuxfoundation.org/elf/gabi4+/ch4.eheader.html>
- ARM ELF for the Arm Architecture (aaelf32: EF_ARM_EABIMASK, EF_ARM_ABI_FLOAT_HARD/SOFT) / Addenda to the ABI (aaelf32 build attributes: Tag_ABI_VFP_args) — <https://github.com/ARM-software/abi-aa>
- RISC-V ELF psABI (EF_RISCV_RVC, FLOAT_ABI, RVE, TSO; `.riscv.attributes`) — <https://github.com/riscv-non-isa/riscv-elf-psabi-doc>
- Clang 문서: Cross-compilation using Clang (`--target`, `--sysroot`, `--gcc-toolchain`) — <https://clang.llvm.org/docs/CrossCompilation.html>
- LLVM `Triple` 정규화 (arch-vendor-os-env) — <https://llvm.org/doxygen/classllvm_1_1Triple.html>
- GCC 문서: ARM Options (`-mfloat-abi`, `-mfpu`), RISC-V Options (`-march`, `-mabi`) — <https://gcc.gnu.org/onlinedocs/gcc/Submodel-Options.html>
- Arm GNU Toolchain (arm-none-eabi, newlib/newlib-nano, specs) — <https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads>
- newlib 시스템 콜 스텁 (`_write`, `_sbrk`, libnosys) — <https://sourceware.org/newlib/libc.html#Syscalls>
- Apple: Mach-O `<mach-o/loader.h>` (MH_MAGIC_64, cputype) — `otool -hv`
- 관련 레슨: TOOL-B01 (컴파일 단계), TOOL-B04 (오브젝트 해부·재배치), TOOL-B05 (링크 에러), TOOL-B06 (size·Flash/RAM), ARM-B02 · ARM-B12 (clang -S, Thumb 밀도), BM-B08 (Cortex-M 부팅·벡터 테이블), C-B02 (정수 승격), TOOL-M01 (링커 스크립트), TOOL-M05 (CMake 툴체인 파일), TOOL-A06 (재현 가능한 빌드), TOOL-A11 (미니 readelf)
