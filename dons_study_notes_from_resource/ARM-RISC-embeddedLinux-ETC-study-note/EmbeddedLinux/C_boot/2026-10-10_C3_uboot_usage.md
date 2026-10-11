# C3. U-Boot 사용 — 콘솔·환경변수·부팅 방법, "bring-up 첫날 U-Boot 프롬프트에서 하는 모든 일"

> **이 노트를 다 읽으면**: U-Boot 프롬프트에서 `md`/`mw`로 DRAM과 PL011 레지스터를 직접 찔러 볼 수 있다 · 환경변수가 flash 어디에 어떤 모양(CRC32 + `key=value\0`)으로 사는지 hexdump와 `fw_printenv`로 증명할 수 있다 · 디스크(FAT/ext4)·TFTP·extlinux(standard boot)·FIT 네 가지 방법으로 Linux를 부팅하고 `booti`/`bootm` 인자를 한 글자씩 설명할 수 있다 · `fdt set`으로 DT를 고쳐 게스트의 `/proc/device-tree`에서 확인하고, 부팅 실패 메시지를 보고 원인을 가를 수 있다
> **왜 필요한가**: 새 보드가 오면 첫날 하는 일이 "U-Boot 프롬프트까지 띄우고, 메모리 찍어 보고, TFTP로 커널 올려서 부팅"이다. 면접에서는 "Walk me through how U-Boot boots Linux", "What is bootargs/bootcmd?", "What is a FIT image and why use it?", "Kernel hangs after Starting kernel — what do you check?"가 단골이다
> **Don 기준 난이도**: 메모리 맵·레지스터 읽기/쓰기·부트로더가 이미지 검증 후 점프하는 구조는 SSD 펌웨어에서 이미 한 일이다 / U-Boot의 명령 체계, 환경변수 수명주기, Linux 부트 프로토콜(Image 헤더·DTB 주소·initrd 크기), standard boot(bootflow)·FIT가 새로 배울 부분
> **선행 노트**: C1 (부트 전체 지도), B1 (크로스 툴체인) — C5(TF-A 신뢰 부팅), C6(커널 시작 과정), D 모듈(Device Tree)과 이어진다

---

## 0. 큰 그림 — 이게 왜 필요한가

Don의 SSD 컨트롤러를 떠올려 보자. 전원이 들어오면 ROM 코드가 NAND(또는 SPI NOR)에서 부트로더를 읽고, 부트로더가 DRAM을 초기화하고, 메인 펌웨어 이미지를 찾아 CRC를 확인한 뒤 엔트리 포인트로 점프했다. 개발 중에는 UART에 "디버그 셸"이 있어서 레지스터를 읽고 쓰고, 이미지를 다른 슬롯에서 올려 보았다.

**U-Boot는 그 부트로더 + 디버그 셸을 Linux용으로 일반화한 것**이다. 하는 일은 딱 네 가지로 요약된다.

1. 보드를 "Linux를 올릴 수 있을 만큼" 초기화한다 (DRAM, 시리얼, 스토리지·네트워크 드라이버).
2. 커널(`Image`), Device Tree(DTB), initramfs를 **어딘가(SD/eMMC/USB/네트워크)에서 DRAM의 정해진 주소로 읽어 온다**.
3. 커널에 넘길 정보(커널 명령줄 `bootargs`, initrd 위치)를 **DTB의 `/chosen` 노드에 써 넣는다**.
4. Linux arm64 부트 프로토콜대로 레지스터를 맞추고(`x0` = DTB 물리 주소) 커널로 점프한다.

그리고 이 모든 과정이 **콘솔 명령과 환경변수로 노출**되어 있어서, 사람이 프롬프트에서 한 줄씩 칠 수도 있고 `bootcmd`라는 변수에 넣어 자동으로 돌릴 수도 있다. 이 노트는 그 "콘솔에서 하는 일"을 전부 직접 해 본다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="20" width="118" height="54" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="69" y="43" text-anchor="middle" font-size="13">BootROM</text>
  <text x="69" y="62" text-anchor="middle" font-size="12">칩 내장 ROM</text>
  <rect x="146" y="20" width="118" height="54" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-dasharray="4 3"/>
  <text x="205" y="43" text-anchor="middle" font-size="13">TF-A (C5)</text>
  <text x="205" y="62" text-anchor="middle" font-size="12">BL1/BL2/BL31</text>
  <rect x="282" y="20" width="118" height="54" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="2"/>
  <text x="341" y="43" text-anchor="middle" font-size="13" font-weight="bold">U-Boot</text>
  <text x="341" y="62" text-anchor="middle" font-size="12">이 노트 (C3)</text>
  <rect x="418" y="20" width="118" height="54" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="477" y="43" text-anchor="middle" font-size="13">Linux kernel</text>
  <text x="477" y="62" text-anchor="middle" font-size="12">C6</text>
  <rect x="554" y="20" width="118" height="54" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="613" y="43" text-anchor="middle" font-size="13">initramfs</text>
  <text x="613" y="62" text-anchor="middle" font-size="12">/init (C7)</text>
  <line x1="128" y1="47" x2="144" y2="47" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="264" y1="47" x2="280" y2="47" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="400" y1="47" x2="416" y2="47" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="536" y1="47" x2="552" y2="47" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="282" y1="74" x2="140" y2="112" stroke="#4a7bd0" stroke-dasharray="4 3"/>
  <line x1="400" y1="74" x2="540" y2="112" stroke="#4a7bd0" stroke-dasharray="4 3"/>
  <rect x="140" y="112" width="400" height="208" rx="8" fill="#4a7bd0" fill-opacity="0.06" stroke="#4a7bd0"/>
  <text x="340" y="134" text-anchor="middle" font-size="13" font-weight="bold">U-Boot 안에서 하는 일</text>
  <rect x="160" y="146" width="360" height="30" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="166" text-anchor="middle" font-size="12">콘솔: help · md · mw · bdinfo (§2, §3)</text>
  <rect x="160" y="180" width="360" height="30" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="200" text-anchor="middle" font-size="12">환경변수: bootcmd · bootargs · saveenv (§4, §5)</text>
  <rect x="160" y="214" width="360" height="30" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="234" text-anchor="middle" font-size="12">읽어 오기: load · tftp · bootflow (§6, §8, §9)</text>
  <rect x="160" y="248" width="360" height="30" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="268" text-anchor="middle" font-size="12">DT 고치기: fdt set /chosen (§7)</text>
  <rect x="160" y="282" width="360" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="302" text-anchor="middle" font-size="12">점프: booti · bootm (FIT) (§6, §10)</text>
</svg>
```
그림 1 — 부트 체인에서 U-Boot의 위치와, 이 노트가 다루는 U-Boot 안의 다섯 가지 일. 이 랩의 `-bios u-boot.bin` 경로는 TF-A(점선)를 건너뛰고 QEMU가 바로 U-Boot를 실행한다 (TF-A 경로는 C5).

Embedded Linux 엔지니어가 U-Boot 프롬프트를 만나는 장면은 이렇다.

- **보드 bring-up 첫날**: JTAG 대신 U-Boot 콘솔에서 `md`로 SoC 레지스터(클럭·핀먹스·UART)를 읽어 데이터시트와 비교한다 (§3).
- **개발 루프**: 커널을 고칠 때마다 SD 카드를 뺐다 꽂지 않고, 개발 PC의 TFTP 서버에서 `tftp`로 받아 부팅한다 (§8).
- **양산 이미지**: 커널·DTB·initramfs를 FIT 하나로 묶고 해시(나중에는 서명)로 검증한다 (§10, C5).
- **현장 장애**: "Starting kernel ... 다음에 아무것도 안 나와요" — 로드 주소 겹침, 잘못된 DTB, initrd 크기 누락을 U-Boot 메시지로 가른다 (§11).

> 실행 환경: 모든 출력은 Lima VM `elinux`(aarch64, `uname -r` = `7.0.0-38-generic`) 안에서 공용 랩 `/var/lab/`의 **U-Boot v2026.10**(`out/u-boot.bin`, `qemu_arm64` defconfig) → **Linux 6.18.55**(`out/Image`) → **BusyBox initramfs**(`out/initramfs.cpio.gz`)를 QEMU 10.2.1(`-machine virt -cpu cortex-a57 -m 512M`, KVM 없는 TCG)로 돌려 얻은 실제 결과다. 시리얼 로그는 `expect`로 받았고 ANSI escape와 `\r`만 지웠다. 주소 중 일부(`fdtcontroladdr`, `rng-seed`, MAC 이외의 임시 값, 시간)는 실행마다 다를 수 있다. 작업 디렉터리는 VM의 `/var/tmp/C3/`, 스크립트는 Mac의 `~/.elinux_scratch/C3/`.

---

## 1. 랩 준비 — QEMU에서 U-Boot 프롬프트 띄우기

### 1.1 `-bios u-boot.bin`이 하는 일

QEMU `virt` 머신은 실제 보드처럼 "리셋 벡터 0x0에 펌웨어가 있는" 구조다. `-bios u-boot.bin`을 주면 QEMU가 U-Boot를 flash 0번(주소 `0x00000000`)에 올리고 CPU를 거기서 출발시킨다. 즉 **U-Boot가 BootROM 다음 단계 부트로더 역할**을 한다. 동시에 QEMU는 자기가 만든 Device Tree를 **DRAM 맨 앞 `0x40000000`**에 넣어 준다 — 실제 보드라면 U-Boot가 자기 이미지에 DTB를 내장하지만, QEMU는 "하드웨어가 DT를 들고 오는" 특이한 보드다(§6.4에서 이 차이가 실패 원인이 되는 것을 본다).

```sh
# VM 안에서 — 손으로 띄워 보기 (Ctrl-A x로 종료)
qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
    -bios /var/lab/out/u-boot.bin
```

### 1.2 예제 1 — `expect`로 U-Boot 세션 자동화

**무엇을 확인하나**: 자동화된 U-Boot 세션을 만들어, 이후 모든 예제를 같은 방식으로 재현 가능하게 한다. 아래 Tcl 스크립트는 명령 파일을 한 줄씩 보내고 `=> ` 프롬프트를 기다린다. `@NOWAIT`(부팅 명령처럼 프롬프트가 돌아오지 않는 명령), `@EXPECT 정규식`, `@MARK 이름`(호스트 시계로 경과 ms 기록 — §12의 측정에 쓴다)을 지원한다.

```text
#!/usr/bin/expect -f
# ub.exp <logfile> <cmdfile> <qemu args...>
set logf [lindex $argv 0]; set cmdf [lindex $argv 1]; set qargs [lrange $argv 2 end]
set timeout 60
log_file -noappend $logf
set t0 [clock milliseconds]
spawn {*}$qargs
proc mark {name} { global t0; send_user "\n@@MARK $name [expr {[clock milliseconds]-$t0}] ms\n" }
expect { "Hit any key to stop autoboot" { send " " } timeout { exit 1 } }
expect "=> "
mark prompt
set f [open $cmdf]
while {[gets $f line] >= 0} {
  if {$line eq ""} continue
  if {[string match "@LINUX*" $line]} {
    expect { -re "Power down|/ # " { mark linux_done } timeout {} eof { exit 0 } }
    continue
  }
  if {[string match "@EXPECT *" $line]} {
    set re [string range $line 8 end]
    expect { -re $re { mark "seen:$re" } timeout {} eof { exit 0 } }
    continue
  }
  if {[string match "@MARK *" $line]} { mark [string range $line 6 end]; continue }
  if {[string match "@NOWAIT *" $line]} { send "[string range $line 8 end]\r"; continue }
  send "$line\r"
  expect { "=> " {} timeout {} eof { exit 0 } }
}
close $f
catch {send "\001x"}
catch {expect eof}
```

실행 래퍼는 QEMU 기본 인자를 붙이고, 로그에서 ANSI escape(`ESC[...m`, 커서 저장/복원 `ESC 7`/`ESC 8`)와 `\r`을 지운다.

```sh
# run.sh <name> <cmdfile> [추가 qemu 인자...]
Q="qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic -bios /var/lab/out/u-boot.bin"
expect -f ub.exp /var/tmp/C3/$name.raw $cmdf $Q "$@" > /var/tmp/C3/$name.stdout 2>&1
sed -r 's/\x1b\[[0-9;?]*[a-zA-Z]//g; s/\x1b[()][AB0]//g; s/\x1b[78]//g' /var/tmp/C3/$name.raw \
    | tr -d '\r' > /var/tmp/C3/$name.log
```

U-Boot가 뜰 때 찍는 배너(실제 출력):

```text
U-Boot 2026.10 (Oct 10 2026 - 09:59:26 -0700)

DRAM:  512 MiB
using memory 0x5e44f000-0x5f48f000 for malloc()
Core:  51 devices, 14 uclasses, devicetree: board
Flash: 64 MiB
Loading Environment from Flash...*** Warning - bad CRC, using default environment

In:    serial,usbkbd
Out:   serial,vidconsole
Err:   serial,vidconsole
No USB controllers found
Net:   eth0: virtio-net#32

Hit any key to stop autoboot: 2Hit any key to stop autoboot: 0
=>
```

**출력에서 볼 것**: 배너 한 줄 한 줄이 초기화 단계다. `DRAM: 512 MiB`(DT의 memory 노드), `using memory ... for malloc()`(U-Boot가 DRAM 꼭대기로 자기 자신을 옮긴 뒤 잡은 heap), `Core: 51 devices, 14 uclasses`(driver model이 DT를 보고 만든 장치 수 — C4), `Flash: 64 MiB`(NOR flash 드라이버), `Loading Environment from Flash...bad CRC`(**환경변수 저장소가 비어 있어서 기본값을 쓴다** — §4의 주제), `Net:`(virtio-net 드라이버). 마지막 줄은 원래 `\r`로 같은 줄을 덮어쓰는 카운트다운(2 → 0)인데, `\r`을 지워서 한 줄에 이어 붙은 것이다. `expect`가 공백 키를 보내 autoboot를 멈추고 `=> ` 프롬프트를 얻었다.

> **함정**: autoboot 카운트다운은 `bootdelay` 초(여기서는 2)뿐이다. 실제 보드에서도 시리얼 터미널을 늦게 열면 프롬프트를 놓치고 `bootcmd`가 그대로 돈다. 양산 보드는 `bootdelay=0`이나 `-2`(키 입력 확인도 안 함)로 두는 경우가 많아서, 개발 중에는 "키를 누르고 전원을 넣는" 습관이 필요하다.

---

## 2. 콘솔 기본 — help · version · bdinfo · coninfo

### 2.1 직관 — RTOS 디버그 셸과 같다

U-Boot 콘솔은 Don이 SSD 펌웨어에 넣었던 UART 디버그 셸과 같은 구조다: 명령 테이블(`struct cmd_tbl`)에 이름·최대 인자 수·핸들러 함수가 등록되어 있고, 셸이 한 줄을 읽어 토큰으로 나눈 뒤 핸들러를 부른다. 다른 점은 셸이 **hush**(BusyBox에서 가져온 작은 POSIX 셸 문법: 변수, `if`, `for`, `&&`, `||`)라는 것이다(§5).

명령은 `.config`의 `CONFIG_CMD_*`로 켜고 끈다. 이 랩의 U-Boot에서 켜진 것 일부:

```text
CONFIG_CMD_BDI=y        CONFIG_CMD_MEMORY=y     CONFIG_CMD_FDT=y
CONFIG_CMD_BOOTI=y      CONFIG_CMD_BOOTM=y      CONFIG_CMD_BOOTFLOW=y
CONFIG_CMD_SAVEENV=y    CONFIG_CMD_EDITENV=y    CONFIG_CMD_SETEXPR=y
CONFIG_CMD_DHCP=y       CONFIG_CMD_TFTPBOOT=y   CONFIG_CMD_PXE=y
CONFIG_CMD_FAT=y        CONFIG_CMD_EXT4=y       CONFIG_CMD_VIRTIO=y
CONFIG_ENV_IS_IN_FLASH=y  CONFIG_ENV_ADDR=0x4000000  CONFIG_ENV_SIZE=0x40000
CONFIG_BOOTDELAY=2      CONFIG_BOOTCOMMAND="bootflow scan -lb"
```

(`/var/lab/build/u-boot/.config`에서 `grep`한 줄을 세 열로 모아 놓은 것이다.) 실제 보드의 양산 U-Boot는 보안 때문에 `md`/`mw`조차 꺼 두기도 한다 — "명령이 없다"(`Unknown command 'md' - try 'help'`)면 먼저 config를 의심한다.

### 2.2 예제 2 — 버전, 보드 정보, 콘솔 장치

**무엇을 확인하나**: 빌드 정보, DRAM 범위, U-Boot 자신이 어디에 재배치(relocate)되었는지, 콘솔이 어떤 장치에 붙었는지.

```text
=> version
U-Boot 2026.10 (Oct 10 2026 - 09:59:26 -0700)

gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0
GNU ld (GNU Binutils for Ubuntu) 2.46
=> help md
md - memory display

Usage:
md [.b, .w, .l, .q] address [# of objects]
=> bdinfo
boot_params = 0x0000000000000000
DRAM bank   = 0x0000000000000000
-> start    = 0x0000000040000000
-> size     = 0x0000000020000000
flashstart  = 0x0000000000000000
flashsize   = 0x0000000004000000
flashoffset = 0x000000000016f3d0
baudrate    = 115200 bps
relocaddr   = 0x000000005f48f000
reloc off   = 0x000000005f48f000
Build       = 64-bit
current eth = virtio-net#32
ethaddr     = 52:54:00:12:34:56
IP addr     = <NULL>
fdt_blob    = 0x000000005e34ed80
lmb_dump_all:
 memory.count = 0x1
 memory[0]	[0x40000000-0x5fffffff], 0x20000000 bytes, flags: none
 reserved.count = 0x2
 reserved[0]	[0x5d32dff0-0x5fdfffff], 0x2ad2010 bytes, flags: no-overwrite
 reserved[1]	[0x5fffd000-0x5fffffff], 0x3000 bytes, flags: no-notify, no-overwrite
devicetree  = board
serial addr = 0x0000000009000000
 width      = 0x0000000000000004
 shift      = 0x0000000000000002
 offset     = 0x0000000000000000
 clock      = 0x0000000000000000
arch_number = 0x0000000000000000
TLB addr    = 0x000000005fde0000
irq_sp      = 0x000000005e32dff0
sp start    = 0x000000005e32dff0
Early malloc usage: e88 / 2000
=> coninfo
List of available devices
|-- pl011@9000000 (IO)
|-- serial (IO)
|   |-- stdin
|   |-- stdout
|   |-- stderr
|-- nulldev (IO)
```

**출력에서 볼 것**:

- `DRAM bank start 0x40000000, size 0x20000000` — QEMU `virt`의 DRAM은 1 GiB 경계(`0x4000_0000`)에서 시작해 512 MiB. 이후 모든 로드 주소는 이 범위 안이어야 한다.
- `relocaddr = 0x5f48f000` — U-Boot는 flash(주소 0)에서 출발하지만, 초기화 후 **자기 코드를 DRAM 꼭대기로 복사(relocation)하고 거기서 계속 돈다**. Don의 SSD 부트로더가 SRAM/TCM으로 자신을 복사하고 점프한 것과 같은 일이다. 그래서 `reloc off`만큼 링크 주소와 실행 주소가 다르고, U-Boot를 gdb로 디버깅할 때 `add-symbol-file u-boot <relocaddr>`가 필요하다(C4).
- `lmb_dump_all` — **LMB(Logical Memory Blocks)**는 U-Boot의 간단한 물리 메모리 예약 장부다. `0x5d32dff0-0x5fdfffff`(U-Boot 코드·heap·stack)가 `no-overwrite`로 예약되어 있다. 파일을 이 영역에 읽어 들이려 하면 거부된다 (§11.1에서 확인).
- `fdt_blob = 0x5e34ed80` — U-Boot **자신이 쓰는** DT(control DT). QEMU가 0x40000000에 준 DT를 복사한 것이다. 환경변수 `fdtcontroladdr`와 같은 값이다.
- `serial addr = 0x09000000` — 콘솔 UART(PL011)의 MMIO 주소. 다음 절에서 직접 찌른다.

---

## 3. 메모리와 레지스터 찌르기 — md · mw · mm · cp · cmp

### 3.1 문법 — 크기 접미사

| 명령 | 뜻 | 예 |
|---|---|---|
| `md[.b/.w/.l/.q] addr [count]` | memory display: addr부터 count개 단위를 hex + ASCII로 | `md.l 0x40000000 8` |
| `mw[.b/.w/.l/.q] addr value [count]` | memory write: value를 count개 채움 | `mw.l 0x50000000 0xdeadbeef 4` |
| `mm[.b/.w/.l/.q] addr` | memory modify: 주소마다 값을 묻고 입력하면 씀 (`.`/`q` 등 hex가 아닌 입력이면 종료) | `mm.l 0x50000000` |
| `cp[.b/.w/.l/.q] src dst count` | 복사 | `cp.l 0x50000000 0x50000100 4` |
| `cmp[.b/.w/.l/.q] a b count` | 비교, 다른 첫 위치 출력 | `cmp.l 0x50000000 0x50000100 4` |
| `crc32 addr len` | CRC32 계산 | 이미지 무결성 확인 |

`.b`=8비트, `.w`=16비트, `.l`=32비트, `.q`=64비트. **레지스터를 읽을 때는 반드시 레지스터 폭에 맞는 접미사**를 쓴다 — 32비트 레지스터를 `.b`로 읽으면 SoC에 따라 바이트 접근이 버스 에러가 되거나 다른 값이 읽힌다. 숫자는 **기본이 16진수**다(`md.l 40000000`도 된다). `0x`를 붙이는 습관이 안전하다.

### 3.2 예제 3 — DRAM에 쓰고, 읽고, 비교하기

**무엇을 확인하나**: 메모리 명령의 동작과 little-endian 배치. 0x50000000은 DRAM 중간(U-Boot 예약 영역 아래)의 빈 곳이다.

```text
=> md.l 0x40000000 8
40000000: edfe0dd0 00001000 40000000 7c1a0000  ...........@...|
40000010: 30000000 11000000 10000000 00000000  ...0............
=> fdt addr 0x40000000
Working FDT set to 40000000
=> fdt header
magic:			0xd00dfeed
totalsize:		0x100000 (1048576)
off_dt_struct:		0x40
off_dt_strings:		0x1a7c
off_mem_rsvmap:		0x30
version:		17
last_comp_version:	16
boot_cpuid_phys:	0x0
size_dt_strings:	0x19b
size_dt_struct:		0x1a3c
number mem_rsv:		0x0

=> mw.l 0x50000000 0xdeadbeef 4
=> md.l 0x50000000 4
50000000: deadbeef deadbeef deadbeef deadbeef  ................
=> mw.b 0x50000004 0x55 1
=> md.l 0x50000000 4
50000000: deadbeef deadbe55 deadbeef deadbeef  ....U...........
=> cp.l 0x50000000 0x50000100 4
=> cmp.l 0x50000000 0x50000100 4
Total of 4 word(s) were the same
=> mw.l 0x50000108 0 1
=> cmp.l 0x50000000 0x50000100 4
word at 0x50000008 (0xdeadbeef) != word at 0x50000108 (0x00)
Total of 2 word(s) were the same
```

**출력에서 볼 것**:

- `0x40000000`의 첫 워드가 `edfe0dd0` — DTB 매직 `0xd00dfeed`는 **big-endian**으로 저장되므로, little-endian CPU가 32비트로 읽으면 바이트가 뒤집혀 보인다. 두 번째 워드 `00001000`도 big-endian `0x00100000` = totalsize 1 MiB. `fdt header`가 해석해 준 값과 맞는다. 즉 QEMU가 DRAM 맨 앞에 DTB를 넣어 둔 것을 메모리 덤프로 증명했다.
- `mw.b 0x50000004 0x55` 후 두 번째 워드가 `deadbe55` — 주소가 가장 낮은 바이트가 워드의 최하위 바이트(little-endian). Don에게는 익숙한 그림이다.
- `cmp`는 첫 불일치에서 멈추고 몇 개가 같았는지 알려 준다. 이미지 두 벌(예: TFTP로 받은 것과 eMMC에서 읽은 것)을 비교할 때 쓴다.

### 3.3 예제 4 — `mm`으로 대화형 수정

**무엇을 확인하나**: 주소를 하나씩 넘기며 값을 써 넣는 `mm`. JTAG 디버거의 memory view에서 값을 고치는 것과 같다.

```text
=> mw.l 0x50000000 0 4
=> mm.l 0x50000000
50000000: 00000000 ? 11111111
50000004: 00000000 ? 22222222
50000008: 00000000 ? q
=> md.l 0x50000000 4
50000000: 11111111 22222222 00000000 00000000  ....""""........
```

**출력에서 볼 것**: 값을 입력하면 쓰고 다음 주소로 간다. 빈 줄(Enter만)이면 쓰지 않고 넘어가고, `q`처럼 hex가 아닌 입력이면 끝난다. 레지스터 하나만 고칠 때는 `mw`가, 연속 레지스터 블록을 하나씩 확인하며 고칠 때는 `mm`이 편하다.

### 3.4 예제 5 — PL011 UART 레지스터 직접 읽고 쓰기

**무엇을 확인하나**: 지금 콘솔로 쓰고 있는 UART의 레지스터를 U-Boot에서 직접 찌른다. ARM PrimeCell PL011의 레지스터 맵(ARM DDI 0183): `UARTDR`(0x000, 데이터), `UARTFR`(0x018, 플래그), `UARTCR`(0x030, 제어), `UARTPeriphID0-3`(0xFE0-0xFEC), `UARTPCellID0-3`(0xFF0-0xFFC).

```text
=> md.l 0x09000fe0 8
09000fe0: 00000011 00000010 00000014 00000000  ................
09000ff0: 0000000d 000000f0 00000005 000000b1  ................
=> md.l 0x09000018 1
09000018: 00000090                             ....
=> md.l 0x09000030 1
09000030: 00000b01                             ....
=> mw.b 0x09000000 0x41 1
A=> mw.b 0x09000000 0x0a 1

=>
```

**출력에서 볼 것**:

- **PeriphID** = `0x11, 0x10, 0x14, 0x00` → part number `0x011`(PL011), designer `0x41`('A' = ARM; ID1 상위 니블 1 + ID2 하위 니블 4 → 0x41), revision 1. **PCellID** = `0x0d, 0xf0, 0x05, 0xb1` — 모든 PrimeCell이 공유하는 고정 패턴(`0xB105F00D`). Linux의 AMBA 버스 드라이버(`amba_read_periphid()`)가 부팅 때 바로 이 값을 읽어 어떤 드라이버를 붙일지 정한다 — §11.4에서 이 읽기가 커널 패닉을 내는 장면을 본다.
- **UARTFR = 0x90** → bit7 `TXFE`(송신 FIFO 비었음) = 1, bit4 `RXFE`(수신 FIFO 비었음) = 1. 지금 보낼 것도 받은 것도 없다.
- **UARTCR = 0xb01** → bit0 `UARTEN`, bit8 `TXE`, bit9 `RXE`, bit11 `RTS` 가 1. UART 켜짐, 송수신 켜짐.
- `mw.b 0x09000000 0x41` → 데이터 레지스터에 `'A'`(0x41)를 쓰자 **화면에 `A`가 찍혔다**(프롬프트 `=>` 앞). 0x0a(LF)를 쓰자 줄바꿈. Don이 bare-metal에서 `*(volatile uint32_t *)UART_DR = 'A'`로 첫 "hello"를 찍던 것을 셸 명령 하나로 한 것이다.

> **Don 경험과 연결**: 새 SoC bring-up에서 U-Boot `md`는 "JTAG 없이 쓰는 memory view"다. 클럭 컨트롤러의 PLL lock 비트, 핀먹스 레지스터, PMIC가 붙은 I2C 컨트롤러 상태를 데이터시트와 대조한다. 단, U-Boot는 MMU를 켜고 돌 수 있으므로(arm64 U-Boot는 캐시를 위해 MMU를 켠다) **장치 영역이 device memory로 매핑되어 있어야** 레지스터 접근이 안전하다. 보드 코드의 `mem_map`이 이걸 정한다(C4).

### 3.5 예제 6 — 없는 장치를 찌르면: U-Boot의 Synchronous Abort

**무엇을 확인하나**: 존재하지 않는 MMIO 주소를 읽으면 어떻게 되나. 0x09030000은 QEMU `virt`에서 PL061 GPIO가 있을 수도 있는 자리인데, `-bios`로 펌웨어를 줄 때 QEMU는 이 장치를 만들지 않는다(§11.4에서 이것이 진짜 문제를 일으킨다).

```text
=> md.l 0x09000fe0 4
09000fe0: 00000011 00000010 00000014 00000000  ................
=> md.l 0x09030fe0 4
"Synchronous Abort" handler, esr 0x96000010, far 0x9030fe0
elr: 00000000000a5618 lr : 00000000000a5568 (reloc)
elr: 000000005f534618 lr : 000000005f534568
x0 : 0000000000000009 x1 : 0000000000000000
x2 : 00000000fffffffe x3 : 0000000000000020
...
x26: 0000000009030fe0 x27: 000000005e32dc79
x28: 0000000000000008 x29: 000000005e32dbb0

Code: 17ffffc9 710012bf 93407f04 54000181 (b9400343)
Resetting CPU ...

resetting ...
```

(레지스터 덤프 중간 `x4`~`x25`는 생략했다.)

**출력에서 볼 것**:

- `esr 0x96000010` — ESR_EL1을 해석하면 EC = `0x25`(Data Abort taken without a change in EL — U-Boot 자신이 낸 abort), IL=1, DFSC = `0x10` = **synchronous external abort**. 즉 "MMU 변환은 됐지만 버스(인터커넥트)가 에러 응답을 돌려줬다". Don이 SSD 컨트롤러에서 본 "AXI DECERR/SLVERR → data abort"와 똑같다.
- `far 0x9030fe0` — 문제의 주소(FAR = Fault Address Register).
- `elr ... (reloc)` — 링크 주소 기준 ELR. `u-boot.map`이나 `aarch64-linux-gnu-addr2line -e u-boot 0xa5618`로 어떤 함수였는지 찾는다. relocation 때문에 두 줄(링크 주소/실제 주소)을 찍어 준다.
- `Code: ... (b9400343)` — 괄호 안이 abort를 낸 명령어 `ldr w3, [x26]`(x26 = 0x9030fe0). U-Boot는 abort 후 **리셋한다** — 그래서 세션 로그에 배너가 다시 나온다.

> **함정**: "md 한 번에 보드가 리셋됐다"는 대개 클럭 게이팅된 블록이나 존재하지 않는 주소 접근이다. 블록의 클럭·리셋을 먼저 풀어야(CCM/CRU 레지스터) 그 블록 레지스터를 읽을 수 있다.

---

## 4. 환경변수 — printenv · setenv · saveenv, 그리고 env가 사는 곳

### 4.1 직관 — "부트로더의 설정 파일 = NVRAM의 key=value 테이블"

SSD 펌웨어에도 "부팅 파라미터 블록"이 있었을 것이다 — 어느 슬롯의 이미지를 올릴지, 디버그 레벨, 시리얼 번호. U-Boot의 **환경변수(environment)**가 그것이다: 문자열 `key=value` 쌍의 집합이고, 부팅 동작 전체가 이 변수들로 조정된다.

- **RAM에는** 해시 테이블(`env_htab`)로 산다. `setenv`/`printenv`는 이 RAM 사본만 다룬다.
- **비휘발 저장소에는** "CRC32 4바이트 + `key=value\0key=value\0...\0\0`" 블롭으로 산다. 어디냐는 config가 정한다: `CONFIG_ENV_IS_IN_MMC`(eMMC의 특정 오프셋), `ENV_IS_IN_SPI_FLASH`, `ENV_IS_IN_FAT`(`uboot.env` 파일), `ENV_IS_IN_UBI`, `ENV_IS_NOWHERE`(저장 안 함). 이 랩은 `CONFIG_ENV_IS_IN_FLASH=y`, `ENV_ADDR=0x4000000`, `ENV_SIZE=0x40000`(256 KiB) — **QEMU virt의 두 번째 NOR flash(pflash1)의 맨 앞**이다.
- **기본값(default env)은** U-Boot 바이너리 안에 컴파일되어 있다. 보드의 `.env` 파일(`board/emulation/qemu-arm/qemu-arm.env`)과 config 값(`CONFIG_BOOTCOMMAND`, `BOOTDELAY`)에서 만들어진다. 저장소의 CRC가 틀리면(처음 부팅, 지워진 flash) 이것을 쓴다 — 배너의 `bad CRC, using default environment`가 그 뜻이다.

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="30" width="190" height="96" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="105" y="52" text-anchor="middle" font-size="13" font-weight="bold">default env</text>
  <text x="105" y="74" text-anchor="middle" font-size="12">u-boot.bin 안에 내장</text>
  <text x="105" y="94" text-anchor="middle" font-size="12">qemu-arm.env</text>
  <text x="105" y="113" text-anchor="middle" font-size="12">+ CONFIG_BOOTCOMMAND</text>
  <rect x="245" y="30" width="190" height="96" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="2"/>
  <text x="340" y="52" text-anchor="middle" font-size="13" font-weight="bold">RAM 해시 테이블</text>
  <text x="340" y="74" text-anchor="middle" font-size="12">env_htab</text>
  <text x="340" y="94" text-anchor="middle" font-size="12">setenv · printenv · run</text>
  <text x="340" y="113" text-anchor="middle" font-size="12">전원 끄면 사라짐</text>
  <rect x="480" y="30" width="190" height="96" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="575" y="52" text-anchor="middle" font-size="13" font-weight="bold">비휘발 저장소</text>
  <text x="575" y="74" text-anchor="middle" font-size="12">NOR 0x04000000</text>
  <text x="575" y="94" text-anchor="middle" font-size="12">256 KiB 블록</text>
  <text x="575" y="113" text-anchor="middle" font-size="12">CRC32 + key=val\0...</text>
  <line x1="200" y1="62" x2="243" y2="62" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="222" y="150" text-anchor="middle" font-size="12">CRC 불량 시,</text>
  <text x="222" y="168" text-anchor="middle" font-size="12">env default -a</text>
  <line x1="222" y1="138" x2="222" y2="66" stroke="#888" stroke-dasharray="3 3"/>
  <line x1="478" y1="58" x2="437" y2="58" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="458" y="150" text-anchor="middle" font-size="12">위 화살표: 부팅 시 env_load</text>
  <text x="458" y="168" text-anchor="middle" font-size="12">아래 빨강: saveenv</text>
  <line x1="437" y1="100" x2="478" y2="100" stroke="#d0564a" stroke-width="2" marker-end="url(#ah2)"/>
  <rect x="245" y="240" width="425" height="96" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="457" y="264" text-anchor="middle" font-size="13" font-weight="bold">Linux 유저스페이스 (부팅 후)</text>
  <text x="457" y="286" text-anchor="middle" font-size="12">fw_printenv · fw_setenv (libubootenv)</text>
  <text x="457" y="306" text-anchor="middle" font-size="12">/etc/fw_env.config = 장치 · 오프셋 · 크기</text>
  <text x="457" y="326" text-anchor="middle" font-size="12">A/B 슬롯 선택 · bootcount · 업데이트 플래그</text>
  <line x1="575" y1="238" x2="575" y2="128" stroke="currentColor" marker-start="url(#ah2)" marker-end="url(#ah2)"/>
  <rect x="10" y="240" width="210" height="96" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="115" y="264" text-anchor="middle" font-size="13" font-weight="bold">주의</text>
  <text x="115" y="286" text-anchor="middle" font-size="12">setenv만 하고 saveenv를</text>
  <text x="115" y="306" text-anchor="middle" font-size="12">안 하면 재부팅 때 사라진다</text>
  <text x="115" y="326" text-anchor="middle" font-size="12">CRC가 깨지면 전부 기본값</text>
</svg>
```
그림 2 — 환경변수의 세 장소와 수명주기. 부팅 때 저장소에서 RAM으로 읽고(CRC가 틀리면 내장 기본값), 콘솔 명령은 RAM만 바꾸며, `saveenv`가 RAM 사본을 저장소에 쓴다. Linux에서는 `fw_printenv`/`fw_setenv`가 같은 저장소를 직접 읽고 쓴다.

### 4.2 예제 7 — 기본 환경 살펴보기

**무엇을 확인하나**: 부팅을 좌우하는 핵심 변수들과 전체 기본 환경.

```text
=> printenv bootcmd bootdelay kernel_addr_r ramdisk_addr_r fdt_addr scriptaddr fdtcontroladdr boot_targets
bootcmd=bootflow scan -lb
bootdelay=2
kernel_addr_r=0x40400000
ramdisk_addr_r=0x44000000
fdt_addr=0x40000000
scriptaddr=0x40200000
fdtcontroladdr=5e34ed80
boot_targets=qfw usb scsi virtio nvme dhcp
=> env print -a
arch=arm
baudrate=115200
board=qemu-arm
board_name=qemu-arm
boot_targets=qfw usb scsi virtio nvme dhcp
bootcmd=bootflow scan -lb
bootdelay=2
cpu=armv8
ethaddr=52:54:00:12:34:56
fdt_addr=0x40000000
fdtcontroladdr=5e34ed80
kernel_addr_r=0x40400000
loadaddr=0x40200000
preboot=usb start
pxefile_addr_r=0x40300000
ramdisk_addr_r=0x44000000
scriptaddr=0x40200000
stderr=serial,vidconsole
stdin=serial,usbkbd
stdout=serial,vidconsole
usb_ignorelist=0x1050:*,
vendor=emulation

Environment size: 471/262140 bytes
```

**출력에서 볼 것** — 이름이 관례로 정해진 변수들(distro boot / standard boot가 이 이름을 그대로 쓴다):

| 변수 | 값 | 뜻 |
|---|---|---|
| `bootcmd` | `bootflow scan -lb` | autoboot 때 실행할 명령 (§9) |
| `bootdelay` | `2` | 키 입력을 기다리는 초. `0`=기다리지 않되 키 확인, `-1`=autoboot 안 함, `-2`=키 확인도 안 함 |
| `bootargs` | (없음) | 커널 명령줄. `booti`/`bootm`이 DT `/chosen/bootargs`에 써 넣는다 |
| `kernel_addr_r` | `0x40400000` | 커널 `Image`를 읽어 올 주소 (`_r` = RAM 주소라는 관례) |
| `ramdisk_addr_r` | `0x44000000` | initramfs를 읽어 올 주소 |
| `fdt_addr` | `0x40000000` | QEMU가 준 DTB 위치 (실제 보드는 보통 `fdt_addr_r`에 파일로 읽어 온다) |
| `scriptaddr` | `0x40200000` | `boot.scr` 같은 스크립트를 읽어 올 주소 |
| `pxefile_addr_r` | `0x40300000` | `extlinux.conf`/PXE 설정 파일을 읽어 올 주소 |
| `loadaddr` | `0x40200000` | 주소를 생략한 `load`/`tftp`의 기본 목적지 |
| `fdtcontroladdr` | `5e34ed80` | U-Boot 자신이 쓰는 control DT (런타임에 설정) |
| `boot_targets` | `qfw usb scsi virtio nvme dhcp` | standard boot가 장치를 훑는 순서 |
| `ethaddr` | `52:54:00:12:34:56` | MAC 주소 (한 번 설정되면 기본적으로 덮어쓰기 금지) |

`Environment size: 471/262140` — 256 KiB 블록에서 CRC 4바이트를 뺀 262140바이트 중 471바이트를 쓰고 있다.

### 4.3 예제 8 — setenv · saveenv · env default, 재부팅 너머로 확인

**무엇을 확인하나**: 환경이 정말 flash에 저장되고 다음 부팅에 살아 있는지. QEMU에 env용 flash 이미지(pflash unit 1, 64 MiB)를 붙여 같은 이미지로 세 번 부팅한다.

```sh
cd /var/tmp/C3 && truncate -s 64M c3_env.img
P="-drive if=pflash,unit=1,format=raw,file=/var/tmp/C3/c3_env.img"
./run.sh c2a c2a.cmds $P; ./run.sh c2b c2b.cmds $P; ./run.sh c2c c2c.cmds $P
```

첫 번째 부팅 (빈 flash → bad CRC → 기본값, 그리고 저장):

```text
Loading Environment from Flash...*** Warning - bad CRC, using default environment
...
=> setenv c3_board sensorkit-01
=> setenv bootdelay 5
=> printenv c3_board bootdelay
c3_board=sensorkit-01
bootdelay=5
=> saveenv
Saving Environment to Flash... Un-Protected 2 sectors
Erasing Flash...
.. done
Erased 2 sectors
Writing to Flash... done
Protected 2 sectors
OK
```

두 번째 부팅 (저장된 env를 읽음 — 카운트다운이 5부터 시작한다):

```text
Loading Environment from Flash...Loading Environment from Flash... OK
...
Hit any key to stop autoboot: 5Hit any key to stop autoboot: 0
=> printenv c3_board bootdelay
c3_board=sensorkit-01
bootdelay=5
=> md.b 0x04000000 0x40
04000000: 4b 08 68 fa 61 72 63 68 3d 61 72 6d 00 62 61 75  K.h.arch=arm.bau
04000010: 64 72 61 74 65 3d 31 31 35 32 30 30 00 62 6f 61  drate=115200.boa
04000020: 72 64 3d 71 65 6d 75 2d 61 72 6d 00 62 6f 61 72  rd=qemu-arm.boar
04000030: 64 5f 6e 61 6d 65 3d 71 65 6d 75 2d 61 72 6d 00  d_name=qemu-arm.
=> env default -a
## Resetting to default environment
=> printenv c3_board bootdelay
## Error: "c3_board" not defined
bootdelay=2
=> saveenv
Saving Environment to Flash... Un-Protected 2 sectors
...
OK
```

세 번째 부팅 (기본값으로 되돌린 것이 저장되었음):

```text
Loading Environment from Flash...Loading Environment from Flash... OK
...
Hit any key to stop autoboot: 2Hit any key to stop autoboot: 0
=> printenv c3_board bootdelay
## Error: "c3_board" not defined
bootdelay=2
```

**출력에서 볼 것**:

- `md.b 0x04000000` — NOR flash는 **메모리 맵드**라서 env 블록을 `md`로 그냥 읽을 수 있다. 첫 4바이트 `4b 08 68 fa`가 CRC32(little-endian), 그 뒤가 `arch=arm\0baudrate=115200\0...` — 알파벳 순으로 정렬된 `key=value` 문자열이 NUL로 구분되어 있다.
- `Un-Protected 2 sectors / Erasing / Writing` — NOR flash는 쓰기 전에 지워야 한다(쓰기는 1→0만 가능, 지우기가 섹터 전체를 1로). U-Boot CFI 드라이버가 보는 섹터 2개를 unprotect → erase → write → protect 했다.
- `env default -a`는 **RAM만** 기본값으로 되돌린다. 저장소까지 되돌리려면 `saveenv`를 따로 해야 한다.
- `Loading Environment from Flash...Loading Environment from Flash... OK`처럼 두 번 보이는 것은 `env/env.c`가 첫 메시지를 `\r`로 끝내고 같은 줄에 `... OK`를 덮어쓰기 때문이다. 터미널에서는 한 줄로 보이는데, 로그에서 `\r`을 지워서 이어 붙었다.

### 4.4 예제 9 — Linux 쪽에서 같은 env 블록 읽고 쓰기 (CRC 검증 + fw_setenv)

**무엇을 확인하나**: env 블록 형식이 정말 "CRC32 + 문자열"인지 Python으로 직접 검증하고, Linux의 `fw_printenv`/`fw_setenv`(libubootenv)로 고친 값이 U-Boot에 보이는지.

```python
import sys, zlib, struct
ENV_SIZE = 0x40000                      # CONFIG_ENV_SIZE
with open(sys.argv[1], "rb") as f:
    blob = f.read(ENV_SIZE)
stored = struct.unpack("<I", blob[:4])[0]   # little-endian CRC32 header
calc = zlib.crc32(blob[4:]) & 0xffffffff    # CRC over the data area
print(f"stored crc = 0x{stored:08x}, computed = 0x{calc:08x}, match = {stored == calc}")
data = blob[4:].split(b"\x00\x00")[0]       # vars end at double NUL
for kv in data.split(b"\x00")[:6]:
    print("  ", kv.decode())
print(f"   ... {data.count(b'=')} variables, {len(data)} bytes used of {ENV_SIZE - 4}")
```

```sh
xxd -l 48 c3_env.img && python3 envcrc.py c3_env.img
echo "/var/tmp/C3/c3_env.img 0x0 0x40000 0x40000" > c3_fw_env.config
fw_printenv -c c3_fw_env.config bootcmd bootdelay
fw_setenv -c c3_fw_env.config c3_slot B
fw_setenv -c c3_fw_env.config bootdelay 1
fw_printenv -c c3_fw_env.config c3_slot bootdelay
python3 envcrc.py c3_env.img | head -1
```

```text
00000000: be75 cb28 6172 6368 3d61 726d 0062 6175  .u.(arch=arm.bau
00000010: 6472 6174 653d 3131 3532 3030 0062 6f61  drate=115200.boa
00000020: 7264 3d71 656d 752d 6172 6d00 626f 6172  rd=qemu-arm.boar
stored crc = 0x28cb75be, computed = 0x28cb75be, match = True
   arch=arm
   baudrate=115200
   board=qemu-arm
   board_name=qemu-arm
   boot_targets=qfw usb scsi virtio nvme dhcp
   bootcmd=bootflow scan -lb
   ... 20 variables, 419 bytes used of 262140
bootcmd=bootflow scan -lb
bootdelay=2
c3_slot=B
bootdelay=1
stored crc = 0x9fbd1317, computed = 0x9fbd1317, match = True
```

그리고 그 flash 이미지로 U-Boot를 다시 부팅:

```text
Hit any key to stop autoboot: 1Hit any key to stop autoboot: 0
=> printenv c3_slot bootdelay
c3_slot=B
bootdelay=1
```

**출력에서 볼 것**: 저장된 CRC와 계산한 CRC가 일치한다 — CRC는 **데이터 영역 전체(ENV_SIZE − 4바이트, 뒤쪽의 0 패딩 포함)**에 대해 계산된다. `fw_setenv`가 값을 바꾸고 CRC를 다시 계산해 쓰자, U-Boot는 그것을 정상 env로 받아들여 카운트다운이 1부터 시작했다. `fw_env.config`의 형식은 "장치(또는 파일) · 오프셋 · env 크기 · 섹터 크기"다. 실제 보드에서는 `/dev/mtd2 0x0 0x40000 0x40000`이나 `/dev/mmcblk0 0x3f8000 0x8000` 같은 줄이 들어간다.

> **실무**: A/B 업데이트(SWUpdate, RAUC, Mender)는 바로 이 메커니즘을 쓴다. Linux가 새 rootfs를 B 슬롯에 쓰고 `fw_setenv boot_slot B; fw_setenv upgrade_available 1`을 한 뒤 재부팅 → U-Boot 스크립트가 `boot_slot`을 보고 B를 부팅, `bootcount`가 한도를 넘으면 A로 되돌린다(L 모듈). 그래서 **env 위치가 U-Boot config와 `fw_env.config`에서 정확히 같아야** 한다. 다르면 Linux에서 쓴 값이 U-Boot에 안 보이거나, 더 나쁘게는 엉뚱한 곳을 지운다. 또 **redundant env**(`CONFIG_SYS_REDUNDAND_ENVIRONMENT`, 두 벌 + 플래그 바이트)를 켜면 저장 중 전원이 나가도 한 벌은 산다 — 양산 보드라면 거의 필수다.

---

## 5. 변수 · run · 스크립트 — U-Boot를 프로그래밍하기

### 5.1 hush 문법 한 장 요약

U-Boot 콘솔은 hush 셸이다. 알아야 할 것은 다섯 가지뿐이다.

- `${name}` — 변수 확장. 확장은 **명령이 실행될 때** 일어난다. `setenv a '${b}'`처럼 작은따옴표로 넣으면 `${b}`가 문자 그대로 저장되고, `run a` 때 확장된다.
- `;` — 명령 구분. 그래서 `setenv x 'cmd1; cmd2'`로 여러 명령을 변수에 담는다(작은따옴표 필수 — 없으면 `;`에서 setenv가 끝난다).
- `run var1 var2` — 변수의 내용을 명령으로 실행. 함수 호출과 같다.
- `if test ...; then ...; else ...; fi`, `for i in ...; do ...; done`, `&&`, `||` — 셸과 같다. 명령의 반환값이 0이면 성공.
- `setexpr` — 산술·문자열 연산 결과를 변수에. `echo $?`로 직전 명령의 반환값.

### 5.2 예제 10 — 변수에 명령을 담고 run으로 실행

**무엇을 확인하나**: 변수 = 함수, `run` = 호출, 조건·반복·산술.

```text
=> setenv c3_a 'echo step-A; setenv c3_cnt 1'
=> setenv c3_b 'echo step-B cnt=${c3_cnt}'
=> run c3_a c3_b
step-A
step-B cnt=1
=> setenv c3_x 0x40400000
=> setexpr c3_y ${c3_x} + 0x2000000
=> echo ${c3_y}
42400000
=> if test ${c3_cnt} -eq 1; then echo cnt-is-one; else echo other; fi
cnt-is-one
=> for i in 1 2 3; do echo loop $i; done
loop 1
loop 2
loop 3
=> setenv c3_fail 'false && echo never'
=> run c3_fail || echo run-failed-rc=$?
run-failed-rc=1
=> echo bootcmd is "${bootcmd}"
bootcmd is bootflow scan -lb
```

**출력에서 볼 것**: `c3_b`에 넣은 `${c3_cnt}`는 저장 시점이 아니라 `run` 시점에 확장되어, 바로 앞의 `c3_a`가 만든 값 1이 보였다. `setexpr` 결과는 `0x` 없는 hex(`42400000`)다. `run`이 실패하면 `||` 뒤가 실행된다 — 옛 distro boot 스크립트가 "이 장치에서 실패하면 다음 장치"를 이렇게 이어 붙였다. 이런 변수 묶음이 예전 보드의 `bootcmd`였다:

```text
bootcmd=run mmcboot || run netboot
mmcboot=load mmc 0:1 ${kernel_addr_r} Image && load mmc 0:1 ${fdt_addr_r} ${fdtfile} && booti ${kernel_addr_r} - ${fdt_addr_r}
netboot=dhcp && tftp ${kernel_addr_r} Image && tftp ${fdt_addr_r} ${fdtfile} && booti ${kernel_addr_r} - ${fdt_addr_r}
```

(위 블록은 실행 출력이 아니라 관례적인 env 예시다.)

### 5.3 스크립트 이미지 — boot.scr와 `source`

변수에 담기엔 긴 절차는 **스크립트 파일**로 만든다. 텍스트(`boot.cmd`)를 `mkimage -T script`로 감싸면 64바이트 legacy 헤더(매직 `0x27051956`, 이름, 타입, 데이터 CRC)가 붙은 `boot.scr`가 된다. U-Boot는 이것을 메모리로 읽은 뒤 `source <addr>`로 실행한다. 헤더 CRC 덕분에 깨진 스크립트를 실행하지 않는다.

예제에 쓸 `boot.cmd`:

```text
echo "== C3 boot.scr: devtype=${devtype} devnum=${devnum} part=${distro_bootpart} prefix=${prefix} =="
load ${devtype} ${devnum}:${distro_bootpart} ${kernel_addr_r} ${prefix}Image
load ${devtype} ${devnum}:${distro_bootpart} ${ramdisk_addr_r} ${prefix}initrd.gz
setenv bootargs "console=ttyAMA0 LAB_CMD=echo~C3-BOOTED-BY-SCRIPT;cat~/proc/cmdline"
booti ${kernel_addr_r} ${ramdisk_addr_r}:${filesize} ${fdt_addr}
```

`devtype`/`devnum`/`distro_bootpart`/`prefix`는 standard boot의 script bootmeth가 스크립트를 실행하기 전에 설정해 주는 변수다(U-Boot `boot/bootmeth_script.c`). 그래서 같은 스크립트가 SD에 있든 USB에 있든 동작한다. `LAB_CMD=...`는 이 랩의 initramfs가 명령 하나를 실행하고 전원을 끄게 하는 커널 인자다(공백은 `~`).

### 5.4 예제 11 — mkimage로 boot.scr 만들기

**무엇을 확인하나**: 스크립트 이미지의 헤더 내용.

```sh
mkimage -A arm64 -O linux -T script -C none -n "C3 boot script" -d boot.cmd boot.scr
mkimage -l boot.scr
```

```text
Image Name:   C3 boot script
Created:      Sat Oct 10 10:26:12 2026
Image Type:   AArch64 Linux Script (uncompressed)
Data Size:    420 Bytes = 0.41 KiB = 0.00 MiB
Load Address: 00000000
Entry Point:  00000000
Contents:
   Image 0: 412 Bytes = 0.40 KiB = 0.00 MiB
```

**출력에서 볼 것**: 데이터 420바이트 = 길이 테이블 8바이트(이미지 1개 길이 + 0 종료) + 스크립트 412바이트. 파일 전체는 헤더 64바이트를 더한 484바이트다(§6의 `ls` 출력과 맞는다). 스크립트를 고치면 **반드시 mkimage를 다시** 돌려야 한다 — `boot.cmd`만 고치고 `boot.scr`를 안 만들면 옛 스크립트가 돈다(흔한 실수).

### 5.5 예제 12 — 디스크에서 boot.scr를 읽어 source로 실행

**무엇을 확인하나**: 스크립트를 수동으로 읽고 실행하는 경로(standard boot가 자동으로 해 주는 일을 손으로). 디스크 준비는 §6.1에서 한다.

```text
=> virtio scan
=> load virtio 0:1 ${scriptaddr} boot.scr
484 bytes read in 1 ms (472.7 KiB/s)
=> iminfo ${scriptaddr}

## Checking Image at 40200000 ...
   Legacy image found
   Image Name:   C3 boot script
   Created:      2026-10-10  17:36:34 UTC
   Image Type:   AArch64 Linux Script (uncompressed)
   Data Size:    420 Bytes = 420 Bytes
   Load Address: 00000000
   Entry Point:  00000000
   Contents:
      Image 0: 412 Bytes = 412 Bytes
   Verifying Checksum ... OK
=> setenv devtype virtio
=> setenv devnum 0
=> setenv distro_bootpart 1
=> setenv prefix /
=> source ${scriptaddr}
## Executing script at 40200000
== C3 boot.scr: devtype=virtio devnum=0 part=1 prefix=/ ==
42740224 bytes read in 60 ms (679.3 MiB/s)
1210338 bytes read in 3 ms (384.8 MiB/s)
## Flattened Device Tree blob at 40000000
   Booting using the fdt blob at 0x40000000
Working FDT set to 40000000
   Loading Ramdisk to 5d206000, end 5d32d7e2 ... OK
   Loading Device Tree to 000000005d103000, end 000000005d205fff ... OK
Working FDT set to 5d103000

Starting kernel ...

lab initramfs: 6.18.55 up 1.13s
C3-BOOTED-BY-SCRIPT
console=ttyAMA0 LAB_CMD=echo~C3-BOOTED-BY-SCRIPT;cat~/proc/cmdline
```

(커널 부팅 로그 `[    0.000000] ...` 줄들은 생략했다. 이하 모든 부팅 예제에서 같다.)

**출력에서 볼 것**: `iminfo`가 legacy 헤더를 확인하고(`Verifying Checksum ... OK`), `source`가 스크립트의 각 줄을 콘솔에 친 것처럼 실행했다. 게스트의 `/proc/cmdline`이 스크립트의 `bootargs`와 같다 — U-Boot 변수 → DT `/chosen/bootargs` → 커널 명령줄 경로가 증명됐다.

---

## 6. 디스크에서 Linux 부팅 — virtio · part · ls · load · booti

### 6.1 예제 13 — 부팅 디스크 만들기 (FAT + ext4, root 권한 없이)

**무엇을 확인하나**: SD 카드처럼 생긴 디스크 이미지를 만든다. 파티션 1 = FAT32 부트 파티션(커널, initramfs, `boot.scr`, `extlinux/extlinux.conf`, FIT 두 개), 파티션 2 = ext4(rootfs 자리 — 여기서는 `/boot`에 커널 사본). `mtools`(`mcopy`)와 `mkfs.ext4 -d`를 쓰면 loop 장치 마운트 없이 만들 수 있다.

```sh
cd /var/tmp/C3
cp /var/lab/out/Image Image; cp /var/lab/out/initramfs.cpio.gz initrd.gz
truncate -s 320M c3_disk.img
printf 'label: dos\nstart=2048, size=409600, type=c\nstart=411648, type=83\n' | sfdisk -q c3_disk.img
truncate -s 200M p1.img; mkfs.vfat -F 32 -n C3BOOT p1.img >/dev/null
mmd -i p1.img ::/extlinux
mcopy -i p1.img Image initrd.gz boot.scr c3.itb c3_gz.itb ::/
mcopy -i p1.img extlinux.conf ::/extlinux/extlinux.conf
mkdir -p p2root/boot; cp Image initrd.gz p2root/boot/; echo "c3 rootfs partition" > p2root/c3_hello.txt
truncate -s 110M p2.img; mkfs.ext4 -q -L c3root -d p2root p2.img
dd if=p1.img of=c3_disk.img bs=1M seek=1 conv=notrunc status=none
dd if=p2.img of=c3_disk.img bs=512 seek=411648 conv=notrunc status=none
sfdisk -l c3_disk.img; mdir -i p1.img ::/
```

```text
Disk c3_disk.img: 320 MiB, 335544320 bytes, 655360 sectors
Units: sectors of 1 * 512 = 512 bytes
Sector size (logical/physical): 512 bytes / 512 bytes
I/O size (minimum/optimal): 512 bytes / 512 bytes
Disklabel type: dos
Disk identifier: 0x4c1e0260

Device       Boot  Start    End Sectors  Size Id Type
c3_disk.img1        2048 411647  409600  200M  c W95 FAT32 (LBA)
c3_disk.img2      411648 655359  243712  119M 83 Linux
 Volume in drive : is C3BOOT
 Volume Serial Number is 53A5-1D2D
Directory for ::/

extlinux     <DIR>     2026-10-10  10:27
IMAGE         42740224 2026-10-10  10:27  Image
initrd   gz    1210338 2026-10-10  10:27
boot     scr       484 2026-10-10  10:27
c3       itb  45000508 2026-10-10  10:27
c3_gz    itb  17507620 2026-10-10  10:27
        6 files         106 459 174 bytes
                        100 011 008 bytes free
```

**출력에서 볼 것**: MBR(dos) 파티션 테이블, 1 MiB 정렬(섹터 2048)에서 시작하는 FAT32(type `c`)와 Linux(type `83`). 커널 `Image`가 **40.8 MiB**나 되는 것은 arm64 defconfig에 드라이버가 잔뜩 built-in이기 때문이다(양산 커널은 보통 10~20 MiB, 압축하면 그 1/3). 처음에 ext4를 47 MiB로 만들었더니 `mkfs.ext4: Could not allocate block in ext2 filesystem while populating file system` — 커널 사본 + 저널이 안 들어갔다. 파티션 크기 계산은 실무에서도 자주 틀린다.

(FIT 파일 크기는 §10에서 DTB를 바꿔 다시 만든 뒤의 값과 몇 바이트 다를 수 있다 — 이 표는 첫 빌드 때 것이다.)

QEMU에는 이렇게 붙인다(`snapshot=on`은 게스트의 쓰기를 원본에 반영하지 않는다):

```sh
-drive if=none,file=/var/tmp/C3/c3_disk.img,format=raw,id=d0,snapshot=on -device virtio-blk-device,drive=d0
```

### 6.2 예제 14 — 장치 찾기, 파티션 보기, 파일 읽기

**무엇을 확인하나**: 블록 장치 명령의 기본 문법 `<인터페이스> <장치번호>:<파티션>` 과, 읽은 커널 이미지의 헤더.

```text
=> virtio scan
=> virtio info
Device 0: QEMU VirtIO Block Device
            Type: Hard Disk
            Capacity: 320.0 MB = 0.3 GB (655360 x 512)
=> part list virtio 0

Partition Map for virtio device 0  --   Partition Type: DOS

Part	Start Sector	Num Sectors	UUID		Type
  1	2048      	409600    	4c1e0260-01	0c
  2	411648    	243712    	4c1e0260-02	83
=> ls virtio 0:1
            extlinux/
 42740224   Image
  1210338   initrd.gz
      484   boot.scr

3 file(s), 1 dir(s)

=> ls virtio 0:2 /boot
            ./
            ../
 42740224   Image
  1210338   initrd.gz

2 file(s), 2 dir(s)

=> fatload virtio 0:1 ${kernel_addr_r} Image
42740224 bytes read in 142 ms (287 MiB/s)
=> printenv filesize
filesize=28c2a00
=> md.l ${kernel_addr_r} 16
40400000: fa405a4d 147fa83b 00000000 00000000  MZ@.;...........
40400010: 02980000 00000000 0000000a 00000000  ................
40400020: 00000000 00000000 00000000 00000000  ................
40400030: 00000000 00000000 644d5241 00000040  ........ARMd@...
40400040: 00004550 0002aa64 00000000 00000000  PE..d...........
40400050: 00000000 020600a0                    ........
=> load virtio 0:1 ${ramdisk_addr_r} initrd.gz
1210338 bytes read in 18 ms (64.1 MiB/s)
=> printenv filesize
filesize=1277e2
```

(이 세션은 FIT를 넣기 전 디스크로 돌린 것이라 `ls`에 `.itb`가 없다.)

**출력에서 볼 것**:

- `virtio 0:1` = virtio 블록 장치 0번의 파티션 1. 실제 보드라면 `mmc 0:1`(SD/eMMC), `usb 0:1`, `nvme 0:1`, `scsi 0:1`이다. 파티션을 생략하면 1번(또는 bootable 표시된 파티션)이다.
- `fatload`(FAT 전용), `ext4load`(ext4 전용), **`load`(파일시스템 자동 판별)** — 요즘은 `load`를 쓴다. 파티션 2(ext4)도 `ls`가 그대로 읽었다.
- **`filesize`** — 모든 load/tftp 명령은 읽은 바이트 수를 이 변수에 16진수로 남긴다(`0x28c2a00` = 42740224). 다음 명령이 덮어쓰므로, 필요하면 바로 다른 변수에 복사해 둔다(§6.3에서 이게 왜 중요한지).
- **arm64 `Image` 헤더** (커널 `Documentation/arch/arm64/booting.rst`) — `md` 출력을 해독하면:

```
offset  field          value (LE)            meaning
0x00    code0          0xfa405a4d            "MZ" + branch (EFI stub PE header trick)
0x04    code1          0x147fa83b            branch to stext
0x08    text_offset    0x0000000000000000    load offset from 2MB-aligned base
0x10    image_size     0x0000000002980000    41.5 MiB incl. BSS -> must be free
0x18    flags          0x000000000000000a    bit0=0 LE, bit1-2=01 4K pages, bit3=1 place anywhere
0x20    res2..res4     0
0x38    magic          0x644d5241            "ARM\x64"
0x3c    res5           0x00000040            offset to PE header
```

`image_size`(0x2980000 = 41.5 MiB)가 파일 크기(40.8 MiB)보다 큰 것은 **BSS 등 런타임 메모리까지 포함**하기 때문이다. U-Boot는 이 값으로 "커널이 차지할 영역"을 계산해 겹침을 검사한다(§11.2). `flags` bit3 = 1이면 커널을 DRAM 어디에든(2 MiB 정렬) 둘 수 있다 — 그래서 `booti`가 커널을 옮기지 않고 0x40400000에서 그대로 실행한다.

### 6.3 `booti` 인자 한 글자씩

```text
booti ${kernel_addr_r} ${ramdisk_addr_r}:${filesize} ${fdt_addr}
       ^ 1st: Image 주소   ^ 2nd: initrd 주소:크기       ^ 3rd: DTB 주소
```

| 인자 | 이 랩 값 | 뜻 | 생략하면 |
|---|---|---|---|
| 1번째 | `0x40400000` | arm64 `Image`(비압축)가 있는 주소. 헤더 매직 `ARM\x64` 검사 | `loadaddr` |
| 2번째 | `0x44000000:1277e2` | initramfs의 **주소:크기**. raw cpio.gz는 헤더가 없어서 크기를 알 수 없으므로 반드시 `:크기`를 붙인다 | `-`로 쓰면 initrd 없음 |
| 3번째 | `0x40000000` | DTB 주소. U-Boot가 이 DT를 복사해 `/chosen`을 고친 뒤 커널에 넘긴다 | 없으면 U-Boot control DT(`fdtcontroladdr`)를 쓸 수 있다 (`CONFIG_OF_BOARD` 보드 등) |

- `bootz`는 32비트 ARM `zImage`용, `booti`는 arm64/RISC-V `Image`용, `bootm`은 legacy uImage와 **FIT**용이다.
- 2번째 인자의 `${filesize}`는 **직전에 읽은 파일의 크기**다. 그래서 initrd를 **마지막에** 읽어야 한다. 커널을 나중에 읽으면 `filesize`가 커널 크기가 되어, 커널은 initramfs 뒤의 쓰레기까지 풀려고 한다.
- DTB 인자는 **QEMU에서만** `${fdt_addr}`(QEMU가 준 DT)이다. 실제 보드에서는 보통 `load mmc 0:1 ${fdt_addr_r} imx8mp-sensorkit.dtb` 후 `${fdt_addr_r}`를 쓴다. 3번째 인자를 아예 생략하면 U-Boot는 `fdtcontroladdr`의 control DT를 넘길 수 있는데, 이것은 "U-Boot용으로 다듬어진 DT"라서 커널이 기대하는 노드와 다를 수 있다 — 양산 보드에서는 **커널 트리에서 빌드한 DTB를 명시적으로** 넘기는 것이 원칙이다.

### 6.4 DRAM 지도 — 누가 어디에 놓이나

```svg
<svg viewBox="0 0 680 450" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="18" text-anchor="middle" font-size="13" font-weight="bold">QEMU virt DRAM 0x40000000 - 0x5fffffff (축척 아님)</text>
  <text x="196" y="47" text-anchor="end" font-size="12">0x40000000 fdt_addr</text>
  <rect x="205" y="30" width="250" height="28" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="330" y="49" text-anchor="middle" font-size="12">QEMU가 넣은 DTB (1 MiB)</text>
  <text x="196" y="77" text-anchor="end" font-size="12">0x40200000 scriptaddr</text>
  <rect x="205" y="60" width="250" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="330" y="79" text-anchor="middle" font-size="12">boot.scr · loadaddr</text>
  <text x="196" y="107" text-anchor="end" font-size="12">0x40300000 pxefile_addr_r</text>
  <rect x="205" y="90" width="250" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="330" y="109" text-anchor="middle" font-size="12">extlinux.conf</text>
  <text x="196" y="137" text-anchor="end" font-size="12">0x40400000 kernel_addr_r</text>
  <rect x="205" y="120" width="250" height="70" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="2"/>
  <text x="330" y="150" text-anchor="middle" font-size="12">Image 40.8 MiB</text>
  <text x="330" y="170" text-anchor="middle" font-size="12">image_size 41.5 MiB (BSS 포함)</text>
  <text x="196" y="196" text-anchor="end" font-size="12">0x42d80000 커널 끝</text>
  <rect x="205" y="192" width="250" height="20" fill="none" stroke="#888" stroke-dasharray="3 3"/>
  <text x="330" y="207" text-anchor="middle" font-size="12">빈 공간</text>
  <text x="196" y="231" text-anchor="end" font-size="12">0x44000000 ramdisk_addr_r</text>
  <rect x="205" y="214" width="250" height="28" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="330" y="233" text-anchor="middle" font-size="12">initrd.gz 1.2 MiB</text>
  <text x="196" y="261" text-anchor="end" font-size="12">0x48000000 (이 노트)</text>
  <rect x="205" y="244" width="250" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="330" y="263" text-anchor="middle" font-size="12">FIT c3.itb 43 MiB (§10)</text>
  <rect x="205" y="274" width="250" height="20" fill="none" stroke="#888" stroke-dasharray="3 3"/>
  <text x="330" y="289" text-anchor="middle" font-size="12">빈 공간 (커널이 나중에 씀)</text>
  <text x="196" y="313" text-anchor="end" font-size="12">0x5d103000</text>
  <rect x="205" y="296" width="250" height="28" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="330" y="315" text-anchor="middle" font-size="12">booti가 옮긴 DTB 사본</text>
  <text x="196" y="343" text-anchor="end" font-size="12">0x5d206000</text>
  <rect x="205" y="326" width="250" height="28" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="330" y="345" text-anchor="middle" font-size="12">booti가 옮긴 initrd 사본</text>
  <text x="196" y="373" text-anchor="end" font-size="12">0x5d32dff0</text>
  <rect x="205" y="356" width="250" height="56" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="330" y="378" text-anchor="middle" font-size="12">U-Boot: stack · heap · 코드</text>
  <text x="330" y="398" text-anchor="middle" font-size="12">relocaddr 0x5f48f000</text>
  <text x="196" y="428" text-anchor="end" font-size="12">0x60000000 DRAM 끝</text>
  <line x1="205" y1="424" x2="455" y2="424" stroke="currentColor"/>
  <text x="468" y="137" font-size="12">booti: 2 MiB 정렬이고</text>
  <text x="468" y="155" font-size="12">flags bit3=1 이라 제자리 실행</text>
  <text x="468" y="231" font-size="12">booti 2번째 인자 주소:크기</text>
  <text x="468" y="313" font-size="12">/chosen 수정 후 커널에 x0로</text>
  <text x="468" y="343" font-size="12">initrd_high 아래 꼭대기로 복사</text>
  <text x="468" y="373" font-size="12">LMB no-overwrite 예약</text>
  <text x="468" y="391" font-size="12">여기로 load 하면 거부</text>
</svg>
```
그림 3 — 이 랩의 DRAM 배치. 왼쪽은 주소와 환경변수 이름, 가운데는 내용, 오른쪽은 누가 왜 그곳을 쓰는지. 파일을 읽어 오는 곳(`*_addr_r`)과 커널에 실제로 넘어가는 곳(위쪽 꼭대기로 옮긴 DTB·initrd 사본)이 다르다는 것이 핵심이다.

`booti` 출력의 이 두 줄이 그림의 아래쪽 화살표다:

```text
   Loading Ramdisk to 5d206000, end 5d32d7e2 ... OK
   Loading Device Tree to 000000005d103000, end 000000005d205fff ... OK
```

U-Boot는 기본적으로 **initrd와 DTB를 DRAM 꼭대기 쪽(U-Boot 자신 바로 아래)으로 복사**하고, 복사본의 주소를 DT `/chosen`에 써서 넘긴다. 이유는 (1) 커널이 압축 해제·BSS로 덮어쓸 수 있는 낮은 주소를 피하고, (2) DT를 고칠 여유 공간(`/chosen` 추가)을 확보하기 위해서다. 위치는 `bootm_size`·`bootm_low`·`initrd_high`·`fdt_high` 변수로 조정한다. 예를 들어 `setenv initrd_high 0xffffffffffffffff`(64비트에서 "옮기지 마")로 두면 initrd를 읽은 자리에서 그대로 넘긴다 — 부팅 시간을 몇 ms 아끼는 트릭이지만, 그 자리가 커널이 쓰는 영역과 겹치지 않는다는 것을 직접 보장해야 한다.

> **로드 주소 고르는 규칙** (실제 보드의 `kernel_addr_r` 등을 정할 때): ① 커널은 DRAM 시작에서 2 MiB 정렬 + `text_offset`. ② 커널 영역은 파일 크기가 아니라 `image_size`(+ 여유)로 잡는다. ③ initrd·DTB·FIT·스크립트는 커널 영역 밖, U-Boot 예약 영역(`bdinfo`의 LMB `reserved`) 밖. ④ 압축 커널(`Image.gz`)을 `booti`로 풀 때는 `kernel_comp_addr_r`/`kernel_comp_size`에 압축본 자리가 따로 필요하다. 커널이 커지면(드라이버 추가) ②가 깨져 initrd와 겹친다 — §11.2.

---

## 7. `fdt` 명령 — 부팅 직전에 Device Tree 고치기

### 7.1 직관

DTB는 U-Boot가 커널에 넘기는 "하드웨어 설명서 + 메모"다(D 모듈에서 자세히). U-Boot의 `fdt` 명령은 메모리에 있는 DTB를 **libfdt로 직접 편집**한다. 실무에서 쓰는 장면: 보드 리비전에 따라 노드를 `status = "disabled"`로 끄기, MAC 주소·시리얼 번호 넣기, 메모리 크기 고치기, 디버깅 중 `/chosen`에 실험용 값 넣기. (영구적인 변경은 DTS를 고치거나 DT overlay를 쓴다 — D 모듈.)

| 명령 | 뜻 |
|---|---|
| `fdt addr <addr>` | 작업 대상 DTB 지정 (U-Boot 변수 `fdtaddr`도 설정) |
| `fdt header` | 헤더(매직, 크기, 버전) |
| `fdt print <path>` / `fdt list <path>` | 노드·속성 출력 (list는 한 단계만) |
| `fdt set <path> <prop> <value>` | 속성 설정. 문자열은 `"..."`, 셀은 `<0x1 0x2>` |
| `fdt mknode <path> <name>` | 노드 추가 |
| `fdt rm <path> [prop]` | 노드·속성 삭제 |
| `fdt resize [n]` | 여유 공간 늘리기 (속성을 많이 추가할 때) |
| `fdt apply <addr>` | DT overlay(`.dtbo`) 적용 |

### 7.2 예제 15 — `/chosen`에 속성 추가, 새 노드 만들기, 게스트에서 확인

**무엇을 확인하나**: U-Boot에서 고친 DT가 커널의 `/proc/device-tree`(= `/sys/firmware/devicetree/base`)에 그대로 나타나는지. 그리고 U-Boot가 `booti` 때 `/chosen`에 무엇을 써 넣는지. (§6.2 세션에 이어서 친 명령이다.)

```text
=> fdt addr ${fdt_addr}
Working FDT set to 40000000
=> fdt print /chosen
chosen {
	stdout-path = "/pl011@9000000";
	rng-seed = <0x92e5d46f 0x9a79d7b5 0xf3390d60 0xec64cf40 0x92d66a96 0xda289911 0x8a2ca285 0xf0315a93>;
	kaslr-seed = <0x1f977cc1 0xf070ed78>;
};
=> fdt set /chosen c3_note "hello-from-uboot"
=> fdt mknode / c3_marker
=> fdt set /c3_marker compatible "c3,marker"
=> fdt print /chosen
chosen {
	c3_note = "hello-from-uboot";
	stdout-path = "/pl011@9000000";
	rng-seed = <0x92e5d46f 0x9a79d7b5 0xf3390d60 0xec64cf40 0x92d66a96 0xda289911 0x8a2ca285 0xf0315a93>;
	kaslr-seed = <0x1f977cc1 0xf070ed78>;
};
=> setenv bootargs 'console=ttyAMA0 LAB_CMD=cat~/proc/device-tree/chosen/c3_note;echo;ls~/proc/device-tree/c3_marker;cat~/proc/device-tree/chosen/bootargs;echo;hexdump~-C~/proc/device-tree/chosen/linux,initrd-start'
=> booti ${kernel_addr_r} ${ramdisk_addr_r}:${filesize} ${fdt_addr}
## Flattened Device Tree blob at 40000000
   Booting using the fdt blob at 0x40000000
Working FDT set to 40000000
   Loading Ramdisk to 5d206000, end 5d32d7e2 ... OK
   Loading Device Tree to 000000005d103000, end 000000005d205fff ... OK
Working FDT set to 5d103000

Starting kernel ...

lab initramfs: 6.18.55 up 3.84s
hello-from-uboot^@
compatible  name
console=ttyAMA0 LAB_CMD=cat~/proc/device-tree/chosen/c3_note;echo;ls~/proc/device-tree/c3_marker;cat~/proc/device-tree/chosen/bootargs;echo;hexdump~-C~/proc/device-tree/chosen/linux,initrd-start^@
00000000  00 00 00 00 5d 20 60 00                           |....] `.|
00000008
```

(게스트 출력은 `cat -v`로 본 것이라 DT 문자열 끝의 NUL이 `^@`로 보인다.)

**출력에서 볼 것**:

- `c3_note`와 `/c3_marker` 노드(속성 `compatible`, `name`)가 게스트의 `/proc/device-tree`에 그대로 있다. **U-Boot에서 고친 DT = 커널이 본 DT**. 노드에 `compatible`이 있으므로 커널은 이것을 플랫폼 디바이스로 만들고 `compatible = "c3,marker"`에 맞는 드라이버를 찾는다(G 모듈) — DT 노드 하나로 드라이버 probe가 시작되는 경로의 출발점이다.
- `/chosen/bootargs`는 U-Boot에서 `fdt set` 하지 않았는데도 있다. **`booti`가 `bootargs` 환경변수를 `/chosen/bootargs`에 써 넣었다** (`boot/fdt_support.c`의 `fdt_chosen()`).
- `/chosen/linux,initrd-start` = `0x5d206000` (big-endian 64비트) — "Loading Ramdisk to 5d206000"의 그 주소다. **옮긴 사본의 주소**를 `fdt_initrd()`가 기록했다. 커널은 이 값으로 initramfs를 찾는다.
- `rng-seed`는 게스트에 없다(LAB_CMD에서 보지 않았지만, 커널은 `early_init_dt_scan_chosen()`에서 `rng-seed`를 엔트로피로 쓴 뒤 **`fdt_nop_property()`로 지운다** — 시드가 유저스페이스에 새지 않도록).

> **함정**: `fdt set`은 `booti`에 넘길 **바로 그 DTB**에 해야 한다. `fdt addr`가 다른 주소(예: control DT)를 가리킨 채 고치면 커널에는 반영되지 않는다. 또 `booti` 직전에 고쳐도 `bootargs`·`linux,initrd-*`는 `booti`가 다시 덮어쓴다 — 커맨드라인은 `fdt set /chosen bootargs`가 아니라 `setenv bootargs`로 바꾼다.

---

## 8. 네트워크 부팅 — dhcp · tftp

### 8.1 직관 — 개발 루프를 빠르게

커널이나 드라이버를 하루에 수십 번 고친다면, 매번 SD 카드에 복사하는 것은 고문이다. 대신 개발 PC에 TFTP 서버를 띄우고 빌드 산출물을 그 디렉터리에 두면, 보드는 리셋만 하면 최신 커널을 받아 부팅한다. rootfs까지 NFS로 받으면(`root=/dev/nfs nfsroot=<서버>:<경로>,v3 ip=dhcp`) 유저스페이스 바이너리도 복사 없이 바로 바뀐다. (NFS root는 이 랩의 QEMU user 네트워크로는 서버를 띄울 수 없어서 실행하지 않았다 — 개념만.)

QEMU의 user 네트워크(slirp)에는 TFTP 서버와 DHCP 서버가 내장되어 있다:

```sh
-netdev user,id=n,tftp=/var/tmp/C3/tftp -device virtio-net-device,netdev=n
```

게스트는 DHCP로 `10.0.2.15`를 받고, 서버(=게이트웨이)는 `10.0.2.2`다.

### 8.2 예제 16 — dhcp → tftp → booti

**무엇을 확인하나**: 네트워크 부팅의 전 과정과 각 변수.

```text
=> net list
eth0 : virtio-net#31 52:54:00:12:34:56 active
=> setenv autoload no
=> dhcp
BOOTP broadcast 1
DHCP client bound to address 10.0.2.15 (7 ms)
=> printenv ipaddr serverip netmask gatewayip
ipaddr=10.0.2.15
serverip=10.0.2.2
netmask=255.255.255.0
gatewayip=10.0.2.2
=> ping ${serverip}
Using virtio-net#31 device
host 10.0.2.2 is alive
=> tftp ${kernel_addr_r} Image
Using virtio-net#31 device
TFTP from server 10.0.2.2; our IP address is 10.0.2.15
Filename 'Image'.
Load address: 0x40400000
Loading: *#################################################################
	 #################################################################
	 ...
	 ####
	 4.6 MiB/s
done
Bytes transferred = 42740224 (28c2a00 hex)
=> tftp ${ramdisk_addr_r} initrd.gz
Using virtio-net#31 device
TFTP from server 10.0.2.2; our IP address is 10.0.2.15
Filename 'initrd.gz'.
Load address: 0x44000000
Loading: *#################################################################
	 ####################
	 19.9 MiB/s
done
Bytes transferred = 1210338 (1277e2 hex)
=> setenv bootargs 'console=ttyAMA0 LAB_CMD=ip~addr~show~eth0'
=> booti ${kernel_addr_r} ${ramdisk_addr_r}:${filesize} ${fdt_addr}
## Flattened Device Tree blob at 40000000
   Booting using the fdt blob at 0x40000000
Working FDT set to 40000000
   Loading Ramdisk to 5d206000, end 5d32d7e2 ... OK
   Loading Device Tree to 000000005d103000, end 000000005d205fff ... OK
Working FDT set to 5d103000

Starting kernel ...

lab initramfs: 6.18.55 up 1.65s
2: eth0: <BROADCAST,MULTICAST> mtu 1500 qdisc noop qlen 1000
    link/ether 52:54:00:12:34:56 brd ff:ff:ff:ff:ff:ff
```

(`#` 진행 표시 45줄은 `...`으로 줄였다. `#` 하나가 기본 블록 크기 기준 일정량의 수신을 뜻한다.)

**출력에서 볼 것**:

- `setenv autoload no` — 없으면 `dhcp`가 DHCP 응답의 boot file 이름으로 **자동 TFTP까지** 한다. 우리는 파일을 직접 고를 것이라 끈다. (`dhcp ${kernel_addr_r} Image`처럼 한 번에 할 수도 있다.)
- `dhcp`가 `ipaddr`/`serverip`/`netmask`/`gatewayip`를 채웠다. 고정 IP 보드라면 `setenv ipaddr 192.168.1.50; setenv serverip 192.168.1.10`으로 직접 넣는다.
- `virtio-net#31` — `-device virtio-net-device`(MMIO virtio)를 붙이면 장치 이름이 바뀐다. 아무 것도 안 붙이면 QEMU 기본 NIC(virtio-net-pci, `virtio-net#32`)가 생긴다(§2의 배너).
- **4.6 MiB/s** — 이 세션은 다른 작업이 VM CPU를 많이 쓰던 때라 느렸다. 한가할 때 측정한 값은 23~33 MiB/s다(§12). TFTP는 블록마다 ACK를 기다리는 stop-and-wait 프로토콜이라 지연에 민감하다. `tftpblocksize`(RFC 2348 blksize)와 `tftpwindowsize`(RFC 7440)를 키우면 빨라진다.
- 게스트 커널의 `eth0`가 **같은 MAC**(`52:54:00:12:34:56`)이다. 실제 보드에서는 U-Boot가 env의 `ethaddr`를 DT의 `local-mac-address`에 넣어 주는 방식으로 이어진다.

> **함정**: TFTP 서버 디렉터리 권한, 방화벽(UDP 69 + 임시 포트), 파일명 대소문자. U-Boot가 `TFTP error: 'File not found' (1)` 또는 `'Access violation' (2)`를 찍으면 서버 쪽 문제다(§9.2의 scan 출력에 실제 예가 나온다). `T T T T`(타임아웃 반복)면 네트워크 경로나 `serverip`가 틀린 것이다.

---

## 9. Standard boot — bootcmd · bootflow · extlinux.conf

### 9.1 직관 — "어떤 장치에서 무엇을 부팅할지 U-Boot가 찾게 하기"

예전 보드는 `bootcmd`에 손으로 쓴 스크립트(`run mmcboot || run netboot`)를 넣었다. 보드마다 제각각이라 배포판(Fedora, Debian, Yocto)이 하나의 이미지로 여러 보드를 부팅하기 어려웠다. 그래서 나온 것이:

- **distro boot** (2016~): `config_distro_bootcmd.h`의 거대한 env 스크립트(`distro_bootcmd`, `scan_dev_for_boot` ...)가 `boot_targets` 순서대로 장치를 훑으며 `extlinux/extlinux.conf`나 `boot.scr`를 찾는다.
- **standard boot (bootstd)** (2022~, 현재 기본): 같은 일을 **C 코드 + driver model**로 한다. 핵심 개념 세 가지:
  - **bootdev** — 부팅 가능한 장치(MMC, USB, virtio, 이더넷, QEMU fw_cfg ...).
  - **bootmeth** — 부팅 방법(extlinux, script, efi, efi_mgr, pxe, qfw ...). 장치에서 무엇을 찾을지.
  - **bootflow** — bootdev × bootmeth 조합으로 찾아낸 "부팅 가능한 것 하나". `bootflow scan`이 목록을 만들고 `bootflow boot`가 실행한다.

이 랩의 `bootcmd=bootflow scan -lb` = "목록을 찍으면서(`-l`) 훑다가 찾는 대로 부팅 시도(`-b`)".

```svg
<svg viewBox="0 0 680 500" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="60" y="10" width="300" height="34" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="210" y="32" text-anchor="middle" font-size="12">초기화: DRAM · relocation · DM</text>
  <rect x="60" y="64" width="300" height="34" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="210" y="86" text-anchor="middle" font-size="12">env_load: 저장소 env의 CRC 확인</text>
  <rect x="420" y="64" width="250" height="34" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="545" y="86" text-anchor="middle" font-size="12">CRC 불량이면 default env</text>
  <rect x="60" y="118" width="300" height="34" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="210" y="140" text-anchor="middle" font-size="12">preboot 실행 후 bootdelay 카운트다운</text>
  <rect x="420" y="118" width="250" height="34" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="545" y="140" text-anchor="middle" font-size="12">키 입력: 콘솔 프롬프트 =&gt;</text>
  <rect x="60" y="172" width="300" height="34" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="210" y="194" text-anchor="middle" font-size="12">run bootcmd = bootflow scan -lb</text>
  <rect x="60" y="226" width="300" height="34" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="210" y="248" text-anchor="middle" font-size="12">global bootmeth 먼저 (efi_mgr)</text>
  <rect x="60" y="280" width="300" height="34" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="210" y="302" text-anchor="middle" font-size="12">bootdev 순회: qfw · virtio · eth</text>
  <rect x="60" y="334" width="300" height="34" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="210" y="356" text-anchor="middle" font-size="12">파티션마다 bootmeth 순서대로</text>
  <rect x="60" y="388" width="300" height="34" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="210" y="410" text-anchor="middle" font-size="12">찾았다: bootflow 부팅 시도</text>
  <rect x="420" y="388" width="250" height="34" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="545" y="410" text-anchor="middle" font-size="12">성공: Starting kernel ...</text>
  <rect x="60" y="446" width="300" height="34" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="210" y="468" text-anchor="middle" font-size="12">더 없음: 콘솔 프롬프트로</text>
  <line x1="210" y1="44" x2="210" y2="62" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="210" y1="98" x2="210" y2="116" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="360" y1="81" x2="418" y2="81" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="360" y1="135" x2="418" y2="135" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="210" y1="152" x2="210" y2="170" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="222" y="166" font-size="12">시간 초과</text>
  <line x1="210" y1="206" x2="210" y2="224" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="210" y1="260" x2="210" y2="278" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="210" y1="314" x2="210" y2="332" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="210" y1="368" x2="210" y2="386" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="360" y1="405" x2="418" y2="405" stroke="currentColor" marker-end="url(#ah3)"/>
  <path d="M60,405 L30,405 L30,297 L58,297" fill="none" stroke="#d0564a" marker-end="url(#ah3)"/>
  <text x="22" y="351" text-anchor="middle" font-size="12" transform="rotate(-90 22 351)">실패하면 계속</text>
  <line x1="210" y1="422" x2="210" y2="444" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="222" y="438" font-size="12">목록 끝</text>
</svg>
```
그림 4 — U-Boot의 autoboot 결정 흐름. env를 읽고, 카운트다운 중 키가 없으면 `bootcmd`를 실행한다. standard boot는 global bootmeth(efi_mgr)를 먼저 시도한 뒤 `boot_targets` 순서로 bootdev를 훑고, 각 파티션에서 bootmeth 순서(extlinux → script → efi ...)로 부팅할 것을 찾는다. 하나가 실패하면 다음 후보로 넘어간다.

### 9.2 예제 17 — bootmeth · bootdev · bootflow 목록

**무엇을 확인하나**: 이 U-Boot가 아는 부팅 방법과 장치, 그리고 우리 디스크에서 찾아낸 bootflow.

```text
=> bootmeth list
Order  Seq  Name                Description
-----  ---  ------------------  ------------------
    0    0  extlinux            Extlinux boot from a block device
    1    1  script              Script boot from a block device
 glob    2  efi_mgr             EFI bootmgr flow
    3    3  efi                 EFI boot from an .efi file
    4    4  qfw                 QEMU boot using firmware interface
    5    5  pxe                 PXE boot from a network device
 glob    6  vbe_simple          VBE simple
-----  ---  ------------------  ------------------
(7 bootmeths)
=> bootdev list -p
Seq  Probed  Status  Uclass    Name
---  ------  ------  --------  ------------------
  0   [ + ]      OK  qfw       fw-cfg@9020000.bootdev
  1   [ + ]      OK  virtio    virtio-blk#31.bootdev
  2   [ + ]      OK  ethernet  virtio-net#32.bootdev
---  ------  ------  --------  ------------------
(3 bootdevs)
=> bootflow scan -l
Scanning for bootflows in all bootdevs
Seq  Method       State   Uclass    Part  Name                      Filename
---  -----------  ------  --------  ----  ------------------------  ----------------
Scanning global bootmeth 'efi_mgr':
Cannot persist EFI variables without system partition
Missing TPMv2 device for EFI_TCG_PROTOCOL
Missing RNG device for EFI_RNG_PROTOCOL
  0  efi_mgr      ready   (none)       0  <NULL>
Scanning bootdev 'fw-cfg@9020000.bootdev':
fatal: no kernel available
scanning bus for devices...
Scanning bootdev 'virtio-blk#31.bootdev':
  1  extlinux     ready   virtio       1  virtio-blk#31.bootdev.par /extlinux/extlinux.conf
  2  script       ready   virtio       1  virtio-blk#31.bootdev.par /boot.scr
Scanning bootdev 'virtio-net#32.bootdev':
BOOTP broadcast 1
DHCP client bound to address 10.0.2.15 (1 ms)
*** Warning: no boot file name; using '0A00020F.img'
Using virtio-net#32 device
TFTP from server 10.0.2.2; our IP address is 10.0.2.15
Filename '0A00020F.img'.
Load address: 0x40400000
Loading: *
TFTP error: 'Access violation' (2)
Not retrying...
No more bootdevs
---  -----------  ------  --------  ----  ------------------------  ----------------
(3 bootflows, 3 valid)
=> bootflow list
Showing all bootflows
Seq  Method       State   Uclass    Part  Name                      Filename
---  -----------  ------  --------  ----  ------------------------  ----------------
  0  efi_mgr      ready   (none)       0  <NULL>
  1  extlinux     ready   virtio       1  virtio-blk#31.bootdev.par /extlinux/extlinux.conf
  2  script       ready   virtio       1  virtio-blk#31.bootdev.par /boot.scr
---  -----------  ------  --------  ----  ------------------------  ----------------
(3 bootflows, 3 valid)
```

**출력에서 볼 것**:

- 파티션 1에서 **두 개의 bootflow**를 찾았다: extlinux(`/extlinux/extlinux.conf`)와 script(`/boot.scr`). bootmeth 순서가 extlinux → script라서 `-b`였다면 extlinux가 먼저 부팅된다. 순서는 `bootmeth order "script extlinux"`나 env `bootmeths`로 바꾼다.
- `qfw`(QEMU `-kernel`로 넘긴 커널을 fw_cfg로 받는 방법)는 `fatal: no kernel available` — 우리는 `-kernel`을 안 줬다.
- **ethernet bootdev는 PXE를 시도**했다: DHCP로 받은 IP를 hex로 바꾼 `0A00020F.img`(10.0.2.15)를 TFTP로 요청했다가, slirp TFTP 서버(이 세션은 `tftp=` 옵션 없는 기본 NIC)가 거부(`Access violation`). PXE는 실제로 `pxelinux.cfg/01-<MAC>`, `pxelinux.cfg/<IP hex>`, ..., `pxelinux.cfg/default` 순서로 설정 파일을 찾는 규칙이 있다.
- `efi_mgr` bootflow는 "ready"지만 실제로는 부팅할 EFI 앱이 없다 — 아래 예제에서 실패하고 다음으로 넘어가는 것을 본다.

### 9.3 예제 18 — extlinux.conf로 부팅

**무엇을 확인하나**: 배포판이 쓰는 extlinux 형식. 파일 하나로 커널·initrd·DTB·커맨드라인을 기술한다.

```text
# C3 extlinux.conf (U-Boot standard boot / distro boot)
default c3-linux
timeout 1
menu title C3 boot menu
label c3-linux
    kernel /Image
    initrd /initrd.gz
    append console=ttyAMA0 LAB_CMD=echo~C3-BOOTED-BY-EXTLINUX;cat~/proc/cmdline
```

실제 보드라면 `fdt /dtbs/imx8mp-sensorkit.dtb`나 `fdtdir /dtbs/`(U-Boot의 `fdtfile` 변수로 파일 선택), `fdtoverlays /dtbs/overlays/imu.dtbo` 줄이 더 들어간다. 여기서는 `fdt` 줄이 없어서 U-Boot가 `fdt_addr` 변수(QEMU DT)를 쓴다.

```text
=> bootflow select 1
=> bootflow info
Name:      virtio-blk#31.bootdev.part_1
Device:    virtio-blk#31.bootdev
Block dev: virtio-blk#31
Method:    extlinux
State:     ready
Partition: 1
Subdir:    (none)
Filename:  /extlinux/extlinux.conf
Buffer:    5e4aa100
Size:      f2 (242 bytes)
OS:        c3-linux
Cmdline:   (none)
Logo:      (none)
FDT:       <NULL>
Error:     0
=> bootflow boot
** Booting bootflow 'virtio-blk#31.bootdev.part_1' with extlinux
C3 boot menu
1:	c3-linux
Enter choice: 1:	c3-linux
Retrieving file: /Image
Retrieving file: /initrd.gz
append: console=ttyAMA0 LAB_CMD=echo~C3-BOOTED-BY-EXTLINUX;cat~/proc/cmdline
## Flattened Device Tree blob at 40000000
   Booting using the fdt blob at 0x40000000
Working FDT set to 40000000
   Loading Ramdisk to 5d206000, end 5d32d7e2 ... OK
   Loading Device Tree to 000000005d103000, end 000000005d205fff ... OK
Working FDT set to 5d103000

Starting kernel ...

lab initramfs: 6.18.55 up 0.72s
C3-BOOTED-BY-EXTLINUX
console=ttyAMA0 LAB_CMD=echo~C3-BOOTED-BY-EXTLINUX;cat~/proc/cmdline
```

**출력에서 볼 것**: extlinux bootmeth는 결국 `kernel` → `kernel_addr_r`, `initrd` → `ramdisk_addr_r`로 읽고 **`booti`와 같은 경로**(같은 "Loading Ramdisk to 5d206000")로 부팅했다. 즉 extlinux는 "§6에서 손으로 친 명령을 파일 하나로 선언한 것"이다. `timeout 1`은 메뉴 대기 시간인데, U-Boot는 이 값을 10으로 나눠 올림(`DIV_ROUND_UP(timeout, 10)`)해 **초 단위로** 쓴다(syslinux 문법상 1/10초 단위). 그래서 1이어도 1초를 기다린다 — §12의 측정에서 extlinux가 약 1초 더 걸린 이유다.

### 9.4 예제 19 — 아무 키도 누르지 않으면: 기본 autoboot 전 과정

**무엇을 확인하나**: 프롬프트에서 멈추지 않았을 때 `bootcmd`가 실제로 어떤 순서로 무엇을 시도하는지. (§11.1의 실패 예제가 U-Boot를 리셋시킨 뒤, 아무도 키를 누르지 않아 그대로 이어진 로그다.)

```text
Hit any key to stop autoboot: 2Hit any key to stop autoboot: 1Hit any key to stop autoboot: 0
Scanning for bootflows in all bootdevs
Seq  Method       State   Uclass    Part  Name                      Filename
---  -----------  ------  --------  ----  ------------------------  ----------------
Scanning global bootmeth 'efi_mgr':
Cannot persist EFI variables without system partition
Missing TPMv2 device for EFI_TCG_PROTOCOL
Missing RNG device for EFI_RNG_PROTOCOL
  0  efi_mgr      ready   (none)       0  <NULL>
** Booting bootflow '<NULL>' with efi_mgr
Loading Boot0000 'virtio 0' failed
EFI boot manager: Cannot load any image
Boot failed (err=-14)
Scanning bootdev 'fw-cfg@9020000.bootdev':
fatal: no kernel available
scanning bus for devices...
Scanning bootdev 'virtio-blk#31.bootdev':
  1  extlinux     ready   virtio       1  virtio-blk#31.bootdev.par /extlinux/extlinux.conf
** Booting bootflow 'virtio-blk#31.bootdev.part_1' with extlinux
C3 boot menu
1:	c3-linux
Enter choice: 1:	c3-linux
Retrieving file: /Image
Retrieving file: /initrd.gz
append: console=ttyAMA0 LAB_CMD=echo~C3-BOOTED-BY-EXTLINUX;cat~/proc/cmdline
...
Starting kernel ...
```

**출력에서 볼 것**: 그림 4의 흐름 그대로다. efi_mgr 시도 → 실패(`Boot failed (err=-14)`) → 계속 → qfw 장치 없음 → virtio 디스크에서 extlinux 발견 → 부팅. `-b` 모드에서는 **찾는 즉시 부팅**하므로 script bootflow(`/boot.scr`)는 아예 스캔 목록에 나오지 않았다. 실패한 시도마다 시간이 든다 — 양산 보드에서는 `boot_targets`를 실제 장치만 남기고(`setenv boot_targets mmc0`), 불필요한 bootmeth를 빼서 부팅 시간을 줄인다(C7).

---

## 10. FIT 이미지 — 커널 + DTB + initrd를 하나로, 해시까지

### 10.1 직관 — Don의 "펌웨어 패키지 헤더"

SSD 펌웨어 업데이트 파일을 떠올려 보자. 여러 구성요소(부트로더, 메인 FW, 설정 테이블)를 하나로 묶고, 각각의 길이·로드 주소·CRC(나중에는 서명)를 헤더에 적고, 부트로더가 헤더를 보고 검증·배치했다. **FIT(Flattened Image Tree)**가 정확히 그것이다. 특이한 점은 그 "헤더 형식"으로 **Device Tree 바이너리 포맷(FDT)을 재사용**한다는 것이다 — 그래서 `.its` 소스는 DTS 문법이고, `.itb`는 `dtc`/`fdtdump`로 열어 볼 수 있는 DTB다.

- `images` 노드 아래에 구성요소(kernel, fdt, ramdisk, 펌웨어, FPGA 비트스트림 ...)를 각각 `data` + 메타데이터(type, arch, compression, load, entry) + `hash` 서브노드로 둔다.
- `configurations` 노드 아래에 "어떤 커널 + 어떤 DTB + 어떤 ramdisk를 함께 부팅할지" 조합을 둔다. 한 이미지에 여러 보드 리비전용 DTB를 넣고 `bootm addr#conf-revB`로 고를 수 있다.
- 해시(crc32/sha1/sha256)로 무결성을, **서명(`signature` 노드 + RSA/ECDSA, U-Boot DT에 공개키)**으로 진위를 검증한다 — verified boot(C5, L 모듈).

```svg
<svg viewBox="0 0 680 410" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="40" width="180" height="36" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="100" y="63" text-anchor="middle" font-size="12">Image (40.8 MiB)</text>
  <rect x="10" y="90" width="180" height="36" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="100" y="113" text-anchor="middle" font-size="12">c3_live.dtb (1 MiB)</text>
  <rect x="10" y="140" width="180" height="36" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="100" y="163" text-anchor="middle" font-size="12">initrd.gz (1.2 MiB)</text>
  <rect x="10" y="200" width="180" height="50" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="100" y="221" text-anchor="middle" font-size="12">c3.its</text>
  <text x="100" y="239" text-anchor="middle" font-size="12">설명 파일 (DTS 문법)</text>
  <line x1="190" y1="150" x2="256" y2="150" stroke="currentColor" stroke-width="2" marker-end="url(#ah4)"/>
  <text x="223" y="140" text-anchor="middle" font-size="12">mkimage</text>
  <text x="223" y="170" text-anchor="middle" font-size="12">-f</text>
  <rect x="260" y="20" width="410" height="300" rx="8" fill="#888" fill-opacity="0.06" stroke="currentColor"/>
  <text x="465" y="42" text-anchor="middle" font-size="13" font-weight="bold">c3.itb — FDT 형식 컨테이너</text>
  <rect x="272" y="56" width="216" height="252" rx="6" fill="none" stroke="#888"/>
  <text x="380" y="76" text-anchor="middle" font-size="12">/images</text>
  <rect x="282" y="88" width="196" height="60" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="380" y="109" text-anchor="middle" font-size="12">kernel-1 (Image)</text>
  <text x="380" y="128" text-anchor="middle" font-size="12">load 0x40400000</text>
  <text x="380" y="143" text-anchor="middle" font-size="12">hash-1: sha256</text>
  <rect x="282" y="160" width="196" height="60" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="380" y="181" text-anchor="middle" font-size="12">fdt-1 (flat_dt)</text>
  <text x="380" y="200" text-anchor="middle" font-size="12">hash-1: sha256</text>
  <rect x="282" y="232" width="196" height="60" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="380" y="253" text-anchor="middle" font-size="12">ramdisk-1</text>
  <text x="380" y="272" text-anchor="middle" font-size="12">hash-1: sha256</text>
  <rect x="498" y="56" width="162" height="252" rx="6" fill="none" stroke="#888"/>
  <text x="579" y="76" text-anchor="middle" font-size="12">/configurations</text>
  <text x="579" y="100" text-anchor="middle" font-size="12">default = conf-1</text>
  <rect x="512" y="116" width="134" height="150" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="579" y="138" text-anchor="middle" font-size="12" font-weight="bold">conf-1</text>
  <text x="579" y="165" text-anchor="middle" font-size="12">kernel-1</text>
  <text x="579" y="195" text-anchor="middle" font-size="12">fdt-1</text>
  <text x="579" y="225" text-anchor="middle" font-size="12">ramdisk-1</text>
  <text x="579" y="252" text-anchor="middle" font-size="12">(+ signature)</text>
  <line x1="540" y1="161" x2="481" y2="118" stroke="#d0564a" marker-end="url(#ah4)"/>
  <line x1="556" y1="191" x2="481" y2="190" stroke="#d0564a" marker-end="url(#ah4)"/>
  <line x1="542" y1="221" x2="481" y2="262" stroke="#d0564a" marker-end="url(#ah4)"/>
  <text x="340" y="352" text-anchor="middle" font-size="12">bootm 0x48000000#conf-1: 설정 선택 → 각 이미지 해시 검증</text>
  <text x="340" y="374" text-anchor="middle" font-size="12">→ kernel을 load 주소로 복사(압축이면 해제) → ramdisk · fdt 배치</text>
  <text x="340" y="396" text-anchor="middle" font-size="12">→ booti와 같은 Linux 진입 (/chosen 수정, x0 = DTB)</text>
</svg>
```
그림 5 — FIT의 구조. `mkimage -f`가 `.its`의 `/incbin/`으로 지정한 파일들을 하나의 FDT 형식 파일에 넣고 해시를 계산해 채운다. configuration이 이미지들을 이름으로 가리키며, `bootm`은 configuration을 골라 각 이미지를 검증한 뒤 배치한다.

### 10.2 예제 20 — `.its` 작성과 `mkimage -f`

**무엇을 확인하나**: FIT 소스 문법과 mkimage가 만든 결과.

```dts
/dts-v1/;

/ {
    description = "C3 FIT: kernel + fdt + ramdisk";
    #address-cells = <1>;

    images {
        kernel-1 {
            description = "Linux 6.18.55 arm64 Image";
            data = /incbin/("Image");
            type = "kernel";
            arch = "arm64";
            os = "linux";
            compression = "none";
            load = <0x40400000>;
            entry = <0x40400000>;
            hash-1 { algo = "sha256"; };
        };
        fdt-1 {
            description = "QEMU virt DTB dumped with -bios";
            data = /incbin/("c3_live.dtb");
            type = "flat_dt";
            arch = "arm64";
            compression = "none";
            hash-1 { algo = "sha256"; };
        };
        ramdisk-1 {
            description = "BusyBox initramfs";
            data = /incbin/("initrd.gz");
            type = "ramdisk";
            arch = "arm64";
            os = "linux";
            compression = "none";
            hash-1 { algo = "sha256"; };
        };
    };

    configurations {
        default = "conf-1";
        conf-1 {
            description = "sensor kit, initramfs";
            kernel = "kernel-1";
            fdt = "fdt-1";
            ramdisk = "ramdisk-1";
        };
    };
};
```

DTB는 **이 QEMU 구성이 실제로 만든 DT**를 덤프해서 넣는다(왜 랩의 `virt.dtb`를 그대로 쓰면 안 되는지는 §11.4):

```sh
qemu-system-aarch64 -machine virt,dumpdtb=c3_live.dtb -cpu cortex-a57 -m 512M -nographic \
    -bios /var/lab/out/u-boot.bin \
    -drive if=none,file=/var/tmp/C3/c3_disk.img,format=raw,id=d0,snapshot=on -device virtio-blk-device,drive=d0
mkimage -f c3.its c3.itb
```

```text
FIT description: C3 FIT: kernel + fdt + ramdisk
Created:         Sat Oct 10 10:27:25 2026
 Image 0 (kernel-1)
  Description:  Linux 6.18.55 arm64 Image
  Created:      Sat Oct 10 10:27:25 2026
  Type:         Kernel Image
  Compression:  uncompressed
  Data Size:    42740224 Bytes = 41738.50 KiB = 40.76 MiB
  Architecture: AArch64
  OS:           Linux
  Load Address: 0x40400000
  Entry Point:  0x40400000
  Hash algo:    sha256
  Hash value:   6718e4e6cbea7fe66fbd3eaf1c1986f306d2ed5c134a2df43a2a7c05337f8448
 Image 1 (fdt-1)
  ...
 Image 2 (ramdisk-1)
  Description:  BusyBox initramfs
  ...
  Load Address: unavailable
  Entry Point:  unavailable
  Hash algo:    sha256
  Hash value:   a1fe6b27719bd3b531996e4dbe982bf254b59316b8a84ce17a84d4a237d74c5a
 Default Configuration: 'conf-1'
 Configuration 0 (conf-1)
  Description:  sensor kit, initramfs
  Kernel:       kernel-1
  Init Ramdisk: ramdisk-1
  FDT:          fdt-1
```

(이 출력은 첫 빌드 때 것이라 fdt-1의 설명이 달랐다 — 중간 부분은 생략했다. 최종 이미지의 내용은 다음 예제의 `iminfo`가 보여 준다.)

**출력에서 볼 것**: mkimage가 각 이미지의 sha256을 계산해 `hash-1/value`에 채웠다. ramdisk는 `load`를 안 줬으므로 `unavailable` — `bootm`이 FIT 안의 데이터를 그대로 쓰거나 적당한 곳으로 옮긴다. 결과 크기 45,000,504바이트 ≈ 세 파일의 합 + 수 KB의 트리 구조.

압축 커널 변형(`c3_gz.its`)은 `data = /incbin/("Image.gz")`, `compression = "gzip"` 두 줄만 다르다:

```sh
gzip -9 -k Image; mkimage -f c3_gz.its c3_gz.itb; ls -l c3.itb c3_gz.itb Image.gz
```

```text
-rw-rw-r-- 1 donh donh 45000504 Oct 10 10:36 c3.itb
-rw-rw-r-- 1 donh donh 17507616 Oct 10 10:36 c3_gz.itb
-rw-rw-r-- 1 donh donh 15247334 Oct 10 10:26 Image.gz
```

압축하면 FIT가 43 MiB → 16.7 MiB로 줄어든다. 읽을 양이 줄어드는 대신 U-Boot가 압축을 풀어야 한다 — 어느 쪽이 빠른지는 저장장치 속도와 CPU 속도의 싸움이다(§12).

### 10.3 예제 21 — iminfo로 검증하고 bootm으로 부팅

**무엇을 확인하나**: U-Boot가 FIT를 해석·검증·배치하는 과정.

```text
=> virtio scan
=> setenv c3_fit 0x48000000
=> load virtio 0:1 ${c3_fit} c3.itb
45000504 bytes read in 197 ms (217.8 MiB/s)
=> iminfo ${c3_fit}

## Checking Image at 48000000 ...
   FIT image found
   FIT description: C3 FIT: kernel + fdt + ramdisk
   Created:         2026-10-10  17:36:28 UTC
    Image 0 (kernel-1)
     Description:  Linux 6.18.55 arm64 Image
     Created:      2026-10-10  17:36:28 UTC
     Type:         Kernel Image
     Compression:  uncompressed
     Data Start:   0x480000dc
     Data Size:    42740224 Bytes = 40.8 MiB
     Architecture: AArch64
     OS:           Linux
     Load Address: 0x40400000
     Entry Point:  0x40400000
     Hash algo:    sha256
     Hash value:   6718e4e6cbea7fe66fbd3eaf1c1986f306d2ed5c134a2df43a2a7c05337f8448
    Image 1 (fdt-1)
     Description:  QEMU virt DTB dumped with -bios
     ...
     Data Start:   0x4a8c2be4
     Data Size:    1048576 Bytes = 1 MiB
     ...
    Image 2 (ramdisk-1)
     Description:  BusyBox initramfs
     ...
     Data Start:   0x4a9c2cb0
     Data Size:    1210338 Bytes = 1.2 MiB
     ...
    Default Configuration: 'conf-1'
    Configuration 0 (conf-1)
     Description:  sensor kit, initramfs
     Kernel:       kernel-1
     Init Ramdisk: ramdisk-1
     FDT:          fdt-1
## Checking hash(es) for FIT Image at 48000000 ...
   Hash(es) for Image 0 (kernel-1): sha256+
   Hash(es) for Image 1 (fdt-1): sha256+
   Hash(es) for Image 2 (ramdisk-1): sha256+
=> setenv bootargs 'console=ttyAMA0 LAB_CMD=cat~/proc/cmdline'
=> bootm ${c3_fit}#conf-1
## Loading kernel (any) from FIT Image at 48000000 ...
   Using 'conf-1' configuration
   Verifying Hash Integrity ... OK
   Trying 'kernel-1' kernel subimage
     ...
   Verifying Hash Integrity ... sha256+ OK
## Loading ramdisk (any) from FIT Image at 48000000 ...
   Using 'conf-1' configuration
   Verifying Hash Integrity ... OK
   Trying 'ramdisk-1' ramdisk subimage
     ...
   Verifying Hash Integrity ... sha256+ OK
## Loading fdt (any) from FIT Image at 48000000 ...
   Using 'conf-1' configuration
   Verifying Hash Integrity ... OK
   Trying 'fdt-1' fdt subimage
     ...
   Verifying Hash Integrity ... sha256+ OK
   Booting using the fdt blob at 0x5e4c4730
Working FDT set to 5e4c4730
   Loading Kernel Image to 40400000
   Loading Ramdisk to 5d206000, end 5d32d7e2 ... OK
   Loading Device Tree to 000000005d103000, end 000000005d205fff ... OK
Working FDT set to 5d103000

Starting kernel ...

lab initramfs: 6.18.55 up 2.95s
console=ttyAMA0 LAB_CMD=cat~/proc/cmdline
```

(각 서브이미지의 속성 반복 출력은 `...`으로 줄였다.)

**출력에서 볼 것**:

- `Data Start: 0x480000dc` — 커널 데이터가 FIT 시작에서 0xdc(220)바이트 뒤에 있다. FIT는 기본적으로 데이터를 트리 안에 **내장**한다. `mkimage -E`(external data)로 만들면 데이터를 트리 뒤에 정렬해 붙여서, 큰 이미지에서 검증·복사가 빨라지고 4바이트 정렬 문제가 줄어든다.
- `Hash(es) ... sha256+` — `+`가 통과 표시. `bootm`은 configuration 해시와 각 서브이미지 해시를 다시 검증한다(iminfo와 합쳐 이 세션에서 sha256을 두 번씩 계산했다 — §12에서 이 비용을 본다).
- `Loading Kernel Image to 40400000` — FIT 안의 커널을 `load` 주소로 **복사**했다(booti는 제자리 실행이었다).
- `Booting using the fdt blob at 0x5e4c4730` — DTB 데이터 시작 `0x4a8c2be4`는 8바이트 정렬이 아니라서(libfdt는 정렬된 DTB를 요구한다) U-Boot가 정렬된 곳으로 먼저 옮겼다. 그 뒤는 `booti`와 똑같이 꼭대기로 복사.
- `bootm ${c3_fit}#conf-1` — `#conf-1`을 생략하면 `default` configuration. 특정 이미지만 지정하는 문법(`bootm addr:kernel-1 addr:ramdisk-1 addr:fdt-1`)도 있다.

> **왜 FIT를 쓰나 (면접 답변 포인트)**: ① 커널·DTB·initrd 버전이 항상 짝이 맞는다(원자적 업데이트 단위). ② 해시로 깨진 이미지를 부팅 전에 거부한다. ③ 서명으로 secure boot 체인을 커널까지 잇는다(U-Boot SPL → U-Boot → FIT 모두 서명 검증). ④ 한 이미지에 여러 보드용 DTB를 넣고 configuration으로 고른다. 단점은 이미지를 만드는 단계가 하나 더 생기고, 검증 시간이 든다는 것.

---

## 11. 부팅 실패는 이렇게 생겼다

실패 메시지를 미리 한 번씩 봐 두면, 실제 보드에서 원인을 바로 가를 수 있다. 아래는 전부 실제로 일으킨 실패다.

### 11.1 예제 22 — 인자·주소·이미지 실수 모음

**무엇을 확인하나**: initrd 크기 누락, 커널 아닌 것을 booti, 예약 영역에 load, 손상된 FIT, DTB 자리에 엉뚱한 것.

```text
=> virtio scan
=> load virtio 0:1 ${kernel_addr_r} Image
42740224 bytes read in 22 ms (1.8 GiB/s)
=> load virtio 0:1 ${ramdisk_addr_r} initrd.gz
1210338 bytes read in 1 ms (1.1 GiB/s)
=> booti ${kernel_addr_r} ${ramdisk_addr_r} ${fdt_addr}
Wrong Ramdisk Image Format
Ramdisk image is corrupt or invalid
=> booti ${ramdisk_addr_r} - ${fdt_addr}
kernel_comp_addr_r or kernel_comp_size is not provided!
=> load virtio 0:1 0x5f000000 Image
** Reading file would overwrite reserved memory **
Failed to load 'Image'
=> load virtio 0:1 0x48000000 c3.itb
45000504 bytes read in 30 ms (1.4 GiB/s)
=> mw.b 0x48100000 0x00 1
=> bootm 0x48000000
## Loading kernel (any) from FIT Image at 48000000 ...
   Using 'conf-1' configuration
   Verifying Hash Integrity ... OK
   Trying 'kernel-1' kernel subimage
     ...
   Verifying Hash Integrity ... sha256 error!
Bad hash value for 'hash-1' hash node in 'kernel-1' image node
Bad Data Hash
ERROR -2: can't get kernel image!
=> load virtio 0:1 0x40000000 boot.scr
484 bytes read in 0 ms
=> booti ${kernel_addr_r} - ${fdt_addr}
## Flattened Device Tree from Legacy Image at 40000000
   Image Name:   C3 boot script
   Created:      2026-10-10  17:36:34 UTC
   Image Type:   AArch64 Linux Script (uncompressed)
   Data Size:    420 Bytes = 420 Bytes
   Load Address: 00000000
   Entry Point:  00000000
   Contents:
      Image 0: 412 Bytes = 412 Bytes
   Verifying Checksum ... OK
ERROR: uImage is not a fdt - must RESET the board to recover.
Working FDT set to 0
FDT and ATAGS support not compiled in

resetting ...
```

**출력에서 볼 것** — 메시지별 원인:

| 메시지 | 원인 | 고치기 |
|---|---|---|
| `Wrong Ramdisk Image Format` / `Ramdisk image is corrupt or invalid` | initrd 주소만 주고 `:크기`를 안 줌. 헤더 없는 cpio.gz를 legacy uImage로 해석하려다 실패 | `${ramdisk_addr_r}:${filesize}` (initrd를 마지막에 load) 또는 `mkimage -T ramdisk`로 감싸기 |
| `kernel_comp_addr_r or kernel_comp_size is not provided!` | 첫 인자 주소의 내용이 gzip 매직(`1f 8b`)이라 booti가 "압축된 Image"로 판단. 실제로는 커널이 아니라 initrd였다 | 주소 확인. 압축 커널이 맞다면 `kernel_comp_addr_r`/`kernel_comp_size` 설정 |
| `** Reading file would overwrite reserved memory **` | LMB가 U-Boot 영역(`0x5d32dff0~`)을 보호 | `bdinfo`의 LMB `reserved`를 보고 그 아래로 |
| `sha256 error!` / `Bad Data Hash` | FIT 데이터 1바이트 손상(`mw.b`로 일부러 깨뜨림) — 실제로는 전송 오류, 저장장치 bit flip, 덜 쓰인 이미지 | 이미지 재전송. 해시 덕분에 깨진 커널로 부팅하지 않았다 |
| `uImage is not a fdt - must RESET` | DTB 자리(`fdt_addr`)에 DTB가 아닌 legacy 이미지(여기선 스크립트)가 있음 | DTB를 다시 load. 이 경로는 U-Boot가 스스로 리셋한다 |

### 11.2 예제 23 — initrd가 커널 영역과 겹치면

**무엇을 확인하나**: 로드 주소 겹침을 U-Boot가 잡아 주는지. initrd를 커널 영역 한가운데(0x40800000)에 읽었다.

```text
=> virtio scan
=> load virtio 0:1 ${kernel_addr_r} Image
42740224 bytes read in 26 ms (1.5 GiB/s)
=> load virtio 0:1 0x40800000 initrd.gz
1210338 bytes read in 1 ms (1.1 GiB/s)
=> setenv bootargs 'console=ttyAMA0 earlycon'
=> booti ${kernel_addr_r} 0x40800000:${filesize} ${fdt_addr}
ERROR: RD image overlaps OS image (OS=40400000..42d80000)
=>
```

**출력에서 볼 것**: `OS=40400000..42d80000` — 끝 주소 `0x42d80000` = `0x40400000 + image_size(0x2980000)`. U-Boot는 파일 크기가 아니라 **헤더의 `image_size`**로 커널 영역을 계산해 initrd·FDT와의 겹침을 검사한다(`boot/bootm.c`). 그런데 initrd를 읽는 순간 이미 커널 데이터 일부를 덮어썼다는 것에 주의 — 이 검사는 부팅을 막아 줄 뿐 이미지를 되살리지는 않는다. 그리고 **이 검사가 못 잡는 겹침**도 있다: 커널이 부팅 후 쓰는 메모리(예: 커널이 initramfs를 풀거나 CMA·page table을 잡는 영역)에 DTB·initrd를 두거나, `initrd_high`를 꺼서 옮기지 않은 initrd를 커널이 덮어쓰는 경우다. 그때 증상이 "Starting kernel ... 이후 무소식" 또는 `Initramfs unpacking failed`다.

### 11.3 예제 24 — 커널을 DTB 위에 읽으면

**무엇을 확인하나**: QEMU처럼 DTB가 DRAM 맨 앞에 있을 때, 커널을 DRAM 시작 주소에 읽어 DTB를 지워 버리는 실수.

```text
=> virtio scan
=> fdt addr ${fdt_addr}
Working FDT set to 40000000
=> fdt header
magic:			0xd00dfeed
totalsize:		0x100000 (1048576)
...
=> load virtio 0:1 0x40000000 Image
42740224 bytes read in 55 ms (741.1 MiB/s)
=> fdt addr ${fdt_addr}
libfdt fdt_check_header(): FDT_ERR_BADMAGIC
=> booti 0x40000000 - ${fdt_addr}
ERROR: Did not find a cmdline Flattened Device Tree
Could not find a valid device tree
```

**출력에서 볼 것**: 커널 로드 전에는 `0xd00dfeed`였던 매직이 로드 후 `FDT_ERR_BADMAGIC`. 실제 보드에서도 "커널은 2 MiB 정렬된 DRAM 시작에 둔다"는 규칙만 외우고 `kernel_addr_r=${DRAM 시작}`으로 두면, 같은 곳에 둔 DTB·스크립트를 지운다. 그래서 qemu-arm의 env는 커널을 `0x40400000`(4 MiB 위)에 둔다.

### 11.4 예제 25 — "다른 하드웨어"의 DTB를 넘기면: 커널의 synchronous external abort

**무엇을 확인하나**: 이 노트를 쓰다가 실제로 만난 실패다. 처음 FIT에 랩의 `out/virt.dtb`(`-kernel`로 커널을 직접 띄우는 구성에서 덤프한 DT)를 넣었더니, `bootm` 후 "Starting kernel ..." 다음에 아무것도 나오지 않았다. `earlycon`을 붙이고 같은 DTB로 `booti`를 해서 원인을 찾았다.

```text
=> load virtio 0:1 0x4f000000 virt.dtb
1048576 bytes read in 33 ms (30.3 MiB/s)
=> setenv bootargs 'console=ttyAMA0 earlycon LAB_CMD=cat~/proc/cmdline'
=> booti ${kernel_addr_r} ${ramdisk_addr_r}:${filesize} 0x4f000000
...
Starting kernel ...

[    0.000000] Booting Linux on physical CPU 0x0000000000 [0x411fd070]
...
[    0.000000] earlycon: pl11 at MMIO 0x0000000009000000 (options '')
[    0.000000] printk: legacy bootconsole [pl11] enabled
...
[    1.951909] Serial: AMBA PL011 UART driver
[    2.588037] Internal error: synchronous external abort: 0000000096000010 [#1]  SMP
...
[    2.635705] pc : amba_read_periphid+0x128/0x2a0
[    2.643402] lr : amba_read_periphid+0x114/0x2a0
...
[    2.690056] Call trace:
[    2.690419]  amba_read_periphid+0x128/0x2a0 (P)
[    2.690797]  amba_device_add+0x98/0xbc
[    2.691023]  of_platform_bus_create+0x35c/0x4d4
[    2.704074]  of_platform_populate+0x84/0x15c
[    2.704433]  of_platform_default_populate_init+0xec/0x114
[    2.704752]  do_one_initcall+0x60/0x1e0
[    2.704974]  kernel_init_freeable+0x24c/0x2c8
[    2.705202]  kernel_init+0x20/0x140
[    2.705379]  ret_from_fork+0x10/0x20
[    2.784928] Kernel panic - not syncing: Attempted to kill init! exitcode=0x0000000b
```

(이 실험은 디스크 사본에 `virt.dtb`를 추가해서 돌렸다. 중간 로그는 줄였다.)

QEMU가 `-bios` 구성에서 실제로 만든 DT와 비교하면 원인이 보인다:

```sh
diff <(grep -v seed /var/lab/out/virt.dts) <(grep -v seed c3_live.dts)
```

```text
263,283d262
< 	gpio-keys {
< 		compatible = "gpio-keys";
<
< 		poweroff {
< 			gpios = <0x8004 0x03 0x00>;
< 			linux,code = <0x74>;
< 			label = "GPIO Key Poweroff";
< 		};
< 	};
<
< 	pl061@9030000 {
< 		phandle = <0x8004>;
< 		clock-names = "apb_pclk";
< 		clocks = <0x8000>;
< 		interrupts = <0x00 0x07 0x04>;
< 		gpio-controller;
< 		#gpio-cells = <0x02>;
< 		compatible = "arm,pl061", "arm,primecell";
< 		reg = <0x00 0x9030000 0x00 0x1000>;
< 	};
<
```

**출력에서 볼 것**: 정적 DTB에는 `pl061@9030000`(GPIO 컨트롤러)가 있지만, QEMU는 펌웨어(`-bios`)를 줄 때 이 장치를 만들지 않는다(펌웨어 몫으로 남기는 구성). 커널은 DT를 믿고 `compatible = "arm,primecell"` 노드를 AMBA 장치로 등록하면서 `amba_read_periphid()`로 0x9030fe0의 PeriphID를 읽었다 → **§3.5에서 U-Boot `md`로 같은 주소를 읽었을 때와 똑같은 `esr 0x96000010` synchronous external abort**. 다만 이번엔 커널 init 스레드 안에서 나서 init이 죽고 패닉. `earlycon` 없이는 콘솔 드라이버가 등록되기 전에 죽어서 **"Starting kernel ..." 이후 아무 출력도 없었다**.

> **교훈 세 가지**: ① DT는 "하드웨어가 이렇게 생겼다"는 **약속**이다. 없는 장치를 적으면 커널이 그 레지스터를 찌르다 죽는다 — 보드 리비전마다 DTB가 다른 이유. ② "Starting kernel 후 무소식"의 1순위 디버깅 도구는 **`earlycon`**(DT `stdout-path`나 `earlycon=pl011,0x9000000`)이다(C6). ③ 같은 주소를 U-Boot `md`로 먼저 읽어 보면 "그 장치가 정말 있나"를 커널 없이 확인할 수 있다.

---

## 12. 측정 — 부팅 방법별 "U-Boot가 커널을 넘기기까지" 시간

### 12.1 방법

각 방법을 같은 QEMU 구성(디스크 + `-netdev user,tftp=...` + virtio-net-device)으로 3번씩 번갈아 부팅하고, `expect`의 `@MARK`로 호스트 시계를 찍었다.

- **U-Boot 구간** = 첫 로드 명령을 보내기 직전(`@MARK go`) → `Starting kernel` 문자열이 보일 때.
- **커널 구간** = `Starting kernel` → initramfs의 `lab initramfs:` 줄이 보일 때.
- 값은 3회의 **중앙값**(ms). 커맨드라인은 모두 `console=ttyAMA0 LAB_CMD=true`(extlinux만 자기 `append`).

방법별 명령 파일(요지):

```text
disk     : virtio scan; load Image; load initrd.gz; booti
tftp     : setenv autoload no; dhcp; tftp Image; tftp initrd.gz; booti
fit      : virtio scan; load c3.itb (45 MB); bootm
fitgz    : virtio scan; load c3_gz.itb (17.5 MB); bootm   (gzip kernel)
fittftp  : dhcp; tftp c3.itb; bootm
extlinux : bootflow scan -lb virtio
```

### 12.2 결과

원자료(`이름 회차 prompt go Starting_kernel lab_initramfs poweroff`, ms):

```text
disk 1 273 277 339 1419 1463
tftp 1 171 177 1987 3128 3188
fit 1 194 197 3297 4310 4358
fitgz 1 180 184 1425 2333 2371
fittftp 1 164 170 5315 6764 6819
extlinux 1 219 220 1288 2270 2320
disk 2 165 167 210 1164 1215
tftp 2 170 172 1664 2646 2703
fit 2 199 201 3597 5126 5207
fitgz 2 315 320 1784 2716 2755
fittftp 2 149 152 5726 6910 6981
extlinux 2 200 201 1247 2069 2110
disk 3 153 155 196 986 1024
tftp 3 146 149 1903 2866 2913
fit 3 168 171 3770 4965 5014
fitgz 3 176 178 1261 2170 2225
fittftp 3 183 187 5665 6962 7035
extlinux 3 207 210 1262 2133 2175
```

| 방법 | U-Boot 구간 (3회) | 중앙값 | 커널 구간 중앙값 | 무엇이 시간을 먹나 |
|---|---|---|---|---|
| disk: load + booti | 62, 43, 41 | **43 ms** | 954 ms | 40.8 MiB를 virtio에서 읽기 (1.5~1.8 GiB/s) |
| extlinux (bootflow) | 1068, 1046, 1052 | **1052 ms** | 871 ms | 메뉴 `timeout 1` → 1초 대기 + 스캔 |
| FIT-gzip (disk) | 1241, 1464, 1083 | **1241 ms** | 909 ms | sha256(15 MB) + gunzip 41 MB |
| tftp: Image + initrd | 1810, 1492, 1754 | **1754 ms** | 982 ms | TFTP 23~30 MiB/s |
| FIT (disk) | 3100, 3396, 3599 | **3396 ms** | 1195 ms | sha256(43 MB) — TCG에서 느림 |
| FIT (tftp) | 5145, 5574, 5478 | **5478 ms** | 1297 ms | TFTP + sha256 |

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="22" text-anchor="middle" font-size="13" font-weight="bold">부팅 방법별 시간 (중앙값, ms) — QEMU TCG</text>
  <rect x="170" y="32" width="14" height="10" fill="#4a7bd0" fill-opacity="0.75"/>
  <text x="190" y="42" font-size="12">U-Boot: 로드 명령 → Starting kernel</text>
  <rect x="420" y="32" width="14" height="10" fill="#e08a3c" fill-opacity="0.75"/>
  <text x="440" y="42" font-size="12">커널 → initramfs</text>
  <text x="162" y="65" text-anchor="end" font-size="12">disk: load + booti</text>
  <rect x="170" y="50" width="2.5" height="22" fill="#4a7bd0" fill-opacity="0.75"/>
  <rect x="172.5" y="50" width="54.5" height="22" fill="#e08a3c" fill-opacity="0.75"/>
  <text x="233.0" y="65" font-size="12">43 + 954 ms</text>
  <text x="162" y="99" text-anchor="end" font-size="12">extlinux (bootflow)</text>
  <rect x="170" y="84" width="60.1" height="22" fill="#4a7bd0" fill-opacity="0.75"/>
  <rect x="230.1" y="84" width="49.8" height="22" fill="#e08a3c" fill-opacity="0.75"/>
  <text x="285.9" y="99" font-size="12">1052 + 871 ms</text>
  <text x="162" y="133" text-anchor="end" font-size="12">FIT-gzip (disk)</text>
  <rect x="170" y="118" width="70.9" height="22" fill="#4a7bd0" fill-opacity="0.75"/>
  <rect x="240.9" y="118" width="51.9" height="22" fill="#e08a3c" fill-opacity="0.75"/>
  <text x="298.9" y="133" font-size="12">1241 + 909 ms</text>
  <text x="162" y="167" text-anchor="end" font-size="12">tftp: Image + initrd</text>
  <rect x="170" y="152" width="100.2" height="22" fill="#4a7bd0" fill-opacity="0.75"/>
  <rect x="270.2" y="152" width="56.1" height="22" fill="#e08a3c" fill-opacity="0.75"/>
  <text x="332.3" y="167" font-size="12">1754 + 982 ms</text>
  <text x="162" y="201" text-anchor="end" font-size="12">FIT (disk)</text>
  <rect x="170" y="186" width="194.1" height="22" fill="#4a7bd0" fill-opacity="0.75"/>
  <rect x="364.1" y="186" width="68.3" height="22" fill="#e08a3c" fill-opacity="0.75"/>
  <text x="438.3" y="201" font-size="12">3396 + 1195 ms</text>
  <text x="162" y="235" text-anchor="end" font-size="12">FIT (tftp)</text>
  <rect x="170" y="220" width="313.0" height="22" fill="#4a7bd0" fill-opacity="0.75"/>
  <rect x="483.0" y="220" width="74.1" height="22" fill="#e08a3c" fill-opacity="0.75"/>
  <text x="563.1" y="235" font-size="12">5478 + 1297 ms</text>
  <line x1="170" y1="258" x2="570" y2="258" stroke="currentColor"/>
  <line x1="170.0" y1="258" x2="170.0" y2="263" stroke="currentColor"/>
  <text x="170.0" y="277" text-anchor="middle" font-size="12">0s</text>
  <line x1="227.1" y1="258" x2="227.1" y2="263" stroke="currentColor"/>
  <text x="227.1" y="277" text-anchor="middle" font-size="12">1s</text>
  <line x1="284.3" y1="258" x2="284.3" y2="263" stroke="currentColor"/>
  <text x="284.3" y="277" text-anchor="middle" font-size="12">2s</text>
  <line x1="341.4" y1="258" x2="341.4" y2="263" stroke="currentColor"/>
  <text x="341.4" y="277" text-anchor="middle" font-size="12">3s</text>
  <line x1="398.6" y1="258" x2="398.6" y2="263" stroke="currentColor"/>
  <text x="398.6" y="277" text-anchor="middle" font-size="12">4s</text>
  <line x1="455.7" y1="258" x2="455.7" y2="263" stroke="currentColor"/>
  <text x="455.7" y="277" text-anchor="middle" font-size="12">5s</text>
  <line x1="512.9" y1="258" x2="512.9" y2="263" stroke="currentColor"/>
  <text x="512.9" y="277" text-anchor="middle" font-size="12">6s</text>
  <line x1="570.0" y1="258" x2="570.0" y2="263" stroke="currentColor"/>
  <text x="570.0" y="277" text-anchor="middle" font-size="12">7s</text>
  <text x="370" y="305" text-anchor="middle" font-size="12">경과 시간 (각 3회 측정의 중앙값)</text>
</svg>
```
그림 6 — 부팅 방법별 시간. 파랑은 U-Boot가 이미지를 읽고 검증해 커널로 점프하기까지, 주황은 커널이 initramfs의 `/init`을 실행하기까지. 커널 구간은 방법과 거의 무관하고(0.9~1.3초), 차이는 전부 U-Boot 구간에서 난다.

### 12.3 해석

- **디스크 직접 load가 압도적으로 빠르다(43 ms)** — QEMU virtio-blk는 호스트 page cache에서 읽으므로 1.5 GiB/s가 넘는다. 실제 eMMC(HS200 ~150 MB/s)라면 40 MiB에 0.3초쯤, SD 카드(~20 MB/s)라면 2초쯤 걸린다. 실제 보드에서는 **저장장치 대역폭 × 이미지 크기**가 U-Boot 구간을 지배한다.
- **FIT의 대가는 해시 계산이다**: 같은 디스크에서 FIT는 3.4초. 읽기는 수십 ms이니, 나머지 3초 이상이 43 MB에 대한 sha256이다(TCG는 ARMv8 Crypto Extension의 SHA 명령을 소프트웨어로 흉내 내고, 이 U-Boot가 그것을 쓰는지도 확인하지 않았다 — 실제 Cortex-A53 하드웨어라면 훨씬 빠르다). 그래도 **"보안·무결성은 공짜가 아니다"**는 결론은 같다: 양산 보드에서 FIT 검증이 부팅 시간의 큰 덩어리가 되는 일이 실제로 있고, 그래서 해시 대상(커널만? 전부?)과 알고리즘·하드웨어 가속(CAAM, CE)을 고른다.
- **압축 FIT가 비압축 FIT보다 빠르다(1.2초 vs 3.4초)**: 해시할 데이터가 43 MB → 15 MB로 줄어든 효과가 gunzip 비용보다 컸다. 저장장치가 느린 실제 보드에서는 이 효과가 더 커진다(읽기도 줄어든다). 반대로 아주 빠른 NVMe + 느린 CPU라면 비압축이 이길 수 있다 — **측정해서 고른다**.
- **TFTP는 디스크보다 느리지만 개발 루프에서는 이긴다**: 1.7초가 걸려도 SD 카드를 뺐다 꽂는 30초보다 훨씬 빠르다. 그리고 TFTP 속도는 부하에 민감하다 — §8.2 세션은 VM이 다른 빌드로 바빴던 때라 4.6 MiB/s(40 MiB에 약 9초)였다. 같은 측정을 여러 번 해야 하는 이유다.
- **extlinux의 1초는 설정 실수다**: `timeout 1`이 1초 대기가 되었다. 메뉴가 필요 없으면 `timeout 0`(또는 줄 삭제)으로 바로 부팅한다. 이것이 부팅 시간 최적화(C7)의 전형적인 첫 발견이다 — "기다리는 시간"부터 없앤다(`bootdelay`, 메뉴 timeout, 없는 장치 스캔, 네트워크 PXE 시도).

---

## 13. 커널 안에서는 무슨 일이 일어나나

### 13.1 U-Boot 쪽: `booti`/`bootm`이 점프 직전에 하는 일

U-Boot 소스(`/var/lab/src/u-boot`)에서 경로를 따라가면 이렇다.

```
do_booti()                       cmd/booti.c
  booti_start()                  - gzip magic? -> needs kernel_comp_addr_r
    booti_setup()                arch/arm/lib/image.c: check "ARM\x64" magic,
                                 read text_offset/image_size/flags, pick reloc addr
  bootm_run_states(...)          boot/bootm.c: find ramdisk/fdt, overlap check
                                 ("RD image overlaps OS image")
    image_setup_linux()          boot/image-board.c
      boot_ramdisk_high()        copy initrd below initrd_high  -> "Loading Ramdisk to"
      boot_relocate_fdt()        copy DTB, add free space       -> "Loading Device Tree to"
      image_setup_libfdt()       boot/image-fdt.c
        fdt_chosen()             boot/fdt_support.c: /chosen/bootargs = $bootargs
        fdt_initrd()             /chosen/linux,initrd-start/end
        (board fixups: memory node, MAC, ...)
    boot_jump_linux()            arch/arm/lib/bootm.c: "Starting kernel ..."
                                 caches off, jump: x0 = DTB phys addr, x1..x3 = 0
```

`bootm`(FIT)은 앞부분만 다르다: FIT를 파싱하고 configuration을 골라 해시·서명을 검증한 뒤, 커널을 `load` 주소로 복사(필요하면 압축 해제)하고 같은 `image_setup_linux()` → `boot_jump_linux()`로 합류한다. 그래서 FIT로 부팅해도 "Loading Ramdisk to 5d206000"이 똑같이 찍혔다.

### 13.2 arm64 부트 프로토콜 — 커널이 기대하는 CPU 상태

커널 문서 `Documentation/arch/arm64/booting.rst`가 부트로더에게 요구하는 것:

```
x0          physical address of DTB (8-byte aligned, <= 2 MiB, not overlapping kernel)
x1, x2, x3  0 (reserved)
MMU         off
D-cache     off for the kernel image range (cleaned to PoC)
I-cache     on or off
EL          EL2 (recommended, for KVM) or non-secure EL1
Image       2 MiB aligned base + text_offset, image_size bytes free after it
```

이 랩의 QEMU는 virtualization을 켜지 않아 CPU가 EL1에서 시작한다 — 그래서 커널이 `CPU: All CPU(s) started at EL1`, `kvm [1]: HYP mode not available`이라고 찍었다(§9.4 세션의 커널 로그 — 노트에서는 생략한 부분). 실제 보드에서는 TF-A가 EL3, U-Boot가 EL2에서 돌고 커널을 EL2로 넘기는 것이 보통이다(C5).

### 13.3 커널 쪽: DT의 `/chosen`이 커널 변수가 되기까지

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="20" width="200" height="120" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="110" y="42" text-anchor="middle" font-size="13" font-weight="bold">U-Boot</text>
  <text x="110" y="66" text-anchor="middle" font-size="12">setenv bootargs</text>
  <text x="110" y="88" text-anchor="middle" font-size="12">fdt_chosen · fdt_initrd</text>
  <text x="110" y="110" text-anchor="middle" font-size="12">x0 = DTB 주소</text>
  <text x="110" y="130" text-anchor="middle" font-size="12">boot_jump_linux</text>
  <rect x="240" y="20" width="200" height="120" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="42" text-anchor="middle" font-size="13" font-weight="bold">DTB /chosen</text>
  <text x="340" y="66" text-anchor="middle" font-size="12">bootargs</text>
  <text x="340" y="88" text-anchor="middle" font-size="12">linux,initrd-start/end</text>
  <text x="340" y="110" text-anchor="middle" font-size="12">rng-seed · kaslr-seed</text>
  <text x="340" y="130" text-anchor="middle" font-size="12">stdout-path</text>
  <rect x="470" y="20" width="200" height="120" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="570" y="42" text-anchor="middle" font-size="13" font-weight="bold">커널 early boot</text>
  <text x="570" y="66" text-anchor="middle" font-size="12">head.S: x0 저장</text>
  <text x="570" y="88" text-anchor="middle" font-size="12">__fdt_pointer</text>
  <text x="570" y="110" text-anchor="middle" font-size="12">setup_machine_fdt()</text>
  <text x="570" y="130" text-anchor="middle" font-size="12">early_init_dt_scan</text>
  <line x1="210" y1="80" x2="238" y2="80" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="440" y1="80" x2="468" y2="80" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="10" y="180" width="200" height="100" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="110" y="204" text-anchor="middle" font-size="12">boot_command_line</text>
  <text x="110" y="226" text-anchor="middle" font-size="12">→ 파라미터 파싱</text>
  <text x="110" y="248" text-anchor="middle" font-size="12">모르는 것은 init에게</text>
  <text x="110" y="270" text-anchor="middle" font-size="12">(LAB_CMD=...)</text>
  <rect x="240" y="180" width="200" height="100" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="204" text-anchor="middle" font-size="12">phys_initrd_start/size</text>
  <text x="340" y="226" text-anchor="middle" font-size="12">→ populate_rootfs</text>
  <text x="340" y="248" text-anchor="middle" font-size="12">Unpacking initramfs</text>
  <text x="340" y="270" text-anchor="middle" font-size="12">→ /init 실행</text>
  <rect x="470" y="180" width="200" height="100" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="570" y="204" text-anchor="middle" font-size="12">unflatten_device_tree</text>
  <text x="570" y="226" text-anchor="middle" font-size="12">→ of_root (device_node)</text>
  <text x="570" y="248" text-anchor="middle" font-size="12">→ /proc/device-tree</text>
  <text x="570" y="270" text-anchor="middle" font-size="12">→ 플랫폼 디바이스 생성</text>
  <line x1="530" y1="140" x2="140" y2="178" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="560" y1="140" x2="360" y2="178" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="590" y1="140" x2="580" y2="178" stroke="currentColor" marker-end="url(#ah5)"/>
</svg>
```
그림 7 — U-Boot 변수가 커널 상태가 되는 경로. U-Boot가 DTB `/chosen`에 써 넣은 값을 커널이 early boot에서 읽어 `boot_command_line`(커맨드라인), `phys_initrd_start`(initramfs), `of_root`(DT 트리 — `/proc/device-tree`의 원본)로 바꾼다.

- `arch/arm64/kernel/head.S`가 진입 시 `x0`를 저장하고, `arch/arm64/kernel/setup.c`의 `setup_machine_fdt(__fdt_pointer)`가 DTB를 매핑해 검증한다. DTB가 깨졌으면 여기서 멈춘다(콘솔도 없어 무소식 — earlycon도 DT를 못 읽으면 소용없다).
- `drivers/of/fdt.c`의 `early_init_dt_scan_chosen()`이 `bootargs`를 `boot_command_line`으로, `rng-seed`를 엔트로피로(그리고 지움), `linux,initrd-start/end`를 `phys_initrd_start`/`phys_initrd_size`로 가져간다. 커널이 모르는 파라미터(`LAB_CMD=...`)는 `Unknown kernel command line parameters "LAB_CMD=...", will be passed to user space.`라고 알리고 init 환경에 넘긴다 — 이 랩의 모든 부팅 로그에 실제로 찍힌다(노트에서는 커널 로그를 생략했다).
- `unflatten_device_tree()`가 평평한 DTB를 `struct device_node` 트리(`of_root`)로 펼치고, `of_platform_default_populate_init()`이 `compatible`이 있는 노드를 platform device로 만든다. §7의 `/c3_marker`와 §11.4의 `pl061@9030000`이 정확히 이 경로를 탔다(후자는 AMBA 경로 `amba_device_add()` → `amba_read_periphid()`에서 죽었다).

**드라이버를 쓸 때 다시 만나는 곳**: DT 노드 → platform device → 드라이버의 `of_match_table`과 `compatible` 매칭 → `probe()` (G 모듈). 드라이버가 `of_property_read_u32()`로 읽는 속성은 바로 U-Boot가 `fdt set`으로 고칠 수 있는 그 속성이다(D 모듈). 커맨드라인 파라미터는 모듈 파라미터(`module_param`, `<모듈>.<파라미터>=값`)로 이어진다(E 모듈).

---

## 14. Embedded 실무에서 다시 보기

### 14.1 센서 키트 bring-up 첫날의 U-Boot 체크리스트

새 i.MX8M Plus 기반 센서 키트 보드가 왔다고 하자.

1. 시리얼 콘솔에서 SPL·U-Boot 배너 확인 → `DRAM:` 크기가 보드 사양과 같은가? (다르면 DDR 설정 — C4)
2. `bdinfo`로 DRAM 범위·relocaddr·LMB 예약 확인 → 로드 주소 계획(§6.4 규칙).
3. `md`로 핵심 레지스터 확인 — 예를 들어 IMU가 붙은 I2C 컨트롤러의 클럭 게이트, 핀먹스. U-Boot에 `i2c` 명령이 있다면 `i2c dev 2; i2c probe`로 IMU 주소(예: 0x68)가 응답하는지 커널 없이 확인한다.
4. `dhcp; tftp`로 개발 PC에서 커널·DTB를 받아 부팅 → 매일의 개발 루프.
5. 커널이 안정되면 eMMC에 FIT + extlinux(또는 boot.scr)를 굽고 `bootcmd`/`boot_targets`를 고정, `bootdelay`를 줄이고, `saveenv`.

이 과정을 env 변수로 스크립트화해 두면 팀 전체가 같은 절차를 쓴다:

```text
setenv serverip 192.168.10.1
setenv netdev 'dhcp && tftp ${kernel_addr_r} Image && tftp ${fdt_addr_r} sensorkit.dtb && tftp ${ramdisk_addr_r} rootfs.cpio.gz && booti ${kernel_addr_r} ${ramdisk_addr_r}:${filesize} ${fdt_addr_r}'
setenv bootcmd 'run netdev'
saveenv
```

(관례적인 env 예시이며 이 랩에서 실행한 것이 아니다. 이 랩에서 같은 명령 순서를 실행한 결과는 §8.2.)

### 14.2 A/B 업데이트와 bootcount

필드에 나간 드론에서 OTA 업데이트가 실패해 벽돌이 되면 회수 비용이 크다. 그래서 U-Boot env에 `boot_slot`, `upgrade_available`, `bootcount`를 두고(§4.4의 `fw_setenv`), U-Boot 스크립트가 "업데이트 후 첫 부팅이 N번 실패하면 이전 슬롯으로" 되돌린다. U-Boot의 `CONFIG_BOOTCOUNT_LIMIT` + `bootlimit` + `altbootcmd`가 이 기능이고, Linux 쪽에서 부팅 성공을 확인하면 `fw_setenv upgrade_available 0`(그리고 `bootcount=0`)으로 확정한다. SWUpdate·RAUC·Mender가 이 패턴을 쓴다(L 모듈).

### 14.3 보드 리비전별 DTB 선택

센서 키트 rev A는 IMU가 I2C, rev B는 SPI라면 DTB가 다르다. 방법: (1) FIT에 DTB 두 개와 configuration 두 개를 넣고 U-Boot가 GPIO strap/EEPROM으로 리비전을 읽어 `bootm ${addr}#conf-revB`; (2) 공통 DTB + 리비전별 DT overlay(`fdtoverlays` in extlinux, 또는 `fdt apply`); (3) U-Boot 보드 코드가 `ft_board_setup()`에서 노드의 `status`를 고친다(§7의 `fdt set`을 C로 하는 것).

### 14.4 양산 보안 설정

양산 U-Boot는 콘솔이 공격면이다: `md`/`mw`로 메모리를 읽고, `setenv bootargs init=/bin/sh`로 루트 셸을 얻을 수 있다. 그래서 `bootdelay=-2`, `CONFIG_AUTOBOOT_KEYED`(비밀 문자열을 쳐야 멈춤), 환경을 읽기 전용으로(`CONFIG_ENV_IS_NOWHERE` 또는 env 쓰기 제한 `ENV_WRITEABLE_LIST`), 서명된 FIT만 부팅(`CONFIG_FIT_SIGNATURE` + required key) 같은 조치를 한다(C5, L 모듈).

---

## 15. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| initrd를 먼저, 커널을 나중에 load하고 `:${filesize}` 사용 | 커널이 initramfs 풀다 실패 (`Initramfs unpacking failed`) | `filesize`가 마지막 파일(커널) 크기 | initrd를 마지막에 읽거나 크기를 별도 변수에 저장 |
| `booti`에 initrd 크기 생략 | `Wrong Ramdisk Image Format` | raw cpio.gz는 크기 정보가 없음 | `addr:size` 형식 |
| 커널을 DTB·스크립트 위에 load | `FDT_ERR_BADMAGIC`, `Could not find a valid device tree` | DRAM 시작에 여러 개를 둠 | 로드 주소 표를 만들고 `image_size` 기준으로 간격 확보 |
| 커널이 커졌는데 `ramdisk_addr_r` 그대로 | `RD image overlaps OS image` | 커널 영역 침범 | `ramdisk_addr_r`을 위로, 또는 커널 다이어트 |
| `setenv`만 하고 `saveenv` 안 함 | 재부팅하면 설정이 사라짐 | env는 RAM에만 있었음 | `saveenv`; 그리고 `fw_env.config` 위치 일치 확인 |
| `setenv x cmd1; cmd2` (따옴표 없음) | `cmd2`가 바로 실행되고 x에는 cmd1만 | `;`가 명령 구분자 | `setenv x 'cmd1; cmd2'` |
| `boot.cmd`만 고치고 `mkimage` 안 함 | 옛 동작 그대로 | 실행되는 것은 `boot.scr` | 빌드 단계에 mkimage 포함 |
| 하드웨어와 맞지 않는 DTB | "Starting kernel" 후 무소식, earlycon으로 보면 external abort | 없는 장치를 DT가 기술 | 보드에 맞는 DTB, `earlycon`으로 확인 |
| extlinux `timeout 1`을 0.1초로 착각 | 부팅이 1초 느림 | U-Boot는 10으로 나눠 올림해 초로 씀 | 메뉴 불필요 시 `timeout 0` |
| env 크기·오프셋이 U-Boot와 `fw_env.config`에서 다름 | `fw_printenv`가 `Bad CRC`, 또는 U-Boot가 기본 env로 돌아감 | 서로 다른 블록을 봄 | 두 설정을 같은 값으로, redundant env 고려 |

---

## 16. 면접에서 이렇게 말한다

**Q.** Walk me through how U-Boot boots a Linux kernel on an arm64 board.

**A.** U-Boot는 DRAM과 콘솔·스토리지 드라이버를 초기화하고 자신을 DRAM 꼭대기로 재배치한 뒤, env를 읽고 `bootdelay`가 지나면 `bootcmd`를 실행한다. 요즘은 `bootflow scan`이 장치(bootdev)와 방법(bootmeth: extlinux, script, EFI, PXE)을 훑어 부팅할 것을 찾는다. 커널 `Image`, DTB, initramfs를 정해진 주소로 읽고, `booti`가 Image 헤더를 확인하고 겹침을 검사한 뒤 initrd와 DTB를 안전한 위치로 옮기고 `/chosen`에 bootargs와 initrd 위치를 써 넣는다. 마지막으로 캐시를 끄고 x0에 DTB 주소를 넣어 커널로 점프한다.

> U-Boot initializes DRAM, the console and storage/network drivers, relocates itself to the top of RAM, loads its environment, and after the bootdelay runs bootcmd. On current U-Boot that's standard boot: `bootflow scan` iterates boot devices and boot methods — extlinux, boot scripts, EFI, PXE — until it finds something bootable. It loads the kernel Image, the DTB and the initramfs into RAM, and `booti` checks the arm64 Image header, makes sure the regions don't overlap, relocates the initrd and DTB, and writes bootargs and the initrd range into the DT's /chosen node. Then it disables the caches and MMU and jumps to the kernel with x0 holding the physical address of the DTB.

**꼬리 질문**: "Where does the kernel command line come from?" → `bootargs` env → `fdt_chosen()`이 `/chosen/bootargs`로 → 커널 `early_init_dt_scan_chosen()`.

**Q.** What's the difference between bootcmd and bootargs?

**A.** `bootcmd`는 **U-Boot가 실행할 명령**(autoboot 때 돌리는 스크립트)이고, `bootargs`는 **커널에게 넘길 커맨드라인 문자열**이다. bootcmd는 U-Boot 안에서 끝나고, bootargs는 DT `/chosen/bootargs`를 거쳐 `/proc/cmdline`이 된다.

> bootcmd is the U-Boot command sequence that runs automatically when the autoboot countdown expires — it's how U-Boot decides what to load and boot. bootargs is just a string: the kernel command line. U-Boot writes it into /chosen/bootargs in the device tree before jumping, and the kernel parses it into boot_command_line, which you then see in /proc/cmdline.

**Q.** Where is the U-Boot environment stored, and how would you change it from Linux?

**A.** config가 정한다 — eMMC의 고정 오프셋, SPI NOR 섹터, FAT 파일, UBI 볼륨 등. 형식은 CRC32 + `key=value\0` 목록이고, redundant 설정이면 두 벌이다. Linux에서는 `fw_env.config`에 같은 위치를 적고 `fw_printenv`/`fw_setenv`로 읽고 쓴다. A/B 업데이트의 슬롯 선택과 bootcount가 이 방식이다.

> It depends on the board config — commonly a fixed offset on eMMC or a sector in SPI NOR, sometimes a uboot.env file on FAT or a UBI volume. The format is a CRC32 followed by NUL-separated key=value strings, optionally redundant with two copies. From Linux you use fw_printenv and fw_setenv from libubootenv with an fw_env.config that points to exactly the same device, offset and size. That's how A/B update agents flip the boot slot and manage bootcount.

**Q.** What is a FIT image and why would you use it over a plain Image plus DTB?

**A.** FIT는 DTB 형식의 컨테이너로 커널, DTB, initramfs 등을 각각 해시와 함께 담고, configuration으로 조합을 정의한다. 장점은 구성요소 버전이 항상 짝이 맞고, 해시로 손상을 검출하고, 서명으로 verified boot를 구현할 수 있고, 여러 보드용 DTB를 한 이미지에 넣을 수 있다는 것이다. 대가는 검증 시간 — 직접 재 보니 43 MB sha256이 QEMU에서 3초가 넘었다.

> A FIT is a container that reuses the flattened device tree format: an images node holds the kernel, DTBs and ramdisk, each with metadata and hashes, and a configurations node defines which combination boots together. You get atomic versioning of kernel+DTB+initrd, integrity checking before you jump, the ability to sign configurations for verified boot, and multiple board variants in one file. The cost is verification time — hashing a 43 MB image took over three seconds in my emulated setup, so in production you pick hash algorithms and hardware acceleration carefully.

**Q.** The board prints "Starting kernel ..." and then nothing. How do you debug it?

**A.** 먼저 커널 쪽 콘솔이 살아 있는지 — `earlycon`을 bootargs에 넣고 `console=`이 맞는 UART인지 확인한다. 다음으로 U-Boot에서 로드 주소와 크기(`image_size` 기준 겹침, DTB가 덮이지 않았는지 `fdt addr`/`fdt header`), DTB가 이 보드용인지 확인한다. 실제로 하드웨어에 없는 장치가 DTB에 있어서 커널이 PeriphID를 읽다 external abort로 죽는 것을 earlycon으로 잡은 적이 있다. 그래도 안 나오면 JTAG으로 PC를 보고 `__log_buf`를 메모리에서 덤프한다.

> First I make sure I can see kernel output at all: add earlycon and confirm console= matches the right UART, since a crash before the serial driver registers is completely silent. Then I check what U-Boot handed over — load addresses against the Image header's image_size, that the DTB is still valid at the address I pass, and that the DTB actually matches the hardware. I hit exactly that once: the DT described a PL061 that didn't exist, and the kernel took a synchronous external abort reading its peripheral ID; earlycon showed it immediately. If earlycon shows nothing, I attach a debugger and dump the kernel log buffer from memory.

**꼬리 질문**: "What's earlycon?" → 일반 콘솔 드라이버 전에 최소한의 MMIO 쓰기로 UART에 출력하는 early console. DT `stdout-path`나 `earlycon=pl011,0x9000000` 형식.

**Q.** How do you set up a fast development loop for kernel work on a new board?

**A.** 개발 PC에 TFTP(필요하면 NFS) 서버를 두고, U-Boot env에 `dhcp; tftp kernel; tftp dtb; booti` 스크립트를 넣어 리셋만 하면 최신 빌드가 부팅되게 한다. rootfs는 NFS root나 initramfs로. 커널 빌드 → TFTP 디렉터리 복사 → 리셋이 1분 안에 끝난다.

> I put a TFTP server — and usually an NFS export for the root filesystem — on my workstation, and store a netboot script in the U-Boot environment: dhcp, tftp the Image and DTB into the standard load addresses, booti. Then iterating is build, copy to the TFTP directory, reset the board. No SD card swapping, and with NFS root I can update user-space binaries without reflashing either.

**Q.** Why does U-Boot relocate the initrd and DTB before jumping?

**A.** 커널이 부팅 중 쓰는 낮은 메모리(이미지 뒤의 BSS, 초기 페이지 테이블)와 겹치지 않게 하고, DT를 고칠 여유 공간을 확보하기 위해서다. 위치는 `initrd_high`, `fdt_high`, `bootm_size`로 조정하고, 옮기지 않게 할 수도 있지만 그러면 안전한 배치를 직접 보장해야 한다.

> So they end up somewhere the kernel won't clobber during early boot — away from the image and its BSS — and so the DTB has room to grow when U-Boot adds /chosen properties and fixups. You can control or disable that with initrd_high, fdt_high and bootm_size, which saves a copy, but then guaranteeing a safe layout is on you.

---

## 17. 직접 해보기

1. **개념**: `bootdelay`가 `0`, `-1`, `-2`일 때 각각 어떻게 동작하나? 양산 보드에는 무엇을 쓰고 그 이유는?
   정답/힌트: 0 = 기다리지 않지만 이미 눌린 키는 확인, -1 = autoboot 안 함, -2 = 키 확인도 안 하고 바로 부팅. 양산은 -2 또는 `AUTOBOOT_KEYED`(보안·부팅 시간).
2. **레지스터**: §3.4의 방법으로 QEMU `virt`의 PL031 RTC(0x09010000)를 `md`로 읽어 PeriphID를 해독하고, `RTCDR`(0x000)을 두 번 읽어 초 단위로 증가하는지 확인하라.
   정답/힌트: PeriphID0이 `0x31`이면 part 0x031. `md.l 0x09010000 1`을 `sleep 2` 사이에 두 번.
3. **env**: §4.4처럼 `fw_setenv`로 `bootcmd`를 `echo C3-from-linux`로 바꾸고, U-Boot가 autoboot 때 그것을 실행하는지 확인하라(ub.exp 없이 `timeout 20 qemu ... -drive if=pflash,unit=1,...`).
   정답/힌트: 카운트다운 후 `C3-from-linux`가 찍히고 프롬프트로 돌아온다.
4. **스크립트**: `boot.cmd`를 고쳐 `c3_slot` 변수가 `B`이면 파티션 2(ext4)의 `/boot/Image`를, 아니면 파티션 1을 부팅하게 하라. `mkimage`를 다시 하는 것을 잊지 말 것.
   정답/힌트: `if test "${c3_slot}" = "B"; then setenv p 2; setenv prefix /boot/; else setenv p 1; fi` 후 `load virtio 0:${p} ...`.
5. **FIT**: `.its`에 configuration `conf-2`를 추가해 같은 커널 + 다른 커맨드라인용 DTB(`/chosen/bootargs`를 넣은 DTB)를 쓰고, `bootm 0x48000000#conf-2`로 부팅해 `/proc/cmdline`을 비교하라. bootargs env가 비어 있을 때와 있을 때 무엇이 이기나?
   정답/힌트: `fdt_chosen()`은 bootargs env가 있으면 DT 값을 덮어쓴다. env를 `setenv bootargs`로 지운 뒤 비교.
6. **관찰**: 게스트에서 `/proc/device-tree/chosen/`의 파일 목록을 보고, U-Boot의 `fdt print /chosen`(booti 전) 출력과 비교해 누가 무엇을 더하고 지웠는지 표로 정리하라.
   정답/힌트: U-Boot가 `bootargs`, `linux,initrd-start/end`를 더함. 커널은 `rng-seed`를 지움(`fdt_nop_property`).
7. **측정**: extlinux.conf의 `timeout`을 0으로 바꾸고 §12의 측정을 다시 해 extlinux 막대가 디스크 직접 load 수준으로 내려오는지 확인하라.
   정답/힌트: 약 1초가 사라져야 한다. 남는 차이가 bootflow 스캔 비용이다.

---

## 18. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| U-Boot | Das U-Boot | 임베디드 Linux에서 가장 널리 쓰는 오픈소스 부트로더 + 콘솔 |
| relocation | 재배치 | U-Boot가 자신을 DRAM 꼭대기로 복사하고 거기서 계속 실행하는 것 |
| environment (env) | 환경변수 | `key=value` 설정 집합. RAM 해시 테이블 + CRC 붙은 비휘발 블록 |
| `bootcmd` | autoboot 명령 | 카운트다운이 끝나면 실행하는 U-Boot 명령 |
| `bootargs` | 커널 커맨드라인 | `/chosen/bootargs`로 커널에 넘어가는 문자열 |
| `bootdelay` | autoboot 대기 | 키 입력을 기다리는 초 |
| hush | U-Boot 셸 | 변수·if·for·`&&`를 지원하는 작은 셸 |
| `boot.scr` | 스크립트 이미지 | `mkimage -T script`로 헤더(CRC)를 붙인 U-Boot 스크립트 |
| LMB | Logical Memory Blocks | U-Boot의 물리 메모리 예약 장부. 예약 영역 load를 거부 |
| `booti` | arm64 Image 부팅 | Image 헤더 확인 → DT 수정 → 점프 |
| `bootm` | uImage/FIT 부팅 | FIT 파싱·검증 후 커널 배치 → 점프 |
| Image header | arm64 커널 헤더 | text_offset, image_size, flags, 매직 `ARM\x64` |
| `/chosen` | DT 노드 | 부트로더 → 커널 전달용(bootargs, initrd 위치, stdout-path) |
| standard boot (bootstd) | U-Boot 부팅 프레임워크 | bootdev × bootmeth → bootflow |
| bootdev / bootmeth / bootflow | 장치 / 방법 / 찾은 것 | 어디서 / 어떻게 / 무엇을 부팅하나 |
| extlinux.conf | 부팅 설정 파일 | syslinux 형식의 커널·initrd·fdt·append 기술 |
| distro boot | 옛 표준 부팅 | env 스크립트 기반 장치 스캔 (bootstd의 전신) |
| FIT | Flattened Image Tree | FDT 형식의 다중 이미지 컨테이너 + 해시/서명 |
| `.its` / `.itb` | FIT 소스 / 바이너리 | `mkimage -f`로 변환 |
| TFTP | Trivial FTP | UDP 기반 단순 파일 전송, 네트워크 부팅용 |
| PXE | Preboot eXecution Env | DHCP + TFTP로 설정 파일·커널을 찾는 규칙 |
| `fw_printenv`/`fw_setenv` | Linux env 도구 | libubootenv, `fw_env.config`로 env 블록 접근 |
| earlycon | 초기 콘솔 | 정식 드라이버 전 UART 출력, "무소식" 디버깅 1순위 |
| synchronous external abort | 동기 외부 abort | 버스가 에러 응답 — 없는/꺼진 장치 접근 |

---

## 19. 요약 & 체크리스트

U-Boot는 "보드 초기화 + 디버그 셸 + 커널 로더"다. 콘솔에서 `md`/`mw`로 메모리·레지스터를 직접 다루고, 환경변수(RAM 해시 테이블 ↔ CRC32 붙은 비휘발 블록 ↔ Linux의 `fw_setenv`)로 동작을 조정한다. Linux 부팅은 결국 "Image·DTB·initrd를 겹치지 않는 주소로 읽고, `/chosen`에 bootargs와 initrd 위치를 쓰고, x0 = DTB로 점프"이며, 디스크 `load`, `tftp`, extlinux(standard boot), FIT `bootm`은 모두 이 한 경로의 다른 포장이다. 이 랩에서 측정하니 차이는 전부 U-Boot 구간(읽기·검증·대기)에서 났다: 디스크 43 ms, extlinux 1.05초(메뉴 timeout), 압축 FIT 1.24초, TFTP 1.75초, 비압축 FIT 3.4초(sha256). 실패 메시지(`Wrong Ramdisk Image Format`, `RD image overlaps OS image`, `Bad Data Hash`, `FDT_ERR_BADMAGIC`)는 각자 원인을 정확히 가리키며, "Starting kernel 후 무소식"은 earlycon으로 보면 대개 DTB와 하드웨어의 불일치나 주소 문제다.

- [ ] U-Boot 배너의 각 줄(DRAM, relocation, Flash, env 로드, Net)이 무슨 단계인지 설명할 수 있다
- [ ] `md`/`mw`/`mm`/`cmp`로 DRAM과 MMIO 레지스터를 읽고 쓰고, PL011 PeriphID·FR·CR을 해독할 수 있다
- [ ] U-Boot의 Synchronous Abort 덤프에서 ESR·FAR·ELR로 원인을 가를 수 있다
- [ ] env가 어디 어떤 형식으로 저장되는지, `setenv`/`saveenv`/`env default`의 차이를 설명할 수 있다
- [ ] `fw_printenv`/`fw_setenv`와 `fw_env.config`로 Linux에서 env를 바꾸고 A/B 업데이트에 쓰는 법을 말할 수 있다
- [ ] hush 변수·`run`·`if`·`setexpr`로 부팅 스크립트를 쓰고 `mkimage -T script`로 `boot.scr`를 만들 수 있다
- [ ] `booti ${kernel_addr_r} ${ramdisk_addr_r}:${filesize} ${fdt_addr}`의 각 인자와 `filesize` 함정을 설명할 수 있다
- [ ] arm64 Image 헤더(text_offset, image_size, flags, magic)를 `md` 덤프에서 읽을 수 있다
- [ ] 로드 주소를 겹치지 않게 설계하고, U-Boot가 initrd·DTB를 옮기는 이유를 설명할 수 있다
- [ ] `dhcp`/`tftp`로 네트워크 부팅하고, `bootflow scan/list/boot`와 extlinux.conf를 쓸 수 있다
- [ ] `.its`를 써서 FIT를 만들고 `iminfo`/`bootm`으로 검증·부팅하며, FIT의 장단점을 측정값으로 말할 수 있다
- [ ] `fdt set`으로 DT를 고쳐 `/proc/device-tree`에서 확인하고, 부팅 실패 메시지별 원인을 말할 수 있다

## 참고 자료

- U-Boot 공식 문서 — https://docs.u-boot.org/en/latest/ (Usage: Environment, Standard Boot, `bootflow`/`booti`/`bootm`/`fdt`/`setexpr` 명령 페이지)
- U-Boot 문서: Standard boot 개요 — https://docs.u-boot.org/en/latest/develop/bootstd/overview.html
- U-Boot 문서: Flattened Image Tree (FIT) — https://docs.u-boot.org/en/latest/usage/fit/index.html
- U-Boot 문서: extlinux/PXE 형식 — 소스 트리 `doc/usage/pxe.rst`
- Linux 커널 문서: Booting AArch64 Linux — https://docs.kernel.org/arch/arm64/booting.html
- Linux 커널 문서: The kernel's command-line parameters — https://docs.kernel.org/admin-guide/kernel-parameters.html
- ARM PrimeCell UART (PL011) Technical Reference Manual (ARM DDI 0183)
- U-Boot 소스(이 랩 `/var/lab/src/u-boot`): `cmd/booti.c`, `arch/arm/lib/image.c`, `boot/bootm.c`, `boot/fdt_support.c`, `boot/image-board.c`, `boot/bootmeth_script.c`, `boot/pxe_utils.c`, `env/env.c`
- Linux 소스(elixir.bootlin.com, v6.18): `drivers/of/fdt.c`(`early_init_dt_scan_chosen`), `arch/arm64/kernel/setup.c`(`setup_machine_fdt`), `drivers/amba/bus.c`(`amba_read_periphid`)
- Bootlin, "Embedded Linux system development" 교육 슬라이드 (U-Boot 장) — https://bootlin.com/doc/training/embedded-linux/
- libubootenv (`fw_printenv`/`fw_setenv`) — https://github.com/sbabic/libubootenv
