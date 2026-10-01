# Ch.39 파일과 디렉터리 — UNIX 파일시스템 API 를 syscall 단위로

> 📖 원문: [39. Interlude: Files and Directories](../book-md/C39_interlude_files_and_directories.md) · [PDF p.459](../Operating%20Systems%20-%20Three%20Easy%20Pieces.pdf#page=459) · ⏱️ 읽기 약 40분 · 🔗 선행: [Ch.05 프로세스 API](2026-09-30_C05_process_api.md) (fd 상속), [Ch.37 디스크 (쓰기 캐시)](2026-09-30_C37_hard_disk_drives.md)

## 0. 한눈에 보기

> **CRUX: HOW TO MANAGE A PERSISTENT DEVICE** — "How should the OS manage a persistent device? What are the APIs? What are the important aspects of the implementation?"
> (OS 는 영속 장치를 어떻게 관리해야 하나? API 는? 구현에서 중요한 점은?)

- 프로세스(CPU 가상화), 주소 공간(메모리 가상화)에 이어 세 번째 가상화: **영속 저장**. 핵심 추상화는 둘 — **파일**(바이트의 선형 배열, 저수준 이름 = inode 번호)과 **디렉터리**(이름 → inode 번호 쌍의 목록).
- API: `open`(→ **file descriptor**), `read`/`write`(현재 offset 에서), `lseek`(offset 만 바꿈, 디스크 seek 아님), `fsync`(지금 당장 영속화), `rename`(원자적), `stat`(메타데이터), `link`/`unlink`(이름 붙이기/떼기), `mkdir`/`readdir`/`rmdir`, `symlink`, `mkfs`/`mount`.
- "삭제"가 `unlink` 인 이유: 파일 = **inode**, 이름 = 디렉터리 안의 **링크**. 링크 수가 0 이 되고 아무도 열고 있지 않을 때 비로소 inode 와 데이터가 해제된다.
- 영속성의 함정: `write` 는 메모리에만 쓴다. 새 파일을 확실히 남기려면 파일 `fsync` + **디렉터리 `fsync`**. 원자적 갱신은 tmp 에 쓰고 `fsync` → `rename`.

## 1. 5분 복습표

| 용어 | 한 줄 뜻 | 예시 / 비유 |
|---|---|---|
| 파일 (file) | 바이트의 선형 배열, OS 는 내용 구조를 모름 | foo.c 가 C 코드라는 보장 없음 |
| inode 번호 | 파일의 저수준 이름 | `ls -i` 의 숫자 |
| 디렉터리 (directory) | (사람용 이름, inode 번호) 목록 | ("foo", 10) |
| 절대 경로 | 루트 / 부터의 경로 | /foo/bar.txt |
| file descriptor (fd) | 프로세스별 정수 핸들, 열린 파일에 대한 capability | open 이 3 을 돌려줌 |
| 현재 offset | 열린 파일마다 다음 read/write 위치 | read 하면 자동 증가 |
| lseek | offset 변수만 바꿈 (디스크 I/O 없음) | SEEK_SET/CUR/END |
| fsync | 그 파일의 dirty 데이터를 장치까지 강제로 | DB 커밋 |
| rename | 이름 바꾸기, 크래시에 대해 원자적 | 에디터 저장 |
| metadata / stat | 파일에 대한 정보 (크기, 소유자, 시간, 링크 수) | struct stat |
| hard link | 같은 inode 에 이름 하나 더 | `ln file file2` |
| link count | inode 를 가리키는 이름 수 | stat 의 Links |
| unlink | 이름 하나 제거, 링크 수 감소 | rm |
| symbolic link | 대상 경로 문자열을 내용으로 갖는 별도 파일 | `ln -s` |
| dangling link | 대상이 사라진 symlink | cat: No such file |
| mkfs / mount | 빈 FS 만들기 / 디렉터리 트리에 붙이기 | mount -t ext4 /dev/sda1 /home |

## 2. 파일과 디렉터리 (39.1)

- **파일** 은 그냥 바이트 배열이다. OS 는 그 안이 사진인지 C 코드인지 모른다. 파일시스템의 일은 **넣은 그대로 돌려주는 것** 뿐이다(그게 생각보다 어렵다는 게 이 파트 전체의 주제).
- 각 파일에는 사용자가 잘 모르는 **저수준 이름** 이 있다 — 역사적으로 **inode 번호**.
- **디렉터리** 도 inode 번호를 가진 "파일"이지만, 내용이 정해져 있다: (사람이 읽는 이름, 저수준 이름) 쌍의 목록. 디렉터리 안에 디렉터리를 넣으면 **디렉터리 트리** 가 된다. 루트는 `/`.
- 확장자(.c, .jpg)는 **관례** 일 뿐 강제되지 않는다.

> **TIP — THINK CAREFULLY ABOUT NAMING**: UNIX 에서는 파일뿐 아니라 장치, 파이프, 심지어 프로세스(/proc)까지 파일시스템 이름 공간에 있다. 이름 체계를 하나로 통일하면 개념 모델이 단순해진다.

## 3. 파일 만들기 (39.3)

```c
int fd = open("foo", O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
```

- `O_CREAT`: 없으면 만든다. `O_WRONLY`: 쓰기 전용으로 연다. `O_TRUNC`: 이미 있으면 크기를 0 으로 자른다.
- `O_CREAT` 를 쓸 때는 **세 번째 인자(권한)** 를 꼭 줘야 한다. 책 예제는 생략했는데, 생략하면 스택의 쓰레기 값이 권한이 된다(정의되지 않은 동작). 실무 버그 1순위.
- 옛 API `creat()` = `open(..., O_CREAT|O_WRONLY|O_TRUNC)`. Ken Thompson: "다시 설계한다면 creat 에 e 를 붙이겠다."

`open` 의 반환값 **file descriptor** 는 프로세스마다 따로 있는 작은 정수다. 책의 표현으로 **capability**: 이것을 가지고 있으면 그 파일에 특정 연산(read/write)을 할 권한이 있다는 불투명한 핸들. 객체 지향으로 보면 "file 객체를 가리키는 포인터", read/write 는 메서드다.

## 4. 읽고 쓰기 (39.4)

`strace cat foo` (Linux) 결과:

```text
open("foo", O_RDONLY|O_LARGEFILE)  = 3
read(3, "hello\n", 4096)           = 6
write(1, "hello\n", 6)             = 6
read(3, "", 4096)                  = 0
close(3)                           = 0
```

- fd 가 **3** 인 이유: 모든 프로세스는 0(stdin), 1(stdout), 2(stderr)를 이미 열고 시작한다. 새로 여는 첫 파일은 가장 작은 빈 번호 3.
- `read` 는 읽은 바이트 수를 돌려준다. **0 = EOF**. 그래서 cat 은 0 이 나올 때까지 읽는다.
- `write(1, …)` 는 표준 출력. cat 이 `printf` 를 썼더라도 결국 libc 가 `write(1, …)` 를 부른다.
- `O_LARGEFILE` 은 32비트 환경에서 64비트 offset 을 쓰겠다는 표시.

macOS 에는 strace 가 없다. 대응 도구 `dtruss` 는 SIP(System Integrity Protection) 때문에 기본 설정에서 시스템 바이너리를 추적하지 못한다. 그래서 이 노트의 "직접 해보기"는 syscall 을 직접 부르고 **반환값을 출력** 하는 방식으로 같은 것을 확인한다.

## 5. 순차가 아닌 읽기/쓰기: lseek (39.5)

```c
off_t lseek(int fd, off_t offset, int whence);
// SEEK_SET: offset 으로
// SEEK_CUR: 현재 + offset
// SEEK_END: 파일 크기 + offset
```

열린 파일마다 커널은 **현재 offset** 을 기억한다. offset 이 바뀌는 길은 둘: (1) N 바이트 read/write 후 자동으로 +N, (2) `lseek` 로 명시적으로.

> **ASIDE — CALLING LSEEK() DOES NOT PERFORM A DISK SEEK**: `lseek` 는 커널 메모리의 변수 하나를 바꿀 뿐 **디스크 I/O 를 전혀 하지 않는다.** 디스크 seek 은 실제 I/O 가 이전 I/O 와 다른 트랙일 때 암이 움직이는 것. 다만 lseek 로 여기저기 읽으면 그 뒤의 read 가 디스크 seek 을 일으킬 수는 있다.

두 가지 더 (책 v0.91 에는 없지만 꼭 알아야 할 것):

- 파일 끝 너머로 `lseek` 후 `write` 하면 사이 구간은 **hole** 이 되어 0 으로 읽히고 블록을 차지하지 않는다 — **sparse file**. VM 디스크 이미지, core dump 가 이걸 쓴다.
- 멀티스레드에서 `lseek` + `read` 는 원자적이지 않다(다른 스레드가 사이에 offset 을 바꿈). 그래서 offset 을 인자로 받는 `pread`/`pwrite` 가 있다.

## 6. 즉시 쓰기: fsync (39.6)

`write` 는 보통 "언젠가 영속 저장소에 써 줘" 라는 뜻이다. 파일시스템은 성능을 위해 쓰기를 메모리(page cache)에 **5~30초** 모았다가 장치로 보낸다. 그 사이 크래시하면 사라진다.

DBMS 같은 응용은 "지금 당장 디스크에" 가 필요하다 → `fsync(fd)`: 그 파일의 dirty 데이터(와 메타데이터)를 장치에 쓰고, **다 끝나야** 리턴한다.

```c
int fd = open("foo", O_CREAT | O_WRONLY | O_TRUNC, 0644);
assert(fd > -1);
int rc = write(fd, buffer, size);
assert(rc == size);
rc = fsync(fd);
assert(rc == 0);
```

함정 (책이 짚은 것): **새로 만든 파일** 이라면 파일 `fsync` 만으로는 부족할 수 있다. "foo 라는 이름이 디렉터리에 있다"는 사실은 **디렉터리의 데이터** 다. 디렉터리도 열어서 `fsync` 해야 한다. 이걸 빼먹은 응용 버그가 많다(책 참고문헌 P+13).

책 밖의 세 가지 현실:

- **macOS 의 `fsync` 는 드라이브 캐시를 비우지 않는다.** 데이터를 장치까지 보내기만 하고, 장치의 휘발성 쓰기 캐시에 머물 수 있다. 진짜 영속화는 `fcntl(fd, F_FULLFSYNC)`. 아래 실측에서 두 방식이 **50배 이상** 차이 난다. Linux 의 `fsync` 는 기본적으로 FLUSH(또는 FUA)까지 내린다.
- **`fdatasync`**: 데이터와 "데이터를 읽는 데 필요한" 메타데이터(크기)만. mtime 갱신을 생략해 저널 쓰기를 줄인다.
- **fsync 에러는 한 번만 보고될 수 있다.** 2018년 PostgreSQL "fsyncgate": Linux 에서 writeback 실패 후 `fsync` 가 EIO 를 한 번 돌려주면 dirty 페이지는 clean 으로 표시되어 버려, 재시도한 `fsync` 는 성공한다 — 데이터는 날아갔는데. 교훈: **fsync 실패는 재시도하지 말고 크래시 복구 경로로**.

## 7. 이름 바꾸기: rename (39.7)

`mv foo bar` → `rename("foo", "bar")`. 핵심 보장: 크래시에 대해 **원자적**. 크래시 후 파일은 옛 이름 아니면 새 이름이고, 중간 상태는 없다.

에디터가 파일 중간에 한 줄을 넣고 저장하는 안전한 방법:

```c
int fd = open("foo.txt.tmp", O_WRONLY | O_CREAT | O_TRUNC, 0644);
write(fd, buffer, size);   // 새 버전 전체를 tmp 에
fsync(fd);                 // 내용을 먼저 영속화
close(fd);
rename("foo.txt.tmp", "foo.txt");   // 원자적으로 교체 (옛 파일은 이 순간 unlink 됨)
// (+ 디렉터리 fsync: rename 자체를 영속화)
```

순서가 핵심이다. `fsync` 없이 `rename` 하면, 일부 파일시스템(지연 할당하는 ext4)에서는 크래시 후 **이름은 새 파일인데 내용은 0 바이트** 가 될 수 있다 — 메타데이터(rename)가 데이터보다 먼저 디스크에 갔기 때문. 2009년 ext4 에서 실제로 KDE 설정 파일이 대량으로 0 바이트가 된 사건이 있었고, ext4 는 이후 "rename 으로 덮어쓰는 경우 데이터를 먼저 쓰는" 휴리스틱(auto_da_alloc)을 넣었다. 응용이 FS 구현 세부에 기대면 안 된다는 교훈.

## 8. 파일 정보: stat (39.8)

메타데이터는 `stat(path)` / `fstat(fd)` 로 얻는다.

```c
struct stat {
    dev_t     st_dev;     // 파일이 있는 장치 ID
    ino_t     st_ino;     // inode 번호
    mode_t    st_mode;    // 종류 + 권한
    nlink_t   st_nlink;   // hard link 수
    uid_t     st_uid;     // 소유자
    gid_t     st_gid;     // 그룹
    dev_t     st_rdev;    // 장치 파일이면 장치 ID
    off_t     st_size;    // 크기 (바이트)
    blksize_t st_blksize; // I/O 권장 블록 크기
    blkcnt_t  st_blocks;  // 할당된 블록 수 (512B 단위)
    time_t    st_atime;   // 마지막 접근
    time_t    st_mtime;   // 마지막 내용 수정
    time_t    st_ctime;   // 마지막 상태(메타데이터) 변경
};
```

이 정보는 파일시스템이 **inode** 라는 영속 자료구조에 보관한다. `st_size`(논리 크기)와 `st_blocks`(실제 할당)가 다를 수 있다는 점(sparse file)을 아래 실험에서 본다. `ctime` 은 "생성 시간"이 아니라 **change time** 이다.

## 9. 삭제와 디렉터리 (39.9–39.12)

- `strace rm foo` → `unlink("foo")`. 왜 "remove" 가 아니라 "unlink" 인지는 10장에서.
- **mkdir**: `mkdir("foo", 0777)`. 디렉터리는 **직접 write 할 수 없다** — 형식이 FS 메타데이터라서, 파일/디렉터리를 만들고 지우는 간접적인 방법으로만 바꾼다. 빈 디렉터리에도 `.`(자기)와 `..`(부모) 두 엔트리가 있다.
- **readdir**: `opendir` / `readdir` / `closedir`. `struct dirent` 에는 이름, inode 번호, 종류(d_type) 정도만 있다. 크기·권한이 필요하면 각 항목에 `stat` 을 불러야 한다 — `ls -l` 이 `ls` 보다 느린 이유.

```c
DIR *dp = opendir(".");
struct dirent *d;
while ((d = readdir(dp)) != NULL)
    printf("%llu %s\n", (unsigned long long)d->d_ino, d->d_name);
closedir(dp);
```

- **rmdir**: 디렉터리가 **비어 있어야**(. 과 .. 만) 지워진다. 한 번에 많은 데이터를 날리는 사고를 막기 위해서. 비어 있지 않으면 `ENOTEMPTY`.

> **TIP — BE WARY OF POWERFUL COMMANDS**: `rm -rf *` 를 루트에서 치면 끝이다. 적은 타이핑으로 큰일을 하는 도구는 큰 피해도 쉽게 낸다.

## 10. Hard link (39.13)

```text
prompt> echo hello > file
prompt> ln file file2
prompt> ls -i file file2
67158084 file
67158084 file2
```

`link(old, new)` 는 디렉터리에 **새 이름** 을 만들고 **같은 inode 번호** 를 가리키게 한다. 복사가 아니다. 파일시스템 입장에서 `file` 과 `file2` 는 완전히 동등하다 — 둘 다 inode 67158084 에 대한 링크일 뿐.

그래서 파일을 "만드는" 일은 두 가지다: (1) inode(크기, 블록 위치 등 모든 정보) 만들기, (2) 사람용 이름을 그 inode 에 **링크** 해서 디렉터리에 넣기. 지우는 일은 그 반대 — 이름 하나를 **unlink** 하고 inode 의 **link count** 를 1 줄인다. link count 가 **0** 이 될 때 비로소 inode 와 데이터 블록이 해제된다.

```text
echo hello > file   → Links: 1
ln file file2       → Links: 2
ln file2 file3      → Links: 3
rm file             → Links: 2
rm file2            → Links: 1
rm file3            → (해제)
```

정확히는 **link count == 0 이고 그 파일을 연 fd 도 없을 때** 해제된다. 열린 채로 unlink 된 파일은 이름 없이 살아 있다가 마지막 close 때 사라진다(아래 실험에서 확인). 임시 파일을 만들자마자 unlink 해서 크래시해도 쓰레기가 안 남게 하는 고전 기법이고, Linux 의 `O_TMPFILE` 이 이를 정식화했다.

## 11. Symbolic link (39.14)

hard link 의 한계: **디렉터리에는 못 건다**(트리에 순환이 생길 수 있음), **다른 파일시스템으로는 못 건다**(inode 번호는 FS 안에서만 유일). 그래서 **symbolic(soft) link**.

```text
prompt> ln -s file file2
prompt> ls -al
-rw-r----- 1 remzi remzi  6 May  3 19:10 file
lrwxrwxrwx 1 remzi remzi  4 May  3 19:10 file2 -> file
```

- symlink 는 **세 번째 종류의 파일**(regular, directory, symlink). `ls -l` 첫 글자 `l`.
- 내용 = **대상 경로 문자열**. 그래서 크기가 4 바이트("file"). 대상 경로가 길면 크기도 크다(alongerfilename → 15).
- 대상을 지우면 **dangling reference**: `cat file2` → No such file or directory. hard link 에서는 일어나지 않는 일.

```svg
<svg viewBox="0 0 700 300" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <defs><marker id="C39-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="20" y="22" fill="currentColor" font-weight="bold">디렉터리 엔트리 (이름 → inode #)</text>
  <rect x="20" y="35" width="210" height="150" rx="6" fill="none" stroke="currentColor"/>
  <g font-family="Menlo, monospace" font-size="13">
    <text x="35" y="62" fill="currentColor">"file"   → 67158084</text>
    <text x="35" y="97" fill="currentColor">"file2"  → 67158084</text>
    <text x="35" y="132" fill="currentColor">"sym"    → 67158090</text>
    <text x="35" y="167" fill="currentColor">"."  ".." → 자기/부모</text>
  </g>
  <rect x="330" y="35" width="200" height="90" rx="6" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="430" y="57" text-anchor="middle" fill="currentColor" font-weight="bold">inode 67158084</text>
  <text x="430" y="78" text-anchor="middle" fill="currentColor" font-size="12">type=regular  links=2</text>
  <text x="430" y="96" text-anchor="middle" fill="currentColor" font-size="12">size=6  blocks → [data]</text>
  <text x="430" y="114" text-anchor="middle" fill="currentColor" font-size="12">"hello\n"</text>
  <rect x="330" y="160" width="200" height="80" rx="6" fill="none" stroke="currentColor"/>
  <text x="430" y="182" text-anchor="middle" fill="currentColor" font-weight="bold">inode 67158090</text>
  <text x="430" y="202" text-anchor="middle" fill="currentColor" font-size="12">type=symlink  links=1</text>
  <text x="430" y="222" text-anchor="middle" fill="currentColor" font-size="12">size=4  data = "file"</text>
  <line x1="195" y1="57" x2="328" y2="70" stroke="currentColor" marker-end="url(#C39-arrow)"/>
  <line x1="195" y1="92" x2="328" y2="85" stroke="currentColor" marker-end="url(#C39-arrow)"/>
  <line x1="195" y1="128" x2="328" y2="190" stroke="currentColor" marker-end="url(#C39-arrow)"/>
  <path d="M 530 210 C 650 210 650 20 234 50" fill="none" style="stroke:var(--accent)" stroke-width="2" stroke-dasharray="6 4" marker-end="url(#C39-arrow)"/>
  <text x="590" y="140" fill="currentColor" font-size="12">경로 "file" 을</text>
  <text x="590" y="156" fill="currentColor" font-size="12">다시 해석</text>
  <text x="20" y="265" fill="currentColor" font-size="12">hard link: 두 이름이 같은 inode 를 직접 가리킴 → unlink("file") 해도 file2 로 읽힘 (links 2→1)</text>
  <text x="20" y="285" fill="currentColor" font-size="12">symlink: "file" 이라는 문자열을 저장 → "file" 이름이 사라지면 dangling (ENOENT)</text>
</svg>
```

## 12. 열린 파일의 구조: fd → open file → inode

책 v0.91 은 자세히 다루지 않지만, 위 동작(offset, unlink 후에도 읽힘, fork 후 공유)을 이해하려면 이 세 층을 그려야 한다.

```svg
<svg viewBox="0 0 700 260" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <defs><marker id="C39-arrow2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="20" y="20" fill="currentColor" font-weight="bold">프로세스 A fd 표</text>
  <rect x="20" y="30" width="120" height="110" fill="none" stroke="currentColor"/>
  <g font-size="12" font-family="Menlo, monospace"><text x="30" y="50" fill="currentColor">0 stdin</text><text x="30" y="72" fill="currentColor">1 stdout</text><text x="30" y="94" fill="currentColor">2 stderr</text><text x="30" y="116" fill="currentColor">3 ●</text><text x="30" y="134" fill="currentColor">4 ●</text></g>
  <text x="20" y="170" fill="currentColor" font-weight="bold">프로세스 B (A 의 fork)</text>
  <rect x="20" y="180" width="120" height="50" fill="none" stroke="currentColor"/>
  <g font-size="12" font-family="Menlo, monospace"><text x="30" y="200" fill="currentColor">3 ●</text><text x="30" y="220" fill="currentColor">…</text></g>
  <text x="250" y="20" fill="currentColor" font-weight="bold">open file table (시스템 전역)</text>
  <rect x="250" y="40" width="190" height="55" rx="5" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="345" y="62" text-anchor="middle" fill="currentColor" font-size="12">open#1: O_RDONLY, offset=6</text>
  <text x="345" y="82" text-anchor="middle" fill="currentColor" font-size="12">refcnt=2 (A:3, B:3)</text>
  <rect x="250" y="120" width="190" height="55" rx="5" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="345" y="142" text-anchor="middle" fill="currentColor" font-size="12">open#2: O_WRONLY, offset=0</text>
  <text x="345" y="162" text-anchor="middle" fill="currentColor" font-size="12">refcnt=1 (A:4)</text>
  <text x="520" y="20" fill="currentColor" font-weight="bold">in-memory inode</text>
  <rect x="520" y="70" width="160" height="70" rx="5" fill="none" stroke="currentColor"/>
  <text x="600" y="93" text-anchor="middle" fill="currentColor" font-size="12">inode 67158084</text>
  <text x="600" y="112" text-anchor="middle" fill="currentColor" font-size="12">links=0 (unlink됨)</text>
  <text x="600" y="130" text-anchor="middle" fill="currentColor" font-size="12">open refs=2 → 아직 살아 있음</text>
  <line x1="62" y1="112" x2="248" y2="65" stroke="currentColor" marker-end="url(#C39-arrow2)"/>
  <line x1="62" y1="196" x2="248" y2="80" stroke="currentColor" marker-end="url(#C39-arrow2)"/>
  <line x1="62" y1="131" x2="248" y2="145" stroke="currentColor" marker-end="url(#C39-arrow2)"/>
  <line x1="440" y1="67" x2="518" y2="95" stroke="currentColor" marker-end="url(#C39-arrow2)"/>
  <line x1="440" y1="147" x2="518" y2="115" stroke="currentColor" marker-end="url(#C39-arrow2)"/>
  <text x="20" y="252" fill="currentColor" font-size="12">offset 은 "open 한 번"(open file)에 속한다 → fork 한 자식과 부모는 offset 공유, 같은 파일을 두 번 open 하면 따로.</text>
</svg>
```

- fd 는 프로세스별 표의 인덱스, 실제 상태(offset, 모드)는 **open file** 객체에, 파일 자체는 **inode** 에.
- `fork` 와 `dup` 는 open file 을 **공유** 한다 → 부모와 자식이 같은 fd 로 쓰면 offset 이 이어진다(셸의 `>>` 리다이렉션이 섞이지 않는 이유와 연결).
- inode 는 "디렉터리 링크 수"와 "열린 참조 수" 둘 다 0 이어야 해제된다.

## 13. 파일시스템 만들기와 마운트 (39.15)

- `mkfs`: 장치(파티션, 예 /dev/sda1)와 FS 종류(ext4)를 받아 **빈 FS**(루트 디렉터리 하나)를 쓴다.
- `mount`: 기존 디렉터리(**마운트 지점**)에 그 FS 의 루트를 붙인다. `mount -t ext3 /dev/sda1 /home/users` 후에는 `/home/users/a/foo` 처럼 하나의 트리로 접근한다.
- `mount` 출력에는 ext3(디스크 FS), proc(프로세스 정보), sysfs, tmpfs(메모리 FS), AFS(분산 FS)가 한 트리에 섞여 있다. **이름 공간 하나로 모든 것** — 39.1 의 naming TIP 의 실현.

## 14. 직접 해보기

### 14.1 C: syscall 하나하나의 반환값으로 확인하기

임시 디렉터리에서 open/read/write → lseek(1 GiB hole) → stat → link/symlink/unlink → mkdir/readdir/rmdir → atomic rename → fsync 비용을 차례로 실행한다.

```bash
cc -Wall -Wextra -O0 code/C39_files_api.c -o .work/bin/C39_files_api
.work/bin/C39_files_api
```

```c
// C39_files_api.c — OSTEP Ch.39 파일/디렉터리 API 를 실제 syscall 로 하나씩 확인
//
//  (strace 대신) 각 syscall 의 반환값을 직접 출력한다. macOS / Linux 모두 동작.
//  1) open/write/read/close, fd 번호가 3부터 시작하는 이유
//  2) lseek: SEEK_SET/CUR/END, 파일 끝 너머(1GiB) 쓰기 → sparse file(st_size vs st_blocks)
//  3) stat: inode 번호, link count
//  4) hard link vs symbolic link, unlink, dangling symlink, 열린 fd 는 unlink 후에도 읽힘
//  5) mkdir / readdir / rmdir(비어 있지 않으면 ENOTEMPTY)
//  6) atomic update 패턴: tmp 에 write → fsync → rename → 디렉터리 fsync
//  7) fsync vs F_FULLFSYNC(macOS) 지연 측정
//
// build: cc -Wall -Wextra -O0 code/C39_files_api.c -o .work/bin/C39_files_api
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define CHECK(x) do { if ((x) < 0) { perror(#x); exit(1); } } while (0)

static void show_stat(const char *path, int follow) {
    struct stat st;
    int rc = follow ? stat(path, &st) : lstat(path, &st);
    if (rc < 0) { printf("    %-6s %-10s -> -1 (%s)\n", follow ? "stat" : "lstat", path, strerror(errno)); return; }
    const char *type = S_ISREG(st.st_mode) ? "regular" : S_ISDIR(st.st_mode) ? "directory"
                     : S_ISLNK(st.st_mode) ? "symlink" : "other";
    printf("    %-6s %-10s ino=%-10llu links=%u size=%-8lld blocks(512B)=%-4lld %s\n",
           follow ? "stat" : "lstat", path, (unsigned long long)st.st_ino, (unsigned)st.st_nlink,
           (long long)st.st_size, (long long)st.st_blocks, type);
}

static double now_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

int main(void) {
    char dir[] = "/tmp/ostep39.XXXXXX";
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }
    CHECK(chdir(dir));
    printf("work dir: (temp dir)\n");

    // ---- 1) open/write/read
    printf("\n[1] open/write/read/close\n");
    int fd = open("foo", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    printf("    open(\"foo\", O_CREAT|O_WRONLY|O_TRUNC) = %d   (0,1,2 = stdin/stdout/stderr)\n", fd);
    printf("    write(%d, \"hello\\n\", 6) = %zd\n", fd, write(fd, "hello\n", 6));
    printf("    close(%d) = %d\n", fd, close(fd));
    char buf[4096];
    fd = open("foo", O_RDONLY);
    ssize_t n = read(fd, buf, sizeof buf);
    printf("    open(\"foo\", O_RDONLY) = %d ; read(%d, buf, 4096) = %zd ; ", fd, fd, n);
    printf("read again = %zd (EOF)\n", read(fd, buf, sizeof buf));
    close(fd);

    // ---- 2) lseek
    printf("\n[2] lseek & sparse file\n");
    fd = open("sparse", O_CREAT | O_RDWR | O_TRUNC, 0644);
    printf("    write 4 bytes -> offset now %lld (SEEK_CUR,0)\n", (write(fd, "ABCD", 4), (long long)lseek(fd, 0, SEEK_CUR)));
    printf("    lseek(fd, 1 GiB, SEEK_SET) = %lld  (디스크 I/O 없음: 커널 변수만 바뀜)\n", (long long)lseek(fd, 1 << 30, SEEK_SET));
    CHECK(write(fd, "Z", 1));
    printf("    lseek(fd, 0, SEEK_END) = %lld\n", (long long)lseek(fd, 0, SEEK_END));
    CHECK(lseek(fd, 2, SEEK_SET));
    n = read(fd, buf, 4);
    printf("    pread-like: lseek(2) + read(4) = %zd bytes \"%.2s\" + hole bytes %d %d\n", n, buf, buf[2], buf[3]);
    close(fd);
    show_stat("sparse", 1);
    struct stat sp; stat("sparse", &sp);
    printf("    -> st_size = %lld B, 실제 할당 = st_blocks*512 = %lld B  (hole 은 할당 없이 0 으로 읽힘)\n",
           (long long)sp.st_size, (long long)sp.st_blocks * 512);

    // ---- 3,4) links
    printf("\n[3] hard link: 같은 inode 에 이름을 하나 더\n");
    show_stat("foo", 1);
    CHECK(link("foo", "foo2"));   printf("    link(\"foo\",\"foo2\") = 0\n");
    CHECK(link("foo2", "foo3"));  printf("    link(\"foo2\",\"foo3\") = 0\n");
    show_stat("foo", 1); show_stat("foo3", 1);

    printf("\n[4] symlink: 경로 문자열을 데이터로 가진 별도 파일\n");
    CHECK(symlink("foo", "sym"));   printf("    symlink(\"foo\",\"sym\") = 0\n");
    show_stat("sym", 0); show_stat("sym", 1);
    n = readlink("sym", buf, sizeof buf); buf[n] = 0;
    printf("    readlink(\"sym\") = \"%s\" (%zd bytes = lstat size)\n", buf, n);

    int keep = open("foo3", O_RDONLY);           // 열린 fd 하나 유지
    printf("    unlink(\"foo\") = %d\n", unlink("foo"));
    show_stat("foo2", 1);
    fd = open("sym", O_RDONLY);
    printf("    open(\"sym\") after unlink(foo) = %d (%s)  <- dangling symlink\n", fd, fd < 0 ? strerror(errno) : "ok");
    printf("    unlink(\"foo2\") = %d, unlink(\"foo3\") = %d  -> link count 0, 이름은 모두 사라짐\n",
           unlink("foo2"), unlink("foo3"));
    n = read(keep, buf, sizeof buf);
    printf("    but read(keep_fd) = %zd \"%.5s\"  <- 열린 fd 가 있으면 inode 는 close 때까지 살아 있음\n", n, buf);
    close(keep);

    // ---- 5) directories
    printf("\n[5] mkdir / readdir / rmdir\n");
    CHECK(mkdir("d", 0755)); show_stat("d", 1);
    fd = open("d/a", O_CREAT | O_WRONLY, 0644); close(fd);
    printf("    + file d/a\n"); show_stat("d", 1);
    CHECK(mkdir("d/sub", 0755));
    printf("    + dir  d/sub\n"); show_stat("d", 1);
    printf("    -> ext4 등 전통 UNIX FS: 2 + 하위 '디렉터리' 수. APFS 는 파일 엔트리까지 세어 보고한다\n");
    DIR *dp = opendir("d"); struct dirent *de;
    while ((de = readdir(dp)) != NULL)
        printf("    readdir: ino=%-10llu type=%-3s name=%s\n", (unsigned long long)de->d_ino,
               de->d_type == DT_DIR ? "dir" : de->d_type == DT_REG ? "reg" : "?", de->d_name);
    closedir(dp);
    printf("    rmdir(\"d\") = %d (%s)\n", rmdir("d"), strerror(errno));
    unlink("d/a"); rmdir("d/sub");
    printf("    after emptying: rmdir(\"d\") = %d\n", rmdir("d"));

    // ---- 6) atomic update
    printf("\n[6] atomic file update: write tmp -> fsync -> rename -> fsync(dir)\n");
    fd = open("conf", O_CREAT | O_WRONLY | O_TRUNC, 0644); CHECK(write(fd, "version=1\n", 10)); fsync(fd); close(fd);
    struct stat before; stat("conf", &before);
    fd = open("conf.tmp", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    CHECK(write(fd, "version=2\n", 10));
    printf("    fsync(tmp) = %d\n", fsync(fd)); close(fd);
    printf("    rename(\"conf.tmp\",\"conf\") = %d\n", rename("conf.tmp", "conf"));
    int dfd = open(".", O_RDONLY);
    printf("    fsync(dir fd) = %d   (rename 이라는 '디렉터리 변경'을 영속화)\n", fsync(dfd)); close(dfd);
    struct stat after; stat("conf", &after);
    fd = open("conf", O_RDONLY); n = read(fd, buf, sizeof buf); close(fd);
    printf("    conf now: \"%.9s\", inode %llu -> %llu (이름은 같고 inode 가 통째로 바뀜)\n", buf,
           (unsigned long long)before.st_ino, (unsigned long long)after.st_ino);

    // ---- 7) fsync cost
    printf("\n[7] durability cost: 4KB write + sync, 50 rounds\n");
    memset(buf, 'x', sizeof buf);
    fd = open("log", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    double t0 = now_ms();
    for (int i = 0; i < 50; i++) CHECK(write(fd, buf, 4096));
    double t_none = (now_ms() - t0) / 50;
    t0 = now_ms();
    for (int i = 0; i < 50; i++) { CHECK(write(fd, buf, 4096)); CHECK(fsync(fd)); }
    double t_fsync = (now_ms() - t0) / 50;
#ifdef F_FULLFSYNC
    t0 = now_ms();
    for (int i = 0; i < 50; i++) { CHECK(write(fd, buf, 4096)); CHECK(fcntl(fd, F_FULLFSYNC)); }
    double t_full = (now_ms() - t0) / 50;
    printf("    write only        : %8.3f ms/op\n    write + fsync     : %8.3f ms/op\n    write + F_FULLFSYNC: %7.3f ms/op\n",
           t_none, t_fsync, t_full);
#else
    printf("    write only : %8.3f ms/op\n    write+fsync: %8.3f ms/op\n", t_none, t_fsync);
#endif
    close(fd); unlink("log"); unlink("conf"); unlink("sparse"); unlink("sym");
    CHECK(chdir("/")); CHECK(rmdir(dir));
    printf("\ncleanup done\n");
    return 0;
}
```

실제 출력 (macOS, APFS, Apple 내장 SSD):

```text
work dir: (temp dir)

[1] open/write/read/close
    open("foo", O_CREAT|O_WRONLY|O_TRUNC) = 3   (0,1,2 = stdin/stdout/stderr)
    write(3, "hello\n", 6) = 6
    close(3) = 0
    open("foo", O_RDONLY) = 3 ; read(3, buf, 4096) = 6 ; read again = 0 (EOF)

[2] lseek & sparse file
    write 4 bytes -> offset now 4 (SEEK_CUR,0)
    lseek(fd, 1 GiB, SEEK_SET) = 1073741824  (디스크 I/O 없음: 커널 변수만 바뀜)
    lseek(fd, 0, SEEK_END) = 1073741825
    pread-like: lseek(2) + read(4) = 4 bytes "CD" + hole bytes 0 0
    stat   sparse     ino=12105209   links=1 size=1073741825 blocks(512B)=40   regular
    -> st_size = 1073741825 B, 실제 할당 = st_blocks*512 = 20480 B  (hole 은 할당 없이 0 으로 읽힘)

[3] hard link: 같은 inode 에 이름을 하나 더
    stat   foo        ino=12105208   links=1 size=6        blocks(512B)=8    regular
    link("foo","foo2") = 0
    link("foo2","foo3") = 0
    stat   foo        ino=12105208   links=3 size=6        blocks(512B)=8    regular
    stat   foo3       ino=12105208   links=3 size=6        blocks(512B)=8    regular

[4] symlink: 경로 문자열을 데이터로 가진 별도 파일
    symlink("foo","sym") = 0
    lstat  sym        ino=12105213   links=1 size=3        blocks(512B)=0    symlink
    stat   sym        ino=12105208   links=3 size=6        blocks(512B)=8    regular
    readlink("sym") = "foo" (3 bytes = lstat size)
    unlink("foo") = 0
    stat   foo2       ino=12105208   links=2 size=6        blocks(512B)=8    regular
    open("sym") after unlink(foo) = -1 (No such file or directory)  <- dangling symlink
    unlink("foo2") = 0, unlink("foo3") = 0  -> link count 0, 이름은 모두 사라짐
    but read(keep_fd) = 6 "hello"  <- 열린 fd 가 있으면 inode 는 close 때까지 살아 있음

[5] mkdir / readdir / rmdir
    stat   d          ino=12105214   links=2 size=64       blocks(512B)=0    directory
    + file d/a
    stat   d          ino=12105214   links=3 size=96       blocks(512B)=0    directory
    + dir  d/sub
    stat   d          ino=12105214   links=4 size=128      blocks(512B)=0    directory
    -> ext4 등 전통 UNIX FS: 2 + 하위 '디렉터리' 수. APFS 는 파일 엔트리까지 세어 보고한다
    readdir: ino=12105214   type=dir name=.
    readdir: ino=12105207   type=dir name=..
    readdir: ino=12105215   type=reg name=a
    readdir: ino=12105216   type=dir name=sub
    rmdir("d") = -1 (Directory not empty)
    after emptying: rmdir("d") = 0

[6] atomic file update: write tmp -> fsync -> rename -> fsync(dir)
    fsync(tmp) = 0
    rename("conf.tmp","conf") = 0
    fsync(dir fd) = 0   (rename 이라는 '디렉터리 변경'을 영속화)
    conf now: "version=2", inode 12105217 -> 12105218 (이름은 같고 inode 가 통째로 바뀜)

[7] durability cost: 4KB write + sync, 50 rounds
    write only        :    0.004 ms/op
    write + fsync     :    0.078 ms/op
    write + F_FULLFSYNC:   4.393 ms/op

cleanup done
```

해석:

- **[1]** 첫 open 은 3. 두 번째 read 는 0 = EOF. 책의 strace 결과와 같은 모양이다.
- **[2] sparse file**: `st_size` 는 1 GiB + 1 인데 실제 할당은 **20 KB**. hole 은 디스크를 차지하지 않고 0 으로 읽힌다. (참고: 같은 실험을 1 MiB, 16 MiB hole 로 했을 때 APFS 는 블록을 다 할당했다 — APFS 는 작은 hole 은 sparse 로 만들지 않는다. "sparse 여부는 FS 구현이 정한다"는 좋은 예.)
- **[3] hard link**: 세 이름 모두 inode 12105208 — **같은 번호**, links 1 → 3.
- **[4] symlink**: `lstat` 은 링크 자체(별도 inode, size 3 = "foo" 세 글자, type symlink), `stat` 은 따라간 대상. 원본 이름을 지우자 `open("sym")` 은 **ENOENT(dangling)**. 반대로 hard link 들은 링크 수만 줄었고, **세 이름을 다 지웠는데도** 미리 열어 둔 fd 로는 "hello" 가 읽혔다 — 12장의 "열린 참조" 그대로.
- **[5] 디렉터리**: 비어 있지 않으면 `rmdir` 은 `Directory not empty`. 디렉터리 link count 는 ext4 같은 전통 FS 에서 "2 + 하위 디렉터리 수"(각 하위 디렉터리의 `..` 가 부모를 가리키므로)인데, **APFS 는 파일 엔트리까지 세어** 2 → 3 → 4 로 늘었다(숙제 Q1 의 답이 FS 마다 다르다는 것).
- **[6] atomic update**: 같은 이름 conf 인데 inode 번호가 바뀌었다. rename 은 "내용 덮어쓰기"가 아니라 **이름을 새 inode 로 옮겨 다는 것** 이다. 그래서 크래시 시점에 따라 옛 inode 아니면 새 inode, 중간이 없다.
- **[7] 영속화 비용**: write 만 ≈ 0.004 ms(메모리 복사), `fsync` ≈ 0.08 ms, `F_FULLFSYNC` ≈ **4.4 ms**. macOS 의 `fsync` 는 장치 캐시 flush 를 안 하기 때문에 이렇게 싸다. F_FULLFSYNC 는 NVMe Flush 까지 내려서 SSD 가 쓰기 버퍼와 매핑 정보를 NAND 에 내릴 때까지 기다린다. "fsync 가 빠르다"는 측정 결과를 보면 먼저 **어디까지 내려갔는지** 를 의심해야 한다.

### 14.2 시뮬레이터

이 장(API 소개)에는 OSTEP 시뮬레이터가 없다. 원문 Homework 도 "유틸리티를 직접 짜 보라"는 형식이다(아래 15장).

## 15. 펌웨어 엔지니어의 눈으로

- **`fsync` 의 끝은 Don 이 짜던 Flush 핸들러다.** 호스트의 `fsync`(Linux) / `F_FULLFSYNC`(macOS)는 결국 NVMe **Flush** 명령(또는 FUA 쓰기)이 된다. FW 는 Flush 를 받으면 (1) 쓰기 버퍼(DRAM/SRAM)에 있는 사용자 데이터를 NAND 에 program, (2) 열린 블록의 부분 페이지 패딩, (3) 그 데이터에 대한 **L2P 변경분(delta)을 NAND 에 기록** 까지 끝나야 완료 CQE 를 보낸다. 위 실측 4.4 ms 는 이 경로의 비용이다. VWC(Volatile Write Cache) 비트를 0 으로 보고하는 PLP 엔터프라이즈 SSD 는 Flush 를 거의 즉시 완료할 수 있다 — 그래서 DB 서버는 PLP SSD 를 쓴다.
- **unlink 는 SSD 에게 아무 말도 하지 않는다 — TRIM 을 보내기 전까지.** 파일을 지우면 FS 는 블록을 free 로 표시할 뿐, 장치는 그 LBA 가 여전히 유효하다고 믿고 GC 때 **쓰레기를 계속 복사** 한다. FS 가 `discard`(ATA TRIM / NVMe Dataset Management Deallocate)를 보내야 FTL 이 L2P 를 unmap 하고 valid count 를 줄인다. [부록 I](2026-09-30_C0I_flash_ssd.md)의 toy FTL 에서 TRIM 을 전원 손실 후에도 유지하려면 별도 로그가 필요하다는 것을 직접 확인한다.
- **rename 의 원자성 = FTL 의 L2P 갱신 원자성과 같은 문제.** FS 는 rename 을 저널 트랜잭션 하나로 묶어 원자성을 얻고, FTL 은 "데이터 program 완료 후 L2P 포인터 교체" 로 4KB 쓰기의 원자성을 얻는다. 둘 다 **새 사본을 다른 곳에 완성한 뒤 포인터 하나를 원자적으로 바꾼다** 는 shadow paging 패턴이다.
- **sparse file / hole ↔ unmapped LBA.** FS 의 hole 은 "블록 포인터가 없음", FTL 의 unmapped LBA 는 "L2P 엔트리가 없음". 둘 다 읽으면 0(NVMe 는 DLFEAT 로 deallocated 읽기 값을 보고)이고, 공간을 차지하지 않는다.
- **AI 학습의 체크포인트 저장.** 수백 GB 체크포인트를 "tmp 파일에 쓰기 → fsync → rename" 으로 교체하지 않으면, 쓰는 도중 노드가 죽었을 때 **유일한 체크포인트가 반쯤 쓰인 상태** 로 남는다. 이 장의 에디터 예제가 그대로 ML 인프라의 정석이다.

## 16. 면접 질문

### Q1. 파일을 안전하게(크래시에도 옛 버전 아니면 새 버전) 갱신하는 코드를 써 보라. 각 단계가 왜 필요한가?
<details>
<summary>답 보기</summary>

`fd=open("f.tmp", O_WRONLY|O_CREAT|O_TRUNC, 0644)` → `write` 전체 → **`fsync(fd)`**(rename 보다 데이터가 먼저 영속화되도록; 빠지면 지연 할당 FS 에서 0 바이트 파일 가능) → `close` → **`rename("f.tmp","f")`**(원자적 교체) → 디렉터리를 열어 **`fsync(dirfd)`**(rename 이라는 디렉터리 변경 자체를 영속화). macOS 라면 `F_FULLFSYNC` 로 장치 캐시까지. 에러가 나면 tmp 를 unlink. 실패한 fsync 는 재시도하지 말고 실패로 처리.

</details>

### Q2. hard link 와 symbolic link 의 차이를 inode 관점에서 설명하라. 왜 디렉터리에는 hard link 를 못 거나?
<details>
<summary>답 보기</summary>

hard link 는 디렉터리 엔트리 하나가 **같은 inode 번호** 를 가리키는 것 — 원본/사본 구분이 없고 inode 의 **link count** 가 늘어난다. 다른 FS 로는 못 건다(inode 번호는 FS 안에서만 유일). symlink 는 **대상 경로 문자열을 담은 별도 inode**(type=symlink)로, 접근 시 경로를 다시 해석한다 → FS 경계·디렉터리 OK, 대상이 사라지면 dangling. 디렉터리 hard link 금지 이유: 트리에 **순환** 이 생겨 `..` 가 모호해지고, 순회(find, fsck)가 끝나지 않으며, link count 기반 해제가 깨진다(순환 참조는 refcount 로 못 지움).

</details>

### Q3. 열려 있는 파일을 rm 하면 어떻게 되나? 디스크 공간은 언제 반환되나?
<details>
<summary>답 보기</summary>

`rm` 은 `unlink` — 디렉터리 엔트리만 지우고 link count 를 줄인다. inode 는 **link count == 0 이고 열린 참조(open file)가 0** 일 때 해제된다. 그래서 열어 둔 프로세스는 계속 읽고 쓸 수 있고, `df` 는 공간이 그대로인데 `du` 로는 안 보이는 현상이 생긴다(로그 파일을 rm 해도 디스크가 안 비는 고전 장애; `lsof +L1` 로 찾음). 마지막 close(또는 프로세스 종료) 때 반환된다. 크래시하면 다음 마운트 때 orphan inode 를 정리한다(ext4 orphan list).

</details>

### Q4. lseek 는 디스크 seek 을 일으키나? read/write 와 offset 의 관계, pread 가 왜 필요한가?
<details>
<summary>답 보기</summary>

아니다. lseek 는 커널의 **open file 객체 안의 offset 변수** 만 바꾼다. 다음 read/write 가 그 위치에서 시작하고 N 바이트 후 offset += N. 디스크 seek 은 실제 블록 I/O 가 다른 트랙일 때만 생긴다. offset 이 **open file 에 공유 상태** 라서 멀티스레드에서 lseek+read 는 경쟁 조건이 된다 → offset 을 인자로 받고 공유 offset 을 바꾸지 않는 **pread/pwrite** 를 쓴다. fork/dup 로 공유된 fd 도 offset 을 같이 움직인다.

</details>

### Q5. fsync 가 성공했다면 데이터는 정말 안전한가? 무엇이 그 보장을 깰 수 있나?
<details>
<summary>답 보기</summary>

조건부다. (1) **OS 가 장치 캐시 flush 를 내렸는가**: macOS fsync 는 안 내림(F_FULLFSYNC 필요), Linux 는 barrier/flush 를 내리지만 `nobarrier` 마운트면 아님. (2) **장치가 Flush 를 정직하게 처리하는가**: 일부 소비자 SSD/USB 브리지는 Flush 를 무시하거나 PLP 없이 일찍 완료. (3) **새 파일이면 디렉터리도 fsync 했는가**. (4) **에러 처리**: writeback 실패 후 fsync 는 EIO 를 한 번만 보고할 수 있고 페이지는 clean 처리되어 재시도 fsync 가 거짓 성공(fsyncgate). (5) 조용한 손상(bit rot)은 fsync 와 무관 → 체크섬. FW 관점에서는 Flush 완료 = 데이터 + **매핑 메타데이터** 까지 NAND 에 있어야 한다.

</details>

## 17. 자가 점검 & 숙제

### 퀴즈

**퀴즈 1.** 새 프로세스가 처음 open 한 파일의 fd 가 보통 3 인 이유는?

<details>
<summary>정답</summary>

0, 1, 2 가 stdin/stdout/stderr 로 이미 열려 있고, open 은 가장 작은 빈 번호를 준다.

</details>

**퀴즈 2.** `echo hello > f; ln f g; ln -s f s; rm f` 후 `cat g` 와 `cat s` 의 결과는?

<details>
<summary>정답</summary>

`cat g` → hello (hard link, link count 1 남음). `cat s` → No such file or directory (dangling symlink).

</details>

**퀴즈 3.** stat 의 st_size 와 st_blocks*512 가 다를 수 있는 두 경우는?

<details>
<summary>정답</summary>

sparse file(hole 이라 할당 < 크기), 그리고 작은 파일의 블록 단위 올림(6 바이트 파일도 4KB 블록 → 할당 > 크기). APFS 처럼 압축/인라인 데이터를 쓰는 FS 도 다를 수 있다.

</details>

**퀴즈 4.** rename 이 원자적이라는 것이 정확히 무엇을 보장하나? 무엇은 보장하지 않나?

<details>
<summary>정답</summary>

크래시 후 그 이름이 옛 파일 또는 새 파일을 가리키며 중간 상태가 없다는 것. **새 파일의 내용이 디스크에 있다는 것은 보장하지 않는다**(그래서 rename 전에 fsync), rename 자체가 영속화됐다는 것도 디렉터리 fsync 전에는 보장되지 않는다.

</details>

### 숙제 (원문 Homework 에서 고른 것)

- **Q1 Stat**: `stat()` 으로 크기, 블록 수, link count 를 출력하는 `mystat` 을 짜고, 디렉터리 안 항목 수를 바꾸며 디렉터리 link count 가 어떻게 변하는지 보기 → 위 실험처럼 ext4 와 APFS 의 답이 다르다는 것까지 확인.
- **Q3 Tail**: 파일 끝 근처로 `lseek` 해서 블록 하나를 읽고 뒤에서부터 개행을 세는 `mytail -n` → lseek 가 I/O 가 아니라 offset 조작이라는 것, 큰 파일을 처음부터 읽지 않는 효율의 감각.
- **Q4 Recursive Search**: `opendir/readdir` + `lstat` 으로 트리를 재귀 순회하는 `find` 흉내 → symlink 를 따라가면 순환에 빠질 수 있다는 것(그래서 lstat), `.`/`..` 건너뛰기.

## 18. 다음으로

API 를 봤으니, 이제 이 API 를 **어떻게 구현하나** — inode, 데이터 비트맵, 디렉터리 블록, 경로 해석 시 몇 번의 I/O 가 일어나는지 → [Ch.40 파일시스템 구현](2026-09-30_C40_file_system_implementation.md). fsync/rename 의 원자성이 크래시에서 어떻게 지켜지는지는 [Ch.42 저널링](2026-09-30_C42_crash_consistency_journaling.md)에서.
