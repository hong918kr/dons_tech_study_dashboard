---
id: OS-B09
time: 70분
updated: 2026-11-11
---
# OS-B09 mmap: 파일 매핑, 익명 매핑, MAP_SHARED vs PRIVATE

## 학습 목표

- `mmap` 의 네 조합(파일/익명 × `MAP_SHARED`/`MAP_PRIVATE`)이 **쓰기를 어디까지 보이게 하는지** — 파일, 같은 파일의 다른 매핑, `write(2)`, fork 한 부모·자식 — 를 **테스트 코드가 만든 표** 로 말할 수 있다.
- 매핑이 파일 끝(EOF)을 넘으면 무슨 일이 생기는지(마지막 페이지 꼬리는 0, 그 다음 페이지는 **SIGBUS**), `munmap` 한 곳을 만지면 **SIGSEGV** 인 것을 자식 프로세스 안에서 안전하게 재현한다.
- `msync`·`munmap`·`mmap` 의 오류 규칙(페이지 정렬, `len 0`, `MS_SYNC|MS_ASYNC`, 읽기 전용 fd + `MAP_SHARED`+`PROT_WRITE` = `EACCES`)을 실제 errno 로 확인한다.
- mmap 파일 복사와 read/write 복사를 직접 짜고, 어느 쪽이 왜 빠른지/느린지 **실측 숫자**(페이지 폴트 수 포함)로 설명한다.
- 이 맥(Apple M6, 페이지 16 KiB)에서 확인한 macOS 고유 동작 — `MAP_PRIVATE` 는 mmap 시점 스냅숏, 보호 위반 쓰기는 SIGBUS — 과 POSIX 가 "미지정" 으로 남긴 부분을 구분한다.

## 1. 문제

"mmap 이 read 보다 빠른가요? MAP_PRIVATE 에 쓰면 파일이 바뀌나요? fork 하면요? SIGBUS 는 언제 나죠?" — 시스템·임베디드 리눅스 면접의 단골이다. 임베디드 리눅스에서 mmap 은 매일 쓴다: `/dev/mem`·UIO 로 주변장치 레지스터를 사용자 공간에 매핑(ELX-A06), 드라이버가 DMA 버퍼를 사용자 공간에 내보내기(ELX-M03), V4L2 카메라 프레임 버퍼(CAM-A01), 프로세스 간 공유 메모리(OS-B14), 펌웨어 업데이트 도구가 이미지 파일을 매핑해 CRC 검사(BM-A04). 반대로 MMU 가 없는 Cortex-M 에는 mmap 이 없다 — 대신 MPU 영역(BM-A08, ARCH-M15)이 "이 주소 범위는 읽기 전용" 을 정한다.

`mmap(addr, len, prot, flags, fd, off)` 은 파일(또는 아무것도 아닌 0 페이지)을 **내 주소 공간의 한 구간** 으로 붙인다. 이후 `p[i]` 로 읽고 쓰는 것이 곧 파일 I/O 다. 핵심은 "그 페이지가 **누구의 것** 인가" 이다.

```svg
<svg viewBox="0 0 740 360" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">같은 파일의 페이지를 누가 가리키나 (macOS 의 통합 버퍼 캐시, OSTEP 40.7)</text>
  <rect x="20" y="45" width="190" height="120" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="32" y="65" fill="currentColor">프로세스 A</text>
  <rect x="35" y="75" width="160" height="30" class="f-ok" stroke="currentColor"/>
  <text x="45" y="95" fill="currentColor" font-family="monospace">m1 MAP_SHARED</text>
  <rect x="35" y="120" width="160" height="30" class="f-warn" stroke="currentColor"/>
  <text x="45" y="140" fill="currentColor" font-family="monospace">m2 MAP_PRIVATE</text>
  <rect x="20" y="190" width="190" height="70" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="32" y="210" fill="currentColor">프로세스 B (또는 read/write)</text>
  <rect x="35" y="220" width="160" height="30" class="f-ok" stroke="currentColor"/>
  <text x="45" y="240" fill="currentColor" font-family="monospace">m3 MAP_SHARED</text>
  <rect x="320" y="110" width="170" height="80" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="335" y="135" fill="currentColor">페이지 캐시의 파일 페이지</text>
  <text x="335" y="155" fill="currentColor">(커널 소유, 하나뿐)</text>
  <text x="335" y="175" fill="currentColor" font-family="monospace">pread/pwrite 도 여기</text>
  <rect x="320" y="230" width="170" height="50" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="335" y="252" fill="currentColor">m2 전용 사본</text>
  <text x="335" y="270" fill="currentColor">(쓰는 순간 복사, COW)</text>
  <rect x="580" y="130" width="140" height="40" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="595" y="155" fill="currentColor">저장장치 (SSD)</text>
  <g stroke="currentColor" stroke-width="2" fill="none">
    <path d="M195 90 L318 135"/><path d="M195 235 L318 170"/>
    <path d="M195 135 L318 250" stroke-dasharray="5 4"/>
    <path d="M490 150 L578 150"/>
  </g>
  <text x="500" y="140" fill="currentColor">writeback</text>
  <text x="500" y="190" fill="currentColor">msync/fsync</text>
  <text x="20" y="305" fill="currentColor">SHARED: 모두가 같은 페이지 → 쓰기가 서로·파일에 보인다.</text>
  <text x="20" y="325" fill="currentColor">PRIVATE: 쓰는 페이지는 내 사본 → 아무에게도 안 보인다.</text>
  <text x="20" y="345" fill="currentColor">macOS 관찰: PRIVATE 는 매핑 뒤의 파일 변경도 안 보인다 (스냅숏, POSIX 는 미지정).</text>
</svg>
```

COW(copy-on-write) 자체의 구현 — 쓰기 보호 PTE, 페이지 폴트, `ru_minflt` 로 세기 — 는 OS-M03 에서 한다. 여기서는 **보이는 결과** 만 실험으로 확정한다.

### Part A — 파일 매핑: SHARED vs PRIVATE 표

- `char *map_file(int fd, size_t len, int flags)` — `PROT_READ|PROT_WRITE` 로 매핑, 실패 시 `NULL`.
- `int probe_file(const char *path, int flags, file_row_t *r)` — 'A' 로 채운 2 페이지 파일을 만들고 `flags` 매핑 `m` 과 비교용 `MAP_SHARED` 매핑 `o` 를 만든 뒤 네 칸을 잰다:
  (a) `m[0]='W'` 가 `pread` 로 보이나, (b) `o[0]` 에 보이나, (c) `pwrite` 로 2번째 페이지를 바꾸면 `m` 에 보이나, (d) `munmap` 뒤 파일에 남았나.
- 하네스가 추가로 확인: `len 0` → `EINVAL`, 읽기 전용 fd 에 `MAP_SHARED`+`PROT_WRITE` → `EACCES`, 같은 fd 에 `MAP_PRIVATE`+`PROT_WRITE` 는 **된다**, `close(fd)` 뒤에도 매핑은 살아 있다.

### Part B — 익명 매핑, fork, SIGBUS/SIGSEGV

- `char *map_anon(size_t len, int flags)` — `MAP_ANON`, fd = -1.
- `child_write_seen_by_parent(p)` / `parent_write_seen_by_child(p)` — 파일 SHARED / 파일 PRIVATE / 익명 SHARED / 익명 PRIVATE 네 줄의 fork 표. 두 번째는 부모가 **fork 뒤에** 쓰고 pipe 로 자식을 깨운다.
- `int touch_in_child(volatile char *p, bool write)` — 자식 안에서 접근하고, 죽었으면 **진짜 시그널 번호** 를 돌려준다. 하네스는 이걸로 ① 100 바이트 파일을 2 페이지로 매핑했을 때 같은 페이지 꼬리(OK)와 다음 페이지(SIGBUS) ② 매핑 중 `ftruncate(0)` (SIGBUS) ③ 가운데 페이지만 `munmap` (그 페이지만 SIGSEGV) 을 잰다. `msync` 규칙(정렬, 플래그, 구멍 → `ENOMEM`)도 같이 본다.

### Part C — 파일 복사 두 가지

- `int copy_rw(src, dst, bufsz)` — read/write 루프 (부분 쓰기, `EINTR`).
- `int copy_mmap(src, dst)` — src 를 `PROT_READ` 로, dst 를 `ftruncate` 로 크기부터 만든 뒤 `MAP_SHARED` 로 매핑해 `memcpy`. 빈 파일은 mmap 하지 않는다.
- 크기 0, 1, 100, PG-1, PG, PG+1, 3PG+123, 1 MiB+7 에서 바이트 단위로 같아야 한다. 속도는 `demo_bench.c` (256 MiB) 에서 잰다 — 펌웨어 쪽 연결: 장비의 eMMC 로그 수집기, 업데이트 이미지 복사·검증 도구가 바로 이 선택을 한다.

## 2. Problem (English)

> **mmap semantics and a mapped file copy.**
>
> ```c
> char *map_file(int fd, size_t len, int flags);           // PROT_READ|PROT_WRITE; NULL on failure
> int   probe_file(const char *path, int flags, file_row_t *r);
> char *map_anon(size_t len, int flags);                   // MAP_ANON, fd -1
> int   child_write_seen_by_parent(volatile char *p);      // 1 / 0 / -1
> int   parent_write_seen_by_child(volatile char *p);      // parent writes AFTER fork
> int   touch_in_child(volatile char *p, bool write);      // 0, or the signal number that killed the access
> int   copy_rw(const char *src, const char *dst, size_t bufsz);
> int   copy_mmap(const char *src, const char *dst);
> ```
>
> 1. Fill in a table: for `MAP_SHARED` and `MAP_PRIVATE` file mappings, is a store through the mapping visible to `read()`, to another shared mapping of the same file, and does it survive `munmap`? Is a later `pwrite()` visible through the mapping?
> 2. For file/anonymous × shared/private mappings, are writes visible across `fork()` in both directions?
> 3. Map a 100-byte file with a length of two pages. What do you read at offset 100? At offset `PAGE_SIZE + 10`? What if the file is truncated while mapped?
> 4. Copy a file both ways; handle empty files and short writes. Which is faster and why?
>
> Follow-ups: Why can `MAP_PRIVATE` + `PROT_WRITE` succeed on an `O_RDONLY` fd but `MAP_SHARED` cannot? What does `msync` guarantee? When is mmap I/O a bad idea? What happens to a mapping after `close(fd)`?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

처음 실행 결과 (실제 출력 일부, 0.1 초에 끝남 — 멈추지 않는다):

```text
-- Part A: file mapping (page size 16384)
FAIL starter.c:233  m != NULL
FAIL starter.c:234  m && m[0] == 'A' && m[PG - 1] == 'A'
FAIL starter.c:238  map_file(fd, 0, MAP_SHARED) == NULL && errno == EINVAL
...
  MAP_SHARED   | ?                       | ?                       | ?                      | ?
...
FAIL starter.c:391  copy_mmap(tmp_path("src"), tmp_path("dst_mm")) == 0 && files_equal(...)
2/40 passed
```

하네스 설계:

- **페이지 크기는 하드코딩하지 않는다.** `PG = sysconf(_SC_PAGESIZE)` — 이 맥(Apple Silicon)은 **16384**, 보통의 Linux/x86 은 4096. "다음 페이지" 실험은 전부 `PG` 기준이다.
- **시그널은 자식 안에서만.** `touch_in_child` 의 자식은 `child_catch_faults()` 로 SIGBUS/SIGSEGV 를 잡아 `_exit(100 + sig)` 한다. 잡지 않고 죽게 두면 macOS 가 `~/Library/Logs/DiagnosticReports/` 에 크래시 리포트(.ips)를 실행마다 남긴다 — 실제로 탐색 중에 그렇게 쌓였다. 잡아서 끝내면 남지 않는다.
- **멈추지 않는다.** 자식은 `alarm(2)`, 부모는 `wait_child(pid, 2000)` (1 ms 간격 `WNOHANG` 폴링, 시간 초과면 SIGKILL 후 회수). `main` 의 `alarm(60)` 은 최후의 안전장치.
- **임시 파일** 은 `mkdtemp("/tmp/OS-B09_XXXXXX")` 안에만 만들고 끝에서 디렉터리째 지운다. 어느 cwd 에서 실행해도 같다.
- 체크 개수는 TODO 가 비어도 40 개로 고정 (매핑이 실패해도 해당 체크를 건너뛰지 않고 FAIL 로 센다).

관찰용 파일 (하네스와 별개):

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_sigbus.c -o dsb && ./dsb          # si_code / si_addr 까지 보기
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o bench && ./bench 256 5  # 복사 속도 + msync/fsync
```

## 4. 힌트

<details>
<summary>힌트 1 — mmap 의 실패 값은 NULL 이 아니다</summary>

`mmap` 은 실패하면 `MAP_FAILED` (`(void *)-1`) 를 돌려준다. `if (!p)` 로 검사하면 실패를 놓친다. 이 레슨의 래퍼는 그걸 `NULL` 로 바꿔 준다.

</details>

<details>
<summary>힌트 2 — probe_file: 두 번째 페이지를 쓰는 이유</summary>

(c) 는 "내가 **아직 쓰지 않은** 페이지를 파일 쪽에서 바꾸면 보이나" 를 묻는다. 내가 쓴 페이지(0번)는 PRIVATE 라면 이미 내 사본이라 당연히 안 보인다. 그래서 `pwrite(fd, "F", 1, PG)` 로 1번 페이지를 바꾸고 `m[PG]` 를 본다.

</details>

<details>
<summary>힌트 3 — fork 뒤 부모가 쓴 것을 자식이 보려면 "순서" 가 필요하다</summary>

자식이 너무 일찍 읽으면 SHARED 라도 'A' 를 본다. pipe 를 하나 만들어 자식은 `read` 로 기다리고, 부모는 `p[1]='P'` 를 쓴 **다음** 1 바이트를 써서 깨운다. 자식은 쓰기 끝을 닫아 둬야 부모가 죽었을 때 `read` 가 EOF(0)로 풀린다. 결과는 종료 코드(1 = 보임, 2 = 안 보임)로 돌려준다.

</details>

<details>
<summary>힌트 4 — touch_in_child: 종료 상태 해석</summary>

`WIFEXITED` 이고 코드 0 → 정상. 코드가 `FAULT_EXIT_BASE` 보다 크면 핸들러가 잡은 시그널 = 코드 − 100. `WIFSIGNALED` 면 `WTERMSIG` (핸들러가 없었던 경우). 시간 초과(`wait_child` 가 -1)는 -1. 상태 해석 자체는 OS-B01 과 같다.

</details>

<details>
<summary>힌트 5 — copy_mmap: 크기부터 만든다</summary>

`O_TRUNC` 로 연 dst 는 크기 0 이다. 그 위에 n 바이트를 매핑하면 **모든 페이지가 EOF 너머** → 첫 `memcpy` 에서 SIGBUS. 반드시 `ftruncate(out, n)` 먼저. dst 는 `MAP_SHARED`+`PROT_WRITE` 이므로 `O_RDWR` 로 열어야 한다 (`O_WRONLY` 면 `EACCES` — POSIX 는 매핑에 읽기 권한을 항상 요구한다). 크기 0 이면 `mmap(len=0)` 이 `EINVAL` 이니 건너뛴다.

</details>

## 5. 풀이

### Part A — 결과 표 (solution 실제 출력)

```text
-- Part A: file mapping (page size 16384)
  mapping      | (a) map write -> read() | (b) -> other SHARED map | (c) pwrite -> this map | (d) kept after munmap
  MAP_SHARED   | yes                     | yes                     | yes                    | yes
  MAP_PRIVATE  | no                      | no                      | no                     | no
```

| 칸 | SHARED | PRIVATE | 근거 |
|---|---|---|---|
| (a) 매핑 쓰기 → `pread` | 보임 | 안 보임 | SHARED 매핑과 read/write 는 같은 페이지 캐시 페이지를 쓴다 (통합 버퍼 캐시, OSTEP 40.7). PRIVATE 는 쓰는 순간 사본 |
| (b) → 다른 SHARED 매핑 | 보임 | 안 보임 | 같은 이유 |
| (c) `pwrite` → 이 매핑 | 보임 | **안 보임 (macOS)** | POSIX: "MAP_PRIVATE 매핑을 만든 뒤 바뀐 내용이 보이는지는 **미지정**". 이 맥에서는 안 쓴 페이지, 읽기만 한 페이지, `PROT_READ` 매핑, 100 ms 기다린 뒤 모두 'A' — mmap 시점 스냅숏으로 동작 |
| (d) `munmap` 뒤 파일 | 남음 | 없음 | SHARED 의 변경은 이미 페이지 캐시에 있다. `munmap` 은 "버리기" 가 아니다. PRIVATE 사본은 그냥 사라진다 |

(c) 는 이식성 함정이다. 리눅스 `mmap(2)` 도 같은 "unspecified" 문구를 쓴다. 리눅스에서는 아직 COW 가 안 된 페이지에 파일 변경이 보인다고 알려져 있지만, **이 레슨에서는 리눅스로 검증하지 않았다**. 그래서 이 체크는 `#ifdef __APPLE__` 안에만 있다. 설계 규칙: PRIVATE 매핑으로 읽는 동안 파일이 바뀔 수 있다면, 어느 쪽 동작에도 기대지 말 것.

탐색 단계에서 본 macOS 사실들 (`explore` 프로그램, 실제 출력):

```text
ro fd shared+W: 0xffffffffffffffff errno=13(Permission denied)    ← EACCES
ro fd private+W: 0x10437c000                                      ← 된다
 file still C                                                     ← PRIVATE 쓰기는 파일로 안 간다
len0: 0xffffffffffffffff errno=22 Invalid argument
offset 100: 0xffffffffffffffff errno=22 Invalid argument          ← off 는 페이지 정렬
dir mmap: ... errno=22   pipe mmap: ... errno=22                  ← man 은 ENODEV 라 하지만 실제는 EINVAL
after close read c[0]=C                                           ← close 뒤에도 매핑 유효
```

읽기 전용 fd 에 `MAP_PRIVATE`+`PROT_WRITE` 가 되는 이유: 쓰기가 **파일로 가지 않으니** 파일 쓰기 권한이 필요 없다. POSIX `EACCES` 조건도 정확히 "fd 가 읽기용이 아님, 또는 **MAP_SHARED** 에 PROT_WRITE 인데 fd 가 쓰기용이 아님" 이다. 실행 파일·공유 라이브러리의 `__DATA` 가 이렇게 매핑된다 (OS-B08).

`close(fd)` 뒤에도 매핑이 사는 이유: POSIX — mmap 은 파일에 **별도의 참조** 를 하나 더 걸고, 그 참조는 `close` 로 없어지지 않는다. 마지막 매핑이 사라질 때 풀린다. 그래서 "매핑 후 바로 close" 는 흔한 정석 패턴이다 (연습 1).

### Part B — fork 표 (solution 실제 출력)

```text
  mapping       | child write -> parent | parent write (after fork) -> child
  file SHARED   | yes                   | yes
  file PRIVATE  | no                    | no
  anon SHARED   | yes                   | yes
  anon PRIVATE  | no                    | no
```

POSIX `fork()`: 매핑은 자식에 그대로 상속되고, PRIVATE 매핑은 자식에서도 PRIVATE. fork **전** 부모의 변경은 자식에 보이고, fork **후** 의 변경은 각자에게만. 일반 `malloc` 메모리·스택이 바로 이 "익명 PRIVATE" 이다 — fork 후 자식이 전역 변수를 바꿔도 부모가 모르는 이유. 반대로 **익명 SHARED** 는 파일 없이 부모·자식이 공유하는 가장 간단한 방법이다 (연습 2 의 공유 카운터, OS-M17 의 프로세스 공유 mutex 가 이 위에 선다). 이름 있는 공유 메모리가 필요하면 `shm_open` (OS-B14).

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">fork 후: SHARED 는 같은 물리 페이지, PRIVATE 는 쓰는 쪽이 사본을 받는다</text>
  <rect x="20" y="40" width="130" height="34" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="32" y="62" fill="currentColor">부모 p (SHARED)</text>
  <rect x="20" y="100" width="130" height="34" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="32" y="122" fill="currentColor">자식 p (SHARED)</text>
  <rect x="220" y="68" width="120" height="40" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="232" y="93" fill="currentColor">물리 페이지 X</text>
  <g stroke="currentColor" stroke-width="2" fill="none">
    <path d="M150 57 L218 80"/><path d="M150 117 L218 98"/>
  </g>
  <rect x="400" y="40" width="140" height="34" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="410" y="62" fill="currentColor">부모 p (PRIVATE)</text>
  <rect x="400" y="100" width="140" height="34" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="410" y="122" fill="currentColor">자식 p (PRIVATE)</text>
  <rect x="600" y="40" width="120" height="34" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="612" y="62" fill="currentColor">원래 페이지 Y</text>
  <rect x="600" y="100" width="120" height="34" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="612" y="122" fill="currentColor">사본 Y' ('C')</text>
  <g stroke="currentColor" stroke-width="2" fill="none">
    <path d="M540 57 L598 57"/><path d="M540 117 L598 117"/>
  </g>
  <text x="400" y="160" fill="currentColor">자식 p[0]='C' → 보호 폴트 → 사본 생성,</text>
  <text x="400" y="178" fill="currentColor">자식 PTE 만 바꾼다 (COW 상세는 OS-M03)</text>
  <text x="20" y="160" fill="currentColor">자식이 p[0]='C' → 부모도 'C' 를 본다</text>
  <text x="20" y="215" fill="currentColor">표의 두 열 = 이 그림의 두 방향. 파일 SHARED 는 그 페이지가 페이지 캐시 페이지라는 것만 다르다.</text>
  <text x="20" y="235" fill="currentColor">익명 SHARED 는 파일 대신 커널의 익명 메모리 객체가 X 를 소유한다.</text>
</svg>
```

### Part B — 파일 끝(EOF) 너머: 꼬리는 0, 다음 페이지는 SIGBUS

```text
  EOF: inside=0  next page read=10 write=10  (SIGBUS=10)  tail write -> size 100, byte200 after grow=0
  after ftruncate(0): read=10   after ftruncate(PG+1): page 2 read=0
```

```svg
<svg viewBox="0 0 740 200" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">100 바이트 파일을 2 페이지(2 × 16 KiB)로 매핑</text>
  <rect x="20" y="50" width="30" height="50" class="f-ok" stroke="currentColor"/>
  <rect x="50" y="50" width="300" height="50" class="f-mut" stroke="currentColor"/>
  <rect x="350" y="50" width="330" height="50" class="f-warn" stroke="currentColor"/>
  <text x="22" y="120" fill="currentColor" font-family="monospace">0..99</text>
  <text x="60" y="80" fill="currentColor">100..16383: 0 으로 채운 꼬리 (읽기·쓰기 OK)</text>
  <text x="360" y="80" fill="currentColor">16384..32767: 파일에 대응 페이지 없음</text>
  <text x="20" y="145" fill="currentColor">꼬리에 쓴 값은 파일로 안 간다 (크기 100 그대로). ftruncate 로 늘리면 그 자리는 0 (POSIX).</text>
  <text x="360" y="120" fill="currentColor">접근 → SIGBUS (매핑은 있으나 객체가 없음)</text>
  <text x="20" y="170" fill="currentColor">매핑 중 ftruncate(0) → 0번 페이지도 SIGBUS.  ftruncate(PG+1) → 2번째 페이지가 다시 생겨 OK.</text>
  <text x="20" y="190" fill="currentColor">munmap 한 주소 → SIGSEGV (매핑 자체가 없음). 둘을 구분해 기억하자.</text>
</svg>
```

POSIX `mmap` 의 문장 그대로: 시스템은 객체 끝의 **부분 페이지를 항상 0 으로 채우고**, 그 부분의 변경을 파일에 **쓰지 않으며**, 객체 끝 다음의 **온전한 페이지** 를 참조하면 SIGBUS 를 보낸다. 측정이 정확히 이렇다:

- `m[100]`, `m[PG-1]` = 0, `m+10` 읽기 OK — 같은 페이지.
- `m+PG+10` 읽기·쓰기 = **10 (SIGBUS)**.
- `m[200]='Z'` 뒤 파일 크기 100. `ftruncate(300)` 뒤 200 번째 바이트는 0 — POSIX `ftruncate` 는 "늘어난 영역은 0 으로 채운 것처럼 보여야" 한다고 요구한다. 이 맥에서는 매핑 쪽 `m[200]` 도 0 으로 바뀌었다 (탐색 출력 `m200=0`).
- **다른 프로세스가 파일을 줄이는 것** 이 실무에서 SIGBUS 의 주범이다 — 로그 로테이터가 `truncate` 한 파일을 다른 데몬이 mmap 으로 읽다 죽는 식. 방어: 파일을 잠그거나, SIGBUS 핸들러(+`sigsetjmp`)로 복구하거나, mmap 대신 `pread`.

`demo_sigbus.c` 로 핸들러가 받은 정보를 직접 본다 (실제 출력):

```text
page size 16384, SIGBUS=10 SIGSEGV=11, BUS_ADRALN=1 BUS_ADRERR=2 BUS_OBJERR=3 SEGV_MAPERR=1 SEGV_ACCERR=2
1) 100-byte file mapped as 2 pages: read base+200 (same page as EOF)
    no fault
2) read base+PG+10 (whole page beyond EOF)
    caught signo=10 (SIGBUS) si_code=1 si_addr=0x0000000100f1c00a = base+16394
3) ftruncate(fd, 0) while mapped, then read base+0
    caught signo=10 (SIGBUS) si_code=1 si_addr=0x0000000100f18000 = base+0
4) write to a PROT_READ anonymous page
    caught signo=10 (SIGBUS) si_code=1 si_addr=0x0000000100f18000 = base+0
5) read after munmap
    caught signo=11 (SIGSEGV) si_code=2 si_addr=0x0000000100f18000 = base+0
```

- `si_addr` 는 **정확한 바이트 주소** (base+16394 = PG+10) — 폴트 주소로 "어느 매핑의 몇 번째 바이트" 인지 알 수 있다. OS-A06 의 사용자 공간 페이지 폴트 처리가 이걸 쓴다.
- macOS 의 `si_code` 는 이름과 잘 안 맞는다: EOF 너머도, 보호 위반 쓰기도 SIGBUS 에 `si_code=1` (`BUS_ADRALN`, "정렬 오류" 라는 이름). 그리고 **읽기 전용 페이지에 쓰기 = SIGBUS** 다 — 리눅스 `mmap(2)` 은 이 경우를 SIGSEGV 로 문서화한다. 이식성 있는 코드는 둘 다 잡아야 한다. (리눅스의 si_code 는 이 레슨에서 검증하지 않음.)
- 핸들러 안에서는 `printf` 대신 손으로 만든 문자열을 `write(2)` 한다 — async-signal-safe 함수만 (OS-B07).

### Part B — munmap 과 msync 규칙

```text
  munmap middle page: left=0 middle=11 right=0 (SIGSEGV=11)  msync over hole=-1 errno=12  after full munmap=11
```

| 호출 | 결과 (이 맥) | 근거 |
|---|---|---|
| `munmap(t+1, PG)` | -1, `EINVAL` | 주소는 페이지 정렬 (`man 2 munmap`) |
| `munmap(t+PG, PG)` (가운데) | 0, 가운데만 SIGSEGV, 양옆 OK | 매핑 하나가 둘로 쪼개진다 |
| 같은 구간 `munmap` 한 번 더 | 0 (탐색에서 확인) | 이미 없는 구간도 성공 — POSIX 가 허용 |
| `msync(t, 3PG, MS_SYNC)` (구멍 포함) | -1, `ENOMEM` | "unmapped page 를 포함" (`man 2 msync`) |
| `msync(m+1, 10, MS_SYNC)` | -1, `EINVAL` | 주소 정렬 |
| `msync(m, PG, MS_SYNC\|MS_ASYNC)` | -1, `EINVAL` | 둘은 함께 못 쓴다 |
| `msync(m, PG, MS_SYNC)` | 0 | 이후 `pread` 에도 's' (애초에 같은 페이지) |

**msync 는 "보이게" 하는 함수가 아니다.** Part A 의 (a) 처럼 SHARED 매핑의 쓰기는 msync 없이도 즉시 `read()` 에 보인다 (같은 페이지 캐시). msync 는 그 더러운 페이지를 **저장장치로 내려 보내는** 함수다 — 매핑판 `fsync`. 그리고 macOS 의 `fsync` 는 드라이브의 휘발성 캐시까지는 비우지 않는다(`man 2 fsync` → `F_FULLFSYNC`). 전원 차단까지 버티는 저장은 OS-B13.

### Part C — 두 복사 함수

`copy_rw` 의 핵심은 루프 두 개다: 바깥은 `read` 가 0(EOF)이 될 때까지, 안쪽은 `write` 가 **부분 쓰기** 를 하면 남은 바이트만 다시. `EINTR` 은 재시도. `copy_mmap` 의 핵심은 순서다: `open(O_RDWR|O_CREAT|O_TRUNC)` → `ftruncate(n)` → 두 매핑 → `memcpy` → `munmap` → `close`. 크기 0 은 파일만 만들고 끝.

결과: 8 가지 크기 × 3 방식 모두 바이트 단위 일치 (`sizes tested: 8   copy_rw ok=8  copy_rw(odd buf) ok=8  copy_mmap ok=8`). 홀수 버퍼(7 바이트, 큰 파일은 4093)는 "버퍼가 페이지·파일 크기와 안 맞아도 된다" 를 확인한다.

### Part C — 속도: `demo_bench.c` 실제 출력

Apple M6 (`sysctl -n machdep.cpu.brand_string`), macOS 27.2, `Apple clang version 21.0.0`, APFS 내장 SSD, RAM 32 GB. 256 MiB, 5 회 번갈아 실행 중앙값, 워밍업 1 회, 결과 파일은 시간 밖에서 `memcmp` 로 검증. 다른 작업이 함께 돌던 시점이라 실행마다 흔들린다 — **비율만** 보자.

```text
file 256 MiB, 5 runs (median), page size 16384, warm page cache (src just written), no fsync in copy
method                              median ms       MB/s   minflt/run   majflt verified
read/write  4 KiB buf                   654.7        391            0        0 yes
read/write 64 KiB buf                   104.1       2460            0        0 yes
read/write  1 MiB buf                    63.3       4041           13        0 yes
mmap src + mmap dst + memcpy             99.5       2573        16384    16384 yes
mmap src + write() 1 MiB                 53.0       4833        16384        0 yes
mmap+madvise(SEQ|WILLNEED)+memcpy        94.3       2715        16384    16384 yes

64 MiB dirty -> storage (median of 5, fresh file each):
  mapping: msync(MS_ASYNC)                 :     7.38 ms
           then msync(MS_SYNC)             :     0.41 ms
  mapping: msync(MS_SYNC) only             :    16.12 ms
  write() 64 x 1 MiB, then fsync()         :     1.22 ms
```

같은 명령을 세 번 돌린 범위 (MB/s): 4 KiB 362–444, 64 KiB 2460–2923, 1 MiB 4041–4606, mmap+memcpy 2573–2823, mmap+write 3668–4833, madvise 1574–3241.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">256 MiB 복사 처리량 (MB/s, 위 실행의 중앙값, warm cache, fsync 없음)</text>
  <g stroke="currentColor">
    <rect x="240" y="40" width="39" height="22" class="f-warn"/>
    <rect x="240" y="72" width="246" height="22" class="f-mut"/>
    <rect x="240" y="104" width="404" height="22" class="f-ok"/>
    <rect x="240" y="136" width="257" height="22" class="f-acc"/>
    <rect x="240" y="168" width="483" height="22" class="f-ok"/>
    <rect x="240" y="200" width="272" height="22" class="f-acc"/>
  </g>
  <g fill="currentColor">
    <text x="10" y="56">read/write 4 KiB</text><text x="285" y="56">391</text>
    <text x="10" y="88">read/write 64 KiB</text><text x="492" y="88">2460</text>
    <text x="10" y="120">read/write 1 MiB</text><text x="650" y="120">4041</text>
    <text x="10" y="152">mmap + mmap + memcpy</text><text x="503" y="152">2573</text>
    <text x="10" y="184">mmap src + write()</text><text x="640" y="184" fill="currentColor">4833</text>
    <text x="10" y="216">mmap + madvise + memcpy</text><text x="518" y="216">2715</text>
    <text x="10" y="244">막대 길이 = MB/s ÷ 10 px. 32768 페이지 폴트(16 KiB 당 1 번 × src·dst)가 mmap+memcpy 의 비용.</text>
  </g>
</svg>
```

읽는 법:

1. **"mmap 이 항상 빠르다" 는 틀렸다.** 순차 복사 한 번이면 큰 버퍼 read/write(1 MiB)가 mmap+memcpy 보다 1.5 배쯤 빠르다. mmap 쪽은 **페이지마다 폴트** 를 낸다: 256 MiB ÷ 16 KiB = 16384 — 표의 `minflt 16384` (src, 이미 캐시에 있음)와 `majflt 16384` (dst) 가 정확히 그 숫자다. 폴트 한 번 = 예외 진입 + 페이지 테이블 갱신. read/write 1 MiB 는 시스템 콜 512 번이면 끝난다.
2. **버퍼 크기가 제일 큰 손잡이다.** 4 KiB 버퍼는 시스템 콜 131072 번(읽기·쓰기 각 65536) — 1 MiB 대비 약 10 배 느리다. 시스템 콜 한 번의 고정 비용은 OS-B03.
3. **mmap src + write()** 가 가장 빨랐다: 사용자 버퍼로의 복사 한 번이 없어지고(커널이 매핑된 src 페이지에서 바로 페이지 캐시로), 시스템 콜도 256 번. 대신 src 폴트 16384 번은 남는다.
4. dst 의 폴트가 `majflt` 로 잡힌 것은 새 파일의 빈 페이지도 vnode pager 를 거쳐 들어오기 때문으로 **추정** 한다 (디스크 읽기를 실제로 했다는 증거는 없다).
5. `madvise(SEQUENTIAL|WILLNEED)` 는 이 측정에서 일관된 이득이 없었다 (1574–3241, 가장 흔들림). 힌트일 뿐 보장이 아니다.
6. **cold cache 는 재지 않았다.** src 를 방금 써서 페이지 캐시에 있다. 캐시를 비우려면 `sudo purge` 가 필요해 실행하지 않았다. cold 에서는 SSD 읽기가 지배해 방식 차이가 줄어든다 — 이것도 측정 안 한 예측이다.
7. 복사 시간에 **fsync 는 없다** = 페이지 캐시까지만. 두 번째 표: `MS_SYNC` 만 하면 64 MiB 에 13–16 ms, `MS_ASYNC` 는 6–7 ms 를 쓰고 돌아왔고 그 뒤 `MS_SYNC` 는 0.3–0.4 ms — 이 맥에서 `MS_ASYNC` 는 "즉시 반환" 이라는 man 문구와 달리 쓰기를 상당 부분 진행하고 돌아온다. `write()`+`fsync()` 1.2 ms 는 64 MiB 를 1.2 ms 에 옮겼다는 뜻이 아니다 (53 GB/s 는 불가능) — `write` 도중 이미 writeback 이 시작됐을 가능성(추정)과, `fsync` 가 드라이브 캐시를 비우지 않는다는 점(`man 2 fsync`)을 함께 봐야 한다. 내구성 비용의 제대로 된 측정은 OS-B13.

**그러면 mmap 은 언제 이기나?** ① 같은 파일을 **여러 번, 무작위로** 읽을 때 (DB 인덱스, 실행 파일·라이브러리 로딩, GGUF 모델 가중치 — EML-A01): 시스템 콜 없이 포인터로 접근하고 사본이 하나뿐이다. ② 여러 프로세스가 같은 데이터를 공유할 때 (사본 0 개). ③ 장치 메모리(레지스터, DMA 버퍼)처럼 애초에 read/write 로는 못 하는 것. 순차 스트리밍 한 번은 큰 버퍼 read/write 로 충분하다.

### Part C — 펌웨어로 옮기면

| 이 레슨 | 임베디드 리눅스 / MCU |
|---|---|
| 파일 SHARED 매핑 | `/dev/mem`·UIO `mmap` 으로 레지스터 접근 (ELX-A06), VFIO 로 NVMe BAR 매핑 (PCIE-A01). 장치 메모리는 캐시 불가 속성으로 매핑된다 |
| 익명 SHARED | 같은 보드의 센서 데몬 ↔ 로거 데몬 공유 버퍼 (OS-B14, OS-M17) |
| 드라이버가 내보내는 매핑 | `remap_pfn_range` 로 커널 DMA 버퍼를 사용자 공간에 (ELX-M03). CPU 캐시와 DMA 의 일관성은 ARCH-M12 |
| SIGBUS | 장치가 사라진(핫플러그 제거된) BAR, 줄어든 파일, 저장장치 I/O 오류(`MAP_RESILIENT_MEDIA` 가 macOS 헤더에 있다) |
| `mprotect(PROT_READ)` 로 잠근 설정 (연습 3) | Cortex-M 의 MPU 영역 읽기 전용 + MemManage 폴트 (BM-A08, ARCH-M15, SEC-M08) |
| MMU 없음 | Cortex-M 에는 mmap·COW·fork 가 없다. 주소 배치는 링커 스크립트가 정하고, "공유" 는 그냥 같은 전역 변수다 (SYSD-B12) |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
```

```text
(Part A 표, Part B fork 표 / EOF / munmap 줄, Part C 줄 — 5. 풀이에 실은 것과 같다)
40/40 passed
```

7 번 연속 실행(레슨 디렉터리 6 번 + 다른 cwd 1 번) 모두 `40/40 passed`, 실행 시간 약 0.25 초. 새니타이저 — 경고 0, 종료 코드 0, 크래시 리포트 0 개:

```bash
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o so_asan && ./so_asan
```

ASan 아래에서도 자식의 SIGBUS/SIGSEGV 핸들러가 그대로 동작한다 (자식이 `sigaction` 으로 ASan 의 핸들러를 덮어쓴다). mmap 영역은 ASan 이 계측하지 않는 메모리라, 이 레슨의 버그(EOF 너머 접근 등)는 ASan 이 아니라 **시그널** 로 드러난다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `if (p == NULL)` 로 mmap 실패 검사 | 실패 값은 `MAP_FAILED` (`(void*)-1`) | `if (p == MAP_FAILED)` |
| `O_TRUNC` 로 연 dst 를 바로 n 바이트 매핑 | 파일 크기 0 → 모든 페이지가 EOF 너머 → SIGBUS | `ftruncate(out, n)` 먼저 |
| dst 를 `O_WRONLY` 로 열고 `MAP_SHARED` | 매핑에는 읽기 권한이 필요 → `EACCES` | `O_RDWR` |
| 빈 파일도 mmap | `len 0` → `EINVAL` | 크기 0 은 따로 처리 |
| 페이지 크기 4096 하드코딩 | Apple Silicon 은 16384. offset 정렬·"다음 페이지" 계산이 틀린다 | `sysconf(_SC_PAGESIZE)` |
| "msync 해야 다른 프로세스에 보인다" | SHARED 쓰기는 즉시 보인다. msync 는 **저장장치** 로 보내는 것 | 공유엔 동기화(원자 연산·락), 내구성엔 msync/fsync |
| MAP_PRIVATE 로 열어 두고 파일 변경을 기대 (또는 기대하지 않음) | POSIX 미지정. macOS 는 스냅숏 | 바뀌는 파일은 SHARED 로 읽거나 다시 매핑 |
| 매핑 중인 파일을 다른 곳에서 `truncate` | 줄어든 뒤 페이지 접근 = SIGBUS | 잠금/프로토콜, 또는 SIGBUS 복구 |
| fork 후 익명 PRIVATE(`malloc`) 메모리로 부모·자식 통신 | 각자 사본 — 서로 안 보인다 | `MAP_SHARED\|MAP_ANON` 또는 pipe |
| 공유 매핑 위 카운터를 `c++` | 프로세스 간 데이터 레이스 — 스레드와 같다 (OS-B04) | `_Atomic` + `atomic_fetch_add` (연습 2), 또는 프로세스 공유 mutex (OS-M17) |
| 테스트에서 자식이 SIGSEGV 로 그냥 죽게 둠 | macOS 가 실행마다 크래시 리포트(.ips)를 남김 | 자식에서 핸들러로 잡아 `_exit(100+sig)` |
| `munmap` 크기를 매핑 때와 다르게 | 일부만 풀리거나 남의 매핑을 풀 수 있다 | 매핑 길이를 구조체에 같이 저장 (연습 4 `cap`) |

## 8. 면접 Q&A

<details>
<summary>Q1. MAP_SHARED 와 MAP_PRIVATE 의 차이를 한 문장으로? fork 하면?</summary>

SHARED 는 매핑한 모두가 **같은 페이지**(파일이면 페이지 캐시 페이지)를 쓰므로 쓰기가 파일·다른 매핑·fork 한 상대에 보이고, PRIVATE 는 **쓰는 순간 내 사본** (COW)이 생겨 아무에게도 안 보이며 파일에도 안 간다. fork 는 매핑 종류를 그대로 물려준다: SHARED 는 계속 공유, PRIVATE 는 fork 후 각자의 변경이 각자에게만 (POSIX `fork`). 이 레슨의 표 2 개가 이를 실측으로 보여 준다. 꼬리: "PRIVATE 매핑에서 다른 프로세스의 파일 변경이 보이나?" → POSIX 미지정. macOS 에서는 mmap 시점 스냅숏(안 보임)을 관찰했다.

</details>

<details>
<summary>Q2. 읽기 전용으로 연 파일에 PROT_WRITE 매핑이 가능한가요?</summary>

`MAP_PRIVATE` 면 가능하다 — 쓰기가 파일로 가지 않으니 파일 쓰기 권한이 필요 없다 (실제로 성공, 파일은 그대로 'C'). `MAP_SHARED` 면 `EACCES`. POSIX 의 EACCES 조건이 정확히 이 구분이다. 덧붙여 fd 가 **읽기** 권한이 없으면 prot 와 상관없이 EACCES (매핑은 항상 읽을 수 있어야 한다) — 그래서 mmap 복사의 dst 를 `O_WRONLY` 로 열면 실패한다. 실행 파일·라이브러리의 데이터 세그먼트가 PRIVATE+WRITE 로 매핑되는 이유이기도 하다.

</details>

<details>
<summary>Q3. SIGBUS 와 SIGSEGV 는 각각 언제 나나요?</summary>

SIGSEGV: 그 주소에 **매핑이 없거나**(munmap 뒤, NULL) 보호 위반. SIGBUS: 매핑은 있는데 **뒤를 받칠 객체가 없음** — 파일 끝 다음의 온전한 페이지, 매핑 중 파일이 잘림, 저장장치 I/O 오류, (일부 아키텍처) 정렬 오류. 단, 플랫폼마다 다르다: 이 맥에서는 읽기 전용 페이지 쓰기도 SIGBUS 였고 si_code 는 모두 1(`BUS_ADRALN`)이었다. 리눅스 man 은 읽기 전용 쓰기를 SIGSEGV 로 적는다. 꼬리: "EOF 가 있는 그 페이지의 꼬리는?" → 0 으로 읽히고 써도 되지만 파일로는 안 간다 (POSIX).

</details>

<details>
<summary>Q4. mmap 이 read 보다 빠른가요?</summary>

경우에 따라. 이 맥에서 256 MiB 순차 복사: read/write 1 MiB 버퍼 약 4000 MB/s, mmap+memcpy 약 2600 MB/s, read/write 4 KiB 약 400 MB/s. mmap 은 사용자 버퍼 복사를 없애지만 **페이지마다 폴트**(여기선 16 KiB 당 1 번, src+dst 32768 번)와 TLB 비용, 매핑 생성·해제(TLB shootdown) 비용이 든다. read 는 큰 버퍼면 시스템 콜이 적고 커널의 read-ahead 가 잘 맞는다. mmap 이 이기는 곳: 무작위·반복 접근, 여러 프로세스 공유, 큰 파일의 일부만 보기, 장치 메모리. 꼬리: "폴트를 줄이려면?" → 큰 페이지, `madvise`, 리눅스 `MAP_POPULATE` (macOS 헤더엔 없음) — 단 이 측정에서 macOS `madvise(WILLNEED)` 는 일관된 이득이 없었다.

</details>

<details>
<summary>Q5. msync 는 무엇을 보장하나요? 안 부르면 데이터가 사라지나요?</summary>

SHARED 매핑의 변경은 msync 없이도 페이지 캐시에 있고 다른 프로세스·`read()` 에 즉시 보이며, 프로세스가 죽어도 남는다 (OS 가 나중에 writeback). msync(`MS_SYNC`) 는 그 범위의 더러운 페이지를 **저장장치로 쓰고 끝날 때까지 기다린다** — OS 크래시·전원 차단 대비. `MS_ASYNC` 는 시작만 요청 (이 맥에서는 64 MiB 에 6–7 ms 를 쓰고 돌아왔다). macOS 에서 드라이브 캐시까지 비우려면 `fcntl(F_FULLFSYNC)` (OS-B13). 규칙: 주소 페이지 정렬, `MS_SYNC|MS_ASYNC` 동시 금지(EINVAL), 매핑 안 된 페이지 포함 시 ENOMEM — 전부 실측 확인.

</details>

<details>
<summary>Q6. close(fd) 하면 매핑은? munmap 하면 파일은?</summary>

`close` 해도 매핑은 유효하다 — POSIX: mmap 이 파일에 별도 참조를 걸고 close 는 그걸 없애지 않는다. 그래서 "mmap 후 즉시 close" 가 정석이고 fd 고갈도 피한다. `munmap` 은 SHARED 의 변경을 버리지 않는다 (표 (d): munmap 뒤 파일에 'W'). PRIVATE 사본은 사라진다. 프로세스 종료 = 모든 매핑의 munmap.

</details>

<details>
<summary>Q7. 두 프로세스가 공유 매핑으로 통신할 때 주의할 점은?</summary>

① **동기화는 따로**: 공유 메모리는 그냥 메모리다. 카운터는 `_Atomic` (lock-free 원자 연산은 주소 무관해야 한다는 C11 7.17.5 의 권고 덕에 프로세스 간에도 동작 — 연습 2), 복잡한 구조는 `PTHREAD_PROCESS_SHARED` mutex (OS-M17). ② **포인터를 넣지 말 것**: 매핑 주소가 프로세스마다 다를 수 있다 → 오프셋으로 저장. ③ 상대가 죽었을 때 (robust mutex, 하트비트 — OS-A12). ④ 크기와 버전을 헤더에 (매직·레이아웃). 펌웨어 쪽 대응: 듀얼 코어 MCU 의 공유 SRAM 메일박스(+하드웨어 세마포어), 캐시가 있으면 clean/invalidate (ARCH-M12).

</details>

<details>
<summary>Q8. malloc 은 mmap 과 어떤 관계인가요?</summary>

큰 할당은 allocator 가 익명 PRIVATE `mmap` 으로 직접 받고 free 때 `munmap` 한다 (glibc 는 기본 128 KiB 임계값 `M_MMAP_THRESHOLD`; macOS libmalloc 도 큰 블록은 별도 VM 영역). 작은 할당은 이미 받은 영역을 쪼갠다 (OS-M04, OS-M05, OSTEP 14.5·17장). 익명 매핑이 0 으로 채워져 나오는 것(테스트로 확인)은 다른 프로세스의 데이터가 새지 않게 하는 보안 요구다 — 그래서 `calloc` 의 큰 블록은 따로 0 을 쓸 필요가 없다.

</details>

## 9. 추가 연습

### 연습 1 — 펌웨어 이미지 CRC32 를 mmap 으로

`int crc32_mmap(const char *path, uint32_t *out)` — 업데이트 이미지 파일 전체의 CRC-32 (IEEE, 반사형 다항식 0xEDB88320). `"123456789"` → `0xCBF43926`, 빈 파일, 3PG+77 바이트 랜덤 파일은 read 버전과 같아야 한다.

<details>
<summary>답 보기</summary>

`open` → `fstat` → 크기 0 이면 mmap 없이 `crc32(빈)`=0 → `mmap(PROT_READ, MAP_PRIVATE)` → **바로 `close(fd)`** (매핑은 산다) → 포인터로 한 번에 계산 → `munmap`. 읽기만 하니 PRIVATE 든 SHARED 든 같다. 실제 출력: `ex1: crc32("123456789")=0xCBF43926 ok, 49229-byte image mmap=0x798DD1D6 read=0x798DD1D6`. 펌웨어 쪽: 부트로더는 플래시가 이미 주소 공간에 있으니(XIP) "mmap" 없이 바로 포인터로 CRC 를 계산한다 (BM-A04). 하드웨어 CRC 명령은 ARM-A06.

</details>

### 연습 2 — 프로세스 간 공유 카운터

자식 4 개가 각자 20000 번 `+1`. `MAP_SHARED|MAP_ANON` 위의 `_Atomic unsigned` 면 부모가 80000 을, `MAP_PRIVATE` 면 0 을 봐야 한다.

<details>
<summary>답 보기</summary>

`_Atomic unsigned *c = mmap(..., flags | MAP_ANON, -1, 0)` → `atomic_init(c, 0)` → fork 4 번, 자식은 `atomic_fetch_add_explicit(c, 1, memory_order_relaxed)` 반복 후 `_exit(0)` → 전부 `wait_child`. 실제 출력: `MAP_SHARED parent sees 80000, MAP_PRIVATE parent sees 0`. 이 맥의 `ATOMIC_INT_LOCK_FREE` 는 2 (항상 lock-free) — lock-free 원자 연산은 주소에 묶인 내부 락이 없어 서로 다른 가상 주소로 같은 물리 페이지를 보는 두 프로세스에서도 맞다. 카운터 값만 필요하니 relaxed 로 충분하다 (C-A03). PRIVATE 에서 0 인 이유: 각 자식의 증가는 자기 사본에.

</details>

### 연습 3 — 읽기 전용 설정 테이블 (mprotect)

부팅 시 설정 구조체를 채운 뒤 `mprotect(PROT_READ)` 로 잠근다. 잠긴 상태에서 쓰면 어떤 시그널인가? 다시 열면 쓸 수 있나? 정렬 안 된 주소로 `mprotect` 하면?

<details>
<summary>답 보기</summary>

익명 PRIVATE 1 페이지에 채우고 `mprotect(c, PG, PROT_READ)`. 자식 읽기 = 0, 자식 쓰기 = **10 (SIGBUS, macOS)** — `#ifdef __APPLE__` 로 기대값을 나누고 그 밖에서는 리눅스 man 대로 SIGSEGV. `mprotect(c+1, ...)` → `EINVAL`, `mprotect(c, PG, RW)` 후 쓰기 OK. 실제 출력: `ex3: locked config read=0 write=10 (SIGBUS=10 SIGSEGV=11)`. 펌웨어 대응: Cortex-M MPU 로 설정·벡터 테이블 영역을 읽기 전용으로 두고 잘못된 쓰기를 MemManage 폴트로 잡는 것 (BM-A08). 쓰기 감지로 dirty 페이지만 저장하는 응용은 OS-A06.

</details>

### 연습 4 — 커지는 매핑 로그

`mlog_open/append/close` — 파일을 SHARED 로 매핑해 두고 append 는 `memcpy` 만. 공간이 모자라면 용량을 두 배로 키운다. 닫을 때 파일 크기는 정확히 쓴 만큼. 다시 열면 이어 쓴다.

<details>
<summary>답 보기</summary>

키우기 순서: `ftruncate(fd, ncap)` **먼저** (아니면 새 영역이 EOF 너머 → SIGBUS) → `munmap(old)` → `mmap(ncap)`. macOS 에는 `mremap` 이 없다 (헤더 확인) — 리눅스라면 `mremap(MREMAP_MAYMOVE)` 한 번. 두 배씩이라 다시 매핑은 O(log n) 번 (실제: 3000 레코드에 1 번). `mlog_close` 는 `ftruncate(len)` 으로 꼬리의 0 을 자른다. 재오픈 시 `len = st_size`, `cap = 페이지 올림`. 실제 출력: `ex4: 5000 records, 43890 bytes, file size 43890, first cap 16384, remaps in first 3000 = 1`. 주의: 다시 매핑하면 **주소가 바뀔 수 있다** — 예전 포인터를 들고 있으면 안 된다. 크래시 시 꼬리가 0 으로 남을 수 있어 실무 로그는 레코드 길이·체크섬을 붙인다 (STOR-B03).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice    # 17/17 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_asan && ./pr_asan   # 17/17, 경고 0
```

## 10. 핵심 정리

- `mmap` = 파일(또는 0 페이지)을 주소 공간에 붙이기. 실패 값은 `MAP_FAILED`, `len 0`·정렬 안 된 offset 은 `EINVAL`, 페이지 크기는 `sysconf` (이 맥 16 KiB).
- **SHARED**: 모두가 같은 페이지 → 파일·다른 매핑·fork 상대에 즉시 보임, munmap 해도 남음. **PRIVATE**: 쓰면 내 사본(COW) → 아무에게도 안 보임. 매핑 뒤의 파일 변경이 PRIVATE 에 보이는지는 POSIX 미지정 (macOS: 스냅숏).
- 읽기 전용 fd: PRIVATE+WRITE 는 OK, SHARED+WRITE 는 EACCES. `close(fd)` 뒤에도 매핑은 유효.
- EOF 가 있는 페이지의 꼬리는 0 (써도 파일로 안 감), 그 다음 페이지는 **SIGBUS**, 매핑 중 파일이 줄어도 SIGBUS. munmap 한 곳은 **SIGSEGV**. macOS 는 읽기 전용 쓰기도 SIGBUS.
- msync 는 "보이게" 가 아니라 "저장장치로". 공유엔 원자 연산·락, 내구성엔 msync/fsync(+F_FULLFSYNC, OS-B13).
- 순차 복사 한 번: 큰 버퍼 read/write ≈ mmap+write > mmap+memcpy(페이지당 폴트) ≫ 4 KiB read/write. mmap 은 무작위·반복 접근, 공유, 장치 메모리에서 이긴다.
- 테스트에서 시그널은 자식 안에서 잡아 종료 코드로 — 하네스가 안 멈추고, macOS 크래시 리포트도 안 쌓인다.
- 펌웨어: `/dev/mem`·UIO·드라이버 mmap 은 임베디드 리눅스의 일상. MMU 없는 Cortex-M 은 mmap 대신 링커 스크립트 + MPU.

## 참고자료

- POSIX `mmap` — MAP_PRIVATE 이후 변경의 가시성 "unspecified", 부분 페이지 0 채우기·SIGBUS, close 후 참조 유지, EACCES/EINVAL 조건: <https://pubs.opengroup.org/onlinepubs/9699919799/functions/mmap.html>
- POSIX `msync`, `munmap`, `mprotect`, `fork` (매핑 상속), `ftruncate` (늘린 영역 0): <https://pubs.opengroup.org/onlinepubs/9699919799/functions/msync.html>, <https://pubs.opengroup.org/onlinepubs/9699919799/functions/munmap.html>, <https://pubs.opengroup.org/onlinepubs/9699919799/functions/fork.html>, <https://pubs.opengroup.org/onlinepubs/9699919799/functions/ftruncate.html>
- macOS man pages (이 맥에서 확인): `man 2 mmap` (MAP_ANON, EACCES, len 0), `man 2 msync` (MS_ASYNC 단독, ENOMEM), `man 2 munmap`, `man 2 fsync` (F_FULLFSYNC 안내). 헤더: `MacOSX26.4.sdk/usr/include/sys/mman.h` (`MAP_NOCACHE`, `MAP_RESILIENT_MEDIA`, mremap 없음), `sys/signal.h` (BUS_*/SEGV_* 값)
- Linux `mmap(2)` — MAP_PRIVATE unspecified 문구, Signals 절 (SIGSEGV: read-only 쓰기, SIGBUS: 파일 끝 너머): <https://man7.org/linux/man-pages/man2/mmap.2.html>
- OSTEP 14장 *Memory API* (14.5–14.6 mmap 으로 익명 메모리), 23.5 (copy-on-write, demand zeroing), 39.6 (`fsync`), 40.7 *Caching and Buffering* (통합 페이지 캐시): <https://pages.cs.wisc.edu/~remzi/OSTEP/> (로컬: `operating-system-three-easy-pieces/book-md/C14_*.md`, `C23_*.md`, `C39_*.md`, `C40_*.md`, Don 노트 `notes/2026-09-30_C14_memory_api.md`)
- C11 (N1570) 7.17.5 — lock-free 원자 연산은 address-free 여야 한다는 권고 (프로세스 간 공유 원자 변수): <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- 관련 레슨: OS-B01 (fork/wait 상태 해석), OS-B03 (시스템 콜 비용), OS-B04 (레이스), OS-B07 (시그널·async-signal-safe), OS-B08 (주소 공간), OS-B13 (원자적 저장·F_FULLFSYNC), OS-B14 (shm_open IPC), OS-M03 (COW fork 와 페이지 폴트), OS-M04/M05 (malloc), OS-M17 (프로세스 공유 mutex), OS-A06 (mprotect + 폴트 처리), OS-A12 (견고한 공유 메모리), ELX-M03 (mmap 드라이버), ELX-A06 (UIO/VFIO), PCIE-A01 (BAR 매핑), CAM-A01 (V4L2 mmap), LPERF-B08/B09 (page cache·minor/major fault), ARCH-M12 (DMA 캐시 일관성), BM-A04 (이미지 CRC), BM-A08 (MPU), C-A03 (atomics), STOR-B03 (레코드 체크섬)
