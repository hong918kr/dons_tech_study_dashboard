---
id: ELX-B02
time: 70분
updated: 2026-11-15
---
# ELX-B02 크로스 컴파일 기초: aarch64 툴체인과 sysroot

## 학습 목표

- **triplet**(arch-vendor-os-abi), **sysroot**, **동적 링커**(PT_INTERP)가 각각 무엇이고 어디서 쓰이는지 한 그림으로 설명한다.
- 공유 라이브러리 + 프로그램을 `aarch64-linux-gnu` · `arm-linux-gnueabihf` · `x86_64-linux-gnu` 세 타깃으로 크로스 빌드하고, `file` / `readelf -h -l -d` 로 class · machine · INTERP · NEEDED · RUNPATH 를 읽는다.
- `qemu-<arch>` user-mode 에 `-L <sysroot>` 와 `-E LD_LIBRARY_PATH=` 를 줘서 타깃 바이너리를 실행하고, **고전적인 실패 3종**(파일이 있는데 `No such file or directory`, `.so` 못 찾음, 틀린 `-L`)의 실제 메시지를 알아본다.
- 32/64-bit ELF 리더 `elfinfo` 를 C로 짜서 readelf 와 **같은 답**을 내고, 망가진 입력에도 죽지 않게(ASan 깨끗) 만든다.
- bring-up 상황에서 "이 바이너리가 이 rootfs 에서 돌까?"를 **실행하지 않고** 예측하고, 그 예측을 실제 실행과 맞춰 본다.

## 1. 문제

보드 bring-up 첫날 가장 흔한 질문이 "PC에서 만든 바이너리를 보드에 올렸더니 `./app: not found` 가 나와요"다. 파일은 분명히 있는데 말이다. 임베디드 리눅스 면접에서 크로스 컴파일을 묻는 이유는 이 한 줄을 **ELF 헤더 → 동적 링커 → 라이브러리 탐색 순서**로 풀어 설명할 수 있는지 보려는 것이다.

> 환경: Mac(Apple Silicon) 위 Lima VM `elinux` — aarch64 Ubuntu 26.04, 커널 7.0, GCC 15.2 크로스 툴체인 3종, binutils 2.46, qemu-user 10.2.1. 이 VM은 **aarch64 그 자체**라서 `aarch64-linux-gnu-gcc` 는 사실 네이티브 gcc다 (`/usr/bin/gcc → aarch64-linux-gnu-gcc-15`). 그래서 진짜 "다른 아키텍처" 크로스는 armhf(32-bit ARM)와 x86_64 이고, aarch64 는 "크로스 이름을 쓴 네이티브"로 본다. 모든 출력은 이 VM에서 실제로 돌린 것이다.

펌웨어(Cortex-M) 세계에서는 `arm-none-eabi-gcc` 가 만든 `.elf` 를 그대로 플래시에 굽는다. 운영체제도 동적 링커도 없다. 리눅스 타깃은 다르다. 실행 파일이 **"나를 실행하려면 `/lib/ld-linux-armhf.so.3` 를 먼저 불러라"**(PT_INTERP)와 **"`libc.so.6` 이 필요하다"**(DT_NEEDED)를 적어 두고, 그 파일들은 타깃의 루트 파일시스템에 있어야 한다. 빌드할 때 그 타깃 루트의 복사본이 **sysroot** 다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">이 레슨의 지도: 한 소스 → 세 triplet → 세 가지 실행 방법</text>
<rect x="10" y="40" width="150" height="70" class="f-mut" stroke="currentColor"/>
<text x="20" y="62" fill="currentColor">sensor.c (lib)</text>
<text x="20" y="82" fill="currentColor">app.c (main)</text>
<text x="20" y="102" fill="currentColor" font-size="12">Lima VM = aarch64</text>
<line x1="160" y1="75" x2="200" y2="75" stroke="currentColor"/>
<rect x="200" y="35" width="230" height="40" class="f-acc" stroke="currentColor"/>
<text x="210" y="60" fill="currentColor" font-family="monospace" font-size="12">aarch64-linux-gnu-gcc</text>
<rect x="200" y="85" width="230" height="40" class="f-acc" stroke="currentColor"/>
<text x="210" y="110" fill="currentColor" font-family="monospace" font-size="12">arm-linux-gnueabihf-gcc</text>
<rect x="200" y="135" width="230" height="40" class="f-acc" stroke="currentColor"/>
<text x="210" y="160" fill="currentColor" font-family="monospace" font-size="12">x86_64-linux-gnu-gcc</text>
<line x1="430" y1="55" x2="470" y2="55" stroke="currentColor"/>
<line x1="430" y1="105" x2="470" y2="105" stroke="currentColor"/>
<line x1="430" y1="155" x2="470" y2="155" stroke="currentColor"/>
<rect x="470" y="35" width="260" height="40" class="f-ok" stroke="currentColor"/>
<text x="480" y="52" fill="currentColor" font-size="12">ELF64 AArch64 → 그냥 실행 (네이티브)</text>
<text x="480" y="68" fill="currentColor" font-size="12">또는 qemu-aarch64 -L /</text>
<rect x="470" y="85" width="260" height="40" class="f-warn" stroke="currentColor"/>
<text x="480" y="102" fill="currentColor" font-size="12">ELF32 ARM → ./ 는 Exec format error</text>
<text x="480" y="118" fill="currentColor" font-size="12">qemu-arm -L /usr/arm-linux-gnueabihf</text>
<rect x="470" y="135" width="260" height="40" class="f-warn" stroke="currentColor"/>
<text x="480" y="152" fill="currentColor" font-size="12">ELF64 x86-64 → binfmt 가 qemu-x86_64</text>
<text x="480" y="168" fill="currentColor" font-size="12">+ QEMU_LD_PREFIX 또는 -L 필요</text>
<rect x="10" y="200" width="720" height="115" fill="none" stroke="currentColor"/>
<text x="20" y="220" fill="currentColor" font-weight="bold">sysroot = 타깃 rootfs 의 복사본 (-L 이 가리키는 곳)</text>
<text x="20" y="242" fill="currentColor" font-family="monospace" font-size="12">/usr/arm-linux-gnueabihf/lib/ld-linux-armhf.so.3   ← app 의 PT_INTERP "/lib/ld-linux-armhf.so.3"</text>
<text x="20" y="262" fill="currentColor" font-family="monospace" font-size="12">/usr/arm-linux-gnueabihf/lib/libc.so.6             ← app 의 DT_NEEDED "libc.so.6"</text>
<text x="20" y="282" fill="currentColor" font-family="monospace" font-size="12">$ORIGIN/lib/libsensor.so.1                         ← DT_RUNPATH "$ORIGIN/lib" 로 찾음</text>
<text x="20" y="304" fill="currentColor" font-size="12">qemu-user 는 명령어만 번역하고 syscall 은 호스트 커널(7.0)로 보낸다. 파일 경로는 -L 접두사를 먼저 시도한다.</text>
</svg>
```

### Part A — 크로스 빌드하고 ELF 헤더 · PT_INTERP 읽기

- `libsensor.so.1`(`-fPIC -shared -Wl,-soname,libsensor.so.1`)과 그것을 링크한 `app` 을 세 triplet 으로 빌드한다. 변형: `app`(RUNPATH `$ORIGIN/lib`), `app_rpath`(옛 RPATH), `app_norpath`, `app_nopie`, `app_static`.
- `elfinfo` 의 **A1** `elf_parse_header()`: 매직 · class(32/64) · 엔디언 · `e_type` · `e_machine` · `e_flags` · 프로그램 헤더 위치를 32/64 레이아웃 표로 읽는다.
- **A2** `find_interp()`: `PT_INTERP` 세그먼트의 경로 문자열. 커널과 같은 검사(길이 2 이상, 끝이 NUL)를 한다.
- 출력은 `readelf -h -l` 과 한 줄씩 같아야 한다 (`check.sh` 가 readelf 출력을 파싱해서 diff).

### Part B — 동적 섹션과 망가진 입력

- **B1** `vaddr_to_off()`: `DT_STRTAB` 은 파일 오프셋이 아니라 **가상 주소**다. 그 주소를 담은 `PT_LOAD` 로 파일 오프셋을 구한다.
- **B2** `parse_dynamic()`: `PT_DYNAMIC` 의 `(d_tag, d_val)` 배열을 `DT_NULL` 까지 걸으며 NEEDED · RUNPATH · RPATH · SONAME 을 모은다. 섹션 헤더(`.dynamic`, `.dynstr`)는 쓰지 않는다 — 로더도 안 쓴다.
- 망가진 입력(매직 틀림, big-endian, 잘림, 오프셋 오버플로, NUL 없는 문자열, STRTAB 이 어느 LOAD 에도 없음)은 **에러 코드**로 답하고 절대 죽지 않는다. `--selftest` 27개 + `--fuzz` (파일당 약 3000개 변형)를 ASan/UBSan 빌드로 돌린다.

### Part C — bring-up: "이 rootfs 에서 돌까?"를 실행 전에 맞히기

- **C** `sysroot_find()`: ld.so 의 탐색 순서(RPATH → LD_LIBRARY_PATH → RUNPATH → 기본 디렉터리)대로 sysroot 안에서 `.so` 를 찾는다. 다른 class/machine 의 파일은 ld.so 처럼 건너뛴다.
- `elfinfo --sysroot DIR [--libpath P] FILE` 이 `verdict: RUNNABLE` 또는 `verdict: MISSING n` 을 낸다.
- `check.sh` 는 16가지 상황에서 이 예측을 **qemu-user 실제 실행 결과**(exit code + 에러 문구)와 맞춘다: 맞는 sysroot, rpath 없음, 손으로 만든 mini rootfs + `LD_LIBRARY_PATH`, 틀린 `-L`, 64-bit `.so` 를 32-bit 앱 옆에, libc 없는 rootfs.

## 2. Problem (English)

> You bring up a new ARM Linux board. A dynamically linked program cross-built on your workstation fails on the board with `./app: not found`, although `ls` shows the file. Write a small ELF inspector that never executes the target binary.
>
> ```c
> int elf_parse(const uint8_t *buf, size_t len, struct elf_info *out);   /* 0 or ELFI_NOTELF/UNSUP/TRUNC/BAD */
> static int vaddr_to_off(const uint8_t *b, const struct elf_info *e, uint64_t vaddr, uint64_t *off);
> static int sysroot_find(const struct elf_info *e, const char *sysroot, const char *libpath,
>                         const char *origin, const char *name, char *out, size_t cap, const char **via);
> ```
>
> Requirements: support ELFCLASS32 and ELFCLASS64, little-endian only; read fields with byte loads (no struct casts over the buffer); report class, `e_type`, `e_machine`, PT_INTERP, every DT_NEEDED, DT_RUNPATH, DT_RPATH, DT_SONAME using **program headers only**; all offsets bounds-checked without integer overflow; malformed input returns an error, never crashes (ASan/UBSan clean).
> Given a sysroot, resolve the interpreter and each DT_NEEDED the way glibc's `ld.so` would (DT_RPATH only if there is no DT_RUNPATH, then LD_LIBRARY_PATH, then DT_RUNPATH, then `/lib/<multiarch>:/usr/lib/<multiarch>:/lib:/usr/lib`; `$ORIGIN` = directory of the binary; skip candidates of another class/machine) and print `verdict: RUNNABLE` or `verdict: MISSING <n>`.
>
> Example (armhf app, Ubuntu cross sysroot):
> ```text
> $ elfinfo --sysroot /usr/arm-linux-gnueabihf out/arm-linux-gnueabihf/app
> class: ELF32
> data: LE
> type: DYN
> machine: ARM (40)
> interp: /lib/ld-linux-armhf.so.3
> needed: libsensor.so.1
> needed: libc.so.6
> runpath: $ORIGIN/lib
> interp /lib/ld-linux-armhf.so.3 -> /usr/arm-linux-gnueabihf/lib/ld-linux-armhf.so.3
> needed libsensor.so.1 -> /tmp/…/out/arm-linux-gnueabihf/lib/libsensor.so.1
> needed libc.so.6 -> /usr/arm-linux-gnueabihf/lib/libc.so.6
> verdict: RUNNABLE
> ```
>
> Follow-ups: why does the kernel say ENOENT for a file that exists? Why can't you run `ldd` on a cross binary? What is the difference between DT_RPATH and DT_RUNPATH?

## 3. 시작 코드

파일: 실습 대상 `sensor.h` · `sensor.c` · `app.c`, 문제 `starter.c`(= `elfinfo`), 채점 `check.sh`, 실패 재현 `demo_failures.sh`(판정 없음, 출력만). 모든 명령은 **Mac 터미널에서 한 줄**로 친다. `check.sh` 는 VM 안 `/tmp/ELX-B02_work/<variant>.$$` 에 복사해서 빌드하고 끝나면 지운다 (`KEEP=1` 이면 실패 시 남김).

```bash
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B02/check.sh starter
```

<!-- code: sensor.h -->

<!-- code: sensor.c -->

<!-- code: app.c -->

<!-- code: starter.c -->

빌드만 따로 해 보려면 (VM 안 `/tmp` 에서):

```bash
limactl shell elinux -- bash -lc 'cd /tmp && gcc -std=c11 -Wall -Wextra -Werror -O2 /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B02/starter.c -o /tmp/ELX-B02_elfinfo && /tmp/ELX-B02_elfinfo --selftest | tail -1'
```

starter 실측: `6/27 passed` (레이아웃 표 5개 + "big-endian → UNSUP" 하나만 우연히 통과), `check.sh starter` 는 `23/65 passed`, `RESULT: FAIL (starter)`, exit 1, 약 3초. 통과하는 23개는 빌드 · 크로스 빌드 · qemu 실행처럼 `elfinfo` 와 상관없는 줄이다.

<!-- code: check.sh -->

## 4. 힌트

<details><summary>힌트 1 — 구조체 캐스트 대신 "표 + 바이트 읽기"</summary>

`(Elf64_Ehdr *)buf` 캐스트는 정렬(alignment)과 호스트 엔디언에 기댄다. 32/64를 한 코드로 다루려면 필드 위치를 표(`L32`, `L64`)로 두고 `rd(p, n)` 로 n바이트 little-endian 을 읽는다. `e_phoff` 는 32-bit 에서 28번 바이트(4바이트), 64-bit 에서 32번 바이트(8바이트)다. selftest 의 첫 5개가 표를 `offsetof(Elf64_Ehdr, e_phoff)` 등과 비교해 준다.
</details>

<details><summary>힌트 2 — 범위 검사는 뺄셈으로</summary>

`off + n <= len` 은 `off` 가 거의 2^64 이면 덧셈이 넘쳐서 참이 된다. `off <= len && n <= len - off` 로 쓴다(`in_file()` 이 이미 있다). selftest 에 `e_phoff = 0xfffffffffffffff0` 케이스가 있다.
</details>

<details><summary>힌트 3 — STRTAB 은 주소다</summary>

`readelf -d` 의 `(STRTAB) 0x2ac` 는 메모리에 올라간 뒤의 **가상 주소**다. PIE(`ET_DYN`)는 첫 LOAD 의 vaddr 가 0 이라서 우연히 파일 오프셋과 같다. `-no-pie` 로 빌드하면 armhf 는 `0x102a4`, aarch64 는 `0x4003d8` 이 된다. `p_vaddr <= v < p_vaddr + p_filesz` 인 PT_LOAD 를 찾아 `p_offset + (v - p_vaddr)`.
</details>

<details><summary>힌트 4 — 탐색 순서는 LD_DEBUG 가 알려 준다</summary>

`qemu-arm -L /usr/arm-linux-gnueabihf -E LD_DEBUG=libs ./app` 은 ld.so 가 실제로 시도한 경로를 순서대로 찍는다 (§5 Part C 에 실제 출력). 그 순서를 그대로 코드로 옮기면 된다. `search_list()` 가 콜론 목록과 `$ORIGIN` 을 처리해 주니 TODO C 는 "어느 목록을 어떤 조건에서 어떤 순서로" 만 정하면 된다.
</details>

<details><summary>힌트 5 — 파일이 있는 것과 쓸 수 있는 것은 다르다</summary>

같은 이름의 64-bit `libsensor.so.1` 이 경로에 있으면 32-bit ld.so 는 `(incompatible ELF class)` 로 건너뛰고 다음 경로를 본다. `same_target()` 이 후보의 class · machine 을 확인한다.
</details>

## 5. 풀이

### 0. 세 triplet 빌드 결과 (실측)

`check.sh` 의 빌드 명령 (triplet `T` 마다, `O=out/$T`):

```bash
$T-gcc -std=c11 -Wall -Wextra -Werror -O2 -fPIC -shared -Wl,-soname,libsensor.so.1 sensor.c -o $O/lib/libsensor.so.1
$T-gcc ... app.c -L$O/lib -lsensor -Wl,-rpath,'$ORIGIN/lib' -o $O/app          # RUNPATH (기본 new-dtags)
$T-gcc ... app.c -L$O/lib -lsensor -Wl,--disable-new-dtags,-rpath,'$ORIGIN/lib' -o $O/app_rpath
```

| triplet | `file` (앞부분) | Class | Machine (`e_machine`) | PT_INTERP | `e_flags` |
|---|---|---|---|---|---|
| aarch64-linux-gnu | ELF 64-bit LSB pie executable, ARM aarch64 | ELF64 | AArch64 (183) | `/lib/ld-linux-aarch64.so.1` | 0x0 |
| arm-linux-gnueabihf | ELF 32-bit LSB pie executable, ARM, EABI5 | ELF32 | ARM (40) | `/lib/ld-linux-armhf.so.3` | 0x5000400 (EABI5, hard-float) |
| x86_64-linux-gnu | ELF 64-bit LSB pie executable, x86-64 | ELF64 | Advanced Micro Devices X86-64 (62) | `/lib64/ld-linux-x86-64.so.2` | 0x0 |

세 `app` 모두 `NEEDED libsensor.so.1`, `NEEDED libc.so.6`, `RUNPATH $ORIGIN/lib`, `FLAGS_1 NOW PIE`. 툴체인이 sysroot 를 어디서 찾는지도 직접 물어볼 수 있다 (`demo_failures.txt` §0 실측):

```text
--- aarch64-linux-gnu: dumpmachine=aarch64-linux-gnu sysroot=''
    libc.so.6 = /usr/lib/gcc/aarch64-linux-gnu/15/../../../aarch64-linux-gnu/libc.so.6
--- arm-linux-gnueabihf: dumpmachine=arm-linux-gnueabihf sysroot='/'
    libc.so.6 = /usr/lib/gcc-cross/arm-linux-gnueabihf/15/../../../../arm-linux-gnueabihf/lib/libc.so.6
```

Ubuntu 의 크로스 gcc 는 sysroot 를 `/` 로 두고 `/usr/<triplet>/lib` 를 내장 검색 경로로 쓴다. Yocto/Buildroot SDK 는 보통 `--sysroot=<sdk>/sysroots/<machine>` 을 쓴다 — 개념은 같다: **헤더 · crt*.o · libc 를 타깃 것으로**.

### Part A — ELF 헤더와 PT_INTERP

32-bit 과 64-bit 는 필드 순서는 같지만 주소 크기 때문에 위치가 밀린다. 이 표가 `L32` / `L64` 이고 selftest 가 `<elf.h>` 의 `offsetof` 와 맞춰 본다.

| 필드 | ELF32 위치(크기) | ELF64 위치(크기) |
|---|---|---|
| `e_type` / `e_machine` | 16 / 18 (2) | 16 / 18 (2) |
| `e_phoff` | 28 (4) | 32 (8) |
| `e_flags` | 36 (4) | 48 (4) |
| `e_phentsize` / `e_phnum` | 42 / 44 (2) | 54 / 56 (2) |
| `sizeof(Phdr)` | 32 | 56 |
| `p_offset` / `p_vaddr` / `p_filesz` | 4 / 8 / 16 | 8 / 16 / 32 |
| `sizeof(Dyn)` | 8 | 16 |

함정 하나: **Phdr 에서 `p_flags` 위치가 다르다**. ELF64 는 `p_type` 바로 뒤(4번 바이트)로 옮겨서 8바이트 필드들을 정렬했다. 그래서 `p_offset` 이 32-bit 은 4, 64-bit 은 8 이다.

PT_INTERP 는 커널이 직접 읽는다. Linux 6.18.55 `fs/binfmt_elf.c` 의 `load_elf_binary()` (870–910행)를 읽어 보면:

```c
if (elf_ppnt->p_filesz > PATH_MAX || elf_ppnt->p_filesz < 2)   /* -ENOEXEC */
...
if (elf_interpreter[elf_ppnt->p_filesz - 1] != '\0')           /* -ENOEXEC */
...
interpreter = open_exec(elf_interpreter);                      /* 없으면 -ENOENT */
```

`find_interp()` 는 같은 검사를 한다. 그리고 여기서 "파일이 있는데 not found"의 정체가 나온다: `execve()` 가 돌려준 `ENOENT` 는 **실행 파일이 아니라 인터프리터(`/lib/ld-…`)가 없다는 뜻**이다. 실측 (`demo_failures.txt` §4, 일부러 `--dynamic-linker=/lib/ld-linux-board.so.1` 로 링크):

```text
-rwxrwxr-x 70608 aarch64-linux-gnu/app_badinterp
      [Requesting program interpreter: /lib/ld-linux-board.so.1]
$ ./aarch64-linux-gnu/app_badinterp
…: ./aarch64-linux-gnu/app_badinterp: cannot execute: required file not found
[exit=127]
$ busybox sh -c ./aarch64-linux-gnu/app_badinterp
sh: ./aarch64-linux-gnu/app_badinterp: not found
$ python3 -I -c 'import os; os.execv("./aarch64-linux-gnu/app_badinterp", ["x"])'
FileNotFoundError: [Errno 2] No such file or directory
```

bash 5.3 은 친절하게 `required file not found` 라고 하지만, 보드의 BusyBox `sh` 는 그냥 `not found` 다. 둘 다 같은 `ENOENT` 다.

### Part B — 동적 섹션: STRTAB 은 주소다

로더는 섹션 헤더를 보지 않는다. `PT_DYNAMIC` 이 가리키는 `(tag, val)` 배열을 읽고, 문자열 표는 `DT_STRTAB`(주소) + `DT_STRSZ`(크기)로 찾는다. 아래는 `app_nopie`(armhf, check.sh 와 같은 플래그로 빌드)의 실제 숫자다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">armhf app_nopie: DT_STRTAB 0x102a4 (주소) → 파일 오프셋 0x2a4 (실측)</text>
<text x="10" y="50" fill="currentColor">파일 (69284 B = 0x10ea4)</text>
<rect x="10" y="60" width="300" height="34" class="f-acc" stroke="currentColor"/>
<text x="18" y="82" fill="currentColor" font-size="12">LOAD#0 off 0x0, filesz 0x7a4</text>
<rect x="310" y="60" width="250" height="34" class="f-mut" stroke="currentColor"/>
<text x="318" y="82" fill="currentColor" font-size="12">… .text 끝 ~ 0xff00</text>
<rect x="560" y="60" width="170" height="34" class="f-acc" stroke="currentColor"/>
<text x="566" y="82" fill="currentColor" font-size="12">LOAD#1 off 0xff00</text>
<text x="10" y="130" fill="currentColor">메모리</text>
<rect x="10" y="140" width="300" height="34" class="f-ok" stroke="currentColor"/>
<text x="18" y="162" fill="currentColor" font-size="12">vaddr 0x10000 … 0x107a4</text>
<line x1="160" y1="94" x2="160" y2="140" stroke="currentColor" stroke-dasharray="4 3"/>
<text x="170" y="122" fill="currentColor" font-size="12">p_vaddr − p_offset = 0x10000</text>
<rect x="560" y="140" width="170" height="34" class="f-ok" stroke="currentColor"/>
<text x="566" y="162" fill="currentColor" font-size="12">vaddr 0x2ff00 …</text>
<text x="10" y="210" fill="currentColor" font-family="monospace" font-size="12">DYNAMIC @ off 0xff08: (NEEDED,0x98) (NEEDED,0xa7) (RUNPATH,0xc6) … (STRTAB,0x102a4) (STRSZ,210)</text>
<text x="10" y="234" fill="currentColor" font-family="monospace" font-size="12">0x102a4 ∈ [0x10000, 0x107a4) → off = 0x0 + (0x102a4 − 0x10000) = 0x2a4</text>
<text x="10" y="258" fill="currentColor" font-family="monospace" font-size="12">0x2a4 + 0x98 = 0x33c → "libsensor.so.1"   0x2a4 + 0xa7 → "libc.so.6"   0x2a4 + 0xc6 → "$ORIGIN/lib"</text>
<rect x="10" y="272" width="720" height="48" class="f-warn" stroke="currentColor"/>
<text x="18" y="292" fill="currentColor" font-size="12">흔한 버그: STRTAB 값을 그대로 파일 오프셋으로 쓰면 0x102a4 &lt; 0x10ea4 라서 범위 검사도 통과한다.</text>
<text x="18" y="310" fill="currentColor" font-size="12">결과: 크래시 없이 needed 가 빈 문자열 (실측, §7). PIE 만 테스트하면 vaddr == offset 이라 영원히 안 보인다.</text>
</svg>
```

`parse_dynamic()` 은 두 번 걷는다. 태그 순서는 보장되지 않으므로(`NEEDED` 가 `STRTAB` 보다 앞에 온다 — 위 실측이 그렇다) 1차에서 인덱스만 모으고, 2차에서 STRTAB 을 오프셋으로 바꾼 뒤 `dyn_str()` 로 복사한다. `dyn_str()` 은 `idx < STRSZ` 와 "NUL 이 STRSZ 안에 있음"을 확인한다.

끝 조건은 `DT_NULL` 또는 세그먼트 끝 **중 먼저 오는 것**이다. `DT_NULL` 이 없는 파일에서 세그먼트 밖까지 읽으면 그게 바로 OOB 읽기다 (selftest "DT_NULL 없음" 케이스).

망가진 입력 실측 (`--selftest` 의 Part B 와 `--fuzz`, ASan+UBSan 빌드):

| 입력 | 결과 |
|---|---|
| 매직 `\x7fEXF` / 3바이트 파일 | `ELFI_NOTELF` |
| `EI_DATA = ELFDATA2MSB` | `ELFI_UNSUP` (big-endian 은 지원 안 한다고 **말한다**) |
| 40바이트로 잘린 ELF64 / `e_phoff = 0xffff…fff0` | `ELFI_TRUNC` |
| `e_phentsize = 32` (64-bit 인데) | `ELFI_BAD` |
| interp 끝 NUL 없음 / NEEDED 인덱스 4096 ≥ STRSZ / STRTAB 이 LOAD 밖 / 마지막 문자열 NUL 없음 | `ELFI_BAD` |
| fuzz armhf `app` (잘라내기 81개 + 4비트 뒤집기 3000개) | `3081 cases, ok=2912 notelf=23 unsup=7 trunc=88 bad=51`, ASan 경고 0 |

### Part C — 탐색 순서와 sysroot 판정

`LD_DEBUG=libs` 가 ld.so 의 실제 순서를 보여 준다 (armhf `app`, `LD_LIBRARY_PATH` 에 64-bit `.so` 를 둔 경우, `demo_failures.txt` §6):

```text
find library=libsensor.so.1 [0]; searching
 search path=mix/lib		(LD_LIBRARY_PATH)
  trying file=mix/lib/libsensor.so.1
    (incompatible ELF class)
 search path=arm-linux-gnueabihf/lib		(RUNPATH from file ./arm-linux-gnueabihf/app)
  trying file=arm-linux-gnueabihf/lib/libsensor.so.1
find library=libc.so.6 [0]; searching
 …
 search cache=/etc/ld.so.cache
 search path=/lib/arm-linux-gnueabihf:/usr/lib/arm-linux-gnueabihf:/lib:/usr/lib		(system search path)
```

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">glibc ld.so 의 .so 탐색 순서 (sysroot_find 가 흉내 내는 것)</text>
<rect x="10" y="40" width="130" height="50" class="f-warn" stroke="currentColor"/>
<text x="18" y="60" fill="currentColor">1. DT_RPATH</text>
<text x="18" y="80" fill="currentColor" font-size="12">RUNPATH 없을 때만</text>
<rect x="155" y="40" width="130" height="50" class="f-acc" stroke="currentColor"/>
<text x="163" y="60" fill="currentColor">2. LD_LIBRARY</text>
<text x="163" y="80" fill="currentColor" font-size="12">_PATH (--libpath)</text>
<rect x="300" y="40" width="130" height="50" class="f-acc" stroke="currentColor"/>
<text x="308" y="60" fill="currentColor">3. DT_RUNPATH</text>
<text x="308" y="80" fill="currentColor" font-size="12">$ORIGIN/lib</text>
<rect x="445" y="40" width="130" height="50" class="f-mut" stroke="currentColor"/>
<text x="453" y="60" fill="currentColor">4. ld.so.cache</text>
<text x="453" y="80" fill="currentColor" font-size="12">(elfinfo 는 생략)</text>
<rect x="590" y="40" width="140" height="50" class="f-ok" stroke="currentColor"/>
<text x="598" y="60" fill="currentColor">5. 기본 디렉터리</text>
<text x="598" y="80" fill="currentColor" font-size="12">/lib/&lt;ma&gt; … /usr/lib</text>
<line x1="140" y1="65" x2="155" y2="65" stroke="currentColor"/>
<line x1="285" y1="65" x2="300" y2="65" stroke="currentColor"/>
<line x1="430" y1="65" x2="445" y2="65" stroke="currentColor"/>
<line x1="575" y1="65" x2="590" y2="65" stroke="currentColor"/>
<text x="10" y="125" fill="currentColor">각 후보 파일마다:</text>
<rect x="10" y="135" width="230" height="40" class="f-mut" stroke="currentColor"/>
<text x="18" y="160" fill="currentColor" font-size="12">없음 → (no such file) 다음 경로</text>
<rect x="255" y="135" width="250" height="40" class="f-warn" stroke="currentColor"/>
<text x="263" y="160" fill="currentColor" font-size="12">class/machine 다름 → 건너뜀 (incompatible)</text>
<rect x="520" y="135" width="210" height="40" class="f-ok" stroke="currentColor"/>
<text x="528" y="160" fill="currentColor" font-size="12">맞음 → 로드, 탐색 끝</text>
<text x="10" y="205" fill="currentColor" font-size="12">다 실패: "cannot open shared object file" (exit 127). 건너뛴 게 있으면 "wrong ELF class: ELFCLASS64".</text>
<text x="10" y="228" fill="currentColor" font-size="12">&lt;ma&gt; = multiarch: aarch64-linux-gnu / arm-linux-gnueabihf (e_flags 0x400 이면) / x86_64-linux-gnu</text>
</svg>
```

경로를 sysroot 에 어떻게 붙일지가 설계 포인트다. `elfinfo` 는 **보드 기준**으로 정했다: 절대 경로(`/lib/…`, `--libpath /opt/sensor/lib`)는 sysroot 아래, `$ORIGIN` 은 바이너리가 실제로 놓인 디렉터리. qemu-user 는 여기서 하나가 다르다 — `-L` 아래에 없으면 **호스트 경로로 다시 시도한다**(관찰: `qemu-aarch64 -L /usr/arm-linux-gnueabihf ./aarch64/app` 이 그냥 돈다, `demo_failures.txt` §3). 그래서 aarch64 에서는 "틀린 -L" 이 실패를 만들지 못하고, check.sh 의 틀린 `-L` 케이스는 armhf/x86_64 로만 한다. 보드에는 이 안전망이 없다.

`check.sh solution` 의 Part C 판정 16개 (전부 PASS, 실측):

| 상황 | elfinfo 예측 | 실제 실행 (qemu-user) |
|---|---|---|
| 세 triplet `app`, 맞는 sysroot | `RUNNABLE` | rc 0, `… OK` |
| 세 triplet `app_norpath` | `MISSING 1` | rc 127, `libsensor.so.1: cannot open shared object file` |
| 세 triplet `app_norpath`, mini rootfs (로더+libc만) | `MISSING 1` | rc 127, 같은 문구 |
| 위 + `--libpath /opt/sensor/lib` / `-E LD_LIBRARY_PATH=/opt/sensor/lib` | `RUNNABLE` | rc 0 |
| armhf `app`, `-L /usr/x86_64-linux-gnu` | `MISSING 2` (interp + libc) | rc 255, `qemu-arm: Could not open '/lib/ld-linux-armhf.so.3'` |
| x86_64 `app`, `-L /usr/arm-linux-gnueabihf` | `MISSING 2` | rc 255, `Could not open '/lib64/ld-linux-x86-64.so.2'` |
| armhf `app` 옆 `lib/` 에 64-bit `libsensor.so.1` | `MISSING 1` | rc 127, `libsensor.so.1: wrong ELF class: ELFCLASS64` |
| armhf, 로더만 있고 libc 없는 rootfs | `MISSING 1` | rc 127, `libc.so.6: cannot open shared object file` |

틀린 `-L` 에서 qemu 는 첫 실패(로더)에서 멈추지만 `elfinfo` 는 **둘 다**(로더와 libc) 없다고 말한다. 실행해 보는 것보다 정적 분석이 더 많은 걸 한 번에 알려 주는 경우다.

### 고전적인 실패 모음 (전부 실측, `demo_failures.txt`)

| 증상 | 원인 | 확인 |
|---|---|---|
| `cannot execute binary file: Exec format error` (126) | armhf 를 aarch64 에서 `./` 로. binfmt 에 qemu-arm 없음 + Apple Silicon 은 AArch32 EL0 가 없음 (`arch/arm64/kernel/process.c` `compat_elf_check_arch()` → `system_supports_32bit_el0()`) | `file`, `ls /proc/sys/fs/binfmt_misc` |
| `qemu-x86_64: Could not open '/lib64/ld-linux-x86-64.so.2'` (255) | binfmt_misc 가 qemu 를 대신 불렀지만 sysroot 를 모름 | `QEMU_LD_PREFIX=/usr/x86_64-linux-gnu ./app` 로 해결 |
| `cannot execute: required file not found` / BusyBox `not found` (127) | PT_INTERP 경로가 없음 | `readelf -l app \| grep interpreter` |
| `error while loading shared libraries: X: cannot open shared object file` (127) | NEEDED 를 어디서도 못 찾음 | `readelf -d`, `LD_DEBUG=libs` |
| `… X: wrong ELF class: ELFCLASS64` (127) | 이름은 맞는데 다른 class 의 `.so` | `file lib/X` |
| `error: mixed uses VFP register arguments, soft.o does not` (링크) | soft-float `.o` 를 hard-float 앱에 | `readelf -h` Flags (추가 연습 P1) |

<!-- code: demo_failures.sh -->

```bash
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B02/demo_failures.sh
```

### 실행 결과 (solution)

`check_solution.txt` 에서 발췌 (Lima VM elinux, 약 4초):

```text
PASS: build solution.c (-Wall -Wextra -Werror) + asan build
PASS: selftest 27/27 passed
PASS: cross build arm-linux-gnueabihf
INFO: ELF 32-bit LSB pie executable, ARM, EABI5 version 1 (SYSV), dynamically linked, interpreter /lib/ld-linux-armhf.so.3
…
PASS: elfinfo == readelf: arm-linux-gnueabihf/app_nopie
PASS: elfinfo == readelf: /usr/arm-linux-gnueabihf/lib/libc.so.6
PASS: fuzz /tmp/ELX-B02_work/solution.625680/out/arm-linux-gnueabihf/app: 3081 cases, ok=2912 notelf=23 unsup=7 trunc=88 bad=51
PASS: elfinfo == readelf: riscv64 libc.so.6 (4번째 아키텍처, 빌드 없이)
PASS: run arm-linux-gnueabihf/app (qemu-arm -L /usr/arm-linux-gnueabihf)
PASS: run arm-linux-gnueabihf/app_static (no -L)
…
PASS: predict armhf app + 64-bit libsensor: 'MISSING 1' and run rc=127
PASS: predict armhf app, rootfs without libc: 'MISSING 1' and run rc=127
65/65 passed
RESULT: PASS (solution)
```

readelf 비교 25개 = 3 triplet × (app, app_norpath, app_rpath, app_nopie, app_static, libsensor.so.1, sysroot 의 libc.so.6, ld.so) + riscv64 libc. 정적 바이너리는 `-L` 없이 돈다 — PT_INTERP 가 없으니 찾을 로더가 없다. 3회 연속 실행 모두 `65/65`.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B02/check.sh solution
```

## 7. 자주 하는 실수

**1. STRTAB 값을 파일 오프셋으로 쓴다.** PIE 만 테스트하면 통과한다. 실측 (solution 에서 `vaddr_to_off` 호출만 `stroff = strtab` 으로 바꾼 버전):

```text
--- PIE app (armhf)          needed: libsensor.so.1 / needed: libc.so.6 / runpath: $ORIGIN/lib   ← 우연히 맞음
--- no-pie app (armhf)       needed:  / needed:                                                 ← rc 0, 조용한 쓰레기
--- no-pie app (aarch64)     elfinfo: …/app_nopie: malformed                                    ← 0x4003d8 이 파일 밖
selftest: 8 FAIL
```

고친 코드: `vaddr_to_off(b, e, strtab, &stroff)` — 주소를 담은 PT_LOAD 로 바꾼다.

**2. `off + n <= len` 으로 범위 검사.** `off` 가 크면 덧셈이 넘친다 → `in_file()` 의 `off <= len && n <= len - off`.

**3. 섹션 헤더(`.dynamic`, `.dynstr`)로 읽는다.** `strip --strip-section-headers`(또는 sstrip) 한 바이너리, 섹션 헤더를 일부러 깨 놓은 바이너리에서 실패한다. 커널과 ld.so 가 보는 것은 프로그램 헤더뿐이다.

**4. "파일이 있으니 된다".** `ls lib/libsensor.so.1` 은 성공해도 64-bit 일 수 있다 → `wrong ELF class`. 후보마다 class · machine 을 본다 (`same_target()`).

**5. 호스트에서 `LD_LIBRARY_PATH=… qemu-arm …`.** 동작은 한다 (qemu 가 환경을 넘긴다, 실측). 하지만 그 변수는 **호스트의 qemu 프로세스 자신**에게도 적용된다. 타깃 전용으로는 `-E LD_LIBRARY_PATH=…` 를 쓴다.

**6. sysroot 안의 절대 심볼릭 링크.** 보드 rootfs 를 복사한 sysroot 에 `libfoo.so -> /usr/lib/libfoo.so.1` 같은 **절대** 링크가 있으면 빌드 머신의 `/usr/lib` 를 가리킨다. 이 VM 의 크로스 sysroot 는 상대 링크다 (`lib64/ld-linux-x86-64.so.2 -> ../lib/ld-linux-x86-64.so.2`). Yocto/Buildroot 는 SDK 를 만들 때 이를 고친다.

**7. float ABI 를 섞는다.** `-mfloat-abi=soft` 오브젝트를 armhf 앱에 링크하면 `error: mixed uses VFP register arguments, soft.o does not` (실측). triplet 의 `hf` 는 장식이 아니라 호출 규약이다.

## 8. 면접 Q&A

<details><summary>Q1. triplet `arm-linux-gnueabihf` 의 각 부분은 무엇이고, `arm-none-eabi` 와 무엇이 다른가요?</summary>

arch(`arm`) – (vendor 생략) – os(`linux`) – abi(`gnueabihf` = glibc + ARM EABI + hard-float 호출 규약). `arm-none-eabi` 는 OS 가 없다(`none`): libc 는 newlib, 동적 링커도 PT_INTERP 도 없고, 링커 스크립트로 절대 주소에 놓는 펌웨어용이다. `linux` triplet 은 리눅스 syscall ABI 와 glibc 를 전제하고, 결과물은 ld.so 가 실행 시점에 라이브러리를 붙인다.

꼬리: *gnueabi 와 gnueabihf 를 섞으면?* — float 인자를 정수 레지스터(r0–r3)로 넘기느냐 VFP 레지스터(s0/d0)로 넘기느냐가 다르다. 링커가 `e_flags`(0x400 hard / 0x200 soft)를 보고 거부한다 (§7-7 실측). 멀티아치 경로도 다르다 (`/lib/arm-linux-gnueabihf` vs `/lib/arm-linux-gnueabi`).
</details>

<details><summary>Q2. sysroot 는 무엇이고 빌드 때와 실행 때 각각 어디에 쓰이나요?</summary>

타깃 rootfs 의 "개발용 복사본": 헤더(`usr/include`), 시작 파일(`crt1.o`, `crti.o`), 라이브러리(`libc.so.6`, `libc.so` 링커 스크립트, `.a`), 로더. **빌드 때**는 컴파일러/링커가 `--sysroot` 아래에서 헤더와 라이브러리를 찾는다 (호스트 헤더가 섞이지 않게). **실행 때**(qemu-user)는 `-L`/`QEMU_LD_PREFIX` 로 같은 디렉터리를 타깃의 `/` 로 쓴다. 실제 보드에서는 rootfs 자체가 그 역할이다.

꼬리: *sysroot 의 glibc 가 보드보다 새로우면?* — 새 심볼 버전에 링크되어 보드에서 `version 'GLIBC_2.xx' not found` 류로 실패한다 (이 레슨에서 재현하지 않음, 메시지 형식은 기억에 의존). 그래서 sysroot 는 **보드와 같은 버전**이어야 하고, Yocto 의 `populate_sdk` 가 그걸 보장하는 방법이다.
</details>

<details><summary>Q3. 파일이 분명히 있는데 `./app: not found` / `No such file or directory` 가 나옵니다. 왜죠?</summary>

`execve()` 가 `ENOENT` 를 돌려줬는데, 그 대상이 실행 파일이 아니라 **PT_INTERP 의 로더**다. 커널 `fs/binfmt_elf.c` `load_elf_binary()` 가 `open_exec(elf_interpreter)` 에서 실패한 에러를 그대로 돌려준다. 순서: `file app` (dynamically linked, interpreter …) → `readelf -l app | grep interpreter` → 보드에서 `ls -l <그 경로>`. 흔한 원인: glibc 로 빌드했는데 보드가 musl/uClibc, armhf 로더를 기대하는데 보드는 armel, 64-bit 앱을 32-bit rootfs 에.

꼬리: *스크립트에서도 같은 일이?* — 그렇다. `#!/bin/bash` 인데 보드에 bash 가 없으면 같은 `not found` 다. 원리 동일: 인터프리터 경로가 없다.
</details>

<details><summary>Q4. RPATH, RUNPATH, LD_LIBRARY_PATH 의 우선순위는?</summary>

ld.so: `DT_RPATH`(단, `DT_RUNPATH` 가 있으면 RPATH 무시) → `LD_LIBRARY_PATH` → `DT_RUNPATH` → `/etc/ld.so.cache` → 기본 디렉터리. 추가 연습 P4 에서 `LD_DEBUG=libs` 로 다섯 경우를 실측했다: RPATH 가 LD_LIBRARY_PATH 를 이기고, RUNPATH 는 진다. 요즘 링커는 `-rpath` 를 기본으로 RUNPATH 로 쓴다 (`--enable-new-dtags`; 이 VM 의 binutils 2.46 실측: `-Wl,-rpath` → `(RUNPATH)`).

꼬리: *왜 RUNPATH 를 새로 만들었나?* — RPATH 는 사용자가 `LD_LIBRARY_PATH` 로 덮어쓸 수 없어서 디버깅/교체가 어려웠다. 또 RUNPATH 는 그 객체의 **직접** 의존성에만 적용된다(man ld.so, 이 레슨에서 직접 재현은 안 함). setuid 프로그램에서는 `LD_LIBRARY_PATH` 가 무시된다(secure-execution 모드, 기억에 의존).
</details>

<details><summary>Q5. 크로스 바이너리에 `ldd` 를 쓰면 안 되는 이유는?</summary>

`ldd` 는 대개 ld.so 를 `LD_TRACE_LOADED_OBJECTS=1` 로 **실행**해서 답을 얻는다. 타깃 바이너리는 호스트에서 실행이 안 되거나(Exec format error), binfmt 로 qemu 가 돌면서 호스트 경로를 섞어 엉뚱한 답을 낸다. 믿을 수 없는 바이너리에 쓰면 코드가 실행될 수도 있다. 대신 `readelf -d`(직접 의존성만), `objdump -p`, 또는 이 레슨의 `elfinfo --sysroot`. 전이 폐포(transitive closure)를 ELF 만으로 계산하는 건 ELX-B12 의 문제다.
</details>

<details><summary>Q6. PIE 와 non-PIE 는 ELF 에서 어떻게 구별하나요?</summary>

`e_type`: non-PIE 는 `ET_EXEC`(고정 주소, aarch64 0x400000 / armhf 0x10000 에 링크 — §5 실측), PIE 는 `ET_DYN`. 그런데 공유 라이브러리도 `ET_DYN` 이다. 구별은 `DT_FLAGS_1` 의 `DF_1_PIE`(readelf `Flags: NOW PIE`). `libc.so.6` 은 PT_INTERP 가 있는(실행도 되는) `ET_DYN` 이지만 DF_1_PIE 가 없어서 "shared" 다 (추가 연습 P2 실측). static-pie 는 `ET_DYN` + DF_1_PIE + INTERP 없음 — aarch64/x86_64 는 만들었고, 이 VM 의 armhf sysroot 는 `rcrt1.o` 가 없어 `-static-pie` 가 실패했다(실측).

꼬리: *PIE 가 크로스 디버깅에 주는 영향?* — 로드 주소가 실행마다 바뀌므로(ASLR) gdb 에서 심볼 주소에 로드 베이스를 더해야 한다. `vaddr_to_off` 와 같은 계산이다.
</details>

<details><summary>Q7. qemu-user 로 무엇을 검증할 수 있고 무엇은 못 하나요?</summary>

된다: 명령어 집합/ABI(호출 규약, `sizeof(long)` — armhf 4, 나머지 8 실측), 동적 링킹과 sysroot 구성, 유저 공간 로직의 단위 테스트, CI 에서 크로스 빌드 결과 스모크 테스트. 안 된다: 커널은 호스트 것(여기선 7.0)이라 타깃 커널 버전 차이, 드라이버/`/dev`/ioctl, 인터럽트·타이밍, 실제 주변장치. 그건 qemu-system(ELX-B04) 또는 보드. binfmt_misc 에 등록하면 `./app` 만으로 qemu 가 돈다 — 이 VM 은 x86_64 · riscv64 등은 등록되어 있고 qemu-arm 은 없다 (Debian 계열은 arm64 호스트가 AArch32 를 직접 돌릴 수 있다고 보고 등록하지 않는다, 기억에 의존).

꼬리: *binfmt flags `POF` 의 F 는?* — 등록 시점에 인터프리터 파일을 열어 둔다(fix-binary). 컨테이너/chroot 안에 qemu 바이너리가 없어도 동작하게 하는 옵션이다 (기억에 의존, 이 레슨에서 실험하지 않음).
</details>

<details><summary>Q8. 그럼 다 정적 링크하면 되지 않나요?</summary>

`app_static` 은 `-L` 없이 세 triplet 모두 돌았다(실측). bring-up 초기의 진단 도구(BusyBox, 테스트 유틸)는 실제로 정적으로 많이 만든다. 단점: 크기(라이브러리마다 libc 복사), 보안 패치 시 전부 재빌드, glibc 의 NSS/`dlopen` 계열 함수는 정적에서도 런타임에 `.so` 를 찾는다(링크 경고가 난다, 기억에 의존), LGPL 라이브러리를 정적으로 묶을 때의 라이선스 조건. 그래서 제품 rootfs 는 보통 동적 + 일관된 sysroot 다.
</details>

## 9. 추가 연습

`practice.c` = `solution.c` + 아래 4개. `--more` 옵션으로 켜지고, `check.sh practice` 가 readelf/LD_DEBUG 와 비교한다.

**P1. `e_flags` 해석.** ARM 이면 `flags: 0x… eabi=N float=hard|soft|none` 을, 그 외에는 `flags: 0x…` 를 찍어라. soft-float 오브젝트(`-mfloat-abi=soft -c`)도 비교하라.

<details><summary>답</summary>

`eabi = (flags & EF_ARM_EABIMASK) >> 24`, float 는 `0x400` (EF_ARM_ABI_FLOAT_HARD) → hard, `0x200` → soft, 둘 다 없으면 none. 실측: armhf app `flags: 0x5000400 eabi=5 float=hard`, `soft.o` 는 `0x5000000` (readelf: `Version5 EABI` — gcc 가 soft 플래그를 따로 세우지 않았다) → `float=none`. riscv64 libc 는 `flags: 0x5` (RVC + double-float ABI). 이 플래그 때문에 링커가 섞기를 거부한다 (§7-7).
</details>

**P2. 실행 형태 `kind:`.** relocatable / exec / static / pie / static-pie / shared 를 구별하라.

<details><summary>답</summary>

`DT_FLAGS_1` 을 `parse_dynamic` 에서 같이 모으고: `ET_REL` → relocatable, `ET_EXEC` → INTERP 있으면 exec 아니면 static, `ET_DYN` → DF_1_PIE 있으면 (INTERP 있으면 pie, 없으면 static-pie), 없으면 shared. 실측: `app` pie, `app_nopie` exec, `app_static` static, `app_spie`(aarch64, x86_64) static-pie, `libc.so.6` · `ld-linux-*.so` · `libsensor.so.1` shared.
</details>

**P3. Build ID.** `PT_NOTE` 에서 `NT_GNU_BUILD_ID` 를 찾아 `buildid: <hex>` 로 찍어라.

<details><summary>답</summary>

노트는 `namesz, descsz, type` (각 4바이트) + 이름(정렬 패딩) + 설명(정렬 패딩). 정렬은 보통 4, `p_align == 8` 인 노트 세그먼트(x86_64 의 `.note.gnu.property`)는 8. `type == 3 && name == "GNU\0"` 이면 desc 를 16진으로. 모든 크기는 세그먼트 안인지 먼저 검사한다. 실측 armhf app `buildid: 3fac7ff961d5998ae8fecc3d68fc9a5186b23a09` = `file` 의 `BuildID[sha1]=3fac7ff9…`. 보드에서 크래시 덤프를 받으면 이 값으로 심볼 있는 바이너리를 찾는다.
</details>

**P4. 어디서 찾았나 (`via`).** `--sysroot` 출력의 needed 줄에 `(via rpath|libpath|runpath|default)` 를 붙이고, `LD_DEBUG=libs` 가 실제로 성공한 search path 종류와 비교하라.

<details><summary>답</summary>

`sysroot_find` 가 이미 `*via` 를 채운다 — 출력만 붙이면 된다. 실측 5케이스 전부 일치:

```text
PASS: via app_rpath: rpath (LD_DEBUG: rpath)
PASS: via app + LD_LIBRARY_PATH=/opt/sensor/lib: libpath (LD_DEBUG: libpath)
PASS: via app_rpath + LD_LIBRARY_PATH (RPATH wins): rpath (LD_DEBUG: rpath)
PASS: via app + LD_LIBRARY_PATH=/opt/mix (64-bit, skipped): runpath (LD_DEBUG: runpath)
PASS: via app_norpath, lib only in rootfs /usr/lib: default (LD_DEBUG: default)
```

한계: elfinfo 는 `ld.so.cache` 를 모델링하지 않는다. qemu 아래에서 armhf ld.so 는 호스트의 `/etc/ld.so.cache`(aarch64 항목)를 열지만 쓸 항목이 없어서 결과에 영향이 없었다.
</details>

**P5. (코드 없음) 보드 triage 순서.** 보드 셸에서 `./app: not found` 를 받았다. 보드에 `file`/`readelf` 가 없다. 무엇을 어떤 순서로 보나?

<details><summary>답</summary>

1) 호스트에서 `elfinfo app`(또는 `readelf -h -l -d`): class · machine · interp · needed. 2) 보드에서 `uname -m`, `ls -l <interp>`, `ls /lib`. 3) 로더가 없으면: libc 종류(glibc/musl)와 float ABI 가 rootfs 와 같은지. 4) 로더가 있으면 그 로더를 직접 불러 본다: `/lib/ld-linux-armhf.so.3 --list ./app` (glibc 로더는 직접 실행 가능 — 이 VM 에서는 보드가 없어 확인하지 않음). 5) 증거를 rootfs 복사본에 대해 `elfinfo --sysroot <rootfs> app` 으로 재현하고 `qemu-arm -L <rootfs>` 로 확인.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B02/check.sh practice
```

실측: selftest `32/32`, readelf 비교 28개(기본 25 + static-pie 2 + soft.o 1, 각 비교에 flags/kind/buildid 줄 포함), via 5개, `73/73 passed`, `RESULT: PASS (practice)`, 약 4초.

## 10. 핵심 정리

- triplet = arch-vendor-os-abi. `linux` 타깃의 바이너리는 **PT_INTERP(로더 경로) + DT_NEEDED(.so 이름)** 를 들고 다니고, 둘 다 타깃 rootfs(빌드 때는 sysroot)에 있어야 한다.
- "파일이 있는데 not found" = 커널 `open_exec(interp)` 의 ENOENT. 첫 확인은 `readelf -l app | grep interpreter`.
- ELF 리더는 **프로그램 헤더만**: PT_INTERP, PT_DYNAMIC, PT_LOAD. `DT_STRTAB` 은 주소 → PT_LOAD 로 오프셋 변환. PIE 만 테스트하면 이 버그가 숨는다.
- 32/64 는 표로: `e_phoff` 28 vs 32, Phdr 32 vs 56 B, `p_flags` 위치가 다르다. 범위 검사는 `off <= len && n <= len - off`.
- ld.so 순서: RPATH(RUNPATH 없을 때) → LD_LIBRARY_PATH → RUNPATH → cache → `/lib/<multiarch>` 등. 다른 class 는 건너뛰고 `wrong ELF class` 로 보고. `LD_DEBUG=libs` 가 진실.
- qemu-user: `-L`/`QEMU_LD_PREFIX` = sysroot, `-E` = 타깃 환경 변수, 접두사에 없으면 호스트로 되돌아간다(보드엔 없는 안전망). 커널은 호스트 것.
- armhf 를 Apple Silicon 위 aarch64 에서 `./` 로 돌리면 Exec format error — AArch32 EL0 없음. 정적 바이너리는 `-L` 없이 돈다.

## 참고자료

- Linux 6.18.55 소스 (`/var/lab/src/linux-6.18.55`, 실제로 읽음): `fs/binfmt_elf.c` (`load_elf_binary`, PT_INTERP 검사), `arch/arm64/kernel/process.c` (`compat_elf_check_arch`), `include/uapi/linux/elf.h`, `include/uapi/linux/elf-em.h` (EM_ARM 40, EM_X86_64 62, EM_AARCH64 183, EM_RISCV 243)
- glibc `<elf.h>` (VM `/usr/include/elf.h`): `DT_RUNPATH` 29, `DF_1_PIE`, `EF_ARM_ABI_FLOAT_HARD` 0x400, `NT_GNU_BUILD_ID` 3
- [System V ABI — ELF gABI (Object Files, Program Loading, Dynamic Linking)](https://refspecs.linuxfoundation.org/elf/gabi4+/contents.html)
- [ELF for the Arm Architecture (AAELF32) — e_flags, EABI 버전](https://github.com/ARM-software/abi-aa/blob/main/aaelf32/aaelf32.rst)
- `man 8 ld.so` — 탐색 순서, `$ORIGIN`, `LD_DEBUG`, secure-execution 모드
- [QEMU user-mode emulation 문서 (`-L`, `-E`, `QEMU_LD_PREFIX`)](https://www.qemu.org/docs/master/user/main.html)
- [Linux kernel: Kernel Support for miscellaneous Binary Formats (binfmt_misc)](https://docs.kernel.org/admin-guide/binfmt-misc.html)
- 이 랩의 배경 노트: EmbeddedLinux `B_toolchain/2026-10-10_B1_cross_toolchain.md` (§5 qemu-user 와 sysroot), `2026-10-10_B3_elf_linking.md` (§5 soname · rpath/runpath)
- 레슨 교차 참조: ELX-B01 (부트 흐름), ELX-B04 (qemu-system + initramfs), ELX-B12 (rootfs 와 DT_NEEDED 전이 폐포), BM-B05 (펌웨어 링커 스크립트 · 섹션), OS-B08 (주소 공간 · ASLR), ARM-B01 (aarch64 호출 규약)
