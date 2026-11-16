---
id: OS-A10
time: 120분
updated: 2026-11-15
---
# OS-A10 xv6 커널 해킹 2: COW fork 구현

## 학습 목표

- xv6-riscv 의 `uvmcopy()` 를 **복사 없는 fork** 로 바꾼다. 쓰기 가능 페이지는 부모·자식 양쪽 PTE 에서 `PTE_W` 를 지우고 RSW 비트(`PTE_COW = 1<<8`)를 켠다. 물리 페이지마다 **참조 카운트**를 두고 마지막 참조가 사라질 때만 free list 로 돌려보낸다.
- 저장 페이지 폴트(`scause == 15`)를 `usertrap()` 에서 잡아 `cowfault()` 로 보낸다. `stval` 이 가리키는 페이지가 COW 이면 refcount 1 일 때는 `PTE_W` 만 되살리고, 아니면 `kalloc` → 복사 → 재매핑 → 옛 페이지 `kfree` 를 한다. COW 가 아닌 주소, MAXVA 이상, 메모리 부족이면 프로세스를 죽인다.
- **커널이 사용자 메모리에 쓰는 길(`copyout`)에서는 폴트가 나지 않는다**는 점을 이해하고 거기서도 COW 를 깬다. `read()` 버퍼와 `wait(&status)` 가 실제로 이 길을 탄다.
- 결정적인 테스트로 "정말 COW 인가"를 **페이지 수**로 증명한다. fork 7 페이지 대 eager 87 페이지, 자식의 첫 쓰기 10 번이 정확히 10 페이지, 메모리 0 에서도 refcount-1 쓰기 성공, 누수 0 을 보인다. `usertests -q` 도 통과시킨다.
- 같은 일을 Linux 6.18.55 가 어떻게 하는지(`do_wp_page`, `wp_can_reuse_anon_folio`, `ptep_clear_flush`) 읽고, MMU 없는 펌웨어(uClinux, Cortex-M)에서는 COW 가 왜 불가능한지 말할 수 있다.

## 1. 문제

"fork 는 메모리를 다 복사하나요? 그럼 커널은 언제, 어떻게 복사하죠?" 면접관이 OS-M03 수준의 답("COW 라서 쓸 때 복사합니다")을 들은 뒤 던지는 꼬리질문은 늘 같은 곳으로 간다. **누가 참조 수를 세나, 커널이 직접 쓰는 경우는, TLB 는 언제 비우나, 메모리가 없으면 누가 죽나**. 임베디드 리눅스 보드(256 MB 램)에서 큰 데몬이 `system()` 을 부를 때 fork 가 실패하는지, 그리고 MMU 없는 MCU 에는 왜 fork 가 아예 없는지도 이 답에서 나온다. 이 레슨에서는 그 메커니즘을 **실제 커널에 직접 넣는다**. 교육용 유닉스 xv6-riscv(MIT)를 Lima VM 의 `qemu-system-riscv64` 에서 띄우고 커널 C 코드를 고친다.

이미 한 것: OS-M03 은 macOS 에서 `ru_minflt` 로 COW **관찰**만 했다(자식이 k 페이지 쓰면 폴트 k+1). OS-M01 은 페이지 테이블 관리와 TLB 무효화 규칙, ARCH-A01/A02 는 워커와 TLB 모델, OS-A06 은 같은 아이디어를 사용자 공간(mprotect + SIGSEGV)에서 한다. 여기서는 **커널 쪽**만 한다. OSTEP 23장(VAX/VMS 의 copy-on-write, demand zeroing)과 21장(페이지 폴트 처리 흐름)이 배경이다. 과제 출처는 MIT 6.1810 의 *Lab: Copy-on-Write Fork for xv6* 다(https://pdos.csail.mit.edu/6.1810/2025/labs/cow.html — 연도 경로는 해마다 바뀐다). 랩의 아이디어만 빌렸고 코드와 테스트는 이 레슨용으로 새로 썼다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="a10a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">COW fork 직후: 두 페이지 테이블이 같은 프레임을 가리키고, 프레임마다 참조 수를 센다</text>
  <text x="20" y="48" fill="currentColor">부모 PTE (fork 뒤)</text>
  <rect x="20" y="56" width="190" height="26" class="f-warn" stroke="currentColor"/><text x="28" y="74" fill="currentColor" font-family="monospace" font-size="12">VA 0x0000 → F1  R-X U</text>
  <rect x="20" y="82" width="190" height="26" class="f-acc" stroke="currentColor"/><text x="28" y="100" fill="currentColor" font-family="monospace" font-size="12">VA 0xb000 → F2  R-- U COW</text>
  <rect x="20" y="108" width="190" height="26" class="f-acc" stroke="currentColor"/><text x="28" y="126" fill="currentColor" font-family="monospace" font-size="12">VA 0x4000 → F3  R-- U COW</text>
  <text x="530" y="48" fill="currentColor">자식 PTE (새 페이지 테이블)</text>
  <rect x="530" y="56" width="190" height="26" class="f-warn" stroke="currentColor"/><text x="538" y="74" fill="currentColor" font-family="monospace" font-size="12">VA 0x0000 → F1  R-X U</text>
  <rect x="530" y="82" width="190" height="26" class="f-acc" stroke="currentColor"/><text x="538" y="100" fill="currentColor" font-family="monospace" font-size="12">VA 0xb000 → F2  R-- U COW</text>
  <rect x="530" y="108" width="190" height="26" class="f-acc" stroke="currentColor"/><text x="538" y="126" fill="currentColor" font-family="monospace" font-size="12">VA 0x4000 → F3  R-- U COW</text>
  <rect x="300" y="56" width="140" height="26" class="f-mut" stroke="currentColor"/><text x="310" y="74" fill="currentColor" font-family="monospace" font-size="12">F1 텍스트 ref=2</text>
  <rect x="300" y="82" width="140" height="26" class="f-mut" stroke="currentColor"/><text x="310" y="100" fill="currentColor" font-family="monospace" font-size="12">F2 데이터 ref=2</text>
  <rect x="300" y="108" width="140" height="26" class="f-mut" stroke="currentColor"/><text x="310" y="126" fill="currentColor" font-family="monospace" font-size="12">F3 스택   ref=2</text>
  <path d="M210 69 L298 69" stroke="currentColor" marker-end="url(#a10a)"/><path d="M530 69 L442 69" stroke="currentColor" marker-end="url(#a10a)"/>
  <path d="M210 95 L298 95" stroke="currentColor" marker-end="url(#a10a)"/><path d="M530 95 L442 95" stroke="currentColor" marker-end="url(#a10a)"/>
  <path d="M210 121 L298 121" stroke="currentColor" marker-end="url(#a10a)"/><path d="M530 121 L442 121" stroke="currentColor" marker-end="url(#a10a)"/>
  <text x="20" y="170" fill="currentColor" font-weight="bold">자식이 VA 0x4000 에 저장 → store page fault (scause 15, stval = 0x4f88)</text>
  <rect x="530" y="185" width="190" height="26" class="f-ok" stroke="currentColor"/><text x="538" y="203" fill="currentColor" font-family="monospace" font-size="12">VA 0x4000 → F4  RW- U</text>
  <rect x="300" y="185" width="140" height="26" class="f-ok" stroke="currentColor"/><text x="310" y="203" fill="currentColor" font-family="monospace" font-size="12">F4 새 복사본 ref=1</text>
  <rect x="300" y="215" width="140" height="26" class="f-mut" stroke="currentColor"/><text x="310" y="233" fill="currentColor" font-family="monospace" font-size="12">F3 스택   ref=1</text>
  <path d="M530 198 L442 198" stroke="currentColor" marker-end="url(#a10a)"/>
  <rect x="20" y="215" width="190" height="26" class="f-acc" stroke="currentColor"/><text x="28" y="233" fill="currentColor" font-family="monospace" font-size="12">VA 0x4000 → F3  R-- U COW</text>
  <path d="M210 228 L298 228" stroke="currentColor" marker-end="url(#a10a)"/>
  <text x="20" y="270" fill="currentColor">부모가 나중에 0x4000 에 쓰면: F3 ref == 1 → 복사 없이 PTE_W 만 되살린다 (reuse).</text>
  <text x="20" y="292" fill="currentColor">텍스트 F1 은 원래 읽기 전용 → COW 비트 없이 공유. 여기에 쓰면 폴트 → 프로세스 kill (복사하면 안 된다).</text>
  <text x="20" y="314" fill="currentColor">fork 의 비용 = 페이지 테이블 몇 장 + trapframe (실측 7 페이지). 데이터 페이지는 0 장.</text>
</svg>
```

**환경 (이 레슨의 모든 출력)**: Lima VM `elinux` (aarch64 Ubuntu 26.04, 6 vCPU) 안에서 `riscv64-linux-gnu-gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0`, `QEMU emulator version 10.2.1 (Debian 1:10.2.1+ds-1ubuntu3.3)`, GNU gdb 17.1(`gdb-multiarch`). xv6-riscv 는 upstream main **commit `fa2ddcb7`** (2026-11-05, "usertests: bound the zero-length pipe read regression test") 를 VM 의 `~/.esn_scratch/xv6-riscv` 에 클론한 것이다. 아래 `파일:줄` 은 모두 이 커밋 기준이다. QEMU 게스트는 128 MB 램, `CPUS=3`(Makefile 기본값)이다. 3 hart 를 쓰면 부모와 자식이 서로 다른 hart 에서 동시에 폴트를 내므로 참조 카운트 락이 실제로 경합한다. gdb 데모만 결정성을 위해 1 hart 로 돈다.

**이 커밋의 xv6 는 이미 "lazy sbrk" 가 있다.** `sys_sbrk(n, SBRK_LAZY)` 는 `p->sz` 만 늘리고(sysproc.c:54-62), 처음 건드릴 때 `vmfault()`(vm.c:458-478)가 페이지를 만든다. `usertrap()` 은 이미 `scause 13/15` 를 `vmfault` 로 보낸다(trap.c:71-74). 그래서 COW 처리기는 **그 앞에** 끼워야 한다. 이미 매핑된 COW 페이지에 대해 `vmfault` 는 `ismapped` 라서 0 을 돌려주고, 그러면 프로세스가 죽는다. 이 점이 인터넷의 옛 풀이(2020~2023 xv6)와 다르다.

### Part A — 참조 카운트 + COW `uvmcopy` + 폴트 처리기

- `kernel/riscv.h`: `PTE_COW` 를 RSW 비트로 정의한다. riscv.h:395-399 에는 V R W X U(비트 0~4)만 이름이 있고, `PTE_FLAGS` 마스크 `0x3FF`(riscv.h:406)가 비트 0~9 를 덮는다. RISC-V 특권 스펙의 Sv39 PTE 에서 비트 5 G, 6 A, 7 D, **8~9 RSW(Reserved for Supervisor Software, 하드웨어가 무시)** 다.
- `kernel/kalloc.c`: `kref.cnt[(pa − KERNBASE)/PGSIZE]` 와 그것을 지키는 스핀락. `kalloc` 은 1 로 세팅하고, `kfree` 는 1 을 빼서 **0 이 될 때만** free list 에 넣는다. `freerange` 는 부팅 때 1 로 세팅하고 kfree 한다. `krefinc`, `krefcnt` 도 만든다.
- `kernel/vm.c`: `uvmcopy` 는 kalloc·memmove 없이 부모 프레임을 자식에 매핑한다. 쓰기 가능했던 페이지는 **양쪽 PTE** 에서 W 를 끄고 COW 를 켠다. `krefinc` 를 부른다.
- `kernel/vm.c` `cowfault(pagetable, va)` 와 `kernel/trap.c` 분기: `scause == 15` 이고 COW 페이지면 해결하고 0 을 돌려준다. 아니면 −1 → lazy 경로 → 거기서도 안 되면 kill.

### Part B — 커널 쓰기(copyout)와 경계 사례

- `copyout()`(vm.c:344-377)은 사용자 VA 를 **소프트웨어로 걸어서** 얻은 물리 주소에 `memmove` 한다. MMU 를 거치지 않으므로 폴트가 없다. COW 페이지면 여기서 `cowfault` 를 직접 부른다. 원래 있던 "읽기 전용 텍스트에는 쓰지 않는다" 검사(vm.c:363-365)는 지켜야 한다.
- refcount 1 재사용: 다른 쪽이 이미 복사했거나 종료했으면 새 페이지 없이 W 만 되살린다. **메모리가 0 일 때도** 성공해야 한다.
- kill 사례: `p->sz` 위, 텍스트 페이지, 트램폴린(`MAXVA − PGSIZE`, U 비트 없음), `MAXVA` 자체, `0xfffffffffffff000`. 커널은 panic 하지 않고 그 프로세스만 죽여야 한다. 복사 중 `kalloc` 실패도 마찬가지다.

### Part C — 검증과 실제 시스템

- `user/cowtest.c`(새로 작성, 18 테스트)와 `usertests -q` 를 `run_xv6.py` 가 QEMU 콘솔로 돌려 판정한다. 테스트 훅으로 `freepages()` 시스템 콜(free list 길이)을 하나 추가한다. xv6 원래 기능이 아니라 이 레슨의 측정 장치다.
- gdb 로 실제 COW 폴트 하나를 멈춰 `scause/stval/sepc` 와 PTE 비트가 바뀌는 것을 본다(`check.sh gdb`).
- Linux 6.18.55 의 같은 경로(`__copy_present_ptes` → `do_wp_page` → `wp_page_copy`)와 TLB 처리 차이, MMU 없는 시스템(uClinux: fork 없음 → vfork/posix_spawn)을 비교한다.

## 2. Problem (English)

> Implement copy-on-write fork in xv6-riscv (commit fa2ddcb7).
>
> ```c
> // kalloc.c — one reference count per physical page (index (pa - KERNBASE) / PGSIZE)
> void *kalloc(void);          // count = 1
> void  kfree(void *pa);       // count -= 1; put on the free list only when it reaches 0
> void  krefinc(void *pa);     // one more page table maps pa
> int   krefcnt(void *pa);
> // vm.c
> int   uvmcopy(pagetable_t old, pagetable_t new, uint64 sz); // share frames; writable -> !W + PTE_COW in both
> int   cowfault(pagetable_t pagetable, uint64 va);           // 0 = resolved, -1 = kill the process
> int   copyout(pagetable_t, uint64 psz, uint64 dstva, char *src, uint64 len); // must break COW itself
> ```
>
> `usertrap()` must send store page faults (`scause == 15`) on COW pages to `cowfault()` before the existing
> lazy-allocation path. A write to a page whose count is 1 must not allocate. Faults on non-COW pages, at or
> above `MAXVA`, or when `kalloc()` fails kill only the faulting process; the kernel must never panic.
> Read-only text pages are shared but never become writable. Add a test hook syscall `freepages()`.
> Example: a process of 81 pages forks using 7 new pages; 10 first writes in the child use exactly 10 pages;
> a process owning 60 % of RAM can fork. All of `cowtest` and `usertests -q` pass.
> Follow-ups: where does xv6 flush the TLB after changing a PTE? What breaks if `copyout` ignores COW?
> How does Linux decide between reuse and copy? Why can't an MMU-less MCU run fork()?

## 3. 시작 코드

이 레슨은 starter.c 한 파일이 아니라 **xv6 위에 덮어쓰는 오버레이 디렉터리** 3 개(`starter/`, `solution/`, `practice/`)다. 각 디렉터리는 xv6 경로를 그대로 따르고, 바뀐 파일은 **파일 전체**를 담는다. 그래서 `solution/X` 를 `starter/X` 와 diff 하면 바뀐 줄만 강조된다. 오버레이 `.c` 파일에는 첫 줄에 `/* ENV: qemu ... */` 주석을 하나 더 달았다(사이트 빌드 표시용). 그래서 오버레이의 줄 번호는 원본보다 1 이상 밀린다. 본문의 `파일:줄` 은 원본 커밋 기준이다.

| 파일 | starter 상태 |
|---|---|
| `Makefile` | `UPROGS` 에 `_cowtest` 추가 (완성) |
| `kernel/syscall.h/.c`, `sysproc.c`, `user/usys.pl`, `user/user.h`, `kernel/defs.h` | `freepages()` 테스트 훅 + 프로토타입 (완성) |
| `kernel/riscv.h` | `#define PTE_COW 0` — **TODO** |
| `kernel/kalloc.c` | `kref` 배열·락·`kfreepages` 완성, refcount 갱신 4 곳 **TODO** |
| `kernel/vm.c` | `uvmcopy` 는 원래의 eager 복사, `copyout` 그대로, `cowfault` 는 −1 을 돌려주는 스텁 — **TODO** |
| `kernel/trap.c` | COW 분기 자리에 주석만 — **TODO** |
| `user/cowtest.c` | 테스트 프로그램 (완성, starter/solution 동일) |

<!-- code: starter/kernel/vm.c -->

<!-- code: starter/kernel/kalloc.c -->

<!-- code: starter/user/cowtest.c -->

`check.sh` 는 **VM 안에서** 돈다. 원본 클론을 `/tmp/OS-A10_work/<variant>.<pid>/xv6` 로 rsync 하고, 오버레이를 덮어쓰고, `make kernel/kernel fs.img` 를 한 뒤 `run_xv6.py` 를 부른다. 원본 클론은 읽기만 한다. 매번 새로 복사하므로 지난번의 `fs.img` 나 `.o` 가 남지 않는다(빌드 1 초 미만). `run_xv6.py` 는 stdlib 만 쓰는 드라이버다. `make qemu CPUS=3` 을 자기 프로세스 그룹으로 띄우고, 논블로킹 stdout 에서 `$ ` 프롬프트를 기다린 뒤 명령을 보낸다. `PASS 이름` 줄을 모으고, 기대 목록에 있는데 줄이 없으면 FAIL 이다. `panic` 이나 타임아웃이면 남은 것을 모두 FAIL 로 친다. 끝나면 QEMU 그룹을 `SIGKILL` 하고 `N/M passed` 를 찍는다.

<!-- code: check.sh -->

<!-- code: run_xv6.py -->

```bash
L=/Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/OS/OS-A10
limactl shell elinux -- bash -lc "bash $L/check.sh starter"     # 시작 코드: FAIL, exit 1 (약 10 초)
limactl shell elinux -- bash -lc "bash $L/check.sh solution"    # cowtest + usertests -q (약 60 초)
limactl shell elinux -- bash -lc "bash $L/check.sh practice"    # + cowpractice
limactl shell elinux -- bash -lc "bash $L/check.sh bug nocopyout"   # 7장의 버그 하나 주입
limactl shell elinux -- bash -lc "bash $L/check.sh gdb"         # cowfault 에서 멈춰 레지스터·PTE 보기
```

시작 코드 실행(실제 출력, 발췌). eager 복사라서 COW 를 요구하는 4 개가 FAIL 이고 나머지는 원래 xv6 도 통과한다:

```text
    | cowtest: start, 32487 free pages
    | simple: fork of a 19491-page heap failed, 12958 pages free
    | counts: process 81 pages, fork used 87 pages, 10 child writes used 0 pages
    | cowtest: 14/18 passed
FAIL cowtest/simple  (fork failed (eager copy needs twice the memory))
FAIL cowtest/fork-cheap  (fork allocated about as many pages as the process owns)
FAIL cowtest/write-cost  (first write to a shared page must cost exactly one page)
FAIL cowtest/oom-kill  (fork failed)
14/18 passed  (QEMU CPUS=3, wall 7.2 s)
RESULT: FAIL (starter) console log: /tmp/OS-A10_work/starter.last-console.log
```

## 4. 힌트

<details>
<summary>힌트 1 — 참조 카운트 배열의 크기와 초기값</summary>

xv6 는 `KERNBASE`(0x80000000)부터 `PHYSTOP`(+128 MB)까지만 할당한다(memlayout.h:43-44). 그러면 `(PHYSTOP − KERNBASE)/PGSIZE = 32768` 칸이고 int 면 128 KB 다(커널 BSS). 커널 이미지가 차지하는 앞쪽 칸은 안 쓰이지만 계산이 단순하다. 함정은 `kinit()` → `freerange()` → `kfree()` 다. kfree 가 "1 빼고 0 이면 해제" 라면 부팅 때 카운트가 0 이라 음수가 된다. freerange 에서 1 로 세팅하고 kfree 를 부른다.

</details>

<details>
<summary>힌트 2 — uvmcopy: 무엇을 COW 로 만들고 무엇을 안 만드나</summary>

`PTE_W` 가 켜진 페이지만 COW 로 바꾼다. **부모 PTE 를 먼저 고친 뒤** 그 flags 를 자식에게 준다. 원래 읽기 전용인 텍스트(`R|X|U`)는 COW 비트 없이 그대로 공유한다. 이 페이지에 쓰는 것은 버그이므로 폴트 → kill 이 맞다. 이미 COW 인 페이지(fork 의 fork)는 W 가 없고 COW 가 있으니 그대로 공유된다. 매핑할 때마다 `krefinc(pa)` 한다. 실패 경로의 `uvmunmap(new, 0, i/PGSIZE, 1)` 은 kfree 로 카운트를 돌려주니 그대로 둬도 된다.

</details>

<details>
<summary>힌트 3 — usertrap 의 분기 순서</summary>

기존 분기는 `(scause == 15 || scause == 13) && vmfault(...) != 0` 이다. `vmfault` 는 매핑이 이미 있으면 0 을 돌려주므로, COW 페이지가 여기로 가면 "unexpected scause" 로 죽는다. 그래서 `else if (r_scause() == 15 && cowfault(p->pagetable, r_stval()) == 0)` 를 **앞에** 둔다. `cowfault` 가 −1 이면 다음 분기(lazy)로 넘어가고, 거기서도 안 되면 else 가 kill 한다. 폴트 주소는 `sepc` 가 아니라 `stval` 이다.

</details>

<details>
<summary>힌트 4 — cowfault 의 검사 순서</summary>

① `va >= MAXVA` 면 −1. `walk()` 는 MAXVA 이상에서 **panic** 한다(vm.c:101-102). ② `walk(pagetable, va, 0)` 가 0 이면 −1. ③ `V|U|COW` 가 모두 켜져 있어야 한다. U 가 없는 트램폴린·trapframe, COW 가 없는 텍스트는 −1 이다. ④ `krefcnt == 1` 이면 `W` 켜고 `COW` 끄고 끝. ⑤ 아니면 kalloc, 실패면 −1. memmove 하고 PTE 를 새 PA + `(flags | W) & ~COW` 로 바꾼 뒤 `kfree(옛 pa)` 로 내 참조를 내려놓는다.

</details>

<details>
<summary>힌트 5 — copyout 이 왜 따로 필요한가, 어떻게 확인하나</summary>

`copyout` 은 `walkaddr` 로 얻은 물리 주소(커널 직접 매핑)에 쓴다. 사용자 PTE 의 W 비트를 하드웨어가 검사할 기회가 없다. 원래 코드는 W 가 없으면 −1 을 돌려주므로, 고치지 않으면 `read()` 가 −1 을 돌려준다. 게다가 **`wait(&st)` 가 실패**한다(proc.c:391 이 copyout). `pte` 를 구한 뒤 `PTE_COW && !PTE_W` 면 `cowfault(pagetable, va0)` 를 부르고 `pa0 = PTE2PA(*pte)` 로 다시 읽는다. PTE 슬롯 자체는 같으므로 `pte` 포인터는 그대로 유효하다.

</details>

## 5. 풀이

### 0. 지도 — 이 커밋에서 손대는 곳

| 위치 (commit fa2ddcb7) | 원래 하는 일 | COW 에서 할 일 |
|---|---|---|
| proc.c:271 `kfork` → `uvmcopy` | 부모 메모리를 페이지마다 kalloc+복사 | 그대로 호출 (uvmcopy 내부만 바뀜) |
| vm.c:298-326 `uvmcopy` | eager 복사 | 공유 + W 끄기 + COW 켜기 + krefinc |
| trap.c:54-79 `usertrap` | `scause` 8=syscall, 13/15 → `vmfault` (lazy), 그 외 kill | 15 + COW → `cowfault` 를 lazy 앞에 |
| vm.c:458-478 `vmfault` | lazy 페이지 할당 (이미 매핑이면 0) | 그대로 |
| vm.c:344-377 `copyout` | walkaddr 후 W 없으면 −1, PA 에 memmove | W 검사 앞에서 COW 해결 |
| kalloc.c:46-63 `kfree`, 68-82 `kalloc` | free list push/pop | refcount 감소/초기화 |
| vm.c:202-212 `uvmunmap(…, do_free)` | kfree | 그대로 (kfree 가 refcount 를 본다) |
| trampoline.S:88,94 (uservec), 109-111 (userret) | `sfence.vma zero, zero` 앞뒤로 `csrw satp` | 그대로 — **이게 TLB 플러시를 대신한다** |

`usertrap` 은 `p->trapframe->epc = r_sepc()` 로 폴트 난 명령 주소를 저장한다(trap.c:52). syscall 이 아니면 `epc += 4` 를 하지 않는다. 그래서 폴트를 해결하고 돌아가면 **같은 저장 명령이 다시 실행되고**, 이번에는 W 가 있어서 성공한다. 이것이 "폴트 → 고치고 → 재시도" 계약이다.

### Part A — 참조 카운트: 배열 하나, 락 하나

설계 선택지는 둘이다. (1) xv6 처럼 물리 페이지 번호로 인덱싱하는 **고정 배열**은 단순하고 O(1) 이다. 대신 램 크기에 비례하는 메타데이터(여기 128 KB = 32 페이지)를 미리 잡는다. starter 의 free pages 가 32487 인 것이 그 값이 빠진 뒤의 숫자다. (2) Linux 처럼 `struct page`/`folio` 안의 `_refcount` 는 같은 아이디어인데, 원래 있는 페이지 메타데이터에 얹는다. 락은 **전역 스핀락 하나**를 쓴다. 3 hart 정도에서는 문제없고, 확장하려면 페이지별 원자 카운터(`__atomic_fetch_sub`)가 맞다(Q&A 7).

```c
kref.cnt[PA2IDX(p)] = 1;          // freerange: kfree() below drops it to 0 and frees
...
acquire(&kref.lock);              // kfree
if (kref.cnt[PA2IDX(pa)] < 1) panic("kfree: refcount");
int left = --kref.cnt[PA2IDX(pa)];
release(&kref.lock);
if (left > 0) return;             // another page table still maps it
```

`< 1` panic 은 디버깅 장치다. 이중 해제를 그 자리에서 잡는다. 7장의 `norefcount` 버그가 정확히 이것으로 멈춘다. 카운트는 "이 프레임을 매핑한 **페이지 테이블 수**" 다. 페이지 테이블 페이지, trapframe, 파이프 버퍼처럼 사용자 매핑이 아닌 kalloc 페이지도 1 을 갖는다. 그래서 kalloc/kfree 의 "1 로 시작, 0 이면 해제" 규칙 하나로 모두 그대로 동작한다.

### Part A — uvmcopy: 복사하지 않는 복사

```c
pa = PTE2PA(*pte);
if (*pte & PTE_W)                       // writable: both sides become read-only + COW
  *pte = (*pte & ~PTE_W) | PTE_COW;
flags = PTE_FLAGS(*pte);                // read-only text stays plain read-only (no COW)
if (mappages(new, i, PGSIZE, pa, flags) != 0) goto err;
krefinc((void *)pa);
```

**부모 쪽 W 도 꺼야 하는 이유**: 자식만 읽기 전용으로 만들면, 부모의 쓰기가 공유 프레임에 그대로 들어가서 자식에게 보인다. 7장 `childonly` 버그가 `isolation`·`wait-copyout` 을 깨는 것으로 이것을 확인한다. **텍스트를 COW 로 만들지 않는 이유**: 텍스트에 쓰는 것은 정당한 쓰기가 아니라 버그다. COW 로 표시하면 그 쓰기가 "성공" 해서 사적인 쓰기 가능 텍스트가 생긴다. `kill-text-write` 테스트가 이것을 지킨다.

fork 비용 실측(`fork-cheap`): 81 페이지 프로세스의 fork 에 **7 페이지**가 들었다. 대략 페이지 테이블(루트 + 아래 영역 L1/L0 + 트램폴린 영역 L1/L0)과 trapframe 1 장이고, 자식 스택의 첫 쓰기 복사 1 장이 섞였을 수 있다. 정확한 분해는 재지 않았다. eager starter 에서는 같은 fork 가 **87 페이지**였다.

### Part A — 폴트 처리: usertrap → cowfault

```svg
<svg viewBox="0 0 740 400" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="a10b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="15" width="300" height="34" class="f-mut" stroke="currentColor"/>
  <text x="30" y="37" fill="currentColor" font-family="monospace" font-size="12">사용자 sd/sb (VA, W=0) → 트랩</text>
  <rect x="20" y="70" width="300" height="34" class="f-mut" stroke="currentColor"/>
  <text x="30" y="92" fill="currentColor" font-family="monospace" font-size="12">uservec: satp=커널, sfence.vma</text>
  <rect x="20" y="125" width="300" height="40" class="f-acc" stroke="currentColor"/>
  <text x="30" y="142" fill="currentColor" font-family="monospace" font-size="12">usertrap: epc=sepc, scause==15 ?</text>
  <text x="30" y="158" fill="currentColor" font-family="monospace" font-size="12">cowfault(pagetable, stval)</text>
  <path d="M170 49 L170 68" stroke="currentColor" marker-end="url(#a10b)"/>
  <path d="M170 104 L170 123" stroke="currentColor" marker-end="url(#a10b)"/>
  <rect x="390" y="15" width="330" height="30" class="f-warn" stroke="currentColor"/>
  <text x="400" y="35" fill="currentColor" font-family="monospace" font-size="12">va ≥ MAXVA → -1 (walk 는 panic!)</text>
  <rect x="390" y="55" width="330" height="30" class="f-warn" stroke="currentColor"/>
  <text x="400" y="75" fill="currentColor" font-family="monospace" font-size="12">PTE 에 V|U|COW 가 다 없음 → -1</text>
  <rect x="390" y="95" width="330" height="30" class="f-ok" stroke="currentColor"/>
  <text x="400" y="115" fill="currentColor" font-family="monospace" font-size="12">ref == 1 → PTE |= W, &amp;= ~COW (reuse)</text>
  <rect x="390" y="135" width="330" height="30" class="f-warn" stroke="currentColor"/>
  <text x="400" y="155" fill="currentColor" font-family="monospace" font-size="12">kalloc() == 0 → -1 (OOM)</text>
  <rect x="390" y="175" width="330" height="44" class="f-ok" stroke="currentColor"/>
  <text x="400" y="193" fill="currentColor" font-family="monospace" font-size="12">memmove(new, old) ; PTE = new|W</text>
  <text x="400" y="210" fill="currentColor" font-family="monospace" font-size="12">kfree(old)  // ref 하나 내려놓기</text>
  <path d="M320 145 L388 30" stroke="currentColor" marker-end="url(#a10b)"/>
  <path d="M555 45 L555 53" stroke="currentColor" marker-end="url(#a10b)"/>
  <path d="M555 85 L555 93" stroke="currentColor" marker-end="url(#a10b)"/>
  <path d="M555 125 L555 133" stroke="currentColor" marker-end="url(#a10b)"/>
  <path d="M555 165 L555 173" stroke="currentColor" marker-end="url(#a10b)"/>
  <rect x="20" y="250" width="300" height="44" class="f-warn" stroke="currentColor"/>
  <text x="30" y="268" fill="currentColor" font-family="monospace" font-size="12">-1 → (15|13) &amp;&amp; vmfault() ? lazy</text>
  <text x="30" y="285" fill="currentColor" font-family="monospace" font-size="12">아니면 printk + setkilled → kexit(-1)</text>
  <rect x="390" y="250" width="330" height="44" class="f-ok" stroke="currentColor"/>
  <text x="400" y="268" fill="currentColor" font-family="monospace" font-size="12">0 → prepare_return → userret:</text>
  <text x="400" y="285" fill="currentColor" font-family="monospace" font-size="12">sfence.vma; csrw satp; sfence.vma</text>
  <path d="M440 220 L170 248" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#a10b)"/>
  <path d="M600 220 L600 248" stroke="currentColor" marker-end="url(#a10b)"/>
  <rect x="390" y="320" width="330" height="30" class="f-ok" stroke="currentColor"/>
  <text x="400" y="340" fill="currentColor" font-family="monospace" font-size="12">sret → sepc 의 같은 저장 명령 재실행 → 성공</text>
  <path d="M555 294 L555 318" stroke="currentColor" marker-end="url(#a10b)"/>
  <text x="20" y="335" fill="currentColor">점선: 경고 박스(-1)들은 모두 왼쪽 kill 경로로 간다.</text>
  <text x="20" y="380" fill="currentColor">copyout 은 이 그림의 위 절반(트랩)을 거치지 않고 cowfault 를 직접 부른다.</text>
</svg>
```

트랩 레지스터 세 개(RISC-V 특권 스펙의 supervisor CSR):

| CSR | 뜻 | 이 레슨에서 실측한 값 (gdb, sh 의 자식 pid 3) |
|---|---|---|
| `scause` | 원인. 8 = U 모드 ecall, 12/13/15 = 명령/로드/저장 페이지 폴트 | `0xf` = 15 (store/AMO page fault) |
| `stval` | 폴트 난 **가상 주소** (페이지 폴트일 때) | `0x4f88` → 페이지 `0x4000` (스택) |
| `sepc` | 폴트 낸 **명령의 주소** | `0x870` |

자주 헷갈리는 점: `sepc` 는 코드 주소, `stval` 은 데이터 주소다. `cowfault` 에 `sepc` 를 넘기면 텍스트 페이지를 보게 되고, 그 페이지는 COW 가 아니라서 −1 → 이유 없는 kill 이 된다. AMO(원자 연산, `amoswap` 등)도 저장으로 분류되어 15 가 난다. 그래서 COW 페이지의 스핀락 단어에 대한 `amoswap` 도 같은 경로로 해결된다.

### Part B — copyout: 하드웨어가 대신 검사해 주지 않는 쓰기

```c
pte = walk(pagetable, va0, 0);
if ((*pte & PTE_COW) && (*pte & PTE_W) == 0) {
  if (cowfault(pagetable, va0) < 0) return -1;
  pa0 = PTE2PA(*pte);         // same PTE slot, new frame (or same if refcount 1)
}
if ((*pte & PTE_W) == 0) return -1;   // forbid copyout over read-only user text pages
```

xv6 커널은 모든 물리 램을 VA = PA 로 직접 매핑해 둔다(vm.c:42-43 `kvmmap(... PHYSTOP ...)`). 그래서 `copyout` 은 사용자 페이지 테이블을 소프트웨어로 걸어서 PA 를 얻고 거기에 쓴다. **이 쓰기는 커널 매핑(쓰기 가능)으로 나가므로 사용자 PTE 의 W=0 은 아무 효과가 없다.** COW 를 무시하고 그냥 쓰면 공유 프레임이 바뀐다(다른 프로세스에 보인다). 원래 코드처럼 W 검사로 거부하면 `read()`·`pipe()`·`fstat()`·`wait()` 가 실패한다. 테스트 세 개가 이 길을 잡는다:

- `pipe-copyout`: 자식이 `read(fd, cbuf + PGSIZE − 100, 200)` 한다. 두 COW 페이지에 걸친 버퍼다. piperead 는 바이트마다 copyout 한다(pipe.c:133).
- `wait-copyout`: 부모가 `wait(&gstatus)` 를 부르는데, `gstatus` 가 있는 .bss 페이지는 아직 살아 있는 다른 자식과 공유 중이다. kwait 의 copyout(proc.c:391)이 COW 를 깨야 하고, 그 다른 자식은 여전히 0 을 봐야 한다.
- `copyout-readonly`: `read(fd, (char *)report, 8)` 처럼 텍스트에 쓰는 read 는 여전히 −1 이어야 한다.

### Part B — refcount 1 재사용과 경쟁 조건

`krefcnt(pa) == 1` 이면 이 프레임을 매핑한 페이지 테이블은 **나 하나**다. 그러면 복사하지 않고 `W` 를 켜고 `COW` 를 끈다. 이 검사가 경쟁에서 안전한 이유: 카운트를 **올릴** 수 있는 것은 이 프레임을 이미 매핑한 프로세스의 fork(uvmcopy)뿐이다. 매핑한 것이 나뿐이면 다른 hart 가 그 사이에 올릴 수 없다. xv6 프로세스는 스레드가 하나라서 내 fork 와 내 폴트가 동시에 일어나지도 않는다. 반대로 카운트가 2 일 때 두 프로세스가 동시에 폴트하면 둘 다 복사하고 둘 다 kfree → 0 → 원본 해제가 된다. 복사가 하나 낭비될 뿐 결과는 맞다.

재사용 테스트는 둘이다. `reuse-refcount-1` 은 자식이 끝난 뒤 부모가 16 페이지에 써도 free pages 가 그대로인지 본다. 그런데 이 테스트는 "항상 복사" 버그(`alwayscopy`)를 **못 잡는다**. 복사 1 장 + 옛 프레임 해제 1 장 = 순변화 0 이기 때문이다(7장 실측). 그래서 `reuse-at-oom` 을 만들었다. 자식 X 가 refcount-1 COW 페이지 16 장을 만든다. 그 뒤 "먹보" E 가 sbrk 로 **남은 메모리를 0 까지** 가져간다. 그 상태에서 X 가 16 장에 쓴다. 올바른 구현은 할당이 필요 없으므로 성공하고, 항상 복사하면 kalloc 이 실패해서 X 가 죽는다. "숫자가 같다" 보다 "자원이 0 일 때도 되나" 가 더 날카로운 테스트다.

### Part B — 죽여야 하는 폴트들

| 테스트 | 저장 주소 | 왜 COW 가 아닌가 | 콘솔 (실측) |
|---|---|---|---|
| `kill-beyond-sz` | `sbrk(0) + 16·PGSIZE` | 매핑 없음, `va ≥ p->sz` → vmfault 도 0 | `scause 0xf … stval=0x21000` |
| `kill-text-write` | `report` 함수 (VA 0) | `R\|X\|U`, COW 비트 없음 | `stval=0x0` |
| `kill-trampoline` | `MAXVA − PGSIZE` | U 비트 없음 (커널 전용 매핑) | `stval=0x3ffffff000` |
| `kill-maxva` | `MAXVA` = `1<<38` | walk 가 panic 할 주소 | `stval=0x4000000000` |
| `kill-high-va` | `0xfffffffffffff000` | Sv39 상위 절반, xv6 는 안 씀 | `stval=0xfffffffffffff000` |
| `oom-kill` | 힙 전체에 쓰기 | COW 지만 kalloc 실패 | `cowfault: out of memory, pid 16 va 0x3299000` |

모두 `wait()` 상태가 −1 이고 커널은 계속 돈다. `nomaxva`(MAXVA 검사 삭제)를 주입하면 `panic: walk` 로 커널 전체가 멈춘다. 사용자 프로그램 하나가 시스템을 죽이는, 가장 나쁜 종류의 버그다.

### Part B — TLB: xv6 에는 왜 `sfence.vma` 가 없어도 되나

PTE 를 W=1 → W=0 으로 **권한을 줄이는** 변경(uvmcopy 의 부모 쪽)은 원칙적으로 TLB 무효화가 필요하다. 옛 W=1 항목이 TLB 에 남아 있으면 부모의 쓰기가 폴트 없이 공유 프레임에 들어간다(OS-M01 의 "권한 축소는 플러시 필수" 규칙). xv6 는 이것을 **구조로** 해결한다:

- 사용자 → 커널 진입(trampoline.S:88,94)과 커널 → 사용자 복귀(trampoline.S:109-111)마다 `sfence.vma zero, zero` 로 TLB **전체**를 비운다. ASID 를 쓰지 않으므로 매번 전부 비우는 것이 정확성의 근거다.
- fork 중인 부모는 커널 안에 있고, 사용자 코드로 돌아가려면 반드시 userret 을 지난다. xv6 프로세스는 스레드가 하나라서 같은 페이지 테이블로 **다른 hart 에서 동시에 도는 사용자 코드가 없다**. 그래서 원격 TLB shootdown(IPI)도 필요 없다.
- cowfault 의 W=0 → W=1(권한 확대)은 원래 플러시가 필요 없는 방향이다. 어차피 userret 이 비운다.

Linux 는 이 가정이 하나도 맞지 않는다. 스레드가 여러 CPU 에서 같은 mm 을 쓰고 ASID/PCID 로 TLB 를 보존한다. 그래서 fork 는 `wrprotect_ptes` 뒤 `dup_mmap()` 끝에서 `flush_tlb_mm(oldmm)` 을 부르고(mm/mmap.c:1873), 복사 경로는 새 PTE 를 쓰기 **전에** `ptep_clear_flush()` 를 한다(mm/memory.c:3760). 그 주석(3753-3759)은 "예전에는 새 PTE 를 쓰고 플러시했더니 어떤 CPU 의 TLB 에는 새 PTE, 다른 CPU 에는 옛 PTE 가 남는 창이 있었다" 고 설명한다. 이것이 ARM 의 break-before-make 와 같은 생각이다.

### Part C — 결과 (solution, 실제 출력)

```text
    | cowtest: start, 32487 free pages
    | counts: process 81 pages, fork used 7 pages, 10 child writes used 10 pages
    | cowtest: 18/18 passed
    (cowtest took 3.0 s)
    (usertests -q took 56.1 s)
PASS cowtest/simple
...                                  (cowtest 18 개 모두 PASS)
PASS usertests -q
19/19 passed  (QEMU CPUS=3, wall 59.7 s)
RESULT: PASS (solution)
```

두 번 돌렸고 둘 다 `19/19` 였다(한 번은 wall 70.2 s, usertests 60.7 s — 호스트에 다른 작성 세션들이 돌고 있어서 시간이 흔들린다. 시간은 판정에 쓰지 않는다). 같은 시점의 커널 콘솔에는 죽인 프로세스마다 xv6 원래의 진단 두 줄이 찍힌다:

```text
cowfault: out of memory, pid 16 va 0x3299000
usertrap(): unexpected scause 0xf pid=16
            sepc=0xd56 stval=0x3299000
usertrap(): unexpected scause 0xf pid=17
            sepc=0x7a stval=0x21000
...
usertrap(): unexpected scause 0xf pid=21
            sepc=0x7a stval=0xfffffffffffff000
```

`MAXVA` 와 `0xfffffffffffff000` 모두 QEMU 는 `scause 15`(store page fault)로 보고했다. 비정규(non-canonical) VA 에 접근 폴트가 아니라 페이지 폴트를 낸 것을 이 QEMU 에서 관찰했다. `usertests -q` 는 이 VM 에서 46–61 초가 걸려서 3 분 기준 안이다. 그래서 solution·practice 체크에서 매번 돌린다. starter 체크는 cowtest 만 돌린다(약 10 초).

### Part C — gdb 로 COW 폴트 하나 멈춰 보기

`check.sh gdb` 는 solution 커널을 `-S -gdb tcp:127.0.0.1:<빈 포트>` 로 띄운다. 포트는 커널이 고른 빈 포트라서 병렬 실행과 겹치지 않는다. 프롬프트가 뜨면 FIFO 로 `echo hi` 를 입력한다. 셸이 fork 하고 자식이 스택에 쓰는 순간이 첫 COW 폴트다. gdb 스크립트는 `script(1)` 아래서 돈다. tty 없이 `-batch` 로 돌리면 gdb 17 의 `continue` 가 기다리지 않고 바로 돌아와서 "Cannot execute this command while the target is running" 이 났다(실측). 또 `uartinit()` 이 수신 FIFO 를 비우므로 입력은 프롬프트 **뒤에** 보내야 했다. 실제 출력:

```text
Breakpoint 1, cowfault (pagetable=<optimized out>, va=16384) at kernel/vm.c:526
== cowfault() for pid 3 (sh)
#0  cowfault (pagetable=<optimized out>, va=16384) at kernel/vm.c:526
#1  0x0000000080002a10 in usertrap () at kernel/trap.c:72
#2  0x0000003ffffff09c in ?? ()
scause         0xf	15
stval          0x4f88	20360
sepc           0x870	2160
va=0x4000  *pte=0x21fcf1d3  refcount=2
Breakpoint 2, cowfault (pagetable=<optimized out>, va=16384) at kernel/vm.c:538
== copy made for pid 3: *pte=0x21fcecd7  new pa=0x87f3b000  old pa=0x87f3c000
```

```svg
<svg viewBox="0 0 740 255" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">Sv39 PTE — gdb 로 본 실제 값 (pid 3 sh, VA 0x4000, 폴트 전/후)</text>
  <g font-family="monospace" font-size="12">
    <text x="10" y="53" fill="currentColor">비트</text>
    <text x="10" y="83" fill="currentColor">전 0x21fcf1d3</text>
    <text x="10" y="113" fill="currentColor">후 0x21fcecd7</text>
    <rect x="160" y="36" width="110" height="24" class="f-mut" stroke="currentColor"/><text x="215" y="53" fill="currentColor" text-anchor="middle">PPN</text>
    <rect x="160" y="66" width="110" height="24" class="f-ok" stroke="currentColor"/><text x="215" y="83" fill="currentColor" text-anchor="middle">0x87f3c</text>
    <rect x="160" y="96" width="110" height="24" class="f-ok" stroke="currentColor"/><text x="215" y="113" fill="currentColor" text-anchor="middle">0x87f3b</text>
    <rect x="270" y="36" width="50" height="24" class="f-acc" stroke="currentColor"/><text x="295" y="53" fill="currentColor" text-anchor="middle">9 RSW</text>
    <rect x="270" y="66" width="50" height="24" class="f-mut" stroke="currentColor"/><text x="295" y="83" fill="currentColor" text-anchor="middle">0</text>
    <rect x="270" y="96" width="50" height="24" class="f-mut" stroke="currentColor"/><text x="295" y="113" fill="currentColor" text-anchor="middle">0</text>
    <rect x="320" y="36" width="50" height="24" class="f-acc" stroke="currentColor"/><text x="345" y="53" fill="currentColor" text-anchor="middle">8 COW</text>
    <rect x="320" y="66" width="50" height="24" class="f-ok" stroke="currentColor"/><text x="345" y="83" fill="currentColor" text-anchor="middle">1</text>
    <rect x="320" y="96" width="50" height="24" class="f-ok" stroke="currentColor"/><text x="345" y="113" fill="currentColor" text-anchor="middle">0</text>
    <rect x="370" y="36" width="40" height="24" class="f-mut" stroke="currentColor"/><text x="390" y="53" fill="currentColor" text-anchor="middle">7 D</text>
    <rect x="370" y="66" width="40" height="24" class="f-mut" stroke="currentColor"/><text x="390" y="83" fill="currentColor" text-anchor="middle">1</text>
    <rect x="370" y="96" width="40" height="24" class="f-mut" stroke="currentColor"/><text x="390" y="113" fill="currentColor" text-anchor="middle">1</text>
    <rect x="410" y="36" width="40" height="24" class="f-mut" stroke="currentColor"/><text x="430" y="53" fill="currentColor" text-anchor="middle">6 A</text>
    <rect x="410" y="66" width="40" height="24" class="f-mut" stroke="currentColor"/><text x="430" y="83" fill="currentColor" text-anchor="middle">1</text>
    <rect x="410" y="96" width="40" height="24" class="f-mut" stroke="currentColor"/><text x="430" y="113" fill="currentColor" text-anchor="middle">1</text>
    <rect x="450" y="36" width="40" height="24" class="f-mut" stroke="currentColor"/><text x="470" y="53" fill="currentColor" text-anchor="middle">5 G</text>
    <rect x="450" y="66" width="40" height="24" class="f-mut" stroke="currentColor"/><text x="470" y="83" fill="currentColor" text-anchor="middle">0</text>
    <rect x="450" y="96" width="40" height="24" class="f-mut" stroke="currentColor"/><text x="470" y="113" fill="currentColor" text-anchor="middle">0</text>
    <rect x="490" y="36" width="40" height="24" class="f-mut" stroke="currentColor"/><text x="510" y="53" fill="currentColor" text-anchor="middle">4 U</text>
    <rect x="490" y="66" width="40" height="24" class="f-mut" stroke="currentColor"/><text x="510" y="83" fill="currentColor" text-anchor="middle">1</text>
    <rect x="490" y="96" width="40" height="24" class="f-mut" stroke="currentColor"/><text x="510" y="113" fill="currentColor" text-anchor="middle">1</text>
    <rect x="530" y="36" width="40" height="24" class="f-mut" stroke="currentColor"/><text x="550" y="53" fill="currentColor" text-anchor="middle">3 X</text>
    <rect x="530" y="66" width="40" height="24" class="f-mut" stroke="currentColor"/><text x="550" y="83" fill="currentColor" text-anchor="middle">0</text>
    <rect x="530" y="96" width="40" height="24" class="f-mut" stroke="currentColor"/><text x="550" y="113" fill="currentColor" text-anchor="middle">0</text>
    <rect x="570" y="36" width="40" height="24" class="f-warn" stroke="currentColor"/><text x="590" y="53" fill="currentColor" text-anchor="middle">2 W</text>
    <rect x="570" y="66" width="40" height="24" class="f-ok" stroke="currentColor"/><text x="590" y="83" fill="currentColor" text-anchor="middle">0</text>
    <rect x="570" y="96" width="40" height="24" class="f-ok" stroke="currentColor"/><text x="590" y="113" fill="currentColor" text-anchor="middle">1</text>
    <rect x="610" y="36" width="40" height="24" class="f-mut" stroke="currentColor"/><text x="630" y="53" fill="currentColor" text-anchor="middle">1 R</text>
    <rect x="610" y="66" width="40" height="24" class="f-mut" stroke="currentColor"/><text x="630" y="83" fill="currentColor" text-anchor="middle">1</text>
    <rect x="610" y="96" width="40" height="24" class="f-mut" stroke="currentColor"/><text x="630" y="113" fill="currentColor" text-anchor="middle">1</text>
    <rect x="650" y="36" width="40" height="24" class="f-mut" stroke="currentColor"/><text x="670" y="53" fill="currentColor" text-anchor="middle">0 V</text>
    <rect x="650" y="66" width="40" height="24" class="f-mut" stroke="currentColor"/><text x="670" y="83" fill="currentColor" text-anchor="middle">1</text>
    <rect x="650" y="96" width="40" height="24" class="f-mut" stroke="currentColor"/><text x="670" y="113" fill="currentColor" text-anchor="middle">1</text>
  </g>
  <text x="10" y="150" fill="currentColor">전 0x1d3 = COW|D|A|U|R|V (W 없음).  후 0x0d7 = D|A|U|W|R|V, PPN 이 바뀜 (색칠한 칸).</text>
  <text x="10" y="174" fill="currentColor">refcount 2 (부모 sh 와 공유) → 복사 경로. 새 프레임 0x87f3b000.</text>
  <text x="10" y="198" fill="currentColor">옛 프레임 0x87f3c000 은 kfree 로 ref 1 이 되어 부모에 남는다.</text>
  <text x="10" y="222" fill="currentColor">A·D 가 이미 1: 이 QEMU 가 A/D 를 하드웨어로 갱신한 것으로 보인다 (xv6 는 안 씀).</text>
  <text x="10" y="246" fill="currentColor">PA = (pte &gt;&gt; 10) &lt;&lt; 12 (PTE2PA, riscv.h:404). 비트 8·9 는 하드웨어가 무시한다.</text>
</svg>
```

### Part C — Linux 6.18.55 은 같은 일을 어떻게 하나

- **fork 쪽** `__copy_present_ptes()`(mm/memory.c:1097-1118): `if (is_cow_mapping(src_vma->vm_flags) && pte_write(pte)) { wrprotect_ptes(src_mm, …); pte = pte_wrprotect(pte); }`. 주석은 "If it's a COW mapping, write protect it both processes." 다. xv6 의 "양쪽 W 끄기" 와 같다. `is_cow_mapping()` = `(flags & (VM_SHARED | VM_MAYWRITE)) == VM_MAYWRITE`(include/linux/mm.h:1643-1646)이다. 즉 **MAP_PRIVATE 이면서 쓰기 가능할 수 있는** VMA 만 COW 다. Linux 는 RSW 비트 대신 **VMA 정보**로 "원래 쓰기 가능했는가" 를 안다.
- **폴트 쪽** `handle_pte_fault()` 가 쓰기 폴트이고 `!pte_write(entry)` 면 `do_wp_page(vmf)` 로 간다(mm/memory.c:6225-6226). `do_wp_page()`(4063-)는 익명 페이지가 `PageAnonExclusive` 이거나 `wp_can_reuse_anon_folio()` 가 참이면 `wp_page_reuse()` 를 부른다. 그 함수가 바로 우리의 refcount-1 경로다. 아니면 "Ok, we need to copy. Oh, well.." 주석 뒤에 `wp_page_copy()`(3672-)를 부른다.
- **재사용 판정** `wp_can_reuse_anon_folio()`(4000-4037): `folio_ref_count(folio) > 1 + folio_test_swapcache(folio)` 면 거절하고, 락을 잡은 뒤 `folio_ref_count(folio) != 1` 을 다시 확인한다. 우리의 `krefcnt == 1` 과 같지만 swap cache·KSM·LRU per-CPU 캐시가 잡고 있는 참조까지 고려한다. 오래 논쟁된 부분이다(GUP 로 고정된 페이지가 fork 뒤 엉뚱한 쪽으로 가는 문제, 2020~2022 년 PageAnonExclusive 도입 — 세부는 기억에 의존, 헤지).
- **TLB**: 앞 절의 `flush_tlb_mm(oldmm)`(mm/mmap.c:1873)과 `ptep_clear_flush()`(memory.c:3760).

### Part C — MMU 없는 시스템: COW 는 불가능하다

COW 는 "쓰기 금지 페이지 + 폴트 + 재매핑" 으로 이루어지는데, 셋 다 MMU 의 **주소 변환**이 있어야 한다. Cortex-M 의 MPU(BM-A08, ARCH-M15)는 영역 권한 검사는 하지만 변환이 없다. 그래서 같은 VA 를 두 프로세스에서 다른 프레임에 둘 수 없다. 그래서 Linux 문서 `Documentation/admin-guide/mm/nommu-mmap.rst` 12-13 줄은 "Under uClinux there is no fork(), and clone() must be supplied the CLONE_VM flag." 라고 쓴다. nommu 의 `dup_mmap()`(mm/nommu.c:1897-1903)은 매핑을 아예 복사하지 않는다. 실무 결론은 둘이다. (1) MMU 없는 리눅스(uClinux, 예: STM32F7/H7 의 일부 포트)에서는 `vfork()` + `exec` 또는 `posix_spawn()` 만 쓴다. (2) MMU 가 있어도 큰 데몬은 fork 대신 posix_spawn 이 낫다. OS-M03 이 실측했다: 1 GB 부모에서 fork 3.6 ms 대 posix_spawn 0.15 ms(macOS). COW 는 데이터 복사를 없앨 뿐, 페이지 테이블 복사(이 레슨의 7 페이지)와 fork 뒤 부모의 쓰기 폴트 비용은 남는다.

## 6. 정답 코드

바뀐 파일은 넷이다(나머지 오버레이 파일은 starter 와 같다). 강조된 줄이 TODO 를 채운 부분이다.

<!-- code: solution/kernel/riscv.h diff=starter/kernel/riscv.h -->

<!-- code: solution/kernel/kalloc.c diff=starter/kernel/kalloc.c -->

<!-- code: solution/kernel/vm.c diff=starter/kernel/vm.c -->

<!-- code: solution/kernel/trap.c diff=starter/kernel/trap.c -->

## 7. 자주 하는 실수

`check.sh bug <이름>` 은 solution 에 **정확히 한 군데**를 바꿔 넣고 cowtest 를 돌린다. 아래는 모두 실제 출력이다.

**① copyout 을 안 고침 (`nocopyout`)** — "폴트 처리기만 고치면 끝" 이라는 착각이다. 실패하는 테스트가 엉뚱해 보인다는 점이 교훈이다.

```text
FAIL cowtest/pipe-copyout  (read() into a COW page failed in the child)
FAIL cowtest/wait-copyout  (wait() could not store the status)
FAIL cowtest/reuse-refcount-1  (a page with refcount 1 was copied instead of made writable)
FAIL cowtest/reuse-at-oom  (setup failed or pages leaked)
FAIL cowtest/oom-kill  (sbrk failed)
FAIL cowtest/fork-loop-no-leak  (pages leaked across 20 forks)
12/18 passed
```

앞의 두 개만 copyout 테스트다. 나머지 넷은 COW 와 무관해 보이지만 원인은 같다. 부모의 `wait(&st)` 에서 `st` 가 COW 스택 페이지에 있으면 kwait 의 copyout 이 −1 이 된다. 그러면 kwait 는 **`freeproc` 전에** −1 을 돌려준다(proc.c:390-395). 좀비가 수거되지 않고 메모리를 쥔 채 남는다. 그래서 뒤 테스트에서 "누수" 와 "sbrk failed" 가 연쇄로 난다. 고침: 5장 Part B 의 copyout 블록.

**② COW 비트를 안 보고 "읽기 전용이면 COW" (`nocowbit`)** — `if ((*pte & (PTE_V|PTE_U|PTE_COW)) == 0)` 처럼 검사를 느슨하게 했다.

```text
FAIL kill-text-write: store into read-only text did not kill
panic: kfree
13/18 passed
```

텍스트 쓰기가 "복사" 로 성공해 버렸다. 이어서 U 비트도 COW 비트도 없는 **트램폴린**을 COW 로 착각하고, 커널 텍스트 안의 프레임을 kfree 하다가 범위 검사(kalloc.c:51)에서 panic 했다. 고침: `V|U|COW` 가 **모두** 있어야 COW 다.

**③ 자식 PTE 만 읽기 전용 (`childonly`)** — 부모 PTE 를 고치지 않았다.

```text
FAIL cowtest/isolation  (a process saw another process's writes)
FAIL cowtest/wait-copyout  (the status leaked into the other child)
16/18 passed
```

부모는 여전히 W=1 이라서 공유 프레임에 바로 쓴다. 고침: 부모 `*pte` 를 먼저 바꾸고 그 flags 를 자식에게 준다.

**④ 항상 복사 (`alwayscopy`)** — refcount 1 검사를 빼도 결과는 맞다. 낭비일 뿐이다. 그런데 처음 만든 테스트 17 개는 **모두 통과**했다(`17/17 passed`). 복사 1 + 해제 1 = free pages 순변화 0 이라서다. 그래서 추가한 `reuse-at-oom` 이 잡는다:

```text
FAIL cowtest/reuse-at-oom  (refcount-1 write needed a new page and was killed)
17/18 passed
```

교훈: "숫자가 맞다" 는 테스트가 불변식을 다 지키지는 않는다. 자원이 0 인 경계에서 다시 본다.

**⑤ MAXVA 검사 빠뜨림 (`nomaxva`)** — `walk()` 가 `va >= MAXVA` 에서 panic 한다(vm.c:101-102).

```text
panic: walk
FAIL cowtest/kill-maxva  (no PASS line (panic))
15/18 passed
```

사용자 프로그램 한 줄(`*(char *)(1L<<38) = 1`)이 커널을 멈춘다. 신뢰할 수 없는 입력(`stval`)은 쓰기 전에 범위를 검사한다.

**⑥ 참조 카운트 무시 (`norefcount`)** — kfree 가 카운트와 상관없이 해제한다.

```text
cowtest: start, 32496 free pages
panic: kfree: refcount
0/18 passed
```

아직 다른 페이지 테이블이 매핑한 프레임이 free list 로 간다. 다음 kfree 에서 카운트가 이미 0 이라 우리가 넣은 `< 1` 검사가 panic 시켰다. 검사가 없었다면 그 프레임이 다른 용도로 재사용되어 조용한 데이터 손상이 났을 것이다. **"불가능한 상태에서 panic" 은 커널 개발의 기본 방어선**이다.

**⑦ (설계) `sepc` 를 cowfault 에 넘김** — 폴트 주소는 `stval` 이다(5장 표). **⑧ (설계) uvmcopy 에서 kalloc 실패 정리를 잊음** — 이 레슨의 uvmcopy 는 kalloc 하지 않는다. 그래도 `mappages` 가 페이지 테이블 페이지를 못 얻으면 실패한다. 그때 `uvmunmap(new, 0, i/PGSIZE, 1)` 이 이미 매핑한 페이지의 카운트를 kfree 로 되돌린다. 부모 쪽에서 이미 W 를 끈 페이지는 COW 상태로 남는다. 카운트가 1 로 돌아왔으니 다음 쓰기에서 복사 없이 W 가 되살아나고, 해롭지 않다.

## 8. 면접 Q&A

<details>
<summary>Q1. COW fork 를 한 문단으로 설명하면? 꼬리: fork 가 정말 "공짜" 인가?</summary>

fork 는 부모의 페이지 테이블을 복사하되 데이터 페이지는 공유한다. 쓰기 가능 페이지는 양쪽에서 읽기 전용 + COW 표시로 바꾸고, 물리 페이지마다 참조 수를 센다. 누가 쓰면 저장 페이지 폴트가 나고, 처리기가 그 페이지만 복사해서 쓰기 가능으로 매핑한 뒤 명령을 재실행한다. 공짜는 아니다. 페이지 테이블 복사(xv6 실측 81 페이지 프로세스에 7 페이지, Linux 는 큰 프로세스에서 ms 단위 — OS-M03 실측 1 GB 에 3.6 ms)가 든다. fork 뒤에는 **부모도** 쓸 때마다 폴트를 낸다. 그리고 자식이 끝나기 전 부모가 쓰는 페이지는 결국 복사된다. 그래서 큰 프로세스에서 다른 프로그램을 띄울 때는 posix_spawn/vfork 가 낫다.

</details>

<details>
<summary>Q2. COW 페이지인지 어떻게 아나? 꼬리: xv6 는 PTE 비트, Linux 는?</summary>

xv6 는 PTE 의 RSW 비트 8 을 쓴다. 하드웨어가 무시하도록 스펙이 예약한 비트라서 소프트웨어 표시로 안전하다. W=0 인 페이지 중 "원래 쓰기 가능이었다" 를 구분하는 것이 목적이다. 이것이 없으면 텍스트 쓰기도 복사로 "성공" 한다(7장 ②). Linux 는 PTE 비트 대신 VMA 를 본다. `is_cow_mapping(vm_flags)` 는 `VM_MAYWRITE` 이고 `VM_SHARED` 가 아닌 매핑이다. 쓰기 폴트 시 VMA 가 `VM_WRITE` 이면 COW 처리, 아니면 SIGSEGV 다. 페이지 단위 RSW 표시는 VMA 가 없는 커널(xv6)의 해법이다.

</details>

<details>
<summary>Q3. 커널이 사용자 메모리에 쓸 때(copy_to_user)는 COW 를 어떻게 처리하나?</summary>

xv6 의 copyout 은 페이지 테이블을 소프트웨어로 걸어 얻은 PA 에 커널 매핑으로 쓴다. 사용자 PTE 권한이 적용되지 않으므로 직접 `cowfault` 를 불러야 한다. 안 하면 `read()`·`wait()` 가 실패하거나, 무시하고 쓰면 공유 페이지가 오염된다. Linux 의 `copy_to_user` 는 사용자 VA 로 직접 저장한다(커널이 사용자 주소 공간을 그대로 본다. arm64 에서는 PAN 을 잠깐 풀거나 `sttr` 같은 비특권 저장을 쓴다 — 헤지). 그래서 MMU 가 실제로 쓰기 폴트를 내고, 예외 테이블(`extable`)을 통해 `handle_mm_fault` → `do_wp_page` 로 똑같이 처리된다. 설계 차이: "소프트웨어 워크 + 수동 처리" 대 "하드웨어 폴트 + 픽스업 테이블" 이다.

</details>

<details>
<summary>Q4. PTE 를 바꾼 뒤 TLB 는 언제 비우나? xv6 는 왜 sfence.vma 를 부르지 않나?</summary>

권한을 줄이는 변경(fork 의 W 끄기)은 다른 CPU 포함 모든 TLB 에서 옛 항목이 사라져야 안전하다. xv6 는 ASID 를 쓰지 않는다. 그리고 사용자↔커널 전환마다 trampoline.S:88,94,109-111 에서 `sfence.vma zero, zero` 로 전부 비운다. 프로세스는 단일 스레드라 같은 페이지 테이블이 동시에 다른 hart 에서 사용자 코드를 돌리지 않는다. 그래서 커널 안에서 바꾼 PTE 는 복귀할 때 자동으로 반영된다. Linux 는 멀티스레드 + ASID 라서 fork 끝에 `flush_tlb_mm(oldmm)`(mm/mmap.c:1873)을 부른다. 복사 시에는 `ptep_clear_flush()` 로 "지우고 플러시한 다음 새 PTE" 순서를 지킨다(memory.c:3753-3760 주석). 꼬리: xv6 에 ASID 를 넣으면 무엇이 깨지나? → uvmcopy 와 cowfault 뒤에 해당 ASID 플러시를 넣어야 한다. 멀티스레드를 넣으면 다른 hart 로 IPI shootdown 도 필요하다.

</details>

<details>
<summary>Q5. refcount == 1 이면 복사 없이 쓰기를 허용해도 되는 근거는? 경쟁 조건은 없나?</summary>

카운트를 올릴 수 있는 것은 그 프레임을 이미 매핑한 프로세스의 fork 뿐이다. 카운트가 1 이면 매핑한 것은 나뿐이고, 나는 지금 폴트 처리 중이라 fork 할 수 없다(xv6 는 단일 스레드). 그러므로 검사와 PTE 변경 사이에 카운트가 오를 수 없다. 카운트 2 에서 둘이 동시에 폴트하면 둘 다 복사하고 둘 다 감소 → 0 → 해제가 된다. 낭비지만 정확하다. Linux 는 다르다. 같은 mm 의 다른 스레드가 GUP(get_user_pages)로 참조를 잡을 수 있다. 그래서 `wp_can_reuse_anon_folio` 가 folio 락 아래서 다시 확인하고, `PageAnonExclusive` 로 "이 익명 페이지는 이 mm 전용" 을 명시한다. 멀티스레드가 되는 순간 "1 이면 내 것" 이라는 단순 논리가 깨진다는 게 핵심이다.

</details>

<details>
<summary>Q6. COW 처리 중 메모리가 없으면? 꼬리: Linux 는 누구를 죽이나? fork 시점에 실패시키는 게 낫지 않나?</summary>

xv6 는 그 프로세스를 죽인다(`oom-kill` 테스트: 상태 −1, 부모 데이터 무사, free pages 원상복구). 문제는 실패가 **fork 가 아니라 한참 뒤 아무 저장 명령**에서 난다는 점이다. 프로그램은 처리할 방법이 없다. Linux 는 overcommit 정책(`vm.overcommit_memory` 0/1/2)으로 fork 시점에 얼마나 약속할지 정한다. 2(strict)면 fork 가 ENOMEM 으로 실패하고, 0/1 이면 나중에 OOM killer 가 badness 점수로 희생자를 고른다. 임베디드에서는 보통 overcommit 을 끄거나(2), 큰 데몬의 fork 를 금지하고 posix_spawn 을 쓴다. 그래서 "나중에 아무 데서나 죽는다" 를 설계에서 없앤다. 이 설계가 틀리는 경우: 하드 실시간 태스크가 fork 한 부모라면 COW 폴트 자체가 예측 불가능한 지연이다. `mlockall(MCL_CURRENT|MCL_FUTURE)` 는 폴트를 막지 못하므로(COW 는 여전히 일어난다 — 헤지) 아예 fork 하지 않는 것이 답이다.

</details>

<details>
<summary>Q7. 이 구현을 64 코어, 수 GB 램으로 확장하라면 무엇을 바꾸겠나?</summary>

① 전역 `kref.lock` → 페이지별 원자 카운터(`atomic_fetch_sub`, Linux 의 `folio->_refcount`). kfree 의 "0 이 되면 해제" 는 `fetch_sub` 의 이전 값이 1 인지로 판정한다. ② 정적 배열 → 기존 페이지 메타데이터(`struct page`)에 얹는다. 램이 띄엄띄엄이면(NUMA, 홀) 배열을 섹션별로 쪼갠다(SPARSEMEM). ③ free list 를 CPU 별 캐시로 쪼갠다(per-CPU pages). ④ fork 의 페이지 테이블 복사도 비싸진다. Linux 는 큰 folio 를 묶어 `copy_present_ptes` 로 배치 처리한다(memory.c:1128). ⑤ TLB: ASID + 범위 플러시 + 지연 shootdown. 측정 지표로는 fork 지연 p99, COW 폴트/초, lock contention(lockstat), shootdown IPI 수를 본다.

</details>

<details>
<summary>Q8. 이 구현이 맞는지 어떻게 알았나? (테스트 설계) 꼬리: 어떤 테스트가 무엇을 못 잡았나?</summary>

관찰 가능한 **불변식을 정확한 숫자**로 잰다. fork 직후 할당 페이지 < NP/2, 자식의 첫 쓰기 K 번 = 정확히 K 페이지, 모든 자식 종료 후 free pages = fork 전, refcount-1 쓰기는 0 페이지. 경쟁은 파이프로 순서를 강제해서 없앤다. 부모와 자식의 스택 첫 쓰기를 측정 전에 끝내 둔다. 그리고 6 가지 버그를 주입해서 각 테스트가 정말 무언가를 잡는지 확인했다. 실패 사례: "항상 복사" 는 처음 17 개를 모두 통과했다. 그래서 자원 0 경계 테스트(`reuse-at-oom`)를 추가했다. 추가 연습 3 의 refcount 감사(모든 페이지 테이블을 다시 세어 카운트와 비교)는 "fsck 처럼 불변식 전체를 검사" 하는 방법이다. 주입한 오차 1 개를 정확히 1 로 잡는 음성 대조도 넣었다.

</details>

<details>
<summary>Q9. MMU 없는 Cortex-M 리눅스나 RTOS 에서 "fork 비슷한 것" 이 필요하면?</summary>

변환이 없으면 같은 VA 를 두 프레임에 둘 수 없다. 그래서 COW 도 fork 도 불가능하다(uClinux 문서: "there is no fork()"). 선택지는 셋이다. vfork(부모를 멈추고 주소 공간을 빌려 쓰다 exec), posix_spawn, 또는 처음부터 별도 태스크로 설계한다(RTOS 의 xTaskCreate — 스택만 새로). 주소를 바꿀 수 없으니 실행 파일도 위치 독립(FDPIC/bFLT 재배치)이어야 한다. MPU(BM-A08)는 영역 권한만 검사하므로 "쓰기 금지 → 폴트" 까지는 된다. 하지만 "재매핑" 이 없으니 COW 의 마지막 단계가 불가능하다. 할 수 있는 것은 MemManage 폴트에서 그 태스크를 격리하는 정도다.

</details>

## 9. 추가 연습

practice 오버레이는 solution 전체에 아래 세 기능을 더한 것이다(시스템 콜 `cowstat` 24, `refaudit` 25 추가). 테스트 프로그램 `user/cowpractice.c` 는 cowtest 다음에 같은 부팅에서 돈다.

### 연습 1 — COW 카운터: 복사 대 재사용, fork+exec 는 몇 장 복사하나

`cowfault` 의 두 성공 경로에 원자 카운터(`__sync_fetch_and_add`)를 달고 `cowstat(0|1)` 로 읽는다. 테스트: 자식의 첫 쓰기 10 번 = copies +10. 자식 종료 뒤 부모의 쓰기 10 번 = copies +0, reuses +10. 64 페이지를 더 키운 프로세스가 fork 하고 자식이 곧장 `exec("echo")` 하면 몇 장이 복사되나?

<details>
<summary>답</summary>

실측: `stat: parent after child exit: 0 copies, 10 reuses`, `fork-exec: process of 69 pages -> 1 copies, 1 reuses`. 69 페이지 프로세스의 fork+exec 에서 복사된 데이터 페이지는 **1 장**(자식이 exec 전에 스택에 쓴 것으로 보인다)이고, 부모 쪽 1 장은 자식이 이미 떠난 뒤라 재사용됐다. 페이지 테이블 비용은 별도다. 테스트는 `copies ≤ 4` 로 느슨하게 잡았다. 정확한 값은 컴파일러의 스택 사용에 달려 있다. eager fork 였다면 이 fork 는 69 장 이상을 복사했다. 이 숫자가 "fork+exec 에 COW 가 왜 결정적인가" 의 답이다.

</details>

### 연습 2 — 공유 제로 페이지 (lazy 읽기 폴트)

`sbrklazy()` 로 늘린 메모리를 **읽기만** 해도 지금은 `vmfault` 가 페이지마다 kalloc + memset 0 한다. 부팅 때 0 으로 채운 프레임 하나(`zeropage`, 커널이 ref 1 을 영구 보유)를 만든다. 읽기 폴트(`scause 13`, 또는 `copyin` 의 `read=1`)에는 그것을 `R|U|COW` 로 매핑하고 `krefinc` 한다. 첫 쓰기는 기존 cowfault 가 처리한다. 카운트가 항상 2 이상이라 늘 복사 경로를 탄다.

<details>
<summary>답</summary>

`vmfault()` 에 `if (read) { mappages(..., zeropage, PTE_R|PTE_U|PTE_COW); krefinc(zeropage); return zeropage; }` 를 넣는다(practice/kernel/vm.c). 실측: `zero: reading 64 lazy pages used 0 pages`. 이어서 10 장에 쓰면 정확히 10 장이 늘고, fork 한 자식이 제로 페이지 매핑을 공유하다 써도 부모는 0 을 그대로 본다. `sbrk(−n)` 이 제로 페이지를 unmap 해도 kfree 는 카운트만 줄인다. 커널의 핀(1) 때문에 해제되지 않는다. Linux 도 같은 일을 한다(익명 읽기 폴트 → zero page, `mm_forbids_zeropage`/`my_zero_pfn` — 헤지). 함정: 제로 페이지의 PTE 에 W 를 주면 모두가 같은 프레임에 쓴다. 반드시 COW 와 함께 매핑한다.

</details>

### 연습 3 — 참조 카운트 감사 (refcount fsck)

`refaudit(inject)`: 모든 프로세스의 페이지 테이블을 `0..p->sz` 로 걸으며 프레임별 매핑 수를 다시 센다(+ 제로 페이지 핀 1). 사용자 매핑이 있는 프레임마다 `kref.cnt` 와 비교해서 불일치 수를 돌려준다. `inject=1` 이면 호출자의 0 번 페이지 카운트를 잠시 +1 해서 감사기가 정확히 1 개를 잡는지 본다(음성 대조).

<details>
<summary>답</summary>

practice/kernel/kalloc.c `krefaudit()`. 실측: `audit: live 0, injected 1, after exit 0 mismatches`. 자식 둘이 페이지를 공유한 채 파이프에서 잠든 상태(카운트 2~3)에서 0, 주입하면 1, 모두 종료 후 0 이다. 주의 두 가지. (1) exec 의 스택 가드 페이지는 `uvmclear` 로 U 비트만 지운 매핑이다. 그래서 "U 가 있는 PTE 만 센다" 고 하면 그 프레임을 놓쳐 불일치가 난다. V 만 보고 센다. (2) 다른 hart 가 페이지 테이블을 바꾸는 중에는 틀린 결과가 나올 수 있다. 그래서 "조용한 시스템의 테스트 훅" 으로만 쓴다(주석에 명시). 실제 커널의 대응물은 `CONFIG_DEBUG_VM` 의 `VM_BUG_ON_PAGE` 와 page_owner 같은 디버그 기능이다(헤지).

</details>

### 연습 4 — (생각) 큰 페이지와 COW

2 MB 메가페이지(Sv39 L1 리프)를 쓰는 프로세스가 fork 했고 자식이 1 바이트를 쓴다면? xv6 에 메가페이지 COW 를 넣는다면 어떤 선택지가 있나?

<details>
<summary>답</summary>

선택지는 둘이다. (a) 2 MB 를 통째로 복사한다(폴트 한 번에 512 배 메모리와 시간). (b) 메가페이지를 4 KB 페이지 512 개로 **쪼갠 뒤** 그 한 장만 복사한다(L0 테이블 1 장 할당 + 512 PTE 를 COW 로 채움 + 해당 범위 플러시). Linux THP 는 익명 THP 의 COW 폴트에서 대체로 (b) 쪽(PMD 분할 후 4 KB 복사)을 택한다. 커널 버전마다 바뀌어 왔으니 세부는 헤지한다. refcount 는 "메가페이지 단위 하나 + 하위 페이지 매핑 수" 를 함께 관리해야 한다. folio 가 그 일을 한다. 측정 포인트는 fork 뒤 첫 쓰기 지연, RSS 증가량, 분할 횟수다.

</details>

### 한 번에 검증

<!-- code: practice/kernel/vm.c diff=solution/kernel/vm.c -->

<!-- code: practice/kernel/kalloc.c diff=solution/kernel/kalloc.c -->

<!-- code: practice/kernel/sysproc.c diff=solution/kernel/sysproc.c -->

<!-- code: practice/user/cowpractice.c -->

```bash
limactl shell elinux -- bash -lc "bash $L/check.sh practice"    # cowtest 18 + cowpractice 9 + usertests -q
```

실제 출력(발췌):

```text
    | cowtest: 18/18 passed
    | cowpractice: start
    | stat: parent after child exit: 0 copies, 10 reuses
    | fork-exec: hello from echo
    | fork-exec: process of 69 pages -> 1 copies, 1 reuses
    | zero: reading 64 lazy pages used 0 pages
    | audit: live 0, injected 1, after exit 0 mismatches
    | cowpractice: 9/9 passed
    (usertests -q took 50.4 s)
28/28 passed  (QEMU CPUS=3, wall 56.6 s)
RESULT: PASS (practice)
```

practice 커널의 부팅 직후 free pages 는 32453 으로 solution(32487)보다 34 장 적다. 감사용 `expect[]` 배열(128 KB = 32 장)과 제로 페이지 1 장 등이다.

최종 검증(이 md 를 쓴 뒤 다시 실행, `check.sh` 전체 시간): starter `14/18 passed` → exit 1 (9.6 초), solution `19/19 passed` 두 번(73 초, 61 초), practice `28/28 passed`(58 초). 모두 VM 의 `/tmp/OS-A10_work/` 만 썼고, 원본 클론 `~/.esn_scratch/xv6-riscv` 는 `git status` 가 깨끗하다.

## 10. 핵심 정리

- COW fork = 페이지 테이블은 복사, 데이터 프레임은 공유. 쓰기 가능 페이지는 **양쪽** PTE 에서 W 를 끄고 COW(RSW 비트 8)를 켠다. 텍스트처럼 원래 읽기 전용인 페이지는 COW 가 아니다.
- 프레임마다 참조 카운트: kalloc = 1, uvmcopy = +1, kfree = −1 이고 0 일 때만 해제한다. 부팅 때 freerange 는 1 로 세팅하고 kfree 한다.
- `scause 15`(저장/AMO 페이지 폴트) + `stval`(데이터 VA) → `cowfault`. refcount 1 이면 W 만 복구(할당 0, 메모리 0 에서도 성공). 아니면 kalloc·복사·재매핑·옛 프레임 kfree 후 같은 명령을 재실행한다.
- 신뢰할 수 없는 `stval` 은 먼저 검사: `≥ MAXVA`(walk panic), `V|U|COW` 셋 다 있어야 COW. 나머지와 OOM 은 그 프로세스만 kill 한다.
- 커널의 사용자 메모리 쓰기(copyout)는 폴트가 나지 않는다 → 거기서 직접 COW 를 깬다. 안 하면 `read()` 와 `wait()` 가 실패한다.
- xv6 는 사용자↔커널 전환마다 `sfence.vma` 로 TLB 전체를 비우고 프로세스가 단일 스레드라서 PTE 변경 후 플러시가 필요 없다. Linux 는 `flush_tlb_mm`/`ptep_clear_flush` 가 필요하다.
- 실측: 81 페이지 프로세스 fork 7 페이지(eager 87), 자식 첫 쓰기 10 번 = 10 페이지, 60 % 램 프로세스도 fork 가능, 누수 0, usertests -q 약 1 분에 통과.
- MMU 없으면 COW·fork 불가(uClinux: vfork/posix_spawn). MMU 가 있어도 큰 데몬은 posix_spawn(OS-M03).

## 참고자료

- xv6-riscv (MIT PDOS), commit `fa2ddcb7ca87a77af579380a6eafd5aff163b44f` — https://github.com/mit-pdos/xv6-riscv (kernel/vm.c, trap.c, kalloc.c, riscv.h, trampoline.S, proc.c)
- MIT 6.1810 *Lab: Copy-on-Write Fork for xv6* — https://pdos.csail.mit.edu/6.1810/2025/labs/cow.html (아이디어 출처. 코드·테스트는 이 레슨에서 새로 작성)
- Russ Cox, Frans Kaashoek, Robert Morris, *xv6: a simple, Unix-like teaching operating system* (RISC-V 판), 3장 "Page tables", 4장 "Traps and system calls" 의 Page-fault exceptions 절에서 COW fork 를 소개한다 (절 번호는 판마다 다름)
- *The RISC-V Instruction Set Manual, Volume II: Privileged Architecture* — Sv39 PTE 형식(RSW 비트 8–9, A/D), `scause` 예외 코드 12/13/15, `stval`, `sfence.vma`
- Linux 6.18.55 소스: mm/memory.c (`__copy_present_ptes` 1097, `wp_page_copy` 3672, `ptep_clear_flush` 3760, `wp_can_reuse_anon_folio` 4000, `do_wp_page` 4063, `handle_pte_fault` 6166/6226), mm/mmap.c:1873 (`flush_tlb_mm(oldmm)`), include/linux/mm.h:1643 (`is_cow_mapping`), mm/nommu.c:1897, Documentation/admin-guide/mm/nommu-mmap.rst
- OSTEP 21장 "Beyond Physical Memory: Mechanisms" (페이지 폴트 처리), 23장 "Complete Virtual Memory Systems" (VAX/VMS demand zeroing · copy-on-write, Linux VM)
- `man 2 fork`, `man 2 vfork`, `man 3 posix_spawn`, `proc(5)` 의 `/proc/sys/vm/overcommit_memory` — https://man7.org/linux/man-pages/
- 이 저장소: OS-M03 (macOS 에서 COW 관찰, fork vs posix_spawn 실측), OS-M01 (페이지 테이블 관리·TLB 무효화 규칙), OS-A06 (사용자 공간 페이지 폴트), ARCH-A01/ARCH-A02 (페이지 워커·TLB 모델), ARCH-A03 (페이지 폴트·TLB 미스 비용), BM-A08 · ARCH-M15 (MPU), ELX-M04 (`check.sh` 커널 실습 패턴)
