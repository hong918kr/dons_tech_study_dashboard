# K6. 바이너리 배포판 방식 — Debian/Ubuntu 기반 이미지, Jetson L4T, 장단점

> **이 노트를 다 읽으면**: "소스에서 빌드(Buildroot·Yocto)"와 "바이너리 조립(Debian·Ubuntu·Fedora IoT)" 두 철학의 차이를 크기·부팅 시간·보안 업데이트·재현성·라이선스 축으로 설명할 수 있다 · Ubuntu Base 타르볼을 받아 서명 검증 → chroot에서 apt로 systemd를 깔고 → ext4 이미지로 묶어 → QEMU에서 systemd로 부팅하고 `systemd-analyze`로 잴 수 있다 · `.deb`·dpkg 데이터베이스·maintainer script가 어떻게 생겼는지, read-only root에서 무엇이 깨지는지 직접 보여 줄 수 있다 · Jetson Linux(L4T)의 `Linux_for_Tegra/` 구조와 `apply_binaries.sh` → `l4t_initrd_flash.sh` 흐름, i.MX(Yocto)·TI(Yocto + Debian)와의 BSP 전달 방식 차이를 말할 수 있다
> **왜 필요한가**: Jetson Orin 기반 로봇·드론 회사는 거의 다 "NVIDIA가 준 Ubuntu"에서 출발하고, 면접에서 "Yocto vs Ubuntu, 우리 제품엔 뭘 쓰겠나?", "필드 장비에 apt upgrade를 돌려도 되나?"를 묻는다. 입사 첫 주에 받는 것도 대개 JetPack 설치 문서다
> **Don 기준 난이도**: 펌웨어 이미지 = "서명된 바이너리 덩어리를 통째로 굽는다"는 감각과 A/B 슬롯 업데이트는 이미 안다 / 패키지 매니저(dpkg/apt)가 장비 위에서 파일을 하나씩 바꾸는 모델, 배포판 보안 업데이트 흐름, chroot·binfmt 같은 조립 도구가 새로 배울 부분
> **선행 노트**: C7(initramfs·switch_root), J2(ext4), J3(read-only root·overlayfs), K1(rootfs 구조) — K3(Buildroot)·K4/K5(Yocto)와 나란히 읽으면 비교가 선명해진다. 라이선스 의무는 K7

---

## 0. 큰 그림 — 이게 왜 필요한가

SSD 펌웨어를 만들 때 Don은 모든 코드를 직접 컴파일했다. 부트로더, FTL, NVMe 프런트엔드, 심지어 `memcpy`까지 회사 빌드 서버에서 나왔고, 결과물은 서명된 이미지 하나였다. 이 세계에는 "남이 미리 컴파일해 둔 라이브러리를 받아서 깐다"는 개념이 거의 없다.

Linux 장비는 다르다. 필요한 유저스페이스(셸, libc, systemd, Python, OpenCV, ROS 2, CUDA 런타임…)가 수천 개 프로젝트에 흩어져 있다. 이것을 모으는 방법이 크게 둘이다.

1. **소스에서 빌드 (build-from-source)** — Buildroot(K3), Yocto/OpenEmbedded(K4·K5). 모든 패키지의 소스를 받아 내 빌드 머신에서 내 툴체인으로 크로스 컴파일한다. SSD 펌웨어 방식과 비슷하다.
2. **바이너리 조립 (assemble-from-binaries)** — Debian, Ubuntu, Fedora IoT 같은 일반 배포판. 배포판이 이미 수만 개 패키지를 aarch64용으로 컴파일·서명해서 저장소에 올려 두었고, 나는 그중 필요한 것을 **골라서 풀기만** 한다.

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="170" y="22" text-anchor="middle" font-size="13" font-weight="bold">소스에서 빌드 (Buildroot · Yocto)</text>
  <text x="510" y="22" text-anchor="middle" font-size="13" font-weight="bold">바이너리 조립 (Debian · Ubuntu)</text>
  <rect x="20" y="36" width="300" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="56" text-anchor="middle" font-size="13">업스트림 소스 tarball + 패치</text>
  <text x="170" y="73" text-anchor="middle" font-size="12">recipe · package .mk 가 버전 고정</text>
  <line x1="170" y1="80" x2="170" y2="104" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="20" y="106" width="300" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="126" text-anchor="middle" font-size="13">내 빌드 머신에서 크로스 컴파일</text>
  <text x="170" y="143" text-anchor="middle" font-size="12">수십 분 ~ 수 시간, 수십 GB 디스크</text>
  <line x1="170" y1="150" x2="170" y2="174" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="20" y="176" width="300" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="170" y="196" text-anchor="middle" font-size="13">이미지 = 내가 고른 것만</text>
  <text x="170" y="213" text-anchor="middle" font-size="12">수 MB ~ 수백 MB, 패키지 매니저 없음</text>
  <rect x="360" y="36" width="300" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="510" y="56" text-anchor="middle" font-size="13">배포판이 미리 빌드한 .deb 수만 개</text>
  <text x="510" y="73" text-anchor="middle" font-size="12">서명된 저장소 (ports.ubuntu.com 등)</text>
  <line x1="510" y1="80" x2="510" y2="104" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="360" y="106" width="300" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="510" y="126" text-anchor="middle" font-size="13">고르고 풀기 (debootstrap · apt)</text>
  <text x="510" y="143" text-anchor="middle" font-size="12">수 분, 컴파일 없음</text>
  <line x1="510" y1="150" x2="510" y2="174" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="360" y="176" width="300" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="510" y="196" text-anchor="middle" font-size="13">이미지 = 배포판 기본값 포함</text>
  <text x="510" y="213" text-anchor="middle" font-size="12">수백 MB ~ 수 GB, apt · dpkg DB 포함</text>
  <line x1="170" y1="220" x2="300" y2="262" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="510" y1="220" x2="380" y2="262" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="140" y="264" width="400" height="58" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="287" text-anchor="middle" font-size="13" font-weight="bold">어느 쪽이든 BSP는 따로: 커널 + 부트로더 + DT</text>
  <text x="340" y="308" text-anchor="middle" font-size="12">SoC 벤더 (NXP · TI · NVIDIA) 가 준다 → C, D, E 모듈</text>
</svg>
```
그림 1 — 두 철학. 위쪽 두 단계가 다르고, 맨 아래 BSP(커널·부트로더·Device Tree)는 어느 방식이든 보드 벤더에게서 온다.

bare-metal 비유로 말하면: 소스 빌드는 "모든 라이브러리를 소스로 받아 내 컴파일러 플래그로 빌드하고 링커 스크립트까지 내가 쓴다", 바이너리 조립은 "벤더가 준 precompiled `.a`/`.so`를 받아서 링크만 한다"에 가깝다. 후자는 빠르지만 그 바이너리가 무엇으로, 어떤 옵션으로 빌드됐는지는 벤더(배포판)가 정한다.

이 노트에서 실제로 한 일(전부 Lima VM `elinux`에서, `uname -r` = `7.0.0-38-generic`, 작업 폴더 `/var/tmp/K6/`):

- Ubuntu Base 24.04.5 arm64 타르볼(약 29 MB)을 받아 SHA256 + GPG 서명을 검증했다.
- 풀어서 크기·패키지 수·`/etc/os-release`를 봤다 (96 MiB, 91개 패키지).
- chroot 안에서 apt로 systemd·udev·dbus·iproute2·python3를 단계별로 깔면서 크기를 쟀다 (96 → 182 MiB).
- `mkfs.ext4 -d`로 이미지를 만들어 랩 커널(`/var/lab/out/Image`, 6.18.55)로 QEMU에서 **systemd로 부팅**했고, `systemd-analyze`로 잰 시간을 BusyBox initramfs와 비교했다.
- 같은 이미지를 read-only root로 부팅해서 무엇이 깨지는지(logind 실패) 보고, tmpfs 세 줄로 고쳤다.
- 아키텍처가 다른 rootfs(amd64)를 qemu-user + binfmt_misc로 chroot해서 "x86 PC에서 arm64 rootfs 만들기" 트릭을 반대 방향으로 확인했다.

Jetson L4T 관련 명령은 Jetson 하드웨어가 없어서 **NVIDIA 문서 기준 가이드(실행 안 함)** 로 표시한다.

---

## 1. 두 철학 — 무엇이 다른가

### 1.1 직관

같은 "센서 키트 이미지"를 만든다고 하자. 필요한 것: 커널, systemd(또는 BusyBox init), 네트워크 도구, Python 3, 카메라 앱.

- **Buildroot**로는 `make menuconfig`에서 BusyBox·Python·앱 패키지를 켜고 `make` 한 번. 툴체인부터 rootfs까지 전부 컴파일된다. 결과는 수십 MB짜리 `rootfs.ext4` 하나. 장비 위에 패키지 매니저는 없다.
- **Yocto**로는 `local.conf`와 레이어를 정하고 `bitbake core-image-minimal`. 내부적으로 레시피마다 소스를 받아 컴파일하고, 결과물을 `.ipk`/`.rpm`/`.deb` 패키지로 만든 다음 그 패키지로 이미지를 조립한다. (그래서 Yocto는 "소스 빌드 + 내부 패키지 조립"의 혼합이다.)
- **Ubuntu**로는 Ubuntu Base(또는 debootstrap)로 최소 rootfs를 깔고 `apt install systemd python3 …`. 컴파일은 0번. 앱만 내가 빌드해서 `.deb`로 만들거나 복사한다.

### 1.2 정의

| 구분 | 소스 빌드 (Buildroot · Yocto) | 바이너리 조립 (Debian · Ubuntu · Fedora IoT) |
|---|---|---|
| 패키지를 누가 컴파일하나 | 나 (내 빌드 머신, 내 툴체인) | 배포판 빌드 팜 (Launchpad, Debian buildd, Koji) |
| 컴파일 옵션 | 내가 정한다 (`-mcpu`, 기능 on/off) | 배포판 기본값 (aarch64 generic, 기능 대부분 on) |
| 장비 위 패키지 매니저 | 보통 없음 (Yocto는 선택적으로 opkg/rpm/dpkg) | dpkg + apt (또는 rpm-ostree) 기본 |
| 첫 이미지까지 시간 | Buildroot 수십 분, Yocto 수 시간 (첫 빌드) | 수 분 |
| 크기 | 수 MB ~ 수백 MB | 수백 MB ~ 수 GB |
| 보안 업데이트 | 내가 레시피 버전을 올리고 다시 빌드 | 배포판이 패치한 패키지를 받는다 |
| 대표 SoC 생태계 | NXP i.MX, TI, ST, Microchip, Qualcomm 일부 | NVIDIA Jetson, Raspberry Pi, x86 산업용 PC |

### 1.3 "바이너리 조립" 쪽의 가족

- **Debian**: 모든 것의 원조. `debootstrap`이 Debian에서 나왔다. arm64·armhf·riscv64 공식 지원.
- **Ubuntu**: Debian 기반, 6개월 릴리스 + 2년마다 LTS. arm64는 `ports.ubuntu.com`에서 배포. Jetson과 Raspberry Pi 공식 이미지가 있다.
- **Raspberry Pi OS**: Debian 기반. 공식 이미지를 만드는 스크립트가 [pi-gen](https://github.com/RPi-Distro/pi-gen)이다 (debootstrap + 단계별 셸 스크립트).
- **Fedora IoT**: rpm 패키지지만 장비 위에서는 `rpm-ostree`로 **이미지 단위(atomic)** 업데이트를 한다. "바이너리 조립 + 이미지 기반 업데이트"의 예.
- **Ubuntu Core**: 전부 snap으로 된 Ubuntu. 역시 이미지/트랜잭션 업데이트 모델.

### 1.4 흔한 함정

- "Yocto는 소스 빌드니까 패키지 매니저가 없다" — 틀렸다. Yocto는 내부적으로 패키지를 만든다(`PACKAGE_CLASSES`). 다만 장비 위에 매니저를 **넣을지**는 선택이다.
- "Ubuntu는 크니까 임베디드에 못 쓴다" — Jetson Orin처럼 eMMC 64 GB, RAM 8~64 GB인 장비에서는 크기가 결정 요인이 아니다. 문제는 크기보다 **업데이트 모델과 재현성**이다 (5절).

---

## 2. Ubuntu Base로 rootfs 조립하기 — 실습

Ubuntu Base는 Canonical이 공식으로 배포하는 **최소 rootfs 타르볼**이다. 커널도, init도, systemd도 없다. debootstrap의 `minbase`를 미리 돌려 묶어 둔 것과 비슷하다. Docker의 `ubuntu:24.04` 이미지도 같은 계열이다.

### 2.1 받고 검증하기

무엇을 확인하나: 공식 위치에서 받은 타르볼이 변조되지 않았는지 SHA256과 GPG 서명 두 단계로 확인한다.

```sh
mkdir -p /var/tmp/K6/dl && cd /var/tmp/K6/dl
U=https://cdimage.ubuntu.com/ubuntu-base/releases/24.04.5/release
curl -sSfO $U/ubuntu-base-24.04.5-base-arm64.tar.gz
curl -sSfO $U/SHA256SUMS
curl -sSfO $U/SHA256SUMS.gpg
ls -l
grep arm64.tar.gz SHA256SUMS | sha256sum -c --ignore-missing
gpgv --keyring /usr/share/keyrings/ubuntu-archive-keyring.gpg SHA256SUMS.gpg SHA256SUMS
```

```text
total 29240
-rw-rw-r-- 1 donh donh     1884 Oct 17 17:37 SHA256SUMS
-rw-rw-r-- 1 donh donh 29936675 Oct 17 17:37 ubuntu-base-24.04.5-base-arm64.tar.gz
ubuntu-base-24.04.5-base-arm64.tar.gz: OK
gpgv: Signature made Thu Sep 10 09:44:43 2026 PDT
gpgv:                using RSA key 843938DF228D22F7B3742BC0D94AA3F0EFE21092
gpgv: Good signature from "Ubuntu CD Image Automatic Signing Key (2012) <cdimage@ubuntu.com>"
```

출력에서 볼 것: 29.9 MB 타르볼, SHA256 `OK`, 그리고 서명은 Ubuntu CD 이미지 서명 키로 확인됐다. (`ls -l`은 `.gpg`를 받기 전에 찍은 것이라 두 파일만 보인다.) `SHA256SUMS`만 맞추면 "다운로드가 깨지지 않았다"까지만 보장된다. **서명까지 확인해야** "Canonical이 만든 것"이 보장된다 — 펌웨어 이미지의 CRC vs 서명 검증과 같은 구분이다.

> 참고: 디렉터리 목록에서 `releases/` 아래에 24.04.3 ~ 26.10까지 있었다. 이 노트는 현재 많이 쓰이는 LTS인 **24.04.5**(noble)를 골랐다. Jetson JetPack 6이 22.04(jammy) 기반이라는 점과 비교해 보라 (6절).

### 2.2 풀어서 들여다보기

무엇을 확인하나: 타르볼을 root 소유권 그대로 풀고(`sudo tar -xp`), 크기·디렉터리·OS 정보·패키지 수를 본다.

```sh
R=/var/tmp/K6/k6_root
sudo mkdir -p $R && sudo tar -xpf /var/tmp/K6/dl/ubuntu-base-24.04.5-base-arm64.tar.gz -C $R
sudo du -sh $R
cat $R/etc/os-release
ls $R
ls -l $R/bin $R/sbin $R/lib
sudo chroot $R dpkg-query -W -f='${Package}\n' | wc -l
ls $R/sbin/init $R/lib/systemd
sudo find $R -xdev -type f | wc -l
```

```text
106M	/var/tmp/K6/k6_root
PRETTY_NAME="Ubuntu 24.04.5 LTS"
NAME="Ubuntu"
VERSION_ID="24.04"
VERSION="24.04.5 LTS (Noble Numbat)"
VERSION_CODENAME=noble
ID=ubuntu
ID_LIKE=debian
...
bin
boot
dev
etc
home
lib
media
mnt
opt
proc
root
run
sbin
srv
sys
tmp
usr
var
lrwxrwxrwx 1 root root 7 Apr 22  2024 /var/tmp/K6/k6_root/bin -> usr/bin
lrwxrwxrwx 1 root root 7 Apr 22  2024 /var/tmp/K6/k6_root/lib -> usr/lib
lrwxrwxrwx 1 root root 8 Apr 22  2024 /var/tmp/K6/k6_root/sbin -> usr/sbin
91
ls: cannot access '/var/tmp/K6/k6_root/sbin/init': No such file or directory
/var/tmp/K6/k6_root/lib/systemd:
system
2564
```

출력에서 볼 것:

- 압축 29.9 MB → 펼치면 약 100 MB(`du -sb` = 100,787,686 바이트), 일반 파일 2,564개, 패키지 91개.
- `/bin`, `/sbin`, `/lib`가 `usr/…`를 가리키는 심볼릭 링크다. **usrmerge** — 요즘 Debian/Ubuntu는 모든 바이너리를 `/usr` 아래에 둔다. K1의 FHS 그림과 비교해 보라.
- `/sbin/init`이 없다. **init이 없는 rootfs**라서 이대로는 부팅할 수 없다. 컨테이너용이기 때문이다. 4절에서 systemd를 깔아 부팅 가능하게 만든다.
- 커널도 없다(`/boot`가 비어 있다). Ubuntu Base에서 커널은 별도 패키지(`linux-image-…`)다. 임베디드에서는 보통 BSP 커널을 쓰므로 일부러 안 깐다.

### 2.3 무엇이 이미 들어 있나 — Essential과 Priority

무엇을 확인하나: 91개 패키지가 왜 "최소"인지, 크기는 누가 먹는지.

```sh
R=/var/tmp/K6/k6_root
sudo chroot $R dpkg-query -W -f='${Installed-Size}\t${Package}\n' | sort -rn | head -8
sudo chroot $R dpkg-query -W -f='${Essential} ${Priority}\n' | sort | uniq -c
cat $R/etc/dpkg/dpkg.cfg.d/excludes | grep -v '^#' | grep .
```

```text
23352	libc6
9464	coreutils
8127	perl-base
6474	dpkg
6123	libssl3t64
5955	util-linux
4596	apt
3149	libstdc++6
      5 no important
     53 no optional
     10 no required
     23 yes required
path-exclude=/usr/share/man/*
path-exclude=/usr/share/locale/*/LC_MESSAGES/*.mo
path-exclude=/usr/share/doc/*
path-include=/usr/share/doc/*/copyright
path-include=/usr/share/doc/*/changelog.*
```

출력에서 볼 것:

- 크기 1위는 glibc(23 MB, `Installed-Size`는 KiB 단위). BusyBox 기반 시스템이 musl이나 정적 BusyBox로 수 MB에 끝나는 이유가 여기서 보인다.
- `perl-base`가 8 MB나 된다. dpkg의 maintainer script 일부와 debconf가 Perl을 쓰기 때문에 Debian 계열에서 Perl은 사실상 필수다.
- **Essential: yes** 23개 — dpkg가 "이 패키지들은 항상 있다고 가정해도 된다"고 약속한 집합(bash, coreutils, dpkg …). 다른 패키지는 이들을 `Depends`에 적지 않는다.
- `/etc/dpkg/dpkg.cfg.d/excludes` — Ubuntu Base는 이미 **man 페이지·번역·문서를 설치하지 않도록** 설정돼 있다. 단 `copyright` 파일은 남긴다(`path-include`). 라이선스 고지 의무 때문이다 (K7).

---

## 3. chroot — 다른 rootfs 안에서 명령 실행하기

### 3.1 직관과 정의

`chroot DIR CMD`는 "이 프로세스에게 `/`는 `DIR`이다"라고 커널에 말하는 것이다. 커널은 프로세스의 `fs_struct->root`를 바꾸고, 이후 경로 해석은 그 디렉터리에서 시작한다. 그 안의 `/usr/bin/apt-get`은 그 안의 `libc.so.6`을 로드한다. 즉 **rootfs를 부팅하지 않고도 그 rootfs의 프로그램을 돌릴 수 있다**. 커널은 호스트(VM) 커널 그대로 쓴다.

apt와 maintainer script는 `/proc`, `/sys`, `/dev`를 기대하므로 bind mount로 호스트 것을 보여 준다. 그리고 chroot 안에서 apt가 깐 서비스가 **호스트에서 시작되지 않도록** `policy-rc.d`(exit 101 = "서비스 시작 금지")를 넣는다.

무엇을 확인하나: chroot용 bind mount를 올리고 내리는 스크립트. 모든 마운트가 `/var/tmp/K6/k6_root` 아래에만 생긴다.

```sh
#!/bin/bash
# usage: k6_mount.sh up|down  (bind mounts for chroot, only under /var/tmp/K6/k6_root)
set -e
R=/var/tmp/K6/k6_root
case "$1" in
up)
  sudo mount -t proc proc $R/proc
  sudo mount --rbind /sys $R/sys && sudo mount --make-rslave $R/sys
  sudo mount --bind /dev $R/dev
  sudo mount --bind /dev/pts $R/dev/pts
  ;;
down)
  sudo umount $R/dev/pts || true
  sudo umount $R/dev || true
  sudo umount -R $R/sys || true
  sudo umount $R/proc || true
  ;;
esac
findmnt -R -o TARGET $R | sed 1d || true
```

```sh
S=/Users/donh/.elinux_scratch/K6; R=/var/tmp/K6/k6_root
sudo cp /etc/resolv.conf $R/etc/resolv.conf
printf '#!/bin/sh\nexit 101\n' | sudo tee $R/usr/sbin/policy-rc.d >/dev/null
sudo chmod +x $R/usr/sbin/policy-rc.d
$S/k6_mount.sh up
findmnt -o TARGET,FSTYPE | grep K6
```

```text
├─/var/tmp/K6/k6_root/proc                                 proc
├─/var/tmp/K6/k6_root/sys                                  sysfs
│ ├─/var/tmp/K6/k6_root/sys/kernel/security                securityfs
│ ├─/var/tmp/K6/k6_root/sys/fs/cgroup                      cgroup2
│ ├─/var/tmp/K6/k6_root/sys/fs/pstore                      pstore
│ ├─/var/tmp/K6/k6_root/sys/firmware/efi/efivars           efivarfs
│ ├─/var/tmp/K6/k6_root/sys/fs/bpf                         bpf
│ ├─/var/tmp/K6/k6_root/sys/kernel/config                  configfs
│ ├─/var/tmp/K6/k6_root/sys/kernel/tracing                 tracefs
│ ├─/var/tmp/K6/k6_root/sys/kernel/debug                   debugfs
│ └─/var/tmp/K6/k6_root/sys/fs/fuse/connections            fusectl
└─/var/tmp/K6/k6_root/dev                                  devtmpfs
  └─/var/tmp/K6/k6_root/dev/pts                            devpts
```

출력에서 볼 것: `--rbind /sys`라서 `/sys` 아래의 cgroup2·bpf·efivarfs 같은 하위 마운트까지 따라 들어왔다. 그래서 내릴 때는 `umount -R`이 필요하고, `--make-rslave`로 chroot 쪽 unmount가 호스트 마운트로 전파되지 않게 막았다. **이걸 안 하고 `umount -R`을 하면 호스트의 `/sys/fs/cgroup`까지 내려갈 수 있다** — chroot 실습의 대표적인 사고다.

### 3.2 apt update와 Recommends의 무게

무엇을 확인하나: 저장소 메타데이터를 받고, systemd를 "추천 패키지 포함/제외"로 깔 때 몇 개가 따라오는지 시뮬레이션(`-s`)한다.

```sh
R=/var/tmp/K6/k6_root
sudo chroot $R /bin/bash -c 'export DEBIAN_FRONTEND=noninteractive LC_ALL=C; time apt-get update -q' 2>&1 | tail -6
C="sudo chroot $R /usr/bin/env LC_ALL=C"
$C apt-get install -s systemd systemd-sysv udev | grep upgraded
$C apt-get install -s --no-install-recommends systemd systemd-sysv udev | grep upgraded
sudo du -sh $R/var/lib/apt/lists $R/var/cache/apt
```

```text
Fetched 37.4 MB in 4s (8519 kB/s)
Reading package lists...

real	0m5.135s
user	0m3.570s
sys	0m0.533s
3 upgraded, 54 newly installed, 0 to remove and 5 not upgraded.
2 upgraded, 12 newly installed, 0 to remove and 6 not upgraded.
205M	/var/tmp/K6/k6_root/var/lib/apt/lists
116M	/var/tmp/K6/k6_root/var/cache/apt
```

출력에서 볼 것:

- `apt update` 한 번에 37.4 MB를 받았고, 디스크에는 **lists 205 MB + 캐시 116 MB = 약 320 MB**가 생겼다. rootfs 본체(96 MB)의 세 배다. `universe` 컴포넌트의 `Packages` 파일 하나가 72 MB다. 장비 위에 apt를 두면 이 공간이 늘 필요하다.
- Recommends를 포함하면 54개, `--no-install-recommends`면 12개. **임베디드 이미지는 항상 `--no-install-recommends`** 가 출발점이다 (또는 `/etc/apt/apt.conf.d/`에 `APT::Install-Recommends "false";`).

---

## 4. 단계별로 깔면서 크기 재기

무엇을 확인하나: 새로 푼 Ubuntu Base에서 출발해 systemd → 네트워크 도구 → python3 순서로 깔고, 단계마다 rootfs 크기(apt 메타데이터 제외)·apt 메타데이터 크기·패키지 수를 잰다.

```sh
#!/bin/bash
# k6_measure.sh <label> -> rootfs bytes excluding apt lists/caches, + pkg count
R=/var/tmp/K6/k6_root
X="--exclude=./proc --exclude=./sys --exclude=./dev --exclude=./var/lib/apt/lists --exclude=./var/cache/apt"
B=$(cd $R && sudo du -sb $X . | cut -f1)
A=$(cd $R && sudo du -sb var/lib/apt/lists var/cache/apt | awk '{s+=$1} END{print s}')
N=$(sudo chroot $R dpkg-query -W -f='${db:Status-Abbrev}\n' | grep -c '^ii')
echo "$1 rootfs_MiB=$((B/1048576)) aptmeta_MiB=$((A/1048576)) pkgs=$N"
```

```sh
#!/bin/bash
# k6_build.sh: fresh Ubuntu Base -> staged apt installs inside chroot, measuring each stage
set -e
S=/Users/donh/.elinux_scratch/K6; R=/var/tmp/K6/k6_root
$S/k6_mount.sh down >/dev/null 2>&1 || true
sudo rm -rf --one-file-system $R && sudo mkdir -p $R
sudo tar -xpf /var/tmp/K6/dl/ubuntu-base-24.04.5-base-arm64.tar.gz -C $R
$S/k6_measure.sh stage0_base
sudo cp /etc/resolv.conf $R/etc/resolv.conf
printf '#!/bin/sh\nexit 101\n' | sudo tee $R/usr/sbin/policy-rc.d >/dev/null
sudo chmod +x $R/usr/sbin/policy-rc.d
$S/k6_mount.sh up >/dev/null
C="sudo chroot $R /usr/bin/env DEBIAN_FRONTEND=noninteractive LC_ALL=C"
$C apt-get update -qq
$S/k6_measure.sh stage1_aptupdate
st() { local L=$1; shift; local t0=$(date +%s.%N)
  $C apt-get install -y -qq --no-install-recommends "$@" >/var/tmp/K6/k6_$L.log 2>&1
  echo "  ($(echo "$(date +%s.%N) - $t0" | bc) s)"; $S/k6_measure.sh $L; }
st stage2_systemd systemd systemd-sysv udev
st stage3_net dbus kmod iproute2 iputils-ping
st stage4_python3 python3
$C apt-get clean; sudo rm -rf $R/var/lib/apt/lists/*
$S/k6_measure.sh stage5_cleaned
```

```text
stage0_base rootfs_MiB=96 aptmeta_MiB=0 pkgs=91
stage1_aptupdate rootfs_MiB=96 aptmeta_MiB=320 pkgs=91
  (2.416474351 s)
stage2_systemd rootfs_MiB=139 aptmeta_MiB=328 pkgs=103
  (1.767319370 s)
stage3_net rootfs_MiB=146 aptmeta_MiB=330 pkgs=119
  (3.036522569 s)
stage4_python3 rootfs_MiB=182 aptmeta_MiB=337 pkgs=132
stage5_cleaned rootfs_MiB=182 aptmeta_MiB=0 pkgs=132
```

출력에서 볼 것: 각 설치는 **2~3초**다(다운로드 포함, 컴파일 0). Buildroot에서 systemd + Python을 켜고 처음부터 빌드하면 수십 분이 걸리는 것과 대조된다. 이 단계에서 새로 풀린 패키지는 각 로그의 `Unpacking` 줄에서 뽑았다:

```sh
cd /var/tmp/K6; for f in k6_stage2_systemd.log k6_stage3_net.log k6_stage4_python3.log; do
  echo "== $f"; grep -oE "^Unpacking [^ ]+" $f | awk '{print $2}' | tr '\n' ' '; echo; done
```

```text
== k6_stage2_systemd.log
libapparmor1:arm64 libargon2-1:arm64 libudev1:arm64 libdevmapper1.02.1:arm64 libjson-c5:arm64 libcryptsetup12:arm64 libfdisk1:arm64 libkmod2:arm64 libsystemd-shared:arm64 libsystemd0:arm64 systemd-dev systemd systemd-sysv udev 
== k6_stage3_net.log
adduser libdbus-1-3:arm64 dbus-bin dbus-session-bus-common libexpat1:arm64 dbus-daemon dbus-system-bus-common dbus libelf1t64:arm64 libbpf1:arm64 libmnl0:arm64 libxtables12:arm64 libcap2-bin iproute2 iputils-ping kmod 
== k6_stage4_python3.log
libpython3.12-minimal:arm64 python3.12-minimal python3-minimal media-types netbase tzdata readline-common libreadline8t64:arm64 libsqlite3-0:arm64 libpython3.12-stdlib:arm64 python3.12 libpython3-stdlib:arm64 python3 
```

`systemd`를 달라고 했는데 `libcryptsetup12`, `libdevmapper`, `libapparmor1`이 따라온다. 배포판 systemd는 **모든 기능을 켜고 빌드**됐기 때문이다(부팅 로그의 `+PAM +AUDIT +SELINUX +APPARMOR … +TPM2` 문자열, 5절). Yocto라면 `PACKAGECONFIG`로 cryptsetup 지원을 빼서 이 라이브러리 자체를 없앨 수 있다. 바이너리 배포판에서는 "패키지 단위"로만 고를 수 있고 "기능 단위"로는 못 고른다 — 두 철학의 핵심 차이다.

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
<text x="340" y="20" text-anchor="middle" font-size="13" font-weight="bold">Ubuntu Base 24.04.5 arm64 — 단계별 크기 (MiB, 실측)</text>
<line x1="70" y1="300" x2="660" y2="300" stroke="currentColor"/>
<line x1="70" y1="300" x2="70" y2="55" stroke="currentColor"/>
<line x1="66" y1="300.0" x2="70" y2="300.0" stroke="currentColor"/>
<text x="62" y="304.0" text-anchor="end" font-size="12">0</text>
<line x1="66" y1="256.4" x2="70" y2="256.4" stroke="currentColor"/>
<text x="62" y="260.4" text-anchor="end" font-size="12">100</text>
<line x1="66" y1="212.7" x2="70" y2="212.7" stroke="currentColor"/>
<text x="62" y="216.7" text-anchor="end" font-size="12">200</text>
<line x1="66" y1="169.1" x2="70" y2="169.1" stroke="currentColor"/>
<text x="62" y="173.1" text-anchor="end" font-size="12">300</text>
<line x1="66" y1="125.5" x2="70" y2="125.5" stroke="currentColor"/>
<text x="62" y="129.5" text-anchor="end" font-size="12">400</text>
<line x1="66" y1="81.8" x2="70" y2="81.8" stroke="currentColor"/>
<text x="62" y="85.8" text-anchor="end" font-size="12">500</text>
<rect x="89.2" y="258.1" width="60" height="41.9" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="119.2" y="283.1" text-anchor="middle" font-size="12">96</text>
<text x="119.2" y="318" text-anchor="middle" font-size="12">base</text>
<rect x="187.5" y="258.1" width="60" height="41.9" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="217.5" y="283.1" text-anchor="middle" font-size="12">96</text>
<rect x="187.5" y="118.5" width="60" height="139.6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="217.5" y="192.3" text-anchor="middle" font-size="12">+320</text>
<text x="217.5" y="318" text-anchor="middle" font-size="12">apt update</text>
<rect x="285.8" y="239.3" width="60" height="60.7" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="315.8" y="273.7" text-anchor="middle" font-size="12">139</text>
<rect x="285.8" y="96.2" width="60" height="143.1" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="315.8" y="171.8" text-anchor="middle" font-size="12">+328</text>
<text x="315.8" y="318" text-anchor="middle" font-size="12">+systemd/udev</text>
<rect x="384.2" y="236.3" width="60" height="63.7" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="414.2" y="272.1" text-anchor="middle" font-size="12">146</text>
<rect x="384.2" y="92.3" width="60" height="144.0" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="414.2" y="168.3" text-anchor="middle" font-size="12">+330</text>
<text x="414.2" y="318" text-anchor="middle" font-size="12">+dbus/ip</text>
<rect x="482.5" y="220.6" width="60" height="79.4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="512.5" y="264.3" text-anchor="middle" font-size="12">182</text>
<rect x="482.5" y="73.5" width="60" height="147.1" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="512.5" y="151.1" text-anchor="middle" font-size="12">+337</text>
<text x="512.5" y="318" text-anchor="middle" font-size="12">+python3</text>
<rect x="580.8" y="220.6" width="60" height="79.4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="610.8" y="264.3" text-anchor="middle" font-size="12">182</text>
<text x="610.8" y="318" text-anchor="middle" font-size="12">apt clean</text>
<rect x="90" y="40" width="14" height="12" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="110" y="51" font-size="12">rootfs (설치된 파일)</text>
<rect x="90" y="60" width="14" height="12" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="110" y="71" font-size="12">apt 메타데이터 (lists + cache)</text>
<text x="340" y="345" text-anchor="middle" font-size="12">패키지 수: 91 → 91 → 103 → 119 → 132 → 132 (dpkg-query, ii 상태)</text>
</svg>
```
그림 2 — 실측 크기 변화. 파랑은 설치된 파일, 주황은 `apt update`가 만든 메타데이터(목록·캐시). 마지막 단계에서 `apt-get clean` + lists 삭제로 주황이 사라진다. systemd+udev가 43 MiB, python3가 36 MiB를 더했다.

마지막으로 무엇이 큰지 다시 본다:

```sh
R=/var/tmp/K6/k6_root
sudo chroot $R dpkg-query -W -f='${Installed-Size}\t${Package}\n' | sort -rn | head -6
sudo du -sh $R/usr/share/doc $R/usr/share/man $R/usr/share/locale $R/usr/lib/udev/hwdb.bin $R/usr/lib/python3.12 $R/usr/lib/aarch64-linux-gnu $R/usr/lib/systemd
```

```text
23352	libc6
15436	systemd
11236	udev
10471	libpython3.12-stdlib
9464	coreutils
8127	perl-base
3.4M	/var/tmp/K6/k6_root/usr/share/doc
300K	/var/tmp/K6/k6_root/usr/share/man
612K	/var/tmp/K6/k6_root/usr/share/locale
12M	/var/tmp/K6/k6_root/usr/lib/udev/hwdb.bin
29M	/var/tmp/K6/k6_root/usr/lib/python3.12
72M	/var/tmp/K6/k6_root/usr/lib/aarch64-linux-gnu
7.9M	/var/tmp/K6/k6_root/usr/lib/systemd
```

출력에서 볼 것: `udev` 패키지 자체(11 MB) 말고도, postinst가 설치 후에 **생성하는** 12 MB짜리 `hwdb.bin`(USB/PCI ID → 이름 데이터베이스)이 있다. dpkg의 `Installed-Size`에는 잡히지 않는 크기다. 센서 키트 장비에서 이게 필요한지는 의문이다. 바이너리 배포판에서 이런 파일을 빼려면 dpkg `path-exclude`나 이미지 생성 후 삭제 스크립트로 처리하는데, 그러면 "패키지 DB는 있다고 하는데 파일은 없는" 상태가 된다 — 이것이 7절 "제품용 하드닝"의 고민이다.

---

## 5. dpkg/apt 내부 — 패키지는 장비 위에서 어떻게 관리되나

### 5.1 직관

apt와 dpkg는 두 층이다.

- **dpkg**: 로컬 도구. `.deb` 하나를 풀고, "어느 파일이 어느 패키지 소유인지"를 `/var/lib/dpkg/`에 기록하고, maintainer script를 실행한다. 의존성은 **검사만** 하고 해결은 안 한다.
- **apt**: 네트워크 도구. 저장소 목록(`/etc/apt/sources.list.d/`)에서 `Packages` 인덱스를 받고, 의존성을 풀어서 필요한 `.deb`를 받아 dpkg에게 넘긴다.

펌웨어 비유: dpkg는 "슬롯에 이미지를 쓰고 매니페스트를 갱신하는 로컬 업데이터", apt는 "서버에서 매니페스트를 받아 무엇을 받을지 계산하는 OTA 클라이언트"다. 차이는 **파일 하나 단위**로 갱신한다는 점이다.

### 5.2 `.deb` 파일 해부

무엇을 확인하나: `.deb`는 `ar` 아카이브 안에 tar 두 개(control, data)가 든 구조라는 것. (VM의 저장소에서 `hello`를 **다운로드만** 했다 — VM 자체는 26.04라서 버전이 26.04용이다. 설치는 하지 않았다.)

```sh
mkdir -p /var/tmp/K6/deb && cd /var/tmp/K6/deb && apt-get download -q hello
ls -l *.deb
ar t hello_*.deb
dpkg-deb -I hello_*.deb
dpkg-deb -c hello_*.deb | head -8
```

```text
Fetched 25.6 kB in 1s (47.6 kB/s)
-rw-r--r-- 1 donh donh 25642 Feb  2  2026 hello_2.10-5build1_arm64.deb
debian-binary
control.tar.zst
data.tar.zst
 new Debian package, version 2.0.
 size 25642 bytes: control archive=857 bytes.
     842 bytes,    21 lines      control
     375 bytes,     6 lines      md5sums
 Package: hello
 Version: 2.10-5build1
 Architecture: arm64
 Maintainer: Ubuntu Developers <ubuntu-devel-discuss@lists.ubuntu.com>
 Original-Maintainer: Santiago Vila <sanvila@debian.org>
 Installed-Size: 144
 Depends: libc6 (>= 2.38)
 Conflicts: hello-traditional
 Breaks: hello-debhelper (<< 2.9)
 Replaces: hello-debhelper (<< 2.9), hello-traditional
 Section: devel
 Priority: optional
 Homepage: https://www.gnu.org/software/hello/
 Description: example package based on GNU hello
...
drwxr-xr-x root/root         0 2026-02-02 12:44 ./
drwxr-xr-x root/root         0 2026-02-02 12:44 ./usr/
drwxr-xr-x root/root         0 2026-02-02 12:44 ./usr/bin/
-rwxr-xr-x root/root     67872 2026-02-02 12:44 ./usr/bin/hello
drwxr-xr-x root/root         0 2026-02-02 12:44 ./usr/share/
drwxr-xr-x root/root         0 2026-02-02 12:44 ./usr/share/doc/
drwxr-xr-x root/root         0 2026-02-02 12:44 ./usr/share/doc/hello/
NEWS.gz 이하 생략
```

출력에서 볼 것: `ar t`로 보이는 세 멤버 — `debian-binary`(포맷 버전 "2.0"), `control.tar.zst`(메타데이터 + 스크립트), `data.tar.zst`(실제로 `/`에 풀릴 파일). `control`의 `Depends: libc6 (>= 2.38)`이 apt 의존성 해결의 입력이다. `Description`은 길어서 `...`로 줄였다.

### 5.3 설치된 뒤 — dpkg 데이터베이스

무엇을 확인하나: chroot 안에서 udev 패키지의 상태, 파일 목록, 소유 관계, maintainer script를 본다.

```sh
R=/var/tmp/K6/k6_root; C="sudo chroot $R /usr/bin/env LC_ALL=C"
$C dpkg-query -s udev | grep -E '^(Package|Status|Priority|Installed-Size|Architecture|Source|Version|Depends|Breaks):'
$C dpkg -L udev | wc -l
$C dpkg -L udev | grep -E 'bin/|\.service$|rules.d/[0-9]+-' | head -8
ls $R/var/lib/dpkg/info/ | grep '^udev'
$C dpkg -S /usr/bin/ls /usr/lib/systemd/systemd
```

```text
Package: udev
Status: install ok installed
Priority: important
Installed-Size: 11236
Architecture: arm64
Source: systemd
Version: 255.4-1ubuntu8.18
Depends: libacl1 (>= 2.2.23), libblkid1 (>= 2.37.2), libc6 (>= 2.38), libcap2 (>= 1:2.10), libkmod2 (>= 15), libselinux1 (>= 3.1~), systemd | systemd-standalone-sysusers | systemd-sysusers, libudev1 (= 255.4-1ubuntu8.18), systemd-dev (>= 255.4-1ubuntu8.18)
Breaks: systemd (<< 255.4-1ubuntu8.18)
165
/usr/bin/systemd-hwdb
/usr/bin/udevadm
/usr/lib/systemd/system/systemd-hwdb-update.service
/usr/lib/systemd/system/systemd-udev-settle.service
/usr/lib/systemd/system/systemd-udev-trigger.service
/usr/lib/systemd/system/systemd-udevd.service
/usr/lib/udev/rules.d/40-vm-hotadd.rules
/usr/lib/udev/rules.d/50-firmware.rules
udev.conffiles
udev.list
udev.md5sums
udev.postinst
udev.postrm
udev.preinst
udev.prerm
udev.triggers
coreutils: /usr/bin/ls
systemd: /usr/lib/systemd/systemd
```

출력에서 볼 것:

- `Source: systemd` — udev는 systemd 소스 패키지 하나에서 나온 여러 바이너리 패키지 중 하나다. CVE 공지는 보통 **소스 패키지 이름**(systemd)으로 나온다.
- `/var/lib/dpkg/info/udev.*` — `.list`(소유 파일 165개), `.md5sums`(무결성 검사용, `dpkg --verify`가 쓴다), `.conffiles`(사용자 수정을 보존할 설정 파일), 그리고 **maintainer script** 네 개(`preinst`, `postinst`, `prerm`, `postrm`)와 `triggers`.
- `dpkg -S`는 역방향 조회: 파일 → 패키지. 디버깅할 때 "이 `.so`는 어디서 왔지?"에 쓴다.

maintainer script는 패키지 설치·업그레이드 때 **root로 실행되는 셸 스크립트**다. udev의 postinst 앞부분:

```sh
head -22 /var/tmp/K6/k6_root/var/lib/dpkg/info/udev.postinst
```

```text
#!/bin/sh

set -e

update_hwdb() {
    systemd-hwdb --usr update || true
}

case "$1" in
    configure)
        # update/create hwdb before we (re)start udev
        update_hwdb

        if dpkg --compare-versions "$2" lt-nl "254.3-1~" && [ ! -f /etc/init.d/udev ]; then
            update-rc.d udev remove || true
        fi
    ;;

    triggered)
        update_hwdb
        exit 0
    ;;
esac
```

출력에서 볼 것: `configure` 단계에서 `systemd-hwdb --usr update`로 위에서 본 12 MB `hwdb.bin`을 **설치 시점에 생성**한다. 즉 바이너리 배포판 이미지는 "파일을 푼 것"만이 아니라 "**스크립트를 실행한 결과**"까지 포함한다. 이것이 두 가지 결과를 낳는다.

1. 이미지를 만들 때 maintainer script가 **타깃 아키텍처에서** 돌아야 한다. x86 PC에서 arm64 이미지를 만들면 스크립트가 arm64 바이너리를 실행하므로 qemu-user가 필요하다 (8절).
2. 재현성: 스크립트가 시간·랜덤·호스트 상태에 의존하면 같은 패키지로 만든 이미지도 바이트 단위로 달라진다 (아래 "machine-id" 함정이 대표 예).

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="30" width="200" height="140" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="120" y="52" text-anchor="middle" font-size="13" font-weight="bold">hello_2.10_arm64.deb</text>
  <text x="120" y="70" text-anchor="middle" font-size="12">(ar 아카이브)</text>
  <rect x="35" y="80" width="170" height="24" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="120" y="97" text-anchor="middle" font-size="12">debian-binary "2.0"</text>
  <rect x="35" y="110" width="170" height="24" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="120" y="127" text-anchor="middle" font-size="12">control.tar.zst</text>
  <rect x="35" y="140" width="170" height="24" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="120" y="157" text-anchor="middle" font-size="12">data.tar.zst</text>
  <rect x="270" y="40" width="130" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="335" y="65" text-anchor="middle" font-size="13" font-weight="bold">dpkg -i</text>
  <line x1="205" y1="122" x2="268" y2="70" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="205" y1="152" x2="268" y2="76" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="440" y="20" width="220" height="170" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="550" y="42" text-anchor="middle" font-size="13" font-weight="bold">/var/lib/dpkg/</text>
  <text x="455" y="66" font-size="12">status — 전체 패키지 상태 DB</text>
  <text x="455" y="88" font-size="12">info/P.list — 소유 파일</text>
  <text x="455" y="110" font-size="12">info/P.md5sums — 검증</text>
  <text x="455" y="132" font-size="12">info/P.conffiles — 설정 보존</text>
  <text x="455" y="154" font-size="12">info/P.preinst · postinst</text>
  <text x="455" y="176" font-size="12">info/P.prerm · postrm</text>
  <line x1="400" y1="60" x2="438" y2="60" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="270" y="220" width="390" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="465" y="240" text-anchor="middle" font-size="13">/usr/bin/hello · /usr/share/doc/hello/copyright</text>
  <text x="465" y="257" text-anchor="middle" font-size="12">data.tar 내용이 / 에 풀린다</text>
  <line x1="335" y1="80" x2="335" y2="218" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="20" y="220" width="210" height="44" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="125" y="240" text-anchor="middle" font-size="13">postinst configure 실행</text>
  <text x="125" y="257" text-anchor="middle" font-size="12">root 권한 셸, 타깃 바이너리</text>
  <line x1="290" y1="80" x2="180" y2="218" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="340" y="300" text-anchor="middle" font-size="12">apt = 저장소 Packages 인덱스 + 의존성 해결 → .deb 다운로드 → dpkg 호출</text>
</svg>
```
그림 3 — `.deb`의 구조와 dpkg가 하는 일. 파일을 푸는 것과 동시에 DB를 갱신하고 maintainer script를 실행한다. `P`는 패키지 이름.

---

## 6. 이미지로 묶고 QEMU에서 systemd로 부팅하기

### 6.1 커널은 systemd를 받아 줄 준비가 됐나

systemd는 커널에 요구 사항이 많다. systemd 소스의 `README`에 "REQUIREMENTS" 목록이 있다(`DEVTMPFS`, `CGROUPS`, `INOTIFY_USER`, `SIGNALFD`, `TIMERFD`, `EPOLL`, `UNIX`, `SYSFS`, `PROC_FS`, `FHANDLE`, 권장으로 `AUTOFS_FS`, `TMPFS_POSIX_ACL`, `TMPFS_XATTR`, `SECCOMP` 등). BusyBox init은 이런 게 거의 필요 없다. 그래서 Buildroot/Yocto의 작은 커널 config로 Ubuntu rootfs를 부팅하려다 실패하는 일이 흔하다.

무엇을 확인하나: 랩 커널(`/var/lab/out/linux-build/.config`, arm64 defconfig 기반)이 위 목록을 만족하는지 검사하는 스크립트.

```sh
#!/bin/sh
# check a kernel .config against options systemd's README lists as required/recommended
CFG=${1:-/var/lab/out/linux-build/.config}
for o in DEVTMPFS CGROUPS INOTIFY_USER SIGNALFD TIMERFD EPOLL UNIX SYSFS PROC_FS FHANDLE \
         TMPFS_POSIX_ACL TMPFS_XATTR SECCOMP SECCOMP_FILTER AUTOFS_FS CGROUP_BPF BPF_SYSCALL \
         MEMCG NET_NS USER_NS PSI IPV6 SECURITY_NETWORK SECURITY_YAMA; do
  v=$(grep -E "^CONFIG_$o=" "$CFG" | cut -d= -f2)
  printf '%-18s %s\n' "$o" "${v:--}"
done
```

```sh
/Users/donh/.elinux_scratch/K6/k6_syscfg.sh | paste - - - | column -t
```

```text
DEVTMPFS    y  CGROUPS           y  INOTIFY_USER   y
SIGNALFD    y  TIMERFD           y  EPOLL          y
UNIX        y  SYSFS             y  PROC_FS        y
FHANDLE     y  TMPFS_POSIX_ACL   y  TMPFS_XATTR    y
SECCOMP     y  SECCOMP_FILTER    y  AUTOFS_FS      y
CGROUP_BPF  y  BPF_SYSCALL       y  MEMCG          y
NET_NS      y  USER_NS           y  PSI            -
IPV6        m  SECURITY_NETWORK  -  SECURITY_YAMA  -
```

출력에서 볼 것: arm64 defconfig는 데스크톱급이라 필수 항목이 전부 `y`다. 빠진 것은 `PSI`, `SECURITY_NETWORK`, `SECURITY_YAMA`(경고만 나온다), 그리고 `IPV6=m`인데 rootfs에 `/lib/modules/6.18.55`가 없어서 **모듈을 로드할 수 없다**. 이 셋이 부팅 로그에서 어떤 경고로 나타나는지 아래에서 확인한다. 참고로 `BINFMT_MISC`도 이 커널에는 꺼져 있다 (`# CONFIG_BINFMT_MISC is not set`).

### 6.2 rootfs를 부팅 가능하게 다듬고 ext4로 묶기

무엇을 확인하나: chroot용 임시 설정(policy-rc.d, resolv.conf)을 걷어 내고, hostname·fstab·빈 machine-id·root 비밀번호 없음·부팅 보고 서비스를 넣은 뒤 `mkfs.ext4 -d`로 디렉터리를 그대로 이미지에 담는다. `-d`는 루프 마운트 없이 디렉터리 내용을 그대로 ext4에 채우는 옵션이다(e2fsprogs 1.43+). root 소유 파일을 읽어야 해서 `sudo`로 돌렸다.

```sh
#!/bin/bash
# k6_mkimg.sh: configure rootfs for QEMU boot and pack it into ext4 image
set -e
S=/Users/donh/.elinux_scratch/K6; R=/var/tmp/K6/k6_root
$S/k6_mount.sh down >/dev/null 2>&1 || true
sudo rm -f $R/usr/sbin/policy-rc.d
sudo truncate -s0 $R/etc/resolv.conf
echo k6-ubuntu | sudo tee $R/etc/hostname >/dev/null
echo "/dev/vda / ext4 rw,relatime 0 1" | sudo tee $R/etc/fstab >/dev/null
sudo truncate -s0 $R/etc/machine-id
sudo chroot $R passwd -d root >/dev/null
sudo install -m755 $S/k6_report.sh $R/usr/local/sbin/k6_report.sh
sudo tee $R/etc/systemd/system/k6-report.service >/dev/null <<'U'
[Unit]
Description=K6 boot report
[Service]
Type=simple
ExecStart=/usr/local/sbin/k6_report.sh
StandardOutput=journal
[Install]
WantedBy=multi-user.target
U
sudo ln -sf /etc/systemd/system/k6-report.service $R/etc/systemd/system/multi-user.target.wants/k6-report.service
rm -f /var/tmp/K6/k6_ubuntu.ext4
sudo mkfs.ext4 -q -L k6root -d $R /var/tmp/K6/k6_ubuntu.ext4 600M
sudo chown $(id -u):$(id -g) /var/tmp/K6/k6_ubuntu.ext4
ls -ls /var/tmp/K6/k6_ubuntu.ext4
```

```text
186772 -rw-r--r-- 1 donh donh 629145600 Oct 17 17:40 /var/tmp/K6/k6_ubuntu.ext4
```

출력에서 볼 것: 600 MiB짜리 sparse 이미지에 실제로 할당된 블록은 186,772 KiB(약 182 MiB) — rootfs 크기와 같다.

게스트 안에서 돌릴 보고 스크립트. 부팅이 끝날 때까지 기다렸다가 `systemd-analyze`를 찍고 전원을 끈다. 결과는 파일로 남기고, VM 쪽에서 `debugfs`로 이미지를 마운트하지 않고 읽는다. (`Type=simple`이라 부팅 완료를 막지 않는다. 처음엔 `StandardOutput=tty`로 콘솔에 찍으려 했지만 serial getty가 같은 tty를 잡으면서 출력이 사라져서 파일 방식으로 바꿨다.)

```sh
#!/bin/sh
# runs in guest: wait for boot to finish, print systemd-analyze, power off
if touch /root/.k6w 2>/dev/null; then exec >/root/k6_report.txt 2>&1; else mount /dev/vdb /mnt && exec >/mnt/k6_report.txt 2>&1; fi
while systemctl is-system-running 2>/dev/null | grep -q -E 'starting|initializing'; do sleep 1; done
echo "K6_STATE=$(systemctl is-system-running)"
uname -r; head -1 /etc/os-release
systemd-analyze
systemd-analyze blame --no-pager | head -10
systemctl --failed --no-pager --no-legend
echo "K6_UNITS_ACTIVE=$(systemctl list-units --no-legend --state=active | wc -l)"
ps -e --no-headers | wc -l; free -m | head -2; df -h / | tail -1
journalctl -b -u systemd-logind --no-pager -o cat | head -8
journalctl -b -p warning --no-pager -o short-monotonic | head -30
echo K6_DONE
sync; umount /mnt 2>/dev/null; systemctl poweroff
```

(rw 부팅 때 쓴 버전은 첫 줄이 `exec >/root/k6_report.txt`뿐이었고 logind 줄이 없었다. 6.5절 read-only 실험에서 위처럼 확장했다.)

### 6.3 부팅

무엇을 확인하나: 랩 커널 + Ubuntu ext4를 virtio-blk로 붙이고 `init=/lib/systemd/systemd`로 부팅. initramfs 없이 커널이 직접 `/dev/vda`를 루트로 마운트한다 (ext4·virtio_blk가 built-in이라 가능 — C7 참고).

```sh
cd /var/tmp/K6
/usr/bin/time -f "WALL %e s" timeout 300 qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 1024 -nographic \
  -kernel /var/lab/out/Image \
  -drive if=none,id=d0,file=k6_ubuntu.ext4,format=raw -device virtio-blk-device,drive=d0 \
  -append "console=ttyAMA0 root=/dev/vda rw rootwait init=/lib/systemd/systemd" > k6_boot2.log 2>&1
tail -2 k6_boot2.log
debugfs -R "cat /root/k6_report.txt" k6_ubuntu.ext4 2>/dev/null
```

```text
[   20.663085] reboot: Power down
WALL 20.88 s
K6_STATE=running
6.18.55
PRETTY_NAME="Ubuntu 24.04.5 LTS"
Startup finished in 1.220s (kernel) + 11.521s (userspace) = 12.741s 
graphical.target reached after 11.232s in userspace.
9.267s dev-vda.device
6.786s systemd-udev-trigger.service
1.153s dev-hugepages.mount
1.130s dev-mqueue.mount
1.111s sys-kernel-debug.mount
 996ms systemd-logind.service
 942ms systemd-tmpfiles-setup.service
 936ms systemd-journald.service
 873ms modprobe@configfs.service
 859ms modprobe@dm_mod.service
K6_UNITS_ACTIVE=93
80
               total        used        free      shared  buff/cache   available
Mem:             963         112         833           0          83         851
/dev/root       574M  172M  361M  33% /
[    0.526447] k6-ubuntu kernel: pci-host-generic 4010000000.pcie: Memory resource size exceeds max for 32 bits
[    0.931864] k6-ubuntu kernel: check access for rdinit=/init failed: -2, ignoring
[    3.178189] k6-ubuntu systemd[1]: systemd-journald-dev-log.socket: SO_PASSSEC failed: Operation not supported
[    3.182271] k6-ubuntu systemd[1]: systemd-journald.socket: SO_PASSSEC failed: Operation not supported
[    3.183882] k6-ubuntu systemd[1]: systemd-journald.socket: SO_PASSSEC failed: Operation not supported
[    5.453077] k6-ubuntu systemd-sysctl[111]: Couldn't write '2' to 'net/ipv6/conf/all/use_tempaddr', ignoring: No such file or directory
[    5.472218] k6-ubuntu systemd-sysctl[111]: Couldn't write '2' to 'net/ipv6/conf/default/use_tempaddr', ignoring: No such file or directory
[    5.477452] k6-ubuntu systemd-sysctl[111]: Couldn't write '1' to 'kernel/yama/ptrace_scope', ignoring: No such file or directory
K6_DONE
```

**부팅됐다.** 상태 `running`(실패 유닛 0개), 활성 유닛 93개, 프로세스 80개, 사용 메모리 112 MB. 출력에서 볼 것:

- `Startup finished in 1.220s (kernel) + 11.521s (userspace)` — TCG 에뮬레이션이라 느리다. 실제 Cortex-A78AE(Orin)라면 수 초 안이다. 상대 비교로만 본다.
- `blame` 1위가 `dev-vda.device`(9.3 s)와 `systemd-udev-trigger`(6.8 s): **udev coldplug**(부팅 시점에 이미 있는 모든 장치에 대해 uevent를 다시 쏘고 규칙을 돌리는 것)가 가장 비싸다. BusyBox `mdev -s`나 devtmpfs만 쓰는 시스템에는 없는 비용이다.
- 경고 세 종류가 6.1절 config 검사와 정확히 맞는다: `SO_PASSSEC failed`(`SECURITY_NETWORK` 없음), `net/ipv6/...`(IPV6가 모듈인데 `/lib/modules` 없음), `kernel/yama/ptrace_scope`(`SECURITY_YAMA` 없음). 모두 무시되고 부팅은 계속된다.
- `df`의 `/dev/root 172M` — ext4 메타데이터를 포함한 사용량.

부팅 로그(콘솔)에서 의미 있는 줄 몇 개 (ANSI 색 코드 제거, 줄 번호는 로그 파일 기준):

```text
253:[    1.044874] Run /lib/systemd/systemd as init process
256:[    1.446796] systemd[1]: Detected architecture arm64.
258:Welcome to Ubuntu 24.04.5 LTS!
261:[    1.465550] systemd[1]: Initializing machine ID from D-Bus machine ID.
262:[    3.046157] systemd[1]: Queued start job for default target graphical.target.
275:[    3.157463] systemd[1]: proc-sys-fs-binfmt_misc.automount - Arbitrary Executable File Formats File System Automount Point was skipped because of an unmet condition check (ConditionPathExists=/proc/sys/fs/binfmt_misc).
317:[    3.424273] systemd[1]: kmod-static-nodes.service - Create List of Static Device Nodes was skipped because of an unmet condition check (ConditionFileNotEmpty=/lib/modules/6.18.55/modules.devname).
```

여기 숨은 **제품 함정**이 하나 있다. 261번 줄: "Initializing machine ID from **D-Bus machine ID**". `/etc/machine-id`는 비웠지만, chroot에서 dbus를 설치할 때 postinst가 `/var/lib/dbus/machine-id`를 이미 만들어 놓았다.

```sh
ls -l /var/tmp/K6/k6_root/var/lib/dbus/
```

```text
total 4
-rw-r--r-- 1 root root 33 Oct 17 17:38 machine-id
```

이 이미지를 공장에서 1,000대에 구우면 **1,000대가 같은 machine-id**를 갖는다. machine-id는 DHCP client ID, journald, 일부 클라우드 에이전트의 장비 식별에 쓰인다. 이미지 생성 마지막 단계에서 `/var/lib/dbus/machine-id`도 지우거나 `/etc/machine-id`로의 심볼릭 링크로 바꿔야 한다. maintainer script가 빌드 시점에 상태를 만든다는 5절의 이야기가 실제로 이렇게 터진다.

그 밖에: `graphical.target`이 기본 타깃이다(Ubuntu 기본값). 헤드리스 장비라면 `systemctl set-default multi-user.target`. `binfmt_misc.automount`는 커널에 `BINFMT_MISC`가 없어서 조건 불충족으로 건너뛰었다.

### 6.4 BusyBox initramfs와 비교

무엇을 확인하나: 같은 커널·같은 QEMU 옵션으로 랩의 BusyBox initramfs를 부팅해서 `/proc/uptime`이 찍히는 시점(= 첫 명령 실행 시점)을 잰다. 3번씩 반복.

```sh
cd /var/tmp/K6
for i in 1 2 3; do
  /usr/bin/time -f "busybox_wall %e" timeout 120 qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 1024 -nographic \
    -kernel /var/lab/out/Image -initrd /var/lab/out/initramfs.cpio.gz \
    -append "console=ttyAMA0 LAB_CMD=cat~/proc/uptime" > k6_bb$i.log 2>&1
  grep -E "Run /init|busybox_wall|^[0-9.]+ [0-9.]+" k6_bb$i.log | tr -d '\r'
done
for i in 1 2; do
  /usr/bin/time -f "systemd_wall %e" timeout 300 qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 1024 -nographic \
    -kernel /var/lab/out/Image -drive if=none,id=d0,file=k6_ubuntu.ext4,format=raw -device virtio-blk-device,drive=d0 \
    -append "console=ttyAMA0 root=/dev/vda rw rootwait init=/lib/systemd/systemd" > k6_boot_r$i.log 2>&1
  tail -1 k6_boot_r$i.log; debugfs -R "cat /root/k6_report.txt" k6_ubuntu.ext4 2>/dev/null | grep -E "Startup|STATE"
done
```

```text
[    1.280906] Run /init as init process
1.38 0.78
busybox_wall 1.72
[    1.045748] Run /init as init process
1.16 0.73
busybox_wall 1.40
[    1.085135] Run /init as init process
1.27 0.74
busybox_wall 1.53
systemd_wall 24.43
K6_STATE=running
Startup finished in 2.029s (kernel) + 13.748s (userspace) = 15.777s 
systemd_wall 18.24
K6_STATE=running
Startup finished in 746ms (kernel) + 9.399s (userspace) = 10.146s 
```

크기도 잰다:

```sh
R=/var/tmp/K6/k6_root; cd /var/tmp/K6
echo bb_rootfs=$(du -sb /var/lab/build/rootfs | cut -f1)
ls -l /var/lab/out/initramfs.cpio.gz | awk '{print $5}'
echo ub_tar_zst=$(sudo tar -C $R -cf - . | zstd -q -3 -T0 | wc -c)
echo ub_img_zst=$(zstd -q -3 -T0 -c k6_ubuntu.ext4 | wc -c)
```

```text
bb_rootfs=2248932
1210338
ub_tar_zst=50165304
ub_img_zst=50024020
```

출력에서 볼 것: BusyBox는 커널이 `/init`을 실행한 뒤 약 0.1~0.2 s 만에 첫 명령이 돈다(`/proc/uptime` 첫 값 1.16~1.38 s). Ubuntu + systemd는 userspace만 9.4~13.7 s(같은 이미지인데 실행마다 차이가 크다 — TCG와 VM 호스트 부하 영향). 크기는 펼친 상태 2.1 MiB vs 182 MiB(약 85배), 압축 1.2 MB vs 50 MB(zstd -3). OTA로 rootfs 전체를 보낸다면 이 50 MB가 한 번의 업데이트 크기다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
<text x="340" y="20" text-anchor="middle" font-size="13" font-weight="bold">부팅 시간 (초, QEMU TCG, 같은 Image 6.18.55, -smp 2 -m 1024)</text>
<line x1="210" y1="150" x2="630" y2="150" stroke="currentColor"/>
<line x1="210.0" y1="150" x2="210.0" y2="154" stroke="currentColor"/><text x="210.0" y="168" text-anchor="middle" font-size="12">0</text>
<line x1="315.0" y1="150" x2="315.0" y2="154" stroke="currentColor"/><text x="315.0" y="168" text-anchor="middle" font-size="12">4</text>
<line x1="420.0" y1="150" x2="420.0" y2="154" stroke="currentColor"/><text x="420.0" y="168" text-anchor="middle" font-size="12">8</text>
<line x1="525.0" y1="150" x2="525.0" y2="154" stroke="currentColor"/><text x="525.0" y="168" text-anchor="middle" font-size="12">12</text>
<line x1="630.0" y1="150" x2="630.0" y2="154" stroke="currentColor"/><text x="630.0" y="168" text-anchor="middle" font-size="12">16</text>
<text x="202" y="53" text-anchor="end" font-size="12">BusyBox initramfs</text>
<rect x="210" y="36" width="28.5" height="24" fill="#888" fill-opacity="0.12" stroke="#888"/>
<rect x="238.5" y="36" width="5.0" height="24" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="249.5" y="53" font-size="12">1.27 s</text>
<text x="202" y="91" text-anchor="end" font-size="12">Ubuntu + systemd (rw)</text>
<rect x="210" y="74" width="32.0" height="24" fill="#888" fill-opacity="0.12" stroke="#888"/>
<rect x="242.0" y="74" width="302.4" height="24" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="550.5" y="91" font-size="12">12.7 s</text>
<text x="202" y="129" text-anchor="end" font-size="12">Ubuntu + systemd (ro+tmpfs)</text>
<rect x="210" y="112" width="20.6" height="24" fill="#888" fill-opacity="0.12" stroke="#888"/>
<rect x="230.6" y="112" width="247.1" height="24" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="483.8" y="129" font-size="12">10.2 s</text>
<rect x="210" y="176" width="14" height="12" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="230" y="187" font-size="12">kernel</text>
<rect x="300" y="176" width="14" height="12" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="320" y="187" font-size="12">userspace (init → 목표 상태)</text>
<text x="340" y="222" text-anchor="middle" font-size="13" font-weight="bold">rootfs 크기 (MiB, 로그 스케일)</text>
<line x1="210" y1="340" x2="630" y2="340" stroke="currentColor"/>
<line x1="210.0" y1="340" x2="210.0" y2="344" stroke="currentColor"/><text x="210.0" y="358" text-anchor="middle" font-size="12">1</text>
<line x1="350.0" y1="340" x2="350.0" y2="344" stroke="currentColor"/><text x="350.0" y="358" text-anchor="middle" font-size="12">10</text>
<line x1="490.0" y1="340" x2="490.0" y2="344" stroke="currentColor"/><text x="490.0" y="358" text-anchor="middle" font-size="12">100</text>
<line x1="630.0" y1="340" x2="630.0" y2="344" stroke="currentColor"/><text x="630.0" y="358" text-anchor="middle" font-size="12">1000</text>
<text x="202" y="249" text-anchor="end" font-size="12">BusyBox 펼친 rootfs</text>
<rect x="210" y="234" width="46.3" height="20" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="262.3" y="249" font-size="12">2.1</text>
<text x="202" y="275" text-anchor="end" font-size="12">BusyBox cpio.gz</text>
<rect x="210" y="260" width="8.5" height="20" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="224.5" y="275" font-size="12">1.2</text>
<text x="202" y="301" text-anchor="end" font-size="12">Ubuntu 펼친 rootfs</text>
<rect x="210" y="286" width="316.4" height="20" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="532.4" y="301" font-size="12">182</text>
<text x="202" y="327" text-anchor="end" font-size="12">Ubuntu tar.zst</text>
<rect x="210" y="312" width="235.1" height="20" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="451.1" y="327" font-size="12">47.8</text>
<text x="340" y="385" text-anchor="middle" font-size="12">BusyBox: /proc/uptime 중앙값 · Ubuntu: systemd-analyze 대표 실행 (실행마다 10~16 s)</text>
</svg>
```
그림 4 — 실측 부팅 시간과 크기. 위: kernel(회색) + userspace(초록). BusyBox는 `Run /init` 시각(중앙값 1.085 s)과 첫 명령 시각(중앙값 1.27 s), Ubuntu는 `systemd-analyze` 값. 아래: 로그 스케일 크기(MiB). 숫자는 TCG 에뮬레이션 기준이라 절대값보다 비율을 본다.

### 6.5 read-only root로 부팅하면 무엇이 깨지나

제품에서는 rootfs를 읽기 전용으로 두는 것이 정석이다(J3). 배포판 rootfs는 "쓰기 가능한 `/`"를 가정하고 만들어졌다. 실제로 깨 보자.

무엇을 확인하나: fstab을 `ro`로 바꾼 이미지를 QEMU에서 `readonly=on`으로 붙이고, 보고서는 두 번째 디스크(`k6_log.ext4`, 16 MiB)에 쓴다.

```sh
R=/var/tmp/K6/k6_root; cd /var/tmp/K6
echo "/dev/vda / ext4 ro,relatime 0 1" | sudo tee $R/etc/fstab >/dev/null
sudo mkfs.ext4 -q -L k6ro -d $R k6_ubuntu_ro.ext4 600M; sudo chown $(id -u) k6_ubuntu_ro.ext4
echo "/dev/vda / ext4 rw,relatime 0 1" | sudo tee $R/etc/fstab >/dev/null
mkfs.ext4 -q k6_log.ext4 16M
timeout 300 qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 1024 -nographic -kernel /var/lab/out/Image \
  -drive if=none,id=d0,file=k6_ubuntu_ro.ext4,format=raw,readonly=on -device virtio-blk-device,drive=d0 \
  -drive if=none,id=d1,file=k6_log.ext4,format=raw -device virtio-blk-device,drive=d1 \
  -append "console=ttyAMA0 root=/dev/vda ro rootwait init=/lib/systemd/systemd" > k6_boot_ro.log 2>&1
grep -E "virtio_blk|VFS: Mounted|Kernel panic" <(sed 's/\x1b\[[0-9;]*m//g' k6_boot_ro.log) | grep -v Dquot
```

```text
[    0.448992] virtio_blk virtio0: 1/0/0 default/read/poll queues
[    0.451484] virtio_blk virtio0: [vda] 32768 512-byte logical blocks (16.8 MB/16.0 MiB)
[    0.461523] virtio_blk virtio1: 1/0/0 default/read/poll queues
[    0.462215] virtio_blk virtio1: [vdb] 1228800 512-byte logical blocks (629 MB/600 MiB)
[    0.622558] VFS: Mounted root (ext4 filesystem) readonly on device 254:0.
[    0.677693] Kernel panic - not syncing: Requested init /lib/systemd/systemd failed (error -2).
```

첫 시도는 **엉뚱한 이유로** 패닉했다. 16 MiB 로그 디스크가 `vda`, 600 MiB 루트가 `vdb`가 됐다. 이 실행에서 QEMU `virt` 머신은 명령줄에 **나중에 쓴** virtio-mmio 장치를 `virtio0`(→ `vda`)으로 잡았다. 즉 명령줄 순서와 `vdX` 순서가 반대였다. 커널은 빈 로그 디스크를 루트로 마운트했고, 거기에 `/lib/systemd/systemd`가 없으니 `error -2`(ENOENT)로 패닉했다. 실제 보드에서도 "eMMC와 SD 카드의 `mmcblkN` 번호가 바뀌어 엉뚱한 루트로 부팅" 같은 사고가 같은 원리다. 해결은 `root=PARTUUID=…`(파티션 테이블이 있을 때) 또는 initramfs에서 `LABEL`/`UUID`로 찾기.

`-drive` 순서를 바꿔(로그 디스크를 먼저 쓰면 루트가 `vda`) 다시 부팅:

```sh
timeout 300 qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 1024 -nographic -kernel /var/lab/out/Image \
  -drive if=none,id=d1,file=k6_log.ext4,format=raw -device virtio-blk-device,drive=d1 \
  -drive if=none,id=d0,file=k6_ubuntu_ro.ext4,format=raw,readonly=on -device virtio-blk-device,drive=d0 \
  -append "console=ttyAMA0 root=/dev/vda ro rootwait init=/lib/systemd/systemd" > k6_boot_ro.log 2>&1
tail -1 k6_boot_ro.log; debugfs -R "cat /k6_report.txt" k6_log.ext4 2>/dev/null
```

```text
[   70.748611] reboot: Power down
K6_STATE=degraded
6.18.55
PRETTY_NAME="Ubuntu 24.04.5 LTS"
Startup finished in 795ms (kernel) + 11.518s (userspace) = 12.314s 
graphical.target reached after 11.248s in userspace.
7.816s dev-vda.device
5.714s systemd-udev-trigger.service
...
● systemd-logind.service loaded failed failed User Login Management
K6_UNITS_ACTIVE=93
...
systemd-logind.service: Failed to set up special execution directory in /var/lib: Read-only file system
systemd-logind.service: Main process exited, code=exited, status=238/STATE_DIRECTORY
systemd-logind.service: Failed with result 'exit-code'.
...
[    3.325210] k6-ubuntu systemd-fsck[86]: fsck failed with exit status 8.
[    5.161736] k6-ubuntu systemd-tmpfiles[125]: Failed to create directory or subvolume "/tmp/.X11-unix": Read-only file system
[    5.178331] k6-ubuntu systemd-tmpfiles[125]: Setting access ACL "u::rwx,g::r-x,g:adm:r-x,m::r-x,o::r-x" on /var/log/journal failed: Read-only file system
[    5.417687] k6-ubuntu systemd-update-utmp[129]: Failed to write wtmp record, ignoring: Read-only file system
...
K6_DONE
Failed to set wall message, ignoring: Connection timed out
Call to PowerOff failed: Connection timed out
```

(`blame`·경고 목록 일부를 `...`로 줄였다. 원본은 logind 재시작 시도가 5번 반복된다.)

출력에서 볼 것:

- 상태가 `degraded`. **systemd-logind**가 `StateDirectory`(`/var/lib/systemd/linger`)를 만들지 못해 `status=238/STATE_DIRECTORY`로 죽었다.
- `/tmp`가 루트와 같은 파일시스템이라 tmpfiles가 소켓 디렉터리를 못 만들었다. `/var/log/journal`도 못 쓴다.
- logind가 없으니 `systemctl poweroff`가 logind에 보내는 D-Bus 호출(wall 메시지, PowerOff)이 차례로 타임아웃됐고(`Connection timed out` 두 줄), 결국 커널 시각 70 s에야 꺼졌다. 정상 부팅은 약 20 s였다. **read-only root 실험에서 "전원 끄기가 느려졌다"는 증상의 원인이 logind**라는 건 직접 보지 않으면 짐작하기 어렵다.
- `fsck failed with exit status 8`은 QEMU가 디스크를 `readonly=on`으로 줘서 fsck가 장치를 쓰기 모드로 못 연 것이다(무시됨).

고치는 법: 쓰기가 필요한 경로만 tmpfs로 덮는다. fstab 세 줄 추가.

```sh
printf "/dev/vda / ext4 ro,relatime 0 1\ntmpfs /tmp tmpfs mode=1777,nosuid,nodev 0 0\ntmpfs /var/lib/systemd tmpfs mode=0755 0 0\ntmpfs /var/log tmpfs mode=0755 0 0\n" | sudo tee $R/etc/fstab
sudo mkfs.ext4 -q -L k6ro -d $R k6_ubuntu_ro2.ext4 600M
# 위와 같은 QEMU 명령(파일만 k6_ubuntu_ro2.ext4)을 /usr/bin/time -f "WALL %e" 로 감싸 부팅
debugfs -R "cat /k6_report.txt" k6_log.ext4 2>/dev/null | grep -v SO_PASSSEC
```

```text
WALL 18.51
K6_STATE=running
6.18.55
PRETTY_NAME="Ubuntu 24.04.5 LTS"
Startup finished in 786ms (kernel) + 9.415s (userspace) = 10.201s 
graphical.target reached after 9.143s in userspace.
7.560s dev-vda.device
5.474s systemd-udev-trigger.service
1.049s systemd-logind.service
...
K6_UNITS_ACTIVE=97
82
/dev/root       574M  165M  368M  31% /
Starting systemd-logind.service - User Login Management...
New seat seat0.
Started systemd-logind.service - User Login Management.
Watching system buttons on /dev/input/event0 (gpio-keys)
...
K6_DONE
```

`running`으로 돌아왔고 poweroff도 정상(전체 18.5 s). 단 이것은 실험용 최소 수정이다. `/var/lib/systemd`를 tmpfs로 덮으면 `random-seed`, timer의 마지막 실행 시각 같은 상태가 재부팅마다 사라진다. 제품에서는 J3의 방식(`/etc`·`/var`에 overlayfs + 별도 데이터 파티션, 또는 systemd의 `systemd-volatile-root`/`tmpfiles.d`로 필요한 디렉터리만 지정)을 쓴다. 요점: **배포판 rootfs를 read-only로 만드는 일은 "fstab에 ro 한 단어"가 아니라 패키지 수백 개가 가정한 쓰기 경로를 찾아내는 작업**이다. Yocto는 `IMAGE_FEATURES += "read-only-rootfs"`가 이 작업을 빌드 시점에 해 준다(K4).

---

## 7. 보안 업데이트 · 장비 위 apt · 라이선스 — 실측으로 보기

### 7.1 타르볼은 받는 순간 이미 낡았다

무엇을 확인하나: 2026-09-10에 서명된 24.04.5 타르볼 + 우리가 깐 패키지 기준으로, 오늘(10-17) 저장소에 보안 업데이트가 몇 개 쌓였는지. 그리고 장비에서 apt를 지울 수 있는지, 라이선스 파일이 몇 개인지.

```sh
S=/Users/donh/.elinux_scratch/K6; R=/var/tmp/K6/k6_root
printf '#!/bin/sh\nexit 101\n' | sudo tee $R/usr/sbin/policy-rc.d >/dev/null; sudo chmod +x $R/usr/sbin/policy-rc.d
sudo cp /etc/resolv.conf $R/etc/resolv.conf; $S/k6_mount.sh up >/dev/null
C="sudo chroot $R /usr/bin/env LC_ALL=C DEBIAN_FRONTEND=noninteractive"
$C apt-get update -qq; $C apt list --upgradable 2>/dev/null
$C apt-get -s purge apt 2>&1 | tail -4
$C dpkg-query -W -f='${Package} ${Essential} ${Priority}\n' apt dpkg bash coreutils systemd
ls $R/usr/share/doc/*/copyright | wc -l
grep -l -E '^License: (GPL|LGPL)' $R/usr/share/doc/*/copyright | wc -l
$C apt-get clean; sudo rm -rf $R/var/lib/apt/lists/* $R/usr/sbin/policy-rc.d; sudo truncate -s0 $R/etc/resolv.conf
$S/k6_mount.sh down; findmnt | grep -c k6_ || true
```

```text
Listing...
libaudit-common/noble-updates 1:3.1.2-2.1ubuntu0.1 all [upgradable from: 1:3.1.2-2.1build1.1]
libaudit1/noble-updates 1:3.1.2-2.1ubuntu0.1 arm64 [upgradable from: 1:3.1.2-2.1build1.1]
libc-bin/noble-updates,noble-security 2.39-0ubuntu8.9 arm64 [upgradable from: 2.39-0ubuntu8.8]
libc6/noble-updates,noble-security 2.39-0ubuntu8.9 arm64 [upgradable from: 2.39-0ubuntu8.8]
libssl3t64/noble-updates,noble-security 3.0.13-0ubuntu3.16 arm64 [upgradable from: 3.0.13-0ubuntu3.15]
perl-base/noble-updates,noble-security 5.38.2-3.2ubuntu0.6 arm64 [upgradable from: 5.38.2-3.2ubuntu0.4]
This should NOT be done unless you know exactly what you are doing!
  apt
0 upgraded, 0 newly installed, 1 to remove and 6 not upgraded.
Purg apt [2.8.3]
apt no required
bash yes required
coreutils yes required
dpkg yes required
systemd no important
132
101
0
```

출력에서 볼 것:

- 6개 업그레이드 대기, 그중 **glibc(`libc6`)와 OpenSSL(`libssl3t64`)이 `noble-security`** 에 있다. 즉 보안 수정이 이미 나왔다. 바이너리 배포판의 장점이 이것이다 — Canonical 보안팀이 CVE를 추적하고 **같은 버전 계열(2.39)에 패치만 얹은** 패키지(`8.8 → 8.9`)를 내준다. 나는 받기만 하면 된다. Yocto라면 `cve-check` 클래스로 찾고, 레시피에 패치를 넣고, 다시 빌드하는 일을 내가(또는 Yocto 커뮤니티 stable 브랜치가) 해야 한다(K5).
- 반대 얼굴: **이미지를 만든 날짜에 따라 내용이 달라진다.** 오늘 만든 이미지와 지난달 만든 이미지는 같은 스크립트여도 libc 버전이 다르다. 재현하려면 저장소 스냅샷을 고정해야 한다 (8.3절).
- `apt`는 `Essential`이 아니라서 지울 수는 있다(apt가 경고만 한다). `dpkg`는 Essential이라 못 지운다. 제품 이미지에서 apt를 빼는 것은 흔한 선택이다 — 장비 위에서 패키지 단위 업데이트를 안 하겠다는 결정이기 때문이다.
- `copyright` 파일 132개 중 101개가 GPL/LGPL을 언급한다. Debian 계열은 패키지마다 라이선스 파일을 남기므로 고지 의무에 유리하지만, **GPL 소스 제공 의무**(대응 소스 `.dsc`·`.orig.tar` 보관)는 여전히 내 몫이다 (K7).
- 마지막 `0` — 모든 bind mount가 내려갔다.

### 7.2 장비 위 업데이트 모델 두 가지

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="170" y="22" text-anchor="middle" font-size="13" font-weight="bold">패키지 단위 (apt upgrade)</text>
  <text x="510" y="22" text-anchor="middle" font-size="13" font-weight="bold">이미지 단위 (A/B OTA)</text>
  <rect x="20" y="36" width="300" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="170" y="61" text-anchor="middle" font-size="13">저장소 (ports.ubuntu.com, NVIDIA repo)</text>
  <line x1="170" y1="76" x2="170" y2="100" stroke="currentColor" marker-end="url(#ah3)"/>
  <rect x="20" y="102" width="300" height="96" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="170" y="124" text-anchor="middle" font-size="13">장비의 쓰기 가능한 / 하나</text>
  <text x="170" y="146" text-anchor="middle" font-size="12">파일을 하나씩 교체 + postinst 실행</text>
  <text x="170" y="166" text-anchor="middle" font-size="12">중간에 전원 차단 → 반쯤 설치된 상태</text>
  <text x="170" y="186" text-anchor="middle" font-size="12">장비마다 상태가 조금씩 달라진다</text>
  <rect x="20" y="214" width="300" height="62" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="170" y="238" text-anchor="middle" font-size="12">장점: 작은 다운로드, 보안 패치 즉시</text>
  <text x="170" y="260" text-anchor="middle" font-size="12">단점: 롤백 어려움, 필드 상태 불확정</text>
  <rect x="360" y="36" width="300" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="510" y="61" text-anchor="middle" font-size="13">빌드 서버에서 이미지 생성 + 서명</text>
  <line x1="510" y1="76" x2="510" y2="100" stroke="currentColor" marker-end="url(#ah3)"/>
  <rect x="360" y="102" width="145" height="96" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="432" y="128" text-anchor="middle" font-size="13">슬롯 A</text>
  <text x="432" y="150" text-anchor="middle" font-size="12">현재 부팅 중</text>
  <text x="432" y="170" text-anchor="middle" font-size="12">read-only</text>
  <rect x="515" y="102" width="145" height="96" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="587" y="128" text-anchor="middle" font-size="13">슬롯 B</text>
  <text x="587" y="150" text-anchor="middle" font-size="12">새 이미지 기록</text>
  <text x="587" y="170" text-anchor="middle" font-size="12">검증 후 전환</text>
  <rect x="360" y="214" width="300" height="62" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="510" y="238" text-anchor="middle" font-size="12">장점: 원자적 전환, 실패 시 롤백</text>
  <text x="510" y="260" text-anchor="middle" font-size="12">단점: 큰 다운로드 (delta로 완화)</text>
  <text x="340" y="310" text-anchor="middle" font-size="12">바이너리 배포판도 이미지 단위로 쓸 수 있다: 빌드 서버에서 apt → 이미지 → A/B 배포</text>
</svg>
```
그림 5 — 업데이트 모델. 왼쪽은 데스크톱 Ubuntu 방식, 오른쪽은 SSD 펌웨어의 dual-bank 업데이트와 같은 방식. 핵심: **배포판 선택(Ubuntu vs Yocto)과 업데이트 모델 선택(apt vs A/B)은 별개의 결정**이다.

Don에게 오른쪽은 익숙하다. SSD 펌웨어의 "두 슬롯 + 활성 포인터 + 부팅 실패 카운터"가 그대로 Linux에서 RAUC·SWUpdate·Mender(L 모듈), Jetson의 rootfs A/B, Fedora IoT의 rpm-ostree로 나타난다.

---

## 8. Debian 기반 이미지를 만드는 도구들

Ubuntu Base 타르볼 + chroot는 가장 원초적인 방법이다. 실무에서는 이것을 자동화한 도구를 쓴다. 이 VM에는 debootstrap이 없어서(설치 금지 조건) 아래 도구들은 **명령 형태만 소개한다(가이드, 실행 안 함)**.

### 8.1 rootfs 생성기

- **debootstrap**: Debian의 셸 스크립트. 저장소에서 `Priority: required`(+옵션) 패키지를 받아 풀고 chroot 안에서 configure한다. `--variant=minbase`(required + apt), `--foreign`(1단계만: 다른 아키텍처면 풀기만 하고, 타깃 위나 qemu-user chroot에서 `/debootstrap/debootstrap --second-stage`).
- **mmdebstrap**: debootstrap의 현대판. 여러 저장소·여러 아키텍처, 출력을 디렉터리/tar/squashfs로 바로, `--mode=unshare`로 **root 없이** 생성 가능, 훨씬 빠르다. 재현성 지원(`SOURCE_DATE_EPOCH`)이 좋아서 Debian 쪽 새 도구들의 기반이다.
- **multistrap**: Emdebian 시절 도구. 여러 저장소를 섞어 받는 용도였는데 지금은 거의 쓰지 않는다(유지보수 정체). 오래된 BSP 스크립트에서 보이면 mmdebstrap으로 대체 대상이다.
- **Ubuntu Base 타르볼**: 이 노트에서 쓴 것. debootstrap 결과를 Canonical이 미리 만들어 둔 것.

```sh
# 가이드 (실행 안 함): x86 PC에서 arm64 Debian rootfs 만들기
sudo debootstrap --arch=arm64 --variant=minbase --foreign bookworm ./rootfs http://deb.debian.org/debian
sudo chroot ./rootfs /debootstrap/debootstrap --second-stage      # qemu-aarch64 binfmt가 있어야 함
# mmdebstrap 한 줄 버전 (root 없이, tar로 바로 출력)
mmdebstrap --mode=unshare --arch=arm64 --variant=minbase --include=systemd,udev bookworm rootfs.tar
```

### 8.2 이미지 빌더 (rootfs + 파티션 + 부트로더까지)

| 도구 | 만든 곳 | 방식 | 언제 쓰나 |
|---|---|---|---|
| debos | Collabora | YAML 레시피 (debootstrap → apt → overlay → run → image-partition → filesystem-deploy), fakemachine VM 안에서 실행 | 보드용 Debian 이미지를 재현 가능한 레시피로 |
| ubuntu-image | Canonical | gadget/모델 정의(Ubuntu Core) 또는 classic 이미지 정의 YAML | 공식 Ubuntu 보드 이미지 |
| pi-gen | Raspberry Pi | debootstrap + 단계(stage0~5) 셸 스크립트 | Raspberry Pi OS 파생 이미지 |
| Armbian build | Armbian 커뮤니티 | `compile.sh`가 커널·U-Boot를 **소스에서 빌드** + Debian/Ubuntu rootfs 조립 | Rockchip·Allwinner·Amlogic SBC |
| ELBE | Linutronix | XML 설명 → Debian 패키지로 이미지, 소스 패키지 수집·라이선스 리포트 | Debian 기반 산업 제품 |
| Isar | ilbers / Siemens | **BitBake + 레이어**(Yocto와 같은 문법) 위에서 **Debian 바이너리 패키지**로 이미지 조립, 내 패키지는 chroot에서 `dpkg-buildpackage` | Yocto 워크플로가 익숙한 팀이 Debian을 쓰고 싶을 때. CIP(Civil Infrastructure Platform)의 `isar-cip-core` |

Isar가 흥미롭다. Yocto의 "레이어로 BSP·배포판·제품을 나눈다"는 구조(K4)는 유지하고, 패키지는 컴파일하지 않고 Debian 것을 쓴다. 두 철학의 중간이다.

### 8.3 재현성 — 저장소 스냅샷

7.1절에서 봤듯이 같은 레시피도 날짜에 따라 다른 이미지를 만든다. 해결은 **스냅샷 저장소**를 고정하는 것이다.

- Debian: `snapshot.debian.org` — 과거 어느 시점의 아카이브든 URL에 타임스탬프로 접근.
- Ubuntu: `snapshot.ubuntu.com` (2023년 공개) — Ubuntu 24.04의 apt는 `sources`에 `Snapshot:` 필드(또는 `apt update --snapshot <시각>`)로 쓸 수 있다고 Ubuntu 문서가 설명한다. (이 노트에서는 실행하지 않았다.)
- 사내 미러(aptly, reprepro)로 "출시 시점 저장소"를 동결하는 방식이 가장 흔하다.

### 8.4 아키텍처가 다른 rootfs — qemu-user + binfmt_misc

x86 빌드 서버에서 arm64 rootfs를 chroot하려면 arm64 바이너리를 실행할 수 있어야 한다. 커널의 **binfmt_misc**는 "ELF 헤더가 이 magic과 mask에 맞으면 이 인터프리터로 실행하라"는 규칙을 등록하는 기능이다. 여기에 `qemu-aarch64-static`을 등록하면 `execve()`가 arm64 ELF를 만났을 때 커널이 자동으로 qemu를 앞에 붙여 실행한다(qemu-user가 명령어를 하나씩 번역하고 syscall은 호스트 커널로 넘긴다).

이 VM은 arm64라 반대 방향(arm64 호스트에서 x86-64 rootfs)으로 확인할 수 있다. 원리는 똑같다.

무엇을 확인하나: VM에 등록된 x86-64 규칙, amd64 Ubuntu Base(같은 SHA256SUMS로 검증)를 chroot해서 `uname -m`, 그리고 네이티브 vs 에뮬레이션 속도.

```sh
cat /proc/sys/fs/binfmt_misc/qemu-x86_64
cd /var/tmp/K6/dl; U=https://cdimage.ubuntu.com/ubuntu-base/releases/24.04.5/release
curl -sSfO $U/ubuntu-base-24.04.5-base-amd64.tar.gz
grep "24.04.5-base-amd64.tar.gz" SHA256SUMS | sha256sum -c
X=/var/tmp/K6/k6_x86root; sudo mkdir -p $X && sudo tar -xpf ubuntu-base-24.04.5-base-amd64.tar.gz -C $X
file $X/usr/bin/ls
sudo chroot $X /usr/bin/uname -m; sudo chroot $X /usr/bin/dpkg --print-architecture
for X in /var/tmp/K6/k6_root /var/tmp/K6/k6_x86root; do
  sudo dd if=/dev/zero of=$X/tmp/k6_blob bs=1M count=64 status=none
  echo "$X: $(sudo chroot $X /usr/bin/uname -m)"
  /usr/bin/time -f "  sha256 64MiB: %e s" sudo chroot $X /usr/bin/sha256sum /tmp/k6_blob >/dev/null
  /usr/bin/time -f "  apt-get -v x20 : %e s" sudo chroot $X /bin/sh -c 'for i in $(seq 20); do apt-get -v >/dev/null; done'
  sudo rm -f $X/tmp/k6_blob
done
```

```text
enabled
interpreter /usr/bin/qemu-x86_64
flags: POF
offset 0
magic 7f454c4602010100000000000000000002003e00
mask fffffffffffefefcfffffffffffffffffeffffff
ubuntu-base-24.04.5-base-amd64.tar.gz: OK
/var/tmp/K6/k6_x86root/usr/bin/ls: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV), dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2, BuildID[sha1]=44900829b39271d878615772bb2a6ffa422f0891, for GNU/Linux 3.2.0, stripped
x86_64
amd64
/var/tmp/K6/k6_root: aarch64
  sha256 64MiB: 0.03 s
  apt-get -v x20 : 0.03 s
/var/tmp/K6/k6_x86root: x86_64
  sha256 64MiB: 0.30 s
  apt-get -v x20 : 1.20 s
```

출력에서 볼 것:

- `magic …3e00` — ELF 헤더의 `e_machine` 필드 0x3E = EM_X86_64. 이 바이트 패턴에 맞는 파일은 `/usr/bin/qemu-x86_64`로 실행된다.
- `flags: POF` — P(preserve argv0), O(open binary: 파일을 열어서 fd로 넘김), **F(fix binary: 등록 시점에 인터프리터를 미리 열어 둔다)**. F 덕분에 chroot 안에 qemu 바이너리를 복사하지 않아도 동작한다. 옛날 문서에서 "`qemu-aarch64-static`을 rootfs의 `/usr/bin`에 복사하라"고 하는 것은 F 플래그 이전의 방식이다.
- chroot 안에서 `uname -m`이 `x86_64`라고 답한다. qemu-user가 `uname()` syscall 결과를 게스트 아키텍처로 바꿔 준다.
- 속도: 해시(64 MiB) 10배, 프로세스 생성이 많은 작업은 **40배** 느렸다(실행마다 다르다). maintainer script 수백 개가 도는 이미지 생성에서는 이 차이가 크다. 그래서 큰 회사는 arm64 빌드 서버(Graviton, Ampere)를 쓰거나 Jetson 자체에서 이미지를 만든다.

---

## 9. NVIDIA Jetson Linux (L4T) — Ubuntu 기반 BSP의 대표

> 이 절의 명령은 **NVIDIA Jetson Linux Developer Guide 기준 가이드이며 실행하지 않았다** (Jetson 하드웨어 없음). 버전·파일명은 릴리스마다 바뀌므로 실제 작업 때는 해당 릴리스 문서를 확인한다.

### 9.1 무엇이 들어 있나

**Jetson Linux**(옛 이름 L4T, Linux for Tegra)는 NVIDIA가 Jetson 모듈용으로 내는 BSP다. 구성:

- 부트 펌웨어: MB1/MB2(초기 부트 단계, BCT 설정), Orin 이후 **UEFI**(EDK2 기반). Xavier 이전(R32)은 **cboot**를 썼다.
- NVIDIA가 관리하는 커널 + Device Tree. R36부터는 커널을 업스트림에 가깝게 두고 NVIDIA 드라이버 상당수(nvgpu, 디스플레이, 카메라 등)를 **out-of-tree 모듈**(`nvidia-oot`)로 분리했다.
- NVIDIA 유저스페이스 드라이버·라이브러리(GPU, 멀티미디어, 카메라 argus 등)를 `.deb`로 묶은 것 (`nvidia-l4t-*` 패키지).
- **Ubuntu 기반 샘플 rootfs** (Tegra_Linux_Sample-Root-Filesystem).
- 플래시·OTA 도구.

**JetPack** = Jetson Linux + CUDA·cuDNN·TensorRT·VPI 등 AI 스택. 버전 대응(대략):

| JetPack | Jetson Linux | Ubuntu | 커널 | 대상 |
|---|---|---|---|---|
| 5.x | R35 | 20.04 | 5.10 | Xavier, Orin |
| 6.x | R36 | 22.04 | 5.15 | Orin |
| 7.x | R38 | 24.04 | 6.8 | Thor (Orin 지원은 릴리스 노트 확인) |

(JetPack 7 행은 2025년 Thor 출시 기준 정보다. 세부 지원 모듈은 릴리스마다 다르다.)

### 9.2 `Linux_for_Tegra/` 구조와 플래시 흐름

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="200" y="20" text-anchor="middle" font-size="13" font-weight="bold">x86-64 Ubuntu 호스트 PC: Linux_for_Tegra/</text>
  <rect x="20" y="32" width="170" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="105" y="50" text-anchor="middle" font-size="12">bootloader/</text>
  <text x="105" y="65" text-anchor="middle" font-size="12">MB1 · MB2 · UEFI · BCT</text>
  <rect x="210" y="32" width="170" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="295" y="50" text-anchor="middle" font-size="12">kernel/</text>
  <text x="295" y="65" text-anchor="middle" font-size="12">Image · dtb · debs</text>
  <rect x="20" y="84" width="170" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="105" y="102" text-anchor="middle" font-size="12">nv_tegra/</text>
  <text x="105" y="117" text-anchor="middle" font-size="12">nvidia-l4t-*.deb</text>
  <rect x="210" y="84" width="170" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="295" y="102" text-anchor="middle" font-size="12">rootfs/</text>
  <text x="295" y="117" text-anchor="middle" font-size="12">Ubuntu 샘플 rootfs</text>
  <rect x="20" y="136" width="360" height="34" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="200" y="158" text-anchor="middle" font-size="12">tools/ — kernel_flash/ · samplefs/ · 기타 스크립트</text>
  <rect x="420" y="70" width="240" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="540" y="90" text-anchor="middle" font-size="13">sudo ./apply_binaries.sh</text>
  <text x="540" y="106" text-anchor="middle" font-size="12">NVIDIA debs + 커널을 rootfs/에</text>
  <line x1="190" y1="104" x2="208" y2="104" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="380" y1="104" x2="418" y2="94" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="420" y="140" width="240" height="52" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="540" y="161" text-anchor="middle" font-size="13">flash.sh / l4t_initrd_flash.sh</text>
  <text x="540" y="180" text-anchor="middle" font-size="12">파티션 이미지 생성 + 전송</text>
  <line x1="540" y1="114" x2="540" y2="138" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="540" y1="192" x2="540" y2="236" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="530" y="220" text-anchor="end" font-size="12">USB · Force Recovery 모드</text>
  <rect x="300" y="238" width="360" height="120" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="480" y="260" text-anchor="middle" font-size="13" font-weight="bold">Jetson 모듈 (예: Orin Nano / AGX Orin)</text>
  <rect x="315" y="272" width="160" height="70" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="395" y="295" text-anchor="middle" font-size="12">QSPI NOR</text>
  <text x="395" y="315" text-anchor="middle" font-size="12">MB1 · MB2 · UEFI</text>
  <text x="395" y="333" text-anchor="middle" font-size="12">(A/B 슬롯)</text>
  <rect x="485" y="272" width="160" height="70" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="565" y="295" text-anchor="middle" font-size="12">eMMC / NVMe / SD</text>
  <text x="565" y="315" text-anchor="middle" font-size="12">APP 파티션 = rootfs</text>
  <text x="565" y="333" text-anchor="middle" font-size="12">(선택: rootfs A/B)</text>
  <text x="20" y="220" font-size="12">가이드 — NVIDIA 문서 기준, 실행 안 함</text>
  <text x="20" y="250" font-size="12">rootfs/ 는 Ubuntu 이지만</text>
  <text x="20" y="270" font-size="12">커널 · 부트로더 · GPU 드라이버는</text>
  <text x="20" y="290" font-size="12">전부 NVIDIA BSP 에서 온다</text>
</svg>
```
그림 6 — Jetson Linux의 호스트 디렉터리와 플래시 경로. rootfs는 Ubuntu 바이너리, 그 위에 NVIDIA BSP 패키지를 덮어서(`apply_binaries.sh`) USB로 굽는다.

### 9.3 전형적인 순서 (가이드, 실행 안 함)

```sh
# 1) BSP와 샘플 rootfs 풀기 (x86-64 Ubuntu 호스트)
tar xf Jetson_Linux_R36.x.y_aarch64.tbz2                         # → Linux_for_Tegra/
sudo tar xpf Tegra_Linux_Sample-Root-Filesystem_R36.x.y_aarch64.tbz2 -C Linux_for_Tegra/rootfs/
cd Linux_for_Tegra
sudo ./tools/l4t_flash_prerequisites.sh                           # 호스트 의존 패키지
sudo ./apply_binaries.sh                                          # nvidia-l4t-* deb + 커널을 rootfs/에 설치
sudo ./tools/l4t_create_default_user.sh -u don -p '...' --accept-license   # 첫 부팅 oem-config 건너뛰기
# 2) 모듈을 Force Recovery 모드로 (리커버리 버튼 + 리셋), 호스트에서 lsusb로 NVIDIA 장치 확인
# 3a) eMMC 모듈 (예: AGX Orin devkit): 고전적 flash.sh
sudo ./flash.sh jetson-agx-orin-devkit internal
# 3b) NVMe/외부 저장장치 (예: Orin Nano devkit): initrd 기반 플래시
sudo ./tools/kernel_flash/l4t_initrd_flash.sh --external-device nvme0n1p1 \
  -c tools/kernel_flash/flash_l4t_t234_nvme.xml -p "-c bootloader/generic/cfg/flash_t234_qspi.xml" \
  --showlogs --network usb0 jetson-orin-nano-devkit internal
```

포인트:

- **`apply_binaries.sh`가 하는 일**은 이 노트 4절과 같다: chroot(호스트가 x86이므로 qemu-user 사용) 안에서 `nvidia-l4t-*.deb`를 dpkg로 설치. Jetson rootfs도 결국 "Ubuntu 바이너리 + 벤더 deb"다.
- **`l4t_initrd_flash.sh`**: Jetson을 USB로 작은 initrd 커널로 먼저 부팅시키고, 그 Linux가 USB 네트워크(`usb0`)로 이미지를 받아 NVMe·eMMC에 쓴다. 외부 저장장치나 대량 생산 플래시(`--massflash`)에 쓰인다. 펌웨어 쪽 비유로 "ROM 부트로더 → RAM에 플래시 에이전트 로드 → 에이전트가 NAND에 기록"과 같다.
- 샘플 rootfs 대신 직접 만들 수도 있다: `tools/samplefs/nv_build_samplefs.sh`가 `minimal`/`desktop` 같은 flavor로 Ubuntu rootfs를 생성한다(R35/R36 문서).
- **업데이트**: NVIDIA 저장소(`repo.download.nvidia.com/jetson`)의 `nvidia-l4t-*` 패키지를 apt로 받는 **패키지 기반 업데이트**와, OTA 도구(`ota_tools` 패키지, `l4t_generate_ota_package.sh`)로 **이미지 기반 업데이트**를 모두 지원한다. 부트로더는 QSPI의 A/B 슬롯과 `nvbootctrl`로 관리하고, rootfs A/B는 플래시할 때 `ROOTFS_AB=1`로 켠다.
- **커널을 바꾸면**: NVIDIA 공개 소스(`public_sources.tbz2`)로 커널과 OOT 모듈을 **같이** 빌드해야 한다. OOT 모듈의 vermagic이 커널과 맞아야 로드된다(E 모듈, 이 노트 10절).

### 9.4 Yocto로 Jetson을 쓰는 방법

NVIDIA 공식은 Ubuntu지만, 커뮤니티 레이어 **meta-tegra**(OE4T, OpenEmbedded for Tegra)가 L4T BSP를 Yocto 레시피로 감싸서 제공한다. 이미지 크기·read-only root·재현성·Mender/RAUC 통합이 필요한 제품 회사들이 쓴다. 단 NVIDIA의 바이너리 라이브러리(CUDA 등)는 여전히 NVIDIA가 준 `.deb`를 풀어서 쓰는 레시피라서, "전부 소스 빌드"는 아니다.

---

## 10. 벤더별 BSP 전달 방식

| SoC 벤더 | 공식 BSP 형태 | rootfs | 비고 |
|---|---|---|---|
| NXP i.MX (8M, 8M Plus, 93, 95) | **Yocto** — `imx-manifest`(repo 도구) + `meta-imx` + 커뮤니티 `meta-freescale` | Yocto 이미지 (`imx-image-full` 등) | 커널 `linux-imx`, U-Boot `uboot-imx`. 데모 이미지도 Yocto로 빌드된 것 |
| TI (AM62x, AM64x, TDA4/J7) | **Yocto** Processor SDK Linux (`meta-ti`, Arago 배포판) + Debian 기반 SDK도 제공 | Yocto 또는 Debian | Debian 쪽은 BeagleBoard 커뮤니티 이미지와 연결돼 있다 |
| NVIDIA Jetson | **Ubuntu** 기반 Jetson Linux (9절) | Ubuntu 샘플 rootfs | Yocto는 커뮤니티 meta-tegra |
| Raspberry Pi | **Debian** 기반 Raspberry Pi OS (pi-gen) | Debian | Yocto는 커뮤니티 `meta-raspberrypi` |
| Qualcomm (로보틱스 RB 시리즈 등) | 세대에 따라 Yocto(`meta-qcom`) 또는 Ubuntu | 혼재 | 제품마다 확인 필요 |

읽는 법: 산업용·차량용 SoC 벤더(NXP, TI, ST)는 **Yocto가 기본**이다 — 고객이 제품 이미지를 직접 커스터마이즈하고 장기 유지보수하기 때문이다. GPU/AI 중심(NVIDIA)과 메이커 생태계(Raspberry Pi)는 **Ubuntu/Debian이 기본**이다 — CUDA·ROS 2·PyTorch 같은 거대한 유저스페이스를 바이너리로 바로 쓰는 것이 핵심 가치이기 때문이다. 입사할 회사의 SoC가 무엇인지가 곧 첫 3개월의 빌드 시스템을 정한다.

---

## 11. 장단점 비교표

| 항목 | 소스 빌드 (Buildroot · Yocto) | 바이너리 배포판 (Debian · Ubuntu) |
|---|---|---|
| 개발 속도 (첫 이미지) | 느림. Yocto 첫 빌드 수 시간, 수십~수백 GB | 빠름. 이 노트에서 apt 설치 단계 2~3 s, 전체 수 분 |
| 거대 유저스페이스 (ROS 2, CUDA, OpenCV, PyTorch) | 레시피 유지 부담 큼 (meta-ros 등) | `apt install` 한 줄, 벤더도 deb로 배포 |
| 보안 업데이트 · CVE 흐름 | 내가 추적(`cve-check`, SBOM) → 레시피 패치 → 재빌드 | 배포판 보안팀이 백포트 패치 제공 (7.1절: libc6·libssl 대기 중) |
| 크기 | 수 MB ~ 수백 MB (기능 단위로 제거 가능) | 182 MiB(이 노트의 최소 + systemd + python3) ~ 수 GB |
| 부팅 시간 | 짧게 깎기 쉬움 (BusyBox 1.3 s vs systemd 10~16 s, TCG 실측) | systemd + udev coldplug 기본, 줄일 수 있지만 기본값이 무겁다 |
| 재현성 | 강함 (소스 해시 고정, sstate, Yocto `reproducible_build`) | 약함 → 스냅샷 저장소 · 사내 미러로 보완 |
| 라이선스 준수 | Yocto `archiver`·SPDX 생성 등 도구 내장 (K7) | 패키지별 `copyright` 있음, 대응 소스 보관은 직접 (`apt-get source`) |
| 필드 업데이트 | 이미지 기반(A/B)이 자연스럽다 | apt가 쉬워 보이지만 원자성 없음 → 제품은 이미지 기반 권장 |
| 장기 지원 (LTS) | Yocto LTS 4년(예: scarthgap 5.0, 2024~2028), CIP SLTS 커널 10년 | Ubuntu LTS 5년 + ESM(유료)으로 10년 이상, Debian 약 5년(LTS 포함) |
| 커스터마이즈 깊이 | 컴파일 옵션·기능까지 | 패키지 선택까지 (기능을 빼려면 재패키징) |
| 벤더 BSP 정합 | NXP·TI가 Yocto로 준다 | NVIDIA·RPi가 Ubuntu/Debian으로 준다 |

"정답"은 없다. 실무에서 자주 보는 조합:

- **Jetson 로봇 스타트업**: JetPack Ubuntu로 시작 → 제품화 단계에서 최소화 + read-only root + 이미지 기반 OTA를 붙이거나, meta-tegra Yocto로 이전.
- **i.MX 산업용 게이트웨이**: 처음부터 Yocto, RAUC/SWUpdate로 A/B.
- **소량 생산 x86 산업용 PC**: Debian/Ubuntu + 사내 미러 + Ansible 같은 구성 관리.

---

## 12. 제품용으로 Debian/Ubuntu 이미지를 다듬기 (하드닝)

이 노트에서 실제로 부딪힌 것과 일반적인 체크리스트를 합친다.

1. **최소로 시작**: Ubuntu Base·`minbase`, `--no-install-recommends` (3.2절: 54 → 12개).
2. **dpkg 제외 규칙**: `/etc/dpkg/dpkg.cfg.d/excludes`로 doc·man·locale 제외 (Ubuntu Base 기본값), 단 `copyright`는 남긴다.
3. **이미지 생성용 임시물 제거**: `policy-rc.d`, 복사한 `resolv.conf`, apt lists/cache(320 MB), `/var/lib/dbus/machine-id`와 `/etc/machine-id`(비워서 첫 부팅 때 생성 — 6.3절 함정), SSH host key(첫 부팅 때 생성).
4. **불필요한 timer·서비스 끄기**: 부팅 로그에 `apt-daily.timer`, `apt-daily-upgrade.timer`, `motd-news.timer`, `dpkg-db-backup.timer`, `e2scrub_all.timer`가 보였다. 필드 장비가 셀룰러 요금제로 매일 apt 목록을 받으면 곤란하다 → `systemctl mask`.
5. **기본 타깃**: `graphical.target` → `multi-user.target`.
6. **read-only root**: 6.5절처럼 쓰기 경로를 찾아 tmpfs/overlay/데이터 파티션으로 (J3).
7. **장비에서 apt를 뺄 것인가**: 이미지 기반 OTA를 쓴다면 apt는 빼는 편이 낫다(7.1절: Essential 아님). 남겨 두면 현장 엔지니어가 `apt install`로 장비를 "유일한 상태"로 만든다. 디버그 빌드에만 남기는 타협도 흔하다.
8. **unattended-upgrades vs 이미지 OTA**: unattended-upgrades는 보안 패치를 자동으로 받지만 원자성·롤백이 없고 장비마다 상태가 달라진다. 제품에서는 **빌드 서버에서** 보안 업데이트를 반영해 새 이미지를 만들고 A/B로 배포하는 것이 정석이다. 배포판의 보안 패치 흐름(장점)은 그대로 누리고, 장비 위 변경(단점)은 없앤다.
9. **커널은 배포판 것이 아니라 BSP 것**: 배포판 커널 패키지(`linux-image-*`)를 안 깔거나 hold. 커널 업데이트는 BSP 쪽 절차로.
10. **보안 기능 확인**: AppArmor 프로파일, `SECURITY_YAMA` 같은 커널 옵션(6.1절에서 빠져 있었다), 사용하지 않는 계정 잠금, 이 노트에서 한 `passwd -d root`는 **실험용이며 제품에서는 절대 금지**.

---

## 13. 커널 안에서는 무슨 일이 일어나나

패키지 매니저는 순수 유저스페이스지만, "배포판 rootfs가 이 커널에서 부팅되느냐"는 철저히 커널 문제다. 이 노트에서 본 장면을 커널 경로로 연결한다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="14" width="300" height="44" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="170" y="34" text-anchor="middle" font-size="13">kernel_init → prepare_namespace</text>
  <text x="170" y="51" text-anchor="middle" font-size="12">root=/dev/vda 마운트 (mount_root)</text>
  <line x1="170" y1="58" x2="170" y2="82" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="20" y="84" width="300" height="44" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="170" y="104" text-anchor="middle" font-size="13">run_init_process(init=)</text>
  <text x="170" y="121" text-anchor="middle" font-size="12">실패 시 panic: error -2 = ENOENT</text>
  <line x1="170" y1="128" x2="170" y2="152" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="20" y="154" width="300" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="170" y="174" text-anchor="middle" font-size="13">systemd (PID 1)</text>
  <text x="170" y="191" text-anchor="middle" font-size="12">cgroup2 · autofs · signalfd · epoll</text>
  <line x1="170" y1="198" x2="170" y2="222" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="20" y="224" width="300" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="244" text-anchor="middle" font-size="13">systemd-udevd + udev-trigger</text>
  <text x="170" y="261" text-anchor="middle" font-size="12">sysfs uevent 재전송 (coldplug)</text>
  <line x1="170" y1="268" x2="170" y2="292" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="20" y="294" width="300" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="170" y="314" text-anchor="middle" font-size="13">kmod / modprobe → finit_module</text>
  <text x="170" y="331" text-anchor="middle" font-size="12">/lib/modules/$(uname -r) 필요</text>
  <rect x="360" y="14" width="300" height="70" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="510" y="36" text-anchor="middle" font-size="13">이 노트에서 본 증상</text>
  <text x="510" y="56" text-anchor="middle" font-size="12">잘못된 vdX → init 없음 → panic</text>
  <text x="510" y="74" text-anchor="middle" font-size="12">(6.5절 첫 시도)</text>
  <rect x="360" y="154" width="300" height="70" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="510" y="176" text-anchor="middle" font-size="12">SO_PASSSEC 실패: SECURITY_NETWORK 없음</text>
  <text x="510" y="196" text-anchor="middle" font-size="12">yama/ptrace_scope 없음: YAMA 없음</text>
  <text x="510" y="214" text-anchor="middle" font-size="12">binfmt_misc automount 건너뜀</text>
  <rect x="360" y="234" width="300" height="44" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="510" y="254" text-anchor="middle" font-size="12">dev-vda.device 9.3 s, trigger 6.8 s</text>
  <text x="510" y="270" text-anchor="middle" font-size="12">(blame 1, 2위)</text>
  <rect x="360" y="294" width="300" height="60" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="510" y="314" text-anchor="middle" font-size="12">IPV6=m 인데 /lib/modules 없음</text>
  <text x="510" y="332" text-anchor="middle" font-size="12">→ net/ipv6 sysctl 실패</text>
  <text x="510" y="348" text-anchor="middle" font-size="12">Jetson: OOT 모듈 vermagic 불일치</text>
  <line x1="320" y1="106" x2="358" y2="60" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="320" y1="176" x2="358" y2="186" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="320" y1="246" x2="358" y2="256" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="320" y1="316" x2="358" y2="320" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="340" y="385" text-anchor="middle" font-size="12">배포판 rootfs = "기능이 다 켜진 커널"을 가정 → BSP 커널 config가 그 가정을 만족해야 한다</text>
</svg>
```
그림 7 — 커널 부팅 경로(왼쪽)와 이 노트에서 관찰한 증상(오른쪽).

- **루트 마운트와 init 실행**: `init/main.c`의 `kernel_init()` → `prepare_namespace()`(`init/do_mounts.c`)가 `root=`를 해석해 마운트하고, `run_init_process()`가 `init=` 경로를 `execve`한다. 실패하면 `"Requested init %s failed (error %d)."`로 패닉한다 — 6.5절에서 본 그 문자열이다. initramfs가 있으면 먼저 `rdinit=/init`을 찾는데, 없어서 `check access for rdinit=/init failed: -2, ignoring`이 찍혔다 (C7).
- **chroot**: `chroot(2)`(`fs/open.c`)가 경로를 찾아 `set_fs_root()`로 `current->fs->root`를 바꾼다. 마운트 네임스페이스는 그대로라서 bind mount가 호스트와 공유되고, 그래서 `--make-rslave`가 필요했다.
- **binfmt_misc**: `fs/binfmt_misc.c`. `execve()` → `search_binary_handler()`가 등록된 `linux_binfmt` 목록을 돌다가 magic/mask에 맞으면 `load_misc_binary()`가 인터프리터(qemu)로 바꿔 다시 실행한다. F 플래그면 등록 때 `open_exec()`해 둔 `struct file`을 쓴다. 랩 커널에는 `CONFIG_BINFMT_MISC`가 꺼져 있어 systemd가 automount를 건너뛰었다.
- **udev coldplug**: `udevadm trigger`는 `/sys/devices/.../uevent`에 `add`를 써서 커널이 `kobject_uevent()`로 netlink 이벤트를 **다시** 보내게 한다. 장치·규칙이 많을수록 비싸다. 드라이버를 쓸 때 `probe()`에서 만든 sysfs 속성·`MODALIAS`가 여기서 udev 규칙과 `modprobe`로 이어진다 (G 모듈).
- **모듈 로드**: `modprobe` → `finit_module(2)` → `load_module()`이 vermagic·심볼 CRC를 검사한다. Jetson에서 커널만 바꾸고 `nvidia-oot` 모듈을 다시 안 빌드하면 여기서 거부된다 (E·G 모듈). 배포판 패키지로 OOT 모듈을 다루는 표준 방법이 **DKMS**(커널 업데이트 때 모듈 소스를 자동 재빌드)다.

---

## 14. Embedded 실무에서 다시 보기

### 14.1 센서 키트를 Jetson Orin으로 — 첫 주

JetPack을 깔고 나면 rootfs에 데스크톱(GNOME), `snapd`, `apt-daily` timer, 수천 개 패키지가 들어 있다. 첫 작업은 보통 다음과 같다.

```sh
# 가이드 (Jetson에서 실행할 명령, 이 노트에서는 실행 안 함)
dpkg-query -W -f='${Installed-Size}\t${Package}\n' | sort -rn | head -20   # 큰 패키지 찾기
systemd-analyze blame | head -20                                           # 느린 서비스
systemctl set-default multi-user.target                                     # 데스크톱 끄기
systemctl mask apt-daily.timer apt-daily-upgrade.timer                      # 자동 apt 중지
apt-mark hold nvidia-l4t-kernel nvidia-l4t-kernel-dtbs                      # BSP 커널 고정
cat /etc/nv_tegra_release                                                   # L4T 릴리스 확인
```

이 노트의 4절(크기)과 6.3절(`blame`)에서 한 일을 실제 장비에서 하는 것이다.

### 14.2 IMU 드라이버를 OOT 모듈로 추가한다면

Ubuntu rootfs + BSP 커널 조합에서 내 드라이버(예: `k6_imu.ko`)를 넣는 길은 셋이다.

- BSP 커널 소스와 같은 설정으로 빌드한 `.ko`를 `/lib/modules/$(uname -r)/updates/`에 넣고 `depmod -a` — 가장 단순하지만 커널 업데이트 때 깨진다.
- **DKMS 패키지**로 만들어 `.deb`로 배포 — 커널 패키지가 바뀌면 자동 재빌드. Ubuntu 생태계의 표준.
- Device Tree overlay로 `compatible`을 붙이고(D 모듈) 커널 이미지와 함께 BSP 쪽에서 관리.

### 14.3 "이 장비에 뭐가 깔려 있나" — SBOM 기초 데이터

바이너리 배포판의 장점: dpkg DB 자체가 정확한 설치 목록이다.

```sh
# 가이드: 장비/이미지에서 패키지 · 버전 · 소스 패키지 목록 뽑기 (K7의 SBOM 입력)
dpkg-query -W -f='${Package}\t${Version}\t${source:Package}\t${source:Version}\n' > packages.tsv
```

CVE 매칭은 `source:Package` 기준으로 한다 (5.3절: udev의 소스는 systemd).

### 14.4 빌드 서버에서 이미지 기반 파이프라인

"Ubuntu로 개발 속도 + 이미지 기반으로 필드 안정성"을 함께 얻는 흔한 구조:

1. CI에서 `mmdebstrap`(또는 Ubuntu Base + chroot, 이 노트 4절)로 스냅샷 고정 저장소에서 rootfs 생성.
2. 내 앱 `.deb`, BSP `.deb`(Jetson이면 `nvidia-l4t-*`) 설치, 하드닝 스크립트(12절) 실행.
3. `mkfs.ext4 -d` 또는 `mksquashfs`/`mkfs.erofs`(J3)로 읽기전용 이미지 생성 + 서명.
4. RAUC/SWUpdate/Mender 번들로 A/B 배포 (L 모듈).

---

## 15. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| chroot 전에 `policy-rc.d`를 안 넣음 | apt가 깐 서비스(sshd, dbus)가 **빌드 호스트에서** 시작되거나 실패 | invoke-rc.d가 chroot 안에서도 서비스 시작을 시도 | `/usr/sbin/policy-rc.d`에 `exit 101`, 이미지 마무리 때 삭제 |
| `/sys`를 `--rbind`하고 `--make-rslave` 없이 `umount -R` | 빌드 호스트의 cgroup·bpf 마운트가 사라짐 | 마운트 전파(shared)로 unmount가 호스트에 전달 | `--make-rslave` 후 해제, 해제 뒤 `findmnt`로 확인 (3.1절) |
| `/var/lib/dbus/machine-id`를 이미지에 남김 | 출하 장비 전부 같은 machine-id, DHCP·로그 수집에서 장비 구분 불가 | dbus postinst가 빌드 시점에 생성 (6.3절) | 이미지 마무리에서 두 machine-id 파일을 비우거나 삭제 |
| `--no-install-recommends` 없이 설치 | 이미지가 예상의 몇 배, 원치 않는 서비스 | Recommends 자동 설치 (3.2절: 12 → 54개) | apt 설정 `APT::Install-Recommends "false";` |
| apt lists·cache를 이미지에 남김 | rootfs 182 MiB인데 이미지 500 MiB 이상 | `apt update` 메타데이터 320 MiB (4절) | `apt-get clean` + `rm -rf /var/lib/apt/lists/*` |
| BSP 커널 config가 작음 (Buildroot용) | systemd가 부팅 중 멈추거나 경고 폭주 | CGROUPS·AUTOFS·FHANDLE 등 systemd 요구 항목 누락 | systemd README 요구 목록으로 `.config` 검사 (6.1절 스크립트) |
| `root=/dev/vdX`·`mmcblkN` 고정 | 장치 추가 후 엉뚱한 디스크를 루트로, `Requested init ... failed (error -2)` panic | 열거 순서가 바뀜 (6.5절) | `root=PARTUUID=…`, initramfs에서 UUID/LABEL |
| ro root에 배포판 rootfs를 그대로 | logind 실패, poweroff 수십 초 지연, `degraded` | 패키지들이 쓰기 가능한 `/var`·`/tmp` 가정 (6.5절) | tmpfs·overlay·데이터 파티션, 쓰기 경로 목록화 (J3) |
| 필드 장비에서 `apt upgrade` | 업데이트 중 전원 차단 → 부팅 불가, 장비마다 다른 버전 | 패키지 단위 비원자적 갱신 | 빌드 서버에서 이미지 생성 + A/B OTA |
| Jetson에서 커널만 교체 | GPU·카메라 드라이버 로드 실패 (`disagrees about version of symbol` 등) | nvidia-oot 모듈 vermagic 불일치 | 커널과 OOT 모듈을 같은 소스·config로 함께 빌드, DKMS |

---

## 16. 면접에서 이렇게 말한다

**Q.** "We're building a product on Jetson Orin. Would you use Yocto or Ubuntu (JetPack)?"

**A.** 단계와 요구 사항에 따라 다르다. 프로토타입·알고리즘 개발 단계에서는 JetPack Ubuntu가 압도적으로 빠르다: CUDA·TensorRT·ROS 2가 바이너리로 바로 나오고 NVIDIA가 공식 지원한다. 제품 단계에서 따질 것은 (1) 업데이트 모델 — A/B 이미지 OTA가 필요한가, (2) 재현성·SBOM·라이선스 감사, (3) 부팅 시간·이미지 크기, (4) 지원 기간이다. 팀이 작고 장비 수가 적으면 "Ubuntu 기반 + 최소화 + read-only root + 빌드 서버에서 이미지 생성 + A/B OTA"로 충분하다. 대량 생산·규제·장기 지원이 중요하면 meta-tegra Yocto로 가되, CUDA 스택은 결국 NVIDIA 바이너리라는 점과 커뮤니티 레이어가 JetPack 릴리스를 따라가는 시차를 감안한다.

> It depends on the phase. For bring-up and algorithm work, JetPack's Ubuntu is the fastest path — CUDA, TensorRT and ROS 2 come as supported binaries. For the product I'd decide on four things: the update model (do we need atomic A/B OTA), reproducibility and SBOM/license auditing, boot time and image size, and support lifetime. For a small fleet, a minimized Ubuntu image built in CI from a frozen mirror, with a read-only root and A/B image updates, works well. For high volume or long-term regulated products I'd move to Yocto with meta-tegra, keeping in mind that the CUDA stack is still NVIDIA binaries and the community layer lags JetPack releases a bit.

**꼬리 질문**: "Then why not just run `apt upgrade` on the devices?" → 원자성·롤백 없음, 장비별 상태 분기, 전원 차단 내성(15절 표).

**Q.** "What's the fundamental difference between Buildroot/Yocto and a Debian-based image?"

**A.** 누가 컴파일하느냐다. Buildroot·Yocto는 모든 패키지를 내 빌드 머신에서 소스로 크로스 컴파일하므로 기능 단위로 끄고 켤 수 있고 재현성이 강하다. Debian 계열은 배포판이 이미 빌드·서명한 바이너리를 조립하므로 빠르고 보안 패치를 받기 쉽지만, 선택 단위가 패키지이고 기본값이 무겁다. 실제로 Ubuntu Base에 systemd만 달라고 해도 cryptsetup·devmapper 라이브러리가 따라왔다 — 배포판 systemd가 모든 기능을 켜고 빌드됐기 때문이다.

> Who compiles. Buildroot and Yocto cross-compile everything from source on my build host, so I can turn features off at compile time and get strong reproducibility. Debian-based images assemble binaries the distro already built and signed — much faster, and security fixes come from the distro, but my granularity is the package, not the feature, and the defaults are heavy. When I installed just systemd on Ubuntu Base, libcryptsetup and libdevmapper came along because the distro builds systemd with everything enabled.

**Q.** "How does the CVE / security-update flow differ?"

**A.** Ubuntu는 보안팀이 CVE를 추적하고 같은 버전 계열에 패치를 백포트해서 `-security` 포켓으로 낸다. 9월에 서명된 24.04.5 타르볼에서 시작했는데 10월 현재 libc6와 libssl3에 보안 업데이트가 이미 대기 중이었다. Yocto에서는 `cve-check`로 찾고, stable 브랜치의 패치를 받거나 직접 패치해서 다시 빌드한다. 어느 쪽이든 제품에서는 빌드 서버에서 새 이미지를 만들어 OTA로 내보내는 흐름이 되어야 한다.

> Ubuntu's security team tracks CVEs and backports fixes into the same version series in the -security pocket. A tarball signed in September already had pending security updates for libc6 and libssl3 a month later. With Yocto you find them with cve-check and pull fixes from the stable branch or patch recipes yourself, then rebuild. Either way, for a product the fix should flow through CI into a new signed image delivered by OTA, not through apt on the device.

**Q.** "How do you build an arm64 root filesystem on an x86 build server?"

**A.** debootstrap `--foreign` 또는 mmdebstrap으로 패키지를 풀고, configure 단계(maintainer script)는 arm64 바이너리를 실행해야 하므로 qemu-user-static을 binfmt_misc에 등록해서 chroot한다. F(fix-binary) 플래그를 쓰면 인터프리터를 chroot 안에 복사하지 않아도 된다. 느리다 — 실측으로 프로세스를 많이 만드는 작업은 40배 느렸다 — 그래서 큰 파이프라인은 arm64 빌더를 쓴다.

> Unpack with debootstrap --foreign or mmdebstrap, and since the configure step runs the packages' maintainer scripts as target binaries, register qemu-user-static with binfmt_misc and chroot. With the F (fix-binary) flag the interpreter doesn't need to be copied into the chroot. It's slow — in my test, process-heavy work was about 40x slower under emulation — so serious pipelines use native arm64 builders.

**Q.** "What do you need in the kernel to boot a stock Ubuntu rootfs with systemd?"

**A.** systemd README의 요구 항목: DEVTMPFS, CGROUPS(cgroup2), INOTIFY_USER, SIGNALFD, TIMERFD, EPOLL, UNIX, SYSFS, PROC_FS, FHANDLE, 권장으로 AUTOFS, TMPFS_XATTR/POSIX_ACL, SECCOMP. 그리고 배포판 설정이 기대하는 모듈이 `/lib/modules`에 있어야 한다. 실제로 랩 커널로 Ubuntu 24.04를 부팅했을 때 필수 항목은 다 있었고, `SECURITY_NETWORK`·`YAMA` 부재와 IPV6가 모듈인데 모듈 디렉터리가 없는 것이 경고로 나왔다.

> The requirements listed in systemd's README — devtmpfs, cgroups (v2), inotify, signalfd, timerfd, epoll, unix sockets, sysfs, proc, fhandle, and recommended autofs, tmpfs xattr/ACLs and seccomp — plus whatever modules the distro config expects under /lib/modules. When I booted Ubuntu 24.04 on a defconfig-based 6.18 kernel it came up fine; the only warnings traced back to SECURITY_NETWORK and Yama being off and IPv6 being a module with no modules installed.

**Q.** "How do you make a Debian/Ubuntu image production-ready?"

**A.** minbase에서 시작해 Recommends 끄기, 이미지 생성 흔적(apt lists, policy-rc.d, machine-id, SSH host key) 제거, apt-daily 같은 timer mask, 기본 타깃을 multi-user로, read-only root + 쓰기 경로를 tmpfs/데이터 파티션으로, 커널은 BSP 것으로 hold, 업데이트는 이미지 기반. 실제로 해 보니 ro root에서 logind가 `/var/lib`에 쓰지 못해 죽고 그 때문에 poweroff까지 느려졌다 — 이런 쓰기 경로를 찾아내는 것이 핵심 작업이다.

> Start from minbase, disable Recommends, strip build artifacts — apt lists, policy-rc.d, both machine-id files, SSH host keys — mask apt-daily and similar timers, default to multi-user.target, make the root read-only and move writable paths to tmpfs or a data partition, hold the BSP kernel, and ship updates as images. When I tried a read-only root, logind died because it couldn't create its state directory under /var/lib, and that even made poweroff hang on D-Bus timeouts — finding those write paths is the real work.

**Q.** "Walk me through how a Jetson gets its software."

**A.** 호스트 x86 Ubuntu에서 Jetson Linux BSP를 풀면 `Linux_for_Tegra/`가 생기고, `rootfs/`에 NVIDIA 샘플 Ubuntu rootfs를 푼다. `apply_binaries.sh`가 NVIDIA 유저스페이스·커널 패키지를 그 rootfs에 설치한다. 모듈을 Force Recovery 모드로 USB 연결하고, eMMC면 `flash.sh`, NVMe 같은 외부 저장장치면 `l4t_initrd_flash.sh`가 작은 initrd로 Jetson을 먼저 띄운 뒤 USB 네트워크로 이미지를 써 넣는다. 부트 펌웨어(MB1/MB2/UEFI)는 QSPI에, rootfs는 APP 파티션에 가고, 부트로더와 rootfs 모두 A/B를 지원한다.

> On an x86 Ubuntu host you unpack the Jetson Linux BSP into Linux_for_Tegra, extract NVIDIA's sample Ubuntu rootfs into rootfs/, and run apply_binaries.sh to install NVIDIA's userspace and kernel packages into it. Then you put the module in Force Recovery over USB and flash — flash.sh for eMMC, or l4t_initrd_flash.sh, which boots a small initrd on the Jetson and writes the image over USB networking, for NVMe and mass flashing. Boot firmware — MB1, MB2, UEFI — lands in QSPI and the rootfs in the APP partition; both support A/B slots.

---

## 17. 직접 해보기

1. (개념) Ubuntu Base에 `/sbin/init`이 없는 이유와, 이 상태로 `init=/bin/bash`로 부팅하면 무엇이 되고 무엇이 안 되는지 설명하라.
정답/힌트: 컨테이너용 rootfs라서 init이 없다. `init=/bin/bash`면 셸은 뜨지만 `/proc`·`/sys` 마운트, udev, 네트워크, 정상 poweroff가 없다 (C7·K1).

2. (실습) 4절 스크립트에 `openssh-server`를 추가하는 단계를 넣고 `--no-install-recommends` 유무로 크기·패키지 수를 비교하라. 그리고 이미지에 들어간 SSH host key를 찾아라.
정답/힌트: `/etc/ssh/ssh_host_*_key`가 빌드 시점에 생성된다 — machine-id와 같은 종류의 함정. 첫 부팅 서비스로 재생성.

3. (관찰) 부팅한 게스트에서 `systemd-analyze critical-chain`을 보고서 스크립트에 추가해, `dev-vda.device`가 왜 9초나 걸리는지 체인으로 확인하라.
정답/힌트: `systemd-udev-trigger` → udev 규칙 처리 → 장치 유닛 활성화. `udevadm settle` 대기와 TCG 속도.

4. (실습) 6.5절의 tmpfs 세 줄 대신 `/var` 전체에 overlayfs(lower=ro 루트의 `/var`, upper=tmpfs)를 쓰는 방식으로 바꿔 보라. 무엇이 달라지는가?
정답/힌트: `/var/lib/dpkg` 같은 기존 내용이 보이면서 쓰기도 된다. 재부팅하면 사라진다 — J3의 overlayfs 절.

5. (관찰) `dpkg -S`와 `dpkg -L`로 `systemd-analyze`가 어느 패키지에 있는지, 그 패키지의 `Source`가 무엇인지 찾아라.
정답/힌트: `systemd` 바이너리 패키지, 소스도 `systemd`. `dpkg-query -W -f='${source:Package}'`.

6. (개념) 같은 레시피로 오늘과 한 달 뒤에 만든 Ubuntu 이미지가 다를 수 있는 이유 세 가지와 각각의 대책을 써라.
정답/힌트: 저장소 갱신(스냅샷·미러 고정), maintainer script의 시간·랜덤 의존(SOURCE_DATE_EPOCH, 생성물 정리), 파일 타임스탬프·순서(mkfs 옵션, tar `--sort=name`).

7. (조사) 지금 쓰는 Jetson 릴리스의 `/etc/nv_tegra_release`와 `dpkg -l 'nvidia-l4t-*'`를 보고, BSP 패키지 중 커널·부트로더·유저스페이스 드라이버를 구분해 표로 정리하라.
정답/힌트: `nvidia-l4t-kernel*`, `nvidia-l4t-bootloader`, `nvidia-l4t-cuda`·`nvidia-l4t-multimedia` 등. 장비가 없으면 NVIDIA 저장소의 Packages 인덱스로 대신한다.

---

## 18. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| binary distribution | 바이너리 배포판 | 배포판이 미리 컴파일·서명한 패키지를 조립해 시스템을 만드는 방식 (Debian, Ubuntu, Fedora) |
| Ubuntu Base | 최소 rootfs 타르볼 | Canonical 공식 minbase 수준 rootfs, init·커널 없음, arm64 약 30 MB |
| debootstrap / mmdebstrap | rootfs 생성기 | 저장소에서 필수 패키지를 받아 디렉터리에 Debian 시스템을 만든다 |
| chroot | 루트 바꾸기 | 프로세스의 `/`를 다른 디렉터리로 바꿔 그 rootfs의 프로그램을 실행 |
| policy-rc.d | 서비스 시작 정책 | `exit 101`이면 패키지 설치 중 서비스 시작 금지 |
| dpkg | 로컬 패키지 도구 | `.deb`를 풀고 `/var/lib/dpkg/`에 소유 파일·상태 기록, maintainer script 실행 |
| apt | 저장소 클라이언트 | 인덱스를 받아 의존성을 풀고 `.deb`를 받아 dpkg에 넘김 |
| maintainer script | 패키지 훅 | `preinst`·`postinst`·`prerm`·`postrm`, root로 실행되는 셸 스크립트 |
| Essential / Priority | 패키지 등급 | Essential은 항상 있다고 가정, Priority required/important/optional |
| Recommends | 약한 의존성 | 기본으로 같이 설치됨, `--no-install-recommends`로 끔 |
| security pocket | 보안 저장소 | `noble-security`처럼 보안 패치만 모은 저장소 |
| binfmt_misc | 커널 실행 형식 등록 | magic/mask에 맞는 파일을 지정 인터프리터로 실행 (qemu-user) |
| Jetson Linux (L4T) | NVIDIA BSP | 부트 펌웨어 + 커널 + NVIDIA 드라이버 + Ubuntu 샘플 rootfs + 플래시 도구 |
| JetPack | NVIDIA SDK | Jetson Linux + CUDA·cuDNN·TensorRT 등 AI 스택 |
| apply_binaries.sh | L4T 스크립트 | NVIDIA deb와 커널을 `rootfs/`에 설치 |
| l4t_initrd_flash.sh | L4T 플래시 | USB로 initrd를 부팅시켜 외부 저장장치·대량 플래시 |
| meta-tegra | Yocto 레이어 | OE4T 커뮤니티의 Jetson BSP 레이어 |
| Isar | 빌드 시스템 | BitBake 레이어 구조 + Debian 바이너리 패키지 |
| DKMS | 동적 커널 모듈 지원 | 커널 패키지가 바뀌면 OOT 모듈을 자동 재빌드 |
| machine-id | 장비 식별자 | `/etc/machine-id`, 장비마다 고유해야 함, 첫 부팅 때 생성 |

---

## 19. 요약 & 체크리스트

임베디드 Linux 이미지는 "소스에서 빌드"(Buildroot·Yocto)와 "바이너리 조립"(Debian·Ubuntu) 두 방식으로 만든다. 바이너리 조립은 빠르고(이 노트에서 systemd·python3 설치가 단계당 2~3 s), 거대한 유저스페이스(CUDA, ROS 2)를 바로 쓰며, 배포판 보안팀의 패치를 받는다(한 달 된 타르볼에 libc6·libssl 보안 업데이트 대기). 대신 크다(최소 구성 182 MiB vs BusyBox 2.1 MiB), 부팅이 무겁다(systemd + udev coldplug, TCG에서 10~16 s vs 1.3 s), 선택 단위가 패키지이며, 날짜에 따라 결과가 바뀌어 재현성이 약하고, maintainer script가 빌드 시점에 상태(machine-id, hwdb)를 만든다. Jetson은 NVIDIA가 Ubuntu 기반 BSP(`Linux_for_Tegra/`, `apply_binaries.sh`, `flash.sh`/`l4t_initrd_flash.sh`)로 주므로 Jetson 회사에서는 이 방식을 피할 수 없다. 핵심은 배포판 선택과 업데이트 모델 선택을 분리하는 것이다: Ubuntu를 쓰더라도 빌드 서버에서 스냅샷 고정 → 하드닝 → read-only 이미지 → A/B OTA로 내보내면 두 세계의 장점을 얻는다.

- [ ] 소스 빌드와 바이너리 조립의 차이를 "누가 컴파일하나, 선택 단위, 업데이트 흐름"으로 설명할 수 있다
- [ ] Ubuntu Base를 받아 SHA256과 GPG 서명을 검증하고 풀 수 있다
- [ ] chroot용 bind mount와 `policy-rc.d`를 설정하고, `--make-rslave`가 왜 필요한지 말할 수 있다
- [ ] `--no-install-recommends`와 apt 메타데이터 정리의 크기 효과를 숫자로 말할 수 있다
- [ ] `.deb` 구조(debian-binary·control·data)와 `/var/lib/dpkg/info/`의 파일들을 설명할 수 있다
- [ ] `dpkg -L`, `dpkg -S`, `dpkg-query -W -f`로 파일·패키지·소스 패키지를 조회할 수 있다
- [ ] systemd 부팅에 필요한 커널 config를 검사하고, 경고를 config 항목과 연결할 수 있다
- [ ] `mkfs.ext4 -d`로 이미지를 만들고 QEMU에서 `init=/lib/systemd/systemd`로 부팅해 `systemd-analyze`로 잴 수 있다
- [ ] read-only root에서 무엇이 깨지는지(logind, tmpfiles, poweroff 지연)와 고치는 법을 말할 수 있다
- [ ] qemu-user + binfmt_misc(F 플래그)로 다른 아키텍처 rootfs를 chroot하는 원리를 설명할 수 있다
- [ ] Jetson Linux의 `Linux_for_Tegra/` 구조와 플래시·OTA 흐름, JetPack 버전 대응을 말할 수 있다
- [ ] "Jetson 제품에 Yocto냐 Ubuntu냐"에 단계·업데이트 모델·재현성·지원 기간으로 답할 수 있다

---

## 참고 자료

- Ubuntu Base 릴리스 디렉터리: https://cdimage.ubuntu.com/ubuntu-base/releases/
- Debian Policy Manual — 패키지 관계(Depends/Recommends), maintainer script: https://www.debian.org/doc/debian-policy/
- `man 1 dpkg`, `man 1 dpkg-query`, `man 1 dpkg-deb`, `man 8 apt-get`, `man 8 debootstrap`, `man 1 mmdebstrap` (Debian manpages: https://manpages.debian.org/)
- `man 2 chroot` — https://man7.org/linux/man-pages/man2/chroot.2.html
- Kernel 문서 — binfmt_misc: https://docs.kernel.org/admin-guide/binfmt-misc.html
- systemd README(REQUIREMENTS 절, 커널 요구 사항): https://github.com/systemd/systemd/blob/main/README
- `man 1 systemd-analyze`, `man 5 machine-id` — https://www.freedesktop.org/software/systemd/man/
- 커널 소스: `init/main.c`(`kernel_init`, `run_init_process`), `init/do_mounts.c`(`prepare_namespace`), `fs/binfmt_misc.c`, `fs/open.c`(chroot) — https://elixir.bootlin.com/linux/v6.18/source
- NVIDIA Jetson Linux Developer Guide (Quick Start, Flashing Support, Over-the-Air Update): https://docs.nvidia.com/jetson/archives/ (릴리스별)
- meta-tegra (OE4T): https://github.com/OE4T/meta-tegra
- debos: https://github.com/go-debos/debos · Isar: https://github.com/ilbers/isar · pi-gen: https://github.com/RPi-Distro/pi-gen
- Debian snapshot archive: https://snapshot.debian.org/ · Ubuntu snapshot service: https://snapshot.ubuntu.com/
- 관련 노트: C7(initramfs), J3(read-only root·overlayfs), K1(rootfs 구조), K3(Buildroot), K4·K5(Yocto), K7(라이선스·SBOM), L 모듈(OTA)
