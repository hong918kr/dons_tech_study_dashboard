---
id: OS-M03
time: 80분
updated: 2026-11-11
---
# OS-M03 COW fork와 페이지 폴트 관찰

## 학습 목표

- fork 직후 부모와 자식이 **같은 물리 페이지**를 쓰기 금지(read-only)로 나눠 갖고, 누군가 쓰는 순간 그 페이지만 복사된다는 것(COW, copy-on-write)을 `getrusage(RUSAGE_SELF).ru_minflt` 숫자로 보인다. 자식이 k 페이지를 쓰면 폴트가 k + 1, 직선 맞춤 기울기는 1.0001이었다(이 맥 실측).
- 폴트가 **바이트가 아니라 페이지 단위**로 난다는 것을 쓰기 목록 → "건드린 서로 다른 페이지 수" 예측(구간 합치기)과 실측 비교로 확인한다. 이 맥의 페이지는 16 KB라서 `sysconf(_SC_PAGESIZE)` 만 쓴다.
- "자식이 읽기만 하면 폴트가 0인가?"에 OS별 실측으로 답한다. 이 맥과 Lima VM(arm64 Linux 7.0) 모두 **페이지당 1번**이었다. 이유는 서로 다르다. Linux 쪽은 `/proc/self/pagemap` 으로 원인을 좁혔다.
- fork / vfork / posix_spawn 비용을 1 GB 부모에서 재고 왜 다른지 설명한다. 실측에서 fork 3.6 ms, posix_spawn 0.15 ms였다. 데이터가 아니라 **페이지 테이블(매핑)** 을 복사하는 비용이고, fork 뒤에는 **부모도** 쓸 때마다 폴트를 낸다(1 GB 다시 쓰기 0.4 ms → 68 ms).
- 큰 데몬에서 다른 프로그램을 띄우는 올바른 방법을 구현한다. fork+exec 에서는 exec 실패를 CLOEXEC 파이프로 errno 로 돌려받고, posix_spawn 에서는 출력 dup2 와 시그널 마스크를 초기화한다. MMU가 없는 uClinux에는 fork가 없고 vfork만 있다는 펌웨어 쪽 사실도 연결한다.

## 1. 문제

"fork 하면 메모리를 다 복사하나요? 1 GB 프로세스가 `system("ls")` 를 부르면 무슨 일이 생기죠?" 시스템·임베디드 리눅스 면접의 단골 질문이다. 펌웨어 현장에서는 이렇게 나타난다. 카메라 프레임 버퍼와 신경망 가중치로 수백 MB를 쥔 비전 데몬이 `popen("iw dev wlan0 link")` 를 1초마다 부른다. 그러면 제어 루프에 수 ms짜리 지연이 생기고, fork 직후에는 데몬 자신의 쓰기마다 폴트가 나서 프레임 처리 시간이 튄다. 원인을 숫자로 말할 수 있어야 고칠 수 있다(posix_spawn, 헬퍼 프로세스를 일찍 띄워 두기, 메모리 배치).

이미 배운 것: fork/exec/waitpid 와 종료 상태 해석(OS-B01), 시그널 마스크(OS-B07), mmap 의 SHARED/PRIVATE 가 fork 뒤 어떻게 **보이는지**(OS-B09). 이 레슨은 그 안쪽, 즉 **언제 복사되고 그 비용이 얼마인지**를 폴트 수로 본다. 페이지 테이블을 관리하는 연산 자체는 OS-M01, xv6 에 COW fork 를 직접 구현하는 것은 OS-A10 이다. OSTEP 5장(Process API), 21장(page fault 흐름), 23.5절(VMS 의 demand zeroing 과 copy-on-write)이 배경이다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
<defs><marker id="m3a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" fill="currentColor" font-weight="bold">fork 직후 → 자식이 페이지 2 에 1 바이트 쓰기</text>
<text x="20" y="48" fill="currentColor">부모 페이지 테이블</text>
<rect x="20" y="58" width="150" height="26" class="f-mut" stroke="currentColor"/><text x="28" y="76" fill="currentColor" font-family="monospace">VP0 → F10  R-</text>
<rect x="20" y="84" width="150" height="26" class="f-mut" stroke="currentColor"/><text x="28" y="102" fill="currentColor" font-family="monospace">VP1 → F11  R-</text>
<rect x="20" y="110" width="150" height="26" class="f-mut" stroke="currentColor"/><text x="28" y="128" fill="currentColor" font-family="monospace">VP2 → F12  R-</text>
<rect x="20" y="136" width="150" height="26" class="f-mut" stroke="currentColor"/><text x="28" y="154" fill="currentColor" font-family="monospace">VP3 → F13  R-</text>
<text x="290" y="48" fill="currentColor">물리 프레임</text>
<rect x="290" y="58" width="90" height="26" class="f-acc" stroke="currentColor"/><text x="300" y="76" fill="currentColor">F10 (공유)</text>
<rect x="290" y="84" width="90" height="26" class="f-acc" stroke="currentColor"/><text x="300" y="102" fill="currentColor">F11 (공유)</text>
<rect x="290" y="110" width="90" height="26" class="f-acc" stroke="currentColor"/><text x="300" y="128" fill="currentColor">F12 (부모)</text>
<rect x="290" y="136" width="90" height="26" class="f-acc" stroke="currentColor"/><text x="300" y="154" fill="currentColor">F13 (공유)</text>
<rect x="290" y="176" width="90" height="26" class="f-ok" stroke="currentColor"/><text x="300" y="194" fill="currentColor">F57 (새것)</text>
<text x="500" y="48" fill="currentColor">자식 페이지 테이블</text>
<rect x="500" y="58" width="150" height="26" class="f-mut" stroke="currentColor"/><text x="508" y="76" fill="currentColor" font-family="monospace">VP0 → F10  R-</text>
<rect x="500" y="84" width="150" height="26" class="f-mut" stroke="currentColor"/><text x="508" y="102" fill="currentColor" font-family="monospace">VP1 → F11  R-</text>
<rect x="500" y="110" width="150" height="26" class="f-ok" stroke="currentColor"/><text x="508" y="128" fill="currentColor" font-family="monospace">VP2 → F57  RW</text>
<rect x="500" y="136" width="150" height="26" class="f-mut" stroke="currentColor"/><text x="508" y="154" fill="currentColor" font-family="monospace">VP3 → F13  R-</text>
<line x1="170" y1="71" x2="288" y2="71" stroke="currentColor" marker-end="url(#m3a)"/>
<line x1="170" y1="97" x2="288" y2="97" stroke="currentColor" marker-end="url(#m3a)"/>
<line x1="170" y1="123" x2="288" y2="123" stroke="currentColor" marker-end="url(#m3a)"/>
<line x1="170" y1="149" x2="288" y2="149" stroke="currentColor" marker-end="url(#m3a)"/>
<line x1="500" y1="71" x2="382" y2="71" stroke="currentColor" marker-end="url(#m3a)"/>
<line x1="500" y1="97" x2="382" y2="97" stroke="currentColor" marker-end="url(#m3a)"/>
<line x1="500" y1="123" x2="382" y2="189" stroke="currentColor" stroke-width="2" marker-end="url(#m3a)"/>
<line x1="500" y1="149" x2="382" y2="149" stroke="currentColor" marker-end="url(#m3a)"/>
<text x="390" y="222" fill="currentColor" font-size="12">① 자식 쓰기 → 쓰기 금지라 폴트 (ru_minflt +1)</text>
<text x="390" y="240" fill="currentColor" font-size="12">② 커널: 새 프레임 F57 할당, F12 내용 복사</text>
<text x="390" y="258" fill="currentColor" font-size="12">③ 자식 PTE 만 F57·RW 로 바꾸고 명령 재실행</text>
<rect x="10" y="212" width="360" height="106" rx="4" class="f-warn" stroke="currentColor"/>
<text x="20" y="232" fill="currentColor">• fork 는 매핑(페이지 테이블)만 복사</text>
<text x="20" y="252" fill="currentColor">• 데이터 복사 = 쓰기가 일어난 페이지만, 그때</text>
<text x="20" y="272" fill="currentColor">• 부모 PTE 도 R- → 부모 첫 쓰기도 폴트</text>
<text x="20" y="292" fill="currentColor">• 1 바이트를 써도 한 페이지 통째(이 맥 16 KB)</text>
<text x="20" y="310" fill="currentColor">• 다시 쓰기 = 이미 내 사본 → 폴트 0</text>
<text x="390" y="290" fill="currentColor" font-size="12">k 페이지를 쓰면 폴트 ≈ k, 복사 = k × 페이지 크기</text>
</svg>
```

**이 레슨에서 실제로 본 폴트 수** (자식이 k = 256 페이지를 건드림, 부모는 fork 전에 영역 전체를 써 둠):

| 자식이 하는 일 | 이 맥 (macOS, 16 KB) | Lima VM (Linux 7.0 arm64, 4 KB) | 무엇 때문인가 |
|---|---|---|---|
| 쓰기 (PRIVATE) | 257 | 257 | COW 복사 (+1 은 아래 Part A 설명) |
| 같은 페이지 다시 쓰기 | 0 | 0 | 이미 내 사본 |
| 읽기만 (PRIVATE) | 257 | 257 | 맥: 자식 매핑이 비어 있음(추정) / Linux: PTE 는 있지만 "old" → 접근 플래그 폴트(§5 B) |
| 읽고 쓰기, 같은 페이지 (300 페이지) | 601 | 601 | 읽기 폴트 + COW 폴트 = 페이지당 2 |
| 쓰기 (MAP_SHARED, 300 페이지) | 301 | 20 | 복사 없음. Linux 는 fault-around(64 KB = 16 페이지씩 매핑) |
| 부모가 안 건드린 페이지에 쓰기 (200) | 201 | 201 | COW 아님: demand-zero (연습 1) |

### Part A — fork 한 자식의 폴트 세기와 직선 맞춤

- **TODO A1** `minflt_self()` 는 `getrusage(RUSAGE_SELF, …)` 의 `ru_minflt` 를 돌려준다. `touch_pages(base, first, k, op, val)` 는 페이지 `first..first+k-1` 의 첫 바이트를 `PG = sysconf(_SC_PAGESIZE)` 간격으로 읽거나(T_READ), 쓰거나(T_WRITE), 읽고 쓴다(T_RW). 읽은 값의 합을 돌려주며, `volatile` 로 접근해서 컴파일러가 읽기를 지우지 못하게 한다.
- **TODO A2** `cow_probe(work, ctx, &res)` — fork 하고, 자식이 `work` 를 **두 번** 돌리며 각각의 `ru_minflt` 증가량(`d1`, `d2`)과 두 번째 반환값을 파이프로 보낸 뒤 `_exit(0)` 한다. 부모는 시간 제한을 두고 읽고(`read_full_timeout`), `wait_child` 로 거둔다.
- **TODO A3** `fit_line(x, y, n, &slope, &icept, &r2)` — 최소제곱 직선과 결정계수다. 하네스는 2048 페이지(이 맥 32 MiB)를 0xA5 로 채운 부모에서 k ∈ {0, 1, 4, 16, 64, 256, 1024, 2048} 을 재고 `k ≤ d1 ≤ k + slack`, `d2 ≈ 0`, 기울기 0.95~1.30, r² > 0.999, 부모 데이터 불변을 체크한다.

### Part B — 페이지 단위, 읽기 폴트, 격리 vs 공유

- **TODO B1** `pages_touched(w, n, pg)` — 쓰기 목록 `{off, len}` 이 덮는 **서로 다른** 페이지 수를 센다. 경계를 넘는 2 바이트 쓰기는 2 페이지이고, 같은 페이지에 세 번 쓰면 1 이다. 겹치는 구간은 합친다. 자식이 실제로 같은 목록을 쓰게 해서 예측 ≤ 실측 ≤ 예측 + slack 을 확인한다.
- 읽기만 하는 자식은 폴트 수가 OS마다 다르므로 **상한만** 체크하고 실제 값을 출력한다(위 표).
- **TODO B2** `child_write_changes(shared, k, &saw, &faults)` — PRIVATE 이면 부모 쪽에서 바뀐 페이지가 0 개(COW 격리), SHARED 이면 k 개(같은 물리 페이지)여야 한다. 두 경우 모두 자식은 자기가 쓴 값을 읽는다.

### Part C — 큰 프로세스에서 프로그램 띄우기

- **TODO C1** `spawn_fork_exec(path, argv, out_fd, &status)` — fork + execv 로 띄우고 stdout 을 `out_fd` 로 dup2 한다. exec 가 실패하면 **FD_CLOEXEC 오류 파이프**로 errno 를 받아 반환값으로 돌려준다(성공하면 exec 가 파이프를 닫아서 부모는 EOF 를 본다).
- **TODO C2** `spawn_posix(…)` — 같은 일을 `posix_spawn` + `posix_spawn_file_actions_adddup2` 로 한다. `POSIX_SPAWN_SETSIGMASK` 로 자식의 시그널 마스크를 비운다. 테스트는 `/bin/echo` 출력 캡처, `/usr/bin/false` 종료 코드 1, 없는 파일이면 `ENOENT`, 좀비 없음, fd 누수 없음, 그리고 부모가 막아 둔 SIGUSR1 이 자식에 새지 않는지를 본다.
- `demo_bench.c` 는 부모 0 / 256 MB / 1 GB 에서 fork+_exit, fork+exec, vfork+exec, posix_spawn, fork + 자식이 전부 쓰기의 지연을 잰다. 이어서 "fork 뒤 부모가 다시 쓰는 비용"도 잰다. 맥과 VM 의 실측을 §5 C 에 실었다.

## 2. Problem (English)

> A process has a large anonymous private buffer (2048 pages, already written). Use `sysconf(_SC_PAGESIZE)`; never assume 4096 (Apple Silicon macOS uses 16 KiB).
>
> 1. `long minflt_self(void)`, `uint64_t touch_pages(char *base, size_t first, size_t k, touch_op op, uint8_t val)` and `int cow_probe(work_fn work, void *ctx, probe_res *out)`: fork; in the child measure `ru_minflt` around `work(ctx)` twice and send `{d1, d2, ret}` back through a pipe, then `_exit(0)`. The parent must never block forever (timeouts) and must reap the child.
> 2. Show that when the child writes *k* pages, `d1` grows linearly with slope ≈ 1, and a second pass costs ≈ 0. Implement `void fit_line(const double *x, const double *y, int n, double *slope, double *icept, double *r2)`. The parent's data must be unchanged (COW isolation), while a `MAP_SHARED` region is visible to the parent (`size_t child_write_changes(bool shared, size_t k, uint64_t *child_saw, long *faults)`).
> 3. Faults are per page: `size_t pages_touched(const wr_t *w, size_t n, size_t pg)` returns the number of distinct pages covered by a list of `{off, len}` writes (merge overlapping/adjacent ranges; `len == 0` covers nothing).
> 4. Spawning from a big process: `int spawn_fork_exec(const char *path, char *const argv[], int out_fd, int *status)` (report exec failure as an errno return value via a close-on-exec pipe) and `int spawn_posix(...)` (`posix_spawn` with `adddup2` and an empty signal mask). Return 0 and the `waitpid` status on success.
>
> Constraints: no exact fault counts in tests (bounds like `k ≤ d ≤ k + slack`), no timing in tests, no zombies, no fd leaks. Follow-ups: do read-only touches fault? Why is `fork()` of a 1 GB process slower than `posix_spawn()`, and what does `vfork()` do on your OS? What happens to the parent after a fork?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

처음 실행한 실제 출력 일부다(0.11초, exit 1). 스텁은 fork 를 하지 않고 `-1`/`ENOSYS` 를 돌려주므로 멈추거나 자식을 남기지 않는다.

```text
page size 16384
== Part A: minflt / touch_pages ==
  zero-fill: 32 fresh pages written -> minflt +0
FAIL starter.c:243  f0 >= 0 && f1 - f0 >= 32 && f1 - f0 <= 32 + SLACK(32)
…
== Part B: COW isolation vs MAP_SHARED ==
  PRIVATE: parent pages changed -1, child faults -1
…
  fork+exec   nonexistent: rc=78 (Function not implemented)
…
7/38 passed
```

TODO 는 A1(`minflt_self`, `touch_pages`), A2(`cow_probe`), A3(`fit_line`), B1(`pages_touched`), B2(`child_write_changes`), C1(`spawn_fork_exec`), C2(`spawn_posix`) 이다. 하네스가 주는 도우미는 `read_full_timeout`(poll + read), `wait_child`(WNOHANG 폴링, 3초 뒤 SIGKILL), `no_children_within`(좀비 확인), `count_open_fds`, `map_region`, `pages_not_equal` 이다. `main` 의 `alarm(30)` 이 마지막 안전장치다.

## 4. 힌트

<details><summary>힌트 1 — 폴트를 세는 루프</summary>

`getrusage(RUSAGE_SELF, &ru)` 의 `ru.ru_minflt` 다. 맥 `man getrusage` 는 이것을 "I/O 없이 처리된 페이지 폴트(page reclaims)"라고 설명한다. 루프는 `for (i = first; i < first + k; i++) p[i * PG] = val;` 이고, `p` 는 `volatile uint8_t *` 로 둔다. volatile 이 없으면 `-O2` 가 "결과를 안 쓰는 읽기"를 지워 버려서 폴트 0 이라는 엉뚱한 결론이 나온다.
</details>

<details><summary>힌트 2 — 자식에서 재고 부모에게 보내기</summary>

`RUSAGE_SELF` 는 부르는 프로세스 자신의 값이다. 그래서 자식 안에서 앞뒤로 재야 한다. 자식은 fork 직후 카운터가 0 부터 시작한다(맥 `man fork`: "resource utilizations are set to 0"). 그래도 차이(델타)를 쓰는 게 안전하다. 결과는 구조체 하나를 `write` 로 보내면 된다. 파이프는 `PIPE_BUF` 이하의 쓰기를 원자적으로 처리한다. 자식은 `exit` 가 아니라 `_exit` 로 끝낸다. 부모에게서 복제된 stdio 버퍼가 두 번 출력되는 것을 막기 위해서다. 부모는 쓰기 끝을 **먼저 닫아야** 자식이 죽었을 때 EOF 를 받는다.
</details>

<details><summary>힌트 3 — 최소제곱</summary>

평균 `mx, my`, `Sxx = Σ(x−mx)²`, `Sxy = Σ(x−mx)(y−my)`, `Syy = Σ(y−my)²` 를 구한다. 그러면 `slope = Sxy/Sxx`, `icept = my − slope·mx`, `r² = 1 − SS_res/Syy` 다. `Sxx = 0` 이면 직선을 정할 수 없다(0 을 돌려준다). `Syy = 0` 이면 모든 점이 수평선 위에 있으니 r² = 1 로 정한다.
</details>

<details><summary>힌트 4 — 서로 다른 페이지 세기</summary>

쓰기 하나는 `[off/pg, (off+len−1)/pg]` 페이지 구간이다(`len == 0` 은 건너뛴다). 구간을 시작 순으로 정렬한 뒤 `다음 시작 ≤ 지금 끝 + 1` 이면 합치고, 아니면 지금 구간 길이를 더하고 새로 시작한다. "+1" 이 있어야 맞닿은 구간 3, 4 도 합쳐진다. 합치지 않아도 개수는 같지만 코드가 단순해진다.
</details>

<details><summary>힌트 5 — exec 실패를 부모에게 알리는 법</summary>

fork 전에 `pipe(ep)` 를 만들고 양쪽에 `FD_CLOEXEC` 를 건다. 자식에서 exec 가 **성공하면** 커널이 쓰기 끝을 닫으므로 부모의 `read` 는 0(EOF)을 받는다. **실패하면** 자식이 `errno` 4 바이트를 쓰고 `_exit(127)` 한다. 부모는 받은 바이트 수로 두 경우를 구별한다. 127 이라는 종료 코드만으로는 "exec 실패"와 "프로그램이 127 로 끝남"을 구별할 수 없다(OS-B01 의 셸 관례).
</details>

<details><summary>힌트 6 — posix_spawn 의 두 객체</summary>

`posix_spawn_file_actions_t` 는 "자식에서 exec 전에 할 fd 작업" 목록이다(`adddup2(out_fd, 1)`). `posix_spawnattr_t` 는 속성이다(`setsigmask` + `setflags(POSIX_SPAWN_SETSIGMASK)`). 반환값은 **오류 번호 그 자체**다. `-1` + `errno` 형식이 아니다. 둘 다 `_destroy` 해야 한다.
</details>

## 5. 풀이

### Part A — k 페이지 쓰기 = k 번 폴트

**자식 안에서 일어나는 일.** fork 는 부모의 쓰기 가능한 PRIVATE 페이지를 양쪽 모두 쓰기 금지로 바꾼다. 자식이 페이지 i 에 처음 쓰면 MMU 가 권한 폴트를 내고, 커널은 "이 페이지는 COW 대상"임을 알아본다(Linux 는 VMA 가 쓰기 가능한데 PTE 가 쓰기 금지인 경우, XNU 는 copy-on-write 엔트리/섀도 객체). 그러면 새 프레임을 할당하고, 16 KB 를 복사하고, 자식 PTE 를 바꾼 뒤 그 명령을 다시 실행한다. 이 폴트는 디스크 I/O 가 없으므로 **minor** fault 로 세어진다. OSTEP 23.5 는 이 기법이 TENEX 까지 거슬러 올라간다고 소개한다.

`cow_probe` 의 뼈대는 이렇다(정답 코드에서 발췌, 세부는 §6).

```c
pid_t pid = fork();
if (pid == 0) {                          /* 자식 */
    long f0 = minflt_self(); (void)work(ctx);
    long f1 = minflt_self(); r.ret = work(ctx);
    long f2 = minflt_self();
    r.d1 = f1 - f0; r.d2 = f2 - f1;
    _exit(write(p[1], &r, sizeof r) == (ssize_t)sizeof r ? 0 : 1);
}
close(p[1]);                             /* 이걸 안 닫으면 자식이 죽어도 EOF 가 안 온다 */
size_t got = read_full_timeout(p[0], out, sizeof *out, 3000);
```

정답의 실제 출력이다(이 맥). Lima VM 에서도 같은 소스로 `d1` 열이 **똑같이** 나왔다(4 KB 페이지).

```text
== Part A: fork, child writes k of 2048 pages ==
       k       d1       d2
       0        0        0
       1        2        0
       4        5        0
      16       17        0
      64       65        0
     256      257        0
    1024     1025        0
    2048     2049        0
  fit: d1 = 1.0001 * k + 0.83   (r2 = 1.000000)
```

| 관찰 | 해석 |
|---|---|
| 기울기 1.0001, r² = 1.000000 | 쓴 페이지 하나 = 폴트 하나 = 페이지 하나 복사. 데이터 복사량 = k × 16 KB |
| `d2` 가 항상 0 | 두 번째 쓰기는 이미 자식 소유 페이지에 한다. 복사는 페이지당 **한 번** |
| k ≥ 1 이면 +1 (k = 0 이면 0) | 출처를 확인하지는 않았다. 추정: `touch_pages` 루프 본문의 **코드 페이지**를 자식이 처음 실행하면서 나는 폴트다. k = 0 이면 루프 본문을 실행하지 않는다. 맥에서 `memset`(dyld 공유 캐시 안)을 쓴 Part B 목록 테스트는 +0 이었고, Linux 는 +1 이었다 |
| ASan 빌드에서 기울기 1.1253 | ASan 이 쓰기마다 **그림자 메모리**(8 바이트당 1 바이트)를 읽는다. 16 KB 앱 페이지 8 개당 그림자 페이지 1 개 = 1/8 = 0.125. 그래서 체크 상한을 1.30 으로, slack 을 `16 + k/4` 로 잡았다 |

체크를 **정확한 수가 아니라 범위**로 둔 이유가 마지막 두 줄이다. 코드 페이지, libc, 새니타이저, 스택 확장 같은 잡음이 OS 와 빌드마다 다르다. 하한 `d1 ≥ k` 는 COW 가 맞다면 어디서나 성립해야 하고, 상한은 "데이터 복사가 그 이상 일어나지 않는다"를 보장한다.

**페이지 크기.** 이 맥의 `hw.pagesize` 는 16384 다. 4096 간격으로 쓰면 한 페이지에 네 번 쓰는 셈이라 폴트는 k/4 로 나오고, 기울기 0.25 를 보고 "COW 가 이상하다"고 잘못 결론 내린다. 반대로 4 KB 페이지 Linux 에서 16384 간격으로 쓰면 페이지를 세 개씩 건너뛴다. 그래서 하네스는 `PG = sysconf(_SC_PAGESIZE)` 하나만 쓴다.

### Part B — 페이지 단위, 읽기 폴트, 격리

**B1 구간 합치기.** 실제 출력은 `predicted 8 pages, child minflt +8 (rewrite +0)` 이다. 목록은 `{PG−100, 200}`(페이지 0, 1에 걸침), 같은 페이지 10 에 두 번, `{20·PG, 3·PG}`(20~22), `{21·PG+50, 10}`(이미 포함), `{100·PG−1, 2}`(99, 100)이다. 합치면 2 + 1 + 3 + 2 = 8 이다. 이 함수가 Part C 와 연습 4 의 "메모리 배치로 COW 비용 줄이기"의 계산기다. **200 바이트 레코드가 페이지 경계에 걸치면 레코드 하나를 고칠 때 32 KB 가 복사된다.**

**읽기만 하는 자식은 폴트가 0 인가?** 교과서 그림대로라면 PTE 가 복사되어 있으니 읽기는 그냥 통과해야 한다. 실측은 두 OS 모두 페이지당 1 이었다.

```text
  child READS 256 pages: minflt +257 (second pass +0)        ← 이 맥
```

원인은 OS 마다 다르다. Linux 쪽은 `demo_pagemap_linux.c` 로 좁혔다. 첫 줄이 `/* ENV: linux */` 인 파일이라 맥 검증에서는 건너뛴다.

<!-- code: demo_pagemap_linux.c -->

```bash
# Lima VM (홈 마운트가 읽기 전용이라 /tmp 로 복사해서 빌드)
limactl shell elinux -- bash -lc 'mkdir -p /tmp/OS-M03 && cp <lesson dir>/*.c /tmp/OS-M03/ && cd /tmp/OS-M03 && gcc -std=c11 -Wall -Wextra -Werror -O2 demo_pagemap_linux.c -o pm && ./pm'
```

Lima VM 실제 출력:

```text
page size 4096
[PRIVATE] child : present before any touch 256/256, read pass 1 +256, pass 2 +0
[PRIVATE] parent: read after fork +0
[SHARED ] child : present before any touch 0/256, read pass 1 +16, pass 2 +0
[SHARED ] parent: read after fork +0
```

- **Linux arm64(이 VM)**: 자식 PTE 는 fork 때 **복사되어 있다**(pagemap present 256/256). 그런데도 첫 읽기가 폴트다. 가장 그럴듯한 설명은 이렇다. Linux 는 fork 때 자식 PTE 를 "old"(접근 비트 0)로 복사한다(`copy_present_pte` 의 `pte_mkold` — 커널 소스 기억, 이 버전에서 확인하지 않음). 하드웨어가 Access Flag 를 자동으로 세우지 못하는 CPU 에서는 첫 접근이 **Access Flag 폴트**가 되고, 커널이 비트를 세우며 minor fault 로 센다. 부모 PTE 는 old 로 만들지 않으므로 부모의 읽기는 0 이다(위 출력과 일치). dmesg 에 "Hardware dirty bit management" 줄이 없는 것도 맞아떨어지지만, 이 VM 의 HAFDBS 지원 여부를 직접 읽지는 못했다. x86 은 하드웨어가 A 비트를 세우므로 자식의 읽기 폴트가 ~0 일 것으로 **예상**한다(실행해 보지 않음). A/D 비트를 소프트웨어로 관리하는 경우는 RISCV-M12 에서 다룬다.
- **macOS**: pagemap 같은 창이 없어서 직접 볼 수 없었다. 자식 매핑이 비어 있다가 첫 접근 때 채워진다고 보면 관찰(읽기 1/page, 읽기 뒤 쓰기 +1/page, 부모 읽기 0)이 모두 설명된다(추정). XNU 의 arm64 `pmap_copy` 가 아무것도 하지 않는다는 기억이 있지만 확인하지 않았다.
- **SHARED**: Linux 는 공유 매핑의 PTE 를 fork 때 아예 복사하지 않는다(present 0/256). 첫 폴트 때 주변 16 페이지(`/sys/kernel/debug/fault_around_bytes` = 65536, VM 에서 읽음)를 한꺼번에 매핑하므로 256 페이지 읽기에 폴트 16 번이다. Part B 의 SHARED 쓰기 300 페이지가 20 번인 것도 같은 이유다(300/16 ≈ 19).

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
<defs><marker id="m3b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" fill="currentColor" font-weight="bold">fork 뒤 자식의 첫 접근 한 번에 일어나는 일 (PRIVATE 익명 페이지)</text>
<rect x="10" y="40" width="150" height="40" rx="4" class="f-mut" stroke="currentColor"/><text x="22" y="65" fill="currentColor">자식이 페이지 접근</text>
<rect x="210" y="40" width="220" height="40" rx="4" class="f-acc" stroke="currentColor"/><text x="220" y="58" fill="currentColor" font-size="12">PTE 가 접근 가능한가?</text><text x="220" y="74" fill="currentColor" font-size="12">(맥: 비어 있음 / Linux: old)</text>
<line x1="160" y1="60" x2="208" y2="60" stroke="currentColor" marker-end="url(#m3b)"/>
<rect x="480" y="40" width="250" height="40" rx="4" class="f-warn" stroke="currentColor"/><text x="490" y="58" fill="currentColor" font-size="12">폴트 #1: 공유 프레임 R- 매핑</text><text x="490" y="74" fill="currentColor" font-size="12">(또는 AF 세움) — 복사 없음</text>
<line x1="430" y1="60" x2="478" y2="60" stroke="currentColor" marker-end="url(#m3b)"/><text x="440" y="54" fill="currentColor" font-size="12">아니오</text>
<rect x="210" y="120" width="220" height="40" rx="4" class="f-acc" stroke="currentColor"/><text x="220" y="145" fill="currentColor">쓰기인가?</text>
<line x1="320" y1="80" x2="320" y2="118" stroke="currentColor" marker-end="url(#m3b)"/><text x="326" y="104" fill="currentColor" font-size="12">예 (또는 #1 뒤)</text>
<rect x="10" y="120" width="150" height="40" rx="4" class="f-ok" stroke="currentColor"/><text x="22" y="145" fill="currentColor">읽기 끝 (폴트 0~1)</text>
<line x1="210" y1="140" x2="162" y2="140" stroke="currentColor" marker-end="url(#m3b)"/><text x="168" y="134" fill="currentColor" font-size="12">아니오</text>
<rect x="480" y="120" width="250" height="40" rx="4" class="f-warn" stroke="currentColor"/><text x="490" y="138" fill="currentColor" font-size="12">폴트 #2: 쓰기 금지 → COW</text><text x="490" y="154" fill="currentColor" font-size="12">새 프레임 + 페이지 복사 + PTE RW</text>
<line x1="430" y1="140" x2="478" y2="140" stroke="currentColor" marker-end="url(#m3b)"/><text x="440" y="134" fill="currentColor" font-size="12">예</text>
<text x="10" y="196" fill="currentColor" font-weight="bold">그래서 실측이 이렇게 나온다 (페이지당)</text>
<text x="10" y="218" fill="currentColor" font-family="monospace" font-size="12">쓰기만          1   (#1 과 #2 가 한 폴트로 처리됨)</text>
<text x="10" y="238" fill="currentColor" font-family="monospace" font-size="12">읽기만          1   (#1), x86 은 0 예상</text>
<text x="10" y="258" fill="currentColor" font-family="monospace" font-size="12">읽고 쓰기       2   (#1 + #2), 예: refcnt++</text>
<text x="10" y="278" fill="currentColor" font-family="monospace" font-size="12">다시 쓰기       0   이미 내 사본</text>
<text x="480" y="218" fill="currentColor" font-size="12">부모도 같은 표를 따른다:</text>
<text x="480" y="238" fill="currentColor" font-size="12">fork 뒤 첫 쓰기 = 폴트 (자식이</text>
<text x="480" y="258" fill="currentColor" font-size="12">이미 죽었어도), 읽기 = 0</text>
</svg>
```

**B2 격리 vs 공유.** 실제 출력은 아래와 같다. 줄마다 자식이 300 페이지를 읽고 0x5A 를 쓴 뒤, 다시 읽어서 자기가 쓴 값을 확인한다.

```text
  PRIVATE: parent pages changed 0, child faults 601
  SHARED : parent pages changed 300, child faults 301
```

PRIVATE 는 자식이 무엇을 써도 부모가 보지 못한다. 이것이 fork 의 의미론이다(POSIX `fork`: 자식은 부모 주소 공간의 **사본**). COW 는 그 의미론을 **게으르게** 구현한 것일 뿐이다. SHARED 는 복사가 없으니 폴트가 "매핑하기" 한 번뿐이고(맥 1/page, Linux fault-around 로 더 적음), 쓰기가 부모에게 그대로 보인다. 보이는지 여부만 표로 정리한 것이 OS-B09 다.

### Part C — fork 비용과 posix_spawn

**C1 오류 파이프.** exec 성공 여부를 아는 방법은 두 가지가 있다. ① 자식이 `_exit(127)` 하고 부모가 종료 코드를 본다. 하지만 `/bin/sh -c 'exit 127'` 과 구별되지 않는다. ② CLOEXEC 파이프를 쓴다. 정답은 ②다. `fork+exec nonexistent: rc=2 (No such file or directory)` 처럼 posix_spawn 과 **같은 모양**(반환값 = errno)이 된다. 파이프 양 끝에 CLOEXEC 를 거는 것도 중요하다. 그래야 다른 스레드가 동시에 fork 한 자식에게 이 fd 가 새지 않는다(멀티스레드 데몬).

**C2 posix_spawn 의 시그널 마스크.** 시그널 마스크와 "무시(SIG_IGN)" 설정은 fork 를 거쳐, 그리고 **exec 를 거쳐서도** 물려진다(POSIX `execve`: 마스크 유지, 잡는 핸들러만 기본값으로). 데몬이 SIGUSR1 을 막아 둔 채 헬퍼를 띄우면 헬퍼도 SIGUSR1 을 못 받는다. 실측:

```text
  blocked SIGUSR1 in parent: fork+exec child exit 3, posix_spawn(SETSIGMASK) child signal 30
```

fork+exec 자식(셸)은 `kill -USR1 $$` 을 보내도 마스크에 막혀 `exit 3` 까지 갔다. SETSIGMASK 로 마스크를 비운 posix_spawn 자식은 자기 시그널에 죽었다(맥에서 SIGUSR1 = 30, Linux VM 에서는 10 — 같은 테스트가 VM 에서도 통과). `POSIX_SPAWN_SETSIGDEF` 로 SIG_IGN 을 기본값으로 되돌리는 것도 같은 맥락이다(이 레슨에서 테스트하지 않음).

**posix_spawn 실패 직후의 "유령 자식" (이 맥 실측).** 맥에서 없는 파일로 `posix_spawn` 을 부르면 `ENOENT` 가 돌아온 **직후** `waitpid(-1, …, WNOHANG)` 가 0(아직 안 끝난 자식이 있음)을 돌려준다. 100 ms 뒤에는 `ECHILD` 였고, 우리가 거둔 적은 없다. 커널 안에서 만들어졌다가 정리되는 프로세스로 보인다. glibc(VM)는 즉시 `ECHILD` 였다. 그래서 테스트는 "즉시 ECHILD"가 아니라 `no_children_within(1000 ms)` 로 본다. 실측으로는 1 ms 안에 사라졌다(`posix_spawn after failure: no child left after 1 ms`).

<!-- code: demo_bench.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o bench && ./bench 1024 15
# Lima VM: gcc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o bench && ./bench 1024 15
```

이 맥 실측이다(Apple M6, Apple clang 21.0.0, `uptime` load average 8.74 8.53 9.50 — 다른 작업이 도는 공유 기기라서 **비율만** 본다).

```text
pagesize 16384, runs 15, load avg 8.74 8.53 9.50
parent   method            create med   create min    total med    total min
    0 MB fork+_exit            459 us       178 us       770 us       491 us
    0 MB fork+exec             545 us       209 us      2024 us      1863 us
    0 MB vfork+exec            239 us       208 us      1918 us      1742 us
    0 MB posix_spawn           137 us       100 us      1208 us      1136 us
  256 MB fork+_exit           1163 us       761 us      1789 us      1333 us
  256 MB fork+exec            1159 us       767 us      3566 us      2938 us
  256 MB vfork+exec            958 us       626 us      3271 us      2584 us
  256 MB posix_spawn           138 us       110 us      1241 us      1067 us
  256 MB fork+dirty all        918 us       771 us     39703 us     38719 us
 1024 MB fork+_exit           3586 us      2822 us      4115 us      3475 us
 1024 MB fork+exec            3475 us      2856 us      5996 us      5516 us
 1024 MB vfork+exec           3684 us      2892 us      5996 us      5351 us
 1024 MB posix_spawn           150 us       123 us      1329 us      1174 us
 1024 MB fork+dirty all       3102 us      2995 us    149729 us    140418 us

parent write pass over 1024 MB (1 byte/page), median of 5:
  no fork (baseline)                0.4 ms  minflt 0
  after fork (child exited)        68.3 ms  minflt 65536
```

Lima VM 실측이다(Linux 7.0.0-38-generic aarch64, gcc 15.2.0, glibc 2.43, 6 vCPU, load 0.07).

```text
pagesize 4096, runs 15, load avg 0.07 0.05 0.01
parent   method            create med   create min    total med    total min
    0 MB fork+_exit             41 us        36 us        93 us        81 us
    0 MB posix_spawn           117 us        81 us       258 us       215 us
  256 MB fork+_exit           1668 us      1287 us      2774 us      2399 us
  256 MB vfork+exec             95 us        67 us       244 us       185 us
  256 MB posix_spawn            85 us        74 us       196 us       168 us
 1024 MB fork+_exit           4406 us      2890 us      8692 us      5768 us
 1024 MB fork+exec            4350 us      2970 us      8601 us      7441 us
 1024 MB vfork+exec            126 us        92 us       265 us       211 us
 1024 MB posix_spawn           135 us       117 us       255 us       224 us
 1024 MB fork+dirty all       4170 us      3797 us    347260 us    344962 us
(일부 줄 생략 — 전체는 demo 를 돌리면 나온다)
parent write pass over 1024 MB (1 byte/page), median of 5:
  no fork (baseline)                2.8 ms  minflt 0
  after fork (child exited)       138.6 ms  minflt 262144
```

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">자식 만들기가 부모에게 돌아오는 시간 (create 중앙값, µs) — 실측</text>
<line x1="150" y1="40" x2="150" y2="250" stroke="currentColor"/>
<line x1="150" y1="250" x2="730" y2="250" stroke="currentColor"/>
<text x="146" y="266" fill="currentColor" font-size="12">0</text>
<text x="390" y="266" fill="currentColor" font-size="12">2000</text>
<text x="640" y="266" fill="currentColor" font-size="12">4000 µs</text>
<line x1="400" y1="246" x2="400" y2="254" stroke="currentColor"/><line x1="650" y1="246" x2="650" y2="254" stroke="currentColor"/>
<text x="10" y="58" fill="currentColor">맥 fork 0 MB</text><rect x="150" y="46" width="57" height="16" class="f-mut" stroke="currentColor"/><text x="212" y="59" fill="currentColor" font-size="12">459</text>
<text x="10" y="80" fill="currentColor">맥 fork 256 MB</text><rect x="150" y="68" width="145" height="16" class="f-warn" stroke="currentColor"/><text x="300" y="81" fill="currentColor" font-size="12">1163</text>
<text x="10" y="102" fill="currentColor">맥 fork 1 GB</text><rect x="150" y="90" width="448" height="16" class="f-warn" stroke="currentColor"/><text x="603" y="103" fill="currentColor" font-size="12">3586</text>
<text x="10" y="124" fill="currentColor">맥 vfork 1 GB</text><rect x="150" y="112" width="460" height="16" class="f-warn" stroke="currentColor"/><text x="615" y="125" fill="currentColor" font-size="12">3684 (= fork)</text>
<text x="10" y="146" fill="currentColor">맥 spawn 1 GB</text><rect x="150" y="134" width="19" height="16" class="f-ok" stroke="currentColor"/><text x="174" y="147" fill="currentColor" font-size="12">150</text>
<text x="10" y="176" fill="currentColor">VM fork 256 MB</text><rect x="150" y="164" width="208" height="16" class="f-acc" stroke="currentColor"/><text x="363" y="177" fill="currentColor" font-size="12">1668</text>
<text x="10" y="198" fill="currentColor">VM fork 1 GB</text><rect x="150" y="186" width="551" height="16" class="f-acc" stroke="currentColor"/><text x="705" y="199" fill="currentColor" font-size="12">4406</text>
<text x="10" y="220" fill="currentColor">VM vfork 1 GB</text><rect x="150" y="208" width="16" height="16" class="f-ok" stroke="currentColor"/><text x="171" y="221" fill="currentColor" font-size="12">126</text>
<text x="10" y="242" fill="currentColor">VM spawn 1 GB</text><rect x="150" y="230" width="17" height="16" class="f-ok" stroke="currentColor"/><text x="172" y="243" fill="currentColor" font-size="12">135</text>
</svg>
```

**표를 읽는 법.**

| 관찰 | 이유 |
|---|---|
| fork 는 부모 크기에 비례한다(맥 0.46 → 1.16 → 3.6 ms, VM 0.04 → 1.7 → 4.4 ms) | 데이터가 아니라 **매핑**을 복사한다. Linux 는 1 GB = 262144 개 PTE 를 복사하고 부모 PTE 를 쓰기 금지로 바꾼다(PTE 하나에 약 17 ns). 맥은 16 KB 페이지라 65536 개이지만 맵 엔트리·pmap 처리 방식이 달라서 페이지당 약 55 ns 다(내부 분해는 하지 않음) |
| posix_spawn 은 부모 크기와 무관(0.14~0.15 ms) | 맥은 posix_spawn 이 **커널 시스템 콜**이라서 부모 주소 공간을 복제하지 않고 새 프로세스를 만든다(맥 libc 구현 기억, 미확인). glibc 는 `clone3(CLONE_VM|CLONE_VFORK|CLONE_CLEAR_SIGHAND)` 로 부모 메모리를 **빌려 쓰는** 자식을 만들고 곧바로 exec 한다(VM 에서 `strace -f` 로 확인한 줄 그대로) |
| 맥 vfork = fork | 맥 `man vfork`: "As of macOS 12.0, this system call behaves identically to the fork(2) system call, except without calling any handlers registered with pthread_atfork(2)." SDK 헤더는 `__deprecated_msg("Use posix_spawn or fork")` 를 붙인다(그래서 데모는 그 줄만 경고를 끈다). Linux vfork 는 0.13 ms 로 크기와 무관하다 |
| fork+dirty all: 맥 150 ms, VM 347 ms | 미뤄 둔 복사를 전부 치르면 fork+_exit 의 수십 배다(total 중앙값 기준 맥 36 배, VM 40 배). COW 의 이득은 "exec 할 자식은 거의 아무것도 안 쓴다"는 데서 나온다 |
| **부모** 다시 쓰기 0.4 ms → 68 ms (맥), 2.8 → 139 ms (VM) | fork 가 부모 PTE 까지 쓰기 금지로 바꿨기 때문이다. 자식이 이미 죽어서 복사할 필요가 없어도 페이지마다 폴트 한 번(65536 / 262144)을 내야 쓰기 권한이 돌아온다. 제어 루프가 fork 직후 프레임 버퍼를 쓰면 이 비용이 그대로 지연 스파이크가 된다 |

**펌웨어·임베디드 리눅스 연결.**

| 상황 | 무엇을 하나 |
|---|---|
| 큰 데몬이 주기적으로 `system()`/`popen()` | posix_spawn 으로 바꾸거나, 시작할 때 작은 헬퍼 프로세스를 먼저 띄워 두고 파이프로 일을 시킨다(fork 는 작을 때 한 번만) |
| 실시간 루프 + fork | fork 직후 부모의 쓰기 폴트 폭풍(위 68 ms). `mlockall` 을 했다면 Linux 는 fork 때 잠긴 페이지를 다르게 다룰 수 있다(세부 미확인). 실시간 스레드가 있는 프로세스에서는 fork 를 하지 않는 것이 규칙이다(LLAT-A05) |
| Linux `vm.overcommit_memory=2` (엄격 모드) | 쓰기 가능한 PRIVATE 1 GB 를 가진 프로세스의 fork 는 커밋 한도를 1 GB 더 요구할 수 있어 `ENOMEM` 이 난다(기억, VM 에서 재현하지 않음). posix_spawn/vfork 는 이 문제가 없다 |
| MMU 없는 Linux (uClinux, `CONFIG_MMU=n`) | 페이지 테이블이 없으니 COW 도 fork 도 없다. 자식이 부모 메모리를 그대로 빌려 쓰다가 exec/`_exit` 하는 **vfork** 만 쓴다(Linux nommu 문서 기억, 미확인). BusyBox 의 NOMMU 빌드가 이 때문에 fork 를 vfork 로 바꾼다 |
| Cortex-M RTOS | 프로세스가 없다. "태스크 만들기" = 스택과 TCB 할당이고, 격리는 MPU 영역이 맡는다(SYSD-B12, BM-A08) |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution          # 38/38 passed, 0.19 초, 5회 반복 동일
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o so_san && ./so_san   # 38/38, 보고 없음
# Lima VM: gcc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so                                 # 38/38 passed
```

시작 코드와 다른 줄은 TODO 여덟 군데의 몸통뿐이다. 하네스(도우미, 테스트)는 같다. 스레드를 쓰지 않으므로 TSan 빌드는 필요 없다. ASan 빌드의 폴트 수가 1/8 더 많은 이유는 Part A 표에 있다. 실제 출력 중 Part B·C 부분이다.

```text
== Part B: predicted vs measured (child writes a list) ==
  predicted 8 pages, child minflt +8 (rewrite +0)
PASS pred == 2 + 1 + 3 + 2
PASS ok == 0 && pr.d1 >= (long)pred && pr.d1 <= (long)pred + SLACK(pred)
  child READS 256 pages: minflt +257 (second pass +0)
…
== Part C: fork+exec vs posix_spawn ==
  fork+exec   echo: rc=0 status=exit output="hello cow"
  fork+exec   nonexistent: rc=2 (No such file or directory)
  fork+exec   after failure: no child left after 0 ms
  posix_spawn echo: rc=0 status=exit output="hello cow"
  posix_spawn nonexistent: rc=2 (No such file or directory)
  posix_spawn after failure: no child left after 1 ms
PASS count_open_fds() == fds0
  blocked SIGUSR1 in parent: fork+exec child exit 3, posix_spawn(SETSIGMASK) child signal 30
PASS rc2 == 0 && WIFSIGNALED(st2) && WTERMSIG(st2) == SIGUSR1
38/38 passed
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 페이지 간격을 4096 으로 하드코딩 | 이 맥은 16 KB 다. 한 페이지에 4 번 쓰게 되어 폴트가 k/4 로 나오고 "COW 가 이상하다"로 오해한다 | `PG = sysconf(_SC_PAGESIZE)` 하나만 쓴다 |
| 부모에서 `RUSAGE_SELF` 로 자식 폴트를 잰다 | 자식의 폴트는 자식 카운터에 쌓인다. 부모 값은 그대로다 | 자식 안에서 재서 파이프로 보낸다(또는 wait 뒤 `RUSAGE_CHILDREN` 차이, 대신 자식 전부의 합이 된다) |
| 읽기 루프에 volatile 이 없다 | `-O2` 가 쓰이지 않는 읽기를 지운다 → 폴트 0 → "읽기는 공짜" 로 잘못 결론 | `volatile uint8_t *p` 또는 합을 반환해 사용 |
| 테스트에서 폴트 수를 `== k` 로 비교 | 코드 페이지 +1, ASan 그림자 +k/8, libc 등 OS·빌드마다 잡음이 다르다 | `k ≤ d ≤ k + slack` — 하한은 의미론, 상한은 "그 이상 복사 없음" |
| "자식이 읽기만 하면 폴트 0" 이라고 단정 | 이 맥과 arm64 Linux VM 모두 1/page 였다(원인은 다름) | OS 마다 재고, 상한만 체크 |
| 자식에서 `exit()` | 부모에게서 복제된 stdio 버퍼가 비워져 출력이 두 번 나온다. atexit 핸들러도 두 번 돈다 | fork 한 자식은 `_exit()`(fork 전에 `fflush(stdout)` 도) |
| 부모가 파이프 쓰기 끝을 안 닫는다 | 자식이 죽어도 쓰기 끝이 열려 있어 `read` 가 영원히 막힌다 | fork 뒤 부모에서 `close(p[1])` + poll 시간 제한 |
| exec 실패를 종료 코드 127 로만 판단 | 프로그램이 127 로 끝난 것과 구별되지 않는다 | CLOEXEC 오류 파이프로 errno 를 받는다(C1) |
| `posix_spawn` 반환을 `-1`/`errno` 로 검사 | posix_spawn 은 오류 번호를 **반환값**으로 준다. errno 는 건드리지 않을 수 있다 | `int rc = posix_spawn(...); if (rc) strerror(rc)` |
| vfork 자식이 변수를 바꾸거나 함수에서 return | Linux vfork 자식은 부모 메모리·스택을 **공유**한다. return 하면 부모 스택이 망가진다(POSIX 도 exec/`_exit` 외에는 미정의) | vfork 뒤에는 exec 또는 `_exit` 만. 더 나은 선택은 posix_spawn |
| 멀티스레드 프로세스에서 fork 후 malloc/printf | 다른 스레드가 쥐고 있던 libc 락이 자식에서 영원히 잠긴다(연습 2) | exec 전에는 async-signal-safe 함수만(맥 `man fork` CAVEATS). 아니면 posix_spawn |

## 8. 면접 Q&A

<details><summary>Q1. fork 가 정말 부모 메모리를 복사하나요? 어떻게 확인하죠?</summary>

의미론적으로는 복사다. 자식은 부모 주소 공간의 사본을 갖고, 서로의 쓰기를 보지 못한다(POSIX `fork`). 구현은 COW 다. fork 는 매핑(페이지 테이블·맵 엔트리)만 복사하고 양쪽 PTE 를 쓰기 금지로 바꾼다. 누가 쓰면 그 페이지만 폴트 → 복사 → 쓰기 허용이 된다. 확인하는 방법: 자식 안에서 `getrusage(RUSAGE_SELF).ru_minflt` 를 앞뒤로 재면서 k 페이지를 쓴다. 이 맥에서 k = 0..2048 의 기울기는 1.0001, 다시 쓰기는 0, 부모 데이터는 그대로였다. 꼬리질문: "1 바이트를 쓰면 얼마나 복사되나요?" → 한 페이지 통째다. 이 맥은 16 KB, 보통의 x86 Linux 는 4 KB 다. 경계에 걸친 레코드는 2 페이지다(B1).
</details>

<details><summary>Q2. 자식이 읽기만 하면 폴트가 0 인가요?</summary>

교과서 모델에서는 PTE 가 복사되어 있으니 0 이다. 실측은 OS·CPU 마다 달랐다. 이 맥과 arm64 Linux VM 모두 페이지당 1 이었다. Linux VM 에서는 `/proc/self/pagemap` 으로 자식 PTE 가 이미 present 임을 확인했다. 그래서 복사가 아니라 "old" PTE 의 **Access Flag 폴트**로 본다(하드웨어 AF 갱신이 없는 CPU — 추정). 맥은 자식 매핑이 처음에 비어 있다고 보면 설명된다(추정). x86 Linux 는 하드웨어가 A 비트를 세우므로 ~0 으로 예상하지만 재지 않았다. 꼬리질문: "그러면 읽고 쓰는 `x++` 는?" → 이 두 환경에서는 페이지당 2 번(읽기 폴트 + COW 폴트)이었다. 연습 4 의 refcnt 증가가 정확히 그렇다.
</details>

<details><summary>Q3. 1 GB 프로세스의 fork 는 왜 느리고, 무엇에 비례하나요?</summary>

상주(매핑된) 페이지 수에 비례한다. 데이터 양에 비례하는 게 아니다. Linux 는 PTE 를 하나씩 복사하고 부모 PTE 를 쓰기 금지로 바꾸며 페이지 참조 수를 올린다. 1 GB = 262144 개, 실측 4.4 ms 였다. 맥은 16 KB 페이지 65536 개에 3.6 ms 였다. 끝이 아니다. fork 뒤에는 **부모의** 첫 쓰기도 페이지마다 폴트가 나서 1 GB 를 다시 쓰는 데 0.4 → 68 ms(맥), 2.8 → 139 ms(VM)가 됐다. 꼬리질문: "huge page 를 쓰면?" → PTE 수가 줄어 fork 의 테이블 복사는 싸진다. 대신 COW 단위가 커져서(2 MB) 1 바이트 쓰기에 큰 복사가 날 수 있다. Linux 의 THP COW 처리는 버전마다 다르다(세부 미확인, 이 VM 은 THP `madvise` 모드).
</details>

<details><summary>Q4. fork+exec, vfork, posix_spawn 중 무엇을 쓰나요?</summary>

다른 프로그램을 띄우는 것이 목적이면 **posix_spawn** 이다. 부모 크기와 무관하게 빠르고(실측 0.14 ms), 스레드 안전 문제가 없고, 실패를 반환값 errno 로 준다. glibc 는 내부에서 `CLONE_VM|CLONE_VFORK` 를 쓰고(strace 확인), 맥은 커널이 직접 만든다. fork+exec 는 exec 전에 자식에서 임의의 설정(setsid, 자원 제한, chroot…)을 해야 할 때 쓴다. 단, async-signal-safe 함수만 써야 한다. vfork 는 Linux 에서는 빠르지만 자식이 부모 메모리를 공유해서 위험하다. 맥 12+ 에서는 그냥 fork 와 같다(man + 실측 3.7 ms). fork 만 하는 것(exec 없이)은 같은 프로그램의 작업자를 만들 때다(pre-fork 서버, 스냅숏). 꼬리질문: "posix_spawn 으로 못 하는 건?" → 표준 file actions/attr 범위 밖의 설정이다. 그럴 때는 작은 헬퍼 실행 파일을 spawn 해서 그 안에서 설정하고 exec 한다.
</details>

<details><summary>Q5. 스레드가 여러 개인 프로세스에서 fork 하면 무슨 일이 생기나요?</summary>

자식에는 fork 를 부른 스레드 **하나만** 있다. 다른 스레드가 쥐고 있던 뮤텍스(libc 의 malloc 락, stdio 락 포함)는 자식에서 영원히 잠긴 채로 남는다. 연습 2 에서 trylock 이 `EBUSY` 였다. 그래서 POSIX 는 exec 전까지 async-signal-safe 함수만 쓰라고 하고, 맥 `man fork` 의 CAVEATS 도 같은 말을 한다. `pthread_atfork(prepare, parent, child)` 로 fork 직전에 락을 잡았다가 양쪽에서 풀 수 있다(연습 2 에서 확인). 하지만 모든 라이브러리 락을 다 챙길 수는 없다. 그래서 실무 답은 "멀티스레드면 posix_spawn" 이다. 꼬리질문: "맥 vfork 는 atfork 핸들러를 부르나요?" → man vfork 가 "부르지 않는다"고 명시한다.
</details>

<details><summary>Q6. exec 가 실패한 걸 부모는 어떻게 아나요?</summary>

FD_CLOEXEC 를 건 파이프를 fork 전에 만든다. exec 가 성공하면 커널이 쓰기 끝을 닫으므로 부모의 read 는 0(EOF)이다. 실패하면 자식이 errno 를 써서 부모가 4 바이트를 받는다. 그러면 posix_spawn 과 같은 "반환값 = errno" 인터페이스가 된다(C1, 실측 rc = 2 ENOENT). 종료 코드 127 만 보는 방법은 진짜 127 종료와 구별되지 않는다. 꼬리질문: "posix_spawn 은 이걸 어떻게 하죠?" → glibc 는 CLONE_VFORK 자식과 메모리를 공유해서 errno 를 직접 전달하고 실패한 자식은 내부에서 거둔다(VM 에서 즉시 ECHILD). 맥은 실패 직후 잠깐 `waitpid` 가 0 을 돌려주는 자식이 보였다가 1 ms 안에 사라졌다(실측). 그래서 좀비 검사는 짧게 기다리며 한다.
</details>

<details><summary>Q7. MMU 가 없는 시스템(uClinux, Cortex-M)에서는요?</summary>

COW 는 페이지 단위 쓰기 보호와 주소 변환이 있어야 가능하다. 두 프로세스가 같은 가상 주소에 다른 물리 페이지를 갖는 것도 MMU 가 있어야 된다. 그래서 MMU 없는 Linux 에는 fork 가 없고, 부모가 멈춘 채 자식이 부모 메모리를 빌려 쓰다가 exec/`_exit` 하는 vfork 만 있다(nommu 문서 기억, 미확인). 프로그램은 XIP 나 재배치 가능한 형식(FLAT/FDPIC)으로 올라간다. Cortex-M RTOS 에는 프로세스 개념이 없고 태스크 = 스택 + TCB 이며, 보호는 MPU 가 영역 단위로 한다(BM-A08, ARCH-M15). 꼬리질문: "MCU 에서 'COW 같은 것' 을 쓰나요?" → 데이터 구조 수준에서 쓴다. 더블 버퍼 설정 테이블을 포인터로 교체하는 것(OS-A03 의 RCU 와 같은 생각), littlefs 의 copy-on-write 메타데이터(STOR-A03)가 그 예다.
</details>

<details><summary>Q8. fork 를 쓰는 큰 서비스에서 메모리 관련 장애가 나는 이유는?</summary>

① 메모리: fork 뒤 부모와 자식이 계속 쓰면 COW 로 물리 메모리 사용량이 최대 두 배까지 늘 수 있다(fork+dirty all). ② 지연: fork 자체(ms 단위, 부모 멈춤)와 그 뒤 부모의 쓰기 폴트 폭풍이다. ③ overcommit: Linux 엄격 모드에서는 fork 가 ENOMEM 으로 실패할 수 있다. Redis 의 BGSAVE(fork 스냅숏)가 이 세 가지로 잘 알려져 있고, 문서가 THP 를 끄라고 권하는 것도 COW 단위 때문이다(기억, 확인하지 않음). 대책은 쓰기를 모으는 메모리 배치(연습 4: AoS → SoA 로 폴트 16 배 감소), 상속 제외(`MADV_DONTFORK`, 맥 `minherit` — 연습 3), 그리고 exec 가 목적이면 posix_spawn 이다. 꼬리질문: "Python 서버에서 fork 뒤 메모리가 계속 느는 이유?" → 객체를 읽기만 해도 참조 카운트를 **쓰므로** 페이지가 복사된다. Instagram 이 이 문제로 GC 를 조정했고 CPython 3.7 에 `gc.freeze()` 가 생겼다고 알려져 있다(세부 미확인).
</details>

## 9. 추가 연습

### 연습 1 — 누가 폴트를 내나

부모가 1024 페이지를 mmap 하고 앞 절반만 써 둔다. (a) 자식이 **안 건드린** 뒤 절반 200 페이지에 쓰면? (b) fork 를 한 번 하고 자식이 끝난 뒤, 부모가 앞 200 페이지에 처음 쓰면? (c) 같은 페이지에 다시 쓰면? (d) 부모가 다른 200 페이지를 읽기만 하면?

<details><summary>답</summary>

(a) 201 — COW 가 아니라 **demand-zero**(OSTEP 23.5)다. 처음 쓰는 익명 페이지는 0 으로 채운 새 프레임을 받는다. 부모 쪽은 여전히 0 이다(그리고 그 읽기가 부모의 첫 접근이다). (b) 200 — COW 보호는 양쪽에 걸리고, 자식이 죽었다고 풀리지 않는다. 다음 쓰기 폴트 때 "공유자가 없으니 복사 없이 쓰기 허용"으로 처리된다(Linux `wp_page_reuse` 류, 이름은 기억). (c) 0. (d) 0 — 부모 PTE 는 present 이고 old 로 만들지 않는다. 이 맥 실측: `child writes 200 never-touched pages: +201`, `parent after fork: first write +200, rewrite +0, read-only +0`. VM 도 같았다.
</details>

### 연습 2 — 스레드가 쥔 뮤텍스와 fork

다른 스레드가 `M1` 을 쥐고 있는 동안 fork 하면 자식에서 `pthread_mutex_trylock(&M1)` 은? `pthread_atfork` 로 `M2` 를 보호하면?

<details><summary>답</summary>

`M1` 은 `EBUSY` 다. 쥔 스레드가 자식에 없으니 아무도 풀 수 없다. 테스트는 lock 대신 trylock 을 써서 멈추지 않게 했다. `M2` 는 prepare 가 fork 직전에 `lock(M2)` 한다. 이때 다른 스레드가 놓을 때까지(50 ms) 기다리게 되고, parent/child 핸들러가 각자 unlock 하므로 자식에서 잡힌다(`with atfork: child trylock -> ok`, 맥·VM 모두). 자식 핸들러의 unlock 이 맥에서도 0 을 돌려주는지 따로 확인했다. 모든 라이브러리의 락을 이렇게 챙길 수는 없으므로 결론은 "멀티스레드 프로세스는 posix_spawn" 이다.
</details>

### 연습 3 — 상속 제어: minherit / MADV_DONTFORK

비밀 키나 DMA 버퍼처럼 자식에게 물려주면 안 되는 영역이 있다. 맥에서 `minherit(p, len, VM_INHERIT_NONE)` 을 하면 자식이 그 영역을 읽을 때 어떻게 되나? `VM_INHERIT_SHARE` 를 하면?

<details><summary>답</summary>

NONE: 자식에는 그 매핑이 없다. 접근하면 `SIGSEGV` 다(실측 signal 11, 맥·VM 모두. Linux 는 `madvise(MADV_DONTFORK)`). ASan 빌드에서는 ASan 의 SEGV 핸들러가 보고서를 찍고 exit 1 로 끝내므로, 자식에서 `signal(SIGSEGV, SIG_DFL)` 로 되돌려야 신호로 끝난다. SHARE: `MAP_PRIVATE` 로 만든 영역이 fork 뒤에는 부모·자식 **공유**가 된다(`SHARE: parent sees child's write: yes`). Linux 에는 같은 것이 없고, 대신 `MADV_WIPEONFORK`(자식에는 0 으로 채운 영역)가 있어 VM 에서는 그걸 확인한다. RDMA/VFIO 처럼 물리 주소가 고정된 DMA 버퍼를 fork 와 함께 쓸 때 DONTFORK 가 필요하다고 알려져 있다(세부 미확인).
</details>

### 연습 4 — COW 친화 배치: AoS vs SoA

64 B 레코드 65536 개(4 MB)의 `refcnt` 를 자식이 전부 하나씩 올린다. 레코드 배열(AoS)과 카운터만 따로 모은 배열(SoA)의 폴트 수를 예측하고 재 보라.

<details><summary>답</summary>

예측(건드린 페이지 수): AoS = 4 MB / 16 KB = 256, SoA = 256 KB / 16 KB = 16 으로 16 배 차이다. 실측은 `AoS: predicted 256 pages, child +512 | SoA: predicted 16 pages, child +32` 였다. 둘 다 정확히 2 배인 이유는 `refcnt++` 가 **읽고 쓰기**라서 페이지당 읽기 폴트 + COW 폴트가 나기 때문이다(§5 B 그림, VM 도 2048 vs 128). 실무에서는 fork 뒤 자식(스냅숏·작업자)이 자주 쓰는 필드를 따로 모아 두면 복사되는 메모리와 폴트가 같이 준다. 반대로 "읽기만 하는" 줄 알았던 코드가 참조 카운트나 캐시 통계를 쓰면 COW 가 조용히 일어난다(Q8).
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o practice && ./practice        # 21/21 passed (VM: 20/20)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread practice.c -o pr_san && ./pr_san   # 21/21, 보고 없음
```

`practice.c` 는 스레드를 쓰므로 첫 줄에 `/* BUILD: -pthread */` 를 두었다. TSan 빌드(`-fsanitize=thread`)는 경고 0 이었지만 폴트 체크 5 개가 실패했다(16/21). TSan 의 그림자 메모리가 앱 페이지마다 폴트를 추가로 내서(예: AoS 512 → 1579) 폴트 수 실험에는 맞지 않는다. 그래서 TSan 은 레이스 확인용으로만 돌렸다. VM 에서는 연습 3 의 SHARE 대신 WIPEONFORK 를 체크하므로 체크 수가 20 개다.

## 10. 핵심 정리

- fork = **매핑 복사 + 양쪽 쓰기 금지**. 데이터는 쓰는 순간 페이지 단위로 복사된다(COW). 자식이 k 페이지를 쓰면 `ru_minflt` 가 ≈ k 만큼 늘고(실측 기울기 1.0001), 다시 쓰면 0 이다.
- 단위는 **페이지**다(이 맥 16 KB, `sysconf(_SC_PAGESIZE)`). 경계에 걸친 쓰기는 2 페이지, 1 바이트도 한 페이지 통째다.
- 읽기 폴트는 OS·CPU 마다 다르다. 이 맥과 arm64 Linux VM 은 페이지당 1(맥: 빈 매핑 추정, Linux: old PTE + AF 폴트), 읽고 쓰기는 2 였다. 테스트는 `k ≤ d ≤ k + slack` 처럼 범위로 쓴다.
- fork 비용은 상주 페이지 수에 비례한다(1 GB: 맥 3.6 ms, VM 4.4 ms). fork 뒤 **부모의 첫 쓰기도 폴트**다(1 GB 다시 쓰기 0.4 → 68 ms).
- 다른 프로그램을 띄울 때는 posix_spawn 을 쓴다(크기 무관 0.14 ms, 스레드 안전, errno 반환, file actions + SETSIGMASK). glibc 는 CLONE_VM|CLONE_VFORK 를 쓰고, 맥 vfork 는 12 부터 fork 와 같다.
- fork+exec 를 쓸 때: 자식은 `_exit`, exec 실패는 CLOEXEC 파이프, 멀티스레드면 exec 전 async-signal-safe 만, 마스크는 exec 를 넘어 물려진다.
- MMU 가 없으면 fork 도 COW 도 없다(uClinux 는 vfork). MCU 의 "COW" 는 데이터 구조 수준이다(포인터 교체, littlefs).

## 참고자료

- OSTEP (Arpaci-Dusseau) 5장 *Interlude: Process API* (fork/exec/wait), 21.3·21.5 *The Page Fault*·*Page Fault Control Flow*, 23.5 *Other Neat VM Tricks* (demand zeroing, copy-on-write, TENEX [BB+72]): <https://pages.cs.wisc.edu/~remzi/OSTEP/> (로컬 `operating-system-three-easy-pieces/book-md/C05_interlude_process_api.md`, `C21_*.md`, `C23_the_vax_vms_virtual_memory_system.md` 로 절 번호 확인)
- POSIX.1-2017 `fork` (주소 공간 사본, 멀티스레드 제약), `exec` (시그널 마스크 유지), `posix_spawn` / `posix_spawn_file_actions_adddup2` / `posix_spawnattr_setsigmask`, `getrusage`, `pthread_atfork`: <https://pubs.opengroup.org/onlinepubs/9699919799/functions/fork.html>, <https://pubs.opengroup.org/onlinepubs/9699919799/functions/exec.html>, <https://pubs.opengroup.org/onlinepubs/9699919799/functions/posix_spawn.html>, <https://pubs.opengroup.org/onlinepubs/9699919799/functions/getrusage.html>, <https://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_atfork.html>
- macOS man pages (이 맥에서 읽음): `fork(2)` (CAVEATS: exec 전 async-signal-safe), `vfork(2)` ("As of macOS 12.0 … identically to fork(2)", deprecated), `getrusage(2)` (`ru_minflt` = page reclaims), `posix_spawn(2)` (반환값 = 오류 번호, ENOENT), `minherit(2)` 와 `<mach/vm_inherit.h>` 의 `VM_INHERIT_SHARE/COPY/NONE`
- Linux man-pages: `fork(2)`, `vfork(2)`, `posix_spawn(3)`, `madvise(2)` (`MADV_DONTFORK`, `MADV_WIPEONFORK`), `proc_pid_pagemap(5)`: <https://man7.org/linux/man-pages/man2/fork.2.html>, <https://man7.org/linux/man-pages/man3/posix_spawn.3.html>, <https://man7.org/linux/man-pages/man2/madvise.2.html>, <https://www.kernel.org/doc/html/latest/admin-guide/mm/pagemap.html>
- A. Baumann, J. Appavoo, O. Krieger, T. Roscoe, "A fork() in the road", HotOS 2019 — fork 의 비용과 posix_spawn 권고 (기억 기반, 로컬 OSTEP 판에는 인용 없음)
- Linux 커널 `mm/memory.c` (`copy_present_pte`, `do_wp_page`), `Documentation/admin-guide/mm/nommu-mmap.rst` (no-MMU) — 기억 기반, 이 레슨에서 소스 버전을 확인하지 않음
- 관련 레슨: OS-B01 (fork/exec/wait, 127 관례), OS-B07 (시그널 마스크), OS-B09 (mmap 가시성 표), OS-B10 (페이지 테이블), OS-M01 (페이지 테이블 관리·TLB 무효화), OS-A06 (mprotect + SIGSEGV 로 사용자 공간 폴트 처리), OS-A10 (xv6 COW fork 구현), OS-A03 (RCU), OS-M17 (프로세스 공유 뮤텍스), ARCH-A01/A02/A03 (워커·TLB·폴트 비용 실측), RISCV-M12 (A/D 비트 소프트웨어 관리), LPERF-B09 (minor vs major 폴트 프로파일링), LLAT-A05 (폴트로 인한 꼬리 지연), SYSD-B12 (MPU vs MMU 격리), BM-A08 / ARCH-M15 (MPU), STOR-A03 (littlefs CoW)
