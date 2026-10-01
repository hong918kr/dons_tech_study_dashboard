# Ch.40 파일 시스템 구현 — vsfs로 보는 디스크 위 자료구조와 접근 경로

> 📖 원문: [40. File System Implementation](../book-md/C40_file_system_implementation.md) · [PDF p.478](../Operating%20Systems%20-%20Three%20Easy%20Pieces.pdf#page=478) · ⏱️ 읽기 약 60분 (시뮬레이터 + 코드 포함 시 +40분) · 🔗 선행: [Ch.39 파일과 디렉터리](2026-09-30_C39_files_and_directories.md), [Ch.37 하드 디스크](2026-09-30_C37_hard_disk_drives.md)

## 0. 한눈에 보기

> **CRUX — "How can we build a simple file system? What structures are needed on the disk? What do they need to track? How are they accessed?"**
> (간단한 파일 시스템을 어떻게 만들까? 디스크 위에 어떤 구조가 필요하고, 그 구조들은 무엇을 추적하며, 어떻게 접근되는가?)

- 파일 시스템은 **순수 소프트웨어**다. CPU/메모리 가상화와 달리 새 하드웨어 기능이 필요 없다. 그래서 설계 자유도가 크고, 종류도 많다.
- 파일 시스템을 이해하는 두 축: **자료구조**(디스크 위에 무엇이 어떻게 놓이나) + **접근 방법**(`open/read/write` 가 그 구조 중 무엇을 읽고 쓰나).
- vsfs(Very Simple File System) = 4KB 블록 64개짜리 장난감 디스크: **superblock · inode bitmap · data bitmap · inode table · data region**.
- inode 하나에 파일의 모든 메타데이터 + 데이터 블록 위치. 큰 파일은 **multi-level index**(direct → indirect → double → triple)로, "대부분 파일은 작다"는 관찰에 맞춘 **불균형 트리**.
- 한 번의 `open`/`create`/`write` 가 디스크 I/O를 놀랄 만큼 많이 만든다 (create 10회, 할당 write 1회당 5회). 그래서 **캐싱(page cache)** 과 **쓰기 버퍼링**이 필수이고, 쓰기 버퍼링은 **내구성 vs 성능** 트레이드오프를 만든다 → [Ch.42 저널링](2026-09-30_C42_crash_consistency_journaling.md)의 출발점.

## 1. 5분 복습표

| 용어 | 한 줄 뜻 | 예시 / 비유 |
|---|---|---|
| 블록 (block) | 파일 시스템이 디스크를 나누는 고정 크기 단위 | vsfs: 4KB, 블록 0~63 |
| 데이터 영역 (data region) | 사용자 데이터가 들어가는 블록들 | vsfs: 블록 8~63 (56개) |
| inode (index node) | 파일 하나의 메타데이터 + 데이터 블록 위치 | ext2 inode 128/256B |
| i-number (inumber) | inode 번호 = 파일의 low-level 이름 | 루트 디렉터리는 보통 2번 |
| inode table | inode 들의 배열이 들어 있는 고정 영역 | vsfs: 블록 3~7, inode 80개 |
| bitmap | 객체 하나당 1비트(0=free, 1=used) | inode bitmap, data bitmap |
| superblock | 파일 시스템 전체 정보(크기, 영역 위치, magic) | mount 때 제일 먼저 읽음 |
| direct pointer | inode 안에 직접 든 데이터 블록 주소 | 보통 12개 → 48KB |
| indirect pointer | "포인터들이 든 블록"을 가리키는 포인터 | 1024개 추가 → ~4MB |
| multi-level index | direct + single/double/triple indirect 불균형 트리 | ext2/ext3, 원조 UNIX FS |
| extent | (시작 주소, 길이) 한 쌍 | ext4, XFS |
| FAT | 블록별 "다음 블록" 링크를 표로 모은 것 | MS-DOS/Windows FAT |
| 디렉터리 엔트리 | (이름, inode 번호) + reclen, strlen | `.` 과 `..` 은 항상 있다 |
| page cache (unified) | VM 페이지와 파일 블록을 같이 담는 동적 캐시 | Linux/macOS/NetBSD UBC |
| write buffering | 쓰기를 5~30초 모았다가 내보내기 | `fsync()` 로 강제 플러시 |

## 2. 생각하는 법 (40.1)

파일 시스템을 공부할 때 머릿속에 그려야 할 **멘탈 모델**은 딱 두 질문이다.

1. **자료구조** — 디스크 위에 데이터와 메타데이터를 무엇으로 정리하나? (vsfs는 블록·객체의 단순 배열, XFS 같은 FS는 트리)
2. **접근 방법** — `open()`, `read()`, `write()` 호출이 그 구조 중 무엇을 **읽고**, 무엇을 **쓰나**? 얼마나 효율적인가?

이 장 전체가 이 두 질문에 대한 답이다. 코드를 외우는 게 아니라 "이 시스템 콜이 디스크 어디를 몇 번 건드리나"를 그릴 수 있으면 이해한 것이다.

## 3. 전체 구성: 64블록짜리 디스크 쪼개기 (40.2)

OSTEP 스타일로 "가장 단순한 것부터 하나씩 추가"해 보자.

1. 디스크를 **4KB 블록** 64개(0~63)로 나눈다. 블록 크기는 하나만 쓴다.
2. 대부분의 공간은 **사용자 데이터**여야 한다 → 마지막 56블록(8~63)을 **data region**.
3. 파일마다 메타데이터(크기, 소유자, 권한, 시각, 데이터 블록 위치)가 필요 → **inode table** 에 5블록(3~7). inode가 256B면 블록당 16개 → **inode 80개 = 이 FS가 담을 수 있는 최대 파일 수**.
4. 무엇이 비었고 무엇이 쓰이는지 알아야 한다 → **inode bitmap**(블록 1), **data bitmap**(블록 2). 4KB 비트맵은 32K개 객체를 추적할 수 있으니 과하지만, 단순함을 위해 블록 하나씩.
5. 남은 블록 0 = **superblock**: inode 80개 / 데이터 블록 56개, inode table 시작 위치(블록 3), 그리고 이 FS가 vsfs라는 **magic number**.

mount 할 때 OS는 **superblock 을 먼저 읽어** 파라미터를 초기화한 뒤 볼륨을 디렉터리 트리에 붙인다. 이후 모든 접근은 "superblock이 알려준 위치"에서 시작한다.

```svg
<svg viewBox="0 0 700 300" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <defs><marker id="C40-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <g stroke="currentColor" fill="none">
    <rect x="20" y="40" width="40" height="44" style="fill:var(--accent-soft)"/>
    <rect x="60" y="40" width="40" height="44"/>
    <rect x="100" y="40" width="40" height="44"/>
    <rect x="140" y="40" width="200" height="44" style="fill:var(--accent-soft)"/>
    <rect x="340" y="40" width="340" height="44"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="40" y="67" font-weight="bold">S</text>
    <text x="80" y="67">i</text>
    <text x="120" y="67">d</text>
    <text x="240" y="60" font-weight="bold">Inode table</text>
    <text x="240" y="77" font-size="11">블록 3–7, inode 80개</text>
    <text x="510" y="60" font-weight="bold">Data region</text>
    <text x="510" y="77" font-size="11">블록 8–63 (56개)</text>
    <text x="40" y="30" font-size="11">0</text><text x="80" y="30" font-size="11">1</text><text x="120" y="30" font-size="11">2</text>
    <text x="160" y="30" font-size="11">3</text><text x="320" y="30" font-size="11">7</text><text x="360" y="30" font-size="11">8</text><text x="660" y="30" font-size="11">63</text>
  </g>
  <g fill="currentColor" font-size="11">
    <text x="20" y="104">S: superblock (magic, inode 수, 영역 위치)</text>
    <text x="20" y="120">i: inode bitmap · d: data bitmap (0=free 1=used)</text>
  </g>
  <g stroke="currentColor" fill="none">
    <rect x="140" y="170" width="100" height="70"/>
    <rect x="240" y="170" width="100" height="70" style="stroke:var(--accent);fill:var(--accent-soft)"/>
    <rect x="340" y="170" width="100" height="70"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="11">
    <text x="190" y="160">iblock 0 (12KB)</text>
    <text x="290" y="160">iblock 1 (16KB)</text>
    <text x="390" y="160">iblock 2 (20KB)</text>
    <text x="190" y="200">inode 0–15</text>
    <text x="290" y="200">inode 16–31</text>
    <text x="390" y="200" font-weight="bold">inode 32–47</text>
    <text x="390" y="220">sector 40</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="460" y="185">inode 32 →</text>
    <text x="460" y="203">32×256 = 8192B 오프셋</text>
    <text x="460" y="221">12KB + 8KB = 20KB</text>
    <text x="460" y="239">20×1024 / 512 = sector 40</text>
  </g>
  <g stroke="currentColor" fill="none" stroke-width="1.3" marker-end="url(#C40-arrow)">
    <path d="M240,86 L190,166"/>
    <path d="M455,200 L444,205"/>
  </g>
  <text x="20" y="280" fill="currentColor" font-size="11">위: vsfs 64블록 전체 배치 · 아래: inode table 확대 — inode 번호만 알면 위치가 산수로 나온다</text>
</svg>
```

## 4. 파일 정리: inode (40.3)

### 4.1 inode 번호 → 디스크 위치 계산

vsfs처럼 단순한 FS에서는 **i-number만 알면 inode의 디스크 위치를 곱셈 한 번으로** 안다. 이게 "index node"라는 이름의 유래다 — 배열의 인덱스로 찾는다.

```c
blk    = (inumber * sizeof(inode_t)) / blockSize;
sector = ((blk * blockSize) + inodeStartAddr) / sectorSize;
```

원문 예제: inode 32 → 오프셋 32×256 = 8192B = 8KB → inode table 시작(12KB) + 8KB = 20KB → 디스크는 바이트 주소가 아니라 512B 섹터 주소를 쓰므로 20×1024/512 = **섹터 40**. (아래 7절의 C 코드가 같은 계산을 여러 i-number로 돌린다.)

**새 예제 — inode 79:** 79×256 = 20224B → blk = 20224/4096 = 4 (inode table의 마지막 블록), 블록 안 슬롯 = (20224 mod 4096)/256 = 15 → 바이트 주소 4×4KB + 12KB = 28KB → 섹터 28×1024/512 = **56**. 즉 inode 블록 4(블록 7)의 마지막 칸.

### 4.2 inode 안에는 무엇이 있나

ext2 inode(원문 그림 40.1)의 핵심 필드: `mode`(권한), `uid/gid`, `size`, `atime/ctime/mtime/dtime`, `links_count`, `blocks`, `flags`, **`block[15]`(60바이트 = 포인터 15개)**, `generation`(NFS용), ACL. 파일 **타입은 디렉터리 엔트리에도** 들어간다(원문 각주). 사용자 데이터가 아닌 이런 정보를 통틀어 **메타데이터**라 한다.

### 4.3 multi-level index — 왜 불균형 트리인가

가장 단순한 설계: inode 안에 **direct pointer** 몇 개. 문제: 블록 크기 × 포인터 수보다 큰 파일은 못 만든다.

해법을 한 단계씩:

1. **indirect pointer**: 데이터 대신 "포인터 1024개(4KB/4B)가 든 블록"을 가리킨다. 파일이 커지면 그때 데이터 영역에서 indirect 블록을 할당. 12 direct + 1 indirect → (12 + 1024) × 4KB = **4144KB**.
2. **double indirect**: 포인터 블록들을 가리키는 포인터 블록 → 1024×1024 블록 추가 → **4GB 넘게**.
3. **triple indirect**: 한 단계 더 → 원문의 "pretty big" 질문의 답은 약 **4TB** (아래 계산).

계산 (4KB 블록, 4B 포인터, p = 1024):

```text
direct           12                    =            12 블록 =  48 KB
+ single         12 + 1024             =          1036 블록 ≈ 4.05 MB
+ double         + 1024^2 = 1,048,576  =     1,049,612 블록 ≈ 4.00 GB
+ triple         + 1024^3              = 1,074,791,436 블록 ≈ 4.00 TB
```

왜 균형 트리가 아니라 이런 **불균형 트리**인가? 측정 연구(원문 그림 40.2, Agrawal FAST'07)가 반복해서 말하는 "진리": **대부분 파일은 작다**(가장 흔한 크기 ~2KB), 평균은 커지는 중(~200KB), **바이트 대부분은 소수의 큰 파일**에 있고, FS는 대략 절반만 차 있고, 디렉터리는 대부분 작다(20개 이하). 작은 파일은 inode 하나로 끝내고(추가 I/O 0), 큰 파일만 간접 블록 비용을 내게 하는 구조다.

```svg
<svg viewBox="0 0 700 330" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <defs><marker id="C40-arrow2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <g stroke="currentColor" fill="none">
    <rect x="20" y="30" width="130" height="270" rx="6"/>
    <rect x="30" y="70" width="110" height="80" style="fill:var(--accent-soft)"/>
    <rect x="30" y="160" width="110" height="30"/>
    <rect x="30" y="200" width="110" height="30"/>
    <rect x="30" y="240" width="110" height="30" stroke-dasharray="4 3"/>
    <rect x="230" y="40" width="70" height="26"/><rect x="230" y="72" width="70" height="26"/><rect x="230" y="104" width="70" height="26"/>
    <rect x="230" y="160" width="90" height="34" style="stroke:var(--accent)"/>
    <rect x="400" y="150" width="70" height="26"/><rect x="400" y="182" width="70" height="26"/>
    <rect x="230" y="220" width="90" height="34" style="stroke:var(--accent)"/>
    <rect x="380" y="225" width="90" height="34" style="stroke:var(--accent)"/>
    <rect x="550" y="215" width="70" height="26"/><rect x="550" y="247" width="70" height="26"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="85" y="52" font-weight="bold">inode</text>
    <text x="85" y="100">direct ×12</text>
    <text x="85" y="118" font-size="11">작은 파일은</text>
    <text x="85" y="133" font-size="11">여기서 끝 (48KB)</text>
    <text x="85" y="180">single ind.</text>
    <text x="85" y="220">double ind.</text>
    <text x="85" y="260">triple ind.</text>
    <text x="265" y="58" font-size="11">data</text><text x="265" y="90" font-size="11">data</text><text x="265" y="122" font-size="11">… ×12</text>
    <text x="275" y="182" font-size="11">ptr ×1024</text>
    <text x="435" y="168" font-size="11">data</text><text x="435" y="200" font-size="11">… ×1024</text>
    <text x="275" y="242" font-size="11">ptr ×1024</text>
    <text x="425" y="247" font-size="11">ptr ×1024</text>
    <text x="585" y="233" font-size="11">data</text><text x="585" y="265" font-size="11">… ×1024²</text>
  </g>
  <g stroke="currentColor" fill="none" stroke-width="1.3" marker-end="url(#C40-arrow2)">
    <path d="M140,85 L228,53"/>
    <path d="M140,100 L228,85"/>
    <path d="M140,175 L228,177"/>
    <path d="M320,172 L398,163"/>
    <path d="M320,182 L398,195"/>
    <path d="M140,215 L228,237"/>
    <path d="M320,237 L378,242"/>
    <path d="M470,236 L548,228"/>
    <path d="M470,248 L548,260"/>
  </g>
  <g fill="currentColor" font-size="11">
    <text x="190" y="300">읽기 비용(캐시 없음): direct = inode+data 2회 · single = 3회 · double = 4회 · triple = 5회</text>
  </g>
</svg>
```

### 4.4 대안: extent 와 linked/FAT

> **TIP — extent 기반 접근을 고려하라.** extent = (디스크 시작 주소, 길이). 블록마다 포인터를 두는 대신 연속 구간 하나를 12바이트 정도로 표현한다. 포인터 방식은 **가장 유연하지만 큰 파일에서 메타데이터가 많고**, extent는 **덜 유연하지만 훨씬 작다** — 연속 공간이 충분할 때(어차피 모든 할당 정책의 목표) 잘 동작한다. ext4·XFS가 extent를 쓴다. 가상 메모리의 세그먼트와 닮았다.

7절 코드의 계산: 연속 1GB 파일을 포인터 방식으로 표현하면 **간접 블록 257개 = 1028KB 메타데이터**, extent 방식이면 **extent 1개**.

> **ASIDE — linked 방식과 FAT.** inode에 첫 블록 포인터 하나만 두고 각 데이터 블록 끝에 "다음 블록" 포인터를 둔다. 마지막 블록 읽기·랜덤 접근이 끔찍하다(체인을 다 따라가야 함). 그래서 "다음" 포인터들만 모아 **메모리에 올린 표**로 만든 게 **FAT(File Allocation Table)**. FAT에는 inode가 없고 디렉터리 엔트리가 메타데이터와 첫 블록을 직접 들고 있어서 **하드 링크가 불가능**하다.

## 5. 디렉터리 정리 (40.4)

디렉터리는 **(엔트리 이름, inode 번호) 쌍의 리스트**다. 원문 예: `dir`(inode 5) 안에 foo(12), bar(13), foobar(24).

```text
inum | reclen | strlen | name
   5       4        2     .
   2       4        3     ..
  12       4        4     foo
  13       4        4     bar
  24       8        7     foobar
```

- `strlen` = 이름의 실제 길이(+NUL), `reclen` = 이 레코드가 차지하는 총 바이트(남는 공간 포함).
- `unlink()` 로 중간 엔트리를 지우면 구멍이 생긴다 → inode 번호 0 같은 예약값으로 표시하고, **reclen 덕분에** 나중에 더 작은 이름이 그 큰 칸을 재사용할 수 있다.
- 디렉터리는 **특수한 종류의 파일**이다: inode table에 inode(타입 = directory)가 있고, 데이터 블록(필요하면 간접 블록도)은 데이터 영역에 있다. 그래서 디스크 구조를 새로 만들 필요가 없다.
- 선형 리스트만 있는 건 아니다. XFS는 디렉터리를 **B-tree** 로 저장해 "이름이 이미 있나?"를 확인해야 하는 create가 빠르다. (ext4의 htree도 같은 동기.)

## 6. 빈 공간 관리 (40.5)

vsfs는 비트맵 두 개로 관리한다. 파일을 만들면: inode bitmap에서 0인 비트를 찾아 1로 바꾸고 inode 할당 → (나중에) 비트맵을 디스크에 기록. 데이터 블록도 같은 방식.

- **사전 할당(pre-allocation)** 휴리스틱: ext2/ext3는 새 파일에 블록이 필요하면 **연속된 빈 블록 8개**쯤을 찾아 한꺼번에 준다 → 파일 일부가 디스크에서 연속이 되어 성능↑. ([Ch.41 FFS](2026-09-30_C41_ffs.md)의 "디스크를 디스크답게 쓰기"로 이어진다.)

> **ASIDE — 빈 공간 관리 방법들.** 옛 FS는 **free list**(superblock이 첫 free 블록을, 그 블록이 다음 free 블록을 가리킴), 현대 XFS는 빈 구간을 **B-tree** 로 관리한다. 시간/공간 트레이드오프의 선택일 뿐.

## 7. 접근 경로: 읽기와 쓰기 (40.6)

가정: FS는 mount되어 **superblock만 메모리**에 있고, inode·디렉터리는 전부 디스크에 있다.

### 7.1 `/foo/bar` 열고 읽기 (그림 40.3)

1. `open("/foo/bar", O_RDONLY)` — 이름밖에 없으니 **경로를 걸어서(traverse)** bar의 inode를 찾아야 한다.
2. 모든 탐색은 루트에서 시작. 루트는 부모가 없으니 i-number가 **잘 알려진 값**이어야 한다 → UNIX FS는 보통 **2번**. 루트 inode가 든 블록을 읽는다.
3. 루트 inode의 포인터로 루트 디렉터리 데이터를 읽어 `foo` 엔트리를 찾는다 → foo의 i-number(예: 44).
4. foo inode 읽기 → foo 데이터 읽기 → `bar` 의 i-number → **bar inode 읽기**. 권한 검사 후 per-process open file table에 fd 할당.
5. `read()` 마다: inode를 참조해 블록 위치 확인 → 데이터 블록 읽기 → **inode의 atime 갱신(write)**. 오프셋은 open file table에서 증가.
6. `close()`: fd 해제만. **디스크 I/O 없음.**

| 시스템 콜 | data bitmap | inode bitmap | root inode | foo inode | bar inode | root data | foo data | bar data[i] |
|---|---|---|---|---|---|---|---|---|
| open(bar) |  |  | read ① | read ③ | read ⑤ | read ② | read ④ |  |
| read() |  |  |  |  | read, write |  |  | read [0] |
| read() |  |  |  |  | read, write |  |  | read [1] |
| read() |  |  |  |  | read, write |  |  | read [2] |

(원문 그림 40.3을 표로 다시 그림. 번호는 open 안에서의 순서.)

> **ASIDE — 읽기는 할당 구조(bitmap)를 건드리지 않는다.** 학생들이 자주 헷갈리는 부분. 이미 inode가 블록을 가리키고 있으니 "이 블록이 할당됐나" 확인할 필요가 없다. 비트맵은 **할당이 필요할 때만** 접근한다.

open의 I/O 양은 **경로 길이에 비례**한다: 디렉터리 하나당 inode + data 최소 2회. 디렉터리가 크면 data 블록을 여러 개 읽어야 할 수도 있다.

### 7.2 `/foo/bar` 만들고 3블록 쓰기 (그림 40.4)

쓰기는 더 나쁘다. 새 블록을 **할당**해야 하기 때문.

| 시스템 콜 | data bitmap | inode bitmap | root inode | foo inode | bar inode | root data | foo data | bar data[i] |
|---|---|---|---|---|---|---|---|---|
| create(/foo/bar) |  | read, write | read | read, write | read, write | read | read, write |  |
| write() | read, write |  |  |  | read, write |  |  | write [0] |
| write() | read, write |  |  |  | read, write |  |  | write [1] |
| write() | read, write |  |  |  | read, write |  |  | write [2] |

- **create = 10 I/O**: 경로 탐색 4회(root inode·data, foo inode·data) + inode bitmap R/W(빈 inode 찾고 표시) + foo data W(엔트리 추가) + bar inode R/W(초기화) + foo inode W(크기·mtime 갱신). 디렉터리가 커져야 하면 data bitmap + 새 디렉터리 블록 I/O가 더 붙는다.
- **할당하는 write 1회 = 5 I/O**: data bitmap R/W, inode R/W, 데이터 W.

**새 예제 — 경로 깊이와 I/O 수.** 디렉터리 d개를 지나는 경로(루트 포함 d개)에서 캐시가 하나도 없으면 open = 2d + 1, create = 2d + 6. `/a/b/c/d/file`(디렉터리 5개: /, a, b, c, d) 를 create하고 4KB 블록 2개를 쓰면 (2×5 + 6) + 2×5 = **26 I/O**. 디스크 랜덤 I/O 하나가 ~10ms면 **0.26초** — 파일 하나 만드는 데! 아래 코드가 깊이별 표를 출력한다.

> **CRUX — "How can a file system reduce the high costs of doing so many I/Os?"** (이 많은 I/O 비용을 FS는 어떻게 줄일까?)

## 8. 캐싱과 버퍼링 (40.7)

### 8.1 읽기: 캐시

캐시가 없으면 `/1/2/3/.../100/file.txt` 를 여는 데 **수백 번** 읽어야 한다. 그래서:

- 초기 FS: 부팅 때 메모리의 ~10%를 **고정 크기 buffer cache** 로 잡고 LRU 등으로 관리. 문제: FS가 그만큼 안 쓰면 그 메모리는 낭비(**정적 분할**).
- 현대 OS: VM 페이지와 FS 페이지를 하나로 합친 **unified page cache**(NetBSD UBC 등) — **동적 분할**. 필요한 쪽이 더 가져간다.
- 첫 open은 디렉터리 inode/데이터를 다 읽지만, 같은 파일·같은 디렉터리의 다음 open은 대부분 **캐시 hit → I/O 0**.

> **TIP — 정적 분할 vs 동적 분할.** 정적: 각 사용자 몫을 보장, 예측 가능, 구현 쉬움. 동적: 활용률↑, 대신 복잡하고 "남이 내 유휴 자원을 가져가서 돌려받는 데 오래 걸리는" 손해 가능. 정답은 없고 문제에 맞게 고른다.

### 8.2 쓰기: 버퍼링

읽기 I/O는 캐시가 충분하면 **완전히** 없앨 수 있지만, 쓰기는 영속성을 위해 **언젠가 디스크에 가야** 한다. 그래도 **write buffering**(5~30초 지연)의 이득은 크다:

1. **배칭(batching)**: inode bitmap을 파일 두 개 만들 때 두 번 갱신 → 한 번만 쓰기.
2. **스케줄링**: 쌓인 쓰기를 디스크 순서대로 정렬해 내보낸다.
3. **회피**: 만들었다 바로 지운 파일은 **아예 안 써도 된다**. 게으름이 미덕.

대가: 그 사이 크래시하면 **버퍼링된 업데이트는 사라진다**. DB처럼 이걸 못 참는 응용은 `fsync()`, direct I/O, raw 디스크 인터페이스를 쓴다.

> **TIP — 내구성/성능 트레이드오프를 이해하라.** 즉시 내구성 = 느리지만 안전. 조금 잃어도 된다 = 메모리에 모았다 나중에 = 빨라 보임. 웹 브라우저 캐시 이미지 몇 장은 잃어도 되지만 은행 계좌 트랜잭션은 안 된다. **응용이 무엇을 요구하는지**가 기준이다.

## 9. 요약 (40.8)

- 파일마다 메타데이터(inode), 디렉터리는 "이름 → inode 번호" 매핑을 담은 특수 파일, 비트맵으로 빈 inode/블록 추적.
- 설계 자유도가 크고, 많은 정책 질문이 남았다: 새 파일을 디스크 **어디에** 둘까? → [Ch.41 FFS](2026-09-30_C41_ffs.md).

## 10. 직접 해보기

### 10.1 vsfs 숫자 계산 (C)

`code/C40_vsfs_math.c` — inode 위치 계산, 최대 파일 크기, 오프셋 → 포인터 경로, 경로 깊이별 I/O 수, extent 비교.

```c
/*
 * C40_vsfs_math.c — vsfs 의 "숫자" 를 직접 계산해 보기
 *   1) inode 번호 → inode 블록 → 섹터 번호 (OSTEP 40.3 공식)
 *   2) multi-level index 로 표현 가능한 최대 파일 크기
 *   3) 파일 오프셋 → 어느 포인터(direct/indirect/double/triple)를 타야 하나 + 메타데이터 읽기 수
 *   4) 경로 깊이에 따른 open()/create() I/O 수 (캐시 없음 가정, 그림 40.3/40.4 모델)
 *   5) 포인터 방식 vs extent 방식의 메타데이터 양
 *
 * build: cc -Wall -Wextra -O0 code/C40_vsfs_math.c -o .work/bin/C40_vsfs_math
 */
#include <stdint.h>
#include <stdio.h>

#define KB 1024ULL

/* vsfs 파라미터 (원문 그대로) */
static const uint64_t blockSize = 4 * KB;
static const uint64_t sectorSize = 512;
static const uint64_t inodeSize = 256;
static const uint64_t inodeStartAddr = 12 * KB; /* S(0KB) i-bmap(4KB) d-bmap(8KB) 다음 */

static void inode_location(uint64_t inum) {
    uint64_t blk = (inum * inodeSize) / blockSize;           /* inode 테이블 안의 블록 번호 */
    uint64_t byteAddr = blk * blockSize + inodeStartAddr;    /* 그 블록의 바이트 주소 */
    uint64_t sector = byteAddr / sectorSize;                 /* 디스크가 아는 섹터 번호 */
    uint64_t slot = (inum * inodeSize) % blockSize / inodeSize;
    printf("inode %2llu -> iblock %llu (byte %3lluKB) -> sector %3llu, slot %2llu in block\n",
           (unsigned long long)inum, (unsigned long long)blk, (unsigned long long)(byteAddr / KB),
           (unsigned long long)sector, (unsigned long long)slot);
}

static void max_file_size(uint64_t bs, uint64_t ptrSize, int ndirect) {
    uint64_t p = bs / ptrSize; /* 간접 블록 하나에 들어가는 포인터 수 */
    uint64_t d = (uint64_t)ndirect, s = p, dd = p * p, t = p * p * p;
    printf("block %4lluB, ptr %lluB (%llu ptr/blk):\n", (unsigned long long)bs,
           (unsigned long long)ptrSize, (unsigned long long)p);
    printf("  direct only          : %12llu blocks = %10.2f MB\n", (unsigned long long)d,
           (double)(d * bs) / (KB * KB));
    printf("  + single indirect    : %12llu blocks = %10.2f MB\n", (unsigned long long)(d + s),
           (double)((d + s) * bs) / (KB * KB));
    printf("  + double indirect    : %12llu blocks = %10.2f GB\n", (unsigned long long)(d + s + dd),
           (double)((d + s + dd) * bs) / (KB * KB * KB));
    printf("  + triple indirect    : %12llu blocks = %10.2f TB\n",
           (unsigned long long)(d + s + dd + t), (double)((d + s + dd + t) * bs) / (KB * KB * KB * KB));
}

/* 오프셋 → 포인터 경로. 캐시가 전혀 없을 때 inode 외에 읽어야 하는 간접 블록 수 */
static void offset_path(uint64_t off) {
    const uint64_t p = blockSize / 4, nd = 12;
    uint64_t lbn = off / blockSize; /* 논리 블록 번호 */
    const char *kind;
    int extra;
    char idx[64];
    if (lbn < nd) {
        kind = "direct"; extra = 0;
        snprintf(idx, sizeof idx, "ptr[%llu]", (unsigned long long)lbn);
    } else if ((lbn -= nd) < p) {
        kind = "single"; extra = 1;
        snprintf(idx, sizeof idx, "ind[%llu]", (unsigned long long)lbn);
    } else if ((lbn -= p) < p * p) {
        kind = "double"; extra = 2;
        snprintf(idx, sizeof idx, "dind[%llu][%llu]", (unsigned long long)(lbn / p),
                 (unsigned long long)(lbn % p));
    } else {
        lbn -= p * p;
        kind = "triple"; extra = 3;
        snprintf(idx, sizeof idx, "tind[%llu][%llu][%llu]", (unsigned long long)(lbn / (p * p)),
                 (unsigned long long)(lbn / p % p), (unsigned long long)(lbn % p));
    }
    printf("offset %14llu B : %-6s %-20s reads = inode + %d indirect + 1 data = %d\n",
           (unsigned long long)off, kind, idx, extra, extra + 2);
}

int main(void) {
    printf("== 1. inode number -> sector (vsfs: 256B inode, 4KB block, table @12KB) ==\n");
    uint64_t inums[] = { 0, 2, 15, 16, 32, 79 };
    for (unsigned i = 0; i < sizeof inums / sizeof inums[0]; i++) inode_location(inums[i]);

    printf("\n== 2. multi-level index max file size (12 direct) ==\n");
    max_file_size(4 * KB, 4, 12);
    max_file_size(1 * KB, 4, 12);

    printf("\n== 3. file offset -> pointer path (4KB blocks, 1024 ptr/blk) ==\n");
    uint64_t offs[] = { 0, 40 * KB, 48 * KB, 4 * KB * KB, 4 * KB * KB + 48 * KB,
                        1ULL << 30, 5ULL << 30 };
    for (unsigned i = 0; i < sizeof offs / sizeof offs[0]; i++) offset_path(offs[i]);

    printf("\n== 4. I/O count without cache (Fig 40.3 / 40.4 model) ==\n");
    printf("depth  path                 open(read)  create   +3 alloc writes  read 3 blks\n");
    for (int depth = 1; depth <= 5; depth++) {
        char path[64] = "";
        for (int i = 1; i < depth; i++) snprintf(path + i * 2 - 2, sizeof path - (size_t)(i * 2 - 2), "/d");
        snprintf(path + (depth - 1) * 2, sizeof path - (size_t)(depth - 1) * 2, "/f");
        /* open: 경로의 디렉터리마다 inode+data 2회, 마지막 파일 inode 1회 */
        int open_ios = 2 * depth + 1;
        /* create: 부모까지 탐색 2*depth, ibmap R/W 2, 부모 dir data W 1, 새 inode R/W 2, 부모 inode W 1 */
        int create_ios = 2 * depth + 6;
        int writes3 = 3 * 5; /* allocating write 1회 = inode R, dbmap R/W, data W, inode W */
        int reads3 = 3 * 3;  /* read 1회 = inode R, data R, inode W(atime) */
        printf("%5d  %-20s %10d %7d %16d %12d\n", depth, path, open_ios, create_ios, writes3, reads3);
    }
    printf("(depth=2 is /foo/bar: open=5, create=10, write=5 each — matches the book)\n");

    printf("\n== 5. metadata for a contiguous 1 GB file ==\n");
    uint64_t nblk = (1ULL << 30) / blockSize, p = blockSize / 4;
    uint64_t rest = nblk - 12;
    uint64_t single = rest < p ? rest : p;
    rest -= single;
    uint64_t dind_children = (rest + p - 1) / p;
    uint64_t ind_blocks = 1 + 1 + dind_children; /* single + double-root + 2단계 간접 블록 */
    printf("pointer-based: %llu data ptrs, %llu indirect blocks = %llu KB of metadata\n",
           (unsigned long long)nblk, (unsigned long long)ind_blocks,
           (unsigned long long)(ind_blocks * blockSize / KB));
    printf("extent-based : 1 extent (start, len=%llu) = 12 bytes inside the inode\n",
           (unsigned long long)nblk);
    return 0;
}
```

실행 (Apple clang, macOS, 경고 0):

```text
$ cc -Wall -Wextra -O0 -pthread code/C40_vsfs_math.c -o .work/bin/C40_vsfs_math && .work/bin/C40_vsfs_math
== 1. inode number -> sector (vsfs: 256B inode, 4KB block, table @12KB) ==
inode  0 -> iblock 0 (byte  12KB) -> sector  24, slot  0 in block
inode  2 -> iblock 0 (byte  12KB) -> sector  24, slot  2 in block
inode 15 -> iblock 0 (byte  12KB) -> sector  24, slot 15 in block
inode 16 -> iblock 1 (byte  16KB) -> sector  32, slot  0 in block
inode 32 -> iblock 2 (byte  20KB) -> sector  40, slot  0 in block
inode 79 -> iblock 4 (byte  28KB) -> sector  56, slot 15 in block

== 2. multi-level index max file size (12 direct) ==
block 4096B, ptr 4B (1024 ptr/blk):
  direct only          :           12 blocks =       0.05 MB
  + single indirect    :         1036 blocks =       4.05 MB
  + double indirect    :      1049612 blocks =       4.00 GB
  + triple indirect    :   1074791436 blocks =       4.00 TB
block 1024B, ptr 4B (256 ptr/blk):
  direct only          :           12 blocks =       0.01 MB
  + single indirect    :          268 blocks =       0.26 MB
  + double indirect    :        65804 blocks =       0.06 GB
  + triple indirect    :     16843020 blocks =       0.02 TB

== 3. file offset -> pointer path (4KB blocks, 1024 ptr/blk) ==
offset              0 B : direct ptr[0]               reads = inode + 0 indirect + 1 data = 2
offset          40960 B : direct ptr[10]              reads = inode + 0 indirect + 1 data = 2
offset          49152 B : single ind[0]               reads = inode + 1 indirect + 1 data = 3
offset        4194304 B : single ind[1012]            reads = inode + 1 indirect + 1 data = 3
offset        4243456 B : double dind[0][0]           reads = inode + 2 indirect + 1 data = 4
offset     1073741824 B : double dind[254][1012]      reads = inode + 2 indirect + 1 data = 4
offset     5368709120 B : triple tind[0][254][1012]   reads = inode + 3 indirect + 1 data = 5

== 4. I/O count without cache (Fig 40.3 / 40.4 model) ==
depth  path                 open(read)  create   +3 alloc writes  read 3 blks
    1  /f                            3       8               15            9
    2  /d/f                          5      10               15            9
    3  /d/d/f                        7      12               15            9
    4  /d/d/d/f                      9      14               15            9
    5  /d/d/d/d/f                   11      16               15            9
(depth=2 is /foo/bar: open=5, create=10, write=5 each — matches the book)

== 5. metadata for a contiguous 1 GB file ==
pointer-based: 262144 data ptrs, 257 indirect blocks = 1028 KB of metadata
extent-based : 1 extent (start, len=262144) = 12 bytes inside the inode
```

해석:

- inode 0~15가 모두 **섹터 24** — 같은 블록에 16개가 들어 있으니 inode 하나를 읽으면 이웃 15개도 같이 캐시된다. inode 32 → 섹터 40은 원문과 일치.
- 1KB 블록이면 같은 구조로 겨우 ~16GB(0.02TB). 블록 크기가 포인터 밀도(p)를 정하고, 최대 크기는 p³에 비례 → **블록 크기 4배 = 최대 파일 64배**.
- 4MB 오프셋은 아직 single indirect의 1012번째(12 direct 를 빼야 하므로). 5GB는 triple 영역 → 캐시가 없으면 **데이터 1블록에 I/O 5회**. 실제로는 간접 블록이 캐시에 남아 거의 hit이다.
- 깊이 2 행이 원문 그림 40.3/40.4의 숫자(5, 10, 5×3)와 정확히 일치.

### 10.2 vsfs.py 시뮬레이터

```text
cd .tools/ostep-homework/file-implementation
python3 vsfs.py -n 6 -s 17 -c
```

실제 출력 (`ARG` 줄 생략):

```text
Initial state

inode bitmap  10000000
inodes       [d a:0 r:2][][][][][][][]
data bitmap   10000000
data         [(.,0) (..,0)][][][][][][][]

mkdir("/u");

inode bitmap  11000000
inodes       [d a:0 r:3][d a:1 r:2][][][][][][]
data bitmap   11000000
data         [(.,0) (..,0) (u,1)][(.,1) (..,0)][][][][][][]

creat("/a");

inode bitmap  11100000
inodes       [d a:0 r:3][d a:1 r:2][f a:-1 r:1][][][][][]
data bitmap   11000000
data         [(.,0) (..,0) (u,1) (a,2)][(.,1) (..,0)][][][][][][]

unlink("/a");

inode bitmap  11000000
inodes       [d a:0 r:3][d a:1 r:2][][][][][][]
data bitmap   11000000
data         [(.,0) (..,0) (u,1)][(.,1) (..,0)][][][][][][]

mkdir("/z");

inode bitmap  11100000
inodes       [d a:0 r:4][d a:1 r:2][d a:2 r:2][][][][][]
data bitmap   11100000
data         [(.,0) (..,0) (u,1) (z,2)][(.,1) (..,0)][(.,2) (..,0)][][][][][]

mkdir("/s");

inode bitmap  11110000
inodes       [d a:0 r:5][d a:1 r:2][d a:2 r:2][d a:3 r:2][][][][]
data bitmap   11110000
data         [(.,0) (..,0) (u,1) (z,2) (s,3)][(.,1) (..,0)][(.,2) (..,0)][(.,3) (..,0)][][][][]

creat("/z/x");

inode bitmap  11111000
inodes       [d a:0 r:5][d a:1 r:2][d a:2 r:2][d a:3 r:2][f a:-1 r:1][][][]
data bitmap   11110000
data         [(.,0) (..,0) (u,1) (z,2) (s,3)][(.,1) (..,0)][(.,2) (..,0)][(.,3) (..,0)][][][][]
```

읽는 법과 관찰:

- 상태 변화만 보고 연산을 맞히는 단서: **inode bitmap만 늘고 data bitmap 그대로 + 부모 dir에 엔트리 추가** = `creat`. **둘 다 늘고 새 블록이 `(.,n) (..,p)`** = `mkdir`. **inode는 그대로, data bitmap만 늘고 `a:-1`→`a:n`** = write. **r(링크 수)만 바뀜** = `link`/`unlink`.
- 디렉터리의 r: 루트가 `r:2` → `/u` 를 만들자 `r:3`. 하위 디렉터리의 `..` 가 부모를 가리키는 링크이기 때문 (디렉터리 링크 수 = 2 + 하위 디렉터리 수).
- `unlink("/a")` 에서 inode 2가 해제되고 **바로 다음 `mkdir("/z")` 가 inode 2를 재사용** → 할당기는 **비트맵에서 가장 낮은 번호의 free 칸**을 고른다 (Homework Q2의 답).

`-r` 모드(연산을 보여주고 상태를 맞히기)로 `-s 21`:

```text
python3 vsfs.py -n 4 -s 21 -r      # 문제
python3 vsfs.py -n 4 -s 21 -r -c   # 정답
```

```text
mkdir("/o");
inode bitmap  11000000
inodes       [d a:0 r:3][d a:1 r:2][][][][][][]
data bitmap   11000000
data         [(.,0) (..,0) (o,1)][(.,1) (..,0)][][][][][][]

creat("/b");
inode bitmap  11100000
inodes       [d a:0 r:3][d a:1 r:2][f a:-1 r:1][][][][][]
...
creat("/o/q");
inode bitmap  11110000
inodes       [d a:0 r:3][d a:1 r:2][f a:-1 r:1][f a:-1 r:1][][][][]
data bitmap   11000000
data         [(.,0) (..,0) (o,1) (b,2)][(.,1) (..,0) (q,3)][][][][][][]

fd=open("/b", O_WRONLY|O_APPEND); write(fd, buf, BLOCKSIZE); close(fd);
inode bitmap  11110000
inodes       [d a:0 r:3][d a:1 r:2][f a:2 r:1][f a:-1 r:1][][][][]
data bitmap   11100000
```

(가운데 일부 줄 생략.) 마지막 write에서 `/b` 는 **data 블록 2**를 받는다 — 역시 가장 낮은 free 번호.

자원이 극단적으로 적을 때 (`-i 3`, inode 3개):

```text
python3 vsfs.py -i 3 -n 30 -s 1 -c -p
...
mkdir("/n");
inode bitmap  110
inodes       [d a:0 r:3][d a:1 r:2][]
...
creat("/w");
File system out of inodes; rerun with more via command-line flag?
```

`-d 2 -s 1`(데이터 블록 2개)로 돌리면 첫 `mkdir("/n")` 에서 바로 `File system out of data blocks` 로 멈춘다. 시뮬레이터는 연산 실패를 "건너뛰기"가 아니라 **실행 중단**으로 처리하므로, Homework Q3/Q4는 "어떤 연산이 실패하는가"를 출력이 멈추는 지점으로 읽어야 한다: 데이터 블록이 없으면 **mkdir과 write**(블록 필요)가 실패하고 creat/link/unlink(블록 불필요)는 가능, inode가 없으면 **creat/mkdir** 이 실패하고 link는 가능하다.

## 11. 펌웨어 엔지니어의 눈으로

- **FTL의 L2P 테이블 = inode의 multi-level index.** 호스트 LBA → NAND 물리 페이지 매핑을 DRAM에 다 못 올리면 매핑 페이지를 NAND에 두고 상위 디렉터리(GTD, global translation directory)로 가리킨다 — direct/indirect 구조와 같다. vsfs에서 "데이터 1블록에 간접 블록 3번 읽기"가 문제인 것처럼, DRAM-less SSD에서 **맵 미스 = NAND 읽기 1회 추가**가 랜덤 읽기 지연을 2배로 만든다.
- **data bitmap ≈ valid page bitmap / VPC(valid page count).** "읽기는 비트맵을 건드리지 않는다"는 원칙도 같다: 호스트 Read는 L2P만 보고, valid bitmap/VPC는 Write(무효화)·Trim·GC 때만 갱신된다.
- **superblock = SSD의 system area / boot block.** magic + 버전 + 영역 위치를 담고, 한 군데 깨지면 전체가 못 뜨므로 SSD FW도 **여러 사본 + 시퀀스 번호**로 가장 최신의 정상 사본을 고른다 (FFS가 그룹마다 superblock 사본을 두는 것과 같은 동기, [Ch.41](2026-09-30_C41_ffs.md)).
- **write buffering 5~30초 ↔ SSD 쓰기 캐시 + PLP.** OS는 크래시 때 버퍼를 잃는 걸 감수하지만, 엔터프라이즈 SSD는 **전원 손실 보호 커패시터**로 DRAM 쓰기 버퍼를 NAND에 덤프할 시간을 번다. PLP가 없는 클라이언트 SSD에서 `fsync()` 는 NVMe **Flush** 명령(또는 FUA 쓰기)으로 내려가고, FW는 그때까지 받은 데이터와 **그걸 가리키는 매핑까지** 영속화해야 완료를 보고할 수 있다 — Don이 만졌던 PLP/flush 경로가 바로 이 트레이드오프의 하드웨어 쪽 끝이다.
- **"create 한 번 = 10 I/O" ↔ 쓰기 증폭.** 4KB 사용자 쓰기 하나가 메타데이터 블록 여러 개 쓰기를 만든다. FS 레벨 증폭 × FTL 레벨 증폭(GC)이 곱해져 NAND에 도달한다. 그래서 page cache의 **배칭/회피**가 SSD 수명에도 직결된다.

## 12. 면접 질문

### Q1. `open("/a/b/c")` 를 캐시가 하나도 없는 상태에서 호출하면 디스크에서 무엇을 몇 번 읽나?
<details>
<summary>답 보기</summary>

루트 i-number는 **잘 알려진 값(보통 2)** 이라 바로 계산 가능. **root inode → root dir data(“a” 찾기) → a inode → a data → b inode → b data → c inode** = 최소 **7회**(디렉터리가 크면 data 블록 여러 개). 일반식: 디렉터리 d개면 **2d + 1**. `read()` 는 inode 참조 + data 읽기 + **atime 쓰기**. 비트맵은 **읽기 경로에서 전혀 안 읽는다**. 실제로는 dentry/inode/page cache가 대부분을 흡수한다.

</details>

### Q2. inode 번호에서 디스크 위치를 어떻게 바로 계산하나? ext4/LFS에서도 그런가?
<details>
<summary>답 보기</summary>

vsfs/ext2: inode table이 **고정 위치의 배열**이므로 `blk = inum × sizeof(inode) / blockSize`, `sector = (blk × blockSize + inodeStart) / 512`. ext2/3/4는 block group마다 inode table이 있어 `group = (inum-1) / inodes_per_group` 로 그룹을 먼저 고른다. **LFS** 는 inode가 계속 이동하므로 계산이 불가능 → **imap**(inode map) 이라는 간접 계층이 필요하다 ([Ch.43](2026-09-30_C43_lfs.md)).

</details>

### Q3. 왜 multi-level index는 불균형 트리이고, extent와 비교하면?
<details>
<summary>답 보기</summary>

**대부분 파일이 작다**(~2KB가 최빈)는 측정 결과에 맞춰, 작은 파일은 inode 안의 direct 포인터로 끝(추가 I/O 0), 큰 파일만 indirect 비용을 낸다. 4KB/4B 기준 12 direct=48KB, +single≈4MB, +double≈4GB, +triple≈4TB. **extent**(시작, 길이)는 연속 할당이 잘 될 때 메타데이터가 압도적으로 작다(1GB 연속 파일: 포인터 방식 간접 블록 257개 vs extent 1개). 단, 단편화가 심하면 extent 수가 늘어나 extent tree가 필요(ext4).

</details>

### Q4. 파일을 하나 만들고 4KB를 쓰는 데 왜 I/O가 그렇게 많고, OS는 어떻게 줄이나?
<details>
<summary>답 보기</summary>

create: 경로 탐색 + **inode bitmap R/W + 새 inode R/W + 부모 dir data W + 부모 inode W** (`/foo/bar` 에서 10회). 할당 write: **data bitmap R/W + inode R/W + data W** = 5회. 줄이는 방법: **page cache**(읽기 흡수), **write buffering**(배칭: 같은 비트맵 블록 여러 번 갱신을 1회로, 스케줄링, 만들자마자 지운 파일은 아예 안 씀), **사전 할당**, **delayed allocation**(ext4: 쓰기 시점까지 블록 결정을 미뤄 연속 할당). 대가는 크래시 시 손실 → 저널링 필요.

</details>

### Q5. `write()` 가 리턴했는데 전원이 나갔다. 데이터는 안전한가? 안전하게 하려면?
<details>
<summary>답 보기</summary>

아니다. write는 보통 **page cache에 복사만** 하고 리턴한다(5~30초 뒤 writeback). 보장하려면 **`fsync(fd)`**(데이터+메타데이터), 새 파일이면 **부모 디렉터리도 fsync**(엔트리 영속화), 또는 `O_DIRECT`/`O_SYNC`. 그리고 디바이스 쓰기 캐시까지 비워야 하므로 FS는 fsync에서 **FLUSH/FUA** 를 내려보낸다. 디바이스가 flush를 무시하면(원문 Ch.42 ASIDE) 그래도 잃을 수 있다.

</details>

## 13. 자가 점검 & 숙제

### 퀴즈 1. vsfs에서 inode가 128B라면 inode 80번은 몇 번 섹터에 있나? (inode table은 12KB부터, 크기 제한은 무시)
<details>
<summary>답 보기</summary>

80×128 = 10240B → blk = 10240/4096 = 2 → 바이트 주소 2×4KB + 12KB = 20KB → 섹터 20×1024/512 = **40**. (블록당 inode가 32개가 되므로 inode 64~95가 같은 블록.)

</details>

### 퀴즈 2. 8KB 블록, 8B 포인터, direct 10개 + single + double 이면 최대 파일 크기는?
<details>
<summary>답 보기</summary>

p = 8192/8 = 1024. (10 + 1024 + 1024²) × 8KB = 1,049,610 × 8KB ≈ **8.0 GB** (정확히 8,598,405,120B ≈ 8.01GB).

</details>

### 퀴즈 3. `/foo/bar` 의 `read()` 가 bitmap을 읽지 않는 이유, 그리고 `write()` 가 덮어쓰기일 때 I/O는 몇 번?
<details>
<summary>답 보기</summary>

읽기는 inode가 이미 블록을 가리키므로 할당 여부를 확인할 필요가 없다. 덮어쓰기 write는 **새 블록 할당이 없으므로** data bitmap R/W가 빠진다: inode R(위치 확인) + data W + inode W(mtime) = **3회**(캐시 없을 때).

</details>

### 퀴즈 4. "write buffering이 쓰기 I/O를 아예 없애는" 예를 두 가지.
<details>
<summary>답 보기</summary>

(1) 파일을 만들었다가 버퍼가 내려가기 전에 지우면 create 관련 쓰기가 모두 사라진다(임시 파일). (2) 같은 블록(inode bitmap, 같은 inode 블록)을 짧은 시간에 여러 번 갱신하면 마지막 상태만 한 번 쓴다.

</details>

### 원문 Homework 중 꼭 해볼 것

- **Q1 (`-s 17,18,19,20`)**: 상태 변화만 보고 연산을 추론 — 10.2절의 "creat/mkdir/write/link 판별 단서"를 손에 익히는 문제.
- **Q2 (`-s 21~24 -r`)**: 연산을 보고 상태를 예측 — inode/데이터 할당기가 **가장 낮은 번호의 free 칸**을 고른다는 걸 확인하는 문제.
- **Q3/Q4 (`-d 2`, `-i 2~3`)**: 자원이 극히 적을 때 어떤 연산이 실패하는가 — **블록이 필요한 연산(mkdir/write) vs inode가 필요한 연산(creat/mkdir) vs 둘 다 필요 없는 연산(link/unlink)** 을 구분하는 문제.

## 14. 다음으로

- 같은 구조를 **디스크를 의식해서** 배치하면? → [Ch.41 FFS](2026-09-30_C41_ffs.md) (cylinder group, 배치 정책)
- 쓰기 버퍼링 중 크래시가 나면? → [Ch.42 크래시 일관성과 저널링](2026-09-30_C42_crash_consistency_journaling.md)
- inode를 고정 위치에 두지 않으면? → [Ch.43 LFS](2026-09-30_C43_lfs.md)
- 복습: [Ch.39 파일과 디렉터리](2026-09-30_C39_files_and_directories.md) (API 관점), [Ch.37 하드 디스크](2026-09-30_C37_hard_disk_drives.md) (랜덤 I/O 비용)
