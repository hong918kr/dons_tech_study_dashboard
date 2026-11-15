---
id: ELX-B08
time: 70분
updated: 2026-11-15
---
# ELX-B08 ioctl 설계: _IO/_IOR/_IOW 번호와 구조체 ABI

## 학습 목표

- ioctl 번호 32비트를 **dir(2) · size(14) · type(8) · nr(8)** 로 손으로 쪼개고, `_IOR/_IOW/_IO` 매크로 값과 비트 단위로 맞춰 본다.
- 드라이버에서 번호의 네 필드를 검사해 모르는 명령은 `-ENOTTY`, 값이 틀리면 `-EINVAL`, 유저 포인터가 나쁘면 `-EFAULT` 로 정확히 갈라 돌려준다.
- `__u32/__u64` 와 명시적 패딩으로 **arm64 · arm32 · x86_64 · i386 모두 40 바이트**인 구조체를 설계하고, `long`/포인터가 들어가면 왜 깨지는지 pahole 로 확인한다.
- 32비트 ARM 앱을 64비트 커널에서 실제로 돌려 `.compat_ioctl = compat_ptr_ioctl` 이 있을 때와 없을 때를 비교한다.
- "nr 만 보고 분기하는 드라이버"가 유저 메모리를 덮어쓰는 것을 실측으로 보고, 면접에서 설명한다.

## 1. 문제

ioctl은 드라이버가 유저 공간과 맺는 **영구 계약(ABI)** 이다. 한 번 내보내면 10년 뒤 32비트 앱도 같은 번호·같은 구조체로 불러야 하므로, BSP/드라이버 면접에서는 "ioctl 번호는 어떻게 생겼나, 32/64비트에서 구조체가 안 깨지게 하려면?"을 꼭 묻는다. SSD 펌웨어로 치면 NVMe 커맨드 구조체(SQE) 레이아웃을 호스트와 컨트롤러가 똑같이 봐야 하는 문제와 같다.

> 환경: 모든 게스트 출력은 **Linux 6.18.55 (`/var/lab/out/Image`), QEMU TCG cortex-a57 `-smp 1 -m 512M`** 실측이다. 모듈은 게스트 안에서만 올린다. 유저 테스트는 AArch64(`ktest`)와 AArch32(`ktest32`, `arm-linux-gnueabihf-gcc -static`) 두 벌을 같은 게스트에서 돌린다. 랩 커널은 `CONFIG_COMPAT=y` 이고, QEMU cortex-a57 은 EL0 AArch32 를 지원하므로 32비트 바이너리가 그대로 돈다.

준비물은 ELX-B07의 `/dev/kecho` 를 **짧게 다시 쓴 사본**이다(4096 B 버퍼, write는 `*ppos` 에 저장·남은 공간만큼 짧은 쓰기·공간 0이면 `-ENOSPC`, read는 `*ppos` 부터, llseek, 장치당 mutex — read/write 의미의 주인은 B07). 여기에 명령 세 개를 붙인다.

| 명령 | 매크로 | 값 (실측) | 인자 | 하는 일 |
|---|---|---|---|---|
| GET_STATS | `_IOR('x', 1, struct kecho_stats)` | `0x80287801` | 커널 → 유저 40 B | 통계 스냅숏 |
| SET_MODE | `_IOW('x', 2, __u32)` | `0x40047802` | 유저 → 커널 4 B | 0 = RAW, 1 = UPPER(쓸 때 대문자로 저장), 그 밖은 `-EINVAL` |
| RESET | `_IO('x', 3)` | `0x00007803` | 없음 (arg 무시) | 데이터·통계를 0으로, mode는 유지 |

magic `'x'`(0x78)는 6.18의 `Documentation/userspace-api/ioctl/ioctl-number.rst` 에 없는 문자라서 골랐다. 처음 떠올린 `'k'` 는 이미 `linux/spi/spidev.h` 가 쓰고 있고 그 표에 `conflict!` 표시까지 붙어 있다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">KECHO_IOC_GET_STATS = _IOR('x', 1, struct kecho_stats) = 0x80287801</text>
<rect x="10" y="35" width="70" height="40" class="f-warn" stroke="currentColor"/>
<rect x="80" y="35" width="330" height="40" class="f-acc" stroke="currentColor"/>
<rect x="410" y="35" width="170" height="40" class="f-ok" stroke="currentColor"/>
<rect x="580" y="35" width="150" height="40" class="f-mut" stroke="currentColor"/>
<text x="20" y="60" fill="currentColor">dir</text>
<text x="90" y="60" fill="currentColor">size (14 bits)</text>
<text x="420" y="60" fill="currentColor">type (8)</text>
<text x="590" y="60" fill="currentColor">nr (8)</text>
<text x="10" y="92" fill="currentColor" font-size="12">31..30</text>
<text x="80" y="92" fill="currentColor" font-size="12">29..16</text>
<text x="410" y="92" fill="currentColor" font-size="12">15..8</text>
<text x="580" y="92" fill="currentColor" font-size="12">7..0</text>
<text x="20" y="115" fill="currentColor" font-family="monospace">10</text>
<text x="90" y="115" fill="currentColor" font-family="monospace">00000000101000</text>
<text x="420" y="115" fill="currentColor" font-family="monospace">01111000</text>
<text x="590" y="115" fill="currentColor" font-family="monospace">00000001</text>
<text x="20" y="135" fill="currentColor" font-size="12">2 = READ</text>
<text x="90" y="135" fill="currentColor" font-size="12">40 = sizeof(struct kecho_stats)</text>
<text x="420" y="135" fill="currentColor" font-size="12">0x78 = 'x'</text>
<text x="590" y="135" fill="currentColor" font-size="12">1</text>
<text x="10" y="170" fill="currentColor" font-family="monospace" font-size="12">SET_MODE 0x40047802 = 01 | 00000000000100 | 01111000 | 00000010  (W, 4 B, 'x', 2)</text>
<text x="10" y="190" fill="currentColor" font-family="monospace" font-size="12">RESET    0x00007803 = 00 | 00000000000000 | 01111000 | 00000011  (none, 0, 'x', 3)</text>
<text x="10" y="215" fill="currentColor" font-size="12">방향은 유저 기준: _IOR = 유저가 읽는다 = 커널이 copy_to_user. _IOW = 커널이 copy_from_user.</text>
<text x="10" y="235" fill="currentColor" font-size="12">비트열은 ktest 가 게스트에서 (c &gt;&gt; 30), (c &gt;&gt; 16) &amp; 0x3fff ... 로 직접 자른 값 (§5 로그).</text>
</svg>
```

### Part A — 번호와 정상 경로

- A1. `kecho_check_cmd(cmd)` — `_IOC_TYPE` 가 `'x'` 인지, `_IOC_NR` 이 표(`kecho_cmds[]`) 안의 빈칸 아닌 자리인지, `_IOC_DIR` · `_IOC_SIZE` 가 그 자리의 번호와 같은지 하나씩 검사한다. 하나라도 다르면 `-ENOTTY`. (거절 수는 이미 짜여 있는 코드가 `bad_cmds` 에 센다.)
- A2. `kecho_get_stats()` — 구조체를 0으로 채우고, 락 안에서 스냅숏을 담고, 락 밖에서 `copy_to_user`. 실패하면 `-EFAULT`.
- A3. `kecho_set_mode()` 와 `kecho_reset()` — `get_user` 로 먼저 복사한 뒤 범위를 검사(`-EINVAL`)하고 저장한다. RESET은 데이터와 통계 6개를 0으로.
- 테스트(`ktest`)는 먼저 세 번호를 비트 단위로 잘라 매크로 결과·손으로 계산한 상수와 비교하고, `sizeof/offsetof` 를 확인한 뒤 write → GET_STATS → SET_MODE → read 순서로 값을 정확히 맞춘다.

### Part B — 틀린 입력 세 종류

| 입력 | 기대 errno | 왜 |
|---|---|---|
| 크기만 다름 `_IOR('x',1,32 B 구조체)`, 방향만 다름 `_IOW('x',1,...)`, `_IO('x',1)`, 없는 nr 9 · 0, 다른 magic `'y'` | `ENOTTY` (25) | "그런 명령 모른다" |
| `TCGETS`(0x5401), `isatty(fd)` | `ENOTTY` | tty 명령이 우리 장치에 온 것 |
| SET_MODE 값 2, 0xffffffff | `EINVAL` (22) | 명령은 알지만 값이 틀림 |
| GET_STATS에 NULL, `(void *)1`, 끝 24 B가 `PROT_NONE` 인 주소, 읽기 전용 페이지 / SET_MODE에 NULL, `PROT_NONE` | `EFAULT` (14) | 유저 메모리 접근 실패 |

거절 후 상태가 변하지 않았는지(`mode` 유지, `bad_cmds == 8`), 크기 틀린 호출 뒤 유저 버퍼 바로 뒤의 **canary** 가 그대로인지도 본다.

### Part C — 32비트 앱과 ABI (bring-up 시나리오)

보드 BSP를 64비트 커널로 올렸더니 고객의 **옛 32비트 ARM 앱**만 ioctl이 전부 실패한다는 버그 리포트가 왔다고 하자.

- C1. `.compat_ioctl` 을 채워서 `ktest32` 가 `ktest` 와 같은 결과(53/53)를 내게 한다.
- C2. `abi_demo.sh` 로 같은 구조체를 arm64/armhf/x86_64/i386 로 컴파일해 pahole 레이아웃과 `_IOR` 번호를 비교한다: 명시적 `__pad` 를 빼면, `long`/포인터를 넣으면 무엇이 달라지나?
- C3. `check.sh demo nocheck` — A1의 검사를 뺀 드라이버(nr 만 보고 분기)가 크기 틀린 호출과 `TCGETS` 에서 무엇을 하는지 본다.

## 2. Problem (English)

> Add three ioctls to a small character driver `/dev/kecho` (4 KiB buffer, mutex-protected):
>
> ```c
> #define KECHO_IOC_GET_STATS _IOR('x', 1, struct kecho_stats)  /* 40-byte struct, __u64/__u32 + explicit __pad */
> #define KECHO_IOC_SET_MODE  _IOW('x', 2, __u32)               /* 0 = RAW, 1 = UPPER */
> #define KECHO_IOC_RESET     _IO('x', 3)                       /* clear data + counters, keep mode */
>
> static int  kecho_check_cmd(unsigned int cmd);                 /* 0 or -ENOTTY */
> static long kecho_get_stats(struct kecho_dev *d, void __user *argp);
> static long kecho_set_mode(struct kecho_dev *d, void __user *argp);
> static long kecho_reset(struct kecho_dev *d);
> ```
>
> Requirements: reject any command whose type, nr, direction or size differs from the driver's own definition with `-ENOTTY`; return `-EINVAL` for an unknown mode and `-EFAULT` for an unusable user pointer; never leak uninitialized kernel bytes; the struct must have the same size and offsets on arm64, arm32, x86_64 and i386; a 32-bit ARM process on the 64-bit kernel must get identical results.
>
> Example (guest, both ABIs): `GET_STATS` after `write("hello")` returns `nwrites=1 bytes_written=5 data_len=5 __pad=0`; `ioctl(fd, _IOR('x', 1, struct v0 /* 32 B */), p)` fails with `ENOTTY` and the byte after `*p` is untouched; `ioctl(fd, KECHO_IOC_GET_STATS, NULL)` fails with `EFAULT`.
>
> Follow-ups: why is `ENOTTY` (not `EINVAL`) right for an unknown command? Why does a struct with `unsigned long` need a hand-written `compat_ioctl`? What breaks if the driver switches on `_IOC_NR(cmd)` without checking the size?

## 3. 시작 코드

파일: 공유 UAPI 헤더 `kecho_ioctl.h`, 모듈(`starter.c` · `solution.c` · `practice.c` — `Makefile` 이 `VARIANT` 를 `kecho.c` 로 복사해 `kecho.ko` 를 만든다), 게스트 테스트 `ktest.c`, 판정 스크립트 `check.sh`. Mac에서 한 줄씩:

```bash
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B08/check.sh starter
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B08/check.sh solution
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B08/check.sh demo nocheck
```

`check.sh` 는 VM 안에서 `/tmp/ELX-B08_work/<variant>.$$` 로 복사 → `make -C /var/lab/out/linux-build M=... modules`(`ccflags-y := -Werror`) → `gcc -static` 과 `arm-linux-gnueabihf-gcc -static` 으로 `ktest`/`ktest32`(`-std=c11 -Wall -Wextra -Werror -O2`) → initramfs → `timeout 170 qemu-system-aarch64 ... LAB_CMD=/test.sh` → `PASS:/FAIL:` 개수 + `dmesg` 에 `BUG:|WARNING:|Oops|Call trace|refcount_t` 0줄로 판정한다. 이 VM에서 한 번에 4–7초(TCG, 시간은 참고용)였다.

<!-- code: kecho_ioctl.h -->

<!-- code: ktest.c -->

<!-- code: check.sh -->

<!-- code: Makefile -->

<!-- code: starter.c -->

starter 실측: `[a64] 29/53 passed`, `[a32] 26/53 passed`, `55/106 passed`, `RESULT: FAIL (starter)`, exit 1 (전체 출력 `starter_result.txt`). a32 쪽은 `.compat_ioctl` 이 없어서 RESET·SET_MODE 같은 **정상 호출까지** 실패한다.

## 4. 힌트

<details><summary>힌트 1 — 네 필드를 무엇과 비교하나</summary>

`kecho_cmds[nr]` 에 그 nr의 **번호 전체**가 들어 있다. 그러니 `_IOC_DIR(cmd) == _IOC_DIR(kecho_cmds[nr])`, `_IOC_SIZE(cmd) == _IOC_SIZE(kecho_cmds[nr])` 처럼 같은 매크로로 양쪽을 자르면 된다. 순서는 type → nr 범위(배열 밖이면 인덱싱 전에 거절) → 빈칸(nr 0) → dir → size.
</details>

<details><summary>힌트 2 — GET_STATS 의 세 줄 습관</summary>

`memset(&st, 0, sizeof(st))` → `mutex_lock` 안에서 필드 대입 → `mutex_unlock` 뒤 `copy_to_user(argp, &st, sizeof(st))`. `copy_to_user` 는 **못 복사한 바이트 수**를 돌려준다(0이 성공). 0이 아니면 `-EFAULT`.
</details>

<details><summary>힌트 3 — SET_MODE 는 "복사 먼저, 검사 나중"</summary>

`get_user(mode, (u32 __user *)argp)` 가 0이 아니면 `-EFAULT`. 커널 변수 `mode` 로 가져온 **다음에** `mode > KECHO_MODE_MAX` 를 검사한다. 유저 메모리를 두 번 읽지 않는다.
</details>

<details><summary>힌트 4 — 32비트 앱이 전부 ENOTTY 일 때</summary>

`fs/ioctl.c` 의 `COMPAT_SYSCALL_DEFINE3(ioctl, ...)` 끝부분을 보라. `f_op->compat_ioctl` 이 없으면 `-ENOIOCTLCMD` 가 그대로 `-ENOTTY` 로 바뀐다. 우리 구조체는 모든 ABI에서 같으므로 커널이 주는 일반 헬퍼 하나면 된다.
</details>

## 5. 풀이

### A1 — 번호 검사: 네 필드를 따로 본다

```c
static int kecho_check_cmd(unsigned int cmd)
{
	unsigned int nr = _IOC_NR(cmd);

	if (_IOC_TYPE(cmd) != KECHO_IOC_MAGIC)
		return -ENOTTY;
	if (nr >= ARRAY_SIZE(kecho_cmds) || !kecho_cmds[nr])
		return -ENOTTY;
	if (_IOC_DIR(cmd) != _IOC_DIR(kecho_cmds[nr]))
		return -ENOTTY;
	if (_IOC_SIZE(cmd) != _IOC_SIZE(kecho_cmds[nr]))
		return -ENOTTY;
	return 0;
}
```

매크로 정의는 `include/uapi/asm-generic/ioctl.h` 그대로다: `_IOC_NRSHIFT 0`, `_IOC_TYPESHIFT 8`, `_IOC_SIZESHIFT 16`, `_IOC_DIRSHIFT 30`, `_IOC_NONE 0U / _IOC_WRITE 1U / _IOC_READ 2U`. arm64·arm·x86은 이 일반 정의를 쓰고, powerpc는 `arch/powerpc/include/uapi/asm/ioctl.h` 에서 `_IOC_SIZEBITS 13`, `_IOC_DIRBITS 3`, `_IOC_NONE 1U`, `_IOC_WRITE 4U` 로 바꾼다. 그래서 **같은 매크로라도 아키텍처마다 숫자가 다를 수 있고**, 드라이버는 숫자를 하드코딩하지 말고 항상 매크로로 비교해야 한다.

| 필드 | 틀리면 무슨 뜻 | 테스트 |
|---|---|---|
| type | 다른 드라이버(또는 tty) 번호가 잘못 왔다 | `BAD_TYPE`('y'), `TCGETS`, `isatty` |
| nr | 모르는 명령 (nr 0은 비워 둔 칸) | `BAD_NR`(9), `BAD_NR0` |
| dir | 같은 nr을 `_IOR` 대신 `_IOW`/`_IO` 로 빌드 | `BAD_DIR`, `BAD_NONE` |
| size | **다른 버전·다른 ABI** 의 구조체로 빌드된 앱 | `BAD_SIZE`(32 B) |

커널 쪽 `_IOC_TYPECHECK` 는 `include/asm-generic/ioctl.h` 에서 `sizeof(t) < (1 << _IOC_SIZEBITS)` 가 아니면 링크 에러를 내게 만들어져 있다(크기 필드 14비트 = 최대 16383 B). 그리고 한 가지 함정: `_IOR(...)` 의 값은 `sizeof` 때문에 C 타입이 `unsigned long` 이다. 처음 빌드에서 `pr_info("%x", KECHO_IOC_GET_STATS)` 가 `-Werror=format=` 으로 실패했고 `(unsigned int)` 캐스트로 고쳤다. `unlocked_ioctl` 의 `cmd` 인자는 `unsigned int` 다.

### A2 — GET_STATS: 0으로 채우고, 스냅숏, 락 밖에서 복사

```c
	memset(&st, 0, sizeof(st));
	mutex_lock(&d->lock);
	st.bytes_written = d->bytes_written;   /* ... 7개 필드 */
	mutex_unlock(&d->lock);
	if (copy_to_user(argp, &st, sizeof(st)))
		return -EFAULT;
```

- `memset` 은 `__pad` 와 (있다면) 컴파일러가 넣은 구멍까지 0으로 만든다. 안 하면 그 자리에 **커널 스택에 남아 있던 값**이 유저에게 간다(정보 누출 — 실제 CVE가 많은 패턴, E6 노트 §6.3). 테스트는 유저 버퍼를 `0xA5` 로 채운 뒤 `__pad == 0` 을 확인한다. starter는 아무것도 복사하지 않으므로 `0xA5A5A5A5` 가 남아 FAIL.
- 스냅숏은 7개 필드를 **한 시점** 값으로 묶는다. 유저 복사는 페이지 폴트로 잠들 수 있으니 락은 짧게 쥐고 밖에서 복사한다(스핀락이면 아예 금지 — ELX-M04).

### A3 — SET_MODE / RESET 과 errno 고르기

```c
	if (get_user(mode, (u32 __user *)argp))
		return -EFAULT;
	if (mode > KECHO_MODE_MAX)
		return -EINVAL;
```

| 상황 | errno | 근거 |
|---|---|---|
| 번호를 모름 (네 필드 중 하나라도 다름) | `-ENOTTY` | 관례. `fs/ioctl.c` `vfs_ioctl()` 도 `unlocked_ioctl` 이 없으면 `-ENOTTY`, 드라이버가 `-ENOIOCTLCMD` 를 주면 `-ENOTTY` 로 바꾼다 |
| 번호는 알지만 값이 틀림 | `-EINVAL` | 유저 라이브러리가 "지원 안 함"과 "호출 잘못"을 구분한다 |
| 유저 포인터로 복사 실패 | `-EFAULT` | `get_user`/`copy_*_user` 가 실패를 알려 준다 |
| (추가 연습) 읽기 전용 fd로 상태 변경 | `-EBADF` | `f->f_mode & FMODE_WRITE` 검사 |

RESET은 `_IO` 라서 arg를 아예 보지 않는다 — 테스트는 일부러 `(void *)1` 을 넘기고 0을 확인한다.

### 실측 — 정상 경로 (solution, 게스트 로그 `solution_guest_log.txt` 에서 발췌)

```text
lab initramfs: 6.18.55 up 1.05s
crw-------    1 0        0         510,   0 Nov 15 23:12 /dev/kecho
510 kecho
[    1.197390] kecho: /dev/kecho 510:0 GET_STATS=0x80287801 SET_MODE=0x40047802 RESET=0x00007803
name                 value      dir size type nr  bits(dir|size|type|nr)
KECHO_IOC_GET_STATS  0x80287801 2   40   'x'  1   10 00000000101000 01111000 00000001
KECHO_IOC_SET_MODE   0x40047802 1   4    'x'  2   01 00000000000100 01111000 00000010
KECHO_IOC_RESET      0x00007803 0   0    'x'  3   00 00000000000000 01111000 00000011
ABI a64: sizeof(long)=8 sizeof(void *)=8 sizeof(struct kecho_stats)=40
  offsetof: bytes_written=0 bytes_read=8 nwrites=16 nreads=20 data_len=24 mode=28 bad_cmds=32 __pad=36
…
[a64] 53/53 passed
…
ABI a32: sizeof(long)=4 sizeof(void *)=4 sizeof(struct kecho_stats)=40
  offsetof: bytes_written=0 bytes_read=8 nwrites=16 nreads=20 data_len=24 mode=28 bad_cmds=32 __pad=36
[a32] 53/53 passed
DMESG_BAD=0
```

`check.sh solution` 의 마지막 줄은 `106/106 passed, dmesg bad lines: 0` / `RESULT: PASS (solution)` 이고, 연속 3회 모두 exit 0이었다. major 510은 `alloc_chrdev_region` 이 동적으로 준 값이다. **a32 에서 `sizeof(long)=4` 인데도 구조체는 40 B, 오프셋도 같다** — 이것이 Part C의 핵심이다.

### Part B — 나쁜 포인터가 Oops가 아니라 EFAULT가 되는 이유

`copy_to_user(NULL...)`, `(void *)1`, 읽기 전용 페이지는 모두 커널이 유저 주소에 쓰다가 페이지 폴트를 낸다. arm64의 `__arch_copy_to_user` 는 예외 테이블(`__ex_table`)에 "여기서 폴트 나면 fixup으로 가라"가 등록돼 있어서 커널이 죽지 않고 "못 복사한 바이트 수"를 돌려준다(원리와 실측은 E6 노트 §4). 끝 24 B가 `PROT_NONE` 인 주소(`pg + 4096 - 16`)는 앞 16 B까지는 복사될 수 있고 나머지에서 멈추므로 반환값이 0이 아니다 → 드라이버는 `-EFAULT`. (앞 16 B가 실제로 쓰였는지는 이 레슨에서 확인하지 않았다.) 나쁜 포인터 6개 전부 `dmesg` 경고 0줄이었다.

isatty에는 작은 반전이 있었다. 처음엔 `isatty()` 가 `TCGETS`(0x5401)를 쓸 줄 알았는데, 이 VM의 glibc 2.43을 `strace` 로 보니 다른 번호였다:

```text
$ strace -e trace=ioctl ./t 3</dev/null      # (VM에서 한 번 돌린 기록) t.c = printf("%d\n", isatty(3));
ioctl(3, TCGETS2, 0xffffebdcdb78)       = -1 ENOTTY (Inappropriate ioctl for device)
```

`TCGETS2` 는 `_IOR('T', 0x2A, struct termios2)` = `0x802c542a`(termios2 = 44 B, VM에서 출력해 확인)이고, 옛 `TCGETS` 는 dir=0, size=0 인 **ioctl 매크로 이전 시대 번호** 0x00005401이다. 그래서 ktest는 `TCGETS` 를 따로 직접 부르는 검사도 둔다. 두 번호 모두 우리 드라이버에서는 type `'T'` 라서 `-ENOTTY`, 그래서 `bad_cmds == 8`(6 + TCGETS + TCGETS2).

### Part C — 32비트 앱의 길: compat_sys_ioctl → compat_ptr_ioctl

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">같은 /dev/kecho, 두 가지 진입 경로 (fs/ioctl.c, 6.18.55)</text>
<rect x="10" y="40" width="130" height="40" class="f-acc" stroke="currentColor"/>
<text x="20" y="65" fill="currentColor">ktest (AArch64)</text>
<rect x="170" y="40" width="160" height="40" class="f-mut" stroke="currentColor"/>
<text x="180" y="65" fill="currentColor">sys_ioctl</text>
<rect x="360" y="40" width="160" height="40" class="f-mut" stroke="currentColor"/>
<text x="370" y="65" fill="currentColor">do_vfs_ioctl → vfs_ioctl</text>
<rect x="560" y="40" width="170" height="40" class="f-ok" stroke="currentColor"/>
<text x="570" y="65" fill="currentColor">kecho_ioctl(cmd, arg)</text>
<line x1="140" y1="60" x2="170" y2="60" stroke="currentColor"/>
<line x1="330" y1="60" x2="360" y2="60" stroke="currentColor"/>
<line x1="520" y1="60" x2="560" y2="60" stroke="currentColor"/>
<rect x="10" y="120" width="130" height="40" class="f-acc" stroke="currentColor"/>
<text x="20" y="145" fill="currentColor">ktest32 (AArch32)</text>
<rect x="170" y="120" width="160" height="40" class="f-mut" stroke="currentColor"/>
<text x="180" y="145" fill="currentColor">compat_sys_ioctl</text>
<rect x="360" y="120" width="160" height="40" class="f-mut" stroke="currentColor"/>
<text x="370" y="145" fill="currentColor">f_op-&gt;compat_ioctl</text>
<rect x="560" y="120" width="170" height="40" class="f-ok" stroke="currentColor"/>
<text x="570" y="137" fill="currentColor">compat_ptr_ioctl</text>
<text x="570" y="153" fill="currentColor" font-size="12">arg = compat_ptr(arg)</text>
<line x1="140" y1="140" x2="170" y2="140" stroke="currentColor"/>
<line x1="330" y1="140" x2="360" y2="140" stroke="currentColor"/>
<line x1="520" y1="140" x2="560" y2="140" stroke="currentColor"/>
<line x1="645" y1="120" x2="645" y2="80" stroke="currentColor" stroke-dasharray="4 3"/>
<text x="638" y="105" fill="currentColor" font-size="12" text-anchor="end">같은 unlocked_ioctl 로</text>
<rect x="300" y="190" width="430" height="40" class="f-warn" stroke="currentColor"/>
<text x="310" y="208" fill="currentColor">starter: compat_ioctl == NULL</text>
<text x="310" y="224" fill="currentColor" font-size="12">-ENOIOCTLCMD → -ENOTTY: 정상 호출도 실패 (a32 26/53)</text>
<line x1="440" y1="160" x2="440" y2="190" stroke="currentColor" stroke-dasharray="4 3"/>
<text x="10" y="260" fill="currentColor" font-size="12">compat_ptr(): arm64 에선 0-확장 캐스트뿐 (include/linux/compat.h).</text>
<text x="10" y="280" fill="currentColor" font-size="12">s390 만 최상위 비트를 지운다 (fs/ioctl.c 의 compat_ptr_ioctl 주석).</text>
<text x="10" y="300" fill="currentColor" font-size="12">구조체가 ABI 독립이면 compat_ptr_ioctl 한 줄, 아니면 직접 변환하는 compat_ioctl 을 짠다.</text>
</svg>
```

`COMPAT_SYSCALL_DEFINE3(ioctl)` 의 `default:` 는 먼저 `do_vfs_ioctl(..., compat_ptr(arg))` 로 공통 명령(FIOCLEX 등)을 처리하고, `-ENOIOCTLCMD` 면 `f_op->compat_ioctl` 을 부르고, 그것도 없으면 `-ENOTTY` 로 바꾼다. `compat_ptr_ioctl` 본체는 두 줄이다:

```c
long compat_ptr_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	if (!file->f_op->unlocked_ioctl)
		return -ENOIOCTLCMD;
	return file->f_op->unlocked_ioctl(file, cmd, (unsigned long)compat_ptr(arg));
}
```

소스 주석 그대로: "must therefore be used only with ioctl functions that either ignore the argument or pass a pointer to a compatible data type". 즉 **번호(크기 포함)와 구조체 레이아웃이 32/64비트에서 같을 때만** 쓸 수 있다. 그 조건을 pahole로 확인한 것이 `abi_demo.sh` 다(전체 출력 `abi_demo.txt`):

```bash
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B08/abi_demo.sh
```

<!-- code: abi_layout.c -->

같은 `abi_layout.c` 를 `gcc`(arm64), `arm-linux-gnueabihf-gcc`, `x86_64-linux-gnu-gcc`, `x86_64-linux-gnu-gcc -m32`(i386)로 `-g -c` 만 하고 pahole과 `.rodata` 의 `_IOR('x',1,T)` 값을 읽었다:

| 구조체 | arm64 | armhf | x86_64 | i386 |
|---|---|---|---|---|
| `kecho_stats` (명시적 `__pad`) | 40 B, `0x80287801` | 40, `0x80287801` | 40, `0x80287801` | 40, `0x80287801` |
| `kecho_stats_nopad` (`__pad` 없음) | 40 (`padding: 4`) | 40 | 40 | **36**, `0x80247801` |
| `kecho_stats_bad` (`u32` + `unsigned long` + `void *` + `u64`) | 32, `0x80207801` | **24**, `0x80187801` | 32 | **20**, `0x80147801` |

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
<text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">kecho_stats_bad 의 레이아웃 (pahole 실측, 1 B = 12 px)</text>
<text x="10" y="52" fill="currentColor">arm64 (32 B)</text>
<rect x="110" y="35" width="48" height="26" class="f-acc" stroke="currentColor"/>
<text x="114" y="52" fill="currentColor">mode</text>
<rect x="158" y="35" width="48" height="26" class="f-mut" stroke="currentColor" stroke-dasharray="3 3"/>
<text x="162" y="52" fill="currentColor">hole</text>
<rect x="206" y="35" width="96" height="26" class="f-warn" stroke="currentColor"/>
<text x="210" y="52" fill="currentColor">long @8</text>
<rect x="302" y="35" width="96" height="26" class="f-warn" stroke="currentColor"/>
<text x="306" y="52" fill="currentColor">ptr @16</text>
<rect x="398" y="35" width="96" height="26" class="f-ok" stroke="currentColor"/>
<text x="402" y="52" fill="currentColor">u64 @24</text>
<text x="10" y="102" fill="currentColor">armhf (24 B)</text>
<rect x="110" y="85" width="48" height="26" class="f-acc" stroke="currentColor"/>
<text x="114" y="102" fill="currentColor">mode</text>
<rect x="158" y="85" width="48" height="26" class="f-warn" stroke="currentColor"/>
<text x="160" y="102" fill="currentColor">long@4</text>
<rect x="206" y="85" width="48" height="26" class="f-warn" stroke="currentColor"/>
<text x="208" y="102" fill="currentColor">ptr@8</text>
<rect x="254" y="85" width="48" height="26" class="f-mut" stroke="currentColor" stroke-dasharray="3 3"/>
<text x="258" y="102" fill="currentColor">hole</text>
<rect x="302" y="85" width="96" height="26" class="f-ok" stroke="currentColor"/>
<text x="306" y="102" fill="currentColor">u64 @16</text>
<text x="10" y="152" fill="currentColor">i386 (20 B)</text>
<rect x="110" y="135" width="48" height="26" class="f-acc" stroke="currentColor"/>
<text x="114" y="152" fill="currentColor">mode</text>
<rect x="158" y="135" width="48" height="26" class="f-warn" stroke="currentColor"/>
<text x="160" y="152" fill="currentColor">long@4</text>
<rect x="206" y="135" width="48" height="26" class="f-warn" stroke="currentColor"/>
<text x="208" y="152" fill="currentColor">ptr@8</text>
<rect x="254" y="135" width="96" height="26" class="f-ok" stroke="currentColor"/>
<text x="258" y="152" fill="currentColor">u64 @12</text>
<line x1="110" y1="180" x2="494" y2="180" stroke="currentColor"/>
<text x="106" y="196" fill="currentColor">0</text>
<text x="200" y="196" fill="currentColor">8</text>
<text x="296" y="196" fill="currentColor">16</text>
<text x="392" y="196" fill="currentColor">24</text>
<text x="488" y="196" fill="currentColor">32</text>
<text x="510" y="52" fill="currentColor">→ _IOR 번호 0x80207801</text>
<text x="510" y="102" fill="currentColor">→ 0x80187801 (번호부터 다름)</text>
<text x="510" y="152" fill="currentColor">→ 0x80147801</text>
<text x="10" y="225" fill="currentColor">u64 정렬: arm64·armhf(AAPCS)·x86_64 = 8, i386 = 4 → __pad 없는 40 B 구조체가 i386 에서만 36 B.</text>
<text x="10" y="243" fill="currentColor">i386 pahole 출력의 __attribute__((__packed__)) 는 pahole 의 추정 표기이고, 소스는 packed 가 아니다.</text>
</svg>
```

읽는 법:

1. **번호에 크기가 들어 있으므로**, 레이아웃이 달라지면 번호부터 달라진다. armhf 앱이 `kecho_stats_bad` 로 빌드되면 `0x80187801` 을 보내고, 64비트 드라이버는 `0x80207801` 만 알므로 `-ENOTTY` — `compat_ptr_ioctl` 로는 못 고친다. 직접 `struct kecho_stats_bad32` 를 정의하고 필드별로 옮기는 `compat_ioctl` 을 짜야 한다.
2. 크기가 우연히 같아도 위험하다. 오프셋이 다르면 커널이 엉뚱한 바이트를 필드로 읽는다.
3. `__pad` 를 빼도 ARM 두 개는 40 B로 같다(AAPCS도 `u64` 를 8에 정렬). **ARM끼리만 보면 버그가 안 보이다가 i386 에서 터진다.** 그래서 꼬리 패딩도 명시한다.

### Part C3 — 검사를 빼면: nr 만 보고 분기하는 드라이버

`check.sh demo nocheck` 는 solution의 `ret = kecho_check_cmd(cmd);` 한 줄을 sed로 `ret = 0;` 으로 바꿔 빌드한다(`switch (_IOC_NR(cmd))` 는 그대로). 실측(`demo_nocheck.txt`, a32도 같은 9줄):

```text
FAIL: [a64] io(fd, BAD_SIZE, &cb.v0) == -ENOTTY  (line 158)
FAIL: [a64] cb.canary == 0x1122334455667788ULL  (line 159)
FAIL: [a64] io(fd, BAD_DIR, &st) == -ENOTTY  (line 160)
FAIL: [a64] io(fd, BAD_NONE, &st) == -ENOTTY  (line 161)
FAIL: [a64] io(fd, BAD_TYPE, &st) == -ENOTTY  (line 164)
FAIL: [a64] io(fd, TCGETS, &tb) == -ENOTTY  (line 166)
FAIL: [a64] tb.canary == 0xdeadbeefu  (line 167)
FAIL: [a64] get_stats(fd, &st) == 0 && st.bad_cmds == 8  (line 170)
FAIL: [a64] get_stats(fd, &st) == 0 && st.mode == KECHO_MODE_UPPER && st.bad_cmds == 8  (line 182)
[a64] 44/53 passed
…
[a32] 44/53 passed
DMESG_BAD=0
```

- `BAD_SIZE`: 유저는 32 B 구조체를 줬는데 드라이버가 40 B를 써서 **바로 뒤 canary 를 덮었다**. 커널 로그는 깨끗하다(`DMESG_BAD=0`) — 망가진 쪽은 유저 메모리라 커널은 모른다.
- `TCGETS`: 0x5401 의 nr도 1이다. 드라이버가 tty 명령을 GET_STATS로 착각해 36 B짜리 termios 자리에 40 B를 쓰고 canary를 깼다.
- `isatty` 는 여기서도 0을 돌려줬다 — glibc 2.43 은 nr 0x2A 인 `TCGETS2` 를 쓰고 그 nr은 `default: -ENOTTY` 로 떨어지기 때문. 옛 glibc라면 `isatty()` 가 1(터미널이다!)을 돌려줬을 것이다(이 부분은 추론, 실행 안 함).

`switch (cmd)` 처럼 **번호 전체**로 분기하면 이 버그는 저절로 없다. nr로 분기해야 할 때(구조체 크기 여러 버전을 받는 G5 노트의 `copy_struct_from_user` 패턴)에는 반드시 type·dir을 먼저 확인하고 크기는 `_IOC_SIZE` 로 받아 처리한다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B08/check.sh solution
```

실측: `106/106 passed, dmesg bad lines: 0`, `RESULT: PASS (solution)`, exit 0 (3회 연속).

## 7. 자주 하는 실수

**1. 모르는 명령에 `-EINVAL`.** 유저 쪽은 `ENOTTY` 를 보고 "이 장치는 그 기능이 없다"고 판단해 다른 경로로 간다. `EINVAL` 이면 "내가 인자를 잘못 줬나"로 오해한다. → `default: return -ENOTTY;`

**2. 구조체에 `long`, `size_t`, 포인터, `enum`, `bool`.** 32비트 앱에서 크기·오프셋이 달라진다(위 표: 32 → 24 → 20 B). → `__u32/__u64` 만, 유저 포인터는 `__u64 buf_ptr` + 커널에서 `u64_to_user_ptr()` (추가 연습 P3).

**3. 꼬리 패딩을 컴파일러에 맡김.** arm64/armhf에서 40 B라 테스트가 통과하지만 i386에선 36 B. → `__u32 __pad` 를 명시하고 커널은 0으로 채운다(읽는 방향이면 0인지 검사해 미래 확장용으로 예약).

**4. `switch (_IOC_NR(cmd))` 만 하고 크기·방향·type 검사 없음.** Part C3의 canary 파괴. → 번호 전체로 분기하거나 `kecho_check_cmd()` 처럼 먼저 검사.

**5. 검사하고 나서 유저 메모리를 다시 읽음 (double fetch).**

```c
/* 틀림: 검사한 값과 쓰는 값이 다를 수 있다 (다른 스레드가 사이에 바꾼다) */
if (get_user(m, p) || m > KECHO_MODE_MAX) return -EINVAL;
if (get_user(d->mode, p)) return -EFAULT;
/* 맞음: 한 번 복사한 커널 변수만 검사하고 쓴다 */
if (get_user(m, p)) return -EFAULT;
if (m > KECHO_MODE_MAX) return -EINVAL;
d->mode = m;
```

**6. `memset` 없이 스택 구조체를 `copy_to_user`.** 패딩·안 채운 필드로 커널 스택이 샌다. → `memset` 또는 `= {}`.

**7. `.compat_ioctl` 누락.** 64비트 테스트만 돌리면 영원히 모른다. starter의 a32 결과처럼 정상 호출까지 `ENOTTY`. → ABI 독립 구조체 + `compat_ptr_ioctl`, 그리고 CI에 32비트 테스트 바이너리.

## 8. 면접 Q&A

<details><summary>Q1. ioctl 번호 32비트는 어떻게 생겼나? _IOR의 R은 누구 기준인가?</summary>

`dir(31:30) | size(29:16) | type(15:8) | nr(7:0)` (asm-generic 기준). R/W는 **유저 기준**: `_IOR` 은 유저가 읽는다 = 커널이 `copy_to_user`. 예: `_IOR('x', 1, 40 B 구조체)` = `0x80287801`.

꼬리질문: 모든 아키텍처가 같나? → 아니다. powerpc는 size 13비트, dir 3비트이고 `_IOC_NONE` 이 1이다(`arch/powerpc/include/uapi/asm/ioctl.h`). 그래서 숫자를 하드코딩하지 않고 매크로로 비교한다.
</details>

<details><summary>Q2. ENOTTY, EINVAL, EFAULT는 각각 언제 돌려주나?</summary>

ENOTTY = 그 번호를 모른다(type/nr/dir/size 중 하나라도 다름). EINVAL = 아는 명령인데 값이 틀림(모르는 mode, 예약 필드가 0 아님). EFAULT = `copy_*_user`/`get_user`/`put_user` 실패. 내부의 `-ENOIOCTLCMD` 는 VFS가 `-ENOTTY` 로 바꿔 유저에게 준다(`fs/ioctl.c` `vfs_ioctl`).

꼬리질문: 권한이 없으면? → `-EPERM`(`capable()` 실패) 또는 fd 모드가 안 맞으면 `-EBADF` (추가 연습 P4).
</details>

<details><summary>Q3. 32/64비트 둘 다에서 안 깨지는 ioctl 구조체를 설계해 보라.</summary>

고정 폭 타입만, 큰 필드부터 또는 명시적 패딩으로 구멍과 꼬리 패딩을 없앤다, 포인터는 `__u64`, 예약 필드는 0 검사, 크기는 pahole로 모든 타깃에서 확인. 그러면 `.compat_ioctl = compat_ptr_ioctl` 하나로 32비트 앱을 받는다. 이 레슨에서 `kecho_stats` 는 arm64/armhf/x86_64/i386 모두 40 B, 번호 `0x80287801` 로 같았다.

꼬리질문: 구조체에 `struct timespec` 이나 `long` 이 꼭 있어야 하면? → `compat_ptr_ioctl` 불가. 32비트 레이아웃 구조체를 따로 정의하고 필드별로 변환하는 `.compat_ioctl` 을 짠다(번호도 32비트 크기로 따로 계산해서 매칭).
</details>

<details><summary>Q4. __pad 없이도 ARM 32/64에서 크기가 같았다. 그럼 왜 넣나?</summary>

AAPCS(armhf)는 `u64` 를 8에 정렬해 arm64와 같지만, i386 SysV ABI는 구조체 안의 `u64` 를 4에 정렬한다. 그래서 `__pad` 없는 구조체가 i386에서만 36 B → 번호 `0x80247801` 로 달라진다(pahole 실측). x86_64 커널에서 i386 앱을 받을 때 실제로 터지는 버그다. 명시적 패딩은 이식성 + "커널이 0으로 채운다"는 정보 누출 방지도 준다.

꼬리질문: `__attribute__((packed))` 로 해결하면? → 정렬 안 된 `u64` 접근이 생기고(일부 CPU에서 느리거나 폴트) 커뮤니티 UAPI 관례에도 어긋난다. 패딩을 명시하는 쪽이 정석.
</details>

<details><summary>Q5. 드라이버가 switch (_IOC_NR(cmd)) 로만 분기하면 무슨 일이 생기나?</summary>

크기가 다른 구조체로 빌드된 앱이나, 같은 nr을 쓰는 남의 명령(`TCGETS` = 0x5401 도 nr 1)이 들어와도 GET_STATS로 처리된다. 실측에서 32 B 버퍼 뒤의 canary와 36 B termios 뒤의 canary가 모두 덮였다. 커널 로그에는 아무것도 안 남는다. 번호 전체로 `switch (cmd)` 하거나 네 필드를 먼저 검사한다.

꼬리질문: 일부러 크기를 여러 개 받고 싶으면? → type/dir 확인 후 `_IOC_SIZE(cmd)` 를 `copy_struct_from_user(dst, ksize, src, usize)` 에 넘겨 작은 건 0으로 채우고, 큰 건 꼬리가 0인지 검사(`-E2BIG`). G5 노트의 확장 패턴.
</details>

<details><summary>Q6. copy_to_user에 NULL을 넘기면 커널이 왜 죽지 않나?</summary>

`copy_to_user` 의 유저 접근 명령은 예외 테이블에 fixup이 등록돼 있다. 폴트가 나면 페이지 폴트 핸들러가 테이블을 찾아 fixup으로 점프하고, 함수는 "못 복사한 바이트 수"를 돌려준다. 드라이버는 0이 아니면 `-EFAULT`. arm64는 PAN(`CONFIG_ARM64_PAN=y`)도 켜져 있어 커널이 `*uptr` 로 직접 유저 메모리를 읽으면 폴트가 난다 — 반드시 uaccess 함수로 접근해야 한다(E6 노트 §4).

꼬리질문: 일부만 복사된 경우는? → 반환값이 남은 바이트 수. ioctl에서는 보통 전부 아니면 실패로 보고 `-EFAULT`, read()라면 복사된 만큼을 돌려주는 설계도 있다(B07).
</details>

<details><summary>Q7. compat_ptr_ioctl은 정확히 뭘 하나? arm64에서 꼭 필요한가?</summary>

`unlocked_ioctl(file, cmd, (unsigned long)compat_ptr(arg))` 를 부를 뿐이다. arm64의 `compat_ptr()` 는 32비트 값을 0-확장하는 캐스트(`include/linux/compat.h`), s390만 최상위 비트를 지운다. 그래도 필요하다: `.compat_ioctl` 이 NULL이면 `compat_sys_ioctl` 이 `-ENOTTY` 를 돌려주기 때문(starter의 a32 26/53이 그 증거).

꼬리질문: arg가 포인터가 아니라 정수인 명령이 섞여 있으면? → 주석대로 `compat_ptr_ioctl` 은 쓰면 안 된다(정수를 포인터 변환하면 s390에서 값이 바뀐다). 직접 `compat_ioctl` 을 짜서 그 명령은 arg를 그대로 넘긴다.
</details>

<details><summary>Q8. ioctl 말고 sysfs나 netlink를 쓰는 게 나은 때는?</summary>

값 하나 읽고 쓰기(임계값, 모드)는 sysfs가 셸에서 바로 보이고 `Documentation/ABI` 로 문서화된다(ELX-B09). 비동기 이벤트·여러 구독자는 netlink/poll. ioctl은 여러 값을 **원자적으로** 주고받거나 START/STOP 같은 명령, 성능이 중요한 제어 경로에 쓴다. 이미 서브시스템(IIO, V4L2, GPIO chardev — ELX-B10)이 있으면 그 ABI를 따른다.

꼬리질문: 새 ioctl을 메인라인에 넣을 때 리뷰어가 뭘 보나? → ioctl-number.rst 등록, 고정 폭·패딩·예약 필드 0 검사, compat 처리, 확장 방법(크기/플래그), 권한 검사.
</details>

## 9. 추가 연습

모두 `practice.c` 에 들어 있고, `ktest practice` 가 기본 검사 53개 뒤에 31개를 더 돈다(번호 해부 9개 포함). 번호는 헤더에 이미 정의돼 있다: `GET_MODE 0x80047804`, `XCHG_MODE 0xc0047805`, `PEEK 0xc0107806` (게스트 실측).

**P1. `KECHO_IOC_GET_MODE` = `_IOR('x', 4, __u32)`.** 값 하나를 돌려주는 가장 짧은 _IOR.

<details><summary>답</summary>

```c
return put_user(READ_ONCE(d->mode), argp) ? -EFAULT : 0;
```
`kecho_cmds[]` 에 `[KECHO_NR_GET_MODE] = KECHO_IOC_GET_MODE` 를 추가해야 검사를 통과한다 — 표에 없으면 solution처럼 `-ENOTTY`.
</details>

**P2. `KECHO_IOC_XCHG_MODE` = `_IOWR('x', 5, __u32)`.** 새 mode를 넣고 옛 mode를 같은 자리로 돌려받는다. 돌려주기(`put_user`)가 실패하면 바꾼 것을 되돌려 "없던 일"로 만든다. 테스트: 0으로 찬 **읽기 전용** 페이지를 넘기면 `get_user` 는 성공(0)하고 `put_user` 가 실패 → `-EFAULT`, mode는 그대로 1.

<details><summary>답</summary>

`get_user` → 범위 검사(`-EINVAL`) → 락 안에서 swap → 락 밖 `put_user(old)`; 실패하면 락을 다시 잡고 `d->mode == mode` 일 때만 `old` 로 복원. 사이에 다른 스레드가 바꿨다면 덮지 않는다. _IOWR = dir 3 → `0xc0047805`.
</details>

**P3. `KECHO_IOC_PEEK` = `_IOWR('x', 6, struct kecho_peek)`.** 구조체 안에 유저 포인터(`__u64 buf_ptr`)를 싣고, 데이터의 `[offset, offset+len)` 을 그 버퍼로 복사한 뒤 실제 길이를 `len` 에 돌려준다. `offset > data_len` 이나 `len > 4096` 은 `-EINVAL`, `buf_ptr = 1` 은 `-EFAULT`.

<details><summary>답</summary>

`copy_from_user(&pk, ...)` → 검사 → mutex 안에서 `copy_to_user(u64_to_user_ptr(pk.buf_ptr), d->buf + pk.offset, n)` → `put_user(n, &argp->len)`. `u64_to_user_ptr` 는 `include/linux/util_macros.h` 의 `(void __user *)(uintptr_t)(x)` 에 `typecheck(u64, x)` 를 더한 매크로다. 32비트 앱은 `(__u64)(uintptr_t)buf` 로 0-확장된 주소를 넣으므로 **compat 변환 없이** 동작한다 — a32 84/84가 그 증거. 포인터 필드를 `void *` 로 뒀다면 armhf에서 구조체가 12 B가 돼 번호부터 달라진다.
</details>

**P4. RESET은 쓰기 권한 있는 fd만.** `O_RDONLY` 로 연 fd의 RESET은 `-EBADF`, 같은 fd의 GET_STATS는 허용.

<details><summary>답</summary>

```c
case KECHO_NR_RESET:
	if (!(f->f_mode & FMODE_WRITE))
		return -EBADF;
```
권한은 번호가 아니라 **fd(파일 모드)** 에 붙어 있다. 더 센 권한이 필요한 명령은 `capable(CAP_SYS_ADMIN)` 을 맨 앞에서 검사해 `-EPERM` (G5 노트 §3.2).
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B08/check.sh practice
```

실측: `[a64] 84/84 passed`, `[a32] 84/84 passed`, `168/168 passed, dmesg bad lines: 0`, `RESULT: PASS (practice)`, exit 0.

## 10. 핵심 정리

- ioctl 번호 = `dir(2) | size(14) | type(8) | nr(8)`, 방향은 유저 기준(`_IOR` = 커널이 `copy_to_user`). `_IOR('x',1,40 B)` = `0x80287801`.
- 드라이버는 번호의 **네 필드 전부**를 확인한다. 모르면 `-ENOTTY`, 값이 틀리면 `-EINVAL`, 유저 포인터가 나쁘면 `-EFAULT`.
- nr만 보고 분기하면 크기 다른 앱·`TCGETS` 에 유저 메모리를 덮어쓴다(canary 실측). `switch (cmd)` 가 기본.
- UAPI 구조체: `__u32/__u64` 만, 포인터는 `__u64`, 패딩 명시(꼬리 포함), 커널은 `memset` 후 채운다.
- `u64` 정렬은 arm64·armhf·x86_64 = 8, i386 = 4 → ARM끼리만 테스트하면 꼬리 패딩 버그를 못 본다. pahole로 모든 타깃 확인.
- 구조체가 ABI 독립이면 `.compat_ioctl = compat_ptr_ioctl`. 없으면 32비트 앱은 정상 호출도 `ENOTTY`.
- 유저 값은 한 번만 복사하고, 복사한 커널 변수로 검사한다(double fetch 금지).

## 참고자료

- 랩 소스 (Linux 6.18.55, `/var/lab/src/linux-6.18.55`): `include/uapi/asm-generic/ioctl.h`(필드·매크로), `include/asm-generic/ioctl.h`(`_IOC_TYPECHECK`), `arch/powerpc/include/uapi/asm/ioctl.h`, `fs/ioctl.c`(`vfs_ioctl`, `SYSCALL_DEFINE3(ioctl)`, `compat_ptr_ioctl`, `COMPAT_SYSCALL_DEFINE3(ioctl)`), `include/linux/compat.h`(`compat_ptr`), `include/linux/util_macros.h`(`u64_to_user_ptr`), `fs/read_write.c`(`generic_file_llseek_size`)
- `Documentation/userspace-api/ioctl/ioctl-number.rst`(magic 등록표), `Documentation/driver-api/ioctl.rst`("ioctl based interfaces": 고정 폭 타입, 패딩, compat)
- 선행 노트: EmbeddedLinux E6(번호 인코딩·copy_to/from_user·예외 테이블·errno), G5(ioctl UAPI 확장·`copy_struct_from_user`·compat), G4(char device)
- 이 사이트: ELX-B07(kecho read/write/ppos), ELX-B09(sysfs), ELX-B10(GPIO chardev ioctl), ELX-M04(락 선택)
- pahole(1) — dwarves; `man 2 ioctl`, `man 3 isatty`
