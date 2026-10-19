# J2. 블록 파일시스템 — ext4 · F2FS · 저널링 · 마운트 옵션 · 파티션(GPT/MBR)

> **이 노트를 다 읽으면**: GPT/MBR 디스크를 바이트 단위로 읽고 `root=PARTUUID=`로 커널이 rootfs를 찾는 과정을 설명할 수 있다 · ext4의 superblock·block group·inode·extent tree·jbd2 저널을 `dumpe2fs`/`debugfs`와 직접 짠 C 파서로 확인할 수 있다 · `data=ordered/writeback/journal`, `commit=`, `barrier`, `fast_commit`이 fsync 한 번에 장치로 가는 바이트와 FLUSH 수를 어떻게 바꾸는지 실측으로 말할 수 있다 · F2FS의 log-structured 배치·multi-head logging·GC를 SSD FTL에 1:1로 대응시키고, ext4 vs F2FS write amplification을 측정해 설명할 수 있다
> **왜 필요한가**: 임베디드 Linux 장비의 rootfs·데이터 파티션 설계, 첫 부팅 파티션 확장, "전원 뽑았더니 로그가 사라졌다", "eMMC가 2년 만에 수명 끝" 같은 현장 문제와 면접 단골 질문("ext4 저널 모드 차이?", "F2FS는 왜 flash 친화적인가?", "GPT의 protective MBR이 뭔가?")이 전부 이 주제다
> **Don 기준 난이도**: LBA·섹터·FTL·GC·WAF·over-provisioning·전원 차단 복구는 Don이 SSD 펌웨어에서 직접 만든 것이다 / 새로 배울 부분은 **그 개념들이 파일시스템 층에서 어떤 이름의 자료구조와 마운트 옵션으로 다시 나타나는가** — 그리고 두 층(FS와 FTL)이 겹칠 때 생기는 문제
> **선행 노트**: A3(파일 I/O — fd·`struct file`·`fsync`), I11(MTD·MMC·block — blk-mq, `queue_rq`, block tracepoint), J1(임베디드 저장장치 — 어떤 장치가 FTL을 갖나). C(부트 흐름)의 커널 커맨드라인 `root=`도 다시 나온다

---

## 0. 큰 그림 — 이게 왜 필요한가

Don이 만든 SSD 펌웨어는 호스트가 준 **LBA**만 보았다. 그 LBA를 누가 골랐을까? 호스트 Linux의 **파일시스템**이다. "`/data/log/imu.csv`의 4096번째 바이트"라는 이름을 "LBA 17272"라는 주소로 바꾸고, 어떤 LBA가 비어 있는지 기억하고, 전원이 꺼져도 그 기억이 깨지지 않게 하는 것이 파일시스템의 일이다. 그리고 그 아래에서 디스크를 몇 조각으로 나누는 것이 **파티션 테이블**이다.

```svg
<svg viewBox="0 0 680 430" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><rect x="20" y="14" width="640" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="340.0" y="38.5" font-size="13" text-anchor="middle">앱: open · write · fsync · mount (A3)</text><rect x="20" y="72" width="640" height="50" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="340.0" y="93.5" font-size="13" text-anchor="middle">VFS: super_block · inode · dentry · file</text><text x="340.0" y="109.5" font-size="13" text-anchor="middle">file_operations → 파일시스템별 구현 (E/F)</text><rect x="20" y="140" width="290" height="50" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="165.0" y="161.6" font-size="13" text-anchor="middle">page cache (address_space)</text><text x="165.0" y="177.6" font-size="13" text-anchor="middle">dirty folio → writeback 스레드</text><rect x="350" y="140" width="310" height="50" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="505.0" y="161.6" font-size="13" text-anchor="middle">파일시스템: ext4(+jbd2) · F2FS</text><text x="505.0" y="177.6" font-size="13" text-anchor="middle">vfat · exFAT · btrfs  ← 이 노트</text><rect x="20" y="208" width="640" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/><text x="340.0" y="232.6" font-size="13" text-anchor="middle">block 계층: bio → request → blk-mq → 드라이버 queue_rq (I11)</text><rect x="20" y="266" width="640" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="340.0" y="290.6" font-size="13" text-anchor="middle">파티션 테이블: GPT / MBR → vda1 · vda2 · vda3  ← 이 노트</text><rect x="20" y="324" width="640" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/><text x="340.0" y="348.6" font-size="13" text-anchor="middle">장치: eMMC · SD · UFS · NVMe — 안에 FTL (J1, Don의 SSD 세계)</text><line x1="340" y1="54" x2="340" y2="72" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/><line x1="340" y1="122" x2="340" y2="140" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/><line x1="340" y1="190" x2="340" y2="208" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/><line x1="340" y1="248" x2="340" y2="266" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/><line x1="340" y1="306" x2="340" y2="324" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/><text x="20.0" y="392.0" font-size="13" text-anchor="start">같은 일을 두 번 한다: 파일시스템도 블록을 배치·회수하고(GC·저널), 장치 FTL도 한다.</text><text x="20.0" y="414.0" font-size="13" text-anchor="start">→ 두 층이 서로를 모르면 write amplification이 곱해진다 (§4 저널, §6 F2FS).</text></svg>
```
그림 1 — 이 노트의 범위. VFS 아래의 파일시스템과 block 계층 아래의 파티션 테이블. 장치 안의 FTL은 J1·I11에서 다뤘다.

bare-metal 비유로 한 줄씩:

- **파티션 테이블** = 플래시 맵(`flash_layout.h`)의 "부트로더는 0x0000~, 설정은 0x8000~" 같은 영역 표. 다만 디스크 맨 앞에 **데이터로** 적혀 있어서 OS가 읽어 간다.
- **파일시스템** = Don의 FTL과 같은 종류의 물건이다. 이름(경로)→주소(LBA) 맵, 빈 공간 관리, 전원 차단 시 맵의 일관성. FTL이 "LBA→PBA"라면 FS는 "파일 오프셋→LBA"다.
- **저널(jbd2)** = FTL의 "맵 업데이트를 로그로 먼저 쓰고, 나중에 본 테이블에 반영"하는 방식과 같다. 전원 차단 후 재생(replay)한다.
- **F2FS** = 아예 FTL처럼 생긴 파일시스템이다. 항상 append(로그 구조), segment 단위 GC, over-provisioning, hot/cold 분리.

### 0.1 실습 환경

- VM 호스트: Lima `elinux`, `uname -r` = `7.0.0-38-generic` (도구 실행: `sgdisk`, `mkfs.*`, `dumpe2fs`, `gcc -static`).
- 게스트 커널: 공용 랩의 **스토리지 커널** `out/Image-fs` = `6.18.55-fs` (ext4·jbd2·vfat 내장, F2FS·btrfs·exFAT·dm-flakey 모듈, `BLK_DEV_IO_TRACE`, `EXT4_DEBUG`, `FAIL_MAKE_REQUEST`). QEMU `virt`, `-smp 2 -m 1024M`, TCG(KVM 없음) — **시간 측정값의 절대 크기는 실제 HW와 다르다**. 그래서 이 노트는 시간보다 **장치가 실제로 받은 섹터 수·FLUSH 수**(결정적인 값)를 주로 비교한다.
- 디스크: VM의 `/var/tmp/J2/` 아래 raw 이미지 파일을 `-drive if=none,id=d1,file=...,format=raw -device virtio-blk-device,drive=d1`로 붙인다 → 게스트에서 `/dev/vda`.
- 게스트 BusyBox에는 `mkfs.ext4`/`mkfs.f2fs`가 없어서, VM의 glibc 도구(e2fsprogs 1.47.2, f2fs-tools 1.16.0, sgdisk)를 공유 라이브러리와 함께 initramfs `/opt/bin`에 복사했다(같은 aarch64라 그대로 돈다). 호스트 커널에는 테스트 이미지를 **한 번도 마운트하지 않았다** — mkfs는 이미지 파일에 직접, 마운트는 전부 게스트 안에서 했다.

게스트 부팅 스크립트(VM에서 실행). 이 노트의 모든 게스트 예제는 이 스크립트로 `t.sh` 하나를 돌리고 자동 poweroff 한다.

```sh
#!/bin/bash
# usage: [QEXTRA="..."] [SMP=n] [TMO=s] [APPEND=...] boot.sh <guest-script.sh>
set -e
G=$1; N=$(basename $G .sh); W=/var/tmp/J2/run_$N
rm -rf $W && mkdir -p $W/r && cp -a /var/lab/build/rootfs/. $W/r/ && mkdir -p $W/r/j2 $W/r/opt/bin $W/r/lib/aarch64-linux-gnu
M=/var/lab/out/modules-fs/lib/modules/6.18.55-fs; D=$W/r/lib/modules/6.18.55-fs
mkdir -p $D && cp $M/modules.* $D/
(cd $M && cp --parents -r kernel/fs/f2fs kernel/fs/btrfs kernel/fs/exfat kernel/lib kernel/crypto kernel/drivers/md $D/ 2>/dev/null || true)
# glibc userland tools from the VM (same arch) + their libs
for b in mkfs.ext4 dumpe2fs debugfs e2fsck resize2fs tune2fs mkfs.f2fs fsck.f2fs dump.f2fs sgdisk; do cp $(which $b) $W/r/opt/bin/; done
for l in $(for b in $W/r/opt/bin/*; do ldd $b | awk '/=>/{print $3}'; done | sort -u); do cp -L $l $W/r/lib/aarch64-linux-gnu/; done
cp -L /lib/ld-linux-aarch64.so.1 $W/r/lib/
mkdir -p $W/r/etc && cp /etc/mke2fs.conf $W/r/etc/ 2>/dev/null || true
cp /var/tmp/J2/bin/* $W/r/j2/ 2>/dev/null || true
cp /Users/donh/.elinux_scratch/J2/$G $W/r/j2/t.sh
(cd $W/r && find . | cpio -o -H newc --quiet | gzip) > $W/i.gz
timeout ${TMO:-300} qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp ${SMP:-2} -m 1024M -nographic \
  -no-reboot -kernel /var/lab/out/Image-fs -initrd $W/i.gz $QEXTRA \
  -append "console=ttyAMA0 quiet $APPEND LAB_CMD=sh~/j2/t.sh" > $W/out.txt 2>&1 || echo "qemu rc=$?"
tr -d '\r' < $W/out.txt > /var/tmp/J2/out_$N.txt
rm -rf $W/r
```

C 예제는 VM에서 `gcc -static -std=gnu11 -Wall -Wextra -O2`로 빌드했다(6개 전부 경고 0개) → `/var/tmp/J2/bin/` → initramfs `/j2/`.

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.elinux_scratch/J2 && for p in frag sbparse fsyncbench randw blkpg_grow ioctl_resize; do gcc -static -std=gnu11 -Wall -Wextra -O2 $p.c -o /var/tmp/J2/bin/$p || echo FAIL $p; done'
```

---

## 1. 파티션 — MBR과 GPT

### 1.1 직관 — 디스크 맨 앞의 "목차"

블록 장치는 그냥 `0 … N-1` 번 섹터의 배열이다. 여기에 "섹터 2048부터 64 MiB는 부트 파티션(FAT), 그다음 192 MiB는 rootfs(ext4)…"라는 표를 디스크 맨 앞(그리고 GPT는 맨 뒤에도)에 적어 둔 것이 파티션 테이블이다. 커널은 디스크를 발견하면 이 표를 읽어 `vda` 밑에 `vda1`, `vda2` … 라는 **부분 블록 장치**를 만든다. 파티션 장치에 대한 I/O는 그냥 "시작 섹터만큼 더해서" 부모 디스크로 간다 — Don의 펌웨어에서 namespace의 LBA에 오프셋을 더하는 것과 같다.

### 1.2 MBR (DOS 파티션 테이블)

1983년 IBM PC 형식. LBA 0 한 섹터(512 B) 안에 전부 들어 있다.

| 오프셋 | 크기 | 내용 |
|---|---|---|
| 0x000 | 446 B | 부트 코드 (BIOS가 실행) |
| 0x1B8 | 4 B | 디스크 시그니처 (MBR의 PARTUUID 앞부분) |
| 0x1BE | 16 B × 4 | 파티션 엔트리 4개: 상태(0x80=부팅), CHS 시작, **타입**, CHS 끝, **LBA 시작(32비트)**, **섹터 수(32비트)** |
| 0x1FE | 2 B | 서명 `55 AA` |

한계: **주 파티션 4개**(더 필요하면 확장 파티션 안에 논리 파티션 체인), **32비트 LBA → 512 B 섹터 기준 2 TiB**, 체크섬·백업 없음.

예제 1 — `parted`로 MBR 이미지를 만들고 엔트리를 직접 읽는다. 무엇을 확인하나: 16바이트 엔트리 안의 타입 바이트와 little-endian LBA.

```sh
truncate -s 64M j2_mbr.img
parted -s j2_mbr.img mklabel msdos mkpart primary fat32 1MiB 17MiB mkpart primary ext4 17MiB 100% set 1 boot on
parted -s j2_mbr.img unit s print
xxd -s 0x1be -l 66 j2_mbr.img
```

```text
Model:  (file)
Disk /var/tmp/J2/j2_mbr.img: 131072s
Sector size (logical/physical): 512B/512B
Partition Table: msdos
Disk Flags: 

Number  Start   End      Size    Type     File system  Flags
 1      2048s   34815s   32768s  primary               boot, lba
 2      34816s  131071s  96256s  primary

000001be: 8000 0110 0c03 600f 0008 0000 0080 0000  ......`.........
000001ce: 0000 4110 8303 e0ff 0088 0000 0078 0100  ..A..........x..
000001de: 0000 0000 0000 0000 0000 0000 0000 0000  ................
000001ee: 0000 0000 0000 0000 0000 0000 0000 0000  ................
000001fe: 55aa                                     U.
```

출력에서 볼 것: 엔트리 1 = `80`(부팅 플래그) … `0c`(FAT32 LBA) … `0008 0000`(시작 0x800 = 2048) `0080 0000`(크기 0x8000 = 32768). 엔트리 2 = 타입 `83`(Linux), 시작 `0088 0000` = 0x8800 = 34816, 크기 `0078 0100` = 0x17800 = 96256. 전부 little-endian이다. CHS 필드(`0110 …`)는 요즘 아무도 안 쓰는 화석이다.

### 1.3 GPT (GUID Partition Table)

UEFI 스펙의 일부. 64비트 LBA, 기본 128개 엔트리, **헤더와 엔트리 배열 모두 CRC32**, 디스크 끝에 **백업 사본**. 각 파티션은 두 개의 GUID를 가진다: **타입 GUID**(이게 무슨 용도인가 — ESP, Linux filesystem …)와 **unique GUID**(이 파티션 하나만의 ID = 커널이 말하는 **PARTUUID**). 그리고 UTF-16 이름 72바이트(= **PARTLABEL**).

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="20.0" y="26.0" font-size="13" text-anchor="start" font-weight="bold">j2_disk.img (512 MiB, 1048576 섹터) — 실제 sgdisk 결과</text><rect x="20" y="40" width="44" height="46" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.3"/><text x="42.0" y="67.2" font-size="12" text-anchor="middle">LBA0</text><rect x="64" y="40" width="44" height="46" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="86.0" y="67.2" font-size="12" text-anchor="middle">LBA1</text><rect x="108" y="40" width="48" height="46" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="132.0" y="67.2" font-size="12" text-anchor="middle">2-33</text><rect x="156" y="40" width="84" height="46" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="198.0" y="67.2" font-size="12" text-anchor="middle">p1</text><rect x="240" y="40" width="160" height="46" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="320.0" y="67.2" font-size="12" text-anchor="middle">p2</text><rect x="400" y="40" width="160" height="46" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="480.0" y="67.2" font-size="12" text-anchor="middle">p3</text><rect x="560" y="40" width="50" height="46" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><rect x="610" y="40" width="50" height="46" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="635.0" y="67.2" font-size="12" text-anchor="middle">last</text><text x="42.0" y="104.0" font-size="12" text-anchor="middle">MBR</text><text x="86.0" y="104.0" font-size="12" text-anchor="middle">헤더</text><text x="132.0" y="104.0" font-size="12" text-anchor="middle">엔트리</text><text x="198.0" y="104.0" font-size="12" text-anchor="middle">boot FAT32</text><text x="320.0" y="104.0" font-size="12" text-anchor="middle">rootfs ext4</text><text x="480.0" y="104.0" font-size="12" text-anchor="middle">data F2FS</text><text x="610.0" y="104.0" font-size="12" text-anchor="middle">백업 엔트리+헤더</text><text x="610.0" y="120.0" font-size="12" text-anchor="middle">33섹터</text><text x="198.0" y="120.0" font-size="12" text-anchor="middle">64 MiB</text><text x="320.0" y="120.0" font-size="12" text-anchor="middle">192 MiB</text><text x="480.0" y="120.0" font-size="12" text-anchor="middle">255 MiB</text><text x="156.0" y="136.0" font-size="12" text-anchor="start">↑ 2048 (1 MiB 정렬)</text><text x="400.0" y="136.0" font-size="12" text-anchor="start">↑ 526336</text><rect x="20" y="158" width="310" height="128" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.3"/><text x="30.0" y="178.0" font-size="13" text-anchor="start" font-weight="bold">LBA0 보호 MBR (0x1BE 엔트리 1개)</text><text x="30.0" y="198.0" font-size="12" text-anchor="start">type 0xEE, start LBA 1</text><text x="30.0" y="216.0" font-size="12" text-anchor="start">size 0x000FFFFF = 디스크 끝까지</text><text x="30.0" y="234.0" font-size="12" text-anchor="start">→ MBR만 아는 도구가 '빈 디스크'로</text><text x="30.0" y="252.0" font-size="12" text-anchor="start">   착각해 덮어쓰지 못하게 막는다</text><text x="30.0" y="272.0" font-size="12" text-anchor="start">0x1FE: 55 AA 서명</text><rect x="350" y="158" width="310" height="128" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="360.0" y="178.0" font-size="13" text-anchor="start" font-weight="bold">LBA1 GPT 헤더 (92 B) 주요 필드</text><text x="360.0" y="198.0" font-size="12" text-anchor="start">"EFI PART", 헤더 CRC32</text><text x="360.0" y="216.0" font-size="12" text-anchor="start">my_lba=1, alternate_lba=1048575</text><text x="360.0" y="234.0" font-size="12" text-anchor="start">first/last usable = 34 / 1048542</text><text x="360.0" y="252.0" font-size="12" text-anchor="start">disk GUID, entries@LBA2, 128 × 128 B</text><text x="360.0" y="272.0" font-size="12" text-anchor="start">entry: type GUID, PARTUUID, LBA, 이름</text></svg>
```
그림 2 — 예제 2에서 만든 GPT 이미지의 실제 배치. 앞쪽 34섹터(보호 MBR + 헤더 + 엔트리 32섹터)와 뒤쪽 33섹터(엔트리 백업 + 헤더 백업)가 GPT 자체가 차지하는 공간이다.

**Protective MBR**: GPT 디스크의 LBA 0에는 타입 `0xEE` 엔트리 하나가 디스크 전체를 덮는 가짜 MBR이 있다. 목적은 하나 — MBR만 아는 오래된 도구가 이 디스크를 "파티션 없는 빈 디스크"로 보고 덮어쓰지 못하게 하는 것.

### 1.4 예제 2 — sgdisk로 GPT 이미지 만들기 (boot FAT32 + rootfs ext4 + data F2FS)

무엇을 확인하나: 루프 마운트 없이 "파티션 테이블 + 파티션별 파일시스템 이미지"를 조립하는 방법. 이건 Yocto `wic`, Buildroot `genimage`가 실제로 하는 일과 같다(K 모듈에서 다시 나온다).

```sh
#!/bin/bash
# build a GPT disk image: p1 FAT32 boot, p2 ext4 rootfs, p3 f2fs data (no loop mount needed)
set -e
cd /var/tmp/J2; rm -f j2_disk.img j2_p*.img
truncate -s 512M j2_disk.img
sgdisk -o j2_disk.img >/dev/null
sgdisk -n 1:2048:+64M -t 1:EF00 -c 1:boot \
       -n 2:0:+192M   -t 2:8300 -c 2:rootfs \
       -n 3:0:0       -t 3:8300 -c 3:data j2_disk.img
sgdisk -p j2_disk.img
# filesystem images, one per partition
read S1 E1 < <(sgdisk -i 1 j2_disk.img | awk '/First sector/{f=$3}/Last sector/{print f,$3}')
read S2 E2 < <(sgdisk -i 2 j2_disk.img | awk '/First sector/{f=$3}/Last sector/{print f,$3}')
read S3 E3 < <(sgdisk -i 3 j2_disk.img | awk '/First sector/{f=$3}/Last sector/{print f,$3}')
mkdir -p rootdir && rm -rf rootdir/* && cp -a /var/lab/build/rootfs/. rootdir/ && echo hello-from-ext4 > rootdir/etc/j2_marker; cp /Users/donh/.elinux_scratch/J2/g1b.sh rootdir/g1b.sh
truncate -s $(( (E1-S1+1)*512 )) j2_p1.img; mkfs.vfat -F 32 -n BOOT j2_p1.img >/dev/null
truncate -s $(( (E2-S2+1)*512 )) j2_p2.img; mkfs.ext4 -q -L rootfs -d rootdir j2_p2.img
truncate -s $(( (E3-S3+1)*512 )) j2_p3.img; mkfs.f2fs -q -l data j2_p3.img >/dev/null
dd if=j2_p1.img of=j2_disk.img bs=512 seek=$S1 conv=notrunc status=none
dd if=j2_p2.img of=j2_disk.img bs=512 seek=$S2 conv=notrunc status=none
dd if=j2_p3.img of=j2_disk.img bs=512 seek=$S3 conv=notrunc status=none
rm -f j2_p*.img
echo "p1 $S1-$E1  p2 $S2-$E2  p3 $S3-$E3"
blkid -p -o export --offset $((S2*512)) j2_disk.img | grep -E "TYPE|LABEL|UUID" || true
sgdisk -i 2 j2_disk.img
```

```text
Warning: The kernel is still using the old partition table.
The new table will be used at the next reboot or after you
run partprobe(8) or kpartx(8)
The operation has completed successfully.
Disk j2_disk.img: 1048576 sectors, 512.0 MiB
Sector size (logical): 512 bytes
Disk identifier (GUID): 41D7B4AD-8515-48DB-81AE-5CD277206069
Partition table holds up to 128 entries
Main partition table begins at sector 2 and ends at sector 33
First usable sector is 34, last usable sector is 1048542
Partitions will be aligned on 2048-sector boundaries
Total free space is 2014 sectors (1007.0 KiB)

Number  Start (sector)    End (sector)  Size       Code  Name
   1            2048          133119   64.0 MiB    EF00  boot
   2          133120          526335   192.0 MiB   8300  rootfs
   3          526336         1048542   255.0 MiB   8300  data
p1 2048-133119  p2 133120-526335  p3 526336-1048542
LABEL=rootfs
UUID=9f9cfe16-8977-49fa-84cf-3b85cbea8781
TYPE=ext4
Partition GUID code: 0FC63DAF-8483-4772-8E79-3D69D8477DE4 (Linux filesystem)
Partition unique GUID: 015385F6-7D47-48F6-AAE0-218A91EA9273
First sector: 133120 (at 65.0 MiB)
Last sector: 526335 (at 257.0 MiB)
Partition size: 393216 sectors (192.0 MiB)
Attribute flags: 0000000000000000
Partition name: 'rootfs'
```

출력에서 볼 것: 첫 파티션이 **2048섹터(1 MiB)**에서 시작한다("aligned on 2048-sector boundaries"). 맨 위 "Warning: kernel is still using the old partition table"은 sgdisk가 대상이 이미지 파일인데도 습관적으로 `BLKRRPART`를 시도해서 나온 무해한 경고다. `UUID=`(파일시스템 UUID, mkfs가 superblock에 기록)와 `Partition unique GUID`(PARTUUID, GPT 엔트리에 기록)는 **서로 다른 ID**다.

### 1.5 예제 3 — 보호 MBR · GPT 헤더 · 엔트리를 hexdump로 읽기

무엇을 확인하나: 그림 2의 필드가 실제 바이트로 어디 있는지.

```sh
xxd -s 0x1b8 -l 72 j2_disk.img          # LBA0 보호 MBR
xxd -s 512 -l 96 j2_disk.img            # LBA1 GPT 헤더
xxd -s 1024 -l 256 j2_disk.img | grep -v "0000 0000 0000 0000 0000 0000 0000 0000"   # LBA2 엔트리 1, 2
xxd -s $((1048575*512)) -l 32 j2_disk.img   # 마지막 LBA = 백업 헤더
```

```text
### LBA0 protective MBR (offset 0x1b8-0x1ff)
000001b8: 0000 0000 0000 0000 0200 ee45 0441 0100  ...........E.A..
000001c8: 0000 ffff 0f00 0000 0000 0000 0000 0000  ................
000001d8: 0000 0000 0000 0000 0000 0000 0000 0000  ................
000001e8: 0000 0000 0000 0000 0000 0000 0000 0000  ................
000001f8: 0000 0000 0000 55aa                      ......U.
### LBA1 GPT header
00000200: 4546 4920 5041 5254 0000 0100 5c00 0000  EFI PART....\...
00000210: f72a f604 0000 0000 0100 0000 0000 0000  .*..............
00000220: ffff 0f00 0000 0000 2200 0000 0000 0000  ........".......
00000230: deff 0f00 0000 0000 adb4 d741 1585 db48  ...........A...H
00000240: 81ae 5cd2 7720 6069 0200 0000 0000 0000  ..\.w `i........
00000250: 8000 0000 8000 0000 6f88 7e0c 0000 0000  ........o.~.....
### LBA2 entries 1,2
00000400: 2873 2ac1 1ff8 d211 ba4b 00a0 c93e c93b  (s*......K...>.;
00000410: 8c9a 5094 ab34 cb46 bc99 7ec8 a962 a956  ..P..4.F..~..b.V
00000420: 0008 0000 0000 0000 ff07 0200 0000 0000  ................
00000430: 0000 0000 0000 0000 6200 6f00 6f00 7400  ........b.o.o.t.
00000480: af3d c60f 8384 7247 8e79 3d69 d847 7de4  .=....rG.y=i.G}.
00000490: f685 5301 477d f648 aae0 218a 91ea 9273  ..S.G}.H..!....s
000004a0: 0008 0200 0000 0000 ff07 0800 0000 0000  ................
000004b0: 0000 0000 0000 0000 7200 6f00 6f00 7400  ........r.o.o.t.
000004c0: 6600 7300 0000 0000 0000 0000 0000 0000  f.s.............
### last LBA (backup header)
1ffffe00: 4546 4920 5041 5254 0000 0100 5c00 0000  EFI PART....\...
1ffffe10: f32f 4580 0000 0000 ffff 0f00 0000 0000  ./E.............
```

출력에서 볼 것:

- **보호 MBR**(0x1BE부터): `00`(부팅 아님) `00 02 00`(CHS) **`ee`**(GPT 보호 타입) `45 04 41`(CHS) **`01 00 00 00`**(시작 LBA 1) **`ff ff 0f 00`**(크기 0xFFFFF = 1048575 = 디스크 전체 − 1).
- **GPT 헤더**: `EFI PART` 시그니처, `0000 0100` = revision 1.0, `5c00 0000` = 헤더 크기 92, 0x210의 4바이트 = 헤더 CRC32, 0x218 = my_lba 1, 0x220 = 백업 LBA `ffff 0f00` = 1048575, 0x228 = first usable `22` = 34, 0x230 = last usable `0xFFFDE` = 1048542, 0x238부터 16바이트 = 디스크 GUID, 0x248 = 엔트리 시작 LBA 2, 0x250 = 엔트리 수 `0x80`=128, 엔트리 크기 `0x80`=128 B, 0x258 = 엔트리 배열 CRC32.
- **엔트리 1**: 타입 GUID `2873 2ac1 1ff8 d211 ba4b 00a0 c93e c93b` = `C12A7328-F81F-11D2-BA4B-00A0C93EC93B`(EFI System Partition). GUID의 앞 3필드는 little-endian, 뒤 2필드는 바이트 순서 그대로라서 바이트열과 문자열 순서가 다르게 보인다. 0x420 = 시작 LBA `0x800` = 2048, 0x428 = 끝 LBA `0x207ff` = 133119, 0x438부터 UTF-16 이름 `b.o.o.t`.
- **엔트리 2**: 0x490의 unique GUID `f685 5301 477d f648 aae0 218a 91ea 9273` = `015385F6-7D47-48F6-AAE0-218A91EA9273` — 예제 2의 sgdisk 출력과 같다. 이게 PARTUUID다.
- **마지막 LBA**에도 `EFI PART`가 있다 = 백업 헤더. 앞쪽이 깨지면 펌웨어·도구가 여기서 복구한다. 반대로 이미지를 **더 큰 장치에 dd 하면 백업 헤더가 장치 끝에 있지 않게 된다** → §5.6의 첫 부팅 확장에서 `sgdisk -e`로 고친다.

### 1.6 예제 4 — 게스트 커널은 파티션을 어떻게 보나

무엇을 확인하나: 커널의 파티션 파서가 만든 `vda1..3`, sysfs의 시작/크기, uevent의 PARTUUID·PARTNAME, 그리고 block queue 한계값.

```sh
export PATH=/opt/bin:$PATH
echo "### /proc/partitions"; cat /proc/partitions
echo "### sysfs start/size (512B sectors)"
for p in /sys/block/vda/vda*; do echo "$(basename $p) start=$(cat $p/start) size=$(cat $p/size) partn=$(cat $p/partition)"; done
echo "### uevent vda2"; cat /sys/block/vda/vda2/uevent
echo "### busybox blkid"; blkid
echo "### dmesg"; dmesg | grep -E "vda|GPT|virtio_blk"
echo "### queue limits"; for f in logical_block_size physical_block_size minimum_io_size optimal_io_size discard_granularity rotational fua write_cache; do echo "$f=$(cat /sys/block/vda/queue/$f)"; done
```

```text
lab initramfs: 6.18.55-fs up 1.34s
### /proc/partitions
major minor  #blocks  name

 254        0     524288 vda
 254        1      65536 vda1
 254        2     196608 vda2
 254        3     261103 vda3
### sysfs start/size (512B sectors)
vda1 start=2048 size=131072 partn=1
vda2 start=133120 size=393216 partn=2
vda3 start=526336 size=522207 partn=3
### uevent vda2
MAJOR=254
MINOR=2
DEVNAME=vda2
DEVTYPE=partition
DISKSEQ=9
PARTN=2
PARTNAME=rootfs
PARTUUID=015385f6-7d47-48f6-aae0-218a91ea9273
### busybox blkid
/dev/vda3: LABEL="data" UUID="cb570fce-ffd4-44d6-84a4-35dd5e03d35c" TYPE="f2fs"
/dev/vda2: LABEL="rootfs" UUID="9f9cfe16-8977-49fa-84cf-3b85cbea8781" TYPE="ext4"
/dev/vda1: LABEL="BOOT" UUID="C08D-FF8E" TYPE="vfat"
### dmesg
[    0.986406] virtio_blk virtio0: 1/0/0 default/read/poll queues
[    0.988659] virtio_blk virtio0: [vda] 1048576 512-byte logical blocks (537 MB/512 MiB)
[    1.000129]  vda: vda1 vda2 vda3
### queue limits
logical_block_size=512
physical_block_size=512
minimum_io_size=512
optimal_io_size=0
discard_granularity=512
rotational=1
fua=0
write_cache=write back
```

출력에서 볼 것: dmesg의 ` vda: vda1 vda2 vda3` 한 줄이 파티션 파싱 결과다. `/proc/partitions`의 크기는 1 KiB 단위, sysfs의 `start`/`size`는 **항상 512 B 섹터 단위**(장치의 logical block이 4 KiB여도 512 B 단위다). uevent의 `PARTUUID`는 소문자 GUID, `PARTNAME`은 GPT 이름. `fua=0`과 `write_cache=write back`은 §4에서 다시 나온다 — 이 장치는 **휘발성 쓰기 캐시가 있고 FUA를 지원하지 않는다**.

### 1.7 예제 5 — initramfs 없이 `root=PARTUUID=`로 ext4 파티션에서 부팅

무엇을 확인하나: 커널이 커맨드라인의 `root=PARTUUID=…`를 파티션으로 바꿔 ext4를 rootfs로 마운트하는 과정. 이미지의 rootfs 파티션에는 랩 BusyBox rootfs와 아래 스크립트(`/g1b.sh`)를 `mkfs.ext4 -d`로 넣어 두었다(예제 2). 부팅에는 `j2_disk.img`의 사본 `j2_boot.img`를 썼다. 랩 rootfs의 `/init`가 `LAB_CMD`를 실행하도록 `init=/init`을 준다.

```sh
echo "### booted from real rootfs (no initramfs)"; cat /proc/cmdline; grep -E " / " /proc/mounts; cat /etc/j2_marker; dmesg | grep -iE "VFS: Mounted|EXT4-fs \(vda2\)|Waiting"
```

```sh
qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 1024M -nographic -no-reboot \
  -kernel /var/lab/out/Image-fs \
  -drive if=none,id=d0,file=/var/tmp/J2/j2_boot.img,format=raw -device virtio-blk-device,drive=d0 \
  -append "console=ttyAMA0 root=PARTUUID=015385f6-7d47-48f6-aae0-218a91ea9273 rootwait init=/init LAB_CMD=sh~/g1b.sh"
```

```text
[    1.028260] virtio_blk virtio0: [vda] 1048576 512-byte logical blocks (537 MB/512 MiB)
[    1.040854]  vda: vda1 vda2 vda3
[    1.212553] EXT4-fs (vda2): mounted filesystem 9f9cfe16-8977-49fa-84cf-3b85cbea8781 ro with ordered data mode. Quota mode: none.
[    1.212841] VFS: Mounted root (ext4 filesystem) readonly on device 254:2.
[    1.214386] devtmpfs: mounted
[    1.273923] Run /init as init process
### booted from real rootfs (no initramfs)
console=ttyAMA0 root=PARTUUID=015385f6-7d47-48f6-aae0-218a91ea9273 rootwait init=/init LAB_CMD=sh~/g1b.sh
/dev/root / ext4 ro,relatime 0 0
hello-from-ext4
```

(부팅 로그는 관련 줄만 남겼다.) 출력에서 볼 것: `-initrd` 없이 부팅했고, 커널이 PARTUUID로 `254:2`(= vda2)를 찾아 ext4를 **읽기 전용(ro)**으로 마운트했다 — 커맨드라인에 `rw`가 없으면 커널은 root를 ro로 붙이고, 보통 init 스크립트가 fsck 후 `mount -o remount,rw /`를 한다. `hello-from-ext4`는 mkfs 때 넣어 둔 파일이다.

왜 `/dev/mmcblk0p2` 대신 PARTUUID인가: 임베디드 보드에서 SD와 eMMC가 둘 다 있으면 `mmcblk0`/`mmcblk1` 번호가 **프로브 순서**에 따라 바뀐다(DT `aliases { mmc0 = &sdhci1; }`로 고정할 수는 있다 — D 모듈). PARTUUID는 매체에 적혀 있으니 바뀌지 않는다. 커널은 `block/early-lookup.c`의 `early_lookup_bdev()`에서 `PARTUUID=`, `PARTUUID=<uuid>/PARTNROFF=<n>`, `PARTLABEL=<name>`, `/dev/…`, `major:minor` 형식을 해석한다(6.18 소스 확인). **파일시스템 UUID(`UUID=`)는 커널이 모른다** — 그건 initramfs나 `/etc/fstab`을 읽는 유저스페이스(blkid 라이브러리)의 일이다. `rootwait`은 장치가 늦게 프로브되는 경우(USB, SD) 나타날 때까지 기다리라는 뜻이다. U-Boot에서는 `part uuid mmc 0:2 rootuuid` 같은 명령으로 PARTUUID를 변수에 담아 bootargs를 만든다(C 모듈).

MBR 디스크에도 PARTUUID가 있다: `SSSSSSSS-PP` 형식(0x1B8의 디스크 시그니처 4바이트 + 파티션 번호 2자리 16진수). 예제 1처럼 시그니처가 0이면 쓸모가 없으니 이미지 생성 도구가 시그니처를 넣어 주는지 확인해야 한다.

### 1.8 정렬 — 왜 1 MiB에서 시작하나

Don에게 익숙한 얘기다. 파일시스템 블록(4 KiB)이나 F2FS 세그먼트(2 MiB)가 플래시의 page(16 KiB 안팎)·erase block·SD의 AU(allocation unit, 보통 4 MiB)에 걸쳐 있으면, 4 KiB 쓰기 하나가 FTL 안에서 두 page의 read-modify-write가 된다 → 장치 내부 WAF 증가. 그래서 도구들은 첫 파티션을 **1 MiB(2048섹터)** 경계에서 시작한다(sgdisk 기본값 `-a 2048`, parted `optimal`). eMMC는 `/sys/block/mmcblk0/device/preferred_erase_size`, 장치가 알려 주는 `optimal_io_size`/`minimum_io_size`(예제 4의 queue 값)를 참고해 더 크게 맞추기도 한다. SD 카드라면 4 MiB 정렬이 안전한 선택이다.

| 비교 | MBR | GPT |
|---|---|---|
| 위치 | LBA 0 한 섹터 | LBA 1 헤더 + LBA 2~33 엔트리, 끝에 백업 |
| 주소 폭 | 32비트 → 2 TiB (512 B 섹터) | 64비트 |
| 파티션 수 | 주 4개 (+확장/논리) | 기본 128개 |
| 무결성 | 없음 | 헤더·엔트리 CRC32, 백업 사본 |
| ID | 디스크 시그니처-번호 | 타입 GUID + unique GUID(PARTUUID) + 이름(PARTLABEL) |
| 임베디드에서 | 오래된 SoC BootROM, Raspberry Pi 펌웨어 | UEFI, 최근 SoC BootROM, Android(super/A·B 슬롯) |
| 커널 파서 | `block/partitions/msdos.c` | `block/partitions/efi.c` (msdos보다 먼저 시도) |

함정: 일부 SoC BootROM은 GPT를 읽지 못하고 **고정 오프셋**(예: 섹터 64, 8 KiB 오프셋)에서 부트로더를 읽는다. 그러면 첫 파티션 시작을 그 영역 뒤로 미뤄야 한다 — 1 MiB 정렬이 그 공간을 자연스럽게 남겨 준다. GPT 엔트리 배열(LBA 2~33)이 그 고정 오프셋과 겹치면 `sgdisk -j`로 엔트리 배열 위치를 옮긴다.

---

## 2. VFS — 파일시스템이 커널에 끼워지는 자리 (짧게)

A3에서 fd → `struct file` → inode의 3단 구조를 봤다. 파일시스템 쪽에서 보면 VFS는 **"이 함수 포인터 테이블들을 채워라"라는 계약서**다.

| 구조체 | 무엇 | 파일시스템이 채우는 ops | Don 비유 |
|---|---|---|---|
| `struct super_block` | 마운트된 FS 하나 (`/dev/vda2` on `/`) | `super_operations` (`alloc_inode`, `sync_fs`, `statfs`, `put_super`) | NVMe namespace 하나의 컨텍스트 |
| `struct inode` | 파일 하나의 메타데이터 (크기, 권한, 블록 위치) | `inode_operations` (`lookup`, `create`, `mkdir`), 그리고 `i_fop` | FTL의 파일 단위 맵 엔트리 |
| `struct dentry` | 경로 이름 한 조각 → inode (dcache) | `dentry_operations` (보통 기본값) | 이름 → ID 캐시 |
| `struct file` | open 한 번 (오프셋, 플래그) | `file_operations` (`read_iter`, `write_iter`, `fsync`, `mmap`) | 호스트 큐의 컨텍스트 |
| `struct address_space` | inode의 **page cache** (folio들의 xarray) | `address_space_operations` (`read_folio`, `writepages`, `dirty_folio`, `write_begin`) | DRAM 쓰기 버퍼 |

ext4에서 실제 이름(6.18 소스): `.fsync = ext4_sync_file`, `.write_iter = ext4_file_write_iter`, `.writepages = ext4_writepages`, `.read_folio = ext4_read_folio`, `data=journal`일 때만 `.dirty_folio = ext4_journalled_dirty_folio`로 바뀐다. F2FS는 `.fsync = f2fs_sync_file`.

**page cache와 writeback**: `write()`는 기본적으로 page cache의 folio를 dirty로 만들고 바로 돌아온다(장치 I/O 없음). 장치로 내려가는 시점은 세 가지다: (1) 앱이 `fsync`/`fdatasync`/`sync`를 부를 때, (2) dirty가 너무 많아질 때(`vm.dirty_background_ratio`/`dirty_ratio`), (3) 오래됐을 때 — flusher 커널 스레드가 `vm.dirty_writeback_centisecs`(기본 500 = 5초)마다 깨어 `vm.dirty_expire_centisecs`(기본 3000 = 30초)보다 오래된 dirty를 쓴다. Don의 SSD에서 "호스트가 FLUSH를 안 보내면 DRAM 쓰기 버퍼에 있는 데이터는 PLP(전원 손실 보호)가 없으면 날아간다"와 정확히 같은 구조가 **호스트 쪽에 한 겹 더** 있는 것이다. 자세한 page cache·writeback 내부는 E/F 모듈의 메모리 관리 노트, fsync의 전원 차단 의미론은 J5에서 다룬다.

ext4는 여기에 **delayed allocation(delalloc)**을 더한다: `write()` 시점에는 블록을 할당하지 않고 "예약"만 해 두었다가 writeback 때 한꺼번에 연속 블록을 할당한다 → 조각화가 줄고 짧게 살다 지워지는 임시 파일은 아예 디스크에 안 간다. 대가는 "write()는 성공했는데 writeback 때 ENOSPC" 같은 늦은 에러, 그리고 `rename()`으로 파일을 교체하는 앱이 fsync를 빼먹으면 크래시 후 0바이트 파일이 남는 문제였다(ext4는 `auto_da_alloc` 휴리스틱으로 이 패턴을 일부 보정한다).

---

## 3. ext4 on-disk 구조

### 3.1 직관 — 블록 그룹으로 나눈 디스크

ext4는 디스크를 **블록 그룹**(4 KiB 블록이면 32768블록 = 128 MiB)으로 나눈다. 왜 32768인가: 그룹마다 블록 비트맵이 **블록 하나**(4096 B × 8비트 = 32768비트)에 들어가도록. 각 그룹은 원래 자기 블록 비트맵, inode 비트맵, inode table을 가지고, 그 그룹의 파일 데이터는 가능하면 같은 그룹에 둔다(헤드 이동을 줄이던 HDD 시절 설계).

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="20.0" y="24.0" font-size="13" text-anchor="start" font-weight="bold">ext4 512 MiB, 4 KiB 블록 → 그룹 4개 × 32768 블록 (dumpe2fs 실측)</text><rect x="20" y="36" width="156" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="98.0" y="52.7" font-size="12" text-anchor="middle">group 0</text><text x="98.0" y="67.7" font-size="12" text-anchor="middle">블록 0-32767</text><rect x="180" y="36" width="156" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/><text x="258.0" y="52.7" font-size="12" text-anchor="middle">group 1</text><text x="258.0" y="67.7" font-size="12" text-anchor="middle">32768-65535</text><rect x="340" y="36" width="156" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="418.0" y="52.7" font-size="12" text-anchor="middle">group 2</text><text x="418.0" y="67.7" font-size="12" text-anchor="middle">65536-98303</text><rect x="500" y="36" width="156" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/><text x="578.0" y="52.7" font-size="12" text-anchor="middle">group 3</text><text x="578.0" y="67.7" font-size="12" text-anchor="middle">98304-131071</text><text x="260.0" y="94.0" font-size="12" text-anchor="middle">백업 SB+GDT</text><text x="420.0" y="94.0" font-size="12" text-anchor="middle">저널 65536-69631</text><text x="580.0" y="94.0" font-size="12" text-anchor="middle">백업 SB+GDT</text><text x="20.0" y="124.0" font-size="13" text-anchor="start">group 0 확대 (flex_bg=16: 4개 그룹의 비트맵·inode table이 group 0에 몰려 있다)</text><rect x="20" y="136" width="34" height="36" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.3"/><text x="37.0" y="158.2" font-size="12" text-anchor="middle">SB</text><text x="37.0" y="188.0" font-size="11" text-anchor="middle">0</text><rect x="54" y="136" width="40" height="36" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.3"/><text x="74.0" y="158.2" font-size="12" text-anchor="middle">GDT</text><text x="74.0" y="188.0" font-size="11" text-anchor="middle">1</text><rect x="94" y="136" width="74" height="36" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/><text x="131.0" y="158.2" font-size="12" text-anchor="middle">예약 GDT</text><text x="131.0" y="188.0" font-size="11" text-anchor="middle">2-64</text><rect x="168" y="136" width="64" height="36" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="200.0" y="158.2" font-size="12" text-anchor="middle">B비트맵</text><text x="200.0" y="188.0" font-size="11" text-anchor="middle">65-68</text><rect x="232" y="136" width="64" height="36" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="264.0" y="158.2" font-size="12" text-anchor="middle">I비트맵</text><text x="264.0" y="188.0" font-size="11" text-anchor="middle">69-72</text><rect x="296" y="136" width="170" height="36" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="381.0" y="158.2" font-size="12" text-anchor="middle">inode table ×4</text><text x="381.0" y="188.0" font-size="11" text-anchor="middle">73-2120</text><rect x="466" y="136" width="194" height="36" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="563.0" y="158.2" font-size="12" text-anchor="middle">데이터</text><text x="563.0" y="188.0" font-size="11" text-anchor="middle">2121-</text><text x="20.0" y="214.0" font-size="12" text-anchor="start">예약 GDT = 온라인 resize2fs가 그룹을 늘릴 때 쓸 자리(resize_inode).</text><rect x="20" y="232" width="200" height="92" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="30.0" y="252.0" font-size="13" text-anchor="start" font-weight="bold">inode 13 (frag.bin)</text><text x="30.0" y="272.0" font-size="12" text-anchor="start">i_block 60 B = 헤더 12 B</text><text x="30.0" y="290.0" font-size="12" text-anchor="start">+ 엔트리 4개 × 12 B</text><text x="30.0" y="310.0" font-size="12" text-anchor="start">depth=1, index 1개</text><rect x="270" y="232" width="180" height="92" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="280.0" y="252.0" font-size="13" text-anchor="start" font-weight="bold">leaf 블록 2671</text><text x="280.0" y="272.0" font-size="12" text-anchor="start">magic 0xF30A</text><text x="280.0" y="290.0" font-size="12" text-anchor="start">extent 256/340개</text><text x="280.0" y="310.0" font-size="12" text-anchor="start">(0)→2159 (2)→2161 …</text><rect x="500" y="232" width="160" height="92" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="510.0" y="252.0" font-size="13" text-anchor="start" font-weight="bold">데이터 블록</text><text x="510.0" y="272.0" font-size="12" text-anchor="start">2159, 2161, 2163 …</text><text x="510.0" y="290.0" font-size="12" text-anchor="start">짝수 논리 블록만</text><text x="510.0" y="310.0" font-size="12" text-anchor="start">남음 (홀수 punch)</text><line x1="220" y1="278" x2="268" y2="278" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah3)"/><line x1="450" y1="278" x2="498" y2="278" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah3)"/><text x="20.0" y="350.0" font-size="12" text-anchor="start">extent 하나 = (논리 블록, 길이 ≤ 32768, 물리 블록 48비트) — big.bin 200 MiB는 extent 2개.</text><text x="20.0" y="370.0" font-size="12" text-anchor="start">Don의 FTL 비유: extent tree = 파일 단위 L2P. 다만 '범위(run-length)'로 저장해서 작다.</text><text x="20.0" y="390.0" font-size="12" text-anchor="start">inode 2 (/) → extent → 블록 2121: sbparse.c가 같은 경로를 raw read로 따라간다 (§3.4).</text></svg>
```
그림 3 — 예제 6에서 만든 512 MiB ext4의 실제 배치(dumpe2fs 값). flex_bg 때문에 메타데이터가 group 0에 모여 있다.

핵심 구조:

- **superblock** (항상 바이트 오프셋 1024, 1 KiB): 블록 수, inode 수, 블록 크기, 그룹당 블록/inode, 기능 플래그, UUID, 상태(clean/error), 마운트 횟수, 저널 inode 번호. `sparse_super`면 백업이 그룹 0, 1 그리고 3·5·7의 거듭제곱 번호 그룹에만 있다(512 MiB에서는 그룹 1·3 → 블록 32768, 98304).
- **group descriptor table (GDT)**: 그룹마다 64 B(`64bit` 기능일 때) — 그 그룹의 비트맵·inode table 위치, free 카운트, 체크섬, 플래그(`INODE_UNINIT`, `ITABLE_ZEROED` …).
- **reserved GDT**: 나중에 **온라인 resize**로 그룹이 늘어나도 GDT가 자랄 자리(`resize_inode` 기능). §5.6에서 쓴다.
- **flex_bg**: 16개 그룹의 비트맵과 inode table을 한곳에 연속으로 모은다 → 메타데이터 읽기가 순차가 되고, 큰 파일 데이터가 그룹 경계에서 덜 끊긴다.
- **inode** (256 B): 모드, 크기, 시간 4개(ns 단위 extra 필드 포함), 링크 수, 플래그, 그리고 `i_block` 60 B — ext4에서는 여기에 **extent tree의 루트**가 들어간다.
- **extent**: (논리 블록 32비트, 길이 16비트 ≤ 32768, 물리 블록 48비트) 12 B. inode 안에 헤더 12 B + extent 4개가 들어가고, 넘치면 depth가 생겨 index 노드 → leaf 블록(블록당 (4096−12)/12 = 340개)으로 내려간다. **파일 단위의 run-length L2P 맵**이라고 보면 Don에게 바로 읽힌다.
- **디렉터리**: 작은 디렉터리는 (inode 번호, 이름) 엔트리의 선형 리스트, 커지면 `dir_index`의 **htree**(이름 해시 → 블록) — 수천 개 파일에서도 lookup이 O(1)에 가깝다.
- **metadata_csum**: superblock·GDT·비트맵·inode·extent 블록·디렉터리 블록마다 crc32c. 펌웨어에서 메타데이터에 CRC를 붙이는 것과 같은 이유다.
- **64bit**: 블록 번호 48비트(→ 4 KiB 블록에서 1 EiB), group descriptor 64 B.

### 3.2 예제 6 — mkfs.ext4 · 조각난 파일 · htree 디렉터리 만들고 dumpe2fs/debugfs로 투어

무엇을 확인하나: superblock 필드, 그룹 배치, extent tree depth 1, htree, 저널 inode·저널 superblock을 도구로 직접 본다. 조각난 파일은 아래 C 코드로 만든다(2 MiB를 쓰고 홀수 블록마다 구멍을 뚫어 extent 256개를 강제).

```c
/* frag.c: write a 2 MiB file, then punch every other 4 KiB block
 * -> ~256 separate extents, forcing an extent tree of depth >= 1 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	if (argc != 2) { fprintf(stderr, "usage: %s file\n", argv[0]); exit(1); }
	int fd = open(argv[1], O_CREAT | O_TRUNC | O_WRONLY, 0644);
	if (fd < 0) { perror("open"); exit(1); }
	static char buf[4096];
	for (int i = 0; i < 512; i++) {
		memset(buf, 'a' + i % 26, sizeof(buf));
		if (write(fd, buf, sizeof(buf)) != (ssize_t)sizeof(buf)) { perror("write"); exit(1); }
	}
	if (fsync(fd)) { perror("fsync"); exit(1); }
	for (off_t blk = 1; blk < 512; blk += 2)
		if (fallocate(fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, blk * 4096, 4096)) {
			perror("fallocate"); exit(1);
		}
	if (fsync(fd)) { perror("fsync"); exit(1); }
	printf("%s: 512 blocks written, 256 punched\n", argv[1]);
	close(fd);
	return 0;
}
```

게스트 스크립트:

```sh
export PATH=/opt/bin:$PATH
echo "### mkfs.ext4"; mkfs.ext4 -F -L j2data /dev/vda 2>&1
mkdir -p /mnt/e && mount -t ext4 /dev/vda /mnt/e && cd /mnt/e
/j2/frag /mnt/e/frag.bin
dd if=/dev/zero of=/mnt/e/big.bin bs=1M count=200 2>/dev/null
mkdir /mnt/e/logs; i=0; while [ $i -lt 3000 ]; do : > /mnt/e/logs/sensor_$i.csv; i=$((i+1)); done
cd /; sync; ls -i /mnt/e; umount /mnt/e
echo "### sbparse /dev/vda"; /j2/sbparse /dev/vda
echo "### dumpe2fs -h"; dumpe2fs -h /dev/vda 2>/dev/null
echo "### dumpe2fs groups"; dumpe2fs /dev/vda 2>/dev/null | sed -n '/^Group 0:/,$p' | head -40
echo "### debugfs stat frag.bin"; debugfs -R "stat /frag.bin" /dev/vda 2>/dev/null | head -20
echo "### debugfs ex frag.bin"; debugfs -R "ex /frag.bin" /dev/vda 2>/dev/null | head -12
echo "### debugfs ex big.bin"; debugfs -R "ex /big.bin" /dev/vda 2>/dev/null
echo "### debugfs htree logs"; debugfs -R "htree /logs" /dev/vda 2>/dev/null | head -16
echo "### debugfs stat <8> (journal inode)"; debugfs -R "stat <8>" /dev/vda 2>/dev/null | head -14
echo "### journal superblock"; debugfs -R "logdump -S" /dev/vda 2>/dev/null | head -20
```

```text
lab initramfs: 6.18.55-fs up 1.35s
### mkfs.ext4
mke2fs 1.47.2 (1-Jan-2025)
Discarding device blocks:      0/131072             done
Creating filesystem with 131072 4k blocks and 32768 inodes
Filesystem UUID: 9e6608ae-5c1d-47d7-a1f2-07e36ca558c1
Superblock backups stored on blocks:
	32768, 98304

Allocating group tables: 0/4   done
Writing inode tables: 0/4   done
Creating journal (4096 blocks): done
Writing superblocks and filesystem accounting information: 0/4   done

/mnt/e/frag.bin: 512 blocks written, 256 punched
     14 big.bin        8193 logs
     13 frag.bin         11 lost+found
### sbparse /dev/vda
s_magic        0xef53 (EXT4)
block size     4096
blocks         131072 (512 MiB)
inodes         32768, inode size 256, per group 8192
blocks/group   32768, groups 4, flex_bg 2^4
state 1 errors 1 mnt_count 1 journal_inum 8 desc_size 64
features compat 0x103c incompat 0x22c2 ro_compat 0x46b
  has_journal dir_index filetype extents 64bit flex_bg sparse_super large_file huge_file dir_nlink extra_isize metadata_csum
s_checksum 0x1da759b3  crc32c(sb[0..0x3fc]) 0x1da759b3
group0: block_bitmap 65 inode_bitmap 69 inode_table 73 free_blocks 30324
inode 2 (/): mode 040755 size 4096 flags 0x80000 extent magic 0xf30a entries 1 depth 0
  extent[0]: lblk 0 len 1 pblk 2121
### dumpe2fs -h
Filesystem volume name:   j2data
Last mounted on:          /mnt/e
Filesystem UUID:          9e6608ae-5c1d-47d7-a1f2-07e36ca558c1
Filesystem magic number:  0xEF53
Filesystem revision #:    1 (dynamic)
Filesystem features:      has_journal ext_attr resize_inode dir_index orphan_file filetype extent 64bit flex_bg metadata_csum_seed sparse_super large_file huge_file dir_nlink extra ...
Filesystem flags:         unsigned_directory_hash
Default mount options:    user_xattr acl
Filesystem state:         clean
Errors behavior:          Continue
Filesystem OS type:       Linux
Inode count:              32768
Block count:              131072
Reserved block count:     6553
Overhead clusters:        6347
Free blocks:              73202
Free inodes:              29753
First block:              0
Block size:               4096
Fragment size:            4096
Group descriptor size:    64
Reserved GDT blocks:      63
Blocks per group:         32768
Fragments per group:      32768
Inodes per group:         8192
Inode blocks per group:   512
Flex block group size:    16
Filesystem created:       Sat Oct 17 21:46:02 2026
Last mount time:          Sat Oct 17 21:46:02 2026
Last write time:          Sat Oct 17 21:46:04 2026
Mount count:              1
Maximum mount count:      -1
Last checked:             Sat Oct 17 21:46:02 2026
Check interval:           0 (<none>)
Lifetime writes:          208 MB
Reserved blocks uid:      0 (user unknown)
Reserved blocks gid:      0 (group unknown)
First inode:              11
Inode size:               256
Required extra isize:     32
Desired extra isize:      32
Journal inode:            8
Default directory hash:   half_md4
Directory Hash Seed:      7ace1a09-30f2-4154-8e82-e629f393736e
Journal backup:           inode blocks
Checksum type:            crc32c
Checksum:                 0x1da759b3
Checksum seed:            0x61fc65bd
Orphan file inode:        12
Journal features:         journal_incompat_revoke journal_64bit journal_checksum_v3
Total journal size:       16M
Total journal blocks:     4096
Max transaction length:   4096
Fast commit length:       0
Journal sequence:         0x00000007
Journal start:            0
Journal checksum type:    crc32c
Journal checksum:         0x99e5b0a4

### dumpe2fs groups
Group 0: (Blocks 0-32767) csum 0x5fcb [ITABLE_ZEROED]
  Primary superblock at 0, Group descriptors at 1-1
  Reserved GDT blocks at 2-64
  Block bitmap at 65 (+65), csum 0xb9da2fcf
  Inode bitmap at 69 (+69), csum 0x11270346
  Inode table at 73-584 (+73)
  30324 free blocks, 5178 free inodes, 2 directories, 5178 unused inodes
  Free blocks: 2216, 2218, 2220, 2222, 2224, 2226, 2228, 2230, 2232, 2234, 2236, 2238, 2240, 2242, 2244, 2246, 2248, 2250, 2252, 2254, 2256, 2258, 2260, 2262, 2264, 2266, 2268, 227 ...
  Free inodes: 3015-8192
Group 1: (Blocks 32768-65535) csum 0xfbf3 [ITABLE_ZEROED]
  Backup superblock at 32768, Group descriptors at 32769-32769
  Reserved GDT blocks at 32770-32832
  Block bitmap at 66 (bg #0 + 66), csum 0xa62eb6e5
  Inode bitmap at 70 (bg #0 + 70), csum 0x17659b85
  Inode table at 585-1096 (bg #0 + 585)
  1983 free blocks, 8191 free inodes, 1 directories, 8191 unused inodes
  Free blocks: 32833-34815
  Free inodes: 8194-16384
Group 2: (Blocks 65536-98303) csum 0x2f00 [INODE_UNINIT]
  Block bitmap at 67 (bg #0 + 67), csum 0xe43d9a88
  Inode bitmap at 71 (bg #0 + 71), csum 0x00000000
  Inode table at 1097-1608 (bg #0 + 1097)
  8192 free blocks, 8192 free inodes, 0 directories, 8192 unused inodes
  Free blocks: 90112-98303
  Free inodes: 16385-24576
Group 3: (Blocks 98304-131071) csum 0x5c8b [INODE_UNINIT]
  Backup superblock at 98304, Group descriptors at 98305-98305
  Reserved GDT blocks at 98306-98368
  Block bitmap at 68 (bg #0 + 68), csum 0x4d7db31a
  Inode bitmap at 72 (bg #0 + 72), csum 0x00000000
  Inode table at 1609-2120 (bg #0 + 1609)
  32703 free blocks, 8192 free inodes, 0 directories, 8192 unused inodes
  Free blocks: 98369-131071
  Free inodes: 24577-32768
### debugfs stat frag.bin
Inode: 13   Type: regular    Mode:  0644   Flags: 0x80000
Generation: 3643453128    Version: 0x00000000:00000011
User:     0   Group:     0   Project:     0   Size: 2097152
File ACL: 0
Links: 1   Blockcount: 2056
Fragment:  Address: 0    Number: 0    Size: 0
 ctime: 0x6ad3ec9b:02dc6c00 -- Sat Oct 17 21:46:03 2026
 atime: 0x6ad3ec9a:dd40a000 -- Sat Oct 17 21:46:02 2026
 mtime: 0x6ad3ec9b:02dc6c00 -- Sat Oct 17 21:46:03 2026
crtime: 0x6ad3ec9a:dd40a000 -- Sat Oct 17 21:46:02 2026
Size of extra inode fields: 32
Inode checksum: 0xbea5dc62
EXTENTS:
(ETB0):2671, (0):2159, (2):2161, (4):2163, (6):2165, (8):2167, (10):2169, (12):2171, (14):2173, (16):2175, (18):2177, (20):2179, (22):2181, (24):2183, (26):2185, (28):2187, (30):21 ...
### debugfs ex frag.bin
Level Entries       Logical        Physical Length Flags
 0/ 1   1/  1     0 -   511   2671             512
 1/ 1   1/256     0 -     0   2159 -   2159      1
 1/ 1   2/256     2 -     2   2161 -   2161      1
 1/ 1   3/256     4 -     4   2163 -   2163      1
 1/ 1   4/256     6 -     6   2165 -   2165      1
 1/ 1   5/256     8 -     8   2167 -   2167      1
 1/ 1   6/256    10 -    10   2169 -   2169      1
 1/ 1   7/256    12 -    12   2171 -   2171      1
 1/ 1   8/256    14 -    14   2173 -   2173      1
 1/ 1   9/256    16 -    16   2175 -   2175      1
 1/ 1  10/256    18 -    18   2177 -   2177      1
### debugfs ex big.bin
Level Entries       Logical        Physical Length Flags
 0/ 0   1/  2     0 - 30719  34816 -  65535  30720
 0/ 0   2/  2 30720 - 51199  69632 -  90111  20480
### debugfs htree logs
Root node dump:
	 Reserved zero: 0
	 Hash Version: 1
	 Info length: 8
	 Indirect levels: 0
	 Flags: 0
Number of entries (count): 26
Number of entries (limit): 507
Checksum: 0xddb94ef2
Entry #0: Hash 0x00000000, block 1
Entry #1: Hash 0x08705eb2, block 26
Entry #2: Hash 0x0ed9613a, block 11
Entry #3: Hash 0x16faf038, block 20
Entry #4: Hash 0x201ce600, block 7
Entry #5: Hash 0x2c16c758, block 15
Entry #6: Hash 0x3aa603fe, block 3
### debugfs stat <8> (journal inode)
Inode: 8   Type: regular    Mode:  0600   Flags: 0x80000
Generation: 0    Version: 0x00000000:00000000
User:     0   Group:     0   Project:     0   Size: 16777216
File ACL: 0
Links: 1   Blockcount: 32768
Fragment:  Address: 0    Number: 0    Size: 0
 ctime: 0x6ad3ec9a:00000000 -- Sat Oct 17 21:46:02 2026
 atime: 0x6ad3ec9a:00000000 -- Sat Oct 17 21:46:02 2026
 mtime: 0x6ad3ec9a:00000000 -- Sat Oct 17 21:46:02 2026
crtime: 0x6ad3ec9a:00000000 -- Sat Oct 17 21:46:02 2026
Size of extra inode fields: 32
Inode checksum: 0x8b4c7bf4
EXTENTS:
(0-4095):65536-69631
### journal superblock
Journal features:         journal_incompat_revoke journal_64bit journal_checksum_v3
Total journal size:       16M
Total journal blocks:     4096
Max transaction length:   4096
Fast commit length:       0
Journal sequence:         0x00000007
Journal start:            0
Journal checksum type:    crc32c
Journal checksum:         0x99e5b0a4

Journal starts at block 0, transaction 7
```

(`dumpe2fs groups`의 group 0 "Free blocks:" 줄은 짝수 번호가 수백 개 이어지는 긴 줄이라 앞부분만 남기고 `...`로 줄였고, `debugfs stat`의 EXTENTS 목록도 앞부분만 남겼다. 나머지는 그대로다.)

출력에서 볼 것:

- **mkfs**: "Discarding device blocks" — mkfs가 먼저 장치 전체에 discard(TRIM)를 보낸다(virtio가 discard를 지원하므로). 저널 4096블록 = 16 MiB, 백업 superblock은 32768·98304(= 그룹 1·3).
- **그룹 배치**: 그룹 0에 SB(0), GDT(1), 예약 GDT(2-64), 그 뒤 4개 그룹 분의 블록 비트맵(65-68), inode 비트맵(69-72), inode table(73-2120)이 몰려 있다 — 그룹 1~3의 항목이 `bg #0 + 66`처럼 그룹 0을 가리킨다. 이것이 flex_bg다.
- **ITABLE_ZEROED / INODE_UNINIT**: 그룹 2·3은 inode를 하나도 쓰지 않아 `INODE_UNINIT`이고 `ITABLE_ZEROED`가 아직 없다. mkfs는 기본(`lazy_itable_init`)으로 inode table을 0으로 채우지 않고 넘어가고, 마운트 후 커널 스레드 `ext4lazyinit`이 백그라운드로 채운다 — 큰 eMMC에서 mkfs가 몇 초 만에 끝나는 이유이자, 첫 부팅 직후 원인 모를 쓰기 I/O가 보이는 이유다(마운트 옵션 `init_itable=n`으로 속도 조절, `noinit_itable`로 끔).
- **저널 위치**: 저널 inode 8의 extent가 `(0-4095):65536-69631` — 디스크 **가운데**(그룹 2 시작)에 있다. mke2fs는 HDD 시절의 탐색 거리 때문에 저널을 가운데에 두는데, flash에서는 상관없다.
- **extent tree**: `frag.bin`은 `Level 0/1` index 1개 → leaf 블록 2671 안에 extent 256개(`1/256 … `), 각 길이 1. `big.bin` 200 MiB는 depth 0, extent **2개**(30720 + 20480블록) — 순차로 쓴 큰 파일은 맵이 몇 십 바이트로 끝난다. FTL의 page 단위 맵과 비교하면 run-length 맵의 효율이 보인다.
- **htree**: 파일 3000개 디렉터리 → `Indirect levels: 0`(루트 1단), 리프 블록 26개, 루트 엔트리 limit 507. `Hash Version: 1` = half_md4.
- **저널 superblock**: `journal_64bit journal_checksum_v3`, 최대 트랜잭션 4096블록, `Journal start: 0` = 깨끗이 언마운트되어 재생할 것이 없다.

### 3.3 lazy init, 예약 블록, mkfs 기본값은 어디서 오나

`mkfs.ext4`의 기본값은 `/etc/mke2fs.conf`에서 온다(이 노트의 게스트에도 VM의 파일을 복사했다). 그래서 **같은 mkfs 명령도 배포판마다 기능 플래그가 다르다** — 예를 들어 이번 e2fsprogs 1.47.2는 `orphan_file`, `metadata_csum_seed`를 기본으로 켠다. 오래된 커널·U-Boot의 ext4 드라이버가 모르는 incompat 기능이 켜지면 **마운트 자체를 거부**한다(ro_compat 기능은 읽기 전용으로는 붙는다). 부트로더가 rootfs의 커널 이미지를 직접 읽는 보드라면 `mkfs.ext4 -O ^metadata_csum_seed,^orphan_file` 처럼 기능을 끄고 만든다 — 이게 실무에서 꽤 자주 만나는 부트 실패 원인이다.

**예약 블록**: 기본 5%는 root만 쓸 수 있다(`Reserved block count: 6553` = 131072의 5%). 이유는 (1) 일반 사용자가 디스크를 꽉 채워도 root 데몬·로그가 돌 수 있게, (2) 블록 할당자가 연속 공간을 찾을 여유. 수십 GB 데이터 파티션에서 5%는 낭비라 `tune2fs -m 1` 처럼 줄인다(예제 10에서 실행).

### 3.4 예제 7 — superblock을 raw read로 직접 파싱 (커널 구조체 ↔ 디스크 바이트)

무엇을 확인하나: `struct ext4_super_block`(`fs/ext4/ext4.h`)의 필드 오프셋이 그대로 디스크 바이트다. superblock → group descriptor 0 → inode table → inode 2(루트 디렉터리) → extent 헤더까지 손으로 따라가고, superblock crc32c를 직접 계산해 저장된 값과 비교한다.

```c
/* sbparse.c: read the ext4 superblock + group desc 0 + root inode straight off a block device */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define U16(p, o) ((uint16_t)((p)[o] | (p)[(o) + 1] << 8))
#define U32(p, o) ((uint32_t)U16(p, o) | (uint32_t)U16(p, (o) + 2) << 16)

static uint32_t crc32c(uint32_t c, const uint8_t *p, size_t n)
{
	while (n--) { c ^= *p++; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0x82F63B78 & -(c & 1)); }
	return c;
}
static void rd(int fd, void *b, size_t n, off_t off)
{
	if (pread(fd, b, n, off) != (ssize_t)n) { perror("pread"); exit(1); }
}
int main(int argc, char **argv)
{
	uint8_t sb[1024], gd[64], ino[256];
	if (argc != 2) { fprintf(stderr, "usage: %s /dev/X\n", argv[0]); exit(1); }
	int fd = open(argv[1], O_RDONLY);
	if (fd < 0) { perror("open"); exit(1); }
	rd(fd, sb, sizeof(sb), 1024);                    /* superblock always at byte 1024 */
	printf("s_magic        0x%04x %s\n", U16(sb, 0x38), U16(sb, 0x38) == 0xEF53 ? "(EXT4)" : "(?)");
	uint32_t bs = 1024u << U32(sb, 0x18);
	uint64_t blocks = U32(sb, 0x04) | (uint64_t)U32(sb, 0x150) << 32;
	printf("block size     %u\nblocks         %llu (%llu MiB)\n", bs,
	       (unsigned long long)blocks, (unsigned long long)(blocks * bs >> 20));
	printf("inodes         %u, inode size %u, per group %u\n", U32(sb, 0x00), U16(sb, 0x58), U32(sb, 0x28));
	printf("blocks/group   %u, groups %llu, flex_bg 2^%u\n", U32(sb, 0x20),
	       (unsigned long long)((blocks + U32(sb, 0x20) - 1) / U32(sb, 0x20)), sb[0x174]);
	printf("state %u errors %u mnt_count %u journal_inum %u desc_size %u\n", U16(sb, 0x3A),
	       U16(sb, 0x3C), U16(sb, 0x34), U32(sb, 0xE0), U16(sb, 0xFE));
	uint32_t c = U32(sb, 0x5C), i = U32(sb, 0x60), r = U32(sb, 0x64);
	printf("features compat 0x%x incompat 0x%x ro_compat 0x%x\n ", c, i, r);
	const char *nm[] = { "has_journal", "dir_index", "filetype", "needs_recovery", "extents", "64bit",
			     "flex_bg", "sparse_super", "large_file", "huge_file", "dir_nlink", "extra_isize", "metadata_csum" };
	uint32_t w[] = { c & 0x4, c & 0x20, i & 0x2, i & 0x4, i & 0x40, i & 0x80, i & 0x200,
			 r & 0x1, r & 0x2, r & 0x8, r & 0x20, r & 0x40, r & 0x400 };
	for (int k = 0; k < 13; k++) if (w[k]) printf(" %s", nm[k]);
	printf("\ns_checksum 0x%08x  crc32c(sb[0..0x3fc]) 0x%08x\n", U32(sb, 0x3FC), crc32c(~0u, sb, 0x3FC));
	rd(fd, gd, sizeof(gd), bs == 1024 ? 2048 : bs);   /* group descriptor table: block after sb */
	uint32_t itab = U32(gd, 0x08);
	printf("group0: block_bitmap %u inode_bitmap %u inode_table %u free_blocks %u\n",
	       U32(gd, 0x00), U32(gd, 0x04), itab, U16(gd, 0x0C));
	rd(fd, ino, sizeof(ino), (off_t)itab * bs + 1 * U16(sb, 0x58)); /* inode 2 = index 1 */
	printf("inode 2 (/): mode 0%o size %u flags 0x%x extent magic 0x%04x entries %u depth %u\n",
	       U16(ino, 0x00), U32(ino, 0x04), U32(ino, 0x20), U16(ino, 0x28), U16(ino, 0x2A), U16(ino, 0x2E));
	printf("  extent[0]: lblk %u len %u pblk %u\n", U32(ino, 0x34), U16(ino, 0x38), U32(ino, 0x3C));
	close(fd);
	return 0;
}
```

출력(예제 6과 같은 부팅에서 실행, 위 출력의 `### sbparse /dev/vda` 부분을 다시 옮김):

```text
s_magic        0xef53 (EXT4)
block size     4096
blocks         131072 (512 MiB)
inodes         32768, inode size 256, per group 8192
blocks/group   32768, groups 4, flex_bg 2^4
state 1 errors 1 mnt_count 1 journal_inum 8 desc_size 64
features compat 0x103c incompat 0x22c2 ro_compat 0x46b
  has_journal dir_index filetype extents 64bit flex_bg sparse_super large_file huge_file dir_nlink extra_isize metadata_csum
s_checksum 0x1da759b3  crc32c(sb[0..0x3fc]) 0x1da759b3
group0: block_bitmap 65 inode_bitmap 69 inode_table 73 free_blocks 30324
inode 2 (/): mode 040755 size 4096 flags 0x80000 extent magic 0xf30a entries 1 depth 0
  extent[0]: lblk 0 len 1 pblk 2121
```

출력에서 볼 것: 직접 계산한 crc32c가 `dumpe2fs`의 `Checksum: 0x1da759b3`과 같다(초기값 `~0`, 최종 반전 없음). GDT 0의 값(65/69/73)이 dumpe2fs의 group 0과 같다. 루트 inode의 `flags 0x80000` = `EXT4_EXTENTS_FL`, `i_block`의 첫 2바이트가 extent 헤더 매직 `0xF30A`. 루트 디렉터리 데이터는 블록 2121 — 그림 3의 "데이터 2121-"의 첫 블록이다. 이 C 코드가 하는 일이 U-Boot의 `ext4load`, GRUB, e2fsprogs가 하는 일의 축소판이다. 부트로더가 커널을 ext4에서 읽다가 실패하면 결국 이 경로 어딘가(모르는 기능 플래그, 체크섬)에서 막힌 것이다.

---

## 4. jbd2 — 저널링

### 4.1 직관 — "맵을 바꾸기 전에 로그부터"

파일 하나를 늘리는 append조차 여러 메타데이터 블록을 건드린다: inode(크기·extent), 블록 비트맵, group descriptor(free 카운트), 경우에 따라 extent leaf. 이 중 일부만 디스크에 반영된 채 전원이 꺼지면 "비트맵은 사용 중인데 아무 inode도 그 블록을 안 가리킨다" 같은 불일치가 남는다. 옛 ext2는 부팅 때 `e2fsck`가 디스크 전체를 훑어 고쳤다(수 분~수십 분).

ext4는 **jbd2**(Journaling Block Device 2, `fs/jbd2/`)에 맡긴다. 바뀐 메타데이터 블록을 먼저 **저널(디스크 위 원형 로그)**에 통째로 쓰고, 마지막에 **commit 블록**을 쓴다. commit 블록까지 디스크에 있으면 그 트랜잭션은 "일어난 일", 없으면 "없던 일"이다. 크래시 후 마운트(또는 e2fsck)는 commit된 트랜잭션만 다시 쓴다(replay). Don의 FTL에서 L2P 업데이트를 journal/log로 먼저 남기고 주기적으로 맵 스냅샷을 쓰는 방식과 같은 원리다 — 차이는 jbd2가 **블록 이미지 전체**(physical journaling)를 로그에 쓴다는 점.

용어:

- **handle**: 파일시스템 연산 하나(예: `write`의 블록 할당)가 여는 작은 단위(`ext4_journal_start` → `jbd2__journal_start`). 여러 handle이 하나의 트랜잭션에 합쳐진다.
- **transaction**: 일정 시간(`commit=`, 기본 5초 = `JBD2_DEFAULT_MAX_COMMIT_AGE`) 또는 fsync 요청까지 모인 handle들의 묶음. 상태: `T_RUNNING → T_LOCKED → T_SWITCH → T_FLUSH → T_COMMIT → T_COMMIT_DFLUSH → T_COMMIT_JFLUSH → T_COMMIT_CALLBACK → T_FINISHED` (`include/linux/jbd2.h`).
- **commit**: `kjournald2` 스레드(이름 `jbd2/vda-8`)가 `jbd2_journal_commit_transaction()`으로 디스크립터 블록 + 메타데이터 블록 사본 + commit 블록을 저널에 쓴다. commit 블록은 `REQ_PREFLUSH | REQ_FUA`로 나간다(`fs/jbd2/commit.c`).
- **checkpoint**: 저널에 있는 블록을 원래 자리(home location)에 쓰고 나면 그 저널 공간을 재사용할 수 있다. `jbd2_log_do_checkpoint()` → `jbd2_cleanup_journal_tail()`이 저널 superblock의 tail을 전진시킨다. FTL의 "맵 스냅샷을 쓰고 나면 그 이전 로그를 버린다"와 같다.
- **revoke**: 저널에 있던 블록이 그 뒤 해제되었다는 표시 — replay 때 옛 내용으로 덮어쓰지 않게.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="20.0" y="22.0" font-size="13" text-anchor="start" font-weight="bold">fsync 1회 = jbd2 트랜잭션 1개 (실측 trace, t=0은 fsync 진입, 단위 ms)</text><text x="20.0" y="64.0" font-size="12" text-anchor="start">앱 (fsync)</text><line x1="130" y1="60" x2="660" y2="60" stroke="#888" stroke-width="1" stroke-dasharray="4 3"/><text x="20.0" y="134.0" font-size="12" text-anchor="start">jbd2/vda-8</text><line x1="130" y1="130" x2="660" y2="130" stroke="#888" stroke-width="1" stroke-dasharray="4 3"/><text x="20.0" y="204.0" font-size="12" text-anchor="start">장치 (vda)</text><line x1="130" y1="200" x2="660" y2="200" stroke="#888" stroke-width="1" stroke-dasharray="4 3"/><circle cx="130.0" cy="60" r="5" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/><text x="130.0" y="50.0" font-size="11" text-anchor="start">sync_file_enter</text><circle cx="275.6" cy="200" r="5" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/><text x="275.6" y="222.0" font-size="11" text-anchor="middle">데이터 4K</text><circle cx="330.8" cy="130" r="5" fill="#3f9a6b" fill-opacity="0.7" stroke="#3f9a6b"/><text x="330.8" y="120.0" font-size="11" text-anchor="middle">start_commit</text><circle cx="352.4" cy="200" r="5" fill="#3f9a6b" fill-opacity="0.7" stroke="#3f9a6b"/><text x="352.4" y="222.0" font-size="11" text-anchor="middle">로그 16K</text><circle cx="391.8" cy="200" r="5" fill="#d0564a" fill-opacity="0.7" stroke="#d0564a"/><text x="391.8" y="190.0" font-size="11" text-anchor="middle">FLUSH</text><circle cx="519.7" cy="200" r="5" fill="#3f9a6b" fill-opacity="0.7" stroke="#3f9a6b"/><text x="519.7" y="222.0" font-size="11" text-anchor="middle">commit 블록</text><circle cx="542.1" cy="200" r="5" fill="#d0564a" fill-opacity="0.7" stroke="#d0564a"/><text x="542.1" y="190.0" font-size="11" text-anchor="middle">FLUSH</text><circle cx="623.1" cy="130" r="5" fill="#3f9a6b" fill-opacity="0.7" stroke="#3f9a6b"/><text x="623.1" y="152.0" font-size="11" text-anchor="end">end_commit</text><circle cx="639.9" cy="60" r="5" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/><text x="639.9" y="50.0" font-size="11" text-anchor="end">sync_file_exit</text><line x1="130.0" y1="66" x2="275.5607142857143" y2="194" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/><line x1="330.83214285714286" y1="136" x2="352.41071428571433" y2="194" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/><line x1="623.0892857142858" y1="124" x2="639.9357142857143" y2="66" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/><text x="130.0" y="238.0" font-size="11" text-anchor="middle">0.0</text><text x="224.6" y="238.0" font-size="11" text-anchor="middle">0.5</text><text x="319.3" y="238.0" font-size="11" text-anchor="middle">1.0</text><text x="413.9" y="238.0" font-size="11" text-anchor="middle">1.5</text><text x="508.6" y="238.0" font-size="11" text-anchor="middle">2.0</text><text x="603.2" y="238.0" font-size="11" text-anchor="middle">2.5</text><rect x="20" y="252" width="640" height="66" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/><text x="30.0" y="272.0" font-size="12" text-anchor="start">data=ordered 순서: ① 데이터 블록을 제자리에 → ② 저널에 디스크립터+메타데이터 3블록</text><text x="30.0" y="290.0" font-size="12" text-anchor="start">③ FLUSH(앞의 것 영구화) → ④ commit 블록(REQ_PREFLUSH|REQ_FUA) → ⑤ FUA가 없어 FLUSH로 대체</text><text x="30.0" y="308.0" font-size="12" text-anchor="start">트랜잭션 상태: RUNNING → LOCKED → SWITCH → FLUSH → COMMIT → FINISHED, 그 뒤 checkpoint</text></svg>
```
그림 4 — fsync 한 번이 만든 jbd2 트랜잭션의 실제 타임라인(예제 9의 trace, transaction 3). 데이터 → 로그 → FLUSH → commit 블록 → FLUSH.

### 4.2 세 가지 data 모드

| 모드 | 저널에 들어가는 것 | 순서 보장 | 크래시 후 최악 | 언제 |
|---|---|---|---|---|
| `data=ordered` (기본) | 메타데이터만 | 메타데이터 commit **전에** 그 메타가 가리키는 새 데이터 블록을 먼저 씀 | 마지막 몇 초 데이터 유실, 하지만 **엉뚱한 옛 데이터가 보이지는 않음** | 거의 항상 |
| `data=writeback` | 메타데이터만 | 데이터와 메타 순서 없음 | 파일 크기는 늘었는데 내용은 **이전에 그 블록에 있던 남의 데이터**(stale data) | 성능 최우선, 보안 상관없는 스크래치 |
| `data=journal` | 메타데이터 + **데이터도** | 데이터도 commit 단위로 원자적 | 가장 안전, 대신 데이터를 두 번 씀 | 작은 동기 쓰기 위주, 강한 일관성 필요 |

`data=journal`은 delalloc과 `O_DIRECT`를 끈다. 모드는 마운트 때만 정할 수 있고(remount로 못 바꿈), `tune2fs -o journal_data`로 기본값을 superblock에 넣을 수도 있다.

### 4.3 예제 8 — 저널 모드별로 "append 4 KiB + fsync"를 400번

무엇을 확인하나: 센서 로그 패턴(작게 쓰고 매번 fsync)에서 모드·옵션마다 장치가 실제로 받는 바이트와 FLUSH 수. `/sys/block/vda/stat`의 쓰기 섹터(7번째 필드)와 flush 횟수(16번째 필드)를 루프 앞뒤로 읽는다.

```c
/* fsyncbench.c: N x (append 4 KiB + fsync) -- the "sensor log" pattern.
 * prints ops/s and what the device actually saw (/sys/block/<dev>/stat) */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void devstat(const char *dev, unsigned long long v[17])
{
	char p[64]; snprintf(p, sizeof(p), "/sys/block/%s/stat", dev);
	FILE *f = fopen(p, "r");
	if (!f) { perror(p); exit(1); }
	memset(v, 0, 17 * sizeof(v[0]));
	for (int i = 0; i < 17 && fscanf(f, "%llu", &v[i]) == 1; i++) ;
	fclose(f);
}
int main(int argc, char **argv)
{
	if (argc != 5) { fprintf(stderr, "usage: %s file n size dev\n", argv[0]); exit(1); }
	int n = atoi(argv[2]); size_t sz = strtoul(argv[3], NULL, 0);
	char *buf = malloc(sz); memset(buf, 'L', sz);
	int fd = open(argv[1], O_CREAT | O_TRUNC | O_WRONLY | O_APPEND, 0644);
	if (fd < 0 || !buf) { perror("open"); exit(1); }
	unsigned long long a[17], b[17];
	struct timespec t0, t1;
	sync(); devstat(argv[4], a); clock_gettime(CLOCK_MONOTONIC, &t0);
	for (int i = 0; i < n; i++) {
		if (write(fd, buf, sz) != (ssize_t)sz) { perror("write"); exit(1); }
		if (fsync(fd)) { perror("fsync"); exit(1); }
	}
	clock_gettime(CLOCK_MONOTONIC, &t1); devstat(argv[4], b);
	double s = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
	double app = (double)n * sz / 512;               /* sectors the app asked for */
	printf("ops/s %.0f  wr_ios %llu  wr_sect %llu  flush %llu  per-fsync KiB %.1f  dev/app %.2f\n",
	       n / s, b[4] - a[4], b[6] - a[6], b[15] - a[15],
	       (b[6] - a[6]) / 2.0 / n, (b[6] - a[6]) / app);
	close(fd);
	return 0;
}
```

```sh
export PATH=/opt/bin:$PATH
mkdir -p /mnt/e
run() { mkfs.ext4 -q -F /dev/vda; mount -t ext4 -o $1 /dev/vda /mnt/e || return
  for r in 1 2; do echo "[$1] run$r $(/j2/fsyncbench /mnt/e/log.bin 400 4096 vda)"; done
  [ "$2" = info ] && cat /proc/fs/jbd2/vda-8/info; umount /mnt/e; }
run data=ordered info
run data=writeback
run data=journal info
run data=ordered,barrier=0
run data=ordered,journal_async_commit
echo "### 512B appends, data=ordered vs journal"
for m in ordered journal; do mkfs.ext4 -q -F /dev/vda; mount -o data=$m /dev/vda /mnt/e
  echo "[$m 512B] $(/j2/fsyncbench /mnt/e/log.bin 400 512 vda)"; umount /mnt/e; done
```

```text
lab initramfs: 6.18.55-fs up 1.43s
/dev/vda contains a ext4 file system labelled 'j2data'
	last mounted on /mnt/e on Sat Oct 17 21:46:02 2026
[data=ordered] run1 ops/s 405  wr_ios 1200  wr_sect 19200  flush 800  per-fsync KiB 24.0  dev/app 6.00
[data=ordered] run2 ops/s 407  wr_ios 1201  wr_sect 19208  flush 801  per-fsync KiB 24.0  dev/app 6.00
802 transactions (802 requested), each up to 1365 blocks
average:
  0ms waiting for transaction
  0ms request delay
  0ms running transaction
  0ms transaction was being locked
  0ms flushing data (in ordered mode)
  0ms logging transaction
  1674us average transaction commit time
  2 handles per transaction
  3 blocks per transaction
  5 logged blocks per transaction
/dev/vda contains a ext4 file system
	last mounted on /mnt/e on Sat Oct 17 21:46:19 2026
[data=writeback] run1 ops/s 481  wr_ios 1201  wr_sect 19208  flush 801  per-fsync KiB 24.0  dev/app 6.00
[data=writeback] run2 ops/s 553  wr_ios 1201  wr_sect 19208  flush 801  per-fsync KiB 24.0  dev/app 6.00
/dev/vda contains a ext4 file system
	last mounted on /mnt/e on Sat Oct 17 21:46:22 2026
[data=journal] run1 ops/s 538  wr_ios 1601  wr_sect 22408  flush 1201  per-fsync KiB 28.0  dev/app 7.00
[data=journal] run2 ops/s 524  wr_ios 1603  wr_sect 22408  flush 1202  per-fsync KiB 28.0  dev/app 7.00
802 transactions (802 requested), each up to 1365 blocks
average:
  0ms waiting for transaction
  0ms request delay
  0ms running transaction
  0ms transaction was being locked
  0ms flushing data (in ordered mode)
  0ms logging transaction
  755us average transaction commit time
  2 handles per transaction
  4 blocks per transaction
  6 logged blocks per transaction
/dev/vda contains a ext4 file system
	last mounted on /mnt/e on Sat Oct 17 21:46:23 2026
[data=ordered,barrier=0] run1 ops/s 918  wr_ios 1200  wr_sect 22400  flush 0  per-fsync KiB 28.0  dev/app 7.00
[data=ordered,barrier=0] run2 ops/s 902  wr_ios 1201  wr_sect 19208  flush 0  per-fsync KiB 24.0  dev/app 6.00
/dev/vda contains a ext4 file system
	last mounted on /mnt/e on Sat Oct 17 21:46:25 2026
[    8.091461] EXT4-fs (vda): can't mount with journal_async_commit in data=ordered mode
mount: mounting /dev/vda on /mnt/e failed: Invalid argument
### 512B appends, data=ordered vs journal
/dev/vda contains a ext4 file system
	created on Sat Oct 17 21:46:26 2026
[    8.184265] EXT4-fs: Mount option(s) incompatible with ext2
[ordered 512B] ops/s 589  wr_ios 1200  wr_sect 13600  flush 800  per-fsync KiB 17.0  dev/app 34.00
/dev/vda contains a ext4 file system
	last mounted on /mnt/e on Sat Oct 17 21:46:26 2026
[    8.968777] EXT4-fs: Mount option(s) incompatible with ext2
[journal 512B] ops/s 573  wr_ios 1600  wr_sect 16800  flush 1200  per-fsync KiB 21.0  dev/app 42.00
```

(`/proc/fs/jbd2/vda-8/info`의 0ms 줄들은 그대로 두었다. 마운트 실패 줄은 실제 결과다. 마지막 512 B 부분은 `-t ext4` 없이 마운트해서 BusyBox mount가 먼저 ext2로 시도한 흔적 `Mount option(s) incompatible with ext2`가 남았다 — ext4로 붙었고 결과에는 영향이 없다.)

이어서 `fast_commit`, `commit=` 간격, 그리고 tracepoint(예제 9)를 한 부팅에서 돌렸다.

```sh
export PATH=/opt/bin:$PATH
mkdir -p /mnt/e
echo "### fast_commit"
mkfs.ext4 -q -F -O fast_commit /dev/vda; mount -t ext4 /dev/vda /mnt/e
for r in 1 2; do echo "[fast_commit] run$r $(/j2/fsyncbench /mnt/e/log.bin 400 4096 vda)"; done
grep -E "fc stats|commits|ineligible|numblks|ms" /proc/fs/ext4/vda/fc_info | head -8; umount /mnt/e
echo "### commit= interval, buffered appends every 100 ms for 10 s, no fsync"
for c in 1 5 30; do mkfs.ext4 -q -F /dev/vda; mount -t ext4 -o commit=$c /dev/vda /mnt/e
  i=0; while [ $i -lt 100 ]; do echo "t=$i imu=0.01,0.02,9.81" >> /mnt/e/s.csv; usleep 100000; i=$((i+1)); done
  echo "commit=$c: $(head -1 /proc/fs/jbd2/vda-8/info)"; umount /mnt/e; done
echo "### tracepoints: 2 x (append 4K + fsync), data=ordered"
mkfs.ext4 -q -F /dev/vda; mount -t ext4 /dev/vda /mnt/e; sync
mount -t tracefs none /sys/kernel/tracing; T=/sys/kernel/tracing
for e in jbd2/jbd2_start_commit jbd2/jbd2_commit_flushing jbd2/jbd2_commit_logging jbd2/jbd2_end_commit ext4/ext4_sync_file_enter ext4/ext4_sync_file_exit block/block_rq_issue; do echo 1 > $T/events/$e/enable; done
echo > $T/trace; /j2/fsyncbench /mnt/e/log.bin 2 4096 vda; echo 0 > $T/tracing_on
grep -v "^#" $T/trace | sed -e 's/^ *//'
umount /mnt/e
```

```text
lab initramfs: 6.18.55-fs up 1.36s
### fast_commit
/dev/vda contains a ext4 file system
	last mounted on /mnt/e on Sat Oct 17 21:46:27 2026
[fast_commit] run1 ops/s 873  wr_ios 806  wr_sect 6592  flush 800  per-fsync KiB 8.2  dev/app 2.06
[fast_commit] run2 ops/s 864  wr_ios 806  wr_sect 6616  flush 800  per-fsync KiB 8.3  dev/app 2.07
fc stats:
788 commits
12 ineligible
788 numblks
### commit= interval, buffered appends every 100 ms for 10 s, no fsync
/dev/vda contains a ext4 file system
	last mounted on /mnt/e on Sat Oct 17 21:46:50 2026
commit=1: 10 transactions (9 requested), each up to 1365 blocks
/dev/vda contains a ext4 file system
	last mounted on /mnt/e on Sat Oct 17 21:46:51 2026
commit=5: 2 transactions (2 requested), each up to 1365 blocks
/dev/vda contains a ext4 file system
	last mounted on /mnt/e on Sat Oct 17 21:47:03 2026
commit=30: 0 transactions (0 requested), each up to 1365 blocks
### tracepoints: 2 x (append 4K + fsync), data=ordered
/dev/vda contains a ext4 file system
	last mounted on /mnt/e on Sat Oct 17 21:47:15 2026
ops/s 389  wr_ios 6  wr_sect 96  flush 4  per-fsync KiB 24.0  dev/app 6.00
fsyncbench-411     [000] .....    38.000951: block_rq_issue: 254,0 RM 4096 () 16968 + 8 be,0,4 [fsyncbench]
fsyncbench-411     [000] .....    38.001815: block_rq_issue: 254,0 RM 4096 () 552 + 8 be,0,4 [fsyncbench]
jbd2/vda-8-406     [001] .....    38.002526: block_rq_issue: 254,0 WSM 4096 () 524288 + 8 be,0,3 [jbd2/vda-8]
jbd2/vda-8-406     [001] .....    38.002802: jbd2_start_commit: dev 254,0 transaction 2 sync 0
jbd2/vda-8-406     [001] ...1.    38.002894: jbd2_commit_flushing: dev 254,0 transaction 2 sync 0
jbd2/vda-8-406     [001] .....    38.002960: jbd2_commit_logging: dev 254,0 transaction 2 sync 0
jbd2/vda-8-406     [001] .....    38.003139: block_rq_issue: 254,0 WSM 24576 () 524296 + 48 be,0,3 [jbd2/vda-8]
kworker/1:1H-49      [001] .....    38.003415: block_rq_issue: 254,0 FF 0 () 0 + 0 none,0,0 [kworker/1:1H]
kworker/0:1H-61      [000] .....    38.004474: block_rq_issue: 254,0 WSM 4096 () 524344 + 8 be,0,3 [kworker/0:1H]
kworker/0:1H-61      [000] .....    38.004623: block_rq_issue: 254,0 FF 0 () 0 + 0 none,0,0 [kworker/0:1H]
jbd2/vda-8-406     [001] .....    38.005246: jbd2_end_commit: dev 254,0 transaction 2 sync 0 head 2
fsyncbench-411     [000] .....    38.005490: block_rq_issue: 254,0 WSM 8192 () 0 + 16 be,0,4 [fsyncbench]
fsyncbench-411     [000] .....    38.005497: block_rq_issue: 254,0 WSM 4096 () 552 + 8 be,0,4 [fsyncbench]
fsyncbench-411     [000] .....    38.005500: block_rq_issue: 254,0 WSM 4096 () 584 + 8 be,0,4 [fsyncbench]
fsyncbench-411     [000] .....    38.005502: block_rq_issue: 254,0 WSM 4096 () 16968 + 8 be,0,4 [fsyncbench]
fsyncbench-411     [000] .....    38.006587: ext4_sync_file_enter: dev 254,0 ino 13 parent 2 datasync 0
kworker/0:1H-61      [000] .....    38.006949: block_rq_issue: 254,0 RAM 16384 () 520 + 32 be,0,4 [kworker/0:1H]
fsyncbench-411     [000] .....    38.007356: block_rq_issue: 254,0 WS 4096 () 17272 + 8 be,0,4 [fsyncbench]
jbd2/vda-8-406     [001] .....    38.007648: jbd2_start_commit: dev 254,0 transaction 3 sync 0
jbd2/vda-8-406     [001] ...1.    38.007665: jbd2_commit_flushing: dev 254,0 transaction 3 sync 0
jbd2/vda-8-406     [001] .....    38.007672: jbd2_commit_logging: dev 254,0 transaction 3 sync 0
jbd2/vda-8-406     [001] .....    38.007762: block_rq_issue: 254,0 WSM 16384 () 524352 + 32 be,0,3 [jbd2/vda-8]
kworker/1:1H-49      [001] .....    38.007970: block_rq_issue: 254,0 FF 0 () 0 + 0 none,0,0 [kworker/1:1H]
kworker/0:1H-61      [000] .....    38.008646: block_rq_issue: 254,0 WSM 4096 () 524384 + 8 be,0,3 [kworker/0:1H]
kworker/0:1H-61      [000] .....    38.008764: block_rq_issue: 254,0 FF 0 () 0 + 0 none,0,0 [kworker/0:1H]
jbd2/vda-8-406     [001] .....    38.009192: jbd2_end_commit: dev 254,0 transaction 3 sync 0 head 2
fsyncbench-411     [000] .....    38.009281: ext4_sync_file_exit: dev 254,0 ino 13 ret 0
fsyncbench-411     [000] .....    38.009515: ext4_sync_file_enter: dev 254,0 ino 13 parent 2 datasync 0
fsyncbench-411     [000] .....    38.009747: block_rq_issue: 254,0 WS 4096 () 17280 + 8 be,0,4 [fsyncbench]
jbd2/vda-8-406     [001] .....    38.010035: jbd2_start_commit: dev 254,0 transaction 4 sync 0
jbd2/vda-8-406     [001] ...1.    38.010051: jbd2_commit_flushing: dev 254,0 transaction 4 sync 0
jbd2/vda-8-406     [001] .....    38.010056: jbd2_commit_logging: dev 254,0 transaction 4 sync 0
jbd2/vda-8-406     [001] .....    38.010135: block_rq_issue: 254,0 WSM 16384 () 524392 + 32 be,0,3 [jbd2/vda-8]
kworker/1:1H-49      [001] .....    38.010415: block_rq_issue: 254,0 FF 0 () 0 + 0 none,0,0 [kworker/1:1H]
kworker/0:1H-61      [000] .....    38.010858: block_rq_issue: 254,0 WSM 4096 () 524424 + 8 be,0,3 [kworker/0:1H]
kworker/0:1H-61      [000] .....    38.010953: block_rq_issue: 254,0 FF 0 () 0 + 0 none,0,0 [kworker/0:1H]
jbd2/vda-8-406     [001] .....    38.011328: jbd2_end_commit: dev 254,0 transaction 4 sync 0 head 2
fsyncbench-411     [000] .....    38.011365: ext4_sync_file_exit: dev 254,0 ino 13 ret 0
```

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="20.0" y="22.0" font-size="13" text-anchor="start" font-weight="bold">append 4 KiB + fsync × 400 (ext4, virtio-blk, TCG) — 실측</text><text x="60.0" y="44.0" font-size="12" text-anchor="start">장치에 쓴 KiB / fsync (막대) · FLUSH 수 (숫자)</text><line x1="60" y1="240" x2="330" y2="240" stroke="currentColor" stroke-width="1.3"/><line x1="60" y1="50" x2="60" y2="240" stroke="currentColor" stroke-width="1.3"/><text x="54.0" y="244.0" font-size="11" text-anchor="end">0</text><line x1="60" y1="240.0" x2="330" y2="240.0" stroke="#888" stroke-width="0.6" stroke-dasharray="4 3"/><text x="54.0" y="180.7" font-size="11" text-anchor="end">10</text><line x1="60" y1="176.66666666666666" x2="330" y2="176.66666666666666" stroke="#888" stroke-width="0.6" stroke-dasharray="4 3"/><text x="54.0" y="117.3" font-size="11" text-anchor="end">20</text><line x1="60" y1="113.33333333333333" x2="330" y2="113.33333333333333" stroke="#888" stroke-width="0.6" stroke-dasharray="4 3"/><text x="54.0" y="54.0" font-size="11" text-anchor="end">30</text><line x1="60" y1="50.0" x2="330" y2="50.0" stroke="#888" stroke-width="0.6" stroke-dasharray="4 3"/><rect x="69.0" y="88.0" width="36.0" height="152.0" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0" stroke-width="1"/><text x="87.0" y="82.0" font-size="11" text-anchor="middle">24</text><text x="87.0" y="256.0" font-size="10" text-anchor="middle">ordered</text><text x="87.0" y="272.0" font-size="11" text-anchor="middle">F=2</text><rect x="123.0" y="88.0" width="36.0" height="152.0" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0" stroke-width="1"/><text x="141.0" y="82.0" font-size="11" text-anchor="middle">24</text><text x="141.0" y="256.0" font-size="10" text-anchor="middle">writeback</text><text x="141.0" y="272.0" font-size="11" text-anchor="middle">F=2</text><rect x="177.0" y="62.7" width="36.0" height="177.3" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c" stroke-width="1"/><text x="195.0" y="56.7" font-size="11" text-anchor="middle">28</text><text x="195.0" y="256.0" font-size="10" text-anchor="middle">journal</text><text x="195.0" y="272.0" font-size="11" text-anchor="middle">F=3</text><rect x="231.0" y="75.3" width="36.0" height="164.7" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a" stroke-width="1"/><text x="249.0" y="69.3" font-size="11" text-anchor="middle">26</text><text x="249.0" y="256.0" font-size="10" text-anchor="middle">barrier=0</text><text x="249.0" y="272.0" font-size="11" text-anchor="middle">F=0</text><rect x="285.0" y="187.8" width="36.0" height="52.2" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b" stroke-width="1"/><text x="303.0" y="181.8" font-size="11" text-anchor="middle">8.25</text><text x="303.0" y="256.0" font-size="10" text-anchor="middle">fast_commit</text><text x="303.0" y="272.0" font-size="11" text-anchor="middle">F=2</text><text x="400.0" y="44.0" font-size="12" text-anchor="start">fsync/s (TCG라 절대값은 의미 없음)</text><line x1="400" y1="240" x2="670" y2="240" stroke="currentColor" stroke-width="1.3"/><line x1="400" y1="50" x2="400" y2="240" stroke="currentColor" stroke-width="1.3"/><text x="394.0" y="244.0" font-size="11" text-anchor="end">0</text><line x1="400" y1="240.0" x2="670" y2="240.0" stroke="#888" stroke-width="0.6" stroke-dasharray="4 3"/><text x="394.0" y="196.5" font-size="11" text-anchor="end">250</text><line x1="400" y1="192.5" x2="670" y2="192.5" stroke="#888" stroke-width="0.6" stroke-dasharray="4 3"/><text x="394.0" y="149.0" font-size="11" text-anchor="end">500</text><line x1="400" y1="145.0" x2="670" y2="145.0" stroke="#888" stroke-width="0.6" stroke-dasharray="4 3"/><text x="394.0" y="101.5" font-size="11" text-anchor="end">750</text><line x1="400" y1="97.5" x2="670" y2="97.5" stroke="#888" stroke-width="0.6" stroke-dasharray="4 3"/><text x="394.0" y="54.0" font-size="11" text-anchor="end">1000</text><line x1="400" y1="50.0" x2="670" y2="50.0" stroke="#888" stroke-width="0.6" stroke-dasharray="4 3"/><rect x="409.0" y="162.9" width="36.0" height="77.1" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0" stroke-width="1"/><text x="427.0" y="156.9" font-size="11" text-anchor="middle">406</text><text x="427.0" y="256.0" font-size="10" text-anchor="middle">ordered</text><rect x="463.0" y="141.8" width="36.0" height="98.2" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0" stroke-width="1"/><text x="481.0" y="135.8" font-size="11" text-anchor="middle">517</text><text x="481.0" y="256.0" font-size="10" text-anchor="middle">writeback</text><rect x="517.0" y="139.1" width="36.0" height="100.9" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c" stroke-width="1"/><text x="535.0" y="133.1" font-size="11" text-anchor="middle">531</text><text x="535.0" y="256.0" font-size="10" text-anchor="middle">journal</text><rect x="571.0" y="67.1" width="36.0" height="172.9" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a" stroke-width="1"/><text x="589.0" y="61.1" font-size="11" text-anchor="middle">910</text><text x="589.0" y="256.0" font-size="10" text-anchor="middle">barrier=0</text><rect x="625.0" y="75.1" width="36.0" height="164.9" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b" stroke-width="1"/><text x="643.0" y="69.1" font-size="11" text-anchor="middle">868</text><text x="643.0" y="256.0" font-size="10" text-anchor="middle">fast_commit</text><text x="20.0" y="300.0" font-size="12" text-anchor="start">fast_commit은 fsync당 8 KiB(데이터 4K + fc 블록 4K) — 전체 트랜잭션 대신 inode 단위 로그.</text><text x="20.0" y="320.0" font-size="12" text-anchor="start">barrier=0은 FLUSH 0회라 빠르지만 전원 차단 시 commit 블록이 메타보다 먼저 남을 수 있다.</text></svg>
```
그림 5 — 예제 8의 결과. 왼쪽이 결정적인 값(fsync당 장치 쓰기 KiB와 FLUSH 수), 오른쪽은 TCG 시간이라 경향만 본다. barrier=0은 두 번의 실행(28, 24 KiB)의 평균.

출력에서 볼 것:

- **ordered = writeback = 24 KiB, FLUSH 2회 / fsync**. fsync 경로(`ext4_sync_file`)는 모드와 상관없이 먼저 `file_write_and_wait_range()`로 그 파일의 데이터를 쓰기 때문에, **fsync를 매번 부르는 앱에서는 ordered와 writeback의 I/O가 같다**. 두 모드의 차이는 fsync를 안 부른 데이터가 **주기적 commit** 시점에 어떻게 되느냐(stale data 노출)에서 나온다. 24 KiB = 데이터 4 KiB + 저널(디스크립터 + 메타데이터 블록 3개 + commit 블록) 20 KiB. jbd2 info의 "5 logged blocks per transaction"과 맞는다. 4 KiB를 쓰려고 24 KiB를 썼으니 **파일시스템 층에서 이미 WAF 6**이다 — 이게 장치 FTL의 WAF와 곱해진다.
- **data=journal = 28 KiB, FLUSH 3회**: 데이터 블록도 저널에 들어가 4 KiB가 늘었다. 데이터의 원래 자리 쓰기는 나중에 checkpoint 때 일어나므로 이 측정 구간 밖일 수 있다(그래서 실제 총량은 더 크다).
- **512 B append**에서 앱 대비 장치 비율이 34~42배 — 작은 쓰기 + fsync가 flash 수명에 가장 나쁜 패턴이라는 숫자다.
- **barrier=0 → FLUSH 0회**, fsync가 두 배 빨라졌다. 하지만 장치 캐시가 쓰기 순서를 바꿀 수 있어서 commit 블록이 로그 블록보다 먼저 영구화될 수 있다 → 크래시 후 저널이 "commit됐다"고 믿고 깨진 블록을 replay. PLP 커패시터가 있는 엔터프라이즈 SSD(Don이 만든 것)에서만 정당화된다. eMMC/SD에서는 쓰지 않는다.
- **fast_commit = 8 KiB / fsync**: 데이터 4 KiB + fast commit 블록 4 KiB. 트랜잭션 전체 대신 "이 inode가 이렇게 바뀌었다"는 **논리 로그**를 저널 끝의 fast commit 영역에 쓴다(`mkfs.ext4 -O fast_commit` 또는 `tune2fs -O fast_commit`). 400번 중 12번은 `ineligible` — rename 같은 fast commit으로 표현 못 하는 연산이 섞이면 일반 commit으로 돌아간다. 장치 쓰기가 1/3로 줄었다.
- **journal_async_commit**은 이 커널에서 `data=ordered`와 함께 쓸 수 없다고 거부했다.
- **commit= 간격**: fsync 없이 100 ms마다 한 줄씩 10초 동안 쓰면 `commit=1`은 트랜잭션 10개, `commit=5`는 2개, `commit=30`은 **0개** — 10초 동안 이 데이터는 메모리에만 있었다. `commit=`을 키우면 쓰기 횟수(=flash 마모와 전력)가 줄고 **전원 차단 시 잃는 시간 창**이 커진다. fsync는 이 간격과 상관없이 즉시 commit한다.

### 4.4 예제 9 — tracepoint로 fsync 한 번을 해부

무엇을 확인하나: 그림 4의 근거. 위 `g4b.sh`의 마지막 부분이 `jbd2:*`, `ext4:ext4_sync_file_*`, `block:block_rq_issue`를 켜고 fsync 2번을 돌린 결과다(출력은 위 블록의 `### tracepoints` 아래). 읽는 법 — `block_rq_issue`의 rwbs 필드: `W`=write, `S`=sync, `M`=metadata, `R`=read, `A`=readahead, **`FF`=flush 요청**. 섹터 번호 524288 = 4 KiB 블록 65536 = 저널 시작(예제 6의 저널 extent `65536-69631`).

fsync 한 번(transaction 3)의 순서:

1. `ext4_sync_file_enter` → 데이터 블록 `WS 4096 () 17272` (제자리 쓰기).
2. `jbd2_start_commit` → `jbd2_commit_flushing` → `jbd2_commit_logging`.
3. 저널에 `WSM 16384 () 524352` = 디스크립터 + 메타데이터 3블록.
4. `FF` — 앞의 쓰기들을 장치 캐시에서 영구 매체로(REQ_PREFLUSH).
5. commit 블록 `WSM 4096 () 524384`.
6. `FF` — commit 블록의 REQ_FUA를 장치가 지원하지 않으므로(`fua=0`, 예제 4) block 계층의 flush state machine이 **post-flush로 흉내 낸다**.
7. `jbd2_end_commit` → `ext4_sync_file_exit`.

FLUSH가 `kworker/1:1H` 같은 다른 스레드에서 나가는 것도 보인다 — flush 시퀀싱은 blk-mq의 flush 머신(`block/blk-flush.c`)이 처리한다(I11의 blk-mq 경로). 실제 eMMC는 CMD6로 캐시 flush, 일부는 reliable write(CMD23 bit31)로 FUA를 구현한다. Don의 NVMe라면 Flush 명령과 FUA 비트다.

---

## 5. 마운트 옵션과 운영 도구

### 5.1 자주 쓰는 마운트 옵션

| 옵션 | 의미 | 임베디드에서 |
|---|---|---|
| `relatime` (기본) | 읽을 때 atime을 갱신하되, atime이 mtime/ctime보다 오래됐거나 24시간 지났을 때만 | 기본으로 충분 |
| `noatime` | atime 갱신 안 함 → 읽기가 쓰기를 만들지 않음 | flash 수명 위해 흔히 켬 (atime 쓰는 앱이 드묾) |
| `strictatime` | 읽을 때마다 갱신 | 거의 안 씀 |
| `lazytime` | 시간 필드 갱신을 메모리에 두었다가 다른 이유로 inode를 쓸 때·24시간마다·sync 때 반영 | F2FS는 기본으로 켜져 있었다 |
| `commit=N` | 주기적 commit 간격(초) | 쓰기 횟수 ↔ 유실 창의 트레이드오프 |
| `barrier=0` / `nobarrier` | FLUSH/FUA 끔 | PLP 없는 장치에선 금지 |
| `discard` | 블록 해제 시 바로 TRIM | 쓰기 경로가 느려질 수 있음 → 보통 주기적 `fstrim` |
| `errors=continue/remount-ro/panic` | 메타데이터 에러 시 동작 | rootfs는 `remount-ro` 또는 `panic`(워치독과 함께) |
| `data=` | 저널 모드 | §4.2 |
| `nodelalloc` | delalloc 끔 → write 시점에 블록 할당 | 늦은 ENOSPC가 싫을 때. 조각화·성능 손해 |
| `ro` | 읽기 전용 | 읽기전용 rootfs(J3) |

### 5.2 예제 10 — discard vs fstrim, atime 세 가지, 예약 블록·tune2fs

무엇을 확인하나: 삭제한 블록이 언제 장치에 TRIM으로 알려지는지(`/sys/block/vda/stat`의 12번째 discard I/O 수, 14번째 discard 섹터), atime 옵션의 차이, 예약 블록 변경.

```sh
export PATH=/opt/bin:$PATH
mkdir -p /mnt/e; D() { awk '{print "discard_ios="$12" discard_sectors="$14}' /sys/block/vda/stat; }
mkfs.ext4 -q -F -E nodiscard /dev/vda; mount -t ext4 /dev/vda /mnt/e
echo "### default mount: rm does NOT discard"; D
dd if=/dev/zero of=/mnt/e/big bs=1M count=100 2>/dev/null; sync; rm /mnt/e/big; sync; sleep 6; D
echo "### fstrim (batched discard of all free space)"; fstrim -v /mnt/e; D
echo "### fstrim again: already-trimmed groups are skipped"; fstrim -v /mnt/e; D
umount /mnt/e; mount -t ext4 -o discard /dev/vda /mnt/e
echo "### -o discard: rm discards after the journal commit"
dd if=/dev/zero of=/mnt/e/big bs=1M count=100 2>/dev/null; sync; D; rm /mnt/e/big; sync; sleep 6; D
umount /mnt/e
echo "### atime: relatime vs noatime vs strictatime"
for o in relatime noatime strictatime; do mount -t ext4 -o $o /dev/vda /mnt/e; echo x > /mnt/e/f; sync; sleep 1
  a0=$(stat -c %X /mnt/e/f); cat /mnt/e/f >/dev/null; a1=$(stat -c %X /mnt/e/f); sleep 1; cat /mnt/e/f >/dev/null; a2=$(stat -c %X /mnt/e/f)
  echo "$o: atime after 1st read +$((a1-a0))s, after 2nd read +$((a2-a0))s"; umount /mnt/e; done
echo "### reserved blocks"; dumpe2fs -h /dev/vda 2>/dev/null | grep -E "^(Block count|Reserved block count)"; tune2fs -m 1 /dev/vda | tail -1; dumpe2fs -h /dev/vda 2>/dev/null | grep "^Reserved block count"
tune2fs -l /dev/vda | grep -E "Maximum mount|Check interval|Errors behavior"; tune2fs -e remount-ro /dev/vda >/dev/null; tune2fs -l /dev/vda | grep "Errors behavior"
```

```text
lab initramfs: 6.18.55-fs up 1.37s
/dev/vda contains a ext4 file system
	last mounted on /mnt/e on Sat Oct 17 21:52:20 2026
### default mount: rm does NOT discard
discard_ios=0 discard_sectors=0
discard_ios=0 discard_sectors=0
### fstrim (batched discard of all free space)
/mnt/e: 510717952 bytes trimmed
discard_ios=4 discard_sectors=997496
### fstrim again: already-trimmed groups are skipped
/mnt/e: 0 bytes trimmed
discard_ios=4 discard_sectors=997496
### -o discard: rm discards after the journal commit
discard_ios=5 discard_sectors=1005168
discard_ios=7 discard_sectors=1503992
### atime: relatime vs noatime vs strictatime
relatime: atime after 1st read +1s, after 2nd read +1s
noatime: atime after 1st read +0s, after 2nd read +0s
strictatime: atime after 1st read +5s, after 2nd read +6s
### reserved blocks
Block count:              131072
Reserved block count:     6553
Setting reserved blocks percentage to 1% (1310 blocks)
Reserved block count:     1310
Errors behavior:          Continue
Maximum mount count:      -1
Check interval:           0 (<none>)
Errors behavior:          Remount read-only
```

출력에서 볼 것:

- 기본 마운트에서 100 MiB 파일을 지워도 **discard 0건** — 장치(FTL)는 그 LBA가 비었다는 걸 모르고, GC 때 쓸모없는 데이터를 계속 옮긴다. Don이 SSD에서 "호스트가 TRIM을 안 보내면 OP가 줄어든 것과 같다"고 했던 바로 그 상황이다.
- `fstrim`은 파일시스템의 free 공간 전체를 큰 덩어리로 discard했다(4건, ≈ 487 MiB). 두 번째 `fstrim`은 0바이트 — ext4가 그룹별로 "이미 trim함" 플래그(`EXT4_GROUP_INFO_WAS_TRIMMED_BIT`, 메모리 안의 그룹 정보)를 기억하고, 그 뒤 블록이 해제된 그룹만 다시 trim한다. 배포판은 `fstrim.timer`로 주 1회 돈다. 임베디드에서는 cron이나 유휴 시간에 직접 돌린다.
- `-o discard`에서는 `rm` 직후 discard가 나갔다(jbd2 commit 이후에 보낸다 — 커밋 전에 TRIM하면 크래시 시 아직 유효한 블록이 사라질 수 있으므로). 이번 실행에서 rm 직후의 discard 섹터 수(약 49만 섹터)가 파일 크기(20만 섹터)보다 컸는데, 원인은 이 실험에서 확인하지 않았다. 또 `dd`+`sync` 직후(rm 전)에도 discard 1건이 늘었다 — 역시 미확인.
- atime: `relatime`은 첫 읽기에서만 갱신(+1 s 그대로), `noatime`은 갱신 없음, `strictatime`은 읽을 때마다 현재 시각으로 갱신.
- `tune2fs -m 1`로 예약 블록 5% → 1%, `tune2fs -e remount-ro`로 superblock의 기본 에러 동작 변경. `Maximum mount count: -1`, `Check interval: 0` = 주기적 강제 fsck가 꺼진 최신 기본값.

### 5.3 discard vs 주기적 fstrim — 왜 기본이 꺼져 있나

`discard` 마운트 옵션은 블록이 풀릴 때마다 작은 discard를 보낸다. 장치에 따라 discard가 **동기적이고 느린 명령**이라(특히 일부 eMMC·SATA는 discard 중 다른 I/O가 멈춤) 삭제가 많은 워크로드에서 지연이 튄다. 그래서 일반적으로는 끄고 `fstrim`을 주기적으로 돌린다. 반대로 F2FS는 기본으로 `discard`가 켜져 있는데(예제 13의 마운트 옵션), F2FS는 discard를 모아서 별도 스레드에서 유휴 시간에 보내도록 설계되어 있다(`discard_granularity`, `max_discard_request` sysfs).

### 5.4 errors= 와 예제 11 — I/O 에러를 주입해 remount-ro 보기

무엇을 확인하나: 장치 쓰기가 실패할 때 ext4/jbd2가 어떻게 반응하는지. 커널의 fault injection(`CONFIG_FAIL_MAKE_REQUEST`)을 쓰면 특정 디스크의 bio를 확률적으로 실패시킬 수 있다: `/sys/block/vda/make-it-fail` = 1 + `/sys/kernel/debug/fail_make_request/{probability,times}`.

```sh
export PATH=/opt/bin:$PATH
mkfs.ext4 -q -F /dev/vda; mkdir -p /mnt/e; mount -t ext4 -o errors=remount-ro /dev/vda /mnt/e
echo ok > /mnt/e/a.txt; sync; grep vda /proc/mounts
F=/sys/kernel/debug/fail_make_request
echo 100 > $F/probability; echo -1 > $F/times; echo 0 > $F/verbose; echo 1 > /sys/block/vda/make-it-fail
echo "### inject: every bio to vda fails"
echo new > /mnt/e/b.txt; /j2/fsyncbench /mnt/e/c.bin 1 4096 vda; echo "fsyncbench rc=$?"
echo 0 > /sys/block/vda/make-it-fail
dmesg | grep -E "EXT4|JBD2|I/O error|Buffer I/O" | head -12
grep vda /proc/mounts
touch /mnt/e/d.txt; echo "touch rc=$?"
umount /mnt/e; dumpe2fs -h /dev/vda 2>/dev/null | grep -iE "state|error"
echo "### e2fsck -p (what an init script does at boot)"; e2fsck -p /dev/vda; echo "e2fsck rc=$?"
dumpe2fs -h /dev/vda 2>/dev/null | grep -iE "state|error count"
```

```text
lab initramfs: 6.18.55-fs up 1.37s
/dev/vda contains a f2fs file system
/dev/vda /mnt/e ext4 rw,relatime,errors=remount-ro 0 0
### inject: every bio to vda fails
[    1.656388] EXT4-fs (vda): failed to convert unwritten extents to written extents -- potential data loss!  (inode 14, error -5)
[    1.656533] Buffer I/O error on device vda, logical block 2160
[    1.657075] Aborting journal on device vda-8.
[    1.657159] Buffer I/O error on dev vda, logical block 65536, lost sync page write
[    1.657283] JBD2: I/O error when updating journal superblock for vda-8.
write: Read-only file system
[    1.661062] EXT4-fs error (device vda): ext4_journal_check_start:87: comm fsyncbench: Detected aborted journal
fsyncbench rc=1
[    1.661460] Buffer I/O error on dev vda, logical block 0, lost sync page write
[    1.661485] EXT4-fs (vda): I/O error while writing superblock
[    1.661527] EXT4-fs (vda): Remounting filesystem read-only
[    1.603802] EXT4-fs (vda): mounted filesystem b8de354f-56f0-428b-9275-67e8ddfc0ebd r/w with ordered data mode. Quota mode: none.
[    1.656143] EXT4-fs warning (device vda): ext4_end_bio:368: I/O error 10 writing to inode 14 starting block 2160)
[    1.656388] EXT4-fs (vda): failed to convert unwritten extents to written extents -- potential data loss!  (inode 14, error -5)
[    1.656533] Buffer I/O error on device vda, logical block 2160
[    1.657159] Buffer I/O error on dev vda, logical block 65536, lost sync page write
[    1.657283] JBD2: I/O error when updating journal superblock for vda-8.
[    1.661062] EXT4-fs error (device vda): ext4_journal_check_start:87: comm fsyncbench: Detected aborted journal
[    1.661460] Buffer I/O error on dev vda, logical block 0, lost sync page write
[    1.661485] EXT4-fs (vda): I/O error while writing superblock
[    1.661527] EXT4-fs (vda): Remounting filesystem read-only
/dev/vda /mnt/e ext4 rw,relatime,errors=remount-ro,emergency_ro 0 0
touch: /mnt/e/d.txt: Read-only file system
touch rc=1
Filesystem state:         clean
Errors behavior:          Continue
### e2fsck -p (what an init script does at boot)
/dev/vda: recovering journal
/dev/vda: clean, 13/32768 files, 6386/131072 blocks
e2fsck rc=0
Filesystem state:         clean
```

(앞쪽의 대괄호 시간 줄들은 커널이 콘솔에 바로 찍은 것이고, 뒤쪽은 `dmesg | grep`이 같은 메시지를 다시 보여 준 것이다.)

출력에서 볼 것:

- 데이터 블록 쓰기 실패(`I/O error 10 writing to inode 14`) → 저널 superblock 쓰기 실패 → **`Aborting journal on device vda-8`** → 다음 handle 시작에서 `Detected aborted journal` → **`Remounting filesystem read-only`**. 앱은 `write: Read-only file system`(EROFS)를 받는다.
- **함정**: `/proc/mounts`에는 여전히 **`rw`**로 보이고 끝에 `emergency_ro`가 붙었다. 6.18 소스(`fs/ext4/super.c`)를 보면 ext4는 이제 에러 시 `SB_RDONLY`를 직접 세우지 않고 `EXT4_FLAGS_EMERGENCY_RO` 비트만 세운다(주석: `s_umount` 없이 SB_RDONLY를 세우면 freeze 등과 데드락). 그러니 헬스 체크 스크립트가 `grep " ro,"`로 "rootfs가 ro로 떨어졌나"를 검사하면 **놓친다** — `emergency_ro`도 찾거나 실제로 쓰기를 시험해야 한다. (오래된 커널에서는 `ro`로 보였다 — 커널 버전에 따라 다르다.)
- superblock 쓰기도 실패했으므로 디스크에는 에러 기록이 남지 않았다(`Filesystem state: clean`). 다음 부팅의 `e2fsck -p`는 저널 replay(`recovering journal`)만 하고 clean으로 끝났다. 쓰기가 일부라도 성공했다면 `Filesystem state: clean with errors`와 `FS Error count`가 남아 `e2fsck -p`가 전체 검사를 했을 것이다.
- `errors=panic`이면 커널 패닉 → 워치독 리셋 → 재부팅 후 fsck — 무인 장비에서는 "ro로 반쯤 살아 있는 좀비"보다 이게 낫다는 설계도 많다.

### 5.5 부팅 시 e2fsck, tune2fs

- 일반 배포판: `/etc/fstab`의 6번째 필드(pass)가 1(루트)·2(그 외)인 파일시스템을 `systemd-fsck`가 마운트 전에 `e2fsck -p`(preen: 안전한 것만 자동 수리)로 검사한다. 결과 코드 0 = 깨끗, 1 = 고쳤음, 4 = 수동 개입 필요.
- BusyBox 기반 임베디드: init 스크립트가 직접 `fsck -a`/`e2fsck -p`를 부른다. rootfs는 ro로 붙어 있으니(예제 5) 검사 후 `mount -o remount,rw /`.
- 저널 replay는 마운트가 자동으로 하므로 "크래시마다 fsck"는 필요 없다. 다만 superblock에 에러가 기록된 경우(`clean with errors`)에는 전체 검사가 필요하다.
- `tune2fs`: `-l`(정보), `-c`/`-i`(주기적 검사 횟수·간격), `-e`(에러 동작), `-m`/`-r`(예약 블록), `-O`(기능 켜기/끄기, 일부는 언마운트 상태에서만), `-L`/`-U`(라벨·UUID).

### 5.6 예제 12 — 첫 부팅에 rootfs를 장치 끝까지 늘리기 (online resize)

장면: 공장 이미지는 작게(예: 256 MiB) 만들어 빨리 굽는다. 실제 eMMC는 8 GB다. 첫 부팅에 rootfs 파티션과 파일시스템을 남은 공간 끝까지 늘린다 — Raspberry Pi OS, cloud-init의 `growpart`, systemd-repart가 하는 일이다.

순서: (1) GPT 백업 헤더를 진짜 디스크 끝으로 옮김(`sgdisk -e`) → (2) 파티션 엔트리를 **같은 시작 섹터, 같은 PARTUUID**로 끝까지 다시 만듦(PARTUUID가 바뀌면 `root=PARTUUID=`가 깨진다!) → (3) 마운트된 상태라 `BLKRRPART`(전체 재스캔)는 EBUSY → 대신 `BLKPG_RESIZE_PARTITION`으로 그 파티션 크기만 커널에 알림 → (4) 마운트된 ext4를 `EXT4_IOC_RESIZE_FS`로 키움(`resize2fs`가 부르는 것과 같은 ioctl).

호스트에서 만든 "공장 이미지":

```sh
#!/bin/bash
# "factory image": 256 MiB GPT, one 128 MiB ext4 rootfs partition, then flashed onto a 512 MiB "eMMC"
set -e; cd /var/tmp/J2; rm -f j2_grow.img j2_gp1.img
truncate -s 256M j2_grow.img; sgdisk -o -n 1:2048:+128M -t 1:8300 -c 1:rootfs j2_grow.img >/dev/null
truncate -s 128M j2_gp1.img; mkfs.ext4 -q -L rootfs -d /var/lab/build/rootfs j2_gp1.img
dd if=j2_gp1.img of=j2_grow.img bs=1M seek=1 conv=notrunc status=none; rm j2_gp1.img
truncate -s 512M j2_grow.img       # the card is bigger than the image
sgdisk -v j2_grow.img | head -6
```

```text
Problem: The secondary header's self-pointer indicates that it doesn't reside
at the end of the disk. If you've added a disk to a RAID array, use the 'e'
option on the experts' menu to adjust the secondary header's and partition
table's locations.

```

(`sgdisk -v`가 바로 그 문제 — 백업 헤더가 디스크 끝에 없음 — 를 지적한다.)

파티션 크기를 커널에 알리는 C 코드와, 마운트된 ext4를 키우는 C 코드:

```c
/* blkpg_grow.c: tell the kernel a (mounted) partition got bigger -- what growpart/partx do.
 * BLKRRPART fails with EBUSY while a partition is mounted; BLKPG_RESIZE_PARTITION does not. */
#include <fcntl.h>
#include <linux/blkpg.h>
#include <linux/fs.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	if (argc != 5) { fprintf(stderr, "usage: %s /dev/disk partno start_sect nr_sect\n", argv[0]); exit(1); }
	int fd = open(argv[1], O_RDONLY);
	if (fd < 0) { perror("open"); exit(1); }
	if (ioctl(fd, BLKRRPART) < 0) perror("BLKRRPART (expected while mounted)");
	struct blkpg_partition p = {
		.pno = atoi(argv[2]),
		.start = atoll(argv[3]) * 512,
		.length = atoll(argv[4]) * 512,
	};
	struct blkpg_ioctl_arg a = { .op = BLKPG_RESIZE_PARTITION, .datalen = sizeof(p), .data = &p };
	if (ioctl(fd, BLKPG, &a) < 0) { perror("BLKPG_RESIZE_PARTITION"); exit(1); }
	printf("partition %d resized to %lld sectors\n", p.pno, p.length / 512);
	close(fd);
	return 0;
}
```

```c
/* ioctl_resize.c: online grow of a mounted ext4 = EXT4_IOC_RESIZE_FS (what resize2fs calls) */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/statvfs.h>
#include <unistd.h>
#define EXT4_IOC_RESIZE_FS _IOW('f', 16, uint64_t)

int main(int argc, char **argv)
{
	if (argc != 3) { fprintf(stderr, "usage: %s mountpoint new_blocks\n", argv[0]); exit(1); }
	int fd = open(argv[1], O_RDONLY | O_DIRECTORY);
	if (fd < 0) { perror("open"); exit(1); }
	struct statvfs v;
	if (fstatvfs(fd, &v)) { perror("statvfs"); exit(1); }
	printf("before: %lu blocks of %lu\n", (unsigned long)v.f_blocks, (unsigned long)v.f_frsize);
	uint64_t n = strtoull(argv[2], NULL, 0);
	if (ioctl(fd, EXT4_IOC_RESIZE_FS, &n) < 0) { perror("EXT4_IOC_RESIZE_FS"); exit(1); }
	if (fstatvfs(fd, &v)) { perror("statvfs"); exit(1); }
	printf("after:  %lu blocks\n", (unsigned long)v.f_blocks);
	close(fd);
	return 0;
}
```

게스트 스크립트와 결과:

```sh
export PATH=/opt/bin:$PATH
mkdir -p /mnt/r && mount -t ext4 /dev/vda1 /mnt/r; df -k /mnt/r | tail -1
P=$(sgdisk -i 1 /dev/vda | awk '/unique GUID/{print $4}'); echo "PARTUUID=$P"
echo "### 1) move backup GPT to the real end, recreate p1 to end with the SAME start and PARTUUID"
sgdisk -e /dev/vda >/dev/null 2>&1
sgdisk -d 1 -n 1:2048:0 -t 1:8300 -c 1:rootfs -u 1:$P /dev/vda 2>&1 | tail -1
E=$(sgdisk -i 1 /dev/vda | awk '/Last sector/{print $3}'); echo "new end $E"; cat /sys/block/vda/vda1/size
echo "### 2) tell the kernel (partition is mounted)"
/j2/blkpg_grow /dev/vda 1 2048 $((E - 2048 + 1)); cat /sys/block/vda/vda1/size
echo "### 3) grow the mounted ext4"
/j2/ioctl_resize /mnt/r 98304
resize2fs /dev/vda1 2>&1
df -k /mnt/r | tail -1; dmesg | grep -E "resiz"; blkid /dev/vda1
umount /mnt/r; e2fsck -fn /dev/vda1 2>&1 | tail -2
```

```text
lab initramfs: 6.18.55-fs up 1.32s
/dev/vda1               106420      2388     94860   2% /mnt/r
PARTUUID=FFB4E78B-C471-4C8D-9CE0-9F59D06B6EFC
### 1) move backup GPT to the real end, recreate p1 to end with the SAME start and PARTUUID
The operation has completed successfully.
new end 1048542
262144
### 2) tell the kernel (partition is mounted)
BLKRRPART (expected while mounted): Device or resource busy
partition 1 resized to 1046495 sectors
1046495
### 3) grow the mounted ext4
before: 26605 blocks of 4096
after:  88024 blocks
resize2fs 1.47.2 (1-Jan-2025)
Filesystem at /dev/vda1 is mounted on /mnt/r; on-line resizing required
old_desc_blocks = 1, new_desc_blocks = 1
The filesystem on /dev/vda1 is now 130811 (4k) blocks long.

/dev/vda1               473856      2388    446612   1% /mnt/r
[    3.748955] EXT4-fs (vda1): resizing filesystem from 32768 to 98304 blocks
[    3.757102] EXT4-fs (vda1): resized filesystem to 98304
[    3.792806] EXT4-fs (vda1): resizing filesystem from 98304 to 130811 blocks
[    3.793769] EXT4-fs (vda1): resized filesystem to 130811
/dev/vda1: LABEL="rootfs" UUID="66fce2ed-60d7-4a61-b6cc-ca6e6a4fea93" TYPE="ext4"
Pass 5: Checking group summary information
rootfs: 430/131072 files (0.2% non-contiguous), 12944/130811 blocks
```

출력에서 볼 것: `BLKRRPART`는 예상대로 EBUSY, `BLKPG_RESIZE_PARTITION`은 마운트된 채로 성공해 `vda1/size`가 262144 → 1046495섹터가 되었다. 그다음 ioctl로 32768 → 98304블록, `resize2fs`로 끝(130811블록)까지 — dmesg에 커널 쪽 `resizing filesystem from … to …`가 두 번 찍혔다. `statvfs`의 `f_blocks`(26605)가 32768보다 작은 것은 statvfs가 메타데이터 오버헤드(inode table, 저널 등)를 뺀 값을 주기 때문이다. 마지막 `e2fsck -fn`은 문제 없음. 온라인 grow가 가능한 이유가 §3.1의 **예약 GDT 블록**이다. 줄이기(shrink)는 ext4에서 **오프라인만** 된다.

---

## 6. F2FS — flash를 위해 태어난 log-structured 파일시스템

### 6.1 직관 — 파일시스템 안에 FTL을 하나 더

ext4는 덮어쓰기를 **제자리에서**(in-place) 한다. 블록 2159를 고치면 LBA 2159에 다시 쓴다. HDD에는 자연스럽지만, flash는 제자리 덮어쓰기가 불가능하므로 FTL이 뒤에서 out-of-place로 바꾸고 GC를 한다. 랜덤한 작은 덮어쓰기는 FTL의 GC 부담(장치 내부 WAF)을 키운다.

F2FS(Flash-Friendly File System, Samsung, Linux 3.8에 합류)는 반대로 간다. **파일시스템 자신이 항상 append**(log-structured)하고, 그래서 장치에는 큰 순차 쓰기만 보낸다. 장치 FTL 입장에서는 "세그먼트 크기(2 MiB) 단위로 순차로 채우고, 통째로 무효화되는" 이상적인 워크로드가 된다. 대신 **파일시스템이 GC를 해야 한다**. Don에게는 "FTL을 호스트로 끌어올린 것"이라고 말하면 한 번에 이해된다.

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="20.0" y="22.0" font-size="13" text-anchor="start" font-weight="bold">F2FS on-disk 배치 (512 MiB 실측: SB 1, CP 2, SIT 2, NAT 2, SSA 1, MAIN 248 세그먼트)</text><rect x="20" y="34" width="50" height="40" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.3"/><text x="45.0" y="58.2" font-size="12" text-anchor="middle">SB×2</text><rect x="70" y="34" width="60" height="40" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.3"/><text x="100.0" y="58.2" font-size="12" text-anchor="middle">CP</text><rect x="130" y="34" width="60" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="160.0" y="58.2" font-size="12" text-anchor="middle">SIT</text><rect x="190" y="34" width="60" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="220.0" y="58.2" font-size="12" text-anchor="middle">NAT</text><rect x="250" y="34" width="60" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="280.0" y="58.2" font-size="12" text-anchor="middle">SSA</text><rect x="310" y="34" width="350" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="485.0" y="58.2" font-size="12" text-anchor="middle">MAIN (2 MiB 세그먼트 × 248)</text><text x="20.0" y="96.0" font-size="12" text-anchor="start">MAIN의 열린 로그 6개: hot/warm/cold × node/data — 각자 다른 세그먼트에 append</text><rect x="20" y="106" width="100" height="30" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="70.0" y="125.2" font-size="12" text-anchor="middle">HOT node</text><rect x="128" y="106" width="100" height="30" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="178.0" y="125.2" font-size="12" text-anchor="middle">WARM node</text><rect x="236" y="106" width="100" height="30" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="286.0" y="125.2" font-size="12" text-anchor="middle">COLD node</text><rect x="344" y="106" width="100" height="30" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="394.0" y="125.2" font-size="12" text-anchor="middle">HOT data</text><rect x="452" y="106" width="100" height="30" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="502.0" y="125.2" font-size="12" text-anchor="middle">WARM data</text><rect x="560" y="106" width="100" height="30" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="610.0" y="125.2" font-size="12" text-anchor="middle">COLD data</text><text x="20.0" y="168.0" font-size="13" text-anchor="start" font-weight="bold">Don의 SSD FTL에 대응시키면</text><rect x="20" y="180" width="250" height="21" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="145.0" y="194.7" font-size="12" text-anchor="middle">CP (2벌, 번갈아)</text><rect x="400" y="180" width="260" height="21" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="530.0" y="194.7" font-size="12" text-anchor="middle">전원 차단 대비 메타 스냅샷 (2벌 핑퐁)</text><line x1="272" y1="190.5" x2="398" y2="190.5" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/><rect x="20" y="205" width="250" height="21" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="145.0" y="219.7" font-size="12" text-anchor="middle">NAT: node id → 블록</text><rect x="400" y="205" width="260" height="21" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="530.0" y="219.7" font-size="12" text-anchor="middle">L2P 맵 (단, node에 대해서만)</text><line x1="272" y1="215.5" x2="398" y2="215.5" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/><rect x="20" y="230" width="250" height="21" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="145.0" y="244.7" font-size="12" text-anchor="middle">SIT: 세그먼트별 valid 수</text><rect x="400" y="230" width="260" height="21" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="530.0" y="244.7" font-size="12" text-anchor="middle">블록별 VPC (valid page count)</text><line x1="272" y1="240.5" x2="398" y2="240.5" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/><rect x="20" y="255" width="250" height="21" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="145.0" y="269.7" font-size="12" text-anchor="middle">SSA: 블록의 주인</text><rect x="400" y="255" width="260" height="21" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="530.0" y="269.7" font-size="12" text-anchor="middle">P2L / spare 영역의 LBA 기록</text><line x1="272" y1="265.5" x2="398" y2="265.5" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/><rect x="20" y="280" width="250" height="21" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="145.0" y="294.7" font-size="12" text-anchor="middle">세그먼트 · 섹션</text><rect x="400" y="280" width="260" height="21" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="530.0" y="294.7" font-size="12" text-anchor="middle">erase block · GC 단위 superblock</text><line x1="272" y1="290.5" x2="398" y2="290.5" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/><rect x="20" y="305" width="250" height="21" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="145.0" y="319.7" font-size="12" text-anchor="middle">6개 로그 hot/cold</text><rect x="400" y="305" width="260" height="21" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="530.0" y="319.7" font-size="12" text-anchor="middle">스트림 분리 (NVMe Streams/FDP)</text><line x1="272" y1="315.5" x2="398" y2="315.5" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/><rect x="20" y="330" width="250" height="21" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="145.0" y="344.7" font-size="12" text-anchor="middle">GC: greedy · cost-benefit</text><rect x="400" y="330" width="260" height="21" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="530.0" y="344.7" font-size="12" text-anchor="middle">FTL GC 희생 블록 선택과 동일</text><line x1="272" y1="340.5" x2="398" y2="340.5" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/></svg>
```
그림 6 — F2FS의 on-disk 영역과 Don의 SSD FTL 개념의 대응. 수치는 예제 13의 512 MiB 볼륨 실측.

### 6.2 on-disk 구조

| 영역 | 내용 | FTL 대응 |
|---|---|---|
| Superblock (2벌) | 볼륨 기하(세그먼트 크기, 영역 위치) | 펌웨어의 drive geometry |
| **CP** (Checkpoint, 2벌) | 일관된 시점의 상태: 각 로그의 현재 세그먼트, NAT/SIT 비트맵, orphan 목록. 2벌을 번갈아 써서 하나는 항상 온전 | 전원 차단 대비 메타 스냅샷 (핑퐁) |
| **SIT** (Segment Info Table) | 세그먼트마다 valid 블록 수 + valid 비트맵 | VPC 테이블 + valid bitmap |
| **NAT** (Node Address Table) | node id → 블록 주소 | L2P 맵 (단 node 블록에 대해서만) |
| **SSA** (Segment Summary Area) | 각 블록의 주인(어느 node의 몇 번째 데이터인가) | P2L / NAND spare의 LBA 기록 (GC가 역참조할 때) |
| **Main** | 2 MiB **segment**(4 KiB × 512) → **section**(GC 단위, segment N개) → **zone** | erase block → GC 단위 superblock → die/plane 그룹 |

**NAT가 왜 필요한가 — wandering tree 문제**: 순수 LFS에서는 데이터 블록을 새 위치에 쓰면 그걸 가리키는 inode/간접 블록도 새 위치로 옮겨야 하고, 그걸 가리키는 상위 블록도… 루트까지 연쇄 갱신이 일어난다. F2FS는 블록 포인터 대신 **node id**로 가리키고 실제 위치는 NAT에 둔다 → 데이터가 움직이면 직접 부모 node 하나와 NAT 엔트리 하나만 바뀐다. FTL이 L2P 간접층으로 "논리 주소는 그대로, 물리 위치만 바뀜"을 만드는 것과 같은 트릭이다.

**multi-head logging**: 열린 로그가 6개다(`active_logs=6`). hot/warm/cold × node/data:

| 로그 | 들어가는 것 |
|---|---|
| HOT node | 디렉터리의 direct node (자주 바뀜) |
| WARM node | 일반 파일의 direct node |
| COLD node | indirect node |
| HOT data | 디렉터리 엔트리 블록 |
| WARM data | 일반 파일 데이터 |
| COLD data | GC로 옮겨진 데이터, 멀티미디어 확장자(`extension_list`), 사용자가 cold로 지정한 파일 |

수명이 비슷한 데이터를 같은 세그먼트에 모으면 세그먼트가 **한꺼번에** 무효화될 확률이 높아져 GC가 옮길 valid 블록이 줄어든다. Don이 SSD에서 NVMe Streams/FDP(Flexible Data Placement)나 펌웨어 내부 hot/cold 분리로 했던 것과 같은 아이디어를 FS 층에서 한다.

**할당 방식**: 평소에는 깨끗한 세그먼트에 순차로 append(**normal logging**). free 세그먼트가 부족해지면 GC 대신 dirty 세그먼트의 빈 구멍에 쓰는 **SSR**(Slack Space Recycling, threaded logging)로 전환한다 — 장치에는 랜덤 쓰기가 되지만 GC를 피한다. 그리고 **IPU**(in-place update): 조건에 따라 ext4처럼 제자리에 덮어쓴다(`ipu_policy` 비트마스크).

**GC**: 희생 섹션을 골라 valid 블록을 다른 곳(COLD data 로그)으로 옮기고 섹션을 free로 만든다. **foreground GC**는 free 섹션이 바닥나면 쓰기 경로에서 동기로, 주로 valid가 가장 적은 섹션(greedy). **background GC**는 `gc_thread_func` 커널 스레드가 유휴 시간에 `gc_min_sleep_time`~`gc_max_sleep_time`(30~60 s) 간격으로 깨어 나이까지 고려한 cost-benefit으로 고른다. 옮긴 결과는 **다음 checkpoint 이후에야** free가 된다(그 전에 크래시하면 옛 위치가 유효해야 하므로 — FTL이 "맵을 영구화하기 전에는 옛 블록을 erase하지 않는다"와 같다).

**over-provisioning**: mkfs.f2fs가 Main의 일부를 GC 여유분으로 숨긴다(예제 13: "Overprovision ratio = 10.000%, segments = 25 (GC reserved = 17)"). SSD의 OP와 정확히 같은 역할이다.

**fsync와 roll-forward**: F2FS는 fsync마다 전체 checkpoint를 쓰지 않는다. 데이터와 그 direct node 블록에 **fsync 표시**를 해서 쓰고, 크래시 후 마운트 때 마지막 checkpoint부터 warm node 로그를 훑어 fsync된 node를 다시 적용한다(roll-forward recovery). 그래서 fsync가 가볍다 — 작은 fsync가 많은 Android 앱(SQLite)에 F2FS가 쓰이는 이유 중 하나다.

### 6.3 예제 13 — mkfs.f2fs, 마운트 옵션, sysfs, debugfs status

무엇을 확인하나: F2FS 볼륨의 영역 크기, 기본 마운트 옵션, 튜닝 노브, 실시간 상태.

```sh
export PATH=/opt/bin:$PATH
echo "### mkfs.f2fs"; mkfs.f2fs -f -l j2f2fs /dev/vda 2>&1
modprobe f2fs && mkdir -p /mnt/f && mount -t f2fs /dev/vda /mnt/f && echo mounted
echo "### df"; df -k /mnt/f
grep vda /proc/mounts
echo "### /sys/fs/f2fs/vda"; ls /sys/fs/f2fs/vda | tr '\n' ' '; echo
for f in gc_urgent gc_urgent_sleep_time gc_min_sleep_time gc_max_sleep_time gc_idle ipu_policy min_ipu_util reclaim_segments ovp_segments reserved_blocks dirty_segments free_segments main_blkaddr cp_interval; do [ -e /sys/fs/f2fs/vda/$f ] && echo "$f = $(cat /sys/fs/f2fs/vda/$f)"; done
ls /sys/fs/f2fs/vda/stat 2>/dev/null
echo "### status"; cat /sys/kernel/debug/f2fs/status
```

```text
lab initramfs: 6.18.55-fs up 1.29s
### mkfs.f2fs

    F2FS-tools: mkfs.f2fs Ver: 1.16.0 (2023-04-11)

Info: Disable heap-based policy
Info: Debug level = 0
Info: Label = j2f2fs
Info: Trim is enabled
	/dev/vda appears to contain an existing filesystem (ext4).
Info: Segments per section = 1
Info: Sections per zone = 1
Info: sector size = 512
Info: total sectors = 1048576 (512 MB)
Info: zone aligned segment0 blkaddr: 512
Info: format version with
  "Linux version 6.18.55-fs (donh@lima-elinux) (gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0, GNU ld (GNU Binutils for Ubuntu) 
2.46) #1 SMP PREEMPT Sat Oct 17 14:23:35 PDT 2026"
Info: [/dev/vda] Discarding device
Info: This device doesn't support BLKSECDISCARD
Info: Discarded 512 MB
Info: Overprovision ratio = 10.000%
Info: Overprovision segments = 25 (GC reserved = 17)
Info: format successful
mounted
### df
Filesystem           1K-blocks      Used Available Use% Mounted on
/dev/vda                522240     65544    456696  13% /mnt/f
/dev/vda /mnt/f f2fs 
rw,lazytime,relatime,background_gc=on,nogc_merge,discard,discard_unit=block,user_xattr,inline_xattr,acl,inline_data,inli
ne_dentry,flush_merge,barrier,extent_cache,mode=adaptive,active_logs=6,alloc_mode=reuse,checkpoint_merge,fsync_mode=posi
x,memory=normal,errors=continue,lookup_mode=perf 0 0
### /sys/fs/f2fs/vda
allocate_section_hint allocate_section_policy atgc_age_threshold atgc_age_weight atgc_candidate_count 
atgc_candidate_ratio atgc_enabled avg_vblocks bggc_io_aware carve_out ckpt_thread_ioprio committed_atomic_block 
compr_new_inode compr_saved_block compr_written_block compress_percent compress_watermark cp_background_calls 
cp_foreground_calls cp_interval current_atomic_write current_reserved_blocks data_io_flag dir_level dirty_nats_ratio 
dirty_segments discard_granularity discard_idle_interval discard_io_aware discard_io_aware_gran discard_urgent_util 
effective_lookup_mode encoding encoding_flags extension_list feature_list features free_segments gc_background_calls 
gc_boost_gc_greedy gc_boost_gc_multiple gc_boost_zoned_gc_percent gc_foreground_calls gc_idle gc_idle_interval 
gc_max_sleep_time gc_min_sleep_time gc_mode gc_no_gc_sleep_time gc_no_zoned_gc_percent gc_pin_file_thresh 
gc_reclaimed_segments gc_remaining_trials gc_segment_mode gc_urgent gc_urgent_sleep_time gc_valid_thresh_ratio 
hot_data_age_threshold idle_interval inject_rate inject_type iostat_enable iostat_period_ms ipu_policy last_age_weight 
lifetime_write_kbytes main_blkaddr max_discard_issue_time max_discard_request max_fragment_chunk max_fragment_hole 
max_io_bytes max_ordered_discard max_read_extent_count max_roll_forward_node_blocks max_small_discards 
max_victim_search mid_discard_issue_time migration_granularity migration_window_granularity min_discard_issue_time 
min_fsync_blocks min_hot_blocks min_ipu_util min_seq_blocks min_ssr_sections mounted_time_sec moved_blocks_background 
moved_blocks_foreground node_io_flag ovp_segments peak_atomic_write pending_discard ra_nid_pages ram_thresh readdir_ra 
reclaim_segments reserved_blocks reserved_pin_section reserved_segments revoked_atomic_block seq_file_ra_mul stat 
umount_discard_timeout unusable warm_data_age_threshold
gc_urgent = 0
gc_urgent_sleep_time = 500
gc_min_sleep_time = 30000
gc_max_sleep_time = 60000
gc_idle = 0
ipu_policy = 129
min_ipu_util = 70
reclaim_segments = 12
ovp_segments = 25
reserved_blocks = 0
dirty_segments = 0
free_segments = 242
main_blkaddr = 4096
cp_interval = 60
cp_status       queued_discard  undiscard_blks
issued_discard  sb_status
### status

=====[ partition info(vda). #0, RW, CP: Good]=====
[SB: 1] [CP: 2] [SIT: 2] [NAT: 2] [SSA: 1] [MAIN: 248(OverProv:25 Resv:17)]

Current Time Sec: 1 / Mounted Time Sec: 1

Policy:
  - IPU: [ FORCE HONOR_OPU_WRITE ]

Utilization: 0% (2 valid blocks, 0 discard blocks)
  - Node: 1 (Inode: 1, Other: 0)
  - Data: 1
  - Inline_xattr Inode: 0
  - Inline_data Inode: 0
  - Inline_dentry Inode: 0
  - Compressed Inode: 0, Blocks: 0
  - Swapfile Inode: 0
  - Donate Inode: 0
  - Orphan/Append/Update Inode: 0, 0, 0

Main area: 248 segs, 248 secs 248 zones
    TYPE            segno    secno   zoneno  dirty_seg   full_seg  valid_blk
  - COLD   data:       61       61       61          0          0          0
  - WARM   data:      123      123      123          0          0          0
  - HOT    data:        3        3        3          1          0          1
  - Dir   dnode:        0        0        0          1          0          1
  - File  dnode:        1        1        1          0          0          0
  - Indir nodes:        2        2        2          0          0          0
  - Pinned file:       -1       -1       -1
  - ATGC   data:       -1       -1       -1

  - Valid: 6
  - Dirty: 0
  - Prefree: 0
  - Free: 242 (242)

CP calls: 0 (BG: 0)
CP count: 0
  - cp blocks : 0
  - sit blocks : 0
  - nat blocks : 0
  - ssa blocks : 0
CP merge:
  - Queued :    0
  - Issued :    0
  - Total :    0
  - Cur time :    0(ms)
  - Peak time :    0(ms)
GC calls: 0 (gc_thread: 0)
  - data segments : 0 (BG: 0)
  - node segments : 0 (BG: 0)
  - Reclaimed segs :
    - Normal : 0
    - Idle CB : 0
    - Idle Greedy : 0
    - Idle AT : 0
    - Urgent High : 0
    - Urgent Mid : 0
    - Urgent Low : 0
Try to move 0 blocks (BG: 0)
  - data blocks : 0 (0)
  - node blocks : 0 (0)
BG skip : IO: 0, Other: 0

...

Distribution of User Blocks: [ valid | invalid | free ]
  [|--|------------------------------------------------]

IPU: 0 blocks
SSR: 0 blocks in 0 segments
LFS: 0 blocks in 0 segments

...
```

(sysfs 목록은 `tr`로 한 줄로 이어 붙였고, 120자가 넘는 줄은 `fold -s -w 120`으로 접었다. status의 Extent Cache·Balancing·Memory 부분은 `...`로 생략했다.)

출력에서 볼 것:

- mkfs: **segment 0이 zone 정렬**("zone aligned segment0 blkaddr: 512"), OP 10%, mkfs가 장치 전체를 discard.
- status 첫 줄: `[SB: 1] [CP: 2] [SIT: 2] [NAT: 2] [SSA: 1] [MAIN: 248(OverProv:25 Resv:17)]` — 영역별 세그먼트 수. 그림 6의 숫자다.
- 로그 6개의 현재 세그먼트(`COLD data: 61`, `WARM data: 123`, `HOT data: 3` …) — 일부러 멀리 떨어진 세그먼트에서 시작한다.
- 기본 마운트 옵션: `background_gc=on`, `discard`, `flush_merge`, `mode=adaptive`, `active_logs=6`, `fsync_mode=posix`, `lazytime`, `errors=continue`.
- **`ipu_policy = 129`** = `F2FS_IPU_FORCE`(bit 0) + `F2FS_IPU_HONOR_OPU_WRITE`(bit 7), status의 `IPU: [ FORCE HONOR_OPU_WRITE ]`. 소스(`fs/f2fs/super.c`의 `f2fs_tuning_parameters()`)를 보면 Main 세그먼트 수가 `SMALL_VOLUME_SEGMENTS`(16 × 512 = 16 GiB) 이하인 **작은 볼륨**이면 lfs 모드가 아닐 때 IPU를 강제한다. 즉 **512 MiB 테스트 볼륨의 F2FS는 기본으로 덮어쓰기를 제자리에서 한다** — 다음 예제의 해석에 결정적이다.

### 6.4 예제 14 — 랜덤 덮어쓰기의 write amplification: ext4 vs F2FS

무엇을 확인하나: 같은 워크로드(파일을 채운 뒤 랜덤 4 KiB 덮어쓰기 30000회, 64회마다 fsync)에서 장치가 받은 쓰기 / 앱이 쓴 양. 파일 크기 200 MiB(사용률 약 44%)와 380 MiB(약 85%), 설정 4가지: ext4, F2FS 기본(작은 볼륨이라 IPU 강제), F2FS `ipu_policy=0`(IPU 끔, adaptive 모드라 SSR 허용), F2FS `mode=lfs`(순수 log-structured, IPU·SSR 없음).

```c
/* randw.c: prefill a file, then N random 4 KiB overwrites (fsync every K),
 * report device-level sectors written / application sectors = write amplification */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned long long wsect(const char *dev)
{
	char p[64]; unsigned long long v[7];
	snprintf(p, sizeof(p), "/sys/block/%s/stat", dev);
	FILE *f = fopen(p, "r");
	if (!f || fscanf(f, "%llu %llu %llu %llu %llu %llu %llu", &v[0], &v[1], &v[2], &v[3],
			 &v[4], &v[5], &v[6]) != 7) { perror(p); exit(1); }
	fclose(f);
	return v[6];
}
int main(int argc, char **argv)
{
	if (argc != 6) { fprintf(stderr, "usage: %s file MiB nwrites fsync_every dev\n", argv[0]); exit(1); }
	long nblk = atol(argv[2]) * 256, n = atol(argv[3]), k = atol(argv[4]);
	static char buf[4096];
	int fd = open(argv[1], O_CREAT | O_RDWR, 0644);
	if (fd < 0) { perror("open"); exit(1); }
	for (long i = 0; i < nblk; i++)                  /* prefill = "fill the drive" */
		if (pwrite(fd, buf, 4096, i * 4096) != 4096) { perror("prefill"); exit(1); }
	if (fsync(fd)) { perror("fsync"); exit(1); }
	sync();
	unsigned long long s0 = wsect(argv[5]);
	srandom(42);
	for (long i = 0; i < n; i++) {
		memset(buf, (int)i, 64);
		if (pwrite(fd, buf, 4096, (random() % nblk) * 4096) != 4096) { perror("pwrite"); exit(1); }
		if ((i + 1) % k == 0 && fsync(fd)) { perror("fsync"); exit(1); }
	}
	if (fsync(fd)) { perror("fsync"); exit(1); }
	sync();
	unsigned long long s1 = wsect(argv[5]);
	printf("file %ld MiB  overwrites %ld  app %ld KiB  dev %llu KiB  WAF %.2f\n",
	       nblk / 256, n, n * 4, (s1 - s0) / 2, (s1 - s0) / 8.0 / n);
	close(fd);
	return 0;
}
```

```sh
export PATH=/opt/bin:$PATH
modprobe f2fs; mkdir -p /mnt/x; MB=$(cat /proc/cmdline | sed 's/.*J2MB=\([0-9]*\).*/\1/')
st() { cat /sys/kernel/debug/f2fs/status | grep -E "^GC calls|data segments|node segments|^Try to move|^IPU|^SSR|^LFS|Utilization|^  - Dirty|^  - Free" | tr -s ' ' | tr '\n' ';'; echo; }
echo "### ext4 file=${MB}MiB"; mkfs.ext4 -q -F /dev/vda; mount -t ext4 /dev/vda /mnt/x; df -k /mnt/x | tail -1
/j2/randw /mnt/x/db.bin $MB 30000 64 vda; umount /mnt/x
for v in default ipu0 lfs; do
  mkfs.f2fs -q -f /dev/vda >/dev/null; o=""; [ $v = lfs ] && o="-o mode=lfs"
  mount -t f2fs $o /dev/vda /mnt/x; [ $v = ipu0 ] && echo 0 > /sys/fs/f2fs/vda/ipu_policy
  echo "### f2fs-$v file=${MB}MiB ipu_policy=$(cat /sys/fs/f2fs/vda/ipu_policy)"
  /j2/randw /mnt/x/db.bin $MB 30000 64 vda; st; umount /mnt/x
done
```

사용률 44% (`J2MB=200`):

```text
lab initramfs: 6.18.55-fs up 1.45s
### ext4 file=200MiB
/dev/vda contains a f2fs file system labelled 'j2f2fs'
/dev/vda                498900       152    462052   0% /mnt/x
file 200 MiB  overwrites 30000  app 120000 KiB  dev 125556 KiB  WAF 1.05
### f2fs-default file=200MiB ipu_policy=129
file 200 MiB  overwrites 30000  app 120000 KiB  dev 121812 KiB  WAF 1.02
Utilization: 44% (51254 valid blocks, 0 discard blocks); - Dirty: 1; - Free: 141 (141);GC calls: 0 (gc_thread: 0); - data segments : 0 (BG: 0); - node segments : 0 (BG: 0);Try to move 0 blocks (BG: 0);IPU: 29980 blocks;SSR: 0 blocks in 0 segments;LFS: 51724 blocks in 101 segments;
### f2fs-ipu0 file=200MiB ipu_policy=0
file 200 MiB  overwrites 30000  app 120000 KiB  dev 189340 KiB  WAF 1.58
Utilization: 44% (51254 valid blocks, 0 discard blocks); - Dirty: 158; - Free: 84 (84);GC calls: 0 (gc_thread: 0); - data segments : 0 (BG: 0); - node segments : 0 (BG: 0);Try to move 0 blocks (BG: 0);IPU: 0 blocks;SSR: 0 blocks in 0 segments;LFS: 98491 blocks in 191 segments;
### f2fs-lfs file=200MiB ipu_policy=0
file 200 MiB  overwrites 30000  app 120000 KiB  dev 189340 KiB  WAF 1.58
Utilization: 44% (51254 valid blocks, 0 discard blocks); - Dirty: 158; - Free: 84 (84);GC calls: 0 (gc_thread: 0); - data segments : 0 (BG: 0); - node segments : 0 (BG: 0);Try to move 0 blocks (BG: 0);IPU: 0 blocks;SSR: 0 blocks in 0 segments;LFS: 98491 blocks in 191 segments;
```

사용률 85% (`J2MB=380`):

```text
lab initramfs: 6.18.55-fs up 1.38s
### ext4 file=380MiB
/dev/vda contains a f2fs file system
/dev/vda                498900       152    462052   0% /mnt/x
file 380 MiB  overwrites 30000  app 120000 KiB  dev 125588 KiB  WAF 1.05
### f2fs-default file=380MiB ipu_policy=129
file 380 MiB  overwrites 30000  app 120000 KiB  dev 121844 KiB  WAF 1.02
Utilization: 85% (97379 valid blocks, 0 discard blocks); - Dirty: 1; - Free: 51 (51);GC calls: 0 (gc_thread: 0); - data segments : 0 (BG: 0); - node segments : 0 (BG: 0);Try to move 0 blocks (BG: 0);IPU: 29988 blocks;SSR: 0 blocks in 0 segments;LFS: 97849 blocks in 191 segments;
### f2fs-ipu0 file=380MiB ipu_policy=0
file 380 MiB  overwrites 30000  app 120000 KiB  dev 211684 KiB  WAF 1.76
Utilization: 85% (97379 valid blocks, 0 discard blocks); - Dirty: 211; - Free: 31 (31);GC calls: 3 (gc_thread: 0); - data segments : 0 (BG: 0); - node segments : 3 (BG: 0);Try to move 222 blocks (BG: 0);IPU: 0 blocks;SSR: 19236 blocks in 359 segments;LFS: 130603 blocks in 254 segments;
### f2fs-lfs file=380MiB ipu_policy=0
file 380 MiB  overwrites 30000  app 120000 KiB  dev 401784 KiB  WAF 3.35
Utilization: 85% (97379 valid blocks, 0 discard blocks); - Dirty: 222; - Free: 20 (20);GC calls: 140 (gc_thread: 0); - data segments : 112 (BG: 0); - node segments : 28 (BG: 0);Try to move 46711 blocks (BG: 0);IPU: 0 blocks;SSR: 0 blocks in 0 segments;LFS: 196672 blocks in 382 segments;
```

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="20.0" y="22.0" font-size="13" text-anchor="start" font-weight="bold">랜덤 4 KiB 덮어쓰기 30000회 (fsync 64회마다) — 장치 쓰기 / 앱 쓰기 (실측)</text><line x1="60" y1="240" x2="660" y2="240" stroke="currentColor" stroke-width="1.3"/><line x1="60" y1="50" x2="60" y2="240" stroke="currentColor" stroke-width="1.3"/><text x="54.0" y="244.0" font-size="11" text-anchor="end">0</text><line x1="60" y1="240.0" x2="660" y2="240.0" stroke="#888" stroke-width="0.6" stroke-dasharray="4 3"/><text x="54.0" y="196.5" font-size="11" text-anchor="end">1</text><line x1="60" y1="192.5" x2="660" y2="192.5" stroke="#888" stroke-width="0.6" stroke-dasharray="4 3"/><text x="54.0" y="149.0" font-size="11" text-anchor="end">2</text><line x1="60" y1="145.0" x2="660" y2="145.0" stroke="#888" stroke-width="0.6" stroke-dasharray="4 3"/><text x="54.0" y="101.5" font-size="11" text-anchor="end">3</text><line x1="60" y1="97.5" x2="660" y2="97.5" stroke="#888" stroke-width="0.6" stroke-dasharray="4 3"/><text x="54.0" y="54.0" font-size="11" text-anchor="end">4</text><line x1="60" y1="50.0" x2="660" y2="50.0" stroke="#888" stroke-width="0.6" stroke-dasharray="4 3"/><rect x="91.0" y="190.1" width="52.0" height="49.9" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0" stroke-width="1"/><text x="117.0" y="184.1" font-size="11" text-anchor="middle">1.05</text><rect x="153.0" y="191.6" width="52.0" height="48.5" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b" stroke-width="1"/><text x="179.0" y="185.6" font-size="11" text-anchor="middle">1.02</text><rect x="215.0" y="164.9" width="52.0" height="75.0" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c" stroke-width="1"/><text x="241.0" y="158.9" font-size="11" text-anchor="middle">1.58</text><rect x="277.0" y="164.9" width="52.0" height="75.0" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a" stroke-width="1"/><text x="303.0" y="158.9" font-size="11" text-anchor="middle">1.58</text><text x="210.0" y="258.0" font-size="12" text-anchor="middle">사용률 44% (200 MiB 파일)</text><rect x="391.0" y="190.1" width="52.0" height="49.9" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0" stroke-width="1"/><text x="417.0" y="184.1" font-size="11" text-anchor="middle">1.05</text><rect x="453.0" y="191.6" width="52.0" height="48.5" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b" stroke-width="1"/><text x="479.0" y="185.6" font-size="11" text-anchor="middle">1.02</text><rect x="515.0" y="156.4" width="52.0" height="83.6" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c" stroke-width="1"/><text x="541.0" y="150.4" font-size="11" text-anchor="middle">1.76</text><rect x="577.0" y="80.9" width="52.0" height="159.1" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a" stroke-width="1"/><text x="603.0" y="74.9" font-size="11" text-anchor="middle">3.35</text><text x="510.0" y="258.0" font-size="12" text-anchor="middle">사용률 85% (380 MiB 파일)</text><rect x="70.0" y="274.0" width="14.0" height="14.0" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0" stroke-width="1"/><text x="90.0" y="286.0" font-size="12" text-anchor="start">ext4</text><rect x="220.0" y="274.0" width="14.0" height="14.0" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b" stroke-width="1"/><text x="240.0" y="286.0" font-size="12" text-anchor="start">f2fs 기본(IPU)</text><rect x="370.0" y="274.0" width="14.0" height="14.0" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c" stroke-width="1"/><text x="390.0" y="286.0" font-size="12" text-anchor="start">f2fs ipu_policy=0</text><rect x="520.0" y="274.0" width="14.0" height="14.0" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a" stroke-width="1"/><text x="540.0" y="286.0" font-size="12" text-anchor="start">f2fs mode=lfs</text><text x="20.0" y="312.0" font-size="12" text-anchor="start">lfs 85%: GC가 46711블록을 옮김 → WAF 3.35. 기본값은 작은 볼륨(&lt;16 GiB)이라 IPU 강제.</text></svg>
```
그림 7 — 예제 14의 실측 WAF(파일시스템 층만. 장치 FTL의 WAF는 여기에 곱해진다).

출력에서 볼 것:

- **ext4 1.05**: 제자리 덮어쓰기 + 64회마다 저널 commit. 사용률과 무관.
- **F2FS 기본 1.02**: `IPU: 29980 blocks` — 덮어쓰기 거의 전부가 제자리였다. 작은 볼륨 튜닝 때문에 F2FS가 ext4처럼 동작한 것이다. 실제 제품의 수십 GB `/data`에서는 이 강제 IPU가 꺼진다(그때 기본 `ipu_policy`는 fsync 경로 위주의 조건부 IPU — 큰 볼륨에서의 실측은 이 노트에서 하지 않았다).
- **F2FS `ipu_policy=0` 1.58 → 1.76**: 모든 덮어쓰기가 새 위치에 append(LFS 블록 수 증가). 데이터 외에 direct node 블록과 checkpoint·NAT·SIT 갱신이 추가된다. 85%에서는 free 세그먼트가 줄어 **SSR 19236블록**이 쓰였고 GC는 node 세그먼트 3개뿐 — SSR이 GC를 대신 막아 준 것이다.
- **F2FS `mode=lfs` 1.58 → 3.35**: SSR도 없어서 85%에서는 foreground GC 140회, **valid 블록 46711개를 옮겼다**(앱이 쓴 30000블록보다 많다). Don의 FTL에서 "사용률이 올라가면 희생 블록의 valid 비율이 올라가고 GC 복사량이 폭발한다"(WAF ≈ 1/(1−u) 근사)와 같은 곡선이 파일시스템 층에서 재현된 것이다.
- 결론: **log-structured는 공짜가 아니다**. F2FS의 이득은 "장치 FTL이 받는 쓰기 패턴이 순차적이 되는 것"(장치 내부 WAF 감소)이고, 대가는 FS 층 GC다. 그래서 F2FS는 IPU·SSR·hot/cold 분리·OP로 FS 층 WAF를 낮추고, 사용률 관리(꽉 채우지 않기)가 중요하다. 이 측정은 장치 내부 WAF를 볼 수 없는 virtio 위에서 한 것이므로, "F2FS가 실제 eMMC 수명을 얼마나 늘리나"는 이 실험으로 말할 수 없다.

### 6.5 예제 15 — background GC를 강제로 돌리고 세그먼트 분포 보기

무엇을 확인하나: `gc_urgent`로 GC 스레드를 깨우고, 옮긴 블록 수와 세그먼트 valid 분포(`/proc/fs/f2fs/vda/segment_info`)의 변화, 그리고 "checkpoint 후에야 free가 된다".

```sh
export PATH=/opt/bin:$PATH
modprobe f2fs; mkdir -p /mnt/x; S=/sys/fs/f2fs/vda
st() { grep -E "^GC calls|data segments|^Try to move|^  - (Dirty|Prefree|Free)" /sys/kernel/debug/f2fs/status | tr -s ' ' | tr '\n' ';'; echo; }
hist() { tail -n +3 /proc/fs/f2fs/vda/segment_info | tr -s ' ' '\n' | grep '|' | awk -F'|' '$1<3{v=$2; b=(v==0)?"0":(v<128)?"1-127":(v<384)?"128-383":(v<512)?"384-511":"512"; h[b]++} END{printf "data segs by valid blocks: 0=%d 1-127=%d 128-383=%d 384-511=%d full=%d\n",h["0"],h["1-127"],h["128-383"],h["384-511"],h["512"]}'; }
mkfs.f2fs -q -f /dev/vda >/dev/null; mount -t f2fs -o mode=lfs /dev/vda /mnt/x
/j2/randw /mnt/x/db.bin 300 20000 64 vda
echo "### before"; st; hist
echo 1 > $S/gc_urgent; sleep 8; echo 0 > $S/gc_urgent
echo "### after gc_urgent=1 8s"; st; hist
sync; echo "### after sync (checkpoint)"; st; hist
echo "reclaimed=$(cat $S/gc_reclaimed_segments) moved_bg=$(cat $S/moved_blocks_background) cp_bg=$(cat $S/cp_background_calls) cp_fg=$(cat $S/cp_foreground_calls)"
umount /mnt/x
```

```text
lab initramfs: 6.18.55-fs up 1.45s
file 300 MiB  overwrites 20000  app 80000 KiB  dev 134888 KiB  WAF 1.69
### before
 - Dirty: 189; - Prefree: 0; - Free: 53 (53);GC calls: 0 (gc_thread: 0); - data segments : 0 (BG: 0);Try to move 0 blocks (BG: 0);
data segs by valid blocks: 0=28 1-127=2 128-383=21 384-511=168 full=0
### after gc_urgent=1 8s
 - Dirty: 189; - Prefree: 0; - Free: 53 (53);GC calls: 16 (gc_thread: 16); - data segments : 16 (BG: 16);Try to move 6048 blocks (BG: 6048);
data segs by valid blocks: 0=28 1-127=2 128-383=21 384-511=168 full=0
### after sync (checkpoint)
 - Dirty: 173; - Prefree: 0; - Free: 58 (58);GC calls: 16 (gc_thread: 16); - data segments : 16 (BG: 16);Try to move 6048 blocks (BG: 6048);
data segs by valid blocks: 0=43 1-127=2 128-383=5 384-511=169 full=11
reclaimed=0 moved_bg=6048 cp_bg=2 cp_fg=3
```

출력에서 볼 것:

- `gc_urgent=1`(high) 8초 동안 GC 스레드가 16번 돌아 **data 섹션 16개에서 valid 6048블록을 옮겼다**. 희생 섹션은 valid가 적은 쪽(128~383 구간이 21 → 5개)이다.
- 그런데 GC 직후에는 Free 53 → 53, Dirty 189 → 189 — **변화 없음**. `sync`(checkpoint) 뒤에야 Free 58, Dirty 173이 되었다. 옮긴 6048블록은 꽉 찬 세그먼트 11개(512 × 11 ≈ 5632, 나머지는 열린 세그먼트)가 되었다.
- 비용 계산: 섹션 16개를 비우려고 블록 6048개(≈ 11.8 세그먼트)를 복사했고, 순수 이득은 free +5 세그먼트. 희생 섹션의 평균 valid가 높을수록 이 비율이 나빠진다 — FTL GC의 효율 계산과 똑같다.
- `gc_urgent`의 값: 0 = 끔, 1 = high(쉬지 않고 GC), 2 = low, 3 = mid(커널 문서 `Documentation/ABI/testing/sysfs-fs-f2fs`). Android는 충전 중·화면 꺼짐 같은 유휴 시간에 이걸 켜서 미리 GC를 해 둔다("idle maintenance"). 임베디드 장비라면 "로봇이 도킹 스테이션에 있을 때" 같은 시점이다.

### 6.6 예제 16 — F2FS 투명 압축

무엇을 확인하나: 파일 단위 압축(`compression` 기능, 클러스터 단위 lz4/zstd/lzo). 로그처럼 압축이 잘 되는 데이터를 덜 쓰면 flash 마모가 준다.

별도 부팅(`g5g.sh`, 앞부분은 예제 15의 첫 시도와 같은 GC 실험이라 생략)의 뒷부분이다:

```sh
mkfs.f2fs -q -f -O extra_attr,compression /dev/vda >/dev/null
mount -t f2fs -o compress_algorithm=lz4,compress_extension=log /dev/vda /mnt/x
i=0; while [ $i -lt 20000 ]; do echo "2026-10-17T12:00:00 imu ax=0.01 ay=0.02 az=9.81 seq=$i"; i=$((i+1)); done > /mnt/x/imu.log
sync; ls -l /mnt/x/imu.log; lsattr /mnt/x/imu.log
echo "compr_written_block=$(cat $S/compr_written_block) compr_saved_block=$(cat $S/compr_saved_block) compr_new_inode=$(cat $S/compr_new_inode)"
du -k /mnt/x/imu.log
```

```text
-rw-r--r--    1 0        0          1148890 Oct 17 21:50 /mnt/x/imu.log
--------c----------- /mnt/x/imu.log
compr_written_block=70 compr_saved_block=210 compr_new_inode=1
1124	/mnt/x/imu.log
```

(`ls`의 색 제어문자는 지웠다.)

출력에서 볼 것: `compress_extension=log`로 `.log` 파일에 자동으로 압축 속성(`c`)이 붙었고, 블록 280개 분량을 70블록으로 썼다(210블록 절약, 4:1) — **장치로 가는 쓰기가 1/4**. 하지만 `du`는 1124 KiB로 그대로다. F2FS 압축은 기본적으로 절약한 블록을 **예약 상태로 남겨 둬서**(덮어쓰기 때 압축이 안 되면 그 자리가 필요하므로) 사용자에게 공간을 돌려주지 않는다. 공간까지 돌려받으려면 `F2FS_IOC_RELEASE_COMPRESS_BLOCKS` ioctl을 쓰는데, 그러면 그 파일은 불변(immutable)이 된다 — 이 부분은 이 노트에서 실행하지 않았다. 즉 F2FS 압축의 1차 이득은 **용량이 아니라 쓰기량(수명)과 읽기 대역폭**이다.

### 6.7 왜 Android와 flash 장비가 F2FS를 쓰나 / 언제 ext4가 낫나

- F2FS 장점: 작은 랜덤 쓰기를 장치에 순차로 바꿔 줌(eMMC/UFS의 내부 GC 부담과 지연 스파이크 감소), 가벼운 fsync(roll-forward), hot/cold 분리, 투명 압축, discard 일괄 처리. 많은 Android 폰의 `/data`가 F2FS다.
- ext4 장점: 압도적으로 오래 검증됨, 도구(e2fsck·debugfs·resize2fs)가 성숙, 부트로더(U-Boot, GRUB) 지원이 넓음, 꽉 찬 상태에서도 성능이 덜 무너짐, 온라인 grow.
- F2FS 주의: 사용률이 높을 때 GC 비용, 볼륨 축소는 제한적(`resize.f2fs`는 grow 위주), U-Boot의 F2FS 지원은 제한적 → **rootfs(읽기 위주)는 ext4/SquashFS/EROFS, 쓰기 많은 /data는 F2FS** 같은 분리가 흔하다(J3에서 읽기전용 rootfs).

---

## 7. btrfs · exFAT · vfat — 한 단락씩

- **vfat(FAT32)**: 저널 없음, 권한 없음, 파일 최대 4 GiB−1. 그런데 **부트 파티션의 표준**이다 — UEFI ESP는 FAT여야 하고, Raspberry Pi 펌웨어·많은 SoC BootROM·U-Boot가 FAT를 읽는다. 예제 2의 p1이 이것이다(`mkfs.vfat -F 32`, sgdisk 타입 `EF00`). 깨끗하지 않게 언마운트되면 "dirty bit"이 남고 `fsck.vfat`으로 고친다. 부트 파티션은 평소 ro로 마운트하거나 아예 마운트하지 않고, 업데이트 때만 붙였다가 `sync` 후 뗀다.
- **exFAT**: SDXC 카드(32 GB 초과)의 표준 포맷. 4 GiB 넘는 파일 가능. Linux 5.7부터 커널 내장 드라이버(Samsung 기여). 카메라·드론이 SD에 영상을 남기고 PC/맥에서 바로 읽게 하려면 exFAT가 현실적인 선택이다. 저널은 없으니 녹화 중 전원 차단 대비는 앱(주기적 fsync, 파일 분할)이 해야 한다.
- **btrfs**: copy-on-write B-tree FS. 데이터·메타데이터 체크섬, 스냅샷, send/receive, 압축, 다중 장치. 스냅샷으로 업데이트 롤백을 만들기 좋지만 메타데이터 오버헤드와 복잡도 때문에 소형 임베디드에서는 드물다. CoW라서 F2FS처럼 out-of-place 쓰기를 한다.

---

## 8. 커널 안에서는 무슨 일이 일어나나

이 노트의 예제가 지나간 경로를 함수 이름으로 다시 읽는다(6.18 소스 기준, trace에서 실제로 본 이름 포함).

```
(1) partition scan at disk probe
    virtblk_probe -> device_add_disk -> ... -> bdev_disk_changed -> blk_add_partitions
      -> check_partition: efi_partition (GPT, CRC check, PMBR check) else msdos_partition
      -> add_partition -> vda1..3 (sysfs start/size, uevent PARTN/PARTNAME/PARTUUID)
    root=PARTUUID=...  -> early_lookup_bdev -> devt_from_partuuid (block/early-lookup.c)
      -> mount_root -> "VFS: Mounted root (ext4 filesystem) readonly on device 254:2"

(2) write + fsync on ext4, data=ordered
    write -> ext4_file_write_iter -> generic_perform_write -> ext4_da_write_begin (delalloc)
    fsync -> ext4_sync_file [trace: ext4_sync_file_enter]
      -> file_write_and_wait_range -> ext4_writepages (allocate blocks now) -> submit_bio [WS data]
      -> ext4_fsync_journal -> ext4_fc_commit (no fast_commit: falls back to jbd2_complete_transaction)
         kjournald2 "jbd2/vda-8" -> jbd2_journal_commit_transaction
           [jbd2_start_commit, jbd2_commit_flushing, jbd2_commit_logging]
           descriptor + metadata copies -> journal area [WSM]
           commit block with REQ_PREFLUSH|REQ_FUA -> blk-flush: [FF] write [FF] (no FUA on vda)
           [jbd2_end_commit]
      later: jbd2_log_do_checkpoint -> write home locations -> jbd2_cleanup_journal_tail

(3) error path
    bio fails (fail_make_request) -> ext4_end_bio "I/O error 10 writing to inode"
      -> jbd2 "Aborting journal" -> ext4_journal_check_start "Detected aborted journal"
      -> ext4_handle_error: errors=remount-ro -> set EXT4_FLAGS_EMERGENCY_RO ("emergency_ro")

(4) F2FS overwrite and GC
    f2fs_file_write_iter -> ... writeback: IPU allowed ? f2fs_inplace_write_data
                                         : f2fs_allocate_data_block (log head, maybe SSR)
    f2fs_sync_file -> f2fs_do_sync_file -> write dnode with fsync mark (roll-forward), no full CP
    free sections low -> f2fs_gc (foreground, greedy via f2fs_get_victim)
    gc_thread_func (background, cost-benefit / gc_urgent) -> move valid blocks to COLD data log
    f2fs_write_checkpoint -> prefree segments become free
```

드라이버·BSP를 쓸 때 이 지식이 다시 나오는 곳:

| 하게 될 일 | 만나는 지점 | 관련 노트 |
|---|---|---|
| 새 블록 드라이버(SD/eMMC 호스트, 가상 디스크) | flush/FUA 지원을 `queue_limits`의 `features`(`BLK_FEAT_WRITE_CACHE`, `BLK_FEAT_FUA`)로 정확히 알려야 jbd2의 순서 보장이 산다 | I11 §6, G1 |
| eMMC 파티션·부팅 | GPT vs 하드웨어 boot 파티션(`mmcblk0boot0`)·RPMB 구분, PARTUUID로 root 지정 | C2, D(aliases) |
| 이미지 빌드 | `genimage`/`wic`로 GPT + 파티션별 FS 이미지 조립, mkfs 기능 플래그를 부트로더와 맞춤 | K |
| 전원 차단 안전 | fsync 의미, `commit=`, barrier, 로그 저장 설계 | J5 |
| 읽기전용 rootfs | SquashFS/EROFS + overlayfs 위의 ext4/F2FS 데이터 파티션 | J3 |
| raw NAND | 이 노트의 FS는 블록 장치 전용 — raw NAND는 UBI/UBIFS | I11, J4 |

---

## 9. Embedded 실무에서 다시 보기

### 9.1 장면 1 — 센서 키트의 파티션 설계 (eMMC 16 GB)

예를 들어 Jetson/i.MX8 센서 키트라면:

```text
GPT on /dev/mmcblk0 (1 MiB aligned, PARTLABELs used by U-Boot/initramfs)
  p1 boot     FAT32  64 MiB   kernel Image, DTB, U-Boot env (A/B: boot_a, boot_b)
  p2 rootfs_a ext4 or SquashFS  1 GiB  read-only, verified (J3/L)
  p3 rootfs_b same           1 GiB  OTA target
  p4 data     F2FS or ext4  rest   logs, calibration, recordings; grown on first boot
eMMC hw partitions (not in GPT): boot0/boot1 = bootloader, RPMB = secure counters
```

설계 포인트: rootfs는 ro로 두어 저널·fsck 걱정을 줄이고(J3), 쓰기는 data 하나로 모은다. `root=PARTUUID=` 또는 initramfs에서 `PARTLABEL=rootfs_a`로 찾는다. data는 첫 부팅에 §5.6 방식으로 끝까지 키운다. data의 사용률이 80%를 넘지 않게 로그 로테이션을 건다(F2FS GC, ext4 조각화 둘 다를 위해).

### 9.2 장면 2 — "로그를 100 ms마다 fsync했더니 eMMC 수명 예측이 2년"

예제 8의 숫자를 그대로 쓴다: 4 KiB append + fsync = 장치 쓰기 24 KiB(ext4 ordered). 초당 10번이면 하루 ≈ 24 KiB × 10 × 86400 ≈ 20 GiB/day. 장치 내부 WAF까지 곱하면 소형 eMMC의 TBW를 금방 쓴다. 해결 순서: (1) fsync를 정말 필요한 지점(이벤트·주기 1초)으로 줄이고 메모리에 모아 큰 단위로 쓴다, (2) `fast_commit`(fsync당 8 KiB), (3) 쓰기 많은 파티션을 F2FS로(+압축), (4) `noatime`, (5) eMMC의 `EXT_CSD` 수명 추정값(`DEVICE_LIFE_TIME_EST_TYP_A/B`, `mmc extcsd read`)을 필드에서 모니터링. 그 전에 반드시 측정: `/sys/block/mmcblk0/stat`의 쓰기 섹터를 하루 간격으로 읽으면 된다 — 이 노트의 `fsyncbench`가 한 것과 같다.

### 9.3 장면 3 — 필드 장비가 "가끔 쓰기 실패" → 헬스 체크가 못 잡음

eMMC가 노화해 쓰기 에러를 내기 시작하면 ext4는 `errors=remount-ro`대로 쓰기를 막는다. 그런데 헬스 체크가 `/proc/mounts`의 `ro`만 보고 있었다면 6.x 커널에서는 `rw,…,emergency_ro`라 못 잡는다(예제 11). 감시는 (1) 실제 쓰기 테스트(작은 파일 write+fsync), (2) dmesg/커널 로그의 `EXT4-fs error`, `Aborting journal` 패턴, (3) `/sys/fs/ext4/<dev>/errors_count`(있으면) 같은 직접 신호를 쓴다. 정책은 `errors=panic` + 워치독으로 재부팅 → 부팅 시 `e2fsck -p` → 실패 반복 시 복구 모드 진입이 무인 장비에서 흔한 선택이다.

### 9.4 장면 4 — SD 카드로 영상 녹화 (드론, 카메라)

사용자가 PC에서 꺼내 읽어야 하므로 exFAT(64 GB SDXC). 저널이 없으니 녹화 중 배터리 분리에 대비해 (1) 일정 크기(예: 1분)마다 파일을 닫고 새로 연다, (2) 컨테이너는 중간에 잘려도 재생 가능한 형식(fragmented MP4, MPEG-TS), (3) 카드의 AU(4 MiB) 단위로 큰 쓰기를 한다. 파티션 시작을 4 MiB 정렬로 맞춰 카드 FTL의 RMW를 피한다(§1.8).

---

## 10. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 이미지를 큰 장치에 dd 후 그대로 사용 | 남은 공간이 안 보이거나 도구가 "secondary header not at end" 경고 | GPT 백업 헤더가 이미지 끝에 있음 | 첫 부팅에 `sgdisk -e` + 파티션 확장 + `resize2fs` (예제 12) |
| 파티션 확장 때 PARTUUID를 새로 만듦 | 다음 부팅에 "VFS: Cannot open root device" | `root=PARTUUID=`가 옛 GUID를 가리킴 | `sgdisk -u N:<기존 GUID>`로 유지 |
| 최신 mkfs.ext4 기본값으로 만든 rootfs | U-Boot/오래된 커널이 마운트 거부 | 모르는 incompat 기능(`metadata_csum_seed`, `orphan_file` 등) | 대상 부트로더·커널 버전에 맞게 `-O ^feature` |
| `barrier=0`을 성능 옵션으로 켬 | 전원 차단 후 fsck가 큰 손상 발견 | 장치 캐시가 commit 블록 순서를 바꿈 | PLP 없는 장치에서는 금지. 대신 fsync 횟수 줄이기·fast_commit |
| `discard` 마운트 옵션으로 지연 스파이크 | 대량 삭제 때 수백 ms 멈춤 | 동기 discard가 I/O 경로를 막음 | 끄고 유휴 시간 `fstrim` |
| ro 감지를 `/proc/mounts`의 `ro`로만 | 쓰기 불가 상태를 헬스 체크가 놓침 | 6.x ext4는 `emergency_ro` 플래그만 세움 | `emergency_ro` 검사 또는 실제 쓰기 테스트 |
| 작은 쓰기마다 fsync | eMMC 수명 급감, 지연 증가 | fsync당 저널 20 KiB + FLUSH 2회 (예제 8) | 배치·주기 fsync, fast_commit, F2FS |
| F2FS 데이터 파티션을 95%까지 채움 | 쓰기 지연 폭증 | foreground GC가 valid 비율 높은 섹션을 복사 (예제 14: WAF 3.35) | 사용률 상한, 유휴 GC(`gc_urgent`), OP 여유 |
| 정렬 안 된 파티션 | 랜덤 쓰기 성능 저하, 장치 WAF 증가 | FS 블록이 flash page/AU에 걸침 | 1 MiB(또는 4 MiB) 정렬 |
| `/dev/mmcblk0p2`로 root 지정 | SD를 꽂으면 부팅 실패 | 프로브 순서로 mmcblk 번호가 바뀜 | PARTUUID/PARTLABEL, DT aliases |

---

## 11. 면접에서 이렇게 말한다

**Q.** What is a protective MBR and why does GPT need it?

**A.** GPT 디스크의 LBA 0에 있는, 타입 0xEE 엔트리 하나가 디스크 전체를 덮는 가짜 MBR이다. GPT를 모르는 옛 도구가 디스크를 비었다고 보고 덮어쓰는 것을 막는다. 실제 표는 LBA 1의 GPT 헤더(CRC32, 백업 위치)와 LBA 2부터의 128 B 엔트리에 있고, 디스크 끝에 백업이 있다.

> A protective MBR is a legacy MBR in LBA 0 with a single 0xEE entry covering the whole disk, so MBR-only tools see the disk as fully used and don't overwrite it. The real table is the GPT header at LBA 1, which has CRCs and points to a backup copy at the end of the disk, plus the 128-byte partition entries starting at LBA 2. Each entry has a type GUID, a unique GUID — that's the PARTUUID the kernel can boot from — and a UTF-16 name, the PARTLABEL.

**꼬리 질문**: "이미지를 더 큰 eMMC에 쓰면?" → 백업 헤더가 끝에 없으니 `sgdisk -e`로 옮기고, 같은 시작·같은 PARTUUID로 파티션을 늘린 뒤 BLKPG로 커널에 알리고 온라인 resize2fs.

**Q.** Explain ext4 journaling modes. Which one would you use on an embedded device?

**A.** 세 모드 모두 메타데이터는 jbd2로 저널링한다. ordered(기본)는 메타데이터를 commit하기 전에 새로 할당된 데이터 블록을 먼저 써서 크래시 후 남의 옛 데이터가 파일에 보이지 않는다. writeback은 그 순서가 없다. journal은 데이터까지 저널에 써서 두 번 쓴다. 보통 ordered를 쓰고, 중요한 건 모드가 아니라 앱이 fsync를 어디서 부르느냐다. 실측하면 fsync마다 쓰는 데이터는 ordered나 writeback이나 같고(24 KiB, FLUSH 2회), journal은 4 KiB와 FLUSH 하나가 더 붙었다.

> All three journal metadata through jbd2. Ordered, the default, forces newly allocated data blocks to disk before the metadata that references them commits, so after a crash you never see stale blocks from other files. Writeback drops that ordering; journal mode also logs the data, so every block is written twice. I'd keep ordered and focus on the application's fsync pattern — on a test I measured 24 KiB and two cache flushes per 4 KiB fsync in both ordered and writeback, and data=journal added another 4 KiB and a flush. For fsync-heavy logging, fast_commit cut that to 8 KiB.

**꼬리 질문**: "fsync 한 번이 장치에 무엇을 보내나?" → 데이터 쓰기, 저널 디스크립터+메타데이터, PREFLUSH, commit 블록(FUA, 장치가 FUA를 못 하면 post-flush).

**Q.** What happens to an ext4 filesystem on power loss, and how does it recover?

**A.** commit 블록까지 디스크에 남은 트랜잭션은 다음 마운트 때(또는 e2fsck가) 저널에서 replay되고, commit 블록이 없는 트랜잭션은 버려진다. 그래서 메타데이터는 항상 일관된 상태로 돌아오지만, 마지막 commit 이후의 데이터(기본 5초, fsync 안 한 것)는 잃을 수 있다. 이게 성립하려면 장치가 FLUSH/FUA를 정직하게 처리해야 한다 — barrier를 끄거나 캐시를 속이는 장치에서는 깨진다.

> Committed transactions — those whose commit block reached stable media — are replayed from the journal at the next mount or by e2fsck; incomplete ones are discarded. Metadata always comes back consistent, but buffered data newer than the last commit, five seconds by default, can be lost unless the app called fsync. All of this depends on the device honoring flush and FUA; with barriers disabled or a device that lies about its cache, the commit block can land before the blocks it describes.

**Q.** Why is F2FS considered flash-friendly? How does it relate to an SSD's FTL?

**A.** F2FS는 log-structured라서 덮어쓰기를 새 위치에 append하고, 2 MiB 세그먼트 단위로 순차로 채운다. 장치 FTL은 큰 순차 쓰기만 받으니 내부 GC가 쉬워진다. 대신 FS가 GC를 한다. 구조가 FTL과 거의 같다: NAT는 node에 대한 L2P, SIT는 세그먼트별 valid count, SSA는 GC용 역맵, checkpoint는 2벌 핑퐁 메타 스냅샷, 6개 로그로 hot/cold 분리, OP 영역. 실측해 보면 사용률 85%에서 순수 lfs 모드는 FS 층 WAF가 3.35까지 갔고, SSR과 IPU가 그걸 막아 준다.

> F2FS is log-structured: updates are appended to 2 MiB segments, so the device sees large sequential writes and its internal garbage collection gets much cheaper. The file system takes over the GC instead, and its design mirrors an FTL — NAT is an L2P table for node blocks that avoids the wandering-tree problem, SIT is per-segment valid counts, SSA is the reverse map GC needs, checkpoints are ping-ponged, six logs separate hot and cold data, and there's over-provisioning. It isn't free: in a random-overwrite test at 85% utilization, pure LFS mode had a file-system-level write amplification of 3.35, which is why F2FS falls back to slack-space recycling and in-place updates.

**꼬리 질문**: "F2FS를 rootfs로 쓰겠나?" → 쓰기 많은 /data에는 좋고, rootfs는 ro SquashFS/EROFS나 ext4를 선호(부트로더 지원·도구 성숙도·ro 설계).

**Q.** How does the kernel find the root filesystem from `root=PARTUUID=...`?

**A.** 디스크 드라이버가 프로브되면 블록 계층이 파티션 파서(efi 먼저, 그다음 msdos)로 vdaN을 만들고 각 파티션의 GPT unique GUID를 기억한다. init 단계에서 `early_lookup_bdev()`가 `PARTUUID=` 문자열을 그 GUID와 비교해 dev_t를 얻고 마운트한다. 장치가 늦게 나오면 `rootwait`으로 기다린다. 파일시스템 UUID는 커널이 모르므로 그건 initramfs가 해야 한다.

> When the disk driver probes, the block layer runs the partition parsers — EFI first, then msdos — creates the partition devices, and records each GPT unique GUID. During early boot, early_lookup_bdev matches the PARTUUID string against those, gets the dev_t, and mounts it, read-only unless rw is given. rootwait handles slow devices like SD or USB. Filesystem UUIDs aren't known to the kernel; resolving UUID= or LABEL= needs an initramfs.

**Q.** What does `errors=remount-ro` do, and how would you monitor for it?

**A.** 메타데이터 에러나 저널 abort가 나면 ext4가 그 파일시스템의 쓰기를 막는다. 실험에서 I/O 에러를 주입하니 "Aborting journal" → "Remounting filesystem read-only"가 나왔고 앱은 EROFS를 받았다. 다만 6.18에서는 `/proc/mounts`에 `rw,…,emergency_ro`로 보여서 `ro`만 검사하면 놓친다. 그래서 실제 쓰기 테스트와 커널 로그 패턴으로 감시하고, 무인 장비는 errors=panic + 워치독도 고려한다.

> On a metadata error or a journal abort, ext4 stops all writes to that filesystem. When I injected I/O errors with fail_make_request, I saw "Aborting journal" and then "Remounting filesystem read-only", and the app got EROFS. On 6.18, though, /proc/mounts still showed rw with an emergency_ro flag, because ext4 now sets an internal flag instead of SB_RDONLY. So I'd monitor with a real write probe and kernel-log patterns, and for unattended devices consider errors=panic plus a watchdog so the next boot runs e2fsck.

**Q.** discard mount option or periodic fstrim?

**A.** 보통 주기적 fstrim이다. discard 옵션은 블록이 풀릴 때마다(저널 commit 뒤) 작은 discard를 보내는데, 장치에 따라 discard가 느린 동기 명령이라 지연이 튄다. fstrim은 유휴 시간에 free 공간을 큰 덩어리로 한 번에 알린다. 실험에서 기본 마운트는 100 MiB를 지워도 discard가 0건이었고, fstrim 한 번이 487 MiB를 4번의 요청으로 trim했다. TRIM을 안 보내면 FTL 입장에서 OP가 줄어든 것과 같다.

> Usually periodic fstrim. The discard option sends small discards as blocks are freed, after the journal commit, and on many eMMC and SATA devices discard is slow and blocking, so latency spikes. fstrim batches all free space into a few large requests at a quiet time — in my test, deleting 100 MiB with default options sent zero discards, and one fstrim trimmed 487 MiB in four requests. Without TRIM, the FTL effectively loses over-provisioning.

---

## 12. 직접 해보기

1. (개념) 4 KiB 블록 ext4에서 블록 그룹 하나가 왜 정확히 128 MiB인지, 1 KiB 블록이면 몇 MiB인지 계산하라. 정답/힌트: 그룹당 블록 수 = 블록 하나의 비트 수 = 8 × 블록 크기. 1 KiB면 8192블록 × 1 KiB = 8 MiB.
2. (hexdump) 예제 3의 GPT 헤더에서 엔트리 배열 CRC32(0x258)를 Python `zlib.crc32`로 LBA 2~33(16 KiB)에 대해 계산해 일치시켜라. 그다음 헤더 CRC(0x210)를 그 4바이트를 0으로 놓고 92바이트에 대해 계산하라. 정답/힌트: 헤더 CRC는 자기 필드를 0으로 둔 상태에서 계산한다.
3. (C) `sbparse.c`를 확장해 inode 13(`frag.bin`)의 extent 헤더(depth 1)를 읽고, index 엔트리가 가리키는 leaf 블록(2671)에서 첫 extent 3개를 출력하라. `debugfs ex` 결과와 비교. 정답/힌트: index 엔트리는 `ei_block`(4) + `ei_leaf_lo`(4) + `ei_leaf_hi`(2) + 패딩(2).
4. (측정) `fsyncbench`를 `fdatasync`로 바꾸고 append 대신 미리 할당된 파일(`fallocate` 후)의 덮어쓰기로 바꿔라. ordered에서 fsync당 KiB가 어떻게 변하나? 정답/힌트: 크기·블록 할당이 안 바뀌면 fdatasync는 메타데이터 commit을 건너뛸 수 있다 — 저널 쓰기가 줄어드는지 직접 확인.
5. (trace) 예제 9를 `data=journal`로 다시 돌려 FLUSH 3회가 어디서 나오는지 trace로 설명하라. 정답/힌트: `block_rq_issue`의 `FF` 직전·직후 이벤트와 저널 섹터 범위를 본다.
6. (F2FS) 예제 14에 `mode=adaptive`(기본)이지만 `ipu_policy=16`(FSYNC 비트만)을 넣어 다시 측정하라. 큰 볼륨의 기본값에 가까운 설정이다. 정답/힌트: `echo 16 > /sys/fs/f2fs/vda/ipu_policy`, status의 IPU/LFS/SSR 블록 수 비교.
7. (dm) `dm-flakey` 모듈(`Image-fs`에 있음)로 "30초 정상, 5초 쓰기 버림"을 만들어 ext4 저널이 커밋을 잃는 상황을 재현하라. 게스트에 `dmsetup`이 없으니 `DM_DEV_CREATE`/`DM_TABLE_LOAD`/`DM_DEV_SUSPEND` ioctl을 쓰는 작은 C가 필요하다. 정답/힌트: 테이블 문자열은 `0 <sectors> flakey /dev/vda 0 30 5 1 drop_writes`. 이 노트에서는 실행하지 않았다.

---

## 13. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| MBR | Master Boot Record | LBA 0의 512 B, 주 파티션 4개, 32비트 LBA |
| GPT | GUID Partition Table | LBA 1 헤더 + 엔트리 배열 + 끝의 백업, CRC32 |
| protective MBR | 보호 MBR | GPT 디스크 LBA 0의 타입 0xEE 가짜 엔트리 |
| PARTUUID | 파티션 unique GUID | GPT 엔트리의 GUID, `root=PARTUUID=`로 부팅 |
| PARTLABEL | 파티션 이름 | GPT 엔트리의 UTF-16 이름 |
| FS UUID | 파일시스템 UUID | superblock 안의 ID, blkid/initramfs가 해석 |
| super_block | VFS 슈퍼블록 | 마운트된 FS 하나의 커널 객체 |
| address_space | 주소 공간 | inode의 page cache + a_ops |
| writeback | 지연 쓰기 | dirty folio를 flusher가 장치로 내림 |
| delalloc | 지연 할당 | writeback 때 블록을 할당하는 ext4 기능 |
| block group | 블록 그룹 | 비트맵 1블록이 덮는 범위(4 KiB 블록이면 128 MiB) |
| flex_bg | flexible block group | 여러 그룹의 메타데이터를 한곳에 모음 |
| extent | 익스텐트 | (논리 블록, 길이, 물리 블록) run-length 매핑 |
| htree | 해시 트리 디렉터리 | 이름 해시로 디렉터리 블록 색인 |
| metadata_csum | 메타데이터 체크섬 | 메타 블록마다 crc32c |
| jbd2 | Journaling Block Device 2 | ext4의 저널 계층, `kjournald2` 스레드 |
| transaction | 트랜잭션 | 원자적으로 commit되는 handle 묶음 |
| checkpoint (jbd2) | 체크포인트 | 저널 블록을 제자리에 쓰고 저널 공간 회수 |
| fast_commit | 빠른 커밋 | inode 단위 논리 로그로 fsync 비용 감소 |
| REQ_PREFLUSH / REQ_FUA | 플러시 / 강제 단위 접근 | 쓰기 전 캐시 비우기 / 이 쓰기를 바로 영구화 |
| discard / TRIM | 해제 통지 | 이 LBA는 더 이상 유효하지 않다고 장치에 알림 |
| online resize | 온라인 크기 변경 | 마운트된 채 ext4 grow (`EXT4_IOC_RESIZE_FS`) |
| BLKPG | 파티션 ioctl | 파티션 하나를 추가·삭제·크기 변경 |
| LFS | Log-structured FS | 모든 쓰기를 로그 끝에 append |
| segment / section / zone | F2FS 단위 | 2 MiB / GC 단위 / 섹션 묶음 |
| CP / NAT / SIT / SSA | F2FS 메타 영역 | 체크포인트 / node 주소표 / 세그먼트 정보 / 역맵 |
| multi-head logging | 다중 로그 | hot/warm/cold × node/data 6개 로그 |
| IPU / OPU / SSR | 쓰기 방식 | 제자리 / 새 위치 / dirty 세그먼트 빈 구멍에 |
| foreground / background GC | F2FS GC | 쓰기 경로 동기 GC / 유휴 시간 스레드 GC |
| over-provisioning | 예비 공간 | GC 여유분으로 숨긴 세그먼트 |
| emergency_ro | 비상 읽기전용 | 6.x ext4가 에러 시 세우는 플래그 |

---

## 14. 요약 & 체크리스트

블록 장치는 섹터 배열이고, 맨 앞(GPT는 끝에도)의 파티션 테이블이 그것을 나눈다. 커널은 프로브 때 efi → msdos 순으로 파싱해 `vdaN`을 만들고, `root=PARTUUID=`는 GPT 엔트리의 unique GUID로 rootfs를 찾는다. ext4는 블록 그룹·flex_bg·extent tree·htree·crc32c로 디스크를 관리하고, jbd2가 메타데이터를 원형 로그에 먼저 써서 크래시 일관성을 지킨다. fsync 한 번은 데이터 + 저널 20 KiB + FLUSH 2회를 만들었고(ordered), fast_commit은 그것을 8 KiB로 줄였다. 마운트 옵션(`noatime`, `commit=`, `barrier`, `discard`, `errors=`)은 수명·지연·안전성 사이의 손잡이다. F2FS는 FTL과 같은 구조(NAT·SIT·SSA·CP·OP·hot/cold 로그·GC)를 FS 층에 넣어 장치에 순차 쓰기를 보내는 대신 FS 층 GC 비용을 진다 — 사용률 85% lfs 모드에서 WAF 3.35를 직접 측정했다. vfat는 부트 파티션, exFAT는 교환용 SD, btrfs는 스냅샷이 필요할 때.

- [ ] MBR 엔트리와 GPT 헤더·엔트리를 hexdump에서 필드 단위로 읽을 수 있다
- [ ] protective MBR, 백업 GPT, PARTUUID vs FS UUID의 차이를 설명할 수 있다
- [ ] `root=PARTUUID=`가 커널 안에서 어떻게 해석되는지(early_lookup_bdev) 말할 수 있다
- [ ] sgdisk + mkfs 이미지 + dd로 GPT 디스크 이미지를 루프 마운트 없이 만들 수 있다
- [ ] ext4 블록 그룹, flex_bg, 예약 GDT, extent tree, htree를 dumpe2fs/debugfs 출력으로 짚을 수 있다
- [ ] superblock을 raw read로 파싱하고 crc32c를 검증하는 C 코드를 짤 수 있다
- [ ] jbd2 트랜잭션의 commit/checkpoint와 data=ordered/writeback/journal 차이를 설명할 수 있다
- [ ] fsync 한 번이 장치에 보내는 I/O 순서를 tracepoint로 보여 줄 수 있다
- [ ] commit=, barrier, discard vs fstrim, noatime, errors=의 트레이드오프를 말할 수 있다
- [ ] 첫 부팅 rootfs 확장(sgdisk -e, PARTUUID 유지, BLKPG, online resize)을 구현할 수 있다
- [ ] F2FS의 CP/NAT/SIT/SSA/multi-head logging/GC/OP를 SSD FTL에 대응시켜 설명할 수 있다
- [ ] /sys/block/X/stat으로 FS 층 write amplification을 측정하고 해석할 수 있다

## 참고 자료

- docs.kernel.org — ext4 데이터 구조와 알고리즘: https://docs.kernel.org/filesystems/ext4/index.html
- docs.kernel.org — ext4 관리(마운트 옵션): https://docs.kernel.org/admin-guide/ext4.html
- docs.kernel.org — F2FS: https://docs.kernel.org/filesystems/f2fs.html
- docs.kernel.org — Journalling API (jbd2): https://docs.kernel.org/filesystems/journalling.html
- docs.kernel.org — Fault injection (fail_make_request): https://docs.kernel.org/fault-injection/fault-injection.html
- 커널 소스 (6.18.55, 랩 `/var/lab/src/linux-6.18.55`): `block/partitions/efi.c`, `block/partitions/core.c`, `block/early-lookup.c`, `fs/ext4/ext4.h`(`struct ext4_super_block`), `fs/ext4/fsync.c`, `fs/ext4/super.c`(emergency_ro), `fs/jbd2/commit.c`, `fs/jbd2/checkpoint.c`, `fs/f2fs/super.c`(`f2fs_tuning_parameters`), `fs/f2fs/segment.h`(IPU 정책), `fs/f2fs/gc.c` — elixir.bootlin.com 에서도 볼 수 있다
- `Documentation/ABI/testing/sysfs-fs-f2fs` (F2FS sysfs 노브 설명)
- UEFI Specification — "GUID Partition Table (GPT) Disk Layout" 장 (uefi.org)
- man7.org: `fsync(2)`, `fallocate(2)`, `statvfs(3)`, `mount(8)`, `ext4(5)`
- e2fsprogs 매뉴얼: `mke2fs(8)`, `mke2fs.conf(5)`, `tune2fs(8)`, `dumpe2fs(8)`, `debugfs(8)`, `resize2fs(8)`, `e2fsck(8)`; `sgdisk(8)`, `mkfs.f2fs(8)`
- Changman Lee et al., "F2FS: A New File System for Flash Storage", USENIX FAST 2015
- Mendel Rosenblum, John Ousterhout, "The Design and Implementation of a Log-Structured File System", ACM TOCS 1992
- OSTEP 42장 "Crash Consistency: FSCK and Journaling", 43장 "Log-structured File Systems"
