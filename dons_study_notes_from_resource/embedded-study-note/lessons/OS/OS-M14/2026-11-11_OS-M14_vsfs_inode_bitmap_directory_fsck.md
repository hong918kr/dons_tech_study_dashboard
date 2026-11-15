---
id: OS-M14
time: 100분
updated: 2026-11-11
---
# OS-M14 파일 시스템 구현: VSFS (inode·비트맵·디렉터리)

## 학습 목표

- 4 KB 블록 64개짜리 디스크 이미지에 OSTEP 40장의 vsfs 배치(superblock · inode 비트맵 · 데이터 비트맵 · inode 표 · 데이터)를 **바이트 단위로** 만들고, inode 번호 → 블록·오프셋, 파일 블록 번호 → 디스크 블록(직접 12 + 1단 간접 1024) 계산을 손으로 할 수 있다.
- 경로 탐색(namei), create/mkdir, write(블록 할당 → 간접 블록으로 성장), read(hole 포함), unlink(링크 수, 블록·inode 반납), ls 를 `bread/bwrite` 만으로 구현한다.
- 연산마다 블록 I/O 를 세어 **OSTEP 그림 40.3/40.4 를 실측으로 다시 그린다.** 예: `/a/b/c` 열기 = 캐시 없음 7번, 연산 안 재사용 4번, 할당하는 4 KB 쓰기 = 5 I/O.
- fsck 를 작성한다. inode 와 디렉터리에서 비트맵·링크 수를 다시 계산하고, 주입한 손상 5종(새는 블록, 이중 할당, 잘못된 포인터, 빈 inode 를 가리키는 이름, 틀린 링크 수)과 고아 inode 를 찾아 고친다.
- 쓰기 **순서**가 왜 중요한지 크래시 주입으로 보인다. 올바른 순서는 크래시 뒤 "새는 것"만 남기고, 틀린 순서는 위험한 상태(nlink < 실제 이름 수)를 남긴다. 이것이 OS-A07 저널링의 출발점이다.

## 1. 문제

"`open("/foo/bar")` 하면 디스크에서 무슨 일이 일어나나요? 블록 몇 개를 읽나요?" 는 커널·스토리지 면접의 단골 질문이다. 펌웨어에서도 그대로 나온다. SPI NOR 위의 littlefs · FatFs, eMMC/SSD 아래의 FTL, 부팅할 때 전원 차단 뒤의 파일 시스템 검사가 모두 이 구조 위에 있다. Don 이 SSD 펌웨어에서 본 "호스트의 4 KB 쓰기" 가 사실은 이 레슨의 bitmap · inode · data 쓰기 묶음이다.

OSTEP 40장의 **vsfs**(Very Simple File System)를 직접 만든다. 디스크는 `/tmp/OS-M14_work/` 안의 `mkstemp` 파일이다. 열자마자 `unlink` 하므로 프로그램이 어떻게 끝나도 파일이 남지 않는다. 파일 시스템 코드는 **`bread(blk)` / `bwrite(blk)`(4 KB) 로만** 디스크에 닿는다. 두 함수는 횟수와 순서(`R I0 R D0 W dbm …`)를 기록한다. 테스트는 그 횟수를 정확히 검사한다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">디스크 64 블록 × 4 KB = 256 KB  (mkfs(80 inodes) — OSTEP 그림과 같은 배치)</text>
<rect x="10" y="35" width="40" height="34" class="f-warn" stroke="currentColor"/><text x="20" y="57" fill="currentColor">S</text>
<rect x="50" y="35" width="40" height="34" class="f-acc" stroke="currentColor"/><text x="62" y="57" fill="currentColor">i</text>
<rect x="90" y="35" width="40" height="34" class="f-acc" stroke="currentColor"/><text x="102" y="57" fill="currentColor">d</text>
<rect x="130" y="35" width="200" height="34" class="f-ok" stroke="currentColor"/><text x="150" y="57" fill="currentColor">I0  I1  I2  I3  I4 (inode 표)</text>
<rect x="330" y="35" width="400" height="34" class="f-mut" stroke="currentColor"/><text x="345" y="57" fill="currentColor">D0 D1 D2 …… D55  (데이터 영역, 블록 8..63)</text>
<text x="14" y="86" fill="currentColor" font-size="12">0</text><text x="56" y="86" fill="currentColor" font-size="12">1</text><text x="96" y="86" fill="currentColor" font-size="12">2</text>
<text x="134" y="86" fill="currentColor" font-size="12">3 … 7  (16 inode/블록 → 80개)</text><text x="334" y="86" fill="currentColor" font-size="12">8</text><text x="712" y="86" fill="currentColor" font-size="12">63</text>
<text x="10" y="104" fill="currentColor" font-size="12">S = superblock (마운트 때 메모리로)   i/d = 비트맵 1 블록씩 (최대 32768 개 추적)</text>
<text x="10" y="120" fill="currentColor" font-size="12">inode 번호 n → 블록 3 + n/16, 오프셋 (n%16)×256</text>
<rect x="10" y="130" width="350" height="185" rx="4" class="f-ok" stroke="currentColor"/>
<text x="20" y="150" fill="currentColor" font-weight="bold">dinode_t (256 B)</text>
<text x="20" y="172" fill="currentColor" font-family="monospace" font-size="12">u16 type   0 free / 1 file / 2 dir</text>
<text x="20" y="192" fill="currentColor" font-family="monospace" font-size="12">u16 nlink  이 inode 를 가리키는 이름 수</text>
<text x="20" y="212" fill="currentColor" font-family="monospace" font-size="12">u32 size   바이트</text>
<text x="20" y="232" fill="currentColor" font-family="monospace" font-size="12">u32 direct[12]  절대 블록 번호, 0 = hole</text>
<text x="20" y="252" fill="currentColor" font-family="monospace" font-size="12">u32 indirect    → 1024 개 블록 번호</text>
<text x="20" y="272" fill="currentColor" font-family="monospace" font-size="12">u8  pad[196]</text>
<text x="20" y="300" fill="currentColor" font-size="12">최대 (12 + 1024) × 4 KB = 4,243,456 B</text>
<rect x="380" y="130" width="350" height="185" rx="4" class="f-mut" stroke="currentColor"/>
<text x="390" y="150" fill="currentColor" font-weight="bold">dirent_t (32 B, 블록당 128 개)</text>
<text x="390" y="172" fill="currentColor" font-family="monospace" font-size="12">u32  inum</text>
<text x="390" y="192" fill="currentColor" font-family="monospace" font-size="12">char name[28]  (27자 + NUL)</text>
<text x="390" y="218" fill="currentColor" font-size="12">name[0] == 0 → 빈 칸 (재사용)</text>
<text x="390" y="238" fill="currentColor" font-size="12">디렉터리 = 이 칸들의 배열을 담은 "파일"</text>
<text x="390" y="258" fill="currentColor" font-size="12">"." → 자기, ".." → 부모 (루트는 둘 다 0)</text>
<text x="390" y="278" fill="currentColor" font-size="12">루트 inode = 0 (vsfs.py 와 같음.</text>
<text x="390" y="296" fill="currentColor" font-size="12">ext2/ext4 는 2 — OSTEP 40.6)</text>
</svg>
```

| 구조 | 크기 | 위치 (64 블록, 80 inode) | 계산 |
|---|---|---|---|
| superblock | 36 B (블록 하나 차지) | 0 | magic "VSFS", nblocks, ninodes, 각 영역 시작 |
| inode 비트맵 | 1 블록 | 1 | 비트 n = inode n 사용 중 |
| 데이터 비트맵 | 1 블록 | 2 | 비트 k = 블록 `data_start + k` 사용 중 |
| inode 표 | 5 블록 = 80 × 256 B | 3–7 | inode n → 블록 `3 + n/16` |
| 데이터 | 56 블록 | 8–63 | 루트 디렉터리가 D0 을 씀 → 처음 빈 블록 55 개 |

**I/O 를 세는 규칙** (테스트가 기대하는 숫자의 근거): superblock 은 메모리에 있다(OSTEP 40.6 의 가정). 모든 메타데이터 블록은 `bget` 으로 읽는다. `g_cache=0` 이면 매번 디스크에서 읽는다(OSTEP 그림 방식). `g_cache=1`(기본)이면 **한 연산 안에서** 이미 읽은 블록을 다시 읽지 않는다. 연산이 시작될 때(`op_begin`) 캐시를 비운다. 쓰기는 늘 즉시 디스크로 간다(write-through). atime 은 없다(`noatime`).

### Part A — 배치와 이름 공간

- **A1** `fs_mkfs(ninodes)` — 위 배치를 쓴다. 읽기 0번, 쓰기 9번(`W sb W ibm W dbm W I0 … W I4 W D0`).
- **A2** `bmap(ino, fbn, alloc, &fresh)` — 파일 블록 번호 → 디스크 블록. `fbn < 12` 면 direct, 아니면 indirect 블록의 칸. `alloc=1` 이면 비어 있는 칸을 할당한다. 디렉터리도 파일이라 Part A 부터 쓴다.
- **A3** `dir_lookup` + `namei_` — 루트부터 이름마다 "디렉터리인가(-ENOTDIR) → 칸 찾기(-ENOENT) → 다음 inode 읽기". `//a///b/`, `.`, `..` 도 처리한다.
- **A4** `dir_add` + `create_node` — create/mkdir. 빈 칸 재사용, 블록 경계에서 디렉터리 성장, 부모 nlink(mkdir), 실패 시 되돌리기.
- **A5** `fs_ls` — 칸 순서대로 이름들. 검증: 깊이별 namei 읽기 횟수 표(캐시 0/1), create/mkdir 의 정확한 trace, inode 고갈 시 `-ENOSPC`.

### Part B — 데이터 경로

- **B1** `fs_write(inum, off, buf, n)` — 블록 전체를 덮으면 읽지 않고, 일부면 RMW(새 블록이면 0 채움). inode 는 바뀌었을 때만 한 번 쓴다. 공간이 떨어지면 짧은 쓰기를 한다.
- **B2** `fs_read` — size 에서 자르고, hole 은 I/O 없이 0. **비트맵은 절대 읽지 않는다.**
- **B3** `fs_unlink` — 이름 먼저 지우기 → nlink−1 → 0 이면 inode → 블록(간접 포함, 비트맵 한 번에) → inode 비트. 하드 링크 `fs_link` 는 주어진다.
- 검증: 할당 쓰기 5 I/O, 덮어쓰기 2, 부분 덮어쓰기 3, 간접 블록이 생기는 쓰기 7, 14 블록 읽기 16(캐시 없음 17), 마지막 블록(1035)만 쓴 sparse 파일, `-EFBIG`, 꽉 찬 디스크의 짧은 쓰기 54 블록.

### Part C — fsck: 전원이 나간 뒤 부팅 경로

- **C1** `fsck(repair, &report)` — OSTEP 42.2 의 검사들. ① inode 표를 훑으며 포인터 범위와 소유자를 기록한다(잘못된 포인터 / 이중 할당). ② 루트부터 디렉터리를 BFS 로 돌며 이름이 빈 inode 를 가리키는지 보고 참조 수를 센다. ③ 고아 inode 와 링크 수를 본다. ④ inode 비트맵, ⑤ 데이터 비트맵을 계산값과 비교한다(새는 블록 / 표시 안 된 블록).
- 주입하는 손상과 기대 보고: leaked block → `leaked-block 1`, /empty 가 /log 의 블록을 가리킴 → `dup-block 1`(고침 = 복제), 포인터 9999 와 3(inode 표) → `bad-ptr 2`, 이름만 남은 해제된 inode → `dangling-dirent 1`, 파일 nlink 3 · /etc nlink 2 → `nlink 2`, 이름만 지워진 파일 → `orphan-inode 1`, 그리고 전부 한꺼번에.
- 같은 손상에 대해 검사만 할 때와 고칠 때 **같은 개수**를 보고하고, 고친 뒤 다시 검사하면 0 이어야 한다. 손상과 무관한 파일 내용은 그대로여야 한다.

## 2. Problem (English)

> Implement a tiny Unix-like file system ("VSFS", OSTEP ch. 40) on a disk image accessed only through `int bread(uint32_t blk, void *buf)` / `int bwrite(uint32_t blk, const void *buf)` (4 KiB blocks; both are counted).
>
> Layout: block 0 superblock, block 1 inode bitmap, block 2 data bitmap, then the inode table (256-byte inodes: `type, nlink, size, direct[12], indirect`), then data blocks. Directories are files of 32-byte entries `{uint32 inum; char name[28]}`; inode 0 is the root.
>
> - `int fs_mkfs(uint32_t ninodes)`; `int fs_namei(const char *path, dinode_t *out)` → inode number or `-ENOENT/-ENOTDIR/-EINVAL/-ENAMETOOLONG`.
> - `int fs_create(const char *path)`, `int fs_mkdir(const char *path)` → new inode number or `-EEXIST/-ENOSPC/...`; `int fs_ls(const char *dir, char *out, size_t cap)`.
> - `long fs_write(uint32_t inum, uint32_t off, const void *buf, uint32_t n)` allocating blocks on demand (direct, then single indirect; `-EFBIG` past 1036 blocks; short write on `-ENOSPC`); `long fs_read(...)` (holes read as zeros, never touches the bitmaps); `int fs_unlink(const char *path)` honoring link counts.
> - `int fsck(int repair, fsck_t *r)`: rebuild both bitmaps and all link counts from the inodes and the directory tree; report and optionally repair bad block pointers, doubly-allocated blocks, directory entries naming free inodes, unreachable inodes, wrong link counts, and bitmap mismatches.
>
> Example (no caching): resolving `/a/b/c` costs 7 block reads — root inode, root dir, a's inode, a's dir, b's inode, b's dir, c's inode. Appending one new 4 KiB block costs 5 I/Os: read inode, read+write data bitmap, write data, write inode.
>
> Follow-ups: how many I/Os does `create` cost and why does write order matter if power fails? Why don't reads consult the bitmap? What can fsck *not* fix? How does this map onto littlefs/FatFs on SPI flash, and an FTL underneath?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

처음 실행 결과 (실제 출력 전부, 0.1 초 안에 끝나고 exit 1):

```text
-- 0: block device layer (given)
PASS disk_open(16) == 0
PASS fstat(D.fd, &st) == 0 && st.st_size == 16 * BSIZE && st.st_nlink == 0
PASS bwrite(5, a) == 0 && bread(5, b) == 0 && memcmp(a, b, BSIZE) == 0
PASS IO(1, 1) && bread(16, b) == -EIO
-- Part A: mkfs, bmap, namei, create/mkdir, ls (+ I/O counts)
PASS disk_open(64) == 0
FAIL starter.c:448  fs_mkfs(80) == 0
  io mkfs(64 blk, 80 inodes)    R=0 W=0 | 
FAIL starter.c:450  IO(0, 9)
  (skip rest of Part A: fs_mkfs not implemented)
-- Part B: write, read, indirect growth, holes, link/unlink, ENOSPC
FAIL starter.c:522  fresh_fs(64, 80) == 0
  (skip Part B: fs_mkfs not implemented)
-- Part C: fsck finds and repairs injected corruption
FAIL starter.c:652  build_base() == 0
  (skip Part C: fs_mkfs not implemented)
5/9 passed
```

주어진 것: 블록 장치(`disk_open` · `bread` · `bwrite` · trace · 크래시 훅 `D.crash_after`), 연산 범위 캐시(`op_begin` · `bget` · `bget_zero`), `iget/iput`, 비트맵 first-fit `bm_alloc` / `ialloc` / `balloc`, 경로 문자열 도우미, `fs_mount` · `fs_stat` · `fs_link`, fsck 보고 구조. **A1 mkfs 를 채우면 Part A 의 나머지가 돌기 시작한다.** 그 뒤 A2→A5, B1→B3, C1 순서로 채우면 테스트가 하나씩 열린다. 스텁은 `-ENOSYS` 를 돌려주고, fsck 스텁은 "문제 없음" 이라고 거짓말한다(0).

| TODO | 함수 | 핵심 |
|---|---|---|
| A1 | `fs_mkfs` | 배치 계산, 9번 쓰기 |
| A2 | `bmap` | direct / indirect, alloc, fresh |
| A3 | `dir_lookup`, `namei_` | 블록마다 한 번 읽기, 오류 코드 |
| A4 | `dir_add`, `create_node` | 쓰기 순서, 되돌리기 |
| A5 | `fs_ls` | 빈 칸·`.`·`..` 건너뛰기 |
| B1–B3 | `fs_write`, `fs_read`, `fs_unlink` | RMW, hole, 링크 수 |
| C1 | `fsck` | 5 단계, 검사 = 고침 개수 |

## 4. 힌트

<details><summary>힌트 1 — mkfs 의 숫자</summary>

inode 블록 수 = ⌈ninodes / 16⌉, `inode_start = 3`, `data_start = 3 + inode 블록 수`, `ndata = nblocks − data_start`. 비트맵 두 개 모두 첫 비트만 1 이다(루트 inode 0, 루트 디렉터리 블록 = 데이터 0번). 루트 inode 의 `direct[0]` 에는 **절대 블록 번호** `data_start` 를 넣는다. 0 은 "없음" 이라서 상대 번호 0 을 넣으면 안 된다. 데이터 영역은 지우지 않는다(mkfs.ext4 의 lazy init 과 같은 생각).
</details>

<details><summary>힌트 2 — bmap 의 간접 블록</summary>

`fbn ≥ 12` 이면 `k = fbn − 12`, `k ≥ 1024` 면 `-EFBIG`. indirect 가 0 이고 할당 모드면 블록을 하나 받아 **0 으로 채운 버퍼**(`bget_zero`)를 쓴다. 디스크에 있는 쓰레기를 읽으면 안 된다. 순서는 "데이터 블록 할당 → indirect 칸에 기록 → indirect 블록 쓰기" 다. 데이터 할당이 실패했는데 indirect 만 새로 만든 경우에도, 0 으로 채운 indirect 블록은 써 두어야 쓰레기 포인터가 남지 않는다.
</details>

<details><summary>힌트 3 — namei 는 "지금 inode" 하나만 들고 간다</summary>

루트 inode 를 읽고, `path_next` 로 이름을 하나씩 꺼낸다. 이름마다 (1) 지금 inode 가 디렉터리가 아니면 `-ENOTDIR`, (2) `dir_lookup` 이 실패하면 그 값, (3) 찾은 번호로 inode 를 읽는다. `.` 과 `..` 는 실제 칸이라 특별 처리가 필요 없다. 루프가 끝나면 마지막 inode 가 답이다. 캐시 없이 깊이 d 의 경로는 2d+1 번 읽는다.
</details>

<details><summary>힌트 4 — create 는 "가리켜질 것부터" 쓴다</summary>

ialloc(비트맵 쓰기) → 새 inode 를 완전히 초기화해서 쓰기 → (mkdir 이면 `.`·`..` 블록) → 부모 디렉터리에 칸 쓰기 → 부모 inode(size, nlink) 쓰기. 이름을 먼저 걸면 그 사이에 전원이 나갈 때 이름이 초기화 안 된 inode 를 가리킨다. `dir_add` 는 빈 칸이 있으면 재사용한다. 이때 size 가 안 바뀌므로 부모 inode 를 쓰지 않아도 된다(mkdir 의 nlink+1 이 있으면 쓴다).
</details>

<details><summary>힌트 5 — write 의 세 경우</summary>

블록마다 `m = min(4096 − bo, 남은 양)`. `m == 4096` 이면 사용자 데이터로 바로 `bwrite`(읽기 0). 아니고 `fresh` 면 0 으로 채운 버퍼에 복사해서 쓴다. 기존 블록이면 `bread` → 복사 → `bwrite`(RMW) 다. 끝에서 `memcmp(&ino, &old)` 가 다를 때만 `iput` 한다. 그래서 덮어쓰기는 inode 쓰기가 없다.
</details>

<details><summary>힌트 6 — fsck 는 "inode 를 믿는다"</summary>

OSTEP 42.2: 비트맵과 inode 가 다르면 inode 쪽이 맞다고 보고 비트맵을 다시 만든다. `owner[b]` 배열 하나로 "이 블록의 주인 inode" 를 기록하면 이중 할당, 새는 블록, 표시 안 된 블록이 모두 나온다. 디렉터리 순회의 `refs[i]` 가 곧 올바른 링크 수다. 고칠 때 비트맵을 그 자리에서 맞춰 두면(복제 블록을 켜고, 고아의 블록을 끔) 검사 모드와 고침 모드의 보고 개수가 같아진다.
</details>

## 5. 풀이

### Part A — mkfs: 9번 쓰기로 빈 파일 시스템

64 블록, 80 inode 로 실행한 실제 trace 는 `W sb W ibm W dbm W I0 W I1 W I2 W I3 W I4 W D0` (R=0 W=9) 이다. 테스트가 확인하는 결과값: `inode_start 3`, `ninode_blocks 5`, `data_start 8`, `ndata 56`, 빈 inode 79, 빈 블록 55, 루트 `DIR nlink 2 size 64`. inode 표 5 블록을 0 으로 쓰는 이유는 "type 0 = 비어 있음" 이 성립해야 하기 때문이다. 데이터 블록은 쓰레기여도 된다. 비트맵이 0 인 블록은 아무도 읽지 않고, 새로 할당할 때는 덮어쓰거나 0 으로 채운다.

### Part A — bmap: 불균형 트리 (multi-level index)

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">bmap(ino, fbn): 파일 블록 번호 → 디스크 블록 번호</text>
<rect x="10" y="40" width="150" height="190" rx="4" class="f-ok" stroke="currentColor"/>
<text x="20" y="60" fill="currentColor">inode</text>
<text x="20" y="84" fill="currentColor" font-family="monospace" font-size="12">direct[0]</text>
<text x="20" y="104" fill="currentColor" font-family="monospace" font-size="12">direct[1]</text>
<text x="20" y="124" fill="currentColor" font-family="monospace" font-size="12">  …</text>
<text x="20" y="144" fill="currentColor" font-family="monospace" font-size="12">direct[11]</text>
<text x="20" y="190" fill="currentColor" font-family="monospace" font-size="12">indirect</text>
<rect x="250" y="70" width="70" height="26" class="f-mut" stroke="currentColor"/><text x="258" y="88" fill="currentColor" font-size="12">fbn 0</text>
<rect x="330" y="70" width="70" height="26" class="f-mut" stroke="currentColor"/><text x="338" y="88" fill="currentColor" font-size="12">fbn 1</text>
<rect x="410" y="70" width="70" height="26" class="f-mut" stroke="currentColor"/><text x="418" y="88" fill="currentColor" font-size="12">fbn 11</text>
<path d="M120 80 C 190 80, 200 83, 248 83" stroke="currentColor" fill="none"/>
<path d="M120 100 C 220 100, 300 100, 365 97" stroke="currentColor" fill="none"/>
<path d="M120 140 C 260 140, 420 120, 445 97" stroke="currentColor" fill="none"/>
<rect x="250" y="165" width="140" height="50" class="f-acc" stroke="currentColor"/>
<text x="258" y="185" fill="currentColor" font-size="12">indirect 블록</text>
<text x="258" y="203" fill="currentColor" font-size="12">u32 × 1024 칸</text>
<path d="M110 186 L248 188" stroke="currentColor" fill="none" stroke-width="1.5"/>
<path d="M241 183 L249 188 L241 193" stroke="currentColor" fill="none"/>
<rect x="470" y="150" width="70" height="26" class="f-mut" stroke="currentColor"/><text x="474" y="168" fill="currentColor" font-size="12">fbn 12</text>
<rect x="550" y="150" width="70" height="26" class="f-mut" stroke="currentColor"/><text x="554" y="168" fill="currentColor" font-size="12">fbn 13</text>
<rect x="640" y="150" width="90" height="26" class="f-mut" stroke="currentColor"/><text x="644" y="168" fill="currentColor" font-size="12">fbn 1035</text>
<path d="M390 178 L468 165" stroke="currentColor" fill="none"/><path d="M390 186 L548 168" stroke="currentColor" fill="none"/>
<path d="M390 200 L638 170" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
<text x="420" y="215" fill="currentColor" font-size="12">칸 [fbn − 12]. 0 = hole (읽으면 0, I/O 없음)</text>
<text x="10" y="244" fill="currentColor" font-size="12">≤ 48 KB 파일은 inode 하나로 끝. 큰 파일만 indirect 1 블록 추가 (OSTEP 그림 40.2: 대부분 파일은 작다)</text>
</svg>
```

할당 모드에서 indirect 를 새로 만들 때 `bget_zero` 를 쓰는 이유: 디스크 블록은 이전 파일이 쓰던 쓰레기일 수 있다. 쓰레기를 읽어서 칸으로 쓰면 1023 개의 엉뚱한 포인터가 생긴다(fsck 가 `bad-ptr` / `dup-block` 으로 보고할 것들). 실측 trace(14 블록 파일의 블록 12 와 13 쓰기, D13 = indirect):

```text
  io write blk12 (new indirect) R=2 W=5 | R I0 R dbm W dbm W dbm W D13 W D14 W I0
  io write blk13                R=3 W=4 | R I0 R D13 R dbm W dbm W D13 W D15 W I0
```

블록 12 는 비트맵을 두 번 쓴다(indirect 하나, 데이터 하나). 비트맵 블록 읽기는 캐시 덕에 한 번이다. 블록 13 은 indirect 를 읽고(칸이 0 인지 확인), 칸을 채워 다시 쓴다.

### Part A — namei 와 경로 길이: OSTEP 그림 40.3 의 앞부분

트리 `/a(inode 1)/b(2)/c(3)` 에서 실측한 값. inode 0–15 는 모두 블록 I0 에 있다:

| 경로 | 캐시 없음 (OSTEP 방식) | 연산 안 재사용 | 캐시 없음 trace |
|---|---|---|---|
| `/` | 1 | 1 | `R I0` |
| `/a` | 3 | 2 | `R I0 R D0 R I0` |
| `/a/b` | 5 | 3 | `… R D1 R I0` |
| `/a/b/c` | **7** = 2d+1 | **4** = d+1 | `R I0 R D0 R I0 R D1 R I0 R D2 R I0` |

캐시가 없으면 이름 하나마다 "디렉터리 블록 + 다음 inode 블록" 두 번이다. 캐시가 있으면 inode 블록이 하나뿐이라(16 inode/블록) 처음 한 번만 읽고 디렉터리 블록만 늘어난다. 실제 커널은 여기서 훨씬 더 간다. Linux 는 dentry 캐시(dcache)가 경로 이름 → inode 를 기억하므로 자주 여는 경로는 디스크 I/O 가 0 이다(OSTEP 40.7). 큰 디렉터리는 칸을 순서대로 훑으므로 뒤에 있는 이름일수록 블록을 더 읽는다(연습 2: 130 개 디렉터리에서 `f0` 5번, `f129` 6번).

### Part A — create/mkdir: 몇 번, 어떤 순서로 쓰나

```text
  io create /a/b/d              R=5 W=4 | R I0 R D0 R D1 R D2 R ibm W ibm W I0 W D2 W I0
  io mkdir /a/b/e               R=6 W=6 | R I0 R D0 R D1 R D2 R ibm W ibm R dbm W dbm W D3 W I0 W D2 W I0
  io create /a/b/f cache=0      R=12 W=4 | R I0 R D0 R I0 R D1 R I0 R D2 R ibm W ibm R I0 W I0 R I0 R D2 R D2 W D2 R I0 W I0
```

create 의 쓰기 4번은 순서가 정해져 있다: **inode 비트맵 → 새 inode → 부모 디렉터리 칸 → 부모 inode(size)**. mkdir 은 그 사이에 데이터 비트맵과 새 디렉터리 블록(`.`·`..`)이 끼고, 부모 inode 쓰기에 nlink+1 이 같이 실린다(`dir_add` 의 `nlink_delta`).

`demo_timeline.c` 로 OSTEP 그림 40.3/40.4 와 같은 시나리오(`/foo/bar` 생성 + 4 KB × 3 쓰기, 열기 + 3 읽기)를 실측했다:

<!-- code: demo_timeline.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_timeline.c -o demo_timeline && ./demo_timeline
```

```text
== cache=0  (I0 = inode block 0 holds inodes 0..15, D0 = root dir, D1 = /foo dir)
  create /foo/bar    R=10 W=4  | R I0 R D0 R I0 R D1 R ibm W ibm R I0 W I0 R I0 R D1 R D1 W D1 R I0 W I0
  write() #1         R=3  W=3  | R I0 R dbm W dbm W D2 R I0 W I0
  …
  create+3 writes    R=19 W=13   (OSTEP Fig 40.4: 10 + 3*5 = 25 I/O)
  open /foo/bar      R=5  W=0  | R I0 R D0 R I0 R D1 R I0
  read() #1          R=2  W=0  | R I0 R D2
  …
  open+3 reads       R=11 W=0    (OSTEP Fig 40.3: 5 + 3*(2R+1W atime) = 14 I/O)
== cache=1  (I0 = inode block 0 holds inodes 0..15, D0 = root dir, D1 = /foo dir)
  create /foo/bar    R=4  W=4  | R I0 R D0 R D1 R ibm W ibm W I0 W D1 W I0
  write() #1         R=2  W=3  | R I0 R dbm W dbm W D2 W I0
  …
  create+3 writes    R=10 W=13   (OSTEP Fig 40.4: 10 + 3*5 = 25 I/O)
  open /foo/bar      R=3  W=0  | R I0 R D0 R D1
  …
  open+3 reads       R=9  W=0    (OSTEP Fig 40.3: 5 + 3*(2R+1W atime) = 14 I/O)
```

| 연산 | OSTEP 그림 | 캐시 없음 (실측) | 연산 안 재사용 (실측) | 차이의 이유 |
|---|---|---|---|---|
| open `/foo/bar` | 5 R | **5 R** (같음) | 3 R | inode 3개가 한 블록 I0 |
| read 4 KB | 2 R + 1 W | 2 R | 2 R | atime 을 안 씀 (noatime) |
| create | 10 | 14 (10 R + 4 W) | 8 (4 R + 4 W) | OSTEP 은 연산 안에서 foo inode·디렉터리 블록을 들고 있다고 가정. 캐시 없음은 get-modify-put 마다 다시 읽음 |
| 할당 write | 5 | 6 (inode RMW 재읽기) | **5** (같음) | `R I0 R dbm W dbm W D W I0` |

정리하면 OSTEP 의 숫자는 "한 시스템 콜 동안은 읽은 블록을 들고 있되, inode 마다 다른 블록에 있다" 는 모델이다. 우리 캐시 모드가 그 모델이고, 차이는 inode 들이 같은 4 KB 블록에 모여 있어서 생긴다.

### Part A — 캐시 슬롯은 블록당 하나 (별칭 버그 방지)

create 에서 부모 inode(foo)와 새 inode(bar)는 **같은 블록 I0** 에 있다. 두 inode 를 각자의 4 KB 사본으로 읽어 고친 뒤 차례로 쓰면, 나중 쓰기가 앞 쓰기를 덮는다. 새 inode 가 사라지는 버그다(7. 자주 하는 실수 1). 이 코드의 규칙은 두 가지다. ① 캐시는 블록 번호당 슬롯 하나다(xv6 `bio.c` 의 "블록당 버퍼 하나" 불변식과 같은 생각). ② `iget/iput` 은 inode 를 구조체로 **복사**해 오고, 쓸 때는 그 순간의 블록 버퍼에 256 B 만 덮어 바로 쓴다. 슬롯 포인터를 다른 `bget` 너머로 들고 있지 않는다. 캐시를 꺼도(`g_cache=0`) 매번 디스크의 최신 블록을 읽고 고쳐 쓰므로 맞다. 느릴 뿐이다.

### Part B — 데이터 경로의 I/O

| 연산 (파일 /f, inode 1) | 실측 | trace |
|---|---|---|
| 새 블록 쓰기 (4 KB) | R2 W3 | `R I0 R dbm W dbm W D1 W I0` |
| 읽기 4 KB | R2 W0 | `R I0 R D1` — 비트맵 없음 |
| 블록 전체 덮어쓰기 | R1 W1 | `R I0 W D1` — inode 안 바뀜 → 안 씀 |
| 100 B 덮어쓰기 | R2 W1 | `R I0 R D1 W D1` — RMW |
| 블록 12 (indirect 생성) | R2 W5 | 위 Part A 참고 |
| 14 블록 읽기 | R16 (캐시 없음 17) | inode 1 + 데이터 14 + indirect 1 (캐시 없음: 2번) |
| sparse 파일의 hole 100 B 읽기 | R1 | inode 만 |
| unlink (14 블록 + indirect) | R5 W4 | `R I0 R D0 W D0 W I0 R D13 R dbm W dbm R ibm W ibm` |

읽기가 비트맵을 보지 않는 이유는 OSTEP 40.6 의 ASIDE 그대로다. inode·간접 블록이 이미 "어디에 있나" 를 다 알고 있고, 할당된 블록인지 다시 확인할 필요가 없다. unlink 는 파일 크기와 상관없이 비트맵을 **한 번** 쓴다. 지울 블록 15개를 모아 같은 버퍼에서 비트를 끄고 한 번 쓰기 때문이다. 블록마다 `bm_set` 하면 15번 쓴다.

꽉 찬 디스크(빈 블록 55): 60 블록을 쓰려 하면 direct 12 + indirect 1 + 데이터 42 = 55 블록을 쓰고 **221184 B(54 블록) 짧은 쓰기**를 돌려준다. 다음 1바이트는 `-ENOSPC` 다. 이어서 `mkdir` 은 inode 를 받은 뒤 데이터 블록이 없어 실패하는데, inode 비트를 되돌려 빈 inode 가 78 로 유지된다. fsck 는 각 단계에서 0 을 보고한다.

### Part B — unlink 와 링크 수: "가리켜지는 쪽 카운트는 늦게 내리고 빨리 올린다"

| 연산 | 순서 (이 코드) | 중간에 전원이 나가면 |
|---|---|---|
| unlink | ① 이름 지움 → ② nlink−1 → ③ inode 0 → ④ 블록 비트 → ⑤ inode 비트 | ①만: 고아 inode (fsck: orphan). ③까지: 새는 블록·inode. 모두 **누수**일 뿐 |
| link (주어짐) | ① 대상 nlink+1 → ② 이름 추가 | ①만: nlink 가 실제보다 큼 → 누수. 반대 순서면 nlink 가 작음 → 위험 |
| create | 비트맵 → inode → 이름 → 부모 size | 이름이 초기화된 inode 만 가리킨다 |

nlink 가 실제 이름 수보다 **작으면** 위험하다. 이름 하나를 지우는 순간 nlink 가 0 이 되어 블록이 반납되는데, 남은 이름이 그 블록을 계속 가리킨다. 다음 할당이 그 블록을 다른 파일에 주면 이중 소유가 된다. 연습 4 가 이것을 크래시 주입으로 보인다.

### Part C — fsck 의 다섯 단계

| 단계 | 하는 일 | 찾는 것 (보고 이름) | 고침 |
|---|---|---|---|
| ① inode 표 | 할당된 inode 의 direct / indirect / indirect 칸 포인터마다 범위 검사, `owner[b]` 기록 | `bad-ptr` (데이터 영역 밖: 9999, inode 표 블록 3), `dup-block` (이미 주인 있음) | 포인터 0. dup 은 빈 블록에 **복제**해서 나중 inode 에 줌 (OSTEP 42.2 "the pointed-to block could be copied") |
| ② 디렉터리 BFS | 루트부터 칸마다 대상 inode 확인, `refs[i]++` (`.`·`..` 포함), `seen[]` 으로 고리 방지 | `dangling-dirent` (범위 밖이거나 빈 inode) | 칸 지움 |
| ③ 참조 | 할당됐는데 `refs == 0`, `nlink ≠ refs` | `orphan-inode`, `nlink` | 고아는 블록·inode 반납 (e2fsck 는 lost+found 로 옮김), nlink = refs |
| ④ inode 비트맵 | 비트 vs `type != FREE` | `inode-bitmap` | 계산값으로 |
| ⑤ 데이터 비트맵 | 비트 vs `owner[b] ≥ 0` | `leaked-block` (켜짐·주인 없음), `unmarked-block` (주인 있음·꺼짐) | 계산값으로 |

주입 손상을 한꺼번에 넣었을 때의 실제 보고(`expect_fsck("all at once")`):

```text
  fsck all at once: 7 problem(s)
  [dup-block] inode 4 direct[0] = 13 is also used by inode 3
  [bad-ptr] inode 4 direct[5] = 7777 is outside the data region [8,64)
  [dangling-dirent] dir inode 1 entry 'sub' -> inode 5, which is not allocated
  [nlink] inode 1 nlink = 3, but directory entries naming it = 2
  [nlink] inode 3 nlink = 9, but directory entries naming it = 1
  [unmarked-block] block 12 (data 4) is used by inode 2 but marked free
  [leaked-block] block 48 (data 40) is marked used but no inode points to it
```

`/etc/sub` 를 해제된 inode 로 만들었더니 `/etc` 의 nlink 도 틀어졌다(3 → 실제 2). sub 의 `..` 가 더는 세어지지 않기 때문이다. 손상 하나가 다른 보고를 끌고 오는 전형적인 예다. 하나씩 주입한 6개 시나리오(leaked 1, dup 1, bad 2, dangling 1, nlink 2, orphan 1)도 모두 "검사 개수 = 고침 개수, 고친 뒤 0" 이고, 손상과 무관한 `/etc/cfg` 내용은 그대로다. dup 복제 뒤에는 `/empty` 에 쓰기를 해도 `/log` 가 안 바뀐다(두 사본이 독립).

**검사 = 고침 개수** 를 지키려면 고칠 때 생기는 변화가 뒤 단계에서 새 문제로 보이지 않아야 한다. 복제 블록은 고칠 때 비트맵을 바로 켜고, 고아의 블록은 바로 끈다. 안 그러면 고침 모드에서만 ⑤ 단계가 `unmarked` / `leaked` 를 추가로 보고한다.

fsck 가 **못 하는 것**: 내용이 맞는지는 모른다(OSTEP 42.2. 비트맵을 고쳤다고 데이터가 옳은 것은 아님). 해제 뒤 다시 할당된 inode 를 가리키는 이름은 "정상" 으로 보인다(엉뚱한 파일로 연결). 그리고 디스크 전체를 읽으므로 디스크 크기에 비례해 느리다. 그래서 저널링이 나왔다(OSTEP 42.3, OS-A07).

### Part C — 크래시 순서와 fsck (연습 4 실측)

```svg
<svg viewBox="0 0 740 210" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">create("/d/x") + write 4 KB = 쓰기 7번. k 번째 뒤 전원 차단 → 재마운트 → fsck</text>
<rect x="10" y="40" width="95" height="34" class="f-acc" stroke="currentColor"/><text x="18" y="62" fill="currentColor">W ibm</text>
<rect x="110" y="40" width="95" height="34" class="f-acc" stroke="currentColor"/><text x="118" y="62" fill="currentColor">W inode</text>
<rect x="210" y="40" width="95" height="34" class="f-acc" stroke="currentColor"/><text x="218" y="62" fill="currentColor">W dir칸</text>
<rect x="310" y="40" width="95" height="34" class="f-acc" stroke="currentColor"/><text x="318" y="62" fill="currentColor">W 부모 size</text>
<rect x="420" y="40" width="95" height="34" class="f-mut" stroke="currentColor"/><text x="428" y="62" fill="currentColor">W dbm</text>
<rect x="520" y="40" width="95" height="34" class="f-mut" stroke="currentColor"/><text x="528" y="62" fill="currentColor">W data</text>
<rect x="620" y="40" width="110" height="34" class="f-mut" stroke="currentColor"/><text x="628" y="62" fill="currentColor">W inode(ptr)</text>
<text x="10" y="98" fill="currentColor" font-size="12">k=1</text><text x="10" y="114" fill="currentColor" font-size="12">inode-bitmap</text>
<text x="110" y="98" fill="currentColor" font-size="12">k=2</text><text x="110" y="114" fill="currentColor" font-size="12">orphan-inode</text>
<text x="210" y="98" fill="currentColor" font-size="12">k=3 (size 전)</text><text x="210" y="114" fill="currentColor" font-size="12">orphan-inode</text>
<text x="310" y="98" fill="currentColor" font-size="12">k=4</text><text x="310" y="114" fill="currentColor" font-size="12">0 (빈 파일 생성됨)</text>
<text x="420" y="98" fill="currentColor" font-size="12">k=5</text><text x="420" y="114" fill="currentColor" font-size="12">leaked-block</text>
<text x="520" y="98" fill="currentColor" font-size="12">k=6</text><text x="520" y="114" fill="currentColor" font-size="12">leaked-block</text>
<text x="620" y="98" fill="currentColor" font-size="12">k=7</text><text x="620" y="114" fill="currentColor" font-size="12">0</text>
<rect x="10" y="135" width="720" height="60" rx="4" class="f-ok" stroke="currentColor"/>
<text x="20" y="157" fill="currentColor">모든 k 에서 "새는 것"(inode·블록이 쓰이지도 못하고 비지도 않음)만 남는다 — fsck 가 안전하게 회수.</text>
<text x="20" y="179" fill="currentColor">틀린 순서(link 에서 이름 먼저, nlink 나중): k=2 에서 nlink 1 &lt; 이름 2 → 위험 (연습 4)</text>
</svg>
```

k=3 이 고아인 이유: 디렉터리 칸은 썼지만 부모 inode 의 size 가 아직 옛 값이라, 새 칸이 size 밖에 있어 **안 보인다**. 칸을 먼저 쓰고 size 를 나중에 늘리는 순서 덕에 "size 안에 쓰레기 칸" 은 생기지 않는다. 한계도 분명하다. 이 시뮬레이션은 쓰기가 순서대로 디스크에 닿는다고 가정한다. 실제 디스크·SSD 는 쓰기 캐시가 순서를 바꿀 수 있다. 그래서 커널은 순서가 중요한 지점에 flush / FUA 를 건다(OS-B13 의 fsync, Don 의 SSD 경험의 PLP).

### OSTEP vsfs.py 교차 확인

`demo_vsfs_check.c` 는 OSTEP 시뮬레이터 `vsfs.py -c` 의 출력을 표준 입력으로 받는다. mkdir / creat / link / unlink / write 를 우리 VSFS 에 그대로 실행하고, **매 상태마다** inode 비트맵 · inode 줄(`[d a:0 r:2]` 형식, r = nlink) · 데이터 비트맵을 글자 그대로 비교한다. 데이터 줄은 디렉터리 칸을 정렬해서 비교한다(vsfs.py 는 지운 칸을 당겨 붙이고, 우리는 빈 칸을 재사용한다). 우리 배치는 inode 가 16 의 배수라 `-i 16 -d 16` 과 20 블록 디스크를 쓴다. 할당이 둘 다 first-fit 이어서 번호까지 같아야 한다.

<!-- code: demo_vsfs_check.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_vsfs_check.c -o demo_vsfs_check
OSTEP=/Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/operating-system-three-easy-pieces
for s in $(seq 1 20); do printf "seed %2d: " $s; python3 -I $OSTEP/.tools/ostep-homework/file-implementation/vsfs.py -s $s -n 40 -i 16 -d 16 -c | ./demo_vsfs_check | tail -1; done
```

실제 출력(seed 1–20, Python 3.9.6, 일부):

```text
seed  1: replayed 40 ops, compared 41 states x 4 lines: 0 mismatches, 0 failed ops; fsck problems=0
seed  2: replayed 40 ops, compared 41 states x 4 lines: 0 mismatches, 0 failed ops; fsck problems=0
seed  5: replayed 38 ops, compared 38 states x 4 lines: 0 mismatches, 0 failed ops; fsck problems=0
…
seed 17: replayed 34 ops, compared 34 states x 4 lines: 0 mismatches, 0 failed ops; fsck problems=0
seed 20: replayed 40 ops, compared 41 states x 4 lines: 0 mismatches, 0 failed ops; fsck problems=0
```

20 개 seed 모두 불일치 0 이다. 상태가 적은 seed(5, 11, 17)는 vsfs.py 가 `File system out of inodes; rerun with more via command-line flag?` 를 찍고 멈춘 경우다(seed 5 에서 확인). 검사기가 정말 틀린 것을 잡는지도 확인했다. seed 3 출력의 10번째 inode 줄을 `r:3` → `r:93` 으로 바꿔 넣으면 `MISMATCH after op 9 (inodes)` 와 `1 mismatches` 가 나온다.

### 펌웨어 연결: SPI NOR 위의 littlefs/FatFs, 그 아래의 FTL

- **블록 장치 층 = 드라이버 콜백.** 이 레슨의 `bread/bwrite` 자리에 FatFs 는 `diskio.c` 의 `disk_read/disk_write`(섹터 단위)를, littlefs 는 `lfs_config` 의 `read/prog/erase/sync` 콜백을 둔다(함수 이름은 기억 기준. 버전별 시그니처는 각 프로젝트 문서로 확인). 4 KB 블록은 많은 SPI NOR 의 섹터 지우기 단위와 맞는다(부품마다 다름, 데이터시트 확인).
- **플래시는 덮어쓰지 못한다.** vsfs 처럼 비트맵·inode 를 제자리에서 고쳐 쓰면, NOR 에서는 매번 4 KB 지우기 + 프로그램이 필요하다. 비트맵 블록 하나가 가장 먼저 닳는다(wear). littlefs 는 메타데이터 쌍과 copy-on-write 로 제자리 쓰기를 피하고, 전원 차단에도 이전 상태가 남도록 설계되어 있다(STOR-A03). FatFs 의 FAT 는 이 레슨의 비트맵 + 블록 체인에 가깝고, 전원 차단 안전성은 따로 챙겨야 한다(STOR-A05).
- **eMMC/SSD 에서는 FTL 이 그 아래에 있다.** 파일 시스템이 "블록 50 덮어쓰기" 를 보내면 FTL 은 새 NAND 페이지에 쓰고 L2P 매핑을 바꾼다(MEM-M10). 파일 시스템의 쓰기 순서는 FTL 의 전원 차단 복구(MEM-M14)와 flush/FUA 가 지켜 줘야 의미가 있다. fsck 의 "inode 를 믿고 비트맵을 다시 만든다" 는 FTL 의 "OOB 를 스캔해 L2P 를 다시 만든다" 와 같은 모양의 복구다. 로그 구조로 가면 OS-A08 이다.
- **부팅 경로**: 임베디드 Linux 는 보통 rootfs 를 읽기 전용으로 두고, 쓰는 파티션만 마운트 전에 fsck(또는 저널 재생)한다. 이 레슨의 `fs_mount` → `fsck(1)` 이 그 축소판이다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 121/121 passed, exit 0 (5번 + 다른 cwd 에서 1번, 모두 같음, 0.1 초)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o s_asan && ./s_asan   # 121/121, 보고 0
ls /tmp/OS-M14_work    # No such file or directory — 디스크 이미지는 열자마자 unlink, 작업 폴더도 비면 지움
```

컴파일러: Apple clang version 21.0.0 (clang-2100.0.123.102), Apple M6, macOS 27.2. 스레드는 쓰지 않으므로 TSan 은 해당 없다.

## 7. 자주 하는 실수

**1. 같은 블록의 두 사본 (버퍼 별칭).** 부모 inode 와 새 inode 를 각각 `uint8_t blk[4096]` 에 읽어 고친 뒤 차례로 쓰면, 둘이 같은 블록 I0 이라서 나중 쓰기가 앞의 변경을 지운다. 그러면 새 inode 가 type 0 으로 돌아가 fsck 가 `dangling-dirent` 를 보고한다. → 블록당 버퍼 하나 + inode 는 복사해 오고 쓸 때 그 순간의 버퍼에 256 B 만 덮기(`iget/iput`).

**2. 이름을 먼저 건다.** `dir_add` → `iput(new inode)` 순서면 그 사이에 전원이 나갈 때 이름이 쓰레기 inode(이전 파일의 잔해일 수도 있음)를 가리킨다. → 가리켜질 것(비트맵, inode, 블록 내용)을 먼저 쓰고 이름은 마지막. link 도 nlink+1 이 먼저다.

**3. 읽기 경로에서 비트맵을 확인한다.** "할당된 블록인지 확인" 하려고 `bm_get` 을 부르면 읽기마다 I/O 가 하나 는다. 할당 구조는 할당할 때만 본다(OSTEP 40.6). 테스트 `read blk0 → IO(2, 0)` 이 이것을 잡는다.

**4. 부분 쓰기 처리를 반대로 한다.** 새로 할당한 블록을 `bread` 해서 RMW 하면 쓰레기를 읽고 I/O 를 낭비한다. 반대로 기존 블록을 읽지 않고 `memset(0)` 위에 복사하면 블록의 나머지 바이트가 사라진다. → `fresh` 플래그로 구분한다: 새 블록은 0 채움, 기존 블록은 RMW, 4 KB 전체는 둘 다 아님.

**5. unlink 에서 nlink 를 무시한다.** 이름을 지울 때 바로 블록을 반납하면 하드 링크로 남은 다른 이름이 반납된 블록을 가리킨다. 다음 할당이 그 블록을 남에게 주면 `dup-block` 이 된다. → `--nlink > 0` 이면 inode 만 쓰고 끝.

**6. mkdir/rmdir 에서 부모 nlink 를 빼먹는다.** 자식의 `..` 도 부모를 가리키는 이름이다. 그래서 디렉터리 nlink = 2 + 하위 디렉터리 수다. 빼먹으면 fsck 가 `nlink` 를 보고한다(테스트 C-5 의 `/etc`). → `dir_add(…, nlink_delta=1)`, rmdir 은 −1(연습 1).

**7. fsck 가 고치는 중에 만든 변화를 다시 문제로 센다.** 복제한 블록을 비트맵에 안 켜 두면 고침 모드에서만 `unmarked-block` 이 추가로 나와 "검사 7개, 고침 8개" 가 된다. → 고침이 만든 할당·해제는 그 자리에서 비트맵에 반영한다. 디렉터리 순회에는 `seen[]` 을 둔다. 깨진 디스크에서는 디렉터리가 두 번 링크되어 고리가 생길 수 있다.

## 8. 면접 Q&A

<details><summary>Q1. open("/a/b/c") 는 디스크 블록을 몇 번 읽나요?</summary>

캐시가 하나도 없다면 2d+1 = 7번이다. 루트 inode, 루트 디렉터리 블록, a inode, a 디렉터리, b inode, b 디렉터리, c inode. 루트 inode 번호는 "잘 알려진" 값이라 찾을 필요가 없다(ext2/4 는 2, 이 레슨은 0). 실측 trace 는 `R I0 R D0 R I0 R D1 R I0 R D2 R I0` 이고, inode 들이 한 블록에 있어 연산 안에서 재사용하면 4번이다. 디렉터리가 크면 칸을 훑는 블록 수만큼 더 든다(연습 2: 2 블록 디렉터리의 뒤쪽 이름 +1). 꼬리: "실제로는?" → Linux 는 dentry 캐시·inode 캐시·페이지 캐시 덕에 자주 쓰는 경로는 I/O 0 이다. 큰 디렉터리는 ext4 가 해시 트리(htree, dir_index)로 칸 검색을 줄인다(기억 기준).
</details>

<details><summary>Q2. 4 KB 를 새로 덧붙이는 write() 한 번은 I/O 가 몇 번이고, 왜 그 순서인가요?</summary>

5번이다(실측 `R I0 R dbm W dbm W D1 W I0`). inode 를 읽어 다음 블록 칸이 비었는지 보고, 데이터 비트맵을 읽고·써서 블록을 할당하고, 데이터를 쓰고, 포인터와 size 가 바뀐 inode 를 쓴다. 순서는 비트맵 → 데이터 → inode 다. inode 가 가리키는 순간 그 블록은 이미 할당되어 있고 내용도 있어야 한다. 중간에 끊기면 새는 블록만 남는다(연습 4 의 k=5, 6). 꼬리: "덮어쓰기는?" → 블록 전체면 inode 읽기 + 데이터 쓰기 2번, 일부면 RMW 로 3번. inode 가 안 바뀌면 inode 쓰기가 없다.
</details>

<details><summary>Q3. 이 inode 구조의 최대 파일 크기는? 더 키우려면?</summary>

(12 + 1024) × 4096 = 4,243,456 B (약 4 MB). 2단 간접을 더하면 1024² 블록이 늘어 4,299,210,752 B 가 되고, 4 GiB 를 넘으므로 `uint32 size` 로는 표현이 안 된다. size 를 64비트로 바꿔야 한다(연습 3 이 계산하고 실제 할당 블록 수로 공식을 검증). ext2/ext3 는 12 direct + 1·2·3단 간접이다. ext4 는 기본이 extent(시작 블록, 길이)라서 연속 블록 묶음을 포인터 하나로 표현하고, 큰 파일의 메타데이터가 크게 준다(기억 기준). 꼬리: "왜 불균형 트리?" → 대부분 파일은 작아서(OSTEP 그림 40.2) 작은 파일은 inode 하나로 끝내고 큰 파일만 간접 블록 비용을 낸다.
</details>

<details><summary>Q4. 하드 링크와 링크 수. 디렉터리에는 왜 하드 링크를 못 거나요?</summary>

하드 링크는 같은 inode 번호를 가리키는 디렉터리 칸을 하나 더 만드는 것이다. inode 의 nlink 가 이름 수이고, 0 이 되면(그리고 연 사람이 없으면) 블록을 반납한다. 디렉터리의 nlink 는 2 + 하위 디렉터리 수다(자기 이름, 자기 `.`, 자식마다 `..`). 디렉터리 하드 링크를 허용하면 트리가 그래프가 되어 고리가 생길 수 있고, `..` 의 뜻(부모가 하나)이 깨지며, 순회·fsck 가 끝나지 않을 수 있다. 그래서 OSTEP 42.2 의 fsck 는 "어떤 디렉터리도 두 번 링크되지 않음" 을 검사한다. 이 코드의 `fs_link` 는 디렉터리면 `-EPERM` 이다. 꼬리: "심볼릭 링크는?" → 경로 문자열을 담은 별도 inode 다. 대상이 없어져도 남고(dangling symlink), 디렉터리·다른 파일 시스템도 가리킬 수 있다.
</details>

<details><summary>Q5. create 도중 전원이 나가면 어떤 상태들이 가능한가요?</summary>

쓰기 순서가 비트맵 → inode → 디렉터리 칸 → 부모 size 이므로 실측(연습 4)으로 이렇게 나온다. 1번 뒤: inode 비트만 켜짐(`inode-bitmap`, 누수). 2·3번 뒤: 초기화된 inode 가 있지만 이름이 없거나 size 밖에 있음(`orphan-inode`). 4번 뒤: 빈 파일이 생김(정상). 이어지는 write 는 5·6번 뒤 `leaked-block`, 7번 뒤 정상이다. **어떤 경우에도 이름이 쓰레기를 가리키거나 블록이 두 주인을 갖지 않는다.** fsck 가 누수만 회수하면 된다. 반대로 link 에서 이름을 먼저 걸면 nlink 1 < 이름 2 인 위험 상태가 나왔다. 꼬리: "순서를 지켜도 부족한 이유?" → 디스크 쓰기 캐시가 순서를 바꿀 수 있으므로 장벽(flush, FUA)이 필요하다. 그리고 fsck 는 디스크 전체를 읽어야 해서 느리다 → 저널링(OS-A07) / COW(littlefs, ZFS, btrfs) / soft updates.
</details>

<details><summary>Q6. fsck 는 무엇을 검사하고, 무엇은 못 하나요?</summary>

inode 를 진실로 놓고 나머지를 다시 계산한다. 포인터 범위(`bad-ptr` → 0), 블록 주인이 둘(`dup-block` → 복제), 이름이 빈 inode 를 가리킴(`dangling` → 칸 삭제), 이름 없는 inode(`orphan` → lost+found 또는 해제), nlink vs 실제 이름 수, 두 비트맵을 다시 만든다(OSTEP 42.2 의 목록과 같음). e2fsck 는 이것을 pass 1(inode·블록), 1B–1D(중복 블록), 2(디렉터리), 3(연결성, lost+found), 4(참조 수), 5(비트맵·요약)로 나눈다(기억 기준). 못 하는 것: 파일 **내용**이 맞는지(비트맵을 고쳐도 데이터는 옛 것일 수 있음), 재사용된 inode 를 가리키는 오래된 이름, 그리고 속도(디스크 크기에 비례). 꼬리: "검사와 고침이 같은 개수를 보고해야 하는 이유?" → 고침이 새 불일치를 만들면 반복 실행해도 수렴하지 않는다. 운영자가 `-n`(검사만)으로 본 것과 실제 고친 것이 같아야 믿을 수 있다.
</details>

<details><summary>Q7. 블록 캐시(버퍼 캐시)는 어떤 문제를 풀고, 어떤 문제를 만드나요?</summary>

푸는 것: 같은 블록을 반복해서 읽는 비용. 이 레슨에서는 연산 안 재사용만으로 `/a/b/c` 가 7 → 4, create 가 14 → 8 I/O 가 됐다. 커널은 연산 사이에도 캐시하고, 쓰기를 모았다가 한꺼번에 쓴다(write-back: 같은 비트맵 블록을 여러 create 가 고치면 한 번만 씀, OSTEP 40.7). 만드는 것: ① 같은 블록의 사본이 둘이면 갱신을 잃는다 → 블록당 버퍼 하나(xv6 bcache 의 불변식). ② write-back 은 쓰기 순서를 흐트러뜨리고 전원 차단 시 잃는 양을 늘린다 → fsync, 저널, 순서 의존성 추적. ③ 메모리. 이 레슨이 write-through 를 고른 이유는 순서와 I/O 수를 눈에 보이게 하기 위해서다. 꼬리: "읽기 캐시와 쓰기 캐시의 차이?" → 읽기는 충분히 크면 I/O 를 없애지만, 쓰기는 결국 디스크에 가야 영속성이 생긴다. 미룰 수 있을 뿐이다.
</details>

<details><summary>Q8. 이 설계를 SPI NOR 플래시에 그대로 올리면 어떤 문제가 생기나요?</summary>

① 제자리 갱신: 비트맵·inode·디렉터리 블록을 고칠 때마다 4 KB 섹터 지우기 + 프로그램이 필요하다. 지우기는 수십 ms 단위로 느리고(부품마다 다름), 그동안 전원이 나가면 블록 전체를 잃는다(기존 내용을 지웠는데 새 내용을 못 씀). ② 마모: 비트맵 블록은 할당·해제마다 지워지므로 가장 먼저 수명(보통 10만 회급, 데이터시트 확인)이 끝난다. ③ fsck 로 복구할 정보 자체가 지우기 도중에 사라질 수 있다. 그래서 littlefs 는 메타데이터 블록 쌍에 번갈아 쓰고(이전 사본이 늘 남음) COW 로 데이터를 옮기며 마모를 분산한다(STOR-A03). eMMC/SSD 는 FTL 이 이 문제를 안에서 풀어 주므로 그 위에서는 vsfs/ext4 같은 제자리 갱신 FS 가 동작한다(MEM-M10–M14). 꼬리: "Don 의 SSD 경험과 연결하면?" → 호스트의 4 KB 덮어쓰기 하나가 FTL 에서는 새 페이지 쓰기 + L2P 갱신 + 나중의 GC 가 된다. 파일 시스템의 5 I/O × FTL 의 쓰기 증폭 = 실제 NAND 쓰기다(STOR-A01).
</details>

## 9. 추가 연습

### 연습 1 — rmdir

빈 디렉터리만 지우는 `fs_rmdir` 를 쓰라. 비지 않으면 `-ENOTEMPTY`, 파일이면 `-ENOTDIR`, `/` 는 `-EINVAL`. 부모에서 무엇이 바뀌고, I/O 는 몇 번인가?

<details><summary>답</summary>

`.`·`..` 말고 쓰인 칸이 있으면 `-ENOTEMPTY`. 순서는 ① 부모에서 칸 지우기 → ② 부모 nlink−1(자식의 `..` 가 사라짐) → ③ 자식 inode 0 → ④ 디렉터리 블록 비트 → ⑤ inode 비트다. 실측 `R I0 R D0 R D1 W D0 W I0 W I0 R dbm W dbm R ibm W ibm`(R5 W5). `W I0` 가 두 번인 것은 부모 inode 와 자식 inode 가 같은 블록이기 때문이다. 끝나면 루트 nlink 2, 빈 블록 +1, 빈 inode +2(파일 x 와 디렉터리 d), fsck 0. practice.c `fs_rmdir`.
</details>

### 연습 2 — 블록 두 개짜리 디렉터리, 빈 칸 재사용

`/big` 에 파일 130 개를 만들면 디렉터리는 몇 블록인가? 캐시 없이 `/big/f0` 과 `/big/f129` 를 찾는 비용은? `f5` 를 지우고 `new` 를 만들면 쓰기는 몇 번인가?

<details><summary>답</summary>

칸 132 개(`.`·`..` 포함) × 32 B = 4224 B 라서 블록 2개다(`direct[1] ≠ 0`). 캐시 없이 f0 은 5번, f129 는 6번 읽는다(첫 블록을 다 훑고 둘째 블록). `new` 는 f5 의 빈 칸(칸 7)과 f5 가 쓰던 inode 7(first-fit)을 재사용한다. size 가 안 바뀌므로 부모 inode 를 쓰지 않아 쓰기 3번이다(`W ibm W I0 W D1`. 보통 create 는 4번). ls 순서에서 `f4 new f6` 으로 끼어든다. practice.c `ex2`.
</details>

### 연습 3 — 크기 → 블록 수, 최대 크기

`blocks_for(size)` = 데이터 블록 + (12 개를 넘으면 indirect 1)을 쓰고, 실제 쓰기 후 비트맵에서 센 블록 수와 비교하라. 2단 간접을 더하면 최대 크기는?

<details><summary>답</summary>

1 B → 1, 4096 → 1, 4097 → 2, 49152(12 블록) → 12, 49153 → 14(데이터 13 + indirect), 81920 → 21, 221184 → 55. 실측 모두 공식과 같다. 최대 크기는 1단 4,243,456 B, 2단을 더하면 4,299,210,752 B(> UINT32_MAX)이므로 size 필드를 64비트로 바꿔야 한다. practice.c `ex3`.
</details>

### 연습 4 — 크래시 주입 스윕

`D.crash_after` 로 "k 번째 쓰기 뒤 전원 차단" 을 흉내 내고, 재마운트 → fsck 하라. create+write, 올바른 link, 이름 먼저 거는 link 를 비교하라.

<details><summary>답</summary>

실측(practice.c `ex4`):

```text
  create+write /d/x: 7 writes
    crash after 1: 1 finding(s) inode-bitmap=1
    crash after 2: 1 finding(s) orphan-inode=1
    crash after 3: 1 finding(s) orphan-inode=1
    crash after 4: 0 finding(s)
    crash after 5: 1 finding(s) leaked-block=1
    crash after 6: 1 finding(s) leaked-block=1
  link /d/x /y (nlink first): 3 writes
    crash after 1: 1 finding(s) nlink=1
    crash after 2: 1 finding(s) nlink=1
  link, name first (buggy): 3 writes
    crash after 1: 0 finding(s)
    crash after 2: 1 finding(s)  ** DANGEROUS ** nlink=1
```

올바른 순서는 모든 k 에서 누수(inode 비트 켜짐 · 고아 · nlink 큼 · 새는 블록)만 남는다. 버그 판 k=1 은 칸을 썼지만 루트 size 가 안 늘어 안 보이는 경우라 문제가 없다. k=2 는 size 까지 늘고 nlink 는 그대로라 nlink 1 < 이름 2 → 위험이다. 판정은 `dangerous()` 가 보고 줄에서 "nlink < 이름 수", "비트맵은 빈데 inode 는 사용 중" 을 골라낸다. 한계: 쓰기가 순서대로 닿는다고 가정했다(실제 장치는 flush 없이는 보장 안 됨).
</details>

### 연습 5 — vsfs.py 와 글자 그대로 같은 상태

`vsfs.py -s 17 -n 6 -i 16 -d 16 -c` 의 연산 6개(mkdir /u, creat /a, unlink /a, mkdir /z, mkdir /s, creat /z/x)를 실행하고, 같은 형식으로 상태를 찍어 시뮬레이터의 마지막 상태와 비교하라.

<details><summary>답</summary>

20 블록 디스크(3 + inode 1 블록 + 데이터 16), `mkfs(16)`. 결과는 vsfs.py 실제 출력과 세 줄 모두 같다:

```text
inode bitmap  1111100000000000
inodes       [d a:0 r:5][d a:1 r:2][d a:2 r:2][d a:3 r:2][f a:-1 r:1][][][][][][][][][][][]
data bitmap   1111000000000000
```

`/a` 를 지운 뒤 `/z` 가 inode 2 를 다시 받는 것(first-fit), 루트 nlink 5(= 2 + 하위 디렉터리 3)가 그대로 맞는다. 20 개 seed 전체 비교는 `demo_vsfs_check.c`(5. 풀이). practice.c `ex5`.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice    # 23/23 passed, exit 0 (solution.c 를 같은 폴더에서 #include)
```

실제 출력 일부:

```text
-- 연습 1: rmdir
  io rmdir /d  R=5 W=5 | R I0 R D0 R D1 W D0 W I0 W I0 R dbm W dbm R ibm W ibm
PASS IO(5, 5)
-- 연습 2: directory spanning two blocks, slot reuse
  namei cache=0: /big/f0 R=5, /big/f129 R=6
  io create into reused slot  R=5 W=3 | R I0 R D0 R D1 R D2 R ibm W ibm W I0 W D1
-- 연습 3: size -> blocks, max file size
  max with 1 indirect = 4243456 B, + double indirect = 4299210752 B
  size  49153 -> used 14 blocks (formula 14)
…
23/23 passed
```

ASan+UBSan 빌드(`-g -O1 -fsanitize=address,undefined`)에서도 23/23, 보고 0.

## 10. 핵심 정리

- 배치: superblock · inode 비트맵 · 데이터 비트맵 · inode 표 · 데이터. inode n → 블록 `inode_start + n/16`, 오프셋 `(n%16)×256`. 블록 포인터는 절대 번호, 0 = 없음.
- 디렉터리 = {inum, name} 칸 배열을 담은 파일. `.`·`..` 도 실제 칸이다. 디렉터리 nlink = 2 + 하위 디렉터리 수.
- 경로 탐색은 이름마다 "디렉터리 블록 + inode" 라서 캐시 없이 2d+1 번 읽는다(`/a/b/c` = 7, 재사용 시 4). 읽기는 비트맵을 보지 않는다.
- 할당 쓰기 = 5 I/O(inode R, 비트맵 R/W, 데이터 W, inode W). 덮어쓰기 2, 부분 덮어쓰기 3(RMW), 간접 블록이 생기는 쓰기 7.
- 쓰기 순서: 가리켜질 것(비트맵 → inode/데이터) 먼저, 이름·포인터는 나중. 참조 수는 빨리 올리고 늦게 내린다. 그러면 크래시가 남기는 것은 누수뿐이다(실측 스윕).
- 블록 캐시는 블록당 버퍼 하나. 같은 블록 사본 둘 = 갱신 분실.
- fsck = inode 를 믿고 비트맵·링크 수를 다시 계산. bad-ptr → 0, dup → 복제, dangling → 칸 삭제, orphan → 회수, 비트맵 재작성. 내용의 옳음은 모르고, 느리다 → 저널링(OS-A07).
- 플래시 위에서는 제자리 갱신이 지우기·마모·전원 차단 문제가 된다 → littlefs 의 COW/메타데이터 쌍, 아래에는 FTL.

## 참고자료

- OSTEP 40장 "File System Implementation" — 40.2 배치, 40.3 inode·다단계 인덱스, 40.4 디렉터리, 40.5 비트맵, 40.6 접근 경로(그림 40.3 읽기 타임라인, 40.4 생성 타임라인, ASIDE "Reads don't access allocation structures"), 40.7 캐시. 로컬: `operating-system-three-easy-pieces/book-md/C40_file_system_implementation.md`, Don 노트 `notes/2026-09-30_C40_file_system_implementation.md`.
- OSTEP 42장 "Crash Consistency: FSCK and Journaling" — 42.1 크래시 시나리오, 42.2 fsck 검사 목록(superblock, free blocks, inode state, inode links, duplicates, bad blocks, directory checks), 42.3 저널링. 로컬 `book-md/C42_crash_consistency_fsck_and_journaling.md`.
- OSTEP homework `file-implementation/vsfs.py` (README 포함) — `python3 -I vsfs.py -s N -n 40 -i 16 -d 16 -c` 로 실행, 교차 확인에 사용.
- xv6 book (MIT 6.1810/6.828), "File system" 장 — buffer cache 층의 "블록당 버퍼 하나", inode 층, 디렉터리 층. https://pdos.csail.mit.edu/6.828/ 에서 최신판 (장 구성·URL 은 기억 기준).
- e2fsck(8) 매뉴얼과 Linux `Documentation/filesystems/ext4/` — pass 구성, extent, htree 는 기억 기준이며 이 레슨에서 실행해 보지 않음.
- POSIX `pread`/`pwrite`, `mkstemp`, `unlink`, `link` — https://pubs.opengroup.org/onlinepubs/9799919799/ (unlink 로 이름을 지워도 열린 fd 로는 계속 접근 가능 — `fstat` 의 `st_nlink == 0` 을 테스트가 확인).
- FatFs (elm-chan.org) `diskio` 인터페이스, littlefs (github.com/littlefs-project/littlefs) `lfs_config` 블록 장치 콜백과 DESIGN.md — 함수 시그니처는 기억 기준.
- 관련 아이템: OS-B13 (원자적 저장, fsync), OS-A07 (write-ahead 저널링 + 크래시 주입), OS-A08 (로그 구조 FS 와 FTL), STOR-A03 (LittleFS 내부), STOR-A05 (FatFs vs LittleFS vs raw KV), STOR-A01 (앱 WA × FTL WA), MEM-M10 (L2P), MEM-M14 (FTL 전원 차단 복구), DS-A09 (FTL 매핑 구조).
