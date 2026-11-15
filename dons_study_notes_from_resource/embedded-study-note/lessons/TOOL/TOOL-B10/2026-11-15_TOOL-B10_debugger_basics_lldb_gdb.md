---
id: TOOL-B10
time: 60분
updated: 2026-11-15
---
# TOOL-B10 디버거 기초: breakpoint·step·backtrace·print

## 학습 목표

- 크래시한 프로그램을 lldb 에서 다시 돌려 **멈춘 이유(stop reason) · 주소 · 줄 · 호출 스택**을 읽고, `bt` → `up`/`frame select` → `frame variable`/`p` 로 "어느 포인터가 왜 이상한가"를 말할 수 있다.
- **크래시 지점(crash site)과 버그 지점(bug site)이 다를 때**, 의심 함수에 breakpoint 를 걸고 `n`(next) / `s`(step) / `finish` / `thread until` 로 한 줄씩 따라가 원인 줄을 찾는다.
- 연결 리스트 삭제의 전형적인 버그 3종(head 미갱신, 해제 후 `next` 읽기, 해제된 노드에 남은 `prev`)을 디버거로 찾아 고친다.
- poison 채우기 · 불변식 검사(`list_check`) · `-DVERIFY` 로 **크래시를 버그 지점 쪽으로 당겨오는** 펌웨어식 디버그 빌드를 만든다.
- 같은 세션을 gdb(Lima VM, `qemu-aarch64 -g` + `gdb-multiarch`, 원격 `target remote`)로 재현하고 lldb ↔ gdb 명령 대응표를 쓴다.

## 1. 문제

"이 코어/크래시에서 뭘 먼저 보세요?" — 펌웨어 면접의 디버깅 라운드와 실제 일(SSD 컨트롤러 펌웨어의 명령 큐, Cortex-M 의 소프트 타이머 리스트)에서 제일 자주 하는 일은 **멈추고, 스택 보고, 변수 보고, 한 줄 진행**이다. ARM-B09 가 같은 손놀림을 어셈블리(`si`/`ni`, 레지스터)로 했다면, 이 레슨은 **C 소스 수준**에서 한다: 함수 breakpoint, `n`/`s`/`finish`, `bt`, `frame variable`, `p *node`.

재료는 단일 연결 리스트(singly linked list)다. 노드는 malloc 대신 **고정 크기 풀(pool)** 에서 나오고(펌웨어처럼), `node_free` 는 노드를 돌려주는 대신 **poison** 으로 덮는다 — `value = 0x6B6B6B6B`(Linux slab 의 `POISON_FREE` 0x6b 흉내), `next = 0xDEAD0000`(이 맥에서는 매핑 안 된 주소, macOS 64비트 프로세스의 하위 4 GB 는 `__PAGEZERO`). 그래서 해제 후 사용(use-after-free)이 "운 좋게 동작" 하지 않고 **항상** 같은 주소에서 터진다. 디버거 연습에 필요한 재현성이다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">크래시 지점 ≠ 버그 지점 — list_delete(1) 의 head 버그 (실측 주소)</text>
  <text x="10" y="50" fill="currentColor">① 삭제 전</text>
  <text x="100" y="50" fill="currentColor" font-family="monospace" font-size="12">head</text>
  <path d="M135 46 L163 46" stroke="currentColor" stroke-width="1.5"/>
  <rect x="165" y="32" width="90" height="26" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="175" y="50" fill="currentColor" font-family="monospace" font-size="12">1 | •</text>
  <path d="M255 46 L283 46" stroke="currentColor" stroke-width="1.5"/>
  <rect x="285" y="32" width="90" height="26" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="295" y="50" fill="currentColor" font-family="monospace" font-size="12">2 | •</text>
  <path d="M375 46 L403 46" stroke="currentColor" stroke-width="1.5"/>
  <rect x="405" y="32" width="100" height="26" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="415" y="50" fill="currentColor" font-family="monospace" font-size="12">3 | NULL</text>
  <text x="530" y="50" fill="currentColor" font-size="12">g_pool+0 / +16 / +32</text>
  <text x="10" y="110" fill="currentColor">② 버그 함수 후</text>
  <text x="100" y="110" fill="currentColor" font-family="monospace" font-size="12">head</text>
  <path d="M135 106 L163 106" stroke="currentColor" stroke-width="1.5"/>
  <rect x="165" y="90" width="210" height="30" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="172" y="110" fill="currentColor" font-family="monospace" font-size="12">0x6b6b6b6b | 0xdead0000</text>
  <rect x="405" y="92" width="90" height="26" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="415" y="110" fill="currentColor" font-family="monospace" font-size="12">2 | •</text>
  <path d="M495 106 L523 106" stroke="currentColor" stroke-width="1.5"/>
  <rect x="525" y="92" width="100" height="26" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="535" y="110" fill="currentColor" font-family="monospace" font-size="12">3 | NULL</text>
  <text x="165" y="140" fill="currentColor" font-size="12">해제된(poison) 노드를 head 가 그대로 가리킴</text>
  <text x="405" y="140" fill="currentColor" font-size="12">아무도 안 가리킴 (잃어버린 노드)</text>
  <text x="10" y="165" fill="currentColor" font-size="12">버그 지점: list_delete (starter.c:136) — prev == NULL 인데 head 를 안 옮김. 이때는 조용하다.</text>
  <text x="10" y="215" fill="currentColor">③ 다음 호출</text>
  <rect x="100" y="195" width="630" height="70" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="110" y="215" fill="currentColor" font-family="monospace" font-size="12">list_sum: n = head(0x100008008)  → s += 0x6b6b6b6b  (s = 1802201963)</text>
  <text x="110" y="235" fill="currentColor" font-family="monospace" font-size="12">          n = n-&gt;next = 0xdead0000 → s += n-&gt;value</text>
  <text x="110" y="255" fill="currentColor" font-family="monospace" font-size="12">          EXC_BAD_ACCESS (code=1, address=0xdead0000)  at starter.c:121</text>
  <text x="10" y="290" fill="currentColor" font-size="12">크래시 지점: list_sum. 여기를 고치면 증상만 숨는다 → "이 포인터는 누가 남겼나" 를 거슬러 간다.</text>
  <text x="10" y="315" fill="currentColor" font-size="12">단서: 주소 0xdead0000 = poison (해제된 노드의 next) · s 에 0x6b6b6b6b 가 이미 더해짐</text>
</svg>
```

### Part A — 크래시 읽기와 breakpoint 추적: `list_delete`

- `./starter` 는 테스트 16개를 각각 `fork()` 한 자식에서 돌린다. 크래시는 `FAIL delete_head  crashed: signal 11` 한 줄이 될 뿐 하네스는 끝까지 간다. 디버거로는 **테스트 하나를 fork 없이** 돌린다: `lldb -- ./starter_dbg delete_head`.
- 1단계(`demo_crash.lldb`): 그냥 `run` → stop reason · 주소 · 줄 읽기 → `bt` → `frame variable` → `up` 해서 호출한 쪽의 `l` 확인 → `memory read` 로 poison 확인.
- 2단계(`demo_debug.lldb`): `b list_delete` → `run` → `n` 으로 한 줄씩, `p *cur`, `p cur->next->value`, `p prev`, `s` 로 `node_free` 안으로, `finish` 로 복귀, `up`/`down`, `c` 로 크래시까지. 원인 줄을 찾고 `list_delete` 를 고친다.

### Part B — 크래시 지점 = 버그 지점인 경우와, 고치면 나타나는 두 번째 버그: `list_delete_all`, `list_free`

- `delete_all_mixed` 는 `list_delete_all` **안에서** 터진다. `frame variable` 에서 `cur = 0xdead0000`, `prev` 는 poison 노드 → 해제한 노드의 `next` 를 따라갔다. `breakpoint set -p "prev = cur;" -X list_delete_all`(소스 정규식 breakpoint) + `display cur` + `n` 으로 `cur` 가 poison 으로 바뀌는 순간을 본다(`demo_delete_all.lldb`).
- 그 버그만 고치면 크래시는 사라지고 **조용한 손상**이 남는다: 지운 노드가 `prev` 로 남아 다음 삭제가 해제된 노드에 링크를 쓴다(`demo_prev.lldb`). `list_free` 도 같은 모양의 버그 + "해제한 뒤 head 를 안 비움" 이 있다.

### Part C — 펌웨어식 디버그 빌드: `list_check` + `-DVERIFY`, 그리고 gdb 원격

- `list_check(l)` — 깨진 리스트를 받아도 **절대 크래시하지 않는** 불변식 검사: 따라가기 전에 살아 있는 노드인지(`LC_DANGLING`), 너무 오래 걸으면 사이클(`LC_CYCLE`), 끝에서 개수 == `len`(`LC_LEN`). 테스트는 일부러 망가뜨린 리스트(해제된 노드 연결, 풀 밖 포인터, len 틀림, 3→2 사이클, 자기 자신 사이클)를 넣는다.
- `-DVERIFY` 로 빌드하면 리스트를 바꾸는 함수 끝에서 `list_check` 가 돌고, 실패하면 `abort()` → lldb 가 **버그 난 함수 안**(`list_delete` frame #3)에서 멈춘다(`demo_verify.lldb`). 크래시 지점을 버그 지점으로 당겨오는 것 — 펌웨어 디버그 빌드의 `ASSERT` · 힙 무결성 검사와 같은 생각이다.
- 같은 Part A 세션을 Lima VM 의 gdb 로: `qemu-aarch64 -g 23410` gdbstub 에 `gdb-multiarch` 가 `target remote` 로 붙는다 — OpenOCD/J-Link GDB 서버로 Cortex-M 에 붙는 것과 같은 모양(TOOL-A03).

## 2. Problem (English)

> **Debug a crashing linked-list module with a debugger.**
>
> A test binary for a singly linked list crashes. Nodes come from a fixed pool; `node_free()` overwrites a node with poison (`value = 0x6B6B6B6B`, `next = (node_t *)0xDEAD0000`). Running `./starter delete_head` prints nothing useful and dies with SIGSEGV.
>
> 1. Using lldb only (no printf), find the root cause of the `delete_head` crash. Report the stop reason, faulting address, crashing function and line, and the **function and line that introduced the bad pointer**.
> 2. Fix `bool list_delete(list_t *l, int value)`, `size_t list_delete_all(list_t *l, int value)` and `void list_free(list_t *l)` without changing their signatures. `list_delete_all` must remove every match in one pass; `list_free` must leave `head == NULL, len == 0` and free every node exactly once.
> 3. Implement `int list_check(const list_t *l)` returning `LC_OK`, `LC_DANGLING` (a reachable pointer is not a live pool node), `LC_CYCLE` or `LC_LEN`. It must never crash on a corrupted list.
> 4. Give the gdb equivalent of every lldb command you used.
>
> Example: `list = [1,2,3]; list_delete(&list, 1); list_sum(&list)` must return 5 (the buggy code crashes at `list_sum`, address `0xdead0000`). Follow-ups: why the crash site differs from the bug site; `step` vs `next` vs `finish`; why you debug at `-O0 -g`; how you would debug the same bug on a Cortex-M over SWD.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter          # 테스트 16개, 각각 fork
cc -std=c11 -g -O0 starter.c -o starter_dbg && lldb -- ./starter_dbg delete_head  # 디버그: 한 테스트, fork 없음
./starter list                                                                     # 테스트 이름 목록
```

처음 실행 결과 (실제 출력, exit 1):

```text
PASS basic
PASS delete_middle
PASS delete_tail
FAIL delete_head       crashed: signal 11 (Segmentation fault: 11)
    FAIL starter.c:236  l.head == NULL && l.len == 0
FAIL delete_only       check failed
    FAIL starter.c:242  EQ(l, 1, 4)
FAIL delete_dup_first  check failed
PASS delete_all_none
FAIL delete_all_mixed  crashed: signal 11 (Segmentation fault: 11)
FAIL delete_all_every  crashed: signal 11 (Segmentation fault: 11)
FAIL free              crashed: signal 11 (Segmentation fault: 11)
PASS free_empty
PASS check_ok
    FAIL starter.c:282  list_check(&l) == LC_DANGLING
    (… starter.c:285 ~ 303 의 list_check FAIL 6줄 생략)
FAIL check_dangling    check failed
FAIL check_len         check failed
FAIL check_cycle       check failed
FAIL random_ops        crashed: signal 11 (Segmentation fault: 11)
6/16 passed
```

하네스 설계 (TODO 아님):

- **테스트마다 `fork()`**: 자식이 SIGSEGV/SIGBUS/SIGABRT/SIGTRAP… 을 받으면 핸들러가 `_exit(128 + 신호)` → 부모가 `crashed: signal 11` 로 찍는다. 그냥 죽게 두면 이 맥은 크래시마다 `~/Library/Logs/DiagnosticReports/*.ips` 를 만든다(실측: 핸들러 없이 3번 크래시 → .ips 3개, 핸들러 있으면 0개). `alarm(2)` 로 무한 루프도 FAIL 로 끝난다.
- **인자로 테스트 이름을 주면 fork 없이 그 테스트만** — 디버거용. googletest 의 `--gtest_filter`, Check 프레임워크의 `CK_FORK=no` 와 같은 이유다(자세히는 7. 자주 하는 실수 3).
- `random_ops`: 같은 시드의 LCG 로 push/delete/delete_all 200번을 배열 모델과 비교 + 매번 `list_check`. 테스트가 못 떠올린 조합을 잡는다.
- `-DVERIFY`: 리스트를 바꾸는 함수 끝의 `VERIFY_LIST(l)` 가 `list_check` 를 부르고 실패하면 `abort()` (Part C).

관찰용 데모 (전부 이 디렉터리, `bash demo_debug.sh` 한 번이면 아래 txt 가 모두 다시 만들어진다, 약 9초):

| 파일 | 하는 일 | 실제 출력 |
|---|---|---|
| `demo_crash.lldb` | Part A 1단계: run → 크래시 읽기 | `demo_crash.txt` |
| `demo_debug.lldb` | Part A 2단계: `b list_delete`, n/s/finish/up/down | `demo_debug.txt` |
| `demo_delete_all.lldb` | Part B 버그 1: 소스 정규식 bp + `display` | `demo_delete_all.txt` |
| `demo_prev.lldb` | Part B 버그 2: 버그 1만 고친 `midfix` + `-DVERIFY` | `demo_prev.txt`, `demo_midfix_run.txt` |
| `demo_verify.lldb` | Part C: starter 버그 + solution 의 `list_check`, `-DVERIFY` | `demo_verify.txt`, `demo_verify_run.txt` |
| `demo_gdb.gdb` | Part A 2단계를 gdb 로 (Lima VM, qemu-aarch64 gdbstub) | `demo_gdb.txt` |
| `demo_practice.lldb`, `demo_practice_bug.c` | 추가 연습 1·2 의 버그 원본 | `demo_practice.txt`, `demo_practice_sorted.txt` |
| (lldb `-o run` 한 줄) | fork 하는 하네스를 그대로 lldb 에 | `demo_fork.txt` |

<!-- code: demo_debug.sh -->

## 4. 힌트

<details>
<summary>힌트 1 — 크래시 주소부터 읽어라</summary>

`address=0x0` 이면 NULL 역참조, `0xdead0000` 이면 이 레슨의 poison 포인터 — 즉 **이미 해제된 노드의 `next` 를 따라갔다**. 그렇다면 질문은 "그 해제된 노드를 누가 아직 가리키고 있나" 이다. `up` 해서 리스트의 `head` 를 `p *l.head` 로 보라.

</details>

<details>
<summary>힌트 2 — head 를 지울 때 바뀌어야 하는 변수는?</summary>

`b list_delete` 후 `n` 으로 진행하며 `p prev` 를 보라. 맨 앞 노드를 지울 때 `prev` 는 무엇이고, 그때 `if (prev != NULL)` 은 무엇을 건너뛰나? 그 경우 바뀌어야 할 포인터는 어느 구조체의 어느 필드인가?

</details>

<details>
<summary>힌트 3 — 루프 증감식은 본문이 끝난 "뒤" 에 돈다</summary>

`for (...; ...; cur = cur->next)` 의 `cur = cur->next` 는 본문의 `node_free(cur)` **다음에** 실행된다. 해제하기 전에 무엇을 저장해야 하나? 그리고 노드를 지웠을 때도 `prev = cur` 를 하면 `prev` 는 무엇을 가리키게 되나? (`display cur` 로 루프를 돌려 보라.)

</details>

<details>
<summary>힌트 4 — list_check 가 크래시하지 않으려면</summary>

`n->next` 를 읽기 **전에** `n` 이 살아 있는 풀 노드인지(`node_is_live`) 확인한다. 살아 있는 노드는 최대 `POOL_SIZE` 개이므로 그보다 많이 걸었다면 같은 노드를 다시 밟은 것 = 사이클. Floyd 토끼-거북이는 추가 연습 4.

</details>

## 5. 풀이

### Part A — 1단계: 크래시를 그대로 읽는다 (`demo_crash.txt`)

```text
(lldb) run
Process 82929 launched: '/tmp/TOOL-B10_work/demo/starter_dbg' (arm64)
Process 82929 stopped
* thread #1, queue = 'com.apple.main-thread', stop reason = EXC_BAD_ACCESS (code=1, address=0xdead0000)
    frame #0: 0x0000000100002850 starter_dbg`list_sum(l=0x000000016fdfea20) at starter.c:121:17
   120 	    for (node_t *n = l->head; n != NULL; n = n->next)
-> 121 	        s += n->value;
    	                ^
(lldb) bt
* thread #1, queue = 'com.apple.main-thread', stop reason = EXC_BAD_ACCESS (code=1, address=0xdead0000)
  * frame #0: 0x0000000100002850 starter_dbg`list_sum(l=0x000000016fdfea20) at starter.c:121:17
    frame #1: 0x0000000100001140 starter_dbg`t_delete_head at starter.c:230:5
    frame #2: 0x00000001000006a4 starter_dbg`main(argc=2, argv=0x000000016fdff158) at starter.c:404:17
    frame #3: 0x000000018753e054 dyld`start + 6652
(lldb) frame variable
(const list_t *) l = 0x000000016fdfea20
(long) s = 1802201963
(node_t *) n = 0x00000000dead0000
(lldb) up
frame #1: 0x0000000100001140 starter_dbg`t_delete_head at starter.c:230:5
-> 230 	    CHECK(list_sum(&l) == 5);                       /* 2 + 3 */
(lldb) frame variable
(list_t) l = {
  head = 0x0000000100008008
  len = 2
}
(lldb) p *l.head
(node_t) {
  value = 1802201963
  next = 0x00000000dead0000
}
(lldb) memory read -s4 -fx -c4 l.head
0x100008008: 0x6b6b6b6b 0x00000000 0xdead0000 0x00000000
(lldb) down
(lldb) p *n
error: Couldn't apply expression side effects : Couldn't dematerialize a result variable: couldn't read its memory
```

(소스 목록 일부 생략. 전체는 `demo_crash.txt`.) 한 줄씩:

| 출력 | 읽는 법 |
|---|---|
| `stop reason = EXC_BAD_ACCESS (code=1, address=0xdead0000)` | macOS 의 Mach 예외 이름. `code=1` 은 `KERN_INVALID_ADDRESS`(매핑 안 된 주소)로 알고 있다(확인 권장). 디버거 밖이었다면 SIGSEGV(11)가 됐다 — 하네스가 찍은 `signal 11` 과 같은 사건. 주소가 곧 poison 값 → **해제된 노드의 next 를 따라감** |
| `frame #0 … list_sum(l=…) at starter.c:121:17` | 죽은 함수·줄·열. 열 17 은 `n->value` 의 `value` 접근 |
| `bt` 의 `frame #1 t_delete_head at starter.c:230` | 누가 불렀나. `list_delete` 는 **스택에 없다** — 이미 반환했다. 버그 함수가 bt 에 안 보이는 전형 |
| `s = 1802201963` (`p/x s` → `0x6b6b6b6b`) | 첫 노드의 값으로 poison 이 이미 더해졌다 → head 자체가 해제된 노드 |
| `up` 후 `l = { head = 0x100008008, len = 2 }` | `len` 은 2 로 줄었는데 head 는 그대로 g_pool[0] (풀의 첫 칸) |
| `memory read -s4 -fx -c4 l.head` | 16 바이트 노드 = `value`(4) + 패딩(4) + `next`(8). poison 이 그대로 보인다 |
| `p *n` → `couldn't read its memory` | 디버거 고장이 아니다. `n` 자체가 읽을 수 없는 주소다. lldb 의 `p` 는 `dwim-print` 의 줄임이고(`help p` 실측), 이 경우 표현식 평가로 넘어가 읽기에 실패했다 |

결론: "누가 해제된 노드를 head 에 남겼나?" → `list_delete` 가 의심 1순위. 크래시 줄(121)은 고칠 곳이 아니다.

### Part A — 2단계: breakpoint 걸고 한 줄씩 (`demo_debug.txt`)

```text
(lldb) b list_delete
Breakpoint 1: where = starter_dbg`list_delete + 20 at starter.c:128:13, address = 0x00000001000029e4
(lldb) run
* thread #1, queue = 'com.apple.main-thread', stop reason = breakpoint 1.1
    frame #0: 0x00000001000029e4 starter_dbg`list_delete(l=0x000000016fdfea20, value=1) at starter.c:128:13
-> 128 	    node_t *prev = NULL;
(lldb) frame variable
(list_t *) l = 0x000000016fdfea20
(int) value = 1
(node_t *) prev = 0x0000000000000003
(node_t *) cur = 0x0000000000000003
(lldb) n
-> 129 	    node_t *cur = l->head;
(lldb) n
-> 130 	    while (cur != NULL && cur->value != value) {
(lldb) p *cur
(node_t) {
  value = 1
  next = 0x0000000100008018
}
(lldb) p prev
(node_t *) NULL
(lldb) n
-> 134 	    if (cur == NULL)
(lldb) n
-> 136 	    if (prev != NULL)
(lldb) n
-> 138 	    node_free(cur);
(lldb) s
    frame #0: 0x0000000100002adc starter_dbg`node_free(n=0x0000000100008008) at starter.c:61:23
-> 61  	    if (!node_is_live(n)) {                         /* 이중 해제 · 풀 밖 포인터 */
(lldb) finish
-> 139 	    l->len--;
(lldb) p *cur
(node_t) {
  value = 1802201963
  next = 0x00000000dead0000
}
(lldb) up
frame #1: 0x00000001000010e4 starter_dbg`t_delete_head at starter.c:229:5
-> 229 	    CHECK(list_delete(&l, 1));
(lldb) p l
(list_t) {
  head = 0x0000000100008008
  len = 3
}
(lldb) down
(lldb) c
* thread #1, queue = 'com.apple.main-thread', stop reason = EXC_BAD_ACCESS (code=1, address=0xdead0000)
    frame #0: 0x0000000100002850 starter_dbg`list_sum(l=0x000000016fdfea20) at starter.c:121:17
```

(멈출 때마다 나오는 `Process … stopped` 줄과 앞뒤 소스 줄은 생략. 전체 166줄은 `demo_debug.txt`.)

| # | 명령 | 멈춘 곳 | 알아낸 것 |
|---|---|---|---|
| 1 | `b list_delete` | — | 위치 `list_delete + 20` = 함수 프롤로그(스택 프레임 만들기) 다음 첫 줄 128 |
| 2 | `run` | 128 | `value=1` — `MAKE(1,2,3)` 의 push 들은 이 함수를 안 부르므로 첫 정지가 바로 그 호출 |
| 3 | `frame variable` | 128 | `prev = 0x3`, `cur = 0x3` — **아직 128줄을 실행하기 전**이라 쓰레기 값. "멈춘 줄 = 다음에 실행할 줄" |
| 4 | `n` ×2 | 130 | `cur` = 첫 노드(value 1), `prev = NULL`, `p cur->next->value` → 2. while 조건이 바로 거짓 → 루프를 안 돈다 |
| 5 | `n` ×3 | 134 → 136 → 138 | 136의 `if (prev != NULL)` 이 거짓이라 137을 건너뛰고 138로. **head 를 옮기는 줄이 아무 데도 없다** ← 원인 |
| 6 | `s` | node_free:61 | `s` 는 호출 안으로 들어간다(`n` 이었다면 node_free 를 통째로 실행하고 139). `bt 2` 로 확인 |
| 7 | `finish` | 139 | 호출한 쪽 다음 줄로 복귀. `*cur` 는 이제 poison |
| 8 | `up`, `p l` | t_delete_head:229 | `head = 0x100008008` = 방금 해제된 노드, `len = 3`(감소 전). 다시 `down` |
| 9 | `c` | list_sum:121 | 1단계와 같은 크래시 — 원인과 결과가 이어졌다 |

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">bt 는 "지금 살아 있는" 호출만 보여 준다 · n / s / finish 는 어디서 멈추나</text>
  <text x="10" y="45" fill="currentColor" font-size="12">크래시 시점의 bt (frame #0 이 맨 위)</text>
  <rect x="10" y="55" width="300" height="34" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="20" y="77" fill="currentColor" font-family="monospace" font-size="12">#0 list_sum        starter.c:121</text>
  <rect x="10" y="95" width="300" height="34" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="20" y="117" fill="currentColor" font-family="monospace" font-size="12">#1 t_delete_head   starter.c:230</text>
  <rect x="10" y="135" width="300" height="34" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="20" y="157" fill="currentColor" font-family="monospace" font-size="12">#2 main            starter.c:404</text>
  <rect x="10" y="175" width="300" height="34" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="20" y="197" fill="currentColor" font-family="monospace" font-size="12">#3 dyld`start</text>
  <path d="M320 72 C350 80 350 105 320 112" stroke="currentColor" stroke-width="1.5" fill="none"/>
  <text x="350" y="96" fill="currentColor" font-size="12">up / frame select 1</text>
  <text x="10" y="232" fill="currentColor" font-size="12">list_delete 는 230줄보다 먼저(229줄) 불려서</text>
  <text x="10" y="250" fill="currentColor" font-size="12">이미 반환했다 → bt 에 없다.</text>
  <text x="10" y="268" fill="currentColor" font-size="12">버그 함수는 breakpoint 로 다시 찾아간다.</text>
  <rect x="470" y="40" width="260" height="120" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="480" y="60" fill="currentColor" font-family="monospace" font-size="12">list_delete</text>
  <text x="480" y="82" fill="currentColor" font-family="monospace" font-size="12">136 if (prev != NULL) …</text>
  <text x="480" y="104" fill="currentColor" font-family="monospace" font-size="12">138 node_free(cur);  ●</text>
  <text x="480" y="126" fill="currentColor" font-family="monospace" font-size="12">139 l-&gt;len--;</text>
  <text x="480" y="148" fill="currentColor" font-family="monospace" font-size="12">140 VERIFY_LIST(l);</text>
  <rect x="470" y="200" width="260" height="60" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="480" y="220" fill="currentColor" font-family="monospace" font-size="12">node_free</text>
  <text x="480" y="242" fill="currentColor" font-family="monospace" font-size="12">61 if (!node_is_live(n)) …</text>
  <path d="M640 108 C690 112 690 118 640 122" stroke="currentColor" stroke-width="1.5" fill="none"/>
  <text x="695" y="118" fill="currentColor" font-size="12">n</text>
  <path d="M560 108 L560 228" stroke="currentColor" stroke-width="1.5" stroke-dasharray="4 3"/>
  <text x="566" y="185" fill="currentColor" font-size="12">s (안으로)</text>
  <path d="M700 230 C735 200 735 150 700 128" stroke="currentColor" stroke-width="1.5" fill="none"/>
  <text x="520" y="285" fill="currentColor" font-size="12">finish → 139 (호출 다음 줄)</text>
</svg>
```

수정: `prev == NULL` 이면 지우는 노드가 head 이므로 `l->head = cur->next`.

### Part B — 버그 1: 해제한 노드의 `next` (`demo_delete_all.txt`)

```text
(lldb) run
* thread #1, queue = 'com.apple.main-thread', stop reason = EXC_BAD_ACCESS (code=1, address=0xdead0000)
    frame #0: 0x0000000100002cb0 starter_dbg`list_delete_all(l=0x000000016fdfea18, value=5) at starter.c:150:18
-> 150 	        if (cur->value == value) {
(lldb) frame variable
(list_t *) l = 0x000000016fdfea18
(int) value = 5
(size_t) removed = 1
(node_t *) prev = 0x0000000100008008
(node_t *) cur = 0x00000000dead0000
(lldb) p *prev
(node_t) {
  value = 1802201963
  next = 0x00000000dead0000
}
(lldb) breakpoint set -p "prev = cur;" -X list_delete_all
(lldb) run
Process 82987 exited with status = 9 (0x00000009) killed
Process 83002 launched: '/tmp/TOOL-B10_work/demo/starter_dbg' (arm64)
* thread #1, queue = 'com.apple.main-thread', stop reason = breakpoint 1.1
-> 159 	        prev = cur;
(lldb) display cur
Stop hook #1 added.
(lldb) n
(node_t *) $0 = 0x0000000100008008
-> 160 	    }
(lldb) n
(node_t *) $1 = 0x0000000100008008
-> 149 	    for (node_t *cur = l->head; cur != NULL; cur = cur->next) {
    	                                                   ^
(lldb) n
(node_t *) $2 = 0x00000000dead0000
-> 150 	        if (cur->value == value) {
```

- 이번엔 **크래시 지점 = 버그 지점**이다. `removed = 1` → 첫 삭제 직후에 죽었다. `prev` 가 poison 노드 → 방금 지운 노드를 `prev` 로 삼았고, `cur` 는 그 노드의 `next`(poison)다.
- `breakpoint set -p <정규식> -X <함수>` 는 **그 함수 안에서 정규식에 맞는 소스 줄**에 건다. 줄 번호를 외울 필요가 없고, 같은 문자열이 있는 `list_delete` 의 131줄은 제외된다. `run` 을 다시 하면 lldb 는 기존 프로세스를 죽이고(`status = 9 killed`) 새로 띄운다 — `settings set auto-confirm true` 가 확인 질문을 없앤다.
- 159줄에서 `frame variable removed cur` → `removed = 1`, `cur = 0x100008008`, `p *cur` 는 이미 poison(전체는 txt). `display cur` 는 lldb 에서 **stop-hook**(멈출 때마다 실행할 명령)으로 구현된다(`Stop hook #1 added`). 증감식 `cur = cur->next`(열 52의 `^`)를 실행한 직후 `cur` 가 `0x100008008` → `0xdead0000` 으로 바뀌는 순간이 보인다.
- 수정: 해제 **전에** `next = cur->next` 를 저장하고 증감식을 `cur = next` 로.

### Part B — 버그 2: 지운 노드가 `prev` 로 남는다 (`demo_prev.txt`)

버그 1만 고친 중간 상태(`demo_debug.sh` 가 solution.c 에서 `continue;` 한 줄만 지워 `midfix.c` 를 만든다)는 더 이상 크래시하지 않는다. 대신 `13/16 passed` — `delete_all_mixed` 는 `EQ(l, 1, 2)`, `delete_all_every` 는 `l.head == NULL && …` 에서, `random_ops` 는 `step 37: op 3 value 4` 에서 모델과 어긋난다(`demo_midfix_run.txt`).

`-DVERIFY` 로 빌드해 lldb 에서 돌리면 `list_delete_all` 이 끝나는 곳에서 `abort()` 로 멈춘다:

```text
(lldb) frame select 3
frame #3: 0x0000000100002f64 midfix_dbg`list_delete_all(l=0x000000016fdfea18, value=5) at midfix.c:164:5
-> 164 	    VERIFY_LIST(l);
(lldb) p *l->head
(node_t) {
  value = 1
  next = 0x0000000100008038
}
(lldb) p *l->head->next
(node) {
  value = 1802201963
  next = 0x00000000dead0000
}
(lldb) breakpoint set -p "prev->next = next;" -X list_delete_all
(lldb) run
-> 157 	                prev->next = next;
(lldb) frame variable value prev cur
(int) value = 5
(node_t *) prev = 0x0000000100008018
(node_t *) cur = 0x0000000100008028
(lldb) c
-> 157 	                prev->next = next;
(lldb) frame variable prev cur
(node_t *) prev = 0x0000000100008028
(node_t *) cur = 0x0000000100008038
(lldb) p *prev
(node_t) {
  value = 1802201963
  next = 0x00000000dead0000
}
```

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">[5,1,5,5,2,5] 에서 delete_all(5) — 버그 2 (prev 가 지운 노드에 남음), 풀 주소 하위 16비트</text>
  <rect x="10" y="40" width="100" height="40" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="18" y="58" fill="currentColor" font-family="monospace" font-size="12">A 5 (8008)</text>
  <text x="18" y="74" fill="currentColor" font-size="12">1번째 삭제</text>
  <rect x="130" y="40" width="100" height="40" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="138" y="58" fill="currentColor" font-family="monospace" font-size="12">B 1 (8018)</text>
  <text x="138" y="74" fill="currentColor" font-size="12">새 head</text>
  <rect x="250" y="40" width="100" height="40" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="258" y="58" fill="currentColor" font-family="monospace" font-size="12">C 5 (8028)</text>
  <text x="258" y="74" fill="currentColor" font-size="12">B-&gt;next = D</text>
  <rect x="370" y="40" width="100" height="40" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="378" y="58" fill="currentColor" font-family="monospace" font-size="12">D 5 (8038)</text>
  <text x="378" y="74" fill="currentColor" font-size="12">prev = C (해제됨)</text>
  <rect x="490" y="40" width="100" height="40" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="498" y="58" fill="currentColor" font-family="monospace" font-size="12">E 2 (8048)</text>
  <rect x="610" y="40" width="100" height="40" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="618" y="58" fill="currentColor" font-family="monospace" font-size="12">F 5 (8058)</text>
  <text x="10" y="110" fill="currentColor" font-size="12">C 삭제: prev = B (살아 있음) → B-&gt;next = D. 올바름. 그런데 그 뒤 prev = cur = C (방금 해제한 노드).</text>
  <text x="10" y="130" fill="currentColor" font-size="12">D 삭제: prev = C → C-&gt;next = E 를 해제된 C 에 씀. 살아 있는 B-&gt;next 는 여전히 D (곧 해제됨).</text>
  <text x="10" y="150" fill="currentColor" font-size="12">lldb 두 번째 정지: prev = 0x…8028 (C), *prev = {0x6b6b6b6b, 0xdead0000} ← 해제된 노드에 쓰려는 순간</text>
  <text x="10" y="180" fill="currentColor">결과</text>
  <text x="60" y="180" fill="currentColor" font-family="monospace" font-size="12">head → B(1) → D(poison) → 0xdead0000</text>
  <text x="10" y="205" fill="currentColor" font-size="12">크래시 없음. list_check = LC_DANGLING(1), EQ(l, 1, 2) 실패, random_ops 는 step 37 에서 어긋남.</text>
  <text x="10" y="230" fill="currentColor" font-size="12">수정: 지웠으면 prev 를 그대로 둔다 (continue). 버그 1이 크래시로 이 버그를 가리고 있었다.</text>
</svg>
```

`list_free` 도 같은 모양이다: `for (n = head; n; n = n->next) node_free(n);` 은 첫 노드를 해제한 뒤 `n->next`(poison)를 읽고(테스트 `free` 크래시), 고쳐도 `head`/`len` 을 안 비우면 해제된 노드를 가리키는 **dangling head** 가 남는다(`free` 테스트의 `l.head == NULL && l.len == 0`).

### Part C — `list_check` 와 `-DVERIFY`

정답(6. 정답 코드)은 8줄이다: 노드마다 `node_is_live(n)` 가 거짓이면 `LC_DANGLING`, 센 개수가 `POOL_SIZE` 를 넘으면 `LC_CYCLE`, 끝에서 개수 ≠ `len` 이면 `LC_LEN`.

- **따라가기(`n->next`) 전에 검사**가 핵심이다. `node_is_live` 는 `uintptr_t` 로 범위를 비교하고(서로 다른 배열의 포인터를 `<` 로 비교하는 건 C 에서 UB 라서), 16바이트 경계인지, 해제 표시가 없는지 본다. 풀 밖 포인터(스택의 `outside` 노드)도 DANGLING.
- 사이클 판정은 풀 크기를 아는 덕에 단순하다. 풀을 모르는 일반 리스트면 Floyd(추가 연습 4).
- starter 의 버그 + 이 `list_check` 를 `-DVERIFY` 로 빌드하면(`demo_debug.sh` 가 awk 로 `list_check` 본문만 갈아 끼운 `verify.c`) — `demo_verify.txt`:

```text
(lldb) run
VERIFY: list_check=1 in list_delete
* thread #1, queue = 'com.apple.main-thread', stop reason = signal SIGABRT
    frame #0: 0x00000001875d3338 libsystem_kernel.dylib`__pthread_kill + 8
(lldb) bt
* thread #1, queue = 'com.apple.main-thread', stop reason = signal SIGABRT
  * frame #0: 0x00000001875d3338 libsystem_kernel.dylib`__pthread_kill + 8
    frame #1: 0x000000018760f700 libsystem_pthread.dylib`pthread_kill + 276
    frame #2: 0x00000001876a2568 libsystem_c.dylib`abort + 148
    frame #3: 0x0000000100002d80 verify_dbg`list_delete(l=0x000000016fdfea30, value=1) at verify.c:140:5
    frame #4: 0x00000001000010e4 verify_dbg`t_delete_head at verify.c:235:5
(lldb) frame select 3
-> 140 	    VERIFY_LIST(l);
(lldb) frame variable
(list_t *) l = 0x000000016fdfea30
(int) value = 1
(node_t *) prev = NULL
(node_t *) cur = 0x0000000100008008
(int) vc_ = 1
(lldb) p *l
(list_t) {
  head = 0x0000000100008008
  len = 2
}
```

1단계에서는 bt 에 없던 `list_delete` 가 이제 frame #3 이고, 그 프레임의 `prev = NULL`, `l->head == cur` 가 원인을 바로 말해 준다. 디버그 정보가 없는 시스템 라이브러리 프레임(#0–#2)은 어셈블리로 보이므로 `frame select 3`(또는 `up 3`)으로 내 코드까지 올라간다. fork 하네스로 전부 돌리면(`demo_verify_run.txt`) `delete_head`, `delete_only`, `delete_dup_first` 가 `crashed: signal 6 (Abort trap: 6)` 로 바뀌고, `check_*` 4개가 통과해 9/16 이다.

**펌웨어 관점.** 같은 구성이 실제로 쓰인다: (1) 고정 풀 + 해제 시 패턴 채우기(디버그 빌드), (2) 자료구조 변경 뒤 무결성 검사 `ASSERT` — 실패하면 `__BKPT()`/HardFault 로 디버거가 그 자리에서 멈춤, (3) 릴리스에서는 `VERIFY_LIST` 를 `((void)0)` 으로 빼서 비용 0(검사는 O(n) 이라 연산마다 돌리면 O(n²)). 주의: Cortex-M 에는 MMU 가 없어서 **`0xDEAD0000` 이나 `0x0` 이 "유효한 주소" 일 수 있다** — 주소 0 은 보통 벡터 테이블/플래시 별칭이라 NULL 읽기가 조용히 성공한다. 그래서 펌웨어는 MPU 로 0 번지 근처를 접근 금지로 만들거나, poison 값을 그 칩에서 BusFault 가 나는 영역으로 고른다(칩의 메모리 맵 확인 필요).

### gdb 로 같은 세션 (Lima VM, `demo_gdb.txt`)

이 VM 의 네이티브 gdb 는 `Unable to fetch SVE/SSVE vector length` 로 `run` 을 못 한다(ARM-B09 에서 확인). 그래서 `gcc -std=c11 -g -O0 -static` 으로 빌드하고 `qemu-aarch64 -g 23410 ./starter_g delete_head &` 의 gdbstub 에 `gdb-multiarch -batch -x demo_gdb.gdb` 로 붙었다. `set trace-commands on` 이라 실행한 명령이 `+` 로 찍힌다:

```text
+target remote :23410
0x00000000004005c0 in _start ()
+break list_delete
Breakpoint 1 at 0x400b44: file starter.c, line 128.
+continue
Breakpoint 1, list_delete (l=0xf2850c1ef900, value=1) at starter.c:128
+info locals
prev = 0xf2850c1ef930
cur = 0x401434 <t_delete_head+72>
+display cur
+next
129	    node_t *cur = l->head;
1: cur = (node_t *) 0x401434 <t_delete_head+72>
+next
130	    while (cur != NULL && cur->value != value) {
1: cur = (node_t *) 0x4a1918 <g_pool>
+print *cur
$2 = {value = 1, next = 0x4a1928 <g_pool+16>}
+step
node_free (n=0x4a1918 <g_pool>) at starter.c:61
+finish
list_delete (l=0xf2850c1ef900, value=1) at starter.c:139
+print *cur
$5 = {value = 1802201963, next = 0xdead0000}
+up
#1  0x0000000000401444 in t_delete_head () at starter.c:229
+print l
$6 = {head = 0x4a1918 <g_pool>, len = 3}
+continue
Program received signal SIGSEGV, Segmentation fault.
0x0000000000400afc in list_sum (l=0xf2850c1ef900) at starter.c:121
+frame 1
+print *l.head
$8 = {value = 1802201963, next = 0xdead0000}
+x/4xw l.head
0x4a1918 <g_pool>:	0x6b6b6b6b	0x00000000	0xdead0000	0x00000000
```

(발췌, 전체 79줄은 `demo_gdb.txt`.) 차이점: gdb 는 포인터 옆에 **심볼+오프셋**(`<g_pool+16>`)을 붙여 줘서 "풀의 두 번째 칸" 이 바로 보인다. 정적 링크라 `g_pool` 이 `0x4a1918`(PIE 아님), 스택 주소는 qemu 실행마다 달랐다(`0xe4579dbff900`, `0xe42d717ef900`, `0xf2850c1ef900` — 세 번 실행 실측). lldb 는 기본으로 ASLR 을 끄므로(`settings show target.disable-aslr` → `true` 실측) 다시 돌려도 같은 주소가 나왔다(`g_pool` = `0x100008008`, 같은 경로·인자로 다시 돌리면 스택 주소도 같았다). Linux 에서는 크래시가 Mach 예외가 아니라 바로 `SIGSEGV` 로 보고된다.

### lldb ↔ gdb 명령 대응표

| 하는 일 | lldb (이 맥) | gdb | 이 레슨에서 실제로 돌렸나 |
|---|---|---|---|
| 인자 주고 시작 | `lldb -- ./p delete_head`, `run` | `gdb --args ./p delete_head`, `run` | lldb 예 / gdb 는 원격이라 `continue` |
| 함수 breakpoint | `b list_delete` | `break list_delete` | 둘 다 |
| 파일:줄 breakpoint | `b starter.c:138` (`breakpoint set -f starter.c -l 138`) | `break starter.c:138` | 아니오 |
| 소스 정규식 breakpoint | `breakpoint set -p "prev = cur;" -X list_delete_all` | (없음, `rbreak` 는 **함수 이름** 정규식) | lldb |
| 다음 줄(호출은 통째로) | `n` = `thread step-over` | `next` | 둘 다 |
| 호출 안으로 | `s` = `thread step-in` | `step` | 둘 다 |
| 함수 끝까지 → 호출자 | `finish` = `thread step-out` (반환값 표시) | `finish` (`Value returned`) | 둘 다 |
| 특정 줄까지 | `thread until 32` | `until 32` / `advance 32` | lldb (추가 연습 1) |
| 계속 | `c` = `process continue` | `continue` | 둘 다 |
| 호출 스택 | `bt`, `bt 2` | `bt`, `bt 2` | 둘 다 |
| 프레임 이동 | `up`, `down`, `frame select 3` (`f 3`) | `up`, `down`, `frame 3` | 둘 다 |
| 지역 변수 · 인자 | `frame variable` (`v`), `v idx n` | `info locals`, `info args` | 둘 다 |
| 값 출력 | `p *cur`, `p/x s` (`dwim-print`) | `print *cur`, `p/x s` | 둘 다 |
| 메모리 | `memory read -s4 -fx -c4 l.head` (`x`) | `x/4xw l.head` | 둘 다 |
| 멈출 때마다 출력 | `display cur` (stop-hook) | `display cur`, `undisplay 1` | 둘 다 |
| 워치포인트 | `watchpoint set variable l->head` | `watch l->head` | 아니오 (TOOL-M07) |
| 원격 연결 | `gdb-remote 23410` | `target remote :23410` | gdb |
| 끝내기 | `kill`, `q` | `kill`, `quit` | gdb `kill` |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution                  # 16/16 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sa && ./sa   # 16/16, 보고 없음
cc -std=c11 -Wall -Wextra -Werror -O2 -DVERIFY solution.c -o sv && ./sv                    # 16/16 (매 변경마다 list_check)
```

실행 시간은 이 맥에서 0.02–0.04 s(5회). 바뀐 곳은 네 함수뿐이다:

| 함수 | 바뀐 줄 | 왜 |
|---|---|---|
| `list_delete` | `else l->head = cur->next;` | 앞 노드가 없으면 "앞 노드의 next" 대신 `head` 가 그 링크다 |
| `list_delete_all` | `next = cur->next` 를 해제 전에, 증감식 `cur = next`, 지웠으면 `continue` | 해제 후 읽기 금지 · 지운 노드는 `prev` 가 될 수 없다 |
| `list_free` | 같은 `next` 저장 + 끝에서 `head = NULL; len = 0` | 해제 후 읽기 금지 · dangling head 금지 |
| `list_check` | 위 Part C | 따라가기 전에 검사 |

참고: 이 풀은 `malloc` 이 아니므로 ASan 은 이 레슨의 해제 후 사용을 **모른다**(전역 배열에 쓰는 평범한 쓰기로 보인다). starter 를 ASan 으로 돌려도 리포트 대신 SIGSEGV 크래시만 난다(실측: `6/16 passed`, ASan 리포트 없음). 진짜 `malloc`/`free` 였다면 ASan 이 `heap-use-after-free` 로 바로 잡는다 — TOOL-B11.

## 7. 자주 하는 실수

**1. 크래시 난 줄을 고친다.** `list_sum` 에 `if (n == POISON_PTR) break;` 같은 걸 넣으면 테스트 하나는 지나가도 리스트는 여전히 해제된 노드를 가리킨다(값 0x6b6b6b6b 가 합에 들어간다). → bt 와 변수로 "나쁜 포인터를 만든 쪽" 을 찾는다. 크래시 줄은 증상이다.

**2. `-O2` 바이너리로 디버깅한다.** 변수가 레지스터에만 있다가 사라지고(`optimized out`), 줄 순서가 뒤섞이고, 인라인된 함수는 breakpoint 가 이상한 곳에 걸린다(TOOL-B03). → 버그를 좁힐 땐 `-g -O0`(필요하면 `-Og`). 최적화에서만 나는 버그면 그때 `-O2 -g` 로.

**3. fork 하는 테스트 러너를 그대로 디버거에 올린다.** `lldb -o run -- ./starter_dbg`(인자 없음, `demo_fork.txt`)는 FAIL 줄 16개 뒤 `6/16 passed`, `Process 82845 exited with status = 1 (0x00000001)` 로 끝났다. 크래시는 자식 프로세스에서 났고 lldb 는 부모만 보고 있어서 **한 번도 안 멈췄다**. → 한 테스트를 fork 없이 돌리는 모드를 쓴다(`./starter_dbg delete_head`, gtest `--gtest_filter`, Check `CK_FORK=no`). gdb 는 `set follow-fork-mode child` 로 자식을 따라갈 수 있다(이 레슨에서 실행하지는 않음).

**4. 디버거 아래에서만 다르게 도는 코드.** 이 하네스의 첫 버전은 `waitpid` 를 한 번만 불렀는데, lldb 아래에서는 매 테스트마다 `waitpid` 가 `-1, errno = EINTR (Interrupted system call)` 를 돌려줘 전부 `fork/wait error` 가 됐다(`w.c` 로 따로 확인: 디버거 없이는 EINTR 0번, lldb 아래에서는 fork 마다 1번). → 느린 시스템 콜은 `EINTR` 이면 다시 부른다(지금 하네스의 `while (… < 0 && errno == EINTR)`).

**5. "멈춘 줄은 이미 실행됐다" 고 읽는다.** breakpoint 가 128줄에 걸렸을 때 `prev = 0x3` 은 쓰레기다 — 128줄은 **아직** 실행 전이다. `n` 한 번 뒤에 봐야 한다. 같은 이유로 `finish` 직후 `p l` 의 `len = 3` 은 139줄(`len--`) 실행 전 값이다. gdb 도 같았다(`info locals` → `cur = 0x401434 <t_delete_head+72>`, 이전 호출이 남긴 스택 내용). 반대로 함수 입구에서 **인자**가 이상하면 호출자를 의심한다(`up`).

**6. 해제 후 읽기가 "지금은 동작하니까" 넘어간다.** 진짜 `free()` 는 메모리를 바로 덮지 않을 때가 많아 `cur = cur->next` 가 대부분 맞는 값을 읽는다 → 릴리스에서만, 가끔, 다른 곳에서 터진다. → 해제 전에 `next` 를 저장하는 습관 + 디버그 빌드에서 poison/ASan.

**7. 하나 고치고 끝낸다.** 버그 1을 고치자 크래시가 사라졌지만 `delete_all_mixed` 는 여전히 틀렸다(13/16). 크래시 버그가 다른 버그를 **가리고** 있었다. → 고친 뒤 전체 테스트(무작위 모델 비교 포함)를 다시 돌린다.

## 8. 면접 Q&A

<details>
<summary>Q1. 크래시가 난 함수와 버그가 있는 함수가 다른 이유는? 이 경우 디버거로 어떻게 좁히나요?</summary>

나쁜 상태(해제된 노드를 가리키는 head)를 만든 함수는 이미 반환했고, 그 상태를 **나중에 쓰는** 함수가 터지기 때문이다. bt 는 지금 살아 있는 호출만 보여 주므로 버그 함수가 없다. 좁히는 순서: (1) 크래시 주소의 성격(0 → NULL, poison → 해제 후 사용, 그럴듯한 힙 주소 → 덮어쓰기)으로 가설, (2) `up` 해서 그 포인터가 어디서 왔는지(여기선 `l.head`), (3) 그 포인터를 바꾸는 함수에 breakpoint 를 걸고 재실행해 `n` 으로 따라가기, (4) 반복이 많으면 불변식 검사(`-DVERIFY`)로 "처음 깨지는 순간" 에 멈추게 하기 — 이 레슨에서 bt 의 frame #3 이 바로 `list_delete` 가 됐다. 그다음 도구는 watchpoint(`l->head` 가 바뀌는 순간, TOOL-M07)와 역방향 디버깅(rr — 확인 권장).

</details>

<details>
<summary>Q2. step, next, finish, until 의 차이는?</summary>

`next`(`n`): 현재 줄을 실행하고 같은 함수의 다음 줄에서 멈춘다. 줄 안의 호출은 통째로 실행된다(그 안의 breakpoint 는 걸린다). `step`(`s`): 줄 안에 호출이 있으면 그 함수의 첫 줄로 들어간다(실측: 138줄 → `node_free:61`). 디버그 정보가 없는 함수(libc 등)는 기본적으로 들어가지 않는다(이 맥 lldb 의 `target.process.thread.step-in-avoid-nodebug = true` 실측; gdb 도 줄 정보 없는 함수는 건너뛴다). `finish`: 현재 함수가 반환할 때까지 실행하고 호출자에서 멈춘다; 반환값을 보여 준다(추가 연습 1: `Return value: (node_t *) $0 = nullptr`). `until <줄>`(lldb `thread until 32`): 그 줄까지 실행 — 루프를 한 번에 건너뛸 때. 명령어 단위는 `si`/`ni`(ARM-B09).

</details>

<details>
<summary>Q3. 왜 -O0 -g 로 디버깅하나요? 릴리스 바이너리(-O2)에서 크래시가 났다면?</summary>

`-g` 는 DWARF 디버그 정보(줄 번호 ↔ 주소, 변수 위치, 타입)를 넣고 코드 자체는 바꾸지 않는다. `-O0` 은 변수를 스택에 두고 줄 순서대로 코드를 내서 `frame variable` 과 `n` 이 소스와 1:1 로 맞는다. `-O2` 에서는 변수가 레지스터에만 잠깐 있거나 사라지고(`optimized out`), 인라인·재배치로 `n` 이 앞뒤로 뛴다(TOOL-B03). 릴리스에서만 나는 크래시는 같은 소스를 `-O2 -g` 로 빌드해 재현하고(코드는 같고 정보만 추가), 배포 바이너리는 strip 하더라도 심볼 파일을 보관해 주소를 줄로 바꾼다(dSYM/`atos`, `addr2line` — TOOL-M14, 코어 덤프 사후 분석은 TOOL-M11).

</details>

<details>
<summary>Q4. EXC_BAD_ACCESS (code=1, address=0xdead0000) 를 보면 무엇을 알 수 있나요? address=0x0 이면요?</summary>

macOS 에서 잘못된 메모리 접근의 Mach 예외이고, 디버거가 없으면 SIGSEGV(이 레슨 하네스: `signal 11`)로 바뀐다. `address` 는 접근하려던 주소다. 0x0(또는 0x8 같은 작은 값) → NULL 포인터(또는 NULL 구조체의 필드) 역참조(추가 연습 2 실측: `address=0x0`, `cur->next == NULL`). 정해 둔 poison(0xdead0000, Linux `LIST_POISON`, 0x6b6b…/0xdeadbeef 같은 패턴) → 해제 후 사용. 그럴듯하지만 매핑 안 된 주소 → 덮어쓴 포인터나 스택 손상. 그래서 debug 빌드에 **눈에 띄는 poison 값**을 쓰는 것이다. Linux 에서는 `SIGSEGV` + `si_addr` 로 같은 정보를 얻는다.

</details>

<details>
<summary>Q5. 해제 후 사용(use-after-free)을 디버거 말고 어떻게 더 빨리 잡나요?</summary>

(1) ASan: `free` 된 메모리를 독(poison) 처리하고 격리(quarantine)해 바로 재사용하지 않으므로 첫 접근에서 리포트(TOOL-B11). (2) 직접 만든 풀이면 이 레슨처럼 해제 시 패턴 채우기 + 재사용 지연 — 재사용하면 poison 이 덮여 dangling 포인터가 **살아 있는 다른 노드**를 가리키는 최악의 경우가 된다. (3) 불변식 검사(`list_check`)를 변경마다 디버그 빌드에서. (4) 해제 후 포인터를 NULL 로(`list_free` 의 `head = NULL`). (5) 언어 수준에선 소유권이 명확한 API(pointer-to-pointer 삭제, 추가 연습 3). 펌웨어 타깃에선 ASan 이 없으니 (2)(3) + MPU 가 주력이다.

</details>

<details>
<summary>Q6. Cortex-M 보드에서 같은 버그를 디버깅한다면 무엇이 다른가요?</summary>

흐름은 같다: 호스트의 `arm-none-eabi-gdb`/`gdb-multiarch` 가 GDB 서버(OpenOCD, J-Link GDB Server, pyOCD)에 `target remote :3333` 으로 붙고, 서버가 SWD/JTAG 로 코어를 멈추고 레지스터·메모리를 읽는다(이 레슨의 qemu gdbstub 원격과 같은 모양, TOOL-A03). 다른 점: (1) 코드가 플래시에 있으면 소프트웨어 breakpoint(명령어 덮어쓰기)를 못 써서 **하드웨어 breakpoint** 를 쓰는데 개수가 적다(FPB, 코어마다 다름 — 보통 수 개, 데이터시트 확인). (2) MMU 가 없어 NULL/poison 접근이 폴트가 안 날 수 있다 → MPU 설정 또는 HardFault 핸들러에서 스택 프레임(PC, LR) 보기(ARM-M12). (3) 멈춰도 주변장치·DMA 는 계속 돌 수 있다. (4) 실시간 동작이 깨지므로 printf 대신 RTT/ITM 트레이스(TOOL-A04).

</details>

<details>
<summary>Q7. head 를 특별 취급하지 않고 리스트에서 노드를 지우는 방법은?</summary>

"현재 노드를 가리키는 링크의 주소" `node_t **pp` 를 들고 다닌다. 처음엔 `pp = &l->head`, 지나갈 땐 `pp = &cur->next`, 지울 땐 `*pp = cur->next` — head 든 중간이든 같은 코드다(추가 연습 3, 흔히 Linus 의 "good taste" 예로 알려진 방식). `prev` 변수가 없으니 이 레슨의 버그 1(head 미갱신)과 버그 2(지운 노드가 prev 로 남음)가 생길 자리가 없다. 정렬 삽입(추가 연습 2)도 같은 방식이면 빈 리스트·맨 앞·맨 끝이 한 루프로 처리된다.

</details>

## 9. 추가 연습

`demo_practice_bug.c` 에 버그를 심은 원본이 있다. 디버거로 찾고, 고친 버전을 practice.c 에서 확인한다. 리스트 연산 자체는 DS-B05 에서 다뤘으니 여기서는 **찾는 과정**이 목적이다.

<!-- code: demo_practice_bug.c -->

### 연습 1 — `list_nth` off-by-one 을 lldb 로

`./pbug nth` 는 `sum += list_nth(&l, i)->value` 에서 죽는다(`[10,20,30]`). bt, `frame variable i sum l.len`, `b list_nth`, `thread until 32`, `finish` 로 원인을 찾아라.

<details>
<summary>답 보기</summary>

`demo_practice.txt` 요약: 크래시는 **main** 에서 `address=0x0`, `i = 2`, `sum = 50`(10+20+30 이 아니라 20+30). `b list_nth` 후 `thread until 32`(return 줄)에서 `idx = 0` 인데 `*n = {value = 20}`, `idx = 1` → `{value = 30, next = NULL}`, `idx = 2` 는 `finish` 의 `Return value: (node_t *) $0 = nullptr` → 호출자가 NULL 을 역참조. 루프 조건 `i <= idx` 가 한 칸 더 간다 → `i < idx`. 또 호출자는 `list_nth` 가 NULL 을 줄 수 있음을 처리해야 한다(범위 밖 idx). practice.c `t_nth`: 합 60, `nth(3)`, `nth(100)`, 빈 리스트 → NULL.

</details>

### 연습 2 — `list_insert_sorted` 의 NULL 검사 누락

`./pbug sorted` 로 `[10,20,30]` 에 40 을 넣으면 죽는다. 크래시 주소와 `p *cur`, `p cur->next` 로 설명하고, 맨 앞·가운데·맨 끝·빈 리스트·같은 값을 모두 처리하게 고쳐라.

<details>
<summary>답 보기</summary>

`demo_practice_sorted.txt`: `EXC_BAD_ACCESS (code=1, address=0x0)` at `while (cur->next->value < nn->value)`, `*cur = {value = 30, next = NULL}`, `p cur->next` → `NULL`. 마지막 노드까지 와서 `cur->next->value` 로 NULL 을 읽었다 — "끝에 넣기" 경우를 빠뜨렸다. 수정은 `while (cur->next != NULL && cur->next->value < v)` 이거나, 더 낫게 pointer-to-pointer: `while (*pp != NULL && (*pp)->value < value) pp = &(*pp)->next; n->next = *pp; *pp = n;` — `&&` 의 단락 평가로 NULL 검사가 먼저다. 같은 값은 기존 값 **앞**에 들어가지만(조건이 `<`) 순서상 구별이 없다.

</details>

### 연습 3 — `prev` 없는 삭제 (pointer-to-pointer)

`list_delete_all_pp(list_t *l, int value)` 를 `node_t **pp` 하나로 써라. head 특례도 `prev` 도 없어야 한다.

<details>
<summary>답 보기</summary>

`pp = &l->head; while (*pp) { cur = *pp; if (cur->value == value) { *pp = cur->next; node_free(cur); … } else pp = &cur->next; }`. 지웠을 때 `pp` 를 그대로 두는 것이 Part B 의 "지웠으면 prev 그대로" 와 같은 뜻이고, 링크를 먼저 고친 뒤 해제하므로 해제 후 읽기가 없다. practice.c: `[5,1,5,5,2,5]` → 4개 삭제, `[1,2]`, `g_bad_free == 0`; 전부 같은 값, 아무것도 안 맞는 경우.

</details>

### 연습 4 — Floyd 사이클 검출 (O(1) 메모리)

풀 크기를 모르는 일반 리스트에서 `bool list_has_cycle(const list_t *l)` 를 추가 메모리 없이 써라(DS-B05 복습 — 여기선 Part C 와 비교가 목적).

<details>
<summary>답 보기</summary>

거북이(1칸)와 토끼(2칸)를 head 에서 출발시켜 `fast != NULL && fast->next != NULL` 동안 진행; 만나면 사이클. 사이클이 없으면 토끼가 먼저 NULL 에 닿는다. 사이클이 있으면 둘 다 사이클 안에 들어간 뒤 거리가 매 걸음 1씩 줄어 반드시 만난다. 시간 O(n). practice.c: 빈 리스트, 노드 1개, 자기 자신, `5 → 3`, `2 → 1`. Part C 의 `list_check` 는 풀 크기라는 상한을 알기 때문에 더 단순한 "N 걸음 넘으면 사이클" 을 썼다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice        # 4/4 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined practice.c -o pa && ./pa   # 4/4, 보고 없음
cc -std=c11 -g -O0 demo_practice_bug.c -o pbug && lldb -b -s demo_practice.lldb -- ./pbug nth            # 연습 1 세션
```

`./pbug` 를 디버거 밖에서 그냥 돌리면 이 맥은 크래시 리포트(.ips)를 남긴다 — lldb 아래에서 돌리자.

## 10. 핵심 정리

- 크래시를 받으면: **stop reason + 주소**(0 = NULL, poison = 해제 후 사용) → `bt` → `up`/`frame select N` → `frame variable` / `p *ptr` / `memory read` 로 "그 포인터는 어디서 왔나".
- bt 에는 살아 있는 호출만 있다. 나쁜 상태를 만든 함수는 이미 반환했을 수 있다 → 그 함수에 `b`, 재실행, `n`/`s`/`finish`/`thread until` 로 원인 줄까지.
- 멈춘 줄은 **아직 실행 전**. `n` 은 호출을 통째로, `s` 는 안으로, `finish` 는 호출자로(반환값 표시).
- 리스트 삭제 3대 버그: head 미갱신(`prev == NULL` 경우), 해제 후 `cur->next`(해제 전에 저장), 지운 노드를 `prev` 로(지웠으면 prev 유지). `node_t **pp` 로 쓰면 셋 다 생길 자리가 없다.
- 크래시를 버그 쪽으로 당기기: 해제 시 poison, 불변식 검사 + `-DVERIFY` 의 `abort()` → 디버거가 버그 함수 안에서 멈춘다. 릴리스에선 0 비용.
- 디버깅은 `-g -O0`. fork 하는 테스트 러너는 단일 테스트·fork 없음 모드로. 디버거 아래에서는 `EINTR` 같은 차이가 실제로 생긴다.
- gdb: `break / run / next / step / finish / until / bt / frame N / info locals / print / x/4xw / display / target remote`. 펌웨어는 같은 gdb 가 OpenOCD/J-Link 서버에 원격으로 붙는다.

## 참고자료

- LLDB — GDB to LLDB command map: <https://lldb.llvm.org/use/map.html>, Tutorial: <https://lldb.llvm.org/use/tutorial.html>
- GDB 매뉴얼 — Stopping and Continuing, Examining the Stack, Examining Data (`x`, `display`), Remote Debugging: <https://sourceware.org/gdb/current/onlinedocs/gdb/>
- QEMU User Mode — gdbstub (`-g port`): <https://www.qemu.org/docs/master/user/main.html>
- Linux `include/linux/poison.h` (LIST_POISON1/2, POISON_FREE 0x6b): <https://github.com/torvalds/linux/blob/master/include/linux/poison.h>
- googletest `--gtest_filter`, Check 프레임워크 `CK_FORK=no`(fork 모드 끄기): <https://google.github.io/googletest/advanced.html>, <https://libcheck.github.io/check/doc/check_html/check_4.html>
- Arm — Cortex-M FPB/DWT (하드웨어 breakpoint·watchpoint 개수는 코어별 TRM 참고): <https://developer.arm.com/documentation/>
- 관련 레슨: ARM-B09 (lldb 명령어 단위·레지스터, gdb 원격), TOOL-B03 (최적화 레벨과 optimized out), TOOL-B11 (ASan heap-use-after-free), TOOL-M07 (조건부 BP·워치포인트·스크립팅), TOOL-M11 (코어 덤프), TOOL-M14 (DWARF·addr2line/atos), TOOL-A03 (QEMU gdbstub 로 OpenOCD 흐름), TOOL-A04 (RTT/ITM), ARM-M12 (HardFault 분석), DS-B05 (단일 연결 리스트·Floyd), DS-B07 (정적 노드 풀), DS-M01 (Linux list_head·poison)
