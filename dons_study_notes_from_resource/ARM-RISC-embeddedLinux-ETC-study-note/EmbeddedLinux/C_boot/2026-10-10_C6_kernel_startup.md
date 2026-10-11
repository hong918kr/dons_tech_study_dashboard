# C6. 커널 시작 과정 — Image에서 start_kernel, initcall, rootfs 마운트까지

> **이 노트를 다 읽으면**: arm64 `Image`의 64바이트 헤더와 부트로더 계약(x0 = DTB, MMU off)을 hexdump·gdb로 직접 보여 줄 수 있다 · `head.S` → `start_kernel` → `rest_init` → `kernel_init` → initcall → rootfs 마운트 → `/init` 경로를 실제 함수 이름으로 설명할 수 있다 · earlycon과 console의 차이, `early_param`/`__setup`, initcall 레벨을 알고 `initcall_debug`로 느린 initcall을 찾을 수 있다 · "VFS: Unable to mount root fs", "No working init found", "콘솔에 아무것도 안 나와요"를 QEMU에서 재현하고 고칠 수 있다
> **왜 필요한가**: bring-up 첫 주의 단골 장면이 "U-Boot에서 `Starting kernel ...` 다음에 아무것도 안 나온다"와 "커널은 뜨는데 root fs에서 panic"이고, 면접에서는 "커널이 시작해서 init이 뜰 때까지 무슨 일이 일어나나요?", "initcall이 뭔가요?", "earlycon은 왜 쓰나요?"로 나온다
> **Don 기준 난이도**: 리셋 핸들러·스택 설정·`.data`/`.bss` 초기화·MMU/캐시 켜기·링커 섹션으로 함수 테이블 모으기는 펌웨어에서 이미 했다 / 새로 배울 부분은 Linux가 그 일을 어떤 순서와 이름으로 하는지, 커맨드라인으로 동작을 바꾸는 법, rootfs와 PID 1이라는 개념
> **선행 노트**: C1(부트 전체 지도), B3(ELF와 링킹 — 특히 §11 initcall 흉내 내기) — C3(U-Boot), C5(EL·TF-A)와 함께 읽으면 좋다

---

## 0. 큰 그림 — 이게 왜 필요한가

Don이 SSD 컨트롤러 펌웨어를 bring-up할 때의 순서를 떠올려 보자. ROM 코드가 이미지를 SRAM에 올리고 `Reset_Handler`로 점프한다. 리셋 핸들러는 스택 포인터를 잡고, 캐시·MPU를 켜고, `.data`를 복사하고 `.bss`를 0으로 채운 뒤 `main()`을 부른다. `main()`은 클럭 → 인터럽트 컨트롤러 → 타이머 → UART → NAND/PCIe 드라이버 순서로 초기화 함수를 줄줄이 부르고, 마지막에 RTOS 스케줄러를 시작한다. 어딘가에서 멈추면 Don은 UART에 찍힌 마지막 줄, 또는 JTAG으로 PC를 보고 위치를 찾는다.

Linux 커널의 시작도 **똑같은 모양**이다. 이름만 다르다.

| 펌웨어에서 | Linux arm64에서 | 이 노트의 절 |
|---|---|---|
| ROM이 이미지를 RAM에 올리고 점프 | U-Boot `booti`(또는 QEMU 내장 stub)가 `Image`를 올리고 x0 = DTB 주소로 점프 | §1, §2 |
| `Reset_Handler` (어셈블리, MMU off) | `arch/arm64/kernel/head.S`의 `primary_entry` → `__primary_switch` | §3 |
| `main()`의 앞부분 (클럭·메모리·인터럽트·타이머) | `init/main.c`의 `start_kernel()` → `setup_arch()` … | §4 |
| 디버그 UART를 아주 먼저 켜기 | `earlycon=` | §5 |
| 빌드 옵션 / NVRAM 설정값 | 커널 커맨드라인(`bootargs`), `early_param`/`__setup` | §6 |
| `drivers_init()`이 부르는 초기화 함수 표 | **initcall** — 링커 섹션에 모인 함수 포인터 배열, 레벨 0~7 | §7 |
| 파일 시스템 마운트 | initramfs 풀기 또는 `root=` 장치 마운트 | §8 |
| RTOS 첫 태스크 시작 | PID 1(`/init`, `/sbin/init`) `execve` | §8 |
| 부트 전용 코드 영역 재사용 | "Freeing unused kernel memory" — `.init` 섹션 해제 | §9 |

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="30" width="104" height="50" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="62" y="52" text-anchor="middle" font-size="13" font-weight="bold">부트로더</text>
  <text x="62" y="70" text-anchor="middle" font-size="12">U-Boot booti</text>
  <rect x="122" y="30" width="104" height="50" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="174" y="52" text-anchor="middle" font-size="13" font-weight="bold">head.S</text>
  <text x="174" y="70" text-anchor="middle" font-size="12">MMU off → on</text>
  <rect x="234" y="30" width="104" height="50" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="286" y="52" text-anchor="middle" font-size="12" font-weight="bold">start_kernel</text>
  <text x="286" y="70" text-anchor="middle" font-size="12">C 코드 진입</text>
  <rect x="346" y="30" width="104" height="50" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="398" y="52" text-anchor="middle" font-size="13" font-weight="bold">rest_init</text>
  <text x="398" y="70" text-anchor="middle" font-size="12">PID 1, 2 생성</text>
  <rect x="458" y="30" width="104" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="510" y="52" text-anchor="middle" font-size="12" font-weight="bold">kernel_init</text>
  <text x="510" y="70" text-anchor="middle" font-size="12">PID 1 커널측</text>
  <rect x="570" y="30" width="104" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="622" y="52" text-anchor="middle" font-size="13" font-weight="bold">/init</text>
  <text x="622" y="70" text-anchor="middle" font-size="12">유저 공간</text>
  <line x1="114" y1="55" x2="121" y2="55" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="226" y1="55" x2="233" y2="55" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="338" y1="55" x2="345" y2="55" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="450" y1="55" x2="457" y2="55" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="562" y1="55" x2="569" y2="55" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="62" y="104" text-anchor="middle" font-size="12">x0 = DTB</text>
  <text x="62" y="122" text-anchor="middle" font-size="12">MMU off</text>
  <text x="174" y="104" text-anchor="middle" font-size="12">Image 헤더</text>
  <text x="174" y="122" text-anchor="middle" font-size="12">EL2 → EL1</text>
  <text x="286" y="104" text-anchor="middle" font-size="12">setup_arch</text>
  <text x="286" y="122" text-anchor="middle" font-size="12">earlycon</text>
  <text x="398" y="104" text-anchor="middle" font-size="12">idle = PID 0</text>
  <text x="398" y="122" text-anchor="middle" font-size="12">kthreadd</text>
  <text x="510" y="104" text-anchor="middle" font-size="12">initcall</text>
  <text x="510" y="122" text-anchor="middle" font-size="12">rootfs 마운트</text>
  <text x="622" y="104" text-anchor="middle" font-size="12">execve</text>
  <text x="622" y="122" text-anchor="middle" font-size="12">실패 시 panic</text>
  <line x1="10" y1="150" x2="114" y2="150" stroke="#888" stroke-width="3"/>
  <text x="62" y="170" text-anchor="middle" font-size="12">C3 · C5</text>
  <line x1="122" y1="150" x2="674" y2="150" stroke="#4a7bd0" stroke-width="3"/>
  <text x="398" y="170" text-anchor="middle" font-size="13" font-weight="bold">C6 — 이 노트의 범위 (첫 명령 ~ PID 1 execve)</text>
  <text x="20" y="200" font-size="12">펌웨어 비유: Reset_Handler(asm) → SystemInit → .data/.bss → main()</text>
  <text x="20" y="218" font-size="12">→ 드라이버 init 표 → RTOS 첫 태스크</text>
  <text x="20" y="244" font-size="12">시리얼 로그의 표지: "Booting Linux on physical CPU" → "Kernel command line:"</text>
  <text x="20" y="262" font-size="12">→ "console [ttyAMA0] enabled" → "Unpacking initramfs"</text>
  <text x="20" y="280" font-size="12">→ "Freeing unused kernel memory" → "Run /init"</text>
</svg>
```
그림 1 — 커널 시작의 여섯 단계. 회색은 부트로더(C3·C5), 파랑은 MMU가 켜지기 전 어셈블리, 주황은 `start_kernel` 단계, 초록은 PID 1 쪽이다.

이 노트가 답하려는 실무 질문은 이것들이다.

- **"`Starting kernel ...` 다음에 아무것도 안 나와요."** — 커널이 죽었나, 콘솔만 안 켜졌나? `earlycon`으로 가른다 (§5).
- **"부팅이 8초 걸려요. 2초로 줄여 주세요."** — 어느 initcall이 느린지 `initcall_debug`로 측정한다 (§7). C7(부팅 시간 단축)의 출발점이다.
- **"`Kernel panic - not syncing: VFS: Unable to mount root fs`"** — `root=`가 틀렸나, 스토리지 드라이버가 모듈이라 아직 없나 (§8).
- **"새 드라이버를 built-in으로 넣었는데 probe가 안 불려요 / 순서가 꼬여요."** — device initcall과 deferred probe (§10, G3 미리보기).

> 실행 환경: 모든 출력은 Mac(M2) 위 Lima VM `elinux`(aarch64, Ubuntu 26.04, `uname -r` = `7.0.0-38-generic`)에서 실제로 돌렸다. 부팅 대상은 공용 랩의 **Linux 6.18.55 LTS** arm64 `Image`(defconfig 기반), BusyBox 1.37 initramfs, QEMU 10.2.1 `virt` 머신(`-cpu cortex-a57 -m 512M`, KVM 없이 TCG 에뮬레이션)이다. 소스 인용은 `/var/lab/src/linux-6.18.55`, 심볼은 빌드 트리 `/var/lab/build/linux`의 `vmlinux`·`System.map`에서 grep한 것이다. 타임스탬프·주소(KASLR)는 실행마다 다르고, 같은 VM에서 다른 작업이 함께 돌아 부팅 시간이 1.2~7초로 흔들렸다.

이 노트의 QEMU 실행은 전부 아래 작은 래퍼로 했다. 시리얼 출력을 파일로 받고, `-no-reboot`라서 커널이 재부팅하려 하면 QEMU가 그냥 끝난다.

```sh
#!/bin/bash
# c6_run.sh <tag> <timeout_s> "<kernel cmdline>" [extra qemu args...]
tag=$1; to=$2; app=$3; shift 3
L=/var/lab/out
cd /var/tmp/C6
timeout "$to" qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -no-reboot -kernel $L/Image -append "$app" "$@" > c6_$tag.log 2>&1
echo "exit=$? lines=$(wc -l < c6_$tag.log)"
```

랩 initramfs의 `/init`는 커맨드라인의 `LAB_CMD=명령~인자`를 찾아 실행하고 전원을 끈다(공백 대신 `~`). 그래서 "부팅 → 게스트 안에서 명령 하나 → 종료"를 스크립트로 반복할 수 있다.

---

## 1. `Image` 파일 — 64바이트 헤더

### 1.1 직관 — "펌웨어 이미지 앞에 붙은 명함"

Don의 SSD 펌웨어 이미지에도 앞부분에 헤더가 있었을 것이다. ROM이 읽는 매직 넘버, 이미지 크기, 로드 주소, 체크섬. arm64 커널 `Image`도 같다. 앞 64바이트는 **부트로더가 읽는 명함**이고, 동시에 **실행 가능한 첫 명령어**이기도 하다. 부트로더는 헤더를 읽어 "어디에, 얼마나 큰 공간을 비워서 올려야 하나"를 알고, 그다음 파일 맨 앞(오프셋 0)으로 점프한다.

먼저 용어 정리.

| 이름 | 무엇 | 누가 쓰나 |
|---|---|---|
| `vmlinux` | 링크 결과 ELF. 심볼·디버그 정보 포함, 가상 주소 기준 | gdb, `addr2line`, `crash`, `perf` |
| `Image` | `vmlinux`를 `objcopy -O binary`로 뽑은 **평평한 바이너리** + 64바이트 헤더 | U-Boot `booti`, QEMU `-kernel`, UEFI(EFI stub) |
| `Image.gz` | `Image`를 gzip한 것. arm64 커널은 **스스로 압축을 풀지 않는다** — 부트로더가 푼다 | U-Boot `booti`(압축 해제 지원 시), FIT |
| `zImage` | 32비트 ARM의 자기 압축 해제 이미지. arm64에는 없다 | U-Boot `bootz` (ARM32) |
| `System.map` | 심볼 → 주소 표 (텍스트) | 사람, 디버깅 |

`Documentation/arch/arm64/booting.rst` §3이 압축에 대해 분명히 말한다: "The AArch64 kernel does not currently provide a decompressor and therefore requires decompression (gzip etc.) to be performed by the boot loader".

### 1.2 예제 1 — `Image` 앞 64바이트 hexdump

무엇을 확인하나: 랩 커널 `Image`의 처음 64바이트를 보고, 문서에 적힌 헤더 형식과 맞춰 본다.

```sh
xxd -l 64 /var/lab/out/Image
file /var/lab/out/Image
```

```text
00000000: 4d5a 40fa 3ba8 7f14 0000 0000 0000 0000  MZ@.;...........
00000010: 0000 9802 0000 0000 0a00 0000 0000 0000  ................
00000020: 0000 0000 0000 0000 0000 0000 0000 0000  ................
00000030: 0000 0000 0000 0000 4152 4d64 4000 0000  ........ARMd@...
/var/lab/out/Image: Linux kernel ARM64 boot executable Image, little-endian, 4K pages
```

출력에서 볼 것: 맨 앞이 `MZ`(DOS/PE 실행 파일 서명), 0x38에 `ARMd`(= `ARM\x64`). `file` 명령도 이 매직으로 "ARM64 boot executable Image"라고 알아본다.

### 1.3 문서와 소스 — 헤더의 정의

헤더 정의는 커널 소스의 `Documentation/arch/arm64/booting.rst` §4 "Call the kernel image"에 있다(랩 소스 80~89행).

```text
  u32 code0;			/* Executable code */
  u32 code1;			/* Executable code */
  u64 text_offset;		/* Image load offset, little endian */
  u64 image_size;		/* Effective Image size, little endian */
  u64 flags;			/* kernel flags, little endian */
  u64 res2	= 0;		/* reserved */
  u64 res3	= 0;		/* reserved */
  u64 res4	= 0;		/* reserved */
  u32 magic	= 0x644d5241;	/* Magic number, little endian, "ARM\x64" */
  u32 res5;			/* reserved (used for PE COFF offset) */
```

그리고 이 64바이트를 실제로 만드는 곳은 `arch/arm64/kernel/head.S`의 맨 앞이다.

```sh
cd /var/lab/src/linux-6.18.55
sed -n 57,71p arch/arm64/kernel/head.S
grep -n "efi_signature_nop" -A8 arch/arm64/kernel/efi-header.S | head -9
```

```text
	__HEAD
	/*
	 * DO NOT MODIFY. Image header expected by Linux boot-loaders.
	 */
	efi_signature_nop			// special NOP to identity as PE/COFF executable
	b	primary_entry			// branch to kernel start, magic
	.quad	0				// Image load offset from start of RAM, little-endian
	le64sym	_kernel_size_le			// Effective size of kernel image, little-endian
	le64sym	_kernel_flags_le		// Informative flags, little-endian
	.quad	0				// reserved
	.quad	0				// reserved
	.quad	0				// reserved
	.ascii	ARM64_IMAGE_MAGIC		// Magic number
	.long	.Lpe_header_offset		// Offset to the PE header.

10:	.macro	efi_signature_nop
11-#ifdef CONFIG_EFI
12-.L_head:
13-	/*
14-	 * This ccmp instruction has no meaningful effect except that
15-	 * its opcode forms the magic "MZ" signature required by UEFI.
16-	 */
17-	ccmp	x18, #0, #0xd, pl
```

재미있는 트릭이다. 첫 명령 `ccmp x18, #0, #0xd, pl`은 **아무 효과가 없는 명령**인데, 그 기계어 하위 2바이트가 우연히(사실은 일부러 고른) `4d 5a` = "MZ"다. 그래서 같은 파일이

- U-Boot·QEMU에게는 "오프셋 0으로 점프하면 되는 arm64 Image"이고 (`ccmp`는 NOP처럼 지나가고 다음 `b primary_entry`로 간다),
- UEFI 펌웨어에게는 "MZ로 시작하고 0x3C 위치(= `res5`)에 PE 헤더 오프셋이 있는 PE/COFF 실행 파일"이다. UEFI는 PE 헤더의 엔트리(`efi_stub_entry`)로 들어가고, EFI stub이 일을 마친 뒤 code0으로 돌아온다(`booting.rst` 헤더 노트).

이것이 **EFI stub**(`CONFIG_EFI_STUB=y`)이다. 서버·노트북형 arm64(그리고 GRUB 없이 UEFI가 커널을 직접 띄우는 구성)는 이 경로를, 대부분의 임베디드 보드는 U-Boot `booti` 경로를 쓴다. C8에서 UEFI 쪽을 비교한다.

### 1.4 예제 2 — 헤더를 Python으로 해석하기

무엇을 확인하나: 필드별 값을 디코드하고, `image_size`가 파일 크기보다 큰 이유와 `b` 명령의 목적지를 계산한다.

```python
# c6_imghdr.py: decode the 64-byte arm64 Image header (Documentation/arch/arm64/booting.rst)
import struct, sys, os
p = sys.argv[1]
h = open(p, 'rb').read(64)
code0, code1, text_offset, image_size, flags, r2, r3, r4, magic, res5 = \
    struct.unpack('<IIQQQQQQII', h)
print(f"code0       = {code0:#010x}  (bytes {h[0:2]!r} -> PE/COFF 'MZ' signature)")
print(f"code1       = {code1:#010x}  (b primary_entry, imm26={(code1 & 0x3ffffff)*4:#x})")
print(f"text_offset = {text_offset:#x}")
print(f"image_size  = {image_size:#x} ({image_size/2**20:.1f} MiB)  file size = {os.path.getsize(p)/2**20:.1f} MiB")
print(f"flags       = {flags:#x}: BE={flags & 1}, page_size_code={(flags>>1)&3} (1=4K), phys_placement={(flags>>3)&1}")
print(f"magic       = {magic:#x} {h[56:60]!r}")
print(f"res5(PE off)= {res5:#x} -> bytes there: {open(p,'rb').read(res5+4)[res5:]!r}")
```

```sh
python3 -I ~/.elinux_scratch/C6/c6_imghdr.py /var/lab/out/Image
```

```text
code0       = 0xfa405a4d  (bytes b'MZ' -> PE/COFF 'MZ' signature)
code1       = 0x147fa83b  (b primary_entry, imm26=0x1fea0ec)
text_offset = 0x0
image_size  = 0x2980000 (41.5 MiB)  file size = 40.8 MiB
flags       = 0xa: BE=0, page_size_code=1 (1=4K), phys_placement=1
magic       = 0x644d5241 b'ARMd'
res5(PE off)= 0x40 -> bytes there: b'PE\x00\x00'
```

출력에서 볼 것:

- `code1`은 `b` 명령이다. 오프셋 4에 있으니 목적지는 4 + 0x1fea0ec = **0x1fea0f0**. `System.map`에서 `primary_entry - _text`를 계산하면 정확히 같은 값이 나온다(아래 예제 3).
- `image_size`(41.5 MiB) > 파일 크기(40.8 MiB). 차이는 **`.bss`** 다. 파일에는 0으로 채울 영역이 없지만, 부트로더는 그만큼을 비워 둬야 한다. Don이 펌웨어 링커 스크립트에서 `_end - _start`로 RAM 예산을 잡던 것과 같다.
- `text_offset = 0`: v3.17 이전에는 0x80000이 관례였지만, 지금은 "2 MB 정렬된 주소 + text_offset"에 올리면 된다.
- `flags = 0xa`: 리틀 엔디언, 4K 페이지, bit3=1(48비트 물리 주소 범위 안 어디든 둘 수 있음).
- `res5 = 0x40`: 오프셋 0x40에 실제로 `PE\0\0` 서명이 있다. EFI stub의 PE 헤더다.

### 1.5 예제 3 — `System.map`으로 헤더 값 검증

무엇을 확인하나: 헤더의 `image_size`와 `b` 목적지가 링커가 정한 심볼과 일치하는지 본다.

```sh
cd /var/lab/build/linux
grep -E " (_text|_stext|primary_entry|_end)$" System.map
python3 -c '
m={l.split()[2]:int(l.split()[0],16) for l in open("System.map")}
print("_end - _text          = %#x" % (m["_end"]-m["_text"]))
print("primary_entry - _text = %#x" % (m["primary_entry"]-m["_text"]))'
```

```text
ffff800080000000 T _text
ffff800080010000 T _stext
ffff800081fea0f0 T primary_entry
ffff800082980000 B _end
_end - _text          = 0x2980000
primary_entry - _text = 0x1fea0f0
```

출력에서 볼 것: `_end - _text` = 헤더의 `image_size`(0x2980000), `primary_entry - _text` = `b` 목적지(0x1fea0f0). 헤더는 링커 심볼(`_kernel_size_le`)로 자동 생성되므로 손으로 맞출 일이 없다. 링크 주소가 `0xffff800080000000`(커널 가상 주소)인데 부트로더는 물리 주소 `0x40200000`에 올린다 — 이 차이를 메우는 것이 §3의 `head.S` 일이다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <text x="20" y="30" font-size="12">0x00</text>
  <text x="180" y="30" font-size="12">0x08</text>
  <text x="340" y="30" font-size="12">0x10</text>
  <text x="500" y="30" font-size="12">0x18</text>
  <rect x="20" y="36" width="80" height="50" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="60" y="56" text-anchor="middle" font-size="13" font-weight="bold">code0</text>
  <text x="60" y="76" text-anchor="middle" font-size="12">fa405a4d</text>
  <rect x="100" y="36" width="80" height="50" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="140" y="56" text-anchor="middle" font-size="13" font-weight="bold">code1</text>
  <text x="140" y="76" text-anchor="middle" font-size="12">147fa83b</text>
  <rect x="180" y="36" width="160" height="50" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="260" y="56" text-anchor="middle" font-size="13" font-weight="bold">text_offset</text>
  <text x="260" y="76" text-anchor="middle" font-size="12">0x0</text>
  <rect x="340" y="36" width="160" height="50" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="420" y="56" text-anchor="middle" font-size="13" font-weight="bold">image_size</text>
  <text x="420" y="76" text-anchor="middle" font-size="12">0x2980000</text>
  <rect x="500" y="36" width="160" height="50" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="580" y="56" text-anchor="middle" font-size="13" font-weight="bold">flags</text>
  <text x="580" y="76" text-anchor="middle" font-size="12">0xa</text>
  <text x="20" y="116" font-size="12">0x20</text>
  <text x="180" y="116" font-size="12">0x28</text>
  <text x="340" y="116" font-size="12">0x30</text>
  <text x="500" y="116" font-size="12">0x38</text>
  <text x="580" y="116" font-size="12">0x3c</text>
  <rect x="20" y="122" width="160" height="50" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="100" y="142" text-anchor="middle" font-size="13" font-weight="bold">res2</text>
  <text x="100" y="162" text-anchor="middle" font-size="12">0</text>
  <rect x="180" y="122" width="160" height="50" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="260" y="142" text-anchor="middle" font-size="13" font-weight="bold">res3</text>
  <text x="260" y="162" text-anchor="middle" font-size="12">0</text>
  <rect x="340" y="122" width="160" height="50" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="420" y="142" text-anchor="middle" font-size="13" font-weight="bold">res4</text>
  <text x="420" y="162" text-anchor="middle" font-size="12">0</text>
  <rect x="500" y="122" width="80" height="50" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="540" y="142" text-anchor="middle" font-size="13" font-weight="bold">magic</text>
  <text x="540" y="162" text-anchor="middle" font-size="12">"ARMd"</text>
  <rect x="580" y="122" width="80" height="50" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="620" y="142" text-anchor="middle" font-size="13" font-weight="bold">res5</text>
  <text x="620" y="162" text-anchor="middle" font-size="12">0x40</text>
  <rect x="20" y="196" width="14" height="12" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="42" y="207" font-size="12">code0 = "MZ" + 아무 효과 없는 ccmp, code1 = b primary_entry (목적지 0x1fea0f0)</text>
  <rect x="20" y="220" width="14" height="12" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="42" y="231" font-size="12">text_offset 0 · image_size = _end - _text (.bss 포함 41.5 MiB) · flags: LE, 4K, 배치 자유</text>
  <rect x="20" y="244" width="14" height="12" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="42" y="255" font-size="12">magic 0x644d5241 — 부트로더(booti)가 "arm64 Image가 맞나" 확인하는 값</text>
  <rect x="20" y="268" width="14" height="12" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="42" y="279" font-size="12">res5 = PE 헤더 오프셋 (DOS 헤더의 e_lfanew 자리) — UEFI는 여기서 EFI stub 엔트리를 찾는다</text>
  <text x="20" y="312" font-size="12">모든 다중 바이트 필드는 리틀 엔디언 (v3.17부터). 칸 너비 = 바이트 수에 비례 (1 바이트 = 20 px)</text>
</svg>
```
그림 2 — arm64 `Image` 헤더 64바이트 지도. 값은 랩 커널 6.18.55에서 읽은 것이다.

### 1.6 함정

- **`Image.gz`를 `booti`가 압축 해제하지 못하는 오래된 U-Boot**에 주면 아무 메시지 없이 멈춘다. 압축 이미지는 `kernel_comp_addr_r`/`kernel_comp_size` 환경변수가 필요하다(C3).
- **`vmlinux`(ELF)를 `Image` 자리에 넣는 실수.** 첫 바이트가 `7f 45 4c 46`(ELF)이라 매직이 안 맞는다. U-Boot `booti`는 "Bad Linux ARM64 Image magic!"을 출력한다. 반대로 gdb에는 `Image`가 아니라 `vmlinux`를 줘야 심볼이 보인다.
- **`image_size`를 무시하고 DTB·initramfs를 커널 바로 뒤에 두는 실수.** 파일 크기만 보고 그 뒤에 DTB를 두면 커널의 `.bss` 초기화가 DTB를 0으로 지운다. 부트로더는 `image_size`만큼 비워야 한다 — 펌웨어에서 "`.bss`가 다음 영역을 덮었다"와 같은 버그다.

---

## 2. 부트로더와의 계약 — 점프하는 순간의 CPU 상태

### 2.1 직관

ROM이 펌웨어로 점프할 때 "스택은 아직 없음, 캐시 off, 클럭은 기본값" 같은 약속이 있었듯이, arm64 커널도 **진입 조건**을 문서로 정해 둔다. 부트로더(U-Boot, UEFI, QEMU 내장 stub)가 이를 어기면 커널은 아주 이른 시점에, 아무 출력 없이 죽는다.

`booting.rst`의 해당 부분(랩 소스 157~180행 발췌):

```text
Before jumping into the kernel, the following conditions must be met:

- Quiesce all DMA capable devices so that memory does not get
  corrupted by bogus network packets or disk data.  This will save
  you many hours of debug.

- Primary CPU general-purpose register settings:

    - x0 = physical address of device tree blob (dtb) in system RAM.
    - x1 = 0 (reserved for future use)
    - x2 = 0 (reserved for future use)
    - x3 = 0 (reserved for future use)

- CPU mode

  All forms of interrupts must be masked in PSTATE.DAIF (Debug, SError,
  IRQ and FIQ).
  The CPU must be in non-secure state, either in EL2 (RECOMMENDED in order
  to have access to the virtualisation extensions), or in EL1.

- Caches, MMUs

  The MMU must be off.
```

`head.S`의 주석은 한 줄로 요약한다: `MMU = off, D-cache = off, I-cache = on or off, x0 = physical address to the FDT blob.` 그리고 DTB 자체에 대한 조건도 있다(53행): "The device tree blob (dtb) must be placed on an 8-byte boundary and must not exceed 2 megabytes in size."

| 조건 | 왜 | 어기면 |
|---|---|---|
| x0 = DTB 물리 주소 | 커널은 하드웨어 목록(메모리 크기, UART 주소, 인터럽트 컨트롤러)을 DTB에서만 안다 | `setup_machine_fdt()`가 "Error: invalid device tree blob"을 찍으려다(콘솔이 없어 보통 안 보임) `cpu_relax()` 무한 루프 |
| x1~x3 = 0 | 미래 확장용 | `setup_arch()`가 "WARNING: x1-x3 nonzero in violation of boot protocol" 출력 |
| MMU off | 커널이 자기 페이지 테이블을 처음부터 만든다 | (켜진 채 진입하면) `head.S`가 캐시 정리 경로를 타고 `TAINT_FIRMWARE_WORKAROUND` 경고 |
| D-cache off, 이미지가 PoC까지 clean | MMU off 상태의 데이터 접근은 non-cacheable — 캐시에 남은 더러운 줄이 있으면 커널이 옛 값을 읽는다 | 랜덤한 초기 크래시 (Don이 DMA 버퍼에서 겪던 캐시 일관성 문제와 같다) |
| DAIF 마스크, EL2 또는 EL1 (non-secure) | 벡터 테이블이 아직 없다. EL3/secure는 TF-A 몫(C5) | 즉시 예외 → 행 |
| DMA 장치 정지 | 부트로더가 켜 둔 이더넷 DMA가 커널 메모리를 덮어씀 | 재현 안 되는 메모리 손상 |

### 2.2 예제 4 — gdb로 커널 첫 명령에서 멈춰 레지스터 보기

무엇을 확인하나: QEMU를 `-S`(시작 정지)와 gdb 서버로 띄우고, 커널 첫 명령(물리 0x40200000)에 하드웨어 브레이크포인트를 건 뒤 x0~x3, 예외 레벨, SCTLR_EL1을 읽는다. Don이 JTAG으로 리셋 직후 레지스터를 보던 것과 똑같다.

```text
# c6_entry.gdb
set pagination off
target remote :12466
hbreak *0x40200000
continue
printf "pc=%#lx x0=%#lx x1=%#lx x2=%#lx x3=%#lx\n", $pc, $x0, $x1, $x2, $x3
printf "cpsr=%#lx  CurrentEL=EL%d  DAIF=%#lx\n", $cpsr, ($cpsr>>2)&3, ($cpsr>>6)&0xf
printf "SCTLR_EL1=%#lx  M(bit0,MMU)=%d C(bit2,D-cache)=%d I(bit12)=%d\n", $SCTLR_EL1, $SCTLR_EL1&1, ($SCTLR_EL1>>2)&1, ($SCTLR_EL1>>12)&1
x/4xb $x0
x/2i $pc
kill
quit
```

```sh
cd /var/tmp/C6; L=/var/lab/out
(timeout 60 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic -no-reboot \
   -kernel $L/Image -initrd $L/initramfs.cpio.gz -append "console=ttyAMA0" \
   -S -gdb tcp::12466 > c6_gdbq.log 2>&1 &)
sleep 1; gdb -q -batch -x ~/.elinux_scratch/C6/c6_entry.gdb
```

```text
0x0000000040000000 in ?? ()
Hardware assisted breakpoint 1 at 0x40200000

Breakpoint 1, 0x0000000040200000 in ?? ()
pc=0x40200000 x0=0x48200000 x1=0 x2=0 x3=0
cpsr=0x400003c5  CurrentEL=EL1  DAIF=0xf
SCTLR_EL1=0xc50838  M(bit0,MMU)=0 C(bit2,D-cache)=0 I(bit12)=0
0x48200000:	0xd0	0x0d	0xfe	0xed
=> 0x40200000:	ccmp	x18, #0x0, #0xd, pl	// pl = nfrst
   0x40200004:	b	0x421ea0f0
```

(gdb 시작 시의 "No executable has been specified" 경고 두 줄과 종료 메시지는 생략했다.)

출력에서 볼 것:

- **x0 = 0x48200000**, 그 주소의 첫 4바이트가 `d0 0d fe ed` — FDT 매직 `0xd00dfeed`다. DTB가 정말 x0에 있다.
- x1~x3 = 0, DAIF = 0xf(Debug·SError·IRQ·FIQ 모두 마스크), SCTLR_EL1의 M·C 비트 = 0(MMU off, D-cache off). 문서의 계약 그대로다.
- 첫 명령이 정말 `ccmp`("MZ")이고 다음이 `b 0x421ea0f0` = 0x40200000 + 0x1fea0f0(`primary_entry`). 예제 2의 계산과 맞는다.
- 커널은 0x40200000(RAM 시작 0x40000000 + 2 MB)에 올라갔다. 2 MB 정렬 + `text_offset` 0이다.
- 이 기본 실행은 **EL1**로 들어왔다. QEMU `virt`는 기본적으로 EL2를 에뮬레이션하지 않기 때문이다.

### 2.3 예제 5 — 세상에서 가장 작은 부트로더: QEMU 내장 stub

무엇을 확인하나: `-kernel`로 부팅할 때 QEMU가 RAM 맨 앞에 써 두는 "부트로더"를 디스어셈블한다. 그리고 `virtualization=on`이면 커널이 EL2로 들어오는지 본다.

```text
# c6_stub.gdb
set pagination off
target remote :12467
x/8i 0x40000000
x/2gx 0x40000018
hbreak *0x40200000
continue
printf "x0=%#lx CurrentEL=EL%d\n", $x0, ($cpsr>>2)&3
kill
quit
```

```sh
(timeout 60 qemu-system-aarch64 -machine virt,virtualization=on -cpu cortex-a57 -m 512M -nographic \
   -no-reboot -kernel $L/Image -initrd $L/initramfs.cpio.gz -append "console=ttyAMA0" \
   -S -gdb tcp::12467 > c6_gdbq2.log 2>&1 &)
sleep 1; gdb -q -batch -x ~/.elinux_scratch/C6/c6_stub.gdb
```

```text
0x0000000040000000 in ?? ()
=> 0x40000000:	ldr	x0, 0x40000018
   0x40000004:	mov	x1, xzr
   0x40000008:	mov	x2, xzr
   0x4000000c:	mov	x3, xzr
   0x40000010:	ldr	x4, 0x40000020
   0x40000014:	br	x4
   0x40000018:	.inst	0x48200000 ; undefined
   0x4000001c:	udf	#0
0x40000018:	0x0000000048200000	0x0000000040200000
Hardware assisted breakpoint 1 at 0x40200000

Breakpoint 1, 0x0000000040200000 in ?? ()
x0=0x48200000 CurrentEL=EL2
```

출력에서 볼 것: 부트 프로토콜의 본질은 **명령어 6개**다 — x0에 DTB 주소, x1~x3에 0, 커널 주소로 `br`. 0x40000018/0x40000020의 두 값은 명령이 아니라 데이터(리터럴 풀)다. U-Boot의 `booti`도 결국 이 일을 하고, 그 전에 이미지 검사·DTB 수정(`/chosen/bootargs` 채우기, 메모리 노드 갱신)·캐시 정리·MMU 끄기를 해 준다(C3). `virtualization=on`이면 같은 커널이 **EL2**로 들어온다. 실제 보드에서는 TF-A BL31이 EL3에서 EL2로 내려 U-Boot를 띄우고, U-Boot가 EL2 그대로 커널에 넘기는 것이 보통이다(C5).

---

## 3. `head.S` — 첫 명령에서 `start_kernel`까지

### 3.1 직관 — 리셋 핸들러가 하는 일, 다만 MMU를 켜야 한다

펌웨어의 `Reset_Handler`는 MPU를 켜도 주소가 바뀌지 않는다. Linux 커널은 다르다. **링크 주소는 가상 주소(0xffff8000…)인데, 지금 PC는 물리 주소(0x4020…)** 다. 그래서 `head.S`는

1. 물리 주소 = 가상 주소인 임시 매핑(**identity map**, idmap)을 만들고,
2. MMU를 켠 순간에도 PC가 유효하도록 idmap 안의 코드(`.idmap.text`)에서 MMU를 켜고,
3. 커널을 진짜 가상 주소(KASLR이면 무작위 오프셋 추가)로 매핑·재배치한 뒤,
4. 가상 주소로 점프해서 C 함수 `start_kernel()`을 부른다.

Don이 Cortex-R에서 MPU 영역을 바꾸면서 "지금 실행 중인 코드 영역의 속성을 바꾸면 안 된다"를 신경 쓰던 것의 MMU판이다.

### 3.2 예제 6 — 소스에서 실제 호출 순서 읽기

무엇을 확인하나: 기억이 아니라 랩 소스(6.18.55)에서 `primary_entry` → `__primary_switch` → `__primary_switched` → `start_kernel` 경로를 grep한다.

```sh
cd /var/lab/src/linux-6.18.55
sed -n 76,84p arch/arm64/kernel/head.S
grep -n "^SYM_CODE_START(primary_entry)" -A46 arch/arm64/kernel/head.S | grep -E "bl\s|b\s"
grep -n "^SYM_FUNC_START_LOCAL(__primary_switch)" -A15 arch/arm64/kernel/head.S | grep -E "bl\s|br\s|ldr"
grep -n "^SYM_FUNC_START_LOCAL(__primary_switched)" -A28 arch/arm64/kernel/head.S | grep -E "msr|str_l|bl\s"
```

```text
	/*
	 * The following callee saved general purpose registers are used on the
	 * primary lowlevel boot path:
	 *
	 *  Register   Scope                      Purpose
	 *  x19        primary_entry() .. start_kernel()        whether we entered with the MMU on
	 *  x20        primary_entry() .. __primary_switch()    CPU boot mode
	 *  x21        primary_entry() .. start_kernel()        FDT pointer passed at boot in x0
	 */
86-	bl	record_mmu_state
87-	bl	preserve_boot_args
94-	bl	__pi_create_init_idmap
102-	dmb     sy
107-	b	1f
120-	bl	init_kernel_el			// w0=cpu_boot_mode
129-	bl	__cpu_setup			// initialise processor
130-	b	__primary_switch
511-	bl	__enable_mmu
518-	bl	__pi_early_map_kernel		// Map and relocate the kernel
520-	ldr	x8, =__primary_switched
522-	br	x8
225-	msr	vbar_el1, x8			// vector table address
231-	str_l	x21, __fdt_pointer, x5		// Save FDT pointer
235-	str_l	x4, kimage_voffset, x5		// physical mappings
238-	bl	set_cpu_boot_mode_flag
241-	bl	kasan_early_init
244-	bl	finalise_el2			// Prefer VHE if possible
246-	bl	start_kernel
```

(`grep`은 `#if` 안의 줄도 잡는다. 241행 `kasan_early_init`은 `CONFIG_KASAN_GENERIC`/`CONFIG_KASAN_SW_TAGS`일 때만 컴파일되는데, 이 커널은 KASAN이 꺼져 있어 실제로는 호출되지 않는다. 102·107행은 캐시 무효화 분기다.)

출력에서 볼 것 — 한 줄씩 펌웨어 언어로 바꾸면:

| 함수 (6.18) | 하는 일 | 펌웨어 비유 |
|---|---|---|
| `record_mmu_state` | 부트로더가 MMU를 켠 채 넘겼는지 기록(x19) | 리셋 원인 레지스터 읽기 |
| `preserve_boot_args` | x0~x3을 `boot_args[]`에 저장 (나중에 x1~x3 검사) | 부트 인자 백업 |
| `__pi_create_init_idmap` | identity map 페이지 테이블 생성 | MPU 영역 표 만들기 |
| `init_kernel_el` | EL2로 들어왔으면 hypervisor stub 설치 후 EL1 설정, 부트 모드 반환(x20) | 특권 모드 정리 |
| `__cpu_setup` | TCR·MAIR 등 MMU 관련 시스템 레지스터 설정 (`arch/arm64/mm/proc.S`) | 캐시·메모리 속성 설정 |
| `__primary_switch` → `__enable_mmu` | MMU on (아직 idmap에서 실행 중) | MPU enable |
| `__pi_early_map_kernel` | 커널을 가상 주소에 매핑, KASLR 오프셋 적용·재배치 | (펌웨어엔 없음) |
| `__primary_switched` | 가상 주소에서 실행. `init_task` 스택, `VBAR_EL1` = 벡터 테이블, `__fdt_pointer` = x21 저장, `finalise_el2` | 벡터 테이블 등록, 스택 설정 |
| `bl start_kernel` | C 세계로. 돌아오면 `ASM_BUG()` | `bl main` |

DTB 주소는 x0 → x21 → 전역 변수 `__fdt_pointer`로 전달되고, `setup_arch()`가 `setup_machine_fdt(__fdt_pointer)`로 꺼내 쓴다(§4).

### 3.3 예제 7 — 어떤 함수가 부팅 후 사라지나 (`.init.text` vs `.text`)

무엇을 확인하나: `System.map`에서 경로상의 함수들이 어느 영역에 있는지 본다. `_sinittext`~`_einittext` 사이는 부팅이 끝나면 해제되는 `.init.text`다.

```sh
cd /var/lab/build/linux
grep -E " (_stext|run_init_process|rest_init|kernel_init|__idmap_text_start|primary_entry|__primary_switch|_sinittext|start_kernel|kernel_init_freeable|setup_arch|__primary_switched|prepare_namespace|_einittext)$" System.map
```

```text
ffff800080010000 T _stext
ffff800080014360 t run_init_process
ffff8000812396c0 t rest_init
ffff8000812397a0 t kernel_init
ffff800081fea000 T __idmap_text_start
ffff800081fea0f0 T primary_entry
ffff800081fea670 t __primary_switch
ffff800081ff0000 T _sinittext
ffff800081ff07dc T start_kernel
ffff800081ff0f20 t kernel_init_freeable
ffff800081ff1af0 T prepare_namespace
ffff800081ff3a08 T setup_arch
ffff800081ff93a4 t __primary_switched
ffff80008207ef28 T _einittext
```

출력에서 볼 것:

- `primary_entry`, `__primary_switch`는 `.idmap.text`(identity map 영역)에 있다. MMU를 켜는 순간에 실행되는 코드라 물리 = 가상 매핑 안에 있어야 한다.
- `start_kernel`, `setup_arch`, `kernel_init_freeable`, `prepare_namespace`, `__primary_switched`는 **`.init.text`** (`_sinittext`~`_einittext`) 안이다. 한 번만 실행되니 부팅 후 메모리를 돌려준다(§9).
- 반대로 `rest_init`과 `kernel_init`은 일반 `.text`에 있다. `init/main.c`의 `start_kernel()` 마지막 주석이 이유를 말한다: `/* Do the rest non-__init'ed, we're now alive */`. `rest_init`을 실행한 부트 CPU의 흐름은 그대로 **idle 태스크(PID 0)** 가 되어 영원히 살고, `kernel_init`은 `.init` 메모리를 해제하는 **주체**이므로 자기가 해제될 영역에 있으면 안 된다. 이름에 `_freeable`이 붙은 `kernel_init_freeable`만 `.init.text`에 있다.

---

## 4. `start_kernel()` — C 세계의 첫 함수

### 4.1 큰 순서

`init/main.c`의 `start_kernel()`은 약 200줄짜리 "초기화 함수 호출 목록"이다. 랩 소스에서 호출만 뽑으면 이렇게 시작한다(일부).

```sh
cd /var/lab/src/linux-6.18.55
awk '/void start_kernel\(void\)/,/^}/' init/main.c | grep -nE "setup_arch|setup_command_line|parse_early_param|parse_args|trap_init|mm_core_init|sched_init\(|init_IRQ|time_init|console_init|rest_init"
```

```text
23:	setup_arch(&command_line);
29:	setup_command_line(command_line);
38:	parse_early_param();
39:	after_dashes = parse_args("Booting kernel",
45:		parse_args("Setting init args", after_dashes, NULL, 0, -1, -1,
48:		parse_args("Setting extra init args", extra_init_args,
61:	trap_init();
62:	mm_core_init();
75:	sched_init();
107:	init_IRQ();
115:	time_init();
136:	 * we've done PCI setups etc, and console_init() must be aware of
139:	console_init();
165:	if (late_time_init)
166:		late_time_init();
202:	rest_init();
```

```svg
<svg viewBox="0 0 680 520" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="112" y="24" text-anchor="middle" font-size="13" font-weight="bold">① head.S (asm)</text>
  <text x="340" y="24" text-anchor="middle" font-size="13" font-weight="bold">② start_kernel()</text>
  <text x="567" y="24" text-anchor="middle" font-size="13" font-weight="bold">③ PID 1 = kernel_init</text>
  <rect x="10" y="40" width="205" height="44" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="112" y="58" text-anchor="middle" font-size="12" font-weight="bold">QEMU stub / U-Boot</text>
  <text x="112" y="76" text-anchor="middle" font-size="12">x0 = DTB, br 0x40200000</text>
  <rect x="10" y="96" width="205" height="44" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="112" y="114" text-anchor="middle" font-size="12" font-weight="bold">primary_entry</text>
  <text x="112" y="132" text-anchor="middle" font-size="12">idmap 생성, 부트 인자 보관</text>
  <rect x="10" y="152" width="205" height="44" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="112" y="170" text-anchor="middle" font-size="12" font-weight="bold">init_kernel_el</text>
  <text x="112" y="188" text-anchor="middle" font-size="12">EL2면 hyp stub → EL1</text>
  <rect x="10" y="208" width="205" height="44" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="112" y="226" text-anchor="middle" font-size="12" font-weight="bold">__cpu_setup</text>
  <text x="112" y="244" text-anchor="middle" font-size="12">TCR · MAIR 설정</text>
  <rect x="10" y="264" width="205" height="44" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="112" y="282" text-anchor="middle" font-size="12" font-weight="bold">__primary_switch</text>
  <text x="112" y="300" text-anchor="middle" font-size="12">MMU on, 커널 재배치</text>
  <rect x="10" y="320" width="205" height="44" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="112" y="338" text-anchor="middle" font-size="12" font-weight="bold">__primary_switched</text>
  <text x="112" y="356" text-anchor="middle" font-size="12">VBAR, __fdt_pointer</text>
  <line x1="112" y1="84" x2="112" y2="95" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="112" y1="140" x2="112" y2="151" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="112" y1="196" x2="112" y2="207" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="112" y1="252" x2="112" y2="263" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="112" y1="308" x2="112" y2="319" stroke="currentColor" marker-end="url(#ah2)"/>
  <path d="M215,342 L226,342 L226,55 L236,55" fill="none" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="237" y="40" width="205" height="30" rx="4" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
  <text x="340" y="60" text-anchor="middle" font-size="13" font-weight="bold">start_kernel()</text>
  <rect x="237" y="82" width="205" height="44" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="100" text-anchor="middle" font-size="12" font-weight="bold">setup_arch()</text>
  <text x="340" y="118" text-anchor="middle" font-size="12">FDT, early_param, unflatten</text>
  <rect x="237" y="134" width="205" height="44" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="152" text-anchor="middle" font-size="12" font-weight="bold">setup_command_line()</text>
  <text x="340" y="170" text-anchor="middle" font-size="12">parse_args → __setup</text>
  <rect x="237" y="186" width="205" height="44" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="204" text-anchor="middle" font-size="12" font-weight="bold">trap_init · mm_core_init</text>
  <text x="340" y="222" text-anchor="middle" font-size="12">sched_init</text>
  <rect x="237" y="238" width="205" height="44" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="256" text-anchor="middle" font-size="12" font-weight="bold">init_IRQ · time_init</text>
  <text x="340" y="274" text-anchor="middle" font-size="12">여기부터 타임스탬프 &gt; 0</text>
  <rect x="237" y="290" width="205" height="44" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="308" text-anchor="middle" font-size="12" font-weight="bold">console_init()</text>
  <text x="340" y="326" text-anchor="middle" font-size="12">con_initcall (tty0 등)</text>
  <rect x="237" y="342" width="205" height="44" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="360" text-anchor="middle" font-size="12" font-weight="bold">rest_init()</text>
  <text x="340" y="378" text-anchor="middle" font-size="12">.text — 끝까지 산다</text>
  <line x1="340" y1="70" x2="340" y2="81" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="340" y1="126" x2="340" y2="133" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="340" y1="178" x2="340" y2="185" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="340" y1="230" x2="340" y2="237" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="340" y1="282" x2="340" y2="289" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="340" y1="334" x2="340" y2="341" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="237" y="410" width="205" height="44" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="428" text-anchor="middle" font-size="12">kthreadd 생성 (PID 2)</text>
  <text x="340" y="446" text-anchor="middle" font-size="12">자신은 PID 0 idle 루프로</text>
  <line x1="340" y1="386" x2="340" y2="409" stroke="currentColor" marker-end="url(#ah2)"/>
  <path d="M442,364 L453,364 L453,55 L463,55" fill="none" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="464" y="40" width="206" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.25" stroke="#3f9a6b"/>
  <text x="567" y="60" text-anchor="middle" font-size="13" font-weight="bold">kernel_init()</text>
  <rect x="464" y="82" width="206" height="44" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="567" y="100" text-anchor="middle" font-size="12" font-weight="bold">kernel_init_freeable()</text>
  <text x="567" y="118" text-anchor="middle" font-size="12">smp_init, early initcall</text>
  <rect x="464" y="134" width="206" height="44" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="567" y="152" text-anchor="middle" font-size="12" font-weight="bold">do_basic_setup()</text>
  <text x="567" y="170" text-anchor="middle" font-size="12">driver_init → do_initcalls</text>
  <rect x="464" y="186" width="206" height="44" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="567" y="204" text-anchor="middle" font-size="12" font-weight="bold">console_on_rootfs()</text>
  <text x="567" y="222" text-anchor="middle" font-size="12">/dev/console → fd 0,1,2</text>
  <rect x="464" y="238" width="206" height="44" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="567" y="256" text-anchor="middle" font-size="12" font-weight="bold">prepare_namespace()</text>
  <text x="567" y="274" text-anchor="middle" font-size="12">rdinit 없을 때만, root=</text>
  <rect x="464" y="290" width="206" height="44" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="567" y="308" text-anchor="middle" font-size="12" font-weight="bold">free_initmem()</text>
  <text x="567" y="326" text-anchor="middle" font-size="12">Freeing unused kernel memory</text>
  <rect x="464" y="342" width="206" height="44" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="567" y="360" text-anchor="middle" font-size="12" font-weight="bold">run_init_process()</text>
  <text x="567" y="378" text-anchor="middle" font-size="12">execve("/init") → 유저</text>
  <line x1="567" y1="70" x2="567" y2="81" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="567" y1="126" x2="567" y2="133" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="567" y1="178" x2="567" y2="185" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="567" y1="230" x2="567" y2="237" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="567" y1="282" x2="567" y2="289" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="567" y1="334" x2="567" y2="341" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="20" y="420" font-size="12">user_mode_thread(kernel_init)</text>
  <text x="20" y="438" font-size="12">→ PID 1 (먼저 만든다)</text>
  <text x="20" y="456" font-size="12">kernel_thread(kthreadd) → PID 2</text>
  <text x="20" y="494" font-size="12">파랑: MMU off/idmap 어셈블리 · 주황: start_kernel 단계 (.init.text)</text>
  <text x="20" y="512" font-size="12">초록: PID 1 커널 스레드 · 회색: 부트로더와 그 밖</text>
</svg>
```
그림 3 — `start_kernel` 호출 트리. 왼쪽 열은 `head.S`, 가운데는 `start_kernel()`의 주요 호출, 오른쪽은 `rest_init()`이 만든 PID 1(`kernel_init`)이 하는 일이다. 함수 이름은 전부 랩 소스 6.18.55에서 grep한 것이다.

### 4.2 `setup_arch()` — DTB를 읽고 메모리를 알게 된다

arm64의 `setup_arch()`(`arch/arm64/kernel/setup.c`)에서 이 노트와 관련된 호출만 뽑으면:

```sh
awk '/^void __init __no_sanitize_address setup_arch/,/^}/' arch/arm64/kernel/setup.c | grep -nE "setup_machine_fdt|parse_early_param|arm64_memblock_init|paging_init|unflatten_device_tree|psci_dt_init|smp_init_cpus|x1-x3"
```

```text
12:	setup_machine_fdt(__fdt_pointer);
19:	parse_early_param();
52:	arm64_memblock_init();
54:	paging_init();
62:		unflatten_device_tree();
73:		psci_dt_init();
80:	smp_init_cpus();
93:		pr_err("WARNING: x1-x3 nonzero in violation of boot protocol:\n"
```

순서가 곧 의미다.

1. **`setup_machine_fdt(__fdt_pointer)`** — DTB를 fixmap으로 매핑하고 `early_init_dt_scan()`으로 **평평한(flattened) 상태 그대로** 훑는다. 이때 읽는 것: `/memory` 노드(RAM 범위), `/chosen`의 `bootargs`(커맨드라인 → `boot_command_line`), `linux,initrd-start/end`(initramfs 위치). 성공하면 "Machine model: …"을 찍는다.
2. **`parse_early_param()`** — `early_param()`으로 등록된 인자(`earlycon`, `mem=`, `loglevel` 등)를 지금 처리한다. 메모리 할당기도 없는 시점이라 "아주 이른" 인자만 여기서 본다(§6). arm64는 `start_kernel()`도 나중에 `parse_early_param()`을 부르지만, 두 번째 호출은 내부 플래그로 바로 돌아온다.
3. **`arm64_memblock_init()` / `paging_init()`** — DT에서 읽은 메모리로 부트 메모리 할당기(memblock)와 커널 페이지 테이블을 완성한다.
4. **`unflatten_device_tree()`** — 평평한 DTB를 `struct device_node` 트리로 펼친다. 이후 드라이버가 쓰는 `of_find_node_by_path()`, `of_property_read_u32()` 등은 전부 이 트리를 본다(D1~D5).
5. **`psci_dt_init()` / `smp_init_cpus()`** — DT의 `psci` 노드와 `cpus` 노드로 보조 CPU를 깨울 방법(PSCI `CPU_ON`, C5)을 정한다.

### 4.3 예제 8 — 부트 로그 한 줄 한 줄을 함수에 대응시키기

무엇을 확인하나: 기본 부팅 로그에서 표지가 되는 줄을 뽑아, 그림 3의 어느 단계인지 맞춰 본다.

```sh
cd /var/tmp/C6; L=/var/lab/out
timeout 120 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic -kernel $L/Image \
   -initrd $L/initramfs.cpio.gz -append "console=ttyAMA0 LAB_CMD=cat~/proc/cmdline" > c6_boot1.log 2>&1
grep -nE "Booting Linux|Machine model|Kernel command line|NR_IRQS|arch_timer|sched_clock|Console:|smp: Brought|Memory:|devtmpfs: initialized|console \[ttyAMA0\]|clocksource: Switched|Unpacking initramfs|Freeing initrd|Freeing unused|Run /init" /var/tmp/C6/c6_boot1.log
```

```text
1:[    0.000000] Booting Linux on physical CPU 0x0000000000 [0x411fd070]
5:[    0.000000] Machine model: linux,dummy-virt
36:[    0.000000] Kernel command line: console=ttyAMA0 LAB_CMD=cat~/proc/cmdline
58:[    0.000000] NR_IRQS: 64, nr_irqs: 64, preallocated irqs: 0
63:[    0.000000] arch_timer: cp15 timer running at 62.50MHz (virt).
65:[    0.000052] sched_clock: 57 bits at 63MHz, resolution 16ns, wraps every 4398046511096ns
66:[    0.013702] Console: colour dummy device 80x25
76:[    0.084398] smp: Brought up 1 node, 1 CPU
84:[    0.131928] Memory: 434352K/524288K available (18752K kernel code, 5770K rwdata, 13816K rodata, 3264K init, 691K bss, 55392K reserved, 32768K cma-reserved)
85:[    0.162523] devtmpfs: initialized
106:[    0.316530] printk: console [ttyAMA0] enabled
129:[    0.407997] clocksource: Switched to clocksource arch_sys_counter
151:[    0.463933] Unpacking initramfs...
166:[    0.638242] Freeing initrd memory: 1180K
246:[    1.211320] Freeing unused kernel memory: 3264K
247:[    1.212089] Run /init as init process
```

출력에서 볼 것:

- 1행 "Booting Linux on physical CPU"는 `start_kernel()` 앞부분의 `smp_setup_processor_id()`가 찍는다. 5행 "Machine model"은 `setup_machine_fdt()`, 36행 "Kernel command line"은 `start_kernel()`의 `pr_notice()`다.
- **64행까지 타임스탬프가 전부 0.000000**이다. printk 시간은 `sched_clock`에서 오는데, 그게 `time_init()`(arch timer 등록, 63~65행) 전에는 없다. "0.000000 구간은 타이머 이전"이라는 사실 하나로 로그를 읽을 때 위치를 잡을 수 있다.
- 66행 "Console: colour dummy device"는 `console_init()`이 부른 VT 더미 콘솔이다. **하지만 시리얼 콘솔(`ttyAMA0`)은 106행(0.316초)에서야 켜진다.** PL011 드라이버가 initcall 단계에서 probe되어야 하기 때문이다. 그 전에 찍힌 105줄은 링 버퍼에 쌓여 있다가 콘솔이 켜질 때 한꺼번에 출력(replay)된 것이다 — §5의 핵심.
- 84행 `Memory: … 3264K init …` 의 3264K가 246행 "Freeing unused kernel memory: 3264K"와 같은 값이다(§9).
- 85행 "devtmpfs: initialized"는 `do_basic_setup()` → `driver_init()`이다. 여기부터 initcall이 돈다(§7).

### 4.4 `rest_init()` — PID 1을 먼저, PID 2를 다음에

```sh
sed -n 672,684p init/main.c; sed -n 695,697p init/main.c
```

```text
static noinline void __ref __noreturn rest_init(void)
{
	struct task_struct *tsk;
	int pid;

	rcu_scheduler_starting();
	/*
	 * We need to spawn init first so that it obtains pid 1, however
	 * the init task will end up wanting to create kthreads, which, if
	 * we schedule it before we create kthreadd, will OOPS.
	 */
	pid = user_mode_thread(kernel_init, NULL, CLONE_FS);
	/*
	numa_default_policy();
	pid = kernel_thread(kthreadd, NULL, NULL, CLONE_FS | CLONE_FILES);
	rcu_read_lock();
```

`kernel_init`은 처음엔 커널 스레드처럼 커널 코드를 돌지만, `user_mode_thread()`로 만들어져서 나중에 `execve("/init")`하면 그대로 유저 프로세스 PID 1이 된다. 그다음 `kthreadd`(PID 2, 모든 커널 스레드의 부모)를 만들고, 부트 CPU의 현재 흐름은 `cpu_startup_entry()`로 들어가 **idle 태스크(PID 0)** 가 된다. RTOS로 치면 "첫 태스크 생성 → 스케줄러 시작, `main()`의 흐름은 idle 훅이 됨"이다. A2(프로세스)에서 본 `ps`의 PID 1, `[kthreadd]` PID 2가 여기서 태어난다.

---

## 5. 콘솔 — earlycon vs console, "아무것도 안 나와요"의 정체

### 5.1 직관 — 로그는 항상 쌓이고 있다, 보여 줄 창구가 없을 뿐

`printk()`는 **링 버퍼에 쓰는 함수**다. 화면(시리얼)에 내보내는 일은 등록된 **console 드라이버**가 한다. 커널은 부팅 첫 명령부터 링 버퍼에 로그를 쌓지만, 시리얼 console은 PL011 드라이버가 initcall 단계에서 등록·probe된 다음(§4.3 예제 8에서 0.316초)에야 생긴다. 그 순간 버퍼에 쌓인 앞의 로그를 한꺼번에 흘려보낸다(replay).

그래서 **콘솔 등록 전에 커널이 죽으면 아무것도 안 보인다.** 로그는 RAM 속 버퍼에 있지만 나올 창구가 없다. Don의 펌웨어에 비유하면 "UART 드라이버 init 전에 hard fault가 났고, 로그는 RAM 링버퍼에만 있다"는 상황이다. 펌웨어에서는 그래서 리셋 직후 레지스터 몇 개만 써서 문자를 찍는 "raw putc"를 따로 두었을 것이다. 그것이 **earlycon**이다.

| | earlycon (bootconsole) | console (정식) |
|---|---|---|
| 켜지는 시점 | `setup_arch()` 안 `parse_early_param()` — 거의 첫 줄부터 | 드라이버 probe 후 (`register_console()`) |
| 구현 | UART 레지스터에 폴링으로 직접 쓰기. 인터럽트·클럭 프레임워크·DMA 없음 | 정식 tty/serial 드라이버 |
| 지정법 | `earlycon=pl011,0x9000000` (드라이버, MMIO 주소), 또는 인자 없이 `earlycon` → DT `/chosen/stdout-path` 사용 | `console=ttyAMA0,115200` (여러 개 가능, 마지막이 `/dev/console`) 또는 DT `stdout-path` |
| 끝나는 시점 | 정식 console이 등록되면 자동 해제 ("bootconsole [pl11] disabled"). `keep_bootcon`으로 유지 가능 | 계속 |
| 사용자 공간 | 연결 안 됨 | `/dev/console` → init의 stdin/stdout/stderr |
| 32비트 ARM의 사촌 | `earlyprintk` (`CONFIG_DEBUG_LL`, 컴파일 시 주소 고정) | 같음 |

`earlycon`이 받는 형식은 커널 문서 `Documentation/admin-guide/kernel-parameters.txt`에 드라이버별로 나와 있다(PL011은 `pl011,<addr>` 또는 `pl011,mmio32,<addr>`). 핸들러는 `drivers/tty/serial/earlycon.c:249`의 `early_param("earlycon", param_setup_earlycon)`이다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="340" y="20" text-anchor="middle" font-size="13" font-weight="bold">earlycon=pl011,0x9000000 console=ttyAMA0 부팅 (실측 시각)</text>
  <text x="14" y="56" font-size="12">printk 링 버퍼</text>
  <rect x="150" y="42" width="500" height="20" fill="#888" fill-opacity="0.3" stroke="#888"/>
  <text x="14" y="96" font-size="12">earlycon (pl11)</text>
  <rect x="150" y="82" width="182" height="20" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
  <text x="14" y="136" font-size="12">console ttyAMA0</text>
  <rect x="331" y="122" width="319" height="20" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
  <text x="14" y="176" font-size="12">PID 1 stdout</text>
  <rect x="616" y="162" width="34" height="20" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
  <line x1="150" y1="200" x2="650" y2="200" stroke="currentColor"/>
  <line x1="150" y1="200" x2="150" y2="206" stroke="currentColor"/>
  <text x="150" y="220" text-anchor="middle" font-size="12">0</text>
  <line x1="258" y1="200" x2="258" y2="206" stroke="currentColor"/>
  <text x="258" y="220" text-anchor="middle" font-size="12">0.3</text>
  <line x1="365" y1="200" x2="365" y2="206" stroke="currentColor"/>
  <text x="365" y="220" text-anchor="middle" font-size="12">0.6</text>
  <line x1="473" y1="200" x2="473" y2="206" stroke="currentColor"/>
  <text x="473" y="220" text-anchor="middle" font-size="12">0.9</text>
  <line x1="580" y1="200" x2="580" y2="206" stroke="currentColor"/>
  <text x="580" y="220" text-anchor="middle" font-size="12">1.2 s</text>
  <line x1="331" y1="112" x2="331" y2="121" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="338" y="116" font-size="12">0.47 s 인계 (버퍼 재생)</text>
  <text x="20" y="252" font-size="12">earlycon이 없으면: 0.47 s 이전에 멈춘 부팅은 화면에 아무것도 남기지 않는다 (로그는 RAM에만 있다).</text>
  <text x="20" y="274" font-size="12">PID 1 출력은 /dev/console을 통해 정식 console로만 나간다 — earlycon은 유저 공간과 무관하다.</text>
</svg>
```
그림 4 — 링 버퍼, earlycon, 정식 console, 유저 공간 출력의 시간 관계. 시각은 아래 예제 10의 실측(earlycon 0초, ttyAMA0 등록 0.470초, bootconsole 해제 0.475초)과 기본 부팅의 "Run /init" 1.21초다.

### 5.2 예제 9 — 진짜 "아무것도 안 나와요" 만들기

무엇을 확인하나: QEMU `virt`의 DT에는 `/chosen/stdout-path = "/pl011@9000000"`이 있어서, `console=`을 빼도 콘솔이 자동으로 잡힌다. 그 줄을 지운 DTB로 부팅하면 현실의 "DT도 bootargs도 콘솔을 말하지 않는 보드"가 된다.

```sh
cd /var/tmp/C6
grep -n "chosen" -A3 /var/lab/out/virt.dts
sed "/stdout-path/d" /var/lab/out/virt.dts > c6_nostdout.dts
dtc -q -I dts -O dtb -o c6_nostdout.dtb c6_nostdout.dts
R=~/.elinux_scratch/C6/c6_run.sh; L=/var/lab/out
$R noconsole 40 "LAB_CMD=echo~hello" -initrd $L/initramfs.cpio.gz          # 원래 DTB, console= 없음
$R silent    40 "LAB_CMD=echo~hello" -initrd $L/initramfs.cpio.gz -dtb c6_nostdout.dtb
```

```text
388:	chosen {
389-		stdout-path = "/pl011@9000000";
390-		rng-seed = <0xf0d4837c 0xfd3f0c55 0xb5d8f04e 0x3234f3a1 0xb647931a 0x6f29cd0c 0x3efea411 0xccaf5ca2>;
391-		kaslr-seed = <0x2b1021cd 0x73a48fc0>;
exit=0 lines=253
exit=0 lines=0
```

출력에서 볼 것: 원래 DTB는 `console=` 없이도 253줄이 나온다(`stdout-path` 덕분). `stdout-path`를 지운 DTB는 **0줄**이다. 커널은 정상 부팅하고 `hello`를 출력하고 전원까지 껐다(QEMU가 timeout이 아니라 exit 0으로 끝났다). 하지만 시리얼에는 한 글자도 없다. "보드가 죽었다"고 오해하기 딱 좋은 상황이다. (`-dtb`를 줘도 QEMU는 `/chosen`의 `bootargs`와 initrd 위치는 채워 넣는다.)

### 5.3 예제 10 — earlycon으로 살리기, 그리고 tty0 함정

무엇을 확인하나: 같은 "침묵" 구성에 `earlycon=pl011,0x9000000`만 추가한다. 그리고 `console=ttyAMA0`까지 더하면 어떻게 달라지는지 본다.

```sh
$R early  40 "earlycon=pl011,0x9000000 LAB_CMD=echo~hello" -initrd $L/initramfs.cpio.gz -dtb c6_nostdout.dtb
sed -n "1,8p;64,70p" c6_early.log
```

```text
exit=0 lines=70
[    0.000000] Booting Linux on physical CPU 0x0000000000 [0x411fd070]
[    0.000000] Linux version 6.18.55 (donh@lima-elinux) (gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0, GNU ld (GNU Binutils for Ubuntu) 2.46) #1 SMP PREEMPT Sat Oct 10 10:05:19 PDT 2026
[    0.000000] KASLR enabled
[    0.000000] random: crng init done
[    0.000000] Machine model: linux,dummy-virt
[    0.000000] earlycon: pl11 at MMIO 0x0000000009000000 (options '')
[    0.000000] printk: legacy bootconsole [pl11] enabled
[    0.000000] efi: UEFI not found.
[    0.000000] clocksource: jiffies: mask: 0xffffffff max_cycles: 0xffffffff, max_idle_ns: 7645041785100000 ns
[    0.000000] arch_timer: cp15 timer running at 62.50MHz (virt).
[    0.000000] clocksource: arch_sys_counter: mask: 0x1ffffffffffffff max_cycles: 0x1cd42e208c, max_idle_ns: 881590405314 ns
[    0.000088] sched_clock: 57 bits at 63MHz, resolution 16ns, wraps every 4398046511096ns
[    0.009100] Console: colour dummy device 80x25
[    0.009540] printk: legacy console [tty0] enabled
[    0.010027] printk: legacy bootconsole [pl11] disabled
```

출력에서 볼 것:

- earlycon은 "Machine model" 바로 다음, **"Kernel command line:" 줄보다도 먼저** 켜진다. arm64는 `setup_arch()` 안에서 `parse_early_param()`을 부르기 때문이다(§4.2). 그 전의 다섯 줄도 보이는 이유는 earlycon 콘솔이 `CON_PRINTBUFFER | CON_BOOT` 플래그로 등록되어(`earlycon.c:31`) 등록 순간 버퍼 앞부분을 재생하기 때문이다.
- 그런데 **70줄에서 끊긴다.** `console=`이 없으니 커널은 기본 console로 VT의 `tty0`(화면 없는 더미 장치)를 켜고, "정식 console이 생겼다"며 earlycon을 꺼 버린다. 출력이 화면 없는 tty0로 가 버린 것이다. 실제 보드에서 `CONFIG_VT=y`인 커널을 쓰면 정확히 이렇게 "앞부분 조금 나오다 멈춘다". 커널이 멈춘 게 아니다.

이번엔 `console=ttyAMA0`을 함께 준다.

```sh
$R early2 40 "earlycon=pl011,0x9000000 console=ttyAMA0 LAB_CMD=echo~hello" -initrd $L/initramfs.cpio.gz -dtb c6_nostdout.dtb
grep -n "bootcon\|console \[\|hello" c6_early2.log
```

```text
exit=0 lines=257
7:[    0.000000] printk: legacy bootconsole [pl11] enabled
38:[    0.000000] Kernel command line: earlycon=pl011,0x9000000 console=ttyAMA0 LAB_CMD=echo~hello
39:[    0.000000] Unknown kernel command line parameters "LAB_CMD=echo~hello", will be passed to user space.
108:[    0.469904] printk: console [ttyAMA0] enabled
109:[    0.469904] printk: console [ttyAMA0] enabled
110:[    0.474544] printk: legacy bootconsole [pl11] disabled
111:[    0.474544] printk: legacy bootconsole [pl11] disabled
254:hello
```

출력에서 볼 것: earlycon(0초) → ttyAMA0 등록(0.4699초) → bootconsole 해제(0.4745초)로 깔끔하게 인계되고, init의 `hello`까지 나온다. 108~111행이 두 번씩 찍힌 것은 인계 순간 같은 메시지가 earlycon과 ttyAMA0 **양쪽으로** 나갔기 때문이다(같은 UART에 두 드라이버가 쓰는 짧은 구간). 이 부팅이 그림 4다.

### 5.4 예제 11 — 콘솔 드라이버가 끝내 안 뜰 때: earlycon만이 보여 준다

무엇을 확인하나: 실무에서 더 흔한 장면 — DT의 UART 노드가 틀렸거나(compatible 오타, clock 누락) 드라이버가 빌드에서 빠져 **정식 console이 영영 등록되지 않는** 경우. `initcall_blacklist=pl011_init`으로 PL011 드라이버 등록을 막아 재현한다(`pl011_init`은 `arch_initcall`이다 — §7).

```sh
$R bl  40 "console=ttyAMA0 initcall_blacklist=pl011_init LAB_CMD=echo~hello" -initrd $L/initramfs.cpio.gz
$R bl2 40 "earlycon console=ttyAMA0 initcall_blacklist=pl011_init LAB_CMD=echo~hello" -initrd $L/initramfs.cpio.gz
grep -n "bootcon\|console \[\|hello\|Warning" c6_bl2.log; tail -3 c6_bl2.log
```

```text
exit=0 lines=0
exit=0 lines=250
8:[    0.000000] printk: legacy bootconsole [pl11] enabled
38:[    0.000000] Kernel command line: earlycon console=ttyAMA0 initcall_blacklist=pl011_init LAB_CMD=echo~hello
39:[    0.000000] Unknown kernel command line parameters "LAB_CMD=echo~hello", will be passed to user space.
245:[    2.322630] Warning: unable to open an initial console.
[    2.729026] Flash device refused suspend due to active operation (state 20)
[    2.729678] Flash device refused suspend due to active operation (state 20)
[    2.730536] reboot: Power down
```

출력에서 볼 것:

- earlycon 없이: **0줄.** `console=ttyAMA0`을 줬지만 ttyAMA0을 만드는 드라이버가 없다.
- 인자 없는 `earlycon`(DT `stdout-path`에서 UART를 찾는다)을 더하면 250줄 전부 보인다. 정식 console이 끝내 안 생기니 bootconsole이 끝까지 남아 있다. 그리고 결정적 단서가 245행에 있다: **"Warning: unable to open an initial console."** — `console_on_rootfs()`가 `/dev/console`을 열지 못했다는 뜻이다. 그래서 init의 `hello`는 어디에도 안 나온다. 커널 로그는 보이는데 셸 프롬프트가 안 나오는 보드라면 이 줄부터 찾는다.

> 진단 순서: (1) `earlycon`을 넣는다(DT에 `stdout-path`가 있으면 인자 없이, 없으면 `earlycon=<드라이버>,<주소>`). (2) 아무것도 안 나오면 커널에 도달하지 못했거나 `setup_arch` 이전에 죽은 것 — 부트로더·DTB 주소·Image 로드 주소를 의심(§2). (3) earlycon은 나오는데 어느 지점에서 끊기면, 그 앞뒤 메시지가 tty0 인계인지 진짜 hang인지 본다. (4) 커널 로그는 다 나오는데 셸이 없으면 "unable to open an initial console"과 `console=` 이름을 확인한다.

### 5.5 `printk.time`과 로그 레벨

타임스탬프는 `CONFIG_PRINTK_TIME=y`(랩 커널은 켜져 있음)가 기본값이고, 커맨드라인 `printk.time=0/1` 또는 런타임 `/sys/module/printk/parameters/time`으로 바꾼다. `quiet`는 `console_loglevel`을 `CONFIG_CONSOLE_LOGLEVEL_QUIET`(랩 커널 4)로 낮추는 `early_param`이다. 콘솔에는 레벨이 그보다 작은 메시지(KERN_ERR 3 이하, 즉 error 이상)만 나가고, 로그 버퍼에는 다 남아 `dmesg`로 볼 수 있다. 부팅 시간 측정에는 `printk.time=1`을, 콘솔 출력이 부팅을 느리게 하는 115200 baud 보드에서는 `quiet`를 쓴다(C7).

```sh
$R ptime 40 "console=ttyAMA0 printk.time=0 LAB_CMD=true" -initrd $L/initramfs.cpio.gz; head -3 c6_ptime.log
```

```text
exit=0 lines=250
Booting Linux on physical CPU 0x0000000000 [0x411fd070]
Linux version 6.18.55 (donh@lima-elinux) (gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0, GNU ld (GNU Binutils for Ubuntu) 2.46) #1 SMP PREEMPT Sat Oct 10 10:05:19 PDT 2026
KASLR enabled
```

출력에서 볼 것: `[    0.000000]` 접두어가 사라졌다. `printk.time`은 built-in "printk" 코드의 **모듈 파라미터**(`module_param_named(time, printk_time, …)`, `kernel/printk/printk.c:1346`)라서 `모듈이름.파라미터` 형식으로 커맨드라인에서 바뀐다 — §6에서 정리한다.

---

## 6. 커널 커맨드라인 — 어디서 와서 누가 읽나

### 6.1 직관

커맨드라인은 커널의 **부팅 설정값 문자열**이다. 펌웨어의 "NVRAM 설정 블록"이나 "빌드 플래그"를 런타임에 바꿀 수 있게 한 것이라고 보면 된다. 재빌드 없이 콘솔 위치, root 장치, 디버그 옵션을 바꾼다. 출처는 세 가지다.

| 출처 | 어떻게 | 비고 |
|---|---|---|
| DT `/chosen/bootargs` | U-Boot가 `bootargs` 환경변수를 DTB에 써 넣는다 (QEMU는 `-append`를 써 넣는다) | 임베디드의 표준 경로. `early_init_dt_scan_chosen()`(`drivers/of/fdt.c`)이 읽는다 |
| `CONFIG_CMDLINE` | 커널에 내장된 기본값. `CONFIG_CMDLINE_FORCE`면 부트로더 것을 무시, `CONFIG_CMDLINE_EXTEND`면 뒤에 붙임 | 랩 커널은 `CONFIG_CMDLINE=""` |
| UEFI / bootconfig | EFI stub이 UEFI 로드 옵션에서, 또는 initrd에 붙인 bootconfig | C8 |

### 6.2 세 가지 등록 방법 — `early_param`, `__setup`, 모듈 파라미터

커널 코드가 커맨드라인 인자를 받는 방법은 세 가지다. 랩 소스에서 실제 등록 지점을 grep하면:

```sh
cd /var/lab/src/linux-6.18.55
grep -rn -E "^(early_param|__setup|core_param)\(\"?(earlycon|console=|root=|rootwait|init=|rdinit=|quiet|loglevel|panic|initcall_debug|initcall_blacklist=)" \
     init/ kernel/panic.c kernel/printk/printk.c drivers/tty/serial/earlycon.c
```

```text
init/main.c:247:early_param("quiet", quiet_kernel);
init/main.c:266:early_param("loglevel", loglevel);
init/main.c:580:__setup("init=", init_setup);
init/main.c:592:__setup("rdinit=", rdinit_setup);
init/main.c:781:core_param(initcall_debug, initcall_debug, bool, 0644);
init/main.c:1168:__setup("initcall_blacklist=", initcall_blacklist);
init/do_mounts.c:69:__setup("root=", root_dev_setup);
init/do_mounts.c:79:__setup("rootwait", rootwait_setup);
init/do_mounts.c:104:__setup("rootwait=", rootwait_timeout_setup);
kernel/panic.c:228:__setup("panic_sys_info=", setup_panic_sys_info);
kernel/panic.c:1000:core_param(panic, panic_timeout, int, 0644);
kernel/panic.c:1002:core_param(panic_on_warn, panic_on_warn, int, 0644);
kernel/panic.c:1004:core_param(panic_console_replay, panic_console_replay, bool, 0644);
kernel/panic.c:1058:early_param("panic_on_taint", panic_on_taint_setup);
kernel/printk/printk.c:2636:__setup("console=", console_setup);
drivers/tty/serial/earlycon.c:249:early_param("earlycon", param_setup_earlycon);
```

그리고 매크로 정의(`include/linux/init.h`):

```sh
sed -n "317,321p;331,338p;344,354p" include/linux/init.h
```

```text
struct obs_kernel_param {
	const char *str;
	int (*setup_func)(char *);
	int early;
};
#define __setup_param(str, unique_id, fn, early)			\
	static const char __setup_str_##unique_id[] __initconst		\
		__aligned(1) = str; 					\
	static struct obs_kernel_param __setup_##unique_id		\
		__used __section(".init.setup")				\
		__aligned(__alignof__(struct obs_kernel_param))		\
		= { __setup_str_##unique_id, fn, early }

#define __setup(str, fn)						\
	__setup_param(str, fn, fn, 0)

/*
 * NOTE: @fn is as per module_param, not __setup!
 * I.e., @fn returns 0 for no error or non-zero for error
 * (possibly @fn returns a -errno value, but it does not matter).
 * Emits warning if @fn returns non-zero.
 */
#define early_param(str, fn)						\
	__setup_param(str, fn, fn, 1)
```

B3 §11에서 initcall을 흉내 냈던 바로 그 패턴이다. `__setup("root=", root_dev_setup)`은 `{ "root=", root_dev_setup, 0 }` 구조체 하나를 **`.init.setup` 섹션**에 떨어뜨린다. 링커가 이것들을 모아 `__setup_start`~`__setup_end` 배열을 만들고, 파서가 배열을 돌며 문자열을 비교한다. 이 커널에는 249개가 있다(아래 예제 13에서 계산).

| 방법 | 언제 처리 | 반환값 의미 | 예 |
|---|---|---|---|
| `early_param("x", fn)` | `parse_early_param()` — arm64는 `setup_arch()` 안, 메모리 할당기 전 | 0 = 성공 | `earlycon`, `mem`, `loglevel`, `quiet` |
| `__setup("x=", fn)` | `start_kernel()`의 `parse_args("Booting kernel", …)` | 1 = 처리함, 0 = 처리 못 함(init에 넘김) | `console=`, `root=`, `init=`, `rdinit=`, `rootwait` |
| `module_param` / `core_param` | `parse_args()` — built-in이면 `모듈.파라미터=`, `core_param`은 접두어 없이. 일부는 initcall 레벨별로 다시 파싱 | `/sys/module/<이름>/parameters/`로 런타임에도 보임 | `printk.time`, `initcall_debug`, `panic` |

주의할 반환값 함정: `__setup` 핸들러는 **1을 반환해야 "처리함"** 이다. 0을 반환하면 커널은 이 인자를 모르는 것으로 보고 init에 넘긴다. `early_param`은 반대로 0이 성공이다(위 주석 "NOTE: @fn is as per module_param, not __setup!").

### 6.3 예제 12 — 커널이 모르는 인자는 어디로 가나

무엇을 확인하나: 커널이 처리하지 않은 인자가 PID 1의 **argv**와 **환경변수**로 넘어가는 규칙, `--` 뒤 인자의 처리, `/proc/cmdline`을 본다.

```sh
$R cmdl 40 "console=ttyAMA0 quiet c6_env=hello c6_word c6mod.param=1 printk.time=0 LAB_CMD=echo~ARGV:;xargs~-0~-n1~</proc/1/cmdline;echo~ENV:;xargs~-0~-n1~</proc/1/environ;echo~CMDLINE:;cat~/proc/cmdline;echo~printk_time=\$(cat~/sys/module/printk/parameters/time) -- c6_after1 c6_after2" \
   -initrd $L/initramfs.cpio.gz
cat c6_cmdl.log
```

```text
exit=0 lines=16
lab initramfs: 6.18.55 up 2.07s
ARGV:
/bin/sh
/init
c6_word
c6_after1
c6_after2
ENV:
HOME=/
TERM=linux
c6_env=hello
LAB_CMD=echo~ARGV:;xargs~-0~-n1~</proc/1/cmdline;echo~ENV:;xargs~-0~-n1~</proc/1/environ;echo~CMDLINE:;cat~/proc/cmdline;echo~printk_time=$(cat~/sys/module/printk/parameters/time)
CMDLINE:
console=ttyAMA0 quiet c6_env=hello c6_word c6mod.param=1 printk.time=0 LAB_CMD=echo~ARGV:;xargs~-0~-n1~</proc/1/cmdline;echo~ENV:;xargs~-0~-n1~</proc/1/environ;echo~CMDLINE:;cat~/proc/cmdline;echo~printk_time=$(cat~/sys/module/printk/parameters/time) -- c6_after1 c6_after2
printk_time=N
reboot: Power down
```

출력에서 볼 것 — 규칙 네 개가 한 번에 보인다.

1. **`이름=값` 형태의 모르는 인자 → init의 환경변수** (`c6_env=hello`, `LAB_CMD=…`). 랩의 `LAB_CMD` 장치가 바로 이 규칙을 이용한다. 기본 환경 `HOME=/`, `TERM=linux`는 커널이 넣는다.
2. **`=` 없는 모르는 인자 → init의 argv** (`c6_word`). `/init`이 셸 스크립트라 argv[0]이 `/bin/sh`, argv[1]이 `/init`이다.
3. **점(`.`)이 들어간 모르는 인자(`c6mod.param=1`)는 어디에도 안 간다.** 커널은 "아직 로드 안 된 모듈의 파라미터"라고 보고 init에 넘기지 않는다. (`modprobe`는 나중에 `/proc/cmdline`을 직접 읽어 `모듈.파라미터`를 적용한다.)
4. **`--` 뒤는 커널이 해석하지 않고 전부 init의 argv로** 간다 (`c6_after1 c6_after2`).

`/proc/cmdline`은 원문 그대로다. 그리고 `printk.time=0`이 `/sys/module/printk/parameters/time` = `N`으로 반영되어, 마지막 "reboot: Power down"에 타임스탬프가 없다. 기본 부팅(예제 8)의 로그에는 `Unknown kernel command line parameters "LAB_CMD=cat~/proc/cmdline", will be passed to user space.`라는 줄이 있었다 — `print_unknown_bootoptions()`가 찍는 이 줄이 오타 난 커널 인자를 찾는 가장 빠른 단서다(`consloe=ttyS0` 같은 오타가 여기 나온다).

---

## 7. initcall — 링커 섹션으로 만든 초기화 함수 표

### 7.1 직관 — Don이 이미 아는 패턴

펌웨어에서 드라이버가 늘어나면 `main()`에 `uart_init(); nand_init(); pcie_init(); …`를 손으로 나열하는 게 고통이 된다. 그래서 흔히 쓰는 기법이 "각 드라이버 파일이 함수 포인터를 특정 링커 섹션에 넣고, 링커 스크립트가 섹션을 모아 배열을 만들고, `main()`은 배열을 돈다"이다. B3 §11 예제 23에서 유저 공간으로 직접 만들어 봤다. **커널 initcall이 정확히 이것**이다. 다만 순서 문제(타이머보다 먼저 버스가, 버스보다 먼저 클럭이 있어야 한다)를 풀기 위해 **레벨**을 둔다.

### 7.2 매크로와 레벨

```sh
sed -n "283p;292,310p" include/linux/init.h
```

```text
#define early_initcall(fn)		__define_initcall(fn, early)
#define pure_initcall(fn)		__define_initcall(fn, 0)

#define core_initcall(fn)		__define_initcall(fn, 1)
#define core_initcall_sync(fn)		__define_initcall(fn, 1s)
#define postcore_initcall(fn)		__define_initcall(fn, 2)
#define postcore_initcall_sync(fn)	__define_initcall(fn, 2s)
#define arch_initcall(fn)		__define_initcall(fn, 3)
#define arch_initcall_sync(fn)		__define_initcall(fn, 3s)
#define subsys_initcall(fn)		__define_initcall(fn, 4)
#define subsys_initcall_sync(fn)	__define_initcall(fn, 4s)
#define fs_initcall(fn)			__define_initcall(fn, 5)
#define fs_initcall_sync(fn)		__define_initcall(fn, 5s)
#define rootfs_initcall(fn)		__define_initcall(fn, rootfs)
#define device_initcall(fn)		__define_initcall(fn, 6)
#define device_initcall_sync(fn)	__define_initcall(fn, 6s)
#define late_initcall(fn)		__define_initcall(fn, 7)
#define late_initcall_sync(fn)		__define_initcall(fn, 7s)

#define __initcall(fn) device_initcall(fn)
```

그리고 `include/linux/module.h`에서 built-in일 때 `#define module_init(x)	__initcall(x);` — 즉 **built-in 드라이버의 `module_init()`은 `device_initcall`(레벨 6)** 이다. 모듈(`.ko`)로 빌드하면 같은 매크로가 "insmod 때 부를 함수"로 바뀐다(§9 예제 19).

| 레벨 | 매크로 | 이름(`initcall_level_names`) | 무엇을 하나 (이 커널 `System.map`에서 찾은 실제 예) |
|---|---|---|---|
| early | `early_initcall` | (early) | SMP 시작 전. `do_pre_smp_initcalls()`가 따로 돈다. 모듈 불가 |
| 0 | `pure_initcall` | pure | 의존성 없는 변수 초기화 |
| 1 | `core_initcall` | core | 핵심 인프라: `sock_init`, `gpiolib_dev_init`, `regulator_init` |
| 2 | `postcore_initcall` | postcore | 버스 타입 등록: `i2c_init`, `spi_init`, `amba_init`, `pci_driver_init` |
| 3 | `arch_initcall` | arch | 플랫폼: `of_platform_default_populate_init`(DT 노드 → platform_device, `_sync`), `pl011_init`, `dma_bus_init` |
| 4 | `subsys_initcall` | subsys | 서브시스템 코어: `usb_init`, `mmc_init`, `input_init`, `net_dev_init` |
| 5 | `fs_initcall` | fs | `clocksource_done_booting`, `init_pipe_fs`, `chr_dev_init` |
| rootfs | `rootfs_initcall` | (rootfs) | `populate_rootfs` — initramfs 풀기 (§8) |
| 6 | `device_initcall` = `module_init` | device | **대부분의 드라이버**: `virtio_blk_init`, `serial8250_init`, 그리고 `ext4_init_fs`(파일시스템이지만 `module_init`이라 여기) |
| 7 | `late_initcall` | late | 마무리. `deferred_probe_initcall` |

`_sync` 변형(`3s` 등)은 같은 레벨의 일반 initcall이 다 끝난 뒤에 돈다. 같은 레벨 안의 순서는 **링크 순서**(Makefile의 `obj-y` 순서, 디렉터리 순서)다 — 순서에 기대는 코드는 깨지기 쉽다는 뜻이다.

### 7.3 예제 13 — `vmlinux`에서 initcall 표 직접 보기

무엇을 확인하나: 매크로가 만든 엔트리가 `vmlinux`의 어느 섹션에 있고, 레벨 경계 심볼이 어떻게 배치되는지 `readelf`와 `System.map`으로 본다.

```sh
cd /var/lab/build/linux
readelf -S -W vmlinux | grep -E "Nr\]|\.init\.text|\.init\.data|\.exit\.text"
grep -E " __initcall(_start|0_start|1_start|2_start|3_start|4_start|5_start|rootfs_start|6_start|7_start|_end)$| __setup_(start|end)$" System.map
readelf -s -W vmlinux | grep -E "__initcall__kmod_(amba_pl011|virtio_blk|initramfs|dd)_"
```

```text
  [Nr] Name              Type            Address          Off    Size   ES Flg Lk Inf Al
  [16] .init.text        PROGBITS        ffff800081ff0000 2000000 08ef28 00  AX  0   0 32
  [17] .exit.text        PROGBITS        ffff80008207ef28 208ef28 00bac4 00  AX  0   0  4
  [19] .init.data        PROGBITS        ffff8000820f8000 2108000 13c027 00  WA  0   0 4096
ffff8000822308f0 D __setup_start
ffff800082232048 D __initcall_start
ffff800082232048 D __setup_end
ffff8000822320e4 D __initcall0_start
ffff800082232100 D __initcall1_start
ffff800082232318 D __initcall2_start
ffff8000822323f8 D __initcall3_start
ffff8000822325fc D __initcall4_start
ffff8000822328d4 D __initcall5_start
ffff8000822329b8 D __initcallrootfs_start
ffff8000822329bc D __initcall6_start
ffff800082233c38 D __initcall7_start
ffff800082233dc4 D __initcall_end
 19925: ffff8000822329b8     0 NOTYPE  LOCAL  DEFAULT   19 __initcall__kmod_initramfs__742_791_populate_rootfsrootfs
142321: ffff8000822325cc     0 NOTYPE  LOCAL  DEFAULT   19 __initcall__kmod_amba_pl011__747_3167_pl011_init3
145287: ffff800082233d48     0 NOTYPE  LOCAL  DEFAULT   19 __initcall__kmod_dd__614_375_deferred_probe_initcall7
146757: ffff8000822334f8     0 NOTYPE  LOCAL  DEFAULT   19 __initcall__kmod_virtio_blk__708_1713_virtio_blk_init6
```

출력에서 볼 것:

- `.initcall6.init` 같은 입력 섹션은 최종 `vmlinux`에서 **출력 섹션 `.init.data`(19번)** 안에 합쳐진다. 엔트리 심볼들의 섹션 번호가 19다. 링커 스크립트의 `INIT_CALLS` 매크로(B3 §11.3)가 레벨 순서대로 `KEEP(*(.initcallN.init))`을 나열한 결과다.
- 엔트리 심볼 이름이 출처를 다 말해 준다: `__initcall__kmod_<파일>__<id>_<줄번호>_<함수><레벨>`. `pl011_init3` = `amba-pl011.c` 3167행의 `arch_initcall(pl011_init)`, `virtio_blk_init6` = 1713행의 `module_init`, `populate_rootfsrootfs` = `init/initramfs.c` 791행의 `rootfs_initcall(populate_rootfs)`, `deferred_probe_initcall7` = `drivers/base/dd.c` 375행의 `late_initcall`. 그리고 주소가 각 레벨 경계 안에 들어 있다(예: pl011 엔트리 `…25cc`는 `__initcall3_start` `…23f8` ~ `__initcall4_start` `…25fc` 사이).
- `__initcallrootfs_start`에서 `__initcall6_start`까지는 **4바이트 = 엔트리 1개**(`populate_rootfs`)뿐이다.
- 이 커널은 `CONFIG_HAVE_ARCH_PREL32_RELOCATIONS=y`라서 엔트리가 8바이트 포인터가 아니라 **4바이트 상대 오프셋**이다(B3에서 본 것과 같다). 계산하면:

```sh
python3 -c '
m={l.split()[2]:int(l.split()[0],16) for l in open("/var/lab/build/linux/System.map")}
print("init region  = %dK" % ((m["__init_end"]-m["__init_begin"])//1024))
print("__setup/early_param entries = %d (24 B each)" % ((m["__setup_end"]-m["__setup_start"])//24))
print("initcall entries = %d (4 B each, PREL32)" % ((m["__initcall_end"]-m["__initcall_start"])//4))'
```

```text
init region  = 3264K
__setup/early_param entries = 249 (24 B each)
initcall entries = 1887 (4 B each, PREL32)
```

`struct obs_kernel_param`은 포인터 2개 + int(패딩 포함 24바이트)라서 `__setup`/`early_param` 등록은 249개, initcall은 1,887개다. 첫 줄의 3264K는 §9에서 다시 나온다.

### 7.4 실행 엔진 — `do_initcalls()`

```sh
sed -n "1293,1322p" /var/lab/src/linux-6.18.55/init/main.c
```

```text
static void __init do_initcall_level(int level, char *command_line)
{
	initcall_entry_t *fn;

	parse_args(initcall_level_names[level],
		   command_line, __start___param,
		   __stop___param - __start___param,
		   level, level,
		   NULL, ignore_unknown_bootoption);

	do_trace_initcall_level(initcall_level_names[level]);
	for (fn = initcall_levels[level]; fn < initcall_levels[level+1]; fn++)
		do_one_initcall(initcall_from_entry(fn));
}

static void __init do_initcalls(void)
{
	int level;
	size_t len = saved_command_line_len + 1;
	char *command_line;

	command_line = kzalloc(len, GFP_KERNEL);
	if (!command_line)
		panic("%s: Failed to allocate %zu bytes\n", __func__, len);

	for (level = 0; level < ARRAY_SIZE(initcall_levels) - 1; level++) {
		/* Parser modifies command_line, restore it each time */
		strcpy(command_line, saved_command_line);
		do_initcall_level(level, command_line);
	}
```

이중 루프 하나가 전부다. 레벨마다 (1) 그 레벨에 묶인 모듈 파라미터를 커맨드라인에서 다시 파싱하고, (2) 엔트리 배열을 돌며 `do_one_initcall()`을 부른다. `do_one_initcall()`은 함수를 부르고 반환값을 **무시**한다(실패해도 부팅은 계속된다). 다만 initcall이 선점 카운트나 인터럽트 상태를 망가뜨리고 돌아오면 "initcall … returned with preemption imbalance" 경고를 찍고 복구한다. initcall 하나가 영원히 안 돌아오면? **부팅 전체가 거기서 멈춘다.** 같은 스레드(PID 1)에서 순서대로 부르기 때문이다 — 그래서 `initcall_debug`가 hang 위치 찾기의 1번 도구다.

### 7.5 예제 14 — `initcall_debug`로 측정하기

무엇을 확인하나: `initcall_debug`를 켜면 initcall마다 "calling"/"returned … after N usecs" 두 줄이 로그에 남는다. 출력이 많아 로그 버퍼를 키우고(`log_buf_len=4M`), 콘솔에는 `quiet`로 줄인 뒤, 부팅 후 `dmesg`를 통째로 콘솔로 내보낸다.

```sh
$R icd 90 "console=ttyAMA0 quiet initcall_debug log_buf_len=4M LAB_CMD=dmesg" -initrd $L/initramfs.cpio.gz
grep -c "calling" c6_icd.log; grep -m3 "calling\|returned" c6_icd.log
grep -n "entering initcall level\|Switched to clocksource\|Freeing unused\|Run /init" c6_icd.log
```

```text
exit=0 lines=4100
1888
[    0.102706] calling  cpu_suspend_init+0x0/0x54 @ 1
[    0.102929] initcall cpu_suspend_init+0x0/0x54 returned 0 after 0 usecs
[    0.105722] calling  prevent_bootmem_remove_init+0x0/0x4c @ 1
76:[    0.102289] entering initcall level: early
169:[    0.314576] entering initcall level: pure
184:[    0.321406] entering initcall level: core
462:[    0.415256] entering initcall level: postcore
583:[    0.480262] entering initcall level: arch
851:[    0.690601] entering initcall level: subsys
1239:[    0.826668] entering initcall level: fs
1247:[    0.829202] clocksource: Switched to clocksource arch_sys_counter
1376:[    0.901429] entering initcall level: device
3886:[    2.211742] entering initcall level: late
4092:[    2.499093] Freeing unused kernel memory: 3264K
4093:[    2.509416] Run /init as init process
```

("calling"이 1,888개로 엔트리 1,887개보다 하나 많다. 남는 한 줄은 `pci 0000:00:00.0: calling  quirk_mmio_always_on+0x0/0x10 @ 1` — `initcall_debug`는 PCI quirk 호출도 같은 형식으로 찍는다. 아래 스크립트는 "initcall … returned" 줄로 정확히 1,887개를 센다.)

레벨 경계 시각이 "entering initcall level" 줄로 나온다. 함수 이름만으로는 레벨을 알 수 없으니, `System.map`의 엔트리 주소를 레벨 경계와 비교해 함수 → 레벨 표를 만들고 로그와 합친다.

```python
# c6_icd.py <System.map> <boot log with initcall_debug>
import re, sys, collections
smap, log = sys.argv[1], sys.argv[2]
bounds, entries = {}, []
for line in open(smap):
    a, t, n = line.split()[:3]
    a = int(a, 16)
    if re.fullmatch(r'__initcall(early|0|1|2|3|4|5|rootfs|6|7)?_start|__initcall_end', n): bounds[n] = a
    m = re.fullmatch(r'__initcall__kmod_.+?__\d+_\d+_(\w+?)(early|0s?|1s?|2s?|3s?|4s?|5s?|rootfss?|6s?|7s?|con)', n)
    if m and m.group(2) != 'con': entries.append((a, m.group(1)))
entries.sort()
order = ['early', '0', '1', '2', '3', '4', '5', 'rootfs', '6', '7']
starts = [bounds['__initcall_start']] + [bounds[f'__initcall{l}_start'] for l in order[1:]] + [bounds['__initcall_end']]
def level(a):
    for i, l in enumerate(order):
        if starts[i] <= a < starts[i+1]: return l
cnt = collections.Counter(level(a) for a, _ in entries)
calls = re.findall(r'initcall (\w+)\+0x0/0x[0-9a-f]+ returned (-?\d+) after (\d+) usecs', open(log).read())
lev_of = collections.defaultdict(list)
for a, fn in entries: lev_of[fn].append(level(a))
tot = collections.Counter(); topn = []
for fn, ret, us in calls:
    l = lev_of[fn][0] if fn in lev_of else '?'
    tot[l] += int(us); topn.append((int(us), fn, l, int(ret)))
names = dict(zip(order, ['early', 'pure', 'core', 'postcore', 'arch', 'subsys', 'fs', 'rootfs', 'device', 'late']))
print(f"{'level':8} {'name':9} {'entries':>7} {'sum_ms':>8}")
for l in order: print(f"{l:8} {names[l]:9} {cnt[l]:7} {tot[l]/1000:8.1f}")
print("total entries", len(entries), " returned lines", len(calls), " unknown", tot['?'])
print("\nslowest 12:")
for us, fn, l, r in sorted(topn, reverse=True)[:12]: print(f"{us/1000:8.1f} ms  L{l:6} {fn}  ret={r}")
print("\nnonzero returns:", [(fn, r) for us, fn, l, r in topn if r][:8])
```

```sh
python3 -I ~/.elinux_scratch/C6/c6_icd.py /var/lab/build/linux/System.map /var/tmp/C6/c6_icd.log
```

```text
level    name      entries   sum_ms
early    early          39     24.0
0        pure            7      8.0
1        core          134     79.5
2        postcore       56     40.0
3        arch          129    168.0
4        subsys        182    108.0
5        fs             57     70.0
rootfs   rootfs          1      2.6
6        device       1183   1202.2
7        late           99    145.4
total entries 1887  returned lines 1887  unknown 0

slowest 12:
   273.4 ms  L6      jent_mod_init  ret=0
   204.1 ms  L6      physmap_init  ret=0
   144.0 ms  L3      of_platform_default_populate_init  ret=0
    68.4 ms  L6      pl061_gpio_driver_init  ret=0
    56.0 ms  L6      loop_init  ret=0
    48.5 ms  L7      param_sysfs_builtin_init  ret=0
    45.0 ms  L7      slab_late_init  ret=0
    31.3 ms  L6      virtio_net_driver_init  ret=0
    28.4 ms  L5      chr_dev_init  ret=0
    26.5 ms  L6      gen_pci_driver_init  ret=0
    24.0 ms  L1      sock_init  ret=0
    20.2 ms  L7      gpio_keys_init  ret=0

nonzero returns: [('cci_init', -19), ('renesas_soc_init', -19), ('rcar_sysc_pd_init', -19), ('cpufreq_dt_platdev_init', -19), ('__gnttab_init', -19), ('rcar_sysc_pd_init_provider', -19), ('rcar_gen4_sysc_pd_init', -19), ('xenbus_init', -19)]
```

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="22" text-anchor="middle" font-size="14" font-weight="bold">initcall 레벨별 소요 시간 (QEMU TCG, initcall_debug 부팅 1회)</text>
  <text x="20" y="48" font-size="12">레벨</text>
  <text x="150" y="48" text-anchor="end" font-size="12">개수</text>
  <text x="200" y="48" font-size="12">벽시계 구간 (ms) — 다음 레벨 진입까지</text>
  <text x="20" y="75" font-size="13">early*</text>
  <text x="150" y="75" text-anchor="end" font-size="13">39</text>
  <rect x="200" y="62" width="63.2" height="18" fill="#888" fill-opacity="0.6" stroke="#888"/>
  <text x="269.2" y="75" font-size="12">212</text>
  <text x="20" y="105" font-size="13">pure</text>
  <text x="150" y="105" text-anchor="end" font-size="13">7</text>
  <rect x="200" y="92" width="2.0" height="18" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <text x="208.0" y="105" font-size="12">7</text>
  <text x="20" y="135" font-size="13">core</text>
  <text x="150" y="135" text-anchor="end" font-size="13">134</text>
  <rect x="200" y="122" width="27.9" height="18" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <text x="233.9" y="135" font-size="12">94</text>
  <text x="20" y="165" font-size="13">postcore</text>
  <text x="150" y="165" text-anchor="end" font-size="13">56</text>
  <rect x="200" y="152" width="19.3" height="18" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <text x="225.3" y="165" font-size="12">65</text>
  <text x="20" y="195" font-size="13">arch</text>
  <text x="150" y="195" text-anchor="end" font-size="13">129</text>
  <rect x="200" y="182" width="62.6" height="18" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <text x="268.6" y="195" font-size="12">210</text>
  <text x="20" y="225" font-size="13">subsys</text>
  <text x="150" y="225" text-anchor="end" font-size="13">182</text>
  <rect x="200" y="212" width="40.5" height="18" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <text x="246.5" y="225" font-size="12">136</text>
  <text x="20" y="255" font-size="13">fs+rootfs</text>
  <text x="150" y="255" text-anchor="end" font-size="13">58</text>
  <rect x="200" y="242" width="22.3" height="18" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <text x="228.3" y="255" font-size="12">75</text>
  <text x="20" y="285" font-size="13">device</text>
  <text x="150" y="285" text-anchor="end" font-size="13">1183</text>
  <rect x="200" y="272" width="390.0" height="18" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="596.0" y="285" font-size="12">1310</text>
  <text x="20" y="315" font-size="13">late</text>
  <text x="150" y="315" text-anchor="end" font-size="13">99</text>
  <rect x="200" y="302" width="85.5" height="18" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <text x="291.5" y="315" font-size="12">287</text>
  <line x1="200" y1="56" x2="200" y2="330" stroke="currentColor"/>
  <text x="20" y="354" font-size="12">early* 구간에는 smp_init·sched_init_smp·driver_init 등 initcall이 아닌 일도 섞여 있다.</text>
  <text x="20" y="374" font-size="12">late 구간은 Freeing unused kernel memory 시각까지. 합계 0.10 → 2.50 s (기본 부팅 1.21 s)</text>
</svg>
```
그림 5 — initcall 레벨별 벽시계 구간. 각 막대는 "entering initcall level" 시각에서 다음 레벨 진입까지(마지막은 "Freeing unused kernel memory"까지)의 실측 시간이다. 개수는 `System.map`에서 센 엔트리 수.

```svg
<svg viewBox="0 0 680 410" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="22" text-anchor="middle" font-size="14" font-weight="bold">가장 느린 initcall 10개 (initcall_debug의 "after N usecs")</text>
  <text x="292" y="55" text-anchor="end" font-size="12">jent_mod_init</text>
  <rect x="300" y="43" width="310.0" height="17" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="616.0" y="56" font-size="12">273.4</text>
  <text x="292" y="83" text-anchor="end" font-size="12">physmap_init</text>
  <rect x="300" y="71" width="231.4" height="17" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="537.4" y="84" font-size="12">204.1</text>
  <text x="292" y="111" text-anchor="end" font-size="12">of_platform_default_populate_init</text>
  <rect x="300" y="99" width="163.3" height="17" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <text x="469.3" y="112" font-size="12">144.0</text>
  <text x="292" y="139" text-anchor="end" font-size="12">pl061_gpio_driver_init</text>
  <rect x="300" y="127" width="77.6" height="17" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="383.6" y="140" font-size="12">68.4</text>
  <text x="292" y="167" text-anchor="end" font-size="12">loop_init</text>
  <rect x="300" y="155" width="63.5" height="17" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="369.5" y="168" font-size="12">56.0</text>
  <text x="292" y="195" text-anchor="end" font-size="12">param_sysfs_builtin_init</text>
  <rect x="300" y="183" width="55.0" height="17" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <text x="361.0" y="196" font-size="12">48.5</text>
  <text x="292" y="223" text-anchor="end" font-size="12">slab_late_init</text>
  <rect x="300" y="211" width="51.0" height="17" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <text x="357.0" y="224" font-size="12">45.0</text>
  <text x="292" y="251" text-anchor="end" font-size="12">virtio_net_driver_init</text>
  <rect x="300" y="239" width="35.5" height="17" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="341.5" y="252" font-size="12">31.3</text>
  <text x="292" y="279" text-anchor="end" font-size="12">chr_dev_init</text>
  <rect x="300" y="267" width="32.2" height="17" fill="#888" fill-opacity="0.6" stroke="#888"/>
  <text x="338.2" y="280" font-size="12">28.4</text>
  <text x="292" y="307" text-anchor="end" font-size="12">gen_pci_driver_init</text>
  <rect x="300" y="295" width="30.0" height="17" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="336.0" y="308" font-size="12">26.5</text>
  <line x1="300" y1="38" x2="300" y2="320" stroke="currentColor"/>
  <rect x="20" y="331" width="14" height="12" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <text x="40" y="342" font-size="12">L3 arch</text>
  <rect x="130" y="331" width="14" height="12" fill="#888" fill-opacity="0.6" stroke="#888"/>
  <text x="150" y="342" font-size="12">L5 fs</text>
  <rect x="240" y="331" width="14" height="12" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="260" y="342" font-size="12">L6 device</text>
  <rect x="350" y="331" width="14" height="12" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <text x="370" y="342" font-size="12">L7 late</text>
  <text x="20" y="366" font-size="12">단위 ms. L3 값은 clocksource 전환(fs 레벨) 이전이라 jiffies 4 ms 단위로 반올림된 값이다.</text>
</svg>
```
그림 6 — 가장 느린 initcall 10개. 같은 `initcall_debug` 부팅의 "after N usecs" 값이다.

출력에서 볼 것:

- **엔트리의 63%(1,183개), 시간의 절반 이상(벽시계 1.31초)이 device 레벨**이다. defconfig는 수많은 SoC 드라이버를 built-in으로 넣기 때문이다. 제품 커널에서 쓰지 않는 드라이버를 빼는 것이 부팅 시간 단축의 첫 단계인 이유다(C7).
- 1위 `jent_mod_init`(273 ms)은 jitter entropy 난수 생성기의 **자체 테스트**다. 2위 `physmap_init`(204 ms)은 QEMU `virt`의 CFI 플래시 2개를 probe하는 시간이다. 실제 보드에서도 "암호 자가 테스트"와 "느린 버스의 장치 probe"가 단골 상위권이다.
- `ret=-19`(`-ENODEV`)가 여럿이다. Renesas·Xen·CCI 드라이버가 "내 하드웨어가 없다"며 빠진 것이다. 실패해도 부팅은 계속된다.
- **측정 함정 두 가지.** (1) `initcall_debug`의 시간은 `ktime_get()`인데, fs 레벨의 `clocksource_done_booting`(0.829초)이 arch timer로 바꾸기 전까지는 **jiffies 시계(CONFIG_HZ=250 → 4 ms 단위)** 다. 그래서 L3 `of_platform_default_populate_init`의 144.0 ms나 early 레벨 합계가 4의 배수로 딱 떨어진다. 이른 레벨의 작은 값은 믿지 말고 printk 타임스탬프 차이(sched_clock, 16 ns 해상도)를 보라. (2) `initcall_debug` 자체가 3,800줄을 로그에 써서 부팅이 느려진다(이 실행은 "Run /init"이 2.51초, 기본 부팅은 1.21초). 상대 비교용이지 절대값이 아니다. 게다가 TCG 에뮬레이션이라 실제 Cortex-A57보다 훨씬 느리다.

### 7.6 예제 15 — 느린 initcall 빼 보기: `initcall_blacklist=`

무엇을 확인하나: 의심 가는 initcall을 재빌드 없이 건너뛴다. "이 드라이버를 빼면 hang이 사라지나?"를 확인하는 bring-up 도구다(§5 예제 11에서 콘솔 드라이버를 막을 때도 썼다).

```sh
$R blk 60 "console=ttyAMA0 quiet log_buf_len=4M initcall_debug initcall_blacklist=jent_mod_init LAB_CMD=dmesg~|~grep~-E~\"blacklist|jent\"" -initrd $L/initramfs.cpio.gz
cat c6_blk.log
```

```text
exit=0 lines=7
lab initramfs: 6.18.55 up 3.34s
[    0.000000] Kernel command line: console=ttyAMA0 quiet log_buf_len=4M initcall_debug initcall_blacklist=jent_mod_init LAB_CMD=dmesg~|~grep~-E~"blacklist|jent"
[    0.000000] blacklisting initcall jent_mod_init
[    0.000000] Unknown kernel command line parameters "LAB_CMD=dmesg~|~grep~-E~"blacklist|jent"", will be passed to user space.
[    1.903654] initcall jent_mod_init blacklisted
[    3.662240]     LAB_CMD=dmesg~|~grep~-E~"blacklist|jent"
```

출력에서 볼 것: "blacklisting initcall jent_mod_init"(인자 파싱 시점)과 "initcall jent_mod_init blacklisted"(device 레벨에서 건너뛴 시점) 두 줄. "calling jent_mod_init"은 없다. 두 메시지는 `pr_debug`지만 `init/main.c` 맨 위에 `#define DEBUG`가 있어서 항상 컴파일되고, `quiet`라 콘솔엔 안 나오고 `dmesg`에만 있다. 마지막 줄은 `initcall_debug`일 때 `run_init_process()`가 PID 1에 넘기는 환경변수를 나열한 것이다. (이 VM은 다른 작업과 CPU를 나눠 써서 "blacklist 전후 부팅 시간"은 1.8~4.0초로 흔들렸다. 시간 비교는 조용한 환경에서 여러 번 재야 한다.)

> 주의: `initcall_blacklist`는 디버깅 도구다. 제품에서 쓰지 않는 드라이버는 `.config`에서 빼는 것이 맞다. 그리고 함수 이름 기준이라 `static` 함수 이름이 겹치면 여러 개가 함께 막힐 수 있다.

---

## 8. rootfs — initramfs인가, `root=` 장치인가

### 8.1 직관 — 커널은 처음부터 루트 디렉터리를 하나 갖고 있다

커널이 `execve("/init")`을 하려면 `/`가 있어야 한다. 커널은 부팅 초기에 메모리 위의 빈 파일시스템 **rootfs**(ramfs 또는 tmpfs)를 `/`로 만들어 둔다. 그다음 두 갈래가 있다.

- **initramfs**: 부트로더가 메모리에 올려 둔 cpio 아카이브(또는 커널에 내장한 것)를 rootfs에 **풀어 놓고**, 그 안의 `/init`(기본값, `rdinit=`로 변경)을 PID 1로 실행한다. 진짜 루트 장치를 찾고 마운트하는 일(`switch_root`)은 그 `/init`이 유저 공간에서 한다. 커널의 `root=` 처리는 건너뛴다.
- **`root=` 직접 마운트**: rootfs에 실행할 `/init`이 없으면 커널이 직접 `root=`로 지정된 블록 장치(eMMC 파티션, virtio 디스크, NFS 등)를 마운트하고 거기로 chroot한 뒤 `init=` 또는 `/sbin/init`을 실행한다. 이때 **스토리지 드라이버와 파일시스템이 커널에 built-in** 되어 있어야 한다(모듈을 읽을 파일시스템이 아직 없으니까).

펌웨어 비유: initramfs는 "부트로더가 SRAM에 같이 올려 준 미니 파일 이미지"이고, `root=`는 "커널이 직접 NAND를 마운트"하는 것이다.

이 갈림길이 `kernel_init_freeable()`의 마지막 부분이다.

```sh
sed -n 1553,1571p /var/lab/src/linux-6.18.55/init/main.c
```

```text
	do_basic_setup();

	kunit_run_all_tests();

	wait_for_initramfs();
	console_on_rootfs();

	/*
	 * check if there is an early userspace init.  If yes, let it do all
	 * the work
	 */
	int ramdisk_command_access;
	ramdisk_command_access = init_eaccess(ramdisk_execute_command);
	if (ramdisk_command_access != 0) {
		pr_warn("check access for rdinit=%s failed: %i, ignoring\n",
			ramdisk_execute_command, ramdisk_command_access);
		ramdisk_execute_command = NULL;
		prepare_namespace();
	}
```

순서가 중요하다. initramfs 풀기(`populate_rootfs`, rootfs 레벨 initcall)는 비동기로 시작되어 `do_basic_setup()` 동안 다른 initcall과 겹쳐 돌고, `wait_for_initramfs()`에서 끝나기를 기다린다. 그다음 `console_on_rootfs()`가 rootfs의 `/dev/console`을 열어 fd 0·1·2를 만든다(§5의 "unable to open an initial console"이 여기서 나온다). 그리고 `rdinit`(기본 `/init`)에 접근해 보고, **없을 때만** `prepare_namespace()`로 `root=`를 처리한다.

```svg
<svg viewBox="0 0 680 560" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="100" y="14" width="330" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="265" y="32" text-anchor="middle" font-size="12" font-weight="bold">rootfs_initcall: populate_rootfs</text>
  <text x="265" y="50" text-anchor="middle" font-size="12">initramfs cpio → rootfs(메모리)에 풀기</text>
  <rect x="100" y="84" width="330" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="265" y="102" text-anchor="middle" font-size="12" font-weight="bold">rdinit (기본 /init) 접근 가능?</text>
  <text x="265" y="120" text-anchor="middle" font-size="12">init_eaccess(ramdisk_execute_command)</text>
  <rect x="466" y="84" width="204" height="62" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="568" y="102" text-anchor="middle" font-size="12" font-weight="bold">rdinit을 PID 1로 실행</text>
  <text x="568" y="120" text-anchor="middle" font-size="12">root= 는 유저 공간 몫</text>
  <text x="568" y="138" text-anchor="middle" font-size="12">(mount + switch_root)</text>
  <line x1="430" y1="106" x2="465" y2="106" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="447" y="98" text-anchor="middle" font-size="12">예</text>
  <line x1="265" y1="128" x2="265" y2="163" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="275" y="146" font-size="12">아니오 — "check access … ignoring"</text>
  <rect x="100" y="164" width="330" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="265" y="182" text-anchor="middle" font-size="12" font-weight="bold">prepare_namespace()</text>
  <text x="265" y="200" text-anchor="middle" font-size="12">root= 해석, rootwait면 장치 대기</text>
  <line x1="265" y1="208" x2="265" y2="235" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="100" y="236" width="330" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="265" y="254" text-anchor="middle" font-size="12" font-weight="bold">장치를 찾고 마운트에 성공?</text>
  <text x="265" y="272" text-anchor="middle" font-size="12">mount_root → 각 fs 타입 시도</text>
  <rect x="466" y="236" width="204" height="62" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="568" y="254" text-anchor="middle" font-size="12" font-weight="bold">panic</text>
  <text x="568" y="272" text-anchor="middle" font-size="12">VFS: Unable to mount</text>
  <text x="568" y="290" text-anchor="middle" font-size="12">root fs on …</text>
  <line x1="430" y1="258" x2="465" y2="258" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="447" y="250" text-anchor="middle" font-size="12">아니오</text>
  <line x1="265" y1="280" x2="265" y2="307" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="275" y="298" font-size="12">예</text>
  <rect x="100" y="308" width="330" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="265" y="326" text-anchor="middle" font-size="12" font-weight="bold">VFS: Mounted root (… readonly)</text>
  <text x="265" y="344" text-anchor="middle" font-size="12">devtmpfs 마운트, 새 루트로 chroot</text>
  <line x1="265" y1="352" x2="265" y2="379" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="100" y="380" width="330" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="265" y="398" text-anchor="middle" font-size="12" font-weight="bold">init= 지정?</text>
  <text x="265" y="416" text-anchor="middle" font-size="12">실패하면 다른 후보로 넘어가지 않는다</text>
  <rect x="466" y="380" width="204" height="44" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="568" y="398" text-anchor="middle" font-size="12">panic: Requested init</text>
  <text x="568" y="416" text-anchor="middle" font-size="12">… failed (error -2)</text>
  <line x1="430" y1="402" x2="465" y2="402" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="447" y="394" text-anchor="middle" font-size="12">실패</text>
  <line x1="265" y1="424" x2="265" y2="451" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="275" y="442" font-size="12">없음</text>
  <rect x="100" y="452" width="330" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="265" y="470" text-anchor="middle" font-size="12" font-weight="bold">/sbin/init → /etc/init → /bin/init</text>
  <text x="265" y="488" text-anchor="middle" font-size="12">→ /bin/sh 순서로 시도</text>
  <rect x="466" y="452" width="204" height="44" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="568" y="470" text-anchor="middle" font-size="12">panic: No working</text>
  <text x="568" y="488" text-anchor="middle" font-size="12">init found</text>
  <line x1="430" y1="474" x2="465" y2="474" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="447" y="466" text-anchor="middle" font-size="12">전부 실패</text>
  <line x1="265" y1="496" x2="265" y2="515" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="100" y="516" width="330" height="30" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="265" y="536" text-anchor="middle" font-size="12" font-weight="bold">PID 1 실행 성공 → 유저 공간 (C7)</text>
  <text x="14" y="102" font-size="12">kernel_init</text>
  <text x="14" y="118" font-size="12">_freeable()</text>
</svg>
```
그림 7 — rootfs 결정 흐름. 주황은 분기, 빨강은 이 절에서 QEMU로 직접 재현한 panic이다. initramfs에 실행 가능한 `/init`이 있으면 커널은 `root=`를 보지도 않는다.

### 8.2 예제 16 — initramfs 부팅을 다시 보기

기본 랩 부팅(예제 8)이 initramfs 경로다. 로그의 해당 줄:

```text
151:[    0.463933] Unpacking initramfs...
166:[    0.638242] Freeing initrd memory: 1180K
246:[    1.211320] Freeing unused kernel memory: 3264K
247:[    1.212089] Run /init as init process
```

"Unpacking initramfs"(`do_populate_rootfs`) → cpio를 다 풀고 나면 원본 압축 이미지 메모리를 반납("Freeing initrd memory: 1180K" — `initramfs.cpio.gz` 파일 크기 1,210,338바이트 ≈ 1182 KiB와 거의 같다. 해제는 페이지 단위다) → `/init` 실행. `prepare_namespace()` 관련 메시지가 하나도 없다.

### 8.3 예제 17 — ext4 virtio 디스크를 만들고 `root=/dev/vda rootwait`로 부팅

무엇을 확인하나: initramfs 없이, 커널이 직접 블록 장치를 루트로 마운트하는 경로. 랩 rootfs 디렉터리를 복사해 ext4 이미지를 만들고(root 권한 없이 `mkfs.ext4 -d`로 디렉터리를 채운다), virtio-mmio 블록 장치로 붙인다.

```sh
cd /var/tmp/C6
cp -a /var/lab/build/rootfs c6_rootdir
echo "I am the ext4 root on virtio" > c6_rootdir/etc/c6_marker
truncate -s 64M c6_root.ext4
mkfs.ext4 -q -F -L c6root -d c6_rootdir c6_root.ext4 && ls -l c6_root.ext4 && file c6_root.ext4
```

```text
-rw-rw-r-- 1 donh donh 67108864 Oct 10 10:27 c6_root.ext4
c6_root.ext4: Linux rev 1.0 ext4 filesystem data, UUID=54b428f1-2689-40d7-b5da-2ebfa71ff908, volume name "c6root" (extents) (64bit) (large files) (huge files)
```

```sh
D="-drive file=c6_root.ext4,format=raw,if=none,id=c6hd -device virtio-blk-device,drive=c6hd"
$R ext4b 60 "console=ttyAMA0 root=/dev/vda rootwait init=/init LAB_CMD=cat~/etc/c6_marker;grep~-E~\"^/dev/root|^devtmpfs\"~/proc/mounts;ls~-l~/dev/vda~|~cat;cat~/sys/block/vda/dev" $D
grep -nE "virtio_blk|check access|EXT4-fs|VFS:|devtmpfs: mounted|Freeing unused|Run /init" c6_ext4b.log | grep -v "Dquot\|Disk quotas"
tail -9 c6_ext4b.log | head -7
```

```text
exit=0 lines=260
197:[    6.111853] virtio_blk virtio0: 1/0/0 default/read/poll queues
198:[    6.147571] virtio_blk virtio0: [vda] 131072 512-byte logical blocks (67.1 MB/64.0 MiB)
246:[    8.281733] check access for rdinit=/init failed: -2, ignoring
247:[    8.457539] EXT4-fs (vda): mounted filesystem 54b428f1-2689-40d7-b5da-2ebfa71ff908 ro with ordered data mode. Quota mode: none.
248:[    8.458153] VFS: Mounted root (ext4 filesystem) readonly on device 254:0.
249:[    8.469042] devtmpfs: mounted
250:[    8.631596] Freeing unused kernel memory: 3264K
251:[    8.633771] Run /init as init process
lab initramfs: 6.18.55 up 7.03s
I am the ext4 root on virtio
/dev/root / ext4 ro,relatime 0 0
devtmpfs /dev devtmpfs rw,relatime,size=218656k,nr_inodes=54664,mode=755 0 0
brw-------    1 0        0         254,   0 Jan  1  1970 /dev/vda
254:0
```

출력에서 볼 것:

- 197~198행: **device initcall `virtio_blk_init`이 probe해서 `vda`가 생긴다.** 이게 없으면 `root=/dev/vda`는 찾을 장치가 없다. 그래서 루트 장치 드라이버는 built-in이어야 한다(`CONFIG_VIRTIO_BLK=y`, `CONFIG_EXT4_FS=y`).
- 246행: initramfs를 안 줬으니 rootfs에 `/init`이 없다 → "check access for rdinit=/init failed: -2(ENOENT), ignoring" → `prepare_namespace()`로 간다. 이 경고는 정상 동작이다.
- 247~248행: ext4 마운트, **기본은 읽기 전용(readonly)**. 유저 공간이 fsck 후 `mount -o remount,rw /` 하는 것이 전통적 순서다. 처음부터 쓰기로 붙이려면 커맨드라인에 `rw`.
- 249행: `CONFIG_DEVTMPFS_MOUNT=y`라 커널이 새 루트의 `/dev`에 devtmpfs를 붙인다(rootfs 디렉터리에 빈 `/dev`가 있어야 한다).
- `/proc/mounts`에는 루트가 `/dev/root`로 보인다. 커널 내부 이름이고 실제 노드는 없다(`/sys/block/vda/dev` = 254:0과 "device 254:0"이 같다). "lab initramfs"라는 문구는 같은 `/init` 스크립트를 디스크에 복사했기 때문에 나온 것이다. `init=/init`으로 그 스크립트를 PID 1로 지정했다.
- 타임스탬프가 6~8초인 것은 이 실행 때 VM에 다른 부하가 있었기 때문이다(조용할 때 같은 커널이 1.2초에 `/init`에 도달한다).

`rootwait`는 "장치가 나타날 때까지 무한히 기다려라"다. USB·SD·eMMC처럼 **probe가 비동기로 늦게 끝나는 장치**에 필수다. `rootwait` 없이 장치가 아직 없으면 바로 panic한다. 반대로 `root=`가 오타면 `rootwait`는 영원히 기다린다 — 6.x에는 상한을 주는 `rootwait=<초>`가 있다.

### 8.4 예제 18 — initramfs와 `root=`를 함께 주면

무엇을 확인하나: initramfs에 `/init`이 있으면 커널은 `root=`를 무시하고, 루트 장치 마운트는 `/init`의 일이 된다. `/init`이 하는 일을 손으로 흉내 낸다.

```sh
$R both 60 "console=ttyAMA0 quiet root=/dev/vda rootwait LAB_CMD=head~-1~/proc/mounts;mount~-o~ro~/dev/vda~/mnt;cat~/mnt/etc/c6_marker;ls~-l~/dev/vda~|~cat" \
   -initrd /var/lab/out/initramfs.cpio.gz $D
cat c6_both.log
```

```text
exit=0 lines=5
lab initramfs: 6.18.55 up 5.79s
rootfs / rootfs rw 0 0
I am the ext4 root on virtio
brw-------    1 0        0         254,   0 Jan  1  1970 /dev/vda
[    7.786283] reboot: Power down
```

출력에서 볼 것: 루트는 여전히 메모리 위의 `rootfs`(rw)다. `root=/dev/vda`는 `/proc/cmdline`에만 있고 커널은 손대지 않았다. 실제 initramfs의 `/init`(dracut, BusyBox 스크립트, systemd)은 `/proc/cmdline`에서 `root=`를 읽어 이렇게 마운트한 뒤 `switch_root /mnt /sbin/init`으로 넘어간다. **"initramfs를 쓰면 `root=`는 커널 인자가 아니라 initramfs 스크립트에 주는 인자"** 라는 점이 면접 단골이다. 루트가 암호화(dm-crypt)·LVM·네트워크·A/B 파티션 선택처럼 결정에 로직이 필요하면 initramfs를 쓴다(C7).

### 8.5 예제 19 — 고전적인 실패를 전부 재현하기

무엇을 확인하나: 현장에서 보는 panic 메시지를 하나씩 직접 만든다. `panic=2`를 주면 panic 후 2초 뒤 재부팅하는데, 래퍼가 `-no-reboot`라 QEMU가 그 시점에 종료된다(그래서 timeout이 아니라 exit 0).

먼저 `root=` 오타(`rootwait` 없음, `panic=` 없음 → 영원히 멈춤):

```sh
$R f_badroot 60 "console=ttyAMA0 root=/dev/vdb" $D
grep -n -A11 "Can't open blockdev" c6_f_badroot.log
sed -n "/Kernel panic - not syncing/,/CPU features/p" c6_f_badroot.log
```

```text
exit=124 lines=280
246:[    4.665580] /dev/root: Can't open blockdev
247-[    4.666734] VFS: Cannot open root device "/dev/vdb" or unknown-block(0,0): error -6
248-[    4.666812] Please append a correct "root=" boot option; here are the available partitions:
249-[    4.667014] fe00           65536 vda 
250-[    4.667116]  driver: virtio_blk
251-[    4.667158] 1f00          131072 mtdblock0 
252-[    4.667172]  (driver?)
253-[    4.667216] List of all bdev filesystems:
254-[    4.667232]  ext3
255-[    4.667246]  ext2
256-[    4.667250]  ext4
257-[    4.667253]  squashfs
[    4.667621] Kernel panic - not syncing: VFS: Unable to mount root fs on unknown-block(0,0)
[    4.677636] CPU: 0 UID: 0 PID: 1 Comm: swapper/0 Not tainted 6.18.55 #1 PREEMPT 
[    4.689395] Hardware name: linux,dummy-virt (DT)
[    4.689836] Call trace:
[    4.690158]  show_stack+0x28/0x7c (C)
[    4.691150]  dump_stack_lvl+0x60/0x80
[    4.691325]  dump_stack+0x18/0x24
[    4.691440]  vpanic+0xf8/0x2dc
[    4.691544]  abort+0x0/0x4
[    4.691638]  mount_root_generic+0x1f0/0x2a8
[    4.701115]  mount_root+0x204/0x244
[    4.701266]  prepare_namespace+0x1d4/0x238
[    4.701367]  kernel_init_freeable+0x2a4/0x2c8
[    4.701476]  kernel_init+0x20/0x140
[    4.701581]  ret_from_fork+0x10/0x20
[    4.702225] Kernel Offset: 0x267c60800000 from 0xffff800080000000
[    4.702400] PHYS_OFFSET: 0xfff1000040000000
[    4.702507] CPU features: 0x104000,04027800,40004000,0400421b
[    4.703240] ---[ end Kernel panic - not syncing: VFS: Unable to mount root fs on unknown-block(0,0) ]---
qemu-system-aarch64: terminating on signal 15 from pid 200846 (timeout)
```

(마지막 두 줄은 `sed` 범위가 "end Kernel panic" 줄에서 다시 시작되어 파일 끝까지 함께 나온 것이다. QEMU 줄은 `timeout`이 60초 뒤 QEMU를 죽였다는 뜻이다.)

출력에서 볼 것 — 이 panic 메시지는 **진단 정보를 다 주고 죽는다**:

- "here are the available partitions": 커널이 아는 블록 장치 목록(`fe00` = major 254, minor 0 = `vda`, 드라이버 `virtio_blk`). 오타라면 여기서 진짜 이름을 본다. **목록이 비어 있으면 스토리지 드라이버가 built-in이 아니거나 probe에 실패한 것**이다.
- "List of all bdev filesystems": built-in 파일시스템 목록. 루트가 f2fs인데 목록에 없으면 `CONFIG_F2FS_FS=y`가 필요하다.
- `unknown-block(0,0)`: `root=` 문자열을 장치 번호로 바꾸지 못했다는 뜻이다. 장치는 찾았는데 파일시스템을 못 읽는 경우라면 `unknown-block(179,2)`처럼 실제 번호가 나온다 — 이 차이가 "장치 문제냐, 파일시스템 문제냐"를 가른다.
- 콜 트레이스가 그림 7의 경로 그대로다: `kernel_init` → `kernel_init_freeable` → `prepare_namespace` → `mount_root` → `mount_root_generic` → panic. `Comm: swapper/0`, `PID: 1` — PID 1이 아직 커널 코드를 돌고 있다.
- `panic=`이 없어서(`CONFIG_PANIC_TIMEOUT=0`) 여기서 영원히 멈췄다(exit=124 = timeout이 죽임). 현장 장비라면 사람이 전원을 뽑을 때까지 벽돌이다.

나머지 실패들은 `panic=2`를 붙여 한꺼번에 돌렸다. `c6_noinit.ext4`는 `/etc/c6_marker`와 빈 `/dev`만 든 ext4다.

```sh
mkdir -p c6_noinitdir/etc c6_noinitdir/dev; echo "no init here" > c6_noinitdir/etc/c6_marker
truncate -s 16M c6_noinit.ext4; mkfs.ext4 -q -F -d c6_noinitdir c6_noinit.ext4
N="-drive file=c6_noinit.ext4,format=raw,if=none,id=c6hd -device virtio-blk-device,drive=c6hd"
$R f_wait     40 "console=ttyAMA0 root=/dev/vdb rootwait=3 panic=2" $D
$R f_noinit   40 "console=ttyAMA0 root=/dev/vda panic=2" $N
$R f_initnope 40 "console=ttyAMA0 root=/dev/vda init=/sbin/c6_nope panic=2" $D
$R f_inittrue 40 "console=ttyAMA0 root=/dev/vda init=/bin/true panic=2" $D
$R f_rdnope   40 "console=ttyAMA0 rdinit=/c6_nope panic=2" -initrd /var/lab/out/initramfs.cpio.gz
for f in f_wait f_noinit f_initnope f_inittrue f_rdnope; do echo "=== $f"
  grep -E "Waiting for root|Cannot open root|Kernel panic|Run |Rebooting|Mounted root|devtmpfs: mounted|Attempted|check access|Unpacking" c6_$f.log | grep -v "end Kernel panic"; done
```

```text
exit=0 lines=280
exit=0 lines=279
exit=0 lines=266
exit=0 lines=273
exit=0 lines=277
=== f_wait
[    3.847912] check access for rdinit=/init failed: -2, ignoring
[    3.848827] Waiting for root device /dev/vdb...
[    6.875671] VFS: Cannot open root device "/dev/vdb" or unknown-block(0,0): error -6
[    6.900629] Kernel panic - not syncing: VFS: Unable to mount root fs on unknown-block(0,0)
[    6.982460] Rebooting in 2 seconds..
=== f_noinit
[    3.525994] check access for rdinit=/init failed: -2, ignoring
[    3.658540] VFS: Mounted root (ext4 filesystem) readonly on device 254:0.
[    3.665232] devtmpfs: mounted
[    3.888071] Run /sbin/init as init process
[    3.890018] Run /etc/init as init process
[    3.895864] Run /bin/init as init process
[    3.896078] Run /bin/sh as init process
[    3.896797] Kernel panic - not syncing: No working init found.  Try passing init= option to kernel. See Linux Documentation/admin-guide/init.rst for guidance.
[    3.914165] Rebooting in 2 seconds..
=== f_initnope
[    4.862821] check access for rdinit=/init failed: -2, ignoring
[    5.098214] VFS: Mounted root (ext4 filesystem) readonly on device 254:0.
[    5.115360] devtmpfs: mounted
[    5.362022] Run /sbin/c6_nope as init process
[    5.368208] Kernel panic - not syncing: Requested init /sbin/c6_nope failed (error -2).
[    5.375283] Rebooting in 2 seconds..
=== f_inittrue
[    1.831918] check access for rdinit=/init failed: -2, ignoring
[    1.878485] VFS: Mounted root (ext4 filesystem) readonly on device 254:0.
[    1.880629] devtmpfs: mounted
[    1.995151] Run /bin/true as init process
[    2.111569] Kernel panic - not syncing: Attempted to kill init! exitcode=0x00000000
[    2.118539] Rebooting in 2 seconds..
=== f_rdnope
[    0.791355] Unpacking initramfs...
[    2.022235] check access for rdinit=/c6_nope failed: -2, ignoring
[    2.053037] VFS: Cannot open root device "" or unknown-block(0,0): error -6
[    2.053861] Kernel panic - not syncing: VFS: Unable to mount root fs on unknown-block(0,0)
[    2.060488] Rebooting in 2 seconds..
```

출력에서 볼 것:

| 실험 | 메시지 | 의미 |
|---|---|---|
| `f_wait`: 없는 장치 + `rootwait=3` | "Waiting for root device /dev/vdb..." 3.85초 → 3.03초 뒤 포기 → VFS panic | `rootwait=<초>` 상한이 정확히 동작. 그냥 `rootwait`였다면 영원히 대기 |
| `f_noinit`: 루트에 init 없음 | 네 후보를 차례로 "Run … as init process" 시도 후 "No working init found" | `-ENOENT`는 조용히 다음 후보로 넘어간다. 그림 7의 마지막 분기 |
| `f_initnope`: `init=` 오타 | "Requested init /sbin/c6_nope failed (error -2)" | `init=`을 주면 **대체 후보를 시도하지 않고** 바로 panic |
| `f_inittrue`: init이 종료됨 | "Attempted to kill init! exitcode=0x00000000" | PID 1은 죽으면 안 된다. 정상 종료(0)도 panic. 셸 스크립트 init 끝에 `exec`을 빼먹으면 이렇게 된다 |
| `f_rdnope`: `rdinit=` 오타 | "check access for rdinit=/c6_nope failed" → `root=`도 없으니 `""` 장치 → VFS panic | initramfs는 풀렸지만 지정한 init이 없어 `root=` 경로로 떨어진 것 |
| 공통 `panic=2` | "Rebooting in 2 seconds.." 후 재부팅(QEMU는 `-no-reboot`라 종료) | 현장 장비는 `panic=` 양수 + 하드웨어 watchdog으로 벽돌을 피한다 |

init 선택 순서는 `kernel_init()`(랩 소스 1468~1506행)에 그대로 적혀 있다.

```sh
sed -n 1468,1506p /var/lab/src/linux-6.18.55/init/main.c
```

```text
	if (ramdisk_execute_command) {
		ret = run_init_process(ramdisk_execute_command);
		if (!ret)
			return 0;
		pr_err("Failed to execute %s (error %d)\n",
		       ramdisk_execute_command, ret);
	}

	/*
	 * We try each of these until one succeeds.
	 *
	 * The Bourne shell can be used instead of init if we are
	 * trying to recover a really broken machine.
	 */
	if (execute_command) {
		ret = run_init_process(execute_command);
		if (!ret)
			return 0;
		panic("Requested init %s failed (error %d).",
		      execute_command, ret);
	}

	if (CONFIG_DEFAULT_INIT[0] != '\0') {
		ret = run_init_process(CONFIG_DEFAULT_INIT);
		if (ret)
			pr_err("Default init %s failed (error %d)\n",
			       CONFIG_DEFAULT_INIT, ret);
		else
			return 0;
	}

	if (!try_to_run_init_process("/sbin/init") ||
	    !try_to_run_init_process("/etc/init") ||
	    !try_to_run_init_process("/bin/init") ||
	    !try_to_run_init_process("/bin/sh"))
		return 0;

	panic("No working init found.  Try passing init= option to kernel. "
	      "See Linux Documentation/admin-guide/init.rst for guidance.");
```

`init=/bin/sh`는 그래서 **복구용 비상구**다. root 비밀번호를 잊었거나 init 시스템이 깨졌을 때 U-Boot에서 `bootargs`에 `init=/bin/sh`를 붙이면 셸이 PID 1로 뜬다(그 셸에서 `exit`하면 위의 "Attempted to kill init!"이다). 그리고 init 바이너리가 있는데도 "No working init found"가 나면, 대개 **동적 링크된 init의 로더(`/lib/ld-linux-aarch64.so.1`)나 라이브러리가 rootfs에 없는** 경우다 — B3 §8의 "파일이 있는데 not found"가 PID 1에서 일어난 것이다. 이때 에러 코드는 -2(ENOENT)라 조용히 다음 후보로 넘어가므로 헷갈리기 쉽다.

---

## 9. "Freeing unused kernel memory" — `.init` 섹션의 끝

### 9.1 무엇이 해제되나

`__init` 함수(`.init.text`)와 `__initdata`(`.init.data`) — `start_kernel`, `setup_arch`, 모든 built-in initcall 함수, initcall 표, `__setup` 표 — 는 부팅에 한 번만 필요하다. `kernel_init()`은 initcall과 rootfs 준비가 끝나면 이 영역을 페이지 할당기에 돌려준다.

```sh
sed -n 1441,1453p /var/lab/src/linux-6.18.55/init/main.c
sed -n 394,406p /var/lab/src/linux-6.18.55/arch/arm64/mm/init.c
```

```text
	wait_for_completion(&kthreadd_done);

	kernel_init_freeable();
	/* need to finish all async __init code before freeing the memory */
	async_synchronize_full();

	system_state = SYSTEM_FREEING_INITMEM;
	kprobe_free_init_mem();
	ftrace_free_init_mem();
	kgdb_free_init_mem();
	exit_boot_config();
	free_initmem();
	mark_readonly();
void free_initmem(void)
{
	void *lm_init_begin = lm_alias(__init_begin);
	void *lm_init_end = lm_alias(__init_end);

	WARN_ON(!IS_ALIGNED((unsigned long)lm_init_begin, PAGE_SIZE));
	WARN_ON(!IS_ALIGNED((unsigned long)lm_init_end, PAGE_SIZE));

	/* Delete __init region from memblock.reserved. */
	memblock_free(lm_init_begin, lm_init_end - lm_init_begin);

	free_reserved_area(lm_init_begin, lm_init_end,
			   POISON_FREE_INITMEM, "unused kernel");
```

`free_reserved_area(…, "unused kernel")`의 마지막 문자열이 로그의 "Freeing **unused kernel** memory: 3264K"가 된다. 크기 3264K는 예제 13에서 `System.map`으로 계산한 `__init_end - __init_begin` = 3264K, 그리고 부팅 초기의 "Memory: … 3264K init …"과 정확히 같다. 해제된 페이지는 `POISON_FREE_INITMEM`(0xcc) 패턴으로 채워지고, 그 뒤 `mark_readonly()`가 커널 텍스트·rodata를 읽기 전용으로 잠근다. 해제 후 `system_state = SYSTEM_RUNNING`이 되고 PID 1 `execve`로 넘어간다.

이것이 B3 §11에서 본 `.init.text`의 운명이다. 그리고 드라이버 개발자에게 중요한 규칙이 나온다.

- **`__init` 함수를 부팅 후에 부르면 해제된(0xcc로 채워진) 메모리로 점프한다** → oops. 예: probe 함수(핫플러그·deferred probe로 나중에 불릴 수 있음)에서 `__init` 헬퍼를 부르는 실수. 빌드 시 `modpost`가 "Section mismatch" 경고로 잡아 준다 — 무시하지 말 것.
- `__initdata` 테이블을 런타임 코드가 참조해도 같은 문제다. `__initconst`도 마찬가지.
- built-in 드라이버의 `__exit` 함수는 절대 호출되지 않는다. arm64 링커 스크립트는 `.exit.text`를 init 영역 안에 둔다 — 예제 13의 readelf에서 `.exit.text`(0x…8207ef28)가 `__init_begin`~`__init_end` 사이에 있으니 함께 해제된다.

### 9.2 예제 20 — 모듈에서도 같다: `module_init`과 `__init`

무엇을 확인하나: `.ko`의 `module_init`도 같은 "initcall" 개념이다. `initcall_debug`가 insmod 때의 init 함수도 측정하고, init이 끝나면 모듈의 `__init` 부분이 해제되어 `/proc/kallsyms`에서 사라지는 것을 본다.

```c
// c6_hello.c: module_init for a .ko is the same initcall mechanism, run at insmod time
#include <linux/module.h>
#include <linux/init.h>
#include <linux/moduleparam.h>

static int c6_level = 6;
module_param(c6_level, int, 0444);
MODULE_PARM_DESC(c6_level, "pretend initcall level");

static const char c6_tag[] __initconst = "c6-init-only";   /* freed after init */

static int __init c6_hello_init(void)
{
	pr_info("c6_hello: init (%s) level=%d, init fn at %px\n",
		c6_tag, c6_level, c6_hello_init);
	return 0;
}

static void __exit c6_hello_exit(void)
{
	pr_info("c6_hello: exit\n");
}

module_init(c6_hello_init);
module_exit(c6_hello_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("C6 demo: module_init is an initcall");
```

```make
obj-m += c6_hello.o
```

```sh
mkdir -p /var/tmp/C6/mod && cp ~/.elinux_scratch/C6/mod/* /var/tmp/C6/mod/ && cd /var/tmp/C6/mod
make -C /var/lab/out/linux-build M=$PWD modules 2>&1 | tail -5
readelf -S -W c6_hello.ko | grep -E " \.init\.text | \.init\.rodata | \.exit\.text | \.modinfo "
```

```text
  CC [M]  c6_hello.mod.o
  CC [M]  .module-common.o
  LD [M]  c6_hello.ko
make[1]: Leaving directory '/var/tmp/C6/mod'
make: Leaving directory '/var/lab/build/linux'
  [10] .init.text        PROGBITS        0000000000000000 00008c 000040 00  AX  0   0  4
  [12] .exit.text        PROGBITS        0000000000000000 0000cc 000024 00  AX  0   0  4
  [19] .init.rodata      PROGBITS        0000000000000000 000110 00000d 00   A  0   0  8
  [20] .modinfo          PROGBITS        0000000000000000 00011d 0000be 00   A  0   0  1
```

빌드 경고는 없다. `c6_hello_init`은 `.init.text`(0x40바이트), `c6_tag`는 `.init.rodata`(0xd = "c6-init-only" 12자 + NUL)에 들어갔다. 이제 랩 initramfs를 복사해 `.ko`를 넣고 QEMU 안에서 insmod한다.

```sh
cd /var/tmp/C6
mkdir c6_r && cp -a /var/lab/build/rootfs/. c6_r/ && cp mod/c6_hello.ko c6_r/
(cd c6_r && find . | cpio -o -H newc --quiet | gzip) > c6_mod.cpio.gz
$R mod 60 "console=ttyAMA0 quiet initcall_debug LAB_CMD=insmod~/c6_hello.ko~c6_level=42;dmesg~|~grep~c6_hello;grep~c6_hello~/proc/kallsyms;cat~/sys/module/c6_hello/parameters/c6_level" \
   -initrd c6_mod.cpio.gz
cat c6_mod.log
```

```text
exit=0 lines=26
lab initramfs: 6.18.55 up 5.06s
[    6.115193]     LAB_CMD=insmod~/c6_hello.ko~c6_level=42;dmesg~|~grep~c6_hello;grep~c6_hello~/proc/kallsyms;cat~/sys/module/c6_hello/parameters/c6_level
[    6.907169] c6_hello: loading out-of-tree module taints kernel.
[    6.929297] calling  c6_hello_init+0x0/0xfdc [c6_hello] @ 63
[    6.934611] c6_hello: init (c6-init-only) level=42, init fn at ffffd153d2a07024
[    6.934700] initcall c6_hello_init+0x0/0xfdc [c6_hello] returned 0 after 4592 usecs
ffffd153d2a05000 r $d	[c6_hello]
ffffd153d2a01024 t $x	[c6_hello]
ffffd153d2a01024 t c6_hello_exit	[c6_hello]
ffffd153d2a05048 r $d	[c6_hello]
ffffd153d2a05048 r __param_str_c6_level	[c6_hello]
ffffd153d2a03000 d $d	[c6_hello]
ffffd153d2a03000 d c6_level	[c6_hello]
ffffd153d2a03008 d $d	[c6_hello]
ffffd153d2a03008 d __UNIQUE_ID___addressable_cleanup_module416	[c6_hello]
ffffd153d2a05058 r $d	[c6_hello]
ffffd153d2a05058 r __param_c6_level	[c6_hello]
ffffd153d2a05080 r $d	[c6_hello]
ffffd153d2a03040 d $d	[c6_hello]
ffffd153d2a050c4 r $d	[c6_hello]
ffffd153d2a050c4 r _note_19	[c6_hello]
ffffd153d2a050dc r _note_18	[c6_hello]
ffffd153d2a03040 d __this_module	[c6_hello]
ffffd153d2a01024 t cleanup_module	[c6_hello]
42
[   19.226612] reboot: Power down
```

출력에서 볼 것:

- insmod 때도 "calling … [c6_hello] @ 63"(PID 63 = insmod 프로세스)과 "returned 0 after N usecs"가 나온다. 모듈 로더(`do_init_module()`)가 **같은 `do_one_initcall()`** 로 init 함수를 부르기 때문이다. 부팅 때와 다른 점은 호출 주체가 PID 1이 아니라 insmod를 실행한 프로세스라는 것뿐이다.
- init 함수 주소는 `…a07024`였는데, insmod 후 `/proc/kallsyms`에 `c6_hello_init`이 **없다.** `c6_hello_exit`·`c6_level`·`__this_module`처럼 계속 필요한 심볼만 남았다. init이 성공하면 모듈의 `.init.*` 영역은 해제된다. 부팅 시 "Freeing unused kernel memory"의 모듈판이다.
- `c6_level=42` 모듈 파라미터가 `/sys/module/c6_hello/parameters/c6_level`에 보인다. built-in이었다면 커맨드라인 `c6_hello.c6_level=42`로 줬을 값이다(§6.2 표의 세 번째 방법).
- (부팅 시각이 5~19초로 늘어진 것은 이 실행 때 VM의 다른 작업 때문이다.)

---

## 10. 커널 안에서는 무슨 일이 일어나나 — 드라이버는 initcall에서 태어난다

### 10.1 device initcall → probe

built-in 드라이버의 일생은 이렇게 시작한다. 드라이버 파일 끝의 `module_platform_driver(foo_driver);`는 `module_init(foo_driver_init)`으로 펼쳐지고, built-in이면 이것이 `device_initcall`(레벨 6) 엔트리가 된다. 레벨 6에서 `foo_driver_init()` → `platform_driver_register()` → `bus_add_driver()` → `driver_attach()`가 버스의 모든 장치와 짝을 맞춰 보고(DT `compatible` 비교), 맞으면 `really_probe()` → 드라이버의 `.probe()`를 **그 자리에서 동기적으로** 부른다(드라이버가 `PROBE_PREFER_ASYNCHRONOUS`를 원하지 않는 한).

그럼 "장치" 쪽은 언제 생기나? **레벨 3**의 `arch_initcall_sync(of_platform_default_populate_init)`(`drivers/of/platform.c:599`)이 §4에서 펼친 DT 트리를 돌며 `platform_device`를 만든다. 그래서 순서가 "L3: 장치 등록 → L6: 드라이버 등록 + probe"가 된다. 예제 14의 로그가 이를 보여 준다.

```sh
grep "pl011\|virtio_blk_init\|populate_rootfs\|deferred_probe" /var/tmp/C6/c6_icd.log
```

```text
[    0.500762] calling  pl011_init+0x0/0x5c @ 1
[    0.501132] initcall pl011_init+0x0/0x5c returned 0 after 0 usecs
[    0.641822] probe of 9000000.pl011:0 returned 0 after 0 usecs
[    0.643372] probe of 9000000.pl011:0.0 returned 0 after 4000 usecs
[    0.644457] 9000000.pl011: ttyAMA0 at MMIO 0x9000000 (irq = 13, base_baud = 0) is a PL011 rev1
[    0.678383] probe of 9000000.pl011 returned 0 after 40000 usecs
[    0.898623] calling  populate_rootfs+0x0/0x58 @ 1
[    0.901203] initcall populate_rootfs+0x0/0x58 returned 0 after 2559 usecs
[    1.725982] calling  virtio_blk_init+0x0/0xa4 @ 1
[    1.726275] initcall virtio_blk_init+0x0/0xa4 returned 0 after 265 usecs
[    2.326822] calling  deferred_probe_initcall+0x0/0xc0 @ 1
[    2.327508] initcall deferred_probe_initcall+0x0/0xc0 returned 0 after 662 usecs
```

출력에서 볼 것:

- `pl011_init`(L3, 0.50초)은 AMBA 드라이버를 **등록만** 하고 즉시 돌아온다. 실제 probe("probe of 9000000.pl011 returned 0 after 40000 usecs")는 그 뒤 0.68초에 끝난다 — 같은 L3의 `of_platform_default_populate_init`(sync 단계)이 AMBA 장치를 만들 때 매칭이 일어났다. `initcall_debug`는 initcall뿐 아니라 **각 probe 시간도** 찍어 준다(이 로그에 "probe of"가 54줄).
- `populate_rootfs`는 2.6 ms 만에 "돌아온다". 압축 해제는 async로 넘기고 바로 리턴하기 때문이다(§8.1).
- `virtio_blk_init`은 L6에서 0.27 ms. 이 안에서 `vda` 장치의 probe가 진행된다(예제 17의 "[vda] 131072 512-byte logical blocks").

### 10.2 deferred probe — late_initcall에서 한 번 더

probe가 필요한 자원(클럭, 레귤레이터, GPIO, 다른 장치)이 아직 없으면 드라이버는 `-EPROBE_DEFER`를 반환한다. 드라이버 코어는 이 장치를 **deferred 목록**에 넣고, 다른 드라이버가 바인딩될 때마다 다시 시도한다. 그리고 `late_initcall(deferred_probe_initcall)`(`drivers/base/dd.c:375`)이 "initcall이 거의 다 끝났으니 남은 것을 전부 다시 시도"한다. 소스 주석이 의도를 말한다: "this initcall makes sure that deferred probing is delayed until late_initcall time". 이 함수는 `/sys/kernel/debug/devices_deferred`도 만든다 — 부팅 후 "왜 이 장치가 안 떴지?"를 볼 때 첫 번째로 열어 볼 파일이다. 모듈 지원 커널에서는 `deferred_probe_timeout`(기본 10초) 뒤에 선택적 의존성을 포기하고 probe를 마무리한다.

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="20" width="300" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="160" y="38" text-anchor="middle" font-size="12" font-weight="bold">L3 of_platform_default_populate_init</text>
  <text x="160" y="56" text-anchor="middle" font-size="12">DT 노드 → platform_device (장치 쪽)</text>
  <rect x="370" y="20" width="300" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="520" y="38" text-anchor="middle" font-size="12" font-weight="bold">L6 foo_driver_init (module_init)</text>
  <text x="520" y="56" text-anchor="middle" font-size="12">platform_driver_register (드라이버 쪽)</text>
  <rect x="190" y="96" width="300" height="44" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="114" text-anchor="middle" font-size="12" font-weight="bold">driver_attach → bus match</text>
  <text x="340" y="132" text-anchor="middle" font-size="12">of_match_table의 compatible 비교</text>
  <path d="M160,64 L160,80 L300,80 L300,95" fill="none" stroke="currentColor" marker-end="url(#ah5)"/>
  <path d="M520,64 L520,80 L380,80 L380,95" fill="none" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="340" y1="140" x2="340" y2="163" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="190" y="164" width="300" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="182" text-anchor="middle" font-size="12" font-weight="bold">really_probe → foo_probe()</text>
  <text x="340" y="200" text-anchor="middle" font-size="12">자원 획득: clk, regulator, gpio, irq</text>
  <rect x="10" y="244" width="250" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="135" y="262" text-anchor="middle" font-size="12" font-weight="bold">0 → 바인딩 완료</text>
  <text x="135" y="280" text-anchor="middle" font-size="12">/sys/bus/…/drivers/foo/ 에 링크</text>
  <rect x="420" y="244" width="250" height="44" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="545" y="262" text-anchor="middle" font-size="12" font-weight="bold">-EPROBE_DEFER</text>
  <text x="545" y="280" text-anchor="middle" font-size="12">deferred 목록에 보관</text>
  <path d="M300,208 L300,226 L135,226 L135,243" fill="none" stroke="currentColor" marker-end="url(#ah5)"/>
  <path d="M380,208 L380,226 L545,226 L545,243" fill="none" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="420" y="316" width="250" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="545" y="334" text-anchor="middle" font-size="12" font-weight="bold">L7 deferred_probe_initcall</text>
  <text x="545" y="352" text-anchor="middle" font-size="12">남은 장치 전부 재시도</text>
  <line x1="545" y1="288" x2="545" y2="315" stroke="currentColor" marker-end="url(#ah5)"/>
  <path d="M420,338 L340,338 L340,209" fill="none" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah5)"/>
  <text x="20" y="320" font-size="12">insmod foo.ko 이면: do_init_module →</text>
  <text x="20" y="338" font-size="12">같은 do_one_initcall(foo_driver_init)</text>
  <text x="20" y="356" font-size="12">→ 이후 경로는 위와 똑같다 (G3)</text>
</svg>
```
그림 8 — 드라이버는 initcall에서 태어난다. 장치는 L3에서 DT로부터, 드라이버는 L6에서 `module_init`으로 등록되고, 짝이 맞는 순간 probe가 불린다. 자원이 아직 없으면 `-EPROBE_DEFER` → L7에서 재시도.

### 10.3 드라이버를 쓸 때 다시 만나는 지점

| 이 노트의 개념 | 드라이버 작성 때 | 노트 |
|---|---|---|
| `module_init` = `device_initcall` | built-in 드라이버의 등록 시점, 링크 순서 의존 | E1, G3 |
| `__init` / `__initdata` 해제 | probe 함수에 `__init` 붙이지 말 것, section mismatch 경고 | E1, E3 |
| `subsys_initcall` 등 이른 레벨 | 버스 코어·클럭·PMIC 드라이버가 일반 드라이버보다 먼저 떠야 할 때 | G2, H6 |
| deferred probe | 클럭·레귤레이터·GPIO 공급자가 늦으면 `-EPROBE_DEFER`, `dev_err_probe()` | G3, G4 |
| `unflatten_device_tree` | `of_property_read_*()`, `of_match_table` | D1~D5 |
| earlycon | 새 SoC의 UART가 earlycon을 지원하는지(`OF_EARLYCON_DECLARE`) | I3, P1 |
| `initcall_debug` | 내 드라이버 probe 시간 측정 | C7, M5 |

---

## 11. Embedded 실무에서 다시 보기

### 11.1 장면 1 — "Starting kernel ..." 다음 침묵 (새 보드 bring-up)

U-Boot가 `Starting kernel ...`을 찍고 끝. 순서대로 본다.

1. `bootargs`에 `earlycon`(DT `stdout-path`가 있으면) 또는 `earlycon=<드라이버>,<UART 물리 주소>`를 추가한다. i.MX8은 `earlycon=ec_imx6q,0x30890000`처럼 SoC별 이름이 있다 — 커널 문서 `kernel-parameters.txt`의 `earlycon` 항목에서 이름을 찾는다.
2. 그래도 아무것도 없으면 커널 진입 전 문제다: `booti`의 주소(`kernel_addr_r`, `fdt_addr_r`)가 겹치는지(Image의 `image_size` 고려, §1.6), DTB가 8바이트 정렬·2 MB 이하인지, Image가 정말 arm64 Image인지(`MZ`…`ARMd`). JTAG이 있으면 예제 4처럼 커널 진입점에 하드웨어 브레이크를 걸어 x0을 본다.
3. earlycon이 어느 지점까지 나오다 멈추면: tty0 인계(예제 10)인지, 진짜 hang인지 구분하고, hang이면 `initcall_debug`로 마지막 "calling"을 찾는다. 돌아오지 않은 그 initcall이 범인이다.
4. 커널 로그는 끝까지 나오는데 셸이 없으면: `console=` 이름(`ttymxc0`, `ttyS2`, `ttyAMA0`…)과 "unable to open an initial console"을 확인한다.

### 11.2 장면 2 — eMMC 루트가 마운트 안 됨

`VFS: Unable to mount root fs on unknown-block(0,0)`과 함께 "available partitions" 목록이 비어 있다. 원인 후보: (a) eMMC 컨트롤러 드라이버(`CONFIG_MMC_SDHCI_ESDHC_IMX` 등)가 `=m`이라 아직 없다 → `=y`로 바꾸거나 initramfs에서 모듈을 로드, (b) eMMC probe가 늦게 끝났다 → `rootwait`, (c) `root=/dev/mmcblk0p2`가 다른 보드에선 `mmcblk1`이 된다 → `root=PARTUUID=…`로 고정. 목록에 `mmcblk0p2`가 있는데 `unknown-block(179,2)`로 실패하면 파일시스템이 built-in이 아니거나 파티션 내용이 깨진 것이다.

### 11.3 장면 3 — 부팅 시간 예산 (C7 미리보기)

"전원 ON부터 카메라 스트림까지 3초." 커널 몫을 측정하는 첫 도구가 예제 14다. `initcall_debug` + 스크립트로 느린 initcall 상위 10개를 뽑으면 보통 (1) 제품에 없는 하드웨어의 드라이버, (2) 암호 자가 테스트, (3) 타임아웃을 기다리는 probe(없는 I2C 장치에 재시도), (4) 시리얼 콘솔 출력 자체(115200 baud에서 80자 250줄이면 약 1.7초 — `quiet`)가 나온다. 이 노트의 QEMU에서도 1위·2위가 jitter entropy 테스트와 플래시 probe였다.

### 11.4 장면 4 — 현장 장비는 panic에서 멈추면 안 된다

드론·로봇이 필드에서 panic으로 멈추면 회수 전까지 벽돌이다. 제품 bootargs에는 `panic=5`(또는 `CONFIG_PANIC_TIMEOUT`), 필요하면 `panic_on_oops=1`, 그리고 하드웨어 watchdog을 함께 둔다. A/B 업데이트에서는 U-Boot의 `bootcount`/`bootlimit`가 "새 커널로 3번 연속 panic 재부팅하면 이전 슬롯으로"를 처리한다(C3, O 모듈). 예제 19의 "Rebooting in 2 seconds.."가 그 출발점이다.

---

## 12. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `console=` 누락 + DT에 `stdout-path` 없음 | 시리얼 완전 침묵, 보드는 사실 정상 부팅 | 정식 console이 tty0(VT)로 감 | `console=ttyXXX,115200` 추가, DT `/chosen/stdout-path` 작성 |
| earlycon만 넣고 `console=` 없음 | 앞부분 몇십 줄 후 멈춘 것처럼 보임 | tty0 등록 시 bootconsole 해제 | `console=`도 함께, 또는 `keep_bootcon`으로 확인 |
| 루트 장치 드라이버·fs가 `=m` | "VFS: Unable to mount root fs on unknown-block(0,0)", 파티션 목록 비어 있음 | 모듈을 읽을 fs가 아직 없음 | `=y`로 빌드하거나 initramfs에서 모듈 로드 |
| 느린 장치에 `rootwait` 없음 | 가끔만 루트 마운트 실패 (부팅마다 다름) | probe가 비동기로 늦게 끝남 | `rootwait` (상한은 `rootwait=<초>`) |
| `root=` 오타 + `rootwait` | 영원히 "Waiting for root device" | 없는 장치를 무한 대기 | 로그의 파티션 목록과 대조, `rootwait=10`, `PARTUUID=` 사용 |
| init이 동적 링크인데 로더/라이브러리 누락 | "No working init found" (파일은 있음) | `execve`가 -ENOENT (PT_INTERP 없음, B3 §8) | 정적 링크 BusyBox 또는 `/lib` 채우기, `init=/bin/sh`로 확인 |
| 셸 스크립트 init 끝에 `exec` 누락 | "Attempted to kill init! exitcode=0x00000000" | PID 1 종료 | 마지막 줄을 `exec /sbin/init` 또는 `exec switch_root …` |
| probe 경로에서 `__init` 함수 호출 | 부팅 후(핫플러그·deferred 시) oops, 주소가 0xcccc…를 가리킴 | `.init.text` 해제됨 | `__init` 제거, Section mismatch 경고 해결 |
| `__setup` 핸들러가 0 반환 | 인자가 처리됐는데도 init 환경변수로도 넘어감 | `__setup`은 1이 "처리함" | `return 1;` (`early_param`은 0이 성공) |
| `Image.gz`/`vmlinux`를 `booti`에 | U-Boot "Bad Linux ARM64 Image magic!" 또는 무반응 | 형식 불일치 | `Image` 사용, 압축은 `kernel_comp_addr_r` 설정 |
| 현장 제품에 `panic=` 없음 | panic 후 영구 정지 | `CONFIG_PANIC_TIMEOUT=0` | `panic=5` + watchdog |

---

## 13. 면접에서 이렇게 말한다

**Q.** "Walk me through what happens from the moment the bootloader jumps to the kernel until init runs."

**A.** 부트로더가 x0에 DTB 물리 주소를 넣고 MMU·D-cache를 끈 상태로 Image 맨 앞으로 점프한다. `head.S`가 identity map을 만들고 EL을 정리하고 MMU를 켠 뒤, 커널을 가상 주소로 재배치하고 `start_kernel()`을 부른다. `start_kernel`은 `setup_arch`에서 DTB로 메모리를 알고 early 파라미터(earlycon)를 처리하고, 커맨드라인 파싱, 메모리·스케줄러·인터럽트·타이머·콘솔을 초기화한 뒤 `rest_init`에서 PID 1(`kernel_init`)과 PID 2(kthreadd)를 만들고 자신은 idle이 된다. `kernel_init`은 initcall을 레벨 순으로 돌려 드라이버를 probe하고, initramfs를 풀거나 `root=`를 마운트한 뒤 `.init` 메모리를 해제하고 `/init`을 execve한다.

> The bootloader loads the Image and jumps to its first instruction with x0 holding the physical address of the device tree, MMU and D-cache off, at EL2 or EL1. head.S builds an identity map, sets up the exception level, turns on the MMU, relocates the kernel to its virtual address and calls start_kernel. start_kernel runs setup_arch, which parses the DTB for memory and handles early params like earlycon, then initializes memory management, the scheduler, IRQs, timers and the console, and finally rest_init spawns PID 1, kernel_init, and kthreadd, while the boot CPU becomes the idle task. kernel_init runs the initcalls level by level, which is where built-in drivers register and probe, then either unpacks the initramfs and runs /init, or mounts root= itself, frees the init sections — "Freeing unused kernel memory" — and execs init.

**꼬리 질문**: "Where does the kernel learn how much RAM there is?" → DTB의 `/memory` 노드(`setup_machine_fdt` → `early_init_dt_scan`), UEFI 시스템이면 EFI 메모리 맵.

**Q.** "What are initcalls, and how are they ordered?"

**A.** `module_init`이나 `subsys_initcall` 같은 매크로가 함수 포인터를 `.initcallN.init` 링커 섹션에 넣고, 링커 스크립트가 레벨 순으로 모아 배열을 만든다. 부팅 중 `do_initcalls`가 레벨 0 pure부터 1 core, 2 postcore, 3 arch, 4 subsys, 5 fs, rootfs, 6 device, 7 late 순으로 배열을 돈다. built-in 드라이버의 `module_init`은 레벨 6이고, 같은 레벨 안은 링크 순서다. 제가 측정한 defconfig 커널은 1,887개 중 1,183개가 device 레벨이었다.

> An initcall is a function pointer placed in a dedicated linker section by macros like core_initcall, subsys_initcall or module_init. The linker script collects those sections in level order into one array, and do_initcalls walks it: pure, core, postcore, arch, subsys, fs, rootfs, device, late. Built-in module_init maps to device_initcall, level 6, and within a level the order is link order, so you shouldn't depend on it. It's the same trick firmware uses with a section-based init table. On a defconfig arm64 kernel I counted 1,887 initcalls, 1,183 of them at device level.

**꼬리 질문**: "Your driver needs the I2C bus core before it probes — what do you do?" → 레벨에 기대지 말고 의존 자원이 없으면 `-EPROBE_DEFER`를 반환한다. 코어 자체는 `subsys_initcall`로 이미 앞에 있다.

**Q.** "The board prints 'Starting kernel ...' and then nothing. How do you debug it?"

**A.** 먼저 `earlycon`을 넣어 커널이 살아 있는지 가른다. 아무것도 안 나오면 커널 진입 전 문제라 로드 주소, DTB 주소·정렬, Image 형식을 보고, 가능하면 JTAG으로 커널 진입점에서 x0을 본다. earlycon이 나오면 어디서 끊기는지 보고, tty0으로 콘솔이 넘어간 건지 진짜 hang인지 구분한다. hang이면 `initcall_debug`로 돌아오지 않은 initcall을 찾고 `initcall_blacklist`로 확인한다. 로그는 끝까지 나오는데 셸이 없으면 `console=` 이름과 "unable to open an initial console"을 확인한다.

> First I split "kernel dead" from "console not set up" by adding earlycon — bare earlycon if the DT has stdout-path, otherwise earlycon=driver,address. If nothing appears, the problem is before the kernel: load addresses overlapping, DTB alignment, wrong image format — with JTAG I'd break at the kernel entry and check x0. If earlycon prints and then stops, I check whether it just handed over to tty0 because console= is missing, or whether it's a real hang; for a hang, initcall_debug shows the last "calling" line without a "returned", and initcall_blacklist confirms the culprit. If the kernel log completes but there's no shell, I look for "unable to open an initial console" and a wrong console= name.

**꼬리 질문**: "Why does output appear all at once when the console comes up?" → printk는 링 버퍼에 쓰고, console 등록 시 버퍼 내용을 재생하기 때문.

**Q.** "What's the difference between earlycon and console?"

**A.** earlycon은 `setup_arch`에서 early_param으로 켜지는 폴링 방식 UART 출력으로, 정식 드라이버·인터럽트·클럭 프레임워크 없이 레지스터에 직접 쓴다. 정식 console이 등록되면 자동으로 꺼진다. console은 tty 드라이버가 probe된 뒤 등록되고 `/dev/console`로 유저 공간까지 이어진다. 그래서 earlycon은 "콘솔 드라이버가 뜨기 전의 크래시"를 보기 위한 도구다.

> earlycon is a minimal polled UART writer enabled from setup_arch via early_param — it pokes the UART registers directly, no IRQs, no clock framework, no tty layer — so you see messages from the first milliseconds. It's a boot console and gets unregistered when a real console registers. The real console comes from the serial driver after it probes, during initcalls, and it's what /dev/console and init's stdout connect to. So earlycon is for crashes before the serial driver is up.

**Q.** "initramfs vs mounting root= directly — when do you use which?"

**A.** `root=` 직접 마운트는 단순하고 빠르지만, 루트 장치 드라이버와 파일시스템이 built-in이어야 하고 결정 로직을 넣을 수 없다. initramfs는 커널이 rootfs에 풀어 `/init`을 실행하고, 그 `/init`이 모듈 로드·암호 해제·A/B 슬롯 선택·네트워크 설정 후 `switch_root`한다. initramfs에 `/init`이 있으면 커널은 `root=`를 무시한다 — 그 값은 initramfs 스크립트가 읽는다.

> Mounting root= directly is simplest and fastest: the kernel waits for the block device, mounts it read-only and execs /sbin/init, but the storage driver and filesystem must be built in and there's no room for logic. With an initramfs the kernel unpacks a cpio into rootfs and runs /init, and that userspace script loads modules, unlocks dm-crypt, picks an A/B slot or sets up the network, then switch_roots. If the initramfs has an /init, the kernel ignores root= entirely — it's just a parameter for the initramfs script to read.

**꼬리 질문**: "What does 'VFS: Unable to mount root fs on unknown-block(0,0)' tell you?" → `root=`를 장치 번호로 해석하지 못함 — 오타이거나 드라이버가 없어 장치가 없다. 파티션 목록이 비어 있으면 드라이버, `(179,2)`처럼 번호가 있으면 파일시스템 쪽.

**Q.** "What does 'Freeing unused kernel memory' mean, and why should a driver writer care?"

**A.** `__init`/`__initdata`로 표시한 코드·데이터를 모은 `.init` 섹션을 부팅 끝에 해제하는 것이다. 제 측정에서 3.2 MB였다. 드라이버 입장에서는 부팅 후에도 불릴 수 있는 probe나 콜백에 `__init`을 붙이면 해제된 메모리로 점프해 oops가 난다. modpost의 section mismatch 경고가 이걸 잡는다. 모듈도 init 함수가 끝나면 `.init` 부분을 해제한다.

> All functions and data marked __init or __initdata live in the .init sections — start_kernel, setup_arch, every built-in initcall. Once init is done, kernel_init calls free_initmem and gives those pages back; on the arm64 kernel I measured it was 3.2 MB. For driver writers it means anything that can run after boot — probe, which can be deferred or hot-plugged, callbacks — must not be __init, otherwise you jump into poisoned, freed memory. modpost's section-mismatch warnings catch most of these. Modules do the same: their init section is freed after the init function returns.

**Q.** "What happens if PID 1 exits?"

**A.** 커널이 "Attempted to kill init!"으로 panic한다. 종료 코드가 0이어도 그렇다. PID 1은 고아 프로세스를 거두는 역할이라 없어지면 시스템이 성립하지 않는다. 그래서 셸 스크립트 init은 마지막에 `exec`로 진짜 init이나 `switch_root`로 넘어가야 한다.

> The kernel panics with "Attempted to kill init!" — even with exit code zero; I reproduced it with init=/bin/true. PID 1 reaps orphans and anchors the process tree, so the kernel won't run without it. That's why a shell-script init must end with exec, either into the real init or into switch_root, never just fall off the end.

**꼬리 질문**: "And in production?" → `panic=<초>`로 자동 재부팅 + watchdog, A/B 부팅이면 bootcount로 롤백.

---

## 14. 직접 해보기

1. (개념) `Image` 헤더의 `image_size`가 `Image` 파일 크기보다 큰 이유는? 부트로더가 이 값을 무시하고 DTB를 커널 파일 끝 바로 뒤에 두면 무슨 일이 생기나?
   정답/힌트: `.bss`. 커널이 `.bss`를 0으로 채우면서 DTB를 지운다 → `setup_machine_fdt`에서 "invalid device tree blob" 무한 루프(콘솔 없음).
2. (관찰) 예제 4의 gdb 스크립트에서 브레이크포인트를 `primary_entry`의 물리 주소(0x40200000 + 0x1fea0f0)에 걸고 x21에 무엇이 들어가는지 몇 명령 진행(`stepi`)하며 본다.
   정답/힌트: `preserve_boot_args`가 x0을 x21에 보관한다. `x/4i $pc`로 확인.
3. (코드) `vmlinux`의 `.init.data`에서 레벨 3 initcall 엔트리(4바이트 상대 오프셋)를 Python으로 읽어 대상 함수 주소를 계산하고 `System.map`으로 이름을 붙여, `initcall_debug` 로그의 arch 레벨 "calling" 순서와 같은지 비교하라.
   정답/힌트: 엔트리 주소 + `int32` 값 = 함수 주소. 파일 오프셋 = 주소 - `.init.data` 주소 + 섹션 오프셋(예제 13의 readelf 값).
4. (관찰) `earlycon=pl011,0x9000000 keep_bootcon`으로 `c6_nostdout.dtb` 부팅을 하면 예제 10과 무엇이 달라지나?
   정답/힌트: bootconsole이 tty0 등록 후에도 남아 전체 커널 로그가 나온다(단, init의 출력은 여전히 tty0로).
5. (실패 재현) `rootfstype=xfs root=/dev/vda`로 ext4 디스크를 부팅하면 어떤 메시지가 나오나? `unknown-block(…)` 안의 숫자는?
   정답/힌트: 장치는 찾았으므로 `unknown-block(254,0)`, xfs가 built-in이 아니라 "error -19"(ENODEV)로 마운트 실패 → VFS panic. 예제 19의 `(0,0)`·error -6과 비교.
6. (코드) 셸 스크립트 init을 만들어 ext4 이미지의 `/sbin/init`으로 넣되 마지막 줄의 `exec`을 빼고 부팅해 "Attempted to kill init!"을 재현한 뒤, `exec /bin/sh`로 고쳐 확인한다.
   정답/힌트: `#!/bin/sh` 스크립트에 실행 권한 필수. 권한이 없으면 -EACCES(-13)로 "Run /sbin/init" 다음 후보로 넘어간다.
7. (측정) `initcall_debug`를 3번 돌려 상위 5개의 순위와 값이 얼마나 흔들리는지 보고, 이른 레벨(jiffies 시계)과 device 레벨(arch timer)의 측정 해상도 차이를 설명하라.
   정답/힌트: `clocksource_done_booting`(fs 레벨) 전은 4 ms 단위. 이른 레벨은 printk 타임스탬프 차이로 재는 것이 낫다.

---

## 15. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| `Image` | arm64 커널 바이너리 | `vmlinux`를 평평하게 뽑고 64바이트 헤더를 붙인 것. 부트로더가 로드 |
| EFI stub | UEFI 실행 가능 커널 | "MZ" + PE 헤더로 같은 `Image`를 UEFI 앱으로도 실행 |
| boot protocol | 부트로더-커널 계약 | x0 = DTB, MMU off, D-cache off, EL2/EL1, DAIF 마스크 |
| `head.S` | 커널 첫 코드 | identity map, EL 설정, MMU on, 재배치 후 `start_kernel` 호출 |
| identity map | 물리 = 가상 매핑 | MMU를 켜는 순간 PC가 유효하도록 하는 임시 페이지 테이블 |
| `start_kernel` | 첫 C 함수 | 아키텍처·메모리·스케줄러·IRQ·타이머·콘솔 초기화 후 `rest_init` |
| `setup_arch` | 아키텍처 초기화 | DTB 스캔, early_param, 페이지 테이블, DT 펼치기, PSCI |
| unflatten | DT 펼치기 | FDT 바이트열 → `struct device_node` 트리 |
| earlycon | 조기 콘솔 | 드라이버 없이 UART 레지스터에 직접 쓰는 bootconsole |
| bootconsole | 임시 콘솔 | 정식 console 등록 시 자동 해제되는 콘솔 |
| `early_param` | 조기 인자 | `setup_arch` 안에서 처리되는 커맨드라인 인자, 0 = 성공 |
| `__setup` | 일반 인자 | `parse_args` 단계에서 처리, 1 = 처리함 |
| initcall | 초기화 함수 표 | 링커 섹션에 레벨별로 모은 함수 포인터 배열 |
| `initcall_debug` | 측정 옵션 | initcall·probe마다 호출과 소요 시간을 로그에 남김 |
| `initcall_blacklist` | 건너뛰기 옵션 | 이름으로 지정한 initcall을 실행하지 않음 |
| rootfs | 최초 루트 | 커널이 처음 만드는 메모리 파일시스템 (ramfs/tmpfs) |
| initramfs | 초기 유저 공간 | rootfs에 풀리는 cpio. 그 안의 `/init`이 PID 1 |
| `rdinit=` | initramfs init 경로 | 기본 `/init` |
| `root=` / `rootwait` | 루트 장치 / 대기 | 커널이 직접 마운트할 장치와 나타날 때까지 기다리기 |
| `init=` | init 경로 | 지정 시 실패하면 대체 후보 없이 panic |
| `panic=` | panic 후 동작 | 양수 N이면 N초 후 재부팅, 0이면 영원히 정지 |
| `free_initmem` | init 메모리 해제 | `.init` 섹션 반납, "Freeing unused kernel memory" |
| deferred probe | 지연 probe | `-EPROBE_DEFER` 장치를 나중에(L7 등) 재시도 |

---

## 16. 요약 & 체크리스트

arm64 커널 시작은 펌웨어 리셋 시퀀스와 같은 모양이다. 부트로더는 x0에 DTB 주소를 넣고 MMU를 끈 채 `Image` 첫 바이트로 점프한다. 그 64바이트 헤더는 부트로더용 명함(크기·플래그·"ARMd" 매직)이자 UEFI용 PE 헤더("MZ")이자 실행 코드다. `head.S`가 identity map을 만들고 MMU를 켜 `start_kernel`로 들어가면, `setup_arch`가 DTB를 읽고 earlycon 같은 early 파라미터를 처리하고, `start_kernel`이 메모리·스케줄러·IRQ·타이머·콘솔을 차례로 세운 뒤 `rest_init`이 PID 1과 2를 만든다. PID 1(`kernel_init`)은 링커 섹션으로 만든 initcall 표를 레벨 순으로 돌며 드라이버를 등록·probe하고(측정하면 device 레벨이 시간 대부분), initramfs의 `/init` 또는 `root=` 장치를 루트로 삼아 `.init` 메모리를 해제한 뒤 init을 `execve`한다. 실패는 메시지로 구분된다 — 침묵은 콘솔 문제(earlycon으로 확인), "Unable to mount root fs"는 장치·드라이버·fs 문제(파티션 목록 확인), "No working init found"와 "Attempted to kill init!"은 PID 1 문제다.

- [ ] arm64 `Image` 헤더 필드(code0/1, text_offset, image_size, flags, magic, PE 오프셋)를 hexdump에서 짚어 설명할 수 있다
- [ ] 부트 프로토콜(x0 = DTB, x1~x3 = 0, MMU·D-cache off, EL2/EL1, DAIF 마스크)과 각 조건이 왜 필요한지 말할 수 있다
- [ ] `primary_entry` → `__primary_switch` → `__primary_switched` → `start_kernel` → `rest_init` → `kernel_init` 경로를 그릴 수 있다
- [ ] `setup_arch`에서 DTB가 어떻게 쓰이는지(메모리, bootargs, initrd 위치, unflatten) 설명할 수 있다
- [ ] earlycon과 console의 차이, bootconsole 인계, "콘솔 침묵"의 진단 순서를 설명할 수 있다
- [ ] `early_param` / `__setup` / 모듈 파라미터의 차이와 반환값 규칙, 모르는 인자가 init의 argv·envp로 가는 규칙을 말할 수 있다
- [ ] initcall 레벨 9단계를 순서대로 말하고 `module_init`이 어느 레벨인지 안다
- [ ] `initcall_debug`로 느린 initcall을 측정하고, 이른 레벨의 시간 해상도 함정을 설명할 수 있다
- [ ] initramfs와 `root=` 경로의 분기 조건(`rdinit` 접근 가능 여부)을 설명하고 ext4 디스크로 부팅해 볼 수 있다
- [ ] "VFS: Unable to mount root fs", "No working init found", "Requested init … failed", "Attempted to kill init!"을 재현하고 원인을 말할 수 있다
- [ ] "Freeing unused kernel memory"가 무엇이고 드라이버의 `__init` 사용에 어떤 제약을 주는지 설명할 수 있다
- [ ] device initcall에서 드라이버가 등록·probe되고 deferred probe가 late_initcall에서 재시도되는 흐름을 설명할 수 있다

---

## 참고 자료

- 커널 문서 — arm64 부트 프로토콜: [Booting AArch64 Linux](https://docs.kernel.org/arch/arm64/booting.html) (소스 트리 `Documentation/arch/arm64/booting.rst`)
- 커널 문서 — 커맨드라인 파라미터 전체 목록: [The kernel's command-line parameters](https://docs.kernel.org/admin-guide/kernel-parameters.html) (`earlycon`, `console`, `root`, `rootwait`, `init`, `rdinit`, `panic`, `initcall_debug`, `initcall_blacklist`, `keep_bootcon`)
- 커널 문서 — "No working init found" 안내: [Explaining the "No working init found." boot hang message](https://docs.kernel.org/admin-guide/init.html)
- 커널 문서 — rootfs와 initramfs: [Ramfs, rootfs and initramfs](https://docs.kernel.org/filesystems/ramfs-rootfs-initramfs.html)
- 커널 문서 — 시리얼 콘솔: [Linux Serial Console](https://docs.kernel.org/admin-guide/serial-console.html)
- 소스 (6.18): `arch/arm64/kernel/head.S`, `arch/arm64/kernel/efi-header.S`, `arch/arm64/kernel/setup.c`, `init/main.c`, `init/do_mounts.c`, `init/initramfs.c`, `include/linux/init.h`, `drivers/base/dd.c`, `drivers/tty/serial/earlycon.c` — [elixir.bootlin.com/linux/v6.18/source](https://elixir.bootlin.com/linux/v6.18/source)
- Bootlin — Embedded Linux system development 교육 자료(부트 과정, 커널 부팅, root filesystem 장): [bootlin.com/training/embedded-linux](https://bootlin.com/training/embedded-linux/)
- 관련 노트: C1(부트 전체 지도), C3(U-Boot `booti`·`bootargs`), C5(EL·TF-A·PSCI), C7(init·부팅 시간), B3(ELF·링커 섹션·`.init`), D1~D5(Device Tree), E1·E3(모듈), G3(probe와 드라이버 모델)
