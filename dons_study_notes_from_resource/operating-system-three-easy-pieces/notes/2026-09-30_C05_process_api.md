# Ch.05 프로세스 API — fork(), exec(), wait() 그리고 셸이 만들어지는 원리

> 📖 원문: [05. Interlude: Process API](../book-md/C05_interlude_process_api.md) · [PDF p.58](../Operating%20Systems%20-%20Three%20Easy%20Pieces.pdf#page=58) · ⏱️ 읽기 약 45분 (+ 실습 1시간) · 🔗 선행: [Ch.04](2026-09-30_C04_process.md)

## 0. 한눈에 보기

- UNIX는 프로세스를 **두 단계**로 만든다. **`fork()`** 로 나를 거의 그대로 복제하고, **`exec()`** 로 복제본을 다른 프로그램으로 갈아 끼운다. 부모는 **`wait()`** 로 자식이 끝나길 기다린다.
- `fork()` 는 **한 번 불리고 두 번 리턴**한다: 부모에게는 자식 PID, 자식에게는 0.
- `exec()` 는 새 프로세스를 만들지 **않는다**. 지금 프로세스의 코드 · 데이터 · 힙 · 스택을 새 프로그램으로 덮어쓰고, 성공하면 **절대 돌아오지 않는다**. 단, **열린 파일 디스크립터는 그대로 남는다**.
- 둘을 나눈 이유: fork와 exec **사이**에 코드를 끼워 넣어 자식의 환경(stdout, stdin, 파이프)을 바꿀 수 있다. 셸의 `>` 리다이렉션과 `|` 파이프가 이렇게 구현된다.
- 원문 CRUX: *"What interfaces should the OS present for process creation and control? How should these interfaces be designed to enable ease of use as well as utility?"* — 프로세스를 만들고 제어하는 인터페이스는 어떤 모양이어야 쓰기 쉬우면서도 쓸모 있을까?

## 1. 5분 복습표

| 용어 | 한 줄 뜻 | 예시 / 비유 |
|---|---|---|
| `fork()` | 호출한 프로세스의 (거의) 복제본을 만든다 | 부모에게 자식 PID, 자식에게 0 리턴 |
| 부모 / 자식 (parent / child) | fork를 부른 쪽 / 새로 생긴 쪽 | `getppid()` 로 부모 PID 확인 |
| `wait()` / `waitpid()` | 자식이 끝날 때까지 기다리고 종료 상태를 받는다 | `WEXITSTATUS(status)` |
| `exec()` 계열 | 현재 프로세스를 다른 프로그램으로 바꾼다 | `execvp("wc", argv)` |
| `execve()` | exec 계열의 실제 시스템 콜 (나머지는 libc 래퍼) | `execl`, `execlp`, `execv`, `execvp`… |
| 비결정성 (nondeterminism) | fork 후 누가 먼저 돌지 모른다 | 출력 순서가 실행마다 다름 |
| 파일 디스크립터 (fd) | 프로세스의 열린 파일 테이블의 번호 | 0=stdin, 1=stdout, 2=stderr |
| 리다이렉션 (redirection) | 프로그램의 stdout/stdin을 파일로 바꿔 끼우기 | `wc p3.c > out.txt` |
| `dup2(old, new)` | fd `new` 가 `old` 와 같은 열린 파일을 가리키게 함 | `dup2(fd, 1)` |
| `pipe()` | 커널 안의 단방향 바이트 큐, fd 두 개(읽기/쓰기) | `grep` 출력을 `wc -l` 입력으로 잇는 통로 |
| 시그널 (signal) | 프로세스에 보내는 비동기 알림 | `kill(pid, SIGKILL)`, `SIGSEGV` |
| copy-on-write (COW) | fork 때 메모리를 바로 복사하지 않고, 쓰는 순간 복사 | fork가 빠른 이유 |
| 좀비 / 고아 (zombie / orphan) | 끝났는데 안 거둬진 자식 / 부모가 먼저 죽은 자식 | `<defunct>` / PID 1에 입양 |
| 셸 (shell) | 프롬프트 → 읽기 → fork → exec → wait 를 반복하는 유저 프로그램 | zsh, bash |


## 2. `fork()` 시스템 콜 (5.1)

책은 `fork()` 를 "당신이 부를 함수 중 가장 이상한 것" 이라고 소개한다. 코드부터 보자(원문 Figure 5.1, `p1.c` — §8.1에 이 Mac에서 돌린 버전이 있다).

```c
printf("hello world (pid:%d)\n", (int) getpid());
int rc = fork();
if (rc < 0) {          // fork 실패
    fprintf(stderr, "fork failed\n");
    exit(1);
} else if (rc == 0) { // 자식 (새 프로세스)
    printf("hello, I am child (pid:%d)\n", (int) getpid());
} else {               // 부모
    printf("hello, I am parent of %d (pid:%d)\n", rc, (int) getpid());
}
```

무슨 일이 일어나나:

1. 프로세스(PID 29146)가 "hello world" 를 찍는다. 한 번만.
2. `fork()` 를 부르면 OS가 **호출한 프로세스의 (거의) 정확한 복사본**을 만든다. 이제 OS 입장에서 `p1` 이 두 개 돌고 있고, **둘 다 `fork()` 에서 리턴하려는 참**이다.
3. 자식은 `main()` 처음부터 시작하지 **않는다**("hello world" 가 한 번만 찍힌 이유). 마치 자기가 `fork()` 를 부른 것처럼 그 리턴 지점에서 태어난다.
4. "거의" 복사본인 이유: 자식은 자기만의 주소 공간 복사본, 자기 레지스터, 자기 PC를 갖지만 **`fork()` 의 리턴값이 다르다**. 부모는 자식의 PID, 자식은 0. 이 차이 덕분에 `if` 하나로 두 경로를 나눌 수 있다.

```svg
<svg viewBox="0 0 700 300" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <defs><marker id="C05-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="250" y="10" width="200" height="70" rx="8" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="350" y="32" text-anchor="middle" fill="currentColor" font-weight="bold">부모 (PID 29146)</text>
  <text x="350" y="52" text-anchor="middle" fill="currentColor" font-size="12">printf("hello world")</text>
  <text x="350" y="70" text-anchor="middle" style="fill:var(--accent)" font-size="12">rc = fork();  ← 커널로 trap</text>
  <path d="M300,80 L170,140" fill="none" stroke="currentColor" marker-end="url(#C05-arrow)"/>
  <path d="M400,80 L530,140" fill="none" style="stroke:var(--accent)" stroke-width="2" marker-end="url(#C05-arrow)"/>
  <text x="200" y="110" fill="currentColor" font-size="12">fork() 리턴: 29147</text>
  <text x="470" y="110" style="fill:var(--accent)" font-size="12">fork() 리턴: 0</text>
  <rect x="40" y="142" width="260" height="140" rx="8" fill="none" stroke="currentColor"/>
  <text x="170" y="164" text-anchor="middle" fill="currentColor" font-weight="bold">부모 (PID 29146)</text>
  <text x="170" y="186" text-anchor="middle" fill="currentColor" font-size="12">주소 공간 원본</text>
  <text x="170" y="206" text-anchor="middle" fill="currentColor" font-size="12">레지스터, PC = fork 다음</text>
  <text x="170" y="226" text-anchor="middle" fill="currentColor" font-size="12">fd 테이블 (0,1,2,…)</text>
  <text x="170" y="260" text-anchor="middle" fill="currentColor" font-size="12">"parent of 29147"</text>
  <rect x="400" y="142" width="260" height="140" rx="8" fill="none" style="stroke:var(--accent)" stroke-width="2"/>
  <text x="530" y="164" text-anchor="middle" fill="currentColor" font-weight="bold">자식 (PID 29147)</text>
  <text x="530" y="186" text-anchor="middle" fill="currentColor" font-size="12">주소 공간 복사본 (COW)</text>
  <text x="530" y="206" text-anchor="middle" fill="currentColor" font-size="12">같은 레지스터, 같은 PC, x0 = 0</text>
  <text x="530" y="226" text-anchor="middle" fill="currentColor" font-size="12">fd 테이블 복사 (같은 열린 파일 공유)</text>
  <text x="530" y="260" text-anchor="middle" fill="currentColor" font-size="12">"I am child"</text>
  <line x1="300" y1="226" x2="400" y2="226" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="350" y="218" text-anchor="middle" fill="currentColor" font-size="11">offset 공유</text>
</svg>
```

### 비결정성: 누가 먼저 찍나?

fork 직후 실행 가능한 프로세스가 둘이 된다. CPU가 하나라면 둘 중 하나가 먼저 돈다. **어느 쪽인지는 스케줄러 마음**이고, 스케줄러는 복잡해서 예측하면 안 된다. 그래서 출력 순서가 실행마다 바뀔 수 있다. 이 **비결정성(nondeterminism)** 은 멀티스레드 프로그램에서 훨씬 더 큰 문제가 된다([Ch.26](2026-09-30_C26_concurrency_intro.md)).

이 Mac에서 `C05_fork` 를 두 번 돌렸을 때(§8.1), 두 번째 실행에서는 **부모가 먼저 출력하고 종료**해 버려서, 셸이 다음 명령을 시작한 뒤에야 자식의 줄이 찍혔다. 부모가 자식을 기다리지 않으면 이런 일이 생긴다.

### 새 예제: fork 개수 세기 (면접 단골)

```c
for (int i = 0; i < 3; i++) fork();
printf("x\n");
```

각 반복에서 **살아 있는 모든 프로세스**가 fork 하므로 프로세스 수가 두 배씩 는다: 1 → 2 → 4 → 8. 루프가 끝나면 **2³ = 8개**, `x` 는 8번 찍힌다. 일반적으로 n번이면 2ⁿ개, 새로 생긴 프로세스는 2ⁿ − 1개.

```c
fork() && fork() || fork();
```

단락 평가(short-circuit)를 따라가면:

- 첫 fork: 부모(리턴 > 0, 참)와 자식 C1(리턴 0, 거짓).
- 부모는 `&&` 의 오른쪽 두 번째 fork 실행 → 부모(참)와 C2(거짓). 부모는 `A && B` 가 참이므로 `||` 뒤를 안 함.
- C1은 `A` 가 거짓이라 `&& fork()` 건너뛰고 `|| fork()` 실행 → C1과 C3.
- C2는 `A && B` 가 거짓(B=0)이라 `|| fork()` 실행 → C2와 C4.
- 합계: 부모, C1, C2, C3, C4 = **5개**. (스크래치로 돌려서 `x` 가 5번 찍히는 것을 확인했다.)

## 3. `wait()` 시스템 콜 (5.2)

부모가 자식이 하던 일을 끝낼 때까지 기다리고 싶을 때 쓴다. 더 많은 기능을 가진 형제 **`waitpid()`** 도 있다(특정 PID 지정, `WNOHANG` 등).

원문 Figure 5.2(`p2.c`)는 p1에 `wait(NULL)` 한 줄을 더했을 뿐인데 **출력이 결정적(deterministic)** 이 된다. 왜?

- 자식이 먼저 돌면: 자식이 먼저 출력. 끝.
- 부모가 먼저 돌면: 부모는 바로 `wait()` 를 부르고, **자식이 실행되어 종료할 때까지 리턴하지 않는다**(부모는 Blocked). 자식이 끝나야 부모가 출력.

어느 쪽이든 자식 먼저. 책도 각주에서 "wait()가 자식 종료 전에 리턴하는 경우도 있으니 man 페이지를 보라" 고 단서를 단다. 예를 들어 시그널에 의해 `EINTR` 로 끊기거나, `waitpid` 에 `WUNTRACED` 를 주면 자식이 **멈췄을 때**도 리턴한다.

`wait()` 가 주는 것:

- 리턴값: 끝난 자식의 PID. 기다릴 자식이 없으면 **−1** (errno `ECHILD`).
- `status`: 어떻게 끝났는지. `WIFEXITED` / `WEXITSTATUS` (정상 종료 + 종료 코드), `WIFSIGNALED` / `WTERMSIG` (시그널로 죽음, 예: `SIGSEGV`).
- 부수 효과: 자식의 **좀비 상태를 거둔다**(PCB 해제, [Ch.04](2026-09-30_C04_process.md) §6).

## 4. 마지막으로, `exec()` 시스템 콜 (5.3)

fork만으로는 **같은 프로그램의 복사본**만 돌릴 수 있다. 다른 프로그램을 돌리려면 `exec()` 가 필요하다. 원문 Figure 5.3(`p3.c`)에서 자식은 `execvp()` 로 단어 세기 프로그램 `wc` 를 돌린다.

```c
char *myargs[3];
myargs[0] = strdup("wc");   // 프로그램 이름
myargs[1] = strdup("p3.c"); // 인자
myargs[2] = NULL;           // argv 끝
execvp(myargs[0], myargs);  // wc 실행
printf("this shouldn't print out");
```

`exec()` 가 하는 일:

1. 실행 파일 이름(`wc`)과 인자를 받는다.
2. 그 실행 파일의 **코드와 정적 데이터를 로드**해서 현재 코드 세그먼트와 정적 데이터를 **덮어쓴다**.
3. 힙, 스택 등 주소 공간의 나머지는 **다시 초기화**한다.
4. 새 프로그램을 시작하며 인자를 그 프로세스의 `argv` 로 넘긴다.

그래서 exec는 **새 프로세스를 만들지 않는다**. 지금 돌던 프로그램(p3)을 다른 프로그램(wc)으로 **변신**시킨다. PID도 그대로다. 성공한 exec는 **절대 리턴하지 않는다** — 리턴할 코드(p3)가 이미 지워졌으니까. 그래서 `"this shouldn't print out"` 은 exec가 **실패했을 때만** 찍힌다.

```svg
<svg viewBox="0 0 700 250" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <defs><marker id="C05-arrow2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="140" y="20" text-anchor="middle" fill="currentColor" font-weight="bold">exec 전: PID 29384</text>
  <rect x="40" y="30" width="200" height="34" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="140" y="52" text-anchor="middle" fill="currentColor">code: p3</text>
  <rect x="40" y="64" width="200" height="34" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="140" y="86" text-anchor="middle" fill="currentColor">data / heap / stack: p3</text>
  <rect x="40" y="110" width="200" height="34" fill="none" style="stroke:var(--accent)" stroke-width="2"/>
  <text x="140" y="132" text-anchor="middle" fill="currentColor">fd 0, 1, 2 (+ 리다이렉션)</text>
  <rect x="40" y="144" width="200" height="34" fill="none" stroke="currentColor"/>
  <text x="140" y="166" text-anchor="middle" fill="currentColor">PID, 부모, cwd, 권한</text>
  <path d="M250,90 L440,90" fill="none" stroke="currentColor" stroke-width="2" marker-end="url(#C05-arrow2)"/>
  <text x="345" y="80" text-anchor="middle" fill="currentColor">execvp("wc", argv)</text>
  <text x="345" y="110" text-anchor="middle" fill="currentColor" font-size="12">코드 · 데이터 덮어쓰기</text>
  <text x="560" y="20" text-anchor="middle" fill="currentColor" font-weight="bold">exec 후: 여전히 PID 29384</text>
  <rect x="460" y="30" width="200" height="34" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="560" y="52" text-anchor="middle" fill="currentColor">code: wc</text>
  <rect x="460" y="64" width="200" height="34" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="560" y="86" text-anchor="middle" fill="currentColor">새 data / heap / stack(argv)</text>
  <rect x="460" y="110" width="200" height="34" fill="none" style="stroke:var(--accent)" stroke-width="2"/>
  <text x="560" y="132" text-anchor="middle" fill="currentColor">fd 0, 1, 2 그대로 유지!</text>
  <rect x="460" y="144" width="200" height="34" fill="none" stroke="currentColor"/>
  <text x="560" y="166" text-anchor="middle" fill="currentColor">PID, 부모, cwd, 권한 유지</text>
  <path d="M240,127 C350,200 350,200 460,127" fill="none" style="stroke:var(--accent)" stroke-dasharray="5 3" marker-end="url(#C05-arrow2)"/>
  <text x="350" y="215" text-anchor="middle" style="fill:var(--accent)" font-size="12">열린 fd는 exec를 건너 살아남는다 (O_CLOEXEC 제외)</text>
  <text x="350" y="238" text-anchor="middle" fill="currentColor" font-size="12">→ 이것이 리다이렉션 · 파이프의 비밀</text>
</svg>
```

> **exec 변형들** — 원문은 "여섯 가지" 라며 `execl`, `execle`, `execlp`, `execv`, `execvp` 를 든다(숙제에는 BSD/macOS의 `execvP` 도 나온다). 이름 규칙: **l** = 인자를 **l**ist(가변 인자)로, **v** = **v**ector(배열)로, **p** = **P**ATH에서 프로그램 검색, **e** = **e**nvironment를 직접 지정. 실제 시스템 콜은 **`execve()`** 하나이고 나머지는 libc가 인자를 정리해서 `execve` 를 부르는 래퍼다. 변형이 많은 이유는 "인자를 어떤 모양으로 갖고 있나 + PATH 검색이 필요한가 + 환경을 바꿀 건가" 의 조합 때문이다.

## 5. 왜 이렇게 이상한 API인가? (5.4)

"새 프로세스 만들기" 를 왜 fork + exec 두 단계로 쪼갰을까? **셸을 만들 때 결정적으로 유용하기 때문이다.** fork 이후, exec 이전에 셸이 코드를 실행할 수 있다. 이 코드로 곧 실행될 프로그램의 **환경을 바꿀 수 있다.**

> **TIP — 제대로 하라 (Lampson's Law)**
> Butler Lampson의 "Hints for Computer Systems Design": "Get it right. Neither abstraction nor simplicity is a substitute for getting it right." 프로세스 생성 API를 설계하는 방법은 많지만, fork와 exec의 조합은 단순하면서도 엄청나게 강력하다. UNIX 설계자들은 그냥 제대로 했다.

셸은 그냥 **유저 프로그램**이다. 하는 일:

1. 프롬프트를 보여 주고 입력을 기다린다.
2. 명령(실행 파일 이름 + 인자)을 읽는다. 실행 파일이 파일 시스템 어디 있는지 찾는다.
3. `fork()` 로 자식을 만들고, 자식에서 `exec()` 계열로 명령을 실행한다.
4. `wait()` 로 명령이 끝나길 기다린다. 끝나면 다시 1번.

### 리다이렉션: `wc p3.c > newfile.txt`

셸은 자식을 만든 뒤 **exec 전에** 표준 출력을 닫고 `newfile.txt` 를 연다. 그러면 곧 실행될 `wc` 의 출력은 화면이 아니라 파일로 간다. 원문 Figure 5.4(`p4.c`):

```c
} else if (rc == 0) { // 자식: 표준 출력을 파일로 리다이렉트
    close(STDOUT_FILENO);
    open("./p4.output", O_CREAT|O_WRONLY|O_TRUNC, S_IRWXU);
    // 이제 wc 를 exec ...
```

이게 되는 이유는 **UNIX가 빈 파일 디스크립터를 0부터 찾기 때문**이다. fd 1(STDOUT_FILENO)을 닫았으니 `open()` 이 돌려주는 첫 빈 번호는 1이다. 이후 자식이 표준 출력(fd 1)에 쓰는 모든 것, 예를 들어 `printf()` 는 투명하게 파일로 간다. 그리고 §4 그림처럼 **열린 fd는 exec를 건너 살아남으므로**, `wc` 는 자기도 모르게 파일에 쓴다.

실행하면 화면엔 아무것도 안 나오고(출력이 파일로 갔으니까), `cat p4.output` 하면 wc의 결과가 있다. 실무에서는 "가장 낮은 번호" 규칙에 기대는 대신 **`dup2(fd, STDOUT_FILENO)`** 를 쓴다. 스레드가 동시에 fd를 열 수 있는 환경에서도 정확하다. §8.4에서 두 방법을 모두 돌린다.

### 파이프: `grep -o foo file | wc -l`

파이프는 비슷하지만 `pipe()` 시스템 콜을 쓴다. 한 프로세스의 출력이 **커널 안의 파이프(큐)** 에 연결되고, 다른 프로세스의 입력이 같은 파이프에 연결된다. 그래서 명령들을 길게 이어 붙일 수 있다.

```svg
<svg viewBox="0 0 700 270" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <defs><marker id="C05-arrow3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="350" y="20" text-anchor="middle" fill="currentColor" font-weight="bold">셸이 "grep -o fork FILE | wc -l" 을 실행할 때의 fd 배선</text>
  <rect x="30" y="40" width="190" height="110" rx="8" fill="none" stroke="currentColor"/>
  <text x="125" y="62" text-anchor="middle" fill="currentColor" font-weight="bold">자식 1 → exec grep</text>
  <text x="125" y="86" text-anchor="middle" fill="currentColor" font-size="12">fd 0: 터미널</text>
  <text x="125" y="106" text-anchor="middle" style="fill:var(--accent)" font-size="12">fd 1: dup2(p[1], 1)</text>
  <text x="125" y="126" text-anchor="middle" fill="currentColor" font-size="12">fd 2: 터미널</text>
  <rect x="270" y="70" width="160" height="56" rx="28" style="fill:var(--accent-soft)" stroke="currentColor" stroke-width="2"/>
  <text x="350" y="94" text-anchor="middle" fill="currentColor" font-weight="bold">커널 파이프 버퍼</text>
  <text x="350" y="113" text-anchor="middle" fill="currentColor" font-size="12">p[1] 쓰기 → p[0] 읽기</text>
  <rect x="480" y="40" width="190" height="110" rx="8" fill="none" stroke="currentColor"/>
  <text x="575" y="62" text-anchor="middle" fill="currentColor" font-weight="bold">자식 2 → exec wc -l</text>
  <text x="575" y="86" text-anchor="middle" style="fill:var(--accent)" font-size="12">fd 0: dup2(p[0], 0)</text>
  <text x="575" y="106" text-anchor="middle" fill="currentColor" font-size="12">fd 1: 터미널</text>
  <text x="575" y="126" text-anchor="middle" fill="currentColor" font-size="12">fd 2: 터미널</text>
  <line x1="220" y1="100" x2="268" y2="100" style="stroke:var(--accent)" stroke-width="2" marker-end="url(#C05-arrow3)"/>
  <line x1="432" y1="100" x2="478" y2="100" style="stroke:var(--accent)" stroke-width="2" marker-end="url(#C05-arrow3)"/>
  <rect x="200" y="180" width="300" height="70" rx="8" fill="none" stroke="currentColor"/>
  <text x="350" y="202" text-anchor="middle" fill="currentColor" font-weight="bold">부모 (셸)</text>
  <text x="350" y="222" text-anchor="middle" fill="currentColor" font-size="12">pipe(p) → fork ×2 → close(p[0]), close(p[1])</text>
  <text x="350" y="240" text-anchor="middle" fill="#d9534f" font-size="12">쓰기 끝을 안 닫으면 wc가 EOF를 영영 못 본다</text>
  <line x1="280" y1="180" x2="150" y2="152" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#C05-arrow3)"/>
  <line x1="420" y1="180" x2="550" y2="152" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#C05-arrow3)"/>
</svg>
```

> **ASIDE — RTFM: man 페이지를 읽어라**
> 시스템 콜 · 라이브러리 콜이 나오면 man 페이지를 읽자. 리턴값과 에러 조건이 다 거기 있다. 동료에게 fork의 세부를 물었다가 "RTFM" 소리를 듣지 않으려면. `man 2 fork`, `man 2 execve`, `man 2 wait`, `man 2 pipe`, `man 2 dup2`.

## 6. 그 밖의 API (5.5)

- **`kill()`**: 프로세스에 **시그널**을 보낸다. 잠들어라(`SIGSTOP`), 죽어라(`SIGKILL`, `SIGTERM`) 등. 시그널 서브시스템 전체가 외부 이벤트를 프로세스에 전달하고 받아 처리하는 인프라다. 키보드 `Ctrl-C` 는 `SIGINT`, `Ctrl-Z` 는 `SIGTSTP`.
- 하드웨어 예외도 시그널로 온다: 잘못된 메모리 접근 → `SIGSEGV`, 불법 명령 → `SIGILL` (§8.2와 [Ch.06](2026-09-30_C06_limited_direct_execution.md) §8.3에서 직접 본다).
- 도구: **`ps`** (어떤 프로세스가 있나), **`top`** (자원을 누가 먹고 있나. 종종 top 자신이 1등이라고 나오는 게 웃음 포인트), CPU 미터(책 저자들은 MenuMeters를 쓴다).

## 7. 정리 (5.6)

`fork()`, `exec()`, `wait()` 로 UNIX 프로세스 생성의 뼈대를 봤다. 더 깊게는 Stevens & Rago, *Advanced Programming in the UNIX Environment* 의 Process Control, Process Relationships, Signals 장.

### 현대적 보충 (원문 밖)

- **copy-on-write**: fork가 주소 공간 전체를 정말로 복사한다면 큰 프로세스는 fork가 엄청 느리다. 실제 OS는 페이지 테이블만 복사하고 페이지를 **읽기 전용으로 공유**하다가, 누군가 쓰는 순간 그 페이지만 복사한다(페이지 폴트 → 복사). fork 직후 exec하면 복사가 거의 일어나지 않는다.
- **`posix_spawn()`**: fork+exec를 한 번에. macOS는 내부적으로 이걸 적극적으로 쓴다(launchd, Foundation의 `Process`). fork 없이 바로 만드니 큰 프로세스에서 빠르다.
- **멀티스레드 프로세스에서 fork 주의**: fork는 **부른 스레드 하나만** 자식에 복제한다. 다른 스레드가 락을 쥔 채로 fork되면 자식에서 그 락은 영원히 풀리지 않는다. 그래서 멀티스레드 프로그램에서는 fork 후 바로 exec하거나(그 사이엔 async-signal-safe 함수만), `posix_spawn` 을 쓴다.
- **Linux `clone()`**: fork, 스레드 생성 모두 `clone()` 플래그 조합이다(무엇을 공유할지: 주소 공간, fd 테이블, 시그널 핸들러…). "프로세스 vs 스레드" 는 공유 정도의 차이다.

## 8. 직접 해보기

모든 C 코드는 macOS(Apple M2, Apple clang 21)에서 `cc -Wall -Wextra -O0 -pthread code/<파일>.c -o .work/bin/<이름>` 으로 경고 0개로 컴파일했다. 프로젝트 루트에서 `.work/bin/<이름>` 으로 실행했고, 아래 출력은 전부 실제 결과다.

### 8.1 `fork()` — [code/C05_fork.c](../code/C05_fork.c)

```c
// C05_fork.c — OSTEP Figure 5.1 (p1.c). fork()는 한 번 불리고 두 번 리턴한다.
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
    printf("hello world (pid:%d)\n", (int)getpid());
    fflush(stdout);                       // fork 전에 버퍼 비우기 (이유는 C05_fork_buffer.c)
    int rc = fork();
    if (rc < 0) {                         // fork 실패
        fprintf(stderr, "fork failed\n");
        exit(1);
    } else if (rc == 0) {                 // 자식: fork()가 0을 리턴
        printf("hello, I am child (pid:%d)\n", (int)getpid());
    } else {                              // 부모: fork()가 자식 PID를 리턴
        printf("hello, I am parent of %d (pid:%d)\n", rc, (int)getpid());
    }
    return 0;
}
```

첫 번째 실행:

```text
hello world (pid:63362)
hello, I am parent of 63374 (pid:63362)
hello, I am child (pid:63374)
```

두 번째 실행 — 부모가 출력하고 먼저 끝나 버려서, 자식의 줄은 **다음 명령을 위해 내가 찍은 프롬프트 줄(`$ ./C05_wait`) 뒤에** 나타났다:

```text
hello world (pid:63375)
hello, I am parent of 63376 (pid:63375)
$ ./C05_wait
hello, I am child (pid:63376)
```

부모가 `wait()` 하지 않으면 자식은 **고아(orphan)** 가 되어 launchd(PID 1)에 입양되고, 출력 타이밍도 보장되지 않는다. 터미널에서 프롬프트가 먼저 뜨고 그 뒤에 출력이 섞여 나오는 경험의 정체다.

### 8.2 `wait()` 와 종료 상태 — [code/C05_wait.c](../code/C05_wait.c)

```c
// C05_wait.c — OSTEP Figure 5.2 (p2.c) + 종료 상태(exit status) 해석.
// wait()로 순서를 결정적으로 만들고, 자식이 정상 종료/시그널로 죽은 경우를 구분한다.
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static void report(pid_t pid, int status) {
    if (WIFEXITED(status))
        printf("  child %d exited normally, exit code = %d\n", (int)pid, WEXITSTATUS(status));
    else if (WIFSIGNALED(status))
        printf("  child %d killed by signal %d (%s)\n", (int)pid, WTERMSIG(status),
               WTERMSIG(status) == SIGSEGV ? "SIGSEGV" : "other");
}

int main(void) {
    printf("hello world (pid:%d)\n", (int)getpid());
    fflush(stdout);
    int rc = fork();
    if (rc < 0) { fprintf(stderr, "fork failed\n"); exit(1); }
    if (rc == 0) {
        usleep(100 * 1000);   // 일부러 늦게 출력해도 부모는 기다린다
        printf("hello, I am child (pid:%d)\n", (int)getpid());
        exit(3);              // 종료 코드 3
    }
    int status;
    int wc = wait(&status);
    printf("hello, I am parent of %d (wc:%d) (pid:%d)\n", rc, wc, (int)getpid());
    report(wc, status);

    // 두 번째 자식: NULL 포인터에 쓰기 → 커널이 SIGSEGV로 죽인다
    rc = fork();
    if (rc == 0) {
        volatile int *bad = NULL;
        *bad = 1;
        _exit(0);
    }
    wc = waitpid(rc, &status, 0);
    report(wc, status);

    // 기다릴 자식이 없을 때 wait()는?
    wc = wait(NULL);
    printf("  wait() with no children returns %d\n", wc);
    return 0;
}
```

실행 결과:

```text
hello world (pid:63377)
hello, I am child (pid:63390)
hello, I am parent of 63390 (wc:63390) (pid:63377)
  child 63390 exited normally, exit code = 3
  child 63391 killed by signal 11 (SIGSEGV)
  wait() with no children returns -1
```

- 자식이 100 ms 늦게 출력해도 부모 줄은 항상 뒤에 온다. `wait()` 가 결정적으로 만든다.
- `exit(3)` → 부모는 `WEXITSTATUS` 로 3을 받는다. 셸의 `$?` 가 이 값이다.
- NULL에 쓴 자식은 MMU 폴트 → 커널 → `SIGSEGV` 로 죽고, 부모는 `WIFSIGNALED` 로 그걸 안다.
- 자식이 없으면 `wait()` 는 −1 (숙제 5의 답 일부).

### 8.3 `exec()` — [code/C05_exec.c](../code/C05_exec.c)

```c
// C05_exec.c — OSTEP Figure 5.3 (p3.c). 자식이 execvp()로 'wc'가 된다.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    const char *target = argc > 1 ? argv[1] : "code/C05_exec.c";
    printf("hello world (pid:%d)\n", (int)getpid());
    fflush(stdout);
    int rc = fork();
    if (rc < 0) {
        fprintf(stderr, "fork failed\n");
        exit(1);
    } else if (rc == 0) {
        printf("hello, I am child (pid:%d)\n", (int)getpid());
        fflush(stdout);
        char *myargs[3];
        myargs[0] = strdup("wc");          // 프로그램: wc
        myargs[1] = strdup(target);        // 인자: 셀 파일
        myargs[2] = NULL;                  // argv 끝 표시
        execvp(myargs[0], myargs);         // 성공하면 절대 돌아오지 않는다
        printf("this shouldn't print out\n");
    } else {
        int wc = wait(NULL);
        printf("hello, I am parent of %d (wc:%d) (pid:%d)\n", rc, wc, (int)getpid());
    }
    return 0;
}
```

실행 결과:

```text
hello world (pid:63392)
hello, I am child (pid:63406)
      30     126    1067 code/C05_exec.c
hello, I am parent of 63406 (wc:63406) (pid:63392)
```

자식(PID 63406)이 `wc` 로 변신해서 자기 소스 파일의 줄 · 단어 · 바이트 수를 출력했다. `wc` 를 실행한 프로세스의 PID도 63406이다(exec는 PID를 바꾸지 않는다). "this shouldn't print out" 은 안 찍혔다.

### 8.4 리다이렉션: close+open vs dup2 — [code/C05_redirect.c](../code/C05_redirect.c)

```c
// C05_redirect.c — OSTEP Figure 5.4 (p4.c): "wc file > out" 을 셸처럼 구현.
// 방법 A: close(1) 후 open() → 가장 낮은 빈 fd(=1)가 재사용된다 (책의 방법)
// 방법 B: open() 후 dup2(fd, 1) → 실제 셸이 쓰는 방법 (경쟁/가정 없음)
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static void run_wc_into(const char *src, const char *out, int use_dup2) {
    int rc = fork();
    if (rc < 0) { perror("fork"); exit(1); }
    if (rc == 0) {
        if (!use_dup2) {
            close(STDOUT_FILENO);                                   // fd 1 비우기
            int fd = open(out, O_CREAT | O_WRONLY | O_TRUNC, S_IRWXU); // → fd 1 받음
            fprintf(stderr, "  [child A] open() returned fd %d\n", fd);
        } else {
            int fd = open(out, O_CREAT | O_WRONLY | O_TRUNC, S_IRWXU);
            fprintf(stderr, "  [child B] open() returned fd %d, dup2(%d, 1)\n", fd, fd);
            dup2(fd, STDOUT_FILENO);                                // fd 1 ← fd
            close(fd);
        }
        execlp("wc", "wc", src, (char *)NULL);
        perror("exec");
        _exit(127);
    }
    waitpid(rc, NULL, 0);
}

int main(void) {
    run_wc_into("code/C05_redirect.c", "/tmp/ostep_p4_A.output", 0);
    run_wc_into("code/C05_redirect.c", "/tmp/ostep_p4_B.output", 1);
    printf("(parent) nothing from wc appeared on my terminal. Files:\n");
    fflush(stdout);
    system("cat /tmp/ostep_p4_A.output /tmp/ostep_p4_B.output");
    return 0;
}
```

실행 결과:

```text
  [child A] open() returned fd 1
  [child B] open() returned fd 3, dup2(3, 1)
(parent) nothing from wc appeared on my terminal. Files:
      39     187    1577 code/C05_redirect.c
      39     187    1577 code/C05_redirect.c
```

- 방법 A: fd 1을 닫았더니 `open()` 이 정확히 **1** 을 돌려줬다. "가장 낮은 빈 번호" 규칙.
- 방법 B: `open()` 은 3을 줬고 `dup2(3, 1)` 로 fd 1을 그 파일로 바꿨다.
- 두 경우 모두 `wc` 는 평소처럼 fd 1에 썼을 뿐인데 결과가 파일로 갔다. 진단 메시지는 stderr(fd 2)로 보냈으니 화면에 나온다.

### 8.5 파이프로 두 자식 연결 (숙제 8) — [code/C05_pipe.c](../code/C05_pipe.c)

```c
// C05_pipe.c — Homework 8: 자식 두 개를 pipe()로 연결. 셸의 "grep -o fork FILE | wc -l".
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    const char *file = argc > 1 ? argv[1] : "code/C05_pipe.c";
    int fds[2];                       // fds[0] = 읽는 쪽, fds[1] = 쓰는 쪽
    if (pipe(fds) < 0) { perror("pipe"); exit(1); }
    printf("pipe fds: read=%d write=%d\n", fds[0], fds[1]);
    fflush(stdout);

    pid_t left = fork();
    if (left == 0) {                  // 왼쪽: stdout → 파이프 쓰기 끝
        dup2(fds[1], STDOUT_FILENO);
        close(fds[0]); close(fds[1]);
        execlp("grep", "grep", "-o", "fork", file, (char *)NULL);
        _exit(127);
    }
    pid_t right = fork();
    if (right == 0) {                 // 오른쪽: stdin ← 파이프 읽기 끝
        dup2(fds[0], STDIN_FILENO);
        close(fds[0]); close(fds[1]);
        execlp("wc", "wc", "-l", (char *)NULL);
        _exit(127);
    }
    // 부모는 양쪽 끝을 반드시 닫는다. 쓰기 끝을 안 닫으면 wc가 EOF를 영영 못 본다.
    close(fds[0]); close(fds[1]);
    int st;
    waitpid(left, &st, 0);
    printf("grep (pid %d) exit %d\n", (int)left, WEXITSTATUS(st));
    fflush(stdout);
    waitpid(right, &st, 0);
    printf("wc   (pid %d) exit %d\n", (int)right, WEXITSTATUS(st));
    return 0;
}
```

실행 결과(그리고 셸로 같은 명령 `grep -o fork code/C05_pipe.c | wc -l` 을 쳤을 때도 `4`):

```text
pipe fds: read=3 write=4
       4
grep (pid 63420) exit 0
wc   (pid 63421) exit 0
```

`pipe()` 가 3, 4를 받은 건 0, 1, 2가 이미 쓰이고 있어서다. 부모가 `close(fds[1])` 을 빼먹으면? 파이프의 쓰기 끝을 가진 프로세스(부모)가 아직 살아 있으니 커널은 "아직 더 쓸 사람이 있다" 고 보고 `wc` 의 `read()` 를 EOF로 끝내 주지 않는다. `wc` 는 영원히 Blocked, 부모는 `waitpid(right)` 에서 영원히 Blocked → **교착**. 실무에서 아주 흔한 버그다.

### 8.6 fork 후 무엇이 복사되고 무엇이 공유되나 (숙제 1, 2) — [code/C05_fork_share.c](../code/C05_fork_share.c)

```c
// C05_fork_share.c — Homework 1, 2: fork 후 무엇이 복사되고 무엇이 공유되나?
//  (1) 변수 x: 주소 공간이 복사되므로 각자 따로 바뀐다.
//  (2) fork 전에 open()한 fd: "열린 파일 항목(offset 포함)"을 공유 → 쓰기가 덮어쓰지 않고 이어 붙는다.
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void) {
    int x = 100;
    const char *path = "/tmp/ostep_c05_shared.txt";
    int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);

    int rc = fork();
    if (rc == 0) {
        x += 1;
        printf("child : x=%d (&x=%p)\n", x, (void *)&x);
        fflush(stdout);   // _exit()는 stdio 버퍼를 비우지 않으므로 직접 flush
        for (int i = 0; i < 3; i++) {
            write(fd, "child\n", 6);
            usleep(1000);
        }
        _exit(0);
    }
    x += 1000;
    printf("parent: x=%d (&x=%p)\n", x, (void *)&x);
    for (int i = 0; i < 3; i++) {
        write(fd, "PARENT\n", 7);
        usleep(1000);
    }
    wait(NULL);
    off_t end = lseek(fd, 0, SEEK_CUR);
    printf("parent: shared offset after both wrote = %lld (3*6 + 3*7 = 39)\n", (long long)end);
    close(fd);
    fflush(stdout);
    system("cat /tmp/ostep_c05_shared.txt");
    return 0;
}
```

실행 결과:

```text
child : x=101 (&x=0x16cfa2be8)
parent: x=1100 (&x=0x16cfa2be8)
parent: shared offset after both wrote = 39 (3*6 + 3*7 = 39)
PARENT
child
PARENT
child
child
PARENT
```

- **변수 x** (숙제 1): 둘 다 100에서 시작. 자식은 101, 부모는 1100. 주소(`&x`)는 같지만 값은 따로. 주소 공간이 복사됐다(COW).
- **fd** (숙제 2): 둘 다 같은 fd에 쓸 수 있다. 그리고 서로 **덮어쓰지 않고 이어 붙었다**. 최종 offset이 정확히 6×3 + 7×3 = **39**. fork는 fd 테이블을 복사하지만, 각 fd가 가리키는 **열린 파일 항목(open file description, offset 포함)** 은 공유한다. 그래서 한쪽이 쓰면 offset이 움직이고 다른 쪽은 그 뒤에 쓴다. 순서는 섞인다(비결정적).
- 처음 시도에서 자식은 `printf` 후 `_exit(0)` 했는데, 출력이 파이프로 갈 때 **child 줄이 사라졌다**. `_exit()` 는 stdio 버퍼를 비우지 않기 때문이다. 그래서 `fflush(stdout)` 를 넣었다. 다음 실험이 이 버퍼 문제의 정체다.

### 8.7 printf 한 줄이 두 번 찍히는 이유 — [code/C05_fork_buffer.c](../code/C05_fork_buffer.c)

```c
// C05_fork_buffer.c — 면접 단골: printf 한 줄이 왜 두 번 찍히나?
// stdio 버퍼는 "유저 공간 메모리"라서 fork 때 주소 공간과 함께 복사된다.
//  - 터미널(tty)로 출력: 줄 단위 버퍼링 → '\n' 있는 줄은 fork 전에 이미 나감
//  - 파이프/파일로 출력: 블록(4KB+) 버퍼링 → '\n'이 있어도 버퍼에 남아 둘 다 출력
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void) {
    printf("line with newline\n");
    printf("line without newline... ");
    if (fork() == 0) {
        printf("[child]\n");
        return 0;              // return/exit() 시 남은 버퍼가 flush 된다
    }
    wait(NULL);
    printf("[parent]\n");
    return 0;
}
```

터미널(pty)에서 실행한 결과 (이 작업 환경은 tty가 없어서 `python3 -c 'import pty …'` 로 가상 터미널을 열어 실행했다):

```text
line with newline
line without newline... [child]
line without newline... [parent]
```

파이프로 실행한 결과(`.work/bin/C05_fork_buffer | cat`):

```text
line with newline
line without newline... [child]
line with newline
line without newline... [parent]
```

- 터미널: stdout은 **줄 버퍼링**. 개행 있는 첫 줄은 fork 전에 이미 커널로 나갔다. 개행 없는 둘째 줄만 버퍼에 남아 **부모와 자식 모두에 복사**되어 두 번 찍힌다.
- 파이프: stdout은 **완전 버퍼링**. 개행이 있어도 버퍼에 남아 있다가 fork로 복사되어 **두 줄 다 두 번** 찍힌다.
- 해결: fork 전에 `fflush(stdout)`, 또는 자식에서 `_exit()` (버퍼를 버림), 또는 `setvbuf` 로 버퍼링 끄기.

덤: OSTEP의 `generator.py -c` 가 이 환경(파이프 출력)에서 똑같은 이유로 줄을 중복 출력했다. 그래서 아래 §8.9는 가상 터미널에서 돌린 결과를 실었다.

### 8.8 미니 셸: fork + exec + wait + dup2 + pipe — [code/C05_minishell.c](../code/C05_minishell.c)

이 장의 결론("셸은 그냥 유저 프로그램이다")을 직접 확인. 75줄로 `cmd args`, `cmd > file`, `cmd1 | cmd2`, `exit` 를 지원한다.

```c
// C05_minishell.c — fork/exec/wait + dup2 + pipe 로 만든 75줄짜리 셸.
// 지원: "cmd args", "cmd args > file", "cmd1 args | cmd2 args", "exit"
// stdin에서 한 줄씩 읽는다 (스크립트를 파이프로 넣어 테스트 가능).
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAXARGS 16

// "a b > f" → argv={"a","b",NULL}, *outfile="f"
static void parse(char *s, char **argv, char **outfile) {
    int n = 0;
    *outfile = NULL;
    for (char *tok = strtok(s, " \t\n"); tok && n < MAXARGS - 1; tok = strtok(NULL, " \t\n")) {
        if (strcmp(tok, ">") == 0) { *outfile = strtok(NULL, " \t\n"); break; }
        argv[n++] = tok;
    }
    argv[n] = NULL;
}

// 자식 안에서만 호출: (필요하면) fd 재배선 후 exec
static void exec_child(char **argv, char *outfile, int in_fd, int out_fd) {
    if (in_fd != STDIN_FILENO)  { dup2(in_fd, STDIN_FILENO);   close(in_fd); }
    if (out_fd != STDOUT_FILENO){ dup2(out_fd, STDOUT_FILENO); close(out_fd); }
    if (outfile) {
        int fd = open(outfile, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fd < 0) { perror(outfile); _exit(1); }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }
    execvp(argv[0], argv);
    fprintf(stderr, "minish: %s: command not found\n", argv[0]);
    _exit(127);
}

int main(void) {
    char line[512];
    for (;;) {
        printf("minish> ");
        fflush(stdout);
        if (!fgets(line, sizeof line, stdin)) break;
        printf("%s", line);                       // 스크립트 입력을 화면에 에코
        fflush(stdout);
        char *bar = strchr(line, '|');
        char *a1[MAXARGS], *a2[MAXARGS], *o1, *o2;
        if (!bar) {
            parse(line, a1, &o1);
            if (!a1[0]) continue;
            if (strcmp(a1[0], "exit") == 0) break;
            pid_t pid = fork();
            if (pid == 0) exec_child(a1, o1, STDIN_FILENO, STDOUT_FILENO);
            int st;
            waitpid(pid, &st, 0);
            if (WEXITSTATUS(st)) printf("[exit %d]\n", WEXITSTATUS(st));
        } else {
            *bar = '\0';
            parse(line, a1, &o1);
            parse(bar + 1, a2, &o2);
            int p[2];
            pipe(p);
            pid_t l = fork();
            if (l == 0) { close(p[0]); exec_child(a1, NULL, STDIN_FILENO, p[1]); }
            pid_t r = fork();
            if (r == 0) { close(p[1]); exec_child(a2, o2, p[0], STDOUT_FILENO); }
            close(p[0]); close(p[1]);
            waitpid(l, NULL, 0);
            waitpid(r, NULL, 0);
        }
    }
    printf("\nbye\n");
    return 0;
}
```

스크립트를 파이프로 넣어 실행:

```text
$ printf 'echo hello from child\nwc -l code/C05_minishell.c > /tmp/minish.out\ncat /tmp/minish.out\ngrep -c fork code/C05_minishell.c\ncat code/C05_minishell.c | grep -c dup2\nnosuchcmd\nexit\n' | .work/bin/C05_minishell
```

```text
minish> echo hello from child
hello from child
minish> wc -l code/C05_minishell.c > /tmp/minish.out
minish> cat /tmp/minish.out
      75 code/C05_minishell.c
minish> grep -c fork code/C05_minishell.c
4
minish> cat code/C05_minishell.c | grep -c dup2
4
minish> nosuchcmd
minish: nosuchcmd: command not found
[exit 127]
minish> exit

bye
```

- `nosuchcmd`: exec가 실패하면 **리턴한다** → 자식이 에러 메시지를 찍고 `_exit(127)`. 셸이 "command not found" 에 127을 쓰는 관례 그대로.
- 리다이렉션 줄은 화면에 아무것도 안 찍혔고, 다음 `cat` 이 파일 내용을 보여 준다.
- 실제 셸은 여기에 작업 제어(job control: 프로세스 그룹, `tcsetpgrp`, `SIGTSTP`), 백그라운드 `&`, 시그널 처리, 여러 단 파이프가 더해진 것이다.

### 8.9 OSTEP 시뮬레이터 (1): `fork.py` — 프로세스 트리

```text
cd .tools/ostep-homework/cpu-api
python3 fork.py -s 10 -c
```

실행 결과(앞의 `ARG ...` 줄 생략):

```text
                           Process Tree:
                               a

Action: a forks b
                               a
                               └── b
Action: a forks c
                               a
                               ├── b
                               └── c
Action: c EXITS
                               a
                               └── b
Action: a forks d
                               a
                               ├── b
                               └── d
Action: a forks e
                               a
                               ├── b
                               ├── d
                               └── e
```

**중간 부모가 죽으면 손자는 어디로?** `python3 fork.py -A a+b,b+c,c+d,b- -c` (a→b→c→d 사슬을 만든 뒤 b를 종료):

```text
Action: b EXITS
                               a
                               ├── c
                               └── d
```

시뮬레이터의 기본 동작은 종료한 b의 **모든 자손**(c와 d)을 최상위 프로세스(a = init)의 자식으로 옮긴다(`fork.py` 의 `do_exit()` 코드가 `collect_children()` 로 자손 전체를 a 밑으로 붙인다). 실제 UNIX는 **직계 자식(c)만** init(또는 subreaper)에 입양되고 d는 c 밑에 그대로 남으니, 이 부분은 시뮬레이터의 단순화로 이해하자. `-R` (local reparent: 직계 자식을 죽은 프로세스의 부모에게 입양)을 주면 실제 UNIX에 더 가까운 모양이 된다:

```text
Action: b EXITS
                               a
                               └── c
                                   └── d
```

c가 b의 부모(a)에게 입양되고 d는 c 밑에 그대로 남는다. 숙제 "-R 을 줬을 때 무엇이 달라지나" 의 답이다.

### 8.10 OSTEP 시뮬레이터 (2): `generator.py` — 실제 fork/wait C 프로그램

`generator.py` 는 fork/wait/exit를 쓰는 C 프로그램을 생성하고, `-c` 를 주면 **실제로 gcc(macOS에서는 clang)로 컴파일해서 실행**한다.

```text
cd .tools/ostep-homework/cpu-api
python3 generator.py -A "fork b,1 {fork c,2 {} fork d,3 {} wait wait} wait" -c
```

생성된 `main()` (`read.c`):

```c
int main(int argc, char *argv[]) {
    // process a
    if (fork_or_die() == 0) {
        sleep(1);
        // process b
        if (fork_or_die() == 0) {
            sleep(2);
            // process c
            exit(0);
        }
        if (fork_or_die() == 0) {
            sleep(3);
            // process d
            exit(0);
        }
        wait_or_die();
        wait_or_die();
        exit(0);
    }
    wait_or_die();
    return 0;
}
```

가상 터미널에서 실행한 결과(첫 열 = 경과 초, `x+` 시작, `x-` 종료, `a->b` fork, `b<-c` wait 리턴):

```text
  0 a+
  0 a->b
  1      b+
  1      b->c
  1      b->d
  3           c+
  3           c-
  3      b<-c
  4                d+
  4                d-
  4      b<-d
  4      b-
  4 a<-b
```

읽는 법: b는 1초 자고 c와 d를 만든다. c는 1+2=3초에, d는 1+3=4초에 끝난다. b의 첫 `wait` 는 먼저 끝난 c를(3초), 둘째는 d를(4초) 거둔다. b가 끝나면 a의 `wait` 가 4초에 리턴. **`wait()` 는 "어느 자식이든 먼저 끝난 것" 을 돌려준다**는 점이 핵심이다.

> 주의: 같은 명령을 출력이 파이프인 환경에서 돌리면 `0 a+`, `0 a->b` 같은 줄이 여러 번 중복 출력된다. 생성된 `run.c` 가 fork 전에 `fflush` 를 하지 않아서 §8.7의 버퍼 복제가 일어나기 때문이다. 터미널에서 직접 치면 문제없다.

## 9. 펌웨어 엔지니어의 눈으로

- **RTOS에는 fork가 없다.** FreeRTOS/Zephyr는 `xTaskCreate(entry, stack, prio)` 처럼 "진입 함수 + 새 스택" 으로 태스크를 만든다. 주소 공간 복사라는 개념 자체가 없다(공유 메모리 하나). UNIX의 fork는 **MMU와 copy-on-write가 있어야 싸다**. MMU 없는 uClinux가 `fork` 대신 `vfork` 만 지원하는 이유다. 이 차이를 말할 수 있으면 "범용 OS vs RTOS" 질문에 강하다.
- **fd = 핸들 + 커널 쪽 공유 객체**. §8.6에서 offset이 공유된 것은 fd가 "열린 파일 항목" 을 가리키는 **참조**이기 때문이다. 펌웨어로 치면 디스크립터 링의 인덱스와 실제 버퍼 객체의 관계. 참조 카운트가 0이 될 때 비로소 해제되는 것(파이프 쓰기 끝을 다 닫아야 EOF)도 같다.
- **파이프 = 생산자/소비자 링 버퍼**. NVMe SQ/CQ, 코프로세서 메일박스와 구조가 같다: 쓰는 쪽 tail, 읽는 쪽 head, 가득 차면 생산자 Blocked, 비면 소비자 Blocked. 파이프는 커널이 그 Blocking과 깨우기를 대신 해 준다.
- **"exec 성공은 리턴하지 않는다" = 부트로더 → 펌웨어 점프**. 부트로더가 이미지를 로드하고 엔트리로 점프하면 돌아오지 않는다. 다만 exec는 PID, fd, 권한 같은 **프로세스 정체성은 유지**한 채 이미지만 바꾼다는 점이 다르다. Apple SoC 코프로세서 펌웨어 로드(이미지 로드 → 리셋 해제 → 핸드셰이크)도 같은 모양이다.
- **종료 상태 = 커맨드 완료 상태 코드**. `WEXITSTATUS` / `WTERMSIG` 를 구분하는 것은 NVMe 완료 엔트리의 Status Code Type(일반 에러 vs 미디어 에러 등)을 구분해 처리하는 것과 같다. "정상 실패" 와 "비정상 종료(크래시)" 를 다르게 다뤄야 한다.

## 10. 면접 질문

### Q1. fork()와 exec()를 왜 따로 두었나?
<details>
<summary>답 보기</summary>

**fork와 exec 사이에 코드를 실행할 수 있게** 하려고. 셸은 fork한 자식에서 exec 전에 **fd를 재배선**(`dup2` 로 리다이렉션, `pipe` 연결), **환경 변수 · 작업 디렉터리 · 시그널 마스크 · 프로세스 그룹 · 권한(setuid)** 을 바꾼 뒤 exec한다. exec는 이미지만 바꾸고 **열린 fd 등 프로세스 상태는 유지**하므로, 새 프로그램은 자기도 모르게 바뀐 환경에서 돈다. 이렇게 하면 "프로세스 생성 API" 가 모든 옵션을 인자로 받을 필요가 없다(Windows `CreateProcess` 와 대비). 단점: 큰 프로세스 fork 비용(COW로 완화), 멀티스레드 fork의 위험 → `posix_spawn` 이 대안.

</details>

### Q2. fork 후 부모와 자식이 공유하는 것과 공유하지 않는 것은?
<details>
<summary>답 보기</summary>

**복사(독립)**: 주소 공간(code, data, heap, stack — COW로 지연 복사), 레지스터, PID(자식은 새 PID), 대기 중 시그널, 리소스 사용량 카운터, 파일 락(fcntl 락은 상속 안 됨). **공유**: 열린 파일 디스크립터가 가리키는 **열린 파일 항목**(파일 offset, 상태 플래그 포함), 그래서 같은 fd로 쓰면 이어 쓰기가 된다. 또 `mmap(MAP_SHARED)` 영역은 공유된다. stdio 버퍼는 **유저 공간 메모리**라서 복사된다(그래서 printf 중복 출력 버그).

</details>

### Q3. `printf("hello"); fork();` 를 실행하면 hello가 몇 번 찍히나? 왜?
<details>
<summary>답 보기</summary>

보통 **두 번**. `printf` 는 바로 시스템 콜을 하지 않고 **유저 공간의 stdio 버퍼**에 쌓는다. 개행이 없으니(터미널이라도 줄 버퍼) 아직 버퍼에 있고, fork가 주소 공간을 복사하면서 **버퍼도 복사**된다. 두 프로세스가 종료하며 각자 버퍼를 flush해서 두 번 출력된다. 출력이 파이프/파일이면 개행이 있어도 완전 버퍼링이라 중복된다. 해결: fork 전 `fflush(stdout)`, 자식은 `_exit()` 로 끝내기. (§8.7에서 실제로 재현.)

</details>

### Q4. 셸이 `ls | wc -l` 을 어떻게 실행하는지 시스템 콜 수준으로 설명하라. 흔한 버그는?
<details>
<summary>답 보기</summary>

`pipe(p)` → `fork()` (자식1: `dup2(p[1], 1)`, `close(p[0])`, `close(p[1])`, `execvp("ls")`) → `fork()` (자식2: `dup2(p[0], 0)`, 양쪽 close, `execvp("wc")`) → 부모는 **`close(p[0])`, `close(p[1])`** → `waitpid` 두 번. 흔한 버그: **쓰기 끝을 어딘가에서 안 닫음**(부모 또는 자식2). 쓰기 끝의 참조가 남아 있으면 `wc` 는 EOF를 못 받고 영원히 Blocked → 셸도 wait에서 멈춤. 또 하나: 읽는 쪽이 먼저 끝나면 쓰는 쪽은 `SIGPIPE` 를 받는다(`yes | head` 가 끝나는 원리).

</details>

### Q5. 좀비 프로세스는 왜 생기고, 서버 프로그램에서 어떻게 방지하나?
<details>
<summary>답 보기</summary>

자식이 종료해도 부모가 **종료 상태를 거두기(wait) 전까지** 커널은 PID와 종료 정보를 남겨 둔다. 부모가 wait를 안 하면 좀비가 쌓여 **PID/프로세스 테이블 고갈**. 방지: (1) `SIGCHLD` 핸들러에서 `while (waitpid(-1, &st, WNOHANG) > 0);` 로 다 거두기 (시그널은 합쳐질 수 있으니 루프 필수), (2) `SIGCHLD` 를 `SIG_IGN` 으로 설정하면 커널이 자동 수거, (3) 더블 fork로 손자를 init에 입양시키기(데몬화). 부모가 먼저 죽은 **고아**는 PID 1(launchd/systemd)이 입양해서 거둔다.

</details>

### Q6. exec가 리턴했다면 무슨 뜻인가? exec 후에도 남는 것은?
<details>
<summary>답 보기</summary>

exec가 리턴했다면 **실패**다(파일 없음 `ENOENT`, 실행 권한 없음 `EACCES`, 형식 오류 `ENOEXEC` 등). 자식에서는 보통 에러를 찍고 `_exit(127)` 한다(부모의 stdio 버퍼를 중복 flush하지 않도록 `exit` 가 아닌 `_exit`). exec 후에도 남는 것: **PID, 부모 PID, 열린 fd(`O_CLOEXEC`/`FD_CLOEXEC` 제외), 현재 디렉터리, umask, 실제 UID/GID, 시그널 마스크, 무시(`SIG_IGN`)로 설정된 시그널**. 바뀌는 것: 코드, 데이터, 힙, 스택, 핸들러가 설정된 시그널은 기본값으로(핸들러 코드가 사라졌으니까), mmap 영역.

</details>

## 11. 자가 점검 & 숙제

### 퀴즈

1. `fork()` 의 리턴값 세 가지 경우와 각각의 의미는?
2. `wait()` 를 넣으면 p1의 출력이 왜 결정적이 되나?
3. 리다이렉션 구현에서 `close(1); open(...)` 이 동작하는 이유가 되는 UNIX 규칙은?
4. `for (i = 0; i < 4; i++) fork();` 뒤에 `printf("x\n")` 가 있으면 x는 몇 번 찍히나? 새로 생긴 프로세스는 몇 개?
5. 파이프에서 부모가 쓰기 끝을 닫지 않으면 무슨 일이?

<details>
<summary>정답</summary>

1. 음수: 실패(자식 없음). 0: 지금 코드가 **자식**에서 실행 중. 양수: 지금 **부모**이고 값은 자식 PID.
2. 부모가 먼저 돌더라도 `wait()` 에서 자식이 끝날 때까지 Blocked 되므로, 부모의 출력은 항상 자식의 출력 뒤에 온다.
3. 새 fd는 **사용 가능한 가장 낮은 번호**를 받는다. fd 1을 닫으면 다음 `open()` 이 1을 받는다.
4. 2⁴ = **16번**. 새로 생긴 프로세스는 16 − 1 = **15개**.
5. `wc` 같은 읽는 쪽이 EOF를 영영 받지 못해 Blocked 상태로 남고, 부모도 wait에서 멈춘다(교착).

</details>

### 원문 숙제 중 꼭 해볼 것

- **숙제 2** (fork 전에 연 fd를 부모/자식이 동시에 쓰기): §8.6에서 실행. **fd 테이블은 복사되지만 열린 파일 항목(offset)은 공유된다**는 것을 확인하는 문제.
- **숙제 7** (자식에서 `close(STDOUT_FILENO)` 후 `printf`): 출력이 사라진다. printf는 에러 없이 버퍼에 쌓았다가 flush 때 `write(1, …)` 가 `EBADF` 로 실패한다. **stdio(유저 공간)와 fd(커널) 계층의 분리**를 확인하는 문제. 직접 짜 보자.
- **숙제 8** (두 자식을 pipe로 연결): §8.5에서 실행. **pipe + dup2 + close 규칙**을 확인하는 문제.
- 시뮬레이터: `python3 fork.py -s 4 -a 10 -F` (최종 트리만 보고 액션 맞히기), `python3 generator.py -n 3 -s 1` 로 생성된 코드의 출력 순서를 먼저 예측.

## 12. 다음으로

- 다음 장: [Ch.06 제한된 직접 실행](2026-09-30_C06_limited_direct_execution.md) — `fork()` 를 부르는 순간 CPU에서는 무슨 일이 일어나나? trap, 커널 모드, 타이머 인터럽트, 컨텍스트 스위치.
- 파일 디스크립터와 열린 파일 테이블을 더 깊게: [Ch.39 파일과 디렉터리](2026-09-30_C39_files_and_directories.md).
- fork의 copy-on-write를 가능하게 하는 페이징: [Ch.18 페이징 입문](2026-09-30_C18_paging_intro.md), [Ch.23 VAX/VMS](2026-09-30_C23_vax_vms.md).
- 스레드 API (프로세스와 무엇이 다른가): [Ch.27](2026-09-30_C27_thread_api.md).
