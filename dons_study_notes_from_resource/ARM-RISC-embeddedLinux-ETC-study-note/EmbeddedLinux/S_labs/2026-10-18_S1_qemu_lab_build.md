# S1. QEMU 랩 만들기 — 보드 없이 TF-A → U-Boot → Linux → BusyBox 부트 체인을 소스부터 직접 세운다

> **이 노트를 다 읽으면**: `/var/lab`이 어떻게 만들어졌는지 재현해서 내 GitHub 저장소 하나로 "소스 받기 → 검증 → 빌드 → 부팅"을 `make` 한 번에 돌릴 수 있다 · tinyconfig + fragment로 QEMU `virt` 전용 6 MB 커널을 5분 안에 만들고, 빠진 옵션을 자동으로 잡는다 · 같은 커널을 세 가지 경로(직접 `-kernel`, U-Boot, TF-A secure → U-Boot → `booti`)로 부팅하고 무엇이 다른지 증거로 말할 수 있다 · virtio-blk ext4 rootfs, user 네트워크 + 9p 파일 공유, gdbstub("JTAG") 워크플로를 붙인다
> **왜 필요한가**: "보드 없이 어떻게 공부/개발했나?", "부트 체인을 직접 빌드해 봤나?", "커널 설정을 어떻게 최소화하나?"는 Embedded Linux 면접의 단골 질문이고, S2–S7 실습 전부가 이 랩 위에서 돈다
> **Don 기준 난이도**: 부트 단계(ROM → 부트로더 → 펌웨어), 이미지 레이아웃, JTAG 디버깅은 SSD/RF bring-up에서 이미 해 봤다 / 새로 배울 부분은 Kconfig fragment 병합, U-Boot·TF-A·BusyBox 각자의 빌드 관례(O=, BUILD_BASE=, CONFIG_PREFIX=), QEMU의 fw_cfg·gdbstub·9p, 재현 가능한 Makefile 설계
> **선행 노트**: C1(부트 지도), C3(U-Boot 사용), C5(TF-A), C6(커널 시작), E2(커널 설정·빌드), K2(BusyBox), K3(Buildroot), M4(커널 디버거) — 개념은 거기서 배우고, 여기서는 손으로 만든다

---

## 0. 큰 그림 — 무엇을 만들고, 끝나면 무엇이 남나

지금까지 146개 노트는 `/var/lab`에 **이미 만들어져 있는** 커널·U-Boot·TF-A·initramfs를 빌려 썼다. 실무에서는 다르다. 새 보드가 오면(Don이 Apple에서 새 실리콘을 받을 때처럼) **부트 체인 전체를 소스에서 직접 세우는 것**이 첫 주 일이다. S1은 그 첫 주를 보드 없이 연습하는 프로젝트다.

최종 산출물은 두 가지다.

| 산출물 | 위치 | 내용 |
|---|---|---|
| 랩 빌더 저장소 | Mac `~/.elinux_scratch/S1/qemu-lab/` (그대로 GitHub에 올린다) | `Makefile`, 커널 fragment, `/init`, expect 스크립트, 체크섬, Buildroot 외부 트리 골격, README |
| 빌드 결과 | VM `/var/tmp/S1/out/` | `Image`(6.2 MB), `vmlinux`, `u-boot.bin`, `flash.bin`(TF-A BL1+FIP), `initramfs.cpio.gz`, `rootfs.ext4` |

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 420">
<defs><marker id="ah1" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<rect x="10" y="10" width="660" height="400" rx="8" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="24" y="32" font-size="13">Mac (M2, macOS) — 소스: ~/.elinux_scratch/S1/qemu-lab (VM 에서는 읽기 전용)</text>
<rect x="30" y="46" width="620" height="352" rx="8" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="44" y="68" font-size="13">Lima VM elinux — Ubuntu 26.04 aarch64 · 산출물 /var/tmp/S1</text>
<text x="50" y="126" font-size="12">VM 쪽 (호스트 역할)</text>
<rect x="50" y="136" width="170" height="30" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="60" y="156" font-size="12">out/Image, flash.bin</text>
<rect x="50" y="176" width="170" height="30" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="60" y="196" font-size="12">터미널 (-nographic)</text>
<rect x="50" y="216" width="170" height="30" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="60" y="236" font-size="12">out/rootfs.ext4</text>
<rect x="50" y="256" width="170" height="30" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="60" y="276" font-size="12">curl 127.0.0.1:43100</text>
<rect x="50" y="296" width="170" height="30" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="60" y="316" font-size="12">/var/tmp/S1/share</text>
<rect x="50" y="336" width="170" height="30" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="60" y="356" font-size="12">gdb out/vmlinux</text>
<rect x="240" y="86" width="396" height="300" rx="8" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="254" y="106" font-size="13">qemu-system-aarch64 -M virt · TCG (KVM 없음)</text>
<text x="256" y="126" font-size="12">QEMU 장치</text>
<text x="436" y="126" font-size="12">CPU 예외 레벨 (cortex-a57)</text>
<rect x="256" y="136" width="150" height="30" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="266" y="156" font-size="12">-bios / -kernel</text>
<rect x="256" y="176" width="150" height="30" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="266" y="196" font-size="12">pl011 @0x09000000</text>
<rect x="256" y="216" width="150" height="30" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="266" y="236" font-size="12">virtio-blk (mmio)</text>
<rect x="256" y="256" width="150" height="30" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="266" y="276" font-size="12">virtio-net (user)</text>
<rect x="256" y="296" width="150" height="30" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="266" y="316" font-size="12">virtio-9p host0</text>
<rect x="256" y="336" width="150" height="30" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="266" y="356" font-size="12">gdbstub :43101</text>
<g stroke="currentColor" marker-end="url(#ah1)"><line x1="220" y1="151" x2="254" y2="151"/><line x1="220" y1="191" x2="254" y2="191"/><line x1="220" y1="231" x2="254" y2="231"/><line x1="220" y1="271" x2="254" y2="271"/><line x1="220" y1="311" x2="254" y2="311"/><line x1="220" y1="351" x2="254" y2="351"/></g>
<rect x="436" y="136" width="186" height="36" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="446" y="159" font-size="12">EL3 TF-A BL1 · BL2 · BL31</text>
<rect x="436" y="190" width="186" height="36" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="446" y="213" font-size="12">EL2/EL1 U-Boot (BL33)</text>
<rect x="436" y="244" width="186" height="36" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="446" y="267" font-size="12">EL1 Linux 6.18.55-s1</text>
<rect x="436" y="298" width="186" height="36" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="446" y="321" font-size="12">EL0 BusyBox /init, sh</text>
<g stroke="currentColor" marker-end="url(#ah1)"><line x1="529" y1="172" x2="529" y2="188"/><line x1="529" y1="226" x2="529" y2="242"/><line x1="529" y1="280" x2="529" y2="296"/></g>
<text x="436" y="356" font-size="12">EL3 단계는 secure=on 일 때만</text>
<line x1="406" y1="246" x2="436" y2="262" stroke="currentColor" stroke-dasharray="3 3"/>
<line x1="406" y1="191" x2="436" y2="262" stroke="currentColor" stroke-dasharray="3 3"/>
</svg>
```
그림 1 — 랩 구조. Mac에는 소스(저장소)만 있고, Lima VM이 빌드 호스트 역할을 하며, 그 안의 QEMU `virt` 머신이 "보드"다. 왼쪽 VM 쪽 자원이 QEMU 장치(가운데)를 통해 게스트 CPU의 예외 레벨 스택(오른쪽)과 만난다.

bare-metal 비유로 정리하면 이렇다.

| 이 랩 | Don이 아는 세계 |
|---|---|
| QEMU `virt` 머신 | 레퍼런스 보드 (회로도 = QEMU가 만든 Device Tree) |
| `flash.bin` (BL1 @0x0) | Boot ROM + SPI NOR 이미지 |
| TF-A BL31 | EL3 secure monitor (SSD로 치면 보안 부트 ROM 코드 + 전원 관리 펌웨어) |
| U-Boot | 2단 부트로더 (SSD FW의 loader) |
| `-gdb tcp::43101 -S` | JTAG 프로브 연결 + reset halt |
| `LAB_CMD=` 커널 인자 | 양산 테스트 지그의 "명령 하나 실행 후 결과 찍고 전원 끄기" |
| `make all` 두 번째 실행이 아무것도 안 함 | 재현 가능한 펌웨어 빌드 (같은 입력 → 재빌드 없음) |

### 0.1 `/var/lab`은 어떻게 만들어졌나 — 참고한 스크립트

과제는 `/var/lab/build_lab.sh`를 참고하라고 했지만, **실제 VM에는 그 기본 스크립트가 남아 있지 않다** (2026-10-18 확인). 남아 있는 것은 후속 커널 변형용 스크립트 6개다.

```sh
limactl shell elinux -- bash -lc 'ls /var/lab/*.sh; ls /var/lab/build /var/lab/dl'
```

```text
/var/lab/build_lab_domfs.sh
/var/lab/build_lab_mn.sh
/var/lab/build_lab_opq.sh
/var/lab/build_lab_sec.sh
/var/lab/build_lab_sysd.sh
/var/lab/build_lab_tracefs.sh
/var/lab/build:
busybox
linux
linux-dbg
...
tfa
trace-root
u-boot

/var/lab/dl:
busybox-1.37.0.tar.bz2
linux-6.18.55.tar.xz
tfa-v2.15.0.tar.gz
u-boot-v2026.10.tar.gz
ubuntu-base-24.04.5-base-arm64.tar.gz
```

(`build/`의 커널 변형 디렉터리 9개는 `...`로 줄였다.)

그래서 기본 랩의 설계는 **남은 흔적**(빌드 디렉터리의 `.config`, `rootfs/init`, README의 "Verified" 메모)과 후속 스크립트의 패턴에서 역으로 읽었다. 후속 스크립트(`build_lab_sec.sh`, `build_lab_opq.sh`)에서 가져온 설계 결정은 다음과 같다.

| `/var/lab` 스크립트의 패턴 | 이유 | S1에서 |
|---|---|---|
| 소스는 `src/` 한 벌, 빌드는 `O=$L/build/linux-<변형>` | 소스 트리를 깨끗하게 두고 변형을 여러 개 만든다 (E2 §4) | `O=$(B)/build/linux`, U-Boot도 `O=`, TF-A는 `BUILD_BASE=` |
| 이전 변형의 `.config`를 복사 → `scripts/config -e/-d/-m` → `olddefconfig` | 설정을 "차이"로 관리 | 같은 사상을 `merge_config.sh` + fragment 파일로 (git diff가 깔끔) |
| `for o in ...; do grep "^CONFIG_$o=" ... echo MISSING` | Kconfig가 의존성 때문에 옵션을 **조용히** 끈 걸 잡는다 | `scripts/check-fragment.sh`로 자동화, 실패 시 빌드 중단 |
| `step "smoke boot"`: `timeout ... LAB_CMD=uname~-r` | 빌드 직후 부팅 검증 | 모든 부팅 예제가 `LAB_CMD`로 자동 종료 |
| `set -euo pipefail`, `step()` 타임스탬프 | 실패를 숨기지 않고, 단계별 시간을 남긴다 | Make의 `.DELETE_ON_ERROR`, `/usr/bin/time` |
| `cp ... $L/out/...` + `ln -sfn $K $L/out/linux-build-<k>` | 산출물 디렉터리(out)와 빌드 트리 분리 | `out/Image`, `out/vmlinux -> build/linux/vmlinux` |

셸 스크립트 대신 **Makefile**을 고른 이유는 하나다: `/var/lab` 스크립트는 매번 처음부터 다시 돌지만, Make는 **파일 타임스탬프로 "무엇이 바뀌었나"를 추적**해서 바뀐 하류만 다시 만든다(§13). 포트폴리오에서 "idempotent rebuild"를 보여 주기 좋다.

---

## 1. 준비물 — 빌드 호스트

실행 환경: Mac(M2) → Lima VM `elinux` (Ubuntu 26.04.1, aarch64, 호스트 커널 `7.0.0-38-generic`, gcc 15.2, QEMU 10.2.1). VM이 aarch64라서 **크로스 컴파일러가 필요 없다** (`CROSS_COMPILE` 비움, B1 §10). x86 Linux 호스트라면 `gcc-aarch64-linux-gnu`를 추가하고 모든 make에 `CROSS_COMPILE=aarch64-linux-gnu-`를 붙이면 된다.

### 1.1 패키지 목록 (설치하지 말고 확인만)

| 패키지 | 누가 쓰나 |
|---|---|
| `build-essential bc bison flex` | 커널·U-Boot Kconfig/빌드 |
| `libssl-dev` | 커널(서명 도구), U-Boot `mkimage`, TF-A `fiptool` |
| `libgnutls28-dev uuid-dev` | U-Boot 호스트 도구 (`mkeficapsule`, `mkimage`) |
| `python3-pyelftools python3-setuptools swig` | U-Boot `binman`·pylibfdt |
| `libelf-dev` | 커널 objtool 류 |
| `device-tree-compiler` | `dtc` (U-Boot, TF-A, DT 실험) |
| `cpio e2fsprogs` | initramfs, `mkfs.ext4 -d` |
| `qemu-system-arm qemu-utils` | `qemu-system-aarch64`, `qemu-img` |
| `gdb expect curl xz-utils bzip2 gnupg` | 디버깅, 스크립트 부팅, 다운로드·검증 |

이 표를 스크립트로 만든 것이 `scripts/check-host.sh`다. **설치는 하지 않고** 빠진 것만 출력한다(공용 VM이라 apt 상태를 바꾸지 않는다는 규칙).

```sh
cat > scripts/check-host.sh <<'EOF'
#!/bin/bash
pk="build-essential bc bison flex libssl-dev libgnutls28-dev uuid-dev libelf-dev python3-pyelftools python3-setuptools swig device-tree-compiler cpio e2fsprogs qemu-system-arm qemu-utils gdb expect curl xz-utils bzip2 gnupg"
miss=""
for p in $pk; do dpkg -s "$p" >/dev/null 2>&1 || miss="$miss $p"; done
printf "%-24s %s\n" arch "$(uname -m)" kernel "$(uname -r)" cpus "$(nproc)" \
  gcc "$(gcc -dumpfullversion)" qemu "$(qemu-system-aarch64 --version | head -1 | awk '{print $4}')" \
  kvm "$( [ -e /dev/kvm ] && echo yes || echo 'no (TCG only)')" free_disk "$(df -h --output=avail /var/tmp | tail -1 | tr -d ' ')"
[ -z "$miss" ] && echo "packages: all $(echo $pk | wc -w) present" || { echo "MISSING:$miss"; echo "sudo apt-get install -y$miss"; exit 1; }
EOF
limactl shell elinux -- bash /Users/donh/.elinux_scratch/S1/qemu-lab/scripts/check-host.sh
```

```text
arch                     aarch64
kernel                   7.0.0-38-generic
cpus                     6
gcc                      15.2.0
qemu                     10.2.1
kvm                      no (TCG only)
free_disk                30G
packages: all 22 present
```

출력에서 볼 것: `kvm no` — Lima VM 안에서는 KVM이 없어서 QEMU가 **TCG(명령어 번역)** 로 돈다. 이 노트의 부팅 시간은 전부 TCG 숫자라서 실제 Cortex-A57 보드의 숫자가 아니다.

체크포인트: **이 단계가 끝나면 `packages: all 22 present`가 보여야 한다.** `MISSING:`이 나오면 마지막 줄의 apt 명령을 자기 머신에서(공용 VM이 아닌 곳에서) 실행한다.

### 1.2 리소스 예산

| 자원 | 이 랩에서 쓴 양 | 규칙 |
|---|---|---|
| CPU | `-j2` | 7명이 VM 6코어를 공유 |
| 디스크 | 소스 2.1 GB + 빌드 0.9 GB + out 85 MB, 최대 3.1 GB | `/var/tmp/S1` (VM의 `/tmp`는 2 GB tmpfs라 커널 빌드가 넘친다) |
| 게스트 RAM | 512 MB, TF-A 경로만 1024 MB | 게스트 ≤ 1 GB |
| 포트 | 43100 (hostfwd), 43101 (gdbstub) | `43000 + 100 × 1` |

---

## 2. 저장소 레이아웃

```text
qemu-lab/                       (Mac: ~/.elinux_scratch/S1/qemu-lab, VM에서는 읽기 전용)
  Makefile                      전체 파이프라인 (아래 단계별로 발췌)
  README.md                     빠른 시작, 측정값, 알려진 함정
  checksums/SHA256SUMS          고정한 tarball 해시 4개
  config/kernel-virt.fragment   tinyconfig 위에 얹는 46개 옵션
  rootfs/init                   initramfs와 ext4 rootfs 공용 /init
  rootfs/udhcpc.script          최소 DHCP 스크립트
  scripts/check-host.sh         준비물 점검
  scripts/check-fragment.sh     fragment 옵션 생존 검사
  scripts/tfa-booti.exp         TF-A → U-Boot → booti 자동화
  buildroot-external/           BR2_EXTERNAL 골격 (가이드, 실행 안 함)

/var/tmp/S1/                    (B=, 모든 산출물. 지워도 make all로 복구)
  dl/       tarball (캐시 /var/lab/dl 로의 symlink 또는 새로 받은 파일)
  src/      linux u-boot tfa busybox  (tarball을 푼 것)
  build/    linux u-boot tfa busybox  (O= / BUILD_BASE= 출력)
  rootfs/   BusyBox install 트리 + /init
  out/      Image vmlinux u-boot.bin flash.bin initramfs.cpio.gz rootfs.ext4
  .stamp/   verified, src-<name>, rootfs
```

핵심 설계: **저장소(입력)는 읽기 전용이어도 된다.** VM에서 Mac 홈은 읽기 전용으로 마운트되는데, 그래도 빌드가 된다는 것이 "소스와 산출물이 완전히 분리됐다"는 증거다. Makefile 맨 위의 변수들이 이 분리를 정의한다.

```make
SHELL   := /bin/bash
TOP     := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
B       ?= /var/tmp/S1
JOBS    ?= 2
DL_CACHE ?= /var/lab/dl
DL      := $(B)/dl
SRC     := $(B)/src
BLD     := $(B)/build
OUT     := $(B)/out
STAMP   := $(B)/.stamp
KVER  := 6.18.55
UBVER := 2026.10
TFVER := 2.15.0
BBVER := 1.37.0
.DELETE_ON_ERROR:
all: $(OUT)/Image $(OUT)/u-boot.bin $(OUT)/flash.bin $(OUT)/initramfs.cpio.gz $(OUT)/rootfs.ext4
```

`.DELETE_ON_ERROR`는 레시피가 중간에 실패하면 반쯤 만들어진 타깃 파일을 지운다. 이게 없으면 §6의 Kconfig 실패 뒤 망가진 `.config`가 "최신"으로 남아 다음 `make`가 그냥 지나간다 — 실제로 이 랩을 만들면서 처음에 빠뜨렸던 줄이다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 410">
<defs><marker id="ah2" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<text x="10" y="30" font-size="13">dl/ (캐시 링크)</text>
<text x="145" y="30" font-size="13">검증 · 풀기</text>
<text x="275" y="30" font-size="13">build/ (O= 출력)</text>
<text x="475" y="30" font-size="13">out/</text>
<rect x="10" y="50" width="120" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="20" y="72" font-size="13">linux 6.18.55</text>
<rect x="10" y="100" width="120" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="20" y="122" font-size="13">u-boot 2026.10</text>
<rect x="10" y="150" width="120" height="34" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="20" y="172" font-size="13">tfa 2.15.0</text>
<rect x="10" y="200" width="120" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="20" y="222" font-size="13">busybox 1.37.0</text>
<rect x="145" y="50" width="110" height="184" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="155" y="130" font-size="13">sha256sum -c</text><text x="155" y="150" font-size="13">tar -x → src/</text>
<rect x="275" y="50" width="180" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="285" y="72" font-size="13">tinyconfig + fragment</text>
<rect x="275" y="100" width="180" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="285" y="122" font-size="13">qemu_arm64_defconfig</text>
<rect x="275" y="150" width="180" height="34" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="285" y="172" font-size="13">PLAT=qemu all fip + dd</text>
<rect x="275" y="200" width="180" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="285" y="222" font-size="13">defconfig + STATIC=y</text>
<rect x="475" y="50" width="150" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="485" y="72" font-size="13">Image + vmlinux</text>
<rect x="475" y="100" width="150" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="485" y="122" font-size="13">u-boot.bin</text>
<rect x="475" y="150" width="150" height="34" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="485" y="172" font-size="13">flash.bin (64 MiB)</text>
<rect x="475" y="200" width="150" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="485" y="222" font-size="13">rootfs/ (트리)</text>
<g stroke="currentColor" marker-end="url(#ah2)">
<line x1="130" y1="67" x2="143" y2="67"/><line x1="130" y1="117" x2="143" y2="117"/><line x1="130" y1="167" x2="143" y2="167"/><line x1="130" y1="217" x2="143" y2="217"/>
<line x1="255" y1="67" x2="273" y2="67"/><line x1="255" y1="117" x2="273" y2="117"/><line x1="255" y1="167" x2="273" y2="167"/><line x1="255" y1="217" x2="273" y2="217"/>
<line x1="455" y1="67" x2="473" y2="67"/><line x1="455" y1="117" x2="473" y2="117"/><line x1="455" y1="167" x2="473" y2="167"/><line x1="455" y1="217" x2="473" y2="217"/>
</g>
<path d="M520,134 L440,148" stroke="currentColor" stroke-dasharray="4 3" fill="none" marker-end="url(#ah2)"/>
<text x="500" y="147" font-size="12">BL33=</text>
<rect x="340" y="270" width="160" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="350" y="292" font-size="13">initramfs.cpio.gz</text>
<rect x="520" y="270" width="130" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="530" y="292" font-size="13">rootfs.ext4</text>
<g stroke="currentColor" fill="none" marker-end="url(#ah2)">
<path d="M530,234 L440,268"/><path d="M570,234 L580,268"/>
<path d="M625,67 L662,67 L662,287 L652,287"/>
</g>
<text x="478" y="258" font-size="12">+ /boot/Image</text>
<rect x="160" y="340" width="490" height="34" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="172" y="362" font-size="13">make run-{direct,uboot,tfa,disk,net,gdb}</text>
<g stroke="currentColor" marker-end="url(#ah2)"><line x1="420" y1="304" x2="420" y2="338"/><line x1="585" y1="304" x2="585" y2="338"/></g>
<text x="10" y="398" font-size="12">모든 산출물이 파일 타깃 + .stamp/ 이라서 두 번째 make 는 "Nothing to be done" — 바뀐 입력의 하류만 다시 만든다</text>
</svg>
```
그림 2 — 빌드 파이프라인. 각 행이 컴포넌트 하나이고, 점선은 컴포넌트 사이 의존성(U-Boot가 TF-A FIP의 BL33으로 들어가고, 커널 Image가 ext4의 `/boot`로 들어간다).

---

## 3. Step 1 — 소스 받기와 검증

### 3.1 왜 검증하나

공급망 공격(빌드 서버가 받은 tarball이 바꿔치기된 경우)과 단순 손상(잘린 다운로드)을 막는 두 단계가 있다.

| 단계 | 무엇을 보장 | 도구 |
|---|---|---|
| 해시 (sha256) | "내가 고정한 바로 그 바이트"인지 | `sha256sum -c SHA256SUMS` |
| 서명 (GPG) | "그 바이트를 릴리스 담당자가 만들었는지" | `gpg --verify x.tar.sign` |

해시는 **저장소에 고정**해 두면 이후 빌드에서 인터넷 없이도 검사된다(Buildroot의 `.hash` 파일, Yocto의 `SRC_URI[sha256sum]`이 같은 개념, K3 §5). 서명은 해시를 처음 고정할 때 한 번 확인한다.

### 3.2 Makefile — 캐시가 있으면 링크, 없으면 다운로드

```make
K_URL  := https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-$(KVER).tar.xz
UB_URL := https://source.denx.de/u-boot/u-boot/-/archive/v$(UBVER)/u-boot-v$(UBVER).tar.gz
TF_URL := https://github.com/ARM-software/arm-trusted-firmware/archive/refs/tags/v$(TFVER).tar.gz
BB_URL := https://busybox.net/downloads/busybox-$(BBVER).tar.bz2

$(DL)/%:
	@mkdir -p $(DL)
	@if [ -f $(DL_CACHE)/$* ]; then ln -sf $(DL_CACHE)/$* $@; echo "cache  $*"; \
	 else case $* in $(K_TAR)) u=$(K_URL);; $(UB_TAR)) u=$(UB_URL);; $(TF_TAR)) u=$(TF_URL);; *) u=$(BB_URL);; esac; \
	      echo "fetch  $$u"; curl -fL --retry 3 -o $@.part $$u && mv $@.part $@; fi
fetch: $(addprefix $(DL)/,$(K_TAR) $(UB_TAR) $(TF_TAR) $(BB_TAR))
$(STAMP)/verified: $(TOP)/checksums/SHA256SUMS | fetch
	@mkdir -p $(STAMP); cd $(DL) && sha256sum -c $< && touch $@
verify: $(STAMP)/verified
```

`/var/lab/dl`의 tarball은 읽기 전용 캐시로 **symlink만** 건다(복사하면 220 MB 낭비). `.part`로 받고 `mv`하는 것은 다운로드가 중간에 끊겨도 잘린 파일이 완성품처럼 남지 않게 하려는 것이다. U-Boot·TF-A URL은 git 태그 아카이브인데, 이 형식은 서버가 즉석에서 만들어서 **해시가 영원히 같다는 보장이 약하다** — 그래서 실무에서는 git 커밋 해시를 같이 고정한다.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp && make -C /Users/donh/.elinux_scratch/S1/qemu-lab B=/var/tmp/S1 verify; \
  make -C /Users/donh/.elinux_scratch/S1/qemu-lab B=/var/tmp/S1 verify'
```

```text
make: Entering directory '/Users/donh/.elinux_scratch/S1/qemu-lab'
cache  linux-6.18.55.tar.xz
cache  u-boot-v2026.10.tar.gz
cache  tfa-v2.15.0.tar.gz
cache  busybox-1.37.0.tar.bz2
linux-6.18.55.tar.xz: OK
u-boot-v2026.10.tar.gz: OK
tfa-v2.15.0.tar.gz: OK
busybox-1.37.0.tar.bz2: OK
make: Leaving directory '/Users/donh/.elinux_scratch/S1/qemu-lab'
make: Entering directory '/Users/donh/.elinux_scratch/S1/qemu-lab'
make: Nothing to be done for 'verify'.
make: Leaving directory '/Users/donh/.elinux_scratch/S1/qemu-lab'
```

출력에서 볼 것: 두 번째 실행은 `Nothing to be done` — 스탬프 파일 `.stamp/verified`가 `SHA256SUMS`보다 새것이라서다.

### 3.3 고정한 해시가 업스트림과 같은지 — 처음 한 번

```sh
limactl shell elinux -- bash -lc 'mkdir -p /var/tmp/S1/verify && cd /var/tmp/S1/verify && \
  curl -sfLO https://cdn.kernel.org/pub/linux/kernel/v6.x/sha256sums.asc && \
  curl -sfLO https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.18.55.tar.sign && \
  curl -sfLO https://busybox.net/downloads/busybox-1.37.0.tar.bz2.sha256; \
  grep " linux-6.18.55.tar.xz" sha256sums.asc; cat busybox-1.37.0.tar.bz2.sha256; \
  sha256sum /var/lab/dl/linux-6.18.55.tar.xz /var/lab/dl/busybox-1.37.0.tar.bz2'
```

```text
f410638061a165c12f42ab871d2f3fcd525515359b5faeee80969cff84524df9  linux-6.18.55.tar.xz
3311dff32e746499f4df0d5df04d7eb396382d7e108bb9250e7b519b837043a4  busybox-1.37.0.tar.bz2
f410638061a165c12f42ab871d2f3fcd525515359b5faeee80969cff84524df9  /var/lab/dl/linux-6.18.55.tar.xz
3311dff32e746499f4df0d5df04d7eb396382d7e108bb9250e7b519b837043a4  /var/lab/dl/busybox-1.37.0.tar.bz2
```

### 3.4 GPG 서명 — 격리된 keyring으로

kernel.org의 `.tar.sign`은 **압축을 푼 `.tar`** 에 대한 서명이다(xz 압축 결과는 xz 버전마다 바이트가 달라질 수 있어서). 그래서 `xz -cd`로 풀면서 파이프로 검증한다. 공용 VM의 `~/.gnupg`를 건드리지 않도록 `GNUPGHOME`을 내 디렉터리로 돌린다.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/S1/verify && export GNUPGHOME=$PWD/gnupg && mkdir -p -m700 $GNUPGHOME && \
  gpg --batch --auto-key-locate clear,wkd --locate-keys torvalds@kernel.org gregkh@kernel.org 2>&1 | tail -4; \
  xz -cd /var/lab/dl/linux-6.18.55.tar.xz | gpg --batch --verify linux-6.18.55.tar.sign - 2>&1 | tail -4; \
  gpg --batch --verify sha256sums.asc 2>&1 | tail -2'
```

```text
      ABAF11C65A2970B130ABE3C479BE3E4300411886
uid           [ unknown] Linus Torvalds <torvalds@kernel.org>
sub   rsa2048 2011-09-20 [E]

gpg: Good signature from "Greg Kroah-Hartman <gregkh@kernel.org>" [unknown]
gpg: WARNING: This key is not certified with a trusted signature!
gpg:          There is no indication that the signature belongs to the owner.
Primary key fingerprint: 647F 2865 4894 E3BD 4571  99BE 38DB BDC8 6092 693E
gpg:                using RSA key B8868C80BA62A1FFFAF5FDA9632D3A06589DA6B1
gpg: Can't check signature: No public key
```

출력에서 볼 것:

- `Good signature from "Greg Kroah-Hartman"` — 안정판(6.18.y)은 stable 메인테이너가 서명한다. `WARNING: not certified`는 "이 키를 내가 직접 신뢰한다고 표시한 적이 없다"는 뜻이지 서명이 틀렸다는 뜻이 아니다. 지문(`647F 2865 …`)을 kernel.org 서명 안내 페이지와 대조하는 것이 마지막 단계다.
- `sha256sums.asc`는 kernel.org **자동 서명 키**(`B886…`)로 서명돼 있어서 그 키를 받지 않으면 `No public key`가 나온다. 해시 목록 자체의 출처 검증까지 하려면 그 키도 WKD로 받는다.
- BusyBox는 `.sig`(DSA, keyid `47B70C55ACC9965B`)를 제공한다. U-Boot·TF-A 태그 아카이브에는 서명이 없다 — 이 경우 **git 태그 서명**(`git tag -v`)이 대안이다.

체크포인트: **`sha256sum -c`가 4개 모두 `OK`, 커널 tarball에 `Good signature`.**

---

## 4. Step 2 — 소스 풀기

```make
$(STAMP)/src-%: $(STAMP)/verified
	@mkdir -p $(SRC)/$*; tar -xf $(DL)/$(TAR_$*) -C $(SRC)/$* --strip-components=1 && touch $@
TAR_linux := $(K_TAR)
TAR_u-boot := $(UB_TAR)
TAR_tfa := $(TF_TAR)
TAR_busybox := $(BB_TAR)
```

`--strip-components=1`은 tarball마다 다른 최상위 이름(`arm-trusted-firmware-2.15.0/`, `u-boot-2026.10/`)을 `src/tfa`, `src/u-boot`처럼 버전 없는 이름으로 맞춘다. 그래야 Makefile의 나머지가 버전을 몰라도 된다.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp && for s in u-boot tfa busybox linux; do \
  /usr/bin/time -f "$s extract: %e s" make -s -C /Users/donh/.elinux_scratch/S1/qemu-lab B=/var/tmp/S1 /var/tmp/S1/.stamp/src-$s; done; \
  du -sh /var/tmp/S1/src/*'
```

```text
u-boot extract: 1.26 s
tfa extract: 0.22 s
busybox extract: 0.27 s
linux extract: 5.96 s
20M	/var/tmp/S1/src/busybox
1.7G	/var/tmp/S1/src/linux
61M	/var/tmp/S1/src/tfa
360M	/var/tmp/S1/src/u-boot
```

체크포인트: **`src/` 아래 네 디렉터리, 커널 1.7 GB.** 디스크가 빠듯하면 커널만 `/var/lab/src/linux-6.18.55`를 `O=`로 빌드해도 된다(소스 트리를 읽기만 하므로 허용, NOTES_SPEC QEMU 랩 절).

---

## 5. Step 3 — U-Boot (`qemu_arm64_defconfig`)

U-Boot은 이 랩에서 두 번 쓰인다: (1) `-bios u-boot.bin`으로 단독 실행(EL1/EL2, 비보안), (2) TF-A FIP 안의 **BL33**. 같은 바이너리가 두 역할을 다 한다 — `qemu_arm64_defconfig`가 `CONFIG_POSITION_INDEPENDENT=y`, `CONFIG_TEXT_BASE=0`이라서 어디에 올라가도 스스로 재배치하기 때문이다(C4에서 본 relocation).

```make
$(OUT)/u-boot.bin: $(STAMP)/src-u-boot
	$(MAKE) -C $(SRC)/u-boot O=$(BLD)/u-boot qemu_arm64_defconfig
	$(MAKE) -C $(SRC)/u-boot O=$(BLD)/u-boot -j$(JOBS)
	@mkdir -p $(OUT); cp $(BLD)/u-boot/u-boot.bin $@
uboot: $(OUT)/u-boot.bin
```

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp && /usr/bin/time -f "u-boot build: %e s wall, %U s user" \
  make -C /Users/donh/.elinux_scratch/S1/qemu-lab B=/var/tmp/S1 uboot > /var/tmp/S1/uboot.log 2>&1; tail -15 /var/tmp/S1/uboot.log; \
  ls -la /var/tmp/S1/out'
limactl shell elinux -- bash -lc 'grep -E "^CONFIG_(BOOTCOMMAND|TEXT_BASE|POSITION_INDEPENDENT)=" /var/lab/build/u-boot/.config'
```

```text
...
  RELOC   u-boot-nodtb.bin
  SYM     u-boot.sym
  COPY    u-boot.bin
  DTC     arch/arm/dts/qemu-arm.dtb
  DTC     arch/arm/dts/qemu-arm64.dtb
  COPY    dts/dt.dtb
  OFCHK   .config
make[1]: Leaving directory '/var/tmp/S1/src/u-boot'
make: Leaving directory '/Users/donh/.elinux_scratch/S1/qemu-lab'
u-boot build: 27.45 s wall, 43.26 s user
...
-rw-rw-r-- 1 donh donh 1504208 Oct 18 16:30 u-boot.bin
CONFIG_POSITION_INDEPENDENT=y
CONFIG_TEXT_BASE=0x00000000
CONFIG_BOOTCOMMAND="bootflow scan -lb"
```

출력에서 볼 것:

- 27초, 1,504,208 바이트 — 랩의 `/var/lab/out/u-boot.bin`과 **크기가 바이트 단위로 같다**. 그런데 `cmp`하면 다르다(아래). 빌드 날짜 문자열 `U-Boot 2026.10 (Oct 18 2026 - 16:29:47 -0700)`이 바이너리에 박히기 때문이다. 완전히 같은 바이너리(reproducible build)를 원하면 `SOURCE_DATE_EPOCH`를 고정한다(B5 §7).
- 기본 부트 명령이 `bootflow scan -lb` — 모든 boot device를 훑어서 처음 찾은 것으로 부팅한다. §9에서 이게 QEMU fw_cfg에서 커널을 찾는 걸 본다.

```sh
limactl shell elinux -- bash -lc 'cmp /var/tmp/S1/out/u-boot.bin /var/lab/out/u-boot.bin && echo uboot-identical'
```

```text
/var/tmp/S1/out/u-boot.bin /var/lab/out/u-boot.bin differ: byte 1249469, line 5556
```

체크포인트: **`out/u-boot.bin` 약 1.5 MB.**

---

## 6. Step 4 — TF-A (`PLAT=qemu`)와 `flash.bin`

TF-A의 단계(BL1 → BL2 → BL31 → BL33)와 FIP는 C5 §2에서 배웠다. 여기서는 만드는 법만 본다.

```make
TFA_REL := $(BLD)/tfa/qemu/release
$(OUT)/flash.bin: $(OUT)/u-boot.bin $(STAMP)/src-tfa
	$(MAKE) -C $(SRC)/tfa PLAT=qemu BUILD_BASE=$(BLD)/tfa BL33=$(OUT)/u-boot.bin -j$(JOBS) all fip
	dd if=/dev/zero of=$@.tmp bs=1M count=64 status=none
	dd if=$(TFA_REL)/bl1.bin of=$@.tmp bs=4096 conv=notrunc status=none
	dd if=$(TFA_REL)/fip.bin of=$@.tmp seek=64 bs=4096 conv=notrunc status=none
	mv $@.tmp $@
tfa: $(OUT)/flash.bin
```

결정마다 이유가 있다.

| 결정 | 이유 |
|---|---|
| `BL33=$(OUT)/u-boot.bin` | FIP에 U-Boot를 넣는다. 의존성에 `u-boot.bin`이 있어서 U-Boot가 바뀌면 FIP도 다시 만든다 |
| `BUILD_BASE=` | TF-A는 `O=`가 아니라 이 변수로 출력 위치를 바꾼다 (`build/<plat>/<release or debug>/`) |
| `bl1.bin`을 offset 0 | QEMU `virt`에서 `-bios` 파일은 flash0(주소 0x0)에 매핑되고, secure=on이면 CPU가 EL3에서 0x0부터 실행한다 = Boot ROM 자리 |
| `fip.bin`을 `seek=64` × 4 KiB = 0x40000 | TF-A qemu 플랫폼의 BL2가 FIP를 flash의 256 KiB 위치에서 찾는다 (TF-A qemu 플랫폼 문서의 `dd` 레시피와 같다) |
| 64 MiB로 패딩 | QEMU flash 장치는 64 MiB 크기 이미지를 기대한다 |
| `.tmp` 후 `mv` | dd 중간 실패 시 반쪽 flash.bin이 남지 않게 |

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp && /usr/bin/time -f "tfa build: %e s wall, %U s user" \
  make -C /Users/donh/.elinux_scratch/S1/qemu-lab B=/var/tmp/S1 tfa > /var/tmp/S1/tfa.log 2>&1; \
  grep -E "Built|error|Error|tfa build|dd " /var/tmp/S1/tfa.log | tail -15; ls -la /var/tmp/S1/build/tfa/qemu/release/*.bin'
limactl shell elinux -- bash -lc '/var/tmp/S1/build/tfa/qemu/release/tools/fiptool/fiptool info /var/tmp/S1/build/tfa/qemu/release/fip.bin'
```

```text
  CC      lib/libfdt/fdt_strerror.c
Built /var/tmp/S1/build/tfa/qemu/release/tools/fiptool/fiptool successfully
Built /var/tmp/S1/build/tfa/qemu/release/bl1.bin successfully
Built /var/tmp/S1/build/tfa/qemu/release/bl2.bin successfully
Built /var/tmp/S1/build/tfa/qemu/release/bl31.bin successfully
Built /var/tmp/S1/build/tfa/qemu/release/fip.bin successfully
dd if=/dev/zero of=/var/tmp/S1/out/flash.bin.tmp bs=1M count=64 status=none
dd if=/var/tmp/S1/build/tfa/qemu/release/bl1.bin of=/var/tmp/S1/out/flash.bin.tmp bs=4096 conv=notrunc status=none
dd if=/var/tmp/S1/build/tfa/qemu/release/fip.bin of=/var/tmp/S1/out/flash.bin.tmp seek=64 bs=4096 conv=notrunc status=none
tfa build: 2.73 s wall, 3.96 s user
-rwxrwxr-x 1 donh donh   22832 Oct 18 16:30 /var/tmp/S1/build/tfa/qemu/release/bl1.bin
-rwxrwxr-x 1 donh donh   25072 Oct 18 16:30 /var/tmp/S1/build/tfa/qemu/release/bl2.bin
-rwxrwxr-x 1 donh donh   49255 Oct 18 16:30 /var/tmp/S1/build/tfa/qemu/release/bl31.bin
-rw-rw-r-- 1 donh donh 1578711 Oct 18 16:30 /var/tmp/S1/build/tfa/qemu/release/fip.bin
Trusted Boot Firmware BL2: offset=0xB0, size=0x61F0, cmdline="--tb-fw"
EL3 Runtime Firmware BL31: offset=0x62A0, size=0xC067, cmdline="--soc-fw"
Non-Trusted Firmware BL33: offset=0x12307, size=0x16F3D0, cmdline="--nt-fw"
```

출력에서 볼 것: 2.7초. BL1 22 KB, BL31 49 KB — EL3 펌웨어는 SSD FW의 ROM 코드처럼 작다. FIP의 BL33 크기 `0x16F3D0` = 1,504,208 = `u-boot.bin` 크기 그대로. FIP는 "UUID로 이름 붙은 이미지들의 목차 + 본문"이다 (Don에게는 SSD 펌웨어 패키지의 섹션 헤더와 같은 것).

체크포인트: **`out/flash.bin` 정확히 67,108,864 바이트, `fiptool info`에 BL2/BL31/BL33 세 줄.**

---

## 7. Step 5 — 커널: `tinyconfig` + virt fragment

### 7.1 무엇을 고르나

| 출발점 | `=y` 개수 (실측) | -j2 빌드 | 쓸모 |
|---|---|---|---|
| `defconfig` (arm64) | 3295 `=y` + 1273 `=m` | 수십 분 (랩 `Image`가 이것 기반, 42.7 MB) | 실제 보드 수백 종 지원 |
| `tinyconfig` + fragment | **718 `=y`, 0 `=m`** | **294 초** | QEMU `virt`만 |

같은 소스에서 `make defconfig`(설정만, 빌드 안 함)로 센 값이다.

```sh
limactl shell elinux -- bash -lc 'mkdir -p /var/tmp/S1/build/defcfg && make -s -C /var/tmp/S1/src/linux O=/var/tmp/S1/build/defcfg defconfig >/dev/null && \
  echo "defconfig: $(grep -c "=y" /var/tmp/S1/build/defcfg/.config) =y, $(grep -c "=m" /var/tmp/S1/build/defcfg/.config) =m"; \
  echo "s1: $(grep -c "=y" /var/tmp/S1/build/linux/.config) =y, $(grep -c "=m" /var/tmp/S1/build/linux/.config) =m"'
```

```text
defconfig: 3295 =y, 1273 =m
s1: 718 =y, 0 =m
```

E2 §5에서 tinyconfig로 최소 커널을 손으로 만들어 봤다. S1은 그걸 **fragment 파일 + 자동 검사**로 재현 가능하게 만든다.

### 7.2 fragment — QEMU `virt`가 실제로 쓰는 하드웨어만

QEMU `virt`의 DT(`/var/lab/out/virt.dts`)를 보면 장치는 몇 개 안 된다: GIC, arch timer, PL011 UART(0x09000000), PL031 RTC, PCIe ECAM, virtio-mmio 32개, fw_cfg, flash. fragment는 이 목록을 그대로 옮긴 것이다.

```text
CONFIG_LOCALVERSION="-s1"
CONFIG_SMP=y
CONFIG_PRINTK=y
CONFIG_PRINTK_TIME=y
CONFIG_KALLSYMS=y
CONFIG_IKCONFIG=y
CONFIG_IKCONFIG_PROC=y
CONFIG_MULTIUSER=y
CONFIG_FUTEX=y
CONFIG_POSIX_TIMERS=y
CONFIG_BLK_DEV_INITRD=y
CONFIG_RD_GZIP=y
CONFIG_BINFMT_ELF=y
CONFIG_BINFMT_SCRIPT=y
CONFIG_TTY=y
CONFIG_SERIAL_AMBA_PL011=y
CONFIG_SERIAL_AMBA_PL011_CONSOLE=y
CONFIG_RTC_CLASS=y
CONFIG_RTC_DRV_PL031=y
CONFIG_PROC_FS=y
CONFIG_SYSFS=y
CONFIG_SHMEM=y
CONFIG_TMPFS=y
CONFIG_DEVTMPFS=y
CONFIG_DEVTMPFS_MOUNT=y
CONFIG_BLOCK=y
CONFIG_EXT4_FS=y
CONFIG_PCI=y
CONFIG_PCI_HOST_GENERIC=y
CONFIG_VIRTIO_MENU=y
CONFIG_VIRTIO_MMIO=y
CONFIG_VIRTIO_PCI=y
CONFIG_VIRTIO_BLK=y
CONFIG_NET=y
CONFIG_INET=y
CONFIG_UNIX=y
CONFIG_PACKET=y
CONFIG_NETDEVICES=y
CONFIG_NET_CORE=y
CONFIG_VIRTIO_NET=y
CONFIG_NET_9P=y
CONFIG_NET_9P_VIRTIO=y
CONFIG_9P_FS=y
CONFIG_DEBUG_KERNEL=y
CONFIG_DEBUG_INFO_DWARF5=y
CONFIG_GDB_SCRIPTS=y
```

옵션별 이유(실제 파일에는 주석으로 그룹을 나눴다).

| 그룹 | 옵션 | 없으면 |
|---|---|---|
| 콘솔 | `SERIAL_AMBA_PL011(_CONSOLE)`, `TTY`, `PRINTK` | 아무 출력 없이 멈춘 것처럼 보인다 (C6 §5) |
| initramfs | `BLK_DEV_INITRD`, `RD_GZIP` | "Unable to mount root fs" |
| 유저 실행 | `BINFMT_ELF`, `BINFMT_SCRIPT`, `MULTIUSER`, `FUTEX`, `POSIX_TIMERS` | `/init`(셸 스크립트) 실행 실패, BusyBox 일부 applet 실패 |
| 파일시스템 | `PROC_FS`, `SYSFS`, `DEVTMPFS(_MOUNT)`, `SHMEM`+`TMPFS`, `EXT4_FS` | `/proc` 없음, `/dev` 비어 있음, ext4 rootfs 불가 |
| 버스 | `PCI`, `PCI_HOST_GENERIC`, `VIRTIO_MMIO`, `VIRTIO_PCI` | QEMU 기본 NIC(virtio-net-pci), `-device virtio-*-device` 인식 안 됨 |
| 장치 | `VIRTIO_BLK`, `VIRTIO_NET`, `RTC_DRV_PL031` | 디스크·네트워크·시계 없음 |
| 네트워크 | `NET`, `INET`, `UNIX`, `PACKET` | `udhcpc`가 raw socket(`AF_PACKET`)을 못 연다 |
| 호스트 공유 | `NET_9P(_VIRTIO)`, `9P_FS` | `-virtfs` 공유 마운트 불가 |
| 디버깅 | `DEBUG_INFO_DWARF5`, `GDB_SCRIPTS`, `KALLSYMS`, `IKCONFIG_PROC` | gdb에 소스 줄 없음, `lx-*` 명령 없음, oops에 함수 이름 없음 |

`ARM_GIC_V3`, `ARM_ARCH_TIMER`, `ARM_PSCI_FW`, `COMMON_CLK`는 넣지 않았다 — arm64 아키텍처 Kconfig가 `select`로 강제로 켠다. 결과 `.config`로 확인했다.

```text
CONFIG_CC_OPTIMIZE_FOR_SIZE=y
CONFIG_EXPERT=y
# CONFIG_MODULES is not set
CONFIG_SLUB_TINY=y
CONFIG_PCI_HOST_GENERIC=y
CONFIG_ARM_PSCI_FW=y
CONFIG_COMMON_CLK=y
CONFIG_ARM_ARCH_TIMER=y
CONFIG_ARM_GIC_V3=y
```

tinyconfig가 남긴 성격도 보인다: `CC_OPTIMIZE_FOR_SIZE`(-Os), `SLUB_TINY`, `MODULES` 꺼짐. 모듈 실습(S2)은 계속 랩의 `linux-build`를 쓰고, 이 커널로 하고 싶으면 fragment에 `CONFIG_MODULES=y`를 더한다(§15 확장 과제).

### 7.3 Makefile — merge 후 검사

```make
KO := $(BLD)/linux
$(KO)/.config: $(STAMP)/src-linux $(TOP)/config/kernel-virt.fragment
	$(MAKE) -C $(SRC)/linux O=$(KO) tinyconfig
	cd $(KO) && $(SRC)/linux/scripts/kconfig/merge_config.sh -m -O $(KO) $(KO)/.config $(TOP)/config/kernel-virt.fragment
	$(MAKE) -C $(SRC)/linux O=$(KO) olddefconfig
	@$(TOP)/scripts/check-fragment.sh $(TOP)/config/kernel-virt.fragment $@
$(OUT)/Image: $(KO)/.config
	$(MAKE) -C $(SRC)/linux O=$(KO) -j$(JOBS) Image scripts_gdb
	@mkdir -p $(OUT); cp $(KO)/arch/arm64/boot/Image $@; ln -sf $(KO)/vmlinux $(OUT)/vmlinux
kernel: $(OUT)/Image
```

`merge_config.sh -m`는 "병합만 하고 olddefconfig는 하지 마라"는 뜻이다. 병합 직후의 `.config`는 의존성이 안 맞을 수 있어서 `olddefconfig`가 Kconfig 규칙대로 정리한다 — 그리고 **이때 의존성이 안 맞는 옵션은 조용히 꺼진다.** 그걸 잡는 게 `check-fragment.sh`다.

```sh
#!/bin/bash
# fragment 의 CONFIG_X=값 이 최종 .config 에 그대로 살아남았는지 검사
frag=$1; cfg=$2; bad=0
while IFS= read -r l; do
  case $l in CONFIG_*=*) grep -qxF "$l" "$cfg" || { echo "DROPPED: $l -> $(grep -E "^(# )?${l%%=*}[= ]" "$cfg" || echo absent)"; bad=1; };; esac
done < "$frag"
[ $bad = 0 ] && echo "fragment OK: $(grep -c '^CONFIG_' "$frag") options applied, $(grep -c '=y' "$cfg") =y total"
exit $bad
```

### 7.4 실제로 걸린 함정 — `CONFIG_TMPFS`가 사라졌다

첫 fragment에는 `CONFIG_SHMEM=y`가 없었다. 결과:

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp && /usr/bin/time -f "kconfig: %e s" make -C /Users/donh/.elinux_scratch/S1/qemu-lab B=/var/tmp/S1 \
  /var/tmp/S1/build/linux/.config > /var/tmp/S1/kconfig.log 2>&1; tail -5 /var/tmp/S1/kconfig.log'
```

```text
DROPPED: CONFIG_TMPFS=y -> absent
make: *** [Makefile:75: /var/tmp/S1/build/linux/.config] Error 1
make: Leaving directory '/Users/donh/.elinux_scratch/S1/qemu-lab'
Command exited with non-zero status 2
kconfig: 6.99 s
```

원인: `fs/Kconfig`의 `config TMPFS`는 `depends on SHMEM`이고, `init/Kconfig`의 `SHMEM`은 `bool "Use full shmem filesystem" if EXPERT` + `default y`다. tinyconfig는 `allnoconfig`에 `kernel/configs/tiny.config`(+ `tiny-base.config`의 `CONFIG_EXPERT=y`)를 얹은 것이라, EXPERT 때문에 보이게 된 `SHMEM`까지 n으로 내리므로 `SHMEM`이 꺼지고, 그 아래 `TMPFS`는 아예 메뉴에서 사라져 `.config`에 줄조차 없다(`absent`). E2 §2의 "`depends on`은 조용히 진다" 규칙의 실물이다. 고친 뒤:

```text
Value of CONFIG_SHMEM is redefined by fragment /Users/donh/.elinux_scratch/S1/qemu-lab/config/kernel-virt.fragment:
Previous value: # CONFIG_SHMEM is not set
New value: CONFIG_SHMEM=y
...
fragment OK: 46 options applied, 718 =y total
```

검사 스크립트가 없었다면 이 커널은 빌드도 부팅도 잘 됐을 것이고, 나중에 `mount -t tmpfs`에서야 "unknown filesystem type"으로 터졌을 것이다. **Kconfig 결과를 검증하는 단계가 빌드에 들어 있어야 한다** — 이게 면접에서 말할 포인트다.

### 7.5 빌드 — 시간과 크기

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp && /usr/bin/time -f "kernel build: %e s wall, %U s user, %S s sys, maxrss %M KB" \
  make -C /Users/donh/.elinux_scratch/S1/qemu-lab B=/var/tmp/S1 kernel > /var/tmp/S1/kernel.log 2>&1'
limactl shell elinux -- bash -lc 'cd /var/tmp/S1; tail -1 kernel.log; grep -c " CC " kernel.log; ls -la out/Image /var/lab/out/Image build/linux/vmlinux; \
  size build/linux/vmlinux; du -sh build/linux'
```

```text
kernel build: 293.70 s wall, 444.95 s user, 68.53 s sys, maxrss 319592 KB
985
-rwxrwxr-x 1 donh donh  6197256 Oct 18 16:35 out/Image
-rw-rw-r-- 1 donh donh 42740224 Oct 10 10:05 /var/lab/out/Image
-rwxrwxr-x 1 donh donh 97870344 Oct 18 16:35 build/linux/vmlinux
   text	   data	    bss	    dec	    hex	filename
4149264	1840816	 387592	6377672	 6150c8	build/linux/vmlinux
791M	/var/tmp/S1/build/linux
```

출력에서 볼 것:

- **294초(4분 54초), C 파일 985개.** 다른 작성자 빌드와 CPU를 나눠 쓰는 중이라(load average 7 근처) 혼자 쓰면 더 빠르다. user 445 s / wall 294 s ≈ 1.5 — `-j2`가 거의 꽉 찼다.
- `Image` 6.2 MB vs 랩 defconfig `Image` 42.7 MB — **약 7분의 1.**
- `vmlinux` 98 MB인데 `size`로 본 실제 코드+데이터는 6.4 MB. 나머지 90 MB는 DWARF5 디버그 정보다. gdb에는 `vmlinux`, 부팅에는 `Image`를 쓰는 이유(E2 §10).

체크포인트: **`out/Image` 약 6 MB, `out/vmlinux` 심볼릭 링크.**

---

## 8. Step 6 — BusyBox static과 initramfs

### 8.1 Makefile

```make
BBO := $(BLD)/busybox
$(BBO)/busybox: $(STAMP)/src-busybox
	@mkdir -p $(BBO)   # BusyBox 의 O= 는 커널과 달리 디렉터리를 만들어 주지 않는다
	$(MAKE) -C $(SRC)/busybox O=$(BBO) defconfig
	sed -i -e 's/^# CONFIG_STATIC is not set/CONFIG_STATIC=y/' -e 's/^CONFIG_TC=y/# CONFIG_TC is not set/' \
	  -e 's/^CONFIG_SHA1_HWACCEL=y/# CONFIG_SHA1_HWACCEL is not set/' \
	  -e 's/^CONFIG_SHA256_HWACCEL=y/# CONFIG_SHA256_HWACCEL is not set/' $(BBO)/.config
	$(MAKE) -C $(BBO) oldconfig </dev/null >/dev/null
	$(MAKE) -C $(BBO) -j$(JOBS)
busybox: $(BBO)/busybox
$(STAMP)/rootfs: $(BBO)/busybox $(TOP)/rootfs/init $(TOP)/rootfs/udhcpc.script
	rm -rf $(B)/rootfs; $(MAKE) -C $(BBO) CONFIG_PREFIX=$(B)/rootfs install >/dev/null
	cd $(B)/rootfs && mkdir -p proc sys dev tmp etc mnt root usr/share/udhcpc boot
	install -m755 $(TOP)/rootfs/init $(B)/rootfs/init
	install -m755 $(TOP)/rootfs/udhcpc.script $(B)/rootfs/usr/share/udhcpc/default.script
	@mkdir -p $(STAMP); touch $@
$(OUT)/initramfs.cpio.gz: $(STAMP)/rootfs
	@mkdir -p $(OUT); cd $(B)/rootfs && find . | LC_ALL=C sort | cpio -o -H newc -R 0:0 --quiet | gzip -9 > $@
```

static 링크를 고른 이유는 K2 §3에 있다: initramfs에 glibc 공유 라이브러리를 넣을 필요가 없어서 rootfs가 "실행 파일 하나 + 심볼릭 링크 수백 개"로 끝난다. `cpio -R 0:0`은 내 uid(501) 소유 파일을 root 소유로 기록한다 — root 권한 없이 rootfs를 만드는 요령. `LC_ALL=C sort`는 파일 순서를 고정해 같은 입력이면 같은 cpio가 나오게 한다.

### 8.2 실제로 걸린 함정 세 개

첫 시도 — 출력 디렉터리:

```text
/bin/sh: 1: cd: can't cd to /var/tmp/S1/build/busybox
Makefile:106: *** output directory "/var/tmp/S1/build/busybox" does not exist.  Stop.
```

커널과 U-Boot는 `O=` 디렉터리를 만들어 주지만 BusyBox는 안 만든다 → `mkdir -p` 추가.

두 번째 — SHA1 하드웨어 가속:

```text
/var/tmp/S1/src/busybox/libbb/hash_md5_sha.c:1316:35: error: ‘sha1_process_block64_shaNI’ undeclared (first use in this function); did you mean ‘sha1_process_block64’?
make[4]: *** [/var/tmp/S1/src/busybox/scripts/Makefile.build:198: libbb/hash_md5_sha.o] Error 1
```

BusyBox 1.37 `defconfig`는 `SHA1_HWACCEL=y`인데, 그 구현(`shaNI`)은 x86 전용이다. aarch64에서는 빌드가 깨진다 → 끈다. 랩의 `/var/lab/build/busybox/.config`도 꺼져 있었다(역으로 읽은 결정 중 하나).

세 번째 — `tc` applet. 랩 config에서 꺼져 있길래 이유를 확인하려고 일부러 켜서 그 파일 하나만 컴파일해 봤다.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/S1/build/busybox && cp .config /var/tmp/S1/bb.config.bak && \
  sed -i "s/^# CONFIG_TC is not set/CONFIG_TC=y/" .config && make oldconfig </dev/null >/dev/null 2>&1; \
  make networking/tc.o 2>&1 | grep -E "error" | head -3; cp /var/tmp/S1/bb.config.bak .config; make oldconfig </dev/null >/dev/null 2>&1'
```

```text
/var/tmp/S1/src/busybox/networking/tc.c:236:27: error: ‘TCA_CBQ_MAX’ undeclared (first use in this function); did you mean ‘TCA_CBS_MAX’?
/var/tmp/S1/src/busybox/networking/tc.c:249:16: error: ‘TCA_CBQ_RATE’ undeclared (first use in this function); did you mean ‘TCA_TBF_RATE64’?
/var/tmp/S1/src/busybox/networking/tc.c:255:16: error: ‘TCA_CBQ_LSSOPT’ undeclared (first use in this function)
```

호스트의 커널 UAPI 헤더(VM은 7.0)에서 CBQ qdisc 정의가 빠졌기 때문이다. **유저 공간 프로그램은 빌드 호스트의 커널 헤더에 묶인다** — 크로스 툴체인이 자기 sysroot에 커널 헤더를 따로 들고 다니는 이유(B1 §9).

### 8.3 성공한 빌드

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp && /usr/bin/time -f "busybox build: %e s wall, %U s user" \
  make -C /Users/donh/.elinux_scratch/S1/qemu-lab B=/var/tmp/S1 busybox > /var/tmp/S1/bb.log 2>&1; tail -4 /var/tmp/S1/bb.log; \
  file /var/tmp/S1/build/busybox/busybox; ls -la /var/tmp/S1/build/busybox/busybox'
```

```text
 Library resolv is needed, can't exclude it (yet)
Final link with: m resolv
make: Leaving directory '/Users/donh/.elinux_scratch/S1/qemu-lab'
busybox build: 43.13 s wall, 56.60 s user
/var/tmp/S1/build/busybox/busybox: ELF 64-bit LSB executable, ARM aarch64, version 1 (GNU/Linux), statically linked, BuildID[sha1]=2a286d68bbc57c114d015a56901a0809947438aa, for GNU/Linux 3.7.0, stripped
-rwxrwxr-x 1 donh donh 2242744 Oct 18 16:37 /var/tmp/S1/build/busybox/busybox
```

빌드 로그에는 glibc static 링크 경고(`getaddrinfo`류 함수는 static이어도 런타임에 NSS 공유 라이브러리를 찾는다)와 `-Wunused-result` 경고가 섞여 나온다. BusyBox 업스트림 코드의 경고라 고치지 않는다 — 이 노트의 "경고 0개" 규칙은 내가 쓴 C 코드에 대한 것이다.

### 8.4 `/init` — 랩 `/init`의 확장판

랩의 `/var/lab/build/rootfs/init`(9줄)를 출발점으로, 네트워크와 9p를 붙였다.

```sh
#!/bin/sh
# S1 qemu-lab /init (initramfs 와 ext4 rootfs 공용)
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev 2>/dev/null
echo "s1-lab: $(uname -r) up $(cut -d' ' -f1 /proc/uptime)s root=$(awk '$2=="/"{print $1}' /proc/mounts)"
# 네트워크: eth0 이 있으면 DHCP (QEMU user net = 10.0.2.0/24)
if [ -e /sys/class/net/eth0 ]; then
  ip link set lo up; ip link set eth0 up
  udhcpc -i eth0 -q -n -t 5 -s /usr/share/udhcpc/default.script >/dev/null 2>&1 \
    && echo "s1-lab: eth0 $(ip -4 -o addr show eth0 | awk '{print $4}')"
fi
# 9p 호스트 공유: -virtfs ...,mount_tag=host0
if cat /sys/bus/virtio/drivers/9pnet_virtio/virtio*/mount_tag 2>/dev/null | grep -q host0 && \
   mkdir -p /mnt/host && mount -t 9p -o trans=virtio,version=9p2000.L host0 /mnt/host; then
  echo "s1-lab: 9p host0 -> /mnt/host"
fi
# LAB_CMD=cmd~args : 명령 하나 실행 후 poweroff (공백 대신 ~)
for a in $(cat /proc/cmdline); do case $a in LAB_CMD=*) C="${a#LAB_CMD=}";; esac; done
if [ -n "$C" ]; then sh -c "$(echo "$C" | tr '~' ' ')"; sync; poweroff -f; fi
exec setsid cttyhack /bin/sh
```

설계 메모:

- 9p는 **장치가 있을 때만** 마운트한다. 처음에는 무조건 `mount -t 9p`를 시도했더니 `-virtfs`가 없는 부팅마다 `9pnet_virtio: no channels available for device host0` 커널 메시지가 찍혔다. sysfs의 `mount_tag` 속성으로 먼저 확인하도록 바꿨다.
- `LAB_CMD`는 커널이 모르는 인자라서 `Unknown kernel command line parameters "LAB_CMD=..." will be passed to user space`로 넘어온다(C6 §6). `~`를 공백으로 바꾸는 이유: 커널 커맨드라인은 공백으로 인자를 자르기 때문.
- `setsid cttyhack sh`는 셸에 controlling tty를 줘서 Ctrl-C가 먹게 한다(K2 §8).
- `sync` 후 `poweroff -f`: ext4 rootfs로 부팅했을 때 쓴 내용이 디스크에 남게 한다(§11).

체크포인트: **`out/initramfs.cpio.gz` 약 1.2 MB.**

---

## 9. Step 7 — 부팅 ①: 직접 `-kernel`

가장 짧은 경로다. QEMU가 부트로더 역할까지 한다: `Image`와 initrd, 자기가 만든 DTB를 RAM(0x40000000부터)에 직접 놓고, 작은 부트 스텁(아래 gdb 출력의 0x40000000 명령 3개)으로 `x0=DTB 주소`를 넣고 커널로 점프한다(C1 §5).

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/S1 && /usr/bin/time -f "wall %e s" timeout 120 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -kernel out/Image -initrd out/initramfs.cpio.gz \
  -append "console=ttyAMA0 earlycon=pl011,0x9000000 LAB_CMD=cat~/proc/version;cat~/proc/device-tree/psci/method;echo;free~-m;ls~/sys/bus/virtio/devices" > boot1.log 2>&1; cat boot1.log'
```

```text
[    0.000000] Booting Linux on physical CPU 0x0000000000 [0x411fd070]
[    0.000000] Linux version 6.18.55-s1 (donh@lima-elinux) (gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0, GNU ld (GNU Binutils for Ubuntu) 2.46) #1 SMP Sun Oct 18 16:35:53 PDT 2026
[    0.000000] Machine model: linux,dummy-virt
[    0.000000] earlycon: pl11 at MMIO 0x0000000009000000 (options '')
[    0.000000] psci: probing for conduit method from DT.
[    0.000000] psci: PSCIv1.1 detected in firmware.
[    0.000000] psci: SMC Calling Convention v1.0
...
[    0.037653] CPU: All CPU(s) started at EL1
[    0.061744] Memory: 504048K/524288K available (3904K kernel code, 676K rwdata, 1060K rodata, 320K init, 382K bss, 19420K reserved, 0K cma-reserved)
[    0.136978] 9000000.pl011: ttyAMA0 at MMIO 0x9000000 (irq = 13, base_baud = 0) is a PL011 rev1
...
[    0.200652] Unpacking initramfs...
[    0.234799] pci 0000:00:01.0: [1af4:1000] type 00 class 0x020000 conventional PCI endpoint
[    0.383879] rtc-pl031 9010000.pl031: registered as rtc0
[    0.439713] Freeing unused kernel memory: 320K
[    0.440470] Run /init as init process
s1-lab: 6.18.55-s1 up 0.52s root=rootfs
s1-lab: eth0 10.0.2.15/24
[    0.747976] 9pnet_virtio: no channels available for device host0
Linux version 6.18.55-s1 (donh@lima-elinux) (gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0, GNU ld (GNU Binutils for Ubuntu) 2.46) #1 SMP Sun Oct 18 16:35:53 PDT 2026
hvc 
              total        used        free      shared  buff/cache   available
Mem:            495           8         485           2           2         480
Swap:             0           0           0
virtio0
[    0.807800] reboot: Power down
wall 0.96 s
```

(144줄 중 핵심만 남기고 `...`로 줄였다. `ls` 색상 제어 문자는 지웠다.)

출력에서 볼 것:

- **QEMU 시작부터 전원 꺼짐까지 0.96초.** `/init`까지 0.44초. 이 측정은 9p 마운트 조건을 고치기 전 `/init`으로 했다 — `9pnet_virtio: no channels available` 줄이 그 흔적이고, §8.4의 수정으로 사라졌다.
- `CPU: All CPU(s) started at EL1` — 직접 부팅에서는 EL3·EL2 펌웨어가 없다.
- `psci/method`가 `hvc`, `SMC Calling Convention v1.0` — PSCI 펌웨어 역할을 **QEMU 자신이** 한다. §10의 TF-A 경로와 비교할 증거다.
- `-netdev`를 주지 않았는데 `eth0 10.0.2.15`가 생겼다. QEMU `virt`는 `-nic`/`-netdev`가 없으면 **기본 NIC(virtio-net-pci, user 모드)** 를 하나 붙인다(`pci 0000:00:01.0: [1af4:1000]` = virtio-net). 네트워크를 정말 없애려면 `-nic none`.
- `Memory: ... 3904K kernel code` — defconfig 커널이면 이 숫자가 수십 MB다.

같은 initramfs로 랩 defconfig 커널과 3회씩 비교했다(`quiet`, `LAB_CMD=true`).

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/S1; \
  for i in 1 2 3; do /usr/bin/time -f "s1 Image direct: %e s" timeout 120 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
    -kernel out/Image -initrd out/initramfs.cpio.gz -append "console=ttyAMA0 quiet LAB_CMD=true" 2>&1 | grep -aE "wall|direct|s1-lab"; done; \
  for i in 1 2 3; do /usr/bin/time -f "lab Image direct: %e s" timeout 120 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
    -kernel /var/lab/out/Image -initrd out/initramfs.cpio.gz -append "console=ttyAMA0 quiet LAB_CMD=true" 2>&1 | grep -aE "direct|s1-lab"; done'
```

```text
s1-lab: 6.18.55-s1 up 0.55s root=rootfs
s1-lab: eth0 10.0.2.15/24
s1 Image direct: 1.01 s
s1-lab: 6.18.55-s1 up 0.48s root=rootfs
s1-lab: eth0 10.0.2.15/24
s1 Image direct: 0.82 s
s1-lab: 6.18.55-s1 up 0.46s root=rootfs
s1-lab: eth0 10.0.2.15/24
s1 Image direct: 0.80 s
s1-lab: 6.18.55 up 1.10s root=rootfs
s1-lab: eth0 10.0.2.15/24
lab Image direct: 1.67 s
s1-lab: 6.18.55 up 0.96s root=rootfs
s1-lab: eth0 10.0.2.15/24
lab Image direct: 1.45 s
s1-lab: 6.18.55 up 1.00s root=rootfs
s1-lab: eth0 10.0.2.15/24
lab Image direct: 1.49 s
```

작은 커널이 `/init`까지 약 2배 빠르다(0.46–0.55 s vs 0.96–1.10 s). initcall이 적기 때문이다(C7). TCG 수치라서 실제 보드에서는 비율도 달라질 수 있다. 하나 더: 내가 만든 initramfs가 **랩 커널에서도 그대로 돈다** — 유저 공간과 커널은 syscall ABI로만 묶여 있어서다(E6).

체크포인트: **`s1-lab: 6.18.55-s1 up ...`과 `reboot: Power down`이 보여야 한다.** 대화형으로 쓰려면 `make B=/var/tmp/S1 run-direct`, 종료는 `Ctrl-A x`.

---

## 10. Step 8 — 부팅 ②: U-Boot `-bios`, 커널은 fw_cfg에서

`-bios u-boot.bin`과 `-kernel`을 **같이** 주면 QEMU는 커널을 RAM에 직접 놓지 않고 **fw_cfg**(QEMU가 펌웨어에 파일을 건네는 MMIO 장치, `fw-cfg@9020000`)에 넣어 둔다. U-Boot의 `bootflow scan`이 `qfw` bootmeth로 그걸 찾아 로드한다. 디스크 이미지를 만들지 않고도 "부트로더가 커널을 로드해서 `booti`하는" 진짜 경로를 탄다.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/S1 && /usr/bin/time -f "wall %e s" timeout 120 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -bios out/u-boot.bin -kernel out/Image -initrd out/initramfs.cpio.gz \
  -append "console=ttyAMA0 LAB_CMD=cat~/proc/device-tree/psci/method;echo;cat~/proc/cmdline" 2>&1 | grep -av "^\[" | sed "s/\x1b\[[0-9;?]*[a-zA-Z]//g; s/\x1b[78]//g"'
```

```text
U-Boot 2026.10 (Oct 18 2026 - 16:29:47 -0700)

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

Hit any key to stop autoboot: 2Hit any key to stop autoboot: 1Hit any key to stop autoboot: 0
Scanning for bootflows in all bootdevs
Seq  Method       State   Uclass    Part  Name                      Filename
---  -----------  ------  --------  ----  ------------------------  ----------------
Scanning global bootmeth 'efi_mgr':
Cannot persist EFI variables without system partition
Missing TPMv2 device for EFI_TCG_PROTOCOL
Missing RNG device for EFI_RNG_PROTOCOL
Scanning bootdev 'fw-cfg@9020000.bootdev':
loading kernel to address 40400000 size 5e9008 initrd 44000000 size 1275e3
  0  qfw          ready   qfw          0  qfw
** Booting bootflow 'qfw' with qfw
## Flattened Device Tree blob at 5e34ed80
   Booting using the fdt blob at 0x5e34ed80
Working FDT set to 5e34ed80
   Loading Ramdisk to 5d206000, end 5d32d5e3 ... OK
   Loading Device Tree to 000000005d103000, end 000000005d205fff ... OK
Working FDT set to 5d103000

Starting kernel ...

s1-lab: 6.18.55-s1 up 0.47s root=rootfs
s1-lab: eth0 10.0.2.15/24
hvc 
console=ttyAMA0 LAB_CMD=cat~/proc/device-tree/psci/method;echo;cat~/proc/cmdline
wall 3.11 s
```

(커널 로그 `[ ...]` 줄은 걸렀고, 카운트다운의 ANSI 제어 문자는 지웠다.)

출력에서 볼 것:

- `size 5e9008` = 6,197,256 = `out/Image` 크기 그대로. `devicetree: board` — U-Boot는 QEMU가 만든 DT를 자기 DT로 쓰고(`fdtcontroladdr`), 그걸 커널에 넘긴다(C3 §6).
- `bad CRC, using default environment` — flash에 저장된 환경 변수가 없다는 정상 메시지(C3 §4).
- 커널 커맨드라인은 `-append` 값 그대로 — qfw bootmeth가 fw_cfg의 cmdline도 같이 가져온다.
- 3.11초 중 **2초가 `bootdelay` 카운트다운**이다(다른 실행에서는 3.21초). 실제 제품에서 부팅 시간을 줄일 때 가장 먼저 0으로 만드는 값(C7).
- PSCI는 여전히 `hvc` = QEMU. U-Boot는 EL1/EL2에서 돌고 PSCI를 제공하지 않는다.

체크포인트: **`** Booting bootflow 'qfw' with qfw`와 `s1-lab:` 줄.**

---

## 11. Step 9 — 부팅 ③: TF-A secure → U-Boot → `ext4load` + `booti`

이번엔 실제 보드와 같은 전체 체인이다. 커널은 fw_cfg가 아니라 **virtio-blk 디스크의 ext4 파일시스템**에서 U-Boot가 직접 읽는다.

### 11.1 ext4 rootfs — `mkfs.ext4 -d` (root 권한 불필요)

```make
$(OUT)/rootfs.ext4: $(STAMP)/rootfs $(OUT)/Image
	rm -rf $(B)/rootfs-disk; cp -a $(B)/rootfs $(B)/rootfs-disk; cp $(OUT)/Image $(B)/rootfs-disk/boot/Image
	rm -f $@; mkfs.ext4 -q -L s1root -E root_owner=0:0 -d $(B)/rootfs-disk $@ 64M
images: $(OUT)/initramfs.cpio.gz $(OUT)/rootfs.ext4
```

`mkfs.ext4 -d <dir>`는 디렉터리 내용을 채운 채로 파일시스템을 만든다 — `sudo mount -o loop`가 필요 없다. 처음 버전은 `rootfs/boot/`에 `Image`를 직접 복사했는데, 그러면 다음에 initramfs를 다시 만들 때 6 MB 커널이 initramfs 안에도 들어간다. 그래서 디스크용 사본 `rootfs-disk/`를 따로 만든다.

```sh
limactl shell elinux -- bash -lc 'debugfs -R "ls -l /boot" /var/tmp/S1/out/rootfs.ext4 2>/dev/null; \
  dumpe2fs -h /var/tmp/S1/out/rootfs.ext4 2>/dev/null | grep -E "volume name|Block count|Free blocks|Block size"'
```

```text
    107   40775 (2)    501   1000    4096 18-Oct-2026 16:38 .
      2   40755 (2)      0      0    4096 18-Oct-2026 16:38 ..
    108  100775 (1)    501   1000   6197256 18-Oct-2026 16:38 Image

Filesystem volume name:   s1root
Block count:              16384
Free blocks:              12396
Block size:               4096
```

출력에서 볼 것: 루트 디렉터리(`..`, inode 2)는 `root_owner=0:0` 덕에 0:0이지만, 그 아래 파일은 **내 uid 501:1000 그대로**다. `-d`는 파일 소유자를 복사하기 때문이다. BusyBox 랩에서는 상관없지만, 제품 이미지에서는 `fakeroot` 또는 Buildroot의 device table로 소유자를 고친다(K3 §7).

### 11.2 직접 부팅으로 ext4 rootfs 확인 (`root=/dev/vda`)

TF-A 경로 전에, 디스크 자체가 좋은지 `-kernel`로 먼저 확인한다(한 번에 한 변수만 바꾸는 bring-up 습관). 사본에 쓰고, 꺼진 뒤 VM에서 읽어서 영속성을 확인한다.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/S1 && cp out/rootfs.ext4 disk-test.ext4 && /usr/bin/time -f "wall %e s" timeout 90 \
  qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic -kernel out/Image \
  -append "console=ttyAMA0 quiet root=/dev/vda rw init=/init LAB_CMD=df~-h~/;echo~persisted-\$(date~+%s)~>~/root/note.txt" \
  -drive if=none,id=d0,file=disk-test.ext4,format=raw -device virtio-blk-device,drive=d0 2>&1; \
  debugfs -R "cat /root/note.txt" disk-test.ext4 2>/dev/null; e2fsck -fn disk-test.ext4 2>&1 | tail -1'
```

```text
s1-lab: 6.18.55-s1 up 0.45s root=/dev/root
s1-lab: eth0 10.0.2.15/24
Filesystem                Size      Used Available Use% Mounted on
/dev/root                56.0M      7.5M     43.9M  15% /
[    0.717798] reboot: Power down
wall 0.85 s
persisted-1792367076
s1root: 433/16384 files (0.5% non-contiguous), 3988/16384 blocks
```

출력에서 볼 것: `-initrd` 없이 `root=/dev/vda`로 부팅했다. `init=/init`이 필요한 이유는 BusyBox 기본 init(`/sbin/init`)이 `/etc/inittab`을 찾기 때문이다 — 우리는 같은 `/init` 스크립트를 쓴다. 게스트가 쓴 `note.txt`를 VM에서 `debugfs`로 읽었고, `e2fsck -fn`이 깨끗하다(`sync` 후 poweroff 덕).

### 11.3 TF-A 경로 — expect로 U-Boot 조작

```text
#!/usr/bin/expect -f
# TF-A(BL1→BL2→BL31) → U-Boot(BL33) → 자동부팅 중단 → ext4load + booti 수동 실행
set out [lindex $argv 0]
set cmd [lindex $argv 1]
if {$cmd eq ""} { set cmd "uname~-a" }
set timeout 240
spawn qemu-system-aarch64 -machine virt,secure=on -cpu cortex-a57 -m 1024 -nographic \
  -bios $out/flash.bin \
  -drive if=none,id=d0,file=$out/rootfs.ext4,format=raw,snapshot=on -device virtio-blk-device,drive=d0
expect "Hit any key to stop autoboot" { send " " }
expect "=> " { send "virtio scan; ext4ls virtio 0 /boot\r" }
expect "=> " { send "ext4load virtio 0 \${kernel_addr_r} /boot/Image\r" }
expect "=> " { send "setenv bootargs \"console=ttyAMA0 root=/dev/vda rw init=/init LAB_CMD=$cmd\"\r" }
expect "=> " { send "booti \${kernel_addr_r} - \${fdtcontroladdr}\r" }
expect {
  "reboot: Power down" { exit 0 }
  "Kernel panic" { puts "\n*** kernel panic"; exit 2 }
  timeout { puts "\n*** timeout"; exit 1 }
  eof { exit 0 }
}
```

결정 메모:

- `-m 1024`: TF-A qemu 플랫폼은 BL33(U-Boot)을 DRAM 시작 + 512 MiB = `0x60000000`에 올린다. `-m 512`면 그 주소에 RAM이 없다(C5 §3에서 소스로 증명). 랩 README는 1057을 썼지만 1024로도 된다 — 게스트 ≤ 1 GB 규칙을 지키는 값.
- `snapshot=on`: 쓰기를 임시 파일로 보내서 `out/rootfs.ext4`가 변하지 않는다. `make`의 타임스탬프도 유지된다.
- `booti ${kernel_addr_r} - ${fdtcontroladdr}`: 가운데 `-`는 "initrd 없음". DT는 U-Boot가 QEMU에게서 받은 것(`fdtcontroladdr`)을 그대로 넘긴다. 랩 README 경고대로 `/var/lab/out/virt.dtb`는 `-kernel` 부팅 전용이라 여기에 쓰면 안 된다.

### 11.4 첫 시도의 실패 — 빈 커맨드라인

처음 스크립트는 `setenv bootargs console=... LAB_CMD=cat~a;echo;...`처럼 따옴표 없이 보냈다.

```text
=> setenv bootargs console=ttyAMA0 root=/dev/vda rw init=/init LAB_CMD=cat~/proc/device-tree/psci/method;echo;dmesg~|~grep~-m1~-i~psci:~PSCIv;cat~/proc/cmdline
syntax error
=> booti ${kernel_addr_r} - ${fdtcontroladdr}
...
[    0.000000] Kernel command line: 
[    0.657507] VFS: Cannot open root device "" or unknown-block(0,0): error -6
[    0.658470] Kernel panic - not syncing: VFS: Unable to mount root fs on unknown-block(0,0)
```

U-Boot의 hush 셸은 `;`를 명령 구분자로, `|`를 문법 오류로 본다. `setenv`가 실패해서 `bootargs`가 비었고, 커널은 `root=` 없이 떠서 panic했다. 그런데 panic 뒤 QEMU가 꺼지지 않아 expect가 240초 timeout까지 기다렸다 → (1) 값을 큰따옴표로 감싸고, (2) `"Kernel panic"` 패턴을 추가해 즉시 실패하게 고쳤다. **자동화 스크립트는 성공 패턴만이 아니라 실패 패턴도 기다려야 한다.**

### 11.5 성공

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/S1 && /usr/bin/time -f "wall %e s" /Users/donh/.elinux_scratch/S1/qemu-lab/scripts/tfa-booti.exp \
  /var/tmp/S1/out "cat~/proc/device-tree/psci/method;echo;cat~/proc/cmdline" > boot3.log 2>&1; \
  sed "s/\x1b\[[0-9;?]*[a-zA-Z]//g" boot3.log | grep -av "^\[ "; grep -a "psci:" boot3.log'
```

```text
spawn qemu-system-aarch64 -machine virt,secure=on -cpu cortex-a57 -m 1024 -nographic -bios /var/tmp/S1/out/flash.bin -drive if=none,id=d0,file=/var/tmp/S1/out/rootfs.ext4,format=raw,snapshot=on -device virtio-blk-device,drive=d0
NOTICE:  Booting Trusted Firmware
NOTICE:  BL1: v2.15.0(release):
NOTICE:  BL1: Built : 16:30:20, Oct 18 2026
NOTICE:  BL1: Booting BL2
NOTICE:  BL2: v2.15.0(release):
NOTICE:  BL2: Built : 16:30:21, Oct 18 2026
NOTICE:  BL1: Booting BL31
NOTICE:  BL31: v2.15.0(release):
NOTICE:  BL31: Built : 16:30:21, Oct 18 2026


U-Boot 2026.10 (Oct 18 2026 - 16:29:47 -0700)

DRAM:  1 GiB
using memory 0x7e44f000-0x7f48f000 for malloc()
Core:  51 devices, 14 uclasses, devicetree: board
Flash: 32 MiB
Loading Environment from Flash...*** Warning - bad CRC, using default environment

In:    serial,usbkbd
Out:   serial,vidconsole
Err:   serial,vidconsole
No USB controllers found
Net:   eth0: virtio-net#32

Hit any key to stop autoboot: 2Hit any key to stop autoboot: 0
=> virtio scan; ext4ls virtio 0 /boot
            ./
            ../
  6197256   Image

1 file(s), 2 dir(s)

=> ext4load virtio 0 ${kernel_addr_r} /boot/Image
6197256 bytes read in 9 ms (656.7 MiB/s)
=> setenv bootargs "console=ttyAMA0 root=/dev/vda rw init=/init LAB_CMD=cat~/proc/device-tree/psci/method;echo;cat~/proc/cmdline"
=> booti ${kernel_addr_r} - ${fdtcontroladdr}
## Flattened Device Tree blob at 7e44cce0
   Booting using the fdt blob at 0x7e44cce0
Working FDT set to 7e44cce0
   Loading Device Tree to 000000007d426000, end 000000007d42b087 ... OK
Working FDT set to 7d426000

Starting kernel ...

s1-lab: 6.18.55-s1 up 0.44s root=/dev/root
s1-lab: eth0 10.0.2.15/24
smc 
console=ttyAMA0 root=/dev/vda rw init=/init LAB_CMD=cat~/proc/device-tree/psci/method;echo;cat~/proc/cmdline
wall 1.20 s
[    0.000000] psci: probing for conduit method from DT.
[    0.000000] psci: PSCIv1.1 detected in firmware.
[    0.000000] psci: Using standard PSCI v0.2 function IDs
[    0.000000] psci: MIGRATE_INFO_TYPE not supported.
[    0.000000] psci: SMC Calling Convention v1.5
```

(커널 로그 `[ ...]` 줄은 걸렀다. 같은 명령의 직전 실행은 `wall 1.88 s`였다.)

출력에서 볼 것:

- `BL1: Booting BL2` → `BL1: Booting BL31` — BL2가 일을 마치면 제어가 BL1을 거쳐 BL31로 간다(qemu 플랫폼은 BL2가 S-EL1에서 돌고 BL1이 EL3에서 BL31로 넘겨 준다, C5 §2). BL31 다음 줄은 바로 U-Boot 배너 — BL33 진입.
- `Built : 16:30:20` — 방금 내가 빌드한 그 TF-A다.
- `Flash: 32 MiB` (②에서는 64 MiB) — secure=on이면 QEMU `virt`가 첫 번째 flash를 secure 세계 전용으로 돌리기 때문으로 보인다(QEMU virt 문서의 secure 설명). 비보안 U-Boot가 보는 flash가 달라진다는 점만 기억하면 된다.
- **`psci/method`가 `smc`, `SMC Calling Convention v1.5`** — ①②의 `hvc`/v1.0과 다르다. PSCI(CPU on/off, system off)를 이제 **TF-A BL31이 EL3에서** 처리한다. 마지막의 `poweroff`도 SMC → BL31 → QEMU 전원 끄기로 갔다.
- `6197256 bytes read in 9 ms` (직전 실행은 25 ms) — 페이지 캐시에 올라간 이미지 파일을 TCG virtio로 읽은 속도라 실제 eMMC 숫자로 쓰면 안 된다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 330">
<defs><marker id="ah3" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<text x="10" y="40" font-size="13">① 직접 -kernel (secure=off) — 측정 0.8~1.0 s</text>
<rect x="10" y="50" width="484" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="252" y="75" font-size="12" text-anchor="middle">QEMU 내장 로더: Image · initrd · DTB 를 RAM 에 놓고 x0=DTB 로 점프</text>
<rect x="502" y="50" width="74" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="539" y="75" font-size="12" text-anchor="middle">Linux EL1</text>
<line x1="494" y1="70" x2="502" y2="70" stroke="currentColor" marker-end="url(#ah3)"/>
<rect x="584" y="50" width="74" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="621" y="75" font-size="12" text-anchor="middle">/init</text>
<line x1="576" y1="70" x2="584" y2="70" stroke="currentColor" marker-end="url(#ah3)"/>
<text x="10" y="110" font-size="12">PSCI = QEMU 가 직접 처리 (method hvc) · DTB = QEMU 생성, /chosen 은 QEMU 가 패치</text>
<text x="10" y="140" font-size="13">② -bios u-boot.bin (secure=off) — 3.1~3.2 s (bootdelay 2 s 포함)</text>
<rect x="10" y="150" width="238" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-dasharray="4 3"/>
<text x="129" y="175" font-size="12" text-anchor="middle">EL3 펌웨어 없음</text>
<rect x="256" y="150" width="74" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="293" y="175" font-size="12" text-anchor="middle">U-Boot</text>
<line x1="248" y1="170" x2="256" y2="170" stroke="currentColor" marker-end="url(#ah3)"/>
<rect x="338" y="150" width="74" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="375" y="175" font-size="12" text-anchor="middle">qfw load</text>
<line x1="330" y1="170" x2="338" y2="170" stroke="currentColor" marker-end="url(#ah3)"/>
<rect x="420" y="150" width="74" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="457" y="175" font-size="12" text-anchor="middle">booti</text>
<line x1="412" y1="170" x2="420" y2="170" stroke="currentColor" marker-end="url(#ah3)"/>
<rect x="502" y="150" width="74" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="539" y="175" font-size="12" text-anchor="middle">Linux EL1</text>
<line x1="494" y1="170" x2="502" y2="170" stroke="currentColor" marker-end="url(#ah3)"/>
<rect x="584" y="150" width="74" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="621" y="175" font-size="12" text-anchor="middle">/init</text>
<line x1="576" y1="170" x2="584" y2="170" stroke="currentColor" marker-end="url(#ah3)"/>
<text x="10" y="210" font-size="12">PSCI = QEMU (hvc) · DTB = U-Boot 가 받은 QEMU DT · 커널·initrd 출처 = fw_cfg</text>
<text x="10" y="240" font-size="13">③ secure=on -bios flash.bin — 1.2~1.9 s (autoboot 즉시 중단)</text>
<rect x="10" y="250" width="74" height="40" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="47" y="275" font-size="12" text-anchor="middle">BL1 EL3</text>
<rect x="92" y="250" width="74" height="40" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="129" y="275" font-size="12" text-anchor="middle">BL2</text>
<line x1="84" y1="270" x2="92" y2="270" stroke="currentColor" marker-end="url(#ah3)"/>
<rect x="174" y="250" width="74" height="40" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="211" y="275" font-size="12" text-anchor="middle">BL31</text>
<line x1="166" y1="270" x2="174" y2="270" stroke="currentColor" marker-end="url(#ah3)"/>
<rect x="256" y="250" width="74" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="293" y="275" font-size="12" text-anchor="middle">U-Boot</text>
<line x1="248" y1="270" x2="256" y2="270" stroke="currentColor" marker-end="url(#ah3)"/>
<rect x="338" y="250" width="74" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="375" y="275" font-size="12" text-anchor="middle">ext4load</text>
<line x1="330" y1="270" x2="338" y2="270" stroke="currentColor" marker-end="url(#ah3)"/>
<rect x="420" y="250" width="74" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="457" y="275" font-size="12" text-anchor="middle">booti</text>
<line x1="412" y1="270" x2="420" y2="270" stroke="currentColor" marker-end="url(#ah3)"/>
<rect x="502" y="250" width="74" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="539" y="275" font-size="12" text-anchor="middle">Linux EL1</text>
<line x1="494" y1="270" x2="502" y2="270" stroke="currentColor" marker-end="url(#ah3)"/>
<rect x="584" y="250" width="74" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="621" y="275" font-size="12" text-anchor="middle">/init</text>
<line x1="576" y1="270" x2="584" y2="270" stroke="currentColor" marker-end="url(#ah3)"/>
<text x="10" y="310" font-size="12">PSCI = TF-A BL31 (method smc, SMCCC v1.5) · 커널 출처 = virtio-blk ext4 /boot/Image</text>
</svg>
```
그림 3 — 세 부팅 경로 비교. 오른쪽 두 칸(Linux, /init)은 같고, 왼쪽에서 누가 커널을 RAM에 놓고 PSCI를 누가 제공하는지가 다르다. 시간은 QEMU 프로세스 시작부터 poweroff까지의 wall time(TCG).

| | ① `-kernel` | ② `-bios u-boot.bin` | ③ `secure=on flash.bin` |
|---|---|---|---|
| 처음 실행되는 코드 | QEMU 부트 스텁 @0x40000000 (EL1) | U-Boot @0x0 flash (EL1, virtualization=off라 EL2 없음) | TF-A BL1 @0x0 (EL3) |
| 커널을 RAM에 놓는 주체 | QEMU | U-Boot (fw_cfg 읽기) | U-Boot (virtio-blk ext4) |
| DT | QEMU 생성, QEMU가 /chosen 패치 | QEMU 생성 → U-Boot → 커널 | 같음, secure 장치는 빠짐 |
| PSCI conduit | `hvc` (QEMU) | `hvc` (QEMU) | `smc` (TF-A BL31) |
| 필요한 RAM | 512 MB | 512 MB | 1024 MB (BL33 @0x60000000) |
| 측정 wall time | 0.80–1.01 s | 3.11–3.21 s (bootdelay 2 s 포함) | 1.20–1.88 s (autoboot 즉시 중단) |
| 언제 쓰나 | 커널·드라이버 개발 루프 | 부트로더 명령·환경 실험 | secure boot, PSCI, OP-TEE, 실제 보드 흉내 |

체크포인트: **③에서 `smc`가 출력되면 전체 체인이 내 빌드로 돈 것이다.**

---

## 12. Step 10 — 네트워크와 호스트 파일 공유 (ssh 없이)

### 12.1 구성

| 기능 | QEMU 옵션 | 게스트 쪽 |
|---|---|---|
| user 모드 NAT | `-netdev user,id=n0 -device virtio-net-device,netdev=n0` | `udhcpc` → 10.0.2.15, gw 10.0.2.2, DNS 10.0.2.3 |
| 호스트 → 게스트 포트 | `hostfwd=tcp:127.0.0.1:43100-:80` | 게스트 80번에 BusyBox `httpd` |
| 파일 공유 | `-virtfs local,path=/var/tmp/S1/share,mount_tag=host0,security_model=none,id=h0` | `mount -t 9p -o trans=virtio host0 /mnt/host` |

user 모드(slirp)는 root 권한도 tap 장치도 필요 없어서 공용 VM에 맞다. 대신 게스트로 들어오는 연결은 `hostfwd`로 연 포트만 된다. `hostfwd`를 `127.0.0.1`에 묶은 것은 VM 밖으로 포트를 열지 않으려는 것이다. virtio-fs(`vhost-user-fs`)도 QEMU에는 있지만 VM에 `virtiofsd`가 없어서(설치 안 함) 9p를 골랐다 — 9p는 QEMU 안에서 서버가 돌아 별도 데몬이 필요 없다.

### 12.2 실행 — 한 번에 4가지 확인

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/S1 && mkdir -p share && echo "hello from VM host $(date +%T)" > share/hello.txt && rm -f share/from_guest.txt; \
 (timeout 90 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic -kernel out/Image -initrd out/initramfs.cpio.gz \
  -append "console=ttyAMA0 quiet LAB_CMD=cat~/mnt/host/hello.txt;uname~-r~>~/mnt/host/from_guest.txt;ip~route;cat~/etc/resolv.conf;nslookup~kernel.org~|~tail~-2;httpd~-p~80~-h~/mnt/host;sleep~8" \
  -netdev user,id=n0,hostfwd=tcp:127.0.0.1:43100-:80 -device virtio-net-device,netdev=n0 \
  -virtfs local,path=/var/tmp/S1/share,mount_tag=host0,security_model=none,id=h0 > net.log 2>&1 &); \
 for i in $(seq 1 40); do curl -s --max-time 2 http://127.0.0.1:43100/hello.txt && break; sleep 0.5; done; echo "curl try=$i"; \
 sleep 9; cat net.log; echo ---; cat share/from_guest.txt'
```

```text
hello from VM host 16:57:03
curl try=3
s1-lab: 6.18.55-s1 up 0.42s root=rootfs
s1-lab: eth0 10.0.2.15/24
s1-lab: 9p host0 -> /mnt/host
hello from VM host 16:57:03
default via 10.0.2.2 dev eth0 
10.0.2.0/24 dev eth0 scope link  src 10.0.2.15 
nameserver 10.0.2.3
Address: 172.105.4.254

[    8.705810] reboot: Power down
---
6.18.55-s1
```

출력에서 볼 것:

- 첫 줄: VM에서 `curl 127.0.0.1:43100`이 **게스트의 httpd가 9p로 서빙한 파일**을 받았다 (VM → hostfwd → 게스트 80 → 9p → VM 디렉터리, 한 바퀴).
- 게스트가 `uname -r`을 `/mnt/host/from_guest.txt`에 썼고 VM에서 그대로 읽혔다 — ssh도 scp도 없이 양방향 파일 전송.
- `nslookup kernel.org`가 실제 주소를 받았다 — slirp가 DNS를 VM의 리졸버로 중계한다.
- `-device virtio-net-device`(MMIO)를 명시하면 §9의 기본 PCI NIC는 생기지 않는다.

이게 드라이버 개발 루프의 핵심이다: VM에서 `.ko`나 테스트 바이너리를 빌드해 `share/`에 두면, 게스트를 다시 만들지 않고 `/mnt/host/x.ko`로 바로 `insmod`한다(S2에서 쓴다).

체크포인트: **`s1-lab: 9p host0 -> /mnt/host`와 `from_guest.txt`.**

---

## 13. Step 11 — gdbstub: 이 랩의 JTAG

M4 §3에서 개념(QEMU gdbstub = JTAG 프로브)을 배웠다. 여기서는 내 커널로 "reset halt → 첫 명령 → `start_kernel` → 드라이버 probe" 순서를 따라간다. 실제 보드에서 Lauterbach/OpenOCD로 하는 첫 세션과 같은 순서다.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/S1 && (timeout 120 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -kernel out/Image -initrd out/initramfs.cpio.gz -append "console=ttyAMA0 nokaslr LAB_CMD=true" \
  -gdb tcp:127.0.0.1:43101 -S > gdbq.log 2>&1 &); sleep 1; \
  timeout 100 gdb -q -batch -iex "set auto-load safe-path /" -ex "file build/linux/vmlinux" -ex "source build/linux/vmlinux-gdb.py" \
  -ex "target remote 127.0.0.1:43101" -ex "info registers pc" -ex "x/3i \$pc" \
  -ex "hbreak start_kernel" -ex "continue" -ex "info registers pc sp" \
  -ex "hbreak pl011_probe" -ex "continue" -ex "bt 8" -ex "lx-version" -ex "p init_task.comm" -ex "delete" -ex "detach" 2>&1 | grep -v "^warning: could not"; \
  sleep 3; tail -3 gdbq.log'
```

```text
0x0000000040000000 in ?? ()
pc             0x40000000          0x40000000
=> 0x40000000:	ldr	x0, 0x40000018
   0x40000004:	mov	x1, xzr
   0x40000008:	mov	x2, xzr
Hardware assisted breakpoint 1 at 0xffff8000804f0728: file /var/tmp/S1/src/linux/init/main.c, line 872.

Breakpoint 1, start_kernel () at /var/tmp/S1/src/linux/init/main.c:872
872		char *command_line;
pc             0xffff8000804f0728  0xffff8000804f0728 <start_kernel>
sp             0xffff800080543ec0  0xffff800080543ec0
Hardware assisted breakpoint 2 at 0xffff80008023e848: file /var/tmp/S1/src/linux/drivers/tty/serial/amba-pl011.c, line 2902.

Breakpoint 2, pl011_probe (dev=0xffff000000d67000, id=0xffff80008040eb50 <pl011_ids>) at /var/tmp/S1/src/linux/drivers/tty/serial/amba-pl011.c:2902
2902		struct vendor_data *vendor = id->data;
#0  pl011_probe (dev=0xffff000000d67000, id=0xffff80008040eb50 <pl011_ids>) at /var/tmp/S1/src/linux/drivers/tty/serial/amba-pl011.c:2902
#1  0xffff800080210b24 in amba_probe (dev=0xffff000000d67000) at /var/tmp/S1/src/linux/drivers/amba/bus.c:282
#2  0xffff800080249984 in call_driver_probe (dev=0xffff000000d67000, drv=0xffff8000805d9db8 <pl011_driver>) at /var/tmp/S1/src/linux/drivers/base/dd.c:640
#3  really_probe (dev=dev@entry=0xffff000000d67000, drv=drv@entry=0xffff8000805d9db8 <pl011_driver>) at /var/tmp/S1/src/linux/drivers/base/dd.c:718
#4  0xffff800080249b70 in __driver_probe_device (drv=drv@entry=0xffff8000805d9db8 <pl011_driver>, dev=dev@entry=0xffff000000d67000) at /var/tmp/S1/src/linux/drivers/base/dd.c:880
#5  0xffff800080249c20 in driver_probe_device (drv=drv@entry=0xffff8000805d9db8 <pl011_driver>, dev=dev@entry=0xffff000000d67000) at /var/tmp/S1/src/linux/drivers/base/dd.c:910
#6  0xffff800080249cf8 in __device_attach_driver (drv=0xffff8000805d9db8 <pl011_driver>, _data=0xffff80008068bbd0) at /var/tmp/S1/src/linux/drivers/base/dd.c:1038
#7  0xffff800080247eb4 in bus_for_each_drv (bus=<optimized out>, start=start@entry=0x0, data=data@entry=0xffff80008068bbd0, fn=fn@entry=0xffff800080249ca0 <__device_attach_driver>) at /var/tmp/S1/src/linux/drivers/base/bus.c:462
Linux version 6.18.55-s1 (donh@lima-elinux) (gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0, GNU ld (GNU Binutils for Ubuntu) 2.46) #1 SMP Sun Oct 18 16:35:53 PDT 2026
$1 = "swapper/0\000\000\000\000\000\000"
[Inferior 1 (process 1) detached]
s1-lab: 6.18.55-s1 up 0.43s root=rootfs
s1-lab: eth0 10.0.2.15/24
[    0.625072] reboot: Power down
```

출력에서 볼 것:

- `-S`로 **리셋 직후 멈춘** CPU의 PC는 `0x40000000` — §9에서 말한 QEMU 부트 스텁이다. `ldr x0, 0x40000018`(DTB 주소 로드), `mov x1, xzr`, `mov x2, xzr`가 **arm64 부팅 프로토콜**(x0=DTB, x1–x3=0)을 그대로 보여 준다(C6 §2).
- `hbreak`(하드웨어 브레이크포인트)를 쓴 이유: MMU가 켜지기 전에는 가상 주소 `0xffff8000...`에 소프트웨어 브레이크(메모리에 BRK 명령 쓰기)를 걸 수 없다. JTAG에서 flash 코드에 HW breakpoint를 쓰는 것과 같은 이유.
- `bt`: `bus_for_each_drv → __device_attach_driver → driver_probe_device → really_probe → call_driver_probe → amba_probe → pl011_probe` — G 모듈의 driver model 매칭 경로가 실물 스택으로 나왔다(G3, D3).
- `lx-version`, `init_task.comm` — `scripts_gdb` 타깃으로 만든 `vmlinux-gdb.py`가 커널 자료구조를 읽는다. `nokaslr`은 이 커널엔 `RANDOMIZE_BASE`가 없어 사실 필요 없지만, defconfig 커널에서도 같은 명령이 되도록 습관으로 넣는다.
- `detach` 후 게스트가 정상 부팅해서 꺼졌다 — 디버거가 상태를 망가뜨리지 않았다.

체크포인트: **`Breakpoint 2, pl011_probe`와 8단 backtrace.** Makefile의 `run-gdb` 타깃이 QEMU 쪽 명령을 담고 있다.

---

## 14. Step 12 — 한 번에 다시 만들기 (idempotent rebuild)

### 14.1 두 번째 `make all`은 아무것도 안 한다

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp && M="make -C /Users/donh/.elinux_scratch/S1/qemu-lab B=/var/tmp/S1"; \
  $M all 2>&1 | tail -2; $M -q all; echo "make -q exit=$?"'
```

```text
make: Nothing to be done for 'all'.
make: Leaving directory '/Users/donh/.elinux_scratch/S1/qemu-lab'
make -q exit=0
```

`make -q`의 종료 코드 0은 "모든 타깃이 최신"이라는 뜻이다. CI에서 "산출물이 소스와 맞는가"를 검사하는 데 쓸 수 있다.

### 14.2 `/init`을 고치면 → rootfs와 이미지 두 개만

§8.4에서 9p 마운트 조건을 고친 직후 `make -n`(실행하지 않고 할 일만 출력)을 찍었다.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp && make -C /Users/donh/.elinux_scratch/S1/qemu-lab B=/var/tmp/S1 -n all | grep -vE "^make" | cut -c1-110; \
  /usr/bin/time -f "rebuild after init edit: %e s" make -s -C /Users/donh/.elinux_scratch/S1/qemu-lab B=/var/tmp/S1 all'
```

```text
rm -rf /var/tmp/S1/rootfs; make -C /var/tmp/S1/build/busybox CONFIG_PREFIX=/var/tmp/S1/rootfs install >/dev/nu
cd /var/tmp/S1/rootfs && mkdir -p proc sys dev tmp etc mnt root usr/share/udhcpc boot
install -m755 /Users/donh/.elinux_scratch/S1/qemu-lab/rootfs/init /var/tmp/S1/rootfs/init
install -m755 /Users/donh/.elinux_scratch/S1/qemu-lab/rootfs/udhcpc.script /var/tmp/S1/rootfs/usr/share/udhcpc
mkdir -p /var/tmp/S1/.stamp; touch /var/tmp/S1/.stamp/rootfs
mkdir -p /var/tmp/S1/out; cd /var/tmp/S1/rootfs && find . | LC_ALL=C sort | cpio -o -H newc -R 0:0 --quiet | g
rm -rf /var/tmp/S1/rootfs-disk; cp -a /var/tmp/S1/rootfs /var/tmp/S1/rootfs-disk; cp /var/tmp/S1/out/Image /va
rm -f /var/tmp/S1/out/rootfs.ext4; mkfs.ext4 -q -L s1root -E root_owner=0:0 -d /var/tmp/S1/rootfs-disk /var/tm
rebuild after init edit: 4.66 s
```

커널·U-Boot·TF-A·BusyBox 컴파일은 하나도 없다. 4.7초.

### 14.3 fragment를 `touch`하면 → 설정은 다시, 컴파일은 0개

```sh
touch ~/.elinux_scratch/S1/qemu-lab/config/kernel-virt.fragment
limactl shell elinux -- bash -lc 'cd /var/tmp && /usr/bin/time -f "rebuild after touching fragment: %e s" \
  make -C /Users/donh/.elinux_scratch/S1/qemu-lab B=/var/tmp/S1 all > /var/tmp/S1/rebuild.log 2>&1; tail -1 /var/tmp/S1/rebuild.log; \
  grep -cE " (CC|LD|AR) " /var/tmp/S1/rebuild.log; grep -vE "Entering|Leaving|^#|^$|redefined|Previous value|New value|^Using|^Merging" /var/tmp/S1/rebuild.log | cut -c1-100'
```

```text
rebuild after touching fragment: 14.33 s
0
make -C /var/tmp/S1/src/linux O=/var/tmp/S1/build/linux tinyconfig
  GEN     Makefile
cd /var/tmp/S1/build/linux && /var/tmp/S1/src/linux/scripts/kconfig/merge_config.sh -m -O /var/tmp/S1/build/li
make -C /var/tmp/S1/src/linux O=/var/tmp/S1/build/linux olddefconfig
  GEN     Makefile
fragment OK: 46 options applied, 718 =y total
make -C /var/tmp/S1/src/linux O=/var/tmp/S1/build/linux -j2 Image scripts_gdb
  SYNC    include/config/auto.conf
  CALL    /var/tmp/S1/src/linux/scripts/checksyscalls.sh
rm -rf /var/tmp/S1/rootfs-disk; cp -a /var/tmp/S1/rootfs /var/tmp/S1/rootfs-disk; cp /var/tmp/S1/out/Image /va
rm -f /var/tmp/S1/out/rootfs.ext4; mkfs.ext4 -q -L s1root -E root_owner=0:0 -d /var/tmp/S1/rootfs-disk /var/tm
rebuild after touching fragment: 14.33 s
```

(`GEN Makefile` 반복 줄은 하나만 남겼다.)

출력에서 볼 것: Make 입장에서는 fragment가 새로워져서 `.config`를 다시 만들었지만, 내용이 같으므로 Kbuild의 `syncconfig`가 `auto.conf`를 비교해서 **다시 컴파일한 오브젝트가 0개**다(E2 §8의 fixdep 이야기). 두 겹의 증분 판단 — Make는 파일 시각, Kbuild는 설정 내용과 헤더 의존성.

주의: Mac에서 `touch`한 직후 VM에서 바로 `make -n`을 돌렸을 때 한 번은 `Nothing to be done`이 나왔다. Lima의 공유 마운트가 mtime을 늦게 반영하는 경우가 있다 — 이상하면 몇 초 뒤 다시 돌린다.

### 14.4 클린 빌드 총 시간

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 300">
<text x="20" y="28" font-size="14">클린 빌드 단계별 wall time (초, -j2, Lima VM on M2) — 합계 약 386 s</text>
<text x="260" y="67" font-size="13" text-anchor="end">Linux 커널 (tinyconfig+fragment)</text>
<rect x="270" y="50" width="323.1" height="24" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<text x="599.1" y="67" font-size="13">293.7 s</text>
<text x="260" y="103" font-size="13" text-anchor="end">BusyBox static</text>
<rect x="270" y="86" width="47.4" height="24" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
<text x="323.4" y="103" font-size="13">43.1 s</text>
<text x="260" y="139" font-size="13" text-anchor="end">U-Boot qemu_arm64</text>
<rect x="270" y="122" width="30.2" height="24" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<text x="306.2" y="139" font-size="13">27.5 s</text>
<text x="260" y="175" font-size="13" text-anchor="end">이미지 (cpio + ext4)</text>
<rect x="270" y="158" width="12.3" height="24" fill="#888" fill-opacity="0.6" stroke="#888"/>
<text x="288.3" y="175" font-size="13">11.2 s</text>
<text x="260" y="211" font-size="13" text-anchor="end">tar 풀기 4개</text>
<rect x="270" y="194" width="8.5" height="24" fill="#888" fill-opacity="0.6" stroke="#888"/>
<text x="284.5" y="211" font-size="13">7.7 s</text>
<text x="260" y="247" font-size="13" text-anchor="end">TF-A PLAT=qemu</text>
<rect x="270" y="230" width="3.0" height="24" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
<text x="279.0" y="247" font-size="13">2.7 s</text>
<line x1="270" y1="44" x2="270" y2="266" stroke="currentColor"/>
<line x1="270.0" y1="266" x2="270.0" y2="271" stroke="currentColor"/><text x="270.0" y="286" font-size="12" text-anchor="middle">0</text>
<line x1="380.0" y1="266" x2="380.0" y2="271" stroke="currentColor"/><text x="380.0" y="286" font-size="12" text-anchor="middle">100</text>
<line x1="490.0" y1="266" x2="490.0" y2="271" stroke="currentColor"/><text x="490.0" y="286" font-size="12" text-anchor="middle">200</text>
<line x1="600.0" y1="266" x2="600.0" y2="271" stroke="currentColor"/><text x="600.0" y="286" font-size="12" text-anchor="middle">300</text>
</svg>
```
그림 4 — 클린 빌드 단계별 wall time. 커널이 전체의 76%. 다른 작성자들과 CPU를 공유하던 중의 측정이다.

| 산출물 | 크기 |
|---|---|
| `Image` | 6,197,256 B (랩 defconfig 42,740,224 B) |
| `vmlinux` | 97,870,344 B (text+data+bss 6.4 MB, 나머지 DWARF) |
| `u-boot.bin` | 1,504,208 B |
| `bl1.bin` · `bl2.bin` · `bl31.bin` | 22,832 · 25,072 · 49,255 B |
| `fip.bin` / `flash.bin` | 1,578,711 B / 67,108,864 B |
| `busybox` (static, stripped) | 2,242,744 B |
| `initramfs.cpio.gz` | 1,209,826 B |
| `rootfs.ext4` | 64 MiB (사용 7.5 MB) |
| 디스크 최대 사용 | `/var/tmp/S1` 3.1 GB (소스 2.1 GB가 대부분) |

---

## 15. Buildroot 경로 — 가이드(실행 안 함)

Don의 결정(무거운 빌드는 가이드만)에 따라 이 절은 **실행하지 않았다.** config만 만드는 가벼운 단계는 Buildroot 트리가 VM에 이미 있으면 돌려도 되지만, 확인 결과 없다(K3가 쓰던 `/var/tmp/K3/`는 정리됐고, 다른 곳에도 `Config.in.legacy`가 있는 트리가 없다). Buildroot를 새로 받는 것도 이번 범위 밖이라 config 단계도 실행하지 않았다.

```sh
limactl shell elinux -- bash -lc 'ls -d /var/tmp/K3/k3_buildroot; find /var/tmp /home /opt -maxdepth 4 -name Config.in.legacy 2>/dev/null | wc -l'
```

```text
ls: cannot access '/var/tmp/K3/k3_buildroot': No such file or directory
0
```

출력에서 볼 것: Buildroot 트리의 표식 파일(`Config.in.legacy`)이 0개 — 쓸 수 있는 트리가 없다.

### 15.1 손으로 만든 것과 Buildroot의 대응

| S1 Makefile | Buildroot |
|---|---|
| `checksums/SHA256SUMS` | 패키지마다 `<pkg>.hash` + `BR2_DOWNLOAD_FORCE_CHECK_HASHES` |
| `dl/` 캐시 symlink | `BR2_DL_DIR` (여러 빌드가 공유) |
| 호스트 gcc 그대로 | 툴체인: internal(직접 빌드) 또는 **external**(Bootlin 등 미리 빌드된 것, K3 §4) |
| `kernel-virt.fragment` | `BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES` |
| BusyBox `sed` 3줄 | `BR2_PACKAGE_BUSYBOX_CONFIG_FRAGMENT_FILES` |
| `rootfs/init` 설치 | `BR2_ROOTFS_OVERLAY` |
| `mkfs.ext4 -d` + `cpio -R 0:0` | `BR2_TARGET_ROOTFS_EXT2_4`, `BR2_TARGET_ROOTFS_CPIO` + fakeroot (소유자 문제를 해결해 준다) |
| TF-A `dd` 레시피 | `BR2_TARGET_ARM_TRUSTED_FIRMWARE` + post-image 스크립트/genimage |
| `.stamp/` 증분 | 패키지별 `.stamp_*` — 단, **설정 변경을 추적하지 않는다** (K3 §10) |

### 15.2 BR2_EXTERNAL 골격 (파일은 만들었고, 빌드는 안 했다)

저장소의 `buildroot-external/`:

```text
buildroot-external/
  external.desc                      name: S1QEMU
  Config.in                          (자체 패키지 Config.in 을 source 할 자리)
  external.mk                        include package/*/*.mk
  configs/s1_qemu_aarch64_defconfig
  board/s1qemu/linux-s1.fragment
  board/s1qemu/overlay/etc/hostname
```

`configs/s1_qemu_aarch64_defconfig` — 업스트림 `qemu_aarch64_virt_defconfig`(K3 §2에서 실물을 봤다)에 external 툴체인과 fragment를 얹은 것이다. 심볼 이름은 K3에서 실제로 확인한 것과 Buildroot 매뉴얼 기준이며, **이 파일 자체로 빌드해 보지는 않았다.**

```text
BR2_aarch64=y
BR2_cortex_a57=y
BR2_TOOLCHAIN_EXTERNAL=y
BR2_TOOLCHAIN_EXTERNAL_BOOTLIN=y
BR2_TOOLCHAIN_EXTERNAL_BOOTLIN_AARCH64_GLIBC_STABLE=y
BR2_SYSTEM_DHCP="eth0"
BR2_TARGET_GENERIC_GETTY_PORT="ttyAMA0"
BR2_ROOTFS_OVERLAY="$(BR2_EXTERNAL_S1QEMU_PATH)/board/s1qemu/overlay"
BR2_ROOTFS_POST_IMAGE_SCRIPT="board/qemu/post-image.sh"
BR2_ROOTFS_POST_SCRIPT_ARGS="$(BR2_DEFCONFIG)"
BR2_LINUX_KERNEL=y
BR2_LINUX_KERNEL_CUSTOM_VERSION=y
BR2_LINUX_KERNEL_CUSTOM_VERSION_VALUE="6.18.55"
BR2_LINUX_KERNEL_USE_CUSTOM_CONFIG=y
BR2_LINUX_KERNEL_CUSTOM_CONFIG_FILE="board/qemu/aarch64-virt/linux.config"
BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES="$(BR2_EXTERNAL_S1QEMU_PATH)/board/s1qemu/linux-s1.fragment"
BR2_LINUX_KERNEL_NEEDS_HOST_OPENSSL=y
BR2_TARGET_ROOTFS_EXT2=y
BR2_TARGET_ROOTFS_EXT2_4=y
BR2_TARGET_ROOTFS_EXT2_SIZE="128M"
BR2_TARGET_UBOOT=y
BR2_TARGET_UBOOT_BUILD_SYSTEM_KCONFIG=y
BR2_TARGET_UBOOT_CUSTOM_VERSION=y
BR2_TARGET_UBOOT_CUSTOM_VERSION_VALUE="2026.10"
BR2_TARGET_UBOOT_BOARD_DEFCONFIG="qemu_arm64"
```

### 15.3 돌린다면 (가이드 — 실행 안 함)

```sh
git clone https://gitlab.com/buildroot.org/buildroot.git /var/tmp/S1/buildroot     # 또는 릴리스 tarball + 서명 확인
cd /var/tmp/S1/buildroot && git checkout <LTS 태그>
make O=/var/tmp/S1/br BR2_EXTERNAL=/Users/donh/.elinux_scratch/S1/qemu-lab/buildroot-external s1_qemu_aarch64_defconfig
make -C /var/tmp/S1/br show-info | head            # 패키지 목록 확인 (빌드 전)
make -C /var/tmp/S1/br -j2                          # 여기부터가 무거운 빌드: 실행하지 않는다
qemu-system-aarch64 -M virt -cpu cortex-a57 -m 512M -nographic \
  -kernel /var/tmp/S1/br/images/Image -append "rootwait root=/dev/vda console=ttyAMA0" \
  -drive file=/var/tmp/S1/br/images/rootfs.ext4,if=none,format=raw,id=hd0 -device virtio-blk-device,drive=hd0 \
  -netdev user,id=n0 -device virtio-net-device,netdev=n0
```

예상되는 막힘(실행하지 않았으므로 확인 안 된 위험 목록):

- U-Boot 2026.10은 호스트 도구 빌드에 gnutls/openssl이 필요하다 → `BR2_TARGET_UBOOT_NEEDS_GNUTLS=y` 류의 옵션이 필요할 수 있다.
- external 툴체인의 커널 헤더 버전 < 타깃 커널이면 괜찮지만, 반대면 Buildroot가 거부한다(B5 §4).
- TF-A까지 Buildroot로 만들려면 `BR2_TARGET_ARM_TRUSTED_FIRMWARE_PLATFORM="qemu"`, `..._UBOOT_AS_BL33=y`, 그리고 §6의 `dd` 레시피를 post-image 스크립트로 옮겨야 한다.

---

## 16. 흔한 실패와 고치는 법

| 실패 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| fragment 옵션 증발 | `DROPPED: CONFIG_TMPFS=y -> absent` (검사 없으면 무증상) | `depends on SHMEM`, tinyconfig가 SHMEM을 끔 | 의존 옵션을 fragment에 추가, `check-fragment.sh` 유지 |
| 망가진 `.config`가 남음 | 실패 뒤 `make`가 그냥 통과 | 레시피 실패해도 타깃 파일 존재 | `.DELETE_ON_ERROR:` |
| BusyBox O= | `output directory ... does not exist` | BusyBox는 O= 디렉터리를 안 만든다 | `mkdir -p $(BBO)` |
| BusyBox SHA1 | `sha1_process_block64_shaNI undeclared` | x86 전용 가속이 defconfig에 켜짐 | `SHA1_HWACCEL`, `SHA256_HWACCEL` 끄기 |
| BusyBox tc | `TCA_CBQ_MAX undeclared` | 호스트 커널 UAPI에서 CBQ 제거 | `CONFIG_TC` 끄기 |
| U-Boot `setenv` | `syntax error` → `Kernel command line:` 빈 줄 → VFS panic | hush가 `;`와 파이프 문자를 해석 | 값 전체를 큰따옴표로 |
| TF-A 무출력 | `-bios flash.bin`인데 아무것도 안 나옴 | `secure=on` 빠짐 (BL1은 EL3에서만 의미) | `-machine virt,secure=on` |
| TF-A가 BL2에서 멈춤 | `BL2: Built : ...` 다음 무출력 | `-m 512`, BL33 주소 0x60000000에 RAM 없음 | `-m 1024` |
| expect가 4분 기다림 | panic 뒤 timeout | 성공 패턴만 기다림 | `"Kernel panic"` 패턴 추가 |
| initramfs에 커널이 들어감 | initramfs가 갑자기 7 MB | rootfs 트리 공유 | 디스크용 사본 `rootfs-disk/` |
| 9p 커널 경고 | `9pnet_virtio: no channels available for device host0` | 장치 없이 mount 시도 | sysfs `mount_tag` 먼저 확인 |
| `-nic` 없는데 eth0 | 예상 밖 PCI NIC | virt 기본 NIC | 원하면 `-nic none` |

---

## 17. 확장 과제

1. **reproducible build**: `SOURCE_DATE_EPOCH=$(git log -1 --format=%ct)`와 `KBUILD_BUILD_TIMESTAMP`, `KBUILD_BUILD_HOST`를 고정해서 두 번 빌드한 `Image`·`u-boot.bin`이 `cmp`로 같아지게 만든다(§5에서 다른 바이트를 확인했다). 정답/힌트: U-Boot는 `SOURCE_DATE_EPOCH`, 커널은 `KBUILD_BUILD_TIMESTAMP/USER/HOST`.
2. **모듈 지원**: fragment에 `CONFIG_MODULES=y`, `CONFIG_MODULE_UNLOAD=y`를 넣고 `modules_prepare` 타깃을 추가해 S2의 hello 모듈을 이 커널로 빌드 → 9p로 넘겨서 `insmod`. 힌트: `make -C build/linux M=$PWD`.
3. **U-Boot 자동 부팅**: expect 대신 `boot.scr`(`mkimage -T script`)를 ext4 `/boot`에 넣고 U-Boot의 `bootflow scan`이 스스로 찾게 한다(C3 §5).
4. **TF-A + OP-TEE**: `SPD=opteed`, BL32=OP-TEE로 FIP를 다시 만든다(C5 §6). RAM 지도가 바뀌는 걸 확인한다.
5. **CI**: GitHub Actions(ubuntu-24.04-arm 러너)에서 `make all` + 세 부팅을 `LAB_CMD`로 돌리고 `s1-lab:` 줄을 grep하는 스모크 테스트. 산출물 해시를 아티팩트로 남긴다.
6. **SMP·성능**: `-smp 4`와 `-cpu max`로 부팅 시간을 다시 재고, `/proc/cpuinfo`와 `psci` CPU_ON 로그를 비교한다.
7. **U-Boot/TF-A도 gdb로**: `-S` 상태에서 `add-symbol-file build/tfa/qemu/release/bl1/bl1.elf`로 EL3 첫 명령을, U-Boot는 relocation 주소(`bdinfo`의 `relocaddr`)를 고려해 `add-symbol-file u-boot <relocaddr>`로 디버깅한다.

---

## 18. 면접과 GitHub에서 보여 주기

### 18.1 README 목차 (저장소 `README.md` 초안이 이미 있다)

1. 한 줄 소개: "Board-less ARM64 Linux lab: TF-A → U-Boot → Linux → BusyBox, built from verified sources with one idempotent Makefile"
2. 그림 1(구조), 그림 3(부팅 경로) — PNG로 내보내서 첨부
3. Quick start: `check-host.sh` → `make all` → `make run-tfa`
4. 측정표: 빌드 시간(그림 4), 크기, 부팅 시간 — "TCG on Lima/M2, not real hardware" 명시
5. 설계 결정: fragment + 생존 검사, O=/BUILD_BASE= 분리, 체크섬 고정, `.DELETE_ON_ERROR`
6. Lessons learned: §16 표의 상위 5개 (실패 로그 그대로)
7. Roadmap: §17

### 18.2 2분 데모 스크립트

1. `make -q all; echo $?` → `0` ("다 최신이다")
2. `make run-tfa` → BL1/BL2/BL31 NOTICE → U-Boot `ext4load` → 커널 → `smc` 출력. "PSCI가 TF-A로 바뀐 게 이 한 단어로 보인다."
3. `run-direct`와 비교해 `hvc` — "같은 커널, 같은 DT 노드, 다른 펌웨어 제공자"
4. `run-gdb` + gdb에서 `hbreak pl011_probe` → backtrace. "보드가 오면 JTAG로 이 순서를 그대로 한다."
5. fragment 한 줄 지우고 `make` → `DROPPED:`로 빌드가 멈추는 것 보여 주기

### 18.3 보여 줄 숫자

| 숫자 | 값 | 말할 문장 |
|---|---|---|
| 커널 크기 | 42.7 MB → 6.2 MB | defconfig 대비 7분의 1, 718개 옵션 |
| 커널 빌드 | 294 s at -j2 | 반복 루프가 5분이면 설정 실험을 하루에 수십 번 한다 |
| 부팅 | `/init`까지 0.46–0.55 s vs 0.96–1.10 s | initcall 수가 부팅 시간을 결정한다 (TCG 수치) |
| 증분 | 수정 없음 0 s, `/init` 수정 4.7 s, fragment touch 14 s · 컴파일 0개 | Make와 Kbuild의 두 겹 증분 |

### 18.4 면접 Q&A

**Q.** "You don't have the board yet. How do you start the BSP work?"

**A.** QEMU `virt`로 같은 아키텍처의 부트 체인을 먼저 세운다. TF-A BL1/BL2/BL31, U-Boot, 최소 커널, BusyBox까지 소스에서 빌드하고, 세 가지 부팅 경로로 각 단계를 따로 검증했다. 보드가 오면 바뀌는 건 플랫폼 포트(TF-A `PLAT`, U-Boot defconfig, DT, 커널 fragment)뿐이고 파이프라인·검증 스크립트·디버그 방식은 그대로 쓴다.

> I build the same boot chain on QEMU virt first: TF-A BL1/BL2/BL31, U-Boot as BL33, a minimal kernel and a static BusyBox, all from checksum-verified sources through one Makefile. I boot it three ways — direct kernel, U-Boot, and the full secure-world chain — so each stage is validated in isolation. When silicon arrives, only the platform ports change: the TF-A platform, U-Boot defconfig, device tree and kernel fragment. The pipeline, smoke tests and gdb workflow carry over.

**꼬리 질문**: "What does QEMU's -kernel skip?" → 펌웨어 전체와 커널 로딩, PSCI 제공자(QEMU가 hvc로 대신), DT의 /chosen 패치까지.

**Q.** "How do you keep a minimal kernel config maintainable?"

**A.** 전체 `.config`를 커밋하지 않고 `tinyconfig` 위에 얹는 fragment로 관리한다. 병합 후 `olddefconfig`가 의존성 때문에 옵션을 조용히 끌 수 있어서, fragment의 모든 줄이 최종 `.config`에 살아남았는지 검사하고 아니면 빌드를 멈춘다. 실제로 `TMPFS`가 `SHMEM` 의존성 때문에 사라진 걸 이렇게 잡았다.

> I keep a small fragment on top of tinyconfig instead of a full .config, merge it with merge_config.sh, and then verify that every fragment line survived olddefconfig — Kconfig silently drops options whose dependencies aren't met. That check caught CONFIG_TMPFS disappearing because SHMEM was off in tinyconfig.

**Q.** "How can you tell which firmware handles PSCI on a running system?"

**A.** DT의 `/psci` 노드 `method` 속성과 부팅 로그의 SMCCC 버전을 본다. QEMU 직접 부팅에서는 `hvc`와 SMCCC v1.0이고, TF-A 경로에서는 `smc`와 v1.5였다. `hvc`는 EL2(여기서는 QEMU 에뮬레이션), `smc`는 EL3 secure monitor로 트랩한다.

> Read /proc/device-tree/psci/method and the "SMC Calling Convention" line in dmesg. In my lab the direct boot shows hvc and SMCCC 1.0 — QEMU emulates PSCI — while the TF-A boot shows smc and SMCCC 1.5, meaning BL31 at EL3 services CPU_ON and SYSTEM_OFF.

**Q.** "Why did your TF-A boot hang with 512 MB?"

**A.** TF-A qemu 플랫폼이 BL33을 DRAM 시작 + 512 MiB인 0x60000000에 로드하도록 고정돼 있어서, RAM이 512 MB면 그 주소가 존재하지 않는다. BL2가 BL33을 복사하다 data abort로 조용히 멈춘다. 1 GB로 올리면 된다. 실제 보드에서도 "부트로더 로드 주소와 실장 DRAM 크기 불일치"는 흔한 bring-up 버그다.

> The qemu TF-A port loads BL33 at DRAM base plus 512 MiB, 0x60000000. With 512 MB of RAM that address doesn't exist, so BL2 aborts while copying U-Boot and the console just stops after "BL2: Built". Giving the guest 1 GB fixes it. On real boards the same class of bug is a load address that doesn't match the populated DRAM.

**Q.** "How do you debug the kernel before the console comes up?"

**A.** QEMU gdbstub를 JTAG처럼 쓴다. `-S`로 리셋 직후 멈추고, `vmlinux`를 심볼로 로드한 뒤 하드웨어 브레이크포인트로 `start_kernel`이나 문제 드라이버의 probe에 건다. MMU 켜기 전에는 소프트웨어 브레이크를 못 쓰기 때문이다. 콘솔 전에는 `earlycon=pl011,0x9000000`으로 UART를 직접 찌른다.

> I treat the QEMU gdbstub as a JTAG probe: start halted with -S, load vmlinux symbols, and use hardware breakpoints on start_kernel or the driver's probe, since software breakpoints don't work before the MMU is on. For output before the console driver binds, earlycon pokes the PL011 directly.

**Q.** "What makes your build reproducible and incremental?"

**A.** 입력은 해시로 고정한 tarball과 저장소 파일, 출력은 전부 별도 디렉터리의 파일 타깃이다. 두 번째 `make`는 아무것도 하지 않고(`make -q` 종료 코드 0), `/init`을 고치면 rootfs와 이미지만 4.7초에 다시 만든다. 바이트 단위 재현성은 빌드 타임스탬프를 고정해야 해서 다음 단계로 남겨 뒀다 — U-Boot 바이너리가 날짜 문자열 때문에 다르다는 걸 확인했다.

> Inputs are pinned tarballs plus repo files; every output is a file target in a separate build directory. A second make is a no-op, editing /init rebuilds only the rootfs and images in under five seconds, and touching the kernel fragment re-runs Kconfig but recompiles zero objects. Bit-for-bit reproducibility is the next step: my U-Boot differs from the lab's only in the embedded build date, so pinning SOURCE_DATE_EPOCH is the fix.

**Q.** "Buildroot or hand-rolled Makefile?"

**A.** 학습과 bring-up 초기에는 손으로 만든 파이프라인이 각 컴포넌트의 빌드 관례를 드러내서 좋다. 제품으로 가면 Buildroot(또는 Yocto)가 툴체인, 라이선스, 소유권(fakeroot), 패키지 해시를 관리해 준다. 내 저장소에는 같은 설정을 BR2_EXTERNAL 트리로 옮긴 골격이 있다.

> Hand-rolled first, because it exposes each component's build conventions — O=, BUILD_BASE=, CONFIG_PREFIX=, FIP packing. For a product I'd move to Buildroot or Yocto for toolchain management, license reports, fakeroot ownership and per-package hashes; my repo already carries a BR2_EXTERNAL skeleton with the same kernel fragment.

---

## 19. 직접 해보기

1. `/var/tmp/<내 ID>`를 지우고 `make B=... all`을 처음부터 돌려 그림 4의 숫자를 자기 측정으로 바꿔라. 정답/힌트: 다른 작업이 없으면 커널이 더 빨라야 한다.
2. fragment에서 `CONFIG_PACKET=y`를 지우고 부팅해 `s1-lab: eth0` 줄이 사라지는 이유를 설명하라. 정답/힌트: `udhcpc`는 `AF_PACKET` raw socket으로 DHCP를 보낸다.
3. `tfa-booti.exp`에서 `-m 1024`를 `-m 600`으로 바꿔 어디서 멈추는지 기록하라(랩 README는 540에서 U-Boot reset loop를 봤다). 정답/힌트: BL33 로드 주소와 U-Boot 재배치 주소(RAM 꼭대기 근처)를 둘 다 생각한다.
4. ②번 부팅에서 `bootdelay`를 0으로 만들어 wall time이 몇 초 줄어드는지 재라. 정답/힌트: QEMU를 `-S`로 두지 않고, U-Boot 프롬프트에서 `setenv bootdelay 0; saveenv`는 flash 환경이 필요하다 — 대신 defconfig에 `CONFIG_BOOTDELAY=0`.
5. gdb로 `hbreak do_one_initcall`을 걸고 `continue`를 10번 하며 `p fn`을 찍어 initcall 순서를 관찰하라. 정답/힌트: C6 §7의 initcall 레벨 순서와 비교.
6. `mkfs.ext4 -d`가 만든 이미지의 파일 소유자를 0:0으로 만드는 방법 두 가지를 찾아라. 정답/힌트: `fakeroot`로 `chown` 후 `mkfs.ext4 -d`, 또는 `debugfs -w`의 `set_inode_field`.
7. 9p 대신 `mkfs.ext4 -d`로 만든 두 번째 virtio 디스크로 파일을 넘기는 방식과 장단점을 비교하라. 정답/힌트: 디스크 방식은 게스트 실행 중 호스트가 쓰면 안 된다(파일시스템 캐시 불일치).

---

## 20. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| QEMU `virt` | 가상 ARM 보드 | 실제 SoC가 아닌 QEMU 전용 머신, DT를 QEMU가 생성 |
| TCG | Tiny Code Generator | KVM 없이 게스트 명령을 호스트 명령으로 번역하는 QEMU 엔진 |
| fw_cfg | firmware configuration device | QEMU가 커널·initrd·cmdline을 펌웨어에 건네는 MMIO 장치 |
| qfw | U-Boot fw_cfg 드라이버 | `bootflow`의 qfw bootmeth가 fw_cfg에서 커널을 읽는다 |
| FIP | Firmware Image Package | BL2·BL31·BL33을 UUID 목차로 묶은 TF-A 이미지 |
| BL33 | non-trusted firmware | TF-A가 마지막에 넘겨주는 비보안 부트로더 (여기서는 U-Boot) |
| PSCI conduit | `hvc` 또는 `smc` | 커널이 PSCI 호출을 어느 예외 레벨로 트랩할지 |
| tinyconfig | 최소 커널 설정 | allnoconfig + `tiny.config`, 크기 최적화 |
| config fragment | 설정 조각 | 기본 설정 위에 덮어쓸 `CONFIG_` 줄 모음 |
| `merge_config.sh` | fragment 병합 스크립트 | `scripts/kconfig/` 안, `-m`은 olddefconfig 생략 |
| `O=` / `BUILD_BASE=` | out-of-tree 빌드 | 소스 트리를 깨끗하게 두고 출력만 다른 곳에 |
| `mkfs.ext4 -d` | 디렉터리로 채운 ext4 생성 | loop mount·root 권한 없이 rootfs 이미지 |
| 9p (virtio-9p) | Plan 9 파일 프로토콜 | QEMU가 호스트 디렉터리를 게스트에 공유 |
| hostfwd | slirp 포트 포워딩 | 호스트 포트를 user 모드 게스트 포트로 연결 |
| gdbstub | QEMU 내장 gdb 서버 | `-gdb tcp::PORT -S`, 이 랩의 JTAG |
| `.DELETE_ON_ERROR` | GNU Make 특수 타깃 | 실패한 레시피의 반쪽 타깃 파일 삭제 |
| idempotent | 멱등 | 같은 입력으로 다시 실행해도 결과·작업이 변하지 않음 |

---

## 21. 요약 & 셀프 체크

S1은 `/var/lab`을 "빌려 쓰는 랩"에서 "내가 다시 만들 수 있는 랩"으로 바꾼다. 해시로 고정한 4개 tarball에서 출발해 U-Boot(27 s), TF-A(3 s), tinyconfig + 46줄 fragment 커널(294 s, 6.2 MB), static BusyBox(43 s)를 하나의 Makefile로 만들고, 같은 커널을 QEMU 직접·U-Boot(fw_cfg)·TF-A secure(ext4 + `booti`) 세 경로로 띄워 PSCI 제공자가 `hvc`에서 `smc`로 바뀌는 것까지 증거로 남겼다. virtio-blk ext4, user 네트워크 + hostfwd + 9p, gdbstub 하드웨어 브레이크포인트로 개발 루프를 갖췄고, 두 번째 `make`가 아무것도 안 하는 증분 빌드와, 빌드 중 실제로 부딪친 7가지 실패(옵션 증발, BusyBox 3종, U-Boot 따옴표, TF-A 메모리, expect timeout)를 고친 기록이 포트폴리오의 핵심 내용이다. Buildroot 경로는 BR2_EXTERNAL 골격까지만 만들고 빌드는 가이드로 남겼다.

- [ ] `check-host.sh`의 패키지 22개가 각각 무엇에 쓰이는지 설명할 수 있다
- [ ] sha256 고정과 GPG 서명 확인의 차이, kernel.org `.tar.sign`이 `.tar`에 대한 서명인 이유를 말할 수 있다
- [ ] U-Boot `O=`, TF-A `BUILD_BASE=`/`BL33=`, BusyBox `O=`/`CONFIG_PREFIX=`를 외워서 쓸 수 있다
- [ ] `flash.bin`의 레이아웃(BL1 @0, FIP @0x40000, 64 MiB)을 그릴 수 있다
- [ ] tinyconfig + fragment로 커널을 만들고, 옵션이 조용히 빠지는 이유와 검출 방법을 설명할 수 있다
- [ ] BusyBox static initramfs를 root 권한 없이 만들 수 있다 (`cpio -R 0:0`)
- [ ] 세 부팅 경로에서 누가 커널을 RAM에 놓고 누가 PSCI를 제공하는지 표로 그릴 수 있다
- [ ] U-Boot에서 `ext4load` + `booti`로 수동 부팅하고, hush 따옴표 함정을 피할 수 있다
- [ ] `mkfs.ext4 -d`로 rootfs 디스크를 만들고 `root=/dev/vda`로 부팅할 수 있다
- [ ] user 네트워크 + hostfwd + 9p로 ssh 없이 파일과 포트를 주고받을 수 있다
- [ ] gdbstub로 리셋 직후부터 `start_kernel`, 드라이버 probe까지 따라갈 수 있다
- [ ] Makefile이 idempotent하다는 것을 `make -q`와 증분 재빌드로 증명할 수 있다

## 참고 자료

- TF-A 문서, "QEMU virt Armv8-A" 플랫폼 페이지 — `PLAT=qemu`, `dd` 레시피, 메모리 지도: https://trustedfirmware-a.readthedocs.io/en/latest/plat/qemu.html
- U-Boot 문서, "QEMU ARM" 보드 페이지: https://docs.u-boot.org/en/latest/board/emulation/qemu-arm.html
- QEMU 문서, "'virt' generic virtual platform": https://www.qemu.org/docs/master/system/arm/virt.html
- QEMU 문서, gdbstub: https://www.qemu.org/docs/master/system/gdb.html
- Linux 커널 문서, Kconfig 언어와 `make` 타깃 (docs.kernel.org/kbuild/), arm64 부팅 프로토콜 "Booting AArch64 Linux": https://docs.kernel.org/arch/arm64/booting.html
- Linux 커널 문서, v9fs: https://docs.kernel.org/filesystems/9p.html
- kernel.org, "Linux kernel releases PGP signatures": https://www.kernel.org/signature.html
- Buildroot 사용자 매뉴얼 (BR2_EXTERNAL, external toolchain): https://buildroot.org/downloads/manual/manual.html
- 소스 위치: `scripts/kconfig/merge_config.sh`, `kernel/configs/tiny.config`, `init/Kconfig`(SHMEM), `fs/Kconfig`(TMPFS) — elixir.bootlin.com의 v6.18 트리
- 이 랩의 선행 노트: C1, C3, C4, C5, C6, C7, E2, K2, K3, M4, B1, B5
