---
id: OS-B13
time: 60분
updated: 2026-11-11
---
# OS-B13 파일 API와 원자적 저장: write→fsync→rename

## 학습 목표

- **임시 파일 쓰기 → fsync(임시) → close → rename → fsync(디렉터리)** 순서로 설정 파일을 저장하는 `atomic_save()` 를 짜고, 각 단계가 무엇을 막는지 한 줄로 말할 수 있다.
- `fork` + "k 번째 파일 연산 직후 `SIGKILL`" 크래시 주입 하네스로 **모든 k 에서 대상 파일이 정확히 옛 것 또는 새 것** 임을 검증한다. 제자리 덮어쓰기가 빈 파일·반쪽 파일을 남기는 것도 같은 하네스로 확인한다.
- **프로세스 kill 은 전원 차단이 아니다** (페이지 캐시가 살아남는다) 를 실제 결과로 보이고, fsync 를 빠뜨린 버그는 "캐시 vs 디스크" 모델로만 잡힌다는 것을 설명한다. 진짜 전원 차단 시험 방법(dm-flakey, VM 강제 종료, 전원 릴레이)을 안다.
- macOS 의 `fsync` / `F_BARRIERFSYNC` / `F_FULLFSYNC` 차이를 man 페이지와 실측(이 맥 SSD 에서 18 µs vs 145 µs vs 3.2 ms)으로 정리하고, NVMe 의 volatile write cache · Flush · FUA · PLP 와 연결한다.

## 1. 문제

"설정 파일을 저장하다가 전원이 나가도 깨지지 않게 하려면?" — 임베디드 리눅스 장비(라우터, 드론, 차량 ECU, 산업용 게이트웨이)에서 설정 파일 손상은 "부팅은 되는데 아무것도 안 되는" 벽돌의 흔한 원인이고, 면접에서는 `fsync` 와 `rename` 의 의미를 정확히 아는지 보는 단골 질문이다. Don 이 SSD 펌웨어에서 받던 Flush·FUA 명령이 바로 이 순서의 맨 아래층이다.

저장 한 번은 데이터가 여러 층을 지나간다. 각 층은 사고 종류마다 살아남는지가 다르다:

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">데이터가 지나는 층과, 사고별로 살아남는가 (O 남음 / X 사라짐)</text>
  <rect x="20" y="40" width="320" height="42" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="32" y="66" fill="currentColor">① 앱 메모리 (buf, stdio 버퍼)</text>
  <rect x="20" y="110" width="320" height="42" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="32" y="128" fill="currentColor">② 커널 페이지 캐시: dirty 페이지,</text>
  <text x="32" y="145" fill="currentColor">   생성·rename 같은 디렉터리 변경</text>
  <rect x="20" y="180" width="320" height="42" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="32" y="206" fill="currentColor">③ SSD 휘발성 쓰기 캐시 (컨트롤러 DRAM/SRAM)</text>
  <rect x="20" y="250" width="320" height="42" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="32" y="276" fill="currentColor">④ NAND (영구)</text>
  <line x1="180" y1="82" x2="180" y2="108" stroke="currentColor" stroke-width="2"/>
  <line x1="180" y1="152" x2="180" y2="178" stroke="currentColor" stroke-width="2"/>
  <line x1="180" y1="222" x2="180" y2="248" stroke="currentColor" stroke-width="2"/>
  <text x="190" y="100" fill="currentColor" font-family="monospace">write()</text>
  <text x="190" y="170" fill="currentColor" font-family="monospace">fsync()  (macOS: 여기까지)</text>
  <text x="190" y="240" fill="currentColor" font-family="monospace">F_FULLFSYNC = NVMe Flush</text>
  <text x="400" y="34" fill="currentColor" font-weight="bold">프로세스 kill</text>
  <text x="510" y="34" fill="currentColor" font-weight="bold">커널 패닉</text>
  <text x="620" y="34" fill="currentColor" font-weight="bold">전원 차단</text>
  <text x="440" y="66" fill="currentColor">X</text><text x="540" y="66" fill="currentColor">X</text><text x="650" y="66" fill="currentColor">X</text>
  <text x="440" y="136" fill="currentColor">O</text><text x="540" y="136" fill="currentColor">X</text><text x="650" y="136" fill="currentColor">X</text>
  <text x="440" y="206" fill="currentColor">O</text><text x="540" y="206" fill="currentColor">O</text><text x="620" y="206" fill="currentColor">X (PLP 면 O)</text>
  <text x="440" y="276" fill="currentColor">O</text><text x="540" y="276" fill="currentColor">O</text><text x="650" y="276" fill="currentColor">O</text>
  <text x="20" y="318" fill="currentColor">이 레슨의 kill 하네스는 왼쪽 열만 시험한다. ②가 사라지는 경우는 Part B 의 모델(plfs)이 맡는다.</text>
</svg>
```

`rename()` 은 POSIX 가 원자성을 약속하는 몇 안 되는 파일 연산이다: 대상 이름은 rename 동안 내내 보이고, 옛 파일 또는 새 파일 중 하나를 가리킨다 (POSIX `rename`, macOS `man 2 rename`: "an instance of new will always exist, even if the system should crash"). 그래서 "다른 이름으로 완성본을 만든 다음 이름만 바꿔 끼우기" 가 기본 패턴이다 (OSTEP 39.7). 단, **rename 은 이름만 원자적으로 바꿀 뿐 내용이 디스크에 있다고 보장하지 않는다** — 그게 rename 전에 fsync 가 필요한 이유다 (OSTEP 39.6, 42장).

모든 파일 연산은 작은 HAL `fsops_t` 를 거친다. 진짜 구현 `realfs` 는 `openat/write/fsync/renameat` 를 부르고 연산마다 번호를 세다가 `kill_at` 번째 직후 스스로 `SIGKILL` 을 쏜다. 모델 구현 `plfs` 는 메모리 안에 "캐시" 와 "디스크" 를 따로 두고, 전원이 나가면 디스크에 남을 수 있는 모든 조합을 나열한다. **같은 `atomic_save` 코드가 둘 위에서 돈다** — 펌웨어에서 플래시 드라이버를 HAL 뒤에 두고 시뮬레이터로 power-cut 테스트하는 것(BM-A06)과 같은 구조다.

### Part A — `atomic_save()` 와 `write_all()`

- `int write_all(fsops_t *o, int h, const void *buf, size_t len)` — 짧은 쓰기(short write)는 이어서, `EINTR` 은 다시 시도, 0 바이트 진행은 `EIO`.
- `int atomic_save(fsops_t *o, const char *name, const void *buf, size_t len)` — `name.tmp` 에 ① open(O_TRUNC) ② write_all ③ sync ④ close ⑤ rename(tmp→name) ⑥ sync_dir. ①~⑤ 가 실패하면 임시 파일을 지우고 errno 를 보존해 -1. 대상은 옛 것 그대로.
- 테스트: 새로 만들기, 긴 옛 것 → 짧은 새 것, 7 바이트씩 짧은 쓰기(연산 수까지 확인), `EINTR` 3 번, 더 긴 찌꺼기 임시 파일, `fsync` 가 `EIO` (원본 유지 + 임시 정리), 없는 디렉터리(`ENOENT`), `F_FULLFSYNC` 모드. 그리고 **연산 순서 자체** 가 `"open write fsync close rename dirsync"` 인지.

### Part B — 크래시 주입: kill 스윕과 전원 차단 모델

- `run_t crash_run(saver, name, buf, len, kill_at, timeout_ms)` — `fork` 후 자식에서 `child_save()` (하네스 제공: realfs 를 열고 `kill_at` 을 걸고 저장 후 `_exit`). 부모는 `wait_child()` (워치독) 로 거둬 `RUN_KILLED / RUN_DONE / RUN_ERROR / RUN_TIMEOUT` 을 돌려준다.
- `tstate_t classify(name, old, new)` — 대상 파일을 읽어 `T_OLD / T_NEW / T_EMPTY / T_TORN / T_MISSING`.
- 하네스가 k = 1, 2, … 마다 옛 버전(2500 B)을 깔고 새 버전(3000 B, write 는 1024 B 씩)을 저장하다 죽인다. 대상: 내 `atomic_save` 와 깨진 변형 3개 — `save_inplace` (O_TRUNC 로 제자리), `save_rename_nosync` (fsync 없음), `save_no_dirsync` (디렉터리 fsync 없음).
- 같은 4개를 `plfs` 모델 위에서 다시 돌려, kill 로는 안 보이던 버그가 나오는지 본다.

### Part C — 펌웨어식 설정 레코드와 부팅 복구

- 레코드: `"CFG1"` magic · len=28 · payload(seq, gain, rate, name[16]) · CRC32 = 40 B, 리틀 엔디언 고정.
- `config_encode()` / `config_decode()` (길이·magic·len·CRC·name 의 NUL 검사) / `config_boot()` (남은 `config.bin.tmp` 삭제 → 읽기 → 없으면 `CFG_MISSING`, 검증 실패면 `CFG_CORRUPT`, 둘 다 기본값).
- 크래시 스윕: seq 7 → 8 저장을 k 번째 연산에서 죽이고 "부팅" 하면 항상 seq 7 또는 8 이 로드되어야 한다. 제자리 덮어쓰기는 CRC 가 손상을 잡아 기본값으로 떨어지는 것(=설정을 잃는 것)을 본다.

## 2. Problem (English)

> **Crash-safe configuration save.**
>
> Implement a function that replaces the contents of a file so that after a crash at *any* point the file contains either the complete old contents or the complete new contents — never a mix, never empty, never missing:
>
> ```c
> int write_all(fsops_t *o, int h, const void *buf, size_t len);   // handles short writes and EINTR
> int atomic_save(fsops_t *o, const char *name, const void *buf, size_t len);
> run_t crash_run(saver_fn save, const char *name, const void *buf, size_t len,
>                 int kill_at, int timeout_ms);                    // fork, kill child after op #kill_at, reap
> tstate_t classify(const char *name, const void *oldb, size_t oldn, const void *newb, size_t newn);
> size_t config_encode(const config_t *c, uint8_t *out, size_t cap);
> bool   config_decode(const uint8_t *in, size_t n, config_t *out);
> cfg_src_t config_boot(config_t *out, bool *removed_tmp);         // clean stale tmp, load or defaults
> ```
>
> Constraints: all file operations go through `fsops_t` (so the same code runs on the real file system and on a power-loss model); on failure the old file must be untouched and no temporary file left behind; every child process must be reaped, and a hung child must be killed by a watchdog.
>
> Example: old = 2500 bytes, new = 3000 bytes written 1024 bytes at a time. The save performs 8 operations (`open write write write fsync close rename dirsync`). Killing the process after operation k = 1..6 must leave the old file; k = 7..8 the new file.
>
> Follow-ups: Why fsync *before* rename? Why fsync the directory? Does killing the process prove crash safety? What does `fsync` guarantee on macOS vs Linux, and what is `F_FULLFSYNC`? What should you do when `fsync` returns `EIO`? How does an SSD honor a flush, and what is PLP?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

처음 실행 결과 (실제 출력 일부, 0.1 초에 끝나고 exit 1 — 멈추지 않는다):

```text
FAIL starter.c:590  atomic_save(&r.ops, AN, "v1\n", 3) == 0
...
Part B kill sweep: SIGKILL right after file op k = 1, 2, ... (old 2500 B -> new 3000 B, write <= 1024 B)
  atomic_save ops:   
  atomic_save        ?:ERR
  inplace            ?:ERR
...
FAIL starter.c:666  crash_run(saver_hang, AN, "x", 1, 0, 150) == RUN_TIMEOUT
...
FAIL starter.c:745  done && wrong == 0 && corrupt > 0
12/43 passed
```

통과하는 12개는 TODO 와 무관한 것(하네스 CRC 표준값, 남은 자식 없음, 모델에서 깨진 변형이 깨지는 쪽)과 스텁이 우연히 맞히는 것(거부해야 할 레코드에 `false`, 파일이 없을 때 `CFG_MISSING`)이다. 하네스 설계:

- **모든 파일은 `mkdtemp("/tmp/OS-B13_t.XXXXXX")` 안** 에 만들고 끝나면 지운다. 실행 위치(cwd)와 무관하다. 레슨 디렉터리 옆에 필요한 데이터 파일은 없다.
- **자식은 전부 거둔다.** `wait_child()` 는 0.25 ms 간격으로 `waitpid(WNOHANG)` 을 보다가 시간이 넘으면 `SIGKILL` + `waitpid`. 자식은 `alarm(5)` 도 건다(이중 안전장치). 마지막 체크가 `waitpid(-1, …, WNOHANG) == -1 && errno == ECHILD` — 남은 자식이 없다는 뜻.
- **체크는 매번 성립하는 것만.** 상태 분류, 연산 순서, 연산 개수. 시간은 재지 않는다.
- realfs 의 `max_io` / `eintr_left` / `fail_sync` / `full` 로 짧은 쓰기, EINTR, fsync 실패, F_FULLFSYNC 를 주입한다.

관찰용 파일:

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o db && ./db 200 5   # fsync 계열 지연 (약 20 초)
```

## 4. 힌트

<details>
<summary>힌트 1 — write 는 "전부" 를 약속하지 않는다</summary>

POSIX `write()` 는 요청보다 적게 쓰고 그 수를 돌려줄 수 있다(디스크가 거의 참, 신호, 파이프·소켓). 아무것도 쓰기 전에 신호를 받으면 -1/`EINTR`. 그래서 `while (len > 0)` 루프로 포인터와 남은 길이를 진행시킨다. 0 이 오면 진행이 없으니 무한 루프 대신 오류로.

</details>

<details>
<summary>힌트 2 — 이름이 바뀌는 순간 이전에 내용이 디스크에 있어야 한다</summary>

rename 이 디스크에 먼저 도착하고 내용이 아직 캐시에만 있으면, 전원 차단 뒤 "새 이름 → 빈 inode" 가 된다. 그러니 순서는 내용(write) → 내용 내구화(fsync) → 이름 교체(rename) → 이름 내구화(디렉터리 fsync). 실패 경로에서는 `errno` 를 먼저 저장하고 close/unlink 한 다음 되돌려 놓는다 (close/unlink 가 errno 를 덮어쓴다).

</details>

<details>
<summary>힌트 3 — crash_run 은 OS-B01 의 fork/wait 그대로</summary>

`fork()` 직전에 `fflush(stdout)`. 자식은 `child_save()` 만 부른다 (`_exit` 로 끝나므로 돌아오지 않는다). 부모는 `wait_child()` 로 상태를 받고 `WIFSIGNALED/WTERMSIG`, `WIFEXITED/WEXITSTATUS` 로 나눈다 (OS-B01 참고).

</details>

<details>
<summary>힌트 4 — 부팅 복구: 임시 파일은 무조건 버린다</summary>

`config.bin.tmp` 가 남아 있다는 건 rename 전에 죽었다는 뜻이다. 완성본인지 모르니 (fsync 전에 죽었을 수도) 그냥 지운다. 그 다음 `config.bin` 을 `CFG_REC + 1` 바이트 버퍼로 읽는다 — 1 바이트 더 읽혀야 "너무 크다" 를 알 수 있다. CRC 는 앞 36 바이트에 대해 계산하고 [36] 의 값과 비교.

</details>

## 5. 풀이

### Part A — 단계마다 무엇을 막는가

```c
int h = o->open_trunc(o, tmp);                      // ① 임시 파일 (찌꺼기가 있으면 O_TRUNC 로 비움)
if (write_all(o, h, buf, len) < 0 ||                // ② 내용 전부
    o->sync(o, h) < 0) { ...close, unlink(tmp)... } // ③ 내용을 디스크로 — rename 보다 먼저
if (o->close(o, h) < 0) { ...unlink(tmp)... }       // ④
if (o->rename(o, tmp, name) < 0) { ...unlink... }   // ⑤ 원자적 교체
return o->sync_dir(o);                              // ⑥ 이름 변경을 디스크로
```

| 단계 | 하는 일 | 빼면 | 누가 잡나 |
|---|---|---|---|
| ① 다른 이름으로 쓰기 | 옛 파일을 건드리지 않는다 | 제자리 O_TRUNC: 열자마자 0 바이트 → 쓰는 중에 죽으면 빈/반쪽 파일 | kill 스윕 (EMPTY, TORN) |
| ② `write_all` | 짧은 쓰기·EINTR 처리 | 1024 B 만 쓰고 "성공" → 잘린 새 버전이 정식으로 교체됨 | 짧은 쓰기 테스트 |
| ③ `fsync(tmp)` | 새 내용을 저장 장치로 | 전원 차단 시 rename 은 남고 내용은 없음 → 빈 파일 | **모델만** (kill 로는 안 보임) |
| ④ `close` 확인 | NFS 등은 close 에서 쓰기 오류를 보고 | 오류를 놓침 | — |
| ⑤ `rename` | 이름을 옛 inode → 새 inode 로 한 번에 | — (패턴의 핵심) | — |
| ⑥ `fsync(dir)` | rename 자체를 디스크로 | "저장 성공" 을 반환했는데 재부팅 후 옛 것 → 사용자가 확인받은 변경을 잃음 | **모델만** |

**짧은 쓰기 테스트의 연산 수:** 1000 B 를 7 B 씩 → write 143 번. `nops == 1 + 143 + 4` 로 루프가 정확히 진행했는지까지 본다.

**오류 경로:** `fail_sync=1` 이면 첫 sync 가 `EIO`. `atomic_save` 는 -1/`EIO` 를 돌려주고 대상은 `"old"` 그대로, `app.conf.tmp` 는 없다. 실패하면 **다시 fsync 하지 말고 버퍼로부터 저장 전체를 다시 하라** — Linux 에서 쓰기 반환 실패 뒤의 fsync 재시도가 성공을 돌려주면서 데이터는 잃는 문제(2018 PostgreSQL "fsyncgate")가 있었다. 우리 함수는 실패 시 임시 파일을 지우므로 자연스럽게 "처음부터 다시" 가 된다.

**rename 뒤 dirsync 가 실패하면?** 이름은 이미 바뀌었을 수 있다 (메모리상으로는 확실히 바뀜). 되돌릴 방법이 없으므로 -1 을 돌려주되 "대상이 새 것일 수 있음" 을 함수 주석에 적었다. 호출자는 "확인 안 됨" 으로 다루고 다시 저장하면 된다 (같은 내용 재저장은 무해하다 = 멱등).

### Part B — kill 스윕: 실제 출력

`crash_run` 의 정답은 OS-B01 의 fork/wait 그대로다:

```c
fflush(stdout);
pid_t pid = fork();
if (pid < 0) return RUN_OTHER;
if (pid == 0) child_save(save, name, buf, len, kill_at);       // _exit 로 끝남
int st = 0;
if (!wait_child(pid, timeout_ms, &st)) return RUN_TIMEOUT;
if (WIFSIGNALED(st)) return WTERMSIG(st) == SIGKILL ? RUN_KILLED : RUN_OTHER;
if (WIFEXITED(st)) return WEXITSTATUS(st) == 0 ? RUN_DONE : RUN_ERROR;
```

자식은 `exit` 가 아니라 `_exit` 로 끝난다. `exit` 는 부모에게서 복사된 stdio 버퍼를 한 번 더 flush 해서 출력이 두 번 찍힐 수 있다. 훅은 `rf_step()` 안에서 연산이 **성공한 직후** `kill(getpid(), SIGKILL)` — 잡을 수도 무시할 수도 없는 신호라 "그 지점에서 프로세스가 사라진다" 를 정확히 흉내 낸다.

이 맥(Apple M6, macOS 27.2, APFS)에서 solution 의 실제 출력:

```text
Part B kill sweep: SIGKILL right after file op k = 1, 2, ... (old 2500 B -> new 3000 B, write <= 1024 B)
  atomic_save ops:   open write write write fsync close rename dirsync
  atomic_save        OLD OLD OLD OLD OLD OLD NEW NEW done:NEW
  inplace            EMPTY TORN TORN NEW NEW NEW done:NEW
  rename_nosync      OLD OLD OLD OLD OLD NEW done:NEW
  no_dirsync         OLD OLD OLD OLD OLD OLD NEW done:NEW
```

| 변형 | 결과 | 해석 |
|---|---|---|
| `atomic_save` | k=1..6 OLD, k=7(rename) 부터 NEW | 경계가 정확히 rename. 반쪽·없음은 한 번도 없다 |
| `inplace` | k=1 EMPTY, k=2,3 TORN | `O_TRUNC` 순간 옛 내용이 사라진다. 1024 B, 2048 B 짜리 반쪽 파일 |
| `rename_nosync` | 전부 OLD/NEW | **fsync 가 없는데도 통과!** |
| `no_dirsync` | 전부 OLD/NEW | 마찬가지 |

깨진 두 변형이 통과하는 이유: `SIGKILL` 은 프로세스만 없앤다. `write()` 가 끝난 데이터는 이미 **커널 페이지 캐시** 에 있고, rename 도 커널의 디렉터리 상태를 이미 바꿨다. 부모가 파일을 읽으면 커널은 캐시에서 준다 — 디스크에 갔는지와 무관하다. 그래서 kill 스윕이 증명하는 것은 **"앱이 어느 지점에서 죽어도 원자적"** 까지이고, **"전원이 나가도 원자적·내구적"** 은 증명하지 못한다. (1. 문제 그림의 왼쪽 열.)

### Part B — 전원 차단 모델 `plfs`

모델의 규칙 (하네스에 있음):

- 파일 내용: `write`/`O_TRUNC` 는 캐시만 바꾼다. `sync(h)` 를 해야 그 파일의 내용이 디스크로.
- 이름: 생성·rename·unlink 는 메타데이터 로그에 순서대로 쌓인다. `sync_dir` 를 해야 디스크로. POSIX 는 파일 fsync 가 그 파일의 디렉터리 엔트리까지 내린다고 약속하지 않는다 — Linux `fsync(2)` 는 그래서 디렉터리도 fsync 하라고 명시한다. 모델은 이 비관적인 쪽을 택했다.
- 전원 차단 = 디스크에 남을 수 있는 모든 조합: 메타데이터 로그의 **앞부분 아무 길이** (저널링 FS 처럼 순서는 지킨다) × 더러운 파일마다 **{디스크 것, 캐시 것}**.
- 단순화: 파일 내용은 통째로 둘 중 하나 (일부 페이지만 내려가는 경우 없음) → 실제보다 관대하다. 그래도 아래 버그는 다 잡힌다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold">각 연산 직후 전원이 나가면 대상 이름이 될 수 있는 것 (plfs 모델, 연산 순서대로)</text>
  <text x="10" y="50" fill="currentColor">연산 →</text>
  <rect x="150" y="34" width="78" height="24" class="f-mut" stroke="currentColor"/><text x="172" y="51" fill="currentColor">open</text>
  <rect x="232" y="34" width="78" height="24" class="f-mut" stroke="currentColor"/><text x="252" y="51" fill="currentColor">write</text>
  <rect x="314" y="34" width="78" height="24" class="f-mut" stroke="currentColor"/><text x="334" y="51" fill="currentColor">fsync</text>
  <rect x="396" y="34" width="78" height="24" class="f-mut" stroke="currentColor"/><text x="418" y="51" fill="currentColor">close</text>
  <rect x="478" y="34" width="78" height="24" class="f-acc" stroke="currentColor"/><text x="494" y="51" fill="currentColor">rename</text>
  <rect x="560" y="34" width="78" height="24" class="f-mut" stroke="currentColor"/><text x="576" y="51" fill="currentColor">dirsync</text>
  <rect x="642" y="34" width="90" height="24" class="f-mut" stroke="currentColor"/><text x="652" y="51" fill="currentColor">반환 뒤</text>
  <text x="10" y="92" fill="currentColor">atomic_save</text>
  <text x="170" y="92" fill="currentColor">OLD</text><text x="252" y="92" fill="currentColor">OLD</text><text x="334" y="92" fill="currentColor">OLD</text><text x="416" y="92" fill="currentColor">OLD</text>
  <rect x="480" y="76" width="74" height="24" class="f-ok" stroke="currentColor"/><text x="486" y="92" fill="currentColor">OLD|NEW</text>
  <rect x="562" y="76" width="74" height="24" class="f-ok" stroke="currentColor"/><text x="584" y="92" fill="currentColor">NEW</text>
  <rect x="644" y="76" width="86" height="24" class="f-ok" stroke="currentColor"/><text x="670" y="92" fill="currentColor">NEW</text>
  <text x="10" y="138" fill="currentColor">rename_nosync</text>
  <text x="170" y="138" fill="currentColor">OLD</text><text x="252" y="138" fill="currentColor">OLD</text><text x="344" y="138" fill="currentColor">—</text><text x="416" y="138" fill="currentColor">OLD</text>
  <rect x="480" y="122" width="74" height="24" class="f-warn" stroke="currentColor"/><text x="484" y="138" fill="currentColor">+EMPTY</text>
  <text x="590" y="138" fill="currentColor">—</text>
  <rect x="644" y="122" width="86" height="24" class="f-warn" stroke="currentColor"/><text x="650" y="138" fill="currentColor">O|N|EMPTY</text>
  <text x="10" y="184" fill="currentColor">no_dirsync</text>
  <text x="170" y="184" fill="currentColor">OLD</text><text x="252" y="184" fill="currentColor">OLD</text><text x="334" y="184" fill="currentColor">OLD</text><text x="416" y="184" fill="currentColor">OLD</text>
  <rect x="480" y="168" width="74" height="24" class="f-ok" stroke="currentColor"/><text x="486" y="184" fill="currentColor">OLD|NEW</text>
  <text x="590" y="184" fill="currentColor">—</text>
  <rect x="644" y="168" width="86" height="24" class="f-warn" stroke="currentColor"/><text x="652" y="184" fill="currentColor">OLD|NEW</text>
  <text x="10" y="222" fill="currentColor">rename_nosync: 이름은 디스크에 갔는데 내용은 캐시에만 → 빈 파일. no_dirsync: 원자적이긴 하지만</text>
  <text x="10" y="240" fill="currentColor">"저장 성공" 을 돌려준 뒤에도 옛 것으로 돌아갈 수 있다 (내구성 위반). 둘 다 kill 스윕에선 안 보였다.</text>
</svg>
```

실제 출력 (`during` = 모든 크래시 지점 × 남을 수 있는 조합의 개수, `after success` = 저장 함수가 반환한 뒤 전원이 나간 경우):

```text
Part B power-loss model (plfs): every surviving state of 'app.conf'
  model ops: open write fsync close rename dirsync
  atomic_save        during: OLD=14 NEW=2  | after success: NEW=1
  inplace            during: OLD=2 NEW=3 EMPTY=1  | after success: NEW=1
  rename_nosync      during: OLD=16 NEW=1 EMPTY=1  | after success: OLD=4 NEW=1 EMPTY=1
  no_dirsync         during: OLD=14 NEW=1  | after success: OLD=2 NEW=1
```

- `atomic_save`: 어떤 지점에서도 OLD/NEW 뿐, 반환 뒤에는 NEW 뿐 — **원자적이면서 내구적**.
- `rename_nosync`: rename 이 로그에 들어간 상태에서 tmp inode 의 디스크 쪽 내용은 0 바이트 → `EMPTY`. 이것이 2009 년 ext4 delayed allocation 초기에 사용자들이 겪은 "재부팅하니 설정 파일이 0 바이트" 문제의 모양이다. ext4 는 이후 `auto_da_alloc` (기본 켜짐: rename 으로 기존 파일을 덮을 때 데이터 할당을 강제)으로 이 흔한 앱 버그를 완화했지만, 이는 ext4 의 휴리스틱일 뿐 POSIX 보장이 아니다 (커널 문서 ext4 `auto_da_alloc`).
- `no_dirsync`: 원자성은 지키지만 반환 뒤 OLD 가 남을 수 있다.

**진짜 전원 차단은 어떻게 시험하나:** ① Linux `dm-flakey` (지정 시간 동안 쓰기를 버리는 device-mapper 타깃) 또는 `dm-log-writes` (블록 쓰기를 기록해 두었다가 임의 지점까지 재생 — CrashMonkey 가 이 방식으로 FS 버그를 찾았다) ② VM 강제 종료 — QEMU 프로세스를 kill 하되 호스트 페이지 캐시가 끼지 않게 디스크를 `cache=none`/`directsync` 로 ③ 보드 전원 릴레이로 실제 전원을 끊고 수천 번 반복 (INFRA-M04). 앱 수준 도구로는 ALICE (OSDI'14) 가 시스템 콜 트레이스에서 "디스크에 남을 수 있는 상태" 를 나열한다 — plfs 는 그 아이디어의 장난감 버전이다.

### macOS: fsync vs F_BARRIERFSYNC vs F_FULLFSYNC

`man 2 fsync` (macOS) 요지: fsync 는 데이터를 호스트에서 **드라이브로** 보낼 뿐, 드라이브가 실제 매체에 언제·어떤 순서로 쓰는지는 보장하지 않는다. 전원이 나가면 일부 또는 전부를 잃거나 나중 쓰기만 남을 수 있고 "이론적인 경우가 아니다". 강한 보장이 필요하면 `F_FULLFSYNC`. `man 2 fcntl`:

- `F_FULLFSYNC` — fsync + 드라이브에 버퍼 전체를 영구 매체로 flush 요청. 큐 전체를 비우므로 같은 장치에서 전에 fsync 된 데이터도 이 호출이 돌아올 때 영구적. HFS, FAT, UDF, APFS 에 구현.
- `F_BARRIERFSYNC` — fsync + 드라이브에 배리어. 배리어 전에 fsync 된 I/O 가 배리어 뒤 I/O 보다 먼저 영구화됨을 보장하지만, 호출이 돌아왔을 때 무엇이 영구적인지는 가정하면 안 된다. "순서는 중요하지만 내구성은 아닐 때". HFS·APFS 에 구현, Apple SSD 는 하드웨어 지원을 보장한다고 적혀 있다. SDK 헤더에 있음: `MacOSX26.4.sdk/usr/include/sys/fcntl.h` 의 `#define F_BARRIERFSYNC 85` (`F_FULLFSYNC` 는 51).

이 맥에서 디렉터리 fd 에 `fsync`, `F_FULLFSYNC`, `F_BARRIERFSYNC` 모두 0 을 돌려줬다 (APFS). `demo_bench.c` 실측 (Apple M6, macOS 27.2, Apple clang 21.0.0, APPLE SSD AP2048Z, APFS, 4 KB, 200회 × 5 run, 워밍업 20회):

```text
4 KB, N=200 x RUNS=5, unit = microseconds (median of run medians | pooled p90 | pooled p99)
  pwrite only (no sync)                          1.9 |       2.7 |       3.4
  pwrite + fsync                                18.3 |      30.0 |      83.9
  pwrite + F_BARRIERFSYNC                      144.9 |     333.8 |    1956.5
  pwrite + F_FULLFSYNC                        3206.9 |    4493.3 |    7175.0
  atomic_save fsync / fsync(dir)               134.9 |     274.9 |    1271.0
  atomic_save BARRIER / FULLFSYNC(dir)        4761.4 |    6598.5 |   10003.4
  atomic_save FULLFSYNC / FULLFSYNC(dir)      8156.4 |   10444.8 |   13882.8
```

두 번째 실행: 2.9 / 21.8 / 147.5 / 3803.9 / 193.0 / 4974.8 / 8057.5 µs (중앙값) — 절대값은 흔들리지만 비율은 같다.

| 관찰 | 해석 |
|---|---|
| fsync 18 µs ≈ write 의 10배 | 커널 → 드라이브 전송까지. 드라이브 캐시 flush 가 없다는 man 페이지와 맞는다 |
| F_FULLFSYNC 3.2~3.8 ms ≈ fsync 의 ~180배 | 드라이브 캐시 전체를 비우기 (NVMe Flush 에 해당). 저장 장치 큐 전체가 대상이라 다른 앱의 I/O 에도 영향 |
| F_BARRIERFSYNC 145 µs | flush 보다 20배 이상 싸다. 순서만 보장 |
| atomic_save(fsync) 135~193 µs | 파일 생성 + rename + 디렉터리 fsync 의 메타데이터 비용이 fsync 1회보다 크다 |
| atomic_save 를 전부 FULLFSYNC 로: 8 ms | 파일·디렉터리 각각 flush = 2회 |

macOS 에서 "전원 차단에도 안전" 하려면 **파일은 `F_BARRIERFSYNC` (rename 보다 먼저 디스크에 가는 순서 보장), 마지막 디렉터리는 `F_FULLFSYNC` (반환 시점 내구성)** 가 man 페이지의 두 정의를 조합한 최소 비용 조합이다 (4.8 ms vs 8.2 ms). man 페이지 해석에 기댄 것이니, 진짜 제품이면 Part B 의 실제 전원 차단 시험으로 확인할 것. SQLite 도 macOS 에서 `PRAGMA fullfsync` 옵션으로 F_FULLFSYNC 사용 여부를 고르게 한다 (기본 꺼짐).

### Part C — 설정 레코드와 부팅

```c
put32(out + 0, CFG_MAGIC);  put32(out + 4, CFG_PAYLOAD);
put32(out + 8, c->seq);     put32(out + 12, (uint32_t)c->gain_mdb);
put32(out + 16, c->rate_hz); memcpy(out + 20, c->name, 16);
put32(out + 36, crc32_ieee(out, 36));
```

`struct` 를 그대로 `write(&cfg, sizeof cfg)` 하지 않고 바이트 단위로 직렬화하는 이유: 패딩·엔디언·컴파일러가 바뀌어도 같은 파일을 읽어야 하기 때문이다 (펌웨어 업데이트 후 옛 설정 읽기, PLAT-M08). `int32_t` → `uint32_t` 변환은 모듈로 2³² 로 정의되고, 되돌릴 때 `(int32_t)` 은 구현 정의지만 2의 보수 대상(clang/gcc)에서는 원래 값이다.

검증 순서: 길이가 정확히 40 → magic → len 필드 → CRC → name 에 NUL. CRC 가 맞아도 magic/len 이 틀리면 거부한다 (다른 형식의 파일이 우연히 맞는 경우, 그리고 len 을 믿고 버퍼를 읽는 코드의 오버런 방지).

실제 출력:

```text
Part C crc32("123456789") = 0xCBF43926
Part C boot after crash at op k (save seq 7 -> 8):
  atomic_save  seq7 seq7 seq7 seq7 seq7 seq7 seq8 seq8 seq8  (tmp removed at boot: 6)
  inplace      CORRUPT CORRUPT CORRUPT seq8 seq8 seq8 seq8  (tmp removed at boot: 0)
```

- `atomic_save` (40 B 를 16 B 씩 = write 3번, 총 8 연산): 언제 죽어도 seq 7 또는 8. k=1..6 에서 남은 임시 파일 6개를 부팅 때 지웠다.
- `inplace`: 처음 세 지점에서 CRC 가 손상을 잡고 기본값으로 떨어졌다. **검출은 했지만 설정은 잃었다** — CRC 는 "깨졌는지" 를 알려줄 뿐 옛 것을 되살리지 못한다. 옛 것을 지키는 건 저장 방식(rename 또는 A/B 슬롯)의 몫이다.

### Part C — 펌웨어로 옮기면

| 이 레슨 | 임베디드 리눅스 | MCU / raw 플래시 |
|---|---|---|
| tmp + rename | 그대로 (ext4, UBIFS, F2FS 위). 설정은 `/etc` 가 아닌 rw 파티션 | rename 이 없다 → **A/B 두 슬롯 + seq + CRC** (연습 2), BM-A06, PLAT-M07 |
| fsync | eMMC/UFS 도 휘발성 캐시가 있으면 커널이 cache flush 명령을 보낸다 | 플래시 program 이 끝났는지 상태 레지스터 폴링 (BM-A02) |
| 부팅 시 tmp 정리 | init 스크립트 / systemd 유닛 (ELX-B13) | 부트로더가 슬롯 유효성 검사 |
| CRC | 같음 | 같음 + 페이지별 ECC는 플래시 컨트롤러 몫 |
| 파일 시스템이 대신 | — | LittleFS: 파일 갱신이 copy-on-write + 메타데이터 쌍으로 원자적 (STOR-A03) |

**SSD 펌웨어 쪽에서 보면 (Don 의 영역):** 호스트의 fsync/F_FULLFSYNC 는 결국 NVMe 의 두 도구로 내려온다. **Flush 명령** — 휘발성 쓰기 캐시(Identify Controller 의 VWC 필드가 "있음")에 있는 데이터를 비휘발 매체로 커밋하고 완료를 보고. **FUA(Force Unit Access) 비트** — 그 Write 하나만 매체에 닿은 뒤 완료. 펌웨어 입장에서 Flush 를 받으면 사용자 데이터뿐 아니라 그 데이터를 다시 찾는 데 필요한 **매핑(L2P) 정보까지** 전원 차단 후 복구 가능한 상태여야 한다 (MEM-M14: 체크포인트 + OOB 스캔). **PLP**(power-loss protection, 커패시터) 가 있는 엔터프라이즈 SSD 는 전원이 끊겨도 캐시를 내려쓸 수 있어 캐시를 사실상 비휘발로 다룰 수 있고, 그래서 Flush 가 싸다. 소비자 SSD 의 Flush 는 비싸다 — 위의 3 ms 가 그 비용이다. Linux 쪽은 ext4 가 저널 커밋 때 PREFLUSH/FUA 를 블록 계층에 요청하는 식으로 fsync 가 장치 캐시까지 내려간다는 점이 macOS 와 다르다 (계층별 실측 표는 STOR-B01).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
```

```text
Part A ops: open write fsync close rename dirsync
Part A ops (full): open write fullfsync close rename dirsync
Part B kill sweep: SIGKILL right after file op k = 1, 2, ... (old 2500 B -> new 3000 B, write <= 1024 B)
  atomic_save ops:   open write write write fsync close rename dirsync
  atomic_save        OLD OLD OLD OLD OLD OLD NEW NEW done:NEW
  inplace            EMPTY TORN TORN NEW NEW NEW done:NEW
  rename_nosync      OLD OLD OLD OLD OLD NEW done:NEW
  no_dirsync         OLD OLD OLD OLD OLD OLD NEW done:NEW
Part B power-loss model (plfs): every surviving state of 'app.conf'
  ... (5. 풀이에 실은 것과 같음)
Part C boot after crash at op k (save seq 7 -> 8):
  atomic_save  seq7 seq7 seq7 seq7 seq7 seq7 seq8 seq8 seq8  (tmp removed at boot: 6)
  inplace      CORRUPT CORRUPT CORRUPT seq8 seq8 seq8 seq8  (tmp removed at boot: 0)
43/43 passed
```

7번 반복(레슨 디렉터리와 `/` 에서) 모두 `43/43 passed`, 한 번에 약 0.5 초. 끝난 뒤 `/tmp/OS-B13_t.*` 는 남지 않는다. 새니타이저 (경고 0, exit 0):

```bash
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o so_asan && ./so_asan
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `fopen(path, "w")` 로 제자리 저장 | `"w"` = O_TRUNC. 여는 순간 0 바이트 (kill 스윕 k=1 EMPTY) | 임시 이름에 쓰고 rename |
| rename 전에 fsync 없음 | 이름이 내용보다 먼저 디스크에 → 빈 파일 (모델의 EMPTY). kill 테스트로는 안 보인다 | `fsync(tmp)` 후 rename |
| 디렉터리 fsync 없음 | 반환 뒤 전원 차단 시 옛 것 (모델의 after OLD). Linux `fsync(2)` 가 명시적으로 요구 | `fd = open(dir, O_DIRECTORY)` 후 `fsync(fd)` |
| `write()` 반환값 무시 | 짧은 쓰기면 잘린 파일이 "성공" 으로 교체된다 | `write_all` 루프 |
| fsync 가 EIO 면 fsync 만 다시 | 실패한 페이지가 clean 으로 표시되어 재시도는 성공을 돌려줄 수 있다 (Linux, 2018) | 임시 파일 버리고 저장 전체를 다시, 또는 오류를 위로 |
| 실패 경로에서 close 후 `return -1` | close/unlink 가 errno 를 덮어써 호출자가 원인을 모름 | `int e = errno; …; errno = e;` |
| 임시 파일을 `/tmp` 에, 대상은 `/data` 에 | 다른 FS 사이 rename 은 `EXDEV` (원자적 이동 불가) | 임시 파일은 **대상과 같은 디렉터리** |
| 여러 프로세스가 고정 이름 `x.tmp` 공유 | 서로의 임시 파일을 O_TRUNC 로 덮는다 | `x.<pid>.tmp` + O_EXCL (연습 1), 필요하면 락 |
| 자식에서 `exit()` | 부모의 stdio 버퍼가 두 번 flush, atexit 핸들러 실행 | `_exit()` + fork 전 `fflush(stdout)` |
| kill 테스트 통과 = 전원 안전이라고 결론 | 페이지 캐시가 살아남는다 | 모델 / dm-flakey / VM 강제 종료 / 전원 릴레이 |
| macOS 에서 fsync 로 충분하다고 가정 | 드라이브 캐시 flush 를 안 한다 (`man 2 fsync`) | F_BARRIERFSYNC(파일) + F_FULLFSYNC(디렉터리) |
| 원자적 저장 후 권한이 0644 로 바뀜 | 새 inode, 새 권한 (umask 적용) | 옛 파일 `fstat` → 임시에 `fchmod` (연습 4) |

## 8. 면접 Q&A

<details>
<summary>Q1. 원자적으로 파일을 바꾸는 표준 방법은? 왜 그 순서인가요?</summary>

같은 디렉터리의 임시 파일에 전부 쓰고 → `fsync(tmp)` → close → `rename(tmp, target)` → `fsync(dir)`. rename 은 POSIX 가 "대상 이름은 항상 옛 것 또는 새 것을 가리킨다" 고 약속하는 원자적 연산이라 교체 지점이 하나가 된다. fsync 가 rename 앞에 있어야 하는 이유: rename(메타데이터)이 내용보다 먼저 디스크에 도착하면 전원 차단 뒤 "새 이름 → 빈/불완전한 inode" 가 된다. 마지막 디렉터리 fsync 는 rename 자체를 내구화해서 "성공" 을 돌려준 뒤 옛 것으로 돌아가지 않게 한다. 꼬리: "fsync(tmp) 대신 fdatasync 는?" → 새로 만든 파일은 크기(메타데이터)도 바뀌었으니 fdatasync 도 크기는 내린다(POSIX: 데이터를 읽는 데 필요한 메타데이터 포함). 이득이 작다. 그리고 이 맥 SDK(MacOSX26.4)에는 `sys/syscall.h` 에 `SYS_fdatasync 187` 번호만 있고 `unistd.h` 에 원형도, `man 2 fdatasync` 도 없다 — macOS 에서는 fsync / F_FULLFSYNC 로 쓴다.

</details>

<details>
<summary>Q2. 프로세스를 kill 하는 테스트로 크래시 안전을 증명할 수 있나요?</summary>

반만. kill 은 "앱이 아무 지점에서 죽어도 원자적" 을 증명한다 — 제자리 덮어쓰기의 빈/반쪽 파일은 잡힌다. 하지만 write 와 rename 은 이미 커널 페이지 캐시에 반영되어 있어 다음 읽기에 보이므로 fsync 누락은 절대 안 보인다. 이 레슨에서 fsync 가 하나도 없는 변형이 kill 스윕을 그대로 통과했다. 전원 차단·커널 패닉은 페이지 캐시를 날린다. 시험법: ① 모델 (ALICE 처럼 시스템 콜 트레이스에서 남을 수 있는 상태 나열 — plfs) ② 블록 계층 주입 (`dm-flakey`, `dm-log-writes` 재생 — CrashMonkey) ③ VM 강제 종료 (호스트 캐시 끄고) ④ 실제 전원 릴레이 수천 회. 장치 캐시까지 보려면 ④ 만이 진짜다. 꼬리: "SIGKILL 대신 `abort()`?" → 같은 범주(프로세스 사망). 커널 패닉 주입은 Linux `echo c > /proc/sysrq-trigger` 로 할 수 있고 장치 캐시는 살아남는다.

</details>

<details>
<summary>Q3. macOS 에서 fsync 와 F_FULLFSYNC 는 무엇이 다르고, 얼마나 비싼가요?</summary>

macOS `man 2 fsync`: fsync 는 데이터를 드라이브까지 보낼 뿐 드라이브가 매체에 언제·어떤 순서로 쓸지는 모른다. 전원 차단 시 일부/전부 손실이나 순서 뒤바뀜이 "실제로 쉽게 재현된다". `F_FULLFSYNC` 는 여기에 드라이브 flush 를 더한다. 이 맥(APFS, Apple SSD) 4 KB 실측: fsync 18~22 µs, `F_BARRIERFSYNC` 145 µs, `F_FULLFSYNC` 3.2~3.8 ms — 약 180배. 그래서 매 쓰기마다 FULLFSYNC 는 못 쓰고, 순서만 필요하면 BARRIER, 사용자에게 "저장됨" 을 보장하는 커밋 지점에서만 FULLFSYNC. Linux 는 대부분의 FS 가 fsync 에서 장치 캐시 flush/FUA 까지 하므로 macOS 의 fsync 보다 Linux 의 fsync 가 더 강하다 (Linux 측 수치는 STOR-B01). 꼬리: "F_FULLFSYNC 의 부작용?" → 장치 큐 전체를 비우므로 다른 프로세스의 I/O 도 같이 기다린다.

</details>

<details>
<summary>Q4. fsync 가 EIO 를 돌려주면 어떻게 하나요?</summary>

다시 fsync 하지 않는다. 2018 년 PostgreSQL 사례(LWN "PostgreSQL's fsync() surprise"): Linux 에서 writeback 이 실패하면 페이지가 clean 으로 표시되거나 버려질 수 있고, 오류는 한 번 보고된 뒤 재시도 fsync 는 성공을 돌려줄 수 있다 — 데이터는 없는데. 올바른 대응: 그 저장을 실패로 취급하고 메모리 버퍼로부터 저장 전체를 다시 하거나(이 레슨: 임시 파일을 지우고 -1, 호출자가 재시도), DB 처럼 로그로부터 복구할 수 없으면 프로세스를 멈추고 재시작(PostgreSQL 은 이후 PANIC 하도록 바뀌었다). 원본 파일은 건드리지 않았으므로 옛 것은 안전하다 — 원자적 저장의 또 하나의 장점. 꼬리: "close 오류는?" → NFS 처럼 close 에서 쓰기 오류를 보고하는 FS 가 있어 close 반환값도 본다.

</details>

<details>
<summary>Q5. 디렉터리 fsync 는 왜 필요하고, 어떻게 하나요?</summary>

파일의 이름(디렉터리 엔트리)은 파일 내용과 다른 객체다. 파일 fsync 가 그 파일을 가리키는 디렉터리 엔트리까지 내린다는 보장이 POSIX 에 없고, Linux `fsync(2)` 는 "디렉터리 엔트리까지 디스크에 있으려면 디렉터리 fd 에도 fsync 하라" 고 명시한다. rename 은 디렉터리만 바꾸는 연산이므로 그 뒤에 디렉터리 fsync 가 없으면 반환 뒤 전원 차단에서 옛 이름 상태로 돌아갈 수 있다(모델의 `no_dirsync`: after success OLD). 방법: `int d = open(dir, O_RDONLY | O_DIRECTORY); fsync(d);` — 이 맥 APFS 에서 fsync·F_FULLFSYNC 모두 0 을 반환하는 것을 확인했다. 새 파일을 처음 만들 때도 마찬가지 (연습 2 의 슬롯 첫 생성). 꼬리: "rename 원자성이 FS 가 크래시해도 유지되나?" → macOS man 페이지가 "시스템이 중간에 크래시해도 new 의 인스턴스가 항상 존재" 를 약속하고, 저널링 FS 는 rename 을 한 트랜잭션으로 처리한다.

</details>

<details>
<summary>Q6. SSD 펌웨어는 Flush 와 FUA 를 받으면 무엇을 해야 하나요? PLP 는?</summary>

Flush: 그 시점까지 완료 보고한 쓰기를 전부 비휘발 매체에 커밋한 뒤 완료. 사용자 데이터만이 아니라 **그 데이터를 다시 찾을 매핑(L2P)과 메타데이터** 도 전원 차단 후 복구 가능해야 한다 — 매핑을 매번 다 쓰는 대신 체크포인트 + 이후 페이지 OOB 의 (LPA, seq) 스캔으로 재구성 (MEM-M14). FUA: 해당 Write 하나를 매체에 닿은 뒤 완료 (캐시 우회 또는 그 데이터만 flush). 순서: NVMe 명령은 기본적으로 순서가 없으므로 호스트가 "A 완료 확인 후 B 제출 + Flush" 로 순서를 만든다 — 그래서 OS 의 fsync 가 완료를 기다리는 것. PLP: 커패시터로 전원 차단 후 수 ms 를 버텨 캐시를 NAND 로 내린다. 그러면 캐시를 비휘발로 취급할 수 있어 Flush 를 즉시 완료할 수 있고(VWC 를 "없음" 으로 보고하는 제품도 있다), 그 차이가 이 레슨의 fsync↔F_FULLFSYNC 3 ms 차이다. 꼬리: "Flush 를 무시하는 드라이브는?" → macOS fcntl man 페이지도 "일부 FireWire 드라이브는 flush 요청을 무시한다" 고 적는다. 검증은 전원 차단 시험으로만.

</details>

<details>
<summary>Q7. rename 기반 원자적 저장이 깨지는/바꾸는 것은 무엇인가요?</summary>

① 새 inode 라서 하드 링크가 끊긴다 — 다른 이름은 옛 내용을 계속 본다(연습 4 에서 확인). ② 권한·소유자·xattr·ACL 이 새 파일 기본값이 된다 → `fstat` 후 `fchmod`/`fchown` 으로 옮긴다. ③ 다른 FS 사이 rename 은 `EXDEV` → 임시 파일은 같은 디렉터리. ④ 심볼릭 링크를 대상으로 하면 링크 자체가 일반 파일로 바뀐다 (rename 은 마지막 요소가 링크면 링크를 옮긴다, `man 2 rename`). ⑤ 동시 저장자 사이의 "마지막 쓰기가 이김" 은 막지 않는다 — 락이 필요. ⑥ 읽는 쪽에게는 장점: 열려 있던 fd 는 옛 inode 를 계속 보고, 새로 여는 쪽은 완성본만 본다 (연습 1: 1000+ 회 읽기 중 반쪽 0). macOS 는 `renamex_np(RENAME_SWAP)` 로 옛 것을 백업 이름으로 받는 원자적 교환, `RENAME_EXCL` 로 "없을 때만" 을 제공한다 (연습 3).

</details>

## 9. 추가 연습

### 연습 1 — 동시 저장·동시 읽기 (프로세스별 임시 이름)

`atomic_save_unique()` — 임시 이름을 `name.<pid>.tmp` 로, `O_EXCL` 로 연다. 쓰는 자식 2개가 A(2000 B)/B(3000 B) 를 150번씩 저장하는 동안 부모는 계속 읽는다. 읽는 쪽은 항상 완전한 A 또는 B 만 봐야 하고, 끝나면 임시 파일이 하나도 없어야 한다. 제자리 덮어쓰기와 비교하라.

<details>
<summary>답 보기</summary>

고정 이름 `x.tmp` 를 두 프로세스가 같이 쓰면 서로의 임시 파일을 O_TRUNC 로 덮어 반쪽이 rename 될 수 있다. pid 를 넣고 `O_EXCL` 로 만들면 남의 것을 덮지 않는다 (`EEXIST` 면 같은 pid 가 전에 죽으며 남긴 내 것이므로 지우고 다시). 실제 출력 (5회 중 하나):

```text
ex1 atomic : reads=1263 ok=1263 torn=0 missing=0
ex1 inplace: reads=470 ok=86 torn=384 missing=0   (not checked: timing-dependent)
```

5회 모두 atomic 은 torn=0, missing=0. 제자리 쪽은 읽기의 약 80% 가 반쪽이었다 (512 B 씩 쓰므로) — 타이밍 의존이라 체크하지 않고 출력만. 읽는 쪽이 "없음" 을 한 번도 안 보는 것이 POSIX rename 의 "new 링크는 rename 동안 내내 보인다" 이다. 마지막 쓰기 승자 문제는 남는다 → 필요하면 `flock`.

</details>

### 연습 2 — rename 없는 A/B 두 슬롯 (플래시식)

raw 플래시나 rename 이 없는 저장소를 흉내 낸다: `slot.a`, `slot.b` 에 `seq | value | crc32` (12 B). 저장은 "현재가 아닌" 슬롯(무효거나 seq 가 작은 쪽)을 **제자리** 에 덮어쓰고 fsync. 로드는 CRC 가 맞는 슬롯 중 seq 가 큰 것. 모든 연산 직후 SIGKILL 스윕으로 항상 옛 seq 또는 새 seq 가 로드됨을 보여라.

<details>
<summary>답 보기</summary>

제자리 덮어쓰기인데도 안전한 이유: 덮어쓰는 슬롯은 **현재 슬롯이 아니다**. 그 슬롯이 반쪽이 되어도 CRC 가 틀려 무시되고, 현재 슬롯이 그대로 쓰인다. 실제 출력:

```text
ex2 A/B sweep (seq2 -> seq3): seq2 seq2 seq2 seq3 seq3 seq3 seq3
```

k=1(open, O_TRUNC) ~ k=3(write 2번째) 은 slot.a 가 비거나 반쪽 → seq2. k=4(마지막 write) 부터 seq3 — kill 은 페이지 캐시를 남기므로 fsync 전이어도 보인다(전원 차단이면 fsync 전까지 seq2 일 수 있다). 펌웨어에서는 슬롯 = 플래시 섹터, "제자리" = erase 후 program, CRC 대신/추가로 "유효" 마커를 마지막에 program 한다 (BM-A02, BM-A06, PLAT-M07, DS-A10). seq 가 32비트 넘침(wrap)이 걱정되면 직렬 번호 산술(serial number arithmetic)로 비교.

</details>

### 연습 3 — macOS `renamex_np`: RENAME_EXCL 과 RENAME_SWAP

`RENAME_EXCL` 로 "대상이 없을 때만" 게시하고 (있으면 `EEXIST`), `RENAME_SWAP` 으로 새 것을 넣으면서 옛 것을 임시 이름으로 돌려받아라.

<details>
<summary>답 보기</summary>

`man 2 rename` (macOS): `RENAME_SWAP` 은 지원하는 FS(`VOL_CAP_INT_RENAME_SWAP`)에서 두 이름을 원자적으로 맞바꾸고, `RENAME_EXCL` 은 대상이 있으면 `EEXIST`. 둘을 같이 주면 `EINVAL`. 이 맥 APFS `/tmp` 에서 확인:

```text
ex3 renamex_np: RENAME_EXCL -> EEXIST, RENAME_SWAP ok
```

SWAP 뒤 `x.cur` = 새 것, `x.new` = 옛 것 → 원자적 교체와 백업을 한 번에. Linux 의 대응은 `renameat2(RENAME_NOREPLACE / RENAME_EXCHANGE)`. 둘 다 이식성이 없으니 일반 코드는 `rename` + 별도 백업.

</details>

### 연습 4 — 권한 유지와 하드 링크

대상 `perm.cfg` 가 0600 이고 하드 링크 `alias.cfg` 가 있다. 원자적 저장 뒤에도 권한이 0600 이게 하라. 하드 링크는 무엇을 보는가?

<details>
<summary>답 보기</summary>

`fstatat(dirfd, name)` 로 옛 권한을 읽고 임시 파일에 `fchmod(fd, mode)` — `open` 의 mode 인자는 umask 에 깎이지만 `fchmod` 는 정확히 설정한다. rename 은 이름만 새 inode 로 옮기므로 하드 링크 `alias.cfg` 는 옛 inode 를 계속 가리킨다:

```text
ex4 inode 3973444 -> 3973447, mode 600, alias still 'old'
```

편집기(vim `backupcopy`) 가 "rename 방식이냐 제자리 복사 방식이냐" 옵션을 두는 이유가 이것이다. 소유자는 `fchown` (root 권한 필요할 수 있음), xattr/ACL 은 macOS `copyfile(COPYFILE_METADATA)` 같은 별도 API.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice      # 19/19 passed (약 0.25 초)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_asan && ./pr_asan   # 19/19, 경고 0
```

## 10. 핵심 정리

- 원자적 저장 = **같은 디렉터리의 임시 파일에 write_all → fsync(tmp) → close → rename → fsync(dir)**. rename 은 이름만 원자적으로 바꾼다. 내용의 내구성은 그 전의 fsync 몫이다.
- 빼먹으면: 제자리 O_TRUNC → 빈/반쪽 파일 (kill 로 잡힘). fsync(tmp) 없음 → 전원 차단 후 빈 파일. fsync(dir) 없음 → "성공" 뒤 옛 것. 뒤의 둘은 **kill 로 안 잡힌다**.
- kill 은 프로세스만 죽인다. 페이지 캐시는 살아남는다. 전원 차단은 모델(ALICE/plfs), dm-flakey·dm-log-writes, VM 강제 종료, 전원 릴레이로 시험한다.
- write 는 짧게 쓸 수 있다 → 루프. fsync 가 EIO 면 재시도하지 말고 저장 전체를 다시 (fsyncgate). 실패 경로에서 errno 보존, 임시 파일 정리.
- macOS fsync 는 드라이브 캐시를 flush 하지 않는다. F_FULLFSYNC = flush (실측 3.2 ms, fsync 의 ~180배), F_BARRIERFSYNC = 순서만 (145 µs). 파일은 BARRIER, 커밋 지점은 FULLFSYNC.
- 장치 바닥: NVMe Flush·FUA, 휘발성 쓰기 캐시(VWC), PLP. Flush 를 받은 펌웨어는 데이터와 매핑을 둘 다 복구 가능하게 해야 한다.
- 부팅 때 남은 임시 파일을 지우고, 레코드는 magic·len·CRC 로 검증 → 실패면 기본값. CRC 는 검출만 한다. 옛 것을 지키는 건 rename / A/B 슬롯.

## 참고자료

- OSTEP 39장 *Interlude: Files and Directories* — 39.6 `fsync()` (디렉터리 fsync 언급), 39.7 rename 과 원자적 갱신 패턴; 42장 *Crash Consistency: FSCK and Journaling*; 44장 *Data Integrity and Protection* (체크섬); 부록 *Flash-based SSDs*: <https://pages.cs.wisc.edu/~remzi/OSTEP/> (로컬: `operating-system-three-easy-pieces/book-md/C39_interlude_files_and_directories.md`, `C42_crash_consistency_fsck_and_journaling.md`, Don 노트 `notes/2026-09-30_C39_files_and_directories.md`, `notes/2026-09-30_C42_crash_consistency_journaling.md`)
- POSIX `rename()` — 원자성, "new 링크는 내내 보인다": <https://pubs.opengroup.org/onlinepubs/9699919799/functions/rename.html>
- POSIX `fsync()`: <https://pubs.opengroup.org/onlinepubs/9699919799/functions/fsync.html> · `write()` (부분 쓰기, EINTR): <https://pubs.opengroup.org/onlinepubs/9699919799/functions/write.html>
- macOS 로컬 man 페이지: `man 2 fsync` (드라이브 캐시 경고, F_FULLFSYNC 권고), `man 2 fcntl` (F_FULLFSYNC, F_BARRIERFSYNC), `man 2 rename` (크래시 중에도 new 존재, renamex_np RENAME_SWAP/RENAME_EXCL)
- Linux `fsync(2)` — 디렉터리 fsync 필요: <https://man7.org/linux/man-pages/man2/fsync.2.html>
- Linux ext4 문서 — `auto_da_alloc`: <https://docs.kernel.org/admin-guide/ext4.html>
- Linux device-mapper `dm-flakey`: <https://docs.kernel.org/admin-guide/device-mapper/dm-flakey.html> · `dm-log-writes`: <https://docs.kernel.org/admin-guide/device-mapper/log-writes.html>
- Pillai et al., *All File Systems Are Not Created Equal: On the Complexity of Crafting Crash-Consistent Applications* (OSDI 2014) — ALICE
- Mohan et al., *Finding Crash-Consistency Bugs with Bounded Black-Box Crash Testing* (OSDI 2018) — CrashMonkey/ACE
- LWN, *PostgreSQL's fsync() surprise* (2018): <https://lwn.net/Articles/752063/>
- SQLite `PRAGMA fullfsync`: <https://www.sqlite.org/pragma.html#pragma_fullfsync>
- NVM Express Base Specification — Flush 명령, Write 의 FUA 비트, Identify Controller VWC 필드: <https://nvmexpress.org/specifications/>
- littlefs DESIGN.md — 메타데이터 쌍, copy-on-write 원자적 갱신: <https://github.com/littlefs-project/littlefs/blob/master/DESIGN.md>
- 관련 레슨: OS-B01 (fork/exec/wait, 상태 분류), OS-B02 (fd 테이블), OS-B07 (시그널), OS-B09 (mmap·msync), STOR-B01 (내구성 계층 지연 분포·NVMe 대응표), STOR-M03 (WAL·group commit), STOR-A03 (LittleFS), OS-M14 (VSFS), OS-A07 (저널링 + 크래시 주입), BM-A02 (플래시 드라이버), BM-A06 (power-cut 전수 주입), PLAT-M07 (A/B 슬롯), PLAT-M08 (설정 저장소 운영), DS-A10 (플래시 KV), MEM-M14 (FTL 전원 차단 복구), INFRA-M04 (보드 전원 제어 자동화), ELX-B13 (systemd 유닛)
