# B3. ELF와 링킹 (Linux 관점) — 섹션과 세그먼트, 공유 라이브러리, PLT/GOT, 그리고 "파일이 있는데 not found"

> **이 노트를 다 읽으면**: `readelf -S`(섹션)와 `readelf -l`(세그먼트)의 차이를 말하고 커널·로더가 실제로 무엇을 보는지 설명할 수 있다 · soname 심볼릭 링크 체인, rpath/runpath, `LD_LIBRARY_PATH`, ld.so.cache로 공유 라이브러리를 만들고 배포할 수 있다 · PLT/GOT lazy binding을 gdb와 `LD_DEBUG`로 직접 보여 주고 RELRO를 설명할 수 있다 · "No such file or directory", "cannot open shared object file", "undefined symbol", "Exec format error"를 5분 안에 진단할 수 있다
> **왜 필요한가**: 보드에 앱을 올렸는데 `./app: not found`가 뜨는 장면은 Embedded Linux 첫 주에 반드시 만나고, 면접에서는 "PLT/GOT가 뭔가요?", "static vs dynamic linking 장단점은?", "커널 모듈은 어떻게 로드되나요?"로 나온다
> **Don 기준 난이도**: ELF·섹션·링커 스크립트·.map 파일·`addr2line`은 펌웨어에서 이미 쓴다 / 새로 배울 부분은 "실행 시간에 일어나는 링크" — 동적 로더, PIC/PIE, GOT, 라이브러리 탐색 순서
> **선행 노트**: A1(유저·커널 경계, execve), A5(mmap·가상 메모리), B1(크로스 툴체인) — B2(C 라이브러리)와 함께 읽으면 좋다

---

## 0. 큰 그림 — 이게 왜 필요한가

Don이 SSD 펌웨어를 빌드하던 장면을 떠올려 보자. `arm-none-eabi-gcc`로 `.c`를 `.o`로 만들고, 링커 스크립트(`MEMORY { FLASH ..., RAM ... }`)를 주고 `ld`가 모든 `.o`를 하나의 `firmware.elf`로 합친다. 이 순간 **모든 주소가 확정된다.** `.map` 파일을 열면 함수마다 절대 주소가 박혀 있고, `objcopy -O binary`로 뽑은 이미지를 플래시에 굽는다. 리셋 핸들러가 `.data`를 플래시에서 RAM으로 복사하고 `.bss`를 0으로 채운 뒤 `main()`으로 뛴다. **링크는 빌드할 때 한 번, 끝.**

Linux 유저 공간은 다르다. 링크가 **두 번** 일어난다.

1. **빌드 시간 링크(static link)**: `ld`가 `.o`들과 `.a`를 합쳐 실행 파일을 만든다. 여기까지는 펌웨어와 같다. 하지만 `printf` 같은 libc 함수는 **실행 파일에 들어가지 않는다.** "나중에 `libc.so.6`에서 `printf`를 찾아라"라는 메모만 남긴다.
2. **실행 시간 링크(dynamic link)**: 커널이 `execve()`로 프로그램을 메모리에 올리면, 먼저 **동적 로더**(`ld-linux-aarch64.so.1`, 줄여서 ld.so)가 돌아서 필요한 `.so`를 찾아 `mmap`하고, 메모 남겨진 자리(GOT)에 진짜 주소를 써 넣은 뒤에야 `main()`이 시작된다.

그리고 주소도 확정되지 않는다. 요즘 실행 파일은 **PIE**(Position-Independent Executable)라서 실행할 때마다 커널이 무작위 기준 주소에 올린다(**ASLR**). Don의 `.map` 파일에 적힌 주소는 "기준 주소로부터의 오프셋"일 뿐이다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="40" width="150" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="85" y="62" text-anchor="middle" font-size="13" font-weight="bold">1. 컴파일</text>
  <rect x="177" y="40" width="150" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="252" y="62" text-anchor="middle" font-size="13" font-weight="bold">2. 정적 링크</text>
  <rect x="344" y="40" width="150" height="34" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="419" y="62" text-anchor="middle" font-size="13" font-weight="bold">3. 로드 (커널)</text>
  <rect x="511" y="40" width="150" height="34" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="586" y="62" text-anchor="middle" font-size="13" font-weight="bold">4. 동적 링크</text>
  <line x1="160" y1="57" x2="175" y2="57" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="327" y1="57" x2="342" y2="57" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="494" y1="57" x2="509" y2="57" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="10" y="84" width="150" height="140" rx="5" fill="#4a7bd0" fill-opacity="0.06" stroke="#4a7bd0"/>
  <text x="20" y="106" font-size="12">gcc -c → hello.o</text>
  <text x="20" y="130" font-size="12">ET_REL (재배치 가능)</text>
  <text x="20" y="154" font-size="12">주소 미정 (0부터)</text>
  <text x="20" y="178" font-size="12">relocation 목록</text>
  <text x="20" y="202" font-size="12">외부 심볼 = U</text>
  <rect x="177" y="84" width="150" height="140" rx="5" fill="#4a7bd0" fill-opacity="0.06" stroke="#4a7bd0"/>
  <text x="187" y="106" font-size="12">ld → 실행 파일</text>
  <text x="187" y="130" font-size="12">섹션 병합·주소 배치</text>
  <text x="187" y="154" font-size="12">.a 에서 멤버 추출</text>
  <text x="187" y="178" font-size="12">-l 순서가 중요</text>
  <text x="187" y="202" font-size="12">PLT·GOT 만들기</text>
  <rect x="344" y="84" width="150" height="140" rx="5" fill="#e08a3c" fill-opacity="0.06" stroke="#e08a3c"/>
  <text x="354" y="106" font-size="12">execve()</text>
  <text x="354" y="130" font-size="12">ELF 헤더 검사</text>
  <text x="354" y="154" font-size="12">PT_LOAD → mmap</text>
  <text x="354" y="178" font-size="12">PT_INTERP 열기</text>
  <text x="354" y="202" font-size="12">스택·auxv 준비</text>
  <rect x="511" y="84" width="150" height="140" rx="5" fill="#e08a3c" fill-opacity="0.06" stroke="#e08a3c"/>
  <text x="521" y="106" font-size="12">ld.so (인터프리터)</text>
  <text x="521" y="130" font-size="12">DT_NEEDED 로드</text>
  <text x="521" y="154" font-size="12">재배치·GOT 채움</text>
  <text x="521" y="178" font-size="12">RELRO → 읽기 전용</text>
  <text x="521" y="202" font-size="12">init → main()</text>
  <line x1="10" y1="246" x2="327" y2="246" stroke="#4a7bd0" stroke-width="2"/>
  <text x="168" y="266" text-anchor="middle" font-size="13">빌드 시간 — 호스트 PC (크로스 툴체인)</text>
  <line x1="344" y1="246" x2="661" y2="246" stroke="#e08a3c" stroke-width="2"/>
  <text x="502" y="266" text-anchor="middle" font-size="13">실행 시간 — 타깃 보드</text>
  <text x="20" y="306" font-size="12">MCU 펌웨어: 1·2 단계만 있고, 3·4는 리셋 핸들러의 .data 복사 · .bss 0 채우기가 대신한다</text>
</svg>
```
그림 1 — Linux 유저 프로그램은 빌드 시간(1·2)과 실행 시간(3·4)에 걸쳐 두 번 링크된다. 펌웨어는 2에서 모든 주소가 끝난다.

Embedded Linux 엔지니어가 이 그림을 머릿속에 갖고 있어야 하는 장면:

- **"파일이 있는데 not found"**: 크로스 빌드한 앱을 보드에 올렸더니 `./sensord: not found`. 파일은 분명히 있다. 3단계에서 커널이 **PT_INTERP에 적힌 로더**를 못 찾은 것이다 (§8).
- **라이브러리 업데이트 사고**: `libimu.so`를 새 버전으로 올렸더니 다른 앱이 `undefined symbol`로 죽는다. 4단계 이야기다 (§5, §8).
- **크래시 덤프 해석**: 필드에서 받은 로그에 `pc=0xaaaab2c40cf0`만 있다. 이걸 소스 줄로 바꾸려면 ASLR 기준 주소를 빼고 debug 정보를 따로 보관하고 있어야 한다 (§9).
- **커널 모듈**: `.ko`는 `.o`와 같은 ET_REL 파일이고, **커널이 4단계(링크)를 직접 한다** (§11). 드라이버를 쓰는 Don에게 이 노트의 마지막 목적지다.

> 실행 환경: 모든 출력은 Lima VM `elinux`(aarch64, `uname -r` = `7.0.0-34-generic`, gcc 15.2, GNU ld 2.46, Apple Silicon 위 가상화)에서 실제로 돌린 결과다. 주소·PID·Build ID는 실행마다 다르다. Ubuntu gcc는 **기본이 PIE + `-z now` + `-z relro`** 라는 점을 기억해 두자(§6에서 중요).

---

## 1. ELF 파일 한 장 — 헤더와 세 가지 타입

### 1.1 직관 — "같은 포장, 다른 내용물"

ELF(Executable and Linkable Format)는 Linux의 **모든** 바이너리 포장 형식이다. `.o`, 실행 파일, `.so`, 커널 모듈 `.ko`, 심지어 `vmlinux`와 코어 덤프도 ELF다. Don이 펌웨어에서 쓰던 `firmware.elf`와 **같은 형식**이다. 차이는 헤더의 `e_type` 필드 하나와, 그에 따라 어떤 테이블이 채워져 있느냐다.

| `e_type` | 이름 | 예 | 주소가 정해졌나 | 누가 소비하나 |
|---|---|---|---|---|
| `ET_REL` (1) | relocatable | `hello.o`, `b3_elfmod.ko` | 아니오 (섹션마다 0부터) | 링커 `ld`, 커널 모듈 로더 |
| `ET_EXEC` (2) | executable | `-no-pie`로 만든 실행 파일, 펌웨어 `.elf` | 예 (절대 주소, 기본 0x400000) | 커널 `execve` |
| `ET_DYN` (3) | shared object | `.so`, **PIE 실행 파일** | 기준 주소 0, 로드 시 결정 | 커널 + ld.so |
| `ET_CORE` (4) | core | 코어 덤프 | — | gdb |

PIE 실행 파일이 `.so`와 같은 `ET_DYN`이라는 것이 첫 번째 놀라운 점이다. PIE는 "`main`이 있는 공유 라이브러리"라고 생각하면 정확하다.

### 1.2 예제 1 — 같은 소스로 네 가지 ELF 만들기

무엇을 확인하나: 같은 `hello.c`를 `.o`, PIE, non-PIE, static으로 빌드해 `e_type`이 어떻게 다른지 본다.

```c
/* ex1: 아주 작은 프로그램 — 섹션별로 무엇이 어디 들어가는지 보기 위한 재료 */
#include <stdio.h>

int g_init = 42;          /* .data   : 초기값 있는 전역 */
int g_zero;               /* .bss    : 0 초기화 전역 (파일에 공간 없음) */
const char g_msg[] = "hello ELF";   /* .rodata : 읽기 전용 상수 */

int main(void)
{
    g_zero = g_init + 1;
    printf("%s %d %d\n", g_msg, g_init, g_zero);
    return 0;
}
```

```sh
S=/Users/donh/.elinux_scratch/B3
gcc -Wall -Wextra -O2 -c $S/hello.c -o hello.o
gcc -Wall -Wextra -O2 $S/hello.c -o hello_pie
gcc -Wall -Wextra -O2 -no-pie $S/hello.c -o hello_nopie
gcc -O2 -static $S/hello.c -o hello_static
./hello_pie
file hello.o hello_pie hello_nopie hello_static /usr/lib/aarch64-linux-gnu/libc.so.6
for f in hello.o hello_pie hello_nopie hello_static; do printf "%-13s " $f; readelf -h $f | grep -E "Type:" ; done
```

```text
hello ELF 42 43
hello.o:                              ELF 64-bit LSB relocatable, ARM aarch64, version 1 (SYSV), not stripped
hello_pie:                            ELF 64-bit LSB pie executable, ARM aarch64, version 1 (SYSV), dynamically linked, interpreter /lib/ld-linux-aarch64.so.1, BuildID[sha1]=3dbb12298442778de37c92e5f7ffa37e06177d5d, for GNU/Linux 3.7.0, not stripped
hello_nopie:                          ELF 64-bit LSB executable, ARM aarch64, version 1 (SYSV), dynamically linked, interpreter /lib/ld-linux-aarch64.so.1, BuildID[sha1]=c7e4526d3a262338856e0610ef9127566d5830c5, for GNU/Linux 3.7.0, not stripped
hello_static:                         ELF 64-bit LSB executable, ARM aarch64, version 1 (GNU/Linux), statically linked, BuildID[sha1]=d410e78f1c22f40fb4d3165839e9d46c54cab73a, for GNU/Linux 3.7.0, not stripped
/usr/lib/aarch64-linux-gnu/libc.so.6: ELF 64-bit LSB shared object, ARM aarch64, version 1 (GNU/Linux), dynamically linked, interpreter /lib/ld-linux-aarch64.so.1, BuildID[sha1]=5697d481680bea92debb50f8a02893668fb7f6e1, for GNU/Linux 3.7.0, stripped
hello.o         Type:                              REL (Relocatable file)
hello_pie       Type:                              DYN (Position-Independent Executable file)
hello_nopie     Type:                              EXEC (Executable file)
hello_static    Type:                              EXEC (Executable file)
```

출력에서 볼 것: `file`이 말하는 "interpreter /lib/ld-linux-aarch64.so.1" — 동적 링크된 실행 파일은 모두 **이 로더 경로가 하드코딩**되어 있다. 정적 링크(`hello_static`)에는 interpreter가 없다. 이 한 줄이 §8의 "not found" 미스터리의 열쇠다.

### 1.3 ELF 헤더 읽기

```sh
readelf -h hello_pie
```

```text
ELF Header:
  Magic:   7f 45 4c 46 02 01 01 00 00 00 00 00 00 00 00 00 
  Class:                             ELF64
  Data:                              2's complement, little endian
  Version:                           1 (current)
  OS/ABI:                            UNIX - System V
  ABI Version:                       0
  Type:                              DYN (Position-Independent Executable file)
  Machine:                           AArch64
  Version:                           0x1
  Entry point address:               0x6c0
  Start of program headers:          64 (bytes into file)
  Start of section headers:          68760 (bytes into file)
  Flags:                             0x0
  Size of this header:               64 (bytes)
  Size of program headers:           56 (bytes)
  Number of program headers:         10
  Size of section headers:           64 (bytes)
  Number of section headers:         29
  Section header string table index: 28
```

64바이트짜리 헤더가 말하는 것은 딱 세 가지다.

1. **이 파일을 누가 실행할 수 있나**: Magic `7f 45 4c 46`(`\x7fELF`), Class(32/64), Data(엔디언), Machine(AArch64). 커널은 여기서 하나라도 안 맞으면 `ENOEXEC`("Exec format error")를 돌려준다 (§8).
2. **어디서 시작하나**: Entry point `0x6c0` — PIE라서 기준 주소 0 기준 오프셋이다. 실제 주소는 "로드 기준 + 0x6c0"이다. 이 값이 `main`이 아니라 `_start`(crt1의 시작 코드)라는 것도 펌웨어의 리셋 벡터 → `Reset_Handler` → `main`과 같은 구조다.
3. **두 개의 지도가 어디 있나**: Program headers는 파일 offset 64부터 10개, Section headers는 68760부터 29개. 이 두 지도가 다음 절의 주제다.

---

## 2. 섹션 vs 세그먼트 — 링커의 지도, 로더의 지도

### 2.1 직관

Don의 펌웨어 링커 스크립트에서 `.text`, `.rodata`, `.data`, `.bss`는 **섹션**이다. 그리고 그것들을 `> FLASH`, `> RAM AT> FLASH`로 **어느 메모리 영역에 둘지** 정했다. Linux ELF도 똑같이 두 층이다.

- **섹션(section)**: 링커와 디버거가 쓰는 **세밀한 단위**. `.text`, `.rodata`, `.data`, `.bss`, `.symtab`, `.debug_info` … `readelf -S`로 본다. 섹션 헤더 테이블이 목차다.
- **세그먼트(segment)**: 커널과 ld.so가 쓰는 **굵은 단위**. "파일의 이 범위를 이 권한(R/W/X)으로 이 가상 주소에 매핑하라". `readelf -l`로 본다. 프로그램 헤더 테이블이 목차다. 비유하자면 링커 스크립트의 `MEMORY` 영역 + MPU 리전 설정이다.

여러 섹션이 권한이 같으면 하나의 `PT_LOAD` 세그먼트로 묶인다. **커널은 섹션 헤더를 아예 보지 않는다.** 실행에 필요한 것은 프로그램 헤더뿐이다.

```svg
<svg viewBox="0 0 680 440" xmlns="http://www.w3.org/2000/svg">
  <text x="250" y="22" text-anchor="middle" font-size="13" font-weight="bold">섹션 (readelf -S) — 링커·디버거</text>
  <text x="572" y="22" text-anchor="middle" font-size="13" font-weight="bold">세그먼트 (readelf -l)</text>
  <text x="572" y="38" text-anchor="middle" font-size="12">커널·ld.so 가 보는 것</text>
  <rect x="140" y="52" width="220" height="24" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="250" y="69" text-anchor="middle" font-size="12">ELF header (64 B)</text>
  <rect x="140" y="76" width="220" height="24" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="250" y="93" text-anchor="middle" font-size="12">program headers</text>
  <rect x="140" y="100" width="220" height="24" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="250" y="117" text-anchor="middle" font-size="12">.note.gnu.build-id .interp</text>
  <rect x="140" y="124" width="220" height="24" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="250" y="141" text-anchor="middle" font-size="12">.gnu.hash .dynsym .dynstr</text>
  <rect x="140" y="148" width="220" height="24" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="250" y="165" text-anchor="middle" font-size="12">.rela.dyn .rela.plt</text>
  <rect x="140" y="172" width="220" height="24" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="250" y="189" text-anchor="middle" font-size="12">.init .plt .text .fini</text>
  <rect x="140" y="196" width="220" height="24" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="250" y="213" text-anchor="middle" font-size="12">.rodata .eh_frame</text>
  <rect x="140" y="220" width="220" height="20" fill="none" stroke="#888" stroke-dasharray="4 3"/>
  <text x="250" y="234" text-anchor="middle" font-size="12">정렬 패딩 (64K)</text>
  <rect x="140" y="240" width="220" height="24" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="250" y="257" text-anchor="middle" font-size="12">.init_array .dynamic .got</text>
  <rect x="140" y="264" width="220" height="24" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="250" y="281" text-anchor="middle" font-size="12">.data</text>
  <rect x="140" y="288" width="220" height="24" fill="none" stroke="#e08a3c" stroke-dasharray="4 3"/>
  <text x="250" y="305" text-anchor="middle" font-size="12">.bss (파일 공간 0)</text>
  <rect x="140" y="312" width="220" height="24" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="250" y="329" text-anchor="middle" font-size="12">.comment .symtab .strtab</text>
  <rect x="140" y="336" width="220" height="24" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="250" y="353" text-anchor="middle" font-size="12">.debug_info ... (-g 일 때)</text>
  <rect x="140" y="360" width="220" height="24" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="250" y="377" text-anchor="middle" font-size="12">section headers</text>
  <text x="132" y="93" text-anchor="end" font-size="12">e_phoff →</text>
  <text x="132" y="189" text-anchor="end" font-size="12">e_entry →</text>
  <text x="132" y="377" text-anchor="end" font-size="12">e_shoff →</text>
  <rect x="375" y="52" width="140" height="168" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="445" y="130" text-anchor="middle" font-size="13">LOAD 0 : R E</text>
  <text x="445" y="150" text-anchor="middle" font-size="12">코드 + 읽기 전용</text>
  <rect x="375" y="240" width="140" height="72" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="445" y="270" text-anchor="middle" font-size="13">LOAD 1 : RW</text>
  <text x="445" y="290" text-anchor="middle" font-size="12">MemSiz > FileSiz</text>
  <rect x="375" y="312" width="140" height="72" rx="4" fill="none" stroke="#888" stroke-dasharray="4 3"/>
  <text x="445" y="342" text-anchor="middle" font-size="13">로드 안 됨</text>
  <text x="445" y="362" text-anchor="middle" font-size="12">strip 이 지우는 곳</text>
  <rect x="530" y="100" width="140" height="24" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="600" y="117" text-anchor="middle" font-size="12">INTERP</text>
  <rect x="530" y="240" width="140" height="24" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="600" y="257" text-anchor="middle" font-size="12">GNU_RELRO → R</text>
  <rect x="530" y="268" width="140" height="22" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="600" y="284" text-anchor="middle" font-size="12">DYNAMIC</text>
  <text x="600" y="190" text-anchor="middle" font-size="12">GNU_STACK: 스택 NX</text>
  <text x="20" y="412" font-size="12">커널은 section header 를 보지 않는다 — strip --strip-section-headers 후에도 실행된다 (§2.4)</text>
</svg>
```
그림 2 — 하나의 ELF 파일에 두 개의 목차가 있다. 왼쪽 섹션은 링커·디버거용, 오른쪽 세그먼트는 실행용. 실행에 필요 없는 섹션(`.symtab`, `.debug_info` 등)은 어떤 세그먼트에도 속하지 않는다.

### 2.2 예제 2 — `readelf -S`와 `readelf -l`을 나란히

무엇을 확인하나: 같은 `hello_pie`의 섹션 29개가 세그먼트 10개로 어떻게 묶이는지 본다.

```sh
readelf -S -W hello_pie
```

```text
There are 29 section headers, starting at offset 0x10c98:

Section Headers:
  [Nr] Name              Type            Address          Off    Size   ES Flg Lk Inf Al
  [ 0]                   NULL            0000000000000000 000000 000000 00      0   0  0
  [ 1] .note.gnu.build-id NOTE            0000000000000270 000270 000024 00   A  0   0  4
  [ 2] .interp           PROGBITS        0000000000000294 000294 00001b 00   A  0   0  1
  [ 3] .gnu.hash         GNU_HASH        00000000000002b0 0002b0 00001c 00   A  4   0  8
  [ 4] .dynsym           DYNSYM          00000000000002d0 0002d0 0000f0 18   A  5   3  8
  [ 5] .dynstr           STRTAB          00000000000003c0 0003c0 00009a 00   A  0   0  1
  [ 6] .gnu.version      VERSYM          000000000000045a 00045a 000014 02   A  4   0  2
  [ 7] .gnu.version_r    VERNEED         0000000000000470 000470 000030 00   A  5   1  8
  [ 8] .rela.dyn         RELA            00000000000004a0 0004a0 0000c0 18   A  4   0  8
  [ 9] .rela.plt         RELA            0000000000000560 000560 000078 18  AI  4  21  8
  [10] .init             PROGBITS        00000000000005d8 0005d8 00001c 00  AX  0   0  4
  [11] .plt              PROGBITS        0000000000000600 000600 000070 00  AX  0   0 16
  [12] .text             PROGBITS        0000000000000680 000680 000168 00  AX  0   0 64
  [13] .fini             PROGBITS        00000000000007e8 0007e8 000018 00  AX  0   0  4
  [14] .rodata           PROGBITS        0000000000000800 000800 000022 00   A  0   0  8
  [15] .eh_frame_hdr     PROGBITS        0000000000000824 000824 00003c 00   A  0   0  4
  [16] .eh_frame         PROGBITS        0000000000000860 000860 0000b4 00   A  0   0  8
  [17] .note.ABI-tag     NOTE            0000000000000914 000914 000020 00   A  0   0  4
  [18] .init_array       INIT_ARRAY      000000000001fd90 00fd90 000008 08  WA  0   0  8
  [19] .fini_array       FINI_ARRAY      000000000001fd98 00fd98 000008 08  WA  0   0  8
  [20] .dynamic          DYNAMIC         000000000001fda0 00fda0 0001f0 10  WA  5   0  8
  [21] .got              PROGBITS        000000000001ff90 00ff90 000070 08  WA  0   0  8
  [22] .data             PROGBITS        0000000000020000 010000 000014 00  WA  0   0  8
  [23] .bss              NOBITS          0000000000020014 010014 00000c 00  WA  0   0  4
  [24] .comment          PROGBITS        0000000000000000 010014 000026 01  MS  0   0  1
  [25] .ARM.attributes   AARCH64_ATTRIBUTES 0000000000000000 01003c 000024 00      0   0  4
  [26] .symtab           SYMTAB          0000000000000000 010060 0008e8 18     27  69  8
  [27] .strtab           STRTAB          0000000000000000 010948 000243 00      0   0  1
  [28] .shstrtab         STRTAB          0000000000000000 010b8b 00010a 00      0   0  1
...
```
(Key to Flags 설명 줄은 생략했다.)

읽는 법: `Flg`의 `A`(alloc)가 붙은 섹션만 메모리에 올라간다. `.comment`, `.symtab`, `.strtab`은 `A`가 없고 Address가 0이다 — **실행에 필요 없다**. `.bss`는 Type이 `NOBITS`라 파일 offset만 있고 공간을 차지하지 않는다(펌웨어에서 `.bss`가 이미지 크기에 안 잡히는 것과 같다).

```sh
readelf -l -W hello_pie
```

```text
Elf file type is DYN (Position-Independent Executable file)
Entry point 0x6c0
There are 10 program headers, starting at offset 64

Program Headers:
  Type           Offset   VirtAddr           PhysAddr           FileSiz  MemSiz   Flg Align
  PHDR           0x000040 0x0000000000000040 0x0000000000000040 0x000230 0x000230 R   0x8
  INTERP         0x000294 0x0000000000000294 0x0000000000000294 0x00001b 0x00001b R   0x1
      [Requesting program interpreter: /lib/ld-linux-aarch64.so.1]
  LOAD           0x000000 0x0000000000000000 0x0000000000000000 0x000934 0x000934 R E 0x10000
  LOAD           0x00fd90 0x000000000001fd90 0x000000000001fd90 0x000284 0x000290 RW  0x10000
  DYNAMIC        0x00fda0 0x000000000001fda0 0x000000000001fda0 0x0001f0 0x0001f0 RW  0x8
  NOTE           0x000270 0x0000000000000270 0x0000000000000270 0x000024 0x000024 R   0x4
  NOTE           0x000914 0x0000000000000914 0x0000000000000914 0x000020 0x000020 R   0x4
  GNU_EH_FRAME   0x000824 0x0000000000000824 0x0000000000000824 0x00003c 0x00003c R   0x4
  GNU_STACK      0x000000 0x0000000000000000 0x0000000000000000 0x000000 0x000000 RW  0x10
  GNU_RELRO      0x00fd90 0x000000000001fd90 0x000000000001fd90 0x000270 0x000270 R   0x1

 Section to Segment mapping:
  Segment Sections...
   00     
   01     .interp 
   02     .note.gnu.build-id .interp .gnu.hash .dynsym .dynstr .gnu.version .gnu.version_r .rela.dyn .rela.plt .init .plt .text .fini .rodata .eh_frame_hdr .eh_frame .note.ABI-tag 
   03     .init_array .fini_array .dynamic .got .data .bss 
   04     .dynamic 
   05     .note.gnu.build-id 
   06     .note.ABI-tag 
   07     .eh_frame_hdr 
   08     
   09     .init_array .fini_array .dynamic .got 
```

출력에서 볼 것:

- **LOAD 두 개**가 전부다. 02번(R E) = 코드+읽기 전용 데이터 17개 섹션, 03번(RW) = 쓰기 가능한 6개 섹션. 커널은 이 두 줄만 보고 `mmap`을 두 번 한다.
- 03번 LOAD의 `FileSiz 0x284 < MemSiz 0x290`: 차이 0xc 바이트가 `.bss`다. 커널이 파일에 없는 뒷부분을 0으로 채운다. **펌웨어 리셋 핸들러의 `.bss` 0 채우기를 커널이 대신하는 것**이다.
- `Align 0x10000`(64K): aarch64 툴체인은 16K·64K 페이지 커널에서도 돌도록 기본 `max-page-size`를 64K로 잡는다. 그래서 두 LOAD 사이에 파일 offset 0x934 → 0xfd90의 빈 공간이 생긴다(§10의 크기 차트에서 다시 본다).
- `GNU_RELRO`는 `.init_array`~`.got`: 재배치가 끝나면 **읽기 전용으로 바꿔 달라**는 요청이다(§6.4).
- `GNU_STACK`의 `RW`(E 없음): 스택은 실행 불가(NX). 펌웨어에서 MPU로 스택에 XN을 걸던 것과 같다.

### 2.3 예제 3 — `.o` 안의 relocation: "여기는 나중에 채워 주세요"

무엇을 확인하나: `hello.o`(ET_REL)에서 `main`이 전역 변수와 `printf`를 가리키는 자리가 아직 0이고, relocation 엔트리가 그 자리를 표시하고 있다는 것.

```sh
readelf -r hello.o
objdump -dr --no-show-raw-insn hello.o | sed -n "/<main>/,\$p"
```

```text
Relocation section '.rela.text.startup' at offset 0x350 contains 9 entries:
  Offset          Info           Type           Sym. Value    Sym. Name + Addend
000000000004  000300000113 R_AARCH64_ADR_PRE 0000000000000000 .data + 0
000000000008  000900000113 R_AARCH64_ADR_PRE 0000000000000000 .rodata + 0
000000000010  00030000011d R_AARCH64_LDST32_ 0000000000000000 .data + 0
000000000014  000400000113 R_AARCH64_ADR_PRE 0000000000000000 .bss + 0
000000000018  000900000115 R_AARCH64_ADD_ABS 0000000000000000 .rodata + 0
000000000020  000500000113 R_AARCH64_ADR_PRE 0000000000000000 .rodata.str1.8 + 0
000000000024  00040000011d R_AARCH64_LDST32_ 0000000000000000 .bss + 0
000000000028  000500000115 R_AARCH64_ADD_ABS 0000000000000000 .rodata.str1.8 + 0
000000000030  00120000011b R_AARCH64_CALL26  0000000000000000 __printf_chk + 0

Relocation section '.rela.eh_frame' at offset 0x428 contains 1 entry:
  Offset          Info           Type           Sym. Value    Sym. Name + Addend
00000000001c  000700000105 R_AARCH64_PREL32  0000000000000000 .text.startup + 0
0000000000000000 <main>:
   0:	stp	x29, x30, [sp, #-16]!
   4:	adrp	x0, 0 <main>
			4: R_AARCH64_ADR_PREL_PG_HI21	.data
   8:	adrp	x2, 0 <main>
			8: R_AARCH64_ADR_PREL_PG_HI21	.rodata
   c:	mov	x29, sp
  10:	ldr	w3, [x0]
			10: R_AARCH64_LDST32_ABS_LO12_NC	.data
  14:	adrp	x0, 0 <main>
			14: R_AARCH64_ADR_PREL_PG_HI21	.bss
  18:	add	x2, x2, #0x0
			18: R_AARCH64_ADD_ABS_LO12_NC	.rodata
  1c:	add	w4, w3, #0x1
  20:	adrp	x1, 0 <main>
			20: R_AARCH64_ADR_PREL_PG_HI21	.rodata.str1.8
  24:	str	w4, [x0]
			24: R_AARCH64_LDST32_ABS_LO12_NC	.bss
  28:	add	x1, x1, #0x0
			28: R_AARCH64_ADD_ABS_LO12_NC	.rodata.str1.8
  2c:	mov	w0, #0x2                   	// #2
  30:	bl	0 <__printf_chk>
			30: R_AARCH64_CALL26	__printf_chk
  34:	mov	w0, #0x0                   	// #0
  38:	ldp	x29, x30, [sp], #16
  3c:	ret
```

출력에서 볼 것:

- `adrp x0, 0`, `bl 0` — 모든 주소가 0이다. 그 아래 `R_AARCH64_...` 줄이 "이 명령의 이 필드를 나중에 채워라"라는 메모다. `ADR_PREL_PG_HI21` + `ADD_ABS_LO12_NC` 쌍은 aarch64에서 PC 기준 ±4GB 주소를 만드는 `adrp`+`add` 패턴이고, `CALL26`은 `bl`의 26비트 오프셋이다. Cortex-M 펌웨어에서 보던 `R_ARM_THM_CALL`과 같은 종류다.
- `printf`가 `__printf_chk`로 바뀌어 있다. Ubuntu gcc는 `-O2`에서 `_FORTIFY_SOURCE`를 기본으로 켜서 버퍼 검사판 함수로 바꾼다. 크로스 툴체인마다 기본값이 다르다는 예다.
- `main`이 `.text`가 아니라 `.text.startup`에 들어갔다. GCC가 "한 번만 불리는 시작 코드"를 따로 모은다. 섹션 이름은 컴파일러가 마음대로 만들 수 있고, 링커 스크립트가 그걸 `*(.text .text.*)`로 모은다.

정적 링커 `ld`는 이 relocation 중 **해결할 수 있는 것은 모두 해결**하고(`.data`, `.rodata`의 위치는 이제 안다), libc에 있는 `__printf_chk`처럼 해결 못 하는 것은 **PLT 스텁 + GOT 슬롯 + 동적 relocation(`.rela.plt`)** 으로 바꿔 실행 시간으로 넘긴다(§6).

### 2.4 예제 4 — 섹션 헤더를 통째로 지워도 실행된다

무엇을 확인하나: 커널과 ld.so는 섹션 헤더 없이 프로그램 헤더만으로 실행한다는 것. binutils 2.42부터 있는 `strip --strip-section-headers`를 쓴다.

```sh
cp hello_pie hello_nosh && strip --strip-section-headers hello_nosh && readelf -S hello_nosh; ./hello_nosh; ls -l hello_pie hello_nosh
```

```text
There are no sections in this file.
hello ELF 42 43
-rwxrwxr-x 1 donh donh 65556 Oct 10 07:58 hello_nosh
-rwxrwxr-x 1 donh donh 70616 Oct 10 07:58 hello_pie
```

출력에서 볼 것: "There are no sections" — 그래도 잘 돈다. 반대로 `objdump -d`, `gdb`, `nm`은 이 파일에서 거의 아무것도 못 한다. **섹션 = 개발 도구용 메타데이터, 세그먼트 = 실행용 지시서**라는 구분을 몸으로 기억하자.

### 2.5 펌웨어 링커 스크립트와 비교

| 개념 | MCU 펌웨어 (Don의 경험) | Linux 유저 공간 ELF |
|---|---|---|
| 메모리 영역 | `MEMORY { FLASH, RAM }` 물리 주소 | 없음. 가상 주소 공간 전체가 내 것 |
| `.data` 초기값 | LMA(플래시) ≠ VMA(RAM), 리셋 핸들러가 복사 | 파일 offset → 같은 VMA로 `mmap` (copy-on-write) |
| `.bss` | 리셋 핸들러가 0으로 채움 | `MemSiz > FileSiz` 부분을 커널이 0 페이지로 |
| 권한 | MPU 리전 (선택) | 세그먼트 `Flg` R/W/E → MMU 페이지 권한 |
| 주소 확정 | 링크 시점 | PIE면 로드 시점 (ASLR) |
| 외부 라이브러리 | 전부 정적 링크 | 대부분 `.so`, 실행 시간 링크 |
| 기본 링커 스크립트 | 벤더가 준 `.ld` 파일 | `ld --verbose`로 보이는 내장 스크립트 |

---

## 3. PIE와 ASLR — 주소는 실행할 때 정해진다

### 3.1 직관

PIC(Position-Independent Code)는 "어느 주소에 올려도 돌아가는 코드"다. aarch64에서는 `adrp`(PC 기준 페이지 주소)가 기본이라 PIC가 거의 공짜다. PIE는 실행 파일 전체를 PIC로 만든 것이고, 덕분에 커널이 **실행할 때마다 다른 기준 주소**에 올릴 수 있다. 이것이 ASLR(Address Space Layout Randomization)이다. 공격자가 "`system()`은 항상 0x400a10에 있다" 같은 가정을 못 하게 만드는 보안 기능이다.

### 3.2 예제 5 — 실행할 때마다 바뀌는 주소

무엇을 확인하나: PIE vs non-PIE에서 코드·데이터·libc·힙·스택 주소가 실행마다 바뀌는지, 그리고 `setarch -R`로 ASLR을 끌 수 있는지.

```c
/* ex2: PIE/ASLR — 실행할 때마다 코드·데이터·libc·스택·힙 주소가 바뀌는지 본다 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_data = 1;

int main(int argc, char **argv)
{
    int local = 0;
    void *heap = malloc(16);
    if (!heap) { perror("malloc"); exit(1); }
    printf("main=%p g_data=%p printf=%p heap=%p stack=%p\n",
           (void *)main, (void *)&g_data, (void *)printf, heap, (void *)&local);
    if (argc > 1) {                     /* 인자 있으면 자기 maps 중 바이너리 매핑만 출력 */
        FILE *f = fopen("/proc/self/maps", "r");
        if (!f) { perror("fopen"); exit(1); }
        char line[256];
        while (fgets(line, sizeof line, f))
            if (strstr(line, argv[0] + 2))   /* "./aslr_pie" → "aslr_pie" */
                fputs(line, stdout);
        fclose(f);
    }
    free(heap);
    return 0;
}
```

```sh
gcc -Wall -Wextra -O2 $S/aslr.c -o aslr_pie
gcc -Wall -Wextra -O2 -no-pie $S/aslr.c -o aslr_nopie
cat /proc/sys/kernel/randomize_va_space
for i in 1 2; do ./aslr_pie; done
for i in 1 2; do ./aslr_nopie; done
setarch -R ./aslr_pie; setarch -R ./aslr_pie
./aslr_pie maps
```

```text
2
main=0xc2c9eb980a40 g_data=0xc2c9eb9a0010 printf=0xec815078f8a0 heap=0xc2c9ffe54010 stack=0xffffdf332c94
main=0xad4c23790a40 g_data=0xad4c237b0010 printf=0xf8dd50f6f8a0 heap=0xad4c4b46a010 stack=0xfffff8c46ef4
main=0x400940 g_data=0x420080 printf=0xfdc13943f8a0 heap=0xa57d010 stack=0xffffc346df54
main=0x400940 g_data=0x420080 printf=0xf22daf83f8a0 heap=0x2be8010 stack=0xffffef138d64
main=0xaaaaaaaa0a40 g_data=0xaaaaaaac0010 printf=0xfffff7e2f8a0 heap=0xaaaaaaac1010 stack=0xfffffffff864
main=0xaaaaaaaa0a40 g_data=0xaaaaaaac0010 printf=0xfffff7e2f8a0 heap=0xaaaaaaac1010 stack=0xfffffffff864
main=0xae48ca380a40 g_data=0xae48ca3a0010 printf=0xf4591947f8a0 heap=0xae4907e04010 stack=0xfffff15bef04
ae48ca380000-ae48ca381000 r-xp 00000000 00:25 2987                       /tmp/B3/aslr_pie
ae48ca39f000-ae48ca3a0000 r--p 0000f000 00:25 2987                       /tmp/B3/aslr_pie
ae48ca3a0000-ae48ca3a1000 rw-p 00010000 00:25 2987                       /tmp/B3/aslr_pie
```

출력에서 볼 것:

- `randomize_va_space = 2`: 스택·mmap·힙(brk)까지 전부 무작위화.
- PIE: `main`이 매번 다르다. 그런데 **끝 세 자리(`a40`)는 항상 같다** — 기준 주소는 페이지 단위로만 바뀌고, 오프셋(`main`은 파일 기준 0xa40)은 고정이다. 크래시 주소를 소스 줄로 바꿀 때 이 성질을 쓴다(§9).
- non-PIE: `main=0x400940`이 고정. 기본 링커 스크립트의 `SEGMENT_START("text-segment", 0x400000)` 그대로다. 그래도 `printf`(libc), 힙, 스택은 바뀐다 — `.so`는 원래 PIC라서 늘 무작위 위치다.
- `setarch -R`(= `personality(ADDR_NO_RANDOMIZE)`): 두 번 다 `0xaaaaaaaa0a40`. gdb가 기본으로 이걸 켜서 디버깅 중 주소가 안 바뀐다.
- `/proc/self/maps`: 파일 offset 0 → `r-xp`, offset `0xf000` → `r--p`, `0x10000` → `rw-p`. 세그먼트는 두 개였는데 매핑은 세 개다. 가운데 `r--p`는 **RELRO 영역을 ld.so가 `mprotect`로 읽기 전용으로 바꾼 것**이다(§6.4). 이 VM 커널은 4K 페이지라서 64K 정렬된 세그먼트도 4K 단위로 쪼개 매핑된다.

> 펌웨어와의 연결: Don의 SSD 펌웨어 크래시 로그는 PC 값을 그대로 `.map`에서 찾으면 됐다. Linux에서는 **"PC − 그 모듈의 로드 기준 주소 = 파일 오프셋"** 을 먼저 계산해야 한다. 기준 주소는 `/proc/<pid>/maps`나 크래시 핸들러의 `dladdr()`로 얻는다.

---

## 4. 정적 링킹 — `.a` 아카이브와 순서 함정

### 4.1 직관

`.a`(static archive)는 `.o` 파일들을 `ar`로 묶은 **zip 같은 상자**일 뿐이다. GNU ld(BFD)는 명령줄을 **왼쪽에서 오른쪽으로 한 번만** 훑는다. `.a`를 만나면 "**지금까지** 미해결인 심볼을 정의하는 멤버만" 꺼내 넣고 지나간다. 나중에 다른 멤버가 그 아카이브의 심볼을 필요로 해도 **되돌아가지 않는다.** 그래서 규칙은 "**쓰는 쪽이 왼쪽, 제공하는 쪽이 오른쪽**"이다.

### 4.2 예제 6 — 센서 라이브러리 순서 사고

무엇을 확인하나: `main → sensor_read()`(libsensor.a) → `i2c_xfer()`(libi2c.a)일 때 `-l` 순서에 따라 링크가 성공/실패하는지.

```c
/* ex3: 정적 링킹 순서 — main → sensor_read() (libsensor.a) → i2c_xfer() (libi2c.a) */
#include <stdio.h>
int sensor_read(void);
int main(void) { printf("sensor=%d\n", sensor_read()); return 0; }
```

```c
/* sensor.c */
int i2c_xfer(int addr);
int sensor_read(void) { return i2c_xfer(0x68); }   /* libi2c.a 의 심볼을 필요로 함 */
```

```c
/* i2c.c */
int i2c_xfer(int addr) { return addr + 1; }
```

```sh
for f in main sensor i2c; do gcc -Wall -Wextra -O2 -c $S/$f.c -o $f.o; done
ar rcs libsensor.a sensor.o && ar rcs libi2c.a i2c.o
ar t libsensor.a && nm libsensor.a
echo "--- wrong order: -li2c -lsensor"
gcc main.o -L. -li2c -lsensor -o app; echo "exit=$?"
echo "--- right order: -lsensor -li2c"
gcc main.o -L. -lsensor -li2c -o app && ./app
echo "--- group"
gcc main.o -L. -Wl,--start-group -li2c -lsensor -Wl,--end-group -o app2 && ./app2
echo "--- trace"
gcc main.o -L. -lsensor -li2c -o app3 -Wl,--trace-symbol=i2c_xfer -Wl,-Map=app3.map
sed -n "1,6p" app3.map
```

```text
sensor.o

sensor.o:
                 U i2c_xfer
0000000000000000 T sensor_read
--- wrong order: -li2c -lsensor
/usr/bin/aarch64-linux-gnu-ld.bfd: ./libsensor.a(sensor.o): in function `sensor_read':
sensor.c:(.text+0x4): undefined reference to `i2c_xfer'
collect2: error: ld returned 1 exit status
exit=1
--- right order: -lsensor -li2c
sensor=105
--- group
sensor=105
--- trace
/usr/bin/aarch64-linux-gnu-ld.bfd: ./libsensor.a(sensor.o): reference to i2c_xfer
/usr/bin/aarch64-linux-gnu-ld.bfd: ./libi2c.a(i2c.o): definition of i2c_xfer

Archive member included to satisfy reference by file (symbol)

./libsensor.a(sensor.o)       main.o (sensor_read)
./libi2c.a(i2c.o)             ./libsensor.a(sensor.o) (i2c_xfer)
```

출력에서 볼 것:

- 잘못된 순서: `-li2c`를 지나갈 때는 아직 아무도 `i2c_xfer`를 원하지 않아서 아무것도 안 꺼냈다. 그 뒤 `sensor.o`가 `i2c_xfer`를 원했지만 이미 지나간 뒤다. 에러 메시지 "**undefined reference**"는 **빌드 시간(정적 링크)** 에러다. 실행 시간의 "**undefined symbol**"(§5.5)과 구분하자.
- `--start-group ... --end-group`: 그룹 안 아카이브를 해결될 때까지 반복해서 훑는다. 상호 의존(A↔B) 아카이브에 필요하다. 느려질 수 있어서 기본값은 아니다.
- `.map` 파일의 "Archive member included to satisfy reference by file" 표는 **왜 이 멤버가 들어왔는지**를 보여 준다. 펌웨어에서 "이 함수가 왜 이미지에 들어왔지?"를 추적하던 그 표다. Linux에서도 `-Wl,-Map=`은 똑같이 쓴다.

LLVM의 lld는 이 순서에 관대하다(아카이브 심볼을 기억해 둔다). 같은 잘못된 순서로 `-fuse-ld=lld`를 주면 성공한다.

```sh
gcc main.o -L. -li2c -lsensor -fuse-ld=lld -o app_lld && ./app_lld
```

```text
sensor=105
```

그래서 "내 PC(lld)에서는 되는데 CI(GNU ld)에서 안 된다"가 생긴다. 이식성 있는 빌드는 항상 **의존 순서대로** 쓴다.

### 4.3 정적 링크된 실행 파일

`gcc -static`은 libc까지 실행 파일 안에 넣는다. INTERP도 DT_NEEDED도 없으니 **로더 문제가 원천적으로 없다** — initramfs의 복구 도구, 부트 직후 도는 작은 init, 컨테이너의 `scratch` 이미지에 자주 쓴다. 대가는 크기(§10의 차트에서 glibc 정적 hello가 712KB)와 보안 업데이트(libc 버그가 고쳐져도 다시 빌드해야 함), 그리고 glibc 정적 링크에서 `getpwnam`/`dlopen` 같은 NSS·동적 로딩 기능이 제한된다는 점이다. 임베디드에서 정적 링크가 필요하면 musl(B2)이 훨씬 작다.

---

## 5. 공유 라이브러리 — soname, 심볼릭 링크 체인, 탐색 순서

### 5.1 직관 — 이름이 세 개인 이유

공유 라이브러리 하나에는 이름이 세 개 붙는다.

| 이름 | 예 | 누가 쓰나 | 언제 바뀌나 |
|---|---|---|---|
| real name | `libimu.so.1.2.3` | 실제 파일 | 릴리스마다 |
| soname | `libimu.so.1` | 런타임 ld.so (`DT_NEEDED`에 기록) | **ABI가 깨질 때만** (major) |
| linker name | `libimu.so` | 빌드 시 `ld`의 `-limu` | 거의 안 바뀜 (dev 패키지) |

핵심은 **앱이 기억하는 것은 soname**이라는 점이다. 1.2.3 → 1.3.0으로 올라가도 soname `libimu.so.1`이 그대로면 앱을 다시 빌드할 필요가 없다. ABI가 깨지면 `libimu.so.2`로 올려서 옛 앱은 계속 `.so.1`을 쓰게 한다. 두 버전이 한 rootfs에 공존할 수 있다.

```svg
<svg viewBox="0 0 680 430" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="340" y="22" text-anchor="middle" font-size="14" font-weight="bold">이름 세 개, 쓰는 사람 셋</text>
  <rect x="20" y="40" width="190" height="52" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="115" y="62" text-anchor="middle" font-size="13">libimu.so</text>
  <text x="115" y="82" text-anchor="middle" font-size="12">linker name (dev)</text>
  <rect x="245" y="40" width="190" height="52" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="62" text-anchor="middle" font-size="13">libimu.so.1</text>
  <text x="340" y="82" text-anchor="middle" font-size="12">soname (런타임)</text>
  <rect x="470" y="40" width="190" height="52" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="565" y="62" text-anchor="middle" font-size="13">libimu.so.1.2.3</text>
  <text x="565" y="82" text-anchor="middle" font-size="12">real name (실제 파일)</text>
  <line x1="210" y1="66" x2="243" y2="66" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="435" y1="66" x2="468" y2="66" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="227" y="58" text-anchor="middle" font-size="11">ln</text>
  <text x="452" y="58" text-anchor="middle" font-size="11">ln</text>
  <text x="115" y="114" text-anchor="middle" font-size="12">ld (빌드): -limu 가 찾음</text>
  <text x="340" y="114" text-anchor="middle" font-size="12">ld.so (실행): DT_NEEDED</text>
  <text x="565" y="114" text-anchor="middle" font-size="12">ldconfig: SONAME 읽음</text>
  <text x="340" y="146" text-anchor="middle" font-size="12">링크할 때 libimu.so 안의 SONAME(libimu.so.1)이 앱의 DT_NEEDED 로 복사된다</text>
  <text x="340" y="186" text-anchor="middle" font-size="14" font-weight="bold">ld.so 가 "libimu.so.1" 을 찾는 순서</text>
  <rect x="20" y="200" width="400" height="32" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="34" y="221" font-size="13">1. DT_RPATH (DT_RUNPATH 가 없을 때만)</text>
  <text x="440" y="221" font-size="12">--disable-new-dtags 로 생성</text>
  <rect x="20" y="240" width="400" height="32" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="34" y="261" font-size="13">2. LD_LIBRARY_PATH</text>
  <text x="440" y="261" font-size="12">setuid 프로그램에선 무시</text>
  <rect x="20" y="280" width="400" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="34" y="301" font-size="13">3. DT_RUNPATH</text>
  <text x="440" y="301" font-size="12">현재 -rpath 의 기본값</text>
  <rect x="20" y="320" width="400" height="32" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="34" y="341" font-size="13">4. /etc/ld.so.cache</text>
  <text x="440" y="341" font-size="12">ldconfig 가 만든 색인</text>
  <rect x="20" y="360" width="400" height="32" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="34" y="381" font-size="13">5. 기본 경로 /lib, /usr/lib (multiarch 포함)</text>
  <text x="440" y="381" font-size="12">-z nodefaultlib 이면 생략</text>
  <text x="20" y="418" font-size="12">이름에 / 가 들어 있으면 (./x.so, /opt/x.so) 탐색 없이 그 경로만 연다</text>
</svg>
```
그림 3 — 위: 공유 라이브러리의 이름 체인. 아래: ld.so의 탐색 순서. RPATH는 LD_LIBRARY_PATH보다 먼저, RUNPATH는 나중이라는 점이 실무에서 자주 문제가 된다.

### 5.2 예제 7 — `libimu.so.1.2.3` 만들고 "cannot open shared object file" 만나기

무엇을 확인하나: `-fPIC -shared -Wl,-soname`으로 라이브러리를 만들고 심볼릭 링크 체인을 구성한 뒤, 앱의 `DT_NEEDED`에 soname이 기록되는 것과, 표준 경로 밖에 있으면 실행이 실패하는 것.

```c
/* libimu 공개 헤더 (imu.h) */
int imu_read_accel(int axis);
int imu_version(void);
```

```c
/* ex4: libimu.so.1.2.3 — soname "libimu.so.1" 을 가진 공유 라이브러리 */
#include "imu.h"
static int scale = 16;                       /* static → 외부에 안 보임 */
int imu_read_accel(int axis) { return axis * scale; }
int imu_version(void) { return 123; }
```

```c
/* ex4: libimu 를 쓰는 앱 */
#include <stdio.h>
#include "imu.h"
int main(void)
{
    printf("libimu v%d, accel x=%d\n", imu_version(), imu_read_accel(1));
    return 0;
}
```

```sh
gcc -Wall -Wextra -O2 -fPIC -shared -Wl,-soname,libimu.so.1 $S/imu_v1.c -o lib/libimu.so.1.2.3
ln -sf libimu.so.1.2.3 lib/libimu.so.1
ln -sf libimu.so.1 lib/libimu.so
ls -l lib
readelf -d lib/libimu.so.1.2.3 | grep -E "SONAME|NEEDED"
gcc -Wall -Wextra -O2 -I$S $S/app.c -Llib -limu -o app
readelf -d app | grep NEEDED
./app; echo "exit=$?"
LD_LIBRARY_PATH=$PWD/lib ./app
ldd ./app
LD_LIBRARY_PATH=$PWD/lib ldd ./app
```

```text
total 12
lrwxrwxrwx 1 donh donh    11 Oct 10 07:58 libimu.so -> libimu.so.1
lrwxrwxrwx 1 donh donh    15 Oct 10 07:58 libimu.so.1 -> libimu.so.1.2.3
-rwxrwxr-x 1 donh donh 69424 Oct 10 07:58 libimu.so.1.2.3
 0x000000000000000e (SONAME)             Library soname: [libimu.so.1]
 0x0000000000000001 (NEEDED)             Shared library: [libimu.so.1]
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
./app: error while loading shared libraries: libimu.so.1: cannot open shared object file: No such file or directory
exit=127
libimu v123, accel x=16
	linux-vdso.so.1 (0x0000ec9f2888e000)
	libimu.so.1 => not found
	libc.so.6 => /usr/lib/aarch64-linux-gnu/libc.so.6 (0x0000ec9f28640000)
	/lib/ld-linux-aarch64.so.1 (0x0000ec9f28840000)
	linux-vdso.so.1 (0x0000e52b3f54d000)
	libimu.so.1 => /tmp/B3/so/lib/libimu.so.1 (0x0000e52b3f4a0000)
	libc.so.6 => /usr/lib/aarch64-linux-gnu/libc.so.6 (0x0000e52b3f2d0000)
	/lib/ld-linux-aarch64.so.1 (0x0000e52b3f500000)
```

출력에서 볼 것:

- `-limu`로 링크했는데(링커는 `libimu.so`를 읽음) 앱의 NEEDED에는 **`libimu.so.1`** 이 적혔다. 라이브러리 안의 SONAME이 복사된 것이다.
- "error while loading shared libraries: ... cannot open shared object file" — 메시지 앞부분이 `./app:`이 아니라 **ld.so가 출력**한 것이다. exit 127. 커널은 실행을 성공시켰고, 그 뒤 ld.so가 실패했다.
- `ldd`는 "누가 not found인지"를 한눈에 보여 준다. 첫 줄 `linux-vdso.so.1`은 파일이 아니라 커널이 모든 프로세스에 끼워 넣는 vDSO(A1 참고)다.

> 주의: `ldd`는 내부적으로 **대상 프로그램을 ld.so로 실행**하는 방식이다(`LD_TRACE_LOADED_OBJECTS=1`). 신뢰할 수 없는 바이너리에 `ldd`를 쓰지 말고 `readelf -d`나 `objdump -p`로 NEEDED만 보는 습관을 들이자. 크로스 바이너리에는 어차피 `ldd`가 안 통한다(§8.4).

**흔한 함정 — dev 링크가 없으면 링크가 안 된다**: 처음에 `lib_new/`에 `libimu.so.1`만 두고 `-Llib_new -limu`로 링크했더니 실패했다.

```text
/usr/bin/aarch64-linux-gnu-ld.bfd: cannot find -limu: No such file or directory
collect2: error: ld returned 1 exit status
```

`-limu`는 `libimu.so`(또는 `libimu.a`)만 찾는다. Yocto/Buildroot가 런타임 패키지(`libimu1`: `.so.1`, `.so.1.2.3`)와 dev 패키지(`libimu-dev`: 헤더 + `libimu.so` 링크)를 나누는 이유가 이것이다. 타깃 rootfs에는 dev 링크가 필요 없다.

### 5.3 예제 8 — rpath vs runpath, `$ORIGIN`, `LD_DEBUG=libs`

무엇을 확인하나: 앱 바이너리 안에 "내 옆의 `../lib`을 보라"(`$ORIGIN`)를 박아 넣는 두 가지 방법과 LD_LIBRARY_PATH와의 우선순위 차이.

```sh
mkdir -p bin
gcc -O2 -I$S $S/app.c -Llib -limu -Wl,-rpath,'$ORIGIN/../lib' -o bin/app_runpath
gcc -O2 -I$S $S/app.c -Llib -limu -Wl,--disable-new-dtags,-rpath,'$ORIGIN/../lib' -o bin/app_rpath
readelf -d bin/app_runpath | grep -E "RPATH|RUNPATH"
readelf -d bin/app_rpath | grep -E "RPATH|RUNPATH"
./bin/app_runpath; ./bin/app_rpath
LD_DEBUG=libs ./bin/app_runpath 2>&1 | grep -v "^ *$" | head -20
```

```text
 0x000000000000001d (RUNPATH)            Library runpath: [$ORIGIN/../lib]
 0x000000000000000f (RPATH)              Library rpath: [$ORIGIN/../lib]
libimu v123, accel x=16
libimu v123, accel x=16
    371613:	find library=libimu.so.1 [0]; searching
    371613:	 search path=/tmp/B3/so/bin/../lib		(RUNPATH from file ./bin/app_runpath)
    371613:	  trying file=/tmp/B3/so/bin/../lib/libimu.so.1
    371613:	
    371613:	find library=libc.so.6 [0]; searching
    371613:	 search path=/tmp/B3/so/bin/../lib		(RUNPATH from file ./bin/app_runpath)
    371613:	  trying file=/tmp/B3/so/bin/../lib/libc.so.6
    371613:	    (no such file)
    371613:	 search cache=/etc/ld.so.cache
    371613:	  trying file=/usr/lib/aarch64-linux-gnu/libc.so.6
    371613:	
    371613:	
    371613:	calling init: /lib/ld-linux-aarch64.so.1
    371613:	
    371613:	
    371613:	calling init: /usr/lib/aarch64-linux-gnu/libc.so.6
    371613:	
    371613:	
    371613:	calling init: /tmp/B3/so/bin/../lib/libimu.so.1
    371613:	
```

출력에서 볼 것:

- 요즘 GNU ld의 `-rpath`는 기본으로 **RUNPATH**를 만든다. 옛 RPATH는 `--disable-new-dtags`로만 나온다.
- `$ORIGIN`은 ld.so가 "실행 파일이 있는 디렉터리"로 치환한다. `/opt/sensorkit/bin/app` + `/opt/sensorkit/lib/libimu.so.1` 같은 **재배치 가능한 설치 디렉터리**를 만들 때 정석이다. 셸에서 `$`가 확장되지 않도록 작은따옴표로 감싼다.
- `LD_DEBUG=libs`는 ld.so가 **어떤 경로를 어떤 이유로 시도했는지** 보여 준다. "왜 이 라이브러리를 못 찾지?"의 1순위 도구다. `LD_DEBUG=help`로 다른 옵션(`bindings`, `symbols`, `statistics`, `files`)도 볼 수 있다.
- 마지막 "calling init" 순서: 의존성이 없는 것부터(ld.so → libc → libimu) 초기화된다. 라이브러리의 `__attribute__((constructor))`가 이 시점에 돈다.

이제 우선순위 차이를 본다. 디버그용 `libimu.so.1`(버전 999)을 다른 디렉터리에 두고 `LD_LIBRARY_PATH`로 가리킨다.

```sh
gcc -Wall -Wextra -O2 -fPIC -shared -Wl,-soname,libimu.so.1 $S/imu_dbg.c -o lib_dbg/libimu.so.1
LD_LIBRARY_PATH=$PWD/lib_dbg ./bin/app_runpath
LD_LIBRARY_PATH=$PWD/lib_dbg ./bin/app_rpath
```

```text
libimu v999, accel x=16
libimu v123, accel x=16
```

출력에서 볼 것: RUNPATH 앱은 `LD_LIBRARY_PATH`의 디버그 라이브러리(999)를 썼고, **RPATH 앱은 LD_LIBRARY_PATH를 무시**하고 자기 rpath(123)를 썼다. "LD_LIBRARY_PATH를 줬는데 왜 안 바뀌지?"의 대표 원인이다. 또 하나의 차이(실행하지 않음, glibc 문서 기준): RUNPATH는 **그 바이너리의 직접 의존성에만** 적용되고, RPATH는 의존성의 의존성 탐색에도 쓰인다.

### 5.4 예제 9 — `ldconfig`와 `/etc/ld.so.cache`

무엇을 확인하나: 표준 경로의 라이브러리를 빠르게 찾기 위한 캐시와, `ldconfig`가 soname 링크를 만드는 역할.

```sh
ldconfig -p | head -1
ldconfig -p | grep -E "libc\.so|libm\.so"
cat /etc/ld.so.conf; ls /etc/ld.so.conf.d/
cat /etc/ld.so.conf.d/*.conf | grep -v "^#"
mkdir -p lib2 && cp lib/libimu.so.1.2.3 lib2/ && ls lib2 && ldconfig -n lib2 && ls -l lib2
```

```text
410 libs found in cache `/etc/ld.so.cache'
	libm.so.6 (libc6,AArch64) => /usr/lib/aarch64-linux-gnu/libm.so.6
	libc.so.6 (libc6,AArch64) => /usr/lib/aarch64-linux-gnu/libc.so.6
include /etc/ld.so.conf.d/*.conf

aarch64-linux-gnu.conf
fakeroot-aarch64-linux-gnu.conf
libc.conf
/usr/local/lib/aarch64-linux-gnu
/usr/lib/aarch64-linux-gnu
/usr/lib/aarch64-linux-gnu/libfakeroot
/usr/local/lib
libimu.so.1.2.3
total 12
lrwxrwxrwx 1 donh donh    15 Oct 10 07:58 libimu.so.1 -> libimu.so.1.2.3
-rwxrwxr-x 1 donh donh 69424 Oct 10 07:58 libimu.so.1.2.3
```

출력에서 볼 것:

- 캐시는 "soname → 경로" 색인이다. ld.so는 디렉터리를 하나하나 뒤지는 대신 이 파일을 `mmap`해서 찾는다.
- `ldconfig -n lib2`는 캐시는 건드리지 않고 그 디렉터리만 처리했다. `.so.1.2.3` 안의 SONAME을 읽고 **`libimu.so.1` 링크를 스스로 만들었다.** 패키지 설치 후 `ldconfig`를 부르는 이유: 새 디렉터리의 라이브러리를 캐시에 넣고 soname 링크를 맞추기 위해서다. (전역 `sudo ldconfig`는 이 노트에서 실행하지 않았다.)
- 임베디드 rootfs에서는 **`/etc/ld.so.cache`가 아예 없을 수 있다.** Buildroot 같은 빌드 시스템 구성에서는 캐시 없이 표준 경로(`/lib`, `/usr/lib`)에만 라이브러리를 두는 경우가 흔하고, musl의 로더는 캐시 개념이 없고 `/etc/ld-musl-<arch>.path` 파일을 쓴다(B2). 그래서 `/usr/local/lib`에 넣었는데 못 찾는 일이 생긴다.

### 5.5 예제 10 — "undefined symbol": 같은 soname, 다른 ABI

무엇을 확인하나: 새 라이브러리(1.3.0, `imu_read_gyro` 추가)로 빌드한 앱을 옛 라이브러리(1.2.3)가 깔린 보드에서 돌리면 어떻게 되는지. 그리고 `-z now`와 `-z lazy`에서 **언제** 터지는지.

```c
/* libimu 1.3.0 — 같은 soname(libimu.so.1)에 새 함수 imu_read_gyro 추가 */
#include "imu.h"
int imu_read_accel(int axis) { return axis * 16; }
int imu_version(void) { return 130; }
int imu_read_gyro(int axis) { return axis * 250; }
```

```c
/* ex5: 새 라이브러리(1.3.0)로 빌드한 앱을 옛 라이브러리(1.2.3)가 깔린 보드에서 돌리면? */
#include <stdio.h>
#include "imu.h"
int imu_read_gyro(int axis);
int main(void)
{
    printf("start: libimu v%d\n", imu_version());
    fflush(stdout);
    printf("gyro x=%d\n", imu_read_gyro(1));      /* 1.2.3 에는 없는 함수 */
    return 0;
}
```

```sh
gcc -Wall -Wextra -O2 -fPIC -shared -Wl,-soname,libimu.so.1 $S/imu_v2.c -o lib_new/libimu.so.1
ln -sf libimu.so.1 lib_new/libimu.so
gcc -Wall -Wextra -O2 -I$S $S/app_gyro.c -Llib_new -limu -o app_gyro_now
gcc -Wall -Wextra -O2 -I$S $S/app_gyro.c -Llib_new -limu -Wl,-z,lazy -o app_gyro_lazy
echo "--- new lib"; LD_LIBRARY_PATH=$PWD/lib_new ./app_gyro_now
echo "--- old lib, BIND_NOW (default)"; LD_LIBRARY_PATH=$PWD/lib ./app_gyro_now 2>&1; echo "exit=$?"
echo "--- old lib, lazy"; LD_LIBRARY_PATH=$PWD/lib ./app_gyro_lazy 2>&1; echo "exit=$?"
nm -D lib/libimu.so.1.2.3 | grep imu
nm -D --undefined-only app_gyro_now
```

```text
--- new lib
start: libimu v130
gyro x=250
--- old lib, BIND_NOW (default)
./app_gyro_now: symbol lookup error: ./app_gyro_now: undefined symbol: imu_read_gyro
exit=127
--- old lib, lazy
start: libimu v123
./app_gyro_lazy: symbol lookup error: ./app_gyro_lazy: undefined symbol: imu_read_gyro
exit=127
00000000000005c0 T imu_read_accel
00000000000005c8 T imu_version
                 w _ITM_deregisterTMCloneTable
                 w _ITM_registerTMCloneTable
                 w __cxa_finalize@GLIBC_2.17
                 w __gmon_start__
                 U __libc_start_main@GLIBC_2.34
                 U __printf_chk@GLIBC_2.17
                 U abort@GLIBC_2.17
                 U fflush@GLIBC_2.17
                 U imu_read_gyro
                 U imu_version
                 U stdout@GLIBC_2.17
```

출력에서 볼 것:

- **BIND_NOW**(Ubuntu 기본): `main` 전에 모든 심볼을 해결하다가 즉시 실패. "start:" 줄조차 안 나온다. 실패가 **빠르고 확실하다**.
- **lazy**: 프로그램이 일단 시작해서 "start: libimu v123"까지 찍고, `imu_read_gyro`를 **처음 부르는 순간** 죽는다. 필드에서 이 함수가 드문 경로(에러 처리, 캘리브레이션)에만 있다면 몇 주 뒤에 터진다. 임베디드 제품에서 `-z now`가 권장되는 이유 중 하나다.
- `nm -D --undefined-only`: 앱이 **밖에서 가져와야 하는** 심볼 목록. `U imu_read_gyro`에는 버전 태그가 없고 libc 심볼에는 `@GLIBC_2.17` 같은 **심볼 버전**이 붙어 있다. glibc는 심볼 버전으로 "이 앱은 GLIBC_2.34의 `__libc_start_main`이 필요하다"를 기록해서, 더 오래된 glibc에서는 시작 시 `version 'GLIBC_2.34' not found`로 명확하게 실패하게 만든다(B2).
- 진짜 원인은 **ABI를 바꿨는데(함수 추가는 하위 호환이지만 앱이 그것을 요구하게 됨) 배포 시 라이브러리를 함께 올리지 않은 것**이다. soname은 "major 호환"만 표현한다. "1.3 이상 필요"는 심볼 버전(version script)으로 표현하는 것이 정석이다.

---

## 6. 동적 링킹의 심장 — PLT, GOT, lazy binding, RELRO

### 6.1 직관 — "간접 점프 테이블을 RAM에 두고 부팅 때 채운다"

Don이 펌웨어에서 ROM 함수 테이블(벡터 테이블, 부트 ROM API 테이블)이나 "패치 가능한 함수 포인터 테이블"을 RAM에 두고 호출을 그 테이블을 거쳐 하게 만든 적이 있다면, 그것이 바로 GOT다.

- **GOT**(Global Offset Table): 쓰기 가능한 데이터 영역에 있는 **주소 배열**. 외부 함수·변수의 실제 주소를 ld.so가 채워 넣는다.
- **PLT**(Procedure Linkage Table): 코드 영역의 **작은 스텁 함수들**. `puts@plt`는 "GOT[puts]에서 주소를 읽어 점프"하는 4개 명령이다.
- 코드(`.text`)는 읽기 전용이고 여러 프로세스가 공유하므로 **고칠 수 없다.** 그래서 고쳐야 하는 주소는 모두 프로세스별 데이터(GOT)로 몰아 놓는다. 이것이 PIC의 핵심 아이디어다.
- **lazy binding**: GOT를 시작할 때 다 채우지 않고, 처음 호출할 때 해석한다. 처음에는 GOT 슬롯이 "해석기로 가는 길"(PLT0)을 가리키고 있다.

```svg
<svg viewBox="0 0 680 470" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="30" width="120" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="70" y="55" text-anchor="middle" font-size="13">main (.text)</text>
  <rect x="140" y="30" width="120" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="200" y="55" text-anchor="middle" font-size="13">puts@plt</text>
  <rect x="270" y="30" width="110" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="325" y="55" text-anchor="middle" font-size="13">GOT[puts]</text>
  <rect x="390" y="30" width="130" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="455" y="55" text-anchor="middle" font-size="13">ld.so resolver</text>
  <rect x="535" y="30" width="130" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="600" y="55" text-anchor="middle" font-size="13">libc: puts</text>
  <line x1="70" y1="70" x2="70" y2="440" stroke="currentColor" stroke-opacity="0.4" stroke-dasharray="4 4"/>
  <line x1="200" y1="70" x2="200" y2="440" stroke="currentColor" stroke-opacity="0.4" stroke-dasharray="4 4"/>
  <line x1="325" y1="70" x2="325" y2="440" stroke="currentColor" stroke-opacity="0.4" stroke-dasharray="4 4"/>
  <line x1="455" y1="70" x2="455" y2="440" stroke="currentColor" stroke-opacity="0.4" stroke-dasharray="4 4"/>
  <line x1="600" y1="70" x2="600" y2="440" stroke="currentColor" stroke-opacity="0.4" stroke-dasharray="4 4"/>
  <text x="10" y="92" font-size="13" font-weight="bold">첫 번째 호출 (lazy)</text>
  <line x1="70" y1="115" x2="198" y2="115" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="135" y="109" text-anchor="middle" font-size="12">1. bl puts@plt</text>
  <line x1="200" y1="145" x2="323" y2="145" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="262" y="139" text-anchor="middle" font-size="12">2. ldr x17</text>
  <line x1="325" y1="170" x2="202" y2="170" stroke="currentColor" stroke-dasharray="5 3" marker-end="url(#ah3)"/>
  <text x="262" y="186" text-anchor="middle" font-size="12">PLT0 주소</text>
  <line x1="200" y1="210" x2="453" y2="210" stroke="#d0564a" marker-end="url(#ah3)"/>
  <text x="330" y="204" text-anchor="middle" font-size="12">3. br x17 → PLT0 → resolve</text>
  <rect x="448" y="222" width="14" height="40" fill="#d0564a" fill-opacity="0.3" stroke="#d0564a"/>
  <text x="470" y="240" font-size="12">4. _dl_fixup:</text>
  <text x="470" y="256" font-size="12">puts 심볼 검색</text>
  <line x1="455" y1="285" x2="327" y2="285" stroke="#d0564a" marker-end="url(#ah3)"/>
  <text x="390" y="279" text-anchor="middle" font-size="12">5. GOT 기록</text>
  <line x1="455" y1="310" x2="598" y2="310" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="527" y="304" text-anchor="middle" font-size="12">6. puts 로 점프</text>
  <line x1="10" y1="330" x2="670" y2="330" stroke="currentColor" stroke-opacity="0.4"/>
  <text x="10" y="350" font-size="13" font-weight="bold">두 번째 이후</text>
  <line x1="70" y1="370" x2="198" y2="370" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="135" y="364" text-anchor="middle" font-size="12">7. bl puts@plt</text>
  <line x1="200" y1="395" x2="323" y2="395" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="262" y="389" text-anchor="middle" font-size="12">8. ldr = &amp;puts</text>
  <line x1="200" y1="425" x2="598" y2="425" stroke="#3f9a6b" stroke-width="2" marker-end="url(#ah3)"/>
  <text x="400" y="419" text-anchor="middle" font-size="12">9. br x17 → puts 바로 실행 (명령 4개 비용)</text>
  <text x="10" y="460" font-size="12">-z now 이면 1~6 의 해석이 main 전에 끝나 있고, GOT 는 RELRO 로 읽기 전용이 된다</text>
</svg>
```
그림 4 — lazy binding 시퀀스. 첫 호출만 ld.so 해석기를 거치고, 그 뒤는 PLT 스텁 → GOT → 진짜 함수로 바로 간다.

### 6.2 예제 11 — PLT 스텁을 디스어셈블하고, gdb로 GOT 슬롯이 바뀌는 순간 보기

무엇을 확인하나: `-z lazy`로 빌드한 프로그램에서 `puts`의 GOT 슬롯이 첫 호출 전에는 PLT0을, 후에는 libc의 `puts`를 가리키는 것. `-z now`(기본)에서는 처음부터 채워져 있는 것.

```c
/* ex6: PLT/GOT lazy binding 관찰용 — puts 를 두 번 부른다 */
#include <stdio.h>
int main(void)
{
    puts("first call");     /* 이때 ld.so 가 puts 주소를 찾아 GOT 에 써 넣는다 */
    puts("second call");    /* 이제 GOT 에 진짜 주소 → 바로 점프 */
    return 0;
}
```

```sh
gcc -Wall -Wextra -O0 -g -Wl,-z,lazy $S/plt.c -o plt_lazy
gcc -Wall -Wextra -O0 -g $S/plt.c -o plt_now
readelf -d plt_lazy | grep -E "FLAGS"; readelf -d plt_now | grep FLAGS
readelf -r plt_lazy | sed -n "/rela.plt/,\$p"
objdump -d --no-show-raw-insn -j .plt plt_lazy
objdump -d --no-show-raw-insn plt_lazy | grep -A8 "<main>:"
```

```text
 0x000000006ffffffb (FLAGS_1)            Flags: PIE
 0x000000000000001e (FLAGS)              BIND_NOW
 0x000000006ffffffb (FLAGS_1)            Flags: NOW PIE
Relocation section '.rela.plt' at offset 0x558 contains 5 entries:
  Offset          Info           Type           Sym. Value    Sym. Name + Addend
000000020000  000300000402 R_AARCH64_JUMP_SL 0000000000000000 __libc_start_main@GLIBC_2.34 + 0
000000020008  000500000402 R_AARCH64_JUMP_SL 0000000000000000 __cxa_finalize@GLIBC_2.17 + 0
000000020010  000600000402 R_AARCH64_JUMP_SL 0000000000000000 __gmon_start__ + 0
000000020018  000700000402 R_AARCH64_JUMP_SL 0000000000000000 abort@GLIBC_2.17 + 0
000000020020  000800000402 R_AARCH64_JUMP_SL 0000000000000000 puts@GLIBC_2.17 + 0

plt_lazy:     file format elf64-littleaarch64


Disassembly of section .plt:

00000000000005f0 <.plt>:
 5f0:	stp	x16, x30, [sp, #-16]!
 5f4:	adrp	x16, 1f000 <__abi_tag+0x1e6fc>
 5f8:	ldr	x17, [x16, #4088]
 5fc:	add	x16, x16, #0xff8
 600:	br	x17
 604:	nop
 608:	nop
 60c:	nop

0000000000000610 <__libc_start_main@plt>:
 610:	adrp	x16, 20000 <__libc_start_main@GLIBC_2.34>
 614:	ldr	x17, [x16]
 618:	add	x16, x16, #0x0
 61c:	br	x17
...
0000000000000650 <puts@plt>:
 650:	adrp	x16, 20000 <__libc_start_main@GLIBC_2.34>
 654:	ldr	x17, [x16, #32]
 658:	add	x16, x16, #0x20
 65c:	br	x17
00000000000007a8 <main>:
 7a8:	stp	x29, x30, [sp, #-16]!
 7ac:	mov	x29, sp
 7b0:	adrp	x0, 0 <_init-0x5d0>
 7b4:	add	x0, x0, #0x7f8
 7b8:	bl	650 <puts@plt>
 7bc:	adrp	x0, 0 <_init-0x5d0>
 7c0:	add	x0, x0, #0x808
 7c4:	bl	650 <puts@plt>
```
(가운데 `__cxa_finalize@plt`, `__gmon_start__@plt`, `abort@plt` 스텁 세 개는 같은 모양이라 생략했다.)

출력에서 볼 것:

- `puts@plt`: `adrp x16` + `ldr x17, [x16, #32]` → 주소 `0x20020`(= `.rela.plt`의 puts 엔트리 Offset)에서 8바이트를 읽어 `br x17`. x16·x17은 AAPCS64가 "veneer·PLT용으로 링커가 마음대로 쓸 수 있다"고 정해 둔 IP0·IP1 레지스터다. Cortex-M 펌웨어에서 링커가 넣던 long-branch veneer와 같은 역할이다.
- PLT0(`.plt` 첫 엔트리): 스텁에서 넘어온 x16(= puts의 GOT 슬롯 주소)과 x30(LR)을 스택에 저장하고, GOT의 예약 슬롯(`0x1fff8` = `.got.plt[2]`)에 있는 해석기 주소로 점프한다. 해석기는 스택에 저장된 x16을 보고 "어떤 슬롯을 채워야 하는지" 안다.
- `main`의 두 호출 모두 `bl 650 <puts@plt>` — 코드는 PLT만 안다. 실제 주소는 GOT가 안다.

이제 gdb로 GOT 슬롯을 직접 본다. 슬롯 주소는 "로드 기준 + 0x20020"이고, 로드 기준은 `&main − 0x7a8`로 계산한다.

```text
# plt.gdb
set debuginfod enabled off
set pagination off
set disable-randomization on
break main
run
set $got = (void **)((char *)&main - 0x7a8 + $gotoff)
printf "GOT[puts] @ %p = %p\n", $got, *$got
info symbol *$got
next
printf "after 1st puts: GOT[puts] = %p\n", *$got
info symbol *$got
continue
```

```sh
gdb -q -batch -ex "set \$gotoff=0x20020" -x $S/plt.gdb ./plt_lazy 2>&1 | grep -v "^\[\|libthread_db"
echo ======
gdb -q -batch -ex "set \$gotoff=0x1ffc8" -x $S/plt.gdb ./plt_now 2>&1 | grep -v "^\[\|libthread_db"
```

```text
Breakpoint 1 at 0x7b0: file /Users/donh/.elinux_scratch/B3/plt.c, line 5.

Breakpoint 1, main () at /Users/donh/.elinux_scratch/B3/plt.c:5
5	    puts("first call");     /* 이때 ld.so 가 puts 주소를 찾아 GOT 에 써 넣는다 */
GOT[puts] @ 0xaaaaaaac0020 = 0xaaaaaaaa05f0
No symbol matches *$got.
6	    puts("second call");    /* 이제 GOT 에 진짜 주소 → 바로 점프 */
after 1st puts: GOT[puts] = 0xfffff7e56b60
puts in section .text of /usr/lib/aarch64-linux-gnu/libc.so.6
first call
second call
======
Breakpoint 1 at 0x7b0: file /Users/donh/.elinux_scratch/B3/plt.c, line 5.

Breakpoint 1, main () at /Users/donh/.elinux_scratch/B3/plt.c:5
5	    puts("first call");     /* 이때 ld.so 가 puts 주소를 찾아 GOT 에 써 넣는다 */
GOT[puts] @ 0xaaaaaaabffc8 = 0xfffff7e56b60
puts in section .text of /usr/lib/aarch64-linux-gnu/libc.so.6
6	    puts("second call");    /* 이제 GOT 에 진짜 주소 → 바로 점프 */
after 1st puts: GOT[puts] = 0xfffff7e56b60
puts in section .text of /usr/lib/aarch64-linux-gnu/libc.so.6
first call
second call
```

출력에서 볼 것:

- lazy: 첫 호출 전 `GOT[puts] = 0xaaaaaaaa05f0` = 로드 기준 `0xaaaaaaaa0000` + `0x5f0` = **PLT0**. 첫 호출 후 `0xfffff7e56b60` = libc의 `puts`. 그림 4의 1~6단계가 `next` 한 번 안에서 일어났다.
- now: `main`에 도착했을 때 이미 `puts`의 주소가 들어 있다. ld.so가 시작 전에 다 채웠다.
- `-z now` 빌드는 GOT 슬롯 위치(0x1ffc8)가 다르다. `.got.plt`가 따로 없고 `.got`에 합쳐져 RELRO 범위 안으로 들어가기 때문이다. 각 바이너리의 슬롯 오프셋은 `readelf -r <bin> | grep puts`로 얻었다.

파일 안에서 lazy GOT의 초기값도 확인할 수 있다. 슬롯마다 `0x5f0`(PLT0)이 들어 있고, 로드할 때 기준 주소만 더해진다.

```sh
readelf -x .got.plt plt_lazy
```

```text
Hex dump of section '.got.plt':
 NOTE: This section has relocations against it, but these have NOT been applied to this dump.
  0x0001ffe8 00000000 00000000 00000000 00000000 ................
  0x0001fff8 00000000 00000000 f0050000 00000000 ................
  0x00020008 f0050000 00000000 f0050000 00000000 ................
  0x00020018 f0050000 00000000 f0050000 00000000 ................
```

### 6.3 예제 12 — `LD_DEBUG=bindings`로 "언제 무엇이 묶이나" 보기

무엇을 확인하나: lazy에서는 호출하지 않은 함수(`abort`)가 바인딩되지 않고, now/`LD_BIND_NOW=1`에서는 바인딩된다는 것.

```sh
echo "--- lazy"
stdbuf -o0 env LD_DEBUG=bindings ./plt_lazy 2>&1 | grep -E "plt_lazy.*(puts|__libc_start_main|abort)|first|second|calling init: \./"
echo "--- lazy + LD_BIND_NOW=1"
stdbuf -o0 env LD_BIND_NOW=1 LD_DEBUG=bindings ./plt_lazy 2>&1 | grep -E "plt_lazy.*(puts|__libc_start_main|abort)|first|second"
echo "--- now (-z now 기본)"
stdbuf -o0 env LD_DEBUG=bindings ./plt_now 2>&1 | grep -E "plt_now.*(puts|__libc_start_main|abort)|first|second"
```

```text
--- lazy
    385129:	binding file ./plt_lazy [0] to /usr/lib/aarch64-linux-gnu/libc.so.6 [0]: normal symbol `__libc_start_main' [GLIBC_2.34]
    385129:	binding file ./plt_lazy [0] to /usr/lib/aarch64-linux-gnu/libc.so.6 [0]: normal symbol `puts' [GLIBC_2.17]
first call
second call
--- lazy + LD_BIND_NOW=1
    385131:	binding file ./plt_lazy [0] to /usr/lib/aarch64-linux-gnu/libc.so.6 [0]: normal symbol `__libc_start_main' [GLIBC_2.34]
    385131:	binding file ./plt_lazy [0] to /usr/lib/aarch64-linux-gnu/libc.so.6 [0]: normal symbol `abort' [GLIBC_2.17]
    385131:	binding file ./plt_lazy [0] to /usr/lib/aarch64-linux-gnu/libc.so.6 [0]: normal symbol `puts' [GLIBC_2.17]
first call
second call
--- now (-z now 기본)
    385133:	binding file ./plt_now [0] to /usr/lib/aarch64-linux-gnu/libc.so.6 [0]: normal symbol `__libc_start_main' [GLIBC_2.34]
    385133:	binding file ./plt_now [0] to /usr/lib/aarch64-linux-gnu/libc.so.6 [0]: normal symbol `abort' [GLIBC_2.17]
    385133:	binding file ./plt_now [0] to /usr/lib/aarch64-linux-gnu/libc.so.6 [0]: normal symbol `puts' [GLIBC_2.17]
first call
second call
```

출력에서 볼 것: lazy에는 **`abort` 바인딩 줄이 없다** — 한 번도 안 불렸으니 해석도 안 했다. 환경 변수 `LD_BIND_NOW=1`은 재빌드 없이 lazy 바이너리를 now처럼 동작시킨다. 위 §5.5의 "드문 경로에서 터지는 undefined symbol"을 **테스트 단계에서 미리 잡는** 가장 싼 방법이다. (`stdbuf -o0`은 stdout 버퍼링을 꺼서 바인딩 로그와 출력의 순서를 맞추려고 썼다.)

### 6.4 예제 13 — RELRO: GOT를 덮어쓸 수 있나?

**RELRO**(RELocation Read-Only)는 "재배치가 끝난 뒤에는 바뀔 일 없는 영역(`.init_array`, `.dynamic`, `.got`)을 `mprotect`로 읽기 전용으로 만든다"는 링커+로더 협약이다. 공격자가 버퍼 오버플로로 GOT의 함수 주소를 덮어써서 실행 흐름을 빼앗는 공격(GOT overwrite)을 막는다.

| 빌드 옵션 | 이름 | `.got.plt`(함수 슬롯) | lazy 가능 |
|---|---|---|---|
| `-z norelro` | RELRO 없음 | 쓰기 가능 | 예 |
| `-z relro` (+ lazy) | partial RELRO | **쓰기 가능** (lazy가 써야 하므로) | 예 |
| `-z relro -z now` | full RELRO | 읽기 전용 | 아니오 |

무엇을 확인하나: 실행 중인 프로그램이 자기 GOT의 `puts` 슬롯을 찾아서 덮어쓰려고 하면 세 가지 빌드에서 각각 어떻게 되는지.

```c
/* ex7: RELRO — 내 GOT 에 있는 puts 슬롯을 덮어쓸 수 있나? (partial vs full RELRO) */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <link.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static void **slot;                          /* 찾은 GOT 슬롯 주소 */
static void *real_puts;                      /* dlsym 으로 얻은 puts 주소 */

static int find_slot(struct dl_phdr_info *info, size_t size, void *data)
{
    (void)size; (void)data;
    for (int i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
        if (ph->p_type != PT_LOAD || !(ph->p_flags & PF_W)) continue;
        void **p = (void **)(info->dlpi_addr + ph->p_vaddr);
        size_t n = ph->p_memsz / sizeof(void *);
        for (size_t k = 0; k < n; k++)
            if (p[k] == real_puts) { slot = &p[k]; return 1; }
    }
    return 1;                                /* 첫 객체 = 메인 실행 파일만 본다 */
}

int main(void)
{
    puts("call puts once (lazy 라면 여기서 GOT 가 채워진다)");
    real_puts = dlsym(RTLD_DEFAULT, "puts");  /* &puts 를 쓰면 .got 에 별도 슬롯이 생겨서 */
    dl_iterate_phdr(find_slot, NULL);
    if (!slot) { fprintf(stderr, "slot not found\n"); exit(1); }
    printf("GOT slot for puts = %p -> %p\n", (void *)slot, *slot);

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); exit(1); }
    if (pid == 0) {                          /* 자식: 공격자처럼 GOT 를 덮어쓴다 */
        *slot = NULL;
        _exit(0);
    }
    int st;
    if (waitpid(pid, &st, 0) < 0) { perror("waitpid"); exit(1); }
    if (WIFSIGNALED(st))
        printf("overwrite -> killed by %s (GOT is read-only)\n", strsignal(WTERMSIG(st)));
    else
        printf("overwrite -> succeeded (GOT is writable!)\n");
    return 0;
}
```

```sh
gcc -Wall -Wextra -O2 $S/relro.c -o relro_full
gcc -Wall -Wextra -O2 -Wl,-z,lazy $S/relro.c -o relro_partial
gcc -Wall -Wextra -O2 -Wl,-z,norelro,-z,lazy $S/relro.c -o relro_none
for b in relro_full relro_partial relro_none; do
  echo "--- $b: $(readelf -lW $b | grep -c GNU_RELRO) GNU_RELRO, $(readelf -d $b | grep -o "BIND_NOW" | head -1)"; ./$b
done
readelf -SW relro_partial | grep -E "\.got"; readelf -lW relro_partial | grep RELRO
```

```text
--- relro_full: 1 GNU_RELRO, BIND_NOW
call puts once (lazy 라면 여기서 GOT 가 채워진다)
GOT slot for puts = 0xb9f4db4cff90 -> 0xfe6815c86b60
overwrite -> killed by Segmentation fault (GOT is read-only)
--- relro_partial: 1 GNU_RELRO, 
call puts once (lazy 라면 여기서 GOT 가 채워진다)
GOT slot for puts = 0xb38604120050 -> 0xe7a41f596b60
overwrite -> succeeded (GOT is writable!)
--- relro_none: 0 GNU_RELRO, 
call puts once (lazy 라면 여기서 GOT 가 채워진다)
GOT slot for puts = 0xb11d2df612a8 -> 0xeb4ebc106b60
overwrite -> succeeded (GOT is writable!)
  [21] .got              PROGBITS        000000000001ffa8 00ffa8 000040 08  WA  0   0  8
  [22] .got.plt          PROGBITS        000000000001ffe8 00ffe8 000098 08  WA  0   0  8
  GNU_RELRO      0x00fda8 0x000000000001fda8 0x000000000001fda8 0x000258 0x000258 R   0x1
```

출력에서 볼 것:

- full RELRO만 GOT 덮어쓰기가 SIGSEGV로 막혔다. partial RELRO는 `GNU_RELRO` 세그먼트가 **있는데도** 함수 슬롯이 쓰기 가능하다. RELRO 범위가 `0x1fda8 + 0x258 = 0x20000`에서 끝나고, `.got.plt`(0x1ffe8~)의 puts 슬롯(파일 기준 0x20050)은 그 **바깥**이기 때문이다.
- 처음 버전에서는 `p[k] == (void *)puts`로 비교했더니 partial에서도 SIGSEGV가 났다. C 코드에서 `puts`의 **주소를 취하면** 컴파일러가 `.got`(RELRO 안쪽)에 별도 슬롯을 만들어서 그걸 먼저 찾았기 때문이다. 그래서 `dlsym`으로 바꿨다. "함수 호출용 슬롯(.got.plt)"과 "주소 참조용 슬롯(.got)"이 따로 있다는 것을 보여 주는 실수였다.
- 실무 결론: 보안 요구가 있는 장비(국방, 차량, 카메라)는 **`-Wl,-z,relro,-z,now` + PIE**가 기본 체크 항목이다. Ubuntu·Fedora gcc는 이미 기본이지만, **벤더 SDK의 크로스 gcc는 기본값이 다를 수 있으니** 반드시 `readelf -d`의 `BIND_NOW`, `readelf -l`의 `GNU_RELRO`로 확인한다.

### 6.5 예제 14 — 측정: lazy vs now, 시작 시간은 얼마나 다른가

lazy binding이 존재하는 이유는 **시작 시간**이다. 큰 GUI 앱이 수천 개 함수를 참조하지만 시작할 때는 일부만 쓴다. 정말 차이가 나는지 직접 잰다.

무엇을 확인하나: N개 함수를 가진 `libmany.so`와 그 N개를 **참조는 하지만 1개만 실제 호출**하는 앱을 만들어, lazy와 now의 실행 시간(fork+exec → 종료)을 300번 재서 중앙값을 비교.

```python
# ex12 생성기: N개 함수를 가진 libmany.so 와, 그 N개를 모두 "참조"하지만 실제로는 1개만 부르는 앱
import sys, os
n, out = int(sys.argv[1]), sys.argv[2]
os.makedirs(out, exist_ok=True)
with open(f"{out}/lib.c", "w") as f:
    for i in range(n):
        f.write(f"int f{i}(int x) {{ return x + {i}; }}\n")
with open(f"{out}/app.c", "w") as f:
    for i in range(n):
        f.write(f"int f{i}(int);\n")
    f.write("int main(int argc, char **argv) {\n  (void)argv;\n  int s = f0(argc);\n")
    f.write("  if (argc > 1000) {\n")          # 실제로는 절대 안 타는 분기
    for i in range(1, n):
        f.write(f"    s += f{i}(s);\n")
    f.write("  }\n  return s == 12345;\n}\n")
```

```python
# ex12 측정기: 같은 바이너리를 R번 fork+exec 해서 한 번 실행에 걸린 평균 시간(µs)
import subprocess, sys, time, statistics
def run(path, env, R=300):
    ts = []
    for _ in range(R):
        t = time.perf_counter()
        subprocess.run([path], env=env, check=False)
        ts.append((time.perf_counter() - t) * 1e6)
    return statistics.median(ts)
base = sys.argv[1]
for n in sys.argv[2:]:
    d = f"{base}/n{n}"
    env = {"LD_LIBRARY_PATH": d}
    lazy = run(f"{d}/app_lazy", env)
    now = run(f"{d}/app_now", env)
    print(f"N={n:>6}  lazy={lazy:8.0f} us  now={now:8.0f} us  diff={now - lazy:7.0f} us")
```
(주석에는 "평균"이라고 썼지만 코드는 **중앙값**(`statistics.median`)을 쓴다.)

```sh
B=/tmp/B3/many
for n in 10 1000 5000 20000; do d=$B/n$n
  python3 $S/gen_many.py $n $d
  gcc -O1 -fPIC -shared $d/lib.c -o $d/libmany.so
  gcc -O1 -Wall -Wextra $d/app.c -L$d -lmany -Wl,-z,lazy -o $d/app_lazy
  gcc -O1 -Wall -Wextra $d/app.c -L$d -lmany -o $d/app_now
done
python3 $S/bench.py $B 10 1000 5000 20000
```

```text
N=    10  lazy=     153 us  now=     150 us  diff=     -4 us
N=  1000  lazy=     153 us  now=     197 us  diff=     43 us
N=  5000  lazy=     180 us  now=     425 us  diff=    245 us
N= 20000  lazy=     248 us  now=    1357 us  diff=   1109 us
```

같은 측정을 한 번 더 돌렸을 때는 N=20000에서 lazy 288µs / now 1540µs였다(실행마다 ±10% 정도 흔들린다). 아래 차트는 위 표의 값이다. `LD_DEBUG=statistics`로 ld.so가 처리한 relocation 수도 확인했다.

```sh
cd /tmp/B3/many/n20000
LD_LIBRARY_PATH=. LD_DEBUG=statistics ./app_lazy
LD_LIBRARY_PATH=. LD_DEBUG=statistics ./app_now
readelf -r app_now | grep -c JUMP_SL
```

```text
    416332:	                 number of relocations: 97
    416332:	      number of relocations from cache: 7
    416332:	        number of relative relocations: 7
    416332:	
    416332:	runtime linker statistics:
    416332:	           final number of relocations: 101
    416332:	final number of relocations from cache: 7

    416333:	                 number of relocations: 20101
    416333:	      number of relocations from cache: 7
    416333:	        number of relative relocations: 7
    416333:	
    416333:	runtime linker statistics:
    416333:	           final number of relocations: 20102
    416333:	final number of relocations from cache: 7
20004
```

```svg
<svg viewBox="0 0 640 320" xmlns="http://www.w3.org/2000/svg">
  <text x="320" y="22" text-anchor="middle" font-size="14" font-weight="bold">시작 시간 (fork+exec → 종료, 중앙값 300회)</text>
  <line x1="70" y1="260.0" x2="620" y2="260.0" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="62" y="264.0" text-anchor="end" font-size="12">0</text>
  <line x1="70" y1="228.6" x2="620" y2="228.6" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="62" y="232.6" text-anchor="end" font-size="12">200</text>
  <line x1="70" y1="197.1" x2="620" y2="197.1" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="62" y="201.1" text-anchor="end" font-size="12">400</text>
  <line x1="70" y1="165.7" x2="620" y2="165.7" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="62" y="169.7" text-anchor="end" font-size="12">600</text>
  <line x1="70" y1="134.3" x2="620" y2="134.3" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="62" y="138.3" text-anchor="end" font-size="12">800</text>
  <line x1="70" y1="102.9" x2="620" y2="102.9" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="62" y="106.9" text-anchor="end" font-size="12">1000</text>
  <line x1="70" y1="71.4" x2="620" y2="71.4" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="62" y="75.4" text-anchor="end" font-size="12">1200</text>
  <line x1="70" y1="40.0" x2="620" y2="40.0" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="62" y="44.0" text-anchor="end" font-size="12">1400</text>
  <line x1="70" y1="260" x2="620" y2="260" stroke="currentColor"/>
  <line x1="70" y1="40" x2="70" y2="260" stroke="currentColor"/>
  <text x="18" y="150" text-anchor="middle" font-size="12" transform="rotate(-90 18 150)">µs</text>
  <rect x="95.8" y="236.0" width="40" height="24.0" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="115.8" y="231.0" text-anchor="middle" font-size="12">153</text>
  <rect x="141.8" y="236.4" width="40" height="23.6" fill="#e08a3c" fill-opacity="0.75"/>
  <text x="161.8" y="231.4" text-anchor="middle" font-size="12">150</text>
  <text x="138.8" y="278" text-anchor="middle" font-size="12">N = 10</text>
  <rect x="233.2" y="236.0" width="40" height="24.0" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="253.2" y="231.0" text-anchor="middle" font-size="12">153</text>
  <rect x="279.2" y="229.0" width="40" height="31.0" fill="#e08a3c" fill-opacity="0.75"/>
  <text x="299.2" y="224.0" text-anchor="middle" font-size="12">197</text>
  <text x="276.2" y="278" text-anchor="middle" font-size="12">N = 1000</text>
  <rect x="370.8" y="231.7" width="40" height="28.3" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="390.8" y="226.7" text-anchor="middle" font-size="12">180</text>
  <rect x="416.8" y="193.2" width="40" height="66.8" fill="#e08a3c" fill-opacity="0.75"/>
  <text x="436.8" y="188.2" text-anchor="middle" font-size="12">425</text>
  <text x="413.8" y="278" text-anchor="middle" font-size="12">N = 5000</text>
  <rect x="508.2" y="221.0" width="40" height="39.0" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="528.2" y="216.0" text-anchor="middle" font-size="12">248</text>
  <rect x="554.2" y="46.8" width="40" height="213.2" fill="#e08a3c" fill-opacity="0.75"/>
  <text x="574.2" y="41.8" text-anchor="middle" font-size="12">1357</text>
  <text x="551.2" y="278" text-anchor="middle" font-size="12">N = 20000</text>
  <text x="345" y="298" text-anchor="middle" font-size="12">앱이 참조하는 라이브러리 함수 개수 (실제 호출은 1개)</text>
  <rect x="90" y="48" width="14" height="14" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="110" y="60" font-size="12">-z lazy (호출할 때 해석)</text>
  <rect x="90" y="70" width="14" height="14" fill="#e08a3c" fill-opacity="0.75"/>
  <text x="110" y="82" font-size="12">-z now (시작할 때 전부 해석, Ubuntu 기본)</text>
</svg>
```
그림 5 — 직접 측정한 시작 시간. 참조 심볼 1000개 정도까지는 차이가 수십 µs로 무시할 만하고, 2만 개에서 now가 약 1.1ms 더 든다. 대략 심볼 하나당 50~55ns.

출력에서 볼 것:

- 시작 시 relocation 수: lazy 97개 vs now 20,101개. lazy에서도 **함수가 아닌 relocation**(데이터 참조, RELATIVE)은 시작 시 다 처리한다. 미뤄지는 것은 `JUMP_SLOT`(함수 호출)뿐이다.
- 센서 데몬 같은 임베디드 앱은 참조 심볼이 많아야 수백~수천 개다. **수십~수백 µs 손해로 "시작 시 즉시 실패 + full RELRO"를 얻는 것**은 거의 항상 좋은 거래다. 부팅 시간을 ms 단위로 깎아야 하는 제품이라면 그때 측정해서 판단한다(N 모듈의 부팅 최적화).
- 이 VM은 Apple Silicon 위 가상화라 절대값은 실제 Cortex-A53 보드보다 훨씬 빠르다. 보드에서는 몇 배 커질 수 있다. **비율과 경향**을 보자.

---

## 7. 심볼 가시성, 인터포지션, LD_PRELOAD, dlopen

### 7.1 직관 — 공유 라이브러리의 "전역"은 프로세스 전체의 전역이다

펌웨어에서 `static`이 아닌 함수는 "링크 단위 전체에서 보인다". 공유 라이브러리에서는 한 단계 더 나간다. 기본 가시성(`default`)인 심볼은 **동적 심볼 테이블(`.dynsym`)에 올라가서 프로세스 안 모든 모듈과 경쟁**한다. ld.so는 심볼을 찾을 때 "로드 순서(실행 파일 → LD_PRELOAD → DT_NEEDED 순)에서 처음 찾은 것"을 쓴다. 이를 **interposition**(끼어들기)이라고 한다.

결과 두 가지:

1. 좋은 쪽: `LD_PRELOAD`로 `malloc`, `open` 같은 libc 함수를 **재빌드 없이** 바꿔 끼울 수 있다 (디버깅, 계측, 테스트용 가짜 하드웨어).
2. 나쁜 쪽: 라이브러리 내부 함수끼리 부를 때도 "누가 끼어들었을지 모르니" **PLT를 거쳐야** 한다. 느리고, 내부 함수 이름이 ABI로 새어 나간다.

해결책이 `-fvisibility=hidden` + 공개 API에만 `__attribute__((visibility("default")))`다.

### 7.2 예제 15 — `-fvisibility=hidden`의 효과

무엇을 확인하나: 같은 소스를 기본 가시성과 hidden으로 빌드했을 때 내보내는 심볼과 내부 호출 방식이 어떻게 달라지는지.

```c
/* ex8: symbol visibility — 공개 API 하나, 내부 헬퍼 둘 */
#define API __attribute__((visibility("default")))

int crc_step(int c, int b) { return (c << 1) ^ b; }      /* 내부용인데 extern */
int crc_finish(int c) { return c ^ 0xff; }               /* 내부용인데 extern */

API int crc_calc(const unsigned char *p, int n)          /* 유일한 공개 API */
{
    int c = 0;
    for (int i = 0; i < n; i++) c = crc_step(c, p[i]);
    return crc_finish(c);
}
```

```sh
gcc -Wall -Wextra -O2 -fPIC -shared $S/vis.c -o libcrc_default.so
gcc -Wall -Wextra -O2 -fPIC -shared -fvisibility=hidden $S/vis.c -o libcrc_hidden.so
for l in libcrc_default.so libcrc_hidden.so; do
  echo "--- $l"; nm -D --defined-only $l
  objdump -d --no-show-raw-insn $l | sed -n "/<crc_calc>:/,/^\$/p" | grep -E "bl|b\s"
done
```

```text
--- libcrc_default.so
0000000000000630 T crc_calc
0000000000000628 T crc_finish
0000000000000620 T crc_step
 650:	ldrb	w1, [x19], #1
 654:	bl	4f0 <crc_step@plt>
 668:	b	4e0 <crc_finish@plt>
 670:	b	4e0 <crc_finish@plt>
--- libcrc_hidden.so
0000000000000590 T crc_calc
 5a0:	ldrb	w3, [x0], #1
```

출력에서 볼 것:

- default: 내부 헬퍼 두 개까지 export되었고, **같은 `.so` 안의 함수를 부르는데도 `crc_step@plt`를 거친다.** 누군가 `crc_step`을 interpose할 수 있으니 컴파일러가 인라인도 못 했다.
- hidden: export는 `crc_calc` 하나. 헬퍼는 **인라인되어 사라졌다**(호출 명령 자체가 없다). 더 작고, 더 빠르고, ABI 표면이 좁다.
- (`ldrb` 줄은 `grep "b\s"` 패턴에 우연히 걸린 것이라 무시한다.)
- 라이브러리를 만들 때 규칙: **`-fvisibility=hidden`을 기본으로 하고 공개 API만 매크로로 연다.** 커널도 같은 철학이다 — 모듈 안의 함수는 기본으로 모듈 밖에 안 보이고, `EXPORT_SYMBOL`로 연 것만 다른 모듈이 쓸 수 있다(§11).

### 7.3 예제 16 — LD_PRELOAD로 `open()`/`fopen()` 가로채기

무엇을 확인하나: 앱 소스 수정 없이 어떤 파일을 여는지 로그를 찍는 shim 라이브러리. `dlsym(RTLD_NEXT, ...)`로 "다음 순서의 진짜 함수"를 찾아 넘긴다.

```c
/* ex9: LD_PRELOAD 로 open()/fopen() 가로채기 — 앱 소스 수정 없이 어떤 파일을 여는지 로그 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

int open(const char *path, int flags, ...)
{
    static int (*real_open)(const char *, int, ...);
    if (!real_open)                                   /* 다음 순서의 진짜 open = libc 것 */
        real_open = (int (*)(const char *, int, ...))dlsym(RTLD_NEXT, "open");
    mode_t mode = 0;
    if (flags & (O_CREAT | O_TMPFILE)) {
        va_list ap; va_start(ap, flags); mode = va_arg(ap, mode_t); va_end(ap);
    }
    int fd = real_open(path, flags, mode);
    dprintf(2, "[logopen] open(\"%s\", 0x%x) = %d\n", path, flags, fd);
    return fd;
}

FILE *fopen(const char *path, const char *m)
{
    static FILE *(*real_fopen)(const char *, const char *);
    if (!real_fopen)
        real_fopen = (FILE *(*)(const char *, const char *))dlsym(RTLD_NEXT, "fopen");
    FILE *f = real_fopen(path, m);
    dprintf(2, "[logopen] fopen(\"%s\", \"%s\") = %p\n", path, m, (void *)f);
    return f;
}
```

```c
/* ex9 대상: 설정 파일과 장치 파일을 여는 가짜 센서 데몬 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int main(void)
{
    FILE *cfg = fopen("/etc/hostname", "r");
    if (!cfg) { perror("fopen"); exit(1); }
    fclose(cfg);
    int fd = open("/dev/imu0", O_RDONLY);       /* 없는 장치 → -1 */
    if (fd < 0) perror("open /dev/imu0");
    else close(fd);
    return 0;
}
```

```sh
gcc -Wall -Wextra -O2 -fPIC -shared $S/logopen.c -o liblogopen.so
gcc -Wall -Wextra -O2 $S/sensord.c -o sensord
echo "--- plain"; ./sensord
echo "--- LD_PRELOAD"; LD_PRELOAD=$PWD/liblogopen.so ./sensord
LD_PRELOAD=$PWD/liblogopen.so LD_DEBUG=bindings ./sensord 2>&1 | grep -E "open" | grep -v "^\["
```

```text
--- plain
open /dev/imu0: No such file or directory
--- LD_PRELOAD
[logopen] fopen("/etc/hostname", "r") = 0xb907868f5010
[logopen] open("/dev/imu0", 0x0) = -1
open /dev/imu0: No such file or directory
    386627:	binding file /tmp/B3/liblogopen.so [0] to /usr/lib/aarch64-linux-gnu/libc.so.6 [0]: normal symbol `__cxa_finalize' [GLIBC_2.17]
    386627:	binding file /tmp/B3/liblogopen.so [0] to /lib/ld-linux-aarch64.so.1 [0]: normal symbol `__stack_chk_guard' [GLIBC_2.17]
    386627:	binding file ./sensord [0] to /tmp/B3/liblogopen.so [0]: normal symbol `fopen' [GLIBC_2.17]
    386627:	binding file ./sensord [0] to /tmp/B3/liblogopen.so [0]: normal symbol `open' [GLIBC_2.17]
    386627:	calling init: /tmp/B3/liblogopen.so
    386627:	binding file /tmp/B3/liblogopen.so [0] to /usr/lib/aarch64-linux-gnu/libc.so.6 [0]: normal symbol `dlsym' [GLIBC_2.34]
    386627:	binding file /tmp/B3/liblogopen.so [0] to /usr/lib/aarch64-linux-gnu/libc.so.6 [0]: normal symbol `fopen'
    386627:	binding file /tmp/B3/liblogopen.so [0] to /usr/lib/aarch64-linux-gnu/libc.so.6 [0]: normal symbol `__dprintf_chk' [GLIBC_2.17]
    386627:	binding file /tmp/B3/liblogopen.so [0] to /usr/lib/aarch64-linux-gnu/libc.so.6 [0]: normal symbol `open'
open /dev/imu0: No such file or directory
    386627:	calling fini: /tmp/B3/liblogopen.so [0]
    386627:	binding file /tmp/B3/liblogopen.so [0] to /usr/lib/aarch64-linux-gnu/libc.so.6 [0]: normal symbol `__cxa_finalize' [GLIBC_2.17]
```

출력에서 볼 것:

- `binding file ./sensord ... to /tmp/B3/liblogopen.so: normal symbol 'fopen'` — 앱의 `fopen`·`open`이 libc가 아니라 **내 shim에 묶였다**. 그 뒤 shim 안의 `dlsym(RTLD_NEXT)`가 libc의 진짜 `fopen`·`open`에 묶였다.
- `fopen`은 내부적으로 `open`을 부르지만 `[logopen] open("/etc/hostname")` 로그는 **없다**. glibc 내부 호출은 PLT를 거치지 않는 내부 별칭으로 가서 interpose되지 않는다. "LD_PRELOAD는 **모듈 경계를 넘는 호출만** 가로챈다"를 기억하자.
- 같은 shim으로 `cat /etc/hostname`을 돌렸을 때는 로그가 하나도 안 나왔다. 이 VM의 `cat`은 Rust로 쓴 uutils coreutils(`/usr/lib/cargo/bin/coreutils/cat`)이고, `nm -D`로 보면 `open`, `open64`, `openat`, `openat64`를 모두 import한다. 실제로 그중 내가 감싸지 않은 쪽을 쓰는 것으로 보인다. 진지한 shim은 `open64`·`openat`·`openat64`·`fopen64`까지 감싸야 한다. 더 확실한 관찰이 필요하면 LD_PRELOAD 대신 `strace -e trace=openat`(syscall 수준, A10)을 쓴다.
- `malloc`을 감쌀 때는 함정이 하나 더 있다: `dlsym` 자체가 내부에서 메모리를 할당할 수 있어서 무한 재귀가 생길 수 있다. 그래서 glibc는 `__libc_malloc` 같은 내부 진입점을 제공하고, 실무에서는 `LD_PRELOAD=libjemalloc.so`처럼 완성된 할당자를 통째로 바꾸거나 `ltrace -e malloc`을 쓴다(이 노트에서는 malloc shim을 실행하지 않았다).

임베디드 활용 예: 센서 키트의 데몬이 `/dev/i2c-1`을 여는데 개발 PC에는 장치가 없다. LD_PRELOAD shim이 `open("/dev/i2c-1")`을 가로채 녹화된 데이터를 내주는 가짜 fd(예: `memfd`나 pipe)를 돌려주면 **하드웨어 없이 데몬을 테스트**할 수 있다.

> 보안 메모: setuid 프로그램에서는 ld.so가 `LD_PRELOAD`·`LD_LIBRARY_PATH`를 무시하거나 제한한다(secure-execution 모드). 그렇지 않으면 누구나 root 프로세스에 코드를 주입할 수 있기 때문이다.

### 7.4 예제 17 — `dlopen`/`dlsym` 플러그인 로더

무엇을 확인하나: 빌드 시에 링크하지 않은 `.so`를 실행 중에 골라 불러오는 플러그인 패턴. 센서 키트에서 "설정 파일에 적힌 IMU 모델에 맞는 드라이버 플러그인을 로드"하는 구조다.

```c
/* 플러그인 ABI (plugin.h): 각 .so 는 이 구조체 하나를 "sensor_plugin" 이름으로 내보낸다 */
struct sensor_plugin {
    const char *name;
    int (*read)(void);
};
```

```c
/* bmi088.c — icm42688.c 도 이름·값만 다르고 같은 모양 */
#include "plugin.h"
static int bmi_read(void) { return 1088; }
__attribute__((visibility("default")))
const struct sensor_plugin sensor_plugin = { "bmi088", bmi_read };
```

```c
/* ex10: dlopen/dlsym 플러그인 로더 — 설정에 적힌 IMU 드라이버(.so)를 실행 중에 고른다 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include "plugin.h"

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        void *h = dlopen(argv[i], RTLD_NOW | RTLD_LOCAL);
        if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); continue; }
        const struct sensor_plugin *p = dlsym(h, "sensor_plugin");
        if (!p) { fprintf(stderr, "dlsym: %s\n", dlerror()); dlclose(h); continue; }
        printf("loaded %-12s name=%-9s read()=%d\n", argv[i], p->name, p->read());
        dlclose(h);
    }
    return 0;
}
```

```sh
for p in bmi088 icm42688; do gcc -Wall -Wextra -O2 -fPIC -shared -fvisibility=hidden -I$S $S/$p.c -o $p.so; done
gcc -Wall -Wextra -O2 -I$S $S/host.c -o host
./host ./bmi088.so ./icm42688.so ./nope.so /usr/lib/aarch64-linux-gnu/libm.so.6 bmi088.so
readelf -d host | grep NEEDED
nm -D --defined-only bmi088.so
```

```text
loaded ./bmi088.so  name=bmi088    read()=1088
loaded ./icm42688.so name=icm42688  read()=42688
dlopen: ./nope.so: cannot open shared object file: No such file or directory
dlsym: /usr/lib/aarch64-linux-gnu/libm.so.6: undefined symbol: sensor_plugin
dlopen: bmi088.so: cannot open shared object file: No such file or directory
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
```
(마지막 `nm` 출력: `000000000001fe30 D sensor_plugin` — 플러그인이 내보내는 심볼은 이것 하나뿐이다.)

출력에서 볼 것:

- `host`의 NEEDED에는 플러그인이 없다. 빌드 시에는 플러그인을 전혀 모른다. glibc 2.34부터 `dlopen`이 libc에 들어와서 `-ldl`도 필요 없다.
- **`bmi088.so`(슬래시 없음)는 실패했다.** 슬래시가 없으면 dlopen은 현재 디렉터리가 아니라 그림 3의 **라이브러리 탐색 순서**로 찾는다. 플러그인 경로는 항상 절대 경로나 `./`로 준다.
- 플러그인 ABI는 **함수 하나 대신 구조체 하나**를 내보내는 방식이 좋다. 버전 필드를 넣어 호환성을 검사할 수 있고, dlsym 호출이 한 번이다. 커널 드라이버의 `struct file_operations`, `struct i2c_driver`가 정확히 이 패턴이다(G 모듈).
- `RTLD_NOW`: dlopen 시점에 모든 심볼을 해결해서, 플러그인에 빠진 심볼이 있으면 dlopen이 바로 실패한다(lazy의 "나중에 터짐" 회피). `RTLD_LOCAL`: 플러그인의 심볼이 다음 플러그인의 심볼 해석에 끼어들지 않게 한다.

---

## 8. "파일이 있는데 실행이 안 된다" — 임베디드 단골 에러 진단

### 8.1 에러 메시지 지도

| 메시지 | 누가 냈나 | 진짜 원인 | 첫 확인 명령 |
|---|---|---|---|
| `not found` (busybox ash, dash) / `cannot execute: required file not found` (bash 5.2+) | 셸 (execve가 ENOENT) | **PT_INTERP의 로더가 없음** | `readelf -l app` 에서 interpreter 확인 |
| `cannot execute binary file: Exec format error` | 셸 (execve가 ENOEXEC) | 다른 아키텍처·ELF class, 지원 안 되는 형식 | `file app`, `readelf -h app` |
| `error while loading shared libraries: X: cannot open shared object file` | ld.so | DT_NEEDED 라이브러리를 탐색 경로에서 못 찾음 | `readelf -d app`, `LD_DEBUG=libs` |
| `... X: wrong ELF class: ELFCLASS32` | ld.so | 찾은 `.so`가 32비트/다른 아키텍처 | `file` 로 그 `.so` 확인 |
| `symbol lookup error: undefined symbol: foo` | ld.so | 라이브러리 버전 불일치 (ABI) | `nm -D --defined-only lib.so` 에서 foo 검색 |
| `version 'GLIBC_2.38' not found` | ld.so | 더 새 glibc로 빌드한 바이너리 | `objdump -T app` 에서 버전 태그 확인 (B2) |
| `undefined reference to foo` | 빌드 시 ld | 링크할 때 정의를 못 찾음 (`-l` 순서 등) | `nm`, `-Wl,--trace-symbol=foo` |

### 8.2 예제 18 — 로더가 없을 때: "No such file or directory"의 정체

무엇을 확인하나: `patchelf --set-interpreter`로 존재하지 않는 로더 경로를 박아 넣으면 실행 파일이 멀쩡히 있어도 ENOENT가 난다는 것. 크로스 컴파일한 바이너리를 다른 libc의 보드에 올렸을 때 정확히 이 상황이다.

```sh
cp hello_pie hello_noloader && patchelf --set-interpreter /lib/ld-linux-aarch64.so.9 hello_noloader
ls -l hello_noloader
./hello_noloader; echo "exit=$?"
strace ./hello_noloader 2>&1 | head -2
readelf -l hello_noloader | grep interpreter
dash -c ./hello_noloader; busybox sh -c ./hello_noloader
```

```text
-rwxrwxr-x 1 donh donh 131417 Oct 10 08:01 hello_noloader
bash: line 1: ./hello_noloader: cannot execute: required file not found
exit=127
execve("./hello_noloader", ["./hello_noloader"], 0xffffe711c4b0 /* 19 vars */) = -1 ENOENT (No such file or directory)
strace: exec: No such file or directory
      [Requesting program interpreter: /lib/ld-linux-aarch64.so.9]
dash: 1: ./hello_noloader: not found
sh: ./hello_noloader: not found
```

출력에서 볼 것:

- 파일은 `ls`에 분명히 있다. 그런데 `execve`가 **ENOENT**. 커널이 PT_INTERP 경로(`/lib/ld-linux-aarch64.so.9`)를 열다가 실패했고, 그 에러가 그대로 올라왔다. ENOENT는 "**무엇이** 없는지"를 말해 주지 않는다.
- 셸마다 메시지가 다르다: 최신 bash는 "required file not found"로 힌트를 주지만, **임베디드 보드의 busybox는 그냥 "not found"** 다. 보드에서 이걸 보면 반사적으로 `readelf -l`(또는 호스트에서 `file`)로 interpreter를 확인한다.
- 원래 로더로 고치면(`patchelf --set-interpreter /lib/ld-linux-aarch64.so.1`) 다시 잘 돈다(실행 확인함). 로더를 직접 실행하는 우회도 된다: `/lib/ld-linux-aarch64.so.1 ./hello_pie` → `hello ELF 42 43`.
- patchelf가 interp 문자열을 늘리면서 파일이 70,616 → 131,417바이트로 커졌다. 새 세그먼트를 덧붙이기 때문이다.

덤으로, 이 VM에는 musl 로더(`/lib/ld-musl-aarch64.so.1`)가 있어서 "glibc로 빌드한 바이너리를 musl 로더로 돌리면?"도 실행해 봤다.

```sh
cp hello_pie hello_badinterp && patchelf --set-interpreter /lib/ld-musl-aarch64.so.1 hello_badinterp && ./hello_badinterp; echo "exit=$?"
```

```text
Error relocating ./hello_badinterp: __printf_chk: symbol not found
exit=127
```

로더는 찾았지만 glibc 전용 심볼(`__printf_chk`)이 musl에 없다. Alpine 기반 컨테이너에 glibc 바이너리를 넣으면 보는 바로 그 에러다(B2에서 자세히).

### 8.3 예제 19 — 다른 아키텍처: "Exec format error"와 qemu-user

무엇을 확인하나: 32비트 ARM(armhf), x86_64, RISC-V 바이너리를 aarch64 VM에서 실행하면 각각 어떻게 되는지.

```sh
arm-linux-gnueabihf-gcc -Wall -Wextra -O2 $S/hello.c -o hello_armhf
x86_64-linux-gnu-gcc -Wall -Wextra -O2 $S/hello.c -o hello_x86
riscv64-linux-gnu-gcc -O2 -static $S/hello.c -o hello_rv_static
file hello_armhf hello_x86 hello_rv_static | cut -c1-140
echo "--- armhf"; ./hello_armhf; echo "exit=$?"
echo "--- x86_64"; ./hello_x86; echo "exit=$?"
echo "--- rv static"; ./hello_rv_static; echo "exit=$?"
readelf -h hello_armhf | grep -E "Class|Machine"
```

```text
hello_armhf:     ELF 32-bit LSB pie executable, ARM, EABI5 version 1 (SYSV), dynamically linked, interpreter /lib/ld-linux-armhf.so.3, Build
hello_x86:       ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV), dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2, Build
hello_rv_static: ELF 64-bit LSB executable, UCB RISC-V, RVC, double-float ABI, version 1 (GNU/Linux), statically linked, BuildID[sha1]=f6300
--- armhf
bash: line 1: ./hello_armhf: cannot execute binary file: Exec format error
exit=126
--- x86_64
qemu-x86_64: Could not open '/lib64/ld-linux-x86-64.so.2': No such file or directory
exit=255
--- rv static
hello ELF 42 43
exit=0
  Class:                             ELF32
  Machine:                           ARM
```

출력에서 볼 것 — 세 개가 전부 다르게 실패/성공했다.

- **armhf → ENOEXEC**: 커널의 ELF 로더가 `Machine: ARM`, `ELF32`를 보고 거절했다. 이 커널은 `CONFIG_COMPAT=y`(32비트 유저 지원)로 빌드됐지만, **Apple Silicon CPU가 AArch32 EL0를 지원하지 않기 때문에** 32비트 바이너리를 못 돌린다. Cortex-A53/A72 같은 실제 SoC에서는 COMPAT로 armhf 바이너리가 돌 수 있다(그 경우엔 armhf 로더·라이브러리가 rootfs에 있어야 한다). 반대로 Cortex-A715, Cortex-X2 같은 일부 Armv9 코어는 AArch32를 아예 빼서 다시 ENOEXEC가 난다.
- **x86_64 → qemu가 받았다**: 이 VM에는 `binfmt_misc`에 `qemu-x86_64`가 등록되어 있어서, 커널이 ELF 헤더의 매직을 보고 qemu로 넘겼다. qemu-user는 x86 로더(`/lib64/ld-linux-x86-64.so.2`)를 못 찾아서 실패했다 — §8.2와 같은 "로더 없음"이 한 단계 위에서 일어난 것이다.
- **RISC-V static → 성공**: binfmt로 `qemu-riscv64`가 실행했고, 정적 링크라 로더가 필요 없었다. "**크로스 바이너리를 빨리 테스트하려면 static으로**"가 실용 팁이다.

sysroot를 주면 armhf 동적 바이너리도 qemu-user로 돌릴 수 있다.

```sh
qemu-arm ./hello_armhf; echo "exit=$?"
qemu-arm -L /usr/arm-linux-gnueabihf ./hello_armhf
```

```text
qemu-arm: Could not open '/lib/ld-linux-armhf.so.3': No such file or directory
exit=255
hello ELF 42 43
```

`-L`은 "타깃의 `/`는 이 디렉터리라고 생각하라"는 뜻이다. 크로스 툴체인의 sysroot(B1)가 정확히 이 용도다. Yocto/Buildroot SDK도 같은 방식으로 호스트에서 타깃 유닛 테스트를 돌린다.

### 8.4 예제 20 — 크로스 바이너리의 의존성 보기, 32비트 `.so` 섞임

무엇을 확인하나: `ldd`가 크로스 바이너리에 안 통하는 것과 그 대안, 그리고 아키텍처가 다른 `.so`가 탐색 경로에 섞였을 때의 메시지.

```sh
ldd ./hello_armhf; echo "exit=$?"
arm-linux-gnueabihf-readelf -d hello_armhf | grep NEEDED
objdump -p hello_armhf | grep -E "NEEDED|file format"
qemu-arm -L /usr/arm-linux-gnueabihf /usr/arm-linux-gnueabihf/lib/ld-linux-armhf.so.3 --list ./hello_armhf
```

```text
	not a dynamic executable
exit=1
 0x00000001 (NEEDED)                     Shared library: [libc.so.6]
hello_armhf:     file format elf32-littlearm
  NEEDED               libc.so.6
	linux-vdso.so.1 (0x40801000)
	libc.so.6 => /lib/libc.so.6 (0x40840000)
	/lib/ld-linux-armhf.so.3 => /usr/arm-linux-gnueabihf/lib/ld-linux-armhf.so.3 (0x00400000)
```

`ldd`는 "not a dynamic executable"이라는 **거짓말**을 한다(호스트 로더가 이 형식을 못 읽어서). `readelf -d`/`objdump -p`는 아키텍처와 상관없이 파일만 읽으므로 항상 통한다. 타깃의 로더를 qemu로 돌려 `--list`를 주면 실제 해석 결과(`ldd`와 같은 것)를 얻는다.

32비트 `libimu.so.1`을 `LD_LIBRARY_PATH`에 섞은 경우:

```sh
arm-linux-gnueabihf-gcc -O2 -fPIC -shared -Wl,-soname,libimu.so.1 $S/imu_v1.c -o lib_arm/libimu.so.1
LD_LIBRARY_PATH=$PWD/lib_arm ./app
LD_LIBRARY_PATH=$PWD/lib_arm LD_DEBUG=libs ./app 2>&1 | grep -A3 "find library=libimu" | head -4
ln -sf libimu.so.1 lib_arm/libimu.so; gcc -O2 -I$S $S/app.c -Llib_arm -limu -o app_x 2>&1 | head -3
```

```text
./app: error while loading shared libraries: libimu.so.1: wrong ELF class: ELFCLASS32
    427025:	find library=libimu.so.1 [0]; searching
    427025:	 search path=/tmp/B3/so/lib_arm		(LD_LIBRARY_PATH)
    427025:	  trying file=/tmp/B3/so/lib_arm/libimu.so.1
    427025:	    (incompatible ELF class)
/usr/bin/aarch64-linux-gnu-ld.bfd: skipping incompatible lib_arm/libimu.so when searching for -limu
/usr/bin/aarch64-linux-gnu-ld.bfd: cannot find -limu: No such file or directory
/usr/bin/aarch64-linux-gnu-ld.bfd: skipping incompatible lib_arm/libimu.so when searching for -limu
```

빌드 시 링커의 "**skipping incompatible ... when searching for -limu**"는 "호스트용 라이브러리 디렉터리를 크로스 링크에 섞었다"(혹은 그 반대)의 대표 증상이다. B1의 sysroot 설정이 잘못됐을 때 자주 본다.

### 8.5 진단 순서 (보드 위에서)

1. `file ./app` (호스트에서) 또는 `readelf -h ./app` — 아키텍처, 32/64비트, static/dynamic, interpreter.
2. `readelf -l ./app | grep interpreter` → 보드에 그 경로가 있나? `ls -l /lib/ld-*`.
3. `readelf -d ./app | grep -E "NEEDED|RPATH|RUNPATH"` → 각 NEEDED가 보드의 탐색 경로에 있나?
4. 보드에서 `LD_DEBUG=libs ./app` (glibc만. musl 로더는 `LD_DEBUG`가 없다) → 어디를 뒤졌나.
5. `symbol lookup error`면 `nm -D --defined-only` 로 보드의 `.so`에 그 심볼이 있는지, `objdump -T ./app`으로 요구하는 심볼 버전이 무엇인지.
6. 그래도 모르겠으면 `strace -f ./app 2>&1 | grep -E "execve|openat"` — 커널이 본 그대로.

---

## 9. 바이너리 도구 상자 — strip, 분리된 debug 정보, addr2line

### 9.1 도구 한 장 정리

| 도구 | 주로 보는 것 | 자주 쓰는 옵션 |
|---|---|---|
| `readelf` | ELF 구조 그대로 (헤더, 섹션, 세그먼트, dynamic, notes) | `-h -S -l -d -r -s --dyn-syms -n -x <sec> -W` |
| `objdump` | 디스어셈블과 BFD 관점 정보 | `-d` 디스어셈블, `-dr` relocation 같이, `-T` 동적 심볼, `-p` 프로그램 헤더+dynamic, `-S` 소스 섞기 |
| `nm` | 심볼 테이블 | `-D` 동적, `-C` C++ demangle, `--defined-only`, `--undefined-only`, `-S` 크기 |
| `size` | text/data/bss 합계 | `-A` 섹션별 |
| `strip` | 심볼·debug 제거 | `--strip-debug`, `--strip-unneeded`, `--strip-section-headers` |
| `objcopy` | 섹션 복사·변환 | `--only-keep-debug`, `--add-gnu-debuglink`, `-O binary` (펌웨어에서 쓰던 그것) |
| `addr2line` | 주소 → 함수·파일:줄 | `-f -i -e <elf>` |
| `patchelf` | interpreter, rpath, NEEDED, soname 수정 | `--set-interpreter --set-rpath --print-needed --replace-needed` |
| `ldd` | 실제 해석 결과 (호스트 아키텍처만) | 신뢰할 수 없는 바이너리에는 쓰지 말 것 |

몇 가지 짧은 실행 결과:

```sh
objdump -T /usr/lib/aarch64-linux-gnu/libc.so.6 | grep -wE "puts|printf|malloc"
nm -D /usr/lib/aarch64-linux-gnu/libc.so.6 | wc -l
echo "int add(int a,int b){return a+b;}" > /tmp/B3/cpp.cc; g++ -c /tmp/B3/cpp.cc -o /tmp/B3/cpp.o && nm /tmp/B3/cpp.o; nm -C /tmp/B3/cpp.o
size hello_pie hello_static hello_musl_static
```

```text
0000000000076b60 g    DF .text	0000000000000268  GLIBC_2.17  puts
000000000009df60 g    DF .text	0000000000000250  GLIBC_2.17  malloc
000000000004f8a0 g    DF .text	00000000000000c0  GLIBC_2.17  printf
3100
0000000000000000 T _Z3addii
0000000000000000 T add(int, int)
   text	   data	    bss	    dec	    hex	filename
   1695	    644	     12	   2351	    92f	hello_pie
 531881	  21532	  21760	 575173	  8c6c5	hello_static
  32046	    448	   1608	  34102	   8536	hello_musl_static
```

`printf`의 libc 내부 오프셋 `0x4f8a0`이 §3.2 출력의 `printf=0x...f8a0`과 끝자리가 맞는다는 것도 확인해 보자. libc 기준 주소 + `0x4f8a0`이다.

### 9.2 예제 21 — 크래시 주소를 소스 줄로 (M3 미리보기)

무엇을 확인하나: SIGSEGV 핸들러가 찍은 PC를 "모듈 기준 오프셋"으로 바꾸고 `addr2line`으로 소스 줄을 찾는 것. 이어서 debug 정보를 분리한 stripped 바이너리(보드에 올리는 것)로도 같은 일을 하는 것.

```c
/* ex11: 크래시 주소 → 소스 줄. SIGSEGV 핸들러가 PC 와 "바이너리 기준 오프셋"을 찍는다 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <unistd.h>

static void on_segv(int sig, siginfo_t *si, void *ctx)
{
    ucontext_t *uc = ctx;
    void *pc = (void *)uc->uc_mcontext.pc;          /* aarch64: 실패한 명령의 주소 */
    Dl_info di;
    if (dladdr(pc, &di) && di.dli_fbase)
        dprintf(2, "SIGSEGV addr=%p pc=%p  %s+0x%lx\n", si->si_addr, pc,
                di.dli_fname, (unsigned long)((char *)pc - (char *)di.dli_fbase));
    (void)sig;
    _exit(139);
}

struct imu_sample { int ax, ay, az; };

__attribute__((noinline)) int parse_sample(const struct imu_sample *s)
{
    return s->ax + s->ay + s->az;                   /* s == NULL 이면 여기서 죽는다 */
}

__attribute__((noinline)) int process(int n)
{
    struct imu_sample *s = (n > 2) ? NULL : malloc(sizeof *s);   /* 버그: 3번째에 NULL */
    if (s) { s->ax = s->ay = s->az = n; }
    int v = parse_sample(s);
    free(s);
    return v;
}

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);               /* _exit 전에 줄 단위로 내보내기 */
    struct sigaction sa = { .sa_sigaction = on_segv, .sa_flags = SA_SIGINFO };
    if (sigaction(SIGSEGV, &sa, NULL) < 0) { perror("sigaction"); exit(1); }
    for (int i = 1; i <= 3; i++)
        printf("sample %d -> %d\n", i, process(i));
    return 0;
}
```

(`dprintf`는 엄밀히 async-signal-safe가 아니다. 데모용이고, 실제 크래시 핸들러는 `write()`로 직접 포맷한 버퍼를 쓴다 — A6 참고.)

```sh
gcc -Wall -Wextra -O2 -g $S/crash.c -o crash && ./crash; echo "exit=$?"
addr2line -f -i -e crash 0xcf0
objdump -d --no-show-raw-insn crash | grep -B3 -A1 " cf0:"
```

```text
sample 1 -> 3
sample 2 -> 6
SIGSEGV addr=(nil) pc=0xb84234770cf0  ./crash+0xcf0
exit=139
parse_sample
/Users/donh/.elinux_scratch/B3/crash.c:26
 cec:	bl	910 <_exit@plt>

0000000000000cf0 <parse_sample>:
 cf0:	ldp	w1, w2, [x0]
 cf4:	ldr	w0, [x0, #8]
```

출력에서 볼 것: PC `0xb84234770cf0`은 매번 바뀌지만 **오프셋 `0xcf0`은 고정**이다(§3.2의 끝자리 관찰). `addr2line`에는 오프셋을 준다(PIE·`.so`는 기준 0으로 링크되어 있으므로). `parse_sample`의 첫 명령 `ldp w1, w2, [x0]`에서 x0=NULL로 죽었다 — `addr=(nil)`과 일치한다.

> 실제로 처음에는 이전 빌드에서 얻은 오프셋 `0xc30`을 새 빌드에 넣어서 `__do_global_dtors_aux / crtstuff.c:?`라는 엉뚱한 답을 받았다. **크래시 주소는 반드시 그 크래시를 낸 바로 그 빌드의 ELF로 풀어야 한다.** 이것을 보장하는 장치가 아래의 Build ID다.

### 9.3 예제 22 — debug 정보 분리: `--only-keep-debug`, debuglink, Build ID

보드에는 작은 stripped 바이너리를, 서버에는 debug 정보를 따로 보관하는 것이 표준 흐름이다(Yocto의 `-dbg` 패키지, Debian의 `-dbgsym`).

```sh
objcopy --only-keep-debug crash crash.debug
strip --strip-debug --strip-unneeded crash -o crash.stripped
objcopy --add-gnu-debuglink=crash.debug crash.stripped
ls -l crash crash.stripped crash.debug
readelf -n crash.stripped | grep "Build ID"
readelf --string-dump=.gnu_debuglink crash.stripped
nm crash.stripped 2>&1 | head -2
addr2line -f -e crash.stripped 0xcf0
```

```text
-rwxrwxr-x 1 donh donh 78992 Oct 10 08:01 crash
-rwxrwxr-x 1 donh donh 17376 Oct 10 08:01 crash.debug
-rwxrwxr-x 1 donh donh 67704 Oct 10 08:01 crash.stripped
    Build ID: 1c180a61b5901c487d0dc84b4f5eb0e69cfeffee

String dump of section '.gnu_debuglink':
  [     0]  crash.debug
  [     f]  K

nm: crash.stripped: no symbols
parse_sample
/Users/donh/.elinux_scratch/B3/crash.c:26
```

출력에서 볼 것:

- stripped 바이너리는 `nm`으로 심볼이 하나도 안 보이는데 `addr2line`은 답을 냈다. `.gnu_debuglink` 섹션에 적힌 이름(`crash.debug`)과 CRC(뒤의 `K` 부분이 CRC 바이트의 일부)를 보고 **같은 디렉터리의 `crash.debug`를 자동으로 찾아 읽었기** 때문이다.
- Build ID(`.note.gnu.build-id`, 링커가 내용 해시로 만든 20바이트)는 stripped와 debug 파일에 **같은 값**으로 들어 있다. "이 크래시를 낸 바이너리와 이 debug 파일이 정말 같은 빌드인가?"를 보장하는 지문이다. 크래시 리포트에 Build ID를 같이 남기자.

`.debug` 파일을 치우고 다시 해 보면, 그리고 Build ID 경로로 찾게 하면:

```sh
mkdir -p dbgstore && mv crash.debug dbgstore/
addr2line -f -e crash.stripped 0xcf0
gdb -q -batch -ex "set debuginfod enabled off" -ex "info line *0xcf0" ./crash.stripped 2>&1 | grep -v "^\["
B=$(readelf -n crash.stripped | sed -n "s/.*Build ID: //p")
mkdir -p dbgstore/.build-id/${B:0:2} && cp dbgstore/crash.debug dbgstore/.build-id/${B:0:2}/${B:2}.debug
ls dbgstore/.build-id/*/
gdb -q -batch -ex "set debuginfod enabled off" -ex "set debug-file-directory /tmp/B3/dbgstore" -ex "file ./crash.stripped" -ex "info line *0xcf0" 2>&1 | grep -v "^\["
```

```text
??
??:0
...
No line number information available for address 0xcf0
180a61b5901c487d0dc84b4f5eb0e69cfeffee.debug
Line 26 of "/Users/donh/.elinux_scratch/B3/crash.c" starts at address 0xcf0 <parse_sample> and ends at 0xcfc <parse_sample+12>.
```
(`...` 자리의 gdb debuginfod 안내 메시지 다섯 줄은 생략했다.)

출력에서 볼 것: debug 파일이 없으면 `??`. gdb는 `debug-file-directory` 아래 **`.build-id/1c/180a...ee.debug`** 경로 규칙으로 debug 파일을 찾았다. 배포판의 `/usr/lib/debug/.build-id/` 구조와 debuginfod 서버가 모두 이 규칙을 쓴다. 팀에서 크래시 분석 서버를 만든다면 "빌드마다 debug 파일을 Build ID 경로로 저장"하는 것이 첫 단계다(M3에서 core dump와 함께 다시).

---

## 10. 크기 — static vs dynamic vs stripped (측정)

임베디드 rootfs는 용량이 빠듯하다. 같은 `hello.c`로 직접 쟀다.

```sh
strip -o hello_pie.s hello_pie && strip -o hello_static.s hello_static
musl-gcc -O2 -static $S/hello.c -o hello_musl_static && strip -o hello_musl_static.s hello_musl_static
gcc -O2 -Wl,-z,max-page-size=4096 $S/hello.c -o hello_4k && strip -o hello_4k.s hello_4k
for f in hello_pie hello_pie.s hello_static hello_static.s hello_musl_static hello_musl_static.s; do printf "%-20s %8d bytes\n" $f $(stat -c %s $f); done
stat -c "%n %s" hello_4k hello_4k.s
ls -lL /usr/lib/aarch64-linux-gnu/libc.so.6 /lib/ld-linux-aarch64.so.1
```

```text
hello_pie               70616 bytes
hello_pie.s             67616 bytes
hello_static           712048 bytes
hello_static.s         603608 bytes
hello_musl_static       80432 bytes
hello_musl_static.s     66960 bytes
hello_4k 9176
hello_4k.s 6176
-rwxr-xr-x 1 root root  201872 Sep  3 07:09 /lib/ld-linux-aarch64.so.1
-rwxr-xr-x 1 root root 1788240 Sep  3 07:09 /usr/lib/aarch64-linux-gnu/libc.so.6
```

```svg
<svg viewBox="0 0 660 340" xmlns="http://www.w3.org/2000/svg">
  <text x="330" y="22" text-anchor="middle" font-size="14" font-weight="bold">같은 hello.c, 파일 크기 (바이트)</text>
  <text x="162" y="59" text-anchor="end" font-size="12">PIE 동적 (기본)</text>
  <rect x="170" y="44" width="38.3" height="20" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="214.3" y="59" font-size="12">70,616</text>
  <text x="162" y="89" text-anchor="end" font-size="12">PIE 동적 + strip</text>
  <rect x="170" y="74" width="36.6" height="20" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="212.6" y="89" font-size="12">67,616</text>
  <text x="162" y="119" text-anchor="end" font-size="12">동적, 4K 정렬</text>
  <rect x="170" y="104" width="5.0" height="20" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="181.0" y="119" font-size="12">9,176</text>
  <text x="162" y="149" text-anchor="end" font-size="12">동적, 4K 정렬 + strip</text>
  <rect x="170" y="134" width="3.3" height="20" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="179.3" y="149" font-size="12">6,176</text>
  <text x="162" y="179" text-anchor="end" font-size="12">glibc 정적</text>
  <rect x="170" y="164" width="385.7" height="20" fill="#d0564a" fill-opacity="0.75"/>
  <text x="561.7" y="179" font-size="12">712,048</text>
  <text x="162" y="209" text-anchor="end" font-size="12">glibc 정적 + strip</text>
  <rect x="170" y="194" width="327.0" height="20" fill="#d0564a" fill-opacity="0.75"/>
  <text x="503.0" y="209" font-size="12">603,608</text>
  <text x="162" y="239" text-anchor="end" font-size="12">musl 정적</text>
  <rect x="170" y="224" width="43.6" height="20" fill="#3f9a6b" fill-opacity="0.75"/>
  <text x="219.6" y="239" font-size="12">80,432</text>
  <text x="162" y="269" text-anchor="end" font-size="12">musl 정적 + strip</text>
  <rect x="170" y="254" width="36.3" height="20" fill="#3f9a6b" fill-opacity="0.75"/>
  <text x="212.3" y="269" font-size="12">66,960</text>
  <line x1="170" y1="38" x2="170" y2="284" stroke="currentColor"/>
  <text x="20" y="306" font-size="12">기본 PIE 7만 바이트의 대부분은 64K 세그먼트 정렬 패딩 (-z max-page-size=4096 이면 9K)</text>
  <text x="20" y="326" font-size="12">동적 바이너리는 대신 libc.so.6 (1.7MB) + ld-linux (197KB)가 타깃에 있어야 한다</text>
</svg>
```
그림 6 — 직접 잰 파일 크기. 동적 hello의 실제 내용은 수 KB이고, 기본 크기의 대부분은 aarch64 기본 64K 정렬 패딩이다.

출력에서 볼 것:

- `size`로 보면 `hello_pie`의 text+data는 2.3KB인데 파일은 70KB다. 범인은 §2.2에서 본 `Align 0x10000`. 보드 커널이 4K 페이지로 고정이라면 `-Wl,-z,max-page-size=4096`으로 9KB까지 줄어든다. 단, 그렇게 만든 바이너리는 **16K·64K 페이지 커널에서 로드가 실패할 수 있다**(세그먼트 정렬이 페이지보다 작으면 커널이 매핑을 못 한다). 일부 aarch64 SoC 배포판은 16K/64K 페이지를 쓴다.
- glibc 정적 hello는 700KB. 정적 링크해도 `printf`가 로캘·스트림·`_FORTIFY` 코드를 끌고 온다. musl 정적은 80KB(그중 상당 부분도 정렬 패딩).
- 동적 바이너리 하나는 작지만, rootfs 전체로 보면 **libc + 로더(약 2MB)를 한 번만** 가지면 수십 개 바이너리가 공유한다. 바이너리가 많으면 동적이, 하나뿐인 init·복구 도구라면 정적이 유리하다. BusyBox가 한 바이너리에 수백 개 명령을 넣는 이유도 이 계산이다.
- `strip`이 줄이는 것은 `.symtab`/`.strtab`(그리고 있으면 `.debug_*`)뿐이다. `-g`로 빌드했다면 strip 효과가 훨씬 크다(§9.3에서 `crash` 78,992 → 67,704, debug 17KB 분리).

---

## 11. 링커 스크립트 — 유저 공간에서 한 번, 커널에서 한 번

### 11.1 기본 링커 스크립트는 이미 있다

Don의 MCU 프로젝트에는 반드시 `.ld` 파일이 있었다. Linux 유저 공간에서는 `ld`에 **내장된 기본 스크립트**가 쓰인다. 보는 법:

```sh
ld --verbose | grep -E "ENTRY|SEGMENT_START|OUTPUT_ARCH|DATA_SEGMENT_RELRO_END|DATA_SEGMENT_ALIGN" | head
ld --verbose | sed -n 135,139p
ld --verbose | wc -l
```

```text
OUTPUT_ARCH(aarch64)
ENTRY(_start)
  PROVIDE (__executable_start = SEGMENT_START("text-segment", 0x400000));
  . = SEGMENT_START("text-segment", 0x400000) + SIZEOF_HEADERS;
  . = DATA_SEGMENT_ALIGN (CONSTANT (MAXPAGESIZE), CONSTANT (COMMONPAGESIZE));
  . = DATA_SEGMENT_RELRO_END (24, .);
  . = SEGMENT_START("ldata-segment", .);
  .init_array    :
  {
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
    KEEP (*(.init_array EXCLUDE_FILE (*crtbegin.o *crtbegin?.o *crtend.o *crtend?.o ) .ctors))
271
```
(줄 번호 135~139는 먼저 `ld --verbose | grep -n init_array`로 위치를 찾은 것이다. 내장 스크립트는 271줄.)

`0x400000`이 §3.2에서 본 non-PIE `main=0x400940`의 출처다. `.init_array`에 `__init_array_start`를 정의하고 `KEEP(*(...))`으로 모으는 모양이 펌웨어 스크립트와 똑같다. C의 `__attribute__((constructor))`는 함수 포인터를 `.init_array`에 넣고, libc 시작 코드가 그 배열을 돌며 호출한다. **이 패턴이 커널 initcall과 정확히 같은 구조**다. 직접 만들어 보자.

### 11.2 예제 23 — initcall을 유저 공간에서 흉내 내기 (C6 미리보기)

무엇을 확인하나: 함수 포인터를 레벨별 섹션(`.b3_initcall.<lvl>`)에 넣고, 작은 링커 스크립트로 이름순 정렬해 모은 뒤, 시작·끝 심볼로 순회하는 것. 전체 스크립트를 새로 쓰지 않고 `INSERT AFTER`로 기본 스크립트에 섹션 하나만 끼워 넣는다.

```c
/* ex13: 커널 initcall 흉내 — 함수 포인터를 레벨별 섹션에 넣고 링커 스크립트로 모아 순서대로 호출 */
#include <stdio.h>

typedef int (*initcall_t)(void);
#define DEFINE_INITCALL(fn, lvl) \
    static initcall_t __b3_initcall_##fn __attribute__((used, section(".b3_initcall." #lvl))) = fn

extern initcall_t __b3_initcall_start[], __b3_initcall_end[];   /* 링커 스크립트가 정의 */

static int clk_init(void)    { puts("  [1] clock tree");      return 0; }
static int bus_init(void)    { puts("  [2] i2c/spi bus");     return 0; }
static int imu_probe(void)   { puts("  [6] imu driver probe"); return 0; }
static int gnss_probe(void)  { puts("  [6] gnss driver probe"); return 0; }
static int app_late(void)    { puts("  [7] late: start daemon"); return 0; }

/* 일부러 뒤죽박죽 순서로 등록 */
DEFINE_INITCALL(imu_probe, 6);
DEFINE_INITCALL(app_late, 7);
DEFINE_INITCALL(clk_init, 1);
DEFINE_INITCALL(gnss_probe, 6);
DEFINE_INITCALL(bus_init, 2);

int main(void)
{
    printf("%td initcalls:\n", __b3_initcall_end - __b3_initcall_start);
    for (initcall_t *fn = __b3_initcall_start; fn < __b3_initcall_end; fn++)
        if ((*fn)() != 0) fprintf(stderr, "initcall failed\n");
    return 0;
}
```

첫 시도 — 섹션을 `.rodata` 뒤에 넣었다.

```text
/* initcall.lds: 기본 링커 스크립트는 그대로 두고, 섹션 하나만 .rodata 뒤에 끼워 넣는다 */
SECTIONS
{
  .b3_initcall : ALIGN(8)
  {
    __b3_initcall_start = .;
    KEEP(*(SORT(.b3_initcall.*)))     /* 이름순 정렬 → 레벨 1,2,6,7 순서 */
    __b3_initcall_end = .;
  }
}
INSERT AFTER .rodata;
```

```sh
gcc -Wall -Wextra -O2 -c $S/initcall.c -o initcall.o && readelf -SW initcall.o | grep b3_
gcc initcall.o -T $S/initcall.lds -o initcall && ./initcall
readelf -SW initcall | grep -E "rodata|b3_init" && nm initcall | grep -E "b3_initcall_(start|end)"
readelf -lW initcall | grep -E "LOAD"
```

```text
  [ 8] .b3_initcall.1    PROGBITS        0000000000000000 000240 000008 00  WA  0   0  8
  [ 9] .rela.b3_initcall.1 RELA            0000000000000000 000a80 000018 18   I 20   8  8
  [10] .b3_initcall.2    PROGBITS        0000000000000000 000248 000008 00  WA  0   0  8
  [11] .rela.b3_initcall.2 RELA            0000000000000000 000a98 000018 18   I 20  10  8
  [12] .b3_initcall.6    PROGBITS        0000000000000000 000250 000010 00  WA  0   0  8
  [13] .rela.b3_initcall.6 RELA            0000000000000000 000ab0 000030 18   I 20  12  8
  [14] .b3_initcall.7    PROGBITS        0000000000000000 000260 000008 00  WA  0   0  8
  [15] .rela.b3_initcall.7 RELA            0000000000000000 000ae0 000018 18   I 20  14  8
/usr/bin/aarch64-linux-gnu-ld.bfd: warning: initcall has a LOAD segment with RWX permissions
5 initcalls:
  [1] clock tree
  [2] i2c/spi bus
  [6] gnss driver probe
  [6] imu driver probe
  [7] late: start daemon
  [14] .rodata           PROGBITS        0000000000000a38 000a38 0000b1 00   A  0   0  8
  [15] .b3_initcall      PROGBITS        0000000000000af0 000af0 000028 00  WA  0   0  8
0000000000000b18 D __b3_initcall_end
0000000000000af0 D __b3_initcall_start
  LOAD           0x000000 0x0000000000000000 0x0000000000000000 0x020010 0x020018 RWE 0x10000
```

동작은 했다 — 등록 순서와 상관없이 레벨 1 → 2 → 6 → 7 순으로 불렸다. 그런데 **"LOAD segment with RWX permissions" 경고**가 났고, 실제로 세그먼트가 `RWE` 하나로 합쳐졌다. PIE에서 함수 포인터 배열은 로드 시 재배치(기준 주소 더하기)가 필요하므로 쓰기 가능(`WA`) 섹션이 된다. 그걸 읽기 전용·실행 영역(`.rodata`) 사이에 끼웠더니 링커가 코드와 데이터를 한 세그먼트로 합쳐 버렸다. 공격자에게 "쓰기도 되고 실행도 되는 메모리"를 준 셈이다. `.data` 뒤로 옮긴다.

```sh
sed 's/INSERT AFTER .rodata;/INSERT AFTER .data;/' initcall.lds > initcall_fixed.lds
gcc initcall.o -T $S/initcall_fixed.lds -o initcall2 && ./initcall2 | head -1
readelf -lW initcall2 | grep -E "LOAD|^   0[23]"
```

```text
5 initcalls:
  LOAD           0x000000 0x0000000000000000 0x0000000000000000 0x000d54 0x000d54 R E 0x10000
  LOAD           0x00fd68 0x000000000001fd68 0x000000000001fd68 0x0002d0 0x0002d8 RW  0x10000
   02     .note.gnu.build-id .interp .gnu.hash .dynsym .dynstr .gnu.version .gnu.version_r .rela.dyn .rela.plt .init .plt .text .fini .rodata .eh_frame_hdr .eh_frame .note.ABI-tag 
   03     .init_array .fini_array .dynamic .got .data .b3_initcall .bss 
```

경고 없음, R E / RW 두 세그먼트로 정상. 출력에서 볼 것:

- 이것이 커널 `module_init()`/`device_initcall()`의 원리 그대로다. 각 드라이버는 함수 포인터를 `.initcall6.init` 같은 섹션에 떨어뜨리고, `vmlinux.lds`가 레벨 순으로 모아 `__initcall_start`~`__initcall_end`를 만든다. 부팅 중 `do_initcalls()`가 그 배열을 돈다.
- **같은 레벨(6) 안의 순서**는 소스의 등록 순서(imu → gnss)와 반대로 나왔다. 같은 섹션 이름 안에서는 링커 입력 순서·컴파일러 배치 순서에 달려 있고 보장이 없다. 커널에서도 같은 레벨의 드라이버 간 순서는 **Makefile의 링크 순서**로 정해지고, 순서 의존은 `-EPROBE_DEFER`나 디바이스 링크로 푸는 것이 정석이다(G 모듈).
- 링커 스크립트에서 주소를 받는 심볼은 C에서 **배열로 선언**(`extern initcall_t __b3_initcall_start[]`)한다. 값이 아니라 **주소 자체**가 의미이기 때문이다. 펌웨어에서 `extern uint32_t _sdata;`를 `&_sdata`로 쓰던 것과 같은 이야기다.

### 11.3 커널의 링커 스크립트 — `vmlinux.lds`와 `module.lds`

이 VM의 커널 헤더 패키지에는 `vmlinux.lds` 완성본은 없고(커널 빌드 트리에서 `arch/arm64/kernel/vmlinux.lds.S`를 전처리해서 만든다), 그 재료인 매크로 헤더와 모듈용 스크립트가 들어 있다.

```sh
H=/usr/src/linux-headers-$(uname -r)
grep -n -A12 "define INIT_CALLS_LEVEL\b\|define INIT_CALLS\b" $H/include/asm-generic/vmlinux.lds.h | head -30
grep -n "define INIT_TEXT_SECTION" -A6 $H/include/asm-generic/vmlinux.lds.h
grep -n "define pure_initcall\|define core_initcall\|define device_initcall\|define late_initcall\|define __define_initcall\b" $H/include/linux/init.h
```

```text
945:#define INIT_CALLS_LEVEL(level)						\
946-		__initcall##level##_start = .;				\
947-		KEEP(*(.initcall##level##.init))			\
948-		KEEP(*(.initcall##level##s.init))			\
949-
950:#define INIT_CALLS							\
951-		__initcall_start = .;					\
952-		KEEP(*(.initcallearly.init))				\
953-		INIT_CALLS_LEVEL(0)					\
954-		INIT_CALLS_LEVEL(1)					\
955-		INIT_CALLS_LEVEL(2)					\
956-		INIT_CALLS_LEVEL(3)					\
957-		INIT_CALLS_LEVEL(4)					\
958-		INIT_CALLS_LEVEL(5)					\
959-		INIT_CALLS_LEVEL(rootfs)				\
960-		INIT_CALLS_LEVEL(6)					\
961-		INIT_CALLS_LEVEL(7)					\
962-		__initcall_end = .;
1154:#define INIT_TEXT_SECTION(inittext_align)				\
1155-	. = ALIGN(inittext_align);					\
1156-	.init.text : AT(ADDR(.init.text) - LOAD_OFFSET) {		\
1157-		_sinittext = .;						\
1158-		INIT_TEXT						\
1159-		_einittext = .;						\
1160-	}
277:#define __define_initcall(fn, id) ___define_initcall(fn, id, .initcall##id)
293:#define pure_initcall(fn)		__define_initcall(fn, 0)
295:#define core_initcall(fn)		__define_initcall(fn, 1)
296:#define core_initcall_sync(fn)		__define_initcall(fn, 1s)
306:#define device_initcall(fn)		__define_initcall(fn, 6)
307:#define device_initcall_sync(fn)	__define_initcall(fn, 6s)
308:#define late_initcall(fn)		__define_initcall(fn, 7)
309:#define late_initcall_sync(fn)		__define_initcall(fn, 7s)
```

예제 23과 한 줄 한 줄 대응한다: `KEEP(*(.initcall6.init))` ↔ `KEEP(*(SORT(.b3_initcall.*)))`, `__initcall6_start` ↔ `__b3_initcall_start`. `.init.text`에 `AT(ADDR(...) - LOAD_OFFSET)`이 붙은 것은 Don의 MCU 스크립트의 `AT>`(LMA ≠ VMA)와 같은 장치다 — 커널은 물리 주소에 로드되지만 가상 주소에서 실행되기 때문이다.

실행 중인 커널에서 이 심볼들을 확인한다(커널 주소는 root만 볼 수 있다. 일반 사용자로 읽으면 `kptr_restrict` 때문에 0으로 보인다).

```sh
sudo grep -E " (__initcall_start|__initcall6_start|__initcall7_start|__initcall_end|_sinittext|_einittext|_stext|_etext)$" /proc/kallsyms
grep -E " (__initcall6_start|_sinittext)$" /proc/kallsyms
sudo dmesg | grep -i "freeing unused\|Freeing initrd"
grep -E "PREL32|CONFIG_RANDOMIZE_BASE" /boot/config-$(uname -r)
```

```text
ffffbcf0c5c70000 T _stext
ffffbcf0c7790000 D _etext
ffffbcf0c8b50000 T _sinittext
ffffbcf0c8c1cf60 T _einittext
ffffbcf0c8e2cde8 D __initcall_start
ffffbcf0c8e2d750 D __initcall6_start
ffffbcf0c8e2e270 D __initcall7_start
ffffbcf0c8e2e49c D __initcall_end
0000000000000000 T _sinittext
0000000000000000 D __initcall6_start
[    0.184452] Freeing initrd memory: 88984K
[    0.191399] Freeing unused kernel memory: 15168K
CONFIG_RANDOMIZE_BASE=y
CONFIG_HAVE_ARCH_PREL32_RELOCATIONS=y
```

출력에서 볼 것:

- `__initcall6_start`~`__initcall7_start` = 0xb20 바이트. 이 커널은 `CONFIG_HAVE_ARCH_PREL32_RELOCATIONS=y`라서 initcall 엔트리가 8바이트 포인터가 아니라 **4바이트 상대 오프셋**이다. 그러니 이 커널에 내장된 레벨 6(device) initcall은 0xb20 / 4 = **712개**, 전체(`__initcall_start`~`__initcall_end`)는 1,453개다(Python으로 계산).
- `_sinittext`~`_einittext` ≈ 819KB의 `.init.text`. 부팅이 끝나면 `free_initmem()`이 이 영역을 포함한 init 메모리를 해제하고 "**Freeing unused kernel memory: 15168K**"를 찍는다. `__init`이 붙은 함수를 부팅 후에 부르면 해제된 메모리로 점프해서 커널 oops가 난다 — 드라이버 리뷰에서 자주 지적되는 버그다(`modpost`가 "section mismatch" 경고로 일부 잡아 준다).
- 커널 주소도 `CONFIG_RANDOMIZE_BASE=y`(KASLR)로 부팅마다 바뀐다. 유저 공간 ASLR의 커널판이다.

---

## 12. 커널 안에서는 무슨 일이 일어나나

### 12.1 `execve()` — 커널이 ELF를 올리는 길

Don이 `./app`을 실행하면 커널에서 이런 일이 일어난다(함수 이름은 `fs/exec.c`, `fs/binfmt_elf.c` 기준).

1. `execve()` → `do_execveat_common()` → 파일을 열고 첫 바이트들을 읽어 `struct linux_binprm`에 담는다.
2. 등록된 바이너리 형식 핸들러(`struct linux_binfmt`)를 차례로 시도한다: ELF(`load_elf_binary`), 스크립트(`#!`, `binfmt_script`), `binfmt_misc`(qemu 같은 사용자 등록 형식). 아무도 못 받으면 **ENOEXEC** → "Exec format error". §8.3에서 x86 바이너리가 qemu로 간 것은 `binfmt_misc`가 받았기 때문이다.
3. `load_elf_binary()`: ELF 헤더 검사(`e_machine`, class) → 프로그램 헤더 읽기 → **PT_INTERP가 있으면 그 경로를 `open_exec()`** (여기서 실패하면 **ENOENT** — §8.2의 정체) → PT_LOAD마다 `vm_mmap()`으로 매핑(권한은 `p_flags`에서), `.bss` 부분은 0 페이지 → PIE면 무작위 기준 주소 선택(ASLR).
4. `create_elf_tables()`: 새 스택에 argv, envp, 그리고 **auxv**(보조 벡터)를 쌓는다. auxv는 커널이 ld.so에게 주는 쪽지다.
5. 시작 PC를 **인터프리터(ld.so)의 엔트리**로 설정하고 유저 모드로 복귀. (정적 바이너리면 프로그램 자신의 `e_entry`.)

auxv는 `LD_SHOW_AUXV=1`로 볼 수 있다.

```sh
LD_SHOW_AUXV=1 ./hello_pie | grep -E "AT_PHDR|AT_ENTRY|AT_BASE|AT_PAGESZ|AT_EXECFN|AT_RANDOM|AT_SYSINFO_EHDR|hello"
```

```text
AT_SYSINFO_EHDR:      0xecd95ef02000
AT_PAGESZ:            4096
AT_PHDR:              0xbea714800040
AT_BASE:              0xecd95eec6000
AT_ENTRY:             0xbea7148006c0
AT_RANDOM:            0xfffffc4ad258
AT_EXECFN:            ./hello_pie
hello ELF 42 43
```

`AT_PHDR` = 로드 기준 `0xbea714800000` + 0x40(§2.2의 PHDR offset), `AT_ENTRY` = 기준 + 0x6c0(§1.3의 Entry point). ld.so는 커널이 이미 매핑해 둔 프로그램 헤더를 `AT_PHDR`로 찾고, 자기 위치를 `AT_BASE`로 알고, 다 끝나면 `AT_ENTRY`로 점프한다. `AT_SYSINFO_EHDR`는 vDSO(ELF 이미지 하나를 커널이 매핑해 준 것), `AT_RANDOM`은 스택 카나리 등에 쓰는 16바이트 난수 주소다.

### 12.2 예제 24 — 커널 모듈 `.ko`는 ET_REL이고, 링커는 커널이다 (E3 미리보기)

`.ko`는 **링크가 덜 끝난 `.o`**(ET_REL)다. 프로그램 헤더가 없고, 외부 심볼(`_printk`)은 `U`로 남아 있다. `insmod`가 파일을 `finit_module()` syscall로 넘기면 **커널 안의 모듈 로더가 ld.so 역할**을 한다: 섹션별로 메모리를 할당하고, 심볼을 커널 심볼 테이블에서 찾고, relocation을 적용하고, init을 부르고, `.init.*`를 버린다.

```c
// SPDX-License-Identifier: GPL-2.0
/* ex14: 가장 작은 커널 모듈 — .ko 가 ET_REL 이고 커널이 로드 시 링크한다는 것을 보는 재료 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>

static int b3_counter = 7;                       /* .data */

static int __init b3_elfmod_init(void)           /* .init.text — 로드 후 버려짐 */
{
	b3_counter++;
	pr_info("b3_elfmod: init, counter=%d, init fn at %px\n", b3_counter, b3_elfmod_init);
	return 0;
}

static void __exit b3_elfmod_exit(void)          /* .exit.text */
{
	pr_info("b3_elfmod: exit\n");
}

module_init(b3_elfmod_init);
module_exit(b3_elfmod_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("B3 ELF demo module");
```

```sh
# Kbuild 파일 한 줄: obj-m += b3_elfmod.o
make -C /lib/modules/$(uname -r)/build M=/tmp/B3/kmod modules
file b3_elfmod.ko
readelf -h b3_elfmod.ko | grep -E "Type|Machine"
readelf -lW b3_elfmod.ko
readelf -SW b3_elfmod.ko | grep -E " \.data | \.init\.text | \.exit\.text | \.modinfo|this_module|__versions"
readelf -rW b3_elfmod.ko | sed -n "/.rela.init.text/,/^\$/p"
readelf -sW b3_elfmod.ko | grep -E "b3_|_printk|__this_module|init_module|cleanup_module"
modinfo b3_elfmod.ko
```

```text
b3_elfmod.ko: ELF 64-bit LSB relocatable, ARM aarch64, version 1 (SYSV), BuildID[sha1]=2086993cd872e511ed4c897071a9788ed70fee51, with debug_info, not stripped
  Type:                              REL (Relocatable file)
  Machine:                           AArch64

There are no program headers in this file.
  [ 5] .data             PROGBITS        0000000000000000 000048 000004 00  WA  0   0  4
  [12] .init.text        PROGBITS        0000000000000000 000098 000068 00  AX  0   0  8
  [14] .exit.text        PROGBITS        0000000000000000 000100 000038 00  AX  0   0  8
  [20] .modinfo          PROGBITS        0000000000000000 000148 0000ab 00   A  0   0  1
  [37] .gnu.linkonce.this_module PROGBITS        0000000000000000 0174e0 000500 00  WA  0   0 64
  [38] .rela.gnu.linkonce.this_module RELA            0000000000000000 02b198 000030 18   I 44  37  8
  [41] __versions        PROGBITS        0000000000000000 017a00 000080 00   A  0   0  8
Relocation section '.rela.init.text' at offset 0x18310 contains 8 entries:
    Offset             Info             Type               Symbol's Value  Symbol's Name + Addend
0000000000000018  0000000400000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000000 .data + 0
0000000000000020  0000002500000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000008 b3_elfmod_init + 0
0000000000000024  0000002500000115 R_AARCH64_ADD_ABS_LO12_NC 0000000000000008 b3_elfmod_init + 0
000000000000002c  000000040000011d R_AARCH64_LDST32_ABS_LO12_NC 0000000000000000 .data + 0
0000000000000034  000000040000011d R_AARCH64_LDST32_ABS_LO12_NC 0000000000000000 .data + 0
0000000000000038  0000000500000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000000 .rodata + 0
000000000000003c  0000000500000115 R_AARCH64_ADD_ABS_LO12_NC 0000000000000000 .rodata + 0
0000000000000040  000000440000011b R_AARCH64_CALL26       0000000000000000 _printk + 0

    33: 0000000000000000     0 FILE    LOCAL  DEFAULT  ABS b3_elfmod.c
    37: 0000000000000008    96 FUNC    LOCAL  DEFAULT   12 b3_elfmod_init
    39: 0000000000000000    56 FUNC    LOCAL  DEFAULT   14 b3_elfmod_exit
    41: 0000000000000000     4 OBJECT  LOCAL  DEFAULT    5 b3_counter
    43: 0000000000000000     8 OBJECT  LOCAL  DEFAULT   16 __UNIQUE_ID_addressable_cleanup_module_416
    45: 0000000000000000     8 OBJECT  LOCAL  DEFAULT   18 __UNIQUE_ID_addressable_init_module_415
    49: 0000000000000000     0 FILE    LOCAL  DEFAULT  ABS b3_elfmod.mod.c
    65: 0000000000000000  1280 OBJECT  GLOBAL DEFAULT   37 __this_module
    66: 0000000000000000    56 FUNC    GLOBAL DEFAULT   14 cleanup_module
    67: 0000000000000008    96 FUNC    GLOBAL DEFAULT   12 init_module
    68: 0000000000000000     0 NOTYPE  GLOBAL DEFAULT  UND _printk
filename:       /tmp/B3/kmod/b3_elfmod.ko
description:    B3 ELF demo module
license:        GPL
srcversion:     7362E37811000426F1C216E
depends:        
name:           b3_elfmod
vermagic:       7.0.0-34-generic SMP preempt mod_unload modversions aarch64
```
(`readelf -SW`는 47개 섹션 중 grep에 걸린 줄만 보인다. 빌드 중 "the compiler differs from the one used to build the kernel", "pahole version differs" 경고 두 줄과 "Skipping BTF generation ... due to unavailability of vmlinux" 메시지가 나왔다 — 헤더 패키지만으로 빌드할 때의 정상적인 안내다.)

출력에서 볼 것:

- `REL (Relocatable file)`, **"There are no program headers"** — 커널 모듈에는 세그먼트가 없다. 커널 로더는 **섹션 단위**로 메모리를 배치한다. 유저 공간 로더(세그먼트 단위)와 반대다.
- `.rela.init.text`의 relocation 종류(`ADR_PREL_PG_HI21`, `ADD_ABS_LO12_NC`, `CALL26`)가 §2.3의 `hello.o`와 **똑같다**. `_printk`에 대한 `CALL26`은 커널이 로드 시 `_printk`의 실제 주소로 채운다. 모듈과 커널 이미지가 ±128MB 밖에 떨어지면 `bl`이 닿지 않으므로 arm64 모듈 로더는 PLT(veneer)를 만든다 — `.plt`, `.init.plt` 섹션이 그 자리다.
- `UND _printk`: 모듈이 커널에서 가져오는 심볼. 커널은 이것을 `EXPORT_SYMBOL`로 내보낸 심볼 테이블(`__ksymtab`)에서 찾는다. `__versions`는 그 심볼들의 CRC(modversions)로, 커널과 ABI가 맞는지 검사한다 — 유저 공간 심볼 버전(`GLIBC_2.17`)의 커널판이다.
- `.gnu.linkonce.this_module`의 `__this_module`(1280바이트)은 `struct module` 초기값이고, `init_module`/`cleanup_module` 별칭이 `module_init()` 매크로로 만들어졌다. `.modinfo`는 `modinfo`가 읽는 `key=value` 문자열들, `vermagic`은 "이 커널 버전·설정에서만 로드하라"는 꼬리표다.

이제 실제로 로드해서 커널이 각 섹션을 어디에 놓았는지 본다(모듈 이름 `b3_elfmod`, 확인 후 바로 언로드).

```sh
sudo insmod b3_elfmod.ko && sudo dmesg | grep b3_elfmod | tail -1
cd /sys/module/b3_elfmod/sections && for s in .init.text .exit.text .data .rodata .gnu.linkonce.this_module; do printf "%-26s %s\n" $s "$(sudo cat $s)"; done
ls -A | tr "\n" " "; echo
cat /sys/module/b3_elfmod/taint
cd /; sudo rmmod b3_elfmod && sudo dmesg | grep b3_elfmod | tail -1; lsmod | grep -c b3_
```

```text
[148469.166523] b3_elfmod: init, counter=8, init fn at ffffbcf0bcf7e030
.init.text                 0xffffbcf0bcf7e028
.exit.text                 0xffffbcf0bcf5d028
.data                      0xffffbcf0bcf6f008
.rodata                    0xffffbcf0bcf71000
.gnu.linkonce.this_module  0xffffbcf0bcf6f040
.data .exit.data .exit.text .gnu.linkonce.this_module .init.data .init.plt .init.text .init.text.ftrace_trampoline .note.Linux .note.gnu.build-id .note.gnu.property .plt .rodata .strtab .symtab .text.ftrace_trampoline __patchable_function_entries 
OE
[148469.199784] b3_elfmod: exit
0
```

출력에서 볼 것:

- `counter=8`: `.data`의 7이 `++`되었다. relocation이 제대로 `.data`의 실제 주소로 채워졌다는 증거다.
- `init fn at ...e030` = `.init.text` 시작 `...e028` + 8. 심볼 테이블의 `b3_elfmod_init` 값 `0x8`과 정확히 맞는다(앞 8바이트는 ftrace용 `__patchable_function_entries` 자리).
- `.init.text`(…7e…)와 `.data`(…6f…), `.exit.text`(…5d…)가 서로 다른 페이지에 흩어져 있다. 최근 커널(6.4 이후)은 모듈 메모리를 text/data/rodata/init 종류별로 따로 할당한다(`mod_mem`). init 종류는 `do_init_module()`이 init 함수를 부른 직후 해제된다. sysfs에 주소가 남아 있어도 그 메모리는 이미 반납된 것이다.
- sysfs의 섹션 파일 이름이 `.`으로 시작해서 `ls`만 하면 안 보인다(`ls -A` 필요). 이 디렉터리는 gdb에서 `add-symbol-file b3_elfmod.ko -s .text <addr> ...`로 모듈 심볼을 올릴 때 쓰인다(M 모듈).
- `taint = OE`: O(out-of-tree 모듈), E(서명 안 된 모듈). 실제 제품은 모듈 서명(`CONFIG_MODULE_SIG`)을 켜고 서명된 것만 받는다(L 모듈의 secure boot).
- 마지막 `0`: `lsmod`에 b3 모듈이 남아 있지 않다. 정리 완료.

```svg
<svg viewBox="0 0 680 370" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="120" y="24" text-anchor="middle" font-size="13" font-weight="bold">b3_elfmod.ko (ET_REL)</text>
  <rect x="20" y="36" width="200" height="262" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="32" y="60" font-size="12">.text / .exit.text</text>
  <text x="32" y="86" font-size="12">.init.text</text>
  <text x="32" y="112" font-size="12">.data / .rodata</text>
  <text x="32" y="138" font-size="12">.modinfo (vermagic)</text>
  <text x="32" y="164" font-size="12">__versions (CRC)</text>
  <text x="32" y="190" font-size="12">.gnu.linkonce.this_module</text>
  <text x="32" y="216" font-size="12">.rela.init.text ...</text>
  <text x="32" y="242" font-size="12">.symtab (UND _printk)</text>
  <text x="32" y="268" font-size="12">program header 없음</text>
  <text x="365" y="24" text-anchor="middle" font-size="13" font-weight="bold">커널 모듈 로더</text>
  <rect x="255" y="36" width="220" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="365" y="58" text-anchor="middle" font-size="12">finit_module(fd) → load_module</text>
  <rect x="255" y="80" width="220" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="365" y="102" text-anchor="middle" font-size="12">vermagic·서명 검사</text>
  <rect x="255" y="124" width="220" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="365" y="146" text-anchor="middle" font-size="12">섹션별 메모리 배치 (mod_mem)</text>
  <rect x="255" y="168" width="220" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="365" y="190" text-anchor="middle" font-size="12">simplify_symbols: UND → 주소</text>
  <rect x="255" y="212" width="220" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="365" y="234" text-anchor="middle" font-size="12">apply_relocate_add (arm64)</text>
  <rect x="255" y="256" width="220" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="365" y="278" text-anchor="middle" font-size="12">do_init_module → init() 호출</text>
  <line x1="220" y1="160" x2="253" y2="160" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="365" y1="70" x2="365" y2="78" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="365" y1="114" x2="365" y2="122" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="365" y1="158" x2="365" y2="166" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="365" y1="202" x2="365" y2="210" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="365" y1="246" x2="365" y2="254" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="580" y="24" text-anchor="middle" font-size="13" font-weight="bold">커널 메모리</text>
  <rect x="500" y="36" width="160" height="70" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="580" y="62" text-anchor="middle" font-size="12">core 영역 (남는다)</text>
  <text x="580" y="84" text-anchor="middle" font-size="12">.text .data .rodata</text>
  <rect x="500" y="124" width="160" height="70" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-dasharray="5 3"/>
  <text x="580" y="150" text-anchor="middle" font-size="12">init 영역</text>
  <text x="580" y="172" text-anchor="middle" font-size="12">.init.text .init.data</text>
  <rect x="500" y="212" width="160" height="56" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="580" y="236" text-anchor="middle" font-size="12">__ksymtab</text>
  <text x="580" y="256" text-anchor="middle" font-size="12">(EXPORT_SYMBOL)</text>
  <line x1="475" y1="141" x2="498" y2="90" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="475" y1="141" x2="498" y2="160" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="498" y1="235" x2="477" y2="190" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="580" y="290" text-anchor="middle" font-size="12">init() 성공 후 해제</text>
  <text x="20" y="330" font-size="12">유저 공간의 ld.so 가 하는 일(배치·심볼 해석·재배치·init 호출)을 커널이 직접 한다</text>
  <text x="20" y="352" font-size="12">vmlinux 도 같다: .init.text 는 부팅 끝에 해제 → "Freeing unused kernel memory"</text>
</svg>
```
그림 7 — 커널 모듈 로드. `.ko`(ET_REL)를 받은 커널 모듈 로더가 ld.so와 같은 일 — 메모리 배치, 심볼 해석, relocation, init 호출 — 을 커널 안에서 한다. 함수 이름은 `kernel/module/main.c`, `arch/arm64/kernel/module.c` 기준.

### 12.3 드라이버를 쓸 때 다시 만나는 지점

| 이 노트의 개념 | 드라이버·커널에서 다시 나오는 곳 | 모듈 |
|---|---|---|
| ET_REL + 로드 시 링크 | `insmod`/`modprobe`, `finit_module`, `depmod`가 만든 의존성 | E3 |
| 심볼 가시성 / `.dynsym` | `EXPORT_SYMBOL` vs `EXPORT_SYMBOL_GPL`, 모듈 밖에서 안 보이는 `static` | E3, E4 |
| 심볼 버전 (`GLIBC_2.17`) | modversions의 `__versions` CRC, `vermagic` | E3 |
| `.init_array` / 예제 23 | `module_init`, `device_initcall`, initcall 레벨, `__init`/`__initdata` 해제 | C6, G1 |
| section mismatch | `__init` 함수를 `__init`이 아닌 코드에서 참조 → modpost 경고 | E2 |
| addr2line / Build ID | oops의 `pc : my_probe+0x48/0x120 [mydrv]` → `faddr2line`, `gdb mydrv.ko` | M3, M4 |
| 플러그인 구조체 (예제 17) | `struct file_operations`, `struct platform_driver` 등록 | G2, F 모듈 |
| ASLR | KASLR (`CONFIG_RANDOMIZE_BASE`), `%px` vs `%pK` 포인터 출력 | E, L |

---

## 13. Embedded 실무에서 다시 보기

### 13.1 장면 1 — 벤더 SDK로 빌드한 앱이 보드에서 "not found"

센서 키트 보드(Buildroot, musl)에 Ubuntu 호스트의 `aarch64-linux-gnu-gcc`(glibc용)로 빌드한 `sensord`를 `scp`로 올렸다. 보드의 busybox가 `-sh: ./sensord: not found`. §8.2 그대로다.

```sh
# 호스트에서
file sensord                     # interpreter /lib/ld-linux-aarch64.so.1 확인
# 보드에서
ls /lib/ld-*                     # ld-musl-aarch64.so.1 만 있다
```

해결책 우선순위: (1) **보드와 같은 툴체인**(Buildroot가 만든 SDK, B5)으로 다시 빌드한다. (2) 급하면 `-static`으로 빌드한다. (3) `patchelf --set-interpreter`로 우회하는 것은 libc가 다르면 §8.2의 musl 예처럼 다음 에러로 넘어갈 뿐이다.

### 13.2 장면 2 — 제품 앱 디렉터리를 재배치 가능하게

`/opt/sensorkit/{bin,lib,plugins}` 구조로 배포하고, OTA 업데이트(L 모듈)에서 A/B 디렉터리를 바꿔 끼운다. 이때 `LD_LIBRARY_PATH`를 systemd 유닛에 박는 대신 바이너리에 `-Wl,-rpath,'$ORIGIN/../lib'`을 넣으면 **디렉터리를 어디로 옮겨도** 동작한다(예제 8). 플러그인은 `dlopen`에 `"$ORIGIN"` 대신 실행 파일 경로(`/proc/self/exe`의 `readlink`)로 절대 경로를 만들어 준다(예제 17의 "슬래시 없으면 탐색" 함정).

### 13.3 장면 3 — 필드 크래시 로그를 소스 줄로

보드에서 `sensord`가 죽으면서 남긴 로그:

```text
SIGSEGV addr=(nil) pc=0xb84234770cf0  ./crash+0xcf0
```

빌드 서버에 Build ID별로 보관한 debug 파일로 `addr2line -f -i -e <buildid>.debug 0xcf0` → `parse_sample crash.c:26`(예제 21~22). **보드에 올리는 바이너리는 stripped, 서버에는 `.debug` + Build ID** 가 팀 규칙이 되어야 한다. 크래시 핸들러는 Build ID도 같이 찍도록 만들자(`dl_iterate_phdr`로 `PT_NOTE`를 읽으면 된다).

### 13.4 장면 4 — 하드웨어 없이 데몬 테스트

CI 서버에는 IMU가 없다. LD_PRELOAD shim이 `open("/dev/i2c-1")`과 `ioctl(I2C_RDWR)`을 가로채 녹화 데이터를 돌려주면, 실제 데몬 바이너리를 **수정 없이** CI에서 돌릴 수 있다(예제 16). 커널 쪽에서 같은 목적으로는 `i2c-stub` 같은 가짜 장치 드라이버를 쓴다(H 모듈).

---

## 14. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 호스트(glibc) 툴체인으로 빌드해 musl 보드에 올림 | `./app: not found` (파일은 있음) | PT_INTERP 로더가 보드에 없음 | 보드용 SDK로 재빌드, 또는 `-static`. `readelf -l`로 interpreter 확인 |
| `-l` 순서를 거꾸로 씀 | `undefined reference to ...` (빌드) | GNU ld는 아카이브를 한 번만 훑음 | 사용하는 쪽을 왼쪽에, 상호 의존이면 `--start-group` |
| 라이브러리를 `/usr/local/lib`에 넣고 끝 | `cannot open shared object file` | ld.so.cache에 없음, 또는 rootfs에 캐시 자체가 없음 | `ldconfig` 실행, 또는 rpath `$ORIGIN` 사용 |
| `LD_LIBRARY_PATH`로 디버그 라이브러리를 줬는데 안 바뀜 | 여전히 옛 라이브러리 사용 | 바이너리에 RPATH(구식)가 있어 LD_LIBRARY_PATH보다 우선 | `readelf -d`로 RPATH 확인, RUNPATH로 다시 링크 또는 `patchelf --set-rpath` |
| 라이브러리 함수 추가 후 앱만 배포 | `symbol lookup error: undefined symbol` | 보드의 `.so`가 옛 버전 (soname은 같음) | 라이브러리와 앱을 함께 배포, 심볼 버전(version script) 도입 |
| lazy binding 바이너리를 출시 | 드문 경로에서만 몇 주 뒤 크래시 | 첫 호출 때까지 심볼 해석이 미뤄짐 | `-Wl,-z,now`, 테스트에서 `LD_BIND_NOW=1` |
| `-fvisibility=hidden` 없이 `.so` 배포 | 다른 라이브러리와 같은 이름 함수 충돌, 느린 내부 호출 | 모든 extern 함수가 export되어 interposition 대상 | hidden 기본 + 공개 API만 `visibility("default")` |
| 크래시 PC를 그대로 `addr2line`에 넣음 | `??:0` 또는 엉뚱한 함수 | PIE/`.so`는 로드 기준 주소를 빼야 함, 또는 다른 빌드의 ELF | `/proc/<pid>/maps`나 `dladdr`로 기준 주소 빼기, Build ID 대조 |
| 링커 스크립트로 쓰기 가능한 섹션을 `.rodata` 옆에 넣음 | `LOAD segment with RWX permissions` 경고 | R E 세그먼트와 RW 섹션이 합쳐짐 | `INSERT AFTER .data` 등 RW 영역에 배치 |
| `__init` 함수를 부팅 후 경로에서 호출 | 커널 oops (해제된 메모리 실행) | `.init.text`는 부팅/모듈 init 후 해제 | `__init` 제거, modpost의 section mismatch 경고를 에러로 취급 |

---

## 15. 면접에서 이렇게 말한다

**Q.** "What's the difference between sections and segments in an ELF file?"

**A.** 섹션은 링커와 디버거를 위한 세밀한 단위(`.text`, `.data`, `.symtab`)이고, 세그먼트는 로더를 위한 단위로 "파일의 이 범위를 이 권한으로 이 주소에 매핑하라"는 지시다. 권한이 같은 섹션들이 하나의 PT_LOAD로 묶인다. 커널은 프로그램 헤더만 보고, 섹션 헤더를 지워도 실행된다.

> Sections are the linker's and debugger's view — fine-grained units like .text, .data, .symtab, described by the section header table. Segments are the loader's view — program headers saying "map this file range at this address with these permissions." Sections with the same permissions are grouped into one PT_LOAD. The kernel only reads program headers; you can strip the section headers entirely and the binary still runs.

**꼬리 질문**: "Where does .bss live in the file?" → 파일에는 공간이 없고 PT_LOAD의 MemSiz > FileSiz 차이로 표현되며, 커널이 0 페이지로 채운다.

**Q.** "Explain PLT and GOT. How does lazy binding work?"

**A.** 코드는 읽기 전용이라 외부 함수 주소를 직접 고칠 수 없으니, 외부 호출은 PLT 스텁을 거쳐 GOT라는 쓰기 가능한 주소 테이블에서 목적지를 읽는다. lazy binding에서는 GOT 슬롯이 처음에 해석기 경로(PLT0)를 가리키고, 첫 호출 때 ld.so가 심볼을 찾아 GOT에 써 넣는다. 이후 호출은 PLT → GOT → 함수로 바로 간다.

> Code is read-only and shared, so calls to external functions go through a small PLT stub that loads the target from the GOT, a writable per-process table of addresses. With lazy binding, each GOT slot initially points back into PLT0, which jumps to the dynamic linker's resolver; on the first call it looks up the symbol, patches the GOT slot, and jumps to the real function. Later calls go PLT → GOT → function directly. With -z now, all of that happens before main and the GOT can be made read-only.

**꼬리 질문**: "How would you demonstrate it?" → gdb에서 첫 호출 전후의 GOT 슬롯 값 비교, 또는 `LD_DEBUG=bindings`.

**Q.** "What is RELRO, and what's the difference between partial and full RELRO?"

**A.** 재배치가 끝난 뒤 바뀔 필요가 없는 영역(.got, .dynamic, .init_array)을 ld.so가 mprotect로 읽기 전용으로 바꾸는 것이다. partial RELRO는 lazy binding을 위해 함수용 GOT(.got.plt)를 쓰기 가능하게 남겨서 GOT overwrite 공격이 여전히 가능하고, full RELRO(`-z relro -z now`)는 모든 심볼을 시작 시 해석한 뒤 GOT 전체를 잠근다.

> RELRO makes relocation-only data — .got, .dynamic, .init_array — read-only after the dynamic linker finishes relocating. Partial RELRO leaves the function GOT writable so lazy binding can patch it, which still allows GOT-overwrite exploits. Full RELRO, -z relro -z now, resolves everything at startup and then mprotects the whole GOT read-only. I measured the cost: about 50 nanoseconds per imported symbol at startup, so for typical embedded daemons it's negligible.

**Q.** "A binary you cross-compiled says 'No such file or directory' on the target, but the file is clearly there. What's going on?"

**A.** 동적 링크된 ELF에는 PT_INTERP로 로더 경로가 박혀 있고, 커널이 execve 때 그 로더를 연다. 로더가 없으면 execve가 ENOENT를 돌려주고 셸은 "not found"라고만 한다. readelf -l로 interpreter를 확인하고, 보드의 libc와 맞는 툴체인으로 다시 빌드하거나 정적으로 빌드한다.

> A dynamically linked ELF has a PT_INTERP header naming its dynamic loader, like /lib/ld-linux-aarch64.so.1. The kernel opens that path during execve; if it's missing — typical when a glibc-built binary lands on a musl or differently-laid-out rootfs — execve returns ENOENT and busybox just prints "not found." I check readelf -l for the interpreter and ls the target's /lib. The fix is building with the target's SDK, or statically linking for a quick test.

**꼬리 질문**: "And 'Exec format error'?" → 커널의 binfmt 핸들러가 형식을 거절한 것(다른 아키텍처·32/64비트). `file`로 확인.

**Q.** "How does the dynamic linker find shared libraries? What's the difference between RPATH and RUNPATH?"

**A.** 순서는 RPATH(RUNPATH가 없을 때만) → LD_LIBRARY_PATH → RUNPATH → ld.so.cache → 기본 경로다. RPATH는 LD_LIBRARY_PATH보다 우선해서 덮어쓸 수 없고, RUNPATH는 나중이라 LD_LIBRARY_PATH로 바꿀 수 있다. 앱 번들에는 `$ORIGIN` 기반 RUNPATH를 쓴다.

> Search order is DT_RPATH if there's no DT_RUNPATH, then LD_LIBRARY_PATH, then DT_RUNPATH, then /etc/ld.so.cache, then the default directories. RPATH beats LD_LIBRARY_PATH so you can't override it, while RUNPATH comes after, so you can — modern ld emits RUNPATH by default. For relocatable app bundles I use -rpath '$ORIGIN/../lib'. To debug it I run LD_DEBUG=libs, which prints every path tried.

**Q.** "Static vs dynamic linking — which would you choose for an embedded product?"

**A.** 바이너리가 많은 rootfs라면 동적이 libc 하나를 공유해 전체 크기와 보안 패치 면에서 유리하다. initramfs 도구, 복구 이미지, 로더 의존을 없애야 하는 작은 데몬은 정적이 낫고, 그때는 glibc보다 musl이 훨씬 작다. 측정해 보면 glibc 정적 hello가 700KB, musl 정적이 80KB였다.

> For a rootfs with many binaries, dynamic linking wins: one copy of libc, smaller total image, and you patch a library CVE once. Static makes sense for initramfs tools, recovery images, or a single critical daemon where you want zero loader dependencies — and then musl, not glibc: a static hello was about 700 KB with glibc and 80 KB with musl when I measured it. I also check page-size alignment, since aarch64 defaults to 64K and that dominates tiny binaries.

**Q.** "How is a kernel module loaded? How is it different from loading a shared library?"

**A.** .ko는 ET_REL, 즉 프로그램 헤더 없는 재배치 가능 오브젝트다. finit_module로 넘기면 커널 모듈 로더가 vermagic과 서명을 검사하고, 섹션별로 메모리를 잡고, UND 심볼을 EXPORT_SYMBOL 테이블에서 찾아, arch별 apply_relocate_add로 재배치를 적용한 뒤 init을 부르고 .init 섹션을 해제한다. ld.so가 하는 일을 커널이 섹션 단위로 하는 것이다.

> A .ko is an ET_REL object — no program headers, unresolved symbols like _printk. insmod passes it to finit_module; the kernel checks vermagic and the signature, lays out sections into module memory, resolves undefined symbols against exported kernel symbols, applies relocations with the arch's apply_relocate_add, calls the init function, and frees the .init sections. It's the same job ld.so does for a shared library, but done in the kernel at section granularity, and symbol visibility is controlled by EXPORT_SYMBOL instead of .dynsym.

**Q.** "You get a crash address from the field. How do you get to a source line?"

**A.** PIE와 .so는 로드 위치가 매번 다르니, 그 모듈의 로드 기준 주소를 빼서 파일 오프셋을 만든다. 그 크래시를 낸 빌드와 Build ID가 같은 debug 파일로 `addr2line -f -i -e`를 돌린다. 그래서 보드에는 stripped 바이너리를, 서버에는 Build ID로 색인한 .debug를 보관한다.

> First turn the PC into a module-relative offset by subtracting the load base from /proc/pid/maps or dladdr — with PIE and ASLR the absolute address changes every run but the offset doesn't. Then run addr2line -f -i against the debug info from exactly that build, matched by GNU build ID. Our process ships stripped binaries and stores the objcopy --only-keep-debug output indexed by build ID, which gdb and debuginfod understand natively.

---

## 16. 직접 해보기

1. **(개념)** `hello_pie`의 RW LOAD 세그먼트에서 `FileSiz`와 `MemSiz`가 다른 이유를 설명하고, 그 차이 바이트 수가 `readelf -S`의 어느 섹션 크기와 같은지 찾아라.
   정답/힌트: 차이 0xc = `.bss` 크기 0xc. 커널이 파일 끝 이후를 0으로 채운다.

2. **(코드)** 예제 7의 `libimu`에 version script(`-Wl,--version-script=imu.map`, `LIBIMU_1.0 { global: imu_read_accel; imu_version; local: *; };`, 1.3에서 `LIBIMU_1.3 { global: imu_read_gyro; } LIBIMU_1.0;`)를 적용해서, 옛 라이브러리에서 실행할 때 에러가 `undefined symbol`에서 `version 'LIBIMU_1.3' not found`로 바뀌는지 확인하라.
   정답/힌트: `objdump -T app`에 `LIBIMU_1.3` 태그가 보여야 한다. 시작 시점(main 전)에 실패하는 것도 확인.

3. **(관찰)** `LD_DEBUG=libs,bindings`와 `strace -e trace=openat,mmap`를 같은 프로그램에 각각 걸어, ld.so가 `libc.so.6`을 찾고 `mmap`하는 과정을 두 관점에서 대조하라. `/proc/<pid>/maps`에서 libc가 몇 개의 매핑으로 나뉘는지도 세어 보라.
   정답/힌트: strace에서 `openat(... "/etc/ld.so.cache")` → `openat(... "libc.so.6")` → `mmap` 여러 번 → `mprotect`(RELRO)가 순서대로 보인다.

4. **(코드)** 예제 16의 shim을 `malloc`/`free` 로깅 버전으로 바꿔라. `dlsym` 재귀 문제를 피하기 위해 glibc의 `__libc_malloc`/`__libc_free`를 쓰는 방식과, 첫 호출 동안 정적 버퍼에서 할당하는 방식 중 하나를 골라 구현하라.
   정답/힌트: `extern void *__libc_malloc(size_t);`를 선언해 직접 부르면 dlsym이 필요 없다. 출력은 `write(2, ...)`로.

5. **(관찰)** 예제 24의 모듈에 `EXPORT_SYMBOL(b3_counter_get)`로 함수를 하나 내보내고, 두 번째 모듈(`b3_user`)에서 그것을 호출하라. 로드 순서를 바꿔(`b3_user` 먼저) 어떤 에러가 나는지, `/proc/kallsyms`와 `Module.symvers`에 무엇이 생기는지 보라. 모듈 이름은 꼭 `b3_`로 시작하고 끝나면 `rmmod`한다.
   정답/힌트: 먼저 로드하면 `Unknown symbol b3_counter_get`(dmesg). `KBUILD_EXTRA_SYMBOLS`로 첫 모듈의 `Module.symvers`를 알려 줘야 modpost 경고도 사라진다.

6. **(개념)** 예제 23에서 `KEEP()`을 빼고 `-Wl,--gc-sections`로 링크하면 어떤 일이 생길지 예측하고 실행해서 확인하라. 커널 `vmlinux.lds.h`가 initcall에 `KEEP`을 쓰는 이유와 연결하라.
   정답/힌트: 아무도 참조하지 않는 섹션이라 gc 대상이 되어 `0 initcalls`가 될 수 있다. 커널은 `CONFIG_LD_DEAD_CODE_DATA_ELIMINATION`에서도 initcall을 살려야 한다.

7. **(측정)** 예제 14의 벤치마크를 `-fvisibility=hidden`으로 빌드한 `libmany.so`와 기본 빌드로 비교하라(앱이 쓰는 함수는 default로 열어 둔다). 동적 심볼 수와 시작 시간이 어떻게 달라지나?
   정답/힌트: `nm -D --defined-only | wc -l`로 export 수부터 비교. 앱이 참조하는 심볼 수가 같으면 now 비용은 거의 같고, 라이브러리 자체의 내부 relocation이 줄어든다.

---

## 17. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| ELF | Executable and Linkable Format | `.o`, 실행 파일, `.so`, `.ko`, 코어 덤프가 모두 쓰는 바이너리 형식 |
| ET_REL / ET_EXEC / ET_DYN | ELF 타입 | 재배치 가능 오브젝트 / 고정 주소 실행 파일 / 공유 오브젝트(PIE 포함) |
| section | 섹션 | 링커·디버거용 단위. `readelf -S` |
| segment (program header) | 세그먼트 | 로더용 단위. PT_LOAD, PT_INTERP, PT_DYNAMIC, PT_GNU_RELRO 등. `readelf -l` |
| relocation | 재배치 | "이 자리를 나중에 이 심볼 주소로 채워라"는 기록. `.rela.*` |
| PIC / PIE | 위치 독립 코드 / 실행 파일 | 어느 주소에 올려도 동작. ASLR의 전제 |
| ASLR / KASLR | 주소 공간 무작위화 | 실행(부팅)마다 기준 주소를 바꾸는 보안 기능 |
| PT_INTERP | 인터프리터 경로 | 커널이 함께 로드할 동적 로더 경로. 없으면 ENOENT |
| ld.so | 동적 로더/링커 | `ld-linux-aarch64.so.1`. 라이브러리 로드, 재배치, 심볼 해석 |
| DT_NEEDED | 의존 라이브러리 | `.dynamic`에 적힌 필요한 soname 목록 |
| soname | 공유 라이브러리 ABI 이름 | `libimu.so.1`. major 호환 단위 |
| RPATH / RUNPATH | 바이너리 내장 탐색 경로 | RPATH는 LD_LIBRARY_PATH보다 먼저, RUNPATH는 나중 |
| `$ORIGIN` | 실행 파일 디렉터리 | rpath에서 쓰는 치환 토큰 |
| ld.so.cache | 라이브러리 색인 | `ldconfig`가 만드는 soname → 경로 캐시 |
| PLT | Procedure Linkage Table | 외부 함수 호출용 코드 스텁 |
| GOT | Global Offset Table | 외부 심볼의 실제 주소를 담는 쓰기 가능 테이블 |
| lazy binding | 지연 바인딩 | 첫 호출 때 심볼 해석. `-z lazy` |
| BIND_NOW | 즉시 바인딩 | 시작 시 전부 해석. `-z now`, `LD_BIND_NOW=1` |
| RELRO | Relocation Read-Only | 재배치 후 GOT 등을 읽기 전용으로. partial / full |
| visibility | 심볼 가시성 | default(export) / hidden(모듈 내부). `-fvisibility=hidden` |
| interposition | 심볼 끼어들기 | 먼저 로드된 모듈의 같은 이름 심볼이 이기는 규칙. LD_PRELOAD의 원리 |
| `RTLD_NEXT` | 다음 정의 | `dlsym`에서 "나 다음 순서의 같은 심볼"을 찾는 핸들 |
| Build ID | 빌드 지문 | `.note.gnu.build-id`. 바이너리와 debug 파일 매칭 |
| gnu_debuglink | debug 파일 링크 | stripped 바이너리에 적힌 debug 파일 이름 + CRC |
| auxv | 보조 벡터 | 커널이 스택에 넣어 ld.so에게 주는 정보(AT_PHDR, AT_ENTRY, AT_BASE…) |
| binfmt_misc | 사용자 정의 실행 형식 | 매직 바이트로 qemu 등 인터프리터를 지정하는 커널 기능 |
| initcall | 커널 초기화 함수 등록 | 레벨별 섹션에 함수 포인터를 넣어 부팅 때 순서대로 호출 |
| modversions | 모듈 심볼 CRC | `.ko`의 `__versions`. 커널 ABI 일치 검사 |

---

## 18. 요약 & 체크리스트

Linux에서 ELF는 펌웨어의 `.elf`와 같은 형식이지만, 링크가 빌드 시간(`ld`)과 실행 시간(ld.so) 두 번에 걸쳐 일어나고 주소는 로드할 때(PIE+ASLR) 정해진다는 점이 다르다. 커널은 프로그램 헤더(세그먼트)만 보고 PT_LOAD를 mmap하고 PT_INTERP의 로더를 함께 올린다 — 로더가 없으면 "not found"다. ld.so는 DT_NEEDED의 soname을 RPATH → LD_LIBRARY_PATH → RUNPATH → ld.so.cache → 기본 경로 순으로 찾고, PLT/GOT를 통해 외부 함수를 연결한다. Ubuntu 기본은 `-z now` + RELRO(full RELRO)이고, 그 비용은 측정상 심볼당 약 50ns다. 정적 링크는 GNU ld가 아카이브를 한 번만 훑으므로 순서가 중요하다. 크래시 분석은 오프셋 + Build ID + 분리된 debug 파일이 핵심이다. 마지막으로 커널 모듈 `.ko`는 ET_REL이고, 커널이 ld.so의 일을 섹션 단위로 직접 한다 — 이 지식이 E3(모듈)과 C6(initcall)로 이어진다.

- [ ] ET_REL / ET_EXEC / ET_DYN의 차이와 PIE가 ET_DYN인 이유를 설명할 수 있다
- [ ] `readelf -S`와 `readelf -l` 출력을 보고 어떤 섹션이 어떤 세그먼트에 들어가는지, 무엇이 로드되지 않는지 말할 수 있다
- [ ] `.o`의 relocation을 `objdump -dr`로 보고 무엇이 링크 때 채워지는지 설명할 수 있다
- [ ] GNU ld의 아카이브 순서 규칙과 `--start-group`을 설명하고 `-Wl,-Map`으로 멤버가 들어온 이유를 추적할 수 있다
- [ ] soname과 `libfoo.so → .so.1 → .so.1.2.3` 체인을 만들고 각 이름을 누가 쓰는지 말할 수 있다
- [ ] 라이브러리 탐색 순서를 외우고, RPATH와 RUNPATH의 차이를 `LD_DEBUG=libs`로 보여 줄 수 있다
- [ ] PLT/GOT lazy binding을 그림으로 그리고 gdb 또는 `LD_DEBUG=bindings`로 시연할 수 있다
- [ ] partial RELRO와 full RELRO의 차이, `-z now`의 장단점을 측정값과 함께 말할 수 있다
- [ ] `-fvisibility=hidden`, LD_PRELOAD + `RTLD_NEXT`, dlopen 플러그인을 코드로 짤 수 있다
- [ ] "not found", "Exec format error", "cannot open shared object file", "undefined symbol"을 각각 무엇으로 진단하는지 안다
- [ ] 크래시 PC를 오프셋으로 바꾸고 분리된 debug 파일 + Build ID로 `addr2line`을 할 수 있다
- [ ] `.ko`가 ET_REL이고 커널이 어떻게 링크·init·init 영역 해제를 하는지, initcall이 링커 스크립트로 어떻게 모이는지 설명할 수 있다

---

## 참고 자료

- man7.org — [ld.so(8)](https://man7.org/linux/man-pages/man8/ld.so.8.html): 탐색 순서, `LD_LIBRARY_PATH`, `LD_PRELOAD`, `LD_BIND_NOW`, `LD_DEBUG`, `LD_SHOW_AUXV`
- man7.org — [elf(5)](https://man7.org/linux/man-pages/man5/elf.5.html), [ldconfig(8)](https://man7.org/linux/man-pages/man8/ldconfig.8.html), [dlopen(3)](https://man7.org/linux/man-pages/man3/dlopen.3.html), [dlsym(3)](https://man7.org/linux/man-pages/man3/dlsym.3.html), [dl_iterate_phdr(3)](https://man7.org/linux/man-pages/man3/dl_iterate_phdr.3.html), [getauxval(3)](https://man7.org/linux/man-pages/man3/getauxval.3.html), [execve(2)](https://man7.org/linux/man-pages/man2/execve.2.html)
- Michael Kerrisk, "The Linux Programming Interface" — 41장 Fundamentals of Shared Libraries, 42장 Advanced Features of Shared Libraries
- Ulrich Drepper, "How To Write Shared Libraries" — https://www.akkadia.org/drepper/dsohowto.pdf (가시성, 재배치 비용, 심볼 버전)
- John R. Levine, "Linkers and Loaders" (Morgan Kaufmann, 1999)
- GNU ld 매뉴얼 — https://sourceware.org/binutils/docs/ld/ (Scripts, `INSERT`, `KEEP`, `-z` 옵션)
- docs.kernel.org — [Building External Modules](https://docs.kernel.org/kbuild/modules.html), [Kernel module signing facility](https://docs.kernel.org/admin-guide/module-signing.html)
- 커널 소스(elixir.bootlin.com) — `fs/binfmt_elf.c`(`load_elf_binary`), `fs/exec.c`, `kernel/module/main.c`(`load_module`, `do_init_module`), `arch/arm64/kernel/module.c`(`apply_relocate_add`), `include/asm-generic/vmlinux.lds.h`(`INIT_CALLS`), `include/linux/init.h`
- Bootlin, "Embedded Linux system development" 교육 자료 — 툴체인·C 라이브러리 장
- ARM, "ELF for the Arm 64-bit Architecture (AAELF64)" — aarch64 relocation 타입 정의 (github.com/ARM-software/abi-aa)
