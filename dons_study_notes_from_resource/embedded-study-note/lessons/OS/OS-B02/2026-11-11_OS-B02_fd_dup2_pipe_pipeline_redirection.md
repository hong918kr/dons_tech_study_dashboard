---
id: OS-B02
time: 70분
updated: 2026-11-11
---
# OS-B02 파일 디스크립터·dup2·파이프: 파이프라인과 리다이렉션

## 학습 목표

- 파일 디스크립터(file descriptor, fd) → 열린 파일 항목(open file description) → vnode/파이프 객체의 **3단 구조** 를 그리고, `fork` / `dup2` / `open` 이 각각 어느 층을 복사·공유하는지 실험으로 확인한다 (오프셋 공유 포함).
- `pipe` + `fork` + `dup2` + `close` + `execvp` 로 `a | b | c` **N단 파이프라인** 을 짜고, 자식·부모가 각각 **어떤 fd 를 언제 닫는지** 를 표로 설명한다.
- `<`, `>`, `2>&1` 리다이렉션을 **왼쪽부터 순서대로**, 파이프 연결 **뒤에** 적용해서 `> f 2>&1` 과 `2>&1 > f` 가 왜 다른지 bash 와 같은 결과로 보인다.
- "닫지 않은 쓰기 끝 → 읽는 쪽이 EOF 를 못 봄 → 영원히 대기" 버그를 **3가지 모양** 으로 재현하고(`demo_hang.c`, 감시자로 끊음), `lsof` 로 누가 그 끝을 쥐고 있는지 찾아 고친다.
- 데몬이 자식 파이프라인에 입력을 먹이고 출력을 모을 때의 함정(쓰기 끝 닫기, `poll` 로 동시 진행, `EPIPE`/`SIGPIPE`, `FD_CLOEXEC`)을 코드로 처리한다.

## 1. 문제

"`ls | wc -l` 을 셸이 어떻게 만드는지 코드로 짜 보세요. `wc` 가 끝나지 않는다면 왜죠?" — 임베디드 리눅스·플랫폼 면접에서 `fork`/`exec`(OS-B01) 다음에 거의 반드시 나오는 질문이다. 펌웨어 쪽에서도 매일 만난다: 보드 bring-up 스크립트의 `i2cget … | awk …`, 데몬이 `gzip`·`logger` 를 자식으로 띄워 파이프로 데이터를 넘기는 일, 생산 테스트(factory test) 하네스가 측정 툴의 stdout/stderr 를 모으는 일, systemd 가 서비스의 stdout 을 저널로 잇는 일(ELX-B13). 그리고 "가끔 테스트가 영원히 안 끝난다" 의 흔한 원인이 이 레슨의 **새는 쓰기 끝** 이다.

핵심은 fd 가 **번호일 뿐** 이라는 것이다. 프로세스마다 fd 표가 있고, 각 칸은 커널의 **열린 파일 항목** 을 가리킨다. 그 항목이 오프셋·플래그와 실제 대상(파일 vnode, 파이프 버퍼)을 가진다. `fork` 는 fd **표** 를 복사하고(항목은 공유), `dup2(old, new)` 는 내 표의 `new` 칸이 `old` 와 같은 항목을 가리키게 하고, `open` 은 새 항목을 만든다.

```svg
<svg viewBox="0 0 740 340" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">fd 표(프로세스별) → 열린 파일 항목(커널 전역) → vnode / 파이프 객체</text>
  <text x="20" y="48" fill="currentColor">부모 (셸)</text>
  <g stroke="currentColor">
    <rect x="20" y="56" width="110" height="26" class="f-mut"/><rect x="20" y="82" width="110" height="26" class="f-mut"/>
    <rect x="20" y="108" width="110" height="26" class="f-mut"/><rect x="20" y="134" width="110" height="26" class="f-acc"/>
  </g>
  <text x="28" y="74" fill="currentColor" font-family="monospace">0 stdin</text>
  <text x="28" y="100" fill="currentColor" font-family="monospace">1 stdout</text>
  <text x="28" y="126" fill="currentColor" font-family="monospace">2 stderr</text>
  <text x="28" y="152" fill="currentColor" font-family="monospace">3 log.txt</text>
  <text x="20" y="196" fill="currentColor">자식 (fork 직후 + dup2(3, 1))</text>
  <g stroke="currentColor">
    <rect x="20" y="204" width="110" height="26" class="f-mut"/><rect x="20" y="230" width="110" height="26" class="f-warn"/>
    <rect x="20" y="256" width="110" height="26" class="f-mut"/><rect x="20" y="282" width="110" height="26" class="f-acc"/>
  </g>
  <text x="28" y="222" fill="currentColor" font-family="monospace">0 stdin</text>
  <text x="28" y="248" fill="currentColor" font-family="monospace">1 → log.txt</text>
  <text x="28" y="274" fill="currentColor" font-family="monospace">2 stderr</text>
  <text x="28" y="300" fill="currentColor" font-family="monospace">3 log.txt</text>
  <rect x="300" y="60" width="200" height="56" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="310" y="80" fill="currentColor">항목 T (터미널)</text>
  <text x="310" y="100" fill="currentColor" font-family="monospace">off -, O_RDWR, ref=5</text>
  <rect x="300" y="150" width="200" height="56" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="310" y="170" fill="currentColor">항목 L (log.txt)</text>
  <text x="310" y="190" fill="currentColor" font-family="monospace">off=4096, O_WRONLY, ref=3</text>
  <rect x="300" y="240" width="200" height="56" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="310" y="260" fill="currentColor">항목 L2 (log.txt 를 또 open)</text>
  <text x="310" y="280" fill="currentColor" font-family="monospace">off=0 (따로!), ref=1</text>
  <rect x="580" y="60" width="140" height="44" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="590" y="87" fill="currentColor">tty 장치</text>
  <rect x="580" y="190" width="140" height="44" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="590" y="217" fill="currentColor">vnode log.txt</text>
  <g stroke="currentColor" fill="none">
    <path d="M130 69 L300 80"/><path d="M130 95 L300 86"/><path d="M130 121 L300 92"/>
    <path d="M130 147 L300 172"/><path d="M130 217 L300 98"/><path d="M130 243 L300 178"/>
    <path d="M130 269 L300 104"/><path d="M130 295 L300 184"/>
    <path d="M500 88 L580 82"/><path d="M500 178 L580 206"/><path d="M500 268 L580 218"/>
  </g>
  <text x="515" y="290" fill="currentColor">fork: 표 복사, 항목 공유</text>
  <text x="515" y="308" fill="currentColor">dup2: 두 칸 → 한 항목</text>
  <text x="515" y="326" fill="currentColor">open: 새 항목, 오프셋 따로</text>
</svg>
```

파이프(pipe)는 커널 안의 단방향 바이트 큐이고, `pipe(p)` 는 읽기 끝 `p[0]`, 쓰기 끝 `p[1]` 두 항목을 준다. 읽는 쪽 `read()` 가 0(EOF)을 받는 조건은 **"버퍼가 비었고, 쓰기 끝을 연 fd 가 시스템 전체에 하나도 없다"** 이다 (POSIX `read`, macOS `man 2 pipe`: "Widowing a pipe is the only way to deliver end-of-file to a reader"). 쓰기 끝 fd 하나라도 어딘가(부모, 형제, 심지어 읽는 자신)에 남아 있으면 EOF 는 오지 않는다.

### Part A — N단 파이프라인 `a | b | c`

`int spawn_pipeline(const pipeline_t *pl, int in_fd, int out_fd, pid_t *pids)` 를 구현한다.

- 단계 n 개 → 파이프 n-1 개, `fork` n 번. 첫 단계 stdin = `in_fd`, 마지막 단계 stdout = `out_fd` (셸이라면 0 과 1).
- 자식: `dup2` 로 0/1 연결 → **쓰지 않는 파이프 fd 전부 close** → `SIGPIPE` 기본값 복원 → `apply_redirs` (Part B) → `execvp`. 실패하면 `mysh: <cmd>: command not found` + `_exit(127)`.
- 부모: 자기가 만든 파이프 fd 를 전부 닫고 pid 만 돌려준다 (기다리는 건 하네스의 `wait_all`).
- 테스트: `printf 'b\na\n' | sort | head -n 1` → `a`, 4단·8단, 종료 코드 = 마지막 단계(`true | false` → 1), 가운데 단계 command-not-found → 127, `yes | head -n 1` 이 끝나는지(=`yes` 가 SIGPIPE 로 죽는지, 141), 부모 fd 개수가 실행 전후 같은지, **각 단계 자식에게 0/1/2 외의 fd 가 없는지** (`sh -c` 로 fd 3..9 를 탐침).

### Part B — 리다이렉션 `<`, `>`, `2>&1`

`int apply_redirs(const stage_t *st)` — 자식 안에서, 파이프 연결 **뒤에**, 파서가 준 순서대로 적용한다.

- `< path` → `open(O_RDONLY)` → fd 0, `> path` → `open(O_WRONLY|O_CREAT|O_TRUNC, 0644)` → fd 1, `2>&1` → `dup2(1, 2)` ("**지금** 1 이 가리키는 것" 을 2 에 복사).
- 테스트: `sort < in`, `… | head -n 1 > out`, `O_TRUNC`(짧은 내용으로 덮어쓰면 꼬리가 남지 않음), `2>&1 | sort` 로 stderr 도 파이프로, **`> f 2>&1` vs `2>&1 > f`**, `printf a > f | cat`(리다이렉션이 파이프를 이김), 없는 파일 → 코드 1, open 한 fd 를 닫고 exec 했는지 탐침.

### Part C — 데몬: 파이프라인에 입력 먹이고 출력 모으기

`int pipe_through(pl, in, inlen, out, cap, &outlen, codes)` — 센서 로그를 `sort | head` 로 거르거나, 큰 버퍼를 `gzip` 에 통과시키는 데몬의 모양 (`popen` 을 양방향으로 확장한 것).

- 입력을 다 쓰면 **쓰기 끝을 닫는다** (안 닫으면 `sort`/`wc` 는 끝나지 않는다).
- 쓰기와 읽기를 **`poll()` 로 함께** 한다 (1 MB 를 다 쓰고 나서 읽으면 양쪽이 서로 막힌다 — 실측 147456 B 에서 멈춤).
- 파이프라인이 입력을 다 안 읽고 끝나면(`head -c 10`) `write` 가 `EPIPE` → 쓰기를 멈추고 계속 읽는다.
- 부모 쪽 끝(입력 쓰기 끝, 출력 읽기 끝)은 `FD_CLOEXEC` — 안 그러면 첫 단계가 **자기 입력의 쓰기 끝** 을 물려받아 EOF 가 영원히 안 온다.
- 제한 시간 2 초, 넘으면 자식을 죽이고 `HUNG`.

## 2. Problem (English)

> **Mini shell, part 2: pipelines and redirection.**
>
> Given a parsed command line (`pipeline_t`: up to 8 stages, each with `argv` and an ordered list of redirections), implement:
>
> ```c
> int spawn_pipeline(const pipeline_t *pl, int in_fd, int out_fd, pid_t *pids); // fork+exec all stages, return #spawned
> int apply_redirs(const stage_t *st);   // in the child: apply "< f", "> f", "2>&1" left to right; 0 / -1
> int pipe_through(const pipeline_t *pl, const char *in, size_t inlen,
>                  char *out, size_t cap, size_t *outlen, int *codes); // feed stdin, collect stdout
> ```
>
> Requirements: stage *i*'s stdout is connected to stage *i+1*'s stdin; redirections are applied after the pipe wiring and in order (so `cmd 2>&1 > f` differs from `cmd > f 2>&1`, as in POSIX sh); no stage and not the parent may keep a pipe end it does not use; the exit code of a pipeline is that of the last stage (signal *s* → 128+*s*); a missing command gives 127. `pipe_through` must not deadlock for inputs larger than the pipe buffer, must deliver EOF to the pipeline, and must survive a pipeline that stops reading early.
>
> Example: `printf 'b\na\n' | sort | head -n 1` → `"a\n"`, exit 0. `sh -c 'echo out; echo err >&2' 2>&1 > f` → stdout `"err\n"`, file `"out\n"`.
>
> Follow-ups: Why does `wc` hang if the parent forgets `close(p[1])`? How do you find the leaking process? What does `fork` share with the parent — the fd or the file offset? Why does `yes | head -1` terminate? What is `FD_CLOEXEC` for, and why is `fcntl` after `pipe` racy in a threaded program? How big is a pipe and what does `PIPE_BUF` guarantee?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

처음 실행 결과 (실제 출력 일부, 0.14 초에 끝남 — 멈추지 않는다):

```text
FAIL starter.c:412  OUT_IS("hello\n") && g.rc == 0
FAIL starter.c:414  OUT_IS("a\n") && g.rc == 0
...
FAIL starter.c:449  OUT_IS("") && file_is(g_out, "out\nerr\n")
FAIL starter.c:451  OUT_IS("err\n") && file_is(g_out, "out\n")
...
FAIL starter.c:478  g.rc == 0 && OUT_IS("s2,7\ns4,7\n")
FAIL starter.c:480  g.rc == 0 && OUT_IS("open:\n") && g.fd_delta == 0
5/31 passed
```

통과하는 5개는 하네스 확인(토크나이저·파서 3개)과 "아무것도 안 했으니 fd 도 안 샜다" 2개다. 하네스 설계:

- **토크나이저·파서는 주어진다** (OS-B01 이 토크나이저 레슨). 공백 분리, `'작은따옴표'`, 연산자 `|` `<` `>` `2>&1` (붙여 써도 됨). 큰따옴표·`$변수`·글로브·`>>`·`2>file` 은 지원하지 않는다. 리다이렉션은 단계마다 **나온 순서대로** `r[]` 에 담긴다.
- **막힐 수 있는 곳은 전부 제한 시간이 있다.** `run_capture()` 는 출력 파이프를 `poll()` 로 읽다가 2 초가 지나면 `wait_all(…, 0)` 으로 전부 `SIGKILL` 하고 `HUNG` 을 돌려준다. Part C 는 `guarded_pipe_through()` 가 **감시 대상 프로세스** 를 하나 더 띄워 그 안에서 실행하고, 4 초 안에 결과가 안 오면 프로세스 그룹째 죽인다 — 내 `pipe_through` 가 `write` 에서 영원히 막혀도 테스트는 FAIL 로 끝난다. 마지막 안전망 `alarm(90)`.
- **하네스가 만든 fd 는 전부 `FD_CLOEXEC`** (캡처 파이프, `/dev/null`). 그래서 자식 탐침에 보이는 여분 fd 는 **내가 만든 파이프** 뿐이다.
- 탐침(`PROBE`): `sh -c` 가 fd 3..9 각각에 `(: >&$f)` 를 시도해 열린 번호를 `open: 3 5` 처럼 출력한다. 정답은 `open:` (아무것도 없음).
- 작업 파일은 `/tmp/OS-B02_work.<pid>/` 에 만들고 끝에 지운다. 데이터 파일은 필요 없다 — 어느 디렉터리에서 실행해도 된다.
- `main` 은 `signal(SIGPIPE, SIG_IGN)` 으로 시작한다 (Part C 에서 독자가 떠난 파이프에 써도 하네스가 죽지 않게). 이 설정이 **자식에게 상속된다** 는 것이 Part A 의 함정 하나다.

관찰용 파일:

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_hang.c -o demo_hang && ./demo_hang        # hang 4가지 + 고친 것
./demo_hang lsof                                                                      # 막힌 순간 lsof 로 fd 표 출력
```

## 4. 힌트

<details>
<summary>힌트 1 — 파이프 하나, 자식 둘: 누가 무엇을 닫나</summary>

`a | b` 하나만 먼저 생각하자. `pipe(p)` 뒤 `fork` 두 번이면 **세 프로세스가 p[0], p[1] 을 모두** 갖고 있다. `a` 는 `dup2(p[1], 1)` 후 p[0], p[1] 둘 다 닫는다. `b` 는 `dup2(p[0], 0)` 후 둘 다 닫는다. 부모도 둘 다 닫는다. "dup2 했으니 원본은 필요 없다" 가 규칙이다.

</details>

<details>
<summary>힌트 2 — N단은 "이전 읽기 끝" 하나만 들고 루프를 돈다</summary>

모든 파이프를 미리 만들면 모든 자식이 2(n-1) 개를 다 물려받아서 닫을 게 많다. 대신 루프에서 `prev`(이전 파이프의 읽기 끝)만 들고 다니자: 단계 i 는 `prev` 를 0 으로, 새 파이프 `p[1]` 을 1 로. 부모는 fork 직후 `prev` 와 `p[1]` 을 닫고 `prev = p[0]`. 이러면 부모가 동시에 쥐는 파이프 fd 는 최대 3 개.

</details>

<details>
<summary>힌트 3 — `2>&1` 은 "1 의 현재 대상" 을 복사한다</summary>

`dup2(1, 2)` 는 그 순간 fd 1 이 가리키는 항목을 2 에도 걸 뿐, 나중에 1 이 바뀌어도 2 는 따라가지 않는다. 그래서 파이프 연결(1 = 파이프) → `2>&1`(2 = 파이프) → `> f`(1 = 파일) 순서면 stderr 는 파이프에 남는다. 순서대로 적용하기만 하면 bash 와 같다.

</details>

<details>
<summary>힌트 4 — <code>yes | head -n 1</code> 이 안 끝나거나 141 이 아니라면</summary>

`exec` 는 **잡은(caught) 시그널은 기본값으로 되돌리지만, 무시(SIG_IGN)한 시그널은 무시한 채로 넘긴다** (POSIX `exec`). 하네스가 SIGPIPE 를 무시했으니 `yes` 도 무시한 채 시작한다 → `head` 가 끝나도 `yes` 는 죽지 않고 `EPIPE` 를 받는다. 자식에서 `signal(SIGPIPE, SIG_DFL)`.

</details>

<details>
<summary>힌트 5 — Part C: 내가 만든 파이프의 "내 쪽 끝" 이 자식에게 샌다</summary>

`spawn_pipeline` 은 `in_fd`/`out_fd` 원본은 닫아 주지만, 부모가 쓸 입력 파이프 **쓰기 끝** 은 그 함수가 모르는 fd 다. fork 하면 모든 자식이 그것을 물려받고, 첫 단계는 자기 stdin 의 쓰기 끝을 쥔 채 EOF 를 기다린다. `fcntl(fd, F_SETFD, FD_CLOEXEC)` 를 걸면 exec 순간 자동으로 닫힌다. 그리고 쓰기 끝은 `O_NONBLOCK` 으로 두고 `poll()` 이 "쓸 수 있음" 이라 할 때 쓸 만큼만 쓴다.

</details>

## 5. 풀이

### Part A — 배선: 단계 i 는 무엇을 0/1 로 받고 무엇을 닫나

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">printf 'b\na\n' | sort | head -n 1  — 파이프 2개, 자식 3개</text>
  <rect x="20" y="50" width="150" height="60" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="30" y="72" fill="currentColor">단계 0: printf</text>
  <text x="30" y="94" fill="currentColor" font-family="monospace">0=in_fd 1=p0[1]</text>
  <rect x="295" y="50" width="150" height="60" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="305" y="72" fill="currentColor">단계 1: sort</text>
  <text x="305" y="94" fill="currentColor" font-family="monospace">0=p0[0] 1=p1[1]</text>
  <rect x="570" y="50" width="150" height="60" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="580" y="72" fill="currentColor">단계 2: head</text>
  <text x="580" y="94" fill="currentColor" font-family="monospace">0=p1[0] 1=out_fd</text>
  <rect x="190" y="66" width="85" height="28" class="f-mut" stroke="currentColor"/>
  <text x="208" y="85" fill="currentColor">pipe0</text>
  <rect x="465" y="66" width="85" height="28" class="f-mut" stroke="currentColor"/>
  <text x="483" y="85" fill="currentColor">pipe1</text>
  <g stroke="currentColor" stroke-width="2" fill="none">
    <path d="M170 80 L188 80"/><path d="M275 80 L293 80"/><path d="M445 80 L463 80"/><path d="M550 80 L568 80"/>
  </g>
  <text x="20" y="140" fill="currentColor" font-weight="bold">부모 루프 (prev = 이전 읽기 끝)</text>
  <text x="20" y="162" fill="currentColor" font-family="monospace">i=0: pipe(p0) fork  close(p0[1])  prev=p0[0]</text>
  <text x="20" y="182" fill="currentColor" font-family="monospace">i=1: pipe(p1) fork  close(prev)   close(p1[1])  prev=p1[0]</text>
  <text x="20" y="202" fill="currentColor" font-family="monospace">i=2:          fork  close(prev)</text>
  <text x="20" y="230" fill="currentColor" font-weight="bold">pipe0 쓰기 끝을 쥔 프로세스 = 단계 0 하나뿐이어야 sort 가 EOF 를 본다</text>
  <text x="20" y="252" fill="currentColor">V1 부모가 안 닫음 · V2 sort 가 물려받은 p0[1] 을 안 닫음 · V3 head(단계 2)가 fork 때 물려받은 p0 을 쥠</text>
  <rect x="20" y="266" width="700" height="26" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="30" y="284" fill="currentColor">셋 중 하나만 있어도 sort 는 영원히 read() 에서 잔다 — 그리고 head, 셸도 줄줄이 기다린다</text>
</svg>
```

정답의 자식 부분:

```c
if (pid == 0) {
    int rd = (i == 0) ? in_fd : prev;
    int wr = last ? out_fd : p[1];
    if (rd != STDIN_FILENO)  dup2(rd, STDIN_FILENO);
    if (wr != STDOUT_FILENO) dup2(wr, STDOUT_FILENO);
    if (!last) { close(p[0]); close(p[1]); }   /* p[0]: 다음 단계 것, p[1]: 이미 1 에 복제 */
    if (prev >= 0) close(prev);
    if (in_fd > STDERR_FILENO)  close(in_fd);
    if (out_fd > STDERR_FILENO) close(out_fd);
    signal(SIGPIPE, SIG_DFL);
    if (apply_redirs(&pl->st[i]) < 0) _exit(1);
    execvp(pl->st[i].argv[0], pl->st[i].argv);
    ...  _exit(errno == ENOENT ? 127 : 126);
}
```

| 줄 | 이유 |
|---|---|
| `if (rd != 0) dup2` | 셸처럼 `in_fd == 0` 으로 부르면 `dup2(0,0)` 후 `close(0)` 하는 실수를 막는다. (`dup2(fd, fd)` 자체는 아무것도 안 닫고 fd 를 돌려준다 — POSIX, 연습 1) |
| 원본 close | `dup2` 는 **복사** 다. 원본 p[1] 이 남아 있으면 이 자식이 끝나기 전까지 다음 단계는 EOF 를 못 본다. 특히 `p[0]` 은 다음 단계의 것인데 fork 로 같이 물려받았다 |
| `_exit`, `exit` 아님 | fork 한 자식이 `exit` 하면 부모에게서 복사된 stdio 버퍼가 **두 번** 출력되고 `atexit` 핸들러가 돈다 |
| `SIGPIPE` → `SIG_DFL` | 무시한 시그널은 exec 너머로 상속된다 (아래 실측) |
| 127 / 126 | 셸 관례: 못 찾음 127, 찾았지만 실행 불가 126. 리다이렉션 실패 1 (OS-B01 의 상태 분류와 같은 표) |

**부모가 기다리지 않는 이유:** 파이프라인 단계는 **동시에** 돌아야 한다. 단계 0 을 띄우고 `waitpid` 하면, 출력이 파이프 버퍼(64 KB, 연습 5)보다 클 때 단계 0 은 아무도 읽지 않는 파이프에 막혀 영원히 안 끝난다.

**종료 코드:** POSIX 셸에서 파이프라인의 코드는 마지막 명령의 코드다(`false | true` → 0). bash 의 `PIPESTATUS` 는 단계별 배열이고, `set -o pipefail` 은 0 이 아닌 것 중 가장 오른쪽을 쓴다 (연습 3).

### Part A — `yes | head -n 1`: SIGPIPE 와 상속 (실측)

`head` 가 한 줄 읽고 끝나면 pipe1 의 읽기 끝이 모두 닫힌다. 그다음 `yes` 의 `write` 는 POSIX `write` 규정대로 `EPIPE` + **SIGPIPE** 를 받는다. 기본 동작은 종료 → 코드 128+13 = **141**. 이게 `yes` 같은 무한 생산자가 파이프라인에서 끝나는 유일한 방법이다.

그런데 부모가 SIGPIPE 를 무시하고 있으면? bash 로도, 내 코드로도 확인했다 (실제 출력):

```text
$ bash -c 'yes | head -n 1; echo "PIPESTATUS=${PIPESTATUS[@]}"'
y
PIPESTATUS=141 0
$ bash -c 'trap "" PIPE; yes | head -n 1; echo "PIPESTATUS=${PIPESTATUS[@]}"'
y
yes: stdout: Broken pipe
PIPESTATUS=1 0
```

```text
$ ./practice   (연습 3, 자식에서 SIG_DFL 복원을 안 한 경우)
연습 3: SIGPIPE 무시 상속 시 yes 의 코드 = 1 (기본값이면 141)
```

macOS `yes` 는 `EPIPE` 를 오류로 보고 1 로 끝나서 다행히 멈추지는 않는다. 하지만 `EPIPE` 를 확인하지 않는 생산자라면 **영원히 돈다**. 실무에서도 같은 버그가 나온다 — SIGPIPE 를 무시하는 서버가 `system()`/`popen()` 으로 띄운 자식이 이상하게 동작하는 경우. Python 의 `subprocess.Popen(restore_signals=True)` 기본값이 바로 이것을 자식에서 되돌리는 옵션이다.

### Part A — hang 재현: 세 가지 새는 쓰기 끝 (`demo_hang.c`)

`demo_hang` 은 일부러 fd 하나를 안 닫고 `printf 'b\na\n' | sort [| cat]` 을 띄운 뒤, `waitpid(WNOHANG)` 폴링 1000 ms 감시자로 끊는다. 인자 `lsof` 를 주면 막힌 순간 `lsof -a -d 0-9 -p <부모>,<자식들>` 을 찍는다.

<!-- code: demo_hang.c -->

실제 출력 (stdout 을 `/tmp/OS-B02_work/hang.txt` 로 보낸 실행, 경로 줄은 일부 생략):

```text
V1 부모가 쓰기 끝을 안 닫음: printf | sort
  HANG: 1000 ms 지나도 1 개 프로세스가 안 끝남: pid 29494
COMMAND PID FD TYPE DEVICE NODE NAME
demo_hang 29486 4 PIPE 0x61e91ea00c20bfcc ->0x25e56acd26880bd1
sort 29494 0 PIPE 0x25e56acd26880bd1 ->0x61e91ea00c20bfcc
V2 sort 가 자기 쓰기 끝을 안 닫음: printf | sort
  HANG: 1000 ms 지나도 1 개 프로세스가 안 끝남: pid 29547
sort 29547 0 PIPE 0x5efcc85a8c4f258f ->0x22db793bfb2af63c
sort 29547 4 PIPE 0x22db793bfb2af63c ->0x5efcc85a8c4f258f
V3 cat 이 pipe0 을 물려받아 쥠: printf | sort | cat
  HANG: 1000 ms 지나도 2 개 프로세스가 안 끝남: pid 29563 pid 29564
sort 29563 0 PIPE 0x894194f383eac4d4 ->0x67a2eb333b8e3ca9
sort 29563 1 PIPE 0x6a76280a30f19bde ->0xe390fda481dee87f
cat 29564 0 PIPE 0xe390fda481dee87f ->0x6a76280a30f19bde
cat 29564 3 PIPE 0x894194f383eac4d4 ->0x67a2eb333b8e3ca9
cat 29564 4 PIPE 0x67a2eb333b8e3ca9 ->0x894194f383eac4d4
FIX 전부 닫음: printf | sort | cat
a
b
  OK: 6.0 ms 에 끝남
```

읽는 법: macOS `lsof` 는 파이프의 **각 끝** 을 주소로 보이고 `->` 뒤에 **상대 끝** 을 적는다. sort 의 fd 0 이 `0x25e5… -> 0x61e9…` 이면 쓰기 끝은 `0x61e9…` 이고, 그 주소를 가진 줄을 찾으면 범인이다.

| 경우 | 쓰기 끝(상대 주소)을 쥔 범인 | 결과 |
|---|---|---|
| V1 | `demo_hang` fd 4 = 부모 | sort 영원히 대기, 부모는 sort 를 기다림 → 서로 기다림 |
| V2 | `sort` fd 4 = **자기 자신** | 자기가 쥔 쓰기 끝 때문에 자기가 EOF 를 못 봄 |
| V3 | `cat` fd 4 (fd 3 은 같은 파이프 읽기 끝) | sort 는 EOF 대기, cat 은 sort 의 출력 EOF 대기 → 2개 다 멈춤 |

세 번 반복해도 매번 같다 (V1–V3 HANG, FIX 4.4–12.9 ms). 리눅스에서는 `ls -l /proc/<pid>/fd` 가 같은 정보를 `pipe:[inode]` 로 보여 준다 — Lima VM (Linux 7.0) 에서 `sleep 2 | sleep 2` 의 앞 프로세스 `1 -> pipe:[574156]`, 뒤 프로세스 `0 -> pipe:[574156]` 처럼 같은 inode 번호로 짝을 찾는다.

**하네스가 같은 버그를 잡는 모습** — 정답에서 부모의 `close(p[1])` 한 줄만 지운 변형을 돌려 보면 (실제 출력 일부):

```text
FAIL m_parent_keeps.c:500  OUT_IS("a\n") && g.rc == 0
FAIL m_parent_keeps.c:501  g.fd_delta == 0
...
19/31 passed        (real 30.31 s — hang 마다 감시자가 3 초 뒤 죽임, 지금은 2 초)
```

자식에서 `close(p[1])` 만 지운 변형은 hang 없이 탐침 2개만 FAIL 한다 (첫 단계 `open: 7`, 가운데 단계 `open: 8` — 자기 출력 파이프 쓰기 끝의 사본). 이 사본은 그 프로세스가 끝날 때 fd 1 과 함께 닫히므로 당장은 EOF 를 막지 않는다. 하지만 exec 된 프로그램이 오래 사는 자식(데몬)을 띄우면 그 자식이 물려받아 V3 와 똑같이 멈춘다 — 그래서 탐침으로 잡는다.

### Part B — 순서대로, 파이프 다음에

POSIX 셸 2.9.2 *Pipelines*: 파이프라인이 표준 입출력을 먼저 할당하고, 그다음 명령에 붙은 리다이렉션이 적용된다. 2.7 *Redirection*: 리다이렉션은 왼쪽부터 순서대로. 그래서 자식의 순서는 **dup2(파이프) → apply_redirs → exec** 다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">cmd 의 fd 1, fd 2 가 가리키는 곳 — 한 단계씩 (시작: 1=파이프, 2=터미널)</text>
  <text x="20" y="50" fill="currentColor" font-family="monospace">cmd &gt; f 2&gt;&amp;1</text>
  <rect x="180" y="34" width="175" height="44" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="190" y="52" fill="currentColor">&gt; f : 1 = f</text>
  <text x="190" y="70" fill="currentColor">2 = 터미널</text>
  <rect x="375" y="34" width="175" height="44" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="385" y="52" fill="currentColor">2&gt;&amp;1 : 2 = (1 의 대상)</text>
  <text x="385" y="70" fill="currentColor">= f</text>
  <text x="570" y="52" fill="currentColor">결과: 둘 다 f</text>
  <text x="570" y="70" fill="currentColor">f = "out\nerr\n"</text>
  <text x="20" y="130" fill="currentColor" font-family="monospace">cmd 2&gt;&amp;1 &gt; f</text>
  <rect x="180" y="114" width="175" height="44" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="190" y="132" fill="currentColor">2&gt;&amp;1 : 2 = (1 의 대상)</text>
  <text x="190" y="150" fill="currentColor">= 파이프</text>
  <rect x="375" y="114" width="175" height="44" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="385" y="132" fill="currentColor">&gt; f : 1 = f</text>
  <text x="385" y="150" fill="currentColor">2 는 그대로 파이프</text>
  <text x="570" y="132" fill="currentColor">결과: out → f</text>
  <text x="570" y="150" fill="currentColor">err → 파이프</text>
  <g stroke="currentColor" fill="none" stroke-width="2">
    <path d="M355 56 L373 56"/><path d="M355 136 L373 136"/>
  </g>
  <text x="20" y="200" fill="currentColor">dup2(1, 2) 는 "1 의 그 순간 대상" 을 복사할 뿐 — 이후 1 이 바뀌어도 2 는 따라가지 않는다.</text>
  <text x="20" y="222" fill="currentColor">그래서 "stdout 은 파일, stderr 는 파이프로" 같은 배선을 순서 하나로 고를 수 있다.</text>
</svg>
```

bash 와 내 셸이 같은지 (실제 출력):

```text
$ bash -c "sh -c 'echo out; echo err >&2' 2>&1 > /tmp/OS-B02_work/f; echo ---; cat /tmp/OS-B02_work/f"
err
---
out
$ bash -c "printf 'a\n' > /tmp/OS-B02_work/g | cat; echo ---; cat /tmp/OS-B02_work/g"
---
a
```

두 번째: `printf` 의 fd 1 은 먼저 파이프로 연결됐다가 `> g` 로 덮인다. 파이프 쓰기 끝은 아무도 안 쓰고 닫히므로 `cat` 은 즉시 EOF. 내 테스트도 같은 결과(`OUT_IS("") && file_is(g_out, "a\n")`)다.

`apply_redirs` 의 세부:

| 항목 | 이유 |
|---|---|
| `O_TRUNC` | 없으면 긴 파일 위에 짧은 내용을 쓸 때 꼬리가 남는다 (`longer line\n` 위에 `x\n` → `x\nnger line\n`). 테스트가 이것을 본다 |
| `O_CREAT, 0644` | 실제 권한은 `0644 & ~umask` |
| open 한 fd 를 dup2 후 close | 안 닫으면 그 fd 를 쥔 채 exec — 탐침이 `open: 3` 을 찍는다. 특히 `<` 로 연 파이프/FIFO 라면 Part A 의 hang 과 같은 문제가 된다 |
| `fd != target` 검사 | 0 이 닫혀 있으면 `open` 이 0 을 돌려줄 수 있다 (가장 낮은 빈 번호, 연습 1). 그때 dup2 후 close 하면 방금 연 것을 닫아 버린다 |
| 실패 → 자식이 `_exit(1)` | 부모 셸이 아니라 **자식** 에서 open 하므로 실패해도 셸은 멀쩡하다. bash 도 `cat < /없음` 에 1 을 돌려준다 |

### Part C — 양방향 파이프의 세 함정

데몬 쪽 구조: 입력 파이프 `ip`, 출력 파이프 `op` → `spawn_pipeline(pl, ip[0], op[1])` → 부모는 `ip[0]`, `op[1]` 을 닫고 `ip[1]` 에 쓰고 `op[0]` 에서 읽는다.

```c
while (rfd >= 0) {                                   /* 출력 EOF 까지 */
    struct pollfd pf[2] = { { rfd, POLLIN }, { wfd, POLLOUT } };
    poll(pf, wfd >= 0 ? 2 : 1, 남은 시간);
    if (pf[0].revents & (POLLIN|POLLHUP|POLLERR))  → read: >0 모으기, 0 이면 close(rfd)
    if (wfd >= 0 && pf[1].revents & (POLLOUT|POLLHUP|POLLERR)) {
        k = write(wfd, in + off, inlen - off);       /* O_NONBLOCK: 들어가는 만큼만 */
        if (k > 0) off += k; else if (errno != EAGAIN && errno != EINTR) off = inlen;  /* EPIPE */
        if (off == inlen) { close(wfd); wfd = -1; }  /* ← EOF 를 보내는 줄 */
    }
}
```

**함정 1 — 쓰기 끝을 안 닫음.** `wc -l` 은 EOF 를 봐야 숫자를 낸다. 정답에서 `close(wfd)` 를 지운 변형: `sort -r`, `wc -l`, `cat`, CSV 정렬 4개가 전부 `HUNG` (감시자가 죽임).

**함정 2 — 다 쓰고 나서 읽기.** `demo_hang` V4 (실제 출력, 3회 모두 같은 숫자):

```text
V4 다 쓰고 나서 읽기: 1 MB → cat → 부모
  HANG: 1007 ms 동안 write 에서 막힘. 들어간 바이트 = 147456 / 1048576
        (부모는 입력 파이프가 꽉 차서, cat 은 출력 파이프가 꽉 차서 서로 기다림)
  정리: 쓰기 끝을 닫고 읽으니 cat 이 147456 바이트를 내보내고 끝남
```

147456 = 65536 (출력 파이프, 꽉 참) + 65536 (입력 파이프, 꽉 참) + 16384 (cat 이 읽어 들고 write 에서 막힌 버퍼). 이 맥의 파이프 용량 65536 B 는 연습 5 에서 직접 잰 값이다. 즉 "입력 + 출력이 약 144 KB 를 넘으면" 순차 코드는 **반드시** 멈춘다 — 작은 입력으로 테스트하면 안 보이는 버그. `poll()` 로 읽을 수 있으면 읽고, 쓸 수 있으면 쓸 만큼만 쓰면 사라진다 (`O_NONBLOCK` 이 필요한 이유: 블로킹 파이프에 큰 `write` 는 전부 들어갈 때까지 돌아오지 않는다). I/O 다중화는 OS-M12 에서 본격적으로.

**함정 3 — 부모 쪽 끝이 자식에게 샘.** `spawn_pipeline` 은 `ip[1]` 의 존재를 모른다. fork 된 모든 자식이 `ip[1]` 을 물려받고, 첫 단계는 **자기 stdin 의 쓰기 끝** 을 쥔 채 EOF 를 기다린다 (V2 와 같은 모양). 정답에서 `set_cloexec(ip[1])` 을 지운 변형은 `sort -r`·`wc -l`·`cat`·CSV·탐침 5개가 FAIL. POSIX `popen` 이 "이전 popen 이 연 스트림을 새 자식에서 닫는다" 고 굳이 규정하는 이유도 이것이다.

**EPIPE:** `head -c 10` 은 10 바이트 읽고 끝난다. 남은 입력을 쓰면 `EPIPE` (SIGPIPE 는 하네스가 무시). 쓰기를 멈추고(`off = inlen`) 출력 EOF 까지 계속 읽는다. 테스트 `OUT_IS("L0000000\nL") && codes[0] == 0`.

### Part C — 펌웨어로 옮기면

| 데몬/셸 | 펌웨어·RTOS 대응 |
|---|---|
| 파이프 = 커널 링 버퍼 + 읽기/쓰기 대기 | RTOS 스트림 버퍼·메시지 큐 (RTOS-B11), ISR↔태스크 링 (DS-A02) |
| EOF = "쓰기 끝 참조 수 0" | 생산자 수를 세는 종료 플래그. 생산자가 여럿이면 마지막 생산자가 닫을 때 소비자를 깨운다 (OS-B06 의 `bb_close`) |
| 64 KB 버퍼 꽉 참 → writer 블록 | 큐 가득 → 송신 태스크 블록 또는 ISR 은 drop 카운터 |
| `FD_CLOEXEC` | 부팅 시 init 이 띄우는 데몬마다 열린 fd 정리 — BusyBox init·systemd 가 자식 fd 를 정리하는 이유 (ELX-B12, ELX-B13) |
| MMU 없는 리눅스(nommu, uClinux) | `fork` 를 못 쓴다 → `vfork` + exec 또는 `posix_spawn` (Q8) |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o so_asan && ./so_asan
```

실제 출력 (0.5 초, 6회 연속 + 다른 디렉터리(`/`)에서 1회 모두 31/31):

```text
-- 아래 'command not found' 한 줄은 테스트가 일부러 만든 것 --
mysh: no_such_cmd_os_b02: command not found
-- 아래 'No such file' 한 줄은 테스트가 일부러 만든 것 --
mysh: /nonexistent_os_b02/x: No such file or directory
31/31 passed
```

ASan/UBSan 빌드도 31/31, 보고 없음 (5.1 초 — ASan 프로세스의 `fork` 는 거대한 섀도 메모리 매핑 때문에 느리다). 이 테스트가 **틀린 구현을 정말 잡는지** 정답을 한 줄씩 망가뜨려 확인했다 (실측):

| 지운 줄 | 결과 |
|---|---|
| 부모 `close(p[1])` | 19/31, hang 10개 (감시자가 죽임) |
| 자식 `close(p[1])` | 29/31, 탐침 2개 |
| 자식 `signal(SIGPIPE, SIG_DFL)` | 30/31, `yes \| head` 의 코드 |
| `set_cloexec(ip[1])` (Part C) | 26/31, 5개 HUNG |
| 다 쓴 뒤 `close(wfd)` (Part C) | 26/31, 4개 HUNG + fd 누수 |
| `apply_redirs` 의 `close(fd)` | 30/31, 탐침 |

## 7. 자주 하는 실수

**① dup2 했으니 끝났다고 생각함.** `dup2(p[1], 1)` 뒤에도 p[1] 은 열려 있다. 출력이 끝나도(1 을 닫아도) p[1] 이 남아 있으면 다음 단계는 EOF 를 못 본다 → 원본은 **항상** close.

**② 부모가 파이프 fd 를 안 닫음.** 가장 흔한 hang (V1). 셸은 아무것도 쓰지 않지만, 쓰기 끝을 들고 있다는 것만으로 EOF 가 막힌다. 고친 코드: fork 직후 `close(prev); close(p[1]);`.

**③ 모든 파이프를 먼저 만들고 각 자식에서 일부만 닫음.** 단계 n 개면 각 자식이 2(n-1) 개를 물려받는다. 하나라도 빠뜨리면 V3. `prev` 하나만 들고 도는 루프가 실수할 자리를 줄인다.

**④ 첫 단계를 띄우고 바로 waitpid.** 출력이 파이프 용량(64 KB)보다 크면 첫 단계가 끝나지 않는다. 다 띄운 다음 다 기다린다.

**⑤ 리다이렉션을 파이프 연결보다 먼저 적용.** `printf a > f | cat` 에서 `> f` 를 먼저 하고 파이프를 나중에 dup2 하면 출력이 파이프로 가 버린다. 그리고 `2>&1` 을 마지막에 몰아서 처리하면 `2>&1 > f` 가 `> f 2>&1` 처럼 동작한다. **파이프 → 리다이렉션(나온 순서)**.

**⑥ fork 한 자식에서 `exit()` 또는 오류 메시지를 `printf`.** 부모 stdio 버퍼에 남은 내용이 자식에서 한 번 더 flush 된다 (출력이 파일·파이프일 때 전체 버퍼링이라 특히 잘 보인다). 자식은 `_exit`, 오류는 버퍼 없는 `stderr` 로.

**⑦ SIGPIPE 를 무시한 채 exec.** 부모의 SIG_IGN 이 자식에게 상속되어 `yes`·`cat` 같은 생산자가 141 대신 오류 코드로 끝나거나, `EPIPE` 를 확인하지 않는 프로그램은 영원히 돈다. 자식에서 `signal(SIGPIPE, SIG_DFL)`.

**⑧ popen 식 함수에서 부모 쪽 끝에 CLOEXEC 를 안 검.** 자식이 자기 입력의 쓰기 끝을 물려받는다 (Part C 함정 3). 리눅스라면 `pipe2(fds, O_CLOEXEC)` 로 한 번에. 이 레슨이 빌드하는 macOS 26.4 SDK 에는 `pipe2` 가 없다 — 27.2 SDK 헤더(`sys/unistd.h`)와 `man 2 pipe` 에는 `pipe2`·`O_CLOFORK` 가 새로 보인다.

## 8. 면접 Q&A

<details>
<summary>Q1. fd, 열린 파일 항목(open file description), inode/vnode 는 어떻게 다르고, fork·dup·open 은 각각 무엇을 공유하나요?</summary>

fd 는 프로세스별 표의 인덱스, 열린 파일 항목은 커널 전역 객체(오프셋, `O_APPEND`/`O_NONBLOCK` 같은 상태 플래그, 참조 수), vnode/inode 는 파일 자체. `fork` 는 fd 표를 복사하므로 부모·자식의 같은 번호가 **같은 항목** 을 가리킨다 → 오프셋 공유. `dup`/`dup2` 도 같은 항목을 가리키는 새 칸. `open` 을 다시 하면 새 항목 → 오프셋 따로. `FD_CLOEXEC` 는 **fd 칸** 의 플래그라 dup 으로 복사되지 않는다(POSIX `dup2`: 새 fd 의 FD_CLOEXEC 는 지워짐). 연습 2 실측: fork 한 자식이 4 바이트 쓰면 부모 `lseek(fd,0,SEEK_CUR)` 가 4, 자식이 따로 open 하면 0 이고 서로 덮어쓴다 (`PPCC`).

</details>

<details>
<summary>Q2. `cmd | wc -l` 이 안 끝납니다. 어떻게 디버깅하죠?</summary>

`wc` 가 `read` 에서 자는 건 파이프의 쓰기 끝이 어딘가 열려 있기 때문이다. `lsof -p <pid>` (macOS: 끝 주소와 `->` 상대 주소) 또는 리눅스 `ls -l /proc/<pid>/fd` (`pipe:[inode]`) 로 wc 의 fd 0 의 짝을 쥔 프로세스를 찾는다. 흔한 범인: 셸/부모(close 누락), 형제 단계(fork 때 물려받음), 백그라운드 데몬(CLOEXEC 없이 띄운 자식), 읽는 자신. `demo_hang lsof` 로 세 경우를 모두 봤다. 리눅스는 `strace -f -e trace=pipe,dup2,close,execve` 로 누가 언제 닫았는지도 볼 수 있다.

</details>

<details>
<summary>Q3. `cmd > f 2>&1` 과 `cmd 2>&1 > f` 의 차이는?</summary>

왼쪽부터 순서대로 적용되고, `2>&1` 은 `dup2(1, 2)` — 그 순간 1 의 대상을 복사한다. 앞의 것은 1 = f 후 2 = f. 뒤의 것은 2 = (원래 1 = 터미널/파이프) 후 1 = f → stderr 는 원래 stdout 으로. 그래서 "에러만 파이프로 넘기기" 는 `cmd 2>&1 >/dev/null | grep …`. 파이프 연결은 그보다 먼저 일어난다 (POSIX 셸 2.9.2).

</details>

<details>
<summary>Q4. `yes | head -1` 은 왜 끝나나요? 서버에서는 SIGPIPE 를 어떻게 다루죠?</summary>

head 가 끝나면 읽기 끝이 모두 닫히고, yes 의 다음 `write` 가 `EPIPE` + SIGPIPE 를 받아 기본 동작(종료)으로 죽는다 → 코드 141. 무한 생산자를 멈추는 장치다. 반대로 소켓 서버는 상대가 끊었을 때 프로세스가 죽으면 안 되므로 SIGPIPE 를 무시하거나(`SIG_IGN`), 리눅스 `send(…, MSG_NOSIGNAL)`, macOS `setsockopt(SO_NOSIGPIPE)` 또는 파이프에 `fcntl(F_SETNOSIGPIPE)` (macOS SDK 헤더에 있음)로 끄고 `EPIPE` 를 처리한다. 단, **무시 상태는 exec 너머로 상속** 되므로 그런 서버가 자식을 띄울 때는 자식에서 SIG_DFL 로 되돌린다.

</details>

<details>
<summary>Q5. 파이프 버퍼는 얼마나 크고, PIPE_BUF 는 무엇을 보장하나요?</summary>

용량은 구현마다 다르다. 이 맥에서 `O_NONBLOCK` 으로 1 바이트씩 채우면 65536 B, 1 MB 를 한 번에 쓰면 65536 B 가 들어가고 부분 성공(연습 5). 리눅스 기본도 65536 B (`pipe(7)`), `F_SETPIPE_SZ` 로 `/proc/sys/fs/pipe-max-size`(Lima VM 에서 1048576)까지 키울 수 있다. `PIPE_BUF` (macOS 512, 리눅스 4096) 이하의 write 는 **원자적** — 여러 writer 가 한 파이프에 써도 그 크기 이하의 메시지는 섞이지 않는다. 논블로킹이면 PIPE_BUF 이하 쓰기는 "전부 아니면 EAGAIN" (100 B 비운 꽉 찬 파이프에 512 B 쓰기 → EAGAIN, 실측). 그래서 여러 프로세스가 한 파이프로 로그를 보낼 때 한 줄을 PIPE_BUF 이하로 유지한다.

</details>

<details>
<summary>Q6. FD_CLOEXEC 는 왜 필요하고, `pipe()` 다음 `fcntl()` 로 거는 것의 문제는?</summary>

exec 할 때 자동으로 닫히는 fd 표시. 없으면 데몬의 내부 fd(소켓, 로그 파일, 파이프의 내 쪽 끝)가 자식 프로그램에 새서 EOF 가 안 오거나, 포트를 계속 점유하거나, 보안 문제(권한 없는 자식이 민감한 fd 접근)가 된다. 멀티스레드 프로그램에서 `pipe()` 와 `fcntl(FD_CLOEXEC)` 사이에 다른 스레드가 `fork+exec` 하면 그 틈에 샌다 — 그래서 `pipe2(O_CLOEXEC)`, `open(O_CLOEXEC)`, `fcntl(F_DUPFD_CLOEXEC)`, `accept4` 처럼 **생성과 동시에** 거는 API 가 생겼다. macOS 는 `posix_spawn` 에 `POSIX_SPAWN_CLOEXEC_DEFAULT` (Apple 확장, `sys/spawn.h` 확인) 로 "명시한 fd 외 전부 닫기" 도 할 수 있다.

</details>

<details>
<summary>Q7. 셸은 왜 파이프라인 단계를 모두 띄운 뒤에 기다리나요? 종료 코드는?</summary>

단계들은 생산자-소비자라 동시에 돌아야 한다. 앞 단계를 먼저 기다리면 파이프 버퍼(64 KB)가 차는 순간 앞 단계가 막혀 끝나지 않는다. 모두 띄운 뒤 모두 거둔다(좀비 방지, OS-B01). 코드는 마지막 단계(POSIX), bash `PIPESTATUS` 로 각 단계, `pipefail` 이면 0 이 아닌 것 중 가장 오른쪽. CI 스크립트에서 `make | tee log` 가 실패를 삼키는 문제가 이것이다.

</details>

<details>
<summary>Q8. MMU 없는 임베디드 리눅스에서는 이 코드가 어떻게 바뀌나요?</summary>

nommu 리눅스는 주소 공간을 복제할 수 없어 `fork` 가 없다. `vfork` (자식이 exec/_exit 할 때까지 부모 메모리를 그대로 쓰고 부모는 멈춤) 또는 `posix_spawn` + `posix_spawn_file_actions_adddup2/addclose` 로 같은 배선을 한다. file actions 가 "자식에서 dup2/close 할 목록" 이라 이 레슨의 자식 코드와 1:1 로 대응한다. MMU 가 있는 큰 데몬에서도 `fork` 의 페이지 테이블 복사 비용(OS-M03) 때문에 `posix_spawn` 을 선호한다. Cortex-M 같은 MCU 에는 프로세스도 fd 도 없고, 같은 역할을 RTOS 큐·스트림 버퍼가 한다.

</details>

## 9. 추가 연습

### 연습 1 — dup / dup2 규칙

(a) 새 fd 는 가장 낮은 빈 번호인가 (b) `dup2(b, b)` 는 무엇을 하나 (c) `dup2(b, p[1])` 처럼 대상이 이미 열려 있으면? (d) OSTEP 그림 5.4(p4.c)의 `close(1); open(file)` 트릭이 정말 fd 1 을 받는가.

<details>
<summary>답 보기</summary>

(a) `open` 두 번 → a < b, `close(a)` 후 `dup(b)` 는 a 를 재사용. (b) 아무것도 닫지 않고 b 반환 (POSIX: fildes == fildes2 이면 닫지 않고 돌려줌). (c) 대상을 **조용히 닫고** 복사한다 — p[1] 이 그 파이프의 유일한 쓰기 끝이었으므로 `read(p[0])` 가 즉시 0(EOF). 리다이렉션 코드에서 실수로 살아 있는 fd 를 덮으면 이렇게 소리 없이 닫힌다. (d) 자식에서 `close(1)` 후 `open` → 1, `printf("fd=%d\n")` 가 파일로 가서 내용 `fd=1\n`. 실무에서는 스레드가 그 사이에 fd 를 열 수 있으므로 `dup2(fd, 1)` 을 쓴다.

</details>

### 연습 2 — 열린 파일 항목 공유: 오프셋

fork 전 연 fd 로 자식이 `CCCC` 를 쓰고 끝난 뒤, 부모가 같은 fd 로 `PPPP` 를 쓰면 파일은? 자식이 같은 경로를 **따로 open** 해서 쓰면? 한 프로세스에서 `dup` 한 두 fd 로 번갈아 쓰면?

<details>
<summary>답 보기</summary>

fork: 항목 공유 → 부모의 오프셋이 자식 때문에 4 로 움직여 있고 파일은 `CCCCPPPP`. 따로 open: 항목 둘 → 부모 오프셋 0 → `PP` 가 앞을 덮어써 `PPCC`. dup: `ab` + `cd` → 오프셋 4. 그래서 셸에서 `(echo a; echo b) > f` 의 두 명령이 서로 덮어쓰지 않는다 — 같은 항목을 쓰기 때문. 반대로 두 프로세스가 같은 로그를 각자 open 하면 `O_APPEND` 가 필요하다.

</details>

### 연습 3 — pipefail 과 SIGPIPE 상속

`int pipefail_code(const int *codes, int n)` 을 짜고 `false | true`, `yes | head -n 1` 의 단계별 코드를 확인하라. 그리고 자식에서 SIGPIPE 를 되돌리지 **않으면** `yes` 의 코드가 어떻게 되는가?

<details>
<summary>답 보기</summary>

오른쪽부터 보며 처음 나온 0 이 아닌 코드, 없으면 0. `false | true` → {1, 0}, 기본 0 / pipefail 1. `yes | head -n 1` → {141, 0}, pipefail 141 (실무에서 `set -o pipefail` 스크립트가 `… | head` 때문에 실패하는 흔한 이유). 되돌리지 않으면 macOS `yes` 는 `EPIPE` 를 받고 메시지와 함께 1 로 끝난다 (실측 `코드 = 1`). 체크는 "141 이 아니고 멈추지도 않음".

</details>

### 연습 4 — FD_CLOEXEC 와 F_DUPFD_CLOEXEC

파이프 하나를 만든 채 `sh -c` 탐침을 띄우면 무엇이 보이나? 두 끝에 `FD_CLOEXEC` 를 걸면? `fcntl(p[0], F_DUPFD_CLOEXEC, 0)` 과 `dup(p[0])` 은 각각?

<details>
<summary>답 보기</summary>

플래그 없음: 실측 `open: 3 4` — exec 한 자식에게 두 끝이 그대로 보인다. CLOEXEC: `open:` (부모에서는 여전히 열려 있음). `F_DUPFD_CLOEXEC` 는 복제와 플래그를 원자적으로 — 새 fd 는 CLOEXEC. `dup` 은 플래그를 물려주지 않는다(FD_CLOEXEC 는 fd 칸의 속성) → 탐침에 그 번호 하나만 보인다. 플래그는 exec 에서만 작동하고 fork 에는 영향이 없다 (fork 에서 닫는 건 POSIX.1-2024 의 `FD_CLOFORK`, 27.2 SDK `sys/fcntl.h` 에 정의가 보인다).

</details>

### 연습 5 — 파이프 용량과 PIPE_BUF

논블로킹 쓰기 끝에 1 바이트씩 `EAGAIN` 이 날 때까지 써서 용량을 재라. 100 바이트를 읽어 비운 뒤 `PIPE_BUF` 바이트를 쓰면? 새 파이프에 1 MB 를 한 번에 쓰면?

<details>
<summary>답 보기</summary>

실측 (Apple M6, macOS 27.2): 용량 65536 B, 1 MB 한 번에 → 65536 B 반환(부분 쓰기), `PIPE_BUF = 512`. 100 B 만 빈 상태에서 512 B 쓰기 → `-1, EAGAIN` — POSIX 규정: `O_NONBLOCK` 이고 nbyte ≤ PIPE_BUF 면 공간이 충분할 때 전부, 아니면 **아무것도 안 쓰고** EAGAIN. PIPE_BUF 초과는 부분 쓰기가 허용되고 다른 writer 의 데이터와 섞일 수 있다. 체크는 용량 자체가 아니라 `용량 ≥ PIPE_BUF`, EAGAIN, 부분 쓰기 여부만 본다 (용량은 OS 마다 다름).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_asan && ./pr_asan
```

실제 출력 (0.22 초, 5회 + 다른 디렉터리 1회 모두 같음, ASan/UBSan 빌드도 25/25 보고 없음):

```text
연습 3: SIGPIPE 무시 상속 시 yes 의 코드 = 1 (기본값이면 141)
연습 4: 플래그 없이 = open: 3 4
연습 5: 1 B 씩 채운 용량 = 65536 B, 1 MB 한 번에 쓰면 65536 B 들어감, PIPE_BUF = 512
25/25 passed
```

## 10. 핵심 정리

- fd = 프로세스별 표의 번호 → 열린 파일 항목(오프셋·플래그·참조 수) → vnode/파이프. **fork 는 표 복사(항목 공유, 오프셋 공유), dup2 는 같은 항목에 칸 하나 더, open 은 새 항목.**
- 파이프 EOF = 버퍼 비었고 **쓰기 끝 참조 수 0**. 부모·형제·자신 중 하나라도 쥐고 있으면 영원히 대기 (V1/V2/V3 실측, lsof `->` 로 범인 찾기).
- N단 파이프라인: `prev` 하나 들고 루프, 자식은 dup2 후 **원본 전부 close**, 부모는 fork 직후 `prev`·`p[1]` close. 다 띄운 다음 다 기다린다. 코드 = 마지막 단계 (pipefail 은 가장 오른쪽 실패).
- 리다이렉션은 파이프 **다음에**, **왼쪽부터**. `2>&1` = `dup2(1, 2)` = "지금 1 의 대상". `> f 2>&1` ≠ `2>&1 > f`.
- 무시한 시그널은 exec 너머로 상속 → 자식에서 SIGPIPE 를 SIG_DFL 로 (`yes | head` 의 141).
- 양방향 파이프: 다 쓰면 쓰기 끝 close, `poll` + `O_NONBLOCK` 으로 동시에 (순차면 실측 147456 B 에서 교착), EPIPE 면 쓰기 중단, 내 쪽 끝은 `FD_CLOEXEC`.
- 파이프 용량 64 KB(이 맥·리눅스 기본), PIPE_BUF(macOS 512) 이하 write 는 원자적.
- 테스트는 hang 을 FAIL 로 바꾸는 감시자(WNOHANG 폴링 + SIGKILL, 감시 프로세스)와 자식 fd 탐침으로.

## 참고자료

- OSTEP 5장 *Interlude: Process API* — 5.4 리다이렉션(그림 5.4 `p4.c`)과 파이프, 숙제 7(자식에서 stdout 닫기)·8(두 자식을 pipe 로 잇기): <https://pages.cs.wisc.edu/~remzi/OSTEP/cpu-api.pdf> (로컬: `operating-system-three-easy-pieces/book-md/C05_interlude_process_api.md`, Don 노트 `notes/2026-09-30_C05_process_api.md`)
- OSTEP 39장 *Interlude: Files and Directories* — fd, open/read/write, 웹판의 "Shared File Table Entries: fork() And dup()" 절 (로컬 book-md 판에는 이 절이 없다): <https://pages.cs.wisc.edu/~remzi/OSTEP/file-intro.pdf>
- POSIX `pipe`: <https://pubs.opengroup.org/onlinepubs/9699919799/functions/pipe.html>
- POSIX `dup`/`dup2` — 대상이 열려 있으면 먼저 닫음, 같은 번호면 그대로 반환, 새 fd 의 FD_CLOEXEC 해제: <https://pubs.opengroup.org/onlinepubs/9699919799/functions/dup.html>
- POSIX `read` (빈 파이프 + writer 없음 → 0), `write` (EPIPE/SIGPIPE, PIPE_BUF 원자성, O_NONBLOCK 규칙): <https://pubs.opengroup.org/onlinepubs/9699919799/functions/read.html>, <https://pubs.opengroup.org/onlinepubs/9699919799/functions/write.html>
- POSIX `exec` — 잡은 시그널은 기본값, 무시한 시그널은 무시 유지, FD_CLOEXEC: <https://pubs.opengroup.org/onlinepubs/9699919799/functions/exec.html>
- POSIX `fork` (fd 복사, 같은 열린 파일 항목 참조), `popen` (이전 popen 스트림을 자식에서 닫음): <https://pubs.opengroup.org/onlinepubs/9699919799/functions/fork.html>, <https://pubs.opengroup.org/onlinepubs/9699919799/functions/popen.html>
- POSIX Shell Command Language 2.7 *Redirection*, 2.9.2 *Pipelines*: <https://pubs.opengroup.org/onlinepubs/9699919799/utilities/V3_chap02.html>
- macOS `man 2 pipe` (widowed pipe → EOF, `F_SETNOSIGPIPE`), `man 2 dup`, `man 2 fcntl` (`F_DUPFD_CLOEXEC`), `man 8 lsof`
- Linux `pipe(7)` — 용량, PIPE_BUF, `F_SETPIPE_SZ`: <https://man7.org/linux/man-pages/man7/pipe.7.html>; `pipe2(2)`: <https://man7.org/linux/man-pages/man2/pipe.2.html>
- W. R. Stevens & S. Rago, *APUE* 3판 — 3장 File I/O (파일 공유, dup/dup2), 15장 IPC (pipe, popen)
- 관련 레슨: OS-B01 (fork/exec/wait, 상태 분류), OS-B07 (시그널·self-pipe), OS-B13 (파일 API·원자적 저장), OS-B14 (IPC 비교), OS-M02 (파이프 핑퐁 컨텍스트 스위치), OS-M03 (COW fork), OS-M12 (I/O 모델·poll/kqueue), OS-M16 (SCM_RIGHTS fd 전달), OS-B06 (bounded buffer·종료 알림), RTOS-B11 (메시지 큐), DS-A02 (ISR↔태스크 링), ELX-B12 (BusyBox rootfs), ELX-B13 (systemd 유닛)
