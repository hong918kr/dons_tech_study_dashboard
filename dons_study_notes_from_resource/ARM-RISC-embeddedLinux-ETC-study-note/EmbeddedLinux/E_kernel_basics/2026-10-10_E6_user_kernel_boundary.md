# E6. 유저 ↔ 커널 경계 — syscall 경로, copy_to/from_user, 에러 코드 규약

> **이 노트를 다 읽으면**: `svc #0`부터 드라이버의 `.read`/`.unlocked_ioctl`까지 arm64 커널 함수 이름을 순서대로 말할 수 있다 · `copy_to_user`/`copy_from_user`의 반환값(못 복사한 바이트 수)으로 부분 복사를 올바르게 처리하는 read/write/ioctl을 짤 수 있다 · 나쁜 유저 포인터가 Oops가 아니라 `-EFAULT`가 되는 이유를 예외 테이블(`__ex_table`)과 PAN으로 설명할 수 있다 · 상황별로 맞는 errno(`EINVAL`/`EFAULT`/`ENOTTY`/`EAGAIN`/`ERESTARTSYS` …)를 고르고 ioctl 구조체를 32/64비트 모두에서 깨지지 않게 설계할 수 있다
> **왜 필요한가**: 드라이버 면접의 단골 질문 "Why can't you just dereference a user pointer in the kernel?"와 "What does copy_from_user return?"은 이 노트 한 장이고, 실무에서 처음 쓰는 char 드라이버의 read/write/ioctl 버그 대부분(부분 복사 무시, 잘못된 errno, 패딩 누출, compat 깨짐)이 여기서 나온다
> **Don 기준 난이도**: SVC 예외 진입·스택 프레임에서 인자 꺼내기·abort 핸들러는 펌웨어에서 이미 했다 / 새로 배울 부분은 "커널이 유저 메모리를 만질 때만 쓰는 특수 명령(LDTR/STTR)과 fault를 견디게 해 주는 예외 테이블", 그리고 errno·ioctl ABI라는 **약속**의 세부
> **선행 노트**: A1(유저 쪽 syscall·`-errno` 규약), A3(파일 I/O), A6(시그널), E3(커널 모듈) — F3(wait queue), G4(char device), G5(ioctl·mmap), G7(misc device)로 이어진다

---

## 0. 큰 그림 — 이게 왜 필요한가

A1에서는 유저 쪽에서 본 syscall을 다뤘다: libc가 `x8`에 번호, `x0`~`x5`에 인자를 넣고 `svc #0`, 돌아온 `x0`가 음수면 `-1` + `errno`. 이 노트는 **반대편, 커널 쪽**이다. 드라이버 작성자 입장에서 경계는 세 가지 방향으로 넘어온다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="30" width="190" height="200" rx="8" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="115" y="54" text-anchor="middle" font-size="14" font-weight="bold">유저 공간 (EL0)</text>
  <text x="115" y="84" text-anchor="middle" font-size="12">앱: read(fd, buf, n)</text>
  <text x="115" y="106" text-anchor="middle" font-size="12">libc 래퍼</text>
  <text x="115" y="128" text-anchor="middle" font-size="12">buf = 유저 가상 주소</text>
  <text x="115" y="150" text-anchor="middle" font-size="12">(TTBR0 영역)</text>
  <rect x="470" y="30" width="190" height="200" rx="8" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="565" y="54" text-anchor="middle" font-size="14" font-weight="bold">커널 공간 (EL1)</text>
  <text x="565" y="84" text-anchor="middle" font-size="12">el0_svc → vfs_read</text>
  <text x="565" y="106" text-anchor="middle" font-size="12">드라이버 e6_read()</text>
  <text x="565" y="128" text-anchor="middle" font-size="12">kbuf = 커널 주소</text>
  <text x="565" y="150" text-anchor="middle" font-size="12">(TTBR1 영역)</text>
  <line x1="210" y1="80" x2="468" y2="80" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="339" y="72" text-anchor="middle" font-size="13">① 제어: svc #0 (x8 = 번호)</text>
  <line x1="210" y1="130" x2="468" y2="130" stroke="#3f9a6b" stroke-width="2" marker-start="url(#ah1)" marker-end="url(#ah1)"/>
  <text x="339" y="122" text-anchor="middle" font-size="13">② 데이터: 포인터만 넘어온다</text>
  <text x="339" y="148" text-anchor="middle" font-size="12">copy_to_user / copy_from_user</text>
  <line x1="468" y1="190" x2="212" y2="190" stroke="#d0564a" stroke-dasharray="6 4" marker-end="url(#ah1)"/>
  <text x="339" y="182" text-anchor="middle" font-size="13">③ 결과: x0 = 값 또는 -errno</text>
  <rect x="20" y="248" width="200" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="120" y="270" text-anchor="middle" font-size="12">§1–2 syscall 진입·복귀</text>
  <rect x="240" y="248" width="200" height="34" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="270" text-anchor="middle" font-size="12">§3–4 uaccess · 예외 테이블</text>
  <rect x="460" y="248" width="200" height="34" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="560" y="270" text-anchor="middle" font-size="12">§5–6 errno · ioctl ABI</text>
</svg>
```
그림 1 — 경계를 넘는 것은 세 가지다. ① 제어(예외로 EL1 진입), ② 데이터(유저가 준 주소를 커널이 **특수한 방법으로만** 읽고 쓴다), ③ 결과(`x0` 하나에 성공값 또는 음수 에러 코드).

Don의 펌웨어 경험으로 옮기면:

- ① 은 RTOS의 **SVC 핸들러**와 같다. 태스크가 `svc`를 치면 핸들러가 예외 스택 프레임에서 r0~r3를 꺼내 서비스 번호로 함수 테이블을 찾는다. Linux도 똑같이 레지스터를 `struct pt_regs`에 저장하고 `sys_call_table[x8]`을 부른다.
- ② 가 펌웨어에는 거의 없는 부분이다. MPU로 태스크를 격리한 RTOS라면 "커널이 태스크가 넘긴 포인터를 믿어도 되나?"를 고민했을 것이다. Linux는 이 문제를 **세 겹**으로 푼다: 주소 범위 검사(`access_ok`), 유저 권한으로만 접근하는 명령(LDTR/STTR) + PAN, 그리고 접근이 fault 나면 죽지 않고 에러로 바꾸는 **예외 테이블**.
- ③ 은 SSD 펌웨어의 NVMe completion status와 같은 **약속**이다. status code를 아무렇게나 주면 호스트 드라이버가 잘못 재시도하듯, 드라이버가 errno를 틀리게 주면 유저 프로그램이 잘못 반응한다.

이 노트에서 하는 실습:

| 절 | 내용 | 실습 |
|---|---|---|
| §1 | `svc #0` → `sys_call_table` → `__arm64_sys_read` | 예제 1–5 (소스·심볼·디스어셈블·전처리) |
| §2 | `ksys_read` → `vfs_read` → 드라이버, 복귀와 시그널 체크 | 예제 6–7 (`e6_dev` 모듈, `dump_stack`) |
| §3 | `copy_to/from_user`, `get/put_user`, 부분 복사 | 예제 8–9 |
| §4 | 예외 테이블과 PAN — 나쁜 포인터가 EFAULT가 되는 원리 | 예제 10–12 |
| §5 | errno 규약, `ERESTARTSYS`와 시그널 | 예제 13 |
| §6 | ioctl 번호와 구조체 ABI(패딩·compat) | 예제 14–16 |
| §7 | syscall + 복사 비용 측정 | 예제 17 + 차트 |

> 실행 환경: 커널 소스·심볼은 Lima VM `elinux`(aarch64, 호스트 커널 `uname -r` = `7.0.0-38-generic`) 안의 공용 랩 `/var/lab/src/linux-6.18.55`와 빌드 트리 `/var/lab/out/linux-build`(vmlinux, System.map)에서 확인했다. 모듈·테스트 출력은 QEMU `virt`(기본 `-cpu cortex-a57`, 512 MB, TCG)로 부팅한 **게스트 커널 6.18.55**(BusyBox initramfs)에서 실제로 돌린 결과다. 테스트 프로그램은 VM에서 `gcc -static`으로 빌드해 initramfs에 넣었다. BusyBox에는 `strace`가 없으므로 errno는 테스트 프로그램이 직접 찍는다. 주소·타임스탬프·PID는 실행마다 다르다.

---

## 1. syscall 진입 — `svc #0`에서 `sys_call_table`까지

### 1.1 직관 — "예외 벡터 → 레지스터 저장 → 번호로 테이블 점프"

Cortex-R 펌웨어에서 SVC를 처리하던 순서를 떠올리면 된다.

1. 하드웨어: 예외 발생, 복귀 주소·상태 저장(`ELR_EL1`, `SPSR_EL1`), `VBAR_EL1` 벡터로 점프.
2. 소프트웨어 진입부: 범용 레지스터 전부를 스택에 저장 (Linux: `struct pt_regs`).
3. 디스패처: 예외 원인(ESR)을 보고 "SVC구나" → 번호로 핸들러 테이블 인덱싱.
4. 핸들러 실행 → 반환값을 저장된 r0(=`pt_regs->regs[0]`) 자리에 써 넣음.
5. 복귀 전 할 일(스케줄, 시그널) 처리 → 레지스터 복원 → `eret`.

arm64 Linux 6.18의 실제 함수 이름은 이렇다.

| 단계 | 함수 (파일) | 하는 일 |
|---|---|---|
| 벡터 | `kernel_ventry 0, t, 64, sync` (`arch/arm64/kernel/entry.S`) | EL0 64비트 동기 예외 벡터 슬롯 |
| 진입 | `el0t_64_sync` (entry.S의 `entry_handler` 매크로가 생성) | `kernel_entry 0` = 레지스터를 `pt_regs`에 저장, `bl el0t_64_sync_handler`, 끝나면 `b ret_to_user` |
| 분기 | `el0t_64_sync_handler()` (`entry-common.c`) | `ESR_EL1.EC`가 `ESR_ELx_EC_SVC64`면 `el0_svc()` |
| 문맥 | `el0_svc()` (static, entry-common.c) | 진입 처리, 인터럽트 다시 켬(`local_daif_restore`), `do_el0_svc()`, 그리고 **복귀 처리** `arm64_exit_to_user_mode()` |
| 공통 | `do_el0_svc()` → `el0_svc_common()` (static, `syscall.c`) | `orig_x0`·`syscallno` 저장, ptrace/seccomp 등 trace 작업 |
| 호출 | `invoke_syscall()` (static, syscall.c) | 번호 범위 검사 + `array_index_nospec` → `sys_call_table[scno](regs)` → 반환값을 `regs->regs[0]`에 |
| 핸들러 | `__arm64_sys_read(const struct pt_regs *)` | `SYSCALL_DEFINE3(read, ...)`가 만든 래퍼 (§1.6) |

### 1.2 예제 1 — 함수 이름을 소스와 System.map에서 확인

무엇을 확인하나: 위 표의 이름이 6.18.55 소스에 실제로 있는지, 그리고 링크된 커널에서 어떤 것이 전역(`T`)이고 어떤 것이 static(`t`)인지.

```sh
cd /var/lab/src/linux-6.18.55
grep -n "entry_handler\s*0, t, 64, sync" arch/arm64/kernel/entry.S
grep -nE "^(asmlinkage void noinstr el0t_64_sync_handler|static void noinstr el0_svc)\(" arch/arm64/kernel/entry-common.c
grep -nE "^(static void invoke_syscall|static void el0_svc_common|void do_el0_svc)\(" arch/arm64/kernel/syscall.c
grep -n "^const syscall_fn_t sys_call_table" arch/arm64/kernel/sys.c
grep -nE "^SYSCALL_DEFINE3\((read|ioctl)," fs/read_write.c fs/ioctl.c
grep -E " (el0t_64_sync|el0t_64_sync_handler|el0_svc|do_el0_svc|el0_svc_common[.a-z0-9]*|invoke_syscall[.a-z0-9]*|sys_call_table|__arm64_sys_read|ksys_read|vfs_read)$" /var/lab/out/linux-build/System.map
```

```text
596:	entry_handler	0, t, 64, sync
743:static void noinstr el0_svc(struct pt_regs *regs)
762:asmlinkage void noinstr el0t_64_sync_handler(struct pt_regs *regs)
38:static void invoke_syscall(struct pt_regs *regs, unsigned int scno,
73:static void el0_svc_common(struct pt_regs *regs, int scno, int sc_nr,
149:void do_el0_svc(struct pt_regs *regs)
60:const syscall_fn_t sys_call_table[__NR_syscalls] = {
fs/read_write.c:722:SYSCALL_DEFINE3(read, unsigned int, fd, char __user *, buf, size_t, count)
fs/ioctl.c:583:SYSCALL_DEFINE3(ioctl, unsigned int, fd, unsigned int, cmd, unsigned long, arg)
ffff800080011404 t el0t_64_sync
ffff800080046180 t invoke_syscall.constprop.0
ffff800080046260 t el0_svc_common.constprop.0
ffff800080046340 T do_el0_svc
ffff800080371dec T vfs_read
ffff8000803728e0 T ksys_read
ffff8000803729e0 T __arm64_sys_read
ffff800081236830 t el0_svc
ffff800081236c40 T el0t_64_sync_handler
ffff800081260ca8 D sys_call_table
```

출력에서 볼 것: `invoke_syscall`과 `el0_svc_common`은 소스에서 `static`이라 System.map에 소문자 `t`로 나오고, 이름 뒤에 `.constprop.0`이 붙었다. GCC가 "항상 같은 상수 인자(`sys_call_table`, `__NR_syscalls`)로 불린다"는 걸 알고 그 인자를 박아 넣은 **복제본**을 만든 것이다 (constant propagation). 이 빌드에서는 인라인되지 않고 별도 함수로 남았지만, 컴파일러·설정이 바뀌면 인라인되어 스택 트레이스에서 사라질 수도 있다. 트레이스에서 이름이 하나 빠져 보여도 놀라지 말 것.

> 함정: static 함수는 `EXPORT_SYMBOL`도 안 되어 있고 모듈에서 부를 수도 없다. "syscall 테이블을 모듈에서 패치한다" 같은 옛날 인터넷 예제는 지금 커널에서 안 된다(`sys_call_table`은 export되지 않으며 rodata 쪽에 있다).

### 1.3 예제 2 — `struct pt_regs`: 저장된 유저 레지스터

무엇을 확인하나: 진입부가 스택에 저장하는 구조체의 실제 레이아웃. syscall 인자와 반환값이 어디 있는지.

```sh
cd /var/lab/out/linux-build && pahole -C pt_regs vmlinux
```

```text
struct pt_regs {
	union {
		struct user_pt_regs user_regs;           /*     0   272 */
		struct {
			u64        regs[31];             /*     0   248 */
			/* --- cacheline 3 boundary (192 bytes) was 56 bytes ago --- */
			u64        sp;                   /*   248     8 */
			/* --- cacheline 4 boundary (256 bytes) --- */
			u64        pc;                   /*   256     8 */
			u64        pstate;               /*   264     8 */
		};                                       /*     0   272 */
	};                                               /*     0   272 */
	/* --- cacheline 4 boundary (256 bytes) was 16 bytes ago --- */
	u64                        orig_x0;              /*   272     8 */
	s32                        syscallno;            /*   280     4 */
	u32                        pmr;                  /*   284     4 */
	u64                        sdei_ttbr1;           /*   288     8 */
	struct frame_record_meta   stackframe;           /*   296     0 */

	/* size: 320, cachelines: 5, members: 6 */
	/* padding: 24 */
};
```

출력에서 볼 것:

- `regs[0]`~`regs[30]` = 유저의 `x0`~`x30`. syscall 인자는 `regs[0]`~`regs[5]`, 번호는 `regs[8]`.
- `pc`는 `ELR_EL1`에서 온 복귀 주소(= `svc` 다음 명령), `pstate`는 `SPSR_EL1`.
- **`orig_x0`가 따로 있는 이유**: 반환값이 `regs[0]`를 덮어쓰기 때문이다. 시그널 때문에 syscall을 **재시작**해야 하면 첫 번째 인자를 되살려야 하는데, 그 원본이 `orig_x0`에 있다 (§5.4). `el0_svc_common()`의 첫 줄이 `regs->orig_x0 = regs->regs[0];`이다.
- `stackframe` 크기가 0으로 보이는 것은 랩 커널이 `CONFIG_DEBUG_INFO_REDUCED=y`라 그 타입 정보가 빠졌기 때문이다. 전체 크기 320 B는 정확하다.

펌웨어 비유: Cortex-M의 예외 진입 때 하드웨어가 쌓는 8-word 스택 프레임 + 소프트웨어가 추가로 쌓는 r4~r11. Linux는 그걸 전부 소프트웨어(`kernel_entry` 매크로)가 한 구조체로 쌓는다.

### 1.4 예제 3 — `sys_call_table`의 실제 내용

무엇을 확인하나: 테이블이 어떻게 만들어지고, 63번(`read`)·29번(`ioctl`) 칸에 정말 무엇이 들어 있는지.

`arch/arm64/kernel/sys.c`는 테이블을 이렇게 만든다 (랩 소스 그대로):

```c
#undef __SYSCALL
#define __SYSCALL(nr, sym)	[nr] = __arm64_##sym,

const syscall_fn_t sys_call_table[__NR_syscalls] = {
	[0 ... __NR_syscalls - 1] = __arm64_sys_ni_syscall,
#include <asm/syscall_table_64.h>
};
```

먼저 모든 칸을 `sys_ni_syscall`(= `-ENOSYS`)로 채우고, 빌드 때 `scripts/syscall.tbl`에서 생성된 `syscall_table_64.h`의 `__SYSCALL(63, sys_read)` 같은 줄로 덮어쓴다. 링크된 vmlinux에서 gdb로 칸을 읽어 본다 (커널을 실행하지 않고 정적 데이터만 읽는다):

```sh
cd /var/lab/out/linux-build
grep -nE "^__SYSCALL(_WITH_COMPAT)?\((29|63|64|173)," arch/arm64/include/generated/asm/syscall_table_64.h
gdb -batch -ex "p/a ((unsigned long *)&sys_call_table)[63]" -ex "p/a ((unsigned long *)&sys_call_table)[29]" \
    -ex "p/a ((unsigned long *)&sys_call_table)[469]" -ex "p/a ((unsigned long *)&sys_call_table)[300]" vmlinux
```

```text
30:__SYSCALL_WITH_COMPAT(29, sys_ioctl, compat_sys_ioctl)
64:__SYSCALL(63, sys_read)
65:__SYSCALL(64, sys_write)
174:__SYSCALL(173, sys_getppid)
$1 = 0xffff8000803729e0 <__arm64_sys_read>
$2 = 0xffff80008038da90 <__arm64_sys_ioctl>
$3 = 0xffff8000803c5940 <__arm64_sys_file_setattr>
$4 = 0xffff80008003d608 <__arm64_sys_ni_syscall>
```

출력에서 볼 것: 63번 칸 = `__arm64_sys_read`(예제 1의 System.map 주소와 같다). 마지막 칸 469번은 6.18에서 추가된 최신 syscall이고, 비어 있는 300번 칸은 `__arm64_sys_ni_syscall`이다. `__NR_syscalls`는 이 커널에서 470이다. ioctl은 `_WITH_COMPAT`이라 32비트 앱용 테이블에는 `compat_sys_ioctl`이 들어간다 (§6.6에서 실제로 탄다).

### 1.5 예제 4 — `invoke_syscall` 디스어셈블: 테이블 점프와 반환값 저장

무엇을 확인하나: C 코드 `syscall_table[array_index_nospec(scno, sc_nr)]` 호출과 `syscall_set_return_value()`가 기계어로 어떻게 되는지.

```sh
cd /var/lab/out/linux-build
objdump -d --no-show-raw-insn --start-address=0xffff800080046180 --stop-address=0xffff8000800461f0 vmlinux | tail -n +7
objdump -d --no-show-raw-insn --start-address=0xffff8000803729e0 --stop-address=0xffff800080372a04 vmlinux | tail -n +7
```

```text
ffff800080046180 <invoke_syscall.constprop.0>:
ffff800080046180:	paciasp
ffff800080046184:	stp	x29, x30, [sp, #-32]!
ffff800080046188:	mov	x29, sp
ffff80008004618c:	stp	x19, x20, [sp, #16]
ffff800080046190:	mov	x19, x0
ffff800080046194:	nop
ffff800080046198:	cmp	w1, #0x1d5
ffff80008004619c:	b.hi	ffff80008004620c <invoke_syscall.constprop.0+0x8c>  // b.pmore
ffff8000800461a0:	mov	w0, w1
ffff8000800461a4:	cmp	x0, #0x1d6
ffff8000800461a8:	ngc	x0, xzr
ffff8000800461ac:	csdb
ffff8000800461b0:	and	w1, w1, w0
ffff8000800461b4:	mov	x0, x19
ffff8000800461b8:	ldr	x1, [x2, x1, lsl #3]
ffff8000800461bc:	blr	x1
ffff8000800461c0:	mrs	x1, sp_el0
ffff8000800461c4:	ldr	x2, [x1]
ffff8000800461c8:	and	x1, x0, #0xffffffff
ffff8000800461cc:	tst	w2, #0x400000
ffff8000800461d0:	csel	x0, x1, x0, ne	// ne = any
ffff8000800461d4:	str	x0, [x19]
ffff8000800461d8:	nop
ffff8000800461dc:	mov	sp, x29
ffff8000800461e0:	ldp	x19, x20, [sp, #16]
ffff8000800461e4:	ldp	x29, x30, [sp], #32
ffff8000800461e8:	autiasp
ffff8000800461ec:	ret
ffff8000803729e0 <__arm64_sys_read>:
ffff8000803729e0:	paciasp
ffff8000803729e4:	stp	x29, x30, [sp, #-16]!
ffff8000803729e8:	mov	x29, sp
ffff8000803729ec:	ldp	x1, x2, [x0, #8]
ffff8000803729f0:	ldr	w0, [x0]
ffff8000803729f4:	bl	ffff8000803728e0 <ksys_read>
ffff8000803729f8:	ldp	x29, x30, [sp], #16
ffff8000803729fc:	autiasp
ffff800080372a00:	ret
```

출력에서 볼 것:

- `cmp w1, #0x1d5` / `b.hi`: 번호가 469(=0x1d5)보다 크면 `do_ni_syscall` 쪽으로. 그다음 `cmp`·`ngc`·`csdb`·`and`가 `array_index_nospec()`이다 — 번호 검사를 CPU가 **투기 실행으로 건너뛰어도** 인덱스가 0으로 눌리게 만드는 Spectre-v1 방어. `csdb`는 이를 위한 배리어 명령이다.
- `ldr x1, [x2, x1, lsl #3]` → `blr x1`: 테이블에서 8바이트 함수 포인터를 꺼내 간접 호출. 인자는 `x0 = regs` 하나뿐이다.
- `str x0, [x19]`: 반환값을 `regs->regs[0]`(pt_regs 오프셋 0)에 저장 = `syscall_set_return_value()`. 그 앞의 `tst w2, #0x400000`은 스레드 플래그 bit 22(`TIF_32BIT`)를 보고 32비트 태스크면 반환값을 32비트로 자르는 부분이다.
- `__arm64_sys_read`는 `pt_regs`에서 `ldp x1, x2, [x0, #8]`(= `regs[1]`, `regs[2]`), `ldr w0, [x0]`(= `regs[0]`의 하위 32비트, `unsigned int fd`)를 꺼내 일반 C 호출 규약으로 `ksys_read(fd, buf, count)`를 부른다. 왜 이런 래퍼가 있는지가 다음 절이다.

### 1.6 예제 5 — `SYSCALL_DEFINEn`이 실제로 펼쳐지는 모습

무엇을 확인하나: 커널의 syscall 정의 매크로가 만드는 세 함수(`__arm64_sys_*`, `__se_sys_*`, `__do_sys_*`)를 전처리 결과로 직접 본다.

모듈 디렉터리에 전처리만 할 파일을 하나 만든다 (모듈로 링크하지는 않는다).

```c
// SPDX-License-Identifier: GPL-2.0
/* SYSCALL_DEFINE3 가 무엇으로 펼쳐지는지 보려고 전처리만 하는 파일 */
#include <linux/syscalls.h>

SYSCALL_DEFINE3(e6read, unsigned int, fd, char __user *, buf, size_t, count)
{
	return fd + count + (long)buf;
}
```

```sh
cd /var/tmp/E6/mod && make -C /var/lab/out/linux-build M=$PWD e6_sysdef.i
sed -n "78172,\$p" e6_sysdef.i | sed -e "s/; /;\n/g" -e "s/) { /) {\n  /g"
```

출력은 한 줄 2,000자가 넘어서 `;` 뒤에 줄바꿈만 넣었다. 아래는 그 결과에서 **긴 타입 표현식만 `...`로 줄인** 것이다. `__se_sys_e6read`의 인자 타입은 원래 `__typeof(__builtin_choose_expr((__builtin_types_compatible_p(typeof(( unsigned int)0), typeof(0LL)) || ...), 0LL, 0L)) fd` 같은 식인데, 뜻은 "64비트 타입이 아니면 `long`"이다.

```text
 long __arm64_sys_e6read(const struct pt_regs *regs);
;
static long __se_sys_e6read(__typeof(...) fd, __typeof(...) buf, __typeof(...) count);
static inline __attribute__((__gnu_inline__)) __attribute__((__unused__)) __attribute__((__no_instrument_function__)) long __do_sys_e6read(unsigned int fd, char * buf, size_t count);
long __arm64_sys_e6read(const struct pt_regs *regs) {
  return __se_sys_e6read(regs->regs[0], regs->regs[1], regs->regs[2]);
} static long __se_sys_e6read(__typeof(...) fd, __typeof(...) buf, __typeof(...) count) {
  long ret = __do_sys_e6read(( unsigned int) fd, ( char *) buf, ( size_t) count);
(void)((int)sizeof(struct {_Static_assert(!(... && sizeof(unsigned int) > sizeof(long)), "!__TYPE_IS_LL(unsigned int) && sizeof(unsigned int) > sizeof(long)" " is true");})), ...;
do { } while (0);
return ret;
} static inline __attribute__((__gnu_inline__)) __attribute__((__unused__)) __attribute__((__no_instrument_function__)) long __do_sys_e6read(unsigned int fd, char * buf, size_t count)
{
 return fd + count + (long)buf;
}
```

세 함수의 역할:

| 함수 | 인자 | 역할 |
|---|---|---|
| `__arm64_sys_e6read` | `const struct pt_regs *` | 테이블에 들어가는 **진입점**. `regs->regs[0..2]`를 꺼낸다. 레지스터 값을 C 인자로 바로 받지 않으므로, 쓰레기 값이 든 다른 레지스터가 커널 C 코드로 흘러 들어가지 않는다 |
| `__se_sys_e6read` | 전부 `long` | "sign extension" 단계. 64비트 레지스터의 상위 비트를 타입에 맞게 **잘라서(cast)** 넘긴다. `_Static_assert`는 인자 타입이 `long`보다 크지 않은지 컴파일 때 검사 |
| `__do_sys_e6read` | 원래 타입 | `{ ... }` 본문이 여기에 붙는다. 우리가 쓴 코드 |

출력에서 볼 것: `__user` 표시는 전처리 후 사라졌다(`char * buf`). `__user`는 **sparse 정적 분석기**에게만 의미가 있는 주석이다(E5). 즉 컴파일러는 유저 포인터를 직접 역참조해도 막지 않는다 — 막아 주는 것은 sparse(`make C=1`)와 런타임의 PAN(§4.5)뿐이다. ftrace syscall 이벤트를 켜면(`CONFIG_FTRACE_SYSCALLS`) 여기에 `SYSCALL_METADATA`도 붙는데, 랩 커널은 `CONFIG_FTRACE`가 꺼져 있어서 없다.

> 함정: 드라이버는 syscall을 추가하지 않는다. 새 syscall은 upstream 리뷰를 거쳐 모든 아키텍처 테이블에 번호를 받는 큰 일이다. 드라이버가 유저에게 기능을 여는 문은 `file_operations`(read/write/ioctl/mmap/poll)와 sysfs다 (G4, G5, G8).

---

## 2. 드라이버까지 — `ksys_read` → `vfs_read` → `f_op->read`, 그리고 돌아오는 길

### 2.1 VFS 구간 — 랩 소스 그대로

`ksys_read()`는 fd를 `struct file`로 바꾸고 파일 위치(`f_pos`)를 지역 변수로 복사해서 넘긴다.

```c
ssize_t ksys_read(unsigned int fd, char __user *buf, size_t count)
{
	CLASS(fd_pos, f)(fd);
	ssize_t ret = -EBADF;

	if (!fd_empty(f)) {
		loff_t pos, *ppos = file_ppos(fd_file(f));
		if (ppos) {
			pos = *ppos;
			ppos = &pos;
		}
		ret = vfs_read(fd_file(f), buf, count, ppos);
		if (ret >= 0 && ppos)
			fd_file(f)->f_pos = pos;
	}
	return ret;
}
```

`vfs_read()`가 드라이버 앞의 마지막 관문이다.

```c
ssize_t vfs_read(struct file *file, char __user *buf, size_t count, loff_t *pos)
{
	ssize_t ret;

	if (!(file->f_mode & FMODE_READ))
		return -EBADF;
	if (!(file->f_mode & FMODE_CAN_READ))
		return -EINVAL;
	if (unlikely(!access_ok(buf, count)))
		return -EFAULT;

	ret = rw_verify_area(READ, file, pos, count);
	if (ret)
		return ret;
	if (count > MAX_RW_COUNT)
		count =  MAX_RW_COUNT;

	if (file->f_op->read)
		ret = file->f_op->read(file, buf, count, pos);
	else if (file->f_op->read_iter)
		ret = new_sync_read(file, buf, count, pos);
	else
		ret = -EINVAL;
	...
```

드라이버 작성자가 알아야 할 점:

- 드라이버의 `.read`가 불리기 전에 **`access_ok(buf, count)`가 이미 한 번 검사**됐다. 커널 주소를 넘기면 드라이버까지 오지도 않고 `-EFAULT`다 (예제 9에서 확인). 그래도 드라이버는 `copy_to_user`를 써야 한다 — `access_ok`는 "범위가 유저 영역인가"만 보지, "그 주소에 실제로 메모리가 매핑돼 있나"는 모른다.
- `ppos`는 `file->f_pos`가 아니라 **지역 변수의 주소**다. 드라이버는 `*ppos`만 갱신하고, `ksys_read`가 성공했을 때 `f_pos`에 반영한다. `pread()`는 같은 경로에서 유저가 준 offset을 `ppos`로 넘긴다.
- `count`는 `MAX_RW_COUNT`(약 2 GB)로 잘린다. 드라이버는 `count`가 0일 수도 있다는 점을 처리해야 한다 (§10 실수 표).

### 2.2 예제 6 — 실습 드라이버 `e6_dev` (1/3): 전역 상태와 read

무엇을 확인하나: misc device 하나로 read/write/ioctl을 제공하면서, 부분 복사·`EAGAIN`·`ERESTARTSYS`·`EFAULT`를 규약대로 돌려주는 드라이버의 read 부분.

장치 모델: 64 KiB 버퍼 하나를 "파일"처럼 다룬다. write는 `*ppos`에 쓰고, read는 `*ppos`부터 읽는다. 데이터가 하나도 없을 때의 read는 blocking(데이터가 올 때까지 잠듦) 또는 `O_NONBLOCK`이면 `-EAGAIN`. 센서 키트라면 "IMU FIFO를 읽는 char device"의 축소판이다. `stack_once`, `allow_bad`, `verbose`는 실험용 모듈 파라미터다.

```c
// SPDX-License-Identifier: GPL-2.0
/* e6_dev: misc char device — read/write/ioctl 에서 유저 메모리를 올바르게 다루는 예제 */
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include "e6_ioctl.h"

#define E6_BUF_SZ (64 * 1024)

static char *e6_buf;			/* 장치 안 데이터 (64 KiB) */
static size_t e6_len;			/* 유효한 바이트 수 */
static DEFINE_MUTEX(e6_lock);
static DECLARE_WAIT_QUEUE_HEAD(e6_wq);
static struct file *e6_owner;		/* E6_IOC_CLAIM 소유자 */
static u32 e6_thresh;
static u64 e6_nreads, e6_nwrites;
static char e6_name[16] = "e6";

static int stack_once;			/* >0 이면 그 횟수만큼 dump_stack() */
module_param(stack_once, int, 0644);
static bool allow_bad;			/* E6_IOC_BAD_DEREF 허용 (실험용) */
module_param(allow_bad, bool, 0644);
static bool verbose;
module_param(verbose, bool, 0644);

static void e6_maybe_stack(const char *who)
{
	if (stack_once > 0) {
		stack_once--;
		pr_info("e6: dump_stack() from %s\n", who);
		dump_stack();
	}
}
```

```c
static ssize_t e6_read(struct file *f, char __user *ubuf, size_t count, loff_t *ppos)
{
	size_t n, left;

	e6_maybe_stack("read");
	if (!count)
		return 0;				/* 0바이트 요청은 성공 0 */
	if (!READ_ONCE(e6_len)) {			/* 데이터 없음 → 기다리거나 EAGAIN */
		if (f->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (wait_event_interruptible(e6_wq, READ_ONCE(e6_len) > 0)) {
			pr_info("e6: read: signal while waiting -> -ERESTARTSYS\n");
			return -ERESTARTSYS;
		}
	}
	if (mutex_lock_interruptible(&e6_lock))
		return -ERESTARTSYS;
	if (*ppos >= e6_len) {				/* EOF */
		mutex_unlock(&e6_lock);
		return 0;
	}
	n = min_t(size_t, count, e6_len - *ppos);
	left = copy_to_user(ubuf, e6_buf + *ppos, n);	/* 반환값 = 못 복사한 바이트 */
	if (left != n)
		e6_nreads++;
	mutex_unlock(&e6_lock);
	if (left && verbose)
		pr_info("e6: read: asked %zu, copy_to_user left %zu\n", n, left);
	if (left == n)
		return -EFAULT;				/* 한 바이트도 못 줬다 */
	*ppos += n - left;
	return n - left;				/* 일부라도 줬으면 그 개수 */
}
```

read의 반환 규칙을 한 줄씩 짚으면:

1. `count == 0` → `0`. 이 줄이 없으면 `n = 0`, `left = 0`, `left == n`이 참이 되어 **0바이트 요청에 `-EFAULT`**를 돌려주는 버그가 생긴다 (처음 짰을 때 실제로 이 버그가 있었다 — 측정 코드에서 발견).
2. 데이터가 없고 `O_NONBLOCK` → `-EAGAIN` (유저는 poll/epoll로 기다린다, A4·G6).
3. 잠들었다가 시그널로 깨면 → `-ERESTARTSYS` (§5.4).
4. 위치가 끝 이상 → `0` = EOF.
5. 복사 결과 한 바이트도 못 줬으면 `-EFAULT`, 일부라도 줬으면 **준 만큼의 양수**. 그리고 `*ppos`도 준 만큼만 전진.

`copy_to_user`를 **mutex를 잡은 채로** 부르는 점도 보자. 유저 페이지가 아직 메모리에 없으면 page fault 처리 중에 잠들 수 있는데, mutex는 잠들어도 되는 락이라 괜찮다. spinlock을 잡은 채로는 절대 안 된다 (F2, F4) — 그럴 땐 커널 임시 버퍼에 복사해 두고 락을 푼 뒤 `copy_to_user`한다.

write도 대칭이다.

```c
static ssize_t e6_write(struct file *f, const char __user *ubuf, size_t count, loff_t *ppos)
{
	size_t n, left;

	e6_maybe_stack("write");
	if (!count)
		return 0;
	if (*ppos >= E6_BUF_SZ)
		return -ENOSPC;
	n = min_t(size_t, count, E6_BUF_SZ - *ppos);
	if (mutex_lock_interruptible(&e6_lock))
		return -ERESTARTSYS;
	left = copy_from_user(e6_buf + *ppos, ubuf, n);
	if (left && verbose)
		pr_info("e6: write: asked %zu, copy_from_user left %zu\n", n, left);
	if (left == n) {
		mutex_unlock(&e6_lock);
		return -EFAULT;
	}
	*ppos += n - left;
	if (*ppos > e6_len)
		e6_len = *ppos;
	e6_nwrites++;
	mutex_unlock(&e6_lock);
	wake_up_interruptible(&e6_wq);
	return n - left;
}
```

ioctl 부분과 등록 부분은 §6.3에 있다. Kbuild와 빌드:

```make
obj-m += e6_dev.o
```

```sh
cd /var/tmp/E6/mod && make -C /var/lab/out/linux-build M=$PWD modules
```

```text
  CC [M]  e6_dev.o
  MODPOST Module.symvers
  LD [M]  e6_dev.ko
```

출력에서 볼 것: 경고 0개. 이 모듈과 테스트 프로그램을 initramfs에 넣어 부팅하는 스크립트(`run.sh`)는 다음과 같다. 이후 모든 게스트 출력은 이 방법으로 얻었다.

```sh
#!/bin/bash
# usage: run.sh <guest-script> [cpu] [extra append]
set -e
W=/var/tmp/E6; S=$1; CPU=${2:-cortex-a57}; EXTRA=$3
rm -rf $W/rootfs && mkdir -p $W/rootfs && cp -a /var/lab/build/rootfs/. $W/rootfs/
cp $W/mod/e6_dev.ko $W/bin/e6_t* $W/rootfs/
cp /Users/donh/.elinux_scratch/E6/guest/$S $W/rootfs/t.sh
(cd $W/rootfs && find . | cpio -o -H newc --quiet | gzip) > $W/i.gz
timeout 600 qemu-system-aarch64 -machine virt -cpu $CPU -m 512M -nographic $QEMU_EXTRA \
  -kernel /var/lab/out/Image -initrd $W/i.gz \
  -append "console=ttyAMA0 quiet LAB_CMD=sh~/t.sh $EXTRA" </dev/null
```

### 2.3 예제 7 — 진짜 call trace: `dump_stack()`와 `/proc/<pid>/stack`

무엇을 확인하나: §1의 표와 §2.1의 경로가 실제 실행에서 그대로 나오는지. 랩 커널은 `CONFIG_FTRACE`가 꺼져 있어(tracefs 없음) function_graph 대신 두 가지 방법을 쓴다: (a) 드라이버 안에서 `dump_stack()`, (b) 잠든 태스크의 커널 스택을 `/proc/<pid>/stack`으로 읽기 (`CONFIG_STACKTRACE=y`).

게스트 스크립트 일부 (예제 8·9·14를 돌린 뒤라 장치에 데이터가 있는 상태):

```sh
dmesg -c > /dev/null
echo 1 > /sys/module/e6_dev/parameters/stack_once
dd if=/dev/e6_dev of=/dev/null bs=4 count=1 2>/dev/null
dmesg
```

(a) `dump_stack()` from read:

```text
[    2.333750] e6: dump_stack() from read
[    2.334290] CPU: 0 UID: 0 PID: 70 Comm: dd Tainted: G           O        6.18.55 #1 PREEMPT 
[    2.334431] Tainted: [O]=OOT_MODULE
[    2.334454] Hardware name: linux,dummy-virt (DT)
[    2.334610] Call trace:
[    2.334785]  show_stack+0x28/0x7c (C)
[    2.335431]  dump_stack_lvl+0x60/0x80
[    2.335555]  dump_stack+0x18/0x24
[    2.335589]  e6_read+0x23c/0x280 [e6_dev]
[    2.335994]  vfs_read+0xc4/0x33c
[    2.336025]  ksys_read+0x64/0x100
[    2.336090]  __arm64_sys_read+0x18/0x24
[    2.336106]  invoke_syscall.constprop.0+0x40/0xe0
[    2.336120]  el0_svc_common.constprop.0+0x38/0xe0
[    2.336132]  do_el0_svc+0x1c/0x28
[    2.336143]  el0_svc+0x38/0x118
[    2.336161]  el0t_64_sync_handler+0xa0/0xe4
[    2.336173]  el0t_64_sync+0x198/0x19c
```

(b) 데이터 없는 장치를 `cat`이 읽다가 잠든 상태의 스택 (`cat /dev/e6_dev > /dev/null &` 후 1초 뒤):

```sh
echo "wchan: $(cat /proc/$!/wchan)"; cat /proc/$!/stack
```

```text
wchan: e6_read
[<0>] e6_read+0x18c/0x280 [e6_dev]
[<0>] vfs_read+0xc4/0x33c
[<0>] ksys_read+0x64/0x100
[<0>] __arm64_sys_read+0x18/0x24
[<0>] invoke_syscall.constprop.0+0x40/0xe0
[<0>] el0_svc_common.constprop.0+0x38/0xe0
[<0>] do_el0_svc+0x1c/0x28
[<0>] el0_svc+0x38/0x118
[<0>] el0t_64_sync_handler+0xa0/0xe4
[<0>] el0t_64_sync+0x198/0x19c
```

출력에서 볼 것: 아래에서 위로 읽으면 §1 표 그대로다 — 벡터(`el0t_64_sync`) → 분기 → `el0_svc` → `do_el0_svc` → `el0_svc_common` → `invoke_syscall` → `__arm64_sys_read` → `ksys_read` → `vfs_read` → `e6_read`. (b)에서 `wchan`(잠든 함수)이 `e6_read`이고 오프셋이 (a)보다 앞(`0x18c`)이다 — `wait_event_interruptible` 안에서 `schedule()`로 잠든 지점이다. 실무에서 "프로세스가 D/S 상태로 멈췄다"면 가장 먼저 보는 파일이 이 `/proc/<pid>/stack`이다.

ioctl과 write도 같은 방법으로 찍었다:

```text
[    8.775037]  e6_write+0x1fc/0x204 [e6_dev]
[    8.775323]  vfs_write+0xd0/0x380
[    8.775354]  ksys_write+0x64/0x100
[    8.775367]  __arm64_sys_write+0x18/0x24
[    8.775380]  invoke_syscall.constprop.0+0x40/0xe0
...
[    8.890900]  e6_ioctl+0x4ec/0x500 [e6_dev]
[    8.890925]  __arm64_sys_ioctl+0xa4/0xf4
[    8.890948]  invoke_syscall.constprop.0+0x40/0xe0
...
```

(위 두 트레이스는 모듈 수정 전 빌드에서 찍은 것이라 `e6_*` 오프셋이 최종 빌드와 다르다. 아래쪽 공통 부분은 같아서 `...`로 생략했다.) ioctl은 `__arm64_sys_ioctl`에서 바로 `e6_ioctl`로 간다. 소스에서는 `SYSCALL_DEFINE3(ioctl)` → `do_vfs_ioctl()`(FIONBIO 같은 공통 명령 처리, 모르면 `-ENOIOCTLCMD`) → `vfs_ioctl()` → `filp->f_op->unlocked_ioctl`인데, static `vfs_ioctl()`이 인라인되어 트레이스에 안 보인다. `vfs_ioctl()`은 드라이버가 `-ENOIOCTLCMD`를 돌려주면 `-ENOTTY`로 바꿔 준다:

```c
static int vfs_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int error = -ENOTTY;

	if (!filp->f_op->unlocked_ioctl)
		goto out;

	error = filp->f_op->unlocked_ioctl(filp, cmd, arg);
	if (error == -ENOIOCTLCMD)
		error = -ENOTTY;
 out:
	return error;
}
```

### 2.4 돌아가는 길 — 반환값 저장, 스케줄, 시그널

`invoke_syscall`이 `regs->regs[0]`에 반환값을 쓰면, `el0_svc()`의 다음 줄 `arm64_exit_to_user_mode(regs)`가 유저로 돌아가기 전 할 일을 처리한다. 6.18에서 arm64는 공통 코드 `kernel/entry/common.c`의 루프를 쓴다:

```c
	while (ti_work & EXIT_TO_USER_MODE_WORK) {

		local_irq_enable_exit_to_user(ti_work);

		if (ti_work & (_TIF_NEED_RESCHED | _TIF_NEED_RESCHED_LAZY))
			schedule();

		if (ti_work & _TIF_UPROBE)
			uprobe_notify_resume(regs);

		if (ti_work & _TIF_PATCH_PENDING)
			klp_update_patch_state(current);

		if (ti_work & (_TIF_SIGPENDING | _TIF_NOTIFY_SIGNAL))
			arch_do_signal_or_restart(regs);
		...
		ti_work = read_thread_flags();
	}
```

즉 **syscall에서 돌아가는 순간이 스케줄링 지점이자 시그널 전달 지점**이다. 드라이버가 `-ERESTARTSYS`를 돌려주면 이 루프 안의 `arch_do_signal_or_restart()`(`arch/arm64/kernel/signal.c`)가 "재시작할지 `-EINTR`로 바꿀지"를 결정한다 (§5.4). 그다음 `ret_to_user` → `kernel_exit 0`이 `pt_regs`에서 레지스터를 복원하고 `eret`. 유저는 `x0`에서 결과를 받는다.

```svg
<svg viewBox="0 0 680 520" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="10" width="660" height="52" rx="6" fill="#4a7bd0" fill-opacity="0.06" stroke="none"/>
  <text x="18" y="28" font-size="12" font-weight="bold">EL0</text>
  <rect x="10" y="68" width="660" height="442" rx="6" fill="#e08a3c" fill-opacity="0.05" stroke="none"/>
  <text x="18" y="86" font-size="12" font-weight="bold">EL1</text>
  <rect x="60" y="20" width="350" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="235" y="41" text-anchor="middle" font-size="12">read(): x8=63, x0..x2 = fd, buf, count → svc #0</text>
  <rect x="60" y="92" width="350" height="32" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="235" y="113" text-anchor="middle" font-size="12">el0t_64_sync: kernel_entry → pt_regs 저장</text>
  <rect x="60" y="136" width="350" height="32" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="235" y="157" text-anchor="middle" font-size="12">el0t_64_sync_handler: ESR.EC == SVC64</text>
  <rect x="60" y="180" width="350" height="32" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="235" y="201" text-anchor="middle" font-size="12">el0_svc → do_el0_svc → el0_svc_common</text>
  <rect x="60" y="224" width="350" height="32" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="235" y="245" text-anchor="middle" font-size="12">invoke_syscall: sys_call_table[63](regs)</text>
  <rect x="60" y="268" width="350" height="32" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="235" y="289" text-anchor="middle" font-size="12">__arm64_sys_read → ksys_read (fd → file)</text>
  <rect x="60" y="312" width="350" height="32" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="235" y="333" text-anchor="middle" font-size="12">vfs_read: access_ok, rw_verify_area</text>
  <rect x="60" y="356" width="350" height="32" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="235" y="377" text-anchor="middle" font-size="12">f_op->read = e6_read → copy_to_user</text>
  <line x1="235" y1="52" x2="235" y2="90" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="235" y1="124" x2="235" y2="134" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="235" y1="168" x2="235" y2="178" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="235" y1="212" x2="235" y2="222" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="235" y1="256" x2="235" y2="266" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="235" y1="300" x2="235" y2="310" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="235" y1="344" x2="235" y2="354" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="440" y="356" width="220" height="32" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="550" y="377" text-anchor="middle" font-size="12">return n 또는 -errno</text>
  <line x1="410" y1="372" x2="438" y2="372" stroke="#d0564a" marker-end="url(#ah2)"/>
  <rect x="440" y="300" width="220" height="32" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="550" y="321" text-anchor="middle" font-size="12">regs->regs[0] = ret</text>
  <rect x="440" y="232" width="220" height="44" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="550" y="250" text-anchor="middle" font-size="12">exit_to_user_mode_loop</text>
  <text x="550" y="267" text-anchor="middle" font-size="12">need_resched? 시그널?</text>
  <rect x="440" y="164" width="220" height="44" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="550" y="182" text-anchor="middle" font-size="12">arch_do_signal_or_restart</text>
  <text x="550" y="199" text-anchor="middle" font-size="12">-ERESTARTSYS → 재시작/EINTR</text>
  <rect x="440" y="96" width="220" height="44" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="550" y="114" text-anchor="middle" font-size="12">ret_to_user: kernel_exit 0</text>
  <text x="550" y="131" text-anchor="middle" font-size="12">레지스터 복원 → eret</text>
  <rect x="440" y="20" width="220" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="550" y="41" text-anchor="middle" font-size="12">x0 → libc: 값 또는 -1+errno</text>
  <line x1="550" y1="354" x2="550" y2="334" stroke="#d0564a" stroke-dasharray="5 3" marker-end="url(#ah2)"/>
  <line x1="550" y1="298" x2="550" y2="278" stroke="#d0564a" stroke-dasharray="5 3" marker-end="url(#ah2)"/>
  <line x1="550" y1="230" x2="550" y2="210" stroke="#d0564a" stroke-dasharray="5 3" marker-end="url(#ah2)"/>
  <line x1="550" y1="162" x2="550" y2="142" stroke="#d0564a" stroke-dasharray="5 3" marker-end="url(#ah2)"/>
  <line x1="550" y1="94" x2="550" y2="54" stroke="#d0564a" stroke-dasharray="5 3" marker-end="url(#ah2)"/>
  <rect x="60" y="410" width="600" height="88" rx="6" fill="#888" fill-opacity="0.10" stroke="#888"/>
  <text x="76" y="432" font-size="12">왼쪽 열: 내려가는 길 (예제 7의 call trace를 위에서 아래로)</text>
  <text x="76" y="454" font-size="12">오른쪽 열: 올라오는 길 (반환값 저장 → 복귀 전 작업 → eret)</text>
  <text x="76" y="476" font-size="12">시그널·선점은 "돌아가기 직전"에만 처리된다 → syscall 복귀 = 전달 지점</text>
</svg>
```
그림 2 — arm64 6.18에서 `read()` 한 번의 왕복. 왼쪽 열 함수 이름은 예제 7의 실제 스택 트레이스와 같다. 오른쪽 열은 `el0_svc()` 안의 `arm64_exit_to_user_mode()`와 entry.S의 `ret_to_user`.

---

## 3. 유저 메모리 복사 — `copy_to_user` / `copy_from_user`와 친구들

### 3.1 왜 `*ubuf`라고 쓰면 안 되나

드라이버의 `.write`가 받은 `const char __user *ubuf`는 **유저 프로세스의 가상 주소**다. 이걸 커널이 그냥 `memcpy`하면 안 되는 이유는 네 가지다.

| 이유 | 무슨 일이 생기나 | 커널의 해결책 |
|---|---|---|
| 1. 주소가 커널 영역일 수 있다 | 악의적인 앱이 `write(fd, 커널주소, n)`으로 커널 메모리를 읽거나 덮게 만든다 | `access_ok()` — 범위가 `TASK_SIZE_MAX` 아래인지 검사 |
| 2. 매핑이 없거나 권한이 없을 수 있다 | NULL, 해제된 버퍼, PROT_NONE 페이지 → 커널 모드 data abort → **Oops** | 예외 테이블 — fault 나면 에러 경로로 점프 (§4) |
| 3. 매핑은 있지만 아직 물리 페이지가 없을 수 있다 | 처음 접근하는 malloc 버퍼, swap된 페이지 | 정상 page fault 처리(`handle_mm_fault`) 후 재시도 — 이 때문에 잠들 수 있다 |
| 4. 하드웨어가 막는다 (PAN) | ARMv8.1+ PAN이 켜져 있으면 EL1의 일반 load/store로 유저 페이지 접근 자체가 permission fault | 유저 접근은 **LDTR/STTR**(unprivileged load/store)로만 한다 |

펌웨어 비유: MPU로 태스크 메모리를 막아 둔 RTOS에서 SVC 핸들러가 태스크 버퍼를 읽으려면 "이 주소가 그 태스크 영역 안인가?"를 확인하고, 읽다가 MemManage fault가 나면 시스템을 죽이지 말고 그 태스크에 에러를 돌려줘야 한다. Linux의 uaccess가 정확히 이것을 일반화한 것이다. Linux에서는 **커널과 유저가 같은 주소 공간(TTBR0 = 유저, TTBR1 = 커널)에 동시에 보인다**는 점이 다르다 — 그래서 실수로 직접 역참조해도 "돌아가 버리는" 위험이 있다 (예제 12).

### 3.2 API 한 장 정리

| API | 방향·크기 | 반환값 | 언제 |
|---|---|---|---|
| `copy_to_user(to, from, n)` | 커널 → 유저, n 바이트 | **못 복사한 바이트 수** (0 = 성공) | read, ioctl 출력 구조체 |
| `copy_from_user(to, from, n)` | 유저 → 커널 | 못 복사한 바이트 수. 실패한 뒤쪽은 커널 버퍼를 **0으로 채움** | write, ioctl 입력 구조체 |
| `get_user(x, ptr)` | 유저 → 커널, `sizeof(*ptr)` (1/2/4/8) | 0 또는 `-EFAULT`. 실패 시 `x = 0` | int 하나 읽기 |
| `put_user(x, ptr)` | 커널 → 유저, 스칼라 하나 | 0 또는 `-EFAULT` | int 하나 쓰기 |
| `strncpy_from_user(dst, src, n)` | NUL 종료 문자열 | 길이(NUL 제외), `n`이면 잘림, 실패 시 `-EFAULT` | 이름·경로 문자열 |
| `memdup_user(src, len)` | 커널에 kmalloc + copy_from_user | 포인터 또는 `ERR_PTR(-ENOMEM/-EFAULT)` | 가변 길이 블롭 |
| `simple_read_from_buffer(to, count, ppos, from, avail)` | 커널 버퍼 → 유저, ppos 처리 포함 | 복사한 바이트, 0(EOF), `-EFAULT` | 고정 버퍼를 보여주는 read (debugfs 등) |
| `access_ok(ptr, n)` | 검사만 | 1/0 | `__get_user` 같은 검사 없는 변형 앞에서만 |
| `u64_to_user_ptr(x)` | `__u64` → `void __user *` | 포인터 | ioctl 구조체 안의 포인터 필드 |
| `clear_user(to, n)` | 유저 메모리 0으로 | 못 지운 바이트 수 | 남는 부분 채우기 |

`copy_from_user`의 0-채움은 랩 소스 `include/linux/uaccess.h`에서 확인할 수 있다:

```c
	res = raw_copy_from_user(to, from, n);
	instrument_copy_from_user_after(to, from, n, res);
	if (likely(!res))
		return 0;
fail:
	memset(to + (n - res), 0, res);
	return res;
```

이 0-채움 덕분에 "실패했는데 커널 스택 버퍼의 쓰레기가 남아 그대로 쓰이는" 정보 누출이 막힌다. arm64는 `INLINE_COPY_FROM_USER`/`INLINE_COPY_TO_USER`를 정의해서 이 코드가 드라이버 안에 인라인되고, 실제 복사는 `__arch_copy_from_user`/`__arch_copy_to_user`(어셈블리, `arch/arm64/lib/`)를 부른다.

### 3.3 "못 복사한 바이트 수" 규약과 부분 복사 처리 패턴

`copy_*_user`가 **성공 시 0, 실패 시 양수**를 돌려준다는 점이 처음엔 헷갈린다. 커널의 다른 함수는 실패 시 음수 errno를 주는데 이것만 다르다. 패턴은 두 가지뿐이다.

```c
/* 패턴 A — 구조체·헤더처럼 "전부 아니면 실패" (ioctl, 고정 크기) */
if (copy_from_user(&cfg, argp, sizeof(cfg)))
	return -EFAULT;

/* 패턴 B — 스트림 데이터 (read/write): 일부라도 옮겼으면 그만큼 성공 */
left = copy_to_user(ubuf, kbuf, n);
if (left == n)
	return -EFAULT;
*ppos += n - left;
return n - left;
```

절대 하지 말 것: `return copy_to_user(...);` — 성공이면 0을 반환하니 read가 "EOF"가 되고, 실패면 양수를 반환하니 유저는 "그만큼 읽었다"고 착각한다. `ret = copy_from_user(...); if (ret < 0)` — 반환형이 `unsigned long`이라 절대 음수가 아니다.

### 3.4 예제 8 — 정상 경로: EAGAIN, 짧은 버퍼, EOF, ENOSPC

무엇을 확인하나: 드라이버의 read/write 반환값 규칙(§2.2)이 유저에게 어떻게 보이는지. 공통 헤더는 결과를 `-> 값` 또는 `-> -1 errno=N NAME`으로 찍는다.

```c
/* e6_common.h — 테스트 프로그램 공용: 결과 한 줄 출력 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "e6_ioctl.h"
#define DEV "/dev/e6_dev"

static inline void show(const char *what, long r)
{
	if (r < 0)
		printf("%-40s -> -1  errno=%-3d %s\n", what, errno, strerrorname_np(errno));
	else
		printf("%-40s -> %ld\n", what, r);
}

static inline int open_dev(int flags)
{
	int fd = open(DEV, flags);
	if (fd < 0) { perror("open " DEV); exit(1); }
	return fd;
}
```

```c
/* e6_t1_basic: 정상 경로 — EAGAIN, 짧은 버퍼, EOF, ENOSPC */
#include "e6_common.h"

int main(void)
{
	char buf[64];
	long r;
	int fd = open_dev(O_RDWR | O_NONBLOCK);

	r = read(fd, buf, sizeof(buf));
	show("read() on empty device, O_NONBLOCK", r);
	r = write(fd, "imu:ax=12,ay=-3,az=981", 22);
	show("write(22 bytes)", r);
	lseek(fd, 0, SEEK_SET);
	r = read(fd, buf, 8);
	show("read(buf, 8)   - short user buffer", r);
	printf("    got \"%.*s\"\n", (int)r, buf);
	r = read(fd, buf, sizeof(buf));
	show("read(buf, 64)  - the rest", r);
	printf("    got \"%.*s\"\n", (int)r, buf);
	r = read(fd, buf, sizeof(buf));
	show("read(buf, 64)  - at end of data (EOF)", r);
	r = read(fd, buf, 0);
	show("read(buf, 0)", r);
	r = pread(fd, buf, 5, 17);
	show("pread(buf, 5, offset 17)", r);
	printf("    got \"%.*s\"\n", (int)r, buf);
	lseek(fd, 64 * 1024, SEEK_SET);
	r = write(fd, "x", 1);
	show("write(1) at offset 64 KiB (device full)", r);
	close(fd);
	return 0;
}
```

```sh
# VM: 정적 빌드 (게스트 initramfs에는 glibc가 없다)
gcc -std=gnu11 -Wall -Wextra -O2 -static -I/Users/donh/.elinux_scratch/E6/mod \
    -I/Users/donh/.elinux_scratch/E6/user /Users/donh/.elinux_scratch/E6/user/e6_t1_basic.c -o e6_t1_basic
# 게스트: insmod /e6_dev.ko verbose=1 && ./e6_t1_basic
```

```text
read() on empty device, O_NONBLOCK       -> -1  errno=11  EAGAIN
write(22 bytes)                          -> 22
read(buf, 8)   - short user buffer       -> 8
    got "imu:ax=1"
read(buf, 64)  - the rest                -> 14
    got "2,ay=-3,az=981"
read(buf, 64)  - at end of data (EOF)    -> 0
read(buf, 0)                             -> 0
pread(buf, 5, offset 17)                 -> 5
    got "z=981"
write(1) at offset 64 KiB (device full)  -> -1  errno=28  ENOSPC
```

출력에서 볼 것: 짧은 버퍼(8)로 읽으면 8만 주고 `*ppos`가 8로 전진해서 다음 read가 나머지 14를 준다. 끝에서는 0(EOF) — 유저의 `while ((r = read(...)) > 0)` 루프가 멈추는 신호다. `pread`는 `f_pos`를 쓰지 않고 offset 17부터 읽었다(`ksys_pread64` → 같은 `vfs_read`).

### 3.5 예제 9 — 나쁜 포인터는 EFAULT, 페이지 경계에서는 부분 복사

무엇을 확인하나: (1) NULL·작은 주소·커널 주소가 각각 어디서 `-EFAULT`가 되는지, (2) 버퍼 중간에 매핑이 끊기면 몇 바이트가 옮겨지는지, (3) 처음 만지는 페이지에 대한 page fault는 에러가 아니라 정상 처리되는지, (4) `get_user`/`put_user`의 실패.

```c
/* e6_t2_fault: 나쁜 포인터 → EFAULT, 페이지 경계 → 부분 복사, 첫 접근 page fault */
#include "e6_common.h"
#include <sys/mman.h>
#include <sys/resource.h>

static long minflt(void)
{
	struct rusage ru;
	getrusage(RUSAGE_SELF, &ru);
	return ru.ru_minflt;
}

/* volatile 로 읽어야 컴파일러(fortify)가 상수 NULL 을 보고 경고하지 않는다 */
static volatile unsigned long bad[] = { 0, 0x10, 0xffff800080000000UL };

int main(void)
{
	long r, pg = sysconf(_SC_PAGESIZE);
	int fd = open_dev(O_RDWR);
	char *m = mmap(NULL, 2 * pg, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (m == MAP_FAILED) { perror("mmap"); return 1; }

	r = write(fd, (void *)bad[0], 10);
	show("write(fd, NULL, 10)", r);
	r = read(fd, (void *)bad[1], 10);
	show("read(fd, 0x10, 10)", r);
	r = read(fd, (void *)bad[2], 10);
	show("read(fd, kernel address, 10)", r);

	memset(m, 'A', pg);				/* 첫 페이지만 채운다 */
	show("pwrite(4096) - valid page", pwrite(fd, m, pg, 0));
	if (mprotect(m + pg, pg, PROT_NONE)) { perror("mprotect"); return 1; }
	r = pwrite(fd, m + pg - 100, 300, 0);		/* 100 바이트 뒤에 PROT_NONE */
	show("pwrite(300) - last 200 bytes unmapped", r);
	r = pread(fd, m + pg - 100, 300, 0);
	show("pread(300)  - last 200 bytes unmapped", r);

	char *fresh = mmap(NULL, pg, PROT_READ | PROT_WRITE,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	long before = minflt();
	r = pread(fd, fresh, 64, 0);			/* 아직 한 번도 안 만진 페이지 */
	long after = minflt();
	show("pread(64) into never-touched page", r);
	printf("    minor faults during that read: %ld, data[0]='%c'\n", after - before, fresh[0]);

	r = ioctl(fd, E6_IOC_GET_LEN, (void *)8);	/* put_user to bad address */
	show("ioctl(GET_LEN, bad ptr)   put_user", r);
	r = ioctl(fd, E6_IOC_SET_THRESH, (void *)8);	/* get_user from bad address */
	show("ioctl(SET_THRESH, bad ptr) get_user", r);
	__u32 len = 0;
	r = ioctl(fd, E6_IOC_GET_LEN, &len);
	show("ioctl(GET_LEN, &len)", r);
	printf("    len=%u\n", len);
	return 0;
}
```

(처음엔 `write(fd, NULL, 10)`이라고 직접 썼는데 gcc 15 + glibc fortify가 `-Wnonnull`·`-Wstringop-overflow` 경고를 냈다. 컴파일러가 이런 실수를 컴파일 때 잡아 준다는 뜻이기도 하다. 실험을 위해 `volatile` 배열로 우회했다.)

```text
write(fd, NULL, 10)                      -> -1  errno=14  EFAULT
read(fd, 0x10, 10)                       -> -1  errno=14  EFAULT
read(fd, kernel address, 10)             -> -1  errno=14  EFAULT
pwrite(4096) - valid page                -> 4096
pwrite(300) - last 200 bytes unmapped    -> 52
pread(300)  - last 200 bytes unmapped    -> 96
pread(64) into never-touched page        -> 64
    minor faults during that read: 1, data[0]='A'
ioctl(GET_LEN, bad ptr)   put_user       -> -1  errno=14  EFAULT
ioctl(SET_THRESH, bad ptr) get_user      -> -1  errno=14  EFAULT
ioctl(GET_LEN, &len)                     -> 0
    len=4096
```

같은 실행의 dmesg (`verbose=1`):

```text
[    2.204960] e6: write: asked 10, copy_from_user left 10
[    2.208047] e6: read: asked 10, copy_to_user left 10
[    2.212157] e6: write: asked 300, copy_from_user left 248
[    2.212824] e6: read: asked 300, copy_to_user left 204
```

출력에서 볼 것 — 이 예제가 이 노트의 핵심이다:

- **커널 주소** read는 EFAULT인데 dmesg에 `read: asked 10` 줄이 **두 개뿐**이다. 세 번째(커널 주소)는 `vfs_read()`의 `access_ok()`에서 걸러져 드라이버까지 오지 않았다. NULL과 0x10은 유저 영역 주소라 `access_ok`를 통과했고, 드라이버의 `copy_*_user`가 fault → 예외 테이블 → `left == n` → 드라이버가 `-EFAULT`를 돌려줬다. **어느 경우에도 Oops는 없다.**
- 페이지 경계 테스트: 유효한 바이트는 100개였는데 write는 **52**, read는 **96**바이트만 옮겼다. `copy_*_user`가 약속하는 것은 "앞의 `n - left` 바이트는 확실히 복사됐다"뿐이고, **fault 직전까지 정확히 한 바이트 단위로 채워 준다는 보장은 없다.** arm64 복사 루틴은 16/64바이트 단위로 load/store를 하므로, fault가 난 묶음 전체를 "못 복사함"으로 센다 (§4.4에서 fixup 코드로 확인). 드라이버는 이 숫자를 그대로 믿고 반환하면 된다 — 유저는 짧은 write/read를 보고 남은 부분을 다시 시도한다.
- 한 번도 안 만진 페이지로 read: 성공(64)했고 그동안 minor fault 1회가 일어났다. 커널의 `sttr`가 유저 페이지에 쓰다가 page fault를 냈고, 이 주소는 유효한 VMA 안이라 `handle_mm_fault()`가 0 페이지를 할당한 뒤 **같은 명령을 다시 실행**했다. 에러가 아니다.
- `get_user`/`put_user`는 반환값 자체가 0/`-EFAULT`라 그대로 돌려주면 된다.

### 3.6 `simple_read_from_buffer` — 패턴 B를 이미 구현해 둔 헬퍼

고정 크기 커널 버퍼(상태 문자열, 캘리브레이션 테이블)를 보여 주는 read라면 직접 짤 필요가 없다. 랩 소스 `fs/libfs.c`:

```c
ssize_t simple_read_from_buffer(void __user *to, size_t count, loff_t *ppos,
				const void *from, size_t available)
{
	loff_t pos = *ppos;
	size_t ret;

	if (pos < 0)
		return -EINVAL;
	if (pos >= available || !count)
		return 0;
	if (count > available - pos)
		count = available - pos;
	ret = copy_to_user(to, from + pos, count);
	if (ret == count)
		return -EFAULT;
	count -= ret;
	*ppos = pos + count;
	return count;
}
```

`e6_read`의 뒷부분과 한 줄씩 대응한다 — 음수 위치 검사, `!count` 처리, EOF, 부분 복사, ppos 갱신. 차이는 blocking·락이 없다는 것뿐이다. 그래서 `e6_read`의 락 안쪽은 `return simple_read_from_buffer(ubuf, count, ppos, e6_buf, e6_len);` 한 줄로 바꿔도 된다(락을 쥔 채 부르는 것은 위와 같은 이유로 괜찮다). 쓰기 쪽 짝은 `simple_write_to_buffer()`다. 메모리 내용을 그대로 보여 주기만 하는 파일이라면 `debugfs_create_blob()`이나 sysfs `bin_attribute`가 더 짧다 (G8).

`memdup_user`도 소스를 보면 단순하다 (`mm/util.c`):

```c
void *memdup_user(const void __user *src, size_t len)
{
	void *p;

	p = kmem_buckets_alloc_track_caller(user_buckets, len, GFP_USER | __GFP_NOWARN);
	if (!p)
		return ERR_PTR(-ENOMEM);

	if (copy_from_user(p, src, len)) {
		kfree(p);
		return ERR_PTR(-EFAULT);
	}

	return p;
}
```

주의할 점: `len`을 **유저가 정한다**. 호출 전에 상한을 검사하지 않으면 유저가 거대한 할당을 유도할 수 있다 (`__GFP_NOWARN`이라 경고도 안 남는다). `e6_load()`(§6.3)가 `b.len > E6_BUF_SZ`를 먼저 보는 이유다. 결과는 `ERR_PTR`이므로 `IS_ERR()`/`PTR_ERR()`로 받는다 (E5).

---

## 4. 예외 테이블과 PAN — 나쁜 포인터가 Oops가 아니라 EFAULT가 되는 원리

### 4.1 직관 — "이 명령에서 fault 나면 여기로 가라" 목록

커널 코드 대부분은 fault가 나면 안 된다. 커널 주소에서 data abort = 커널 버그 = Oops. 그런데 유저 메모리를 만지는 **특정 명령 몇 개**는 fault가 날 수 있다는 걸 미리 안다. 그래서 링커가 **(fault 날 수 있는 명령 주소, 대신 점프할 주소, 처리 방식)** 쌍을 `__ex_table` 섹션에 모아 둔다. 커널 모드 abort가 나면 page fault 핸들러가 먼저 정상 처리(페이지 할당)를 시도하고, 그게 안 되면 **PC로 이 테이블을 검색**한다. 있으면 레지스터에 `-EFAULT`를 넣고 PC를 fixup 주소로 바꿔서 예외에서 복귀. 없으면 Oops.

펌웨어로 치면 "abort 핸들러가 faulting PC를 보고, 그 PC가 `safe_probe_read()` 함수 안이면 에러 코드를 반환시키고 계속 진행" 하는 트릭을, 컴파일러·링커가 자동으로 표를 만들어 주는 형태로 일반화한 것이다.

### 4.2 예제 10 — 우리 모듈의 `__ex_table`

무엇을 확인하나: `get_user`/`put_user`가 인라인되면서 모듈 안에 예외 테이블 엔트리가 생기는지, 그 엔트리가 어떤 명령을 가리키는지.

```sh
cd /var/tmp/E6/mod
readelf -SW e6_dev.ko | grep -E "ex_table"
readelf -rW e6_dev.ko | sed -n "/__ex_table/,/^$/p"
objdump -s -j __ex_table e6_dev.ko | tail -2
objdump -d --no-show-raw-insn -j .text e6_dev.ko > mod.dis
grep -nE "^ +(89c|8a0|8a4|8a8|8ac|8d4|8d8|8dc|8e0|73c|740|744):" mod.dis
```

```text
  [ 3] __ex_table        PROGBITS        0000000000000000 000058 000018 00   A  0   0  4
  [ 4] .rela__ex_table   RELA            0000000000000000 00ac08 000060 18   I 50   3  8
Relocation section '.rela__ex_table' at offset 0xac08 contains 4 entries:
    Offset             Info             Type               Symbol's Value  Symbol's Name + Addend
0000000000000000  0000000800000105 R_AARCH64_PREL32       0000000000000000 .text + 8a8
0000000000000004  0000000800000105 R_AARCH64_PREL32       0000000000000000 .text + 740
000000000000000c  0000000800000105 R_AARCH64_PREL32       0000000000000000 .text + 8dc
0000000000000010  0000000800000105 R_AARCH64_PREL32       0000000000000000 .text + 740

 0000 00000000 00000000 0200ff03 00000000  ................
 0010 00000000 0200ff03                    ........        
477: 73c:	cbz	x0, 5c0 <e6_ioctl+0xd8>
478: 740:	mov	x3, #0xfffffffffffffff2    	// #-14
479: 744:	b	5c0 <e6_ioctl+0xd8>
565: 89c:	b.hi	5c0 <e6_ioctl+0xd8>  // b.pmore
566: 8a0:	and	x1, x1, #0xff7fffffffffffff
567: 8a4:	ldr	x0, [x6, #24]
568: 8a8:	sttr	w0, [x1]
569: 8ac:	mov	x3, #0x0                   	// #0
579: 8d4:	b.hi	5c0 <e6_ioctl+0xd8>  // b.pmore
580: 8d8:	and	x1, x1, #0xff7fffffffffffff
581: 8dc:	ldtr	w1, [x1]
582: 8e0:	cmp	w1, #0x10, lsl #12
```

출력에서 볼 것:

- `__ex_table`은 24바이트 = **엔트리 2개**(12바이트씩). arm64의 엔트리 구조체(`arch/arm64/include/asm/extable.h`)는 `{ int insn, fixup; short type, data; }`이다. `insn`과 `fixup`은 **자기 위치 기준 상대 오프셋**(`R_AARCH64_PREL32`)이라 모듈이 어디에 로드되든 재배치가 쉽다.
- 엔트리 1: 명령 `.text+0x8a8` = `sttr w0, [x1]` → `put_user` (GET_LEN). 엔트리 2: `.text+0x8dc` = `ldtr w1, [x1]` → `get_user` (SET_THRESH). 둘 다 fixup은 `.text+0x740` = `mov x3, #-14` = C 코드의 `return -EFAULT` 경로.
- 원시 바이트 `0200ff03` = `type = 0x0002`(`EX_TYPE_UACCESS_ERR_ZERO`), `data = 0x03ff`. data의 비트 0–4(err 레지스터)와 5–9(zero 레지스터)가 둘 다 31(= `xzr`)이다. 컴파일러가 `asm goto`로 "fault 나면 C 레이블로 점프" 형태를 만들었기 때문에, 핸들러가 레지스터에 값을 써 줄 필요 없이 PC만 바꾸면 된다.
- `ldtr`/`sttr` 바로 앞의 `and x1, x1, #0xff7fffffffffffff`는 `uaccess_mask_ptr()` — 주소의 **bit 55를 지운다**. arm64에서 bit 55가 1이면 TTBR1(커널) 영역이므로, 투기 실행으로라도 커널 주소를 유저 접근 명령에 쓰지 못하게 하는 방어다.
- `copy_to_user`/`copy_from_user`는 이 모듈의 테이블에 없다. 실제 복사 명령은 vmlinux의 `__arch_copy_*_user` 안에 있고, 엔트리도 vmlinux의 `__ex_table`에 있다 (다음 예제).

### 4.3 예제 11 — vmlinux의 `__ex_table`은 몇 개, 누가 쓰나

무엇을 확인하나: 커널 전체 예외 테이블을 파싱해서 엔트리 수, 종류, 어떤 함수에 몰려 있는지 센다.

```sh
cd /var/tmp/E6
objcopy -O binary --only-section=__ex_table /var/lab/out/linux-build/vmlinux ext.bin
readelf -SW /var/lab/out/linux-build/vmlinux | grep -E " __ex_table"
python3 -I ext.py $(readelf -SW /var/lab/out/linux-build/vmlinux | awk '/ __ex_table/{print $4}')
```

```python
import struct, bisect, sys, collections
base = int(sys.argv[1], 16)
data = open("/var/tmp/E6/ext.bin", "rb").read()
syms = []
for line in open("/var/lab/out/linux-build/System.map"):
    a, t, n = line.split()[:3]
    if t in "tTwW": syms.append((int(a, 16), n))
syms.sort(); addrs = [s[0] for s in syms]
types = collections.Counter(); funcs = collections.Counter()
for i in range(0, len(data), 12):
    insn, fixup, typ, dat = struct.unpack("<iihh", data[i:i+12])
    pc = base + i + insn
    types[typ] += 1
    funcs[syms[bisect.bisect_right(addrs, pc) - 1][1]] += 1
print("entries:", len(data) // 12, " by type:", dict(sorted(types.items())))
for f in ["__arch_copy_from_user", "__arch_copy_to_user", "strncpy_from_user", "__arch_clear_user"]:
    print("  %-24s %3d" % (f, funcs[f]))
print("  top:", funcs.most_common(4))
```

```text
  [10] __ex_table        PROGBITS        ffff800081fda618 1fea618 002d0c 00   A  0   0  4
entries: 961  by type: {2: 925, 3: 21, 4: 6, 5: 9}
  __arch_copy_from_user     42
  __arch_copy_to_user       42
  strncpy_from_user          2
  __arch_clear_user          9
  top: [('__arch_copy_from_user', 42), ('__arch_copy_to_user', 42), ('setup_rt_frame', 39), ('compat_setup_sigframe', 30)]
```

출력에서 볼 것: 커널 전체에 엔트리 961개. 대부분(925)이 type 2 `EX_TYPE_UACCESS_ERR_ZERO`다. type 3은 커널 주소 쪽의 안전한 접근(`KACCESS_ERR_ZERO`, 예: `copy_from_kernel_nofault`), 4는 `UACCESS_CPY`(ARMv8.8 FEAT_MOPS의 `cpy*` 명령용), 5는 `LOAD_UNALIGNED_ZEROPAD`. 복사 루틴 하나에 42개씩인 이유는 루프 안의 `ldtr`/`sttr` **명령 하나하나**가 엔트리를 갖기 때문이다. 3·4위가 시그널 프레임을 유저 스택에 쓰는 `setup_rt_frame`인 것도 재미있다 — 커널이 유저 메모리에 쓰는 곳은 전부 여기에 나타난다. 엔트리는 `insn` 기준으로 정렬되어 있어 `search_exception_tables()`가 이진 탐색한다.

### 4.4 fault가 fixup으로 바뀌는 경로

랩 소스에서 따라가면:

1. 커널 모드 data abort → `el1h_64_sync_handler` → `do_mem_abort()` → `do_page_fault()` (`arch/arm64/mm/fault.c`).
2. 주소가 유저 영역(`is_ttbr0_addr`)이고 **permission fault**인데 PC가 uaccess 명령이 아니면(`!insn_may_access_user(regs->pc, esr)`) → `die_kernel_fault("access to user memory outside uaccess routines", ...)`. PAN 위반이 여기서 잡힌다 (예제 12).
3. 그렇지 않으면 VMA를 찾아 `handle_mm_fault()`. 성공하면 같은 명령 재실행 (예제 9의 minor fault).
4. VMA가 없거나 권한이 없으면 → `__do_kernel_fault()` → `fixup_exception(regs, esr)`:

```c
bool fixup_exception(struct pt_regs *regs, unsigned long esr)
{
	const struct exception_table_entry *ex;

	ex = search_exception_tables(instruction_pointer(regs));
	if (!ex)
		return false;

	switch (ex->type) {
	case EX_TYPE_BPF:
		return ex_handler_bpf(ex, regs);
	case EX_TYPE_UACCESS_ERR_ZERO:
	case EX_TYPE_KACCESS_ERR_ZERO:
		return ex_handler_uaccess_err_zero(ex, regs);
	case EX_TYPE_UACCESS_CPY:
		return ex_handler_uaccess_cpy(ex, regs, esr);
	case EX_TYPE_LOAD_UNALIGNED_ZEROPAD:
		return ex_handler_load_unaligned_zeropad(ex, regs);
	}

	BUG();
}
```

`ex_handler_uaccess_err_zero()`는 data 필드가 가리키는 레지스터에 `-EFAULT`와 0을 쓰고 `regs->pc = fixup`으로 바꾼다. 예외에서 돌아가면 CPU는 fixup 주소부터 실행한다. 복사 루틴의 fixup은 `arch/arm64/lib/copy_from_user.S` 끝에 있다:

```text
	// Exception fixups
9996:	b.cs	9997f
	// Registers are in Option A format
	add	dst, dst, count
9997:	cmp	dst, dstin
	b.ne	9998f
	// Before being absolutely sure we couldn't copy anything, try harder
USER(9998f, ldtrb tmp1w, [srcin])
	strb	tmp1w, [dst], #1
9998:	sub	x0, end, dst			// bytes not copied
	ret
```

반환값은 `end - dst` = "목적지 끝 - 지금까지 쓴 위치" = **못 복사한 바이트 수**다. `dst`는 마지막으로 **성공한 묶음**까지만 전진해 있으므로, 예제 9처럼 유효한 100바이트 중 52바이트만 성공으로 셀 수 있다. 하나도 못 옮겼을 때만 "try harder"로 1바이트를 더 시도한다 — 0 진행으로 인한 무한 재시도를 막기 위해서다.

```svg
<svg viewBox="0 0 680 470" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="14" width="300" height="34" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="170" y="36" text-anchor="middle" font-size="12">copy_from_user(kbuf, ubuf, 300)</text>
  <rect x="20" y="66" width="300" height="34" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="170" y="88" text-anchor="middle" font-size="12">access_ok 통과 → __arch_copy_from_user</text>
  <rect x="20" y="118" width="300" height="34" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="170" y="140" text-anchor="middle" font-size="12">ldtr 루프 … PROT_NONE 페이지에서 abort</text>
  <rect x="20" y="170" width="300" height="34" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="170" y="192" text-anchor="middle" font-size="12">do_mem_abort → do_page_fault (EL1)</text>
  <line x1="170" y1="48" x2="170" y2="64" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="170" y1="100" x2="170" y2="116" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="170" y1="152" x2="170" y2="168" stroke="currentColor" marker-end="url(#ah3)"/>
  <rect x="20" y="236" width="200" height="50" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="120" y="256" text-anchor="middle" font-size="12">VMA 있고 권한 OK</text>
  <text x="120" y="274" text-anchor="middle" font-size="12">handle_mm_fault</text>
  <rect x="240" y="236" width="200" height="50" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="256" text-anchor="middle" font-size="12">VMA 없음 / 권한 없음</text>
  <text x="340" y="274" text-anchor="middle" font-size="12">__do_kernel_fault</text>
  <rect x="460" y="236" width="200" height="50" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="560" y="256" text-anchor="middle" font-size="12">PAN 위반 (일반 ldr)</text>
  <text x="560" y="274" text-anchor="middle" font-size="12">uaccess 명령 아님</text>
  <line x1="130" y1="204" x2="120" y2="234" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="210" y1="204" x2="330" y2="234" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="300" y1="204" x2="550" y2="234" stroke="currentColor" marker-end="url(#ah3)"/>
  <rect x="20" y="314" width="200" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="120" y="334" text-anchor="middle" font-size="12">같은 ldtr 재실행</text>
  <text x="120" y="352" text-anchor="middle" font-size="12">복사 계속 (minor fault)</text>
  <rect x="230" y="300" width="220" height="78" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="320" text-anchor="middle" font-size="12">fixup_exception(regs)</text>
  <text x="340" y="338" text-anchor="middle" font-size="12">search_exception_tables(pc)</text>
  <text x="340" y="356" text-anchor="middle" font-size="12">찾음 → regs->pc = fixup</text>
  <text x="340" y="372" text-anchor="middle" font-size="12">못 찾음 → Oops</text>
  <rect x="460" y="314" width="200" height="50" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="560" y="334" text-anchor="middle" font-size="12">die_kernel_fault</text>
  <text x="560" y="352" text-anchor="middle" font-size="12">Oops, 프로세스 kill</text>
  <line x1="120" y1="286" x2="120" y2="312" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="340" y1="286" x2="340" y2="298" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="560" y1="286" x2="560" y2="312" stroke="currentColor" marker-end="url(#ah3)"/>
  <rect x="240" y="400" width="420" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="450" y="420" text-anchor="middle" font-size="12">fixup 9998: x0 = end - dst (못 복사한 바이트) → ret</text>
  <text x="450" y="438" text-anchor="middle" font-size="12">e6_write: left = 248 → return 52 (짧은 write)</text>
  <line x1="340" y1="378" x2="340" y2="398" stroke="currentColor" marker-end="url(#ah3)"/>
</svg>
```
그림 3 — `copy_from_user` 도중 유저 페이지에서 fault가 났을 때의 세 갈래. 왼쪽은 정상 page fault(예제 9의 첫 접근), 가운데는 예외 테이블로 에러 반환(예제 9의 PROT_NONE), 오른쪽은 uaccess가 아닌 명령으로 유저 메모리를 만진 경우(예제 12).

### 4.5 예제 12 — 직접 역참조하면? PAN이 없는 CPU와 있는 CPU

무엇을 확인하나: 드라이버가 `copy_from_user` 없이 `*(u32 *)argp`로 유저 포인터를 읽으면 어떻게 되는지를, PAN이 없는 Cortex-A57(ARMv8.0)과 PAN이 있는 QEMU `-cpu max`에서 비교한다. 이 코드(`E6_IOC_BAD_DEREF`, §6.3)는 일부러 틀린 코드이고 `allow_bad=1`일 때만 동작한다.

```c
/* e6_t6_bad: 커널이 유저 포인터를 직접 역참조하면? (allow_bad=1 일 때만) */
#include "e6_common.h"

int main(int argc, char **argv)
{
	__u32 v = 0x12345678;
	int fd = open_dev(O_RDWR);
	(void)argv;
	void *p = (argc > 1) ? (void *)8 : (void *)&v;	/* 인자 있으면 나쁜 주소 */

	printf("ioctl(BAD_DEREF, %p)\n", p);
	fflush(stdout);
	show("ioctl(BAD_DEREF)", ioctl(fd, E6_IOC_BAD_DEREF, p));
	return 0;
}
```

게스트 스크립트:

```sh
dmesg | grep -iE "Privileged Access Never|PAN"
insmod /e6_dev.ko allow_bad=1
echo "== good pointer"; ./e6_t6_bad
echo "== bad pointer";  ./e6_t6_bad x
echo "shell still alive, exit status of last: $?"
dmesg | grep -A12 -E "e6: BAD|Unable to handle" | grep -vE "^\[ *[0-9.]+\] (x[0-9]|sp |pc |lr )"
```

**(a) `-cpu cortex-a57` (PAN 없음)** — 콘솔 출력 앞부분과 dmesg에서 골라낸 줄:

```text
== good pointer
ioctl(BAD_DEREF, 0xffffe2e8a474)
ioctl(BAD_DEREF)                         -> 0
== bad pointer
ioctl(BAD_DEREF, 0x8)
[    3.142172] Unable to handle kernel NULL pointer dereference at virtual address 0000000000000008
[    3.142324]   FSC = 0x06: level 2 translation fault
[    3.145357] Internal error: Oops: 0000000096000006 [#1]  SMP
[    3.148565] pstate: 60000005 (nZCv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
[    3.148719] pc : e6_ioctl+0x26c/0x4f8 [e6_dev]
Segmentation fault
shell still alive, exit status of last: 139
[    3.124776] e6: BAD_DEREF read 0x12345678 directly from 0000ffffe2e8a474
```

**(b) `-cpu max,pauth-impdef=on` (PAN 있음)** — 같은 프로그램, 같은 모듈:

```text
[    0.116253] CPU features: detected: Enhanced Privileged Access Never
[    0.116501] CPU features: detected: Privileged Access Never
== good pointer
ioctl(BAD_DEREF, 0xffffe9b6c0b4)
[    4.277025] Unable to handle kernel access to user memory outside uaccess routines at virtual address 0000ffffe9b6c0b4
[    4.277097] Mem abort info:
[    4.277113]   ESR = 0x000000009600000f
[    4.277142]   EC = 0x25: DABT (current EL), IL = 32 bits
[    4.277215]   FSC = 0x0f: level 3 permission fault
[    4.278430] Internal error: Oops: 000000009600000f [#1]  SMP
[    4.285536] Modules linked in: e6_dev(O)
[    4.290310] pstate: 61402005 (nZCv daif +PAN -UAO -TCO +DIT -SSBS BTYPE=--)
[    4.290580] pc : e6_ioctl+0x26c/0x4f8 [e6_dev]
[    4.291213] lr : __arm64_sys_ioctl+0xa4/0xf4
...
[    4.297271] Call trace:
[    4.297537]  e6_ioctl+0x26c/0x4f8 [e6_dev] (P)
[    4.297769]  __arm64_sys_ioctl+0xa4/0xf4
...
Segmentation fault
== bad pointer
ioctl(BAD_DEREF, 0x8)
[    4.326358] Unable to handle kernel NULL pointer dereference at virtual address 0000000000000008
...
Segmentation fault
shell still alive, exit status of last: 139
```

(레지스터 덤프 x0~x29와 반복되는 abort 정보 줄은 `...`로 생략했다.)

출력에서 볼 것 — 면접에서 그대로 쓸 수 있는 관찰:

- **PAN 없는 CPU에서는 틀린 코드가 "잘 돈다".** (a)의 good pointer는 `0x12345678`을 그대로 읽어 왔다. 커널과 유저가 같은 주소 공간에 보이니 일반 `ldr`로도 읽힌다. 개발 보드에서 테스트를 통과한 버그가 새 SoC(ARMv8.1+)나 KPTI·SW PAN을 켠 커널에서 갑자기 Oops가 되는 이유다. 버그는 원래 있었다.
- **PAN 있는 CPU**에서는 유효한 유저 포인터여도 즉시 Oops: `level 3 permission fault`, 메시지 `access to user memory outside uaccess routines`(§4.4의 2번 경로), `pstate`에 `+PAN`.
- **나쁜 포인터(0x8)는 두 CPU 모두 Oops.** 직접 `ldr`에는 예외 테이블 엔트리가 없으니 fixup을 못 찾는다. 같은 0x8을 `get_user`로 읽은 예제 9는 `-EFAULT`였다 — 차이는 명령 하나와 테이블 엔트리 하나다.
- Oops 후에도 셸은 살아 있다(`exit status 139` = SIGSEGV). process context Oops는 그 프로세스만 죽이고 커널은 계속 간다(`panic_on_oops=0`일 때). 하지만 `Tainted: [D]=DIE`가 붙고 락을 쥔 채 죽었다면 시스템이 곧 멈출 수 있다 — 제품이라면 `panic_on_oops=1`로 재부팅시키는 게 보통이다.
- 6.18 arm64의 정상 uaccess는 `ldtr`/`sttr`(EL0 권한으로 접근하는 명령)을 쓰므로 PAN을 끄지 않고도 유저 메모리를 읽는다. PAN을 잠깐 끄는 `uaccess_enable_privileged()`는 랩 소스에서 futex(배타적 load/store는 unprivileged 버전이 없다)와 deprecated 명령 에뮬레이션에서만 쓰인다. 랩 커널은 `CONFIG_ARM64_PAN=y`, `CONFIG_ARM64_SW_TTBR0_PAN`은 꺼져 있다 — 그래서 하드웨어 PAN이 없는 A57에서는 보호가 없었다.

---

## 5. 에러 코드 규약 — 커널 `-errno`, libc `-1` + `errno`

### 5.1 층마다 같은 숫자가 모양만 바뀐다

A1 §3.4에서 유저 쪽을 봤다: 커널이 `x0 = -2`를 주면 libc가 `errno = 2`, 반환 `-1`. 커널 안에서는 **함수 반환값 자체가 음수 errno**다. 드라이버가 `return -EFAULT;` 하면 그 `-14`가 VFS를 그대로 통과해 `regs->regs[0]`에 들어간다. libc는 `x0`가 -4095~-1 범위(`MAX_ERRNO` = 4095, `include/linux/err.h`)면 에러로 본다. 같은 범위 규칙이 커널 포인터에도 쓰인다 — `ERR_PTR(-ENOMEM)`은 주소 공간 맨 끝 4095바이트를 "에러 포인터"로 예약한 것이다 (E5).

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="20" width="120" height="70" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="70" y="42" text-anchor="middle" font-size="13" font-weight="bold">드라이버</text>
  <text x="70" y="62" text-anchor="middle" font-size="12">return -EFAULT</text>
  <text x="70" y="80" text-anchor="middle" font-size="12">(-14)</text>
  <rect x="145" y="20" width="120" height="70" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="205" y="42" text-anchor="middle" font-size="13" font-weight="bold">VFS</text>
  <text x="205" y="62" text-anchor="middle" font-size="12">그대로 전달</text>
  <text x="205" y="80" text-anchor="middle" font-size="12">(-14)</text>
  <rect x="280" y="20" width="120" height="70" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="42" text-anchor="middle" font-size="12" font-weight="bold">invoke_syscall</text>
  <text x="340" y="62" text-anchor="middle" font-size="12">regs[0] = -14</text>
  <text x="340" y="80" text-anchor="middle" font-size="12">eret → x0</text>
  <rect x="415" y="20" width="120" height="70" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="475" y="42" text-anchor="middle" font-size="13" font-weight="bold">libc</text>
  <text x="475" y="62" text-anchor="middle" font-size="12">errno = 14</text>
  <text x="475" y="80" text-anchor="middle" font-size="12">return -1</text>
  <rect x="550" y="20" width="120" height="70" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="610" y="42" text-anchor="middle" font-size="13" font-weight="bold">앱</text>
  <text x="610" y="62" text-anchor="middle" font-size="12">perror →</text>
  <text x="610" y="80" text-anchor="middle" font-size="12">Bad address</text>
  <line x1="130" y1="55" x2="143" y2="55" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="265" y1="55" x2="278" y2="55" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="400" y1="55" x2="413" y2="55" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="535" y1="55" x2="548" y2="55" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="340" y="126" text-anchor="middle" font-size="13" font-weight="bold">특수한 경우: -ERESTARTSYS (-512)는 유저에게 절대 안 보인다</text>
  <rect x="10" y="144" width="200" height="54" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="110" y="166" text-anchor="middle" font-size="12">wait_event_interruptible</text>
  <text x="110" y="185" text-anchor="middle" font-size="12">→ return -ERESTARTSYS</text>
  <rect x="240" y="144" width="200" height="54" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="340" y="166" text-anchor="middle" font-size="12">arch_do_signal_or_restart</text>
  <text x="340" y="185" text-anchor="middle" font-size="12">핸들러의 SA_RESTART?</text>
  <line x1="210" y1="171" x2="238" y2="171" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="470" y="136" width="200" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="570" y="161" text-anchor="middle" font-size="12">예: pc -= 4, x0 = orig_x0</text>
  <rect x="470" y="186" width="200" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="570" y="211" text-anchor="middle" font-size="12">아니오: x0 = -EINTR</text>
  <line x1="440" y1="162" x2="468" y2="156" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="440" y1="182" x2="468" y2="204" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="10" y="246" width="660" height="70" rx="6" fill="#888" fill-opacity="0.10" stroke="#888"/>
  <text x="26" y="268" font-size="12">커널 안: 0 = 성공, 양수 = 크기·개수, 음수 = -errno (포인터는 ERR_PTR)</text>
  <text x="26" y="288" font-size="12">유저 쪽: -1 + errno (errno는 실패했을 때만 의미가 있다)</text>
  <text x="26" y="308" font-size="12">예외: copy_to/from_user는 "못 복사한 바이트 수"(양수)를 돌려준다</text>
</svg>
```
그림 4 — errno가 층을 지나는 모습. 위 줄은 예제 9의 `write(fd, NULL, 10)`이 실제로 거친 경로. 아래는 `-ERESTARTSYS`가 복귀 직전에 "재시작" 또는 `-EINTR`로 바뀌는 분기 (§5.4).

### 5.2 언제 어떤 errno를 쓰나 — 드라이버 기준표

| errno (값) | 의미 | 드라이버에서 쓰는 상황 | 이 노트의 예 |
|---|---|---|---|
| `EINVAL` (22) | 인자가 잘못됨 | 범위 밖 값, 모르는 플래그, 예약 필드 != 0, 지원 안 하는 ABI 버전 | `SET_CFG abi_version=2`, `reserved=7`, `LOAD len=70000` |
| `EFAULT` (14) | 나쁜 주소 | `copy_*_user`/`get_user`/`put_user`가 실패했을 때 **만** | NULL, 0x10, PROT_NONE |
| `ENOMEM` (12) | 메모리 부족 | `kmalloc` 등 할당 실패. `memdup_user`가 대신 돌려주기도 함 | (재현 안 함) |
| `EBUSY` (16) | 자원이 사용 중 | 이미 다른 프로세스가 잡은 장치, 진행 중인 작업 때문에 지금 못 바꿈 | `CLAIM from fd #2` |
| `EAGAIN` (11) | 지금은 없음, 나중에 다시 | `O_NONBLOCK`인데 데이터 없음/버퍼 꽉 참 (`EWOULDBLOCK`과 같은 값) | 빈 장치 non-blocking read |
| `ENOTTY` (25) | 이 장치는 그 ioctl을 모름 | **모르는 ioctl 명령** (EINVAL 아님!) | `_IO('E', 99)`, 옛 구조체 크기, `TCGETS` |
| `EINTR` (4) | 시그널로 중단 | 드라이버가 직접 쓰지 않는다. `-ERESTARTSYS`가 변환된 결과 | t4 no SA_RESTART |
| `ERESTARTSYS` (512) | 커널 내부: 재시작 가능 | `wait_event_interruptible`·`mutex_lock_interruptible`이 시그널로 실패 | `e6_read` 대기 중 SIGALRM |
| `ENOSPC` (28) | 공간 없음 | 장치 버퍼·FIFO 끝 | 64 KiB 지점 write |
| `ENAMETOOLONG` (36) | 문자열이 너무 김 | `strncpy_from_user` 결과가 버퍼를 꽉 채움 | 99자 이름 |
| `ENODEV` (19) / `ENXIO` (6) | 장치 없음 | probe 실패, 장치가 사라짐(hot-unplug) 후 접근 | — |
| `EIO` (5) | 하드웨어 I/O 에러 | I2C NACK, CRC 에러, 버스 타임아웃 후 복구 실패 | — |
| `ETIMEDOUT` (110) | 시간 초과 | 칩이 ready 비트를 안 올림 | — |
| `EPERM` (1) / `EACCES` (13) | 권한 | capability 부족(`EPERM`), 파일 모드(`EACCES`) | `BAD_DEREF`를 `allow_bad=0`일 때 |
| `EOPNOTSUPP` (95) | 기능 미지원 | 요청은 이해했지만 이 하드웨어 리비전에 그 기능이 없음 | — |
| `EBADF` (9) | 나쁜 fd | VFS가 돌려줌 (드라이버 아님) | `ioctl on fd 99` |

판단 요령: **EFAULT는 주소 문제에만** 쓴다(값이 틀리면 EINVAL). **모르는 ioctl은 ENOTTY**(역사적으로 "Not a typewriter" — 터미널이 아닌 파일에 터미널 ioctl을 쓴 경우에서 왔다). EINVAL을 쓰면 유저 라이브러리가 "이 명령은 지원되지만 인자가 틀렸다"고 오해한다.

### 5.3 커널 내부 전용 코드 — 유저에게 새면 안 된다

`include/linux/errno.h`(랩 소스)에는 유저 헤더에 없는 코드가 있다:

```c
#define ERESTARTSYS	512
#define ERESTARTNOINTR	513
#define ERESTARTNOHAND	514	/* restart if no handler.. */
#define ENOIOCTLCMD	515	/* No ioctl command */
#define ERESTART_RESTARTBLOCK 516 /* restart by calling sys_restart_syscall */
#define EPROBE_DEFER	517	/* Driver requests probe retry */
```

- `-ERESTARTSYS`는 시그널 처리 경로가 반드시 다른 것으로 바꾼다 (§5.4). 시그널이 **없는데** 드라이버가 `-ERESTARTSYS`를 돌려주면 유저에게 errno 512가 새어 나간다 — `wait_event_interruptible`의 반환값이 아닌 곳에서 이 코드를 쓰지 말 것.
- `-ENOIOCTLCMD`는 `vfs_ioctl()`이 `-ENOTTY`로 바꿔 준다. 서브시스템 안쪽 ioctl 처리(예: 공통 처리 뒤 드라이버 고유 처리)에서 "내 담당 아님"을 뜻한다.
- `-EPROBE_DEFER`는 probe 전용 (D3, G3).

### 5.4 예제 13 — `ERESTARTSYS`와 시그널: EINTR이 되거나 재시작되거나

무엇을 확인하나: 빈 장치에서 잠든 read에 SIGALRM이 오면, 핸들러를 `SA_RESTART` 없이 설치했을 때와 있을 때 결과가 어떻게 다른지. 드라이버는 두 경우 모두 똑같이 `-ERESTARTSYS`를 돌려준다.

```c
/* e6_t4_signal: 막힌 read 중 시그널 — SA_RESTART 없으면 EINTR, 있으면 재시작 */
#include "e6_common.h"
#include <signal.h>
#include <time.h>
#include <sys/wait.h>

static volatile sig_atomic_t hits;
static void on_alarm(int s) { (void)s; hits++; }

static double now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}

static void trial(int flags, const char *name)
{
	struct sigaction sa = { .sa_handler = on_alarm, .sa_flags = flags };
	char buf[32];
	int fd = open_dev(O_RDWR);

	sigemptyset(&sa.sa_mask);
	sigaction(SIGALRM, &sa, NULL);
	hits = 0;
	if (fork() == 0) {			/* 자식: 2초 뒤 데이터를 써 준다 */
		sleep(2);
		int w = open_dev(O_WRONLY);
		if (write(w, "late", 4) != 4) perror("child write");
		_exit(0);
	}
	alarm(1);				/* 1초 뒤 SIGALRM */
	double t0 = now();
	long r = read(fd, buf, sizeof(buf));
	printf("[%s] after %.2f s, handler ran %d time(s)\n", name, now() - t0, (int)hits);
	show("    read() on empty device", r);
	if (r > 0) printf("    got \"%.*s\"\n", (int)r, buf);
	wait(NULL);
	close(fd);
}

int main(int argc, char **argv)
{
	(void)argv;
	trial(argc > 1 ? SA_RESTART : 0, argc > 1 ? "SA_RESTART" : "no SA_RESTART");
	return 0;
}
```

게스트 스크립트 (각 시도 전에 모듈을 다시 올려 장치를 비운다):

```sh
insmod /e6_dev.ko
echo "== t4 (no SA_RESTART)"; ./e6_t4_signal
rmmod e6_dev; insmod /e6_dev.ko
echo "== t4 (SA_RESTART)";    ./e6_t4_signal restart
dmesg | grep "e6:"
```

```text
== t4 (no SA_RESTART)
[no SA_RESTART] after 1.00 s, handler ran 1 time(s)
    read() on empty device               -> -1  errno=4   EINTR
== t4 (SA_RESTART)
[SA_RESTART] after 2.00 s, handler ran 1 time(s)
    read() on empty device               -> 4
    got "late"
[    2.987501] e6: /dev/e6_dev ready, minor 258
[    4.089046] e6: read: signal while waiting -> -ERESTARTSYS
[    5.207913] e6: /dev/e6_dev ready, minor 258
[    6.234637] e6: read: signal while waiting -> -ERESTARTSYS
```

(세 번째 `-ERESTARTSYS` 줄은 예제 7의 `cat`을 `kill`로 끝낼 때 나온 것이라 생략했다.)

출력에서 볼 것: 드라이버 로그는 두 번 다 같은 `-ERESTARTSYS`다. 그런데 유저는 첫 번째에서 1초 만에 `EINTR`을, 두 번째에서는 핸들러가 한 번 돈 **뒤에도 read가 계속 기다려서** 2초에 자식이 쓴 `"late"`를 받았다. 결정은 `arch/arm64/kernel/signal.c`의 이 부분이 한다:

```c
	if (syscall) {
		continue_addr = regs->pc;
		restart_addr = continue_addr - (compat_thumb_mode(regs) ? 2 : 4);
		retval = regs->regs[0];
		...
		switch (retval) {
		case -ERESTARTNOHAND:
		case -ERESTARTSYS:
		case -ERESTARTNOINTR:
		case -ERESTART_RESTARTBLOCK:
			regs->regs[0] = regs->orig_x0;
			regs->pc = restart_addr;
			break;
		}
	}

	if (get_signal(&ksig)) {
		if (regs->pc == restart_addr &&
		    (retval == -ERESTARTNOHAND ||
		     retval == -ERESTART_RESTARTBLOCK ||
		     (retval == -ERESTARTSYS &&
		      !(ksig.ka.sa.sa_flags & SA_RESTART)))) {
			syscall_set_return_value(current, regs, -EINTR, 0);
			regs->pc = continue_addr;
		}

		handle_signal(&ksig, regs);
		return;
	}
```

- 먼저 "재시작" 쪽으로 준비한다: `pc`를 4바이트 뒤로(= 다시 `svc #0`), `x0`를 `orig_x0`로 복원 (§1.3의 `orig_x0`가 여기서 쓰인다).
- 전달할 시그널의 핸들러에 `SA_RESTART`가 없으면 되돌린다: `x0 = -EINTR`, `pc`는 원래대로.
- 그다음 `handle_signal()`이 유저 스택에 시그널 프레임을 만들고(예제 11에서 본 `setup_rt_frame`의 uaccess), 핸들러가 끝나면 `sigreturn`으로 돌아와 **다시 `svc #0`을 실행**한다 — 그래서 SA_RESTART 쪽 read가 다시 잠들 수 있었다.
- 핸들러 없는 치명적 시그널(SIGKILL, 기본 동작 SIGTERM)이면 프로세스가 그냥 죽으므로 재시작 여부는 의미가 없다.

드라이버 작성자가 지킬 규칙:

1. **잠들기 전에는 아무것도 바꾸지 않는다.** `-ERESTARTSYS`는 "아무 일도 없었던 것처럼 다시 불러도 된다"는 약속이다. 데이터를 일부 소비한 뒤에 시그널이 왔다면 `-ERESTARTSYS`가 아니라 **이미 처리한 바이트 수**를 반환해야 한다.
2. 하드웨어에 명령을 보낸 뒤 완료를 기다리는 중이라 중간에 끊으면 안 되면 `wait_event_killable()`(SIGKILL만 깨움) 또는 `wait_event_timeout()`을 쓰고, 실패 시 `-EINTR`/`-ETIMEDOUT`을 고려한다 (F3).
3. `wait_event_interruptible()`의 반환값을 무시하고 루프를 다시 돌면 `kill -9`로도 안 죽는 프로세스가 생긴다 — 그 태스크는 S 상태지만 시그널 확인을 안 하는 셈이다.

### 5.5 반환값에서 절대 하지 말 것

- 양수 에러 코드: `return EINVAL;` → 유저에게는 "22바이트 읽었다"로 보인다. 컴파일러가 못 잡는다.
- `return -1;`: 유저에게는 `errno = 1 = EPERM`("Operation not permitted")으로 보인다. 엉뚱한 디버깅을 하게 만든다.
- 다른 함수의 반환값을 확인 없이 전달: `return copy_to_user(...)`(§3.3), `return i2c_smbus_read_byte_data(...)`(성공이면 데이터 바이트가 양수로 온다 — 그대로 read의 반환값으로 쓰면 "N바이트 읽음"이 된다).
- 요청보다 큰 양수: read가 `count`보다 큰 값을 돌려주면 유저 버퍼 범위를 넘었다고 주장하는 셈이다.
- 커널 내부 코드(512~517)를 시그널 경로 밖에서 반환.

---

## 6. ioctl ABI 설계 — 번호, 구조체, 32/64비트

### 6.1 ioctl 번호의 비트 구조

`_IO`/`_IOR`/`_IOW`/`_IOWR`(`include/uapi/asm-generic/ioctl.h`)는 32비트 번호에 네 필드를 넣는다: 방향(2비트), 인자 크기(14비트), type(8비트, 보통 문자 하나), nr(8비트). 방향은 **유저 기준**이다 — `_IOR`은 "유저가 읽는다" = 커널이 `copy_to_user`.

```svg
<svg viewBox="0 0 680 430" xmlns="http://www.w3.org/2000/svg">
  <text x="20" y="22" font-size="13" font-weight="bold">E6_IOC_SET_CFG = _IOW('E', 2, struct e6_cfg) = 0x40184502</text>
  <rect x="20" y="36" width="60" height="40" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <rect x="80" y="36" width="280" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <rect x="360" y="36" width="160" height="40" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <rect x="520" y="36" width="140" height="40" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="50" y="61" text-anchor="middle" font-size="12">dir</text>
  <text x="220" y="61" text-anchor="middle" font-size="12">size (14 bits)</text>
  <text x="440" y="61" text-anchor="middle" font-size="12">type (8)</text>
  <text x="590" y="61" text-anchor="middle" font-size="12">nr (8)</text>
  <text x="20" y="92" font-size="12">31</text>
  <text x="74" y="92" font-size="12">29</text>
  <text x="354" y="92" font-size="12">15</text>
  <text x="514" y="92" font-size="12">7</text>
  <text x="652" y="92" font-size="12">0</text>
  <text x="50" y="112" text-anchor="middle" font-size="12">01 = W</text>
  <text x="220" y="112" text-anchor="middle" font-size="12">0x018 = 24 bytes</text>
  <text x="440" y="112" text-anchor="middle" font-size="12">0x45 = 'E'</text>
  <text x="590" y="112" text-anchor="middle" font-size="12">0x02</text>
  <text x="20" y="150" font-size="13" font-weight="bold">구조체 레이아웃 (1칸 = 1 byte, 0~24)</text>
  <text x="20" y="184" font-size="12">bad, arm64</text>
  <rect x="140" y="166" width="84" height="28" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="182" y="185" text-anchor="middle" font-size="12">version</text>
  <rect x="224" y="166" width="84" height="28" fill="#888" fill-opacity="0.25" stroke="#888" stroke-dasharray="3 3"/>
  <text x="266" y="185" text-anchor="middle" font-size="12">hole</text>
  <rect x="308" y="166" width="168" height="28" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="392" y="185" text-anchor="middle" font-size="12">name_ptr (u64)</text>
  <rect x="476" y="166" width="84" height="28" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="518" y="185" text-anchor="middle" font-size="12">thresh</text>
  <rect x="560" y="166" width="84" height="28" fill="#888" fill-opacity="0.25" stroke="#888" stroke-dasharray="3 3"/>
  <text x="602" y="185" text-anchor="middle" font-size="12">pad</text>
  <text x="20" y="232" font-size="12">bad, i386</text>
  <rect x="140" y="214" width="84" height="28" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="182" y="233" text-anchor="middle" font-size="12">version</text>
  <rect x="224" y="214" width="168" height="28" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="308" y="233" text-anchor="middle" font-size="12">name_ptr (u64)</text>
  <rect x="392" y="214" width="84" height="28" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="434" y="233" text-anchor="middle" font-size="12">thresh</text>
  <text x="486" y="233" font-size="12">= 16 B → 번호 다름</text>
  <text x="20" y="280" font-size="12">ptr, arm32</text>
  <rect x="140" y="262" width="84" height="28" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="182" y="281" text-anchor="middle" font-size="12">version</text>
  <rect x="224" y="262" width="84" height="28" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="266" y="281" text-anchor="middle" font-size="12">name (4)</text>
  <rect x="308" y="262" width="84" height="28" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="350" y="281" text-anchor="middle" font-size="12">long (4)</text>
  <text x="402" y="281" font-size="12">= 12 B (arm64는 24 B)</text>
  <text x="20" y="328" font-size="12">good, 모두</text>
  <rect x="140" y="310" width="84" height="28" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="182" y="329" text-anchor="middle" font-size="12">version</text>
  <rect x="224" y="310" width="84" height="28" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="266" y="329" text-anchor="middle" font-size="12">thresh</text>
  <rect x="308" y="310" width="168" height="28" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="392" y="329" text-anchor="middle" font-size="12">name_ptr (__u64)</text>
  <rect x="476" y="310" width="84" height="28" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="518" y="329" text-anchor="middle" font-size="12">name_len</text>
  <rect x="560" y="310" width="84" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="602" y="329" text-anchor="middle" font-size="12">reserved</text>
  <line x1="140" y1="356" x2="644" y2="356" stroke="currentColor"/>
  <text x="140" y="374" text-anchor="middle" font-size="12">0</text>
  <text x="308" y="374" text-anchor="middle" font-size="12">8</text>
  <text x="476" y="374" text-anchor="middle" font-size="12">16</text>
  <text x="644" y="374" text-anchor="middle" font-size="12">24</text>
  <text x="20" y="404" font-size="12">원칙: 고정 폭 타입(__u32/__u64), 큰 필드부터 또는 명시적 패딩, 포인터는 __u64, 예약 필드는 0 검사</text>
</svg>
```
그림 5 — (그림에서 `version`은 `abi_version` 필드) 위: ioctl 번호의 네 필드(예제 14의 `0x40184502` 해부). 아래: 같은 의도의 구조체가 ABI마다 어떻게 놓이는지(예제 15의 pahole 결과). "bad"는 i386에서 크기가 달라져 ioctl 번호까지 달라지고, "ptr"은 32비트 ARM에서 반으로 줄어든다. "good"은 모든 ABI에서 24바이트, 구멍 없음.

### 6.2 공유 헤더 `e6_ioctl.h`

유저와 커널이 **같은 파일**을 include한다. `linux/types.h`의 `__u32`/`__u64`는 유저 공간에서도 커널에서도 같은 크기다. 실제 드라이버라면 이 헤더는 `include/uapi/linux/`에 들어간다.

```c
/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* e6_ioctl.h — 유저와 커널이 같이 include 하는 ABI 헤더 */
#ifndef E6_IOCTL_H
#define E6_IOCTL_H
#include <linux/types.h>
#include <linux/ioctl.h>

#define E6_ABI_VERSION 1
#define E6_IOC_MAGIC   'E'

struct e6_info {		/* 커널 → 유저 (_IOR) : 32 bytes, 구멍 없음 */
	__u32 abi_version;
	__u32 buf_size;
	__u64 nreads;
	__u64 nwrites;
	__u32 data_len;
	__u32 thresh;
};

struct e6_cfg {			/* 유저 → 커널 (_IOW) : 24 bytes, 구멍 없음 */
	__u32 abi_version;	/* 반드시 E6_ABI_VERSION */
	__u32 thresh;
	__u64 name_ptr;		/* 유저 포인터는 __u64 로 싣는다 */
	__u32 name_len;		/* 참고용, 커널은 NUL 까지만 읽는다 */
	__u32 reserved;		/* 반드시 0 — 나중 확장용 */
};

struct e6_blob {		/* 가변 길이 데이터 업로드 (memdup_user) */
	__u64 ptr;
	__u32 len;
	__u32 reserved;
};

#define E6_IOC_GET_INFO   _IOR(E6_IOC_MAGIC, 1, struct e6_info)
#define E6_IOC_SET_CFG    _IOW(E6_IOC_MAGIC, 2, struct e6_cfg)
#define E6_IOC_GET_LEN    _IOR(E6_IOC_MAGIC, 3, __u32)
#define E6_IOC_SET_THRESH _IOW(E6_IOC_MAGIC, 4, __u32)
#define E6_IOC_CLAIM      _IO(E6_IOC_MAGIC, 5)
#define E6_IOC_LOAD       _IOW(E6_IOC_MAGIC, 6, struct e6_blob)
#define E6_IOC_BAD_DEREF  _IOW(E6_IOC_MAGIC, 7, __u32)	/* 일부러 틀린 코드 */
#endif
```

magic 문자 `'E'`는 실습용이다. 실제 드라이버는 `Documentation/userspace-api/ioctl/ioctl-number.rst`에서 충돌하지 않는 값을 골라 등록한다.

### 6.3 `e6_dev` (2/3, 3/3): ioctl 처리와 등록

```c
static long e6_set_cfg(void __user *argp)
{
	struct e6_cfg cfg;
	char name[sizeof(e6_name)];
	long ret;

	if (copy_from_user(&cfg, argp, sizeof(cfg)))
		return -EFAULT;
	if (cfg.abi_version != E6_ABI_VERSION || cfg.reserved)
		return -EINVAL;				/* 모르는 버전, 예약 필드 != 0 */
	ret = strncpy_from_user(name, u64_to_user_ptr(cfg.name_ptr), sizeof(name));
	if (ret < 0)
		return ret;				/* -EFAULT */
	if (ret == sizeof(name))
		return -ENAMETOOLONG;			/* NUL 이 안 나왔다 */
	mutex_lock(&e6_lock);
	e6_thresh = cfg.thresh;
	strscpy(e6_name, name);
	mutex_unlock(&e6_lock);
	pr_info("e6: cfg name=\"%s\" thresh=%u\n", name, cfg.thresh);
	return 0;
}

static long e6_load(void __user *argp)
{
	struct e6_blob b;
	void *p;

	if (copy_from_user(&b, argp, sizeof(b)))
		return -EFAULT;
	if (b.reserved || !b.len || b.len > E6_BUF_SZ)
		return -EINVAL;				/* 길이 검사는 할당 전에 */
	p = memdup_user(u64_to_user_ptr(b.ptr), b.len);	/* kmalloc + copy_from_user */
	if (IS_ERR(p))
		return PTR_ERR(p);			/* -ENOMEM 또는 -EFAULT */
	mutex_lock(&e6_lock);
	memcpy(e6_buf, p, b.len);
	e6_len = b.len;
	mutex_unlock(&e6_lock);
	kfree(p);
	wake_up_interruptible(&e6_wq);
	return 0;
}
```

`strncpy_from_user`는 "복사했는데 NUL을 못 만났다"를 `ret == sizeof(name)`으로 알려 준다 — 이때 `name`은 NUL로 끝나지 않으므로 그대로 쓰면 안 된다.

```c
static long e6_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	u32 v;

	e6_maybe_stack("ioctl");
	if (_IOC_TYPE(cmd) != E6_IOC_MAGIC)
		return -ENOTTY;
	switch (cmd) {
	case E6_IOC_GET_INFO: {
		struct e6_info info;

		memset(&info, 0, sizeof(info));		/* 구멍·패딩까지 0 — 커널 정보 누출 방지 */
		info.abi_version = E6_ABI_VERSION;
		info.buf_size = E6_BUF_SZ;
		mutex_lock(&e6_lock);
		info.nreads = e6_nreads;
		info.nwrites = e6_nwrites;
		info.data_len = e6_len;
		info.thresh = e6_thresh;
		mutex_unlock(&e6_lock);
		return copy_to_user(argp, &info, sizeof(info)) ? -EFAULT : 0;
	}
	case E6_IOC_SET_CFG:
		return e6_set_cfg(argp);
	case E6_IOC_GET_LEN:
		return put_user((u32)READ_ONCE(e6_len), (u32 __user *)argp);
	case E6_IOC_SET_THRESH:
		if (get_user(v, (u32 __user *)argp))
			return -EFAULT;
		if (v > E6_BUF_SZ)
			return -EINVAL;
		WRITE_ONCE(e6_thresh, v);
		return 0;
	case E6_IOC_CLAIM: {
		long ret = 0;

		mutex_lock(&e6_lock);
		if (e6_owner && e6_owner != f)
			ret = -EBUSY;			/* 다른 사용자가 이미 잡고 있다 */
		else
			e6_owner = f;
		mutex_unlock(&e6_lock);
		return ret;
	}
	case E6_IOC_LOAD:
		return e6_load(argp);
	case E6_IOC_BAD_DEREF:
		if (!allow_bad)
			return -EPERM;
		v = *(u32 __force *)argp;		/* 틀린 코드: 유저 포인터 직접 역참조 */
		pr_info("e6: BAD_DEREF read 0x%x directly from %px\n", v, argp);
		return 0;
	default:
		return -ENOTTY;				/* 모르는 명령 — EINVAL 아님 */
	}
}
```

`GET_INFO`의 `memset`을 보자. 지금 `e6_info`에는 구멍이 없지만, 구멍 있는 구조체를 스택에 만들고 필드만 채워 `copy_to_user`하면 **패딩 바이트에 남은 커널 스택 내용**이 유저에게 그대로 간다 (info leak, 실제 CVE가 많이 나온 패턴). 습관적으로 `memset` 또는 `= {}` 초기화를 한다. `__force`는 sparse에게 "알고서 주소 공간을 바꿨다"고 말하는 표시인데, 여기서는 틀린 코드를 일부러 통과시키는 데 썼다.

```c
static int e6_release(struct inode *inode, struct file *f)
{
	mutex_lock(&e6_lock);
	if (e6_owner == f)
		e6_owner = NULL;
	mutex_unlock(&e6_lock);
	return 0;
}

static const struct file_operations e6_fops = {
	.owner          = THIS_MODULE,
	.read           = e6_read,
	.write          = e6_write,
	.unlocked_ioctl = e6_ioctl,
	.compat_ioctl   = compat_ptr_ioctl,	/* 32비트 앱도 같은 구조체를 쓴다 */
	.release        = e6_release,
	.llseek         = default_llseek,
};

static struct miscdevice e6_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "e6_dev",
	.fops  = &e6_fops,
	.mode  = 0666,
};

static int __init e6_init(void)
{
	int ret;

	e6_buf = kzalloc(E6_BUF_SZ, GFP_KERNEL);
	if (!e6_buf)
		return -ENOMEM;
	ret = misc_register(&e6_misc);
	if (ret) {
		kfree(e6_buf);
		return ret;
	}
	pr_info("e6: /dev/%s ready, minor %d\n", e6_misc.name, e6_misc.minor);
	return 0;
}

static void __exit e6_exit(void)
{
	misc_deregister(&e6_misc);
	kfree(e6_buf);
}

module_init(e6_init);
module_exit(e6_exit);
MODULE_DESCRIPTION("E6 user<->kernel boundary demo");
MODULE_LICENSE("GPL");
```

misc device는 major 10 하나를 공유하고 minor만 동적으로 받는 char device다 (G7). `misc_register()` 한 번으로 `/dev/e6_dev`가 devtmpfs에 생긴다:

```text
crw-rw-rw-    1 0        0          10, 258 Oct 10 20:50 /dev/e6_dev
258 e6_dev
```

(`ls -l /dev/e6_dev`와 `grep e6_dev /proc/misc`의 게스트 출력.)

### 6.4 예제 14 — ioctl 번호 해부와 요청별 errno

무엇을 확인하나: 번호의 네 필드, 그리고 올바른 요청·틀린 요청이 각각 어떤 errno가 되는지.

```c
/* e6_t3_ioctl: ioctl 번호 해부 + 올바른/틀린 요청별 errno */
#include "e6_common.h"

struct e6_cfg_v0 { __u32 abi_version, thresh; __u64 name_ptr; };	/* 옛 ABI (16 B) */
#define E6_IOC_SET_CFG_V0 _IOW(E6_IOC_MAGIC, 2, struct e6_cfg_v0)

static void dec(const char *n, unsigned int c)
{
	printf("%-18s 0x%08x  dir=%u size=%-3u type='%c' nr=%u\n", n, c,
	       _IOC_DIR(c), _IOC_SIZE(c), _IOC_TYPE(c), _IOC_NR(c));
}

int main(void)
{
	int fd = open_dev(O_RDWR), fd2 = open_dev(O_RDWR);
	struct e6_info in;
	struct e6_cfg c = { .abi_version = E6_ABI_VERSION, .thresh = 500,
			    .name_ptr = (__u64)(unsigned long)"imu0" };
	struct e6_cfg_v0 c0 = { 1, 500, 0 };
	char big[100], blob[100] = "calib-table";

	dec("E6_IOC_GET_INFO", E6_IOC_GET_INFO);
	dec("E6_IOC_SET_CFG", E6_IOC_SET_CFG);
	dec("E6_IOC_SET_CFG_V0", E6_IOC_SET_CFG_V0);
	dec("E6_IOC_CLAIM", E6_IOC_CLAIM);
	show("SET_CFG ok (name \"imu0\")", ioctl(fd, E6_IOC_SET_CFG, &c));
	show("GET_INFO", ioctl(fd, E6_IOC_GET_INFO, &in));
	printf("    abi=%u buf=%u len=%u thresh=%u\n", in.abi_version, in.buf_size,
	       in.data_len, in.thresh);
	c.abi_version = 2;
	show("SET_CFG abi_version=2", ioctl(fd, E6_IOC_SET_CFG, &c));
	c.abi_version = 1; c.reserved = 7;
	show("SET_CFG reserved=7", ioctl(fd, E6_IOC_SET_CFG, &c));
	c.reserved = 0; memset(big, 'n', sizeof(big) - 1); big[99] = 0;
	c.name_ptr = (__u64)(unsigned long)big;
	show("SET_CFG name 99 chars", ioctl(fd, E6_IOC_SET_CFG, &c));
	c.name_ptr = 16;
	show("SET_CFG name_ptr=16 (bad)", ioctl(fd, E6_IOC_SET_CFG, &c));
	show("SET_CFG_V0 (old 16-byte struct)", ioctl(fd, E6_IOC_SET_CFG_V0, &c0));
	show("_IO('E', 99) unknown nr", ioctl(fd, _IO('E', 99)));
	show("TCGETS (is it a tty?)", ioctl(fd, TCGETS, big));
	show("CLAIM from fd #1", ioctl(fd, E6_IOC_CLAIM));
	show("CLAIM from fd #2", ioctl(fd2, E6_IOC_CLAIM));
	struct e6_blob b = { (__u64)(unsigned long)blob, sizeof(blob), 0 };
	show("LOAD 100 bytes", ioctl(fd, E6_IOC_LOAD, &b));
	b.len = 70000;
	show("LOAD len=70000 (> 64 KiB)", ioctl(fd, E6_IOC_LOAD, &b));
	b.len = 100; b.ptr = 64;
	show("LOAD ptr=64 (bad)", ioctl(fd, E6_IOC_LOAD, &b));
	show("ioctl on fd 99 (not open)", ioctl(99, E6_IOC_CLAIM));
	return 0;
}
```

```text
E6_IOC_GET_INFO    0x80204501  dir=2 size=32  type='E' nr=1
E6_IOC_SET_CFG     0x40184502  dir=1 size=24  type='E' nr=2
E6_IOC_SET_CFG_V0  0x40104502  dir=1 size=16  type='E' nr=2
E6_IOC_CLAIM       0x00004505  dir=0 size=0   type='E' nr=5
SET_CFG ok (name "imu0")                 -> 0
GET_INFO                                 -> 0
    abi=1 buf=65536 len=4096 thresh=500
SET_CFG abi_version=2                    -> -1  errno=22  EINVAL
SET_CFG reserved=7                       -> -1  errno=22  EINVAL
SET_CFG name 99 chars                    -> -1  errno=36  ENAMETOOLONG
SET_CFG name_ptr=16 (bad)                -> -1  errno=14  EFAULT
SET_CFG_V0 (old 16-byte struct)          -> -1  errno=25  ENOTTY
_IO('E', 99) unknown nr                  -> -1  errno=25  ENOTTY
TCGETS (is it a tty?)                    -> -1  errno=25  ENOTTY
CLAIM from fd #1                         -> 0
CLAIM from fd #2                         -> -1  errno=16  EBUSY
LOAD 100 bytes                           -> 0
LOAD len=70000 (> 64 KiB)                -> -1  errno=22  EINVAL
LOAD ptr=64 (bad)                        -> -1  errno=14  EFAULT
ioctl on fd 99 (not open)                -> -1  errno=9   EBADF
```

(같은 부팅에서 예제 9 다음에 실행해서 `len=4096`이다.)

출력에서 볼 것:

- `dir=2`가 `_IOR`(커널 → 유저), `dir=1`이 `_IOW`. size 필드에 `sizeof(struct)`가 그대로 들어간다.
- **옛 구조체(16 B)로 만든 `SET_CFG_V0`는 nr이 같아도 번호 전체가 다르다** (`0x40104502` vs `0x40184502`). 드라이버의 `switch`에 없으니 `ENOTTY`. 크기를 번호에 넣는 설계 덕분에, 구조체를 바꾸면 옛 바이너리는 "모르는 명령"으로 안전하게 거절된다 — 엉뚱한 크기로 `copy_from_user`해서 쓰레기를 읽는 대신.
- 그래서 구조체 확장 방법은 둘 중 하나다: (1) 새 nr로 새 명령을 만든다, (2) 처음부터 `abi_version`/`size` 필드와 `reserved`를 넣어 두고 "0이 아니면 EINVAL"로 미래를 예약한다. e6는 (2)를 보여 준다 — `reserved=7`이 거절됐다. 이 검사를 처음부터 안 하면 나중에 그 필드에 의미를 붙일 수 없다(옛 앱이 쓰레기를 넣어 보내고 있을 수 있으니까).
- `TCGETS`(터미널 속성 읽기)는 `isatty()`가 내부에서 쓰는 ioctl이다. 우리 드라이버가 `ENOTTY`를 주니 libc는 "터미널 아님"으로 판단한다. 모르는 ioctl에 `EINVAL`을 주는 드라이버는 이런 표준 탐지를 혼란스럽게 만든다.
- `ioctl on fd 99`는 드라이버까지 오지 않고 `SYSCALL_DEFINE3(ioctl)`의 `fd_empty(f)` 검사에서 `EBADF`.

### 6.5 예제 15 — 패딩과 32/64비트 함정: pahole과 offsetof로 확인

무엇을 확인하나: 같은 구조체 선언이 aarch64, 32비트 ARM(armhf), i386에서 어떻게 배치되는지. 64비트 커널이 32비트 앱(compat)의 ioctl을 받을 때 무엇이 깨지는지.

```c
/* e6_abi: 같은 구조체가 ABI(aarch64 / arm32 / i386)마다 어떻게 놓이는가 */
typedef unsigned int u32;
typedef unsigned long long u64;

struct e6_cfg_bad {		/* 틀린 설계: 자연 정렬에 맡김 */
	u32 abi_version;
	u64 name_ptr;
	u32 thresh;
};

struct e6_cfg_ptr {		/* 틀린 설계: 포인터·long 을 그대로 */
	u32 abi_version;
	const char *name;
	long thresh;
};

struct e6_cfg {			/* 올바른 설계: 고정 폭 + 명시적 패딩 */
	u32 abi_version;
	u32 thresh;
	u64 name_ptr;
	u32 name_len;
	u32 reserved;
};

struct e6_cfg_bad b;
struct e6_cfg_ptr p;
struct e6_cfg g;
```

```sh
S=/Users/donh/.elinux_scratch/E6/abi/e6_abi.c
gcc -g -c -Wall -Wextra $S -o a64.o
arm-linux-gnueabihf-gcc -g -c -Wall -Wextra $S -o arm32.o
clang --target=i686-linux-gnu -g -c -Wall -Wextra $S -o i386.o
for o in a64 arm32 i386; do echo "#### $o"; pahole -C e6_cfg_bad,e6_cfg_ptr,e6_cfg $o.o; done
```

출력이 길어서 arm32·i386의 `e6_cfg` 블록과 i386의 `e6_cfg_ptr` 블록은 뺐다 (`e6_cfg`는 세 ABI 모두 aarch64 것과 완전히 같고, i386의 `e6_cfg_ptr`는 arm32 것과 같다 — 12 B):

```text
#### a64
struct e6_cfg_bad {
	u32                        abi_version;          /*     0     4 */

	/* XXX 4 bytes hole, try to pack */

	u64                        name_ptr;             /*     8     8 */
	u32                        thresh;               /*    16     4 */

	/* size: 24, cachelines: 1, members: 3 */
	/* sum members: 16, holes: 1, sum holes: 4 */
	/* padding: 4 */
	/* last cacheline: 24 bytes */
};

struct e6_cfg_ptr {
	u32                        abi_version;          /*     0     4 */

	/* XXX 4 bytes hole, try to pack */

	const char  *              name;                 /*     8     8 */
	long int                   thresh;               /*    16     8 */

	/* size: 24, cachelines: 1, members: 3 */
	/* sum members: 20, holes: 1, sum holes: 4 */
	/* last cacheline: 24 bytes */
};

struct e6_cfg {
	u32                        abi_version;          /*     0     4 */
	u32                        thresh;               /*     4     4 */
	u64                        name_ptr;             /*     8     8 */
	u32                        name_len;             /*    16     4 */
	u32                        reserved;             /*    20     4 */

	/* size: 24, cachelines: 1, members: 5 */
	/* last cacheline: 24 bytes */
};

#### arm32
struct e6_cfg_bad {
	u32                        abi_version;          /*     0     4 */

	/* XXX 4 bytes hole, try to pack */

	u64                        name_ptr;             /*     8     8 */
	u32                        thresh;               /*    16     4 */

	/* size: 24, cachelines: 1, members: 3 */
	/* sum members: 16, holes: 1, sum holes: 4 */
	/* padding: 4 */
	/* last cacheline: 24 bytes */
};

struct e6_cfg_ptr {
	u32                        abi_version;          /*     0     4 */
	const char  *              name;                 /*     4     4 */
	long int                   thresh;               /*     8     4 */

	/* size: 12, cachelines: 1, members: 3 */
	/* last cacheline: 12 bytes */
};

#### i386
struct e6_cfg_bad {
	u32                        abi_version;          /*     0     4 */
	u64                        name_ptr;             /*     4     8 */
	u32                        thresh;               /*    12     4 */

	/* size: 16, cachelines: 1, members: 3 */
	/* last cacheline: 16 bytes */
} __attribute__((__packed__));
```

32비트 ARM 바이너리를 `qemu-arm`(유저 모드 에뮬레이터)으로 돌려 ioctl 번호까지 비교한다:

```c
/* e6_ionum: 구조체 크기가 다르면 ioctl 번호 자체가 달라진다 (64비트 vs 32비트 빌드) */
#include <stdio.h>
#include <stddef.h>
#include <linux/ioctl.h>
#include <linux/types.h>

struct e6_cfg_bad { __u32 abi_version; __u64 name_ptr; __u32 thresh; };
struct e6_cfg_ptr { __u32 abi_version; const char *name; long thresh; };
struct e6_cfg { __u32 abi_version, thresh; __u64 name_ptr; __u32 name_len, reserved; };

#define SHOW(T) printf("%-18s size=%2zu  _IOW('E',2)=0x%08lx  offsetof(last)=%zu\n", #T, \
	sizeof(struct T), (unsigned long)_IOW('E', 2, struct T), offsetof(struct T, LAST_##T))
#define LAST_e6_cfg_bad thresh
#define LAST_e6_cfg_ptr thresh
#define LAST_e6_cfg reserved

int main(void)
{
	printf("sizeof(long)=%zu sizeof(void *)=%zu\n", sizeof(long), sizeof(void *));
	SHOW(e6_cfg_bad);
	SHOW(e6_cfg_ptr);
	SHOW(e6_cfg);
	return 0;
}
```

```sh
gcc -Wall -Wextra -O2 e6_ionum.c -o ionum64
arm-linux-gnueabihf-gcc -static -Wall -Wextra -O2 e6_ionum.c -o ionum32
echo "## aarch64" && ./ionum64 && echo "## arm32 (qemu-arm)" && qemu-arm ./ionum32
```

```text
## aarch64
sizeof(long)=8 sizeof(void *)=8
e6_cfg_bad         size=24  _IOW('E',2)=0x40184502  offsetof(last)=16
e6_cfg_ptr         size=24  _IOW('E',2)=0x40184502  offsetof(last)=16
e6_cfg             size=24  _IOW('E',2)=0x40184502  offsetof(last)=20
## arm32 (qemu-arm)
sizeof(long)=4 sizeof(void *)=4
e6_cfg_bad         size=24  _IOW('E',2)=0x40184502  offsetof(last)=16
e6_cfg_ptr         size=12  _IOW('E',2)=0x400c4502  offsetof(last)=8
e6_cfg             size=24  _IOW('E',2)=0x40184502  offsetof(last)=20
```

출력에서 볼 것:

- **`e6_cfg_bad`는 ARM끼리는 운 좋게 같다.** ARM EABI는 32비트에서도 `u64`를 8바이트로 정렬하므로 aarch64와 armhf 모두 4바이트 구멍 + 24 B다. 하지만 **i386에서는 `u64` 정렬이 4라** 구멍이 없고 16 B다(pahole은 이걸 "packed처럼 보인다"고 추정해서 `__attribute__((__packed__))`를 붙여 표시했다 — 실제로 packed로 선언한 것은 아니다). x86-64 커널 + i386 앱이면 ioctl 번호도, 필드 오프셋도 달라져 compat 변환 코드를 따로 짜야 한다. "우리 제품은 ARM만"이라도 시뮬레이터·테스트 장비가 x86일 수 있다.
- **`e6_cfg_ptr`(포인터·`long`)은 ARM에서도 깨진다.** 32비트 앱은 12 B 구조체와 `0x400c4502`를 보내는데 64비트 커널은 24 B·`0x40184502`를 기대한다. 64비트 커널 위의 32비트 유저 공간은 Embedded에서 드물지 않다(오래된 armhf rootfs, 벤더 바이너리 블롭).
- **`e6_cfg`(올바른 설계)**는 어느 ABI에서나 24 B, 구멍 없음, 같은 번호. 규칙: `__u8/__u16/__u32/__u64`만, 큰 필드부터 놓거나 명시적 `reserved`/`pad`로 8바이트 경계를 맞추고, 포인터는 `__u64`에 싣고 커널은 `u64_to_user_ptr()`로 꺼낸다. 마지막에 `pahole`로 "XXX hole"이 없는지 확인한다.

### 6.6 예제 16 — 32비트 앱이 64비트 커널의 ioctl을 부를 때 (compat 경로)

무엇을 확인하나: 예제 14의 테스트 프로그램을 armhf로 정적 빌드해서 arm64 게스트(Cortex-A57은 AArch32 EL0을 지원, 랩 커널은 `CONFIG_COMPAT=y`)에서 그대로 돌리면, 올바른 구조체 덕분에 결과가 같은지. 그리고 커널 진입 경로가 어떻게 다른지.

```sh
# VM
arm-linux-gnueabihf-gcc -std=gnu11 -Wall -Wextra -O2 -static -I/Users/donh/.elinux_scratch/E6/mod \
    -I/Users/donh/.elinux_scratch/E6/user /Users/donh/.elinux_scratch/E6/user/e6_t3_ioctl.c -o e6_t3_ioctl32
file e6_t3_ioctl32
# 게스트
./e6_t3_ioctl32 2>&1 | head -12
echo 1 > /sys/module/e6_dev/parameters/stack_once; ./e6_t3_ioctl32 > /dev/null; dmesg | grep -A16 "dump_stack() from"
```

```text
e6_t3_ioctl32: ELF 32-bit LSB executable, ARM, EABI5 version 1 (GNU/Linux), statically linked, BuildID[sha1]=8edc636f35b7272de0ecd9d5affa814ace7925c3, for GNU/Linux 3.2.0, not stripped
E6_IOC_GET_INFO    0x80204501  dir=2 size=32  type='E' nr=1
E6_IOC_SET_CFG     0x40184502  dir=1 size=24  type='E' nr=2
E6_IOC_SET_CFG_V0  0x40104502  dir=1 size=16  type='E' nr=2
E6_IOC_CLAIM       0x00004505  dir=0 size=0   type='E' nr=5
SET_CFG ok (name "imu0")                 -> 0
GET_INFO                                 -> 0
    abi=1 buf=65536 len=0 thresh=500
SET_CFG abi_version=2                    -> -1  errno=22  EINVAL
SET_CFG reserved=7                       -> -1  errno=22  EINVAL
SET_CFG name 99 chars                    -> -1  errno=36  ENAMETOOLONG
SET_CFG name_ptr=16 (bad)                -> -1  errno=14  EFAULT
SET_CFG_V0 (old 16-byte struct)          -> -1  errno=25  ENOTTY
[    3.352843] e6: dump_stack() from ioctl
[    3.353420] CPU: 0 UID: 0 PID: 66 Comm: e6_t3_ioctl32 Tainted: G           O        6.18.55 #1 PREEMPT 
[    3.353731] Tainted: [O]=OOT_MODULE
[    3.353770] Hardware name: linux,dummy-virt (DT)
[    3.354013] Call trace:
[    3.357282]  show_stack+0x28/0x7c (C)
[    3.358227]  dump_stack_lvl+0x60/0x80
[    3.358311]  dump_stack+0x18/0x24
[    3.358337]  e6_ioctl+0x4e4/0x4f8 [e6_dev]
[    3.358871]  compat_ptr_ioctl+0x20/0x40
[    3.358932]  __arm64_compat_sys_ioctl+0x128/0x17c
[    3.358957]  invoke_syscall.constprop.0+0x40/0xe0
[    3.358980]  el0_svc_common.constprop.0+0x38/0xe0
[    3.359002]  do_el0_svc_compat+0x1c/0x38
[    3.359023]  el0_svc_compat+0x30/0xb8
[    3.359084]  el0t_32_sync_handler+0x88/0xac
[    3.359114]  el0t_32_sync+0x19c/0x1a0
```

출력에서 볼 것: 32비트 앱의 결과가 64비트 앱(예제 14)과 같다 — 구조체가 ABI 독립적이라서 `compat_ptr_ioctl`(32비트 포인터 `arg`를 `compat_ptr()`로 64비트로 넓혀서 `unlocked_ioctl`을 다시 부르는 헬퍼)만으로 충분했다. 진입 경로는 벡터부터 다르다: `el0t_32_sync` → `el0_svc_compat` → `do_el0_svc_compat`(번호를 `x8`이 아니라 **`r7`**(= `regs[7]`)에서 읽고 `compat_sys_call_table`을 쓴다) → `__arm64_compat_sys_ioctl`. 구조체에 포인터나 `long`이 있었다면 `.compat_ioctl`에 변환 코드를 직접 짜야 했다.

---

## 7. 비용 측정 — syscall 한 번 + 복사는 얼마나 드나

### 7.1 예제 17 — pread/pwrite 비용 vs 버퍼 크기

무엇을 확인하나: 같은 장치에서 크기만 바꿔 `pread`/`pwrite`를 반복해, 고정 비용(syscall 왕복 + VFS + 드라이버)과 바이트당 비용(`copy_*_user`)을 나눠 본다. 기준선은 아무 일도 안 하는 `getppid`.

```c
/* e6_t5_bench: syscall + copy 비용 vs 버퍼 크기 (pread/pwrite), 9회 중 최솟값 */
#include "e6_common.h"
#include <time.h>
#include <sys/syscall.h>

static char ubuf[64 * 1024];

static double ns_now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e9 + t.tv_nsec;
}

/* op: 0=getppid 1=pread 2=pwrite. 9번 재고 가장 빠른 값(잡음 최소) */
static double bench(int fd, int op, int sz, long it)
{
	double best = 1e30;
	for (int rep = 0; rep < 9; rep++) {
		double a = ns_now();
		for (long k = 0; k < it; k++) {
			long r = op == 0 ? syscall(SYS_getppid)
			       : op == 1 ? pread(fd, ubuf, sz, 0) : pwrite(fd, ubuf, sz, 0);
			if (op && r != sz) { perror("rw"); exit(1); }
		}
		double ns = (ns_now() - a) / it;
		if (ns < best) best = ns;
	}
	return best;
}

int main(int argc, char **argv)
{
	static const int sz[] = { 1, 16, 64, 256, 1024, 4096, 16384, 65536 };
	const char *path = argc > 1 ? argv[1] : DEV;	/* 인자: 비교용 일반 파일 */
	int fd = open(path, O_RDWR | O_CREAT, 0644);
	long base = argc > 2 ? atol(argv[2]) : 2000;	/* 반복 횟수 기준 */

	if (fd < 0) { perror(path); return 1; }
	memset(ubuf, 'z', sizeof(ubuf));
	if (pwrite(fd, ubuf, sizeof(ubuf), 0) != sizeof(ubuf)) { perror("fill"); return 1; }
	printf("%s\ngetppid: %.0f ns/call\n", path, bench(fd, 0, 0, base * 10));
	printf("%6s %10s %10s %9s\n", "bytes", "pread_ns", "pwrite_ns", "rd_MB/s");
	for (unsigned i = 0; i < sizeof(sz) / sizeof(sz[0]); i++) {
		long it = sz[i] > 1024 ? base * 1024 / sz[i] + 20 : base;
		double rd = bench(fd, 1, sz[i], it), wr = bench(fd, 2, sz[i], it);
		printf("%6d %10.0f %10.0f %9.1f\n", sz[i], rd, wr, sz[i] / rd * 1e3);
	}
	return 0;
}
```

**측정상의 함정부터.** QEMU 게스트는 TCG(명령어 번역 에뮬레이션)로 돌고, 같은 VM에서 다른 작업들이 커널을 빌드하고 있었다(load average 약 15, 6코어). 처음에 그냥 돌렸더니 `getppid` 한 번이 30~120 µs로 나오고 크기에 따라 순서가 뒤집히는 잡음투성이였다. 그래서 두 가지로 바꿨다.

1. **게스트**: QEMU `-icount shift=0`으로 부팅. 게스트 시계가 실제 시간이 아니라 **실행한 명령어 수**로 흐른다(명령 1개 = 가상 1 ns). 결과는 호스트 부하와 무관하게 재현되며, 단위는 "ns"로 찍히지만 뜻은 **호출당 게스트 명령어 수**다. 캐시·파이프라인 효과는 없다.
2. **실제 하드웨어 시간**: 같은 프로그램을 Lima VM(Apple M2, 커널 7.0, 하드웨어 가상화라 syscall은 하이퍼바이저를 거치지 않는다)에서 `/var/tmp`의 일반 파일(page cache → 역시 `copy_to_user` 계열)로 돌렸다. 9회 중 최솟값이라 부하의 영향을 덜 받는다.

```sh
# 게스트: QEMU_EXTRA="-icount shift=0" run.sh g4.sh  →  ./e6_t5_bench /dev/e6_dev 300; ./e6_t5_bench /tmp/e6_file 300
# VM 호스트(7.0): ./e6_t5_bench /var/tmp/E6/e6_file 3000
```

```text
/dev/e6_dev
getppid: 390 ns/call
 bytes   pread_ns  pwrite_ns   rd_MB/s
     1        654        726       1.5
    16        664        736      24.1
    64        672        744      95.2
   256        725        797     353.0
  1024        941       1013    1088.0
  4096       1806       1878    2268.4
 16384       5263       5335    3113.2
 65536      19087      19159    3433.5
/tmp/e6_file
getppid: 390 ns/call
 bytes   pread_ns  pwrite_ns   rd_MB/s
     1       1226       1598       0.8
    16       1236       1608      12.9
    64       1244       1616      51.4
   256       1297       1669     197.4
  1024       1513       1885     676.7
  4096       2378       2750    1722.7
 16384       7212       7911    2271.8
 65536      26545      28551    2468.9
```

```text
/var/tmp/E6/e6_file
getppid: 213 ns/call
 bytes   pread_ns  pwrite_ns   rd_MB/s
     1        309        410       3.2
    16        319        415      50.1
    64        320        401     200.2
   256        319        403     802.0
  1024        363        466    2822.9
  4096        410        509    9986.0
 16384        837        918   19585.2
 65536       2438       2290   26879.7
```

(첫 번째 블록은 게스트 icount, 두 번째 블록은 VM 호스트 커널 7.0 실측. 두 번째 실행에서도 64 KiB pread가 2,758 ns로 나오는 등 몇 % 흔들렸다.)

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
<text x="181" y="24" text-anchor="middle" font-size="13" font-weight="bold">QEMU 게스트 e6_dev (icount = 명령어 수)</text>
<line x1="52" y1="260" x2="310" y2="260" stroke="currentColor"/>
<line x1="52" y1="260" x2="52" y2="50" stroke="currentColor"/>
<line x1="48" y1="260.0" x2="310" y2="260.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="45" y="264.0" text-anchor="end" font-size="12">0</text>
<line x1="48" y1="207.5" x2="310" y2="207.5" stroke="currentColor" stroke-opacity="0.15"/>
<text x="45" y="211.5" text-anchor="end" font-size="12">5,000</text>
<line x1="48" y1="155.0" x2="310" y2="155.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="45" y="159.0" text-anchor="end" font-size="12">10,000</text>
<line x1="48" y1="102.5" x2="310" y2="102.5" stroke="currentColor" stroke-opacity="0.15"/>
<text x="45" y="106.5" text-anchor="end" font-size="12">15,000</text>
<line x1="48" y1="50.0" x2="310" y2="50.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="45" y="54.0" text-anchor="end" font-size="12">20,000</text>
<line x1="52.0" y1="260" x2="52.0" y2="264" stroke="currentColor"/>
<text x="52.0" y="278" text-anchor="middle" font-size="12">1</text>
<line x1="116.5" y1="260" x2="116.5" y2="264" stroke="currentColor"/>
<text x="116.5" y="278" text-anchor="middle" font-size="12">16</text>
<line x1="181.0" y1="260" x2="181.0" y2="264" stroke="currentColor"/>
<text x="181.0" y="278" text-anchor="middle" font-size="12">256</text>
<line x1="245.5" y1="260" x2="245.5" y2="264" stroke="currentColor"/>
<text x="245.5" y="278" text-anchor="middle" font-size="12">4K</text>
<line x1="310.0" y1="260" x2="310.0" y2="264" stroke="currentColor"/>
<text x="310.0" y="278" text-anchor="middle" font-size="12">64K</text>
<text x="181" y="296" text-anchor="middle" font-size="12">버퍼 크기 (bytes, log2)</text>
<text x="14" y="155" text-anchor="middle" font-size="12" transform="rotate(-90 14 155)">명령어 수 / 호출</text>
<line x1="52" y1="255.9" x2="310" y2="255.9" stroke="#888" stroke-dasharray="5 4"/>
<text x="58" y="250.9" font-size="12">getppid 390</text>
<polyline points="52.0,252.4 116.5,252.3 148.8,252.2 181.0,251.6 213.2,249.4 245.5,240.3 277.8,204.0 310.0,58.8" fill="none" stroke="#e08a3c" stroke-width="2"/>
<circle cx="52.0" cy="252.4" r="3" fill="#e08a3c"/><circle cx="116.5" cy="252.3" r="3" fill="#e08a3c"/><circle cx="148.8" cy="252.2" r="3" fill="#e08a3c"/><circle cx="181.0" cy="251.6" r="3" fill="#e08a3c"/><circle cx="213.2" cy="249.4" r="3" fill="#e08a3c"/><circle cx="245.5" cy="240.3" r="3" fill="#e08a3c"/><circle cx="277.8" cy="204.0" r="3" fill="#e08a3c"/><circle cx="310.0" cy="58.8" r="3" fill="#e08a3c"/>
<polyline points="52.0,253.1 116.5,253.0 148.8,252.9 181.0,252.4 213.2,250.1 245.5,241.0 277.8,204.7 310.0,59.6" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<circle cx="52.0" cy="253.1" r="3" fill="#4a7bd0"/><circle cx="116.5" cy="253.0" r="3" fill="#4a7bd0"/><circle cx="148.8" cy="252.9" r="3" fill="#4a7bd0"/><circle cx="181.0" cy="252.4" r="3" fill="#4a7bd0"/><circle cx="213.2" cy="250.1" r="3" fill="#4a7bd0"/><circle cx="245.5" cy="241.0" r="3" fill="#4a7bd0"/><circle cx="277.8" cy="204.7" r="3" fill="#4a7bd0"/><circle cx="310.0" cy="59.6" r="3" fill="#4a7bd0"/>
<text x="304.0" y="51.6" text-anchor="end" font-size="12">19,087</text>
<text x="521" y="24" text-anchor="middle" font-size="13" font-weight="bold">Lima VM 커널 7.0, M2 실측 (파일)</text>
<line x1="392" y1="260" x2="650" y2="260" stroke="currentColor"/>
<line x1="392" y1="260" x2="392" y2="50" stroke="currentColor"/>
<line x1="388" y1="260.0" x2="650" y2="260.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="385" y="264.0" text-anchor="end" font-size="12">0</text>
<line x1="388" y1="218.0" x2="650" y2="218.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="385" y="222.0" text-anchor="end" font-size="12">500</text>
<line x1="388" y1="176.0" x2="650" y2="176.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="385" y="180.0" text-anchor="end" font-size="12">1,000</text>
<line x1="388" y1="134.0" x2="650" y2="134.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="385" y="138.0" text-anchor="end" font-size="12">1,500</text>
<line x1="388" y1="92.0" x2="650" y2="92.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="385" y="96.0" text-anchor="end" font-size="12">2,000</text>
<line x1="388" y1="50.0" x2="650" y2="50.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="385" y="54.0" text-anchor="end" font-size="12">2,500</text>
<line x1="392.0" y1="260" x2="392.0" y2="264" stroke="currentColor"/>
<text x="392.0" y="278" text-anchor="middle" font-size="12">1</text>
<line x1="456.5" y1="260" x2="456.5" y2="264" stroke="currentColor"/>
<text x="456.5" y="278" text-anchor="middle" font-size="12">16</text>
<line x1="521.0" y1="260" x2="521.0" y2="264" stroke="currentColor"/>
<text x="521.0" y="278" text-anchor="middle" font-size="12">256</text>
<line x1="585.5" y1="260" x2="585.5" y2="264" stroke="currentColor"/>
<text x="585.5" y="278" text-anchor="middle" font-size="12">4K</text>
<line x1="650.0" y1="260" x2="650.0" y2="264" stroke="currentColor"/>
<text x="650.0" y="278" text-anchor="middle" font-size="12">64K</text>
<text x="521" y="296" text-anchor="middle" font-size="12">버퍼 크기 (bytes, log2)</text>
<text x="354" y="155" text-anchor="middle" font-size="12" transform="rotate(-90 354 155)">ns / 호출</text>
<line x1="392" y1="242.1" x2="650" y2="242.1" stroke="#888" stroke-dasharray="5 4"/>
<text x="398" y="237.1" font-size="12">getppid 213</text>
<polyline points="392.0,225.6 456.5,225.1 488.8,226.3 521.0,226.1 553.2,220.9 585.5,217.2 617.8,182.9 650.0,67.6" fill="none" stroke="#e08a3c" stroke-width="2"/>
<circle cx="392.0" cy="225.6" r="3" fill="#e08a3c"/><circle cx="456.5" cy="225.1" r="3" fill="#e08a3c"/><circle cx="488.8" cy="226.3" r="3" fill="#e08a3c"/><circle cx="521.0" cy="226.1" r="3" fill="#e08a3c"/><circle cx="553.2" cy="220.9" r="3" fill="#e08a3c"/><circle cx="585.5" cy="217.2" r="3" fill="#e08a3c"/><circle cx="617.8" cy="182.9" r="3" fill="#e08a3c"/><circle cx="650.0" cy="67.6" r="3" fill="#e08a3c"/>
<polyline points="392.0,234.0 456.5,233.2 488.8,233.1 521.0,233.2 553.2,229.5 585.5,225.6 617.8,189.7 650.0,55.2" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<circle cx="392.0" cy="234.0" r="3" fill="#4a7bd0"/><circle cx="456.5" cy="233.2" r="3" fill="#4a7bd0"/><circle cx="488.8" cy="233.1" r="3" fill="#4a7bd0"/><circle cx="521.0" cy="233.2" r="3" fill="#4a7bd0"/><circle cx="553.2" cy="229.5" r="3" fill="#4a7bd0"/><circle cx="585.5" cy="225.6" r="3" fill="#4a7bd0"/><circle cx="617.8" cy="189.7" r="3" fill="#4a7bd0"/><circle cx="650.0" cy="55.2" r="3" fill="#4a7bd0"/>
<text x="644.0" y="47.2" text-anchor="end" font-size="12">2,438</text>
<rect x="200" y="312" width="12" height="12" fill="#4a7bd0"/><text x="218" y="323" font-size="12">pread (copy_to_user)</text>
<rect x="380" y="312" width="12" height="12" fill="#e08a3c"/><text x="398" y="323" font-size="12">pwrite (copy_from_user)</text>
</svg>
```
그림 6 — 호출당 비용 vs 버퍼 크기(x축 log2). 왼쪽: 게스트 `e6_dev`, icount 기준 명령어 수. 오른쪽: VM 호스트 커널 7.0에서 page cache 파일, 실제 ns. 두 그래프 모두 1 KiB까지는 평평하고(고정 비용 지배) 4 KiB 이후 선형으로 오른다(복사 지배).

### 7.2 숫자 읽는 법

- **고정 비용**: 게스트에서 빈 syscall(`getppid`)이 약 390 명령어, `e6_dev` 1바이트 pread가 654 명령어. 차이 ~260 명령어가 `ksys_pread64` → `vfs_read` → `e6_read`(락, `copy_to_user` 준비) 비용이다. 실측(M2)에서는 빈 syscall 213 ns, 1바이트 파일 pread 309 ns.
- **바이트당 비용**: 게스트 64 KiB에서 (19,087 − 654) / 65,535 ≈ 0.28 명령어/바이트 → 복사 루프가 명령 하나에 약 3.5바이트를 옮긴다(`ldtr`/`sttr` 페어 + 루프 제어). 실측에서는 64 KiB에 약 2.4 µs = 약 27 GB/s.
- **교차점**: 실측 기준 1 KiB 이하에서는 syscall 오버헤드가 비용의 대부분이다 — 4바이트씩 읽는 드라이버/앱 설계는 시간 대부분을 경계 넘기에 쓴다. 16 KiB를 넘으면 복사가 지배한다. 이보다 큰 데이터(카메라 프레임, LiDAR 패킷 묶음)는 복사 자체를 없애야 한다 → `mmap` + DMA 버퍼 공유 (G5, H9, I5의 videobuf2).
- 파일 쪽 1바이트 pread가 `e6_dev`보다 게스트에서 2배 가까이 비싼 것은 page cache 조회·`iov_iter` 경로(`read_iter`) 때문이다. 드라이버의 단순한 `.read`가 오히려 짧다.

---

## 8. 커널 안에서는 무슨 일이 일어나나 — 한 장 요약

드라이버 관점에서 이 노트의 경로를 다시 정리하면:

| 단계 | 커널 구조체·함수 | 드라이버가 다시 만나는 곳 |
|---|---|---|
| 진입 | `pt_regs`, `el0_svc`, `invoke_syscall`, `sys_call_table` | Oops 로그의 `pstate`/`pc`/`lr`, 콜 트레이스 읽기 (P 모듈 디버깅) |
| fd → 객체 | `struct file`(`f_op`, `f_flags`, `f_pos`, `private_data`) | `open`에서 `private_data`에 장치 구조체 연결 (G4) |
| VFS | `vfs_read`/`vfs_write`/`vfs_ioctl` | `file_operations` 채우기 (G4), misc device (G7) |
| 잠들기 | `wait_queue_head_t`, `wait_event_interruptible` | blocking read, `-ERESTARTSYS` (F3), `.poll`과 짝 (G6) |
| 유저 메모리 | `copy_*_user`, `get/put_user`, `__ex_table`, PAN | read/write/ioctl 전부 (G4, G5), sysfs는 커널이 대신 복사해 줌 (G8) |
| 에러 | `-errno`, `ERR_PTR`, `-ERESTARTSYS` | 모든 함수의 반환 규약 (E5), `-EPROBE_DEFER` (G3) |
| ABI | `_IOR`/`_IOW`, uapi 헤더, `compat_ioctl` | ioctl 설계 (G5), V4L2·IIO 같은 서브시스템은 이미 정의된 ABI를 쓴다 (I1, I2, I5) |
| 복귀 | `exit_to_user_mode_loop`, `arch_do_signal_or_restart` | 시그널 시맨틱, 지연(preemption 지점) (A6, F1) |

중요한 실무 결론 하나: **I2C/SPI 센서 드라이버를 쓸 때는 대개 이 노트의 코드를 직접 쓰지 않는다.** IIO(I2), input(I3), V4L2(I5) 같은 서브시스템에 등록하면 read/ioctl/copy_to_user는 서브시스템이 이미 올바르게 구현해 두었다 (I1 "직접 char device 만들지 말 것"). 그래도 이 노트가 필요한 이유는 (1) 서브시스템이 없는 커스텀 FPGA·MCU 브리지 장치, (2) 서브시스템 코드를 읽고 디버깅할 때, (3) 면접 화이트보드다.

---

## 9. Embedded 실무에서 다시 보기

### 9.1 센서 FIFO read — 부분 복사와 데이터 손실

예를 들어 센서 키트의 커스텀 IMU 브리지(FPGA가 IMU 샘플을 모아 링버퍼에 쌓음)를 char device로 노출한다고 하자. 흔한 버그는 이렇다:

```c
/* 틀린 코드: FIFO 에서 꺼낸 뒤 복사 실패를 무시 */
n = kfifo_out(&fifo, kbuf, count);	/* 이미 FIFO 에서 빠졌다 */
if (copy_to_user(ubuf, kbuf, n))
	return -EFAULT;			/* 꺼낸 샘플 n 개가 사라진다 */
return n;
```

FIFO에서 꺼낸 다음 복사가 실패하면 샘플이 증발한다. 해결: `kfifo_to_user()`를 쓴다 — 복사에 성공한 만큼만 FIFO에서 빼고, 복사한 바이트 수를 `*copied`로 알려 준다. 또는 "복사 성공 후에 소비 포인터를 전진"하는 순서로 직접 짠다. Don의 SSD 펌웨어에서 "호스트 DMA가 끝나기 전에 버퍼 슬롯을 해제하지 않는다"와 같은 원칙이다.

### 9.2 캘리브레이션 업로드 ioctl — 버전 필드와 예약 필드

현장에서 IMU 캘리브레이션 테이블(바이어스·스케일 행렬)을 업로드하는 ioctl은 몇 년간 앱과 커널이 따로 업데이트된다. 첫 버전부터 `e6_cfg`처럼 `abi_version` + `reserved`(0 검사) + `__u64` 포인터 + 명시적 길이를 넣고, 길이 상한을 `memdup_user` **전에** 검사한다. 테이블이 크면 `memdup_user` 대신 `vmemdup_user()`(큰 크기면 vmalloc 사용)를 고려한다. 업데이트 도구가 32비트로 빌드된 레거시라면 §6.6처럼 compat 경로에서 그대로 동작해야 한다.

### 9.3 데몬의 SIGTERM 처리와 blocking read

센서 수집 데몬이 `read(/dev/imu)`에서 잠들어 있을 때 systemd가 SIGTERM을 보내면? 데몬이 SIGTERM 핸들러를 `SA_RESTART`로 걸었다면 read는 재시작되어 계속 잠들고, 핸들러에서 세운 "종료 플래그"를 메인 루프가 영영 못 본다. 드라이버가 `-ERESTARTSYS`를 올바르게 줬어도 그렇다. 앱 쪽 해결: 종료용 시그널은 `SA_RESTART` 없이 걸거나, `signalfd` + `poll`로 처리한다 (A6). 드라이버 쪽 책임: `wait_event_interruptible`의 반환값을 꼭 확인해서 `-ERESTARTSYS`를 돌려줄 것 — 무시하면 `systemctl stop`이 90초 타임아웃 후 SIGKILL로 갈 때까지 매달린다.

### 9.4 "개발 보드에서는 됐는데 양산 SoC에서 Oops"

예제 12의 상황이 실제로 일어난다. Cortex-A53/A57(ARMv8.0) 개발 보드에서는 직접 역참조 버그가 조용히 동작하다가, Cortex-A55/A78(ARMv8.2, PAN 있음) 양산 SoC에서 `access to user memory outside uaccess routines` Oops가 난다. 커널 업그레이드 시 `CONFIG_ARM64_SW_TTBR0_PAN`을 켜도 같은 현상이 드러난다. 예방: 모든 드라이버를 `make C=1`(sparse)로 빌드해서 `__user` 경고를 0으로 유지한다 (E5). sparse는 `incorrect type in argument (different address spaces)`로 이런 코드를 컴파일 단계에서 잡는다.

---

## 10. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 유저 포인터 직접 역참조 (`*ubuf`, `memcpy(k, ubuf, n)`) | PAN 없는 보드에선 동작, PAN 있는 SoC에선 Oops "outside uaccess routines". 나쁜 주소면 어디서든 Oops | 일반 load/store + 예외 테이블 엔트리 없음 | `copy_from_user`/`get_user`. sparse(`make C=1`) |
| `return copy_to_user(...)` | 성공한 read가 0(EOF)을 돌려줌, 실패 시 양수 반환 | 반환값이 "못 복사한 바이트 수" | 패턴 A(`? -EFAULT : 0`) 또는 패턴 B(§3.3) |
| `if (copy_from_user(...) < 0)` | 실패를 절대 감지 못 함 | 반환형 `unsigned long` | `if (copy_from_user(...))` |
| `count == 0` 미처리 | `read(fd, buf, 0)`이 EFAULT | `left == n`이 0 == 0으로 참 | 맨 앞에서 `if (!count) return 0;` |
| 부분 복사 무시하고 n 반환 | 유저 버퍼 일부가 쓰레기, 데이터 유실 | `left`를 안 봄 | `n - left` 반환, `*ppos += n - left` |
| 모르는 ioctl에 `-EINVAL` | `isatty()`·라이브러리의 기능 탐지가 오동작 | 규약 위반 | `default: return -ENOTTY;` |
| `wait_event_interruptible` 반환값 무시 | `kill -9`로도 안 죽음, 100% CPU 루프 | 시그널로 깨도 다시 기다림 | `if (wait_event_interruptible(...)) return -ERESTARTSYS;` |
| 데이터 일부 처리 후 `-ERESTARTSYS` | 재시작 시 데이터 중복·유실 | 재시작은 "아무 일 없었던 것처럼" 전제 | 처리한 만큼 양수 반환 |
| ioctl 구조체에 포인터·`long`·암묵적 패딩 | 32비트 앱에서 ENOTTY 또는 엉뚱한 필드 값, 커널 스택 내용 누출 | ABI마다 크기·정렬 다름, 패딩 미초기화 | `__u32/__u64`만, 명시적 `reserved`, `memset` 후 `copy_to_user`, pahole 확인 |
| `memdup_user(ptr, user_len)` 전에 길이 검사 없음 | 거대한 할당 시도, 메모리 압박 | 길이를 유저가 정함 | 상한 검사 후 호출, 큰 것은 `vmemdup_user` |
| spinlock 잡고 `copy_to_user` | "BUG: sleeping function called from invalid context", 간헐적 데드락 | page fault 처리가 잠들 수 있음 | 커널 버퍼로 복사 → 락 해제 → `copy_to_user` (F2) |
| `return -1` / `return EINVAL` | 유저 errno가 EPERM이거나 "22바이트 읽음" | 음수 errno 규약 위반 | 항상 `-E...` 상수 |

---

## 11. 면접에서 이렇게 말한다

**Q.** Walk me through what happens when a user program calls read() on your device file, on arm64.

**A.** 유저가 `x8`에 63, `x0~x2`에 인자를 넣고 `svc #0`을 실행하면 EL1의 동기 예외 벡터로 들어간다. `el0t_64_sync`가 레지스터를 `pt_regs`에 저장하고, `el0t_64_sync_handler`가 ESR로 SVC임을 확인해 `el0_svc` → `do_el0_svc` → `invoke_syscall`로 간다. 여기서 `sys_call_table[63]`인 `__arm64_sys_read`가 `pt_regs`에서 인자를 꺼내 `ksys_read`를 부르고, fd를 `struct file`로 바꾼 뒤 `vfs_read`가 권한과 `access_ok`를 확인하고 `file->f_op->read`, 즉 내 드라이버 함수를 부른다. 반환값은 `regs[0]`에 저장되고, 유저로 돌아가기 직전에 재스케줄과 시그널을 처리한 뒤 `eret`한다.

> On arm64 the libc wrapper puts the syscall number in x8 and the arguments in x0 to x2, then executes svc #0. That traps to the EL1 sync vector; el0t_64_sync saves all user registers into a pt_regs frame, the handler checks ESR, sees an SVC, and goes el0_svc, do_el0_svc, invoke_syscall, which indexes sys_call_table and calls __arm64_sys_read. That wrapper pulls the arguments out of pt_regs and calls ksys_read, which resolves the fd to a struct file, and vfs_read does the permission and access_ok checks and calls file->f_op->read, which is my driver. The return value goes into regs[0], and on the way out the kernel handles rescheduling and pending signals before eret.

**꼬리 질문**: "Why do syscall handlers take a pt_regs pointer instead of normal arguments?" → 커널 C 코드에 유저가 통제하는 다른 레지스터 값이 흘러 들어가지 않게 하고(투기 실행 공격 표면 축소), 래퍼에서 타입별로 잘라(`__se_sys_*`) 넘기기 위해서다.

**Q.** Why can't the kernel just dereference a user pointer? What does copy_from_user do differently?

**A.** 네 가지 이유다. 유저가 커널 주소를 넘길 수 있고, 매핑이 없을 수 있고, 매핑은 있어도 페이지가 아직 없을 수 있고, ARMv8.1 이상에서는 PAN이 커널의 일반 load/store로 유저 메모리 접근을 하드웨어로 막는다. `copy_from_user`는 `access_ok`로 범위를 확인하고, 유저 권한으로 접근하는 LDTR 명령으로 복사하며, 그 명령들이 예외 테이블에 등록돼 있어서 fault가 나면 page fault 핸들러가 정상 처리하거나, 불가능하면 fixup 코드로 점프해 못 복사한 바이트 수를 돌려준다. 그래서 나쁜 포인터가 Oops 대신 EFAULT가 된다. 실제로 PAN 없는 Cortex-A57에서는 직접 역참조가 동작해 버리고, PAN 있는 CPU에서는 같은 코드가 Oops 나는 것을 QEMU로 확인했다.

> A user pointer may point into kernel space, may be unmapped, may be valid but not yet faulted in, and on ARMv8.1 and later, PAN makes a plain kernel load from user memory fault in hardware. copy_from_user checks the range with access_ok, does the copy with unprivileged LDTR/STTR instructions, and every one of those instructions has an exception table entry. If a fault happens, the page-fault handler either fixes it up normally, or, if the address is really bad, looks up the faulting PC in __ex_table and jumps to fixup code that returns the number of bytes not copied. So a bad pointer becomes -EFAULT instead of an oops. I've seen a direct dereference silently work on a Cortex-A57 without PAN and oops on a CPU with PAN — the bug was there all along.

**Q.** What does copy_to_user return, and how do you handle a partial copy in read()?

**A.** 0이면 전부 성공이고, 아니면 **복사하지 못한 바이트 수**다. 음수 에러가 아니다. read에서는 `left == n`이면 한 바이트도 못 줬으니 `-EFAULT`, 아니면 `n - left`만큼 `*ppos`를 전진시키고 그 값을 돌려준다. 고정 크기 구조체는 그냥 0이 아니면 `-EFAULT`. 그리고 그 숫자가 정확히 fault 지점까지라는 보장은 없다 — arm64에서 유효한 100바이트 중 52바이트만 성공으로 센 것을 직접 측정했다. 보장되는 건 앞쪽 `n - left` 바이트가 복사됐다는 것뿐이다.

> It returns the number of bytes that could NOT be copied, so zero means success — not a negative errno. In read I compute left; if nothing was copied I return -EFAULT, otherwise I advance `*ppos` by n minus left and return that, a short read. For fixed-size structs in ioctl it's all-or-nothing: nonzero means -EFAULT. Also, the count isn't byte-exact at the fault boundary — on arm64 I measured 52 of 100 valid bytes reported as copied — the only guarantee is that the first n minus left bytes made it.

**꼬리 질문**: "What does copy_from_user do with the uncopied tail?" → 커널 쪽 버퍼의 남은 부분을 0으로 채운다. 정보 누출 방지.

**Q.** Which errno do you return for an unknown ioctl, and why not EINVAL?

**A.** `-ENOTTY`. "이 장치는 그 명령을 모른다"는 표준 의미이고, libc의 `isatty()`나 라이브러리들이 이 값으로 기능 유무를 판별한다. `EINVAL`은 "명령은 알지만 인자가 틀렸다"는 뜻이라 섞으면 유저가 잘못 반응한다. 서브시스템 내부에서는 `-ENOIOCTLCMD`를 쓰고 VFS가 `-ENOTTY`로 바꿔 준다.

> -ENOTTY. It's the established meaning of "this device doesn't implement that command" — isatty() and many libraries probe capabilities that way. EINVAL means "I know the command but your argument is wrong," so mixing them misleads callers. Inside a subsystem you can return -ENOIOCTLCMD and vfs_ioctl converts it to -ENOTTY.

**Q.** What is ERESTARTSYS and when does the user see EINTR instead?

**A.** 드라이버가 `wait_event_interruptible` 같은 대기에서 시그널로 깼을 때 돌려주는 커널 내부 코드(512)다. 유저에게는 절대 그대로 안 간다. syscall 복귀 직전 `arch_do_signal_or_restart`가 보고, 처리할 시그널 핸들러에 `SA_RESTART`가 있으면 PC를 `svc` 명령으로 되돌리고 `x0`를 `orig_x0`로 복원해 핸들러 실행 후 syscall을 다시 실행하고, 없으면 `-EINTR`로 바꾼다. QEMU에서 같은 드라이버로 두 경우를 다 확인했다. 드라이버 규칙은 "잠들기 전엔 상태를 바꾸지 말 것" — 일부 처리했으면 처리한 양을 반환해야 한다.

> It's a kernel-internal code the driver returns when an interruptible sleep is broken by a signal; userspace never sees it. On the way back to user mode, arch_do_signal_or_restart checks it: if the handler for the pending signal has SA_RESTART, it rewinds the PC to the svc instruction and restores x0 from orig_x0, so the syscall reruns after the handler; otherwise it rewrites the result to -EINTR. The driver's contract is that returning -ERESTARTSYS means nothing happened yet — if you already consumed data, return the byte count instead.

**Q.** How do you design an ioctl struct so it works for 32-bit userspace on a 64-bit kernel?

**A.** 고정 폭 타입(`__u32`, `__u64`)만 쓰고, 포인터와 `long`은 쓰지 않는다 — 포인터는 `__u64`에 담고 커널에서 `u64_to_user_ptr`로 꺼낸다. 필드는 큰 것부터 놓거나 명시적 패딩을 넣어 구멍이 없게 하고 pahole로 확인한다. i386은 `u64` 정렬이 4라 암묵적 패딩이 있으면 크기가 달라진다. 버전 필드와 0이어야 하는 reserved 필드를 넣어 확장을 대비하고, `compat_ioctl = compat_ptr_ioctl`로 끝낸다. 실제로 포인터가 든 구조체는 armhf에서 12바이트, arm64에서 24바이트라 ioctl 번호부터 달라지는 것을 확인했다.

> Use only fixed-width types, never pointers or long; carry pointers as __u64 and convert with u64_to_user_ptr. Order fields or add explicit padding so there are no holes, and check with pahole — on i386 a u64 is only 4-byte aligned, so implicit padding changes the size and therefore the ioctl number, because _IOW encodes sizeof. Add a version field and reserved fields that must be zero so you can extend it later. With that, .compat_ioctl = compat_ptr_ioctl is all you need.

**Q.** Your driver's read() is called with count == 0, or with a buffer that crosses into an unmapped page. What should happen?

**A.** `count == 0`이면 그냥 0을 돌려준다(성공, 아무것도 안 함). 처리 안 하면 "0바이트 못 복사 == 0바이트 요청"이 되어 EFAULT가 나는 버그가 생긴다 — 실제로 측정 코드를 짜다가 내 드라이버에서 발견했다. 매핑이 끊기는 버퍼면 `copy_to_user`가 일부만 복사하고 나머지 개수를 돌려주므로, 복사된 만큼만 반환하는 짧은 read가 정답이다. 앱은 다음 read에서 EFAULT를 받는다.

> Zero count: return 0 — success, nothing done. If you don't special-case it, "left == n" is true for 0 == 0 and you return -EFAULT; I actually hit that bug in my own test driver. For a buffer that runs into an unmapped page, copy_to_user copies a prefix and returns the remainder, so you return a short count and advance `*ppos` by that much; the application gets -EFAULT on the next call.

---

## 12. 직접 해보기

1. **개념** — `invoke_syscall`이 `sys_call_table[scno]`를 부르기 전에 `array_index_nospec()`을 쓰는 이유를 설명하라. 범위 검사(`if (scno < sc_nr)`)만으로 부족한 이유는?
   정답/힌트: CPU가 분기를 예측해서 검사 결과가 나오기 전에 테이블을 투기적으로 읽을 수 있다(Spectre v1). 예제 4의 `cmp`/`ngc`/`csdb`/`and`가 인덱스를 데이터 의존적으로 0으로 누른다.

2. **코드 과제** — `e6_read`의 락 안쪽 복사 부분을 `simple_read_from_buffer()` 한 줄로 바꾸고, 예제 8·9를 다시 돌려 출력이 같은지 확인하라. `e6_nreads` 카운트는 어떻게 유지할 것인가?
   정답/힌트: `ret = simple_read_from_buffer(ubuf, count, ppos, e6_buf, e6_len); if (ret > 0) e6_nreads++;` (락 안에서). 예제 9의 부분 복사 숫자도 같은 `copy_to_user`를 쓰므로 같아야 한다.

3. **코드 과제** — `e6_ioctl`에 `E6_IOC_GET_NAME`(`_IOR('E', 8, char[16])`)을 추가해 `e6_name`을 돌려주라. 이때 `copy_to_user(argp, e6_name, sizeof(e6_name))`이 안전한 이유와, `strlen+1`만 복사할 때의 장단점을 비교하라.
   정답/힌트: `e6_name`은 전역으로 0 초기화된 고정 배열이라 NUL 뒤가 0이다 → 누출 없음. 스택 버퍼였다면 `memset` 필요. 크기는 ABI(16 B)에 맞추는 편이 단순하다.

4. **관찰 과제** — 게스트에서 `cat /dev/e6_dev &`로 잠들게 한 뒤 `/proc/<pid>/stack`, `/proc/<pid>/wchan`, `/proc/<pid>/syscall`을 차례로 읽어라. `syscall` 파일의 첫 숫자와 두 번째 숫자는 무엇인가?
   정답/힌트: 63(read)과 fd 번호. A1 §3.6의 방법을 커널 쪽 스택과 함께 보는 것이다. `kill -STOP`/`-CONT`를 보내면 스택이 어떻게 변하는지도 보라.

5. **관찰 과제** — 예제 9의 페이지 경계 실험에서 시작 오프셋을 `pg - 100` 대신 `pg - 1`, `pg - 16`, `pg - 64`, `pg - 65`로 바꿔 write/read가 각각 몇 바이트를 반환하는지 표로 만들고, `copy_template.S`의 루프 구조로 설명하라.
   정답/힌트: fixup은 마지막으로 성공한 묶음 경계까지만 센다(§4.4). 아무것도 못 옮긴 경우의 "try harder" 1바이트 규칙도 확인하라.

6. **설계 문제** — IMU 캘리브레이션 테이블(가변 길이, 최대 64 KiB)을 업로드하는 ioctl과 sysfs `bin_attribute` 중 무엇을 고르겠는가? 각각 유저 메모리 복사·권한·ABI 측면에서 비교하라.
   정답/힌트: sysfs bin_attribute는 커널이 버퍼를 대신 복사해 주고 파일 권한으로 접근 제어, 셸에서도 쓸 수 있다(G8). ioctl은 원자적 다중 필드 설정·버전 협상에 유리. 둘 다 길이 상한 검사는 드라이버 몫.

7. **확장 과제** — 랩 커널을 `/var/tmp/<ID>/build`에 `CONFIG_FTRACE=y`, `CONFIG_FUNCTION_GRAPH_TRACER=y`로 다시 빌드해서 `set_graph_function=ksys_read`로 예제 8의 read를 추적하고, 예제 7의 `dump_stack` 결과와 비교하라. `invoke_syscall.constprop.0`과 인라인된 함수들은 어떻게 보이는가?
   정답/힌트: function_graph는 진입·복귀를 모두 보여 주므로 `copy_to_user` 안의 `__arch_copy_to_user` 시간까지 보인다. 인라인된 함수(`vfs_ioctl` 등)는 나타나지 않는다.

---

## 13. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| `pt_regs` | 저장된 유저 레지스터 프레임 | 예외 진입 때 커널 스택에 쌓는 `x0`~`x30`, `sp`, `pc`, `pstate`, `orig_x0`, `syscallno` |
| `sys_call_table` | syscall 함수 포인터 표 | `[nr] = __arm64_sys_*`. 빈 칸은 `sys_ni_syscall`(`-ENOSYS`) |
| `SYSCALL_DEFINEn` | syscall 정의 매크로 | `__arm64_sys_*`(pt_regs 래퍼), `__se_sys_*`(인자 자르기), `__do_sys_*`(본문) 세 함수를 만든다 |
| `orig_x0` | 원래 첫 인자 | 반환값이 `x0`를 덮으므로 syscall 재시작 때 복원용으로 보관 |
| uaccess | 유저 메모리 접근 | `copy_*_user`, `get/put_user` 등 커널이 유저 주소를 만지는 정식 통로 |
| `access_ok` | 주소 범위 검사 | 범위가 유저 영역(`TASK_SIZE_MAX` 미만)인지만 본다. 매핑 여부는 모름 |
| LDTR/STTR | unprivileged load/store | EL1에서 실행해도 EL0 권한으로 접근하는 arm64 명령. PAN 아래서도 유저 메모리 접근 가능 |
| PAN | Privileged Access Never | ARMv8.1. EL1의 일반 접근이 유저 페이지에 닿으면 permission fault |
| `__ex_table` | 예외 테이블 | (fault 가능 명령, fixup 주소, 처리 타입) 목록. fault 시 PC로 검색 |
| fixup | 대체 실행 경로 | fault 난 uaccess 명령 대신 점프하는 코드. 보통 `-EFAULT` 또는 남은 바이트 수 반환 |
| `uaccess_mask_ptr` | 포인터 마스킹 | 주소 bit 55를 지워 투기 실행으로도 커널 주소에 접근하지 못하게 함 |
| `-errno` | 음수 에러 코드 | 커널 함수의 실패 표현. 유저에서는 `-1` + `errno` |
| `ERESTARTSYS` | 재시작 가능 표시 (512) | 시그널로 대기가 깨졌을 때. 복귀 시 재시작 또는 `-EINTR`로 변환 |
| `SA_RESTART` | sigaction 플래그 | 핸들러 후 중단된 syscall을 자동 재시작 |
| `ENOTTY` | 부적절한 ioctl (25) | 모르는 ioctl 명령의 표준 반환값 |
| `_IOR`/`_IOW` | ioctl 번호 매크로 | 방향·크기·type·nr을 32비트에 인코딩. 방향은 유저 기준 |
| compat | 32비트 호환 계층 | 64비트 커널이 32비트 유저 프로그램의 syscall·ioctl을 처리하는 경로 |
| `compat_ptr_ioctl` | compat 헬퍼 | 32비트 포인터 인자를 넓혀 `unlocked_ioctl`을 다시 부름. 구조체가 ABI 독립일 때만 충분 |
| `u64_to_user_ptr` | 변환 매크로 | ioctl 구조체의 `__u64` 필드를 `void __user *`로 |
| `memdup_user` | 할당 + 복사 | `kmalloc` + `copy_from_user`, 실패 시 `ERR_PTR` |
| misc device | 간이 char device | major 10 공유, `misc_register()` 한 번으로 `/dev` 노드 생성 |
| icount | QEMU 명령어 카운트 모드 | 게스트 시간을 실행 명령어 수로 흐르게 해 재현 가능한 측정 |

---

## 14. 요약 & 체크리스트

arm64에서 syscall은 `svc #0` → `el0t_64_sync`(pt_regs 저장) → `el0t_64_sync_handler` → `el0_svc` → `do_el0_svc` → `invoke_syscall`(`sys_call_table[x8]`) → `__arm64_sys_*`(pt_regs에서 인자 추출) → VFS → `f_op->read/write/unlocked_ioctl` 순으로 드라이버에 닿고, 반환값은 `regs[0]`에 저장된 뒤 복귀 직전의 `exit_to_user_mode_loop`에서 스케줄·시그널(재시작 포함)을 처리하고 `eret`한다. 드라이버는 유저 포인터를 절대 직접 만지지 않고 `copy_*_user`/`get_user`/`put_user`/`strncpy_from_user`/`memdup_user`를 쓴다. 이 함수들은 `access_ok`, LDTR/STTR, 예외 테이블 덕분에 나쁜 포인터를 Oops 대신 `-EFAULT`로 바꾸고, `copy_*_user`는 "못 복사한 바이트 수"를 돌려주므로 read/write는 부분 복사를 짧은 반환값으로 정직하게 알려야 한다. 에러는 항상 음수 errno이며, 모르는 ioctl은 `-ENOTTY`, 주소 문제만 `-EFAULT`, 인터럽트 가능한 대기의 시그널은 `-ERESTARTSYS`다. ioctl 구조체는 고정 폭 타입·명시적 패딩·`__u64` 포인터·버전·reserved로 설계해서 32/64비트 모두에서 같은 번호와 레이아웃을 갖게 한다.

- [ ] `svc #0`부터 드라이버의 `.read`까지 arm64 함수 이름을 순서대로 말할 수 있다
- [ ] `pt_regs`에서 인자·번호·반환값·`orig_x0`가 어디 있는지 설명할 수 있다
- [ ] `SYSCALL_DEFINE3`가 만드는 세 함수와 각각의 역할을 설명할 수 있다
- [ ] `copy_to_user`/`copy_from_user`의 반환값 의미와 부분 복사 처리 패턴 두 가지를 코드로 짤 수 있다
- [ ] 유저 포인터를 직접 역참조하면 안 되는 네 가지 이유를 말할 수 있다
- [ ] 예외 테이블 엔트리의 구성과 fault → fixup 경로를 그림으로 설명할 수 있다
- [ ] PAN이 무엇이고 LDTR/STTR이 왜 PAN 아래서도 동작하는지 설명할 수 있다
- [ ] 상황별로 `EINVAL`/`EFAULT`/`ENOMEM`/`EBUSY`/`EAGAIN`/`ENOTTY`를 고를 수 있다
- [ ] `-ERESTARTSYS`가 `SA_RESTART` 여부에 따라 재시작 또는 `EINTR`이 되는 과정을 설명할 수 있다
- [ ] `_IOR`/`_IOW` 번호를 손으로 해독하고, 구조체 크기가 번호에 들어가는 이유를 말할 수 있다
- [ ] 32/64비트에서 깨지지 않는 ioctl 구조체를 설계하고 pahole로 검증할 수 있다
- [ ] blocking read + `O_NONBLOCK` + 시그널을 올바르게 처리하는 misc 드라이버를 처음부터 짤 수 있다

## 참고 자료

- docs.kernel.org — "Adding a New System Call": https://docs.kernel.org/process/adding-syscalls.html
- docs.kernel.org — "ioctl based interfaces": https://docs.kernel.org/driver-api/ioctl.html
- docs.kernel.org — "Ioctl Numbers": https://docs.kernel.org/userspace-api/ioctl/ioctl-number.html
- man7.org — `errno(3)`: https://man7.org/linux/man-pages/man3/errno.3.html
- man7.org — `ioctl(2)`: https://man7.org/linux/man-pages/man2/ioctl.2.html
- man7.org — `signal(7)` ("Interruption of system calls and library functions by signal handlers"): https://man7.org/linux/man-pages/man7/signal.7.html
- man7.org — `syscall(2)` (아키텍처별 호출 규약 표): https://man7.org/linux/man-pages/man2/syscall.2.html
- Michael Kerrisk, "The Linux Programming Interface" — 3장(System Programming Concepts), 21.5절(Interruption and Restarting of System Calls)
- 랩 소스 (elixir.bootlin.com에서 같은 버전으로 볼 수 있다): `arch/arm64/kernel/entry.S`, `arch/arm64/kernel/entry-common.c`, `arch/arm64/kernel/syscall.c`, `arch/arm64/kernel/sys.c`, `arch/arm64/kernel/signal.c`, `arch/arm64/include/asm/uaccess.h`, `arch/arm64/include/asm/extable.h`, `arch/arm64/mm/extable.c`, `arch/arm64/mm/fault.c`, `arch/arm64/lib/copy_from_user.S`, `include/linux/uaccess.h`, `include/linux/syscalls.h`, `arch/arm64/include/asm/syscall_wrapper.h`, `include/linux/errno.h`, `fs/read_write.c`, `fs/ioctl.c`, `fs/libfs.c`, `mm/util.c`, `kernel/entry/common.c` — https://elixir.bootlin.com/linux/v6.18/source
- Arm Architecture Reference Manual (Armv8-A) — PAN(FEAT_PAN), LDTR/STTR 명령 설명
- 이전 노트: A1 §3(유저 쪽 syscall과 `-errno`), A6(시그널), E3(모듈), E5(`ERR_PTR`, `__user`, sparse)
