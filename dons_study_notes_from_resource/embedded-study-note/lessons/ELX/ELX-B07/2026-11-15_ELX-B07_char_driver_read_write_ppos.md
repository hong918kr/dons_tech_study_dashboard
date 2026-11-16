---
id: ELX-B07
time: 70분
updated: 2026-11-15
---
# ELX-B07 문자 디바이스 드라이버: file_operations read/write

## 학습 목표

- `alloc_chrdev_region` → `cdev_add` → `class_create` → `device_create` 순서로 `/dev/kecho` 를 만들고, 실패하면 거꾸로 되감는(unwind) 코드를 쓴다. 각 단계를 일부러 실패시켜(`fail_at=1..4`) 흔적이 남지 않는지 확인한다.
- `read`/`write` 가 `*ppos` 를 기준으로 동작하는 규칙(EOF = 0, short read, short write, `-ENOSPC`)을 코드로 정확히 구현하고 1 / 7 / 4096 바이트 조각으로 검증한다.
- `copy_to_user`/`copy_from_user` 의 반환값(복사 못 한 바이트 수)을 바르게 처리하고, **반쯤 실패한 복사(partial fault)** 에서 무엇을 돌려줘야 하는지 실측으로 설명한다.
- `struct file` 마다 `f_pos` 가 따로라는 것(두 번 open vs `dup` vs `fork`)과, 데이터는 장치 하나에 공유되므로 **장치당 mutex** 가 필요하다는 것을 테스트로 보인다.
- 잘못 짠 드라이버 6종(스핀락 안에서 `copy_to_user`, 락 없음, bounce 없음, 끝에서 0 반환, `*ppos` 무시, class 누수)의 **실제 커널 출력**을 읽는다.

## 1. 문제

"화이트보드에 char driver 를 짜 보세요"는 임베디드 리눅스 드라이버 면접의 단골이고, 보드 bring-up 에서 FPGA·센서·DSP 처럼 서브시스템이 없는 장치는 결국 문자 디바이스(character device)로 유저에게 보인다. 면접관이 보는 것은 등록 API 이름보다 **`*ppos`, 부분 복사, 끝 처리, 락, 에러 되감기** 를 정확히 다루는지다.

> 환경: 모든 커널 출력은 **Lima VM `elinux` 안에서 빌드 → QEMU TCG `virt` cortex-a57 `-smp 2` 512 MB 게스트의 Linux 6.18.55-dbg** (`/var/lab/out/Image-dbg`: lockdep, DEBUG_ATOMIC_SLEEP 포함) 에서 실측했다. 모듈은 게스트 안에서만 올린다. 랩 initramfs(BusyBox 1.37)는 devtmpfs 를 `/dev` 에 mount 하고 udev/mdev 는 돌지 않는다. TCG 는 실제 CPU 가 아니므로 시간 숫자는 의미가 없다.

만들 장치는 `/dev/kecho` 하나다. 4096 바이트 커널 버퍼가 있고, `write` 한 내용을 `read` 로 돌려준다. 동작은 "크기가 4 KiB 로 막힌 작은 파일"과 같다.

```svg
<svg viewBox="0 0 740 380" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">/dev/kecho: 번호 → cdev → fops → 공유 버퍼. f_pos 는 struct file 마다 하나</text>
<rect x="10" y="40" width="200" height="44" class="f-mut" stroke="currentColor"/>
<text x="20" y="58" fill="currentColor">프로세스 P: fd 3</text>
<text x="20" y="76" fill="currentColor" font-size="12">dup(3) = fd 4, fork 자식도 같은 fd</text>
<rect x="10" y="110" width="200" height="44" class="f-mut" stroke="currentColor"/>
<text x="20" y="128" fill="currentColor">프로세스 Q: open() 한 번 더</text>
<text x="20" y="146" fill="currentColor" font-size="12">완전히 새 struct file</text>
<rect x="250" y="40" width="190" height="44" class="f-acc" stroke="currentColor"/>
<text x="260" y="58" fill="currentColor">struct file #1</text>
<text x="260" y="76" fill="currentColor" font-size="12">f_pos = 10 (자식이 10 읽음)</text>
<rect x="250" y="110" width="190" height="44" class="f-acc" stroke="currentColor"/>
<text x="260" y="128" fill="currentColor">struct file #2</text>
<text x="260" y="146" fill="currentColor" font-size="12">f_pos = 5 (따로 움직임)</text>
<line x1="210" y1="62" x2="250" y2="62" stroke="currentColor"/>
<line x1="210" y1="132" x2="250" y2="132" stroke="currentColor"/>
<rect x="480" y="40" width="250" height="114" class="f-ok" stroke="currentColor"/>
<text x="490" y="60" fill="currentColor">struct kecho_dev (장치 하나)</text>
<text x="490" y="80" fill="currentColor" font-family="monospace" font-size="12">mutex lock</text>
<text x="490" y="98" fill="currentColor" font-family="monospace" font-size="12">size_t len   (최고 쓰기 위치)</text>
<text x="490" y="116" fill="currentColor" font-family="monospace" font-size="12">u8 data[4096]</text>
<text x="490" y="134" fill="currentColor" font-family="monospace" font-size="12">u8 bounce[4096]</text>
<text x="490" y="150" fill="currentColor" font-family="monospace" font-size="12">struct cdev cdev</text>
<line x1="440" y1="62" x2="480" y2="80" stroke="currentColor"/>
<line x1="440" y1="132" x2="480" y2="120" stroke="currentColor"/>
<text x="10" y="190" fill="currentColor" font-weight="bold">open 때 한 번만 일어나는 연결 (fs/char_dev.c chrdev_open)</text>
<rect x="10" y="205" width="160" height="50" class="f-mut" stroke="currentColor"/>
<text x="20" y="225" fill="currentColor">/dev/kecho</text>
<text x="20" y="245" fill="currentColor" font-size="12">crw------- 510, 0</text>
<rect x="200" y="205" width="160" height="50" class="f-mut" stroke="currentColor"/>
<text x="210" y="225" fill="currentColor">cdev_map</text>
<text x="210" y="245" fill="currentColor" font-size="12">510:0 → kecho.cdev</text>
<rect x="390" y="205" width="160" height="50" class="f-mut" stroke="currentColor"/>
<text x="400" y="225" fill="currentColor">f_op = kecho_fops</text>
<text x="400" y="245" fill="currentColor" font-size="12">모듈 참조 +2</text>
<rect x="580" y="205" width="150" height="50" class="f-mut" stroke="currentColor"/>
<text x="590" y="225" fill="currentColor">kecho_open()</text>
<text x="590" y="245" fill="currentColor" font-size="12">private_data 설정</text>
<line x1="170" y1="230" x2="200" y2="230" stroke="currentColor"/>
<line x1="360" y1="230" x2="390" y2="230" stroke="currentColor"/>
<line x1="550" y1="230" x2="580" y2="230" stroke="currentColor"/>
<text x="10" y="285" fill="currentColor" font-size="12">private_data = container_of(inode-&gt;i_cdev, struct kecho_dev, cdev)</text>
<text x="10" y="303" fill="currentColor" font-size="12">→ 이후 read/write/llseek 는 번호 조회 없이 이 구조체로 바로 간다.</text>
<text x="10" y="325" fill="currentColor" font-size="12">노드 이름은 의미 없음: mknod /tmp/kalias c 510 0 도 같은 버퍼 (S2), rmmod 뒤엔 ENXIO (S5).</text>
<text x="10" y="347" fill="currentColor" font-size="12">모듈 참조 +2 = cdev_get() (cdev.owner) + fops_get() (fops-&gt;owner)</text>
<text x="10" y="367" fill="currentColor" font-size="12">실측 /proc/modules: kecho 20480 2</text>
</svg>
```

### Part A — 등록과 read/write 의 기본 규칙

- A1. `kecho_init()` / `kecho_exit()`: `alloc_chrdev_region` (minor 1개) → `cdev_init` + `cdev_add` → `class_create("kecho")` → `device_create(..., "kecho")`. 각 단계 앞에 `kecho_inject(N) ?:` 를 붙여 `fail_at=N` 으로 그 단계를 실패시킬 수 있게 하고, 실패하면 이미 끝낸 단계만 거꾸로 되감는다. exit 는 네 단계를 전부 거꾸로.
- A2. `kecho_read()`: `pos >= len` 이면 0(EOF). 아니면 `min(count, len - pos)` 바이트를 `copy_to_user` 하고 `*ppos` 를 그만큼 민다.
- A3. `kecho_write()`: `*ppos` 위치에 저장하고 `len = max(len, pos + n)`. 앞에 빈 곳(hole)이 생기면 0 으로 읽힌다.
- 테스트: 1 + 7 + 4096 바이트로 쪼개 읽으면 1, 7, **4088**, 0 이 나와야 하고, 7 바이트씩 읽으면 0 이 아닌 read 가 **586번**, 마지막은 1 바이트다 (4096 = 7 × 585 + 1).

### Part B — 끝, llseek, 나쁜 포인터

- B1. 버퍼 끝: 4000 에서 200 바이트 쓰기 → **96 바이트만 쓰는 short write**. 4096 에서 쓰기 → **`-ENOSPC`** (0 을 돌려주면 안 되는 이유는 §7).
- B2. `kecho_llseek()`: SEEK_SET 은 0, SEEK_CUR 은 `f_pos`, SEEK_END 는 `len` 기준. 결과가 0..4096 밖이면 `-EINVAL` 이고 `f_pos` 는 그대로.
- B3. `write(fd, (void *)8, 16)` / `read(fd, (void *)8, 16)` → `-EFAULT`, 장치 내용과 `f_pos` 는 그대로.
- B4. 유저 버퍼의 둘째 페이지를 `PROT_NONE` 으로 막고 300 바이트를 요청하면 복사가 중간에 멈춘다. 이때 **실제로 복사된 만큼** 돌려주고 `f_pos` 도 그만큼만 움직여야 하며, 쓰기라면 **복사되지 않은 뒤쪽 장치 바이트가 바뀌면 안 된다** (bounce 버퍼를 쓰는 이유).

### Part C — 여러 opener, 노드, 언로드

- C1. 두 번 open 한 fd 는 `f_pos` 가 따로지만 데이터는 같다. `dup` 과 `fork` 는 같은 `struct file` 이라 `f_pos` 도 같이 움직인다.
- C2. 프로세스 2개가 각자 4096 바이트 'A'/'B' 를 200번씩 쓰고, 다른 프로세스가 4096 바이트를 200번 읽는다. 읽은 블록이 한 번이라도 섞이면(torn) 실패. 장치당 mutex 하나로 막는다.
- C3. (셸) `/proc/devices` 의 major 와 `ls -l /dev/kecho` 가 같은지, `mknod` 로 만든 다른 이름의 노드가 같은 버퍼를 여는지, `dd` 가 4 KiB 뒤에서 `No space left on device` 를 받는지, 열린 동안 `rmmod` 가 거절되는지, `fail_at=1..4` 가 아무것도 남기지 않는지 확인한다.

## 2. Problem (English)

> Write a Linux character driver `kecho` that exposes a single 4096-byte kernel buffer as `/dev/kecho`.
>
> ```c
> static int __init kecho_init(void);   /* alloc_chrdev_region, cdev_add, class_create, device_create; unwind on error */
> static ssize_t kecho_read(struct file *f, char __user *buf, size_t count, loff_t *ppos);
> static ssize_t kecho_write(struct file *f, const char __user *buf, size_t count, loff_t *ppos);
> static loff_t kecho_llseek(struct file *f, loff_t off, int whence);
> ```
>
> Requirements:
> - `write` stores at `*ppos`; the valid length `len` is the highest offset ever written; gaps read as zero.
> - `read` returns `min(count, len - pos)` bytes, 0 at or past `len` (EOF). Partial reads must reassemble exactly.
> - A write that only partly fits is a short write; a write at offset 4096 returns `-ENOSPC`.
> - If `copy_{to,from}_user` copies nothing, return `-EFAULT` with no side effects; if it copies part, return that part, advance `*ppos` by it, and do not modify device bytes beyond it.
> - `llseek` supports SEEK_SET/CUR/END (END = `len`) within 0..4096, else `-EINVAL`.
> - Concurrent openers must never observe a torn 4096-byte record.
> - Any failing init step must leave no class, device node, cdev or chrdev region behind.
>
> Example (guest, after `insmod kecho.ko`): writing `"hello"` then `lseek(fd, 10, SEEK_SET); write(fd, "W", 1)` gives `lseek(fd, 0, SEEK_END) == 11` and a read of `"hello\0\0\0\0\0W"`. `dd if=/dev/zero of=/dev/kecho bs=1024 count=5` prints `4+0 records out` and `No space left on device`.

## 3. 시작 코드

파일 구성: 모듈 `starter.c` / `solution.c` / `practice.c` (Makefile 의 `VARIANT=` 로 골라 항상 `kecho.ko` 로 빌드), 게스트 유저 테스트 `kecho_test.c` (`gcc -static`, CHECK 하네스), 게스트 셸 테스트 `guest_test.sh`, 일부러 망가뜨린 `demo_bugs.c`, 그리고 전부 묶는 `check.sh` (VM 안에서 빌드 → initramfs → QEMU 부팅 → PASS/FAIL 판정, 판정은 개수 + dmesg grep 만 쓰고 시간은 안 쓴다).

```bash
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B07/check.sh starter
```

starter 실측: `4/14 passed`, `RESULT: FAIL (starter)`, exit 1 (등록이 TODO 라 `/dev/kecho` 가 없어서 `FAIL: A0 open /dev/kecho O_RDWR` 에서 C 테스트가 멈추고, 셸 테스트도 대부분 FAIL). 약 10–13 초.

<!-- code: Makefile -->

<!-- code: kecho_test.c -->

<!-- code: guest_test.sh -->

<!-- code: check.sh -->

<!-- code: starter.c -->

## 4. 힌트

<details><summary>힌트 1 — 등록 순서는 "다 준비하고 마지막에 공개"</summary>

`cdev_add` 가 성공하는 순간부터 그 번호로 open 이 들어올 수 있다. 그래서 버퍼와 mutex 는 그 전에 초기화한다. `device_create` 는 uevent 를 보내 `/dev/kecho` 를 생기게 하므로 맨 마지막이다. 되감기는 정확히 반대 순서: `class_destroy` → `cdev_del` → `unregister_chrdev_region`. `class_create`/`device_create` 는 NULL 이 아니라 `ERR_PTR` 을 돌려주므로 `IS_ERR()` 로 검사한다.
</details>

<details><summary>힌트 2 — copy_to_user 는 "못 한 바이트 수"를 돌려준다</summary>

0 이면 전부 성공. `n` 이면 하나도 못 함 → `-EFAULT`. 그 사이면 `n - left` 바이트는 복사됐으므로 그것을 반환하고 `*ppos` 도 그만큼만 민다. `count == 0` 을 먼저 처리하지 않으면 `left == n == 0` 이 되어 `-EFAULT` 로 오판한다.
</details>

<details><summary>힌트 3 — 끝에서 0 을 돌려주면?</summary>

read 의 0 은 EOF 라는 약속이지만, write 의 0 은 "아무것도 못 썼지만 오류도 아니다"라서 `while (left) { n = write(...); left -= n; }` 루프가 영원히 돈다. 아무것도 들어갈 수 없을 때는 `-ENOSPC`, 일부만 들어가면 그 개수.
</details>

<details><summary>힌트 4 — copy_from_user 를 왜 data 에 바로 하지 않나</summary>

`include/linux/uaccess.h` 의 `_inline_copy_from_user()` 끝부분을 보라. 실패하면 목적지의 **복사 못 한 꼬리를 0 으로 memset** 한다. 목적지가 장치 버퍼라면?
</details>

<details><summary>힌트 5 — llseek 범위 검사에서 overflow 피하기</summary>

`base + off` 를 먼저 더하면 `off = LLONG_MAX` 에서 넘친다. `base` 가 0..4096 이므로 `off < -base || off > KECHO_SIZE - base` 로 비교하면 넘칠 일이 없다.
</details>

## 5. 풀이

### A1 — 등록 4단계와 되감기

| 단계 | 호출 | 결과로 생기는 것 | 되감기 |
|---|---|---|---|
| 1 | `alloc_chrdev_region(&devt, 0, 1, "kecho")` | `/proc/devices` 의 `510 kecho` (번호 예약만) | `unregister_chrdev_region(devt, 1)` |
| 2 | `cdev_init` + `cdev.owner = THIS_MODULE` + `cdev_add(&cdev, devt, 1)` | `cdev_map` 에 510:0 → cdev. 이제 open 가능 | `cdev_del` |
| 3 | `class_create("kecho")` | `/sys/class/kecho/` | `class_destroy` |
| 4 | `device_create(cls, NULL, devt, NULL, "kecho")` | `/sys/class/kecho/kecho/dev` = `510:0`, uevent → devtmpfs 가 `/dev/kecho` 생성 | `device_destroy` |

`class_create` 는 6.4 부터 인자가 이름 하나다(랩 `include/linux/device/class.h:226` `struct class * __must_check class_create(const char *name);`). 6.18 의 devtmpfs 는 기본 모드 0600 으로 노드를 만든다(`drivers/base/devtmpfs.c` 주석 "owned by root and have a default mode of 0600"). 실측 `crw-------    1 0        0         510,   0 ... /dev/kecho`.

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">init 은 위에서 아래로, 실패하면 그 위 단계만 아래에서 위로 되감는다 (fail_at=N 실측: 4개 모두 흔적 0)</text>
<rect x="10" y="40" width="250" height="34" class="f-ok" stroke="currentColor"/>
<text x="20" y="62" fill="currentColor">1 alloc_chrdev_region</text>
<rect x="10" y="84" width="250" height="34" class="f-ok" stroke="currentColor"/>
<text x="20" y="106" fill="currentColor">2 cdev_init + cdev_add</text>
<rect x="10" y="128" width="250" height="34" class="f-ok" stroke="currentColor"/>
<text x="20" y="150" fill="currentColor">3 class_create</text>
<rect x="10" y="172" width="250" height="34" class="f-ok" stroke="currentColor"/>
<text x="20" y="194" fill="currentColor">4 device_create → /dev/kecho</text>
<rect x="300" y="84" width="200" height="34" class="f-warn" stroke="currentColor"/>
<text x="310" y="106" fill="currentColor">err_region: unregister</text>
<rect x="300" y="128" width="200" height="34" class="f-warn" stroke="currentColor"/>
<text x="310" y="150" fill="currentColor">err_cdev: cdev_del</text>
<rect x="300" y="172" width="200" height="34" class="f-warn" stroke="currentColor"/>
<text x="310" y="194" fill="currentColor">err_class: class_destroy</text>
<line x1="260" y1="101" x2="300" y2="101" stroke="currentColor"/>
<line x1="260" y1="145" x2="300" y2="145" stroke="currentColor"/>
<line x1="260" y1="189" x2="300" y2="189" stroke="currentColor"/>
<line x1="400" y1="172" x2="400" y2="162" stroke="currentColor"/>
<line x1="400" y1="128" x2="400" y2="118" stroke="currentColor"/>
<text x="520" y="62" fill="currentColor" font-size="12">fail_at=1 → 그냥 return</text>
<text x="520" y="106" fill="currentColor" font-size="12">fail_at=2 → err_region 부터</text>
<text x="520" y="150" fill="currentColor" font-size="12">fail_at=3 → err_cdev 부터</text>
<text x="520" y="194" fill="currentColor" font-size="12">fail_at=4 → err_class 부터</text>
<text x="10" y="232" fill="currentColor" font-size="12">검사(S6): insmod 실패 + dmesg "injected failure at step N"</text>
<text x="10" y="252" fill="currentColor" font-size="12">+ /sys/class/kecho, /proc/devices, /dev/kecho, /proc/modules 에 kecho 없음</text>
</svg>
```

`goto` 사다리(ladder)는 커널 스타일 그대로다. 레이블 이름을 "무엇을 되감는가"로 지으면(`err_class`, `err_cdev`, `err_region`) 단계를 추가할 때 실수가 줄어든다. `cdev_init` 은 실패하지 않으므로 되감을 것이 없다.

`kecho` 구조체는 정적(static)이라 `kfree` 도 없다. 동적 할당으로 바꿔도 안전한 이유: `cdev_del` 이 기존 inode 의 `i_cdev` 연결까지 끊고(`cdev_purge`), 열린 파일이 있으면 모듈 참조 때문에 애초에 rmmod 가 안 된다(C3). 단 `fs/char_dev.c` 의 `cdev_del` 주석대로 "already open" 파일의 fops 는 cdev_del 뒤에도 불릴 수 있으므로, 핫플러그 장치(USB 등)는 별도의 "사라짐" 플래그가 필요하다 — 이 레슨 범위 밖.

### A2 — read: `*ppos` 기준, EOF, short read

```c
	if (!count)
		return 0;
	if (mutex_lock_interruptible(&d->lock))
		return -ERESTARTSYS;
	if (pos >= d->len) { ret = 0; goto out; }        /* EOF */
	n = min_t(size_t, count, d->len - pos);
	left = copy_to_user(buf, d->data + pos, n);
	if (left == n) { ret = -EFAULT; goto out; }      /* 하나도 못 줌 */
	n -= left;                                       /* 준 만큼만 */
	*ppos = pos + n;
```

`*ppos` 는 `file->f_pos` 자체가 아니다. 랩 `fs/read_write.c` 의 `ksys_write()`(read 도 같은 모양)는 `pos = *ppos; ppos = &pos;` 로 **지역 복사본** 을 넘기고, 드라이버가 0 이상을 돌려줄 때만 `f_pos = pos` 로 되쓴다. `pread()` 는 아예 `f_pos` 와 무관한 위치를 넘긴다(테스트 `B2 pread(5 @4000) leaves f_pos alone`). 그래서 드라이버는 **`file->f_pos` 를 read/write 안에서 직접 만지지 않고 `*ppos` 만 쓴다.** (llseek 은 예외: 거기서는 `f_pos` 를 바꾸는 것이 일이다.)

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">바이트 축 0 … 4096: 실측 반환값 (Linux 6.18.55-dbg 게스트)</text>
<line x1="40" y1="50" x2="700" y2="50" stroke="currentColor"/>
<text x="34" y="44" fill="currentColor" font-size="12">0</text>
<text x="676" y="44" fill="currentColor" font-size="12">4096</text>
<text x="590" y="44" fill="currentColor" font-size="12">4000</text>
<line x1="700" y1="45" x2="700" y2="270" stroke="currentColor" stroke-dasharray="4 3"/>
<rect x="40" y="60" width="6" height="22" class="f-acc" stroke="currentColor"/>
<rect x="46" y="60" width="10" height="22" class="f-acc" stroke="currentColor"/>
<rect x="56" y="60" width="644" height="22" class="f-ok" stroke="currentColor"/>
<text x="60" y="76" fill="currentColor" font-size="12">read(1)=1, read(7)=7, read(4096)=4088 (short read), 다음 read=0 (EOF)</text>
<text x="40" y="104" fill="currentColor" font-size="12">7 바이트씩: 0 아닌 read 586번 = 585 × 7 + 1, 합 4096, 패턴 일치</text>
<rect x="625" y="120" width="75" height="22" class="f-ok" stroke="currentColor"/>
<rect x="700" y="120" width="30" height="22" class="f-warn" stroke="currentColor"/>
<text x="40" y="136" fill="currentColor" font-size="12">lseek(4000) + write(200): 96 쓰고 반환 96, f_pos 4096 →</text>
<text x="40" y="164" fill="currentColor" font-size="12">f_pos 4096 에서 write(1) 또는 write(200): -1, errno 28 ENOSPC, f_pos 그대로 4096</text>
<circle cx="700" cy="160" r="5" class="f-warn" stroke="currentColor"/>
<text x="10" y="200" fill="currentColor" font-weight="bold">유저 버퍼가 페이지 경계에서 막힐 때 (PROT_NONE 다음 페이지, 300 바이트 요청)</text>
<rect x="40" y="215" width="200" height="24" class="f-ok" stroke="currentColor"/>
<rect x="240" y="215" width="400" height="24" class="f-mut" stroke="currentColor"/>
<text x="48" y="232" fill="currentColor" font-size="12">유효 100 B</text>
<text x="250" y="232" fill="currentColor" font-size="12">PROT_NONE (200 B 요청분)</text>
<text x="40" y="262" fill="currentColor" font-size="12">read → 96 (copy_to_user 가 청크 단위로 멈춤), write → 52. 둘 다 f_pos == 반환값, -EFAULT 아님</text>
<text x="40" y="284" fill="currentColor" font-size="12">write 52 뒤의 장치 바이트 52..299 는 그대로 (bounce 버퍼 덕분, §7 실수 3 은 여기가 0 이 된다)</text>
</svg>
```

### A3 — write: hole, len, bounce 버퍼

`len = max(len, pos + n)` 이 "파일 크기"다. `lseek(10)` 뒤 `write("W")` 를 하면 5..9 가 빈 곳이 되고, 버퍼가 처음에 0 이므로 `hello\0\0\0\0\0W` 로 읽힌다 (테스트 `A1 hole reads as zeros`). base 드라이버에는 크기를 줄이는 방법(truncate)이 없다. 그건 P2 의 `O_TRUNC` 다.

`copy_from_user` 는 장치 버퍼가 아니라 `d->bounce` 로 받는다. 이유는 B4 에서.

### B1 — 버퍼 끝: short write vs -ENOSPC

| 상황 | kecho | 왜 |
|---|---|---|
| 일부만 들어감 (4000 + 200) | **96** (short write) | POSIX write 는 "쓴 바이트 수"를 돌려준다. 호출자는 나머지를 다시 시도하고, 다음에 `-ENOSPC` 를 받는다 |
| 하나도 안 들어감 (4096) | **`-ENOSPC`** | 블록 장치와 같은 선택: 랩 `block/fops.c` `blkdev_write_iter()` 의 `if (iocb->ki_pos >= size) return -ENOSPC;` |
| `count == 0` | 0 | 0 바이트 쓰기는 항상 0 |

실측 BusyBox `dd`:

```text
dd: error writing '/dev/kecho': No space left on device
5+0 records in
4+0 records out
4096 bytes (4.0KB) copied, 0.003387 seconds, 1.2MB/s
```

"전부 아니면 `-ENOSPC`"(all-or-nothing)도 가능한 설계지만, 그러면 4000 위치에서 200 바이트 쓰기가 아무것도 못 쓰고 실패한다. 일반 파일·블록 장치와 같은 규칙(short write → 다음에 ENOSPC)을 고른 것은 `dd`, `cat >` 같은 도구가 이 규칙을 이미 처리하기 때문이다. 반면 `fs/libfs.c` 의 `simple_write_to_buffer()` 는 `pos >= available` 에서 **0** 을 돌려준다 — 그대로 쓰면 §7 실수 4 의 무한 루프가 된다.

### B2 — llseek

```c
	switch (whence) {
	case SEEK_SET: base = 0; break;
	case SEEK_CUR: base = file->f_pos; break;
	case SEEK_END: base = d->len; break;
	default: ret = -EINVAL; goto out;
	}
	if (off < -base || off > KECHO_SIZE - base) { ret = -EINVAL; goto out; }
	file->f_pos = base + off;
```

- `.llseek` 를 비우면 `lseek()` 은 `-ESPIPE` 이다(ELN G4 노트 §5.1, `fs/open.c` 가 `FMODE_LSEEK` 를 지움).
- `default_llseek` 을 쓰면 SEEK_END 가 **inode 의 `i_size`** 기준인데 devtmpfs 노드의 `i_size` 는 0 이다 (`fs/read_write.c` `default_llseek`: `offset += i_size_read(inode)`). 그래서 직접 짰다.
- whence 99 는 드라이버에 오지도 않는다: `ksys_lseek()` 이 `whence <= SEEK_MAX`(= SEEK_HOLE = 4) 일 때만 `vfs_llseek` 을 부른다. 실측 `-22`. 반면 SEEK_DATA(3)는 드라이버의 `default:` 로 와서 `-EINVAL` 이 된다(테스트 `B2 SEEK_DATA -> -EINVAL (driver default)`).

### B3/B4 — 나쁜 포인터와 반쯤 실패한 복사

| 요청 | 어디서 걸리나 | 실측 |
|---|---|---|
| `write(fd, (void *)8, 16)` | 드라이버의 `copy_from_user` 가 페이지 폴트 → fixup → 16 바이트 전부 "못 함" | `-14` (EFAULT), `f_pos` 0, 장치 `03 0a 11 18 …` 그대로 |
| `read(fd, (void *)0xffff800010000000, 16)` | 드라이버까지 안 옴: `vfs_read` 의 `access_ok()` | `-14` |
| `read(fd, p + 4096 - 100, 300)` | 100 바이트 뒤 PROT_NONE | **96** |
| `write(fd, p + 4096 - 100, 300)` | 같은 경계 | **52** |

왜 100 이 아니라 96, 52 인가? arm64 의 `copy_to_user` 는 `arch/arm64/lib/copy_to_user.S` + `copy_template.S` 로 `ldp/stp` 등 여러 바이트 청크 단위로 복사하고, 폴트가 나면 fixup 이 "목적지가 어디까지 갔나"로 `bytes not copied` 를 계산한다(`9998: sub x0, end, dst`). 바이트 단위 재시도는 **하나도 못 했을 때만** 1 바이트 해 본다. 그래서 반환값은 "적어도 이만큼은 확실히 복사했다"이지 최대치가 아니다. 데모 모듈에서는 구조체 배치가 달라 같은 read 가 **88** 을 돌려줬다 — 정렬에 따라 달라진다. 테스트는 그래서 `1..100` 범위 + `f_pos == 반환값` + 내용 일치로 판정한다.

**bounce 버퍼가 필요한 이유**: 랩 `include/linux/uaccess.h` 의 `_inline_copy_from_user()`:

```c
	res = raw_copy_from_user(to, from, n);
	...
	if (likely(!res))
		return 0;
fail:
	memset(to + (n - res), 0, res);
	return res;
```

복사 못 한 꼬리를 **목적지에서 0 으로 채운다** (커널 정보가 새지 않게 하려는 규칙). 목적지가 `d->data + pos` 이면 write 가 52 를 돌려줬는데 장치의 52..299 가 0 이 된다. 심지어 `(void *)8` 처럼 **`-EFAULT` 로 끝난 write 도 장치 앞 16 바이트를 0 으로 지운다** — §7 실수 3 의 실측이 정확히 그렇다. 그래서 `bounce` 로 받은 뒤 도착한 `n - left` 바이트만 `memcpy` 한다. 4 KiB 정적 버퍼 하나면 되고 mutex 가 보호한다.

### C1 — f_pos 는 struct file 의 것

| 테스트 | 결과 | 이유 |
|---|---|---|
| `C1` fd1 로 50 에 쓰기 → fd2 의 f_pos | 5 그대로, 하지만 fd2 가 50 을 읽으면 `zz` | open 마다 `struct file` 이 새로 생긴다. 데이터는 `kecho_dev` 하나 |
| `C2` `dup(fd)` 로 lseek 77 | 원래 fd 도 77 | 같은 `struct file` |
| `C2` fork 자식이 10 바이트 read | 부모 f_pos 10 | fork 는 fd 테이블을 복사하지만 가리키는 `struct file` 은 같다 |

같은 `struct file` 을 두 스레드가 동시에 read 하면? 일반 파일은 `FMODE_ATOMIC_POS` 가 있어 `fdget_pos()` 가 `f_pos_lock` 을 잡지만, 랩 `fs/open.c` 에서 이 플래그는 `S_ISREG || S_ISDIR` 에만 붙는다. **문자 장치는 f_pos 갱신이 직렬화되지 않는다** — 두 스레드가 같은 위치를 읽고 둘 다 같은 값으로 되쓸 수 있다. 드라이버 mutex 는 데이터를 지키지 f_pos 를 지키지 않는다. 그런 유저 코드는 `pread/pwrite` 를 쓰는 것이 답이다.

### C2 — 장치당 mutex

```text
     C3 stress: rounds=200 torn=0 short=0 bad_writers=0
```

write 의 `memcpy` 4096 바이트와 read 의 `copy_to_user` 4096 바이트가 같은 mutex 안에 있으므로 섞일 수 없다. 왜 spinlock 이 아닌가: `copy_to_user` 는 페이지 폴트로 **잠들 수 있다**(`might_fault()`). 스핀락을 쥔 채 부르면 debug 커널이 바로 잡는다(§7 실수 1). `mutex_lock_interruptible` 을 쓴 것은 다른 opener 가 오래 쥐고 있을 때 Ctrl-C 로 빠져나올 수 있게 하려는 것이고, 시그널이면 `-ERESTARTSYS` 를 돌려 VFS 가 재시작/EINTR 을 정한다. 락을 뺀 데모에서는 300 라운드 × 3 번 중 1 번만 섞였다(TCG -smp 2, 운이 좋은 것이지 안전한 것이 아니다 — §7 실수 2).

### C3 — 노드, 참조 카운트, 언로드 (셸 실측)

```text
crw-------    1 0        0         510,   0 Nov 15 23:19 /dev/kecho
     refcnt with no opener: 0
     /proc/modules: kecho 20480 2 - Live 0xffffc27125bb7000 (O)
     rmmod rc=0 (rmmod: remove 'kecho': Resource temporarily unavailable)
cat: can't open '/tmp/kalias': No such device or address
insmod: can't insert '/kecho.ko': Cannot allocate memory
```

- **열린 fd 하나에 참조 2**: `chrdev_open()` 의 `cdev_get()` 이 `try_module_get(cdev.owner)`, 그리고 `fops_get()` 이 `fops->owner` 를 한 번 더 잡는다. 그래서 `cdev.owner` 와 `fops.owner` 둘 다 `THIS_MODULE` 이어야 한다.
- **BusyBox 1.37 `rmmod` 는 거절당해도 exit code 0** 이었다. 그래서 `guest_test.sh` 는 rc 대신 `/proc/modules` 에 남았는지로 판정한다. 스크립트로 언로드를 검사할 때 실제로 밟는 함정이다.
- `mknod /tmp/kalias c 510 0` 노드는 언로드 뒤 `ENXIO` (`chrdev_open()` 의 `kobj_lookup` 실패 → `return -ENXIO`).
- `fail_at=1` 의 `insmod` 메시지는 init 이 돌려준 `-ENOMEM` 그대로다.

### 실행 결과 (solution)

```bash
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B07/check.sh solution
```

```text
PASS: A3 read(4096) -> short read              = 4088
PASS: A3 7-byte chunks: non-zero reads         = 586
PASS: B1 write 200 at 4000 -> short 96         = 96
PASS: B1 next write -> -ENOSPC                 = -28
…
     B4 write(300) from last 100 bytes of a page returned 52
PASS: B4 partial write: bytes after it NOT clobbered
…
PASS: S6 fail_at=4: insmod fails, nothing leaked
PASS: S7 final rmmod, nothing left
DMESG_BAD=0
84/84 passed, dmesg bad lines: 0
RESULT: PASS (solution)
```

3 회 연속 실행 모두 `84/84`, exit 0, 회당 약 9–21 초(빌드 포함, TCG, 공유 VM 부하에 따라 다름). 게스트 콘솔 전체는 `log_solution_guest.txt`.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

## 7. 자주 하는 실수

모두 `demo_bugs.c` 의 `bug=N` 으로 재현했다. 각 줄은 실제 게스트 출력이다(`log_demo_*.txt`).

```bash
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B07/check.sh demo spin
```

<!-- code: demo_bugs.c -->

**실수 1 — 스핀락을 쥔 채 `copy_to_user` (`demo spin`)**. 유저 페이지가 스왑·미매핑이면 폴트 처리 중 잠든다. debug 커널은 폴트가 안 나도 `might_fault()` 에서 잡는다:

```text
[    4.981766] BUG: sleeping function called from invalid context at /var/lab/src/linux-6.18.55/include/linux/uaccess.h:192
[    4.981858] in_atomic(): 1, irqs_disabled(): 0, non_block: 0, pid: 74, name: cat
[    4.982013]  #0: ffffd7d9c10c00f8 (&kecho.lock){+.+.}-{4:4}, at: kecho_read+0x138/0x258 [demo_bugs]
[    4.983174]  #1: ffffd7d9c10c0140 (&kecho.slock){+.+.}-{3:3}, at: kecho_read+0x16c/0x258 [demo_bugs]
…
[    4.984598]  __might_fault+0x2c/0x88
[    4.984619]  kecho_read+0x188/0x258 [demo_bugs]
…
[    4.987204] [ BUG: Invalid wait context ]
[    4.987473] ffff00000591c470 (&mm->mmap_lock){++++}-{4:4}, at: __might_fault+0x38/0x88
```

`cat` 은 여전히 `hello` 를 출력했다 — 기능 테스트만 보면 통과한다. 고친 코드: mutex 로 감싼다(solution). IRQ 와 공유해야 한다면 스핀락 안에서는 커널 버퍼끼리만 복사하고, `copy_to_user` 는 락 밖에서 한다(ELX-M04).

**실수 2 — 락 없음 (`demo nolock`)**. `stress 300` 을 3 번: `torn=0`, `torn=1`, `torn=0`. 테스트가 대부분 통과하므로 더 위험하다. 고친 코드: read/write/llseek 전부 같은 `d->lock`.

**실수 3 — `copy_from_user(d->data + pos, ...)` (`demo direct`)**:

```text
PASS: B3 write from (void *)8 -> -EFAULT       = -14
     B3 device[0..7] after the EFAULT write: 00 00 00 00 00 00 00 00
FAIL: B3 device unchanged after EFAULT (line 228)
     B4 write(300) from last 100 bytes of a page returned 52
FAIL: B4 partial write: bytes after it NOT clobbered (line 248)
65/68 passed
```

`-EFAULT` 를 돌려줬는데 장치 앞 8 바이트가 0 이 됐다(출력은 8 바이트만 찍었지만, 소스상 `memset` 은 요청한 16 바이트 전체). `B4 partial read: bytes are right` 의 FAIL 도 같은 원인(모델과 장치가 이미 어긋남). 고친 코드: bounce 로 받고 도착한 만큼만 memcpy.

**실수 4 — 끝에서 0 반환 (`demo write0`)**:

```text
dd 4096: rc=0
Terminated
dd 5x1024 under timeout 5: rc=143
```

정확히 4 KiB 는 문제없지만, 5 KiB 를 쓰는 `dd` 는 다섯 번째 write 가 0 을 받고 영원히 재시도하다가 `timeout` 에 SIGTERM 으로 죽었다(143 = 128 + 15). 고친 코드: `return -ENOSPC;`.

**실수 5 — read 가 `*ppos` 를 무시 (`demo noppos`)**:

```text
abcabcabcabcabcabcab
67473
```

`printf abc` 를 넣고 `head -c 20` 을 하면 `abc` 가 반복되고, `cat` 은 EOF 를 못 봐서 3 초 동안 67473 바이트를 뽑다가 `timeout` 에 죽었다(TCG 속도, 숫자 자체는 의미 없음). 고친 코드: `pos = *ppos` 에서 읽고 `*ppos += n`.

**실수 6 — exit 에서 `class_destroy` 를 빠뜨림 (`demo leak`)**. rmmod 뒤에도 `/sys/class/kecho` 가 남고 다음 insmod 가 실패한다:

```text
[    5.038444] sysfs: cannot create duplicate filename '/class/kecho'
…
[    5.039347]  class_create+0x4c/0x98
[    5.039356]  kecho_init+0xc0/0xf58 [demo_bugs]
[    5.039721] kobject: kobject_add_internal failed for kecho with -EEXIST, don't try to register things with the same name in the same directory.
insmod: can't insert '/demo_bugs.ko': File exists
```

보드에서는 "재부팅하면 되던데 모듈만 다시 올리면 안 돼요"라는 버그 리포트로 온다. 고친 코드: exit = init 의 정확한 역순, 그리고 `fail_at` 같은 실패 주입으로 init 실패 경로까지 테스트.

그 밖에: `count == 0` 미처리 → `left == n == 0` 이라 `-EFAULT` (solution 은 맨 앞에서 0). `IS_ERR` 대신 `!ptr` 검사 → `class_create` 실패를 놓친다. `cdev.owner` 누락 → 열려 있어도 rmmod 가 될 수 있다(ELN G4 §8 예제 11).

## 8. 면접 Q&A

<details><summary>Q1. alloc_chrdev_region, register_chrdev_region, register_chrdev 의 차이는?</summary>

`alloc_chrdev_region` 은 커널이 빈 major 를 고르고(이번 게스트에서는 510), `register_chrdev_region` 은 내가 정한 번호를 예약한다(이미 쓰이면 `-EBUSY`). 둘 다 **번호만** 예약하고 장치와 연결은 `cdev_add` 가 한다. `register_chrdev` 는 옛 한 방 API 로 minor 256 개 전체를 잡고 cdev 를 내부에서 만든다. 새 드라이버는 alloc + cdev, 또는 장치 하나면 misc device(P4).

**꼬리질문: 왜 동적 major 인가?** 고정 번호는 `Documentation/admin-guide/devices.txt`(랩 소스에 있음)에 등록해야 하고 충돌하면 로드가 실패한다. 유저는 번호가 아니라 udev/devtmpfs 가 만든 이름으로 연다.
</details>

<details><summary>Q2. read 의 *ppos 는 file->f_pos 와 같은가?</summary>

아니다. `ksys_read/ksys_write` 는 `f_pos` 의 지역 복사본 주소를 넘기고 반환값이 0 이상일 때만 되쓴다. `pread/pwrite` 는 유저가 준 오프셋의 주소를 넘기고 `f_pos` 는 건드리지 않는다. `stream_open()` 한 파일은 `ppos` 가 NULL 이다. 그래서 read/write 안에서는 `*ppos` 만 읽고 쓴다. 이 레슨의 `B2 pread(5 @4000) leaves f_pos alone` 이 그 증거다.

**꼬리질문: 같은 fd 를 두 스레드가 동시에 read 하면?** 문자 장치에는 `FMODE_ATOMIC_POS` 가 없어서(`fs/open.c`, 일반 파일·디렉터리만) `f_pos_lock` 을 안 잡는다. 둘 다 같은 오프셋을 읽을 수 있다. 드라이버 락으로는 못 고치고, 유저가 `pread` 를 쓰거나 fd 를 나눠야 한다.
</details>

<details><summary>Q3. copy_to_user 대신 그냥 포인터로 쓰면 안 되나요?</summary>

안 된다. (1) 유저 주소는 유효성이 보장되지 않는다: 미매핑이면 커널 폴트가 oops 가 된다. `copy_*_user` 는 예외 테이블(fixup)로 폴트를 오류값으로 바꾼다. (2) PAN(Privileged Access Never)을 지원하는 CPU(ARMv8.1+, 랩 커널은 `CONFIG_ARM64_PAN=y`; QEMU cortex-a57 은 v8.0 이라 이 하드웨어 기능은 없다)에서는 커널이 평범한 load/store 로 유저 메모리를 만지는 것 자체가 막힌다. `copy_to_user` 는 `sttr` 같은 unprivileged 명령을 쓴다(`arch/arm64/lib/copy_to_user.S`). (3) `access_ok()` 로 커널 주소를 막는다(테스트 `B3 read into kernel address -> -EFAULT`).

**꼬리질문: 반환값이 0 이 아니면?** "복사 못 한 바이트 수"다. 전부 실패면 `-EFAULT`, 일부면 그 일부를 반환. arm64 에서는 그 값이 바이트 단위로 정확하지 않다(실측 100 중 96, 52).
</details>

<details><summary>Q4. 버퍼가 꽉 찼을 때 write 는 무엇을 돌려줘야 하나요?</summary>

일부라도 들어가면 그 개수(short write), 하나도 안 들어가면 `-ENOSPC`. 0 은 안 된다: 유저의 write-all 루프가 영원히 돈다(실측 `dd ... rc=143`). 블록 장치(`block/fops.c`)도 같은 규칙이다. read 쪽은 반대로 0 이 EOF 라는 정해진 의미다.

**꼬리질문: FIFO 처럼 "나중에 공간이 생기는" 장치라면?** 그때는 `-ENOSPC` 가 아니라 블로킹(wait queue) 또는 `O_NONBLOCK` 이면 `-EAGAIN` 이다(ELN G4 §6, poll 은 G6).
</details>

<details><summary>Q5. 왜 spinlock 이 아니라 mutex 인가요?</summary>

임계구역 안에 `copy_to_user/copy_from_user` 가 있고, 이 둘은 페이지 폴트 처리로 잠들 수 있다. 스핀락을 쥔 채 잠들면 안 된다 — 실측 `BUG: sleeping function called from invalid context at .../uaccess.h:192` 와 `[ BUG: Invalid wait context ]`. 프로세스 컨텍스트끼리만 공유하는 상태이므로 mutex 가 맞다.

**꼬리질문: 하드 IRQ 핸들러가 버퍼를 채운다면?** IRQ 쪽은 스핀락만 쓸 수 있으므로 `spin_lock_irqsave` 안에서는 커널 버퍼 → bounce 로만 복사하고, 락을 푼 뒤 `copy_to_user` 한다. 또는 kfifo + 단일 생산자/소비자. 락 선택 표는 ELX-M04.
</details>

<details><summary>Q6. 장치가 열려 있는데 rmmod 하면 어떻게 되나요?</summary>

거절된다. open 하나가 모듈 참조를 2 개 잡는다(`cdev_get()` 의 `try_module_get(cdev.owner)` + `fops_get()`), 실측 `kecho 20480 2`. BusyBox rmmod 는 `Resource temporarily unavailable` 을 출력했지만 exit code 는 0 이었다. close 하면 참조가 0 이 되고 rmmod 가 된다.

**꼬리질문: `cdev_del` 뒤에 이미 열린 파일은?** `fs/char_dev.c` 주석: "any cdevs already open will remain and their fops will still be callable even after cdev_del returns". 모듈 언로드는 참조로 막지만, USB 처럼 장치가 먼저 사라지는 경우는 드라이버가 "gone" 플래그를 두고 fops 에서 `-ENODEV` 를 돌려줘야 한다.
</details>

<details><summary>Q7. /dev/kecho 는 누가 만드나요? mknod 는 언제 쓰나요?</summary>

`device_create` 가 `/sys/class/kecho/kecho/dev` 를 만들고 uevent 를 보낸다. 커널의 devtmpfs 가 그 즉시 노드를 만들고(기본 0600 root), 데스크톱/systemd 에서는 udev 가 권한·심볼릭 링크를 조정한다. 이 레슨의 initramfs 는 udev 없이 devtmpfs 만으로 노드가 생겼다. devtmpfs 가 없는 오래된 시스템이나 다른 이름이 필요할 때는 `/proc/devices` 에서 major 를 읽어 `mknod name c MAJ MIN` — 실측 `/tmp/kalias` 가 같은 버퍼를 열었다. 노드는 번호만 담은 명함이다.

**꼬리질문: 0666 으로 만들려면?** `class->devnode` 콜백에서 `*mode = 0666` (6.18 시그니처 `char *(*devnode)(const struct device *dev, umode_t *mode)`), 또는 udev 규칙. 이 레슨에서는 돌려 보지 않았다.
</details>

<details><summary>Q8. init 이 중간에 실패하면? 왜 fail_at 같은 걸 넣나요?</summary>

이미 끝낸 단계만 역순으로 되감는다(goto 사다리). 실패 경로는 정상 부팅에서 거의 안 돌기 때문에 버그가 숨어 있다가 "다시 insmod 하면 `File exists`" 같은 증상으로 나온다(실수 6). 실패 주입으로 경로마다 한 번씩 돌리고 "흔적 0"을 검사하는 것이 가장 싸다. 커널에는 일반화된 장치로 `CONFIG_FAULT_INJECTION`(failslab 등)도 있다.

**꼬리질문: 더 짧게 쓰는 방법은?** `cdev_device_add()`(`fs/char_dev.c:544`)가 cdev_add + device_add 를 묶고, 장치 하나면 `misc_register()` 하나로 번호·cdev·class·노드가 끝난다(P4). platform driver 라면 devm_ 계열이 되감기를 대신해 준다(ELX-M01).
</details>

## 9. 추가 연습

`practice.c` 는 네 가지를 한 모듈에 넣었고, base 테스트 전체 + P1–P4 + 셸 테스트(`fail_at=1..5`)를 함께 통과한다.

**P1. minor 2 개.** `/dev/kecho`(minor 0), `/dev/kecho1`(minor 1)이 각자 버퍼를 갖게 하라.

<details><summary>답</summary>

`alloc_chrdev_region(&devt, 0, 2, ...)`, `kdevs[2]` 마다 `cdev_add(MKDEV(MAJOR(devt), i), 1)` 과 `device_create`. open 은 그대로 `container_of(inode->i_cdev, ...)` 라서 바뀌지 않는다 — 이게 private_data 패턴의 장점이다. 되감기는 루프 중간 실패를 위해 `while (i--) cdev_del(...)`. `fail_at=2`, `fail_at=4` 는 **두 번째** minor 에서 실패시켜 "반쯤 한 루프" 되감기를 검사한다.
</details>

**P2. O_TRUNC 와 O_APPEND.** 쓰기 open 에 `O_TRUNC` 가 있으면 비우고, `O_APPEND` 면 항상 `len` 뒤에 붙여라.

<details><summary>답</summary>

문자 장치는 VFS 가 둘 다 처리해 주지 않는다(일반 파일의 truncate/`generic_write_checks` 경로가 없다). open 에서 `(f_mode & FMODE_WRITE) && (f_flags & O_TRUNC)` 이면 mutex 안에서 `memset(data, 0, len); len = 0`(hole 이 다시 0 으로 읽히게). write 에서 `pos = (f_flags & O_APPEND) ? len : *ppos` 를 **락 안에서** 정한다 — 락 밖에서 len 을 읽으면 두 appender 가 같은 위치에 쓴다. 실측: `abc` + append `de` + `lseek(0)` 후 append `f` → `abcdef`, f_pos 6.
</details>

**P3. generic_file_llseek_size.** 손으로 짠 llseek 를 커널 헬퍼로 바꾸고 SEEK_DATA/SEEK_HOLE 을 얻어라.

<details><summary>답</summary>

`generic_file_llseek_size(file, off, whence, KECHO_SIZE, d->len)` 를 mutex 안에서. `must_set_pos()` 가 SEEK_DATA(`off >= eof` 면 `-ENXIO`)와 SEEK_HOLE(→ `eof`)을 처리하고 `vfs_setpos()` 가 0..maxsize 를 검사한다. 실측 `SEEK_HOLE from 0 = 6`, `SEEK_DATA at len = -6`(ENXIO). base 테스트의 "SEEK_DATA → -EINVAL" 은 practice 모드에서 건너뛴다.
</details>

**P4. misc device 로 같은 fops.** `/dev/kecho_misc` 를 `misc_register` 로 추가하라.

<details><summary>답</summary>

`struct miscdevice { .minor = MISC_DYNAMIC_MINOR, .name = "kecho_misc", .fops = &kecho_fops }`. major 는 10 이다. 함정: `drivers/char/misc.c` 의 `misc_open()` 이 드라이버 open 전에 `file->private_data = c`(miscdevice 포인터)를 넣고, misc 장치의 inode `i_cdev` 는 misc 공용 cdev 다. 그래서 open 에서 `imajor(inode) == MISC_MAJOR` 면 `container_of(file->private_data, struct kecho_dev, misc)` 로 갈라야 한다. 실측 `misc: major 10`, 자기 4 바이트만 돌려줌, minor 0 버퍼는 그대로.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B07/check.sh practice
```

실측: `105/105 passed, dmesg bad lines: 0`, `RESULT: PASS (practice)`, exit 0, 약 9–13 초.

## 10. 핵심 정리

- 등록 = 번호(`alloc_chrdev_region`) → 연결(`cdev_add`) → 공개(`class_create` + `device_create` → devtmpfs 노드). 데이터는 `cdev_add` 전에 준비, 되감기는 정확한 역순.
- read: `pos >= len` → 0(EOF), 아니면 있는 만큼(short read). write: 들어가는 만큼(short write), 하나도 안 들어가면 `-ENOSPC`, 절대 0 아님.
- `copy_*_user` 반환 = 못 한 바이트 수. 전부 실패 → `-EFAULT`, 일부 → 그 일부만 반환하고 `*ppos` 도 그만큼. arm64 에서는 청크 단위라 바이트 정확하지 않다(실측 100 중 96, 52).
- `copy_from_user` 는 실패한 꼬리를 목적지에서 0 으로 지운다 → 장치 버퍼에 바로 받지 말고 bounce.
- read/write 는 `*ppos` 만 쓴다(`pread` 와 지역 복사본). `f_pos` 는 `struct file` 마다: 두 번 open 은 따로, `dup`/`fork` 는 같이.
- 임계구역에 `copy_*_user` 가 있으면 mutex. 스핀락이면 `BUG: sleeping function called from invalid context`.
- open 하나 = 모듈 참조 2(cdev.owner + fops.owner). 열린 동안 rmmod 거절. BusyBox rmmod 의 rc 는 믿지 말 것.
- 실패 경로는 실패 주입(`fail_at`)으로 직접 돌려 "흔적 0"을 확인한다.

## 참고자료

- Linux 6.18.55 소스 (`/var/lab/src/linux-6.18.55`, 실제로 읽음): `fs/char_dev.c` (`alloc_chrdev_region`, `chrdev_open`, `cdev_get`, `cdev_del` 주석, `cdev_device_add`), `fs/read_write.c` (`ksys_write`, `vfs_write`, `rw_verify_area`, `ksys_lseek`, `default_llseek`, `generic_file_llseek_size`, `must_set_pos`), `fs/open.c` (`FMODE_ATOMIC_POS`), `fs/file.c` (`file_needs_f_pos_lock`), `fs/libfs.c` (`simple_write_to_buffer`), `block/fops.c` (`-ENOSPC`), `include/linux/uaccess.h` (`_inline_copy_from_user`), `arch/arm64/lib/copy_to_user.S`, `drivers/base/devtmpfs.c`, `drivers/char/misc.c`, `include/linux/device/class.h`
- [LDD3 Chapter 3: Char Drivers](https://lwn.net/Kernel/LDD3/) — 구조는 그대로지만 API 가 오래됨(class_create 인자, `no_llseek` 등은 6.18 과 다름)
- [Documentation/filesystems/vfs.rst](https://www.kernel.org/doc/html/latest/filesystems/vfs.html) — `struct file_operations` 각 멤버의 의미
- [Documentation/admin-guide/devices.rst](https://www.kernel.org/doc/html/latest/admin-guide/devices.html) — 고정 major 목록
- 이 사이트의 배경 노트: EmbeddedLinux `G_driver_model/2026-10-10_G4_char_device.md` (§1 dev_t, §5.1 llseek, §6 f_pos, §8 .owner), `E_kernel_basics` E6 (user/kernel 경계)
- 레슨 교차 참조: ELX-B06 (모듈 기본, refcnt), ELX-B08 (이 kecho 에 ioctl 추가), ELX-B09 (sysfs 속성), ELX-M04 (락 선택: IRQ 와 공유할 때), OS-B 시리즈 (syscall, fd/struct file)
