# F12. VFS 내부 — super_block · inode · dentry · file, file_operations가 불리는 경로

> **이 노트를 다 읽으면**: VFS의 네 객체(`super_block`·`inode`·`dentry`·`file`)와 각각의 ops 테이블이 무엇을 담당하는지 그림으로 그릴 수 있다 · `open("/dev/foo")`가 `do_sys_openat2` → `path_openat` → `vfs_open` → `chrdev_open` → 드라이버 `.open`까지 가는 경로를 실제 ftrace 출력으로 설명할 수 있다 · dcache와 RCU-walk/ref-walk가 경로 탐색을 어떻게 빠르게 하는지 말할 수 있다 · libfs로 50줄짜리 pseudo filesystem을 만들어 mount 할 수 있다
> **왜 필요한가**: 드라이버 면접의 단골 "What happens when you open /dev/foo?"는 이 노트 한 장이고, 실무에서 "release가 안 불려요", "두 번째 open에서 상태가 섞여요", "sysfs 파일 하나 읽는 데 왜 이렇게 느려요" 같은 질문의 답이 전부 VFS 객체의 수명과 공유 관계에 있다
> **Don 기준 난이도**: 함수 포인터 테이블로 HW 차이를 숨기는 HAL은 펌웨어에서 늘 했다 / 새로 배울 부분은 "이름(dentry) · 실체(inode) · 열린 세션(file)이 분리된 객체이고 각각 캐시와 참조 카운트로 따로 산다"는 것, 그리고 char device에서 `f_op`가 open 도중 **바뀐다**는 것
> **선행 노트**: A3(fd → OFD → inode, 유저 시점), E3(커널 모듈), E6(`ksys_read` → `vfs_read` → `f_op->read`) — G4(char device), G7(misc device), G8(sysfs·debugfs), J2·J3(블록·메모리 파일시스템)으로 이어진다

---

## 0. 큰 그림 — 이게 왜 필요한가

A3에서는 유저 시점에서 "fd → open file description → inode" 3단 구조를 봤다. 이 노트는 그 3단이 **커널 안에서 실제로 어떤 구조체이고, 누가 만들고, 어떤 함수 포인터를 거쳐 드라이버까지 가는지**를 본다.

VFS(Virtual File System)는 한 문장으로 **"파일처럼 보이는 모든 것에 대한 공통 인터페이스 + 함수 포인터 테이블"**이다. ext4 파일, `/proc/cpuinfo`, `/sys/class/gpio/export`, `/dev/ttyAMA0`, pipe, socket이 모두 같은 `read()`로 읽히는 이유는 VFS가 공통 객체를 만들고, 실제 동작은 각 구현이 채운 ops 테이블로 넘기기 때문이다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="190" y="16" width="300" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="41" text-anchor="middle" font-size="13">유저: open / read / write / ioctl / mmap</text>
  <line x1="340" y1="56" x2="340" y2="88" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="40" y="90" width="600" height="80" rx="8" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="114" text-anchor="middle" font-size="14" font-weight="bold">VFS — 공통 객체 + 함수 포인터 테이블</text>
  <text x="340" y="138" text-anchor="middle" font-size="13">super_block · inode · dentry · file</text>
  <text x="340" y="160" text-anchor="middle" font-size="13">s_op · i_op / i_fop · d_op · f_op</text>
  <rect x="20" y="200" width="116" height="52" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="78" y="222" text-anchor="middle" font-size="13">ext4</text>
  <text x="78" y="242" text-anchor="middle" font-size="12">블록 FS</text>
  <rect x="144" y="200" width="116" height="52" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="202" y="222" text-anchor="middle" font-size="13">procfs</text>
  <text x="202" y="242" text-anchor="middle" font-size="12">seq_file</text>
  <rect x="268" y="200" width="116" height="52" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="326" y="222" text-anchor="middle" font-size="13">sysfs</text>
  <text x="326" y="242" text-anchor="middle" font-size="12">kernfs</text>
  <rect x="392" y="200" width="116" height="52" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="450" y="222" text-anchor="middle" font-size="13">f12fs</text>
  <text x="450" y="242" text-anchor="middle" font-size="12">libfs (§7)</text>
  <rect x="516" y="200" width="116" height="52" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="574" y="222" text-anchor="middle" font-size="13">char 드라이버</text>
  <text x="574" y="242" text-anchor="middle" font-size="12">f12_vfs (§5)</text>
  <line x1="78" y1="170" x2="78" y2="198" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="202" y1="170" x2="202" y2="198" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="326" y1="170" x2="326" y2="198" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="450" y1="170" x2="450" y2="198" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="574" y1="170" x2="574" y2="198" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="40" y="272" width="600" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="291" text-anchor="middle" font-size="12">펌웨어 HAL 비유: struct nand_ops { read_page, program, erase }</text>
  <text x="340" y="308" text-anchor="middle" font-size="12">상위 FTL 코드는 그대로, 칩(벤더)마다 테이블만 다르게 채운다</text>
</svg>
```
그림 1 — VFS는 다형성(polymorphism) 계층이다. 위쪽 syscall 코드는 하나이고, 아래쪽 구현은 자기 ops 테이블만 채운다.

Don의 SSD 펌웨어 경험으로 옮기면 정확히 이렇다. FTL은 `nand->ops->read_page(die, page, buf)`만 부르고, 벤더 A·B의 NAND 차이는 ops 테이블 구현 안에 숨는다. 커널도 `vfs_read()`는 `file->f_op->read(...)`만 부르고, 그게 ext4인지 `/dev/null`인지 Don이 쓴 드라이버인지 모른다. 차이는 두 가지다.

1. 테이블이 **네 종류**다 — 마운트 단위(`super_operations`), 파일 실체 단위(`inode_operations`), 이름 단위(`dentry_operations`), 열린 세션 단위(`file_operations`).
2. 객체마다 **수명이 다르고 캐시된다**. inode와 dentry는 파일을 닫아도 메모리에 남아(캐시) 다음 open을 빠르게 하고, `struct file`은 open마다 새로 생겼다가 마지막 참조가 사라지면 없어진다.

이 노트의 실습은 전부 랩의 **debug 커널**(`/var/lab/out/Image-dbg`, 6.18.55-dbg: ftrace function_graph, kprobe_events, lockdep 포함)을 QEMU `virt`(cortex-a57, `-smp 2`, 512 MB, TCG)로 부팅해서 돌렸다.

| 절 | 내용 | 실습 |
|---|---|---|
| §1 | 네 객체와 ops 테이블, 필드 확인 | 예제 1 (소스·심볼), 예제 2 (pahole) |
| §2 | `open()` 경로 — `do_sys_openat2`부터 `fd_install`까지 | 예제 3 (function_graph) |
| §3 | 경로 탐색 — dcache, RCU-walk, ref-walk | 예제 4 (lookup 함수 추적) |
| §4 | char device — `chrdev_open`이 `f_op`를 바꾼다 | 예제 5 (function_graph, 3가지 장치) |
| §5 | `f12_vfs` 모듈 — file 두 개, inode 하나, dup | 예제 6 |
| §6 | read 경로와 kprobe로 파일 이름 찍기 | 예제 7 (kprobe_events) |
| §7 | 파일시스템 타입, mount, pseudo FS | 예제 8 (statfs), 예제 9 (f12fs), 예제 10 (misc → sysfs·devtmpfs) |
| §8 | dcache 크기 측정 | 예제 11 + 차트 |

> 실행 환경: 소스는 Lima VM `elinux`(aarch64, 호스트 커널 `uname -r` = `7.0.0-38-generic`) 안의 `/var/lab/src/linux-6.18.55`, 구조체 레이아웃·심볼은 debug 빌드 트리 `/var/lab/out/linux-build-dbg`(vmlinux, System.map)에서 봤다. 모듈과 테스트 프로그램 출력은 전부 **게스트 커널 6.18.55-dbg**(BusyBox initramfs)에서 실제로 돌린 결과다. 테스트 프로그램은 VM에서 `gcc -std=gnu11 -Wall -Wextra -O2 -static`으로 빌드했고 경고 0개다. TCG 에뮬레이션 + lockdep 때문에 **시간 값은 실제 보드보다 수십 배 느리다** — 시간은 상대 비교로만 읽는다. 주소·PID·jiffies는 실행마다 다르다.

### 0.1 실습 준비 — 모듈 빌드와 게스트 부팅 스크립트

모듈 두 개(`f12_vfs`, `f12fs`)는 debug 커널 빌드 트리로 빌드한다 (vermagic `6.18.55-dbg`, 일반 `out/Image`에 넣으면 안 된다).

```make
obj-m += f12_vfs.o f12fs.o
```

```sh
limactl shell elinux -- bash -lc 'mkdir -p /var/tmp/F12/mod && cp ~/.elinux_scratch/F12/{f12_vfs.c,f12fs.c,Kbuild} /var/tmp/F12/mod/ \
  && cd /var/tmp/F12/mod && make -j2 -C /var/lab/out/linux-build-dbg M=$PWD modules'
```

게스트 안에서 돌릴 셸 스크립트를 하나 받아 initramfs에 넣고 부팅하는 스크립트(`boot.sh`, VM에서 실행). initramfs의 `/init`이 `LAB_CMD`를 실행한 뒤 전원을 끈다.

```sh
#!/bin/bash
# usage: boot.sh <guest-script.sh>  -> boots dbg kernel with /f12/* and runs the script
set -e
G=$1; N=$(basename $G .sh); W=/var/tmp/F12/run_$N
rm -rf $W && mkdir -p $W/r
cp -a /var/lab/build/rootfs/. $W/r/
mkdir -p $W/r/f12
cp /var/tmp/F12/mod/*.ko /var/tmp/F12/bin/* $W/r/f12/
cp /Users/donh/.elinux_scratch/F12/$G $W/r/f12/t.sh
(cd $W/r && find . | cpio -o -H newc --quiet | gzip) > $W/i.gz
timeout ${TMO:-180} qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp ${SMP:-2} -m 512M -nographic -no-reboot \
  -kernel /var/lab/out/Image-dbg -initrd $W/i.gz \
  -append "console=ttyAMA0 quiet LAB_CMD=sh~/f12/t.sh" > $W/out.txt 2>&1 || echo "qemu rc=$?"
tr -d '\r' < $W/out.txt > /var/tmp/F12/out_$N.txt
```

랩 initramfs의 `/init`은 proc, sysfs, devtmpfs(`/dev`), debugfs를 mount 한다. tracefs는 직접 mount 해야 한다(`mount -t tracefs none /sys/kernel/tracing`).

---

## 1. 네 객체와 ops 테이블

### 1.1 직관 — 도서관을 다시 보자

A3의 도서관 비유를 커널 쪽으로 한 단계 넓힌다.

- **super_block** = 도서관 **분관** 하나. "이 분관은 ext4 방식으로 책을 꽂는다, 블록 크기는 4 KiB, 루트 서가는 여기". mount 한 번에 하나 생긴다.
- **inode** = **책 그 자체**(메타데이터 카드 포함). 크기, 권한, 소유자, 장치 번호, 데이터 위치. 파일 하나에 하나.
- **dentry** = **서가 안내판의 한 줄** "`/dev` 아래 `f12_vfs` → 이 책". 이름과 부모 디렉터리, 그리고 inode를 잇는다. 하드 링크가 있으면 책 하나에 안내판 줄이 여러 개다.
- **file** = **대출 기록 카드**. 누가 어떤 모드로 빌렸고 몇 쪽까지 읽었나. `open()`마다 하나.

핵심은 **이름(dentry)과 실체(inode)가 다른 객체**라는 점이다. FAT 같은 단순한 파일시스템은 디렉터리 엔트리에 파일 정보를 같이 담지만, VFS는 둘을 분리했다. 그래서 하드 링크(이름 둘, 실체 하나), rename(이름만 바꿈), "열어 둔 채로 unlink"(이름은 사라졌지만 실체와 열린 세션은 살아 있음)가 자연스럽게 된다.

```svg
<svg viewBox="0 0 680 430" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="20" width="160" height="36" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="100" y="43" text-anchor="middle" font-size="13">task_struct</text>
  <rect x="20" y="84" width="160" height="36" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="100" y="107" text-anchor="middle" font-size="13">files_struct</text>
  <rect x="20" y="148" width="160" height="80" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="100" y="168" text-anchor="middle" font-size="13">fdtable fd[]</text>
  <text x="100" y="190" text-anchor="middle" font-size="12">fd 4 (open #1)</text>
  <text x="100" y="208" text-anchor="middle" font-size="12">fd 6 = dup(4)</text>
  <line x1="100" y1="56" x2="100" y2="82" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="100" y1="120" x2="100" y2="146" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="215" y="130" width="200" height="130" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="315" y="151" text-anchor="middle" font-size="13" font-weight="bold">struct file</text>
  <text x="315" y="173" text-anchor="middle" font-size="12">f_pos · f_flags · f_mode</text>
  <text x="315" y="193" text-anchor="middle" font-size="12">f_path = {mnt, dentry}</text>
  <text x="315" y="213" text-anchor="middle" font-size="12">f_inode · private_data</text>
  <text x="315" y="233" text-anchor="middle" font-size="12">f_ref (참조 카운트)</text>
  <text x="315" y="252" text-anchor="middle" font-size="12">f_op</text>
  <line x1="180" y1="188" x2="213" y2="188" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="215" y="300" width="200" height="66" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="315" y="321" text-anchor="middle" font-size="13">file_operations</text>
  <text x="315" y="341" text-anchor="middle" font-size="12">.open .read .write .poll</text>
  <text x="315" y="358" text-anchor="middle" font-size="12">.unlocked_ioctl .mmap</text>
  <line x1="315" y1="260" x2="315" y2="298" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="460" y="20" width="200" height="96" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="560" y="41" text-anchor="middle" font-size="13" font-weight="bold">struct dentry</text>
  <text x="560" y="62" text-anchor="middle" font-size="12">d_name = "f12_vfs"</text>
  <text x="560" y="81" text-anchor="middle" font-size="12">d_parent · d_inode · d_sb</text>
  <text x="560" y="100" text-anchor="middle" font-size="12">d_op (dentry_operations)</text>
  <rect x="460" y="150" width="200" height="110" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="560" y="171" text-anchor="middle" font-size="13" font-weight="bold">struct inode</text>
  <text x="560" y="192" text-anchor="middle" font-size="12">i_ino · i_mode · i_size</text>
  <text x="560" y="211" text-anchor="middle" font-size="12">i_rdev = 10:258</text>
  <text x="560" y="230" text-anchor="middle" font-size="12">i_op · i_fop · i_cdev</text>
  <text x="560" y="249" text-anchor="middle" font-size="12">i_sb · i_mapping</text>
  <rect x="460" y="300" width="200" height="96" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="560" y="321" text-anchor="middle" font-size="13" font-weight="bold">struct super_block</text>
  <text x="560" y="342" text-anchor="middle" font-size="12">s_type = devtmpfs</text>
  <text x="560" y="361" text-anchor="middle" font-size="12">s_root · s_magic · s_dev</text>
  <text x="560" y="380" text-anchor="middle" font-size="12">s_op (super_operations)</text>
  <line x1="415" y1="160" x2="458" y2="80" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="430" y="112" text-anchor="middle" font-size="12">dentry</text>
  <line x1="415" y1="210" x2="458" y2="210" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="560" y1="116" x2="560" y2="148" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="566" y="137" font-size="12">d_inode</text>
  <line x1="560" y1="260" x2="560" y2="298" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="566" y="284" font-size="12">i_sb</text>
  <text x="20" y="270" font-size="12">프로세스마다</text>
  <text x="20" y="288" font-size="12">(스레드는 공유)</text>
  <text x="215" y="410" font-size="12">open()마다 1개, 수명은 f_ref</text>
  <text x="460" y="420" font-size="12">mount마다 1개</text>
</svg>
```
그림 2 — 실습 §5의 실제 상태. fd 4와 fd 6(dup)은 같은 `struct file`을 가리키고, file은 `f_path.dentry`와 `f_inode`로 이름과 실체를 붙잡고, inode는 `i_sb`로 자기가 속한 마운트의 `super_block`을 안다. `/dev`는 devtmpfs이므로 `s_type`이 devtmpfs다.

### 1.2 정의 — 네 객체 한 장 정리

| 객체 | 언제 생기나 | 언제 없어지나 | 핵심 필드 | ops 테이블 | 캐시 |
|---|---|---|---|---|---|
| `struct super_block` | mount 때 (`fill_super`) | 마지막 umount | `s_type`, `s_op`, `s_root`, `s_magic`, `s_fs_info` | `super_operations` | 마운트당 1개 |
| `struct inode` | 처음 lookup 될 때 (디스크에서 읽거나 pseudo FS가 생성) | 참조 0 + 메모리 압박 또는 삭제 | `i_ino`, `i_mode`, `i_rdev`, `i_size`, `i_op`, `i_fop`, `i_sb`, `i_mapping`, `i_private` | `inode_operations` + 기본 `file_operations`(`i_fop`) | inode cache (해시) |
| `struct dentry` | 경로 성분을 처음 lookup 할 때 | 참조 0이면 LRU에 남고, 메모리 압박 때 회수 | `d_name`, `d_parent`, `d_inode`(NULL이면 negative), `d_op`, `d_sb` | `dentry_operations` | dcache (해시 + LRU) |
| `struct file` | `open()`마다 | 마지막 `fput()` (모든 fd·mmap·참조가 사라질 때) | `f_op`, `f_pos`, `f_flags`, `f_mode`, `f_path`, `f_inode`, `private_data`, `f_ref` | `file_operations` | 캐시 없음 (slab `filp`) |

ops 테이블별로 "누가 부르나"를 보면 역할이 갈린다.

| ops 테이블 | 대표 멤버 | VFS가 부르는 장면 | 드라이버 작성자가 채우나 |
|---|---|---|---|
| `super_operations` | `alloc_inode`, `evict_inode`, `statfs`, `sync_fs`, `put_super`, `show_options` | inode 할당·회수, `statfs()`, `sync`, umount, `/proc/mounts` 출력 | 파일시스템을 만들 때만 (libfs가 `simple_super_operations` 제공) |
| `inode_operations` | `lookup`, `create`, `mkdir`, `unlink`, `rename`, `permission`, `getattr`, `setattr`, `get_link` | 경로 탐색, 파일 생성·삭제, `stat()`, `chmod` | 파일시스템을 만들 때만 |
| `dentry_operations` | `d_revalidate`, `d_hash`, `d_compare`, `d_delete`, `d_release`, `d_dname` | dcache hit를 믿어도 되는지 확인(NFS 등), 대소문자 무시 비교(vfat) | 거의 안 씀 |
| `file_operations` | `open`, `release`, `read`, `write`, `read_iter`, `write_iter`, `llseek`, `poll`, `unlocked_ioctl`, `mmap`, `fsync` | fd에 대한 거의 모든 syscall | **char 드라이버의 본업** (G4) |

드라이버 작성자 입장에서 정리하면: **char 드라이버는 `file_operations` 하나만 채운다.** 나머지 세 객체는 devtmpfs·sysfs 같은 기존 파일시스템이 대신 만들어 준다. 파일시스템이나 pseudo FS를 직접 만들 때(§7)만 `super_operations`·`inode_operations`까지 내려간다.

`i_fop`가 왜 inode에 있는지가 중요하다. inode는 "이 파일을 열면 어떤 `file_operations`로 시작할지"를 들고 있다. open 때 `do_dentry_open()`이 `file->f_op = inode->i_fop`로 복사한다. 정규 파일이면 ext4의 `ext4_file_operations`, 장치 파일이면 `init_special_inode()`가 넣어 둔 `def_chr_fops` — 그리고 이 `def_chr_fops`는 **진짜 드라이버를 찾아서 `f_op`를 바꿔 끼우는 일**만 한다(§4).

### 1.3 예제 1 — 함수 이름은 소스와 심볼에서 확인한다

무엇을 확인하나: 이 노트가 쓰는 open 경로 함수들이 랩 커널 6.18.55에 실제로 있는지, 그리고 VM 호스트 커널 7.0과 이름이 다른지.

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/src/linux-6.18.55 && grep -n "^static int do_sys_openat2\|^struct file \*do_filp_open\|^static struct file \*path_openat\|^static int do_open(\|^int vfs_open\|^static int do_dentry_open\|^static int link_path_walk\|^static int chrdev_open\|^const struct file_operations def_chr_fops\|^static int misc_open" fs/*.c drivers/char/misc.c'
```

```text
drivers/char/misc.c:118:static int misc_open(struct inode *inode, struct file *file)
fs/char_dev.c:373:static int chrdev_open(struct inode *inode, struct file *filp)
fs/char_dev.c:452:const struct file_operations def_chr_fops = {
fs/namei.c:2441:static int link_path_walk(const char *name, struct nameidata *nd)
fs/namei.c:3931:static int do_open(struct nameidata *nd,
fs/namei.c:4118:static struct file *path_openat(struct nameidata *nd,
fs/namei.c:4157:struct file *do_filp_open(int dfd, struct filename *pathname,
fs/open.c:903:static int do_dentry_open(struct file *f,
fs/open.c:1092:int vfs_open(const struct path *path, struct file *file)
fs/open.c:1420:static int do_sys_openat2(int dfd, const char __user *filename,
```

같은 이름을 VM 호스트 커널 7.0의 `/proc/kallsyms`와 랩 6.18의 `System.map`에서 찾아본다.

```sh
limactl shell elinux -- bash -lc 'grep -wE "do_file_open|do_filp_open|do_sys_openat2|path_openat|do_open|vfs_open|do_dentry_open|chrdev_open|misc_open|link_path_walk|lookup_fast" /proc/kallsyms | awk "{print \$2, \$3}"; echo ---; grep -wE "do_file_open|do_filp_open|do_sys_openat2|path_openat|do_open|vfs_open|do_dentry_open|chrdev_open|misc_open|link_path_walk|lookup_fast|open_last_lookups" /var/lab/out/linux-build-dbg/System.map | awk "{print \$2, \$3}"'
```

```text
t do_dentry_open
T vfs_open
t do_sys_openat2
t chrdev_open
t lookup_fast
t do_open
t link_path_walk
t path_openat
T do_file_open
t misc_open
---
t do_dentry_open
T vfs_open
t do_sys_openat2
t chrdev_open
t lookup_fast
t link_path_walk
t path_openat
T do_filp_open
t do_open
t misc_open
```

출력에서 볼 것:

- **6.18에서는 `do_filp_open`, 7.0에서는 `do_file_open`**이다. A1·C1 노트에서 7.0 호스트로 본 이름이 `do_file_open`이었던 이유가 이것이다 — 같은 역할의 함수가 이름만 바뀌었다. 면접에서는 "do_filp_open (renamed do_file_open in recent kernels)"처럼 말하면 안전하다.
- `open_last_lookups`는 System.map에 없다. 그리고 6.18의 `do_open` 심볼은 **namei.c의 `do_open`이 아니다**. 아래처럼 gdb로 보면 namei.c의 `do_open` 본문은 `path_openat` 안에 inline 되었고, 심볼 `do_open`은 NFS(`fs/nfs/dir.c`)의 다른 static 함수다.

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/out/linux-build-dbg && gdb -batch -ex "info line do_open" vmlinux 2>&1 | tail -2'
```

```text
Line 3935 of "/var/lab/src/linux-6.18.55/fs/namei.c" starts at address 0xffff800080507458 <path_openat+448> and ends at 0xffff80008050745c <path_openat+452>.
Line 2042 of "/var/lab/src/linux-6.18.55/fs/nfs/dir.c" starts at address 0xffff80008069cbd0 <do_open> and ends at 0xffff80008069cbd8 <do_open+8>.
```

함정: 소스의 함수 이름이 그대로 ftrace·kprobe 대상이 되는 것은 아니다. **static 함수는 inline 되면 심볼이 사라진다.** 추적할 함수는 `System.map`이나 `/sys/kernel/tracing/available_filter_functions`에서 먼저 확인한다.

### 1.4 예제 2 — `struct file`의 실제 레이아웃 (pahole)

무엇을 확인하나: `struct file`에 A3에서 말로만 했던 필드(`f_pos`, `f_flags`, `f_op`, `private_data`, `f_ref`)가 실제로 어디 있는지. 이 오프셋은 §6 kprobe에서 그대로 쓴다.

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/out/linux-build-dbg && pahole -C file vmlinux | head -60'
```

```text
struct file {
	spinlock_t                 f_lock;               /*     0     0 */

	/* XXX 72 bytes hole, try to pack */

	/* --- cacheline 1 boundary (64 bytes) was 8 bytes ago --- */
	fmode_t                    f_mode;               /*    72     4 */

	/* XXX 4 bytes hole, try to pack */

	const struct file_operations  * f_op;            /*    80     8 */
	struct address_space *     f_mapping;            /*    88     8 */
	void *                     private_data;         /*    96     8 */
	struct inode *             f_inode;              /*   104     8 */
	unsigned int               f_flags;              /*   112     4 */
	unsigned int               f_iocb_flags;         /*   116     4 */
	const struct cred  *       f_cred;               /*   120     8 */
	/* --- cacheline 2 boundary (128 bytes) --- */
	struct fown_struct *       f_owner;              /*   128     8 */
	union {
		const struct path  f_path;               /*   136     0 */
		struct path        __f_path;             /*   136     0 */
	};                                               /*   136    16 */
	union {
		struct mutex       f_pos_lock;           /*   152     0 */
		u64                f_pipe;               /*   152     8 */
	};                                               /*   152   160 */
	/* --- cacheline 4 boundary (256 bytes) was 56 bytes ago --- */
	loff_t                     f_pos;                /*   312     8 */
	/* --- cacheline 5 boundary (320 bytes) --- */
	void *                     f_security;           /*   320     8 */
	errseq_t                   f_wb_err;             /*   328     4 */
	errseq_t                   f_sb_err;             /*   332     4 */
	struct hlist_head *        f_ep;                 /*   336     8 */
	union {
		struct callback_head f_task_work __attribute__((__aligned__(8))); /*   344     0 */
		struct llist_node  f_llist;              /*   344     0 */
		struct file_ra_state f_ra;               /*   344    32 */
		freeptr_t          f_freeptr;            /*   344     0 */
	} __attribute__((__aligned__(8)));               /*   344    32 */
	file_ref_t                 f_ref;                /*   376     0 */

	/* size: 384, cachelines: 6, members: 19 */
	/* sum members: 300, holes: 2, sum holes: 76 */
	/* padding: 8 */
	/* forced alignments: 1 */
} __attribute__((__aligned__(8)));
```

출력에서 볼 것:

- `f_lock`(spinlock)이 72바이트, `f_pos_lock`(mutex)이 160바이트를 차지한다. **lockdep(PROVE_LOCKING) 때문에 락 구조체가 커진 debug 커널**이라서다. 일반 커널에서 `struct file`은 이보다 훨씬 작고 오프셋도 다르다(소스 주석에는 `f_path`가 cacheline 1 = 64바이트 근처로 적혀 있다). pahole이 일부 타입 크기를 0으로 찍는 것은 DWARF 표현 문제이고, 오프셋은 맞다(§6에서 실제로 읽어서 검증한다).
- 참조 카운트가 `atomic_long_t f_count`가 아니라 **`file_ref_t f_ref`**다. A3 §11.4에서 말한 대로 6.13부터 바뀌었다. 드라이버는 이 필드를 직접 만지지 않고 `get_file()`/`fput()`/`file_count()`를 쓴다.
- 소스 정의는 `} __randomize_layout`이다. `CONFIG_RANDSTRUCT`를 켜면 필드 순서가 빌드마다 섞인다(랩은 `CONFIG_RANDSTRUCT_NONE=y`). **드라이버가 오프셋을 하드코딩하면 안 되는 이유**다.

같은 방식으로 dentry와 inode의 필요한 필드만 본다.

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/out/linux-build-dbg && pahole -C qstr vmlinux; pahole -C dentry vmlinux | grep -E "d_name|d_inode|d_parent|d_op|d_sb|size:|d_hash|d_flags|d_seq"; pahole -C inode vmlinux | grep -E "i_rdev|i_ino|i_fop|size:"'
```

```text
struct qstr {
	union {
		struct {
			u32        hash;                 /*     0     4 */
			u32        len;                  /*     4     4 */
		};                                       /*     0     8 */
		u64                hash_len;             /*     0     8 */
	};                                               /*     0     8 */
	const unsigned char  *     name;                 /*     8     8 */

	/* size: 16, cachelines: 1, members: 2 */
	/* last cacheline: 16 bytes */
};

	unsigned int               d_flags;              /*     0     4 */
	seqcount_spinlock_t        d_seq;                /*     8     0 */
	struct hlist_bl_node       d_hash;               /*    72     0 */
	struct dentry *            d_parent;             /*    88     8 */
		struct qstr        __d_name;             /*    96    16 */
		const struct qstr  d_name;               /*    96    16 */
	struct inode *             d_inode;              /*   112     8 */
	const struct dentry_operations  * d_op;          /*   160     8 */
	struct super_block *       d_sb;                 /*   168     8 */
	/* size: 328, cachelines: 6, members: 16 */
	long unsigned int          i_ino;                /*    64     8 */
	dev_t                      i_rdev;               /*    76     4 */
		const struct file_operations  * i_fop;   /*   552     8 */
	/* size: 1208, cachelines: 19, members: 55 */
```

출력에서 볼 것: dentry의 이름은 `struct qstr`(hash + len + name 포인터)다. 해시를 미리 계산해 들고 있어서 dcache 비교가 빠르다(§3). dentry 앞부분(`d_flags`, `d_seq`, `d_hash`, `d_parent`, `d_name`, `d_inode`)은 소스 주석대로 "RCU lookup touched fields"로 묶여 있다 — 경로 탐색이 가장 자주 읽는 필드를 같은 캐시라인에 모은 것이다. Don이 SSD 펌웨어에서 hot 필드를 TCM이나 같은 캐시라인에 모으던 것과 같은 설계다.

---

## 2. `open()`의 경로 — `do_sys_openat2`부터 `fd_install`까지

### 2.1 직관 — "번호 예약 → 이름 따라가기 → 세션 만들기 → 번호에 꽂기"

`open()`이 하는 일을 펌웨어식으로 쓰면 네 단계다.

1. **빈 fd 번호를 예약**한다 (아직 아무것도 안 꽂음).
2. **경로 문자열을 따라가** 마지막 이름의 dentry와 inode를 찾는다 (§3).
3. **`struct file`을 만들고** inode의 `i_fop`를 복사한 뒤 `f_op->open`을 부른다 (드라이버가 처음 불리는 지점).
4. 성공하면 **예약한 번호에 file을 꽂는다**(`fd_install`). 실패하면 예약을 취소한다.

번호를 먼저 예약하고 맨 마지막에 꽂는 이유: 다른 스레드가 아직 반쯤 만들어진 file을 그 fd로 보면 안 되기 때문이다. `fd_install()` 전까지 이 file은 이 스레드만 안다.

```svg
<svg viewBox="0 0 680 470" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="14" width="260" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="150" y="34" text-anchor="middle" font-size="13">openat(AT_FDCWD, path, flags)</text>
  <text x="300" y="34" font-size="12">유저 → svc #0 → __arm64_sys_openat (E6)</text>
  <rect x="20" y="58" width="260" height="30" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="150" y="78" text-anchor="middle" font-size="13">do_sys_openat2()</text>
  <text x="300" y="78" font-size="12">getname: 경로 복사 · get_unused_fd_flags: 번호 예약</text>
  <rect x="20" y="102" width="260" height="30" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="150" y="122" text-anchor="middle" font-size="13">do_filp_open()</text>
  <text x="300" y="122" font-size="12">LOOKUP_RCU로 1차 → -ECHILD면 ref-walk로 재시도</text>
  <rect x="20" y="146" width="260" height="30" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="150" y="166" text-anchor="middle" font-size="13">path_openat()</text>
  <text x="300" y="166" font-size="12">alloc_empty_file: struct file 할당</text>
  <rect x="20" y="190" width="260" height="30" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="150" y="210" text-anchor="middle" font-size="13">path_init + link_path_walk()</text>
  <text x="300" y="210" font-size="12">마지막 성분 직전까지 디렉터리 따라가기 (§3)</text>
  <rect x="20" y="234" width="260" height="30" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="150" y="254" text-anchor="middle" font-size="13">open_last_lookups() [inline]</text>
  <text x="300" y="254" font-size="12">마지막 이름: lookup_fast, 없으면 lookup_open</text>
  <rect x="20" y="278" width="260" height="30" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="150" y="298" text-anchor="middle" font-size="13">do_open() [inline]</text>
  <text x="300" y="298" font-size="12">complete_walk · may_open (권한 검사)</text>
  <rect x="20" y="322" width="260" height="30" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="150" y="342" text-anchor="middle" font-size="13">vfs_open() → do_dentry_open()</text>
  <text x="300" y="342" font-size="12">f_op = fops_get(inode->i_fop)</text>
  <rect x="20" y="366" width="260" height="30" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="150" y="386" text-anchor="middle" font-size="13">f_op->open(inode, file)</text>
  <text x="300" y="386" font-size="12">장치 파일이면 chrdev_open (§4) → 드라이버 .open</text>
  <rect x="20" y="410" width="260" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="150" y="430" text-anchor="middle" font-size="13">fd_install(fd, file)</text>
  <text x="300" y="430" font-size="12">fdtable[fd] = file → 유저에게 fd 반환</text>
  <line x1="150" y1="44" x2="150" y2="56" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="150" y1="88" x2="150" y2="100" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="150" y1="132" x2="150" y2="144" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="150" y1="176" x2="150" y2="188" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="150" y1="220" x2="150" y2="232" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="150" y1="264" x2="150" y2="276" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="150" y1="308" x2="150" y2="320" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="150" y1="352" x2="150" y2="364" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="150" y1="396" x2="150" y2="408" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="20" y="460" font-size="12">[inline] = 6.18-dbg 빌드에서 별도 심볼 없이 path_openat 안에 펼쳐짐 (예제 1)</text>
</svg>
```
그림 3 — `open()`의 커널 경로 (6.18.55 기준 이름). 초록은 준비, 주황은 경로 탐색, 빨강은 file 생성과 드라이버 호출, 파랑은 fd 테이블.

### 2.2 소스로 보기 — 랩 커널 그대로

`do_sys_openat2()`는 위 1·4단계를 그대로 보여 준다.

```c
static int do_sys_openat2(int dfd, const char __user *filename,
			  struct open_how *how)
{
	struct open_flags op;
	struct filename *tmp;
	int err, fd;

	err = build_open_flags(how, &op);
	if (unlikely(err))
		return err;

	tmp = getname(filename);
	if (IS_ERR(tmp))
		return PTR_ERR(tmp);

	fd = get_unused_fd_flags(how->flags);
	if (likely(fd >= 0)) {
		struct file *f = do_filp_open(dfd, tmp, &op);
		if (IS_ERR(f)) {
			put_unused_fd(fd);
			fd = PTR_ERR(f);
		} else {
			fd_install(fd, f);
		}
	}
	putname(tmp);
	return fd;
}
```

`do_filp_open()`은 **같은 탐색을 최대 세 번** 시도한다. 먼저 락 없이(RCU-walk), 안 되면 참조 카운트를 잡으면서(ref-walk), NFS 같은 곳에서 캐시가 낡았으면(`-ESTALE`) 재검증하면서.

```c
struct file *do_filp_open(int dfd, struct filename *pathname,
		const struct open_flags *op)
{
	struct nameidata nd;
	int flags = op->lookup_flags;
	struct file *filp;

	set_nameidata(&nd, dfd, pathname, NULL);
	filp = path_openat(&nd, op, flags | LOOKUP_RCU);
	if (unlikely(filp == ERR_PTR(-ECHILD)))
		filp = path_openat(&nd, op, flags);
	if (unlikely(filp == ERR_PTR(-ESTALE)))
		filp = path_openat(&nd, op, flags | LOOKUP_REVAL);
	restore_nameidata();
	return filp;
}
```

`path_openat()`의 핵심 부분 — `struct file`은 경로 탐색 **전에** 할당된다.

```c
	file = alloc_empty_file(op->open_flag, current_cred());
	...
		const char *s = path_init(nd, flags);
		while (!(error = link_path_walk(s, nd)) &&
		       (s = open_last_lookups(nd, file, op)) != NULL)
			;
		if (!error)
			error = do_open(nd, file, op);
		terminate_walk(nd);
```

`while` 루프는 심볼릭 링크 때문이다. 마지막 성분이 symlink면 `open_last_lookups()`가 링크 내용을 새 경로로 돌려주고, 다시 `link_path_walk()`가 그것을 따라간다.

마지막으로 `do_dentry_open()`(fs/open.c)이 file을 채우고 드라이버를 부른다. 드라이버 작성자에게 중요한 줄만 남겼다.

```c
	path_get(&f->f_path);
	f->f_inode = inode;
	f->f_mapping = inode->i_mapping;
	...
	f->f_op = fops_get(inode->i_fop);
	...
	/* normally all 3 are set; ->open() can clear them if needed */
	f->f_mode |= FMODE_LSEEK | FMODE_PREAD | FMODE_PWRITE;
	if (!open)
		open = f->f_op->open;
	if (open) {
		error = open(inode, f);
		if (error)
			goto cleanup_all;
	}
	f->f_mode |= FMODE_OPENED;
	if ((f->f_mode & FMODE_READ) &&
	     likely(f->f_op->read || f->f_op->read_iter))
		f->f_mode |= FMODE_CAN_READ;
	if ((f->f_mode & FMODE_WRITE) &&
	     likely(f->f_op->write || f->f_op->write_iter))
		f->f_mode |= FMODE_CAN_WRITE;
	...
	f->f_flags &= ~(O_CREAT | O_EXCL | O_NOCTTY | O_TRUNC);
```

여기서 드라이버 작성자가 알아야 할 것 세 가지:

- `fops_get()`은 `try_module_get(fops->owner)`다. **`.owner = THIS_MODULE`을 빼먹으면** 열린 fd가 있는데 `rmmod`가 성공하고, 다음 read에서 사라진 코드로 점프한다.
- `FMODE_CAN_READ`/`FMODE_CAN_WRITE`는 **open이 끝난 뒤의 `f_op`** 를 보고 정한다. 그래서 char device처럼 open 도중 `f_op`를 바꿔도 결과가 맞다. 그리고 `.write`가 없는 파일에 write 하면 `vfs_write()`가 `-EINVAL`을 돌려준다 (§7 f12fs에서 실제로 본다).
- `.open`이 실패하면(음수 반환) file은 버려지고 `.release`는 **불리지 않는다**. open에서 할당한 것은 open의 에러 경로에서 직접 풀어야 한다.

### 2.3 예제 3 — function_graph로 본 `/dev/null` open

무엇을 확인하나: 그림 3의 경로가 실제 커널에서 그대로 실행되는지. `do_sys_openat2`를 루트로, 깊이 5까지만 기록한다.

추적 대상 프로세스만 기록하기 위해 작은 도우미 `t_trace`를 썼다. 자기 PID를 `set_ftrace_pid`에 쓰고, 경로 하나를 open(필요하면 read) 하는 동안만 `tracing_on`을 켠다.

```c
/* t_trace: 자기 pid만 ftrace 하도록 걸고, open/read/close 한 번만 기록 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define T "/sys/kernel/tracing/"
static void wr(const char *f, const char *v)
{
	int fd = open(f, O_WRONLY | O_TRUNC);
	if (fd < 0 || write(fd, v, strlen(v)) < 0) { perror(f); exit(1); }
	close(fd);
}
int main(int argc, char **argv)
{
	char pid[16], b[64];
	if (argc < 2) { fprintf(stderr, "usage: %s path [read]\n", argv[0]); return 1; }
	snprintf(pid, sizeof(pid), "%d", getpid());
	wr(T "set_ftrace_pid", pid);
	int on = open(T "tracing_on", O_WRONLY);	/* 미리 열어 두고 1/0만 쓴다 */
	if (on < 0) { perror("tracing_on"); return 1; }
	if (write(on, "1", 1) != 1) { perror("on"); return 1; }
	int fd = open(argv[1], O_RDONLY | O_NONBLOCK);
	if (fd >= 0 && argc > 2 && read(fd, b, sizeof(b)) < 0) perror("read");
	if (write(on, "0", 1) != 1) { perror("off"); return 1; }
	printf("t_trace: open(%s) = %d\n", argv[1], fd);
	if (fd >= 0) close(fd);
	return 0;
}
```

게스트 스크립트(`g2_trace.sh`)의 핵심. lockdep·preempt 카운터 같은 잡음 함수는 `set_ftrace_notrace`로 뺐다.

```sh
cd /f12
insmod f12_vfs.ko
mount -t tracefs none /sys/kernel/tracing
T=/sys/kernel/tracing
echo 0 > $T/tracing_on
for f in lock_acquire lock_release lock_is_held_type lock_acquired lock_contended 'rcu_read_lock_*held' debug_lockdep_rcu_enabled rcu_lockdep_current_cpu_online __rcu_read_lock __rcu_read_unlock preempt_count_add preempt_count_sub __might_sleep __might_resched __might_fault; do echo "$f" >> $T/set_ftrace_notrace || echo "notrace fail $f"; done
echo nofuncgraph-irqs > $T/trace_options
echo 'unwind_find_stack*' >> $T/set_ftrace_notrace
for g in "vfs_open:5" "do_sys_openat2:5"; do
  fn=${g%%:*}; d=${g##*:}
  for p in /dev/null /dev/f12_vfs /dev/ttyAMA0; do
    echo nop > $T/current_tracer
    echo $fn > $T/set_graph_function
    echo $d > $T/max_graph_depth
    echo function_graph > $T/current_tracer
    echo > $T/trace
    ./t_trace $p
    echo "### graph=$fn depth=$d path=$p"
    grep -v "^#" $T/trace | sed -e 's/^ *[0-9])//'
  done
done
```

```sh
limactl shell elinux -- bash -lc 'TMO=300 ~/.elinux_scratch/F12/boot.sh g2_trace.sh; cat /var/tmp/F12/out_g2_trace.txt'
```

`/dev/null`, 루트 `do_sys_openat2`, 깊이 5 (출력 그대로, 앞의 `mutex_unlock`은 `tracing_on`에 "1"을 쓴 write의 끝부분이라 뺐다):

```text
### graph=do_sys_openat2 depth=5 path=/dev/null
               |  do_sys_openat2() {
               |    getname_flags() {
               |      getname_flags.part.0() {
               |        kmem_cache_alloc_noprof() {
 + 14.208 us   |          fs_reclaim_acquire();
   3.584 us    |          fs_reclaim_release();
 + 35.904 us   |        }
 + 50.912 us   |      }
 + 56.864 us   |    }
               |    get_unused_fd_flags() {
               |      alloc_fd() {
               |        _raw_spin_lock() {
   2.448 us    |          do_raw_spin_trylock();
 + 17.744 us   |        }
               |        _raw_spin_unlock() {
   2.112 us    |          do_raw_spin_unlock();
 + 10.880 us   |        }
 + 39.584 us   |      }
 + 45.536 us   |    }
               |    do_filp_open() {
               |      path_openat() {
               |        alloc_empty_file() {
 + 11.040 us   |          kmem_cache_alloc_noprof();
 + 16.544 us   |          init_file();
 + 38.512 us   |        }
               |        path_init() {
 + 15.760 us   |          nd_jump_root();
 + 25.664 us   |        }
               |        link_path_walk() {
   5.872 us    |          inode_permission();
 + 40.800 us   |          walk_component();
   5.120 us    |          inode_permission();
   2.880 us    |          make_vfsuid();
 + 79.168 us   |        }
               |        lookup_fast() {
 + 16.752 us   |          __d_lookup_rcu();
 + 22.416 us   |        }
   2.288 us    |        step_into();
               |        complete_walk() {
 + 35.712 us   |          try_to_unlazy();
 + 42.368 us   |        }
               |        may_open() {
 + 52.912 us   |          inode_permission();
 + 60.544 us   |        }
               |        vfs_open() {
 # 1226.096 us |          do_dentry_open();
 # 1238.880 us |        }
   3.296 us    |        security_file_post_open();
               |        terminate_walk() {
 + 22.864 us   |          dput();
   9.488 us    |          mntput();
 + 42.368 us   |        }
 # 1592.928 us |      }
 # 1599.552 us |    }
   9.328 us    |    fd_install();
               |    putname() {
   5.552 us    |      kmem_cache_free();
 + 11.248 us   |    }
 # 1753.040 us |  }
```

출력에서 볼 것:

- 그림 3의 순서 그대로다: `getname_flags`(경로 복사) → `get_unused_fd_flags`(번호 예약) → `do_filp_open` → `path_openat` → `alloc_empty_file` → `path_init` → `link_path_walk` → … → `vfs_open` → `fd_install`.
- `path_openat` 바로 아래에 `lookup_fast`, `step_into`, `complete_walk`, `may_open`, `vfs_open`이 보이고 `open_last_lookups`·`do_open`은 없다. 예제 1에서 본 **inline** 때문이다. 앞의 둘은 `open_last_lookups`의 몸통, 뒤의 셋은 `do_open`의 몸통이다.
- `link_path_walk`는 `/dev/null`의 `dev`까지만 처리하고(`walk_component` 한 번), 마지막 성분 `null`은 `lookup_fast` → `__d_lookup_rcu`로 dcache에서 찾았다. 파일시스템(devtmpfs)의 `->lookup`은 한 번도 불리지 않았다 — **dcache hit**.
- `complete_walk` → `try_to_unlazy`: RCU-walk로 찾은 결과를 **참조 카운트를 잡은 정상 경로로 확정**하는 단계다(§3).
- 시간: 전체 1.75 ms 중 1.2 ms가 `do_dentry_open`이다. TCG + lockdep 환경이라 절대값은 의미 없지만, 비율로는 "탐색보다 open 콜백 쪽이 무겁다"는 것을 보여 준다.

함정: function_graph는 **함수 진입/복귀마다** 기록하므로 오버헤드가 크다. 이 출력의 µs 값은 추적 비용이 섞인 값이다. 실제 지연 측정에는 함수 몇 개만 고른 `function` 트레이서나 kprobe 타임스탬프 차이를 쓴다(M5 ftrace, M7 eBPF).

---

## 3. 경로 탐색 — dcache, RCU-walk, ref-walk

### 3.1 직관 — 경로는 "디렉터리 하나씩 해시 테이블 조회"

`/sys/class/tty/ttyAMA0/dev`를 여는 것은 펌웨어로 치면 이렇다:

```c
node = root;
for (name in "sys", "class", "tty", "ttyAMA0", "dev")
	node = hash_lookup(node, name);   /* (부모 포인터, 이름) → 자식 */
```

키는 **(부모 dentry, 이름)**이고, 테이블은 전역 해시 테이블 하나(`dentry_hashtable`)다. 부팅 로그에 크기가 찍힌다.

```text
[    0.000000] Dentry cache hash table entries: 65536 (order: 7, 524288 bytes, linear)
[    0.000000] Inode-cache hash table entries: 32768 (order: 6, 262144 bytes, linear)
[    0.101535] Mount-cache hash table entries: 1024 (order: 1, 8192 bytes, linear)
[    0.101629] Mountpoint-cache hash table entries: 1024 (order: 1, 8192 bytes, linear)
```

(게스트 `dmesg | grep -E "cache hash table entries"` 출력에서 앞 4줄. 512 MB 게스트라 작다 — 해시 크기는 메모리 크기에 비례해 정해진다.)

이름이 해시에 없으면(**dcache miss**) 그때 파일시스템의 `dir->i_op->lookup()`을 불러 디스크(또는 kernfs 트리)에서 찾고, 새 dentry를 만들어 해시에 넣는다. 다음부터는 hit다. "없다"는 결과도 **negative dentry**(`d_inode == NULL`)로 캐시한다 — 존재하지 않는 파일을 반복해서 찾는 프로그램(라이브러리 검색 경로, `$PATH` 탐색)이 매번 디스크를 읽지 않게 하기 위해서다.

### 3.2 RCU-walk와 ref-walk

경로 탐색은 커널에서 가장 자주 도는 코드 중 하나라서, **락도 참조 카운트도 안 잡고** 걷는 모드가 있다.

| | RCU-walk (`LOOKUP_RCU`) | ref-walk |
|---|---|---|
| 해시 조회 | `__d_lookup_rcu()` | `__d_lookup()` |
| 동기화 | `rcu_read_lock` + dentry의 `d_seq` seqcount로 "그 사이 바뀌지 않았나" 검증 | `d_lock` spinlock + 참조 카운트(`dget`) |
| 공유 캐시라인 쓰기 | 없음 (읽기만) | 성분마다 refcount 증감 |
| 잠들 수 있나 | 없다 | 있다 |
| 언제 그만두나 | 잠들어야 하는 콜백(권한 검사, `d_revalidate`, 디스크 lookup)을 만나면 `-ECHILD` | — |
| 전환 | `try_to_unlazy()`로 지금까지 걸은 경로에 참조를 잡고 ref-walk로 계속, 실패하면 `do_filp_open`이 처음부터 ref-walk로 재시도 | — |

seqcount는 Don이 아는 "버전 번호를 읽고, 데이터를 읽고, 버전이 그대로면 유효" 패턴이다(F4에서 seqlock으로 다시 나온다). 멀티코어에서 같은 `/usr/lib`를 수천 번 걸어도 dentry의 refcount 캐시라인이 코어 사이를 튕기지 않는다는 것이 RCU-walk의 이점이다.

`lookup_fast()`의 실제 코드(fs/namei.c, 6.18) — RCU 분기와 ref 분기가 그대로 보인다.

```c
static struct dentry *lookup_fast(struct nameidata *nd)
{
	struct dentry *dentry, *parent = nd->path.dentry;
	int status = 1;

	if (nd->flags & LOOKUP_RCU) {
		dentry = __d_lookup_rcu(parent, &nd->last, &nd->next_seq);
		if (unlikely(!dentry)) {
			if (!try_to_unlazy(nd))
				return ERR_PTR(-ECHILD);
			return NULL;
		}
		if (read_seqcount_retry(&parent->d_seq, nd->seq))
			return ERR_PTR(-ECHILD);

		status = d_revalidate(nd->inode, &nd->last, dentry, nd->flags);
		if (likely(status > 0))
			return dentry;
		...
	} else {
		dentry = __d_lookup(parent, &nd->last);
		if (unlikely(!dentry))
			return NULL;
		status = d_revalidate(nd->inode, &nd->last, dentry, nd->flags);
	}
	...
```

`lookup_fast()`가 NULL(miss)을 돌려주면 `walk_component()`가 `lookup_slow()` → `__lookup_slow()` → `d_alloc_parallel()`(새 dentry 할당, 같은 이름을 동시에 찾는 다른 스레드와 조율) → `dir->i_op->lookup()`으로 간다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="14" width="40" height="28" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="40" y="33" text-anchor="middle" font-size="13">/</text>
  <rect x="68" y="14" width="50" height="28" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="93" y="33" text-anchor="middle" font-size="13">sys</text>
  <rect x="126" y="14" width="64" height="28" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="158" y="33" text-anchor="middle" font-size="13">class</text>
  <rect x="198" y="14" width="50" height="28" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="223" y="33" text-anchor="middle" font-size="13">tty</text>
  <rect x="256" y="14" width="86" height="28" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="299" y="33" text-anchor="middle" font-size="13">ttyAMA0</text>
  <rect x="350" y="14" width="50" height="28" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="375" y="33" text-anchor="middle" font-size="13">dev</text>
  <text x="410" y="33" font-size="12">성분마다 아래 과정을 반복</text>
  <rect x="20" y="70" width="440" height="76" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="240" y="92" text-anchor="middle" font-size="13" font-weight="bold">① RCU-walk (LOOKUP_RCU)</text>
  <text x="240" y="114" text-anchor="middle" font-size="12">__d_lookup_rcu: 락·refcount 없이 해시 버킷 탐색</text>
  <text x="240" y="134" text-anchor="middle" font-size="12">d_seq seqcount로 결과가 유효한지 검증</text>
  <rect x="20" y="180" width="440" height="76" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="240" y="202" text-anchor="middle" font-size="13" font-weight="bold">② ref-walk</text>
  <text x="240" y="224" text-anchor="middle" font-size="12">__d_lookup: d_lock + 참조 카운트(dget)</text>
  <text x="240" y="244" text-anchor="middle" font-size="12">잠들어도 되는 모드 — permission, d_revalidate 가능</text>
  <rect x="20" y="290" width="440" height="76" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="240" y="312" text-anchor="middle" font-size="13" font-weight="bold">③ slow path — dcache miss</text>
  <text x="240" y="334" text-anchor="middle" font-size="12">__lookup_slow → d_alloc_parallel (새 dentry)</text>
  <text x="240" y="354" text-anchor="middle" font-size="12">→ dir->i_op->lookup (kernfs_iop_lookup, ext4_lookup …)</text>
  <line x1="240" y1="146" x2="240" y2="178" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="250" y="167" font-size="12">miss 또는 -ECHILD → try_to_unlazy</text>
  <line x1="240" y1="256" x2="240" y2="288" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="250" y="277" font-size="12">miss</text>
  <rect x="500" y="70" width="160" height="186" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="580" y="92" text-anchor="middle" font-size="13">dentry_hashtable</text>
  <text x="580" y="112" text-anchor="middle" font-size="12">65536 버킷</text>
  <text x="580" y="130" text-anchor="middle" font-size="12">key = (parent, name)</text>
  <rect x="520" y="144" width="120" height="20" rx="3" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <rect x="520" y="170" width="120" height="20" rx="3" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <rect x="520" y="196" width="120" height="20" rx="3" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="580" y="240" text-anchor="middle" font-size="12">hit면 ①에서 끝</text>
  <line x1="460" y1="108" x2="498" y2="150" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="460" y1="328" x2="580" y2="258" stroke="currentColor" stroke-dasharray="5 4" marker-end="url(#ah4)"/>
  <text x="560" y="300" text-anchor="middle" font-size="12">d_add: 해시에 등록</text>
  <text x="20" y="390" font-size="12">사용 중이 아닌 dentry는 LRU에 남아 있다가 메모리 압박 때 shrinker가 회수한다 (§8).</text>
</svg>
```
그림 4 — 경로 성분 하나를 찾는 세 단계. 대부분은 ①에서 끝나고, 처음 보는 이름만 ③까지 간다.

### 3.3 예제 4 — sysfs 경로를 두 번 열어 cold/warm 비교

무엇을 확인하나: 같은 경로를 처음 열 때는 `i_op->lookup`(sysfs는 `kernfs_iop_lookup`)이 불려 dentry를 만들고, 두 번째에는 dcache hit로 끝나는지. 그리고 sysfs가 RCU-walk를 유지하는지.

`function` 트레이서에 lookup 관련 함수만 걸고, 호출 위치(`<-부모`)별로 센다.

```sh
cd /f12
mount -t tracefs none /sys/kernel/tracing
T=/sys/kernel/tracing
echo 0 > $T/tracing_on
echo 'lookup_fast __lookup_slow kernfs_iop_lookup d_alloc_parallel try_to_unlazy __d_lookup_rcu __d_lookup' > $T/set_ftrace_filter
echo function > $T/current_tracer
P=/sys/class/tty/ttyAMA0/dev
echo "path=$P"
for i in 1 2; do
  echo > $T/trace
  ./t_trace $P read
  echo "### pass $i"
  grep -v "^#" $T/trace | awk '{print $NF, $(NF-1)}' | sort | uniq -c
done
```

```text
path=/sys/class/tty/ttyAMA0/dev
t_trace: open(/sys/class/tty/ttyAMA0/dev) = 4
### pass 1
     10 <-__lookup_slow d_alloc_parallel
     10 <-__lookup_slow kernfs_iop_lookup
     11 <-d_alloc_parallel __d_lookup_rcu
      1 <-d_lookup __d_lookup
      1 <-link_path_walk try_to_unlazy
     11 <-lookup_fast __d_lookup
      1 <-lookup_fast __d_lookup_rcu
      1 <-lookup_open.isra.0 d_alloc_parallel
      1 <-lookup_open.isra.0 kernfs_iop_lookup
      1 <-path_openat lookup_fast
     10 <-walk_component __lookup_slow
     11 <-walk_component lookup_fast
t_trace: open(/sys/class/tty/ttyAMA0/dev) = 4
### pass 2
      1 <-link_path_walk try_to_unlazy
     11 <-lookup_fast __d_lookup
      1 <-lookup_fast __d_lookup_rcu
      1 <-path_openat lookup_fast
     11 <-walk_component lookup_fast
```

이 경로는 symlink를 지난다. 같은 게스트에서 `readlink /sys/class/tty/ttyAMA0`의 결과:

```text
../../devices/platform/9000000.pl011/9000000.pl011:0/9000000.pl011:0.0/tty/ttyAMA0
```

출력에서 볼 것:

- **pass 1**: `walk_component` → `__lookup_slow` → `kernfs_iop_lookup`이 10번, 마지막 성분 `dev`는 `lookup_open` → `kernfs_iop_lookup`으로 1번. 10개는 `class`, `tty`, `ttyAMA0`(class 쪽 symlink), 그리고 symlink를 따라간 뒤의 `devices`, `platform`, `9000000.pl011`, `9000000.pl011:0`, `9000000.pl011:0.0`, `tty`, `ttyAMA0`이다. sysfs의 dentry·inode는 **누가 처음 볼 때 만들어진다**(kernfs 노드는 원래 있고, VFS 객체만 지연 생성).
- **pass 2**: `kernfs_iop_lookup`이 0번. 전부 `lookup_fast` 안에서 해시로 찾았다 — dcache hit.
- 두 pass 모두 `__d_lookup_rcu`는 1번(`sys`, rootfs 안), 나머지 11번은 `__d_lookup`(ref-walk)이다. 그리고 `link_path_walk`에서 `try_to_unlazy`가 1번 불렸다. 이유는 sysfs(kernfs)의 권한 콜백이 RCU 모드를 거부하기 때문이다:

```c
int kernfs_iop_permission(struct mnt_idmap *idmap,
			  struct inode *inode, int mask)
{
	...
	if (mask & MAY_NOT_BLOCK)
		return -ECHILD;
```

RCU-walk 중에는 권한 검사를 `MAY_NOT_BLOCK`으로 부르는데, kernfs는 rwsem을 잡아야 해서 `-ECHILD`로 "잠들어야 하니 ref-walk로 와라"라고 답한다. 그래서 **`/sys` 아래는 항상 ref-walk**로 걷는다. 이것은 함수 추적으로 처음 알게 된 사실이고, sysfs 파일을 초당 수천 번 여는 데몬(예: 센서 값을 sysfs로 폴링)이 생각보다 CPU를 먹는 이유 중 하나다. 그런 경우 fd를 열어 두고 `pread(fd, buf, n, 0)`으로 다시 읽는 것이 맞다 — 경로 탐색과 open을 매번 하지 않는다.

함정: `/proc/sys/vm/drop_caches`에 2를 쓰면 dentry·inode 캐시가 비워져서 pass 1 상태로 돌아간다. 벤치마크 전에 캐시 상태를 맞추지 않으면 첫 측정만 느린 이유를 엉뚱한 데서 찾게 된다(§8).

---

## 4. char device — `chrdev_open`이 `f_op`를 바꿔 끼운다

### 4.1 직관 — 장치 파일의 inode는 "전화번호부 한 줄"이다

`/dev/ttyAMA0`의 inode는 데이터를 갖고 있지 않다. 들고 있는 것은 **장치 번호 `i_rdev` = (major 204, minor 64)** 하나뿐이다. 파일시스템(devtmpfs, ext4 어느 쪽이든)이 이 inode를 만들 때 `init_special_inode()`가 `i_fop = &def_chr_fops`를 넣는다. `def_chr_fops`의 정의는 이게 전부다:

```c
const struct file_operations def_chr_fops = {
	.open = chrdev_open,
	.llseek = noop_llseek,
};
```

즉 장치 파일의 첫 `f_op`는 **"진짜 드라이버를 찾아 주는 open"** 하나만 가진 껍데기다. 펌웨어로 치면 부트로더의 "디바이스 ID로 드라이버 테이블을 찾아서 함수 포인터 테이블을 갈아 끼우는" 디스패처다.

### 4.2 `chrdev_open()` — 랩 소스 그대로

```c
static int chrdev_open(struct inode *inode, struct file *filp)
{
	const struct file_operations *fops;
	struct cdev *p;
	struct cdev *new = NULL;
	int ret = 0;

	spin_lock(&cdev_lock);
	p = inode->i_cdev;
	if (!p) {
		struct kobject *kobj;
		int idx;
		spin_unlock(&cdev_lock);
		kobj = kobj_lookup(cdev_map, inode->i_rdev, &idx);
		if (!kobj)
			return -ENXIO;
		new = container_of(kobj, struct cdev, kobj);
		spin_lock(&cdev_lock);
		/* Check i_cdev again in case somebody beat us to it while
		   we dropped the lock. */
		p = inode->i_cdev;
		if (!p) {
			inode->i_cdev = p = new;
			list_add(&inode->i_devices, &p->list);
			new = NULL;
		} else if (!cdev_get(p))
			ret = -ENXIO;
	} else if (!cdev_get(p))
		ret = -ENXIO;
	spin_unlock(&cdev_lock);
	cdev_put(new);
	if (ret)
		return ret;

	ret = -ENXIO;
	fops = fops_get(p->ops);
	if (!fops)
		goto out_cdev_put;

	replace_fops(filp, fops);
	if (filp->f_op->open) {
		ret = filp->f_op->open(inode, filp);
		if (ret)
			goto out_cdev_put;
	}

	return 0;

 out_cdev_put:
	cdev_put(p);
	return ret;
}
```

읽는 법:

1. `inode->i_cdev`가 비어 있으면(이 inode로 처음 open) `kobj_lookup(cdev_map, i_rdev)` — major:minor로 **`cdev_add()`가 등록해 둔 `struct cdev`** 를 찾는다. 없으면 `-ENXIO`("No such device or address"). 찾으면 inode에 캐시한다(다음 open은 이 조회를 건너뛴다).
2. `replace_fops(filp, cdev->ops)` — **`file->f_op`를 드라이버의 `file_operations`로 교체**.
3. 바뀐 `f_op->open`을 부른다.

misc device는 이 과정을 한 번 더 한다. misc 서브시스템이 major 10 전체를 하나의 cdev(`misc_fops`, `.open = misc_open`)로 등록해 두고, `misc_open()`이 minor로 `misc_list`를 뒤져 실제 드라이버의 fops로 **두 번째 교체**를 한다. 그 전에 `file->private_data = miscdevice`를 넣어 준다(§5에서 그 값을 쓴다).

```c
	/*
	 * Place the miscdevice in the file's
	 * private_data so it can be used by the
	 * file operations, including f_op->open below
	 */
	file->private_data = c;

	err = 0;
	replace_fops(file, new_fops);
	if (file->f_op->open)
		err = file->f_op->open(inode, file);
```

`/dev/null` 같은 메모리 장치(major 1)도 같은 2단 구조다. `memory_open()`이 minor로 `devlist[]`를 찾아 `filp->f_op = dev->fops`로 바꾼다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="235" y="18" text-anchor="middle" font-size="13" font-weight="bold">누가 무엇을 하나</text>
  <text x="580" y="18" text-anchor="middle" font-size="13" font-weight="bold">그때 file->f_op</text>
  <rect x="20" y="30" width="430" height="60" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="235" y="54" text-anchor="middle" font-size="13">do_dentry_open()</text>
  <text x="235" y="76" text-anchor="middle" font-size="12">f_op = inode->i_fop (init_special_inode가 넣어 둠)</text>
  <rect x="20" y="120" width="430" height="60" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="235" y="144" text-anchor="middle" font-size="13">chrdev_open(): i_rdev = 10:258</text>
  <text x="235" y="166" text-anchor="middle" font-size="12">kobj_lookup(cdev_map) → misc의 cdev → replace_fops</text>
  <rect x="20" y="210" width="430" height="60" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="235" y="234" text-anchor="middle" font-size="13">misc_open(): minor 258 → misc_list 검색</text>
  <text x="235" y="256" text-anchor="middle" font-size="12">private_data = miscdevice, replace_fops</text>
  <rect x="20" y="300" width="430" height="60" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="235" y="324" text-anchor="middle" font-size="13">f12_open(): 드라이버 코드</text>
  <text x="235" y="346" text-anchor="middle" font-size="12">private_data = 우리 컨텍스트로 덮어씀</text>
  <line x1="235" y1="90" x2="235" y2="118" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="235" y1="180" x2="235" y2="208" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="235" y1="270" x2="235" y2="298" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="495" y="42" width="170" height="36" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="580" y="65" text-anchor="middle" font-size="13">&amp;def_chr_fops</text>
  <rect x="495" y="132" width="170" height="36" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="580" y="155" text-anchor="middle" font-size="13">&amp;misc_fops</text>
  <rect x="495" y="222" width="170" height="36" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="580" y="245" text-anchor="middle" font-size="13">&amp;f12_fops</text>
  <rect x="495" y="312" width="170" height="36" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="580" y="335" text-anchor="middle" font-size="13">&amp;f12_fops (최종)</text>
  <line x1="450" y1="60" x2="493" y2="60" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah5)"/>
  <line x1="450" y1="150" x2="493" y2="150" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah5)"/>
  <line x1="450" y1="240" x2="493" y2="240" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah5)"/>
  <line x1="450" y1="330" x2="493" y2="330" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah5)"/>
  <text x="20" y="388" font-size="12">일반 cdev 드라이버(cdev_add)는 misc 단계 없이 chrdev_open에서 바로 드라이버 fops로 바뀐다 (G4).</text>
</svg>
```
그림 5 — misc device를 open 할 때 `file->f_op`가 두 번 바뀐다. 이후 read/ioctl은 마지막 `f_op`만 따라가므로 이 교체는 open 한 번에만 비용이 든다.

### 4.3 예제 5 — function_graph로 본 세 장치의 `vfs_open`

무엇을 확인하나: 그림 5의 교체가 실제로 일어나는지. 같은 `g2_trace.sh` 실행에서 루트를 `vfs_open`, 깊이 5로 잡은 결과다. 실습 모듈 `f12_vfs`(§5)는 이 부팅에서 막 insmod 했으므로 **처음 open**이다.

`/dev/f12_vfs` (출력 그대로, 중간에 끼어든 타이머 인터럽트 처리 4줄만 `...`로 생략):

```text
### graph=vfs_open depth=5 path=/dev/f12_vfs
               |  vfs_open() {
               |    do_dentry_open() {
               |      path_get() {
   3.904 us    |        mntget();
               |        _raw_spin_lock() {
   2.416 us    |          do_raw_spin_trylock();
 + 14.448 us   |        }
               |        _raw_spin_unlock() {
 + 34.752 us   |          do_raw_spin_unlock();
 + 65.200 us   |        }
 ! 106.992 us  |      }
   5.632 us    |      try_module_get();
   3.664 us    |      security_file_open();
   4.336 us    |      fsnotify_open_perm_and_set_mode();
               |      chrdev_open() {
               |        _raw_spin_lock() {
 ...
   4.048 us    |          do_raw_spin_trylock();
 # 3992.496 us |        }
               |        _raw_spin_unlock() {
   3.456 us    |          do_raw_spin_unlock();
 + 20.416 us   |        }
               |        kobj_lookup() {
 + 32.416 us   |          mutex_lock_nested();
   4.160 us    |          try_module_get();
 + 25.792 us   |          exact_lock();
 + 11.664 us   |          mutex_unlock();
   9.456 us    |          exact_match();
   4.000 us    |          module_put();
 ! 220.832 us  |        }
               |        _raw_spin_lock() {
   3.504 us    |          do_raw_spin_trylock();
 + 20.592 us   |        }
               |        _raw_spin_unlock() {
   3.216 us    |          do_raw_spin_unlock();
 + 15.424 us   |        }
   3.792 us    |        try_module_get();
   3.248 us    |        module_put();
               |        misc_open() {
 + 20.912 us   |          mutex_lock_nested();
 # 1002.960 us |          try_module_get();
   5.376 us    |          module_put();
 * 13439.20 us |          f12_open [f12_vfs]();
 + 17.040 us   |          mutex_unlock();
 * 14778.30 us |        }
 * 19148.19 us |      }
               |      file_ra_state_init() {
   5.760 us    |        inode_to_bdi();
 + 16.960 us   |      }
 * 19378.03 us |    }
 * 19398.36 us |  }
```

`/dev/null` (같은 실행, 출력 앞부분의 인터럽트 처리 줄들을 생략하고 `vfs_open`부터):

```text
               |  vfs_open() {
               |    do_dentry_open() {
               |      path_get() {
   3.792 us    |        mntget();
               |        _raw_spin_lock() {
   3.792 us    |          do_raw_spin_trylock();
 ! 123.168 us  |        }
               |        _raw_spin_unlock() {
   1.952 us    |          do_raw_spin_unlock();
   9.872 us    |        }
 ! 152.752 us  |      }
   3.504 us    |      try_module_get();
   2.128 us    |      security_file_open();
   2.752 us    |      fsnotify_open_perm_and_set_mode();
               |      chrdev_open() {
               |        _raw_spin_lock() {
   2.176 us    |          do_raw_spin_trylock();
 + 13.504 us   |        }
   2.672 us    |        try_module_get();
               |        _raw_spin_unlock() {
   2.080 us    |          do_raw_spin_unlock();
 + 12.048 us   |        }
   2.208 us    |        try_module_get();
   2.336 us    |        module_put();
   2.752 us    |        memory_open();
 ! 154.304 us  |      }
               |      file_ra_state_init() {
   2.704 us    |        inode_to_bdi();
   9.248 us    |      }
 ! 372.128 us  |    }
 ! 390.000 us  |  }
```

`/dev/ttyAMA0`의 `chrdev_open` 부분 (같은 실행, 깊이 5에서 잘린 줄 그대로):

```text
               |      chrdev_open() {
               |        _raw_spin_lock() {
   3.664 us    |          do_raw_spin_trylock();
 + 27.680 us   |        }
               |        _raw_spin_unlock() {
   3.248 us    |          do_raw_spin_unlock();
 + 15.200 us   |        }
               |        kobj_lookup() {
 + 30.240 us   |          mutex_lock_nested();
   3.424 us    |          try_module_get();
 + 37.456 us   |          exact_lock();
 + 11.376 us   |          mutex_unlock();
   3.968 us    |          exact_match();
   4.048 us    |          module_put();
 ! 152.464 us  |        }
               |        _raw_spin_lock() {
   3.632 us    |          do_raw_spin_trylock();
 + 20.592 us   |        }
               |        _raw_spin_unlock() {
   3.200 us    |          do_raw_spin_unlock();
 + 14.784 us   |        }
   3.200 us    |        try_module_get();
   3.168 us    |        module_put();
               |        tty_open() {
 + 15.376 us   |          nonseekable_open();
 + 34.304 us   |          __kmalloc_cache_noprof();
 + 33.424 us   |          mutex_lock_nested();
 ! 243.760 us  |          tty_lookup_driver();
 + 19.632 us   |          mutex_unlock();
 ! 100.384 us  |          tty_lock_interruptible();
 ! 411.632 us  |          tty_reopen();
 + 25.488 us   |          _raw_spin_lock();
 + 13.616 us   |          _raw_spin_unlock();
 + 50.912 us   |          check_tty_count();
 * 13098.36 us |          uart_open();
 # 4150.528 us |          tty_open_proc_set_tty();
 + 46.800 us   |          tty_unlock();
 * 18749.36 us |        }
 * 19064.52 us |      }
```

출력에서 볼 것:

- **세 장치 모두 `do_dentry_open` → `chrdev_open`**으로 들어간다 — `i_fop = &def_chr_fops`가 공통이라는 증거다.
- `f12_vfs`: `kobj_lookup`(cdev_map에서 major:minor 검색) → `misc_open` → `f12_open [f12_vfs]`. 그림 5의 두 번 교체가 그대로 보인다. `[f12_vfs]`는 이 함수가 모듈에 있다는 표시다.
- `/dev/null`: `kobj_lookup`이 없다. 이 inode는 부팅 때 `/init`의 `mount -t debugfs ... 2>/dev/null` 리다이렉션이 이미 열었으므로 `inode->i_cdev`에 캐시돼 있었다. 바로 `memory_open`으로 간다.
- `/dev/ttyAMA0`: 셸의 입출력은 `/dev/console`(5:1)이라는 다른 inode를 쓰므로 devtmpfs의 `/dev/ttyAMA0` inode는 이번이 첫 open이고, 그래서 `kobj_lookup`이 보인다. 그다음 `tty_open` → `uart_open`(serial core) — tty 계층이 드라이버 앞에 한 층 더 있다(H8 UART · TTY · serial에서 다룬다).
- `try_module_get`/`module_put` 쌍이 여러 번: `fops_get`이 fops의 `.owner` 모듈 참조를 잡고, 교체되는 옛 fops의 참조는 놓는다(`replace_fops`가 `fops_put`을 한다).

---

## 5. 실습 드라이버 `f12_vfs` — file 두 개, inode 하나, dup은 file 공유

### 5.1 예제 6 — 모듈: open/read/release에서 VFS 객체 찍기

무엇을 확인하나: 드라이버 콜백이 받는 `struct inode *`와 `struct file *`가 무엇인지 — open마다 file은 새로 생기고 inode는 같은지, `private_data`를 누가 먼저 쓰는지, `f_pos`와 `*ppos`의 관계, dup된 fd는 같은 file인지, `.release`는 언제 불리는지.

```c
// SPDX-License-Identifier: GPL-2.0
/* f12_vfs: misc char device — open/read/release에서 VFS 객체를 찍어 본다 */
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/slab.h>

struct f12_ctx { int id; char msg[32]; };	/* open 한 번당 하나 */
static atomic_t f12_nopen = ATOMIC_INIT(0);

static int f12_open(struct inode *inode, struct file *f)
{
	struct miscdevice *m = f->private_data;	/* misc_open()이 넣어 둔 값 */
	struct f12_ctx *c = kzalloc(sizeof(*c), GFP_KERNEL);

	if (!c)
		return -ENOMEM;
	c->id = atomic_inc_return(&f12_nopen);
	snprintf(c->msg, sizeof(c->msg), "hello from open #%d\n", c->id);
	pr_info("f12: open#%d file=%px inode=%px ino=%lu rdev=%u:%u name=%pd misc=%s\n",
		c->id, f, inode, inode->i_ino, imajor(inode), iminor(inode),
		file_dentry(f), m->name);
	pr_info("f12:   f_flags=0%o f_pos=%lld f_ref=%ld i_fop->open=%ps f_op->open=%ps\n",
		f->f_flags, f->f_pos, file_count(f), inode->i_fop->open, f->f_op->open);
	f->private_data = c;			/* 이제부터는 우리 것 */
	return 0;
}

static ssize_t f12_read(struct file *f, char __user *buf, size_t n, loff_t *ppos)
{
	struct f12_ctx *c = f->private_data;

	pr_info("f12: read  open#%d file=%px *ppos=%lld f_pos=%lld n=%zu nonblock=%d\n",
		c->id, f, *ppos, f->f_pos, n, !!(f->f_flags & O_NONBLOCK));
	return simple_read_from_buffer(buf, n, ppos, c->msg, strlen(c->msg));
}

static int f12_release(struct inode *inode, struct file *f)
{
	struct f12_ctx *c = f->private_data;

	pr_info("f12: release open#%d file=%px f_pos=%lld\n", c->id, f, f->f_pos);
	kfree(c);
	return 0;
}

static const struct file_operations f12_fops = {
	.owner = THIS_MODULE, .open = f12_open, .read = f12_read,
	.release = f12_release, .llseek = default_llseek,
};
static struct miscdevice f12_misc = {
	.minor = MISC_DYNAMIC_MINOR, .name = "f12_vfs", .fops = &f12_fops, .mode = 0666,
};
module_misc_device(f12_misc);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("F12: VFS objects seen from a misc char device");
```

포인트:

- `%px`는 커널 주소를 **가리지 않고** 찍는다(`%p`는 해시된 값). 학습용으로만 쓰고 제품 코드에서는 쓰지 않는다 — 주소 유출은 KASLR을 무력화한다.
- `%pd`는 dentry 이름을 찍는 printk 확장이다. `file_dentry(f)`는 `f->f_path.dentry`(overlayfs 같은 경우를 처리한 버전).
- open 첫 줄에서 `f->private_data`를 읽으면 **misc_open이 넣어 둔 `struct miscdevice *`** 다(§4.2). 그 값을 쓴 뒤 우리 컨텍스트로 덮어쓴다. G4의 일반 cdev 드라이버는 대신 `container_of(inode->i_cdev, struct my_dev, cdev)`로 장치 구조체를 찾는다.
- **컨텍스트를 open마다 따로 할당**했다. 같은 장치를 두 프로세스가 열어도 상태가 섞이지 않는다. 장치 전역 상태(레지스터, FIFO)는 별도 구조체에 두고 락으로 보호한다(F4).

### 5.2 테스트 프로그램 — open 두 번 + dup

유저 동작을 `/dev/kmsg`에 써서 커널 로그와 같은 타임라인에 끼워 넣는다.

```c
/* t_open: open 두 번 + dup — struct file 두 개, inode 한 개, dup은 file 공유 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

static int kmsg = -1;
static void mark(const char *s)		/* 유저 동작을 dmesg 타임라인에 끼워 넣기 */
{
	dprintf(kmsg, "f12: user: %s\n", s);
	printf("user: %s\n", s);
}
static void rd(int fd, const char *tag)
{
	char b[8] = {0};
	ssize_t n = read(fd, b, 6);
	if (n < 0) { perror("read"); exit(1); }
	printf("  %s read %zd bytes \"%.*s\"  lseek(fd,0,SEEK_CUR)=%lld\n",
	       tag, n, (int)n, b, (long long)lseek(fd, 0, SEEK_CUR));
}
int main(void)
{
	struct stat s1, s2;
	if ((kmsg = open("/dev/kmsg", O_WRONLY)) < 0) { perror("kmsg"); return 1; }
	mark("fd1 = open(O_RDONLY)");
	int fd1 = open("/dev/f12_vfs", O_RDONLY);
	mark("fd2 = open(O_RDONLY|O_NONBLOCK)");
	int fd2 = open("/dev/f12_vfs", O_RDONLY | O_NONBLOCK);
	if (fd1 < 0 || fd2 < 0) { perror("open"); return 1; }
	mark("fd3 = dup(fd1)");
	int fd3 = dup(fd1);
	if (fd3 < 0 || fstat(fd1, &s1) || fstat(fd2, &s2)) { perror("dup/fstat"); return 1; }
	printf("  fd1=%d fd2=%d fd3=%d  st_ino %lu/%lu  st_rdev %u:%u\n", fd1, fd2, fd3,
	       (unsigned long)s1.st_ino, (unsigned long)s2.st_ino,
	       major(s1.st_rdev), minor(s1.st_rdev));
	mark("read fd1, fd3, fd2");
	rd(fd1, "fd1"); rd(fd3, "fd3"); rd(fd2, "fd2");
	mark("close(fd1)  -- fd3 still holds the file");
	close(fd1);
	mark("close(fd3)");
	close(fd3);
	mark("close(fd2)");
	close(fd2);
	return 0;
}
```

게스트 스크립트:

```sh
cd /f12
echo "### uname"; uname -r
echo "### insmod"; insmod f12_vfs.ko && insmod f12fs.ko
ls -l /dev/f12_vfs /dev/null /dev/ttyAMA0
grep -E "misc|f12" /proc/devices; grep f12 /proc/misc
echo "### filesystems"; cat /proc/filesystems
echo "### t_open"; ./t_open
echo "### dmesg"; dmesg | grep "f12:"
```

```text
lab initramfs: 6.18.55-dbg up 17.61s
### uname
6.18.55-dbg
### insmod
crw-rw-rw-    1 0        0          10, 258 Oct 10 22:14 /dev/f12_vfs
crw-rw-rw-    1 0        0           1,   3 Jan  1  1970 /dev/null
crw-------    1 0        0         204,  64 Jan  1  1970 /dev/ttyAMA0
 10 misc
258 f12_vfs
...
### t_open
user: fd1 = open(O_RDONLY)
user: fd2 = open(O_RDONLY|O_NONBLOCK)
user: fd3 = dup(fd1)
  fd1=4 fd2=5 fd3=6  st_ino 145/145  st_rdev 10:258
user: read fd1, fd3, fd2
  fd1 read 6 bytes "hello "  lseek(fd,0,SEEK_CUR)=6
  fd3 read 6 bytes "from o"  lseek(fd,0,SEEK_CUR)=12
  fd2 read 6 bytes "hello "  lseek(fd,0,SEEK_CUR)=6
user: close(fd1)  -- fd3 still holds the file
user: close(fd3)
user: close(fd2)
### dmesg
[   20.025361] f12: user: fd1 = open(O_RDONLY)
[   20.031687] f12: open#1 file=ffff000005323200 inode=ffff000005eca738 ino=145 rdev=10:258 name=f12_vfs misc=f12_vfs
[   20.031937] f12:   f_flags=0400000 f_pos=0 f_ref=1 i_fop->open=chrdev_open f_op->open=f12_open [f12_vfs]
[   20.037351] f12: user: fd2 = open(O_RDONLY|O_NONBLOCK)
[   20.062807] f12: open#2 file=ffff000005323380 inode=ffff000005eca738 ino=145 rdev=10:258 name=f12_vfs misc=f12_vfs
[   20.062875] f12:   f_flags=0404000 f_pos=0 f_ref=1 i_fop->open=chrdev_open f_op->open=f12_open [f12_vfs]
[   20.063114] f12: user: fd3 = dup(fd1)
[   20.075481] f12: user: read fd1, fd3, fd2
[   20.075929] f12: read  open#1 file=ffff000005323200 *ppos=0 f_pos=0 n=6 nonblock=0
[   20.076844] f12: read  open#1 file=ffff000005323200 *ppos=6 f_pos=6 n=6 nonblock=0
[   20.077302] f12: read  open#2 file=ffff000005323380 *ppos=0 f_pos=0 n=6 nonblock=1
[   20.081509] f12: user: close(fd1)  -- fd3 still holds the file
[   20.082272] f12: user: close(fd3)
[   20.082676] f12: release open#1 file=ffff000005323200 f_pos=12
[   20.082841] f12: user: close(fd2)
[   20.083407] f12: release open#2 file=ffff000005323380 f_pos=6
[   20.441597] reboot: Power down
```

(`ls -l` 출력의 색 제어 문자만 지웠고, `...`은 `/proc/filesystems` 목록 생략 — §7에서 따로 본다. 주소는 실행마다 다르다.)

출력에서 볼 것:

- **file은 둘, inode는 하나**: open#1의 `file=...3200`, open#2의 `file=...3380`은 다르고, `inode=...a738`과 `ino=145`는 같다. 유저 쪽 `fstat`의 `st_ino 145/145`도 일치한다.
- **dup은 새 file을 만들지 않는다**: `fd3 = dup(fd1)` 줄 뒤에 커널 로그가 없다(open 콜백 없음). fd3로 읽은 read가 `open#1 file=...3200`, `*ppos=6`으로 찍혔다 — fd1과 같은 file, 같은 오프셋. 그래서 "hello " 다음 "from o"가 나왔다. fd2는 자기 file이라 처음부터 "hello ".
- **`f_flags`**: open#1은 `0400000`, open#2는 `0404000`. `04000`이 `O_NONBLOCK`, `0400000`은 arm64의 `O_LARGEFILE` — 64비트 커널은 `build_open_flags()`에서 항상 붙인다. 드라이버는 blocking 여부를 `f->f_flags & O_NONBLOCK`으로 본다(F3, G6).
- **`i_fop->open=chrdev_open`, `f_op->open=f12_open`**: inode의 기본 fops는 여전히 `def_chr_fops`이고, file의 `f_op`만 우리 것으로 바뀌었다. 그림 5의 결과를 드라이버 안에서 직접 확인한 것이다.
- **`*ppos`와 `f_pos`**: read 콜백 안에서 둘이 같은 값이다. E6에서 본 대로 `ppos`는 `ksys_read`의 지역 변수 주소이고, 드라이버가 `simple_read_from_buffer`로 `*ppos`를 올리면 read가 끝난 뒤 `f_pos`에 반영된다. release 때 `f_pos=12`(open#1)와 `6`(open#2)이 그 결과다.
- **release는 마지막 참조에서만**: `close(fd1)` 뒤에는 release가 없고, `close(fd3)` 뒤에 `release open#1`이 나온다. fork로 fd를 물려받은 자식, `mmap`, 다른 커널 코드의 `get_file()`도 같은 참조다.

함정: `.release`를 "close 할 때 불리는 함수"로 이해하면 버그가 난다. 장치를 "열려 있는 동안만 전원 ON" 하는 드라이버가 dup된 fd 때문에 전원을 끄지 못하거나, 반대로 `.flush`(close마다 불림)에서 전원을 끄면 아직 쓰는 fd가 있는데 꺼진다. 자원 해제는 `.release`, close마다 할 일(예: 쓰기 버퍼 flush)은 `.flush`다.

---

## 6. read 경로와 kprobe — 커널 안의 이름 찍기

### 6.1 read 경로 복습

E6 §2에서 `read()` → `ksys_read()` → `vfs_read()` → `file->f_op->read` 또는 `->read_iter`를 소스로 봤다. VFS 관점에서 다시 정리하면:

| 단계 | 하는 일 | 사용하는 VFS 객체 |
|---|---|---|
| `ksys_read(fd, ...)` | `fdget_pos(fd)`: fd 번호 → `struct file`, 정규 파일·디렉터리(`FMODE_ATOMIC_POS`)면 `f_pos_lock` | fdtable, file |
| `vfs_read(file, buf, n, &pos)` | `FMODE_READ`/`FMODE_CAN_READ` 검사, `access_ok`, `rw_verify_area` | file의 `f_mode` |
| `f_op->read` | 드라이버·procfs·sysfs 구현 | file의 `f_op`, `private_data` |
| `f_op->read_iter` → `filemap_read()` | 정규 파일은 page cache(`f_mapping` = inode의 `i_mapping`)에서 복사 | file, inode, address_space |

`f_pos_lock`이 정규 파일에만 걸리는 이유: 같은 file을 공유하는 두 스레드가 동시에 read 하면 POSIX는 오프셋이 깨지지 않기를 요구한다. 장치 파일은 `FMODE_ATOMIC_POS`가 없으므로 이 보장을 **드라이버가 알아서** 해야 한다(보통 장치 락으로).

### 6.2 예제 7 — kprobe_events로 `vfs_read`의 파일 이름 찍기

무엇을 확인하나: 커널을 다시 빌드하지 않고 임의의 커널 함수에 탐침을 걸어, 인자(`struct file *`)를 따라가 파일 이름·오프셋을 찍을 수 있는지. 예제 2의 pahole 오프셋을 그대로 쓴다.

fetch-arg 문법: `$arg1`은 첫 인자(arm64에서 x0), `+OFF(X)`는 "X + OFF 주소의 8바이트 값"이다. 이름 문자열은 `file → f_path.dentry → d_name.name → 문자열` 순서로 세 번 따라간다.

| 단계 | 식 | 근거 (예제 2 pahole) |
|---|---|---|
| dentry 포인터 | `+144($arg1)` | `f_path` 136 + `struct path`의 `dentry` 8 |
| 이름 포인터 | `+104(+144($arg1))` | `d_name` 96 + `qstr.name` 8 |
| 문자열 | `+0(+104(+144($arg1))):string` | `:string`은 계산된 주소에서 문자열을 읽는다 |
| 파일 오프셋 | `+312($arg1):s64` | `f_pos` 312 |
| 장치 번호 | `chrdev_open`의 `+76($arg1):x32` | `inode->i_rdev` 76 |

```sh
cd /f12
insmod f12_vfs.ko; insmod f12fs.ko; mkdir -p /mnt/f12; mount -t f12fs none /mnt/f12
mount -t tracefs none /sys/kernel/tracing
T=/sys/kernel/tracing
echo 0 > $T/tracing_on
echo 'p:f12/vread vfs_read name=+0(+104(+144($arg1))):string count=$arg3:u64 f_pos=+312($arg1):s64' >> $T/kprobe_events
echo 'p:f12/chropen chrdev_open rdev=+76($arg1):x32 ino=+64($arg1):u64' >> $T/kprobe_events
echo 'r:f12/vread_ret vfs_read ret=$retval:s64' >> $T/kprobe_events
echo "### kprobe_events"; cat $T/kprobe_events
echo "### format"; cat $T/events/f12/vread/format | tail -4
echo 1 > $T/events/f12/enable
echo > $T/trace
echo 1 > $T/tracing_on
cat /mnt/f12/hello > /dev/null
head -c 10 /dev/f12_vfs > /dev/null
dd if=/dev/zero of=/dev/null bs=4096 count=2 2>/dev/null
echo 0 > $T/tracing_on
echo "### trace"; grep -v "^#" $T/trace
echo 0 > $T/events/f12/enable; echo > $T/kprobe_events
```

```text
lab initramfs: 6.18.55-dbg up 13.46s
### kprobe_events
p:f12/vread vfs_read name=+0(+104(+144($arg1))):string count=$arg3:u64 f_pos=+312($arg1):s64
p:f12/chropen chrdev_open rdev=+76($arg1):x32 ino=+64($arg1):u64
r10:f12/vread_ret vfs_read ret=$retval:s64
### format
	field:u64 count;	offset:20;	size:8;	signed:0;
	field:s64 f_pos;	offset:28;	size:8;	signed:1;

print fmt: "(%lx) name=\"%s\" count=%Lu f_pos=%Ld", REC->__probe_ip, __get_str(name), REC->count, REC->f_pos
### trace
              sh-72      [001] d....    16.452952: chropen: (chrdev_open+0x0/0x218) rdev=0x100003 ino=5
             cat-81      [001] d....    16.521537: vread: (vfs_read+0x0/0x358) name="hello" count=4096 f_pos=0
             cat-81      [001] d..1.    16.523179: vread_ret: (ksys_read+0x6c/0x110 <- vfs_read) ret=17
             cat-81      [001] d....    16.523666: vread: (vfs_read+0x0/0x358) name="hello" count=4096 f_pos=17
             cat-81      [001] d..1.    16.523723: vread_ret: (ksys_read+0x6c/0x110 <- vfs_read) ret=0
              sh-72      [000] d....    16.540255: chropen: (chrdev_open+0x0/0x218) rdev=0x100003 ino=5
            head-82      [001] d....    16.618569: chropen: (chrdev_open+0x0/0x218) rdev=0xa00102 ino=145
            head-82      [001] d....    16.622306: vread: (vfs_read+0x0/0x358) name="f12_vfs" count=4096 f_pos=0
            head-82      [001] d..1.    16.622520: vread_ret: (ksys_read+0x6c/0x110 <- vfs_read) ret=19
              sh-72      [000] d....    16.649729: chropen: (chrdev_open+0x0/0x218) rdev=0x100003 ino=5
              dd-83      [001] d....    16.713952: chropen: (chrdev_open+0x0/0x218) rdev=0x100005 ino=7
              dd-83      [001] d....    16.714516: chropen: (chrdev_open+0x0/0x218) rdev=0x100003 ino=5
              dd-83      [001] d....    16.715263: vread: (vfs_read+0x0/0x358) name="zero" count=4096 f_pos=0
              dd-83      [001] d..1.    16.715860: vread_ret: (ksys_read+0x6c/0x110 <- vfs_read) ret=4096
              dd-83      [001] d....    16.716151: vread: (vfs_read+0x0/0x358) name="zero" count=4096 f_pos=0
              dd-83      [001] d..1.    16.716197: vread_ret: (ksys_read+0x6c/0x110 <- vfs_read) ret=4096
```

출력에서 볼 것:

- `name="hello"`, `"f12_vfs"`, `"zero"` — 커널 메모리를 세 번 따라가 dentry 이름을 꺼냈다. 첫 시도에서 `+104(+144($arg1)):string`(한 단계 부족)으로 썼을 때는 `name="H��"` 같은 쓰레기가 나왔다 — `:string`은 마지막 주소에서 바로 문자열을 읽으므로 이름 **포인터**가 든 칸을 한 번 더 따라가야 한다.
- `cat /mnt/f12/hello`: 첫 read가 17바이트(`ret=17`), 두 번째 read는 `f_pos=17`에서 0바이트 = EOF. 유저의 "read를 0이 나올 때까지 반복" 루프가 커널에서 이렇게 보인다.
- `/dev/zero`는 read마다 `f_pos=0`이다 — `/dev/zero`의 read는 위치를 올리지 않는다. 반면 `f12_vfs`·f12fs는 `simple_read_from_buffer`가 `*ppos`를 올린다.
- `chropen` 줄의 `rdev`는 커널 내부 `dev_t` 인코딩(major << 20 + minor)이다: `0x100003` = 1:3(`/dev/null`), `0x100005` = 1:5(`/dev/zero`), `0xa00102` = 10:258(`f12_vfs`). 유저 쪽 `st_rdev`의 인코딩과 다르다 — 커널 안에서는 `MAJOR()`/`MINOR()`, 유저는 `major()`/`minor()`를 쓴다.
- `sh-72`가 `/dev/null`을 여는 줄은 스크립트의 `> /dev/null` 리다이렉션이다.
- `r10:` — 리턴 프로브(kretprobe)는 동시에 추적할 수 있는 인스턴스 수(maxactive)가 자동으로 10으로 정해졌다는 표시다.

함정: 오프셋은 **이 빌드(debug, lockdep)에만 맞다.** 일반 커널, 다른 config, `CONFIG_RANDSTRUCT`에서는 전부 다르다. 실무에서는 BTF가 있는 커널에서 `$arg1->f_path.dentry->d_name.name` 같은 타입 기반 문법(kprobe의 BTF 인자, 또는 bpftrace)을 쓰는 것이 안전하다. 이 랩 커널은 `CONFIG_DEBUG_INFO_BTF`가 꺼져 있어서 오프셋 방식을 썼다.

---

## 7. 파일시스템 타입, mount, 그리고 pseudo filesystem

### 7.1 `file_system_type` → `super_block` → root dentry

파일시스템 "종류"는 `struct file_system_type` 하나로 커널에 등록된다. 핵심 필드(6.18 include/linux/fs.h):

```c
struct file_system_type {
	const char *name;
	int fs_flags;
	...
	int (*init_fs_context)(struct fs_context *);
	const struct fs_parameter_spec *parameters;
	struct dentry *(*mount) (struct file_system_type *, int,
		       const char *, void *);
	void (*kill_sb) (struct super_block *);
	struct module *owner;
	struct file_system_type * next;
	struct hlist_head fs_supers;
	...
```

- `register_filesystem()`은 이것을 전역 단일 연결 리스트(`file_systems`, `next` 필드)에 넣는다. 같은 이름이 있으면 `-EBUSY`. `/proc/filesystems`는 이 리스트를 그대로 출력하고, `FS_REQUIRES_DEV`가 없는 것(블록 장치가 필요 없는 것) 앞에 `nodev`를 붙인다.
- `mount -t X`는 이름으로 타입을 찾고 `init_fs_context()`로 `struct fs_context`를 만든 뒤 `fc->ops->get_tree()`를 부른다. 새 mount API(fs_context)는 5.x부터의 방식이고, 옛 `.mount` 콜백은 남아 있지만 새 코드에서는 쓰지 않는다.
- `get_tree`는 보통 헬퍼 하나로 끝난다: `get_tree_nodev()`(mount마다 새 super_block, tmpfs·f12fs), `get_tree_single()`(시스템에 super_block 하나, 예: 일부 pseudo FS), `get_tree_bdev()`(블록 장치 기반, ext4). 헬퍼가 super_block을 할당하고 파일시스템의 `fill_super`를 불러 `s_op`, `s_magic`, `s_root`를 채우게 한다.
- `kill_sb`는 umount 때 super_block을 정리한다. 메모리에만 있는 FS는 `kill_litter_super()`(남은 dentry를 정리)를 쓴다.

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="20" y="22" font-size="13" font-weight="bold">insmod: register_filesystem()</text>
  <rect x="20" y="34" width="80" height="30" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="60" y="54" text-anchor="middle" font-size="12">sysfs</text>
  <rect x="120" y="34" width="80" height="30" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="160" y="54" text-anchor="middle" font-size="12">tmpfs</text>
  <rect x="220" y="34" width="80" height="30" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="260" y="54" text-anchor="middle" font-size="12">proc</text>
  <text x="330" y="54" text-anchor="middle" font-size="13">…</text>
  <rect x="360" y="34" width="90" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="405" y="54" text-anchor="middle" font-size="12">f12fs</text>
  <line x1="100" y1="49" x2="118" y2="49" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="200" y1="49" x2="218" y2="49" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="300" y1="49" x2="318" y2="49" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="340" y1="49" x2="358" y2="49" stroke="currentColor" marker-end="url(#ah6)"/>
  <text x="470" y="54" font-size="12">= /proc/filesystems</text>
  <rect x="20" y="96" width="300" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="118" text-anchor="middle" font-size="13">mount -t f12fs none /mnt/f12</text>
  <text x="170" y="140" text-anchor="middle" font-size="12">init_fs_context → fc->ops->get_tree</text>
  <rect x="20" y="184" width="300" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="206" text-anchor="middle" font-size="13">get_tree_nodev(fc, fill_super)</text>
  <text x="170" y="228" text-anchor="middle" font-size="12">super_block 할당 + 익명 dev 0:25</text>
  <rect x="20" y="272" width="300" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="294" text-anchor="middle" font-size="13">simple_fill_super(sb, magic, files)</text>
  <text x="170" y="316" text-anchor="middle" font-size="12">s_op · s_magic · root inode · 파일들</text>
  <line x1="170" y1="152" x2="170" y2="182" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="170" y1="240" x2="170" y2="270" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="405" y1="64" x2="300" y2="94" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah6)"/>
  <rect x="370" y="100" width="290" height="80" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="515" y="122" text-anchor="middle" font-size="13" font-weight="bold">super_block</text>
  <text x="515" y="144" text-anchor="middle" font-size="12">s_magic = 0x66313266</text>
  <text x="515" y="164" text-anchor="middle" font-size="12">s_op = simple_super_operations</text>
  <rect x="370" y="210" width="290" height="44" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="515" y="237" text-anchor="middle" font-size="12">root ino 1: simple_dir_inode_operations</text>
  <rect x="370" y="284" width="90" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="415" y="304" text-anchor="middle" font-size="12">hello</text>
  <text x="415" y="320" text-anchor="middle" font-size="12">ino 2</text>
  <rect x="470" y="284" width="90" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="515" y="304" text-anchor="middle" font-size="12">jiffies</text>
  <text x="515" y="320" text-anchor="middle" font-size="12">ino 3</text>
  <rect x="570" y="284" width="90" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="615" y="304" text-anchor="middle" font-size="12">whoami</text>
  <text x="615" y="320" text-anchor="middle" font-size="12">ino 4</text>
  <line x1="320" y1="300" x2="368" y2="150" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="515" y1="180" x2="515" y2="208" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="515" y1="254" x2="515" y2="282" stroke="currentColor" marker-end="url(#ah6)"/>
  <text x="20" y="362" font-size="12">세 파일 모두 i_fop = f12fs_fops — read 콜백이 i_ino로 구분해서 내용을 그때그때 만든다</text>
</svg>
```
그림 6 — f12fs가 등록되고 mount 되는 과정. 타입은 리스트에 한 번, super_block은 mount마다 하나.

### 7.2 예제 8 — mount마다 super_block 하나: `statfs`의 `f_type`

무엇을 확인하나: `statfs(2)`가 돌려주는 `f_type`이 그 mount의 `super_block->s_magic`이고, `st_dev`가 super_block의 `s_dev`라는 것.

```c
/* t_statfs: 마운트마다 super_block 하나 — statfs의 f_type = sb->s_magic */
#include <stdio.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/statfs.h>

int main(int argc, char **argv)
{
	for (int i = 1; i < argc; i++) {
		struct statfs f;
		struct stat st;
		if (statfs(argv[i], &f) || stat(argv[i], &st)) { perror(argv[i]); continue; }
		printf("%-22s f_type=0x%-10lx st_dev=%u:%-3u ino=%-6lu bsize=%ld\n",
		       argv[i], (unsigned long)f.f_type, major(st.st_dev), minor(st.st_dev),
		       (unsigned long)st.st_ino, (long)f.f_bsize);
	}
	return 0;
}
```

예제 9의 게스트 스크립트 안에서 실행했다(아래 §7.3의 스크립트 `### statfs` 부분):

```text
### statfs
/                      f_type=0x1021994    st_dev=0:2   ino=1      bsize=4096
/proc                  f_type=0x9fa0       st_dev=0:23  ino=1      bsize=4096
/sys                   f_type=0x62656572   st_dev=0:24  ino=1      bsize=4096
/dev                   f_type=0x1021994    st_dev=0:6   ino=1      bsize=4096
/sys/kernel/debug      f_type=0x64626720   st_dev=0:8   ino=1      bsize=4096
/mnt/f12               f_type=0x66313266   st_dev=0:25  ino=1      bsize=4096
/mnt/f12               f_type=0x66313266   st_dev=0:25  ino=1      bsize=4096
/mnt/f12b              f_type=0x66313266   st_dev=0:26  ino=1      bsize=4096
```

출력에서 볼 것:

- `0x9fa0` = `PROC_SUPER_MAGIC`, `0x62656572` = `SYSFS_MAGIC`("beer"), `0x64626720` = `DEBUGFS_MAGIC`("dbg "), `0x1021994` = `TMPFS_MAGIC`. 우리 `0x66313266`은 ASCII "f12f"다.
- `/`(initramfs의 rootfs)와 `/dev`(devtmpfs)가 둘 다 `TMPFS_MAGIC`이다. 이 커널에서 rootfs와 devtmpfs는 내부적으로 tmpfs(shmem)를 쓴다. 그래도 `st_dev`가 0:2, 0:6으로 다르다 — **다른 super_block**이다.
- 모든 pseudo FS의 major가 0이다. 블록 장치가 없는 FS는 `get_tree_nodev()`가 익명 장치 번호(0:N)를 하나 할당한다. f12fs를 두 번 mount 하면 0:25, 0:26 — `get_tree_nodev`라서 mount마다 새 super_block이다.
- 모든 mount의 루트 inode 번호가 1이다. inode 번호는 **super_block 안에서만** 유일하다. 시스템 전체에서 파일을 식별하는 키는 `(st_dev, st_ino)` 쌍이다.

### 7.3 예제 9 — `f12fs`: libfs로 만든 50줄짜리 pseudo filesystem

무엇을 확인하나: 파일시스템을 하나 등록하고, mount 하면 모듈이 정한 파일들이 보이며, 파일 내용은 디스크가 아니라 **read 시점에 커널 코드가 만든다**는 것. procfs·sysfs·debugfs가 전부 이 원리다.

```c
// SPDX-License-Identifier: GPL-2.0
/* f12fs: libfs로 만든 초소형 pseudo filesystem — 파일 내용은 read 때 생성 */
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/jiffies.h>
#include <linux/sched.h>

#define F12FS_MAGIC 0x66313266			/* "f12f" */

static ssize_t f12fs_read(struct file *f, char __user *u, size_t n, loff_t *pos)
{
	char buf[64];
	int len;

	switch (file_inode(f)->i_ino) {		/* inode 번호로 파일 구분 */
	case 2:  len = scnprintf(buf, sizeof(buf), "hello from f12fs\n"); break;
	case 3:  len = scnprintf(buf, sizeof(buf), "jiffies=%lu\n", jiffies); break;
	default: len = scnprintf(buf, sizeof(buf), "pid=%d comm=%s\n",
				 current->pid, current->comm);
	}
	return simple_read_from_buffer(u, n, pos, buf, len);
}
static const struct file_operations f12fs_fops = {
	.owner = THIS_MODULE, .read = f12fs_read, .llseek = default_llseek,
};

static int f12fs_fill_super(struct super_block *sb, struct fs_context *fc)
{
	static const struct tree_descr files[] = {	/* 인덱스 = i_ino, 1은 root */
		[2] = { "hello",   &f12fs_fops, 0444 },
		[3] = { "jiffies", &f12fs_fops, 0444 },
		[4] = { "whoami",  &f12fs_fops, 0444 },
		{ "" }
	};
	return simple_fill_super(sb, F12FS_MAGIC, files);
}
static int f12fs_get_tree(struct fs_context *fc)
{
	return get_tree_nodev(fc, f12fs_fill_super);	/* mount마다 새 super_block */
}
static const struct fs_context_operations f12fs_ctx_ops = { .get_tree = f12fs_get_tree };
static int f12fs_init_fs_context(struct fs_context *fc)
{
	fc->ops = &f12fs_ctx_ops;
	return 0;
}
static struct file_system_type f12fs_type = {
	.owner = THIS_MODULE, .name = "f12fs",
	.init_fs_context = f12fs_init_fs_context, .kill_sb = kill_litter_super,
};
static int __init f12fs_init(void) { return register_filesystem(&f12fs_type); }
static void __exit f12fs_exit(void) { unregister_filesystem(&f12fs_type); }
module_init(f12fs_init);
module_exit(f12fs_exit);
MODULE_LICENSE("GPL");
MODULE_ALIAS_FS("f12fs");
MODULE_DESCRIPTION("F12: tiny libfs pseudo filesystem");
```

`simple_fill_super()`(fs/libfs.c)가 하는 일 — 이 한 함수가 §1의 네 객체 중 셋을 만든다:

```c
	s->s_magic = magic;
	s->s_op = &simple_super_operations;
	...
	inode = new_inode(s);
	...
	inode->i_ino = 1;
	inode->i_mode = S_IFDIR | 0755;
	inode->i_op = &simple_dir_inode_operations;
	inode->i_fop = &simple_dir_operations;
	...
	s->s_root = d_make_root(inode);
	...
	for (i = 0; !files->name || files->name[0]; i++, files++) {
		...
		dentry = d_alloc_name(s->s_root, files->name);
		...
		inode = new_inode(s);
		...
		inode->i_mode = S_IFREG | files->mode;
		inode->i_fop = files->ops;
		inode->i_ino = i;
		d_add(dentry, inode);
	}
```

즉 super_block을 채우고, 루트 inode + root dentry를 만들고, 파일마다 dentry(`d_alloc_name`) + inode(`new_inode`)를 만들어 `d_add`로 잇는다. 그리고 `i_fop = files->ops` — **open 때 `do_dentry_open`이 복사할 바로 그 값**이다.

게스트 스크립트(`g3_fs.sh`):

```sh
cd /f12
insmod f12fs.ko
echo "### filesystems"; grep -E "f12fs|tmpfs|proc|sysfs|debugfs|devtmpfs" /proc/filesystems
mkdir -p /mnt/f12 && mount -t f12fs none /mnt/f12
echo "### mount"; grep -E "f12fs|proc|sysfs|devtmpfs|debugfs" /proc/self/mountinfo
echo "### ls"; ls -li --color=never /mnt/f12
echo "### cat"; cat /mnt/f12/hello; cat /mnt/f12/jiffies; sleep 1; cat /mnt/f12/jiffies; cat /mnt/f12/whoami; sh -c 'cat /mnt/f12/whoami'
echo "### write"; echo x > /mnt/f12/hello; echo "rc=$?"
echo "### create"; touch /mnt/f12/new; echo "rc=$?"
echo "### statfs"; ./t_statfs / /proc /sys /dev /sys/kernel/debug /mnt/f12 2>&1
mkdir -p /mnt/f12b && mount -t f12fs none /mnt/f12b && ./t_statfs /mnt/f12 /mnt/f12b
echo "### lsmod"; lsmod | grep f12
echo "### umount"; rmmod f12fs; lsmod | grep f12; umount /mnt/f12 /mnt/f12b; lsmod | grep f12; rmmod f12fs; grep f12 /proc/filesystems || echo "f12fs unregistered"
```

```text
lab initramfs: 6.18.55-dbg up 56.14s
### filesystems
nodev	sysfs
nodev	tmpfs
nodev	proc
nodev	devtmpfs
nodev	debugfs
nodev	f12fs
### mount
24 1 0:23 / /proc rw,relatime - proc proc rw
25 1 0:24 / /sys rw,relatime - sysfs sysfs rw
26 1 0:6 / /dev rw,relatime - devtmpfs devtmpfs rw,size=204732k,nr_inodes=51183,mode=755
27 25 0:8 / /sys/kernel/debug rw,relatime - debugfs debugfs rw
28 1 0:25 / /mnt/f12 rw,relatime - f12fs none rw
### ls
total 0
      2 -r--r--r--    1 0        0                0 Oct 10 22:20 hello
      3 -r--r--r--    1 0        0                0 Oct 10 22:20 jiffies
      4 -r--r--r--    1 0        0                0 Oct 10 22:20 whoami
### cat
hello from f12fs
jiffies=4294907738
jiffies=4294908124
pid=83 comm=cat
pid=84 comm=cat
### write
sh: write error: Invalid argument
rc=1
### create
touch: /mnt/f12/new: Permission denied
rc=1
### statfs
...  (예제 8에 실은 8줄)
### lsmod
f12fs 12288 2 - Live 0xffffab30d1225000 (O)
### umount
rmmod: remove 'f12fs': Resource temporarily unavailable
f12fs 12288 2 - Live 0xffffab30d1225000 (O)
f12fs 12288 0 - Live 0xffffab30d1225000 (O)
f12fs unregistered
[   84.403025] reboot: Power down
```

출력에서 볼 것:

- **`/proc/filesystems`에 `nodev f12fs`** — `register_filesystem` 한 줄의 결과. `/proc/self/mountinfo`의 `0:25`가 예제 8의 `st_dev`와 같다.
- **크기 0인데 내용이 나온다**: `ls`는 `i_size`(0)를 보여 주지만 `cat`은 17바이트를 읽는다. procfs·sysfs 파일이 모두 크기 0(또는 4096)으로 보이는 이유다. 그래서 이런 파일을 `stat`의 크기만큼 읽는 코드는 틀린다 — EOF(0)까지 read 해야 한다.
- **내용은 read 시점에 생성**: `jiffies`가 `sleep 1`을 사이에 두고 386 증가했다(이 커널은 `CONFIG_HZ=250`이므로 386 tick ≈ 1.5초 — `cat` 프로세스 생성까지 포함한 TCG 게스트의 시간이다. 숫자 자체보다 "매번 다르다"를 본다). `whoami`는 읽는 프로세스마다 `pid`가 다르다(83, 84). sysfs의 `show()` 콜백, procfs의 `seq_file`이 정확히 이 방식이다.
- **write → `EINVAL`**: 모드는 0444지만 root라서 권한 검사는 통과했고, open에 성공했다. 그런데 `f12fs_fops`에 `.write`가 없으니 `do_dentry_open`이 `FMODE_CAN_WRITE`를 세우지 않았고, `vfs_write()`가 `-EINVAL`을 돌려줬다(§2.2).
- **touch → `EACCES`**: 루트 디렉터리의 `i_op`(`simple_dir_inode_operations`)에는 `.lookup`만 있고 `.create`가 없다. `open(O_CREAT)` 경로의 `lookup_open()`은 `if (!dir_inode->i_op->create) error = -EACCES;`다. 소스 주석도 "shouldn't it be ENOSYS?"라고 투덜댄다.
- **mount가 모듈 참조를 잡는다**: mount 두 개 → `lsmod` 사용 카운트 2, 그 상태의 `rmmod`는 `EAGAIN`("Resource temporarily unavailable"). `.owner = THIS_MODULE`이 `file_system_type`에도 있는 이유다. umount 후 0이 되고 rmmod가 성공, `/proc/filesystems`에서도 사라졌다.

### 7.4 드라이버는 pseudo FS를 직접 만들지 않는다 — 어디에 붙이나

f12fs는 원리를 보려고 만든 것이고, 드라이버가 파일을 노출할 때는 이미 있는 pseudo FS의 API를 쓴다. 각자 VFS 위에서 위의 `simple_fill_super` 같은 일을 대신 해 준다.

| pseudo FS | mount 위치 | 드라이버 API | 용도 | 규칙 |
|---|---|---|---|---|
| devtmpfs | `/dev` | `misc_register`, `device_create`(+`cdev_add`) | char/block 장치 노드 | 노드는 커널이 자동 생성, 권한·이름은 udev가 조정 (G9) |
| sysfs | `/sys` | `DEVICE_ATTR_RW` + `show`/`store`, attribute group | 장치 속성 한 파일에 값 하나 | ABI다 — 한 번 내보내면 바꾸기 어렵다 (G8) |
| debugfs | `/sys/kernel/debug` | `debugfs_create_file`, `debugfs_create_u32` 등 | 디버그 전용, 아무 형식 | ABI 아님, 제품에서 꺼질 수 있음 (G8) |
| procfs | `/proc` | `proc_create` + `seq_file` | 프로세스·커널 전역 정보 | 새 드라이버 정보는 넣지 않는 것이 관례 |
| configfs | `/sys/kernel/config` | `configfs_register_subsystem` | 유저가 객체를 **만드는** 설정 (USB gadget) | G8 |
| tmpfs | 어디든 | (드라이버용 아님) | RAM 파일시스템 | J3 |

### 7.5 예제 10 — misc_register 한 번이 sysfs·devtmpfs·procfs에 남기는 것

무엇을 확인하나: 예제 6의 `module_misc_device()` 한 줄이 `/dev` 노드, `/sys/class/misc/...` 디렉터리, `/proc/misc` 항목을 모두 만들고, rmmod 하면 모두 사라지는지.

```sh
cd /f12
echo "### link"; readlink /sys/class/tty/ttyAMA0
echo "### insmod"; ls /dev | grep f12; insmod f12_vfs.ko; ls -l --color=never /dev/f12_vfs
echo "### sysfs"; ls --color=never /sys/class/misc/f12_vfs/; cat /sys/class/misc/f12_vfs/dev /sys/class/misc/f12_vfs/uevent
echo "### proc"; grep f12 /proc/misc; grep -w f12_open /proc/kallsyms
echo "### rmmod"; rmmod f12_vfs; ls /dev/f12_vfs 2>&1
```

```text
### insmod
crw-rw-rw-    1 0        0          10, 258 Oct 10 22:24 /dev/f12_vfs
### sysfs
dev        power      subsystem  uevent
10:258
MAJOR=10
MINOR=258
DEVNAME=f12_vfs
DEVMODE=0666
### proc
258 f12_vfs
ffffdc95e557f088 t f12_open	[f12_vfs]
### rmmod
ls: /dev/f12_vfs: No such file or directory
```

(`g8_misc2.sh` 출력에서 이 부분만. 앞의 `### link` 결과는 §3.3에 실었다.)

출력에서 볼 것:

- insmod 전 `ls /dev | grep f12`는 아무것도 출력하지 않았고, insmod 직후 `/dev/f12_vfs`가 생겼다. udev가 없는 BusyBox initramfs에서도 **devtmpfs는 커널이 직접 노드를 만든다.** 권한 0666은 `miscdevice.mode`에서 왔다(`uevent`의 `DEVMODE=0666`).
- `/sys/class/misc/f12_vfs/uevent`의 `MAJOR`/`MINOR`/`DEVNAME`이 udev(또는 mdev)가 노드를 만들 때 쓰는 정보다(G9).
- rmmod 하면 노드도 사라진다. 이 노드들은 전부 **VFS 객체를 커널이 대신 만들어 준 것**이다 — devtmpfs의 dentry/inode(`i_rdev` = 10:258, `i_fop = def_chr_fops`), sysfs의 kernfs 노드.

---

## 8. dcache와 inode cache의 크기 — 측정

### 8.1 예제 11 — `/sys` 전체를 세 번 걷기

무엇을 확인하나: 경로를 처음 걸을 때 dentry·inode가 얼마나 생기는지, 두 번째부터 얼마나 빨라지는지, `drop_caches`로 회수되는지. `/proc/sys/fs/dentry-state`의 앞 두 숫자는 `nr_dentry`(전체 dentry 수)와 `nr_unused`(참조 0으로 LRU에 있는 수), `/proc/sys/fs/inode-nr`은 `nr_inodes`와 `nr_free_inodes`다.

```c
/* t_walk: /sys 전체를 두 번 걷기 — dcache가 차기 전(cold)과 후(warm) 비교 */
#define _GNU_SOURCE
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static long nent;
static int cb(const char *p, const struct stat *s, int t, struct FTW *f)
{
	(void)p; (void)s; (void)t; (void)f;
	nent++;
	return 0;
}
static void show(const char *file)
{
	char line[128];
	FILE *fp = fopen(file, "r");
	if (!fp || !fgets(line, sizeof(line), fp)) { perror(file); exit(1); }
	printf("    %-28s %s", file, line);
	fclose(fp);
}
static double now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}
int main(void)
{
	for (int pass = 1; pass <= 3; pass++) {
		printf("pass %d before:\n", pass);
		show("/proc/sys/fs/dentry-state");
		show("/proc/sys/fs/inode-nr");
		nent = 0;
		double t0 = now();
		if (nftw("/sys", cb, 64, FTW_PHYS) < 0) { perror("nftw"); return 1; }
		printf("  pass %d: %ld entries in %.1f ms\n", pass, nent, (now() - t0) * 1e3);
	}
	printf("after:\n");
	show("/proc/sys/fs/dentry-state");
	show("/proc/sys/fs/inode-nr");
	return 0;
}
```

(주석의 "두 번"은 실제로는 세 번이다 — 두 번째와 세 번째가 같은지도 보려고 늘렸다.)

```sh
cd /f12
echo "### slabinfo before"; grep -E "^(dentry|inode_cache|kernfs_node_cache|filp|proc_inode_cache|sysfs) " /proc/slabinfo | awk '{print $1, $2, $3, $4}'
./t_walk
echo "### slabinfo after"; grep -E "^(dentry|inode_cache|kernfs_node_cache|filp|proc_inode_cache) " /proc/slabinfo | awk '{print $1, $2, $3, $4}'
echo "### drop_caches"; echo 2 > /proc/sys/vm/drop_caches; cat /proc/sys/fs/dentry-state; cat /proc/sys/fs/inode-nr
```

```text
lab initramfs: 6.18.55-dbg up 14.09s
### slabinfo before
proc_inode_cache 24 24 1288
kernfs_node_cache 17138 17138 184
filp 42 42 384
inode_cache 26 26 1216
dentry 1212 1212 328
pass 1 before:
    /proc/sys/fs/dentry-state    1195	5	45	0	0	0
    /proc/sys/fs/inode-nr        1219	0
  pass 1: 27266 entries in 22822.6 ms
pass 2 before:
    /proc/sys/fs/dentry-state    27879	26690	45	0	0	0
    /proc/sys/fs/inode-nr        27902	0
  pass 2: 27266 entries in 16200.4 ms
pass 3 before:
    /proc/sys/fs/dentry-state    27879	26690	45	0	0	0
    /proc/sys/fs/inode-nr        27902	0
  pass 3: 27266 entries in 16573.1 ms
after:
    /proc/sys/fs/dentry-state    27879	26690	45	0	0	0
    /proc/sys/fs/inode-nr        27902	0
### slabinfo after
proc_inode_cache 24 24 1288
kernfs_node_cache 17138 17138 184
filp 42 42 384
inode_cache 17134 17134 1216
dentry 27936 27936 328
### drop_caches
2288	708	45	0	0	0
2312	0
```

(slabinfo 열: 이름, active 객체 수, 전체 객체 수, 객체 크기(바이트).)

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
  <text x="195" y="22" text-anchor="middle" font-size="13" font-weight="bold">dentry / inode 개수</text>
  <text x="535" y="22" text-anchor="middle" font-size="13" font-weight="bold">/sys 전체 nftw 시간</text>
  <line x1="60" y1="250" x2="330" y2="250" stroke="currentColor"/>
  <line x1="60" y1="183.3" x2="330" y2="183.3" stroke="#888" stroke-opacity="0.4"/>
  <line x1="60" y1="116.7" x2="330" y2="116.7" stroke="#888" stroke-opacity="0.4"/>
  <line x1="60" y1="50" x2="330" y2="50" stroke="#888" stroke-opacity="0.4"/>
  <text x="54" y="254" text-anchor="end" font-size="12">0</text>
  <text x="54" y="187" text-anchor="end" font-size="12">10k</text>
  <text x="54" y="121" text-anchor="end" font-size="12">20k</text>
  <text x="54" y="54" text-anchor="end" font-size="12">30k</text>
  <rect x="70" y="242.0" width="28" height="8.0" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <rect x="102" y="241.9" width="28" height="8.1" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
  <text x="100" y="234" text-anchor="middle" font-size="12">1195</text>
  <rect x="155" y="64.1" width="28" height="185.9" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <rect x="187" y="64.0" width="28" height="186.0" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
  <text x="185" y="58" text-anchor="middle" font-size="12">27879</text>
  <rect x="240" y="234.7" width="28" height="15.3" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <rect x="272" y="234.6" width="28" height="15.4" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
  <text x="270" y="227" text-anchor="middle" font-size="12">2288</text>
  <text x="100" y="268" text-anchor="middle" font-size="12">부팅 직후</text>
  <text x="185" y="268" text-anchor="middle" font-size="12">walk 후</text>
  <text x="270" y="268" text-anchor="middle" font-size="12">drop 후</text>
  <rect x="80" y="288" width="12" height="12" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <text x="98" y="299" font-size="12">nr_dentry</text>
  <rect x="190" y="288" width="12" height="12" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
  <text x="208" y="299" font-size="12">nr_inodes</text>
  <line x1="420" y1="250" x2="650" y2="250" stroke="currentColor"/>
  <line x1="420" y1="170" x2="650" y2="170" stroke="#888" stroke-opacity="0.4"/>
  <line x1="420" y1="90" x2="650" y2="90" stroke="#888" stroke-opacity="0.4"/>
  <text x="414" y="254" text-anchor="end" font-size="12">0 s</text>
  <text x="414" y="174" text-anchor="end" font-size="12">10 s</text>
  <text x="414" y="94" text-anchor="end" font-size="12">20 s</text>
  <rect x="430" y="67.6" width="44" height="182.4" fill="#d0564a" fill-opacity="0.7" stroke="#d0564a"/>
  <text x="452" y="61" text-anchor="middle" font-size="12">22.8 s</text>
  <rect x="505" y="120.4" width="44" height="129.6" fill="#3f9a6b" fill-opacity="0.7" stroke="#3f9a6b"/>
  <text x="527" y="114" text-anchor="middle" font-size="12">16.2 s</text>
  <rect x="580" y="117.2" width="44" height="132.8" fill="#3f9a6b" fill-opacity="0.7" stroke="#3f9a6b"/>
  <text x="602" y="111" text-anchor="middle" font-size="12">16.6 s</text>
  <text x="452" y="268" text-anchor="middle" font-size="12">1차 (cold)</text>
  <text x="527" y="268" text-anchor="middle" font-size="12">2차</text>
  <text x="602" y="268" text-anchor="middle" font-size="12">3차</text>
  <text x="535" y="299" text-anchor="middle" font-size="12">27266 항목, TCG + lockdep</text>
</svg>
```
그림 7 — 예제 11의 측정값. 왼쪽: `/sys`를 한 번 걸으면 dentry·inode가 약 2만 7천 개로 늘고, `drop_caches`(2)로 2288개까지 회수된다(막대 위 숫자는 nr_dentry). 오른쪽: 같은 걷기가 두 번째부터 약 29% 빨라진다.

출력에서 볼 것:

- **dentry +26684, inode +26683**: 걸은 항목 27266개마다 거의 하나씩 생겼다. 단, 이 숫자는 sysfs만이 아니다. `nftw`에 `FTW_MOUNT`를 주지 않았으므로 `/sys/kernel/debug`(debugfs, 그 아래 tracefs 자동 mount 포함)까지 넘어가서 걸었다.
- **`inode_cache` 26 → 17134, `kernfs_node_cache`는 17138 그대로**: sysfs(kernfs) inode는 일반 `inode_cache` slab에서 나오고, 늘어난 수(약 1만 7천)가 kernfs 노드 수와 거의 같다 — kernfs 노드 하나당 VFS inode 하나가 생겼다. 나머지 약 1만 개는 debugfs·tracefs inode로, 이 FS들은 자기 전용 slab(`debugfs_inode_cache` 등, 위 grep에는 넣지 않았다)을 쓴다. kernfs 노드(원본 데이터)는 장치가 등록될 때 이미 있고, VFS의 dentry/inode는 **누가 볼 때 만들어지는 캐시**일 뿐이다(§3.3의 `kernfs_iop_lookup`). 메모리로 보면 dentry 328바이트 + inode 1216바이트(debug 빌드 크기) × 약 2만 7천 ≈ 40 MB. 512 MB 보드에서 무시할 수 없는 양이고, 그래서 이 캐시들은 shrinker로 회수 가능하게 만들어져 있다.
- **`nr_unused` 26690**: 걷기가 끝나면 대부분 참조 0(누구도 열고 있지 않음)이 되어 LRU에 남는다. 이것이 "캐시"이고, `drop_caches`나 메모리 압박 때 회수 대상이다. 회수 후 2288개는 열려 있거나 mount 점처럼 고정된 것 + 이후 다시 만들어진 것이다.
- **시간 22.8 s → 16.2 s**: 2차부터 `kernfs_iop_lookup`과 dentry·inode 할당이 빠진 만큼 빨라졌다. 그래도 2차가 여전히 오래 걸리는 것은 매 항목마다 `newfstatat`/`getdents64` syscall, sysfs의 ref-walk(§3.3), 그리고 TCG + lockdep 비용이 남기 때문이다. 실제 보드에서는 절대값이 수십 배 작다.

함정: `/proc/sys/vm/drop_caches`는 **전역 상태를 바꾸는 운영 도구**이지 튜닝 손잡이가 아니다. 공유 장비에서 쓰면 다른 프로세스의 성능이 출렁인다. 이 측정도 랩 VM 호스트가 아니라 내 QEMU 게스트 안에서만 했다.

---

## 9. 커널 안에서는 무슨 일이 일어나나 — 한 장 요약

이 노트 자체가 "커널 안" 이야기였으므로, 유저 API 하나하나가 어떤 VFS 객체와 ops를 건드리는지 표 하나로 묶는다.

| 유저 호출 | VFS 경로 (6.18 이름) | 만들거나 쓰는 객체 | 불리는 ops | 다시 볼 노트 |
|---|---|---|---|---|
| `open` | `do_sys_openat2` → `do_filp_open` → `path_openat` → `vfs_open` → `do_dentry_open` | file 생성, dentry·inode는 dcache에서 (없으면 생성) | `i_op->lookup`(miss 때), `f_op->open` | §2, G4 |
| `open("/dev/x")` | 위 + `chrdev_open` → (`misc_open`) | `inode->i_cdev` 캐시 | `def_chr_fops.open` → 드라이버 `.open` | §4, G4, G7 |
| `read` / `write` | `ksys_read` → `vfs_read` | file의 `f_pos` | `f_op->read`/`read_iter` | E6, G4 |
| `close` | `file_close_fd` → `filp_flush` → `fput_close_sync` → (마지막이면) `__fput` | file 해제, dentry·inode 참조 감소 | `f_op->flush`(매번), `f_op->release`(마지막) | §5 |
| `dup` / `fork` | `fdtable`에 포인터 복사, `f_ref` 증가 | 새 객체 없음 | 없음 | §5, A3 |
| `stat` | 경로 탐색 → `vfs_getattr` | dentry·inode | `i_op->getattr` (없으면 generic) | §3 |
| `statfs` | `vfs_statfs` | super_block | `s_op->statfs` | §7.2 |
| `mount` | `fs_context` → `get_tree` → `fill_super` | super_block, root dentry/inode | `init_fs_context`, `get_tree` | §7 |
| `umount` | `kill_sb` | super_block 해제 | `s_op->put_super` 등 | §7 |
| `unlink` | `vfs_unlink` | dentry 제거(negative), inode `i_nlink` 감소 | `i_op->unlink`, 마지막에 `s_op->evict_inode` | J2 |
| 메모리 압박 | shrinker (`prune_dcache_sb`, `prune_icache_sb`) | 참조 0인 dentry·inode 회수 | `d_op->d_release`, `s_op->evict_inode` | F9, §8 |

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="20" width="140" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="90" y="42" text-anchor="middle" font-size="13">struct file</text>
  <text x="90" y="60" text-anchor="middle" font-size="12">open ~ 마지막 fput</text>
  <rect x="190" y="20" width="140" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="260" y="42" text-anchor="middle" font-size="13">dentry</text>
  <text x="260" y="60" text-anchor="middle" font-size="12">lookup ~ LRU 회수</text>
  <rect x="360" y="20" width="140" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="430" y="42" text-anchor="middle" font-size="13">inode</text>
  <text x="430" y="60" text-anchor="middle" font-size="12">lookup ~ evict</text>
  <rect x="530" y="20" width="130" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="595" y="42" text-anchor="middle" font-size="13">super_block</text>
  <text x="595" y="60" text-anchor="middle" font-size="12">mount ~ umount</text>
  <line x1="160" y1="45" x2="188" y2="45" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="330" y1="45" x2="358" y2="45" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="500" y1="45" x2="528" y2="45" stroke="currentColor" marker-end="url(#ah7)"/>
  <rect x="20" y="110" width="640" height="120" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="340" y="134" text-anchor="middle" font-size="13" font-weight="bold">char 드라이버가 실제로 만지는 것</text>
  <text x="340" y="158" text-anchor="middle" font-size="12">file_operations 하나 (open · release · read · write · poll · unlocked_ioctl · mmap)</text>
  <text x="340" y="180" text-anchor="middle" font-size="12">file->private_data (open마다 컨텍스트), file->f_flags (O_NONBLOCK), *ppos</text>
  <text x="340" y="202" text-anchor="middle" font-size="12">inode->i_cdev 또는 misc가 준 private_data로 장치 구조체 찾기</text>
  <text x="340" y="222" text-anchor="middle" font-size="12">나머지 객체는 devtmpfs · sysfs가 만들어 준다</text>
  <line x1="90" y1="70" x2="90" y2="108" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah7)"/>
</svg>
```
그림 8 — 객체마다 수명이 다르다(왼쪽에서 오른쪽으로 갈수록 오래 산다). 드라이버가 직접 다루는 것은 맨 왼쪽 `struct file`과 `file_operations`뿐이다.

---

## 10. Embedded 실무에서 다시 보기

### 10.1 센서 키트의 IMU char device — open마다 컨텍스트

IMU 드라이버를 char device로 만들면 보통 두 프로세스가 동시에 연다: 센서 퓨전 데몬(고속 FIFO read)과 진단 툴(가끔 상태 ioctl). §5처럼 **open마다 `private_data`에 컨텍스트**(읽기 위치, 구독 설정)를 두고, 장치 전역 상태(FIFO, 레지스터 캐시)는 `container_of(inode->i_cdev, ...)`로 찾은 장치 구조체에 두고 mutex로 보호한다. 둘을 섞으면 "진단 툴을 켜면 퓨전 데몬의 데이터가 튄다" 같은 버그가 된다. 그리고 "하나만 열 수 있는 장치"(예: 펌웨어 업데이트 채널)는 `.open`에서 `atomic` 플래그로 두 번째 open에 `-EBUSY`를 돌려준다 — 이것도 file이 open마다 따로 생긴다는 사실 위에서 설계하는 것이다.

### 10.2 sysfs 폴링 데몬이 CPU를 먹는다

GPIO 값, 온도, 배터리 잔량을 sysfs에서 100 Hz로 읽는 데몬이 흔하다. 매번 `open` + `read` + `close`를 하면 §3.3에서 본 것처럼 경로 성분마다 ref-walk(kernfs가 RCU-walk를 거부)와 kernfs 콜백, file 할당·해제가 반복된다. 해법은 **fd를 한 번 열어 두고 `pread(fd, buf, n, 0)`** 으로 다시 읽기다. sysfs 속성은 오프셋 0에서 다시 읽으면 `show()`가 새로 불린다. 값 변화를 기다려야 하면 `poll()`(`sysfs_notify`, G6·G8) 또는 GPIO character device(`/dev/gpiochipN`)의 이벤트를 쓴다.

### 10.3 "release가 안 불려서 장치 전원이 안 꺼져요"

카메라나 LiDAR처럼 "열려 있는 동안만 전원 ON" 하는 드라이버에서 자주 나온다. 원인은 거의 항상 **file의 참조가 남아 있는 것**이다: 데몬이 `fork()`/`system()`으로 자식을 띄웠는데 fd에 `O_CLOEXEC`가 없었다, `mmap`한 영역이 남아 있다(VMA가 file 참조를 잡는다), 다른 스레드가 dup 해 갔다. 확인은 `ls -l /proc/*/fd | grep 장치명`과 `/proc/<pid>/maps`. 드라이버 쪽에서는 §6.2의 kprobe처럼 `f12_release`(여기서는 드라이버의 release)에 탐침을 걸어 언제 불리는지 본다.

### 10.4 디버그 인터페이스는 debugfs에, 제품 인터페이스는 sysfs/char에

bring-up 중에는 레지스터 덤프, 내부 카운터, 강제 상태 전이를 노출하고 싶다. 이것을 sysfs에 넣으면 유저스페이스 ABI가 되어 나중에 지울 수 없다. **debugfs에 넣는다**(`debugfs_create_regset32`, `debugfs_create_file`). 제품 빌드에서는 debugfs mount 자체를 막을 수 있고(`debugfs=off` 커널 인자, 또는 `CONFIG_DEBUG_FS` 끄기), 그때 드라이버가 깨지지 않도록 debugfs 함수의 실패를 무시하는 것이 관례다 — debugfs API는 에러를 확인하지 않아도 되게 설계됐다.

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `file_operations`에 `.owner = THIS_MODULE` 누락 | fd가 열린 채 `rmmod` 성공, 다음 read에서 Oops | `fops_get`이 모듈 참조를 못 잡음 | 모든 fops·`file_system_type`에 `.owner` |
| 장치 전역 구조체 하나에 읽기 위치까지 저장 | 두 프로세스가 열면 데이터가 섞임 | file이 open마다 따로 생긴다는 점 무시 | open마다 `private_data`에 컨텍스트 |
| `.release`를 "close마다"로 가정 | dup·fork 뒤 release가 안 불림, 자원 누수 | release는 마지막 `fput` 때만 | 자원 해제는 release, close마다 할 일은 `.flush` |
| `.open` 실패 경로에서 할당 해제 안 함 | open 실패 때마다 메모리 누수 | open이 실패하면 release가 안 불림 | open 안에서 goto 에러 경로로 정리 |
| misc 드라이버 open에서 `private_data`를 덮어쓰기 전에 사용 안 함 | 장치 구조체를 못 찾아 전역 변수에 의존 | misc_open이 넣어 준 `miscdevice *`를 모름 | `container_of(f->private_data, struct my_dev, misc)` 후 덮어쓰기 |
| `.write` 없이 쓰기 가능한 노드 생성 | 유저 write가 `EINVAL` | `FMODE_CAN_WRITE` 미설정 | 노드 모드를 0444로, 또는 `.write` 구현 |
| procfs·sysfs 파일을 `st_size`만큼 읽기 | 0바이트 또는 4096바이트에서 잘림 | pseudo 파일의 크기는 의미 없음 | EOF(0)까지 read 반복 |
| sysfs 값을 루프에서 open/read/close | CPU 사용률 높음, 지연 | 매번 경로 탐색(ref-walk) + file 할당 | fd 유지 + `pread(…, 0)`, 또는 poll |
| 커널 구조체 오프셋 하드코딩 (kprobe·eBPF) | 다른 빌드에서 쓰레기 값 | config·lockdep·RANDSTRUCT로 레이아웃 변경 | BTF 기반 접근, 또는 빌드마다 pahole로 재계산 |
| pseudo FS mount 중 `rmmod` 시도 후 강제 | `EAGAIN`, 강제하면 위험 | mount가 모듈 참조를 잡음 | 먼저 umount |

---

## 12. 면접에서 이렇게 말한다

**Q.** What happens when you call `open("/dev/foo", O_RDWR)`?

**A.** 커널이 빈 fd 번호를 예약하고, 경로를 dcache로 따라가 `foo`의 dentry와 inode를 찾는다. `struct file`을 할당하고 `f_op = inode->i_fop`로 시작하는데, 장치 inode의 `i_fop`는 `def_chr_fops`라서 `chrdev_open`이 불린다. `chrdev_open`이 `i_rdev`(major:minor)로 `cdev_add` 때 등록된 cdev를 찾아 `f_op`를 드라이버의 `file_operations`로 바꾸고 드라이버 `.open`을 부른다. 성공하면 `fd_install`로 fd 번호에 file을 꽂고 번호를 돌려준다.

> The kernel reserves an unused fd, then walks the path through the dentry cache to find the dentry and inode for "foo". It allocates a struct file and initializes f_op from inode->i_fop. For a character device that's def_chr_fops, whose open is chrdev_open: it looks up the cdev registered for the inode's major:minor, swaps file->f_op to the driver's file_operations, and calls the driver's open. If that succeeds, fd_install publishes the file in the process's fd table and the fd is returned. After that, read, ioctl and friends go straight through file->f_op.

**꼬리 질문**: "misc device면 뭐가 달라요?" → major 10 전체가 misc의 cdev 하나라서 `misc_open`이 minor로 한 번 더 찾아 fops를 두 번째로 바꾸고, `private_data`에 `struct miscdevice *`를 넣어 준다.

**Q.** What's the difference between an inode, a dentry, and a file?

**A.** inode는 파일 실체(메타데이터, 장치 번호, 데이터 위치)로 파일당 하나, dentry는 "부모 디렉터리 안의 이름 → inode" 연결로 이름당 하나, file은 열린 세션(오프셋, 플래그, f_op, private_data)으로 open마다 하나다. 하드 링크는 dentry 둘·inode 하나, 같은 파일을 두 번 open 하면 file 둘·inode 하나다.

> An inode is the object itself — metadata, size, device number, where the data lives — one per file. A dentry is a name: it links a name in a parent directory to an inode, and it's what the dcache caches for path lookup. A struct file is an open instance — position, flags, f_op, private_data — created on every open. Two opens of the same device give two struct files and one inode; dup or fork shares the same struct file.

**Q.** When is a driver's `release` called?

**A.** close 할 때가 아니라 그 struct file의 **마지막 참조**가 사라질 때다. dup, fork로 물려받은 fd, mmap이 참조를 잡고 있으면 close 해도 안 불린다. close마다 불리는 것은 `.flush`다.

> Release is called when the last reference to the struct file goes away, not on every close. Dup'ed fds, children that inherited the fd across fork, and mmaps all hold references. If you need a hook on every close, that's flush.

**Q.** How does path lookup stay fast?

**A.** 성분마다 (부모 dentry, 이름) 해시로 dcache를 찾는다. 대부분은 RCU-walk로 락도 refcount도 안 잡고 seqcount로만 검증하며 걷는다. 잠들어야 하는 콜백을 만나면 ref-walk로 내려가고, 캐시에 없을 때만 파일시스템의 `->lookup`을 부른다. 없는 이름도 negative dentry로 캐시한다.

> Each component is a hash lookup in the dcache keyed by parent dentry and name. The common case is RCU-walk: no locks, no refcount changes, validated with per-dentry sequence counts. If something needs to sleep — a permission check, revalidation, or a cache miss — it drops to ref-walk, and only a miss calls the filesystem's lookup. Negative dentries cache "doesn't exist" too.

**꼬리 질문**: "sysfs도 RCU-walk 되나요?" → 6.18 기준 kernfs의 permission 콜백이 `MAY_NOT_BLOCK`에서 `-ECHILD`를 돌려주므로 `/sys` 아래는 ref-walk로 걷는다(함수 추적으로 확인).

**Q.** Where would you expose a driver's tuning knobs vs debug info vs data stream?

**A.** 데이터 스트림과 명령은 char device(또는 서브시스템 프레임워크), 장치 속성 하나하나는 sysfs 속성(ABI, 한 파일 한 값), 개발용 덤프는 debugfs(ABI 아님). procfs에는 새 드라이버 정보를 넣지 않는다.

> Streaming data and commands go through a character device — or better, the right subsystem like IIO or input. Per-device attributes go in sysfs, one value per file, and that's ABI. Debug dumps and knobs go in debugfs, which isn't ABI and may be disabled in production. New driver info doesn't go in procfs.

**Q.** What does `f_pos` vs `*ppos` mean in a driver's read?

**A.** `ppos`는 VFS가 넘기는 위치 포인터다. 일반 read에서는 `ksys_read`가 `f_pos`를 지역 변수로 복사해 그 주소를 넘기고 성공하면 다시 반영한다. `pread`면 유저가 준 오프셋이다. 그래서 드라이버는 `file->f_pos`가 아니라 `*ppos`만 읽고 갱신해야 한다.

> ppos is the position the VFS hands you. For plain read, ksys_read copies f_pos into a local, passes its address, and writes it back on success; for pread it's the caller's offset. A driver should only use and update `*ppos`, never touch `file->f_pos` directly.

**Q.** How do you add a new filesystem type to the kernel?

**A.** `struct file_system_type`에 이름, `init_fs_context`, `kill_sb`를 채워 `register_filesystem`한다. mount 때 `get_tree`가 `get_tree_nodev`/`get_tree_bdev` 같은 헬퍼로 super_block을 만들고 `fill_super`에서 `s_op`, 루트 inode, root dentry를 채운다. 메모리 기반이면 libfs(`simple_fill_super` 등)로 수십 줄이면 된다.

> Fill in a file_system_type with a name, init_fs_context and kill_sb, and call register_filesystem. At mount time get_tree uses a helper like get_tree_nodev or get_tree_bdev to create the super_block and calls your fill_super, which sets s_op and creates the root inode and dentry. For an in-memory filesystem, libfs helpers like simple_fill_super get you there in a few dozen lines.

---

## 13. 직접 해보기

1. (개념) 같은 파일에 하드 링크 `a`, `b`가 있다. `open("a")`, `open("b")`, `dup()`을 하면 file·dentry·inode는 각각 몇 개인가?
   정답/힌트: file 2개, dentry 2개, inode 1개. dup은 새 객체를 만들지 않는다.

2. (코드) `f12_vfs`에 "동시에 한 번만 열 수 있음" 정책을 넣어라. 두 번째 open은 `-EBUSY`, release에서 해제. `t_open`의 두 번째 open이 실패하는지 확인하라.
   정답/힌트: `static atomic_t busy`; open에서 `if (atomic_cmpxchg(&busy, 0, 1)) return -EBUSY;` release에서 `atomic_set(&busy, 0)`. 실패한 open에서는 release가 안 불리므로 kzalloc 전에 검사한다.

3. (관찰) function_graph를 `vfs_open` 대신 `__fput`에 걸고 `t_open`을 돌려라. `close(fd1)` 때는 `__fput`이 없고 `close(fd3)` 때 `__fput` → `f12_release`가 보이는지 확인하라.
   정답/힌트: 6.18의 `close()`는 `file_close_fd` → `filp_flush`(`.flush`) → `fput_close_sync`이고, 마지막 참조일 때만 `__fput` → `.release`까지 간다. 같은 스레드 안에서 실행되므로 pid 필터에 잡힌다.

4. (코드) `f12fs`에 쓰기 가능한 파일 `note`를 추가하라. write는 64바이트 버퍼에 저장, read는 그 내용을 돌려준다. 모드는 0644.
   정답/힌트: `tree_descr`에 `[5] = {"note", &note_fops, 0644}`, `.write`에서 `simple_write_to_buffer`. 여러 프로세스가 쓰면 mutex가 필요하다.

5. (관찰) kprobe로 `do_dentry_open`의 첫 인자에서 `f_flags`(오프셋 112)를 찍고, `cat`, `echo x >`, `dd oflag=...` 각각의 플래그를 비교하라.
   정답/힌트: `p:f12/dopen do_dentry_open flags=+112($arg1):x32`. `O_LARGEFILE`(0400000)이 항상 붙는 것을 확인한다. `O_CREAT`·`O_TRUNC`는 `do_dentry_open` 끝에서 지워지므로 진입 시점 값으로 봐야 한다.

6. (관찰) `/proc/sys/fs/dentry-state`를 보면서 존재하지 않는 경로 1만 개를 `stat` 하는 프로그램을 돌려라. 다섯 번째 숫자(`nr_negative`)가 어떻게 변하나?
   정답/힌트: negative dentry가 그만큼 늘어난다. 이 게스트처럼 `/` 가 tmpfs(rootfs)이면 tmpfs가 negative dentry를 남기는지 먼저 확인하라 — 파일시스템마다 `d_delete` 정책이 다르다. 결과는 직접 실행해서 확인할 것(이 노트에서는 돌리지 않았다).

7. (설계) IMU 드라이버의 "샘플링 주파수 설정", "FIFO 데이터", "레지스터 덤프"를 각각 어디로 노출할지 정하고 이유를 한 줄씩 써라.
   정답/힌트: sysfs 속성(또는 IIO의 `sampling_frequency`), char device read(또는 IIO buffer), debugfs.

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| VFS | Virtual File System | 모든 파일 같은 것에 공통 객체와 ops 테이블을 주는 계층 |
| super_block | 마운트된 파일시스템 인스턴스 | mount마다 하나, `s_op`·`s_root`·`s_magic` |
| inode | 파일 실체 | 메타데이터·장치 번호·`i_op`·`i_fop`, 파일당 하나 |
| dentry | directory entry | 이름 → inode 연결, dcache의 단위 |
| negative dentry | `d_inode == NULL`인 dentry | "그 이름은 없다"를 캐시 |
| struct file | 열린 파일 (POSIX의 open file description) | open마다 하나, `f_pos`·`f_flags`·`f_op`·`private_data` |
| file_ref_t `f_ref` | file 참조 카운트 (6.13+) | 0이 되면 `__fput` → `.release` |
| dcache | dentry cache | (부모, 이름) 해시 테이블 + LRU |
| RCU-walk | 락 없는 경로 탐색 모드 | seqcount로 검증, 잠들어야 하면 `-ECHILD` |
| ref-walk | 참조 카운트를 잡는 탐색 모드 | 잠들 수 있음, RCU-walk 실패 시 사용 |
| `nameidata` | 경로 탐색 상태 구조체 | 현재 위치, 플래그(LOOKUP_RCU 등), 마지막 성분 |
| `def_chr_fops` | 장치 inode의 기본 fops | `.open = chrdev_open`만 있는 디스패처 |
| cdev | char device 등록 단위 | major:minor 범위 → `file_operations` |
| `cdev_map` | cdev 검색 테이블 | `kobj_lookup`으로 `i_rdev` → cdev |
| `replace_fops` | `file->f_op` 교체 매크로 | 옛 fops 참조를 놓고 새 것으로 |
| misc device | major 10 공유 char device | `misc_register` 한 번으로 cdev·노드·sysfs |
| `file_system_type` | 파일시스템 종류 | `register_filesystem`으로 등록, `/proc/filesystems` |
| fs_context | 새 mount API의 문맥 | `init_fs_context` → `get_tree` |
| libfs | pseudo FS 헬퍼 모음 | `simple_fill_super`, `simple_read_from_buffer` … |
| pseudo filesystem | 디스크 없는 FS | procfs, sysfs, debugfs, devtmpfs, tmpfs |
| kernfs | sysfs의 백엔드 | 노드는 미리 있고 VFS 객체는 lookup 때 생성 |
| `s_magic` | FS 식별 번호 | `statfs()`의 `f_type` |
| function_graph | ftrace 트레이서 | 함수 진입·복귀를 호출 트리로 기록 |
| kprobe_events | 동적 탐침 인터페이스 | 임의 커널 함수에 인자 fetch 포함 이벤트 생성 |

---

## 15. 요약 & 체크리스트

VFS는 super_block(마운트) · inode(실체) · dentry(이름) · file(열린 세션)이라는 네 객체와 각각의 ops 테이블로 이루어진 다형성 계층이다. `open()`은 빈 fd를 예약하고, dcache를 RCU-walk로 걸어 dentry/inode를 찾고(없으면 `->lookup`), `struct file`을 만들어 `f_op = i_fop`로 시작한 뒤 `f_op->open`을 부르고, 성공하면 `fd_install`한다. 장치 파일은 `i_fop = def_chr_fops`라서 `chrdev_open`이 `i_rdev`로 cdev를 찾아 `f_op`를 드라이버 것으로 바꾸고(misc는 한 번 더), 이후 read/ioctl은 그 `f_op`만 따른다. 같은 장치를 두 번 열면 file 둘·inode 하나, dup·fork는 file을 공유하고, 드라이버의 `.release`는 마지막 참조에서만 불린다. 파일시스템은 `register_filesystem`으로 등록되고 mount마다 super_block을 만들며, procfs·sysfs·debugfs·devtmpfs는 디스크 없이 read 시점에 내용을 만드는 pseudo FS다 — 드라이버는 그 API로 파일을 노출한다. 이 모든 것을 6.18.55-dbg 게스트에서 function_graph, function 트레이서, kprobe_events, `/proc/sys/fs/dentry-state`로 직접 확인했다.

- [ ] super_block · inode · dentry · file의 역할, 생성 시점, 수명, ops 테이블을 표로 그릴 수 있다
- [ ] `open()`의 커널 경로를 `do_sys_openat2` → `do_filp_open` → `path_openat` → `vfs_open` → `do_dentry_open` → `f_op->open` → `fd_install` 순서로 말할 수 있다
- [ ] 6.18의 `do_filp_open`이 7.0에서 `do_file_open`이 된 것, inline 된 static 함수는 추적할 수 없다는 것을 안다
- [ ] dcache 해시, RCU-walk와 ref-walk의 차이, `-ECHILD`와 `try_to_unlazy`의 의미를 설명할 수 있다
- [ ] `def_chr_fops` → `chrdev_open` → `replace_fops` → 드라이버 `.open`, misc의 두 번째 교체를 설명할 수 있다
- [ ] 두 번 open과 dup의 차이를 file·inode 개수와 `f_pos` 공유로 설명할 수 있다
- [ ] `.release`와 `.flush`가 각각 언제 불리는지 말할 수 있다
- [ ] 드라이버 read에서 `*ppos`를 쓰고 `f_pos`를 직접 쓰지 않는 이유를 말할 수 있다
- [ ] function_graph로 open 경로를, kprobe_events로 커널 함수 인자(파일 이름)를 찍을 수 있다
- [ ] libfs(`simple_fill_super`, `get_tree_nodev`)로 pseudo filesystem을 만들고 mount 할 수 있다
- [ ] 드라이버 정보를 sysfs · debugfs · char device · procfs 중 어디에 노출할지 이유와 함께 고를 수 있다
- [ ] `/proc/sys/fs/dentry-state`와 `/proc/slabinfo`로 dcache·inode cache 크기를 관찰하고 해석할 수 있다

---

## 참고 자료

- docs.kernel.org — Overview of the Linux Virtual File System: https://docs.kernel.org/filesystems/vfs.html
- docs.kernel.org — Pathname lookup (RCU-walk, ref-walk): https://docs.kernel.org/filesystems/path-lookup.html
- docs.kernel.org — ftrace: https://docs.kernel.org/trace/ftrace.html
- docs.kernel.org — Kprobe-based Event Tracing: https://docs.kernel.org/trace/kprobetrace.html
- docs.kernel.org — Filesystem Mount API (fs_context): https://docs.kernel.org/filesystems/mount_api.html
- man7.org — open(2): https://man7.org/linux/man-pages/man2/open.2.html , statfs(2): https://man7.org/linux/man-pages/man2/statfs.2.html , proc(5): https://man7.org/linux/man-pages/man5/proc.5.html
- Michael Kerrisk, "The Linux Programming Interface" — 4장(File I/O), 5장(File I/O: Further Details), 14장(File Systems), 18장(Directories and Links)
- Jonathan Corbet, Alessandro Rubini, Greg Kroah-Hartman, "Linux Device Drivers, 3rd ed." — 3장(Char Drivers): `struct file`, `struct inode`, `file_operations`
- Neil Brown의 LWN 연재 "Pathname lookup in Linux" (2015) — RCU-walk의 설계 배경 (docs.kernel.org의 path-lookup 문서가 이 연재를 바탕으로 한다)
- 랩 소스 (elixir.bootlin.com의 v6.18에서도 같은 경로): `fs/open.c`(`do_sys_openat2`, `vfs_open`, `do_dentry_open`), `fs/namei.c`(`do_filp_open`, `path_openat`, `link_path_walk`, `lookup_fast`), `fs/dcache.c`, `fs/char_dev.c`(`chrdev_open`, `def_chr_fops`), `drivers/char/misc.c`(`misc_open`), `drivers/char/mem.c`(`memory_open`), `fs/libfs.c`(`simple_fill_super`), `fs/super.c`(`get_tree_nodev`), `fs/filesystems.c`(`register_filesystem`), `fs/kernfs/inode.c`(`kernfs_iop_permission`), `include/linux/fs.h`, `include/linux/dcache.h`
