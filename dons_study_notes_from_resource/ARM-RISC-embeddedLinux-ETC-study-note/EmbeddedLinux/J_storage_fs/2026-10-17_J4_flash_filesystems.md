# J4. Flash 파일시스템 — UBIFS · JFFS2 · ubiblock, 이미지 만들기부터 전원 차단 복구까지

> **이 노트를 다 읽으면**: raw flash 위에 JFFS2 · UBIFS · ubiblock+squashfs 중 무엇을 왜 올리는지 표 하나로 고를 수 있다 · `mkfs.ubifs`(`-m -e -c`) → `ubinize.cfg` → `ubiformat`으로 양산 이미지를 만들어 굽고, `nandwrite`가 왜 틀린지 erase counter로 증명할 수 있다 · JFFS2 전체 스캔 · summary · UBI fastmap이 mount 시간을 어떻게 바꾸는지 직접 잰 그래프로 설명할 수 있다 · fsync 레코드 + 강제 종료 테스트로 UBIFS/JFFS2의 전원 차단 복구를 검증하고, SSD 펌웨어의 메타데이터 복구와 1:1로 비교할 수 있다
> **왜 필요한가**: raw NAND/SPI-NAND/SPI-NOR 보드(저가 SoC, 드론·로봇 보조 보드, 산업용 게이트웨이)의 BSP 면접 단골 질문("UBIFS와 JFFS2 차이?", "UBI 이미지를 nandwrite로 구우면?", "mount가 왜 느린가?", "전원이 나가면?")이고, 실무에서는 공장 굽기 스크립트, 파티션 설계, 필드 장비 로그 손실 디버그로 다시 나온다
> **Don 기준 난이도**: NAND · ECC · 배드 블록 · wear leveling · power-loss recovery는 Don의 SSD 펌웨어 전문 분야이고, UBI 자체(EC/VID 헤더, LEB→PEB, WL, 예약 풀)는 I11 §3에서 이미 다뤘다 / 새로 배울 부분은 **그 위의 파일시스템 설계**(로그 노드 vs wandering B+tree, journal/commit, LPT, 공간 계산)와 **양산 도구 흐름**, 그리고 Linux에서 전원 차단을 **재현·검증하는 방법**
> **선행 노트**: I11(MTD · UBI — §3 EC/VID 헤더, LEB→PEB, WL 측정, 배드 블록 예약 고갈), C2(nandsim 배드 블록 · ECC), J1(임베디드 저장장치), J3(squashfs 같은 읽기 전용 FS) — J5(전원 차단 안전성 일반론)로 이어진다

---

## 0. 큰 그림 — raw flash 위에 무엇을 올릴까

I11 §3에서 UBI를 "Linux 안에 들어간 block-level FTL"로 봤다. EC 헤더(erase counter), VID 헤더(볼륨·LEB 번호 + sqnum), attach 스캔, LEB→PEB 매핑, wear leveling(임계값 4096 vs 16에서 마모 편차 191 vs 31), 배드 블록 예약 고갈 → R/O까지 전부 거기서 측정했다. 이 노트는 그 내용을 반복하지 않고 **한 층 위로** 올라간다. UBI 볼륨이나 MTD 위에 실제로 파일을 저장하는 **파일시스템**, 그리고 그 이미지를 빌드 서버에서 만들어 공장에서 굽는 **양산 흐름**이 주제다.

raw flash 위의 선택지는 사실상 네 가지다.

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="14" width="640" height="30" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340.0" y="33.7" font-size="13" text-anchor="middle">유저 프로세스: open / write / fsync → VFS</text>
  <rect x="20" y="66" width="150" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="95.0" y="90.7" font-size="13" text-anchor="middle" font-weight="bold">JFFS2</text>
  <line x1="95.0" y1="44.0" x2="95.0" y2="64.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah1)"/>
  <line x1="95.0" y1="106.0" x2="95.0" y2="262.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah1)"/>
  <text x="95.0" y="392.0" font-size="12" text-anchor="middle">작은 NOR · 수십 MiB</text>
  <rect x="185" y="66" width="150" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="260.0" y="90.7" font-size="13" text-anchor="middle" font-weight="bold">UBIFS</text>
  <rect x="185" y="128" width="150" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="260.0" y="152.7" font-size="13" text-anchor="middle">UBI 볼륨 (dynamic)</text>
  <rect x="185" y="190" width="150" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="260.0" y="214.7" font-size="13" text-anchor="middle">UBI 장치</text>
  <line x1="260.0" y1="44.0" x2="260.0" y2="64.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah1)"/>
  <line x1="260.0" y1="106.0" x2="260.0" y2="126.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah1)"/>
  <line x1="260.0" y1="168.0" x2="260.0" y2="188.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah1)"/>
  <line x1="260.0" y1="230.0" x2="260.0" y2="262.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah1)"/>
  <text x="260.0" y="392.0" font-size="12" text-anchor="middle">RW 데이터 · rootfs</text>
  <rect x="350" y="66" width="150" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="425.0" y="90.7" font-size="13" text-anchor="middle" font-weight="bold">squashfs (RO)</text>
  <rect x="350" y="128" width="150" height="40" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="425.0" y="152.7" font-size="13" text-anchor="middle">ubiblock (RO 블록)</text>
  <rect x="350" y="190" width="150" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="425.0" y="214.7" font-size="13" text-anchor="middle">UBI 볼륨 (static)</text>
  <line x1="425.0" y1="44.0" x2="425.0" y2="64.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah1)"/>
  <line x1="425.0" y1="106.0" x2="425.0" y2="126.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah1)"/>
  <line x1="425.0" y1="168.0" x2="425.0" y2="188.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah1)"/>
  <line x1="425.0" y1="230.0" x2="425.0" y2="262.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah1)"/>
  <text x="425.0" y="392.0" font-size="12" text-anchor="middle">RO rootfs 정석</text>
  <rect x="515" y="66" width="150" height="40" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="590.0" y="90.7" font-size="13" text-anchor="middle" font-weight="bold">ext4 · vfat</text>
  <rect x="515" y="128" width="150" height="40" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="590.0" y="152.7" font-size="13" text-anchor="middle">mtdblock (블록 흉내)</text>
  <line x1="590.0" y1="44.0" x2="590.0" y2="64.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah1)"/>
  <line x1="590.0" y1="106.0" x2="590.0" y2="126.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah1)"/>
  <line x1="590.0" y1="168.0" x2="590.0" y2="262.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah1)"/>
  <text x="590.0" y="392.0" font-size="12" text-anchor="middle">RW 금지: 마모 · 전원</text>
  <rect x="20" y="264" width="640" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340.0" y="288.7" font-size="13" text-anchor="middle">MTD core: struct mtd_info · /dev/mtdN (char) · 파티션</text>
  <rect x="20" y="324" width="640" height="36" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340.0" y="346.7" font-size="13" text-anchor="middle">raw NOR · SPI-NOR · raw NAND · SPI-NAND (FTL 없음)</text>
  <text x="340.0" y="414.0" font-size="12" text-anchor="middle">UBI 계층(주황)의 내부는 I11 §3 — 이 노트는 그 위의 FS와 양산 흐름</text>
</svg>
```
그림 1 — raw flash 위의 FS 스택 선택지. JFFS2는 MTD에 직접 올라가고, UBIFS와 squashfs는 UBI 위에 올라간다. squashfs는 블록 장치를 요구하므로 UBI 볼륨을 읽기 전용 블록 장치로 바꿔 주는 `ubiblock`을 거친다. 맨 오른쪽 mtdblock + ext4는 "되긴 하지만 하면 안 되는" 경로다(I11 §2: 512 B 쓰기 한 번이 erase block 전체를 read-modify-erase-write 한다).

Don이 SSD 펌웨어에서 메타데이터를 어떻게 다뤘는지를 기준 축으로 놓으면 세 FS의 성격이 바로 보인다.

| 질문 | Don의 SSD FW (전형적 설계) | JFFS2 | UBIFS |
|---|---|---|---|
| 매핑 정보가 어디 있나 | L2P 테이블을 DRAM에 두고, 주기적으로 NAND에 **체크포인트** | **flash에 인덱스가 없다.** 각 노드가 "나는 inode X의 offset Y, version V"라고 스스로 말한다 | flash에 **B+tree 인덱스**(wandering tree)가 있다. RAM에는 그 캐시(TNC) |
| 부팅 시 재구성 | 체크포인트 로드 + 그 뒤에 쓴 블록의 spare 태그만 재생 | **전체 스캔**: 모든 노드 헤더를 읽어 RAM에 지도를 새로 만든다 | master 노드 → 인덱스 루트, 그리고 **커밋 이후 journal만** replay |
| 쓰기 경로 | host write → 열린 블록에 append, L2P 갱신은 RAM | 노드를 로그 끝에 append(NOR는 즉시, NAND는 page 버퍼) | 데이터·inode 노드를 **journal(bud LEB)** 에 append, 인덱스는 RAM에서만 갱신 → 나중에 **commit** |
| 무효 데이터 회수 | GC: valid count 낮은 블록을 골라 이동 | GC 스레드가 dirty 블록의 살아 있는 노드를 복사 | GC가 LPT(LEB별 free/dirty)를 보고 LEB를 고른다 |
| 마모·배드 블록 | FTL 자체 | JFFS2가 직접(블록 순환, 배드 블록 skip) — 단순함 | **UBI가 담당**(I11 §3) — FS는 신경 안 씀 |
| mount 시간 | 체크포인트 크기에 비례 | **노드 수 + (NAND면) 빈 블록 수에 비례** | 거의 일정(UBI attach 스캔은 PEB 수에 비례 → fastmap) |

### 0.1 실습 환경과 예제 1 — 저장장치용 커널 `Image-fs`와 게스트 rootfs

모든 게스트 실험은 공용 랩의 **스토리지/FS 커널** `out/Image-fs`(Linux `6.18.55-fs`)에서 돌렸다. VM 호스트 커널은 `7.0.0-38-generic`이고 여기서는 이미지 빌드(mtd-utils 2.3.0)와 C 도구 컴파일만 했다. QEMU `virt`, TCG 에뮬레이션이라 **측정값의 절대 크기는 실제 HW와 다르다** — 같은 조건 안의 비교만 본다.

무엇을 확인하나: 이 노트에 필요한 Flash FS 옵션이 `Image-fs`에 실제로 켜져 있는지.

```sh
limactl shell elinux -- bash -lc 'grep -E "CONFIG_(JFFS2_FS|JFFS2_SUMMARY|JFFS2_FS_WRITEBUFFER|JFFS2_ZLIB|JFFS2_LZO|UBIFS_FS|UBIFS_FS_(LZO|ZLIB|ZSTD|AUTHENTICATION)|MTD_UBI|MTD_UBI_FASTMAP|MTD_UBI_BLOCK|MTD_NAND_NANDSIM|MTD_PHRAM|MTD_PHYSMAP_OF|SQUASHFS)=" /var/lab/out/linux-build-fs/.config; uname -r'
```

```text
CONFIG_MTD_PHYSMAP_OF=y
CONFIG_MTD_PHRAM=m
CONFIG_MTD_NAND_NANDSIM=m
CONFIG_MTD_UBI=m
CONFIG_MTD_UBI_FASTMAP=y
CONFIG_MTD_UBI_BLOCK=y
CONFIG_JFFS2_FS=m
CONFIG_JFFS2_FS_WRITEBUFFER=y
CONFIG_JFFS2_SUMMARY=y
CONFIG_JFFS2_ZLIB=y
CONFIG_UBIFS_FS=m
CONFIG_UBIFS_FS_LZO=y
CONFIG_UBIFS_FS_ZLIB=y
CONFIG_UBIFS_FS_ZSTD=y
CONFIG_UBIFS_FS_AUTHENTICATION=y
CONFIG_SQUASHFS=y
7.0.0-38-generic
```

(`JFFS2_LZO`는 `# CONFIG_JFFS2_LZO is not set`이라 `=` 패턴에 걸리지 않았다 — JFFS2 LZO는 꺼져 있다. 마지막 줄은 VM 호스트 커널 버전이다.)

출력에서 볼 것: I11 때의 `Image-sub`와 달리 **fastmap(`MTD_UBI_FASTMAP`)과 `ubiblock`(`MTD_UBI_BLOCK`)이 켜져 있다**. `ubiblock`은 `ubi.ko` 안에 들어가는 기능이라 별도 모듈이 없다.

게스트 rootfs는 랩 BusyBox rootfs에 `Image-fs`의 MTD·JFFS2·UBIFS·crypto 모듈 트리와 **VM에 설치된 mtd-utils 2.3.0 바이너리**(`ubiformat`, `flash_erase`, `nandwrite`, `ubinfo`, `ubiblock`, `mtdinfo` …, 동적 링크라 glibc `libc.so.6`과 `ld-linux-aarch64.so.1`도 같이)를 넣어 만든다. BusyBox에는 `ubiattach`/`ubimkvol`/`nandwrite`는 있지만 `ubiformat`·`flash_erase`·`ubiblock`이 없어서다. mtd-utils를 정적으로 다시 빌드하지 않고 **VM의 실제 mtd-utils 바이너리를 라이브러리째 복사**하는 방법을 썼다(같은 aarch64라 그대로 돈다).

```sh
#!/bin/bash
# mkbase.sh — J4 guest rootfs: lab BusyBox rootfs + flash modules of Image-fs + VM's mtd-utils 2.3.0 (+glibc)
set -e
B=/var/tmp/J4/base; M=/var/lab/out/modules-fs/lib/modules/6.18.55-fs
rm -rf $B && mkdir -p $B && cp -a /var/lab/build/rootfs/. $B/
D=$B/lib/modules/6.18.55-fs; mkdir -p $D && cp $M/modules.* $D/
(cd $M && cp --parents -r kernel/drivers/mtd kernel/fs/jffs2 kernel/fs/ubifs kernel/crypto kernel/lib $D/)
mkdir -p $B/mu $B/j4 $B/lib/aarch64-linux-gnu
for t in flash_erase nandwrite nanddump ubiformat ubinfo ubiblock ubiupdatevol mtdinfo ubiattach ubidetach ubimkvol jffs2dump; do cp /usr/sbin/$t $B/mu/; done
cp /lib/ld-linux-aarch64.so.1 $B/lib/; cp /usr/lib/aarch64-linux-gnu/libc.so.6 $B/lib/aarch64-linux-gnu/
cp /var/tmp/J4/bin/* $B/j4/
du -sh $B
```

```text
15M	/var/tmp/J4/base
```

게스트 부팅 스크립트 `boot.sh`(VM에서 실행). 게스트 스크립트 하나를 `/j4/t.sh`로 넣고 `LAB_CMD`로 실행한 뒤 자동 poweroff 한다.

```sh
#!/bin/bash
# usage: [MEM=1536] [QEXTRA="..."] [TMO=s] [KAPPEND=".."] boot.sh <guest-script.sh>   (runs on the VM)
set -e
G=$1; N=$(basename $G .sh); W=/var/tmp/J4/run_$N
rm -rf $W && mkdir -p $W/r && cp -a /var/tmp/J4/base/. $W/r/
cp /var/tmp/J4/bin/* $W/r/j4/; cp /Users/donh/.elinux_scratch/J4/$G $W/r/j4/t.sh
(cd $W/r && find . | cpio -o -H newc --quiet | gzip -1) > $W/i.gz
timeout ${TMO:-600} qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp ${SMP:-2} -m ${MEM:-1536} -nographic \
  -no-reboot -kernel /var/lab/out/Image-fs -initrd $W/i.gz $QEXTRA \
  -append "console=ttyAMA0 quiet $KAPPEND LAB_CMD=sh~/j4/t.sh" > $W/out.txt 2>&1 || echo "qemu rc=$?"
tr -d '\r' < $W/out.txt > /var/tmp/J4/out_$N.txt
```

이 노트의 C 도구(`j4_t`, `j4_jscan`, `j4_unode`, `j4_wr`, `j4_chk`, `j4_norw`)는 전부 VM에서 `gcc -static -std=gnu11 -Wall -Wextra -O2`로 빌드했고 **경고 0개**다(처음 빌드에서 `-Wsign-compare` 경고 2개가 나와 캐스트를 고쳤다). 시간 측정용 `j4_t`는 명령 하나를 fork/exec 하고 `CLOCK_MONOTONIC` 차이를 ms로 찍는다.

```c
/* j4_t: run a command and print its wall-clock time in ms (like `time`, but ms resolution) */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

int main(int argc, char **argv)
{
	if (argc < 3) { fprintf(stderr, "usage: %s LABEL cmd [args...]\n", argv[0]); return 2; }
	struct timespec a, b;
	clock_gettime(CLOCK_MONOTONIC, &a);
	pid_t p = fork();
	if (p < 0) { perror("fork"); exit(1); }
	if (p == 0) { execvp(argv[2], argv + 2); perror("execvp"); _exit(127); }
	int st;
	if (waitpid(p, &st, 0) < 0) { perror("waitpid"); exit(1); }
	clock_gettime(CLOCK_MONOTONIC, &b);
	double ms = (b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6;
	printf("TIME %s %.1f ms (exit %d)\n", argv[1], ms, WIFEXITED(st) ? WEXITSTATUS(st) : -1);
	return WIFEXITED(st) ? WEXITSTATUS(st) : 1;
}
```

실습 중에 만난 함정 두 개를 먼저 적어 둔다(둘 다 실제로 겪었다).

- **mtd-utils에 블록 장치를 입력으로 주지 말 것.** 처음에는 이미지를 virtio 디스크(`/dev/vda`)로 넣고 `nandwrite /dev/mtd1 /dev/vda`, `ubiformat -f /dev/vda -S <크기>`로 바로 구웠다. 결과는 **첫 page만 써지고 나머지는 0xFF** — attach 때 `volume table copy #2 is corrupted`, 두 번째 시도에서는 `both volume tables are corrupted`가 났다. 같은 이미지를 게스트 `/tmp`(RAM) 파일로 복사한 뒤 넣으면 정상이었다. 그래서 이 노트의 모든 굽기는 `head -c <크기> /dev/vda > /tmp/img` 다음에 한다. (원인은 추적하지 않았다. "입력은 일반 파일"이 안전한 규칙이다.)
- **QEMU `virt`는 virtio-mmio 슬롯을 `-device` 순서의 역순으로 준다.** 디스크 세 개를 a, b, c 순으로 붙였더니 `/dev/vda`가 c였다(JFFS2 자리에 UBI 이미지가 구워져 `Magic bitmask 0x1985 not found ... 0x1831 instead` — 0x06101831은 UBIFS 노드 매직이다). 그 뒤로는 c, b, a 순으로 붙였다.

---

## 1. JFFS2 — "모든 것이 노드", 그리고 전체 스캔

### 1.1 직관 — 인덱스 없는 로그

JFFS2(Journalling Flash File System v2)는 flash를 **하나의 거대한 로그**로 본다. 파일을 쓰면 "inode 7의 offset 8192부터 4096 바이트, version 15, 데이터 CRC는 …"라는 **노드**를 로그 끝에 붙인다. 같은 범위를 다시 쓰면 version 16 노드를 또 붙인다. 옛 노드는 지우지 않는다(지울 수 없다 — flash다). 그냥 "더 새 version이 있으니 obsolete"가 된다.

그러면 "inode 7의 offset 8192에 지금 무엇이 있나"는 어떻게 알까? **flash에는 그 답이 적힌 곳이 없다.** mount 할 때 모든 erase block을 읽어 모든 노드 헤더를 훑고, RAM 안에 inode별 노드 목록(fragment tree)을 만든다. 이것이 JFFS2의 본질이고, 장점(단순함, 어떤 시점에 전원이 나가도 "마지막 완전한 노드까지"가 일관된 상태)과 단점(mount 시간 · RAM 사용량이 FS 크기에 비례)이 전부 여기서 나온다.

Don의 펌웨어로 치면, **L2P 체크포인트를 아예 쓰지 않고 부팅마다 모든 블록의 spare 영역 LBA 태그를 읽어 L2P를 재구성하는 FTL**이다. 작은 장치에서는 그게 가장 견고하고 단순하다. 8 MiB NOR라면 문제없다. 1 GiB NAND라면 부팅이 수십 초가 된다.

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="20.0" y="20.0" font-size="13" text-anchor="start" font-weight="bold">JFFS2: 모든 것이 노드 — erase block마다 로그처럼 이어 쓴다</text>
  <text x="20.0" y="54.0" font-size="12" text-anchor="start">erase block 0</text>
  <rect x="130" y="40" width="26" height="26" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="143.0" y="57.3" font-size="12" text-anchor="middle">CM</text>
  <rect x="160" y="40" width="70" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="195.0" y="57.3" font-size="12" text-anchor="middle">DIRENT a</text>
  <rect x="234" y="40" width="92" height="26" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="280.0" y="57.3" font-size="12" text-anchor="middle">INODE a v1</text>
  <rect x="330" y="40" width="92" height="26" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="376.0" y="57.3" font-size="12" text-anchor="middle">INODE a v2</text>
  <rect x="426" y="40" width="70" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="461.0" y="57.3" font-size="12" text-anchor="middle">DIRENT b</text>
  <rect x="500" y="40" width="160" height="26" rx="4" fill="none" stroke="#888" stroke-dasharray="4 3"/>
  <text x="20.0" y="98.0" font-size="12" text-anchor="start">erase block 1</text>
  <rect x="130" y="84" width="26" height="26" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="143.0" y="101.3" font-size="12" text-anchor="middle">CM</text>
  <rect x="160" y="84" width="92" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="206.0" y="101.3" font-size="12" text-anchor="middle">INODE b v1</text>
  <rect x="256" y="84" width="92" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="302.0" y="101.3" font-size="12" text-anchor="middle">INODE a v3</text>
  <rect x="352" y="84" width="40" height="26" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="372.0" y="101.3" font-size="12" text-anchor="middle">PAD</text>
  <rect x="396" y="84" width="80" height="26" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="436.0" y="101.3" font-size="12" text-anchor="middle">SUMMARY</text>
  <rect x="480" y="84" width="180" height="26" rx="4" fill="none" stroke="#888" stroke-dasharray="4 3"/>
  <text x="560.0" y="54.0" font-size="12" text-anchor="middle">0xFF (빈 곳)</text>
  <text x="130.0" y="142.0" font-size="12" text-anchor="start">빨강 = 새 version에 덮여 obsolete(dirty) → GC가 회수</text>
  <rect x="20" y="160" width="310" height="92" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="175.0" y="186.3" font-size="12" text-anchor="middle">summary 없이 mount</text>
  <text x="175.0" y="202.3" font-size="12" text-anchor="middle">모든 블록의 모든 노드 헤더를 읽고</text>
  <text x="175.0" y="218.3" font-size="12" text-anchor="middle">CRC 확인 → RAM에 inode·fragment 맵 생성</text>
  <text x="175.0" y="234.3" font-size="12" text-anchor="middle">시간·메모리 ∝ 노드 수</text>
  <rect x="350" y="160" width="310" height="92" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="505.0" y="186.3" font-size="12" text-anchor="middle">CONFIG_JFFS2_SUMMARY</text>
  <text x="505.0" y="202.3" font-size="12" text-anchor="middle">블록 끝 SUMMARY 노드 + 마커만 읽음</text>
  <text x="505.0" y="218.3" font-size="12" text-anchor="middle">(노드 위치·종류 목록)</text>
  <text x="505.0" y="234.3" font-size="12" text-anchor="middle">블록당 한 번 읽기 → 빠른 mount</text>
  <line x1="90.0" y1="112.0" x2="90.0" y2="158.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah2)"/>
  <line x1="600.0" y1="112.0" x2="600.0" y2="158.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah2)"/>
  <rect x="20" y="270" width="640" height="70" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340.0" y="293.3" font-size="12" text-anchor="middle">mount 끝 = RAM 안 지도 완성: jffs2_inode_cache · jffs2_raw_node_ref (노드마다 하나)</text>
  <text x="340.0" y="309.3" font-size="12" text-anchor="middle">그래서 FS가 클수록 mount 시간과 RAM이 커진다 — 온-flash 인덱스가 없기 때문</text>
  <text x="340.0" y="325.3" font-size="12" text-anchor="middle">CM = cleanmarker: '이 블록은 깨끗이 지워졌다'는 표시 (NOR는 블록 맨 앞, NAND는 OOB)</text>
</svg>
```
그림 2 — JFFS2 노드 로그. erase block 안에 노드가 이어 붙고, 같은 데이터의 옛 version은 obsolete(빨강)가 된다. summary가 없으면 mount가 모든 노드를 읽고, `CONFIG_JFFS2_SUMMARY`가 있으면 각 블록 끝의 SUMMARY 노드(그 블록 안 노드 목록)만 읽는다.

### 1.2 노드 종류 — 헤더 12바이트가 전부를 말한다

모든 노드는 같은 12바이트 헤더로 시작한다(`include/uapi/linux/jffs2.h`의 `struct jffs2_unknown_node`): `magic`(0x1985), `nodetype`, `totlen`, `hdr_crc`. 스캐너는 이 헤더만 보고 `totlen`만큼 건너뛸 수 있다.

| nodetype | 값 | 내용 |
|---|---|---|
| `JFFS2_NODETYPE_DIRENT` | 0xE001 | 디렉터리 엔트리(부모 inode, 이름, 대상 inode, version) |
| `JFFS2_NODETYPE_INODE` | 0xE002 | inode 메타데이터 + 데이터 조각(offset, 압축 전/후 크기, 압축 방식, data_crc). 헤더 포함 68 B + 데이터 |
| `JFFS2_NODETYPE_CLEANMARKER` | 0x2003 | "이 블록은 끝까지 정상적으로 지워졌다" 표시. NOR는 블록 맨 앞 12 B, NAND는 OOB |
| `JFFS2_NODETYPE_PADDING` | 0x2004 | 빈 공간 채우기 |
| `JFFS2_NODETYPE_SUMMARY` | 0x2006 | 블록 안 노드 목록. 블록 끝 8 B에 `jffs2_sum_marker`(offset + 매직 0x02851885) |

nodetype 상위 2비트는 호환성 플래그다(`INCOMPAT`=0xC000: 모르면 mount 거부, `RWCOMPAT_DELETE`=0x0000: 모르면 지워도 됨). cleanmarker·padding·summary가 0x2xxx인 이유다 — 옛 커널이 summary를 몰라도 "지워도 되는 노드"로 무시하고 mount할 수 있다. 펌웨어 메타데이터 포맷에 버전 호환 비트를 두는 것과 같은 발상이다.

**cleanmarker가 왜 필요한가**: erase 도중 전원이 나가면 블록이 "대부분 0xFF지만 일부 셀은 덜 지워진" 상태로 남을 수 있다. 읽으면 0xFF로 보여도 program하면 비트 오류가 난다. 그래서 JFFS2는 erase를 끝까지 마친 뒤에만 cleanmarker를 쓰고, mount 때 "0xFF인데 cleanmarker가 없는 블록"은 다시 지운다. Don의 펌웨어에서 erase 완료 후 블록 상태를 메타데이터에 기록하던 것과 같은 이유다.

### 1.3 예제 2 — `mkfs.jffs2` 이미지를 직접 해부하기

무엇을 확인하나: `jffs2_scan_eraseblock()`이 하는 일을 유저 공간에서 흉내 내어, NOR용(cleanmarker 포함) · NAND용(cleanmarker 없음) · `sumtool`로 summary를 넣은 NAND용 이미지의 노드 구성을 비교한다.

```c
/* j4_jscan: walk a JFFS2 image (file or /dev/mtdN) like jffs2_scan_medium() does, count node types */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>

static const char *tname(uint16_t t)
{
	switch (t) {
	case 0xe001: return "DIRENT"; case 0xe002: return "INODE"; case 0x2003: return "CLEANMARKER";
	case 0x2004: return "PADDING"; case 0x2006: return "SUMMARY"; default: return "OTHER";
	}
}

int main(int argc, char **argv)
{
	if (argc < 3) { fprintf(stderr, "usage: %s IMG ERASESIZE [verbose_blocks]\n", argv[0]); return 2; }
	int fd = open(argv[1], O_RDONLY);
	if (fd < 0) { perror("open"); exit(1); }
	long es = strtol(argv[2], NULL, 0), vb = argc > 3 ? atol(argv[3]) : 0;
	uint8_t *b = malloc(es);
	if (!b) { perror("malloc"); exit(1); }
	long cnt[6] = {0}, blocks = 0, empty = 0, withsum = 0, bytes[6] = {0};
	for (long blk = 0; pread(fd, b, es, blk * es) == es; blk++, blocks++) {
		uint32_t mk = *(uint32_t *)(b + es - 4);	/* jffs2_sum_marker.magic at block end */
		if (mk == 0x02851885) withsum++;
		long nodes = 0;
		for (long o = 0; o + 12 <= es; ) {
			uint16_t mg = *(uint16_t *)(b + o), ty = *(uint16_t *)(b + o + 2);
			uint32_t len = *(uint32_t *)(b + o + 4);
			if (mg != 0x1985 || len < 12 || o + (long)len > es) { o += 4; continue; }
			int k = ty == 0xe001 ? 0 : ty == 0xe002 ? 1 : ty == 0x2003 ? 2 : ty == 0x2004 ? 3 : ty == 0x2006 ? 4 : 5;
			cnt[k]++; bytes[k] += len; nodes++;
			if (blk < vb && nodes <= 4)
				printf("blk %ld off 0x%05lx %-11s len %u\n", blk, o, tname(ty), len);
			o += (len + 3) & ~3u;			/* nodes are 4-byte aligned */
		}
		if (!nodes) empty++;
		if (blk < vb) printf("blk %ld: %ld nodes, sum_marker=%s\n", blk, nodes, mk == 0x02851885 ? "yes" : "no");
	}
	const char *n[6] = { "DIRENT", "INODE", "CLEANMARKER", "PADDING", "SUMMARY", "OTHER" };
	printf("blocks=%ld empty=%ld blocks_with_summary=%ld\n", blocks, empty, withsum);
	for (int k = 0; k < 6; k++) if (cnt[k]) printf("  %-11s %7ld nodes %10ld bytes\n", n[k], cnt[k], bytes[k]);
	return 0;
}
```

입력 디렉터리 `data/`는 설정 파일 하나와 IMU 로그 흉내 텍스트(`imu.log`, 20000줄, 약 1.1 MB)다. VM에서 실행:

```sh
cd /var/tmp/J4/img
mkfs.jffs2 -r data -e 256KiB -p -o data_nor.jffs2            # NOR: 256 KiB sector, cleanmarker in-band
mkfs.jffs2 -r data -n -e 128KiB -p -o data_nand.jffs2        # NAND: -n = no cleanmarkers (they live in OOB)
sumtool -n -e 128KiB -p -i data_nand.jffs2 -o data_nand_sum.jffs2
ls -l *.jffs2
echo "-- NOR (cleanmarkers)"; j4_jscan data_nor.jffs2 0x40000 1
echo "-- NAND, no summary"; j4_jscan data_nand.jffs2 0x20000
echo "-- NAND + sumtool"; j4_jscan data_nand_sum.jffs2 0x20000 1
```

```text
-rw-r--r-- 1 donh donh 262144 Oct 17 16:03 data_nand.jffs2
-rw-r--r-- 1 donh donh 262144 Oct 17 16:03 data_nand_sum.jffs2
-rw-r--r-- 1 donh donh 262144 Oct 17 16:03 data_nor.jffs2
-- NOR (cleanmarkers)
blk 0 off 0x00000 CLEANMARKER len 12
blk 0 off 0x0000c DIRENT      len 43
blk 0 off 0x00038 INODE       len 68
blk 0 off 0x0007c DIRENT      len 43
blk 0: 209 nodes, sum_marker=no
blocks=1 empty=0 blocks_with_summary=0
  DIRENT            4 nodes        183 bytes
  INODE           204 nodes     144367 bytes
  CLEANMARKER       1 nodes         12 bytes
-- NAND, no summary
blocks=2 empty=0 blocks_with_summary=0
  DIRENT            4 nodes        183 bytes
  INODE           205 nodes     144671 bytes
-- NAND + sumtool
blk 0 off 0x00000 DIRENT      len 43
blk 0 off 0x0002c INODE       len 68
blk 0 off 0x00070 DIRENT      len 43
blk 0 off 0x0009c INODE       len 68
blk 0: 183 nodes, sum_marker=yes
blocks=2 empty=0 blocks_with_summary=1
  DIRENT            4 nodes        183 bytes
  INODE           205 nodes     144671 bytes
  SUMMARY           1 nodes       4708 bytes
```

출력에서 볼 것:

- NOR 이미지는 블록 0의 **offset 0에 12바이트 CLEANMARKER**가 있다. NAND 이미지(`-n`)에는 없다 — NAND에서는 cleanmarker를 OOB에 두기 때문에, 굽기 전에 `flash_erase -j`(JFFS2 cleanmarker를 OOB에 기록)로 지우고 그 위에 `nandwrite`한다(§4 예제에서 그렇게 했다).
- INODE 204개, 합계 144 KB: 1.1 MB 텍스트가 **zlib으로 약 1/8**이 됐다. 각 INODE 노드가 4 KiB(커널 page) 이하의 데이터 조각 하나를 담는다. `len 68`짜리 INODE는 데이터 없는 메타데이터 노드(헤더 68 B = `struct jffs2_raw_inode`)다.
- summary 이미지: 블록 0 끝에 `sum_marker=yes`, SUMMARY 노드 4708 B. 대신 블록 0에 들어간 노드는 209 → 183개로 줄었다(summary가 공간을 먹었다). §4의 큰 이미지에서도 summary판이 4~5% 컸다(192 MiB 데이터: 204.9 MB → 214.8 MB).

### 1.4 쓰기 · GC · NOR와 NAND의 차이

- **NOR**: write 단위가 사실상 1바이트(`mtd->writesize = 1`)라 JFFS2는 `write()` 안에서 노드를 **즉시** flash에 쓴다. `fsync()`가 할 일이 거의 없다. §6 전원 차단 테스트에서 JFFS2가 레코드를 빠르게 쓴 이유다.
- **NAND**: page 단위로만 쓸 수 있고 page를 두 번 program하면 안 되므로, JFFS2는 page 하나 크기의 **write buffer(wbuf)** 에 노드를 모았다가 page가 차거나 `fsync`/타이머가 오면 내보낸다(`CONFIG_JFFS2_FS_WRITEBUFFER`). wbuf에 있는 동안 전원이 나가면 그 노드는 없던 일이 된다 — `fsync`가 필요한 이유다.
- **GC**: 커널 스레드 `jffs2_gcd_mtdN`(§6의 oops 콜 트레이스에서 실제 이름이 보인다)이 dirty가 많은 블록을 골라 살아 있는 노드를 로그 끝으로 복사하고 블록을 지운다. 지운 뒤 cleanmarker를 쓴다. JFFS2는 GC가 쓸 블록을 몇 개 **예약**해 두고(`resv_blocks_*`) 그 아래로 내려가면 일반 쓰기를 `ENOSPC`로 거부한다.
- **mount 뒤의 CRC 검사**: mount는 노드 헤더만 확인하고, 데이터 CRC 확인은 mount 후 GC 스레드가 "checking" 단계로 천천히 한다. mount가 끝나도 첫 쓰기 전에 이 단계가 끝나야 하므로 "mount는 빨랐는데 첫 쓰기가 느리다"는 현상이 생긴다.
- **배드 블록**: JFFS2는 MTD의 `block_isbad`를 보고 배드 블록을 건너뛴다. 하지만 bit-flip(정정된 비트 오류)이 나도 블록을 옮기지 않는다 — §7에서 UBI와 비교한다.

### 1.5 JFFS2의 한계 — 언제 쓰지 말아야 하나

| 한계 | 이유 | 결과 |
|---|---|---|
| mount 시간 ∝ 노드 수 | flash에 인덱스가 없어 전체 스캔 | §4: summary 없이 192 MiB → 약 530 ms(nandsim 지연 없음), NAND 지연을 켜면 65 초 |
| RAM ∝ 노드 수 | 노드마다 `jffs2_raw_node_ref`를 RAM에 둔다 | §4: 192 MiB에서 slab 약 1.1 MiB, 노드당 약 24 B |
| 큰 NAND에서 비효율 | wear leveling이 단순, 배드 블록 관리가 FS 안에 섞여 있음 | 수백 MiB 이상 NAND에는 UBIFS가 정답 |
| 압축 때문에 `df`가 부정확 | 압축 후 크기를 미리 알 수 없음 | 남은 공간 추정이 보수적 |

정리: **수 MiB ~ 수십 MiB의 NOR(SPI-NOR) 설정 파티션**이 JFFS2의 마지막 남은 자리다. 새 NAND 설계라면 UBIFS(또는 RO 부분은 ubiblock + squashfs)를 먼저 검토한다.

---

## 2. UBIFS — UBI 볼륨 위의 wandering B+tree

### 2.1 직관 — 인덱스를 flash에 두되, 제자리에서 고치지 않는다

UBIFS는 JFFS2의 문제(전체 스캔)를 **flash 위의 인덱스**로 푼다. 하지만 flash는 덮어쓸 수 없으므로 인덱스도 덮어쓸 수 없다. 해법이 **wandering tree**(떠도는 나무)다: 인덱스 노드 하나를 바꾸려면 그 노드의 새 사본을 다른 곳에 쓰고, 그 노드를 가리키는 부모도 새로 쓰고, … 루트까지 새로 쓴다. 마지막으로 **master 노드**가 새 루트를 가리키게 한다. 이것이 **commit**이다.

매 쓰기마다 루트까지 새로 쓰면 쓰기 증폭이 끔찍하므로, 커밋 사이에는 **journal**을 쓴다. 파일 데이터와 inode 노드는 journal LEB(UBIFS 용어로 **bud**)에 append만 하고, 인덱스 변경은 RAM의 TNC(Tree Node Cache)에서만 한다. journal이 일정 크기(`max_bud_bytes`)를 넘거나 umount/sync 때 commit해서 RAM의 인덱스 변경을 flash에 반영한다. mount 때는 master → 인덱스 루트를 읽고, **commit 이후에 쓴 bud만** 다시 훑어(replay) TNC에 반영하면 된다. 그래서 mount 시간이 FS 크기와 거의 무관하다.

Don의 SSD에 그대로 대응한다: L2P 체크포인트 = 커밋된 인덱스, 체크포인트 이후 쓴 블록의 spare 태그 재생 = bud replay, master 노드 2부 = 체크포인트 포인터를 두 곳에 번갈아 쓰는 것.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="20.0" y="20.0" font-size="13" text-anchor="start" font-weight="bold">UBIFS 볼륨의 LEB 배치 (예제 8 live 볼륨 실측: 496 LEB)</text>
  <rect x="20" y="34" width="44" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="42.0" y="50.3" font-size="12" text-anchor="middle">SB</text>
  <text x="42.0" y="66.3" font-size="12" text-anchor="middle">LEB 0</text>
  <rect x="64" y="34" width="88" height="40" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="108.0" y="50.3" font-size="12" text-anchor="middle">master</text>
  <text x="108.0" y="66.3" font-size="12" text-anchor="middle">LEB 1-2</text>
  <rect x="152" y="34" width="132" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="218.0" y="50.3" font-size="12" text-anchor="middle">log</text>
  <text x="218.0" y="66.3" font-size="12" text-anchor="middle">LEB 3-5</text>
  <rect x="284" y="34" width="88" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="328.0" y="50.3" font-size="12" text-anchor="middle">LPT</text>
  <text x="328.0" y="66.3" font-size="12" text-anchor="middle">LEB 6-7</text>
  <rect x="372" y="34" width="88" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="416.0" y="50.3" font-size="12" text-anchor="middle">orphan</text>
  <text x="416.0" y="66.3" font-size="12" text-anchor="middle">LEB 8-9</text>
  <rect x="460" y="34" width="200" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="560.0" y="50.3" font-size="12" text-anchor="middle">main area</text>
  <text x="560.0" y="66.3" font-size="12" text-anchor="middle">LEB 10 ~ 495</text>
  <text x="20.0" y="96.0" font-size="12" text-anchor="start">master ×2: 인덱스 루트 위치 · log 위치 · LPT 위치 (둘 중 CRC 맞는 최신 것을 씀)</text>
  <text x="20.0" y="114.0" font-size="12" text-anchor="start">log: 커밋 이후의 journal bud(=main area의 LEB)를 가리키는 REF 노드 · CS(commit start)</text>
  <text x="20.0" y="132.0" font-size="12" text-anchor="start">LPT: LEB마다 free/dirty/flags (비트 단위 패킹, 자체 B-tree) · orphan: 열린 채 지워진 inode</text>
  <text x="20.0" y="164.0" font-size="13" text-anchor="start" font-weight="bold">wandering B+tree: 노드를 제자리에서 고치지 않고, 바뀐 경로를 새로 써서 루트까지 올라간다</text>
  <rect x="40" y="190" width="110" height="30" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="95.0" y="209.3" font-size="12" text-anchor="middle">master</text>
  <rect x="200" y="190" width="110" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="255.0" y="209.3" font-size="12" text-anchor="middle">IDX root v1</text>
  <rect x="380" y="190" width="110" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="435.0" y="209.3" font-size="12" text-anchor="middle">IDX root v2</text>
  <rect x="170" y="250" width="80" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="210.0" y="269.3" font-size="12" text-anchor="middle">IDX</text>
  <rect x="260" y="250" width="80" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="300.0" y="269.3" font-size="12" text-anchor="middle">IDX</text>
  <rect x="400" y="250" width="80" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="440.0" y="269.3" font-size="12" text-anchor="middle">IDX'</text>
  <rect x="150" y="310" width="50" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="175.0" y="327.3" font-size="12" text-anchor="middle">INO</text>
  <rect x="205" y="310" width="50" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="230.0" y="327.3" font-size="12" text-anchor="middle">DATA</text>
  <rect x="260" y="310" width="50" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="285.0" y="327.3" font-size="12" text-anchor="middle">DATA</text>
  <rect x="315" y="310" width="50" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340.0" y="327.3" font-size="12" text-anchor="middle">DENT</text>
  <rect x="420" y="310" width="50" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="445.0" y="327.3" font-size="12" text-anchor="middle">DATA'</text>
  <line x1="150.0" y1="205.0" x2="198.0" y2="205.0" stroke="currentColor" stroke-width="1.2" stroke-dasharray="4 3" marker-end="url(#ah3)"/>
  <path d="M95,190 C 150,168 360,168 430,188" fill="none" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
  <line x1="255.0" y1="220.0" x2="215.0" y2="248.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
  <line x1="255.0" y1="220.0" x2="295.0" y2="248.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
  <line x1="435.0" y1="220.0" x2="440.0" y2="248.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
  <line x1="435.0" y1="220.0" x2="300.0" y2="248.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
  <line x1="210.0" y1="280.0" x2="175.0" y2="308.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
  <line x1="210.0" y1="280.0" x2="230.0" y2="308.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
  <line x1="300.0" y1="280.0" x2="285.0" y2="308.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
  <line x1="300.0" y1="280.0" x2="340.0" y2="308.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
  <line x1="440.0" y1="280.0" x2="445.0" y2="308.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
  <text x="500.0" y="210.0" font-size="12" text-anchor="start">커밋 후 새 루트</text>
  <text x="500.0" y="270.0" font-size="12" text-anchor="start">바뀐 경로만 새로</text>
  <text x="500.0" y="328.0" font-size="12" text-anchor="start">새 데이터</text>
  <text x="156.0" y="232.0" font-size="12" text-anchor="start">옛 루트(점선)</text>
  <rect x="20" y="348" width="640" height="44" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340.0" y="366.3" font-size="12" text-anchor="middle">커밋 전 쓰기는 journal(bud LEB)에 데이터·inode 노드로만 간다 — 인덱스는 RAM의 TNC에서만 바뀜</text>
  <text x="340.0" y="382.3" font-size="12" text-anchor="middle">mount = master → 인덱스 루트 읽기 + log가 가리키는 bud만 replay → 크기와 무관하게 빠름</text>
</svg>
```
그림 3 — UBIFS 볼륨의 LEB 배치(예제 8에서 실제로 읽은 값)와 wandering tree. 커밋은 바뀐 경로만 새로 쓰고(IDX', DATA'), 바뀌지 않은 부분트리는 새 루트가 옛 노드를 그대로 가리킨다.

### 2.2 영역별 역할

| 영역 | LEB | 내용 | SSD FW 대응 |
|---|---|---|---|
| superblock | 0 | geometry(min I/O, LEB 크기, LEB 수, **max LEB 수**), 기본 압축, 영역 크기, UUID, fanout | 펌웨어 메타 헤더 / format 정보 |
| master | 1, 2 (같은 내용 2부) | 인덱스 루트 위치, log 위치, LPT 위치, `highest_inum`, `cmt_no`, 총 free/dirty | 체크포인트 포인터 + 이중화 |
| log | 3 ~ | 커밋 시작(CS) 노드와, 커밋 이후 bud LEB를 가리키는 REF 노드 | "체크포인트 이후 열린 블록 목록" |
| LPT | log 뒤 | LEB별 free·dirty·flags(index LEB인지 등). 비트 단위로 압축된 별도 B-tree(작으면 "small LPT model") | 블록별 valid count 테이블 |
| orphan | LPT 뒤 | "열린 채로 unlink된 inode" 목록 — 전원 차단 후 회수용 | 삭제 진행 중 목록 |
| main | 나머지 | 데이터·inode·dentry 노드(journal bud), 인덱스(IDX) 노드, GC용 빈 LEB | 사용자 데이터 + 맵 페이지 |

모든 UBIFS 노드는 24바이트 공통 헤더 `struct ubifs_ch`(magic 0x06101831, crc, **sqnum**, len, node_type)로 시작하고 **8바이트 정렬**이다. `sqnum`은 FS 전체에서 단조 증가하는 64비트 번호라, 같은 키의 노드가 둘이면 큰 쪽이 최신이다(UBI VID 헤더의 sqnum, Don 펌웨어의 sequence number와 같은 역할).

### 2.3 예제 3 — `mkfs.ubifs` 출력 이미지를 LEB 단위로 지도 그리기

무엇을 확인하나: `mkfs.ubifs`가 만든 이미지(아직 UBI가 씌워지지 않은 순수 UBIFS, LEB가 순서대로 이어진 파일)의 superblock 필드를 읽고, 각 LEB가 어느 영역이며 어떤 노드가 들어 있는지 센다. 같은 도구를 §5에서 게스트의 살아 있는 볼륨(`/dev/ubi0_0`)에도 쓴다.

```c
/* j4_unode: map a UBIFS volume/image LEB by LEB: SB fields, then first node type per area */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

struct ch { uint32_t magic, crc; uint64_t sqnum; uint32_t len; uint8_t type, group, pad[2]; } __attribute__((packed));
static const char *nm[] = { "INO", "DATA", "DENT", "XENT", "TRUN", "PAD", "SB", "MST", "REF", "IDX", "CS", "ORPH", "AUTH", "SIG" };

int main(int argc, char **argv)
{
	if (argc < 3) { fprintf(stderr, "usage: %s IMG|/dev/ubiX_Y LEBSIZE [maxleb]\n", argv[0]); return 2; }
	int fd = open(argv[1], O_RDONLY);
	if (fd < 0) { perror("open"); exit(1); }
	long ls = strtol(argv[2], NULL, 0), maxl = argc > 3 ? atol(argv[3]) : 1 << 30;
	uint8_t *b = malloc(ls);
	if (!b || pread(fd, b, ls, 0) != ls) { perror("read LEB0"); exit(1); }
	struct ch *c = (struct ch *)b;
	if (c->magic != 0x06101831 || c->type != 6) { fprintf(stderr, "LEB 0 is not a UBIFS SB\n"); exit(1); }
	uint32_t *sb = (uint32_t *)(b + 24 + 4);	/* after ch + padding[2], key_hash, key_fmt */
	uint32_t leb_cnt = sb[3], log = sb[7], lpt = sb[8], orph = sb[9];
	uint16_t compr = *(uint16_t *)(b + 24 + 4 + 4 * 14);
	printf("SB: min_io=%u leb_size=%u leb_cnt=%u max_leb_cnt=%u log_lebs=%u lpt_lebs=%u orph_lebs=%u default_compr=%u\n",
	       sb[1], sb[2], leb_cnt, sb[4], log, lpt, orph, compr);
	uint32_t lpt0 = 3 + log, orph0 = lpt0 + lpt, main0 = orph0 + orph;
	for (long l = 0; l < leb_cnt && l < maxl; l++) {
		if (pread(fd, b, ls, l * ls) != ls) break;
		const char *area = l == 0 ? "sb" : l < 3 ? "master" : l < lpt0 ? "log" : l < orph0 ? "lpt" : l < main0 ? "orphan" : "main";
		long nodes = 0, used = 0, ty[14] = {0};
		for (long o = 0; o + 24 <= ls; ) {
			c = (struct ch *)(b + o);
			if (c->magic != 0x06101831 || c->len < 24 || o + c->len > ls) break;	/* rest is erased/LPT bits */
			if (c->type < 14) ty[c->type]++;
			nodes++; o += (c->len + 7) & ~7u; used = o;		/* nodes are 8-byte aligned */
		}
		int erased = 1;
		for (long i = used; i < ls && erased; i++) erased = b[i] == 0xff;
		printf("LEB %3ld %-6s nodes=%-4ld used=%-6ld %s", l, area, nodes, used, erased ? "rest=0xFF" : "rest=non-FF");
		for (int k = 0; k < 14; k++) if (ty[k]) printf(" %s:%ld", nm[k], ty[k]);
		printf("\n");
	}
	return 0;
}
```

(superblock 필드 오프셋은 `fs/ubifs/ubifs-media.h`의 `struct ubifs_sb_node` 순서 — `flags, min_io_size, leb_size, leb_cnt, max_leb_cnt, max_bud_bytes(64비트), log_lebs, lpt_lebs, orph_lebs, jhead_cnt, fanout, lsave_cnt, fmt_version, default_compr` — 를 그대로 따랐다.)

```sh
mkfs.ubifs -r data -m 2048 -e 129024 -c 2000 -x zstd -o data.ubifs      # §3 예제 4에서 만든 이미지
j4_unode data.ubifs 129024
```

```text
SB: min_io=2048 leb_size=129024 leb_cnt=14 max_leb_cnt=2000 log_lebs=5 lpt_lebs=2 orph_lebs=1 default_compr=3
LEB   0 sb     nodes=3    used=4192   rest=non-FF INO:1 PAD:1 SB:1
LEB   1 master nodes=2    used=544    rest=non-FF PAD:1 MST:1
LEB   2 master nodes=2    used=544    rest=non-FF PAD:1 MST:1
LEB   3 log    nodes=2    used=64     rest=non-FF PAD:1 CS:1
LEB   4 log    nodes=0    used=0      rest=0xFF
LEB   5 log    nodes=0    used=0      rest=0xFF
LEB   6 log    nodes=0    used=0      rest=0xFF
LEB   7 log    nodes=0    used=0      rest=0xFF
LEB   8 lpt    nodes=0    used=0      rest=non-FF
LEB   9 lpt    nodes=0    used=0      rest=0xFF
LEB  10 orphan nodes=0    used=0      rest=0xFF
LEB  11 main   nodes=212  used=88112  rest=non-FF INO:5 DATA:202 DENT:4 PAD:1
LEB  12 main   nodes=0    used=0      rest=0xFF
LEB  13 main   nodes=33   used=5888   rest=non-FF PAD:1 IDX:32
```

출력에서 볼 것:

- `leb_cnt=14`: 데이터 1.1 MB짜리 FS라 **이미지는 LEB 14개**뿐이다. 하지만 `max_leb_cnt=2000`(`-c 2000`)이 superblock에 적혀 있어서, 더 큰 UBI 볼륨에 올리면 **첫 mount 때 볼륨 크기까지 FS를 늘린다**(§3 예제 5에서 14 LEB → 477 LEB). `-c`는 LPT 크기를 정하므로 너무 크게 잡으면 LPT가 공간을 낭비하고, 작게 잡으면 큰 장치에서 공간을 못 쓴다.
- `default_compr=3`: UBIFS 압축 번호(0 none, 1 lzo, 2 zlib, 3 zstd) — `-x zstd`가 기록됐다.
- master LEB 1과 2가 **같은 크기의 MST 노드를 하나씩** 가진다(2부 이중화). LEB 3은 CS(commit start) 노드 하나. LPT LEB 8은 `nodes=0`인데 `rest=non-FF` — LPT는 공통 헤더 없는 **비트 패킹 노드**라 내 도구가 노드로 세지 못했을 뿐 내용이 있다.
- LEB 11: INO 5개 · DATA 202개 · DENT 4개 = 파일 데이터. LEB 13: IDX 32개 = 인덱스. 커밋된 인덱스와 데이터가 서로 다른 LEB에 있다(인덱스 LEB는 LPT에서 "index" 플래그를 받는다).
- 모든 LEB의 첫 노드가 끝난 뒤 남은 공간은 `PAD` 노드로 채우거나 0xFF로 남긴다. **0xFF로 남은 page는 "아직 안 쓴 page"라는 뜻이고, UBIFS는 나중에 거기에 이어 쓴다** — §3에서 `nandwrite`가 위험한 이유와 연결된다.

### 2.4 LPT · budgeting · 공간 계산 — `df`가 거짓말하는 이유

UBIFS는 **쓰기를 허락하기 전에 공간을 예약(budgeting)** 한다. page cache에 dirty page가 쌓여도 나중에 flash에 못 쓰면 데이터를 잃기 때문이다. 문제는 압축 · 노드 헤더 · 인덱스 성장 · LEB 끝 자투리(dark space) 때문에 "정말 남은 바이트"를 미리 알 수 없다는 것. 그래서:

- `df`의 Size/Avail은 **"이만큼 압축 안 되는 파일을 쓸 수 있다"는 보수적 추정**이다(`ubifs_get_free_space_nolock()` 주석이 직접 그렇게 말한다: 사용자는 N 바이트가 남았다면 N 바이트 파일이 들어가길 기대하므로 오버헤드를 미리 빼서 보고한다).
- **dark space**: LEB 끝에 남은, 노드 하나가 들어가기 애매한 자투리(`dark_wm`). **dead space**: GC로도 못 쓰는 공간.
- **reserved for root**(`rp_size`): 커널이 FS를 새로 만들 때는 5%를 root 전용으로 둔다. `mkfs.ubifs`의 기본값은 0이다(§3 예제 5에서 mkfs 이미지는 `reserved for root: 0 bytes`, 커널이 만든 FS는 `2912989 bytes`).
- **GC용 LEB**: UBIFS는 GC가 쓸 빈 LEB 하나를 항상 잡아 둔다(debugfs `dump_budg`의 `gc_lnum`).

§5 예제 9에서 이것을 실측한다.

### 2.5 압축 · bulk_read · 인증

- **압축**: 데이터 노드(최대 4 KiB 페이지)마다 압축한다. 압축 결과가 더 크면 그 노드는 비압축으로 저장한다(랜덤 데이터). 마운트 옵션 `compr=none|lzo|zlib|zstd`는 **이후 새로 쓰는 데이터**의 압축 방식을 바꾼다. 파일별로 `chattr`로 압축을 끌 수도 있다(UBIFS는 `FS_COMPR_FL` 플래그를 지원한다).
- **bulk_read**: 순차 읽기를 감지하면 같은 LEB 안에 연속해 있는 데이터 노드를 **한 번의 큰 읽기**로 가져온다. 노드가 page 경계에 걸쳐 있는 flash에서 page를 중복으로 읽지 않게 된다. 기본은 꺼져 있다.
- **인증(authentication, 커널 4.20+)**: `CONFIG_UBIFS_FS_AUTHENTICATION`. 인덱스 트리와 master에 HMAC을 걸어 오프라인 변조를 감지한다. mount 옵션 `auth_key=`(커널 keyring의 키 이름)와 `auth_hash_name=`, 이미지 쪽은 `mkfs.ubifs --auth-key/--auth-cert/--hash-algo`로 서명한다. 데이터 기밀성은 fscrypt가 따로 맡는다. **이 노트에서는 실행하지 않았다**(키 관리는 L 모듈 보안에서 다룬다). 위 mount 메시지의 `Mounting in unauthenticated mode`가 이 기능이 꺼진 상태라는 뜻이다.
- **fsck가 없다**: UBIFS에는 오프라인 fsck가 사실상 필요 없다는 것이 설계 목표다(mount 때 replay + recovery가 그 역할). VM의 mtd-utils 2.3.0에는 `/usr/sbin/fsck.ubifs`가 들어 있긴 하지만(최근 추가된 도구다), 정상 운용에서는 mount 시 복구에 맡긴다.

---

## 3. 이미지 만들기와 굽기 — 양산 흐름

### 3.1 큰 그림

빌드 서버에서는 **장치 없이** 이미지를 만든다. 공장(또는 첫 부팅 스크립트, 또는 U-Boot)이 그 이미지를 flash에 굽는다. raw NAND에서는 이 두 단계 모두에 함정이 있다.

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="20.0" y="20.0" font-size="13" text-anchor="start" font-weight="bold">양산 이미지 흐름 (빌드 서버 → 공장/OTA)</text>
  <rect x="20" y="40" width="130" height="34" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="85.0" y="61.3" font-size="12" text-anchor="middle">rootfs/ 디렉터리</text>
  <rect x="20" y="110" width="130" height="34" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="85.0" y="131.3" font-size="12" text-anchor="middle">data/ 디렉터리</text>
  <rect x="185" y="40" width="150" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="260.0" y="61.3" font-size="12" text-anchor="middle">mksquashfs -comp xz</text>
  <rect x="185" y="110" width="150" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="260.0" y="131.3" font-size="12" text-anchor="middle">mkfs.ubifs -m -e -c</text>
  <rect x="370" y="40" width="110" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="425.0" y="61.3" font-size="12" text-anchor="middle">rootfs.sqfs</text>
  <rect x="370" y="110" width="110" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="425.0" y="131.3" font-size="12" text-anchor="middle">data.ubifs</text>
  <line x1="150.0" y1="57.0" x2="183.0" y2="57.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
  <line x1="150.0" y1="127.0" x2="183.0" y2="127.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
  <line x1="335.0" y1="57.0" x2="368.0" y2="57.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
  <line x1="335.0" y1="127.0" x2="368.0" y2="127.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
  <rect x="510" y="60" width="150" height="64" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="585.0" y="80.3" font-size="12" text-anchor="middle">ubinize</text>
  <text x="585.0" y="96.3" font-size="12" text-anchor="middle">-p PEB -m page -s sub</text>
  <text x="585.0" y="112.3" font-size="12" text-anchor="middle">+ ubinize.cfg</text>
  <line x1="480.0" y1="57.0" x2="508.0" y2="78.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
  <line x1="480.0" y1="127.0" x2="508.0" y2="108.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
  <rect x="510" y="160" width="150" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="585.0" y="181.3" font-size="12" text-anchor="middle">ubi.img (PEB 단위)</text>
  <line x1="585.0" y1="124.0" x2="585.0" y2="158.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
  <rect x="330" y="220" width="330" height="62" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="495.0" y="239.3" font-size="12" text-anchor="middle">ubiformat /dev/mtdN -f ubi.img</text>
  <text x="495.0" y="255.3" font-size="12" text-anchor="middle">기존 EC 헤더를 읽어 erase 횟수 보존</text>
  <text x="495.0" y="271.3" font-size="12" text-anchor="middle">0xFF page는 쓰지 않음 · 배드 블록 skip</text>
  <line x1="585.0" y1="194.0" x2="585.0" y2="218.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
  <rect x="20" y="220" width="280" height="62" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="160.0" y="239.3" font-size="12" text-anchor="middle">flash_erase + nandwrite ubi.img</text>
  <text x="160.0" y="255.3" font-size="12" text-anchor="middle">EC 기록 소실 · 0xFF page까지 program</text>
  <text x="160.0" y="271.3" font-size="12" text-anchor="middle">→ 나중에 UBIFS가 같은 page 재-program</text>
  <line x1="510.0" y1="185.0" x2="160.0" y2="218.0" stroke="#d0564a" stroke-width="1.2" stroke-dasharray="5 4" marker-end="url(#ah4)"/>
  <rect x="20" y="304" width="640" height="60" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340.0" y="322.3" font-size="12" text-anchor="middle">대안 경로: U-Boot에서 ubi part ubi; ubi write (볼륨 단위) · 장비에서 ubiupdatevol(OTA, 한 볼륨만 교체)</text>
  <text x="340.0" y="338.3" font-size="12" text-anchor="middle">공장 프로그래머(Xeltek 등)로 raw 이미지를 쓸 때도 'UBI 인식' 모드가 필요 — 같은 이유</text>
  <text x="340.0" y="354.3" font-size="12" text-anchor="middle">-m = 최소 I/O(page) · -e = LEB 크기(PEB − 헤더) · -c = 최대 LEB 수(볼륨이 자랄 상한)</text>
</svg>
```
그림 4 — 양산 이미지 흐름. `mkfs.ubifs`는 UBI 볼륨에 들어갈 **FS 이미지**를, `ubinize`는 그것을 볼륨으로 묶고 EC/VID 헤더를 붙인 **PEB 단위 UBI 이미지**를 만든다. 굽기는 `ubiformat`으로 해야 한다(빨간 경로는 §3.4에서 왜 틀린지 증명한다).

### 3.2 파라미터는 장치 geometry에서 나온다

`mkfs.ubifs`와 `ubinize`의 숫자는 외워서 넣는 것이 아니라 **NAND 데이터시트 + UBI 헤더 배치**에서 계산된다. 이 노트의 nandsim(Samsung 2 KiB page, 128 KiB block, sub-page 512 B) 기준:

| 파라미터 | 도구 · 옵션 | 값 | 계산 |
|---|---|---|---|
| PEB 크기 | `ubinize -p` | 128 KiB | erase block 크기 그대로 |
| min I/O | `ubinize -m`, `mkfs.ubifs -m` | 2048 | NAND page 크기(NOR는 1, SPI-NOR + UBIFS면 보통 1 또는 8) |
| sub-page | `ubinize -s` | 512 | 칩이 sub-page 쓰기를 지원할 때만(`mtdinfo`의 Sub-page size). 아니면 min I/O와 같게 |
| VID 헤더 위치 | (자동) | 512 | sub-page가 있으면 EC 헤더와 같은 page의 두 번째 sub-page |
| LEB 크기 | `mkfs.ubifs -e` | 129024 | PEB − 데이터 오프셋(2048). sub-page가 없으면 PEB − 2 × page = 126976 |
| 최대 LEB 수 | `mkfs.ubifs -c` | 2000 | 이 FS가 자랄 수 있는 상한. 실제 볼륨 LEB 수보다 크게(여유), LPT 크기가 여기에 비례 |

`-e`를 틀리면 attach는 되는데 mount가 `LEB size mismatch`류 에러로 실패한다. 공장에서 칩을 다른 page 크기 제품으로 바꾸면(예: 2 KiB → 4 KiB page) 이미지를 **다시 만들어야 한다** — 같은 바이너리를 여러 칩에 쓰려면 칩별 이미지를 빌드 시스템에서 같이 뽑는다. 이것이 eMMC(블록 장치라 칩 geometry와 무관) 대비 raw NAND의 숨은 운영 비용이다.

### 3.3 예제 4 — 빌드 서버 쪽: squashfs + UBIFS + ubinize

무엇을 확인하나: RO rootfs(squashfs, **static** 볼륨)와 RW data(UBIFS, **dynamic** + `autoresize`) 두 볼륨을 가진 UBI 이미지를 VM에서 만든다. 실제 장비의 rootfs + data 구성을 축소한 것이다.

```sh
#!/bin/bash
# mkimg.sh (VM side): images for a 2 KiB-page / 128 KiB-PEB / 512 B-subpage NAND (nandsim geometry)
set -e
I=/var/tmp/J4/img; rm -rf $I && mkdir -p $I/rootfs $I/data && cd $I
cp -a /var/lab/build/rootfs/. rootfs/                     # BusyBox rootfs as the "RO system image"
mkdir -p data/log data/cfg; echo "robot_id=j4-001" > data/cfg/robot.conf
seq 1 20000 | awk '{printf "2026-10-17T12:%02d:%02d imu ax=%d ay=%d az=981\n", $1/60%60, $1%60, $1%7, $1%11}' > data/log/imu.log
echo "== mksquashfs"; mksquashfs rootfs rootfs.sqfs -comp xz -noappend -quiet -all-root
echo "== mkfs.ubifs"; mkfs.ubifs -r data -m 2048 -e 129024 -c 2000 -x zstd -o data.ubifs -v | sed -n '1,30p'
cat > ubinize.cfg <<CFG
[rootfs]
mode=ubi
image=rootfs.sqfs
vol_id=0
vol_type=static
vol_name=rootfs

[data]
mode=ubi
image=data.ubifs
vol_id=1
vol_type=dynamic
vol_name=data
vol_flags=autoresize
CFG
echo "== ubinize"; ubinize -v -o ubi.img -p 128KiB -m 2048 -s 512 ubinize.cfg
ls -l rootfs.sqfs data.ubifs ubi.img
```

```text
== mksquashfs
[=================================================================|] 20/20 100%
== mkfs.ubifs
mkfs.ubifs
	root:         data/
	min_io_size:  2048
	leb_size:     129024
	max_leb_cnt:  2000
	output:       data.ubifs
	jrn_size:     8388608
	reserved:     0
	keyhash:      r5
	fanout:       8
	orph_lebs:    1
	space_fixup:  0
	selinux file: (null)
	super lebs:   1
	master lebs:  2
	log_lebs:     5
	lpt_lebs:     2
	orph_lebs:    1
	main_lebs:    3
	gc lebs:      1
	index lebs:   1
	leb_cnt:      14
	UUID:         083E0B9E-6AA4-481F-9C41-F56374783187
Success!
== ubinize
ubinize: LEB size:                  129024
ubinize: PEB size:                  131072
ubinize: min. I/O size:             2048
ubinize: sub-page size:             512
ubinize: VID offset:                512
ubinize: data offset:               2048
ubinize: UBI image sequence number: 1713787708
ubinize: loaded the ini-file "ubinize.cfg"
ubinize: count of sections: 2

ubinize: parsing section "rootfs"
ubinize: mode=ubi, keep parsing
ubinize: volume type: static
ubinize: volume ID: 0
ubinize: volume size was not specified in section "rootfs", assume minimum to fit image "rootfs.sqfs"1011712 bytes (988.0 KiB)
...
ubinize: parsing section "data"
...
ubinize: volume size was not specified in section "data", assume minimum to fit image "data.ubifs"1806336 bytes (1.7 MiB)
ubinize: volume name: data
ubinize: volume alignment: 1
ubinize: autoresize flags found
...
ubinize: writing layout volume
ubinize: done
-rw-rw-r-- 1 donh donh 1806336 Oct 17 14:47 data.ubifs
-rw-r--r-- 1 donh donh 1011712 Oct 17 14:47 rootfs.sqfs
-rw-rw-r-- 1 donh donh 3145728 Oct 17 14:47 ubi.img
```

(`ubinize -v`의 볼륨별 반복 줄 일부를 `...`로 생략했다.)

출력에서 볼 것:

- `ubinize`가 LEB 크기를 **스스로 계산해** 129024를 보여 준다. `mkfs.ubifs -e`에 준 값과 같아야 한다 — 빌드 스크립트에서 이 줄을 grep 해 검증하는 것이 좋은 습관이다.
- `jrn_size: 8388608`: journal 8 MiB(기본은 FS 크기에 비례, `-j`로 지정 가능). journal이 클수록 커밋 빈도가 줄어 쓰기 증폭이 낮지만, 전원 차단 후 replay할 양과 mount 시간이 늘어난다. SSD에서 체크포인트 주기를 고르던 트레이드오프와 같다.
- `ubi.img` = 3 MiB = PEB 24개. layout volume 2 PEB + rootfs 8 LEB(1011712 / 129024 올림) + data 14 LEB. **볼륨 크기를 지정하지 않으면 이미지에 딱 맞는 최소 크기**가 되고, `autoresize` 플래그를 받은 볼륨 하나가 첫 attach 때 남은 공간을 전부 가져간다(아래 예제 5).
- `image sequence number`: 이 이미지 고유 번호가 모든 EC 헤더에 들어간다(UBI는 다른 이미지에서 온 PEB가 섞였는지 판별할 때 쓴다). `ubiformat`은 기본적으로 **새 번호를 만들어** 쓴다 — 예제 6에서 차이가 보인다.

`ubi.img` 첫 PEB의 헤더(VM에서 `hexdump`):

```text
00000000  55 42 49 23 01 00 00 00  00 00 00 00 00 00 00 00  |UBI#............|
00000010  00 00 02 00 00 00 08 00  66 26 53 3c 00 00 00 00  |........f&S<....|
00000020  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
00000030  00 00 00 00 00 00 00 00  00 00 00 00 26 98 47 61  |............&.Ga|
00000040
00000200  55 42 49 21 01 01 00 05  7f ff ef ff 00 00 00 00  |UBI!............|
00000210  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
00000230  00 00 00 00 00 00 00 00  00 00 00 00 b8 25 64 a8  |.............%d.|
00000240
```

I11 예제 7에서 본 헤더와 같은 구조인데 **EC 값(0x08~0x0f)이 0**이다. 빌드 서버는 장치의 마모 이력을 모르므로 `ubinize`는 EC를 0으로 쓴다(`-E`로 바꿀 수 있다). 0x14의 `66 26 53 3c` = 0x6626533c = 1713787708 = 위 image sequence number. VID 헤더의 `7f ff ef ff`는 layout volume(`UBI_LAYOUT_VOLUME_ID`)이고, `01` 다음 `01`은 vol_type = dynamic, `05`는 compat 플래그다.

### 3.4 예제 5 — 장치 쪽: `ubiformat` → attach → autoresize → ubiblock + squashfs → UBIFS

무엇을 확인하나: 64 MiB nandsim에 예제 4의 이미지를 `ubiformat`으로 굽고, attach 때 autoresize가 일어나는지, sysfs가 무엇을 보여 주는지, static 볼륨을 `ubiblock`으로 블록 장치화해 squashfs로 mount 되는지, UBIFS 볼륨이 mkfs 이미지(14 LEB)에서 볼륨 크기로 커지는지 본다. 이어서 쓰기를 반복해 erase counter 이력을 만든 뒤 **`ubiformat`으로 다시 구울 때와 `flash_erase + nandwrite`로 구울 때** EC가 어떻게 되는지 비교한다(예제 6).

게스트 스크립트 `g2_ubiflow.sh` (이미지는 virtio 디스크로 넣고 RAM으로 복사):

```sh
export PATH=/mu:/j4:$PATH
head -c $(( $(cat /sys/block/vda/size) * 512 )) /dev/vda > /tmp/ubi.img   # copy: mtd-utils misread a block-device input
K() { dmesg -c | sed 's/^\[[ 0-9.]*\] //' | grep -vE "^nand: |^nandsim|^\[nandsim\]|Creating|^ns_" ; }
modprobe nandsim id_bytes=0xec,0xf1,0x00,0x95 overridesize=9   # 512 PEB x 128 KiB = 64 MiB
modprobe ubi; modprobe ubifs; dmesg -c >/dev/null
echo "### 1. ubiformat the ubinized image (ubi.img, 3 MiB)"
ubiformat /dev/mtd1 -y -f /tmp/ubi.img
echo "### 2. attach"
ubiattach -m 1; K
echo "### 3. sysfs"
ls --color=never /sys/class/ubi/
for v in ubi0_0 ubi0_1; do echo "$v: name=$(cat /sys/class/ubi/$v/name) type=$(cat /sys/class/ubi/$v/type) reserved_ebs=$(cat /sys/class/ubi/$v/reserved_ebs) data_bytes=$(cat /sys/class/ubi/$v/data_bytes) corrupted=$(cat /sys/class/ubi/$v/corrupted)"; done
echo "ubi0: total=$(cat /sys/class/ubi/ubi0/total_eraseblocks) avail=$(cat /sys/class/ubi/ubi0/avail_eraseblocks) max_ec=$(cat /sys/class/ubi/ubi0/max_ec) reserved_for_bad=$(cat /sys/class/ubi/ubi0/reserved_for_bad) fastmap=$(cat /sys/class/ubi/ubi0/fastmap 2>/dev/null)"
echo "### 4. ubiblock + squashfs (RO rootfs), ubifs (RW data)"
ubiblock -c /dev/ubi0_0; ls -l --color=never /dev/ubiblock*
mkdir -p /mnt/r /mnt/d; mount -t squashfs -o ro /dev/ubiblock0_0 /mnt/r && ls --color=never /mnt/r | tr '\n' ' '; echo
mount -t ubifs ubi0:data /mnt/d; K; cat /mnt/d/cfg/robot.conf; df -k /mnt/r /mnt/d; grep -E "ubi" /proc/mounts
echo "### 5. churn the data volume (400 x 2 MiB rewrite) to build erase-count history"
dd if=/dev/urandom of=/tmp/hot bs=64k count=32 2>/dev/null
for i in $(seq 1 400); do cp /tmp/hot /mnt/d/hot; sync; rm /mnt/d/hot; sync; done
echo "after churn: max_ec=$(cat /sys/class/ubi/ubi0/max_ec)"
umount /mnt/d /mnt/r; ubiblock -r /dev/ubi0_0; ubidetach -m 1; dmesg -c > /dev/null
echo "### 6. reflash with ubiformat (keeps erase counters)"
ubiformat /dev/mtd1 -y -f /tmp/ubi.img
ubiattach -m 1 >/dev/null; K | grep -E "erase counter|available PEBs|resized"; ubidetach -m 1 >/dev/null
echo "### 7. reflash with flash_erase + nandwrite (erase counters lost)"
flash_erase -q /dev/mtd1 0 0; nandwrite /dev/mtd1 /tmp/ubi.img | grep -v "^Writing data"; dmesg -c > /dev/null
ubiattach -m 1 >/dev/null; K | grep -E "erase counter|available PEBs|resized|warn|error"; ubidetach -m 1 >/dev/null
```

VM에서 `QEXTRA="-drive if=none,id=d0,file=/var/tmp/J4/img/ubi.img,format=raw,readonly=on -device virtio-blk-device,drive=d0" boot.sh g2_ubiflow.sh`. `ubiformat`의 진행률 출력(`\r`로 갱신되는 수백 줄)은 `[... progress...]`로 줄였다.

```text
lab initramfs: 6.18.55-fs up 1.58s
### 1. ubiformat the ubinized image (ubi.img, 3 MiB)
ubiformat: mtd1 (nand), size 67108864 bytes (64.0 MiB), 512 eraseblocks of 131072 bytes (128.0 KiB), min. I/O size 2048 bytes
[libscan progress...] 
ubiformat: 512 eraseblocks are supposedly empty
[flashing progress...] 
[formatting progress...] 
### 2. attach
UBI device number 0, total 512 LEBs (66060288 bytes, 63.0 MiB), available 0 LEBs (0 bytes), LEB size 129024 bytes (126.0 KiB)
ubi0: default fastmap pool size: 25
ubi0: default fastmap WL pool size: 12
ubi0: attaching mtd1
ubi0: scanning is finished
ubi0: volume 1 ("data") re-sized from 14 to 488 LEBs
ubi0: attached mtd1 (name "NAND simulator partition 0", size 64 MiB)
ubi0: PEB size: 131072 bytes (128 KiB), LEB size: 129024 bytes
ubi0: min./max. I/O unit sizes: 2048/2048, sub-page size 512
ubi0: VID header offset: 512 (aligned 512), data offset: 2048
ubi0: good PEBs: 512, bad PEBs: 0, corrupted PEBs: 0
ubi0: user volume: 2, internal volumes: 1, max. volumes count: 128
ubi0: max/mean erase counter: 0/0, WL threshold: 4096, image sequence number: 1942879874
ubi0: available PEBs: 0, total reserved PEBs: 512, PEBs reserved for bad PEB handling: 10
ubi0: background thread "ubi_bgt0d" started, PID 87
### 3. sysfs
ubi0     ubi0_0   ubi0_1   version
ubi0_0: name=rootfs type=static reserved_ebs=8 data_bytes=1011712 corrupted=0
ubi0_1: name=data type=dynamic reserved_ebs=488 data_bytes=62963712 corrupted=0
ubi0: total=512 avail=0 max_ec=1 reserved_for_bad=10 fastmap=
### 4. ubiblock + squashfs (RO rootfs), ubifs (RW data)
brw-------    1 0        0         253,   0 Oct 17 21:49 /dev/ubiblock0_0
bin dev etc init lib linuxrc mnt proc root sbin sys tmp usr 
block ubiblock0_0: created from ubi0:0(rootfs)
UBIFS (ubi0:1): Mounting in unauthenticated mode
UBIFS (ubi0:1): background thread "ubifs_bgt0_1" started, PID 114
UBIFS (ubi0:1): UBIFS: mounted UBI device 0, volume 1, name "data"
UBIFS (ubi0:1): LEB size: 129024 bytes (126 KiB), min./max. I/O unit sizes: 2048 bytes/2048 bytes
UBIFS (ubi0:1): FS size: 61544448 bytes (58 MiB, 477 LEBs), max 2000 LEBs, journal size 9033728 bytes (8 MiB, 71 LEBs)
UBIFS (ubi0:1): reserved for root: 0 bytes (0 KiB)
UBIFS (ubi0:1): media format: w4/r0 (latest is w5/r0), UUID 083E0B9E-6AA4-481F-9C41-F56374783187, small LPT model
robot_id=j4-001
Filesystem           1K-blocks      Used Available Use% Mounted on
/dev/ubiblock0_0          1024      1024         0 100% /mnt/r
ubi0:data                55696       100     55596   0% /mnt/d
/dev/ubiblock0_0 /mnt/r squashfs ro,relatime,errors=continue 0 0
ubi0:data /mnt/d ubifs rw,relatime,assert=read-only,ubi=0,vol=1 0 0
### 5. churn the data volume (400 x 2 MiB rewrite) to build erase-count history
after churn: max_ec=24
```

출력에서 볼 것 (image sequence number와 PID, 시각은 실행마다 다르다):

- `volume 1 ("data") re-sized from 14 to 488 LEBs`: **autoresize**. 512 PEB − layout 2 − 배드 블록 예약 10(`MTD_UBI_BEB_LIMIT` 20/1024 × 512) − rootfs 8 − 그 밖의 내부 예약 4(WL·EBA용, 그리고 이 커널은 fastmap이 켜져 있어 fastmap용) = 488. 같은 이미지를 128 MiB 칩에 구우면 그 칩에 맞게 커진다 — 칩 용량이 다른 제품 변형을 이미지 하나로 지원하는 방법이다. 한 UBI 장치에서 autoresize 볼륨은 **하나만** 둘 수 있다.
- `ubi0_0 type=static data_bytes=1011712`: static 볼륨은 **데이터 크기와 LEB별 data CRC**를 VID 헤더에 갖고 있어서 정확히 squashfs 크기(1011712)를 안다. attach 때 LEB 개수가 맞는지 보고, 볼륨을 **처음 열 때**(`ubi_open_volume()` → `ubi_check_volume()`) LEB별 CRC를 검사해 틀리면 `corrupted`를 1로 만든다(위 값은 아직 열기 전에 읽은 것). RO 이미지의 무결성 검사를 UBI가 공짜로 해 주는 셈이다(보안 검증은 아니다 — 그건 dm-verity, L 모듈). dynamic 볼륨의 `data_bytes`는 그냥 예약 크기(488 × 129024)다.
- `/dev/ubiblock0_0`(major 253, 동적 block major) — `ubiblock -c`가 `UBI_IOCVOLCRBLK` ioctl로 만든 **읽기 전용** 블록 장치다. squashfs가 그대로 mount 됐다. 커널 부팅 인자로는 `ubi.mtd=<파티션> ubi.block=0,rootfs root=/dev/ubiblock0_0 rootfstype=squashfs`처럼 쓴다(이 부팅 방식은 이 노트에서 실행하지 않았다 — 기억 기반, `drivers/mtd/ubi/block.c`의 `block=` 파라미터는 위 소스 확인으로 존재를 확인했다).
- UBIFS: `FS size: ... 477 LEBs, max 2000 LEBs` — mkfs 이미지의 14 LEB가 **첫 mount에서 볼륨(488 LEB) 크기로 늘어났다**(488 − 477 = UBIFS 관리 영역 11 LEB). `media format: w4/r0 (latest is w5/r0)`: mtd-utils 2.3.0의 `mkfs.ubifs`는 format 4로 만들고 커널은 5까지 안다. `reserved for root: 0 bytes`: mkfs 기본값(§2.4).
- `df`: squashfs 1024 KiB 100% 사용(RO니까 당연), UBIFS 55696 KiB 중 100 KiB 사용.
- 400번 × 2 MiB를 쓰고 지우자 `max_ec=24`가 됐다. 이 이력을 아래 예제 6에서 보존하느냐 잃느냐가 갈린다.

### 3.5 예제 6 — `ubiformat` vs `flash_erase + nandwrite`: 마모 이력과 0xFF page

같은 스크립트의 6·7단계 출력:

```text
### 6. reflash with ubiformat (keeps erase counters)
ubiformat: mtd1 (nand), size 67108864 bytes (64.0 MiB), 512 eraseblocks of 131072 bytes (128.0 KiB), min. I/O size 2048 bytes
[libscan progress...] 
ubiformat: 512 eraseblocks have valid erase counter, mean value is 14
[flashing progress...] 
[formatting progress...] 
ubi0: max/mean erase counter: 25/15, WL threshold: 4096, image sequence number: 110243142
ubi0: available PEBs: 0, total reserved PEBs: 512, PEBs reserved for bad PEB handling: 10
### 7. reflash with flash_erase + nandwrite (erase counters lost)
Written 942 blocks containing only 0xff bytes
Those block may be incorrectly treated as empty!
ubi0: max/mean erase counter: 1/0, WL threshold: 4096, image sequence number: 1713787708
ubi0: available PEBs: 0, total reserved PEBs: 512, PEBs reserved for bad PEB handling: 10
[   45.045022] reboot: Power down
```

출력에서 볼 것:

- **`ubiformat`**: 굽기 전에 장치를 스캔해 `512 eraseblocks have valid erase counter, mean value is 14`를 읽고, 이미지의 PEB를 쓸 때 **그 PEB의 기존 EC + 1**을 EC 헤더에 넣었다. attach 결과 `max/mean erase counter: 25/15` — 마모 이력이 살아 있다. image sequence number도 새로 만들었다(110243142).
- **`flash_erase + nandwrite`**: `max/mean 1/0`. erase 횟수 정보가 **전부 사라졌다**. UBI의 wear leveling은 EC를 기준으로 동작하므로(I11 §3.5), 필드에서 펌웨어를 몇 번 다시 구운 장비는 실제로는 마모가 큰 블록을 "새것"으로 보고 계속 쓴다. image sequence number가 ubinize가 만든 1713787708 그대로인 것도 보인다 — 굽는 장비마다 같은 번호가 된다.
- **`Written 942 blocks containing only 0xff bytes / Those block may be incorrectly treated as empty!`** — `nandwrite` 스스로가 경고한다(여기서 "blocks"는 page 단위 쓰기를 말한다). 이미지 안의 0xFF page(LEB의 빈 꼬리, 빈 log LEB 등)까지 **program 명령으로 썼다**는 뜻이다. UBIFS는 0xFF인 page를 "아직 안 쓴 page"로 보고 나중에 거기에 데이터를 쓴다. 그러면 그 page는 **erase 없이 두 번 program**된다. nandsim은 AND 의미론으로 그냥 넘어가지만(I11 §1.6), 실제 MLC/TLC/대부분의 SLC는 같은 page의 두 번째 program을 허용하지 않거나(NOP 제한) 비트 오류가 생기고, ECC 바이트가 이미 "0xFF 데이터의 ECC"로 써져 있어 새 데이터와 맞지 않는다 → 나중에 읽을 때 ECC 에러. `ubiformat`은 PEB마다 **꼬리의 0xFF page를 잘라내고** 쓴다. (`nandwrite`에도 `-k, --skip-all-ffs` 옵션이 있어 0xFF page 문제는 피할 수 있지만, EC 이력 문제는 그대로 남는다. JFFS2 이미지를 NAND에 구울 때는 `flash_erase -j` + `nandwrite -p -k`가 맞다 — §4 실험은 이미지를 `-o ro`로만 mount 해서 `-k` 없이 구웠다.)
- 한 가지 더: `nandwrite`는 배드 블록을 만나면 **다음 블록으로 밀어서** 쓴다. UBI 이미지는 PEB 위치와 무관해서(attach가 스캔으로 찾는다) 밀려도 동작은 하지만, 이미지 끝이 파티션 밖으로 넘칠 수 있다. `ubiformat`은 배드 블록을 건너뛰고, 이미지가 좋은 블록 수 안에 들어가는지 먼저 확인한다(동작 설명은 mtd-utils 문서 기반, 배드 블록이 있는 경우는 이 노트에서 실행하지 않았다).

공장 프로그래머(오프라인 칩 프로그래머)로 raw 이미지를 칩에 미리 굽는 양산 라인도 같은 문제를 겪는다. 그래서 칩 프로그래머 벤더들이 "UBI 인식 굽기(skip 0xFF pages, bad block handling)" 옵션을 제공한다(벤더별 이름은 다르다 — 기억 기반). 아니면 공장에서는 **부트로더 + 최소 커널만** 굽고, 첫 부팅 때 장비가 스스로 `ubiformat`/`ubiupdatevol`로 나머지를 쓰는 방식이 흔하다.

U-Boot도 UBI를 안다: `ubi part ubi`(MTD 파티션 attach) → `ubi write ${loadaddr} rootfs_a ${filesize}`(볼륨 단위 쓰기) → `ubifsmount ubi0:data` / `ubifsload`. U-Boot의 UBI 쓰기는 EC를 보존한다. 이 명령들은 **이 노트에서 실행하지 않았다**(랩 U-Boot는 QEMU virt용이라 NAND 드라이버가 없다) — C3 노트의 U-Boot 사용법과 U-Boot 문서 기반이다.

### 3.6 OTA에서는 — 볼륨 단위 교체

장비 위에서 rootfs를 바꿀 때는 이미지 전체를 다시 굽지 않고 **볼륨 하나만** 교체한다: `ubiupdatevol /dev/ubi0_2 rootfs_b.sqfs`(내부적으로 `UBI_IOCVOLUP` — 업데이트를 시작할 때 볼륨에 "update marker"를 세우고 끝나야 지운다. 중간에 전원이 나가면 marker가 남아 그 볼륨은 다시 업데이트를 끝내기 전까지 정상 볼륨으로 쓰이지 않는다(sysfs `upd_marker`). 반쯤 쓴 rootfs로 부팅하는 일이 없다) → 검증 → `ubirename`(`UBI_IOCRNVOL`, 여러 볼륨 이름을 **원자적으로** 교환)으로 A/B를 바꾼다. 이 흐름은 L4(OTA) 노트에서 다시 다룬다.

---

## 4. mount 시간 — JFFS2 전체 스캔 vs summary vs UBI 스캔 vs fastmap

### 4.1 무엇을 재나

- **JFFS2(summary 없음)**: `jffs2_scan_medium()`이 모든 블록의 모든 노드를 읽는다 → 데이터 양에 비례해야 한다.
- **JFFS2(summary)**: 블록 끝 summary만 읽는다(`jffs2_sum_scan_sumnode()`) → 거의 일정해야 한다.
- **UBI attach(스캔)**: PEB마다 EC·VID 헤더만 읽는다(I11 예제 7) → **장치 크기(PEB 수)에 비례**, 데이터 양과 무관.
- **UBI attach(fastmap)**: `ubi_scan_fastmap()`이 flash 앞쪽의 fastmap anchor → fastmap 블록을 읽고, fastmap 이후 쓰인 "pool" PEB만 스캔 → 장치 크기와도 거의 무관.
- **UBIFS mount**: master + 인덱스 루트 + replay → 데이터 양과 무관.

### 4.2 예제 7 — 채움 정도별 mount 시간

무엇을 확인하나: 256 MiB nandsim(2048 PEB)에 0 · 32 · 64 · 128 · 192 MiB의 랜덤 데이터 파일(1 MiB × N)을 담은 세 종류 이미지를 굽고, 각각 2번씩 mount/attach 시간을 잰다. 이미지는 VM에서 만든다.

```sh
#!/bin/bash
# mkfill.sh (VM side): random-data trees of N MiB and their JFFS2 (no summary / summary) and UBI images
set -e
F=/var/tmp/J4/fill; mkdir -p $F; cd $F
for n in 0 32 64 128 192; do
  rm -rf d$n && mkdir -p d$n/cfg && echo "fill=$n" > d$n/cfg/fill.txt
  for i in $(seq 1 $n); do head -c 1048576 /dev/urandom > d$n/f$i.bin; done
  mkfs.jffs2 -r d$n -n -e 128KiB -p -o j$n.jffs2
  sumtool -n -e 128KiB -p -i j$n.jffs2 -o js$n.jffs2
  mkfs.ubifs -r d$n -m 2048 -e 129024 -c 2040 -o u$n.ubifs
  printf "[data]\nmode=ubi\nimage=u$n.ubifs\nvol_id=0\nvol_type=dynamic\nvol_name=data\nvol_flags=autoresize\n" > u$n.cfg
  ubinize -o u$n.img -p 128KiB -m 2048 -s 512 u$n.cfg
  rm -rf d$n u$n.ubifs
done
ls -l $F
```

```text
-rw-r--r-- 1 donh donh    131072 Oct 17 14:51 j0.jffs2
-rw-r--r-- 1 donh donh 204865536 Oct 17 14:51 j192.jffs2
-rw-r--r-- 1 donh donh 214827008 Oct 17 14:51 js192.jffs2
...
-rw-rw-r-- 1 donh donh 211156992 Oct 17 14:51 u192.img
```

(`ls -l` 15줄 중 4줄만 남겼다. summary판 `js192`가 `j192`보다 약 10 MB 크다.)

게스트 스크립트 `g4_mount.sh`. `J4DELAY=1`이면 nandsim의 `do_delays=1`로 **실제 NAND 타이밍을 busy-wait로 흉내** 낸다(기본값: page 읽기 접근 25 µs + 바이트당 50 ns 전송 → 2 KiB page 약 127 µs, program 200 µs + 전송, erase 2 ms — `nandsim.c`의 `CONFIG_NANDSIM_*` 기본값).

```sh
export PATH=/mu:/j4:$PATH
for a in $(cat /proc/cmdline); do case $a in J4FILL=*) N=${a#J4FILL=};; J4DELAY=*) DL=${a#J4DELAY=};; esac; done
K() { dmesg -c | sed 's/^\[[ 0-9.]*\] //' | grep -E "$1"; }
slab() { awk '/^jffs2/ {kb+=$2*$4/1024} END {printf "jffs2 slab objects: %d KiB\n", kb}' /proc/slabinfo; }
modprobe nandsim id_bytes=0xec,0xda,0x00,0x95 do_delays=${DL:-0}     # Samsung 256 MiB SLC: 2048 PEB x 128 KiB, 2 KiB page
modprobe jffs2; modprobe ubi; modprobe ubifs; mkdir -p /mnt/j /mnt/u; dmesg -c >/dev/null
echo "### fill=${N} MiB do_delays=${DL:-0}"; mtdinfo /dev/mtd1 | grep -E "Sub-page|Amount"
for d in vda vdb; do
  head -c $(( $(cat /sys/block/$d/size) * 512 )) /dev/$d > /tmp/img
  flash_erase -q -j /dev/mtd1 0 0; nandwrite -q -p /dev/mtd1 /tmp/img; rm /tmp/img
  L=$([ $d = vda ] && echo jffs2_nosum || echo jffs2_sum)
  for r in 1 2; do
    j4_t ${L}_mount$r mount -t jffs2 -o ro mtd1 /mnt/j
    [ $r = 2 ] && { df -k /mnt/j | tail -1; slab; }
    umount /mnt/j
  done
  K "jffs2" | head -3
done
head -c $(( $(cat /sys/block/vdc/size) * 512 )) /dev/vdc > /tmp/img
ubiformat -q -y /dev/mtd1 -f /tmp/img; rm /tmp/img
ubiattach -m 1 >/dev/null; mount -t ubifs ubi0:data /mnt/u; umount /mnt/u; ubidetach -m 1 >/dev/null   # warm-up: autoresize
dmesg -c > /dev/null
for r in 1 2; do
  j4_t ubi_attach_scan$r ubiattach -m 1 | grep TIME; j4_t ubifs_mount_scan$r mount -t ubifs ubi0:data /mnt/u
  umount /mnt/u; ubidetach -m 1 >/dev/null
done
K "attached|fastmap"
echo 1 > /sys/module/ubi/parameters/fm_autoconvert
ubiattach -m 1 >/dev/null; ubidetach -m 1 >/dev/null; dmesg -c >/dev/null      # writes the first fastmap
for r in 1 2; do
  j4_t ubi_attach_fm$r ubiattach -m 1 | grep TIME; j4_t ubifs_mount_fm$r mount -t ubifs ubi0:data /mnt/u
  [ $r = 2 ] && { df -k /mnt/u | tail -1; cat /sys/class/ubi/ubi0/fastmap 2>/dev/null; }
  umount /mnt/u; ubidetach -m 1 >/dev/null
done
K "attached|fastmap"
```

(처음에는 `id_bytes=0xec,0xf1,... overridesize=11`로 256 MiB를 만들려 했는데 erase가 `MEMERASE64 ioctl failed ... error 5`로 전부 실패했다. 128 MiB 칩 ID에 크기만 키우면 row 주소 사이클이 모자라는 것으로 보인다 — 256 MiB 칩 ID `0xda`로 바꿔 해결했다.)

`fm_autoconvert`는 런타임에 바꿀 수 있는 모듈 파라미터(권한 0644)다. 1로 두고 attach → detach 하면 detach 때 fastmap이 써지고, 다음 attach는 `attached by fastmap`이 된다. VM에서 `run4.sh 0 32 64 128 192`(지연 없음), `DLY=1 run4.sh ...`(지연 있음). 지연 없는 쪽은 `J4DELAY` 옵션을 넣기 전 버전의 스크립트로 돌렸다(차이는 `do_delays` 인자와 첫 `echo` 줄뿐이라 헤더가 `### fill=192 MiB`로만 찍혔다). fill=192, 지연 없음 출력:

```text
lab initramfs: 6.18.55-fs up 1.30s
### fill=192 MiB
Amount of eraseblocks:          2048 (268435456 bytes, 256.0 MiB)
Sub-page size:                  512 bytes
Written 38 blocks containing only 0xff bytes
Those block may be incorrectly treated as empty!
TIME jffs2_nosum_mount1 534.2 ms (exit 0)
TIME jffs2_nosum_mount2 521.3 ms (exit 0)
mtd1                    262144    205744     56400  78% /mnt/j
jffs2 slab objects: 1139 KiB
Written 1685 blocks containing only 0xff bytes
Those block may be incorrectly treated as empty!
TIME jffs2_sum_mount1 121.8 ms (exit 0)
TIME jffs2_sum_mount2 89.4 ms (exit 0)
mtd1                    262144    215476     46668  82% /mnt/j
jffs2 slab objects: 1211 KiB
TIME ubi_attach_scan1 66.4 ms (exit 0)
TIME ubifs_mount_scan1 11.4 ms (exit 0)
TIME ubi_attach_scan2 61.6 ms (exit 0)
TIME ubifs_mount_scan2 10.9 ms (exit 0)
ubi0: default fastmap pool size: 100
ubi0: default fastmap WL pool size: 50
ubi0: attached mtd1 (name "NAND simulator partition 0", size 256 MiB)
ubi0: default fastmap pool size: 100
ubi0: default fastmap WL pool size: 50
ubi0: attached mtd1 (name "NAND simulator partition 0", size 256 MiB)
TIME ubi_attach_fm1 66.5 ms (exit 0)
TIME ubifs_mount_fm1 10.8 ms (exit 0)
TIME ubi_attach_fm2 54.0 ms (exit 0)
TIME ubifs_mount_fm2 9.7 ms (exit 0)
ubi0:data               235120    191716     43404  82% /mnt/u
ubi0: default fastmap pool size: 100
ubi0: default fastmap WL pool size: 50
ubi0: attached by fastmap
ubi0: fastmap pool size: 100
ubi0: fastmap WL pool size: 50
ubi0: attached mtd1 (name "NAND simulator partition 0", size 256 MiB)
ubi0: default fastmap pool size: 100
ubi0: default fastmap WL pool size: 50
ubi0: attached by fastmap
ubi0: fastmap pool size: 100
ubi0: fastmap WL pool size: 50
ubi0: attached mtd1 (name "NAND simulator partition 0", size 256 MiB)
[  104.941813] reboot: Power down
```

지연을 켠 실행(`DLY=1`)의 fill=192 출력 앞부분(fastmap 메시지 줄은 지연 없는 쪽과 같아 생략):

```text
lab initramfs: 6.18.55-fs up 9.48s
### fill=192 MiB do_delays=1
Amount of eraseblocks:          2048 (268435456 bytes, 256.0 MiB)
Sub-page size:                  512 bytes
Written 38 blocks containing only 0xff bytes
Those block may be incorrectly treated as empty!
TIME jffs2_nosum_mount1 65537.3 ms (exit 0)
TIME jffs2_nosum_mount2 65381.3 ms (exit 0)
mtd1                    262144    205744     56400  78% /mnt/j
jffs2 slab objects: 1115 KiB
Written 1685 blocks containing only 0xff bytes
Those block may be incorrectly treated as empty!
TIME jffs2_sum_mount1 7487.7 ms (exit 0)
TIME jffs2_sum_mount2 7163.6 ms (exit 0)
mtd1                    262144    215476     46668  82% /mnt/j
jffs2 slab objects: 1187 KiB
TIME ubi_attach_scan1 4773.3 ms (exit 0)
TIME ubifs_mount_scan1 217.6 ms (exit 0)
TIME ubi_attach_scan2 5112.1 ms (exit 0)
TIME ubifs_mount_scan2 215.0 ms (exit 0)
ubi0: default fastmap pool size: 100
ubi0: default fastmap WL pool size: 50
ubi0: attached mtd1 (name "NAND simulator partition 0", size 256 MiB)
ubi0: default fastmap pool size: 100
ubi0: default fastmap WL pool size: 50
ubi0: attached mtd1 (name "NAND simulator partition 0", size 256 MiB)
TIME ubi_attach_fm1 756.2 ms (exit 0)
TIME ubifs_mount_fm1 266.2 ms (exit 0)
TIME ubi_attach_fm2 782.0 ms (exit 0)
TIME ubifs_mount_fm2 146.9 ms (exit 0)
```

두 실행을 합친 표(각 칸: **지연 없음 / 지연 있음**, 2회 평균. 마지막 열은 지연 없는 실행의 slab):

| 채움 | JFFS2 summary 없음 | JFFS2 + summary | UBI 스캔 attach + UBIFS mount | UBI fastmap attach + UBIFS mount | JFFS2 slab (없음/summary) |
|---|---|---|---|---|---|
| 0 MiB | 46 ms / 8.6 s | 44 ms / 9.6 s | 49 + 8 ms / 4.40 + 0.22 s | 35 + 8 ms / 0.70 + 0.20 s | 19 / 19 KiB |
| 32 MiB | 123 ms / 18.7 s | 52 ms / 8.9 s | 56 + 10 ms / 4.84 + 0.23 s | 37 + 10 ms / 0.66 + 0.23 s | 258 / 223 KiB |
| 64 MiB | 179 ms / 27.8 s | 49 ms / 8.6 s | 56 + 8 ms / 4.81 + 0.22 s | 40 + 8 ms / 0.72 + 0.20 s | 386 / 430 KiB |
| 128 MiB | 356 ms / 48.4 s | 60 ms / 8.5 s | 54 + 8 ms / 5.04 + 0.20 s | 41 + 8 ms / 0.67 + 0.22 s | 760 / 864 KiB |
| 192 MiB | 528 ms / 65.5 s | 106 ms / 7.3 s | 64 + 11 ms / 4.94 + 0.22 s | 60 + 10 ms / 0.77 + 0.21 s | 1139 / 1211 KiB |

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="185.0" y="88.0" font-size="13" text-anchor="middle" font-weight="bold">nandsim 지연 없음 (CPU 비용만)</text>
  <line x1="70.0" y1="316.0" x2="300.0" y2="316.0" stroke="currentColor" stroke-width="1.2"/>
  <line x1="70.0" y1="96.0" x2="70.0" y2="316.0" stroke="currentColor" stroke-width="1.2"/>
  <line x1="66.0" y1="316.0" x2="70.0" y2="316.0" stroke="currentColor" stroke-width="1.2"/>
  <line x1="70" y1="316.0" x2="300" y2="316.0" stroke="#888" stroke-opacity="0.3"/>
  <text x="64.0" y="320.0" font-size="12" text-anchor="end">0</text>
  <line x1="66.0" y1="279.3" x2="70.0" y2="279.3" stroke="currentColor" stroke-width="1.2"/>
  <line x1="70" y1="279.3" x2="300" y2="279.3" stroke="#888" stroke-opacity="0.3"/>
  <text x="64.0" y="283.3" font-size="12" text-anchor="end">100</text>
  <line x1="66.0" y1="242.7" x2="70.0" y2="242.7" stroke="currentColor" stroke-width="1.2"/>
  <line x1="70" y1="242.7" x2="300" y2="242.7" stroke="#888" stroke-opacity="0.3"/>
  <text x="64.0" y="246.7" font-size="12" text-anchor="end">200</text>
  <line x1="66.0" y1="206.0" x2="70.0" y2="206.0" stroke="currentColor" stroke-width="1.2"/>
  <line x1="70" y1="206.0" x2="300" y2="206.0" stroke="#888" stroke-opacity="0.3"/>
  <text x="64.0" y="210.0" font-size="12" text-anchor="end">300</text>
  <line x1="66.0" y1="169.3" x2="70.0" y2="169.3" stroke="currentColor" stroke-width="1.2"/>
  <line x1="70" y1="169.3" x2="300" y2="169.3" stroke="#888" stroke-opacity="0.3"/>
  <text x="64.0" y="173.3" font-size="12" text-anchor="end">400</text>
  <line x1="66.0" y1="132.7" x2="70.0" y2="132.7" stroke="currentColor" stroke-width="1.2"/>
  <line x1="70" y1="132.7" x2="300" y2="132.7" stroke="#888" stroke-opacity="0.3"/>
  <text x="64.0" y="136.7" font-size="12" text-anchor="end">500</text>
  <line x1="66.0" y1="96.0" x2="70.0" y2="96.0" stroke="currentColor" stroke-width="1.2"/>
  <line x1="70" y1="96.0" x2="300" y2="96.0" stroke="#888" stroke-opacity="0.3"/>
  <text x="64.0" y="100.0" font-size="12" text-anchor="end">600</text>
  <text x="70.0" y="332.0" font-size="12" text-anchor="middle">0</text>
  <text x="108.3" y="332.0" font-size="12" text-anchor="middle">32</text>
  <text x="146.7" y="332.0" font-size="12" text-anchor="middle">64</text>
  <text x="223.3" y="332.0" font-size="12" text-anchor="middle">128</text>
  <text x="300.0" y="332.0" font-size="12" text-anchor="middle">192</text>
  <text x="185.0" y="350.0" font-size="12" text-anchor="middle">채운 데이터 (MiB)</text>
  <text x="34.0" y="88.0" font-size="12" text-anchor="start">ms</text>
  <polyline points="70.0,299.0 108.3,271.0 146.7,250.5 223.3,185.3 300.0,122.5" fill="none" stroke="#d0564a" stroke-width="2.5"/>
  <circle cx="70.0" cy="299.0" r="3.5" fill="#d0564a"/>
  <circle cx="108.3" cy="271.0" r="3.5" fill="#d0564a"/>
  <circle cx="146.7" cy="250.5" r="3.5" fill="#d0564a"/>
  <circle cx="223.3" cy="185.3" r="3.5" fill="#d0564a"/>
  <circle cx="300.0" cy="122.5" r="3.5" fill="#d0564a"/>
  <polyline points="70.0,299.9 108.3,296.9 146.7,298.2 223.3,294.0 300.0,277.3" fill="none" stroke="#e08a3c" stroke-width="2.5"/>
  <circle cx="70.0" cy="299.9" r="3.5" fill="#e08a3c"/>
  <circle cx="108.3" cy="296.9" r="3.5" fill="#e08a3c"/>
  <circle cx="146.7" cy="298.2" r="3.5" fill="#e08a3c"/>
  <circle cx="223.3" cy="294.0" r="3.5" fill="#e08a3c"/>
  <circle cx="300.0" cy="277.3" r="3.5" fill="#e08a3c"/>
  <polyline points="70.0,294.8 108.3,292.0 146.7,292.6 223.3,293.2 300.0,288.4" fill="none" stroke="#4a7bd0" stroke-width="2.5"/>
  <circle cx="70.0" cy="294.8" r="3.5" fill="#4a7bd0"/>
  <circle cx="108.3" cy="292.0" r="3.5" fill="#4a7bd0"/>
  <circle cx="146.7" cy="292.6" r="3.5" fill="#4a7bd0"/>
  <circle cx="223.3" cy="293.2" r="3.5" fill="#4a7bd0"/>
  <circle cx="300.0" cy="288.4" r="3.5" fill="#4a7bd0"/>
  <polyline points="70.0,300.0 108.3,298.6 146.7,298.3 223.3,297.9 300.0,290.1" fill="none" stroke="#3f9a6b" stroke-width="2.5"/>
  <circle cx="70.0" cy="300.0" r="3.5" fill="#3f9a6b"/>
  <circle cx="108.3" cy="298.6" r="3.5" fill="#3f9a6b"/>
  <circle cx="146.7" cy="298.3" r="3.5" fill="#3f9a6b"/>
  <circle cx="223.3" cy="297.9" r="3.5" fill="#3f9a6b"/>
  <circle cx="300.0" cy="290.1" r="3.5" fill="#3f9a6b"/>
  <text x="535.0" y="88.0" font-size="13" text-anchor="middle" font-weight="bold">do_delays=1 (NAND 타이밍)</text>
  <line x1="420.0" y1="316.0" x2="650.0" y2="316.0" stroke="currentColor" stroke-width="1.2"/>
  <line x1="420.0" y1="96.0" x2="420.0" y2="316.0" stroke="currentColor" stroke-width="1.2"/>
  <line x1="416.0" y1="316.0" x2="420.0" y2="316.0" stroke="currentColor" stroke-width="1.2"/>
  <line x1="420" y1="316.0" x2="650" y2="316.0" stroke="#888" stroke-opacity="0.3"/>
  <text x="414.0" y="320.0" font-size="12" text-anchor="end">0</text>
  <line x1="416.0" y1="284.6" x2="420.0" y2="284.6" stroke="currentColor" stroke-width="1.2"/>
  <line x1="420" y1="284.6" x2="650" y2="284.6" stroke="#888" stroke-opacity="0.3"/>
  <text x="414.0" y="288.6" font-size="12" text-anchor="end">10</text>
  <line x1="416.0" y1="253.1" x2="420.0" y2="253.1" stroke="currentColor" stroke-width="1.2"/>
  <line x1="420" y1="253.1" x2="650" y2="253.1" stroke="#888" stroke-opacity="0.3"/>
  <text x="414.0" y="257.1" font-size="12" text-anchor="end">20</text>
  <line x1="416.0" y1="221.7" x2="420.0" y2="221.7" stroke="currentColor" stroke-width="1.2"/>
  <line x1="420" y1="221.7" x2="650" y2="221.7" stroke="#888" stroke-opacity="0.3"/>
  <text x="414.0" y="225.7" font-size="12" text-anchor="end">30</text>
  <line x1="416.0" y1="190.3" x2="420.0" y2="190.3" stroke="currentColor" stroke-width="1.2"/>
  <line x1="420" y1="190.3" x2="650" y2="190.3" stroke="#888" stroke-opacity="0.3"/>
  <text x="414.0" y="194.3" font-size="12" text-anchor="end">40</text>
  <line x1="416.0" y1="158.9" x2="420.0" y2="158.9" stroke="currentColor" stroke-width="1.2"/>
  <line x1="420" y1="158.9" x2="650" y2="158.9" stroke="#888" stroke-opacity="0.3"/>
  <text x="414.0" y="162.9" font-size="12" text-anchor="end">50</text>
  <line x1="416.0" y1="127.4" x2="420.0" y2="127.4" stroke="currentColor" stroke-width="1.2"/>
  <line x1="420" y1="127.4" x2="650" y2="127.4" stroke="#888" stroke-opacity="0.3"/>
  <text x="414.0" y="131.4" font-size="12" text-anchor="end">60</text>
  <line x1="416.0" y1="96.0" x2="420.0" y2="96.0" stroke="currentColor" stroke-width="1.2"/>
  <line x1="420" y1="96.0" x2="650" y2="96.0" stroke="#888" stroke-opacity="0.3"/>
  <text x="414.0" y="100.0" font-size="12" text-anchor="end">70</text>
  <text x="420.0" y="332.0" font-size="12" text-anchor="middle">0</text>
  <text x="458.3" y="332.0" font-size="12" text-anchor="middle">32</text>
  <text x="496.7" y="332.0" font-size="12" text-anchor="middle">64</text>
  <text x="573.3" y="332.0" font-size="12" text-anchor="middle">128</text>
  <text x="650.0" y="332.0" font-size="12" text-anchor="middle">192</text>
  <text x="535.0" y="350.0" font-size="12" text-anchor="middle">채운 데이터 (MiB)</text>
  <text x="384.0" y="88.0" font-size="12" text-anchor="start">초</text>
  <polyline points="420.0,289.0 458.3,257.1 496.7,228.5 573.3,163.9 650.0,110.3" fill="none" stroke="#d0564a" stroke-width="2.5"/>
  <circle cx="420.0" cy="289.0" r="3.5" fill="#d0564a"/>
  <circle cx="458.3" cy="257.1" r="3.5" fill="#d0564a"/>
  <circle cx="496.7" cy="228.5" r="3.5" fill="#d0564a"/>
  <circle cx="573.3" cy="163.9" r="3.5" fill="#d0564a"/>
  <circle cx="650.0" cy="110.3" r="3.5" fill="#d0564a"/>
  <polyline points="420.0,286.0 458.3,287.9 496.7,289.0 573.3,289.3 650.0,293.0" fill="none" stroke="#e08a3c" stroke-width="2.5"/>
  <circle cx="420.0" cy="286.0" r="3.5" fill="#e08a3c"/>
  <circle cx="458.3" cy="287.9" r="3.5" fill="#e08a3c"/>
  <circle cx="496.7" cy="289.0" r="3.5" fill="#e08a3c"/>
  <circle cx="573.3" cy="289.3" r="3.5" fill="#e08a3c"/>
  <circle cx="650.0" cy="293.0" r="3.5" fill="#e08a3c"/>
  <polyline points="420.0,301.5 458.3,300.1 496.7,300.2 573.3,299.5 650.0,299.8" fill="none" stroke="#4a7bd0" stroke-width="2.5"/>
  <circle cx="420.0" cy="301.5" r="3.5" fill="#4a7bd0"/>
  <circle cx="458.3" cy="300.1" r="3.5" fill="#4a7bd0"/>
  <circle cx="496.7" cy="300.2" r="3.5" fill="#4a7bd0"/>
  <circle cx="573.3" cy="299.5" r="3.5" fill="#4a7bd0"/>
  <circle cx="650.0" cy="299.8" r="3.5" fill="#4a7bd0"/>
  <polyline points="420.0,313.2 458.3,313.2 496.7,313.1 573.3,313.2 650.0,312.9" fill="none" stroke="#3f9a6b" stroke-width="2.5"/>
  <circle cx="420.0" cy="313.2" r="3.5" fill="#3f9a6b"/>
  <circle cx="458.3" cy="313.2" r="3.5" fill="#3f9a6b"/>
  <circle cx="496.7" cy="313.1" r="3.5" fill="#3f9a6b"/>
  <circle cx="573.3" cy="313.2" r="3.5" fill="#3f9a6b"/>
  <circle cx="650.0" cy="312.9" r="3.5" fill="#3f9a6b"/>
  <line x1="20" y1="18" x2="44" y2="18" stroke="#d0564a" stroke-width="2.5"/>
  <text x="50.0" y="22.0" font-size="12" text-anchor="start">JFFS2 (summary 없음)</text>
  <line x1="350" y1="18" x2="374" y2="18" stroke="#e08a3c" stroke-width="2.5"/>
  <text x="380.0" y="22.0" font-size="12" text-anchor="start">JFFS2 + summary</text>
  <line x1="20" y1="38" x2="44" y2="38" stroke="#4a7bd0" stroke-width="2.5"/>
  <text x="50.0" y="42.0" font-size="12" text-anchor="start">UBI 스캔 attach + UBIFS mount</text>
  <line x1="350" y1="38" x2="374" y2="38" stroke="#3f9a6b" stroke-width="2.5"/>
  <text x="380.0" y="42.0" font-size="12" text-anchor="start">UBI fastmap attach + UBIFS mount</text>
</svg>
```
그림 5 — mount(+attach) 시간 대 채운 데이터 양(예제 7 실측, 2회 평균). 왼쪽: nandsim 지연 없음(순수 CPU 비용, ms). 오른쪽: `do_delays=1`로 NAND page 읽기 약 127 µs를 흉내 낸 경우(초).

출력에서 볼 것:

- **JFFS2(summary 없음)는 데이터에 정비례**: 46 → 528 ms(지연 없음), 8.6 → 65.5 초(지연 있음). 노드 하나하나를 읽고 CRC를 확인하기 때문이다. slab(노드마다 `jffs2_raw_node_ref` 등)도 19 KiB → 1139 KiB로 같이 자랐다 — 192 MiB면 4 KiB 노드 약 4.9만 개, 노드당 약 24 B.
- **JFFS2 + summary**는 지연 없을 때 44~106 ms로 거의 평평하다. 그런데 **NAND 지연을 켜면 데이터가 0이어도 8.6~9.6 초**다. 빈 블록에는 summary가 없으니 JFFS2가 블록마다 OOB cleanmarker와 내용이 정말 비었는지를 읽어 확인해야 하기 때문이다(2048 블록 × 여러 page). 오히려 데이터가 많을수록(빈 블록이 적을수록) 7.3 초로 조금 빨라졌다. **JFFS2의 mount 비용은 "노드 수 + 빈 블록 수" — 즉 장치 크기에 비례**한다. 256 MiB NAND에 빈 JFFS2를 mount하는 데 9초는 부팅 예산을 넘는다.
- **UBI 스캔 attach**는 데이터와 무관하게 약 4.4~5.0 초(지연 있음): PEB 2048개의 EC·VID 헤더를 하나씩 읽는다. 지연 없을 때는 50~64 ms라 차이가 안 보였다 — **실제 flash 타이밍을 넣어야 보이는 문제**다.
- **fastmap attach**는 0.66~0.77 초로 약 6~7배 빠르다. fastmap anchor(flash 앞쪽 64 PEB 안)와 fastmap 블록, 그리고 fastmap 이후 쓰인 pool PEB만 읽는다. 출력의 `attached by fastmap`이 그 증거다.
- **UBIFS mount 자체**는 데이터와 무관하게 약 0.2 초(지연 있음) / 10 ms(지연 없음) — master, 인덱스 루트, LPT 일부, 그리고 비어 있는 journal을 읽을 뿐이다.
- 절대값은 TCG 에뮬레이션 + busy-wait라 실제 보드와 다르다(실제 SPI-NAND는 page 읽기가 더 느린 경우가 많다). 비율과 기울기만 본다.


---

## 5. UBIFS 속 들여다보기 — debugfs · dynamic debug · 압축과 `df`

### 5.1 예제 8 — mount 메시지, `dump_budg`, `dump_lprops`, replay 로그, live LEB 지도

무엇을 확인하나: UBIFS가 mount 때 무엇을 읽고(replay), 각 LEB를 어떻게 분류하며(LPT), 공간을 어떻게 예산 잡는지(budgeting)를 커널이 직접 보여 주는 창구로 본다. 64 MiB nandsim(`do_delays=1`)에서 예제 9(§5.2)의 압축 실험을 끝낸 zstd 볼륨을 다시 mount 했다.

게스트 스크립트 `g3_ubifs.sh`의 해당 부분(전체 스크립트는 §5.2와 이어진다):

```sh
dmesg -n 1                                   # keep debugfs dumps off the console
umount /mnt/u; dmesg -c > /dev/null; mount -t ubifs ubi0:data /mnt/u; K
ls --color=never /sys/kernel/debug/ubifs/ /sys/kernel/debug/ubifs/ubi0_0/
echo 1 > /sys/kernel/debug/ubifs/ubi0_0/dump_budg; K | head -24
echo 1 > /sys/kernel/debug/ubifs/ubi0_0/dump_lprops; K | head -12
umount /mnt/u; echo "module ubifs +p" > /sys/kernel/debug/dynamic_debug/control; mount -t ubifs ubi0:data /mnt/u
echo "module ubifs -p" > /sys/kernel/debug/dynamic_debug/control; K > /tmp/dbg; wc -l < /tmp/dbg
grep "UBIFS DBG" /tmp/dbg | cut -d: -f1 | sed 's/ (pid [0-9]*)//' | sort | uniq -c
grep -E "UBIFS DBG mnt|UBIFS DBG rcvry" /tmp/dbg | head -12
umount /mnt/u
j4_unode /dev/ubi0_0 129024 | awk 'NR==1 || $3 != "main" || $4 != "nodes=0"' | head -24
```

```text
UBIFS (ubi0:0): Mounting in unauthenticated mode
UBIFS (ubi0:0): background thread "ubifs_bgt0_0" started, PID 182
UBIFS (ubi0:0): UBIFS: mounted UBI device 0, volume 0, name "data"
UBIFS (ubi0:0): LEB size: 129024 bytes (126 KiB), min./max. I/O unit sizes: 2048 bytes/2048 bytes
UBIFS (ubi0:0): FS size: 62705664 bytes (59 MiB, 486 LEBs), max 496 LEBs, journal size 3096576 bytes (2 MiB, 24 LEBs)
UBIFS (ubi0:0): reserved for root: 2961743 bytes (2892 KiB)
UBIFS (ubi0:0): media format: w5/r0 (latest is w5/r0), UUID 04C8BD32-5425-40EE-97F1-C70580C8F575, small LPT model
/sys/kernel/debug/ubifs/:
chk_fs        chk_index     chk_orphans   ubi0_0
chk_general   chk_lprops    tst_recovery

/sys/kernel/debug/ubifs/ubi0_0/:
chk_fs        chk_index     chk_orphans   dump_lprops   ro_error
chk_general   chk_lprops    dump_budg     dump_tnc      tst_recovery
(pid 73) Budgeting info: data budget sum 0, total budget sum 0
	budg_data_growth 0, budg_dd_growth 0, budg_idx_growth 0
	min_idx_lebs 4, old_idx_sz 105984, uncommitted_idx 0
	page_budget 4144, inode_budget 160, dent_budget 312
	nospace 0, nospace_rp 0
	dark_wm 6144, dead_wm 2048, max_idx_node_sz 192
	freeable_cnt 0, calc_idx_sz 105984, idx_gc_cnt 0
	dirty_pg_cnt 0, dirty_zn_cnt 0, clean_zn_cnt 5
	gc_lnum 12, ihead_lnum 10
	jhead 0 (GC)	 LEB -1
	jhead 1 (base)	 LEB 11
	jhead 2 (data)	 LEB 87
	bud LEB 11
	bud LEB 87
	commit state 0
Budgeting predictions:
	available: 49162960, outstanding 0, free 48567556
(pid 73) start dumping LEB properties
(pid 73) Lprops statistics: empty_lebs 407, idx_lebs  1
	taken_empty_lebs 1, total_free 52721664, total_dirty 66024
	total_used 9811992, total_dark 2530904, total_dead 38592
LEB 10      free 12288    dirty 10752    used 105984   free + dirty 23040    flags 0x30 (index, taken)
LEB 11      free 122880   dirty 5536     used 608      free + dirty 128416   dark 6144 dead 0    nodes fit 30  flags 0x10 (taken, jhead 1 (base))
LEB 12      free 129024   dirty 0        used 0        free + dirty 129024   dark 6144 dead 0    nodes fit 30  flags 0x10 (taken, GC LEB)
LEB 13      free 0        dirty 536      used 128488   free + dirty 536      dark 0    dead 536  nodes fit 0   flags 0x0  (not categorized)
LEB 14      free 0        dirty 160      used 128864   free + dirty 160      dark 0    dead 160  nodes fit 0   flags 0x0  (not categorized)
...
273
     31 UBIFS DBG gen
     13 UBIFS DBG io
      2 UBIFS DBG log
     37 UBIFS DBG lp
     11 UBIFS DBG mnt
    163 UBIFS DBG scan
      7 UBIFS DBG tnc
UBIFS DBG mnt (pid 195): start replaying the journal
UBIFS DBG mnt (pid 195): replay log LEB 5:0
UBIFS DBG mnt (pid 195): commit start sqnum 4556
UBIFS DBG mnt (pid 195): add replay bud LEB 11:6144, head 1
UBIFS DBG mnt (pid 195): add replay bud LEB 87:63488, head 2
UBIFS DBG mnt (pid 195): replay log LEB 3:0
UBIFS DBG mnt (pid 195): replay bud LEB 11, head 1, offs 6144, is_last 1
UBIFS DBG mnt (pid 195): bud LEB 11 replied: dirty 0, free 122880
UBIFS DBG mnt (pid 195): replay bud LEB 87, head 2, offs 63488, is_last 1
UBIFS DBG mnt (pid 195): bud LEB 87 replied: dirty 0, free 65536
UBIFS DBG mnt (pid 195): finished, log head LEB 5:2048, max_sqnum 4563, highest_inum 66
SB: min_io=2048 leb_size=129024 leb_cnt=496 max_leb_cnt=496 log_lebs=3 lpt_lebs=2 orph_lebs=2 default_compr=3
LEB   0 sb     nodes=1    used=4096   rest=0xFF SB:1
LEB   1 master nodes=2    used=544    rest=non-FF PAD:1 MST:1
LEB   2 master nodes=2    used=544    rest=non-FF PAD:1 MST:1
LEB   3 log    nodes=4    used=192    rest=non-FF PAD:1 REF:2 CS:1
LEB   4 log    nodes=0    used=0      rest=0xFF
LEB   5 log    nodes=0    used=0      rest=0xFF
LEB   6 lpt    nodes=0    used=0      rest=non-FF
LEB   7 lpt    nodes=0    used=0      rest=0xFF
LEB   8 orphan nodes=0    used=0      rest=0xFF
LEB   9 orphan nodes=0    used=0      rest=0xFF
LEB  10 main   nodes=2    used=80     rest=non-FF PAD:1 IDX:1
LEB  11 main   nodes=2    used=192    rest=non-FF INO:1 PAD:1
LEB  13 main   nodes=176  used=128520 rest=non-FF DATA:175 PAD:1
LEB  14 main   nodes=176  used=128896 rest=non-FF DATA:175 PAD:1
...
LEB  23 main   nodes=32   used=128496 rest=non-FF DATA:31 PAD:1
```

(`dump_lprops`는 496개 LEB를 전부 찍는다 — 앞 LEB 5개만 남겼다. live LEB 지도도 LEB 15~22(모두 `DATA:174~175`)를 `...`로 줄였다. dmesg의 `[시각]` 접두어는 `sed`로 지웠고, PID·UUID는 실행마다 다르다.)

출력에서 볼 것:

- **커널이 만든 FS**(`ubimkvol` 후 빈 볼륨을 mount → `default file-system created`)는 `media format: w5`, `reserved for root: 2892 KiB`(5%), journal 24 LEB, log 3 LEB, orphan 2 LEB다. 예제 5의 **mkfs.ubifs 이미지**(w4, root 예약 0, journal 71 LEB)와 다르다. 같은 장치라도 "누가 FS를 만들었나"에 따라 기본값이 달라진다 — 양산 이미지와 공장 초기화(장비에서 새로 만든 FS)의 동작 차이로 나타날 수 있다.
- `dump_budg`: `jhead 1 (base) LEB 11`, `jhead 2 (data) LEB 87`, `jhead 0 (GC) LEB -1` — UBIFS의 journal에는 **head가 3개** 있다. base(inode·dentry 메타데이터), data(파일 데이터), GC(GC가 옮기는 노드). 메타데이터와 데이터를 다른 LEB에 쓰면 GC 효율이 좋아진다 — Don의 SSD에서 hot/cold 또는 host/GC 스트림을 다른 블록에 쓰던 multi-stream 설계와 같은 생각이다. `gc_lnum 12`: GC용으로 비워 둔 LEB. `dark_wm 6144`: LEB 끝 6 KiB 미만 자투리는 "dark" 취급.
- `dump_lprops`: LEB 10은 `flags 0x30 (index, taken)` — 인덱스 LEB. 13~17은 꽉 찬 데이터 LEB(free 0, dirty 수백 바이트). 이 표가 LPT의 내용이고, GC는 `free + dirty`가 큰 LEB를 고른다.
- **replay 로그**(dynamic debug): `commit start sqnum 4556` → log LEB 5와 3에서 REF 노드를 읽어 **bud LEB 11(base head)과 87(data head)** 을 찾고, 각 bud의 커밋 이후 부분(LEB 11은 offset 6144부터, 87은 63488부터)만 다시 훑었다. `max_sqnum 4563` — 커밋 이후 노드가 7개뿐이라 mount가 즉시 끝났다. 이것이 "mount 시간이 FS 크기와 무관"의 실체다.
- live LEB 지도: 압축 실험에서 쓴 8 MiB 텍스트(zstd)와 8 MiB 랜덤이 LEB 13~23에 있다. 4 KiB 데이터 노드 175개가 한 LEB(126 KiB)에 들어갔다 = 노드당 평균 약 735 B — 랜덤 데이터라면 노드당 4 KiB + 48 B 헤더라 LEB당 31개뿐이다(LEB 23: `DATA:31`). 텍스트는 노드 단위 zstd로 약 1/5.6이 됐다.

### 5.2 예제 9 — 압축별 `df`, `df`가 말한 것보다 더 들어가는 공간, bulk_read

무엇을 확인하나: (A) 같은 데이터(IMU 로그 흉내 텍스트 7.2 MB, 랜덤 8 MiB)를 압축 방식별로 새 볼륨에 썼을 때 `df` 사용량, (E) `df`의 Avail과 실제로 ENOSPC까지 들어가는 양, (F) `bulk_read`가 순차 읽기를 얼마나 바꾸는지.

```sh
export PATH=/mu:/j4:$PATH
modprobe nandsim id_bytes=0xec,0xf1,0x00,0x95 overridesize=9 do_delays=1   # 64 MiB, realistic page timings
modprobe ubi; modprobe ubifs; mkdir -p /mnt/u
ubiattach -m 1 > /dev/null; dmesg -c > /dev/null
awk 'BEGIN{for(i=0;i<140000;i++) printf "2026-10-17T12:%02d:%02d.%03d imu ax=%d ay=%d az=%d gz=%d\n", i/60000%60, i/1000%60, i%1000, i%97-48, i%53-26, 981+i%7, i%13}' > /tmp/text
head -c 8388608 /dev/urandom > /tmp/rand; ls -l /tmp/text /tmp/rand
used() { sync; df -k /mnt/u | awk 'NR==2 {print $3}'; }
fresh() { umount /mnt/u 2>/dev/null; ubirmvol /dev/ubi0 -N data 2>/dev/null; ubimkvol /dev/ubi0 -N data -m > /dev/null; mount -t ubifs -o $1 ubi0:data /mnt/u; }
echo "### A. df after writing 8 MiB text log and 8 MiB random, per compressor"
for c in none lzo zlib zstd; do
  fresh compr=$c; u0=$(used); cp /tmp/text /mnt/u/; u1=$(used); cp /tmp/rand /mnt/u/; u2=$(used)
  echo "compr=$c base=${u0}KiB text=$((u1-u0))KiB random=$((u2-u1))KiB du: $(du -k /mnt/u/text /mnt/u/rand | tr "\t\n" "  ")"
done
# ... (B, C, D = example 8) ...
echo "### E. df free vs what really fits (compr=none, random data until ENOSPC)"
fresh compr=none; df -k /mnt/u | tail -1
dd if=/dev/urandom of=/mnt/u/fill bs=64k 2>&1 | tail -1; sync; ls -l /mnt/u/fill | awk '{print "fill file bytes:", $5}'; df -k /mnt/u | tail -1
rm /mnt/u/fill; sync; df -k /mnt/u | tail -1
echo "### F. bulk_read (8 MiB random file, cold cache)"
cp /tmp/rand /mnt/u/r; umount /mnt/u
for o in no_bulk_read bulk_read; do
  mount -t ubifs -o $o ubi0:data /mnt/u; for r in 1 2 3; do echo 3 > /proc/sys/vm/drop_caches; j4_t ${o}_$r sh -c "cat /mnt/u/r > /dev/null"; done; umount /mnt/u
done
```

```text
-rw-r--r--    1 0        0          8388608 Oct 17 22:56 /tmp/rand
-rw-r--r--    1 0        0          7372686 Oct 17 22:56 /tmp/text
### A. df after writing 8 MiB text log and 8 MiB random, per compressor
compr=none base=20KiB text=7020KiB random=7968KiB du: 7200 /mnt/u/text 8192 /mnt/u/rand 
compr=lzo base=20KiB text=2280KiB random=7968KiB du: 7200 /mnt/u/text 8192 /mnt/u/rand 
compr=zlib base=20KiB text=1496KiB random=7968KiB du: 7200 /mnt/u/text 8192 /mnt/u/rand 
compr=zstd base=20KiB text=1348KiB random=7968KiB du: 7200 /mnt/u/text 8192 /mnt/u/rand 
...
### E. df free vs what really fits (compr=none, random data until ENOSPC)
ubi0:data                56764        20     53852   0% /mnt/u
59600896 bytes (56.8MB) copied, 34.932353 seconds, 1.6MB/s
fill file bytes: 59600896
ubi0:data                56764     56756         0 100% /mnt/u
ubi0:data                56764        28     53844   0% /mnt/u
### F. bulk_read (8 MiB random file, cold cache)
TIME no_bulk_read_1 4663.4 ms (exit 0)
TIME no_bulk_read_2 4757.7 ms (exit 0)
TIME no_bulk_read_3 5244.9 ms (exit 0)
TIME bulk_read_1 2277.9 ms (exit 0)
TIME bulk_read_2 2367.0 ms (exit 0)
TIME bulk_read_3 2401.2 ms (exit 0)
```

(BusyBox `ls`의 색 제어 문자를 지웠다.)

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="340.0" y="20.0" font-size="13" text-anchor="middle" font-weight="bold">8 MiB급 데이터를 쓴 뒤 df 사용량 증가 (KiB)</text>
  <text x="110.0" y="70.0" font-size="12" text-anchor="end">compr=none</text>
  <rect x="120" y="44" width="402.8" height="18" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <text x="528.8" y="58.0" font-size="12" text-anchor="start">7020</text>
  <rect x="120" y="66" width="457.1" height="18" fill="#888" fill-opacity="0.6" stroke="#888"/>
  <text x="583.1" y="80.0" font-size="12" text-anchor="start">7968</text>
  <text x="110.0" y="128.0" font-size="12" text-anchor="end">compr=lzo</text>
  <rect x="120" y="102" width="130.8" height="18" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <text x="256.8" y="116.0" font-size="12" text-anchor="start">2280</text>
  <rect x="120" y="124" width="457.1" height="18" fill="#888" fill-opacity="0.6" stroke="#888"/>
  <text x="583.1" y="138.0" font-size="12" text-anchor="start">7968</text>
  <text x="110.0" y="186.0" font-size="12" text-anchor="end">compr=zlib</text>
  <rect x="120" y="160" width="85.8" height="18" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <text x="211.8" y="174.0" font-size="12" text-anchor="start">1496</text>
  <rect x="120" y="182" width="457.1" height="18" fill="#888" fill-opacity="0.6" stroke="#888"/>
  <text x="583.1" y="196.0" font-size="12" text-anchor="start">7968</text>
  <text x="110.0" y="244.0" font-size="12" text-anchor="end">compr=zstd</text>
  <rect x="120" y="218" width="77.3" height="18" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <text x="203.3" y="232.0" font-size="12" text-anchor="start">1348</text>
  <rect x="120" y="240" width="457.1" height="18" fill="#888" fill-opacity="0.6" stroke="#888"/>
  <text x="583.1" y="254.0" font-size="12" text-anchor="start">7968</text>
  <rect x="120" y="282" width="14" height="12" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <text x="140.0" y="292.0" font-size="12" text-anchor="start">텍스트 로그 (du 7200 KiB)</text>
  <rect x="340" y="282" width="14" height="12" fill="#888" fill-opacity="0.6" stroke="#888"/>
  <text x="360.0" y="292.0" font-size="12" text-anchor="start">랜덤 (du 8192 KiB)</text>
</svg>
```
그림 6 — 압축 방식별 `df` 사용량 증가(KiB, 예제 9-A 실측). 텍스트 로그는 압축 방식에 따라 7020 → 1348 KiB(약 5.2배 차이), 랜덤 데이터는 어떤 압축이든 7968 KiB로 같다(압축 결과가 더 크면 UBIFS가 그 노드를 비압축으로 저장한다).

출력에서 볼 것:

- **압축**: 텍스트 로그는 zstd 1348 KiB < zlib 1496 < lzo 2280 < none 7020. 로그·설정·JSON 위주의 data 파티션이라면 zstd가 공간을 5배 이상 아낀다(대신 CPU — 저사양 Cortex-A7에서는 쓰기 처리량을 같이 재 봐야 한다. 이 노트는 TCG라 CPU 비교를 하지 않았다). `du`는 압축과 무관하게 논리 크기(7200, 8192 KiB)를 보여 준다.
- **`df`의 "used"는 실제 flash 바이트가 아니다**: 비압축 텍스트 7200 KiB를 썼는데 df used는 7020 KiB, 랜덤 8192 KiB는 7968 KiB만큼 늘었다. `df`의 Size/Free는 `ubifs_reported_space()`가 인덱스·헤더 오버헤드를 미리 빼고 **"이만큼의 파일 데이터"로 환산한** 값이라, used = Size − Free도 환산값이다. 정확한 flash 점유는 debugfs `dump_lprops`(LEB별 used)나 UBI의 `reserved_ebs`로 본다.
- **E: df는 Avail 53852 KiB라고 했는데 실제로는 59600896 B = 58204 KiB가 들어갔다**(+4352 KiB, 약 8%). 이유 두 가지: (1) 우리는 root라 `reserved for root` 2892 KiB도 쓸 수 있다(df의 Avail은 그것을 뺀 값, Used + Avail ≠ Size인 이유). (2) df는 압축 안 되는 데이터·최악의 인덱스 성장을 가정한 보수적 추정이다. 반대로 압축이 잘 되는 데이터라면 df가 말한 것의 몇 배가 들어간다. **앱이 `statvfs()`로 남은 공간을 보고 로그 회전을 결정한다면 이 오차를 알고 있어야 한다** — 남은 공간 판단은 보수적으로, 그리고 ENOSPC 처리 코드는 반드시 있어야 한다.
- 지운 뒤 Used는 28 KiB(처음 20 KiB) — 지운 파일의 공간이 즉시 거의 다 돌아왔다(UBIFS는 삭제를 journal에 기록하고 커밋/GC 때 실제로 회수하지만, budgeting은 바로 반영한다).

### 5.3 bulk_read 결과 읽기

예제 9-F에서 `bulk_read`가 순차 읽기를 2배 빠르게 했다. 이것이 우연인지 보려고 순서를 번갈아(없음 → 있음 → 없음 …) 3라운드를 따로 돌렸다(`g5_bulk.sh`, 같은 64 MiB nandsim · `do_delays=1`):

```sh
export PATH=/mu:/j4:$PATH
dmesg -n 1
modprobe nandsim id_bytes=0xec,0xf1,0x00,0x95 overridesize=9 do_delays=1   # 64 MiB, realistic page timings
modprobe ubi; modprobe ubifs; mkdir -p /mnt/u
ubiattach -m 1 > /dev/null; ubimkvol /dev/ubi0 -N data -m > /dev/null
mount -t ubifs -o compr=none ubi0:data /mnt/u; head -c 8388608 /dev/urandom > /mnt/u/r; umount /mnt/u
for round in 1 2 3; do
  for o in no_bulk_read bulk_read; do
    mount -t ubifs -o $o ubi0:data /mnt/u; echo 3 > /proc/sys/vm/drop_caches
    j4_t ${o}_$round sh -c "cat /mnt/u/r > /dev/null"; umount /mnt/u
  done
done
grep -E "^(nand|ubi)" /proc/mounts; cat /sys/class/mtd/mtd1/writesize /sys/class/mtd/mtd1/subpagesize
```

```text
lab initramfs: 6.18.55-fs up 9.56s
TIME no_bulk_read_1 6868.5 ms (exit 0)
TIME bulk_read_1 3038.3 ms (exit 0)
TIME no_bulk_read_2 6830.8 ms (exit 0)
TIME bulk_read_2 4198.8 ms (exit 0)
TIME no_bulk_read_3 7215.5 ms (exit 0)
TIME bulk_read_3 3113.9 ms (exit 0)
2048
512
[   62.470748] reboot: Power down
```

출력에서 볼 것: 매 라운드 `bulk_read`가 약 2배 빠르다(6.8~7.2 초 → 3.0~4.2 초). 마지막 두 줄은 이 칩의 `writesize` 2048, `subpagesize` 512다. 이유는 page 경계다. UBIFS 데이터 노드는 "4 KiB 데이터 + 48 B 헤더"(랜덤이라 비압축)라 2 KiB NAND page 경계와 어긋나 있고, `bulk_read` 없이는 VFS의 page 하나를 채울 때마다 그 노드가 걸친 NAND page 2~3개를 읽는다. 이웃 노드가 같은 NAND page를 공유하므로 **같은 page를 두 번씩 읽게 된다**(측정한 2배 차이에 대한 내 해석이다 — page 읽기 횟수를 직접 세지는 않았다). `bulk_read`는 순차 패턴을 감지하면 같은 LEB 안 연속 노드 범위를 한 번에 읽어 각 NAND page를 한 번만 읽는다. NAND 접근 지연(page당 약 127 µs)이 지배적일 때 효과가 크고, 지연 없는 nandsim이라면 차이가 거의 안 보일 것이다. 대가는 추가 읽기 버퍼 메모리와 랜덤 읽기에서의 낭비라 기본값이 꺼져 있다. rootfs·큰 로그 파일을 순차로 읽는 장비라면 켜 볼 만하다.

(9-F의 첫 실행과 이 재실행은 다른 작업과 VM CPU를 나눠 쓰는 중에 돌았다 — 절대 시간은 흔들리지만 비율은 두 번 다 약 2배였다.)

---

## 6. 전원 차단 테스트 — fsync한 것은 살아남는가

### 6.1 무엇을 증명해야 하나

Don이 SSD에서 했던 power-loss 테스트와 질문은 같다. **"호스트가 완료(ack)를 받은 쓰기는, 어느 시점에 전원이 끊겨도 다음 부팅 후 읽혀야 한다. 완료를 받지 못한 쓰기는 있어도 되고 없어도 되지만, 반쯤 쓴 쓰레기가 정상 데이터처럼 보이면 안 된다."** Linux 앱에서 "완료"는 `fsync()`의 반환이다.

검증 설계:

- **writer**(`j4_wr`): 256 B 레코드(magic, seq, boot id, payload, CRC32)를 `O_APPEND`로 쓰고 **매번 `fsync()`**, `fsync`가 돌아온 뒤에만 시리얼 콘솔에 `ok <seq>`를 찍는다. 콘솔에 찍힌 마지막 seq = "완료를 받은 마지막 레코드"의 하한.
- **killer**(VM 쪽 `pc.sh`): writer가 시작되면 1~4초(랜덤) 뒤 QEMU 프로세스를 **`kill -9`** 한다. 게스트 커널이 아무것도 정리할 기회가 없다 — 전원 차단과 같다.
- **checker**(`j4_chk`): 다음 부팅에서 FS를 mount 하고 파일을 처음부터 검사한다: magic·CRC·seq 연속성. 끝에 깨진 꼬리(torn tail)가 있으면 잘라 낸다(`-t`, 앱의 로그 복구가 할 일). **유효 레코드 뒤에 또 유효 레코드가 나오면 CORRUPT**(중간이 깨진 것)로 판정한다.
- 판정: 복구된 마지막 seq ≥ 직전 부팅에서 콘솔에 찍힌 마지막 seq 이면 통과.

### 6.2 "전원이 꺼져도 남는 flash"를 QEMU에서 만들기 — 두 번의 실패와 해결

nandsim은 게스트 RAM 안에 있어서 QEMU를 죽이면 flash도 사라진다. 그래서 세 가지를 시도했다.

**시도 1 — QEMU `virt`의 CFI NOR(pflash) + DT 파티션.** `virt.dts`를 복사해 `flash@0` 노드에 `fixed-partitions`를 넣고(`j4_boot` 64 MiB 읽기 전용, `j4_jffs2` 32 MiB, `j4_ubi` 32 MiB) `-dtb`로 부팅했다. pflash 쓰기는 QEMU가 즉시 호스트 파일에 반영하므로 `kill -9`에도 남는다. DT 파티션 자체는 잘 동작했다:

```dts
	flash@0 {
		bank-width = <0x04>;
		reg = <0x00 0x00 0x00 0x4000000 0x00 0x4000000 0x00 0x4000000>;
		compatible = "cfi-flash";

		partitions {
			compatible = "fixed-partitions";
			#address-cells = <0x01>;
			#size-cells = <0x01>;
			partition@0 { label = "j4_boot"; reg = <0x0 0x4000000>; read-only; };
			partition@4000000 { label = "j4_jffs2"; reg = <0x4000000 0x2000000>; };
			partition@6000000 { label = "j4_ubi"; reg = <0x6000000 0x2000000>; };
		};
	};
```

```text
dev:    size   erasesize  name
mtd0: 04000000 00040000 "j4_boot"
mtd1: 02000000 00040000 "j4_jffs2"
mtd2: 02000000 00040000 "j4_ubi"
```

하지만 JFFS2 writer가 약 1200개 레코드째에서 매번 같은 지점에서 죽었다.

```text
WRITER START seq=273 boot=1
[    2.258584] 0.flash: buffer write error (status timeout)
[    2.262859] 0.flash: buffer write error (status timeout)
write: Timer expired
```

raw 쓰기 도구(`j4_norw`: 지운 파티션에 324 B씩 순차 `pwrite`)로 좁혀 보니 `FAIL at off 0xdec0 (chunk 176): Read-only file system` — `cfi_cmdset_0001.c`의 buffer write 경로는 상태 레지스터에 에러 비트가 서고 그중 `0x02`(block locked)가 있으면 `-EROFS`를 돌려준다. 즉 QEMU pflash가 그 시점에 에러 상태를 보고했다. 원인(QEMU pflash 에뮬레이션과 커널 buffer-write 경로의 상호작용으로 보인다)은 끝까지 추적하지 않았다 — **이 노트의 전원 차단 결과는 pflash를 쓰지 않는다.** 두 가지 교훈은 남는다: pflash unit 0에 드라이브를 주면 QEMU가 그것을 **펌웨어로 보고 거기서 부팅을 시도**한다(처음 시도에서 화면에 아무것도 안 나왔다 — 그래서 unit 1만 썼다), 그리고 "flash 쓰기 에러가 `ETIME`(Timer expired)으로 유저에게 올라오는" 경로가 실제로 있다.

**시도 2 — `phram` + 파일로 백업된 게스트 RAM.** `phram`은 물리 메모리 영역을 MTD 장치로 보여 주는 드라이버다(`Image-fs`에 모듈로 있다). QEMU의 게스트 RAM 전체를 `memory-backend-file,share=on`으로 **호스트 파일에 mmap**하고, 커널에는 `mem=384M`을 줘서 위쪽 128 MiB를 RAM으로 쓰지 않게 한 뒤, 그 영역에 phram을 올린다. 게스트가 쓴 바이트는 곧바로 호스트 페이지 캐시의 파일 페이지가 되므로 QEMU를 `kill -9`해도 남는다. 다음 부팅 전에는 아래 384 MiB(진짜 DRAM 역할)를 0으로 지우고 위 128 MiB("flash")만 남긴다.

그런데 업스트림 `phram`을 모듈 파라미터로 올리자 JFFS2 GC 스레드가 바로 oops 했다:

```text
[    1.729033] Unable to handle kernel paging request at virtual address ffff80008bfc0004
[    1.729110]   FSC = 0x21: alignment fault
[    1.731207] CPU: 0 UID: 0 PID: 75 Comm: jffs2_gcd_mtd1 Not tainted 6.18.55-fs #1 PREEMPT 
[    1.732219] pc : __pi_memcpy_generic+0x48/0x22c
[    1.732557] lr : phram_write+0x2c/0x44 [phram]
[    1.733625] x8 : ffff0000041c9000 x7 : e41eb0b10000000c x6 : 0000000c20031985
[    1.733720] x5 : ffff80008bfc000c x4 : ffff80008049bcb4 x3 : ffff80008049bbe0
[    1.733956] Call trace:
[    1.734090]  __pi_memcpy_generic+0x48/0x22c (P)
[    1.734210]  mtd_write_oob_std+0x74/0x80
[    1.734279]  mtd_write_oob+0x64/0xc0
[    1.734335]  mtd_writev+0x114/0x180
[    1.734383]  jffs2_flash_direct_writev+0x38/0x80 [jffs2]
[    1.734467]  jffs2_erase_pending_blocks+0x594/0x75c [jffs2]
[    1.734543]  jffs2_garbage_collect_pass+0x248/0x87c [jffs2]
[    1.734624]  jffs2_garbage_collect_thread+0x100/0x230 [jffs2]
```

(oops 전체 중 핵심 줄만 남겼다.) 해독하면: `phram.c`의 `register_device()`는 DT가 아닌 모듈 파라미터 경로에서 `cached = false` → `ioremap()`으로 매핑한다. arm64에서 `ioremap`은 **Device 메모리** 속성이라 정렬되지 않은 접근이 alignment fault를 낸다. 그런데 `phram_write()`는 그냥 `memcpy()`를 하고, 최적화된 `memcpy`는 8바이트 비정렬 store를 한다(주소 끝이 `...0004`). 레지스터 `x6 = 0x0000000c20031985`를 리틀 엔디언으로 읽으면 magic **0x1985**, nodetype **0x2003**(CLEANMARKER), totlen **0x0c**(12) — JFFS2 GC 스레드가 블록을 막 지우고 **12바이트 cleanmarker를 쓰는 순간**이었다. bare-metal에서 Device-nGnRE 영역에 `memcpy`를 쓰면 안 되는 것과 똑같은 버그다.

**해결**: `phram.c`를 복사해 out-of-tree 모듈 `j4_phram.ko`로 두 줄만 고쳤다 — 파라미터 경로도 `memremap(MEMREMAP_WB)`(Normal 메모리)로 매핑하고, UBI가 요구하는 `writebufsize`를 채운다(처음에는 UBI attach가 `bad write buffer size 0 for 1 min. I/O unit`으로 거부했다).

```sh
mkdir -p /var/tmp/J4/phram && cd /var/tmp/J4/phram
cp /var/lab/src/linux-6.18.55/drivers/mtd/devices/phram.c j4_phram.c
sed -i "s|bool cached = np ? !of_property_read_bool(np, \"no-map\") : false;|bool cached = np ? !of_property_read_bool(np, \"no-map\") : true;\t/* j4: param path cached too (ioremap = Device memory -> memcpy alignment fault on arm64) */|" j4_phram.c
sed -i "s|\tnew->mtd.writesize = 1;|\tnew->mtd.writesize = 1;\n\tnew->mtd.writebufsize = 64;\t/* j4: UBI refuses writebufsize 0 */|" j4_phram.c
diff /var/lab/src/linux-6.18.55/drivers/mtd/devices/phram.c j4_phram.c
echo "obj-m += j4_phram.o" > Kbuild && make -j2 -C /var/lab/out/linux-build-fs M=$PWD modules
modinfo j4_phram.ko | grep vermagic
```

```text
129c129
< 	bool cached = np ? !of_property_read_bool(np, "no-map") : false;
---
> 	bool cached = np ? !of_property_read_bool(np, "no-map") : true;	/* j4: param path cached too (ioremap = Device memory -> memcpy alignment fault on arm64) */
157a158
> 	new->mtd.writebufsize = 64;	/* j4: UBI refuses writebufsize 0 */
  LD [M]  j4_phram.ko
vermagic:       6.18.55-fs SMP preempt mod_unload aarch64
```

(두 번째 `sed`는 UBI 시험에서 attach가 실패한 뒤에 추가했다 — 실제로는 두 번에 나눠 빌드했다. 빌드 로그는 `LD` 줄만 남겼고 경고 0개다.)

이 "flash"는 phram이라 **NOR처럼** 동작한다(write 단위 1바이트, erase = memset 0xFF, OOB·ECC·배드 블록 없음). 그래서 이 테스트는 **FS의 메타데이터 일관성과 fsync 의미론**을 검증하고, NAND 고유의 물리 현상(아래 §6.5)은 검증하지 않는다.

### 6.3 예제 10 — writer, checker, 그리고 kill 루프

```c
/* j4_rec.h: 256-byte log record with sequence number + CRC32 (shared by writer and checker) */
#include <stdint.h>
#include <stddef.h>
#define J4_MAGIC 0x4a345243u		/* "J4RC" */
struct j4_rec { uint32_t magic, seq, boot; uint8_t pay[240]; uint32_t crc; };
_Static_assert(sizeof(struct j4_rec) == 256, "record must be 256 B");
static uint32_t j4_crc32(const void *buf, size_t n)	/* bitwise CRC-32 (IEEE, reflected) */
{
	const uint8_t *p = buf; uint32_t c = 0xffffffffu;
	while (n--) { c ^= *p++; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xedb88320u & -(c & 1)); }
	return ~c;
}
```

```c
/* j4_wr: append 256 B records, fsync() after each, print "ok SEQ" only after fsync returned */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "j4_rec.h"

int main(int argc, char **argv)
{
	if (argc < 3) { fprintf(stderr, "usage: %s FILE BOOTID [max]\n", argv[0]); return 2; }
	int fd = open(argv[1], O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (fd < 0) { perror("open"); exit(1); }
	struct stat st;
	if (fstat(fd, &st)) { perror("fstat"); exit(1); }
	uint32_t seq = st.st_size / sizeof(struct j4_rec);	/* checker truncated a torn tail */
	uint32_t boot = atoi(argv[2]), max = argc > 3 ? (uint32_t)atoi(argv[3]) : 0xffffffffu;
	if (fsync(fd)) { perror("fsync dir entry"); exit(1); }
	printf("WRITER START seq=%u boot=%u\n", seq, boot); fflush(stdout);
	for (uint32_t n = 0; n < max; n++, seq++) {
		struct j4_rec r = { .magic = J4_MAGIC, .seq = seq, .boot = boot };
		for (int i = 0; i < 240; i++) r.pay[i] = (uint8_t)(seq * 31 + i);
		r.crc = j4_crc32(&r, offsetof(struct j4_rec, crc));
		if (write(fd, &r, sizeof(r)) != sizeof(r)) { perror("write"); exit(1); }
		if (fsync(fd)) { perror("fsync"); exit(1); }
		printf("ok %u\n", seq); fflush(stdout);
	}
	return 0;
}
```

```c
/* j4_chk: verify a j4_wr log; -t truncates a torn tail (what an app's log recovery would do) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "j4_rec.h"

int main(int argc, char **argv)
{
	int trunc = argc > 2 && !strcmp(argv[2], "-t");
	if (argc < 2) { fprintf(stderr, "usage: %s FILE [-t]\n", argv[0]); return 2; }
	int fd = open(argv[1], trunc ? O_RDWR : O_RDONLY);
	if (fd < 0) { perror("open"); printf("CHK nofile\n"); return 0; }
	struct j4_rec r; ssize_t n; uint32_t good = 0, boots = 0, lastboot = ~0u; long bad_mid = 0;
	off_t off = 0, tail = 0;
	while ((n = pread(fd, &r, sizeof(r), off)) == sizeof(r)) {
		int ok = r.magic == J4_MAGIC && r.seq == good &&
			 r.crc == j4_crc32(&r, offsetof(struct j4_rec, crc));
		if (!ok) break;
		if (r.boot != lastboot) { boots++; lastboot = r.boot; }
		good++; off += sizeof(r);
	}
	if (n < 0) { perror("pread"); exit(1); }
	off_t end = lseek(fd, 0, SEEK_END);
	tail = end - off;		/* bytes after the last valid record */
	for (off_t o = off + sizeof(r); o + (off_t)sizeof(r) <= end; o += sizeof(r))	/* valid rec AFTER a bad one = real corruption */
		if (pread(fd, &r, sizeof(r), o) == sizeof(r) && r.magic == J4_MAGIC &&
		    r.crc == j4_crc32(&r, offsetof(struct j4_rec, crc))) bad_mid++;
	printf("CHK records=%u last_seq=%d boots=%u tail_bytes=%lld valid_after_bad=%ld %s\n", good,
	       (int)good - 1, boots, (long long)tail, bad_mid, bad_mid ? "CORRUPT" : tail ? "TORN_TAIL" : "CLEAN");
	if (trunc && tail && ftruncate(fd, off)) { perror("ftruncate"); exit(1); }
	if (trunc && tail && fsync(fd)) { perror("fsync"); exit(1); }
	return 0;
}
```

게스트 스크립트(`pc_guest.sh`)와 VM 쪽 kill 루프(`pc.sh`):

```sh
# pc_guest.sh — power-cut guest: mount FS on the persistent "flash", verify+repair the log, then append until killed
export PATH=/mu:/j4:$PATH
for a in $(cat /proc/cmdline); do case $a in J4FS=*) FS=${a#J4FS=};; J4BOOT=*) B=${a#J4BOOT=};; esac; done
insmod /j4/j4_phram.ko phram=j4_nv,0x58000000,96Mi,128ki    # RAM above mem=384M, backed by a host file (survives kill -9)
[ "$B" = 0 ] && { cat /proc/mtd; grep -i "58000000" /proc/iomem; }
mkdir -p /mnt/f
if [ $FS = jffs2 ]; then
  modprobe jffs2; j4_t mount_jffs2 mount -t jffs2 mtd1 /mnt/f
else
  modprobe ubi; modprobe ubifs
  j4_t ubiattach ubiattach -m 1 | grep TIME
  [ -e /dev/ubi0_0 ] || ubimkvol /dev/ubi0 -N data -m > /dev/null
  j4_t mount_ubifs mount -t ubifs ubi0:data /mnt/f
fi
dmesg | sed 's/^\[[ 0-9.]*\] //' | grep -E "jffs2|JFFS2|UBIFS|ubi0" | grep -vE "J4FS|0x0000|version 2.2|fastmap pool|LEB size:|min./max.|VID header|user volume|background thread|PEB size|Mounting in|media format|reserved for root|attaching mtd" | sed "s/^/K: /"
j4_chk /mnt/f/log.bin -t
[ "$B" = final ] && { ls -l /mnt/f; df -k /mnt/f | tail -1; umount /mnt/f; exit 0; }
grep -q " /mnt/f " /proc/mounts || { echo "NOT MOUNTED"; exit 1; }
j4_wr /mnt/f/log.bin $B
```

```sh
#!/bin/bash
# pc.sh (VM side): power-cut loop. usage: pc.sh jffs2|ubifs CYCLES   (hard-kills QEMU with SIGKILL while the writer runs)
FS=$1; N=$2; P=/var/tmp/J4/pc; W=$P/$FS
rm -rf $W && mkdir -p $W/r && cp -a /var/tmp/J4/base/. $W/r/ && cp /var/tmp/J4/bin/* $W/r/j4/
cp /Users/donh/.elinux_scratch/J4/pc_guest.sh $W/r/j4/t.sh
(cd $W/r && find . | cpio -o -H newc --quiet | gzip -1) > $W/i.gz
truncate -s 512M $W/ram.img; head -c 128M /dev/zero | tr '\0' '\377' | dd of=$W/ram.img bs=1M seek=384 conv=notrunc status=none  # guest RAM file; top 128 MiB = erased 'flash'
q() { exec qemu-system-aarch64 -machine virt,memory-backend=ram0 -object memory-backend-file,id=ram0,size=512M,mem-path=$W/ram.img,share=on -cpu cortex-a57 -m 512M -display none -monitor none -serial file:$2 -no-reboot -kernel /var/lab/out/Image-fs \
      -initrd $W/i.gz -append "console=ttyAMA0 quiet mem=384M J4FS=$FS J4BOOT=$1 LAB_CMD=sh~/j4/t.sh" < /dev/null; }
for c in $(seq 0 $N); do
  [ $c = $N ] && c=final
  dd if=/dev/zero of=$W/ram.img bs=1M count=384 conv=notrunc status=none   # DRAM loses its content at power-off; the "flash" (top 128 MiB) keeps it
  q $c $W/c$c.raw > $W/c$c.err 2>&1 &
  QP=$!
  if [ $c != final ]; then
    for t in $(seq 1 900); do grep -q "WRITER START" $W/c$c.raw && break; sleep 0.2; done; grep -q "WRITER START" $W/c$c.raw || echo "cycle $c: writer never started"
    D=$(( 1 + RANDOM % 4 )); sleep $D; kill -9 $QP; wait $QP 2>/dev/null
    echo "cycle $c: killed after ${D}s, last printed: $(tr -d '\r' < $W/c$c.raw | grep '^ok ' | tail -1)"
  else
    ( sleep 300; kill -9 $QP 2>/dev/null ) & WD=$!; wait $QP; kill $WD 2>/dev/null
  fi
  tr -d '\r' < $W/c$c.raw > $W/c$c.txt
done
```

(DRAM 영역을 매 부팅 0으로 지우는 줄은 처음에 없었다. 그때는 이전 부팅의 RAM 내용이 그대로 남아 부팅 초기에 `Unable to handle kernel paging request`가 몇 줄 찍혔다 — 실제 전원 차단에서는 DRAM이 사라지므로 지우는 것이 맞는 모델이다.)

VM에서 `pc.sh jffs2 12; pc.sh ubifs 12` — FS마다 12번 kill + 마지막 검사 부팅 1번. 요약 스크립트 `pcsum.sh`(bash + grep/sed, 15줄)는 각 부팅 출력에서 "직전 부팅의 마지막 `ok`"(`last_ok_prev`)와 "이번 부팅에서 `j4_chk`가 복구한 마지막 seq"(`recovered`)를 나란히 놓고, 뒤의 것이 크거나 같으면 `ok(+차이)`, 작으면 `LOST`를 찍는다. 끝에는 mount 때 본 커널 메시지를 숫자만 `N`으로 바꿔 세었다:


```text
$ pcsum.sh jffs2
boot   mount_ms       last_ok_prev recovered  tail      status
0      8.5            -            -          -         nofile 
1      12.3           4125         4125       0         CLEAN ok(+0)
2      12.6           18758        18758      0         CLEAN ok(+0)
3      12.8           29524        29525      0         CLEAN ok(+1)
4      13.7           44139        44139      0         CLEAN ok(+0)
5      14.1           48313        48314      0         CLEAN ok(+1)
6      15.1           52093        52093      0         CLEAN ok(+0)
7      14.5           56135        56135      0         CLEAN ok(+0)
8      15.6           60159        60159      0         CLEAN ok(+0)
9      18.3           67247        67248      0         CLEAN ok(+1)
10     19.2           80978        80979      0         CLEAN ok(+1)
11     20.8           94318        94318      0         CLEAN ok(+0)
final  20.2           99819        99819      0         CLEAN ok(+0)
--- kernel messages seen at mount (all boots, counted)
$ pcsum.sh ubifs
boot   mount_ms       last_ok_prev recovered  tail      status
0      30.0           -            -          -         nofile 
1      60.1           11119        11120      0         CLEAN ok(+1)
2      55.3           22124        22124      0         CLEAN ok(+0)
3      39.5           36406        36406      0         CLEAN ok(+0)
4      40.4           45062        45062      256       TORN_TAIL ok(+0)
5      46.2           47915        47915      0         CLEAN ok(+0)
6      28.2           60746        60746      0         CLEAN ok(+0)
7      50.6           64128        64129      0         CLEAN ok(+1)
8      48.3           67910        67910      0         CLEAN ok(+0)
9      76.9           81612        81612      0         CLEAN ok(+0)
10     27.6           88551        88552      0         CLEAN ok(+1)
11     46.9           96850        96851      0         CLEAN ok(+1)
final  61.6           106902       106902     0         CLEAN ok(+0)
--- kernel messages seen at mount (all boots, counted)
     13 K: ubiN: scanning is finished
     13 K: ubiN: max/mean erase counter: N/N, WL threshold: N, image sequence number: N
     13 K: ubiN: good PEBs: N, bad PEBs: N, corrupted PEBs: N
     13 K: ubiN: default fastmap WL pool size: N
     13 K: ubiN: available PEBs: N, total reserved PEBs: N, PEBs reserved for bad PEB handling: N
     13 K: ubiN: attached mtdN (name "jN_nv", size N MiB)
     13 K: UBIFS (ubiN:N): UBIFS: mounted UBI device N, volume N, name "data"
     13 K: UBIFS (ubiN:N): FS size: N bytes (N MiB, N LEBs), max N LEBs, journal size N bytes (N MiB, N LEBs)
     12 K: UBIFS (ubiN:N): recovery needed
     12 K: UBIFS (ubiN:N): recovery completed
      1 K: ubiN: empty MTD device detected
      1 K: UBIFS (ubiN:N): default file-system created
```

UBIFS 한 부팅(3번)의 원래 출력과, 깨진 꼬리가 나온 4번 부팅의 검사 줄:

```text
lab initramfs: 6.18.55-fs up 1.46s
TIME ubiattach 50.1 ms (exit 0)
TIME mount_ubifs 39.5 ms (exit 0)
K: ubi0: default fastmap WL pool size: 17
K: ubi0: scanning is finished
K: ubi0: attached mtd1 (name "j4_nv", size 96 MiB)
K: ubi0: good PEBs: 768, bad PEBs: 0, corrupted PEBs: 0
K: ubi0: max/mean erase counter: 2/1, WL threshold: 4096, image sequence number: 899206987
K: ubi0: available PEBs: 0, total reserved PEBs: 768, PEBs reserved for bad PEB handling: 0
K: UBIFS (ubi0:0): recovery needed
K: UBIFS (ubi0:0): recovery completed
K: UBIFS (ubi0:0): UBIFS: mounted UBI device 0, volume 0, name "data"
K: UBIFS (ubi0:0): FS size: 98469888 bytes (93 MiB, 752 LEBs), max 762 LEBs, journal size 4975872 bytes (4 MiB, 38 LEBs)
CHK records=36407 last_seq=36406 boots=3 tail_bytes=0 valid_after_bad=0 CLEAN
WRITER START seq=36407 boot=3
...
ok 45061
ok 45062
--- (kill -9) --- boot 4:
CHK records=45063 last_seq=45062 boots=4 tail_bytes=256 valid_after_bad=0 TORN_TAIL
```

(`--- (kill -9) --- boot 4:` 줄은 두 출력 파일을 이어 붙이며 내가 넣은 구분선이다.)

출력에서 볼 것:

- **24번의 강제 종료(JFFS2 12 + UBIFS 12) 모두에서 `fsync`가 끝났다고 보고된 레코드가 전부 살아남았다**(`LOST` 0건), 중간이 깨진 파일도 0건(`CORRUPT` 0). `ok(+1)`은 "fsync는 끝났지만 `ok` 출력이 시리얼로 나가기 전에 kill이 온" 레코드다. **복구된 데이터가 앱이 알고 있던 것보다 많을 수는 있어도 적지는 않았다** — 이것이 fsync 계약이다.
- **UBIFS 4번 부팅의 `TORN_TAIL`(256 B)**: 3번 부팅이 마지막으로 보고한 것은 `ok 45062`. 4번 부팅에서 파일 크기는 레코드 45063개분인데 마지막 레코드(45063번째, seq 45062 다음)가 CRC 검사를 통과하지 못했다. 즉 **fsync가 끝나지 않은 레코드의 "파일 크기 증가"는 살아남았지만 내용은 유효하지 않았다**(그 자리를 읽으면 쓰다 만 데이터나 0이 나온다). FS 입장에서는 정상이다 — fsync 전의 쓰기는 어떤 상태로 남아도 된다. 그래서 **앱 레코드에 seq + CRC를 넣고 부팅 때 꼬리를 검사해 잘라 내는 코드가 필요하다**(`j4_chk -t`가 그 역할을 해서 5번 부팅은 다시 `CLEAN`). Don의 펌웨어로 치면 "host가 flush 받지 않은 쓰기는 반쯤 쓴 상태로 보일 수 있으니 상위 프로토콜(호스트 FS)이 판단한다"는 계약과 같다.
- **UBIFS: 12번 모두 `recovery needed` → `recovery completed`**. 깨끗한 umount를 하지 않으면 master 노드에 "unclean" 표시가 남고, 다음 mount에서 그림 8의 복구를 한다. 마지막 `final` 부팅도 직전이 kill이었으므로 복구가 있었다. 첫 부팅은 `empty MTD device detected` → `default file-system created`.
- **JFFS2: 커널 메시지 0개.** JFFS2에는 "복구 모드"가 없다. 매 mount가 원래 전체 스캔이라, 끊긴 노드는 CRC가 안 맞아 무시되고 마지막 완전한 노드까지가 FS 상태가 된다. 이번 12번의 kill에서는 노드 중간이 끊긴 경우가 없어서 CRC 경고도 안 나왔다 — phram 쓰기는 `memcpy` 한 번이라 끊길 창이 매우 좁다(진짜 NOR는 워드 단위 program에 µs 단위 시간이 걸려 창이 훨씬 넓다).
- **mount 시간**: JFFS2는 로그가 자라면서 8.5 → 20.8 ms로 늘었다(약 10만 노드, 25 MB) — §4와 같은 "스캔 비용 ∝ 노드 수". UBIFS mount는 recovery를 해도 28~77 ms로 로그 크기와 무관하게 흔들렸다(TCG 노이즈). UBI attach는 768 PEB 스캔에 48~68 ms.
- 1~4초 사이에 부팅당 레코드 수는 JFFS2 3778~14632개, UBIFS 2852~14281개로 비슷했다(TCG + RAM 기반 "flash"라 처리량 비교에는 의미를 두지 않는다).

**하네스 버그 하나(실제로 겪었다)**: 처음 버전의 `pc.sh`는 `q() { qemu-system-aarch64 ...; }`를 `q ... &`로 띄웠는데, 이러면 `$!`가 **QEMU가 아니라 함수를 실행하는 서브셸의 PID**라 `kill -9`가 QEMU를 죽이지 못한다. 죽지 않은 옛 QEMU는 다음 사이클의 DRAM 0 채우기(`dd`) 때 커널이 지워지며 멈췄고, 24개의 좀비 QEMU가 CPU를 먹고 있었다. 함수 안을 `exec qemu-system-aarch64 ...`로 고쳐 다시 돌린 결과가 위 표다(옛 결과는 버렸다). 전원 차단 하네스는 "정말로 끊겼는가"부터 검증해야 한다 — 실제 보드 테스트에서 릴레이가 정말 전원을 끊는지 scope로 확인하는 것과 같은 이야기다.

### 6.4 Don의 SSD 펌웨어 복구와 나란히 놓기

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah8" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="20.0" y="20.0" font-size="13" text-anchor="start" font-weight="bold">전원 차단 뒤 UBIFS mount가 하는 일 (mount_ubifs → ubifs_replay_journal)</text>
  <rect x="20" y="40" width="200" height="70" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="120.0" y="71.3" font-size="12" text-anchor="middle">1. master 2부 읽기</text>
  <text x="120.0" y="87.3" font-size="12" text-anchor="middle">깨진 쪽은 다른 사본으로 복구</text>
  <line x1="220.0" y1="75.0" x2="238.0" y2="75.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah8)"/>
  <rect x="240" y="40" width="200" height="70" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340.0" y="71.3" font-size="12" text-anchor="middle">2. 'recovery needed'</text>
  <text x="340.0" y="87.3" font-size="12" text-anchor="middle">recover_inl_heads: 쓰던 head</text>
  <line x1="440.0" y1="75.0" x2="458.0" y2="75.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah8)"/>
  <rect x="460" y="40" width="200" height="70" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="560.0" y="71.3" font-size="12" text-anchor="middle">3. LPT · 인덱스 루트</text>
  <text x="560.0" y="87.3" font-size="12" text-anchor="middle">마지막 커밋 시점의 상태</text>
  <rect x="20" y="150" width="200" height="70" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="120.0" y="181.3" font-size="12" text-anchor="middle">4. log → bud replay</text>
  <text x="120.0" y="197.3" font-size="12" text-anchor="middle">마지막 bud는 recover_leb</text>
  <line x1="220.0" y1="185.0" x2="238.0" y2="185.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah8)"/>
  <rect x="240" y="150" width="200" height="70" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340.0" y="181.3" font-size="12" text-anchor="middle">5. orphan · 파일 크기</text>
  <text x="340.0" y="197.3" font-size="12" text-anchor="middle">mount_orphans · recover_size</text>
  <line x1="440.0" y1="185.0" x2="458.0" y2="185.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah8)"/>
  <rect x="460" y="150" width="200" height="70" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="560.0" y="181.3" font-size="12" text-anchor="middle">6. GC 커밋 → 완료</text>
  <text x="560.0" y="197.3" font-size="12" text-anchor="middle">'recovery completed'</text>
  <line x1="570.0" y1="110.0" x2="570.0" y2="128.0" stroke="currentColor" stroke-width="1.2"/>
  <line x1="570.0" y1="128.0" x2="120.0" y2="128.0" stroke="currentColor" stroke-width="1.2"/>
  <line x1="120.0" y1="128.0" x2="120.0" y2="148.0" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah8)"/>
  <rect x="20" y="262" width="640" height="70" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340.0" y="285.3" font-size="12" text-anchor="middle">Don의 SSD FW 대응: 맵 체크포인트(= 커밋된 인덱스)</text>
  <text x="340.0" y="301.3" font-size="12" text-anchor="middle">+ 체크포인트 이후 쓴 블록의 spare 태그를 sequence 순으로 다시 읽어 L2P 갱신(= bud replay)</text>
  <text x="340.0" y="317.3" font-size="12" text-anchor="middle">+ 마지막으로 열려 있던 블록의 꼬리 검증·폐기(= recover_leb)</text>
</svg>
```
그림 8 — 전원 차단 뒤 UBIFS mount의 복구 순서(`mount_ubifs()`의 실제 호출 순서: `ubifs_read_master` → "recovery needed" → `ubifs_recover_inl_heads` → `ubifs_lpt_init` → `ubifs_replay_journal`(마지막 bud는 `ubifs_recover_leb`) → `ubifs_mount_orphans` → `ubifs_rcvry_gc_commit` → "recovery completed")와 SSD 펌웨어 대응.

| 복구 문제 | SSD FW (전형적) | JFFS2 | UBIFS | UBI(아래층, I11) |
|---|---|---|---|---|
| 어디까지가 유효한가 | 열린 블록 page를 읽어 ECC/CRC 통과한 마지막 page까지 | 노드 hdr_crc · node_crc · data_crc | 노드 CRC + sqnum, 마지막 bud는 `recover_leb`가 깨진 꼬리를 잘라 냄 | VID 헤더 CRC + data CRC(copy_flag) |
| 같은 논리 주소의 두 사본 | 더 큰 sequence 번호 | 더 큰 version | 더 큰 sqnum | 더 큰 sqnum(LEB 이동 중 끊김) |
| 메타데이터 루트 손상 | 체크포인트 2벌, 최신 유효본 | 루트가 없다(전체 스캔) | master 2부(LEB 1, 2) | layout volume 2부 |
| 반쯤 지운 블록 | erase 완료 표시 없으면 재erase | cleanmarker 없으면 재erase | UBI가 처리 | EC 헤더 없으면 "corrupted/free"로 재erase |
| 진행 중이던 삭제 | 삭제 저널 | 다음 GC가 obsolete 처리 | orphan 영역 | — |

### 6.5 이 테스트가 증명하지 못하는 것

- **NAND의 물리적 power-cut 효과**: MLC/TLC의 **paired page**(upper page를 program하다 끊기면 이미 완료된 lower page까지 깨짐), 끊긴 program/erase가 만드는 **불안정 셀**(읽을 때마다 값이 다름), 다음 부팅 후 시간이 지나서야 비트 오류가 늘어나는 현상. phram은 이런 것을 흉내 내지 않는다. UBI는 SLC 계열을 전제로 설계됐고, MLC raw NAND는 UBI 문서도 권하지 않는다(paired page 문제 때문 — 커뮤니티에서 MLC 지원 패치가 논의됐으나 메인라인에 들어오지 않았다, 기억 기반). Don의 SSD는 이 문제를 펌웨어의 페어 page 백업/소프트 디코딩으로 풀었을 것이다.
- **UBI 자체의 테스트 훅**: UBI debugfs에는 `tst_emulate_power_cut`(N번째 쓰기 뒤부터 쓰기를 무시하고 R/O로 전환), `tst_emulate_bitflips`, `tst_emulate_io_failures`가 있고, UBIFS debugfs에는 `tst_recovery`가 있다(위 예제 8의 `ls`에 보인다). nandsim 위에서 이것들로 수천 번의 "가짜 전원 차단"을 빠르게 돌리는 것이 mtd-utils 테스트 스위트의 방식이다. 이 노트에서는 사용하지 않았다.
- **실제 보드**: 결국 진짜 전원을 끊는 장비(릴레이 + 랜덤 타이밍, 수천~수만 회)로 해야 한다. Don이 SSD 검증에서 하던 그 테스트다. 이 노트의 harness는 그 앞 단계의 "로직 검증" 도구다.

---

## 7. 비트 플립 · 배드 블록이 FS 위에서 보이는 모습

### 7.1 예제 11 — nandsim `bitflips=1 badblocks=20,21`: UBI는 scrub 하고, JFFS2는 조용하다

무엇을 확인하나: 32 MiB nandsim에 공장 배드 블록 2개와 "page 읽기 약 1/1024 확률로 1비트 뒤집기"(`bitflips=1` — `nandsim.c`의 `ns_do_bit_flips()`는 `get_random_u16() < (1 << 6)`일 때 최대 `bitflips`개 비트를 뒤집는다)를 넣고, 같은 12 MiB 파일을 UBIFS와 JFFS2에 각각 써서 캐시를 비우며 20번씩 읽는다.

```sh
export PATH=/mu:/j4:$PATH
dmesg -n 1
K() { dmesg -c | sed 's/^\[[ 0-9.]*\] //'; }
M=/sys/class/mtd/mtd1
modprobe nandsim id_bytes=0xec,0xf1,0x00,0x95 overridesize=8 bitflips=1 badblocks=20,21   # 32 MiB, 1-bit flips on ~1/1024 page reads
modprobe ubi; modprobe ubifs; modprobe jffs2; mkdir -p /mnt/u /mnt/j
head -c 12582912 /dev/urandom > /tmp/rand
echo "### UBI + UBIFS"
ubiattach -m 1 > /dev/null; ubimkvol /dev/ubi0 -N data -m > /dev/null; mount -t ubifs -o compr=none ubi0:data /mnt/u
K | grep -E "bad PEBs|good PEBs"
cp /tmp/rand /mnt/u/r; sync; md5sum /tmp/rand | cut -d" " -f1; dmesg -c > /dev/null
for i in $(seq 1 20); do echo 3 > /proc/sys/vm/drop_caches; cat /mnt/u/r > /dev/null; done
md5sum /mnt/u/r | cut -d" " -f1
echo "corrected_bits=$(cat $M/corrected_bits) ecc_failures=$(cat $M/ecc_failures)"
K > /tmp/k; grep -v nandsim /tmp/k | grep -cE "fixable bit-flip" | sed 's/^/fixable bit-flip messages: /'
grep -cE "scrubbed PEB" /tmp/k | sed 's/^/scrubbed PEB messages: /'
grep -E "fixable bit-flip|scrubbed PEB" /tmp/k | head -6
umount /mnt/u; ubidetach -m 1 > /dev/null
echo "### JFFS2 on the same NAND"
flash_erase -q -j /dev/mtd1 0 0; K | grep -iE "bad|skip" | head -3
mount -t jffs2 mtd1 /mnt/j; K | grep -iE "jffs2" | head -5
c0=$(cat $M/corrected_bits); cp /tmp/rand /mnt/j/r; sync; umount /mnt/j; mount -t jffs2 mtd1 /mnt/j; dmesg -c > /dev/null
for i in $(seq 1 20); do echo 3 > /proc/sys/vm/drop_caches; cat /mnt/j/r > /dev/null; done
md5sum /mnt/j/r | cut -d" " -f1
echo "corrected_bits delta=$(( $(cat $M/corrected_bits) - c0 )) ecc_failures=$(cat $M/ecc_failures)"
K | grep -vE "nandsim|drop_caches" | head -8; echo "(end of JFFS2 messages)"
```

```text
lab initramfs: 6.18.55-fs up 3.58s
### UBI + UBIFS
ubi0: good PEBs: 254, bad PEBs: 2, corrupted PEBs: 0
c49a8a1c0dd12a919b0ae987c1ed85f7
c49a8a1c0dd12a919b0ae987c1ed85f7
corrected_bits=281 ecc_failures=0
fixable bit-flip messages: 248
scrubbed PEB messages: 232
ubi0: fixable bit-flip detected at PEB 86
ubi0: scrubbed PEB 86 (LEB 0:17), data moved to PEB 182
ubi0: fixable bit-flip detected at PEB 96
ubi0: scrubbed PEB 96 (LEB 0:25), data moved to PEB 174
ubi0: fixable bit-flip detected at PEB 105
ubi0: fixable bit-flip detected at PEB 108
### JFFS2 on the same NAND
libmtd: error!: MEMERASE64 ioctl failed for eraseblock 0 (mtd1)
        error 5 (Input/output error)
flash_erase: error!: /dev/mtd1: MTD Erase entire chip failureTrying one by one each sector.
             error 5 (Input/output error)
nand: nand_erase_nand: attempt to erase a bad block at 0x00280000
c49a8a1c0dd12a919b0ae987c1ed85f7
corrected_bits delta=448 ecc_failures=0
(end of JFFS2 messages)
[   27.357026] reboot: Power down
```

(md5 값은 실행마다 다르다 — `/dev/urandom` 데이터. 같은 실행 안에서 원본·UBIFS·JFFS2 세 값이 같은지만 본다.)

출력에서 볼 것:

- **UBI**: `bad PEBs: 2`(block 20, 21 — 공장 배드를 attach 때 제외). 20번 읽는 동안 SW Hamming ECC가 비트 281개를 고쳤고(`corrected_bits`), UBI는 그때마다 MTD에서 `-EUCLEAN`("고쳤지만 비트 오류가 있었다")을 받아 `fixable bit-flip detected at PEB 86` → 그 PEB의 내용을 **다른 PEB로 옮기고**(`scrubbed PEB 86 (LEB 0:17), data moved to PEB 182`) 원래 PEB는 지운다. 이것이 **scrubbing**(`ubi_wl_scrub_peb()` → WL 워커)이다. Don의 SSD에서 read-retry/ECC 정정 비트 수가 임계를 넘으면 블록을 refresh(read reclaim)하던 것과 같다 — 여기서는 정정 비트 **1개만** 나와도 옮겼다. MTD는 한 ECC step에서 정정한 비트 수가 `bitflip_threshold` 이상일 때만 `-EUCLEAN`을 올리는데, raw NAND 코어의 기본값이 `DIV_ROUND_UP(ecc_strength * 3, 4)`(`nand_base.c`)라 Hamming 1비트 ECC면 1이기 때문이다. BCH-8 칩이라면 기본 6비트 이상일 때만 scrub이 일어나고, sysfs `/sys/class/mtd/mtdN/bitflip_threshold`로 조정할 수 있다.
- `fixable` 248 vs `scrubbed` 232: 같은 PEB에 여러 번 비트 플립이 났거나, 이미 scrub 대기 중인 PEB는 다시 옮기지 않는다.
- 이 실험은 **비트 플립이 랜덤하게 매번 새로 생기는** nandsim 모델이라 scrub이 끝없이 일어난다. 실제 NAND의 read disturb/retention 오류는 블록에 누적되므로 scrub 한 번으로 해결된다. 그래도 "scrub 메시지가 폭주한다"는 필드 증상은 칩 마모 말기 · 전압 문제 · ECC 설정 불일치의 신호다.
- **JFFS2**: 비트 448개가 정정됐지만 **커널 메시지가 하나도 없고 아무것도 옮기지 않았다**. JFFS2의 `jffs2_flash_read()`(`fs/jffs2/wbuf.c`)는 `-EUCLEAN`은 물론 `-EBADMSG`까지 "노드 CRC가 걸러 줄 것"이라며 성공(0)으로 바꾼다(소스 주석: "So we return success. :)"). 정정 가능한 오류가 쌓여 정정 불가능해질 때까지 그냥 둔다는 뜻이다 — 큰 NAND에 JFFS2를 쓰지 말아야 하는 또 하나의 이유다.
- `flash_erase`가 배드 블록(0x280000 = block 20)에서 chip erase 실패 → 한 블록씩 재시도하며 배드를 건너뛰었다. `nand_erase_nand: attempt to erase a bad block` — MTD가 배드 블록 erase를 거부한다(BBM 보존).
- 정정 불가능한 오류는 이 실험에서 일어나지 않았다(`ecc_failures=0`). nandsim의 `gravepages=` 파라미터로 "N번 읽으면 데이터를 잃는 page"를 만들 수 있다 — UBI는 그때 `-EBADMSG`를 받고, UBIFS는 해당 노드를 읽는 연산을 `-EUCLEAN`이 아닌 I/O 에러로 실패시킨다(실행하지 않았다, I11 §3.6의 배드 블록 고갈 실험이 가까운 사례다).

---

## 8. 무엇을 고를까 — 선택표와 raw NAND 로봇 보드 레이아웃

### 8.1 선택표

| 기준 | JFFS2 | UBIFS (UBI 위) | squashfs on ubiblock (UBI 위) | ext4 on mtdblock |
|---|---|---|---|---|
| 쓰기 | RW | RW | **RO** | RW(하면 안 됨) |
| 맞는 매체 | 작은 NOR · SPI-NOR | raw NAND · SPI-NAND(SLC), 큰 NOR도 가능 | raw NAND · SPI-NAND · NOR | 없음 |
| 맞는 크기 | 수 MiB ~ 수십 MiB | 수십 MiB ~ 수 GiB | 이미지 크기 그대로 | — |
| mount 시간 | 노드 수에 비례(summary로 완화, NAND는 빈 블록도 스캔) | UBI attach(PEB 수 비례, fastmap으로 완화) + 거의 일정 | UBI attach + 즉시 | — |
| RAM | 노드 수에 비례 | 인덱스 캐시(TNC), 크기와 약하게 비례 | 작음 | — |
| 마모·배드 블록 | JFFS2 자체(단순) | UBI | UBI | 없음(블록 단위 덮어쓰기) |
| 비트 플립 대응 | 무시(§7) | UBI scrub | UBI scrub | 없음 |
| 전원 차단 | 매 mount가 스캔이라 자연히 일관 | journal replay + recovery(§6에서 12/12) | RO라 문제없음(업데이트는 볼륨 단위 원자 교체) | 위험 |
| 압축 | zlib/rtime(/lzo) | lzo/zlib/zstd, 파일별 on/off | xz/zstd/lz4/gzip, 블록 단위 | — |
| 전형적 용도 | U-Boot env 옆의 작은 설정 파티션 | `/data`, 로그, 설정, RW rootfs | RO rootfs(A/B), 커널 옆 복구 이미지 | 하지 말 것 |

기본값으로 정리하면: **"RO 부분은 ubiblock + squashfs, RW 부분은 UBIFS, 둘 다 하나의 UBI 장치 위의 볼륨"**. JFFS2는 작은 SPI-NOR만. 그리고 `/tmp`·런타임 로그 버퍼는 tmpfs(RAM)로 빼서 flash 쓰기 자체를 줄인다(J3).

**SPI-NOR + 무엇?** 16~64 MiB SPI-NOR만 있는 보드(MCU급 Linux, 일부 산업용 게이트웨이)라면: 부트로더·커널·squashfs rootfs는 raw 파티션(또는 UBI static 볼륨), 설정만 JFFS2 또는 UBIFS 작은 파티션. NOR는 erase가 느리고(64 KiB 섹터 erase에 수백 ms가 흔하다 — 데이터시트 확인) erase 수명이 10만 회 수준이라, NOR 위 UBIFS는 `-m 1`(min I/O 1바이트, VID 헤더 오프셋 64)로 만들고 LEB가 작아(PEB 64 KiB − 128 B) 오버헤드 비율이 커진다. 작은 NOR에서는 JFFS2의 단순함이 여전히 장점이다. 이 단락의 SPI-NOR 수치는 **이 노트에서 실행하지 않았다**(데이터시트·UBI 문서 기반). §6의 phram 장치는 write 단위 1바이트인 NOR형 MTD라 UBI·UBIFS가 그 위에서 동작하는 것까지는 확인했다.

### 8.2 raw NAND 로봇 보드의 파티션 설계

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="20.0" y="20.0" font-size="13" text-anchor="start" font-weight="bold">예: 로봇 보드 512 MiB SPI-NAND (128 KiB PEB × 4096) 레이아웃</text>
  <rect x="20" y="36" width="76" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="58.0" y="60.3" font-size="12" text-anchor="middle">SPL ×4</text>
  <rect x="96" y="36" width="76" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="134.0" y="60.3" font-size="12" text-anchor="middle">U-Boot ×2</text>
  <rect x="172" y="36" width="76" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="210.0" y="60.3" font-size="12" text-anchor="middle">env ×2</text>
  <rect x="248" y="36" width="412" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="454.0" y="60.3" font-size="12" text-anchor="middle">ubi (나머지 전부, 하나의 UBI 장치)</text>
  <text x="20.0" y="96.0" font-size="12" text-anchor="start">MTD 파티션(DT fixed-partitions) — SPL·U-Boot는 ROM/SPL이 읽을 수 있게 raw 오프셋, 배드 블록은 사본으로 견딤</text>
  <rect x="20" y="116" width="70" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="55.0" y="140.3" font-size="12" text-anchor="middle">kernel_a</text>
  <rect x="94" y="116" width="70" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="129.0" y="140.3" font-size="12" text-anchor="middle">kernel_b</text>
  <rect x="168" y="116" width="110" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="223.0" y="140.3" font-size="12" text-anchor="middle">rootfs_a</text>
  <rect x="282" y="116" width="110" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="337.0" y="140.3" font-size="12" text-anchor="middle">rootfs_b</text>
  <rect x="396" y="116" width="200" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="496.0" y="140.3" font-size="12" text-anchor="middle">data (UBIFS, autoresize)</text>
  <rect x="600" y="116" width="60" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="630.0" y="140.3" font-size="12" text-anchor="middle">예약</text>
  <text x="20.0" y="176.0" font-size="12" text-anchor="start">UBI 볼륨: kernel·rootfs는 static(squashfs → ubiblock), data만 dynamic(UBIFS). 예약 = UBI의 배드 블록 PEB 풀</text>
  <rect x="20" y="192" width="310" height="92" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="175.0" y="218.3" font-size="12" text-anchor="middle">A/B 업데이트</text>
  <text x="175.0" y="234.3" font-size="12" text-anchor="middle">비활성 슬롯에 ubiupdatevol</text>
  <text x="175.0" y="250.3" font-size="12" text-anchor="middle">→ ubirename로 이름 교체 (원자적)</text>
  <text x="175.0" y="266.3" font-size="12" text-anchor="middle">U-Boot env의 slot 변수 전환</text>
  <rect x="350" y="192" width="310" height="92" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="505.0" y="218.3" font-size="12" text-anchor="middle">왜 UBI 장치를 하나로?</text>
  <text x="505.0" y="234.3" font-size="12" text-anchor="middle">WL·배드 블록 예약은 UBI 장치 안에서만 공유</text>
  <text x="505.0" y="250.3" font-size="12" text-anchor="middle">볼륨을 나눠도 PEB 풀은 하나</text>
  <text x="505.0" y="266.3" font-size="12" text-anchor="middle">(I11 §8.1)</text>
</svg>
```
그림 7 — 512 MiB SPI-NAND 로봇 보드 예시 레이아웃. MTD 파티션은 부트 단계별 raw 영역 + 나머지 전부 하나의 `ubi` 파티션, 그 안에 UBI 볼륨으로 커널 A/B, rootfs A/B(static, squashfs), data(UBIFS, autoresize). 숫자는 설계 예시이지 측정값이 아니다.

설계 규칙:

- **ROM이 읽는 첫 단계(SPL)는 UBI 밖**, ROM이 정한 위치·ECC 형식으로 여러 사본(C2 §5). U-Boot도 보통 raw 파티션에 2벌(SPL이 UBI를 읽을 수 있으면 UBI 볼륨에 넣기도 한다 — U-Boot SPL의 `CONFIG_SPL_UBI`, 기억 기반).
- **U-Boot env는 2벌**(`CONFIG_ENV_OFFSET_REDUND`) — 저장 중 전원 차단 대비. 또는 UBI 볼륨 2개에 env를 둔다(`CONFIG_ENV_IS_IN_UBI`).
- **나머지는 MTD 파티션 하나 = UBI 장치 하나**. WL과 배드 블록 예약은 UBI 장치 안에서만 공유되므로(I11 §8.1) 볼륨으로 나눠야지 MTD 파티션으로 나누면 안 된다.
- **data는 autoresize**, 나머지는 크기 고정. 배드 블록 예약(`MTD_UBI_BEB_LIMIT` 또는 attach 시 `max_beb_per1024`)은 칩 데이터시트의 최소 유효 블록 수(NVB)로 정한다.
- **fastmap**은 큰 NAND(1 GiB 이상)에서 부팅 시간이 중요할 때만, 전원 차단 테스트를 충분히 한 뒤 켠다(§4).
- DT에는 `fixed-partitions`로 MTD 파티션을 적는다(§6.2에서 pflash에 적용한 바로 그 형식). 커널 인자로는 `ubi.mtd=ubi ubi.block=0,rootfs_a root=/dev/ubiblock0_2 rootfstype=squashfs`처럼 부트로더가 슬롯에 맞게 넘긴다(볼륨 번호는 설계에 따라 다르다).

---

## 9. 커널 안에서는 무슨 일이 일어나나

| 유저 동작 | 커널 경로 (6.18 소스 기준) | 드라이버를 쓸 때 다시 만나는 곳 |
|---|---|---|
| `mount -t jffs2 mtd1` | `jffs2_fill_super` → `jffs2_do_mount_fs()` → `jffs2_build_filesystem()` → `jffs2_scan_medium()` → 블록마다 `jffs2_scan_eraseblock()`(summary 있으면 `jffs2_sum_scan_sumnode()`) | MTD 드라이버의 `_read`/`_read_oob` 성능이 곧 mount 시간 |
| JFFS2 쓰기 | `jffs2_write_end` → 노드 생성 → `jffs2_flash_writev()`(NAND면 wbuf, NOR면 `jffs2_flash_direct_writev` → `mtd_writev`) | §6 oops: `mtd_writev` → `mtd_write_oob_std` → 드라이버 `_write` |
| JFFS2 GC | `jffs2_garbage_collect_thread()`(`jffs2_gcd_mtdN`) → `jffs2_garbage_collect_pass()` → `jffs2_erase_pending_blocks()`(erase + cleanmarker) | MTD `_erase` |
| `ubiattach` | `UBI_IOCATT` → `ubi_attach_mtd_dev()` → `ubi_attach()` → fastmap 있으면 `ubi_scan_fastmap()`, 없으면 `scan_all()` | I11 §3 |
| `mount -t ubifs ubi0:data` | `ubifs_fill_super()` → `mount_ubifs()` → `ubifs_read_master()` → (unclean이면 "recovery needed") → `ubifs_lpt_init()` → `ubifs_replay_journal()` → `ubifs_mount_orphans()` → `ubifs_rcvry_gc_commit()` | UBI kernel API `ubi_leb_read/write/change` |
| UBIFS `write()` + `fsync()` | page cache → `ubifs_writepage` → `ubifs_jnl_write_data()`(data head에 노드 append, `ubifs_tnc_add()`로 TNC 갱신) → `ubifs_fsync()` → inode 노드 쓰기 + `ubifs_wbuf_sync_nolock()` | wbuf = min I/O 단위 정렬 쓰기, UBI → MTD `_write` |
| UBIFS 커밋 | `ubifs_bg_thread()` → `ubifs_run_commit()` → `do_commit()`(TNC의 dirty znode를 새 IDX 노드로, master 갱신) | — |
| `df` | `ubifs_statfs()` → `ubifs_get_free_space()` → `ubifs_reported_space()` | §5.2의 오차 원인 |
| `ubiblock -c` | `UBI_IOCVOLCRBLK` → `ubiblock_create()` → blk-mq 장치, `ubiblock_queue_rq()` → work에서 `ubi_read()` | I11 §6 blk-mq 드라이버 구조 그대로 |
| 비트 플립 | MTD가 `-EUCLEAN` → `ubi_io_read()`가 `UBI_IO_BITFLIPS` → `ubi_wl_scrub_peb()` → WL 워커가 복사 | NAND 컨트롤러 드라이버가 정정 비트 수를 정확히 보고해야 한다(`max_bitflips` 반환) |

**드라이버 작성자 관점의 요점**: 이 노트의 모든 FS 동작은 결국 `struct mtd_info`의 `_read/_write/_erase/_block_isbad/_block_markbad`와 ECC 통계(정정 비트 수 반환) 위에 서 있다. raw NAND 컨트롤러 드라이버를 bring-up할 때 FS 수준 테스트로 넘어가기 전에 `mtd_tests`(`mtd_readtest`, `mtd_pagetest`, `mtd_nandbiterrs`, `mtd_torturetest` — `Image-fs`에 모듈로 있다)로 MTD 계약부터 검증하는 이유다. 정정 비트 수를 0으로 보고하는 드라이버는 UBI scrub을 무력화하고, 배드 블록 표시를 잘못 쓰는 드라이버는 UBI 예약을 갉아먹는다. §6의 phram 버그처럼 **매핑 속성(Device vs Normal) 하나**가 FS 전체를 죽이기도 한다.

---

## 10. Embedded 실무에서 다시 보기

### 10.1 장면 1 — "새 보드에서 UBIFS mount가 `bad node`로 실패한다"

증상: 빌드 서버 이미지를 굽고 attach는 되는데 mount에서 `UBIFS error ... bad node type` 또는 `LEB size mismatch`. 점검 순서: (1) 칩의 page·sub-page 크기(`mtdinfo /dev/mtdN`)와 `ubinize -m/-s`, `mkfs.ubifs -m/-e`가 맞는지 — 칩 대체품이 sub-page를 지원하지 않으면 VID 헤더 오프셋이 달라진다. (2) 굽기를 `nandwrite`로 하지 않았는지(§3.5). (3) 부트로더 ECC 설정과 커널 ECC 설정(BCH 강도·step 크기)이 같은지 — U-Boot에서 쓴 page를 커널이 다른 ECC 레이아웃으로 읽으면 전부 ECC 에러다. (4) DT 파티션 오프셋이 굽기 스크립트와 같은지.

### 10.2 장면 2 — "로봇이 부팅에 8초 걸리는데 그중 3초가 flash"

`dmesg` 타임스탬프에서 `ubi0: attaching` ~ `attached` 구간을 본다. §4처럼 PEB 수에 비례하는 스캔이면 fastmap(`ubi.fm_autoconvert=1`, 처음 한 번 attach/detach로 fastmap 생성)으로 줄인다. 이 노트의 측정(nandsim 지연 켬, 256 MiB): 스캔 attach 약 4.4초 → fastmap 0.7초. JFFS2 rootfs라면 summary 이미지로 바꾸거나, 근본적으로 RO rootfs를 squashfs로 옮긴다.

### 10.3 장면 3 — "현장에서 로그가 몇 초씩 사라진다"

로거가 `write()`만 하고 `fsync()`를 안 하면 UBIFS는 page cache와 write buffer에 데이터를 들고 있다가 커밋/writeback 때 쓴다(기본 dirty writeback 주기 수 초). 전원이 나가면 그만큼 사라진다. §6처럼 레코드마다 `fsync()`하면 손실은 0이지만 실제 NAND에서는 fsync마다 journal 노드와 write buffer(최소 1 page)를 내보내므로 처리량이 떨어지고 쓰기 증폭이 커진다(§6의 phram은 RAM이라 이 비용이 드러나지 않았다). 실무 답: 레코드에 seq + CRC를 넣고(§6의 형식), N개 또는 T ms마다 `fsync()`, 부팅 때 checker로 꼬리를 잘라 내는 로그 복구 코드를 앱에 둔다. 손실 허용 창 = fsync 주기가 요구사항이 된다(J5).

### 10.4 장면 4 — "수년 된 장비에서 `ubi0: scrubbed PEB` 메시지가 폭주"

§7의 scrub 메시지가 늘어난다는 것은 정정 가능한 비트 오류가 늘고 있다는 뜻이다(마모, retention, read disturb, 또는 전원/온도 문제). 헬스 모니터에 `/sys/class/mtd/mtdN/corrected_bits`, `ecc_failures`, `/sys/class/ubi/ubi0/max_ec`, `bad_peb_count`, `reserved_for_bad`, `ro_mode`를 올린다. `reserved_for_bad`가 0에 가까워지면(I11 §3.6) 곧 R/O 전환이 온다 — 교체 시점 예측 지표다. Don이 SSD에서 SMART 속성(예비 블록 잔량, ECC 정정 수)을 보던 것과 같은 일이다.

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| UBI 이미지를 `nandwrite`/`dd`로 굽기 | 처음엔 되다가 나중에 ECC 에러, 마모 편중 | 0xFF page까지 program, EC 이력 소실(§3.5: max/mean EC 25/15 → 1/0) | `ubiformat -f`, U-Boot `ubi write`, 또는 UBI 인식 프로그래머 |
| `mkfs.ubifs -e/-m`을 칩과 다르게 | attach는 되는데 mount 실패 | LEB 크기·min I/O 불일치 | `mtdinfo`·`ubinize` 출력의 LEB 크기로 빌드 스크립트 검증 |
| `-c`(max LEB)를 작게 | 큰 칩에서 남는 공간을 못 씀 | FS가 `max_leb_cnt` 이상으로 못 자람 | 제품군 최대 칩 기준으로 여유 있게 |
| autoresize 볼륨 두 개 | `ubinize` 또는 attach 에러 | 장치당 하나만 허용 | 하나(data)만 |
| raw NAND에 ext4 on mtdblock | 빠른 마모, 전원 차단 시 블록 단위 손실 | 블록 장치 흉내가 erase block 단위 RMW | UBIFS 또는 ubiblock+squashfs |
| 큰 NAND에 JFFS2 | 부팅 수 초~수십 초, RAM 사용 증가 | 전체 스캔(§4), 비트 플립 무시(§7) | UBIFS로 이전 |
| `df`로 남은 공간을 정확히 판단 | 로그 회전이 너무 이르거나 ENOSPC | UBIFS `df`는 환산 추정치(§5.2: Avail 53852 KiB, 실제 58204 KiB) | ENOSPC 처리 + 보수적 임계값 |
| 레코드마다 `fsync` | 로깅 처리량 급감, 마모 증가 | UBIFS fsync = journal 노드 + wbuf sync | 배치 fsync + 앱 레벨 seq/CRC 복구 |
| MTD 파티션을 여러 개 만들어 UBI를 따로 attach | 일부 파티션만 빨리 마모 | WL·배드 블록 예약이 UBI 장치 단위 | 한 UBI 장치 + 여러 볼륨 |
| NAND 드라이버가 정정 비트 수를 0으로 보고 | 아무 경고 없이 어느 날 ECC 실패 | UBI scrub이 동작하지 않음 | `mtd_nandbiterrs`로 검증, `max_bitflips` 반환 확인 |

---

## 12. 면접에서 이렇게 말한다

**Q.** What's the difference between JFFS2 and UBIFS, and when would you still use JFFS2?

**A.** JFFS2는 MTD 위에 바로 올라가는 로그 구조 FS로 flash에 인덱스가 없어서 mount 때 전체를 스캔하고 RAM에 지도를 만든다. 그래서 mount 시간과 RAM이 FS 크기에 비례한다. UBIFS는 UBI 볼륨 위에 올라가고 flash에 B+tree 인덱스(wandering tree)와 journal이 있어 mount는 커밋 이후 journal만 replay하면 되고, 마모·배드 블록은 UBI가 맡는다. 제가 측정했을 때 192 MiB에서 JFFS2(summary 없음)는 데이터에 비례해 늘었고 UBIFS mount는 거의 일정했다. JFFS2는 수 MiB짜리 SPI-NOR 설정 파티션 정도에만 쓴다.

> JFFS2 is a log-structured filesystem directly on MTD with no on-flash index, so every mount scans the whole medium and builds the map in RAM — mount time and memory grow with the filesystem. UBIFS sits on a UBI volume, keeps a wandering B+tree index plus a journal on flash, so mount just reads the master node and replays the journal since the last commit, and UBI handles wear leveling and bad blocks. When I measured it, JFFS2 without summaries scaled linearly with data while UBIFS mount stayed flat. I'd only use JFFS2 for a small SPI-NOR config partition today.

**꼬리 질문**: "JFFS2 summary는 뭘 해결하나?" → 블록 끝에 노드 목록을 둬서 블록당 한 번만 읽게 한다. 그래도 NAND의 빈 블록 검사는 남는다(§4 지연 실험).

**Q.** Why can't you flash a UBI image with nandwrite?

**A.** 두 가지다. 첫째, `nandwrite`는 이미지 안의 0xFF page까지 program 한다. UBIFS는 0xFF page를 빈 공간으로 보고 나중에 거기 다시 쓰는데, NAND는 erase 없이 같은 page를 두 번 program하면 안 되고 ECC도 이미 써진 상태라 깨진다. 둘째, 장치에 있던 EC 헤더(erase 횟수)를 지워 wear leveling 이력이 사라진다. 실제로 재현해 보니 `ubiformat`은 EC max/mean 25/15를 보존했고 `flash_erase + nandwrite`는 1/0으로 리셋됐다. `ubiformat`이나 U-Boot의 `ubi write`를 써야 한다.

> Two reasons. nandwrite programs the all-0xFF pages in the image too; UBIFS later treats those pages as free and writes into them again, which is a second program without erase — not allowed on NAND, and the ECC bytes are already burned. And it wipes the erase counters, so wear-leveling history is lost. I reproduced it: ubiformat preserved max/mean EC of 25/15, while flash_erase plus nandwrite reset it to 1/0. Use ubiformat, or ubi write from U-Boot.

**Q.** How does UBIFS survive a power cut? Is there an fsck?

**A.** 커밋된 인덱스는 wandering tree라 제자리 수정이 없고, master 노드가 2부라 하나가 깨져도 된다. 커밋 이후 쓰기는 journal(bud LEB)에 sqnum과 CRC가 붙은 노드로 들어가므로, mount 때 log를 따라 bud를 replay하고 마지막 bud의 깨진 꼬리는 잘라 낸다. orphan 영역으로 열린 채 지워진 inode도 회수한다. 오프라인 fsck에 기대지 않는 설계다. 저는 file-backed phram으로 QEMU를 12번 kill -9 해서 매번 "recovery needed → recovery completed"가 나오고 fsync된 레코드가 전부 살아남는 것을 확인했다.

> The committed index is copy-on-write, and the master node is stored twice, so the root is always recoverable. Everything after the last commit is in the journal as CRC'd nodes with sequence numbers; on mount UBIFS replays the buds listed in the log and truncates a torn tail on the last one, and the orphan area cleans up unlinked-but-open inodes. So it doesn't rely on an offline fsck. I verified it by SIGKILLing QEMU twelve times on a file-backed phram device — every mount logged "recovery needed / recovery completed" and every fsync'ed record survived.

**꼬리 질문**: "그 테스트의 한계는?" → phram은 NOR처럼 동작해서 NAND의 paired page, 반쯤 erase된 셀 같은 물리 현상은 재현하지 못한다. 최종 검증은 실제 보드에서 릴레이로 전원을 끊는 수천 회 테스트다.

**Q.** How would you design the flash layout for a robot with 512 MB of SPI-NAND?

**A.** ROM이 읽는 SPL은 ROM 형식으로 여러 사본, U-Boot 2벌, env 2벌은 raw 파티션. 나머지는 MTD 파티션 하나를 UBI 장치 하나로 attach하고 그 안에 커널 A/B, rootfs A/B는 static 볼륨에 squashfs를 넣어 ubiblock으로 mount, data는 UBIFS에 autoresize. 볼륨을 MTD 파티션으로 쪼개지 않는 이유는 WL과 배드 블록 예약이 UBI 장치 안에서만 공유되기 때문이다. OTA는 비활성 볼륨에 `ubiupdatevol` 후 `ubirename`으로 원자적 교체.

> Raw partitions for the boot chain — SPL in the ROM's format with multiple copies, two U-Boot copies, redundant env. Everything else is one MTD partition attached as one UBI device, with volumes for kernel A/B, rootfs A/B as static volumes holding squashfs mounted through ubiblock, and a UBIFS data volume with autoresize. One UBI device, not many partitions, because wear leveling and the bad-block reserve are only shared inside a UBI device. OTA writes the inactive volume with ubiupdatevol and swaps names atomically with ubirename.

**Q.** UBI attach is slow on boot. What do you do?

**A.** attach는 모든 PEB의 EC·VID 헤더를 읽는 스캔이라 PEB 수에 비례한다. fastmap을 켜면(`CONFIG_MTD_UBI_FASTMAP`, `fm_autoconvert`) 매핑 스냅샷과 pool만 읽어서 크게 준다 — 256 MiB nandsim에 NAND 지연을 켰을 때 4.4초에서 0.7초가 됐다. 대신 fastmap은 복구 경로를 하나 더 만드는 것이라 전원 차단 테스트를 다시 해야 한다. 그리고 RO 부분을 squashfs로 빼서 RW 볼륨을 작게 유지하는 것도 효과가 있다.

> Attach scans the EC and VID headers of every PEB, so it's linear in the number of eraseblocks. Fastmap stores a snapshot of the mapping and only rescans the pool, which in my 256 MB nandsim test with realistic NAND timing cut attach from about 4.4 seconds to 0.7. But fastmap adds its own recovery path, so you redo power-cut testing before shipping it.

**Q.** What happens when NAND reports correctable bit flips?

**A.** MTD가 정정 비트 수가 `bitflip_threshold` 이상이면 `-EUCLEAN`을 돌려주고, UBI는 그 PEB를 scrub한다 — 다른 PEB로 복사하고 원래 것을 지운다. JFFS2는 그걸 성공으로 취급해서 아무것도 안 한다. nandsim `bitflips=1`로 재현하면 UBI는 `fixable bit-flip detected` → `scrubbed PEB`를 수백 번 찍고 데이터 md5는 그대로였다. 드라이버 입장에서는 정정 비트 수를 정확히 보고하는 게 중요하다 — 0으로 보고하면 scrub이 영영 안 일어난다. SSD의 read reclaim과 같은 개념이다.

> MTD returns -EUCLEAN when the corrected bit count reaches bitflip_threshold, and UBI scrubs that PEB — copies it elsewhere and erases the original. JFFS2 just treats it as success. With nandsim bitflips=1, UBI logged hundreds of "fixable bit-flip detected / scrubbed PEB" lines and the data checksum stayed intact. For the driver writer the key is reporting max_bitflips correctly; report zero and scrubbing never happens. It's the same idea as read reclaim in SSD firmware.

**Q.** Why does `df` on UBIFS look wrong?

**A.** UBIFS는 압축 · 노드 헤더 · 인덱스 성장 · LEB 끝 자투리 때문에 진짜 남은 바이트를 미리 알 수 없어서, "이만큼의 비압축 파일을 쓸 수 있다"는 보수적 추정을 `df`로 보고한다. root 예약(커널이 만든 FS는 5%)도 Avail에서 빠진다. 실측에서 df Avail은 53852 KiB였는데 root로 랜덤 데이터 58204 KiB가 들어갔고, 텍스트 로그는 zstd 압축으로 df 사용량이 원본의 1/5이었다. 앱은 `statvfs` 값을 대략치로만 쓰고 ENOSPC를 반드시 처리해야 한다.

> Because of compression, node headers, index growth and LEB tail waste, UBIFS can't know the true free space, so df reports a conservative "you can write at least this much incompressible data" estimate, minus the root reserve. In my test df said 53.8 MB available, but 58.2 MB of random data fit as root, and a text log compressed with zstd used a fifth of its size. Apps should treat statvfs as approximate and always handle ENOSPC.

---

## 13. 직접 해보기

1. **개념**: JFFS2 mount가 NAND에서 "빈" 장치에서도 느린 이유를 §4의 지연 실험 결과로 설명하라. 정답/힌트: summary는 데이터가 든 블록만 줄여 준다. 빈 블록은 cleanmarker(OOB)와 내용이 정말 0xFF인지 확인하려고 읽어야 한다 — 그래서 256 MiB NAND는 데이터 0에서도 수 초가 걸렸다.
2. **계산**: 4 KiB page, sub-page 없음, 256 KiB block의 SPI-NAND에서 `ubinize -p/-m/-s`와 `mkfs.ubifs -m/-e` 값을 구하라. 정답/힌트: `-p 256KiB -m 4096 -s 4096`, LEB = 262144 − 2 × 4096 = 253952, `mkfs.ubifs -m 4096 -e 253952`.
3. **코드**: `j4_unode`를 고쳐 master 노드(LEB 1)의 `cmt_no`, `log_lnum`, `root_lnum`, `root_offs`, `lpt_lnum`을 출력하라. 커밋 전후(`sync` 전후)에 값이 어떻게 바뀌는지 보라. 정답/힌트: `struct ubifs_mst_node`는 ch(24 B) 뒤에 `highest_inum`(8), `cmt_no`(8), `flags`(4), `log_lnum`, `root_lnum`, `root_offs`, `root_len`… 순서. master LEB에는 커밋마다 새 MST 노드가 **뒤에 append**되므로 마지막 유효 노드를 찾아야 한다.
4. **관찰**: UBI debugfs `tst_emulate_power_cut`을 켜고(`/sys/kernel/debug/ubi/ubi0/`) UBIFS에 쓰기를 반복해 UBI가 R/O로 바뀌게 한 뒤, detach → 다시 attach → mount 해서 recovery 메시지를 보라. 정답/힌트: `tst_emulate_power_cut_min/max`로 몇 번째 쓰기 뒤에 "전원이 꺼질지" 범위를 정한다. 이 노트의 kill -9 실험보다 훨씬 빨리 수백 번 돌릴 수 있다.
5. **실험**: §6의 `pc.sh`에서 writer의 `fsync()`를 지우고(그냥 `write()`만) 같은 실험을 UBIFS에서 하라. `ok` 출력과 복구된 seq를 비교하라. 정답/힌트: `LOST`가 나와야 정상이다 — dirty page가 writeback 되기 전에 kill이 오면 `write()`가 성공했던 레코드도 사라진다. JFFS2-on-NOR는 `write()`가 곧바로 flash에 가므로 차이가 작을 것이다.
6. **설계**: 64 MiB SPI-NOR만 있는 게이트웨이에 커널 6 MiB, rootfs 20 MiB, 설정 1 MiB, 로그 버퍼를 두려 한다. 파티션과 FS를 정하라. 정답/힌트: 커널·rootfs는 raw 파티션(squashfs) 또는 UBI static 볼륨, 설정은 작은 JFFS2(또는 UBIFS), 로그는 tmpfs에 두고 중요한 이벤트만 주기적으로 flash에. NOR erase 수명과 erase 시간을 계산에 넣는다.
7. **드라이버 관점**: `mtd_nandbiterrs` 모듈을 nandsim에 돌려 보고 출력이 무엇을 검증하는지 설명하라. 정답/힌트: page 하나에 비트 오류를 하나씩 늘려 가며 ECC가 몇 비트까지 정정하는지, 정정 비트 수 보고가 맞는지를 본다 — NAND 컨트롤러 드라이버 bring-up 체크리스트의 첫 항목이다.

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| JFFS2 | Journalling Flash File System v2 | MTD 위 로그 구조 FS, 인덱스 없음, mount 때 전체 스캔 |
| node (JFFS2) | 노드 | 12 B 공통 헤더(magic 0x1985) + 내용. INODE/DIRENT/CLEANMARKER/SUMMARY |
| cleanmarker | 지움 완료 표시 | erase가 끝까지 됐다는 표시. NOR는 블록 앞, NAND는 OOB |
| summary | 블록 요약 노드 | 블록 끝에 그 블록의 노드 목록 — mount 스캔 단축(`CONFIG_JFFS2_SUMMARY`, `sumtool`) |
| wbuf | write buffer | NAND page 단위로 노드를 모으는 JFFS2/UBIFS 버퍼 |
| UBIFS | UBI File System | UBI 볼륨 위 FS, wandering B+tree 인덱스 + journal |
| wandering tree | 떠도는 나무 | 노드를 제자리 수정 없이 새로 쓰고 루트까지 갱신하는 CoW B+tree |
| TNC | Tree Node Cache | UBIFS 인덱스의 RAM 캐시. 커밋 전 변경은 여기에만 |
| journal / bud | 저널 / 버드 | 커밋 사이 쓰기가 들어가는 main area의 LEB들 |
| log | 로그 영역 | 커밋 시작(CS)과 bud 위치(REF) 기록 — replay의 출발점 |
| commit | 커밋 | TNC 변경을 새 IDX 노드로 쓰고 master 갱신 |
| master node | 마스터 노드 | 인덱스 루트·LPT·log 위치. LEB 1·2에 2부 |
| LPT | LEB Properties Tree | LEB별 free/dirty/flags, GC 대상 선택에 사용 |
| orphan | 고아 inode | 열린 채 unlink된 inode — 전원 차단 후 회수 |
| budgeting | 공간 예산 | 쓰기 전에 flash 공간을 예약하는 UBIFS 메커니즘 |
| dark / dead space | 어두운/죽은 공간 | LEB 끝 자투리 / GC로도 못 쓰는 공간 |
| bulk_read | 일괄 읽기 | 연속 데이터 노드를 한 번에 읽는 마운트 옵션 |
| static / dynamic volume | 정적/동적 볼륨 | static: 크기·LEB별 CRC 보유, RO 이미지용. dynamic: 일반 RW |
| autoresize | 자동 크기 조정 | 첫 attach 때 남은 PEB를 전부 갖는 볼륨 플래그(장치당 하나) |
| ubinize | — | 볼륨 이미지들을 EC/VID 헤더 붙은 PEB 단위 UBI 이미지로 |
| ubiformat | — | EC를 보존하며 UBI 이미지를 굽거나 장치를 포맷 |
| ubiblock | — | UBI 볼륨을 RO 블록 장치로(`/dev/ubiblockX_Y`) |
| fastmap | — | UBI 매핑 스냅샷을 flash에 저장해 attach 스캔을 생략 |
| scrubbing | 스크럽 | 정정 가능한 비트 오류가 난 PEB를 다른 PEB로 옮기기 |
| `-EUCLEAN` | — | MTD: "읽기 성공, 하지만 정정한 비트 오류가 임계 이상" |
| phram | — | 물리 메모리 영역을 MTD 장치로 보여 주는 드라이버(§6의 영속 flash 흉내) |

---

## 15. 요약 & 체크리스트

raw flash 위의 FS는 "FTL이 없는 매체 위에서 메타데이터를 어떻게 일관되게 유지하나"에 대한 세 가지 답이다. JFFS2는 인덱스를 버리고 매 mount를 전체 스캔으로 만들어 단순함과 견고함을 얻는 대신, mount 시간과 RAM을 FS 크기에 지불한다(측정: 192 MiB에서 summary 없이 약 530 ms, NAND 지연을 켜면 65 초 · slab 1.1 MiB, 그리고 NAND에서는 빈 장치조차 9 초). UBIFS는 UBI(I11의 커널 FTL) 위에서 wandering B+tree 인덱스와 journal을 둬 mount를 크기와 무관하게 만들고(replay는 커밋 이후 bud만), 전원 차단은 master 2부 + replay + recover_leb + orphan으로 견딘다(측정: kill -9 12회 모두 recovery 완료, fsync된 레코드 손실 0). RO 부분은 ubiblock + squashfs가 가장 싸고 안전하다. 양산에서는 `mkfs.ubifs -m/-e/-c`와 `ubinize -p/-m/-s`를 칩 geometry에서 계산하고, 반드시 `ubiformat`으로 구워야 EC 이력과 "0xFF page = 빈 page" 규칙이 지켜진다. 측정 도구(debugfs `dump_budg`/`dump_lprops`, dynamic debug, sysfs `corrected_bits`·`max_ec`·`ro_mode`)를 알고 있으면 필드 장비의 flash 문제를 FS 계층에서 진단할 수 있다.

- [ ] JFFS2의 노드 종류(INODE·DIRENT·CLEANMARKER·SUMMARY)와 cleanmarker가 필요한 이유를 설명할 수 있다
- [ ] JFFS2 mount가 왜 FS 크기에 비례하는지, summary가 무엇을 줄이고 무엇을 못 줄이는지 설명할 수 있다
- [ ] UBIFS의 LEB 배치(SB · master×2 · log · LPT · orphan · main)를 그리고 각 영역의 역할을 말할 수 있다
- [ ] wandering tree · journal(bud) · commit · replay를 SSD FTL의 체크포인트 + 재생과 대응시켜 설명할 수 있다
- [ ] 칩 geometry에서 `mkfs.ubifs -m -e -c`와 `ubinize -p -m -s` 값을 계산하고 `ubinize.cfg`(static/dynamic, autoresize)를 쓸 수 있다
- [ ] `ubiformat`과 `nandwrite`의 차이를 EC 이력과 0xFF page 재-program 문제로 증명할 수 있다
- [ ] ubiblock + squashfs로 RO rootfs를 구성하고 커널 인자를 쓸 수 있다
- [ ] UBI fastmap이 attach 시간을 어떻게 줄이고 어떤 위험을 더하는지 설명할 수 있다
- [ ] UBIFS `df`가 실제 flash 사용량과 다른 이유(환산 추정, root 예약, 압축)를 설명할 수 있다
- [ ] fsync + seq/CRC 레코드 + 강제 종료 루프로 전원 차단 테스트 harness를 짤 수 있다
- [ ] 비트 플립이 MTD(`-EUCLEAN`, `bitflip_threshold`) → UBI scrub으로 이어지는 경로와 JFFS2의 차이를 설명할 수 있다
- [ ] raw NAND 로봇 보드의 파티션 · UBI 볼륨 레이아웃을 설계하고 근거를 댈 수 있다

## 참고 자료

- Linux 커널 문서 — UBI File System: https://docs.kernel.org/filesystems/ubifs.html
- Linux 커널 문서 — UBIFS Authentication: https://docs.kernel.org/filesystems/ubifs-authentication.html
- Linux 커널 문서 — MTD NAND Error-correction Code: https://docs.kernel.org/driver-api/mtd/nand_ecc.html
- linux-mtd 프로젝트 문서(UBI, UBIFS, FAQ — "Why do I have to use ubiformat?", fastmap, ubiblock): http://www.linux-mtd.infradead.org/doc/ubi.html , http://www.linux-mtd.infradead.org/doc/ubifs.html , http://www.linux-mtd.infradead.org/faq/ubi.html
- Adrian Hunter, Artem Bityutskiy, "UBIFS file system" 백서(UBIFS 설계 — wandering tree, journal, LPT): http://www.linux-mtd.infradead.org/doc/ubifs_whitepaper.pdf
- Linux 소스(랩, 6.18.55): `fs/jffs2/scan.c`(`jffs2_scan_medium`, `jffs2_scan_eraseblock`), `fs/jffs2/summary.c`, `fs/jffs2/wbuf.c`(`jffs2_flash_read`), `fs/ubifs/super.c`(`mount_ubifs`, `ubifs_statfs`), `fs/ubifs/replay.c`, `fs/ubifs/recovery.c`, `fs/ubifs/budget.c`(`ubifs_get_free_space_nolock`), `fs/ubifs/ubifs-media.h`, `drivers/mtd/ubi/fastmap.c`, `drivers/mtd/ubi/block.c`, `drivers/mtd/devices/phram.c`, `drivers/mtd/nand/raw/nandsim.c` — 온라인: https://elixir.bootlin.com/linux/v6.18.55/source
- mtd-utils 소스(`ubi-utils/ubiformat.c`, `nand-utils/nandwrite.c`, `mkfs.ubifs/`): https://git.infradead.org/mtd-utils.git
- Bootlin, "Embedded Linux system development" 교육 슬라이드 — Flash storage and filesystems 장: https://bootlin.com/training/embedded-linux/
- 이 노트의 선행: I11 §1~3(MTD · UBI), C2 §5(nandsim 배드 블록·ECC). 후속: J5(전원 차단 안전성), L4(OTA)
