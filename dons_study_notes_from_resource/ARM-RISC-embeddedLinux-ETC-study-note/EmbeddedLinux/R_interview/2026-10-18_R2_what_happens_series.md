# R2. "무슨 일이 일어나나" 시리즈 — 일곱 개의 끝에서 끝까지 경로를 실제 트레이스로 말하기

> **이 노트를 다 읽으면**: 전원 ON → 셸, `open("/dev/x")` → `.open`, 인터럽트 → 잠든 `read()` 깨어남, `insmod` → `probe()`를 함수 이름으로 끊김 없이 말할 수 있다 · page fault, `fork()+exec()`, 패킷 → `recv()`를 1분 안에 설명한다 · 모든 단계를 ftrace/kprobe 실측 줄로 뒷받침한다 · 면접관이 어느 단계에서 끊고 "거기서 더 깊이"를 물어도 해당 모듈 노트로 내려갈 수 있다
> **왜 필요한가**: 시니어 Embedded Linux 면접의 "깊이 확인" 질문은 거의 이 일곱 개의 변형이다. 개념 정의(R1)를 아는지보다 **경계를 넘을 때마다 누가 무엇을 넘겨주는지**를 아는지를 본다
> **Don 기준 난이도**: 예외 진입·IRQ·DMA·리셋 시퀀스는 펌웨어로 이미 안다 / 새로 외울 것은 Linux가 붙인 함수 이름, 그리고 "잠든다 = runqueue에서 빠진다"는 스케줄러 쪽 연결
> **선행 노트**: C1, C6, C7, E3, E6, F3, F6, F7, F11, F12, G3, G4 (이 노트는 종합 노트라 각 절에서 해당 노트·절을 가리킨다)

---

## 0. 큰 그림 — 일곱 경로는 같은 부품을 돌려 쓴다

### 0.1 면접관이 이 질문으로 보는 것

"`open("/dev/x")`를 하면 무슨 일이 일어나죠?"는 지식 퀴즈가 아니다. 면접관은 지원자가 **추상화 경계**(유저 ↔ 커널, VFS ↔ 드라이버, hardirq ↔ 프로세스 문맥)를 넘을 때 **무엇이 넘어가는지**(fd 번호, `struct file`, `dev_t`, `pt_regs`, wait queue 엔트리)를 아는지 본다. 그래서 답은 세 단으로 준비한다.

1. **30초 버전** — 경계 5~6개를 한 문장씩. 끊기지 않고 끝까지 가는 것이 목표다.
2. **전체 walk** — 번호 붙은 단계, 각 단계에 실제 커널 함수 이름. 면접관이 중간에 끊으면 그 번호에서 내려간다.
3. **꼬리 질문** — 각 경로에서 단골로 나오는 "그럼 X면요?" 3~5개.

Don의 펌웨어 경험으로 바꾸면 이렇다. SSD 펌웨어에서 "호스트가 NVMe Read를 보내면 무슨 일이 일어나나"를 doorbell 쓰기 → SQ fetch → FTL 조회 → NAND 읽기 → DMA → CQ → MSI-X까지 막힘없이 말할 수 있었을 것이다. 같은 능력을 Linux 커널 함수 이름으로 하는 것이 이 노트다.

### 0.2 공통 부품 지도

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="22" font-size="13">일곱 시나리오(왼쪽)가 거쳐 가는 커널 공통 부품(오른쪽)</text>
<rect x="20" y="40" width="230" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="32" y="60" font-size="13">1. 전원 ON → 셸 프롬프트</text>
<rect x="20" y="84" width="230" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="32" y="104" font-size="13">2. open/read → 드라이버</text>
<rect x="20" y="128" width="230" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="32" y="148" font-size="13">3. IRQ → 잠든 read() 깨움</text>
<rect x="20" y="172" width="230" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="32" y="192" font-size="13">4. insmod → probe()</text>
<rect x="20" y="216" width="230" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="32" y="236" font-size="13">5. malloc 첫 접근 page fault</text>
<rect x="20" y="260" width="230" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="32" y="280" font-size="13">6. fork() + exec()</text>
<rect x="20" y="304" width="230" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="32" y="324" font-size="13">7. 패킷 도착 → recv()</text>
<rect x="330" y="40" width="330" height="34" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="342" y="62" font-size="12">A 예외 진입 — el0t_64_sync / el1h_64_irq</text>
<rect x="330" y="92" width="330" height="34" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="342" y="114" font-size="12">B VFS — do_sys_openat2 / vfs_read / f_op</text>
<rect x="330" y="144" width="330" height="34" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="342" y="166" font-size="12">C 드라이버 코어 — bus match / really_probe</text>
<rect x="330" y="196" width="330" height="34" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="342" y="218" font-size="12">D IRQ 계층 — GIC → irq_domain → 핸들러</text>
<rect x="330" y="248" width="330" height="34" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="342" y="270" font-size="12">E 잠·깨움 — wait queue → try_to_wake_up</text>
<rect x="330" y="300" width="330" height="34" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="342" y="322" font-size="12">F 스케줄러·mm — schedule / handle_mm_fault</text>
<g stroke="currentColor" stroke-opacity="0.45" fill="none">
<line x1="250" y1="55" x2="330" y2="161"/><line x1="250" y1="55" x2="330" y2="317"/>
<line x1="250" y1="99" x2="330" y2="57"/><line x1="250" y1="99" x2="330" y2="109"/>
<line x1="250" y1="143" x2="330" y2="213"/><line x1="250" y1="143" x2="330" y2="265"/><line x1="250" y1="143" x2="330" y2="317"/>
<line x1="250" y1="187" x2="330" y2="57"/><line x1="250" y1="187" x2="330" y2="161"/>
<line x1="250" y1="231" x2="330" y2="57"/><line x1="250" y1="231" x2="330" y2="317"/>
<line x1="250" y1="275" x2="330" y2="57"/><line x1="250" y1="275" x2="330" y2="317"/>
<line x1="250" y1="319" x2="330" y2="213"/><line x1="250" y1="319" x2="330" y2="265"/>
</g>
</svg>
```
그림 1 — 일곱 경로가 공유하는 부품. 파랑 네 개는 Don이 요청받은 핵심 시나리오, 주황 세 개는 단골 추가 질문이다. 부품 A(예외 진입)와 E(잠·깨움)를 한 번 확실히 이해하면 나머지는 조합이다.

| 부품 | 깊이 공부한 노트 | 이 노트에서 다시 나오는 곳 |
|---|---|---|
| A 예외 진입 (`svc`, data abort, IRQ) | E6 §1, F6 §1, F11 §5 | §2, §3, §4, §5, §6 |
| B VFS (`struct file`, `f_op`) | A3 §11, F12 §2–§4, E6 §2 | §2 |
| C 드라이버 코어 (bus/driver/device) | G2 §6, D3 §4–§5, G3 §13 | §1, §4 |
| D IRQ 계층 (GIC, irq_domain, flow handler) | F6 §1–§6, F7 §3–§8 | §3, §7 |
| E 잠·깨움 (wait queue) | F3 §1–§8, G6 §13 | §3, §7 |
| F 스케줄러·메모리 | F1 §10, F11 §5, A5 §3, A2 §2 | §1, §5, §6 |

### 0.3 증명 환경 — 이 노트의 모든 트레이스는 여기서 나왔다

- 랩 커널: `/var/lab/out/Image-trace` = Linux **6.18.55-trace** (ftrace 전 tracer, kprobes, `funcgraph-retval`). VM 호스트 커널(`uname -r` = `7.0.0-38-generic`)에는 아무것도 넣지 않았다.
- QEMU `virt`, `-cpu cortex-a57 -smp 1 -m 1G`, TCG(에뮬레이션). **모든 µs/ms 값은 TCG 값이라 실제 보드보다 10~100배 느리다.** 함수 이름과 순서가 증거이고, 시간은 비율로만 본다.
- 게스트 두 종류: (가) 랩 BusyBox initramfs 복사본 + 이 노트의 테스트 바이너리, (나) `rootfs-trace.ext4` 위 qcow2 overlay(systemd, kmod `insmod`, strace).
- DT: `-machine virt,dumpdtb=`로 `-m 1G -smp 1` 기준 DT를 떠서 노드 하나를 추가했다.

```dts
	r2_demo {
		compatible = "r2,demo";
	};
```

- 트레이스 수집: tracefs의 `function_graph`(+`set_graph_function`, `set_ftrace_pid`, `options/funcgraph-retval`), kprobe/kretprobe 이벤트 + `stacktrace` 트리거, sched/irq/napi/net tracepoint. 기법 자체는 M5 §4–§6에 있다.

### 0.4 실험용 드라이버 `r2_demo` — DT 매칭 platform driver + char device

무엇을 확인하는 코드인가: §2(open/read)와 §4(insmod → probe)를 같은 모듈로 추적하기 위한 최소 드라이버. `compatible = "r2,demo"` 노드와 매칭되면 `probe()`에서 char device `/dev/r2_demo`를 만든다(G3·G4 패턴의 축소판).

```c
// r2_demo: platform driver (DT "r2,demo") -> char device /dev/r2_demo
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/uaccess.h>
static dev_t r2_devt;
static struct cdev r2_cdev;
static struct class *r2_class;
static int r2_open(struct inode *inode, struct file *f)
{
	pr_info("r2_demo: open minor=%u\n", iminor(inode));
	return 0;
}
static ssize_t r2_read(struct file *f, char __user *buf, size_t n, loff_t *pos)
{
	static const char msg[] = "hello from r2_demo\n";
	return simple_read_from_buffer(buf, n, pos, msg, sizeof(msg) - 1);
}
static const struct file_operations r2_fops = {
	.owner = THIS_MODULE, .open = r2_open, .read = r2_read,
};
static int r2_probe(struct platform_device *pdev)
{
	int ret = alloc_chrdev_region(&r2_devt, 0, 1, "r2_demo");
	if (ret)
		return ret;
	cdev_init(&r2_cdev, &r2_fops);
	ret = cdev_add(&r2_cdev, r2_devt, 1);
	if (ret)
		goto err_region;
	r2_class = class_create("r2_demo");
	if (IS_ERR(r2_class)) { ret = PTR_ERR(r2_class); goto err_cdev; }
	device_create(r2_class, &pdev->dev, r2_devt, NULL, "r2_demo");
	dev_info(&pdev->dev, "probed, major=%d\n", MAJOR(r2_devt));
	return 0;
err_cdev:
	cdev_del(&r2_cdev);
err_region:
	unregister_chrdev_region(r2_devt, 1);
	return ret;
}
static void r2_remove(struct platform_device *pdev)
{
	device_destroy(r2_class, r2_devt);
	class_destroy(r2_class);
	cdev_del(&r2_cdev);
	unregister_chrdev_region(r2_devt, 1);
}
static const struct of_device_id r2_of_match[] = { { .compatible = "r2,demo" }, { } };
MODULE_DEVICE_TABLE(of, r2_of_match);
static struct platform_driver r2_driver = {
	.probe = r2_probe, .remove = r2_remove,
	.driver = { .name = "r2_demo", .of_match_table = r2_of_match },
};
module_platform_driver(r2_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("R2 demo: DT platform driver + char device");
```

```sh
make -C /var/lab/out/linux-build-trace M=$PWD modules 2>&1 | grep -i "warn\|error" || echo "no warnings"
modinfo r2_demo.ko | grep -E "vermagic|alias"
```

```text
no warnings
alias:          of:N*T*Cr2,demoC*
alias:          of:N*T*Cr2,demo
vermagic:       6.18.55-trace SMP preempt mod_unload aarch64
```

출력에서 볼 것: `MODULE_DEVICE_TABLE(of, …)`가 `of:N*T*Cr2,demo` modalias를 만든다. udev/`modprobe`가 DT 노드의 uevent `MODALIAS`와 이 문자열을 맞춰 모듈을 자동 로드한다(D3 §9). (트레이스는 공백만 다른 직전 버전으로 찍었고, 두 `.o`의 `objdump -d` 출력을 `cmp`로 비교해 `.text`가 바이트 단위로 같음을 확인했다.)

---

## 1. 전원 ON → 셸 프롬프트

### 1.1 30초 버전

> "SoC BootROM이 boot pin/fuse로 매체를 골라 첫 단계 로더(TF-A BL1 또는 SPL)를 SRAM에 올린다. BL2가 DRAM을 초기화하고 BL31(EL3 상주 펌웨어, PSCI)과 BL33(U-Boot)을 올린다. U-Boot는 커널 Image·DTB·initramfs를 DRAM에 읽고 `booti`로 x0=DTB 주소, MMU off 상태로 EL2/EL1 커널 진입점에 점프한다. `head.S`가 페이지 테이블을 만들고 MMU를 켠 뒤 `start_kernel()`을 부르고, `setup_arch`가 DT를 풀고, 각종 서브시스템 초기화 뒤 `rest_init`이 PID 1 커널 스레드 `kernel_init`을 만든다. `kernel_init`은 initcall 레벨 순으로 드라이버를 등록·probe하고 rootfs를 마운트한 뒤 `/sbin/init`을 exec한다. systemd가 unit 의존성 그래프를 풀어 `serial-getty@ttyAMA0`을 띄우고, agetty → login → 셸이 프롬프트를 찍는다."

### 1.2 전체 walk

1. **리셋 → BootROM** (마스크 ROM, EL3). boot pin/eFuse로 부트 매체 선택, 첫 이미지를 on-chip SRAM에 올리고 (secure boot면) 서명 검증. → C2 §1–§3
2. **TF-A BL1** (EL3, ROM 또는 플래시). FIP에서 BL2를 읽어 검증·실행. 로그 `BL1: Booting BL2`. → C5 §2
3. **TF-A BL2** (S-EL1). DRAM 초기화(실 보드), BL31·BL32(OP-TEE)·BL33(U-Boot)을 DRAM에 로드·검증, BL1에 SMC로 BL31 실행 요청. QEMU 랩에서 BL33은 DRAM+512 MiB에 올라가서 `-m 512`로는 data abort가 난다(C5 §3, README).
4. **TF-A BL31** (EL3 상주). PSCI·SMC 런타임 서비스 설치 후 `ERET`으로 BL33을 EL2(non-secure)에서 시작. 커널이 나중에 CPU on/off·reset을 PSCI `SMC`로 이 코드에 요청한다. → C5 §4
5. **U-Boot (BL33)**. `board_init_f`(재배치 전) → `relocate_code` → `board_init_r` → `main_loop` → `bootcmd`(bootdelay, bootflow/extlinux 스캔) → `load`로 Image·initrd·DTB → `booti`: DTB에 `/chosen/bootargs`, `linux,initrd-start/end`, memory 노드를 고쳐 쓰고 `x0`=DTB 물리 주소, `x1..x3`=0, MMU·D-cache off로 커널 진입점 점프. → C3, C1 §4
6. **커널 `head.S`** (`arch/arm64/kernel/head.S`): `primary_entry` → `record_mmu_state` → `preserve_boot_args` → `__pi_create_init_idmap` → `init_kernel_el`(EL2면 hyp stub 설치 후 EL1) → `__cpu_setup` → `__primary_switch` → `__enable_mmu` → `__pi_early_map_kernel`(KASLR 재배치) → `__primary_switched`(`VBAR_EL1`, `__fdt_pointer`=x21) → `bl start_kernel`. → C6 §3 (랩 소스 grep 결과가 거기 있다)
7. **`start_kernel()`** (`init/main.c`): `setup_arch`(`setup_machine_fdt` → memblock, `paging_init`, `unflatten_device_tree`) → `trap_init`/`mm_core_init` → `sched_init` → `init_IRQ`(GIC 등록) → `time_init`(arch timer) → `console_init` … → `rest_init`. → C6 §4
8. **`rest_init()`**: `user_mode_thread(kernel_init)` = PID 1, `kernel_thread(kthreadd)` = PID 2, 그리고 **부트 CPU 자신은 `cpu_startup_entry` → `do_idle`로 들어가 PID 0(idle)이 된다.** §3·§7의 실측 스택 맨 아래에 `do_idle ← cpu_startup_entry ← rest_init ← start_kernel ← __primary_switched`가 찍혀 있는 것이 바로 이 사실의 증거다.
9. **`kernel_init()` → `kernel_init_freeable()`**: `do_basic_setup()` → `driver_init()` + **`do_initcalls()`**(early → core → postcore → arch → subsys → fs → rootfs → device → late, `module_init`된 built-in 드라이버는 `device_initcall`). 이때 DT 노드에서 만들어진 `platform_device`와 드라이버가 만나 probe된다(§4와 같은 `really_probe`). 이어서 `wait_for_initramfs()` → `console_on_rootfs()`(`/dev/console`을 fd 0/1/2로) → (initramfs가 없거나 `/init`이 없으면) `prepare_namespace()`로 `root=` 마운트. → C6 §7–§8
10. **`free_initmem()`** — "Freeing unused kernel memory: 4224K" 줄. `.init.text`가 사라진다. → C6 §9
11. **`run_init_process()`** — 순서: `rdinit=`(initramfs `/init`) → `init=` → `CONFIG_DEFAULT_INIT` → `/sbin/init`, `/etc/init`, `/bin/init`, `/bin/sh`. 전부 실패하면 `panic("No working init found")`. 내부는 `kernel_execve` = §6의 exec 경로 그대로다. → C7 §1
12. **PID 1 systemd**: generator 실행(`console=ttyAMA0` → `serial-getty@ttyAMA0.service`), `sysinit.target`(udev coldplug, apparmor, tmpfiles) → `basic.target` → `multi-user.target`/`getty.target`. → L1 §2, C7 §2
13. **`serial-getty@ttyAMA0`** = `agetty` → (autologin) `exec login` → login이 PAM 세션을 열고 **셸을 fork**해서 기다린다 → bash가 프롬프트를 쓴다. BusyBox에서는 `login`이 셸을 **exec**해서 PID가 이어진다(C7 §2.2 실측). 아래 실측 `ps`에서 util-linux 판은 login(283)이 bash(320)의 부모로 남는다.

### 1.3 실측 1 — 리셋에서 initramfs까지 (M10 §1.4 재사용)

TF-A → U-Boot → 커널 전 체인은 M10 §1.4에서 호스트 시각 스탬프(`m10_ts.py`)로 이미 찍었다. 그 로그의 핵심 줄을 그대로 인용한다(왼쪽 = 캡처 시작 후 초).

```text
    0.158 NOTICE:  Booting Trusted Firmware
    0.159 NOTICE:  BL1: Booting BL2
    0.164 NOTICE:  BL2: v2.15.0(release):
    0.215 NOTICE:  BL1: Booting BL31
    0.219 NOTICE:  BL31: v2.15.0(release):
    0.245 U-Boot 2026.10 (Oct 17 2026 - 23:14:06 -0700)
    2.628 Hit any key to stop autoboot: 0
    2.807 ** Booting bootflow 'virtio-blk#31.bootdev.part_1' with extlinux
    2.906 Starting kernel ...
    3.776 [    0.000000] Booting Linux on physical CPU 0x0000000000 [0x411fd070]
    5.418 [    2.149356] Run /init as init process
    5.952 M10_READY since_reset=5.782 uptime=2.580 pre_kernel_clock=3.202
```

출력에서 볼 것: `BL1: Booting BL31`이 BL2 **다음**에 BL1에서 찍힌다 — BL2가 SMC로 BL1에게 "BL31 실행"을 부탁하는 AArch64 TF-A 흐름(C5 §2)의 증거다. `Starting kernel`(2.906)과 printk 0점 사이 약 0.36 s는 "시계 없는 커널 초기" 구간(head.S, `setup_arch`)이다(M10 §1.4의 분석).

### 1.4 실측 2 — 커널에서 systemd → getty → 셸까지 (이 노트에서 실행)

무엇을 확인하나: `Image-trace` + `rootfs-trace` overlay를 `-m 1G`로 부팅해 호스트 시각 스탬프를 붙이고, 자동 로그인된 셸에 명령을 보낸다. 캡처기는 M10의 `m10_ts.py`와 같은 원리(pty + 줄마다 `time.monotonic()`)의 `r2_ts.py`다.

```sh
qemu-img create -f qcow2 -F raw -b /var/lab/out/rootfs-trace.ext4 r2c.qcow2
python3 r2_ts.py bootC.log 'root@[^ ]*:[^#]*# ?$' r2c_cmds.txt -- \
  qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 1 -m 1G -nographic \
  -kernel /var/lab/out/Image-trace -dtb r2.dtb \
  -drive file=r2c.qcow2,if=virtio,format=qcow2 -drive file=xfer.img,if=virtio,format=raw,readonly=on \
  -append "console=ttyAMA0 root=/dev/vda rw rootwait"
```

```text
    0.651 [    0.000000][    T0] Booting Linux on physical CPU 0x0000000000 [0x411fd070]
    0.652 [    0.000000][    T0] Machine model: linux,dummy-virt
    0.677 [    0.280026][    T1] 9000000.pl011: ttyAMA0 at MMIO 0x9000000 (irq = 13, base_baud = 0) is a PL011 rev1
    1.810 [    1.440670][    T1] virtio_blk virtio1: [vda] 8388608 512-byte logical blocks (4.29 GB/4.00 GiB)
    2.318 [    1.951266][    T1] VFS: Mounted root (ext4 filesystem) on device 254:0.
    2.370 [    2.002913][    T1] Freeing unused kernel memory: 4224K
    2.371 [    2.003467][    T1] Run /sbin/init as init process
    2.811 [    2.443141][    T1] systemd[1]: systemd 255.4-1ubuntu8.18 running in system mode (...)
   10.887          Starting apparmor.service - Load AppArmor profiles...
   23.079 (2 of 3) Job dev-ttyAMA0.device/start running (18s / 1min 30s)
   29.268 [  OK  ] Finished systemd-udev-trigger.service - Coldplug All udev Devices.
   29.695 [  OK  ] Finished apparmor.service - Load AppArmor profiles.
   29.709 [  OK  ] Reached target sysinit.target - System Initialization.
   32.817 [  OK  ] Started serial-getty@ttyAMA0.service - Serial Getty on ttyAMA0.
   32.829 [  OK  ] Reached target getty.target - Login Prompts.
   33.979 labtrace login: root (automatic login)
   40.442 [prompt] root@labtrace:~#
```

(색 제어 문자와 중간 줄은 지웠다.) 프롬프트에서 보낸 `systemd-analyze`, `critical-chain`, `ps` 세 명령의 출력(일부 줄만, `ps`는 두 번 부른 결과를 합쳤다):

```text
Startup finished in 2.162s (kernel) + 31.115s (userspace) = 33.278s
getty.target @30.210s
└─serial-getty@ttyAMA0.service @30.167s
  └─systemd-user-sessions.service @28.639s +1.090s
    └─basic.target @27.204s
    PID    PPID COMMAND
      1       0 systemd
    282       1 agetty
    320     283 bash
    283       1 login
```

출력에서 볼 것:

- `[    T1]`(PID 1 = `kernel_init`)이 rootfs 마운트, `Freeing unused kernel memory`, `Run /sbin/init`을 찍는다. walk 9–11단계가 이 세 줄이다. `T0`는 부트 CPU의 `start_kernel` 문맥(PID 0)이다.
- 커널은 2.2 s, 유저 공간은 31 s. 그중 `sysinit.target` 대기 27 s는 **apparmor 프로필 로드와 udev coldplug가 `dev-ttyAMA0.device`를 늦게 알려 준 것**이다 — L2 §8(coldplug 74초 사례)과 같은 모양이고 TCG 부하 탓이 크다.
- `bash`(320)의 부모가 `login`(283)이다. util-linux login은 셸을 fork하고 남는다. "getty가 셸을 띄운다"는 말은 정확히는 getty → exec login → fork/exec 셸이다.

```svg
<svg viewBox="0 0 680 260" xmlns="http://www.w3.org/2000/svg">
<text x="40" y="40" font-size="12">A. M10 §1.4 실측 — 리셋 → initramfs READY 5.78 s (TF-A·U-Boot 포함)</text>
<rect x="40" y="50" width="9" height="30" fill="#888" fill-opacity="0.35" stroke="#888"/>
<rect x="49" y="50" width="37" height="30" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<rect x="86" y="50" width="202" height="30" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<rect x="288" y="50" width="27" height="30" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<rect x="315" y="50" width="251" height="30" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<rect x="566" y="50" width="52" height="30" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<text x="40" y="114" font-size="12">TF-A+U-Boot 0.46 s</text>
<text x="187" y="98" font-size="12" text-anchor="middle">bootdelay 2.02 s</text>
<text x="440" y="98" font-size="12" text-anchor="middle">커널 2.51 s</text>
<text x="640" y="114" font-size="12" text-anchor="end">init→READY 0.52 s</text>
<text x="40" y="150" font-size="12">B. 이 노트 실측 — -kernel 부팅 → systemd → 자동 로그인 셸 40.4 s</text>
<rect x="49" y="160" width="25" height="30" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<rect x="74" y="160" width="390" height="30" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<rect x="464" y="160" width="45" height="30" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
<rect x="509" y="160" width="109" height="30" fill="#888" fill-opacity="0.35" stroke="#888"/>
<text x="40" y="208" font-size="12">커널 1.7 s</text>
<text x="269" y="208" font-size="12" text-anchor="middle">systemd → sysinit.target 27.3 s (apparmor·udev)</text>
<text x="487" y="224" font-size="12" text-anchor="middle">getty 3.1 s</text>
<text x="640" y="208" font-size="12" text-anchor="end">login→bash 7.6 s</text>
<text x="40" y="250" font-size="12">두 막대는 배율이 다르다 (A: 100 px/s, B: 14 px/s). TCG 수치 — 실제 보드 값 아님</text>
</svg>
```
그림 2 — 전원 ON → 셸의 두 실측 타임라인. A는 펌웨어 체인을 포함한 M10 실측, B는 이 노트의 systemd 부팅. 실제 제품에서도 "부팅이 느리다"의 범인은 대개 커널이 아니라 bootdelay와 유저 공간 의존성 대기다.

### 1.5 꼬리 질문

- **"커널이 콘솔에 아무것도 안 찍어요. 어디부터 봅니까?"** — `earlycon=pl011,0x9000000`(또는 DT `stdout-path`)로 `console_init` 전 단계부터 본다. 아무것도 안 나오면 U-Boot→커널 계약(DTB 주소 x0, Image 로드 주소 2 MiB 정렬, MMU off)을 의심. 그다음 `__log_buf`를 JTAG로 덤프. → C6 §5, M1
- **"initramfs는 왜 쓰나요?"** — root 장치 드라이버가 모듈이거나, 암호화·A/B 선택·fsck를 루트 마운트 전에 해야 할 때. 커널은 `populate_rootfs`로 cpio를 rootfs(tmpfs)에 풀고 `/init`을 실행한다. → C7 §3
- **"PSCI는 언제 다시 쓰이나요?"** — 보조 CPU 기동(`CPU_ON`), CPU idle(`CPU_SUSPEND`), `reboot`/`poweroff`(`SYSTEM_RESET`/`SYSTEM_OFF`). 전부 EL3 BL31로 `SMC`. → C5 §4
- **"initcall 순서가 왜 중요하죠?"** — 공급자(clock, regulator, GPIO, pinctrl)가 소비자보다 먼저 준비돼야 한다. 순서가 어긋나면 `-EPROBE_DEFER`로 미뤄졌다가 `late_initcall`의 deferred probe 재시도에서 붙는다. → C6 §7, G3 §9
- **"부팅 시간을 줄이라면?"** — 실측 먼저(bootstage, `initcall_debug`, `systemd-analyze critical-chain`). 그림 2의 가장 긴 막대부터. → C7 §5, M10

---

## 2. `open("/dev/x")` → 드라이버의 `.open`, 그리고 `read()`

### 2.1 30초 버전

> "유저가 `svc #0`으로 커널에 들어오면 `el0_svc` → `invoke_syscall`이 `openat` 핸들러를 부르고 `do_sys_openat2`로 간다. 먼저 빈 fd 번호를 예약하고, `path_openat`이 dcache에서 `/`, `dev`, `r2_demo`를 한 성분씩 찾는다(RCU-walk). 찾은 inode가 char device면 inode의 `i_fop`은 범용 `def_chr_fops`이고, `do_dentry_open`이 그 `.open`인 `chrdev_open`을 부른다. `chrdev_open`은 inode의 `dev_t`로 `cdev_map`에서 드라이버의 `cdev`를 찾아 `file->f_op`을 드라이버 fops로 **바꿔 끼우고** 드라이버 `.open`을 부른다. 성공하면 `fd_install`로 fd 테이블에 `struct file`을 꽂고 fd 번호를 돌려준다. 이후 `read(fd)`는 fd → `struct file` → `f_op->read`로 바로 드라이버에 간다."

### 2.2 전체 walk

1. 유저: `openat(AT_FDCWD, "/dev/r2_demo", O_RDONLY)` → `svc #0`, x8 = `__NR_openat`(56). → E6 §1
2. `el0t_64_sync` → `el0t_64_sync_handler` → `el0_svc` → `do_el0_svc` → `el0_svc_common` → `invoke_syscall` → `__arm64_sys_openat` → `do_sys_open` → `do_sys_openat2`. (이 체인은 §4의 실측 스택에 그대로 찍혀 있다.)
3. `getname_flags` — 유저 문자열을 커널 `struct filename`으로 복사(`strncpy_from_user`). 나쁜 포인터면 여기서 `-EFAULT`. → E6 §3–§4
4. `get_unused_fd_flags` → `alloc_fd` — **fd 번호를 먼저 예약**(아직 `struct file`은 꽂지 않는다). 실측 `ret=0x3`.
5. `do_filp_open` → `path_openat` → `alloc_empty_file`(새 `struct file`, LSM `security_file_alloc`) → `path_init`(루트에서 시작) → `link_path_walk`(`/dev` 성분: `inode_permission` → `walk_component` → `lookup_fast`/`__d_lookup_rcu` → `step_into`, 마운트 포인트면 `__lookup_mnt`로 devtmpfs로 건너감) → 마지막 성분 `lookup_fast` → `complete_walk`(`try_to_unlazy`: RCU-walk를 ref-walk로 확정). → F12 §3
6. `may_open` → `inode_permission` + `devcgroup_check_permission`(컨테이너의 장치 cgroup) + LSM.
7. `vfs_open` → `do_dentry_open`: `file->f_op = inode->i_fop`. char device inode는 생성될 때 `init_special_inode`가 `i_fop = &def_chr_fops`, `i_rdev = dev_t`로 만들어 둔다. 그래서 여기서 불리는 `.open`은 **`chrdev_open`**이다. → F12 §4
8. `chrdev_open`: `inode->i_cdev`가 비었으면 `kobj_lookup(cdev_map, inode->i_rdev, &idx)`로 `cdev_add` 때 등록된 `cdev`를 찾고(`exact_match`), `try_module_get(owner)`으로 모듈 참조를 잡고, **`replace_fops(filp, cdev->ops)`** → 드라이버 `.open`(`r2_open`) 호출. → G4 §2
9. `security_file_post_open`(IMA) → `terminate_walk`(경로 참조 해제) → `fd_install(3, file)` → 반환값 3이 x0으로 유저에게.
10. `read(3, buf, 63)` → `ksys_read` → `fdget_pos`(fd → `struct file`, `f_pos` 락) → `vfs_read` → `rw_verify_area`(LSM) → **`file->f_op->read` = `r2_read`** → `simple_read_from_buffer` → `copy_to_user` → 19 반환. → E6 §2, G4 §6

### 2.3 실측 — `function_graph`로 본 open과 read (1회차 부팅)

무엇을 확인하는 코드인가: 정적 바이너리 `r2_open`이 `/dev/r2_demo`를 열고 한 번 읽는다. 트레이스는 이 프로세스의 `do_sys_openat2`와 `ksys_read` 아래만 찍는다.

```c
/* r2_open: open /dev/r2_demo, read once, close (static, for ftrace) */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void)
{
	char buf[64];
	int fd = open("/dev/r2_demo", O_RDONLY);
	if (fd < 0) { perror("open"); exit(1); }
	ssize_t n = read(fd, buf, sizeof(buf) - 1);
	if (n < 0) { perror("read"); exit(1); }
	buf[n] = '\0';
	printf("fd=%d read %zd bytes: %s", fd, n, buf);
	close(fd);
	return 0;
}
```

```sh
cd /sys/kernel/tracing
echo do_sys_openat2 > set_graph_function; echo ksys_read >> set_graph_function
echo 1 > options/funcgraph-retval
echo function_graph > current_tracer
sh -c 'echo $$ > /sys/kernel/tracing/set_ftrace_pid; echo 1 > /sys/kernel/tracing/tracing_on; exec /r2_open'
echo 0 > tracing_on; cat trace
```

```text
fd=3 read 19 bytes: hello from r2_demo
 0)               |  do_sys_openat2() {
 0)               |    getname_flags() {
 0) + 14.800 us   |    } /* getname_flags ret=0xffff000006b3d000 */
 0)               |    get_unused_fd_flags() {
 0)               |      alloc_fd() {
 0) + 14.832 us   |      } /* alloc_fd ret=0x3 */
 0)               |    do_filp_open() {
 0)               |      path_openat() {
 0) + 28.336 us   |        } /* alloc_empty_file ret=0xffff000004f3a600 */
 0)               |        link_path_walk() {
 0)               |          walk_component() {
 0)               |            lookup_fast() {
 0)   2.000 us    |              __d_lookup_rcu(); /* ret=0xffff000004864900 */
 0)               |            step_into() {
 0)   1.664 us    |              __lookup_mnt(); /* ret=0xffff000004ed9c00 */
 0) + 42.880 us   |        } /* link_path_walk ret=0x0 */
 0)               |        lookup_fast() {
 0)   1.664 us    |          __d_lookup_rcu(); /* ret=0xffff0000048933c0 */
 0) + 16.176 us   |        } /* complete_walk ret=0x0 */
 0)               |        may_open() {
 0) + 22.624 us   |            } /* devcgroup_check_permission ret=0x0 */
 0) + 59.248 us   |        } /* may_open ret=0x0 */
 0)               |        vfs_open() {
 0)               |          do_dentry_open() {
 0)               |            chrdev_open() {
 0)               |              kobj_lookup() {
 0)   3.216 us    |                exact_match(); /* ret=0xffffb3aa9701c100 */
 0) + 47.872 us   |              } /* kobj_lookup ret=0xffffb3aa9701c100 */
 0)   1.920 us    |              try_module_get(); /* ret=0x1 */
 0)               |              r2_open [r2_demo]() {
 0) ! 414.208 us  |              } /* r2_open [r2_demo] ret=0x0 */
 0) ! 510.960 us  |            } /* chrdev_open ret=0x0 */
 0) ! 555.200 us  |          } /* do_dentry_open ret=0x0 */
 0) ! 782.288 us  |      } /* path_openat ret=0xffff000004f3a600 */
 0)               |    fd_install() {
 0) ! 851.424 us  |  } /* do_sys_openat2 ret=0x3 */
 0)               |  ksys_read() {
 0)   2.496 us    |    fdget_pos(); /* ret=0xffff000004f3a600 */
 0)               |    vfs_read() {
 0) + 20.736 us   |      } /* rw_verify_area ret=0x0 */
 0)               |      r2_read [r2_demo]() {
 0) + 46.672 us   |        simple_read_from_buffer(); /* ret=0x13 */
 0) + 61.296 us   |      } /* r2_read [r2_demo] ret=0x13 */
 0) ! 108.368 us  |    } /* vfs_read ret=0x13 */
 0) ! 130.592 us  |  } /* ksys_read ret=0x13 */
```

(실제 트레이스는 약 260줄이다. LSM 훅, spinlock, RCU, printk 내부 줄을 지웠고 남긴 줄은 원문 그대로다.)

출력에서 볼 것:

- `alloc_fd ret=0x3`이 `path_openat`보다 **먼저** 나온다. fd 번호 예약 → 파일 열기 → `fd_install`의 순서다. 열기가 실패하면 `put_unused_fd`로 번호만 되돌린다.
- `path_openat ret=0xffff000004f3a600` = `alloc_empty_file`이 만든 `struct file`, 그리고 `ksys_read`의 `fdget_pos ret=0xffff000004f3a600` — **같은 주소**다. "fd는 `struct file *` 배열의 인덱스"(A3 §1)를 주소로 확인한 것이다.
- `chrdev_open` 안에 `kobj_lookup` → `exact_match`가 `cdev`를 돌려주고, 바로 아래에서 `r2_open [r2_demo]`가 불린다. 그 사이의 `replace_fops`는 인라인 매크로라 트레이스에 안 보인다(소스 `fs/char_dev.c:412`).
- `r2_open`의 414 µs 중 399 µs는 `pr_info`(printk) 비용이었다(트레이스에서 지운 부분). TCG라 절대값은 의미 없지만 **open 경로에서 printk 한 줄이 경로 전체보다 비쌀 수 있다**는 비율은 실제 보드에서도 맞다.
- read 쪽은 `vfs_read` → `r2_read`로 **VFS 탐색 없이** 곧장 간다. 두 번째 이후 시스템 콜이 싼 이유다.

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="20" y="22" font-size="13">open() 경로(왼쪽)와 read() 경로(오른쪽) — 6.18.55 실측 함수</text>
<g font-size="12">
<rect x="20" y="40" width="320" height="26" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="30" y="58">openat(AT_FDCWD, "/dev/r2_demo") — svc #0</text>
<rect x="20" y="74" width="320" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="30" y="92">el0_svc → invoke_syscall → sys_openat</text>
<rect x="20" y="108" width="320" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="30" y="126">do_sys_openat2</text>
<rect x="20" y="142" width="320" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="30" y="160">get_unused_fd_flags → alloc_fd (= 3)</text>
<rect x="20" y="176" width="320" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="30" y="194">do_filp_open → path_openat</text>
<rect x="20" y="210" width="320" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="30" y="228">link_path_walk + lookup_fast (dcache)</text>
<rect x="20" y="244" width="320" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="30" y="262">may_open → inode_permission, devcgroup</text>
<rect x="20" y="278" width="320" height="26" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="30" y="296">do_dentry_open: f_op = def_chr_fops</text>
<rect x="20" y="312" width="320" height="26" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="30" y="330">chrdev_open: kobj_lookup(cdev_map, i_rdev)</text>
<rect x="20" y="346" width="320" height="26" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="30" y="364">replace_fops → r2_fops, then r2_open()</text>
<rect x="20" y="380" width="320" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="30" y="398">fd_install(3, file) → return 3</text>
<rect x="360" y="40" width="300" height="26" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="370" y="58">read(3, buf, 63) — svc #0</text>
<rect x="360" y="74" width="300" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="370" y="92">ksys_read → fdget_pos (fd → file)</text>
<rect x="360" y="108" width="300" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="370" y="126">vfs_read → rw_verify_area (LSM)</text>
<rect x="360" y="142" width="300" height="26" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="370" y="160">file->f_op->read = r2_read</text>
<rect x="360" y="176" width="300" height="26" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="370" y="194">simple_read_from_buffer → copy_to_user</text>
<rect x="360" y="210" width="300" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="370" y="228">return 19 (0x13) → x0 → eret</text>
<text x="370" y="276">핵심: 장치 노드의 inode는</text>
<text x="370" y="294">i_rdev(510:0)만 들고 있다.</text>
<text x="370" y="312">첫 open 때 chrdev_open이</text>
<text x="370" y="330">file의 f_op를 드라이버 fops로</text>
<text x="370" y="348">바꿔 끼운다 (F12 §4, G4 §2).</text>
</g>
<g stroke="currentColor" marker-end="url(#ah3)">
<line x1="180" y1="66" x2="180" y2="73"/><line x1="180" y1="100" x2="180" y2="107"/><line x1="180" y1="134" x2="180" y2="141"/>
<line x1="180" y1="168" x2="180" y2="175"/><line x1="180" y1="202" x2="180" y2="209"/><line x1="180" y1="236" x2="180" y2="243"/>
<line x1="180" y1="270" x2="180" y2="277"/><line x1="180" y1="304" x2="180" y2="311"/><line x1="180" y1="338" x2="180" y2="345"/>
<line x1="180" y1="372" x2="180" y2="379"/>
<line x1="510" y1="66" x2="510" y2="73"/><line x1="510" y1="100" x2="510" y2="107"/><line x1="510" y1="134" x2="510" y2="141"/>
<line x1="510" y1="168" x2="510" y2="175"/><line x1="510" y1="202" x2="510" y2="209"/>
</g>
</svg>
```
그림 3 — open과 read의 함수 경로. 주황 두 칸이 "VFS가 드라이버를 찾는" 순간, 초록이 드라이버 코드다. open은 탐색이 비싸고 read는 fd 한 번 따라가면 끝이다.

### 2.4 꼬리 질문

- **"`/dev/x`가 없으면? 있는데 드라이버가 없으면?"** — 노드가 없으면 `path_openat`의 마지막 `lookup`이 실패해 `-ENOENT`. 노드는 있는데 `cdev_map`에 그 `dev_t`가 없으면 `chrdev_open`이 `-ENXIO`(`No such device or address`). 증상이 다르다. → A3 §9, G4 §3
- **"누가 `/dev/r2_demo`를 만들었나요?"** — `device_create` → `device_add` → devtmpfs가 커널 안에서 노드 생성 + uevent → udev가 권한·심볼릭 링크 정리. 실측 `crw------- 510, 0`. → G4 §3, L2
- **"모듈을 open한 채로 rmmod하면?"** — `chrdev_open`의 `try_module_get(cdev->owner)`가 refcount를 잡고 있어 `rmmod`가 `EBUSY`로 거절된다. `.owner = THIS_MODULE`이 빠지면 이 보호가 사라진다. → G4 §8, E3 §8
- **"RCU-walk가 뭐죠?"** — 락·refcount 없이 dcache를 seqcount로 읽고, 마지막에 `try_to_unlazy`로 참조를 확정한다. 실패하면 ref-walk로 다시. 실측의 `__d_lookup_rcu`와 `try_to_unlazy`가 그것이다. → F12 §3
- **"블록 장치나 일반 파일이면 뭐가 달라지나요?"** — 일반 파일은 파일시스템 inode의 `i_fop`(ext4 `ext4_file_operations`)이 바로 쓰이고, read는 page cache(`filemap_read`)를 거친다. → F12 §6–§7

---

## 3. HW 인터럽트 → `read()`에서 잠든 유저 스레드가 깨어난다

### 3.1 30초 버전

> "스레드는 `evdev_read` 안의 `wait_event_interruptible`에서 상태를 `TASK_INTERRUPTIBLE`로 바꾸고 wait queue에 자기를 건 뒤 `schedule()`로 runqueue에서 빠져 있다. 핀이 바뀌면 GPIO 컨트롤러가 GIC에 SPI를 올리고, CPU는 IRQ 예외로 `el1h_64_irq`(유저 실행 중이었다면 `el0t_64_irq`) → `gic_handle_irq`에서 IAR을 읽어 hwirq를 얻고, `generic_handle_domain_irq`가 irq_domain으로 Linux IRQ 번호를 찾아 flow handler와 드라이버 ISR을 부른다. ISR은 짧게 끝내고 나머지는 bottom half(여기서는 hrtimer debounce, 일반적으로는 threaded IRQ나 workqueue)로 미룬다. bottom half가 데이터를 버퍼에 넣고 `wake_up_interruptible`을 부르면 `try_to_wake_up`이 태스크를 runqueue에 넣고 need_resched를 세운다. IRQ에서 나올 때(또는 다음 스케줄 지점에서) `schedule()`이 그 스레드로 전환하고, 스레드는 `wait_event`의 조건을 다시 확인한 뒤 `copy_to_user`로 데이터를 복사하고 `eret`으로 유저에 돌아간다."

### 3.2 무엇으로 증명했나 — QEMU 전원 키 = 진짜 GPIO 인터럽트

QEMU `virt`의 전원 버튼은 PL061 GPIO 핀 3에 연결되어 있고, DT의 `gpio-keys` 노드가 그 핀을 `KEY_POWER`(116)로 보고한다(I3 §1과 같은 장치). 호스트에서 QEMU 모니터에 `system_powerdown`을 보내면 핀이 올라갔다가 약 100 ms 뒤 내려간다. 즉 **에뮬레이션된 하드웨어가 GIC로 진짜 SPI를 올리는** 경로다. gpio-sim(irq_sim)과 달리 GIC → 체인 핸들러 → ISR이 전부 실제 코드다.

무엇을 확인하는 코드인가: `/dev/input/event0`를 열고 `read()`에서 잠들어 있다가 키 이벤트를 받는다.

```c
/* r2_evread: block in read() on /dev/input/event0 until gpio-keys reports a key */
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	const char *dev = argc > 1 ? argv[1] : "/dev/input/event0";
	struct input_event ev;
	int fd = open(dev, O_RDONLY);
	if (fd < 0) { perror("open"); exit(1); }
	for (int i = 0; i < 4; i++) {
		ssize_t n = read(fd, &ev, sizeof(ev));     /* sleeps in evdev_read() */
		if (n != (ssize_t)sizeof(ev)) { perror("read"); exit(1); }
		printf("event type=%u code=%u value=%d\n", ev.type, ev.code, ev.value);
		if (ev.type == EV_KEY && ev.value == 0)
			break;
	}
	close(fd);
	return 0;
}
```

게스트 쪽 트레이스 설정(발췌)과 호스트 쪽 주입:

```sh
# guest (r2b.sh)
echo 'p:gk_isr gpio_keys_gpio_isr irq=$arg1' >> kprobe_events
echo 'p:gk_tmr gpio_keys_debounce_timer' >> kprobe_events
echo 'p:evpass evdev_pass_values.part.0 count=$arg3' >> kprobe_events
echo 'r:evread_ret evdev_read ret=$retval' >> kprobe_events
echo stacktrace > events/kprobes/gk_isr/trigger
echo stacktrace > events/kprobes/gk_tmr/trigger
echo stacktrace > events/kprobes/evpass/trigger
for e in kprobes/gk_isr kprobes/gk_tmr kprobes/evpass kprobes/evread_ret irq/irq_handler_entry \
         irq/irq_handler_exit sched/sched_waking sched/sched_switch; do echo 1 > events/$e/enable; done
/r2_evread > /tmp/ev.out 2>&1 &
# host (VM side), QEMU started with -monitor tcp:127.0.0.1:42200,server,nowait
echo system_powerdown | socat -t1 - tcp:127.0.0.1:42200
```

`evdev_pass_values`는 GCC가 쪼갠 `.part.0` 이름으로만 존재했다 — 첫 시도의 `p:evpass evdev_pass_values`는 `write error: Invalid argument`로 실패했다. 트레이스할 함수 이름은 `available_filter_functions`나 `System.map`에서 확인한다(§6의 `.isra.0`도 같은 함정).

### 3.3 실측 — 잠에서 깨어날 때까지 (2회차 부팅, 발췌)

```text
event type=1 code=116 value=1
event type=0 code=0 value=0
event type=1 code=116 value=0
 18:          2 9030000.pl061   3 Edge      GPIO Key Poweroff
       r2_evread-71  [000] d..2.  2.226858: sched_switch: prev_comm=r2_evread prev_pid=71 prev_prio=120 prev_state=S ==> next_comm=swapper/0 ...
          <idle>-0   [000] d.h1.  3.592858: irq_handler_entry: irq=18 name=GPIO Key Poweroff
          <idle>-0   [000] d.h1.  3.593240: gk_isr: (gpio_keys_gpio_isr+0x0/0xf0) irq=0x12
          <idle>-0   [000] d.h1.  3.594400: <stack trace>
 => gpio_keys_gpio_isr
 => handle_irq_event
 => handle_edge_irq
 => handle_irq_desc
 => generic_handle_domain_irq
 => pl061_irq_handler
 => handle_irq_desc
 => generic_handle_domain_irq
 => gic_handle_irq
 => call_on_irq_stack
 => do_interrupt_handler
 => el1_interrupt
 => el1h_64_irq_handler
 => el1h_64_irq
 => default_idle_call
 => do_idle
 => cpu_startup_entry
 => rest_init
 => start_kernel
 => __primary_switched
          <idle>-0   [000] d.h1.  3.595083: irq_handler_exit: irq=18 ret=handled
          <idle>-0   [000] d.h1.  3.600169: irq_handler_entry: irq=11 name=arch_timer
          <idle>-0   [000] d.h1.  3.600264: gk_tmr: (gpio_keys_debounce_timer+0x0/0x80)
 => gpio_keys_debounce_timer
 => hrtimer_interrupt
 => arch_timer_handler_virt
 => handle_percpu_devid_irq
 => handle_irq_desc
 => generic_handle_domain_irq
 => gic_handle_irq
          <idle>-0   [000] dNh2.  3.601924: evpass: (evdev_pass_values.part.0+0x0/0x280) count=0x2
 => evdev_pass_values.part.0
 => input_pass_values
 => input_event_dispose
 => input_handle_event
 => input_event
 => gpio_keys_debounce_timer
 => __hrtimer_run_queues
 => hrtimer_interrupt
          <idle>-0   [000] dNh5.  3.602341: sched_waking: comm=r2_evread pid=71 prio=120 target_cpu=000
          <idle>-0   [000] dNh1.  3.602522: irq_handler_exit: irq=11 ret=handled
     kworker/0:2-57  [000] d..2.  3.603188: sched_switch: prev_comm=kworker/0:2 prev_pid=57 prev_prio=120 prev_state=I ==> next_comm=r2_evread next_pid=71 next_prio=120
       r2_evread-71  [000] d..1.  3.604010: evread_ret: (vfs_read+0xcc/0x350 <- evdev_read) ret=24
```

(각 스택의 맨 위 kprobe 자체 프레임 8줄 — `kprobe_trace_func` … `el1h_64_sync` — 과, 두 번째·세 번째 스택의 `gic_handle_irq` 아래 공통 부분을 지웠다. 그 사이의 arch_timer 틱, rcu 스레드 줄도 지웠다.)

출력에서 볼 것:

- **잠**: 2.227 s에 `r2_evread`가 `prev_state=S`로 CPU를 내놓았다. 1.4 s 동안 runqueue에 없다(CPU는 idle).
- **하드웨어 경로**(첫 스택, 아래에서 위로): idle 루프(`default_idle_call` → WFI) 중에 IRQ 예외 → `el1h_64_irq` → `gic_handle_irq`(GIC IAR에서 SPI 번호) → `generic_handle_domain_irq`(GIC 도메인) → **`pl061_irq_handler`**(chained handler: GPIO 칩 자체의 IRQ) → 다시 `generic_handle_domain_irq`(PL061 도메인, hwirq 3) → `handle_edge_irq`(엣지 flow) → `handle_irq_event` → (`__handle_irq_event_percpu`, 스택에서는 빠짐) → `gpio_keys_gpio_isr`. **IRQ 도메인이 두 겹**이다. `/proc/interrupts`에 `9030000.pl061 3 Edge`로 보이는 줄이 PL061 도메인의 hwirq 3이고, Linux IRQ 번호 18은 커널이 붙인 번호다(F6 §3).
- **ISR은 짧다**: 3.592858 → 3.595083. `gpio_keys_gpio_isr`는 이벤트를 보고하지 않고 **5 ms debounce hrtimer만 건다**(PL061이 `gpiod_cansleep() == false`라 workqueue 대신 hrtimer를 고른다 — `drivers/input/keyboard/gpio_keys.c:567`).
- **bottom half**: 7.4 ms 뒤 **다른 IRQ**(irq 11 = arch timer)의 `hrtimer_interrupt` → `gpio_keys_debounce_timer` → `input_event` → `input_pass_values` → `evdev_pass_values`(이벤트 2개 = `EV_KEY` + `EV_SYN`)가 버퍼에 넣고 `wake_up_interruptible_poll(&client->wait, …)`을 부른다(`drivers/input/evdev.c:289`). 그 결과가 `sched_waking: comm=r2_evread`다. 깨우는 쪽은 hardirq 문맥(`h` 플래그)이다.
- **빠진 한 칸**: 첫 스택에 `__handle_irq_event_percpu`(실제로 `action->handler`를 부르는 함수)가, `evpass` 스택에 `evdev_events`가 없다. `gk_tmr` 스택에는 `__hrtimer_run_queues`가 없는데 `evpass` 스택(더 깊은 곳에서 찍힌)에는 있다. 모두 §4.3의 "`+0x0` kprobe는 직전 호출자를 빠뜨린다" 규칙이다.
- **`N` 플래그**: `dNh5.`의 `N` = need_resched가 섰다. IRQ에서 나온 직후(먼저 runqueue에 있던 kworker 다음) `sched_switch ==> next_comm=r2_evread`.
- **유저 복귀**: `evdev_read`가 24(= `sizeof(struct input_event)`)를 반환. IRQ부터 read 반환까지 11.2 ms 중 5 ms는 debounce 설정값, 나머지는 TCG 비용이다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" font-size="13">KEY_POWER 누름 1회 — 실측 타임라인 (3.590 ~ 3.606 s, QEMU TCG)</text>
<g font-size="12">
<text x="10" y="54">HW (pl061 핀 3)</text>
<text x="10" y="104">hardirq irq18 GPIO</text>
<text x="10" y="154">hardirq irq11 timer</text>
<text x="10" y="204">스케줄러</text>
<text x="10" y="254">r2_evread (유저)</text>
</g>
<g stroke="currentColor" stroke-opacity="0.25"><line x1="150" y1="50" x2="660" y2="50"/><line x1="150" y1="100" x2="660" y2="100"/><line x1="150" y1="150" x2="660" y2="150"/><line x1="150" y1="200" x2="660" y2="200"/><line x1="150" y1="250" x2="660" y2="250"/></g>
<circle cx="239" cy="50" r="5" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
<text x="160" y="40" font-size="12">핀 3 상승 엣지 → GIC SPI</text>
<line x1="239" y1="56" x2="239" y2="89" stroke="currentColor" marker-end="url(#ah4)"/>
<rect x="239" y="90" width="70" height="20" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="239" y="124" font-size="12">gpio_keys_gpio_isr → hrtimer_start(5 ms)</text>
<rect x="468" y="140" width="73" height="20" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="528" y="174" font-size="12" text-anchor="end">debounce_timer → input_event → evdev → wake_up</text>
<circle cx="536" cy="200" r="5" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
<circle cx="562" cy="200" r="5" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
<line x1="536" y1="161" x2="536" y2="193" stroke="currentColor" marker-end="url(#ah4)"/>
<text x="560" y="222" font-size="12" text-anchor="end">sched_waking(3.6023) → sched_switch(3.6032)</text>
<line x1="150" y1="250" x2="562" y2="250" stroke="#4a7bd0" stroke-width="3" stroke-dasharray="6 4"/>
<text x="155" y="242" font-size="12">S 상태 (wait_event_interruptible, 2.227 s부터 잠)</text>
<line x1="562" y1="250" x2="600" y2="250" stroke="#4a7bd0" stroke-width="5"/>
<line x1="562" y1="206" x2="562" y2="244" stroke="currentColor" marker-end="url(#ah4)"/>
<circle cx="588" cy="250" r="5" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<text x="640" y="270" font-size="12" text-anchor="end">evdev_read 반환 24 B (3.6040)</text>
<line x1="150" y1="290" x2="650" y2="290" stroke="currentColor"/>
<g font-size="12" text-anchor="middle">
<line x1="150" y1="290" x2="150" y2="295" stroke="currentColor"/><text x="150" y="306">3.590</text>
<line x1="275" y1="290" x2="275" y2="295" stroke="currentColor"/><text x="275" y="306">3.594</text>
<line x1="400" y1="290" x2="400" y2="295" stroke="currentColor"/><text x="400" y="306">3.598</text>
<line x1="525" y1="290" x2="525" y2="295" stroke="currentColor"/><text x="525" y="306">3.602</text>
<line x1="650" y1="290" x2="650" y2="295" stroke="currentColor"/><text x="650" y="306">3.606</text>
</g>
<text x="10" y="324" font-size="12">IRQ → read() 반환 11.2 ms 중 5 ms는 gpio-keys 소프트웨어 debounce (TCG 실측)</text>
</svg>
```
그림 4 — 인터럽트 하나가 잠든 스레드를 깨우기까지. 하드웨어 IRQ(빨강)는 2 ms 만에 끝나고, 실제 "깨우기"는 5 ms 뒤 다른 IRQ(타이머)의 bottom half에서 일어난다. 유저 스레드는 그동안 runqueue에 없다(점선).

### 3.4 일반화 — 센서 드라이버라면 어디가 바뀌나

이 실측은 hrtimer bottom half였다. 면접에서 "I2C IMU의 data-ready 인터럽트"를 물으면 bottom half가 바뀐다. I2C 읽기는 잠드는 작업이라 hardirq에서 할 수 없으므로:

1. `request_threaded_irq(irq, NULL, imu_thread_fn, IRQF_ONESHOT, …)` — hardirq 쪽은 core가 `irq_default_primary_handler`로 `IRQ_WAKE_THREAD`만 돌려주고 라인을 mask한다(ONESHOT).
2. `__irq_wake_thread` → `irq/NN-imu` 커널 스레드 `wake_up_process` (F6 §6 실측 트레이스에 `__irq_wake_thread`가 찍혀 있다).
3. 스레드 문맥에서 `i2c_transfer`(잠듦 허용) → 링 버퍼에 저장 → `wake_up_interruptible(&dev->wq)` → IRQ unmask.
4. 이후는 위 실측과 같다: `try_to_wake_up` → `schedule()` → 드라이버 `.read`가 `wait_event_interruptible`에서 돌아와 `copy_to_user`.

| 단계 | 이 실측 (gpio-keys) | threaded IRQ 드라이버 | workqueue 드라이버 |
|---|---|---|---|
| top half | `gpio_keys_gpio_isr` (hardirq) | core primary → `IRQ_WAKE_THREAD` | ISR이 `schedule_work` |
| bottom half 문맥 | hrtimer 콜백 (hardirq) | `irq/NN-name` kthread (잠듦 가능) | kworker (잠듦 가능) |
| 깨우는 함수 | `wake_up_interruptible_poll` | `wake_up_interruptible` | `wake_up_interruptible` |
| 깊이 공부한 곳 | I3 §5 | F6 §5, F7 §8 | F7 §6 |

### 3.5 꼬리 질문

- **"wake_up과 wait_event 사이에 lost wakeup이 안 생기나요?"** — `wait_event`는 상태를 `TASK_INTERRUPTIBLE`로 **먼저** 바꾸고 wait queue에 건 **다음** 조건을 검사한다. 그 사이 wake_up이 오면 상태가 `TASK_RUNNING`으로 돌아가 `schedule()`이 바로 돌아온다. 깨우는 쪽은 조건 변수를 먼저 쓰고 깨운다. → F3 §5
- **"깨어난 스레드가 바로 도나요?"** — 아니다. runqueue에 들어가고 need_resched가 설 뿐이다. 실제 전환은 IRQ 복귀 시 preemption 지점이나 현재 태스크의 다음 `schedule()`. 실측에서도 먼저 깨어 있던 kworker가 한 번 돈 뒤 전환됐다. SCHED_FIFO면 바로 선점. → F1 §7–§8
- **"IRQ가 유저 코드 실행 중에 오면?"** — 진입이 `el0t_64_irq` → `el0_interrupt`가 되고, 복귀 때 `exit_to_user_mode` 에서 need_resched·시그널을 처리한다. 이 실측은 CPU가 idle이라 `el1h_64_irq`였다. → E6 §1, F6 §1
- **"hwirq와 Linux IRQ 번호는?"** — 실측: GIC 위 PL061 칩의 hwirq 3 → Linux IRQ 18, virtio는 `GIC-0 79` → IRQ 15. 매핑은 irq_domain이 한다. → F6 §3
- **"poll/epoll이면 뭐가 다르죠?"** — 같은 `wake_up_interruptible_poll`이 wait queue의 `ep_poll_callback` 엔트리를 부른다. 드라이버는 `.poll`에서 `poll_wait`로 같은 wait queue를 등록만 하면 된다. → G6 §1, §13

---

## 4. `insmod foo.ko` → `probe()`가 돌기까지

### 4.1 30초 버전

> "kmod `insmod`는 `.ko`를 열어 `finit_module(fd)`를 부른다(BusyBox `insmod`는 파일을 읽어 `init_module(buf)`). 커널은 `load_module`에서 ELF를 검사하고(vermagic, 서명, 라이선스), 모듈 메모리를 할당해 섹션을 복사하고 재배치·심볼 해결을 한 뒤 sysfs에 모듈을 등록한다. `do_init_module`이 `module_init` 함수를 `do_one_initcall`로 부르는데, `module_platform_driver`면 그게 `platform_driver_register`다. `driver_register` → `bus_add_driver` → `driver_attach`가 버스의 모든 장치에 대해 `__driver_attach`를 돌며 `platform_match`로 DT `compatible`을 `of_match_table`과 비교하고, 맞는 장치에 대해 `driver_probe_device` → `really_probe` → `platform_probe` → 드라이버 `probe()`를 부른다. 장치가 나중에 생기면 반대 방향(`device_add` → `bus_probe_device`)으로 같은 `really_probe`에 도착한다."

### 4.2 전체 walk

1. 유저: kmod `insmod` → `finit_module(fd, "", 0)`. (BusyBox `insmod`는 `init_module(image, len, "")` — 아래 실측이 둘 다 보여 준다.) → E3 §3
2. `el0_svc` → `invoke_syscall` → `__arm64_sys_finit_module` → `idempotent_init_module`(같은 파일 동시 로드 중복 제거) → `init_module_from_file`(커널이 파일을 읽음) → **`load_module`**. `init_module`이면 `__do_sys_init_module` → `copy_module_from_user` → `load_module`.
3. `load_module` 내부(대부분 static이라 트레이스에 이름이 안 나온다, `kernel/module/main.c:3370`): `module_sig_check`(서명 — 실측 "module verification failed … tainting kernel") → `elf_validity_cache_copy` → `early_mod_check`(vermagic, 이미 로드됐는지) → `layout_and_allocate`(core/init 영역 할당, 섹션 복사) → `simplify_symbols`(EXPORT_SYMBOL 해결) → `apply_relocations` → `post_relocation` → `complete_formation`(RO/NX 권한) → `prepare_coming_module` → `parse_args`(모듈 파라미터) → `mod_sysfs_setup`(`/sys/module/r2_demo`). → E3 §2, §10, §13
4. **`do_init_module`** → `do_one_initcall(mod->init)` → `r2_driver_init` (`module_platform_driver` 매크로가 만든 함수) → `__platform_driver_register`(`drv->driver.bus = &platform_bus_type`) → `driver_register` → `bus_add_driver`(드라이버 kobject, `/sys/bus/platform/drivers/r2_demo`).
5. `driver_attach` → `bus_for_each_dev(&platform_bus_type, …, __driver_attach)` — **버스에 이미 있는 모든 장치**를 순회. 실측 45개.
6. `__driver_attach` → `driver_match_device` → **`platform_match`**: `driver_override` → **`of_driver_match_device`**(DT `compatible` vs `of_match_table`) → ACPI → `id_table` → 이름 비교 순서. → D3 §4
7. 매치(=1)면 `driver_probe_device` → `__driver_probe_device` → **`really_probe`**: `dev->driver = drv` → pinctrl 기본 상태 적용, `dma_configure` → `call_driver_probe` → **`platform_probe`**(clock 기본 설정, PM domain attach) → **`drv->probe(pdev)` = `r2_probe`**. 실패하면 devres 해제, `-EPROBE_DEFER`면 deferred 목록. → G3 §13, G2 §6
8. probe 성공 → `driver_bound`(`/sys/bus/platform/drivers/r2_demo/r2_demo` 링크, uevent `BIND`). `do_init_module`이 init 섹션을 해제하고 상태를 `MODULE_STATE_LIVE`로.
9. 유저: `finit_module` = 0 반환. 이후 `/dev/r2_demo`는 `device_create` → devtmpfs가 이미 만들었다.

### 4.3 실측 1 — `function_graph`(필터)로 본 등록·매칭·probe (3회차 부팅, BusyBox insmod)

무엇을 확인하나: 위 walk의 함수만 `set_ftrace_filter`에 넣고(아직 로드 안 된 모듈 함수는 `:mod:r2_demo`로 예약), `platform_match`에 kprobe를 걸어 비교하는 장치 이름을 찍고, `r2_probe`에 스택 트레이스를 건다.

```sh
for f in __arm64_sys_finit_module __arm64_sys_init_module idempotent_init_module init_module_from_file \
  load_module do_init_module do_one_initcall __platform_driver_register driver_register bus_add_driver \
  driver_attach bus_for_each_dev __driver_attach platform_match of_driver_match_device driver_probe_device \
  __driver_probe_device really_probe platform_probe alloc_chrdev_region cdev_add class_create device_create; do
  echo $f >> set_ftrace_filter || echo "R2 nofilter $f"
done
echo ':mod:r2_demo' >> set_ftrace_filter
echo 'p:r2probe r2_demo:r2_probe' >> kprobe_events
echo 'p:pmatch platform_match dev=+0(+0($arg1)):string drv=+0(+0($arg2)):string' >> kprobe_events
echo 'r:pmatch_ret platform_match ret=$retval' >> kprobe_events
echo stacktrace > events/kprobes/r2probe/trigger
echo 1 > events/kprobes/enable; echo function_graph > current_tracer
echo 1 > tracing_on; insmod /r2_demo.ko; echo 0 > tracing_on; cat trace
```

```text
R2 nofilter idempotent_init_module
R2 nofilter of_driver_match_device
 0)               |  __arm64_sys_init_module() {
 0)               |    load_module() {
 0)               |      do_init_module() {
 0)               |        do_one_initcall() {
 0)               |          r2_driver_init [r2_demo]() {
 0)               |            __platform_driver_register() {
 0)               |              driver_register() {
 0)               |                bus_add_driver() {
 0)               |                  driver_attach() {
 0)               |                    bus_for_each_dev() {
 0)               |                      __driver_attach() {
 0)               |                        /* pmatch: (platform_match+0x0/0x128) dev="psci" drv="r2_demo" */
 ...
 0)               |                        /* pmatch: (platform_match+0x0/0x128) dev="r2_demo" drv="r2_demo" */
 0)   6.288 us    |                        platform_match(); /* ret=0x1 */
 0)               |                        driver_probe_device() {
 0)               |                          __driver_probe_device() {
 0)               |                            really_probe() {
 0)               |                              platform_probe() {
 0)               |                                /* r2probe: (r2_probe+0x0/0x138 [r2_demo]) */
 0)               |                                r2_probe [r2_demo]() {
 0) + 23.920 us   |                                  alloc_chrdev_region(); /* ret=0x0 */
 0) + 12.624 us   |                                  cdev_add(); /* ret=0x0 */
 0) + 63.824 us   |                                  class_create(); /* ret=0xffff000005217960 */
 0) ! 577.840 us  |                                  device_create(); /* ret=0xffff000006077400 */
 0) ! 813.504 us  |                                } /* r2_probe [r2_demo] ret=0x0 */
 0) # 1332.448 us |                              } /* platform_probe ret=0x0 */
 0) # 1668.704 us |                            } /* really_probe ret=0x0 */
 0) # 1765.088 us |                      } /* __driver_attach ret=0x0 */
 0)               |                        /* pmatch: (platform_match+0x0/0x128) dev="gpio-keys" drv="r2_demo" */
 ...
 0) # 4731.824 us |                    } /* bus_for_each_dev ret=0x0 */
 0) # 5068.848 us |          } /* r2_driver_init [r2_demo] ret=0x0 */
 0) # 5748.336 us |      } /* do_init_module ret=0x0 */
 0) @ 133703.2 us |    } /* load_module ret=0x0 */
 0) @ 134919.9 us |  } /* __arm64_sys_init_module ret=0x0 */
[    4.542084][   T71] r2_demo r2_demo: probed, major=510
```

`r2_probe` 진입 순간의 스택(kprobe 자체 프레임 8줄 생략):

```text
          insmod-71      [000] d....     4.541155: <stack trace>
 => r2_probe
 => really_probe
 => __driver_probe_device
 => driver_probe_device
 => __driver_attach
 => bus_for_each_dev
 => driver_attach
 => bus_add_driver
 => driver_register
 => __platform_driver_register
 => r2_driver_init
 => do_one_initcall
 => do_init_module
 => load_module
 => __do_sys_init_module
 => __arm64_sys_init_module
 => invoke_syscall.constprop.0
 => el0_svc_common.constprop.0
 => do_el0_svc
 => el0_svc
 => el0t_64_sync_handler
 => el0t_64_sync
```

출력에서 볼 것:

- `idempotent_init_module`, `of_driver_match_device`는 필터에 못 넣었다(`Invalid argument`) — `available_filter_functions`에 없다. `of_driver_match_device`는 헤더의 `static inline`이고(`include/linux/of_device.h:20`), `idempotent_init_module`은 static 함수가 인라인된 것이다. 그래서 `platform_match` 안의 OF 비교는 트레이스에 함수로 안 보이고, 대신 kretprobe 반환값 1로 확인했다.
- `pmatch` 줄이 45번: psci, fw-cfg, virtio_mmio 32개, gpio-keys, pcie, pmu, flash, timer … 그중 `dev="r2_demo"`(우리가 DT에 넣은 노드에서 `of_platform_populate`가 부팅 때 만든 `platform_device`)만 `ret=0x1`. **드라이버 등록은 "버스의 모든 장치와 한 번씩 맞춰 보는" 작업**이다.
- 스택에서 `platform_probe`가 빠져 있다(그래프에는 있다). kprobe가 `r2_probe+0x0`, 즉 **함수 프롤로그 전**에 걸렸기 때문이다. 이 순간 직전 호출자로 돌아갈 주소는 아직 LR(x30)에만 있고 스택의 frame record에는 없어서, frame pointer 기반 unwinder는 `r2_probe`(예외 시점 PC) 다음에 바로 `really_probe`(호출자의 호출자)를 찍는다. 이 노트의 모든 `+0x0` kprobe 스택에서 **바로 위 호출자 한 칸이 빠진다**(§3, §5, §7에서도 확인). 스택 트레이스와 function_graph가 다르게 보일 수 있다는 것도 면접 포인트다.
- 시간: `load_module` 133.7 ms 중 probe를 포함한 `do_init_module`은 5.7 ms. 나머지는 ELF 복사·재배치·서명 검사·BTF(`BTF [M]`) 처리다(TCG).
- 이 BusyBox `insmod`는 `init_module`을 불렀다. 실제 배포판의 kmod는? 다음 실측.

### 4.4 실측 2 — kmod `insmod`는 `finit_module` (systemd 게스트, §1.4와 같은 부팅)

```sh
mount -o ro /dev/vdb /mnt/r2 && strace -e trace=finit_module,init_module insmod /mnt/r2/r2_demo.ko; dmesg | tail -2
```

```text
finit_module(3, "", 0)                  = 0
+++ exited with 0 +++
[   56.920045] r2_demo: module verification failed: signature and/or required key missing - tainting kernel
[   56.958259] r2_demo r2_demo: probed, major=510
```

출력에서 볼 것: kmod는 파일 fd를 넘긴다(`finit_module(3, …)`). 커널이 파일을 직접 읽으므로 IMA·LoadPin 같은 "어느 파일에서 왔나" 정책을 걸 수 있다(L5·L6). 두 시스템 콜은 `load_module`에서 합류한다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="22" font-size="13">insmod → probe 호출 사슬 (function_graph 실측 깊이 그대로)</text>
<g font-size="12">
<text x="20" y="44">__arm64_sys_init_module (finit: idempotent_init_module)</text>
<text x="34" y="64">load_module</text>
<text x="48" y="84">do_init_module</text>
<text x="62" y="104">do_one_initcall</text>
<text x="76" y="124">r2_driver_init (module_init)</text>
<text x="90" y="144">__platform_driver_register</text>
<text x="104" y="164">driver_register</text>
<text x="118" y="184">bus_add_driver</text>
<text x="132" y="204">driver_attach</text>
<text x="146" y="224">bus_for_each_dev</text>
<text x="160" y="244">__driver_attach ×45</text>
<text x="174" y="264">platform_match() = 1 (r2_demo)</text>
<text x="174" y="284">driver_probe_device</text>
<text x="188" y="304">__driver_probe_device</text>
<text x="202" y="324">really_probe</text>
<text x="216" y="344">platform_probe</text>
<text x="230" y="364">r2_probe()</text>
<text x="470" y="64">ELF 검사·복사·재배치·서명</text>
<text x="470" y="124">module_platform_driver 매크로</text>
<text x="470" y="184">sysfs drivers/r2_demo 생성</text>
<text x="470" y="224">platform 장치 45개 순회</text>
<text x="470" y="264">DT compatible 비교, 1개만 1</text>
<text x="470" y="324">dev->driver 설정, pinctrl, DMA</text>
<text x="470" y="364">cdev_add · device_create</text>
</g>
<rect x="20" y="250" width="640" height="20" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<rect x="20" y="350" width="640" height="20" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="20" y="392" font-size="12">load_module 133.7 ms 중 probe까지 포함한 do_init_module은 5.7 ms (TCG, 3회차)</text>
</svg>
```
그림 5 — 들여쓰기가 호출 깊이다. 주황 줄이 "어느 장치와 맞는가"를 정하는 버스 match, 초록 줄이 드라이버 코드다. 장치가 나중에 생기는 경우(hotplug, overlay)는 `device_add` → `bus_probe_device` → `__device_attach`로 올라와 같은 `really_probe`에서 만난다(G2 §6).

### 4.5 꼬리 질문

- **"probe가 안 불려요. 순서대로 무엇을 보나요?"** — ① 모듈이 로드됐나(`lsmod`) ② 장치가 있나(`/sys/bus/platform/devices/`에 노드 이름, DT `status = "okay"`) ③ compatible 철자(`/proc/device-tree/.../compatible`와 `of_match_table`) ④ probe가 deferred인가(`/sys/kernel/debug/devices_deferred`) ⑤ probe가 에러를 냈나(`dmesg`의 `probe ... failed with error`). → D3 §13, G3 §9, M1
- **"DT 노드는 언제 `platform_device`가 되나요?"** — 부팅 때 `of_platform_default_populate_init`(arch_initcall_sync)이 루트 아래 `compatible` 있는 노드와 `simple-bus` 자식을 `platform_device`로 만든다. I2C/SPI 자식은 해당 버스 컨트롤러 드라이버가 probe에서 만든다. → D3 §2, §8
- **"built-in이면 무엇이 다른가요?"** — `load_module`이 없고, `module_init`이 `device_initcall`이 되어 §1의 `do_initcalls`에서 같은 `driver_register`를 부른다. → E3 §12, C6 §10
- **"modprobe는 insmod와 무엇이 다른가요?"** — `modules.dep`로 의존 모듈을 먼저 넣고, alias(`of:N*T*Cr2,demo`)로 이름을 찾는다. udev가 uevent의 `MODALIAS`로 `modprobe`를 부르는 것이 자동 로드다. → E3 §11, D3 §9
- **"probe에서 잠들어도 되나요?"** — 된다. 프로세스 문맥(insmod 태스크 또는 부팅 때 kernel_init/async 워커)이다. 다만 오래 걸리면 부팅을 막으므로 `PROBE_PREFER_ASYNCHRONOUS`를 검토한다. → G3 §11

---

## 5. malloc한 메모리를 처음 건드리면 — page fault

### 5.1 30초 버전

> "`malloc(8 MiB)`는 glibc가 `mmap(MAP_ANONYMOUS)`로 VMA만 만들 뿐 물리 페이지도 PTE도 없다. 첫 store에서 MMU가 translation fault를 내고, EL0 동기 예외로 `el0t_64_sync` → `el0_da` → `do_mem_abort`가 ESR의 fault 종류로 `do_translation_fault` → `do_page_fault`를 부른다. 커널은 `lock_vma_under_rcu`로 주소가 속한 VMA를 찾고 권한을 확인한 뒤 `handle_mm_fault` → `__handle_mm_fault`에서 익명 페이지를 할당·0으로 채우고 PTE를 설치한다. THP가 켜져 있고 2 MiB 정렬 범위가 VMA 안에 있으면 4 KiB 대신 2 MiB 한 장을 PMD로 매핑한다. 예외에서 돌아오면 같은 store가 다시 실행되어 성공한다. VMA 밖이면 `SIGSEGV`다."

### 5.2 전체 walk

1. glibc `malloc(8 MiB)` > `M_MMAP_THRESHOLD`(기본 128 KiB) → `mmap(NULL, …, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS)` → 커널은 `vm_area_struct`만 추가. RSS 증가 없음. → A5 §5
2. `p[4096] = 1` → MMU 테이블 워크 실패(PTE 없음) → Data Abort from EL0, `ESR_EL1.EC = 0x24`, DFSC = translation fault level N, `FAR_EL1` = 주소.
3. `el0t_64_sync` → `el0t_64_sync_handler` → `el0_da` → **`do_mem_abort(far, esr, regs)`** → `fault_info[]` 표에서 DFSC로 `do_translation_fault` → (유저 주소면) `do_page_fault`. → F11 §5
4. `do_page_fault`: `mm_flags` 계산(write fault) → **`lock_vma_under_rcu`**(per-VMA lock, mmap_lock 없이) → VMA 권한 검사 → `handle_mm_fault(vma, addr, flags, regs)`.
5. `__handle_mm_fault`: pgd/p4d/pud/pmd 할당 → (THP 조건 충족) `do_huge_pmd_anonymous_page` 또는 → `handle_pte_fault` → `do_pte_missing` → **`do_anonymous_page`**: `vma_alloc_zeroed_movable_folio` → memcg charge → `set_pte_at` → `folio_add_new_anon_rmap` → LRU 추가. (6.18 lab 커널에서 이 세 함수는 `__handle_mm_fault`에 인라인돼 트레이스에 안 보인다.)
6. 반환 → `ret_to_user` → `eret` → **같은 store 명령을 다시 실행** → 이번엔 TLB miss 후 테이블 워크 성공.
7. 결과: `ru_minflt`가 1 증가(디스크 I/O 없는 minor fault). 읽기 먼저였다면 공유 zero page를 읽기 전용으로 매핑했다가 쓰기 때 COW.

### 5.3 실측 — 4 KiB 경로 (3회차, `MADV_NOHUGEPAGE`)

무엇을 확인하는 코드인가: 8 MiB를 malloc하고 한 바이트만 쓴다. `trace_marker`로 그 store 앞뒤에 표시를 남기고, minor fault 수 차이를 출력한다. `nothp` 인자면 `madvise(MADV_NOHUGEPAGE)`로 THP를 끈다.

```c
/* r2_pf [nothp]: malloc 8 MiB (mmap'd), touch ONE byte -> one anonymous page fault */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>

static long minflt(void)
{
	struct rusage ru;
	if (getrusage(RUSAGE_SELF, &ru)) { perror("getrusage"); exit(1); }
	return ru.ru_minflt;
}

int main(int argc, char **argv)
{
	char *p = malloc(8 << 20);              /* > M_MMAP_THRESHOLD -> fresh mmap, no page yet */
	if (!p) { perror("malloc"); exit(1); }
	if (argc > 1 && !strcmp(argv[1], "nothp") && madvise((void *)((uintptr_t)p & ~4095UL), 8 << 20, MADV_NOHUGEPAGE))
		{ perror("madvise"); exit(1); }
	FILE *m = fopen("/sys/kernel/tracing/trace_marker", "w");
	if (!m) { perror("trace_marker"); exit(1); }
	fprintf(m, "r2_pf: start\n"); fflush(m);  /* stdio buffer faults happen here, not below */
	long before = minflt();
	fprintf(m, "r2_pf: touching %p\n", (void *)(p + 4096)); fflush(m);
	p[4096] = 1;                            /* first touch: MMU translation fault */
	fprintf(m, "r2_pf: touched\n"); fflush(m);
	long after = minflt();
	printf("p=%p minflt before=%ld after=%ld delta=%ld\n", (void *)p, before, after, after - before);
	return 0;
}
```

이 코드도 두 번 고쳤다. 첫 판은 `madvise(p, …)`가 `EINVAL`이었다 — malloc 포인터는 mmap 시작 + 16바이트라 페이지 정렬이 아니다. 두 번째 판은 `delta=2`였다 — `before`를 읽은 뒤 첫 `fprintf`가 stdio 버퍼를 malloc해 fault가 하나 더 났다. "start" 마커를 먼저 써서 해결했다.

```sh
echo do_mem_abort > set_graph_function; echo function_graph > current_tracer
echo 'p:hmf handle_mm_fault addr=$arg2 flags=$arg3' >> kprobe_events
echo 'stacktrace if comm == "r2_pf"' > events/kprobes/hmf/trigger
sh -c "echo \$\$ > /sys/kernel/tracing/set_ftrace_pid; echo 1 > /sys/kernel/tracing/tracing_on; exec /r2_pf nothp"
```

```text
p=0xffff8d7ef010 minflt before=91 after=92 delta=1
 0)               |  /* r2_pf: touching 0xffff8d7f0010 */
 0)               |  do_mem_abort() {
 0)               |    lock_vma_under_rcu() {
 0) + 10.080 us   |    } /* lock_vma_under_rcu ret=0xffff00000509e180 */
 0)               |        /* hmf: (handle_mm_fault+0x0/0x2e8) addr=0xffff8d7f0010 flags=0x1255 */
           r2_pf-84      [000] d....    16.407657: <stack trace>
 => handle_mm_fault
 => do_translation_fault
 => do_mem_abort
 => el0_da
 => el0t_64_sync_handler
 => el0t_64_sync
 0)               |    handle_mm_fault() {
 0)               |      __handle_mm_fault() {
 0)               |        vma_alloc_zeroed_movable_folio() {
 0) + 45.072 us   |        } /* vma_alloc_zeroed_movable_folio ret=0xfffffdffc00e5c80 */
 0)               |        __mem_cgroup_charge() {
 0) + 13.696 us   |        } /* __mem_cgroup_charge ret=0x0 */
 0)               |        __pte_offset_map_lock() {
 0) + 12.000 us   |        } /* __pte_offset_map_lock ret=0xffff000006ad4f80 */
 0)               |        folio_add_new_anon_rmap() {
 0) + 28.128 us   |        } /* folio_add_new_anon_rmap ret=0xfffffdffc00e5c80 */
 0)               |        folio_add_lru_vma() {
 0)   9.968 us    |        } /* folio_add_lru_vma ret=0x100000001 */
 0) ! 145.792 us  |      } /* __handle_mm_fault ret=0x0 */
 0) ! 168.112 us  |    } /* handle_mm_fault ret=0x0 */
 0) ! 361.712 us  |  } /* do_mem_abort ret=0x0 */
 0)               |  /* r2_pf: touched */
```

(kprobe 브레이크포인트 처리 줄과 스택의 kprobe 프레임 8줄, 하위 함수 줄을 지웠다. `do_mem_abort` 361 µs에는 kprobe + 스택 저장 비용 약 130 µs가 들어 있다.)

출력에서 볼 것:

- 두 마커 사이에 `do_mem_abort`가 **정확히 한 번**, `delta=1`. 한 번의 store = 한 번의 minor fault.
- 스택: `el0t_64_sync` → `el0t_64_sync_handler` → `el0_da` → `do_mem_abort` → `do_translation_fault` → `handle_mm_fault`. **`do_page_fault`가 안 보인다** — §4.3에서 본 "`+0x0` kprobe는 바로 위 호출자를 빠뜨린다"는 규칙 때문이다(`handle_mm_fault`를 부르는 것은 `do_page_fault`다, `arch/arm64/mm/fault.c`). function_graph에도 `do_translation_fault`/`do_page_fault`가 안 보이는 것은 다른 이유다: `fault.c`의 이 함수들은 `__kprobes`/`noinstr` 계열이라 `available_filter_functions`에 없다(실측 grep에서 `do_page_fault`, `do_translation_fault`, `el0_da` 모두 없음).
- `__handle_mm_fault` 바로 아래가 `vma_alloc_zeroed_movable_folio`다. `handle_pte_fault` → `do_anonymous_page`가 전부 인라인된 것이다(`do_anonymous_page`도 `available_filter_functions`에 없다). **0으로 채운 4 KiB 한 장 할당 → memcg 과금 → PTE 락 → rmap → LRU** 순서가 그대로 보인다.
- `flags=0x1255` = `FAULT_FLAG_WRITE`(1<<0) + `ALLOW_RETRY`(1<<2) + `KILLABLE`(1<<4) + `USER`(1<<6) + `INTERRUPTIBLE`(1<<9) + `VMA_LOCK`(1<<12) (`include/linux/mm_types.h:1651-1663`). 쓰기 fault이고, 유저 모드이고, mmap_lock이 아니라 per-VMA lock으로 처리 중이라는 뜻이다.

### 5.4 실측 — THP 경로 (1회차, THP `always`)

같은 바이너리를 `nothp` 없이 돌린 1회차에서는 다른 일이 일어났다.

```text
[always] madvise never
 0)               |  /* r2_pf: touching 0xffffb9200010 */
 0)               |  do_mem_abort() {
 0)               |    handle_mm_fault() {
 0)               |      __handle_mm_fault() {
 0)               |        do_huge_pmd_anonymous_page() {
 0)               |          vma_alloc_anon_folio_pmd() {
 0) ! 830.336 us  |            folio_zero_user(); /* ret=0xffff000008601000 */
 0) # 1057.840 us |          } /* vma_alloc_anon_folio_pmd ret=0xfffffdffc0218000 */
 0)               |          map_anon_folio_pmd() {
 0) ! 531.712 us  |          } /* map_anon_folio_pmd ret=0x100000001 */
 0) # 3000.256 us |        } /* do_huge_pmd_anonymous_page ret=0x0 */
 0) # 3029.088 us |    } /* handle_mm_fault ret=0x0 */
 0) # 3047.872 us |  } /* do_mem_abort ret=0x0 */
 0)               |  /* r2_pf: touched */
```

출력에서 볼 것:

- 주소 `0xffffb9200010`의 2 MiB 정렬 시작 `0xffffb9200000`이 VMA 안에 있었기 때문에 **2 MiB 한 장**이 할당되었다(`do_huge_pmd_anonymous_page`). 0 채우기만 830 µs, fault 전체 3.05 ms — 4 KiB 경로의 약 8배.
- 3회차의 "thp" 모드 실행(위 출력 아님)에서는 주소가 `0xffffbdc43010`이었고 그 2 MiB 정렬 시작은 VMA 시작보다 앞이라 4 KiB 경로로 갔다. **THP 여부는 ASLR이 준 주소에 따라 실행마다 달라졌다.** RT 루프에서 첫 접근 지연이 들쭉날쭉한 원인 중 하나다 — 그래서 `mlockall(MCL_CURRENT|MCL_FUTURE)` + 미리 건드리기(prefault)를 한다(A5 §8).

```svg
<svg viewBox="0 0 680 372" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="20" y="22" font-size="13">첫 접근 page fault — arm64 6.18 실측 경로</text>
<g font-size="12">
<rect x="20" y="40" width="290" height="28" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="30" y="59">유저 store: p[4096] = 1 (EL0)</text>
<rect x="20" y="78" width="290" height="28" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="30" y="97">MMU: PTE 없음 → translation fault</text>
<rect x="20" y="116" width="290" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="30" y="135">el0t_64_sync → el0t_64_sync_handler</text>
<rect x="20" y="154" width="290" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="30" y="173">el0_da → do_mem_abort(far, esr)</text>
<rect x="20" y="192" width="290" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="30" y="211">do_translation_fault → do_page_fault</text>
<rect x="20" y="230" width="290" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="30" y="249">lock_vma_under_rcu (VMA 찾기)</text>
<rect x="20" y="268" width="290" height="28" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="30" y="287">handle_mm_fault → __handle_mm_fault</text>
<rect x="340" y="200" width="320" height="44" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="350" y="218">THP 가능 (2 MiB 정렬 범위가 VMA 안)</text>
<text x="350" y="236">do_huge_pmd_anonymous_page: 2 MiB 0 채움</text>
<rect x="340" y="256" width="320" height="44" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="350" y="274">4 KiB 경로 (정렬 안 맞음 / MADV_NOHUGEPAGE)</text>
<text x="350" y="292">do_anonymous_page(인라인): 4 KiB + PTE</text>
<rect x="340" y="312" width="320" height="28" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="350" y="331">eret → store 재실행 → minflt +1</text>
</g>
<g stroke="currentColor" fill="none" marker-end="url(#ah6)">
<line x1="165" y1="68" x2="165" y2="77"/><line x1="165" y1="106" x2="165" y2="115"/><line x1="165" y1="144" x2="165" y2="153"/>
<line x1="165" y1="182" x2="165" y2="191"/><line x1="165" y1="220" x2="165" y2="229"/><line x1="165" y1="258" x2="165" y2="267"/>
<line x1="310" y1="282" x2="339" y2="222"/><line x1="310" y1="282" x2="339" y2="278"/>
<path d="M660,222 H672 V326 H661"/><path d="M660,278 H672"/>
</g>
<text x="20" y="364" font-size="12">실측(TCG): THP 경로 do_mem_abort 3.05 ms (folio_zero_user 0.83 ms), 4 KiB 경로 0.36 ms</text>
</svg>
```
그림 6 — 첫 접근 fault의 두 갈래. 어느 쪽으로 갈지는 THP 설정과 **주소 정렬**이 정한다.

### 5.5 꼬리 질문

- **"minor와 major fault 차이?"** — major는 디스크(파일 page cache 미스, swap-in) I/O가 필요하다. 익명 메모리의 첫 접근은 minor. → A5 §3
- **"커널 코드가 유저 포인터를 건드리다 fault 나면?"** — `copy_from_user` 같은 함수의 명령 주소가 예외 테이블에 있어 `fixup_exception`이 `-EFAULT`로 바꾼다. 테이블에 없으면 Oops. → E6 §4
- **"fork 뒤 쓰기는?"** — 같은 `handle_mm_fault`지만 PTE가 있고 읽기 전용이라 `do_wp_page`(COW)로 간다. → A2 §2, §6
- **"RT 루프에서 fault를 없애려면?"** — `mlockall(MCL_CURRENT|MCL_FUTURE)`, 스택·힙 prefault, THP는 `madvise`로 통제, 실행 중 `malloc` 금지. → A5 §8, N1
- **"page fault와 MMU abort, 펌웨어와 뭐가 다르죠?"** — 펌웨어에서는 MPU fault = 버그 → HardFault 핸들러에서 리셋. Linux에서는 "아직 안 채운 것"이 정상 경로다. demand paging이 메모리 과다 약정(overcommit)을 가능하게 하고, 그 대가가 OOM killer다. → A5 §4, F11 §9

---

## 6. `fork()` + `exec()`

### 6.1 30초 버전

> "`fork()`는 glibc에서 `clone(SIGCHLD)`이고 커널 `kernel_clone` → `copy_process`가 `task_struct`와 커널 스택을 복제하고, 파일 테이블·fs·시그널 핸들러를 복사하거나 공유하고, `dup_mmap`으로 VMA를 복사하면서 쓰기 가능한 사설 페이지의 PTE를 양쪽 모두 읽기 전용으로 바꾼다(COW). `alloc_pid`로 PID를 받고 `wake_up_new_task`로 런큐에 넣는다. 자식은 `ret_from_fork`에서 x0=0으로 유저에 돌아온다. `execve`는 `do_execveat_common` → `bprm_execve` → `load_elf_binary`가 새 `mm`을 만들고 `begin_new_exec`에서 옛 `mm`을 버리고(여기서 COW 공유가 끝난다) ELF 세그먼트를 mmap하고 스택에 argv/envp/auxv를 쌓은 뒤 `START_THREAD`로 PC와 SP를 새 진입점으로 바꾼다. 부모는 `wait4`에서 잠들었다가 자식의 `exit` → SIGCHLD + wake로 깨어난다."

### 6.2 실측 — `kernel_clone`과 `execve` (3회차, `max_graph_depth=3`)

무엇을 확인하는 코드인가: fork → 자식은 `execv("/bin/true")` → 부모는 `waitpid`.

```c
/* r2_fe: fork() + execve("/bin/true") + waitpid */
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
	pid_t pid = fork();
	if (pid < 0) { perror("fork"); exit(1); }
	if (pid == 0) {
		char *argv[] = { "true", NULL };
		execv("/bin/true", argv);
		perror("execv"); _exit(127);
	}
	int st;
	if (waitpid(pid, &st, 0) < 0) { perror("waitpid"); exit(1); }
	printf("parent=%d child=%d exit=%d\n", getpid(), pid, WEXITSTATUS(st));
	return 0;
}
```

```sh
echo kernel_clone > set_graph_function; echo __arm64_sys_execve >> set_graph_function
echo 3 > max_graph_depth
echo 1 > options/function-fork; echo 1 > options/event-fork
for e in sched_process_fork sched_wakeup_new sched_process_exec sched_process_exit sched_process_wait; do
  echo 1 > events/sched/$e/enable; done
sh -c 'echo $$ > set_ftrace_pid; echo $$ > set_event_pid; echo 1 > tracing_on; exec /r2_fe'
```

```text
 0)               |  __arm64_sys_execve() {
 0)               |    do_execveat_common.isra.0() {
 0)               |      bprm_execve() {
 0)               |        /* sched_process_exec: filename=/r2_fe pid=86 old_pid=86 */
 0) # 4253.504 us |  } /* __arm64_sys_execve ret=0x0 */
 0)               |  kernel_clone() {
 0)               |    copy_process() {
 0) + 23.504 us   |      arch_dup_task_struct(); /* ret=0x0 */
 0) + 19.504 us   |      copy_creds(); /* ret=0x0 */
 0) + 21.664 us   |      sched_fork(); /* ret=0x0 */
 0) + 16.800 us   |      dup_fd(); /* ret=0xffff0000046ea840 */
 0) + 13.584 us   |      copy_fs_struct(); /* ret=0xffff000004799e40 */
 0) ! 120.624 us  |      mm_init(); /* ret=0xffff000005608580 */
 0) ! 731.040 us  |      dup_mmap(); /* ret=0x0 */
 0)   7.664 us    |      copy_thread(); /* ret=0x0 */
 0) + 28.800 us   |      alloc_pid(); /* ret=0xffff000005245300 */
 0) # 1486.864 us |    } /* copy_process ret=0xffff000004410000 */
 0)               |      /* sched_process_fork: comm=r2_fe pid=86 child_comm=r2_fe child_pid=87 */
 0)               |    wake_up_new_task() {
 0)               |      /* sched_wakeup_new: comm=r2_fe pid=87 prio=120 target_cpu=000 */
 0) ! 122.080 us  |    } /* wake_up_new_task ret=0xffff000004414600 */
 0) # 1813.920 us |  } /* kernel_clone ret=0x57 */
 0)               |  /* sched_process_wait: comm=r2_fe pid=87 prio=120 */
 ------------------------------------------
 0)    r2_fe-86    =>    true-87
 ------------------------------------------
 0)               |  __arm64_sys_execve() {
 0)               |    do_execveat_common.isra.0() {
 0)               |      bprm_execve() {
 0)               |        /* sched_process_exec: filename=/bin/true pid=87 old_pid=87 */
 0) # 3649.712 us |      } /* bprm_execve ret=0x0 */
 0) # 4560.624 us |  } /* __arm64_sys_execve ret=0x0 */
 0)               |  /* sched_process_exit: comm=true pid=87 prio=120 group_dead=true */
 ------------------------------------------
 0)    true-87     =>    r2_fe-86
 ------------------------------------------
 0)               |  /* sched_process_exit: comm=r2_fe pid=86 prio=120 group_dead=true */
```

(`copy_process` 아래 약 60개 함수 중 주요 줄만 남겼다.)

출력에서 볼 것:

- 첫 `execve`는 셸이 `exec /r2_fe`한 것 — 같은 PID(86)가 프로그램만 바꿨다(`old_pid=86`).
- `kernel_clone ret=0x57` = 87 = 자식 PID. 부모 쪽 fork 반환값이다. 자식 쪽 0은 `copy_thread`가 자식 `pt_regs`의 x0을 0으로 만들어서 생긴다.
- `copy_process` 1.49 ms 중 `dup_mmap` 0.73 ms — **fork 비용의 절반이 주소 공간 복사**(VMA + PTE 쓰기 금지 표시)다. 그래서 큰 프로세스에서 fork+exec 대신 `posix_spawn`/`vfork`를 쓴다(A2 §10에 실측 비교).
- `sched_process_wait` 다음 `r2_fe-86 => true-87`: 부모가 `wait4`에서 잠들고 자식이 돈다. 자식 exit 후 `true-87 => r2_fe-86`.
- `do_execveat_common`을 `set_graph_function`에 넣으면 실패했다 — 실제 심볼은 `do_execveat_common.isra.0`이다(GCC IPA-SRA 최적화 접미사). 그래서 `__arm64_sys_execve`로 잡았다.

```svg
<svg viewBox="0 0 680 465" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="20" y="22" font-size="13">fork() + exec() — 부모(왼쪽)와 자식(오른쪽)</text>
<g font-size="12">
<rect x="20" y="50" width="300" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="30" y="70">fork() → svc → kernel_clone</text>
<rect x="20" y="90" width="300" height="30" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="30" y="110">copy_process: task_struct · 스택 복사</text>
<rect x="20" y="130" width="300" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="30" y="150">dup_mmap: VMA 복사 + PTE 읽기 전용(COW)</text>
<rect x="20" y="170" width="300" height="30" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="30" y="190">alloc_pid → child_pid 87</text>
<rect x="20" y="210" width="300" height="30" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="30" y="230">wake_up_new_task → 런큐에 넣기</text>
<rect x="20" y="250" width="300" height="30" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="30" y="270">wait4 → S 상태로 잠</text>
<rect x="20" y="410" width="300" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="30" y="430">깨어남 → waitpid 반환 (exit=0)</text>
<rect x="360" y="210" width="300" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="370" y="230">ret_from_fork → fork() = 0 반환</text>
<rect x="360" y="250" width="300" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="370" y="270">execve → do_execveat_common.isra.0</text>
<rect x="360" y="290" width="300" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="370" y="310">bprm_execve → load_elf_binary</text>
<rect x="360" y="330" width="300" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="370" y="350">begin_new_exec: 옛 mm 버림 (COW 끝)</text>
<rect x="360" y="370" width="300" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="370" y="390">START_THREAD → /bin/true 진입점</text>
<rect x="360" y="410" width="300" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="370" y="430">exit → 부모에 SIGCHLD + 깨움</text>
</g>
<g stroke="currentColor" marker-end="url(#ah7)">
<line x1="170" y1="80" x2="170" y2="89"/><line x1="170" y1="120" x2="170" y2="129"/><line x1="170" y1="160" x2="170" y2="169"/>
<line x1="170" y1="200" x2="170" y2="209"/><line x1="170" y1="240" x2="170" y2="249"/>
<line x1="320" y1="225" x2="359" y2="225"/>
<line x1="510" y1="240" x2="510" y2="249"/><line x1="510" y1="280" x2="510" y2="289"/><line x1="510" y1="320" x2="510" y2="329"/>
<line x1="510" y1="360" x2="510" y2="369"/><line x1="510" y1="400" x2="510" y2="409"/>
<line x1="360" y1="425" x2="321" y2="425"/>
</g>
<line x1="170" y1="280" x2="170" y2="408" stroke="#4a7bd0" stroke-width="2" stroke-dasharray="5 4"/>
<text x="20" y="458" font-size="12">실측(TCG): kernel_clone 1.81 ms (dup_mmap 0.73 ms), execve 4.3 ms</text>
</svg>
```
그림 7 — fork와 exec의 역할 분담. 주황 두 칸이 COW의 시작과 끝이다. fork 직후 exec하면 COW로 공유한 페이지는 거의 복사되지 않고 버려진다.

### 6.3 꼬리 질문

- **"스레드 생성과 fork의 차이?"** — 같은 `kernel_clone`, 플래그만 다르다. `pthread_create`는 `CLONE_VM|CLONE_FILES|CLONE_SIGHAND|CLONE_THREAD…`라 `dup_mmap` 대신 `mm`을 공유한다. → A2 §8
- **"exec 후 열린 fd는?"** — 기본은 유지된다. `O_CLOEXEC` 붙은 것만 `begin_new_exec` → `do_close_on_exec`에서 닫힌다. 데몬이 장치 fd를 자식에게 새는 버그의 원인. → A3 §2
- **"좀비는 언제 생기나?"** — 자식 exit 후 부모가 `wait`하기 전. `task_struct` 일부와 종료 코드만 남는다. 실측의 `prev_state=Z`. → A2 §4
- **"exec가 실패하면 원래 프로그램은?"** — `begin_new_exec`(point of no return) 전에 실패하면 원래 프로그램으로 -1/errno 반환. 그 뒤 실패면 `SIGSEGV`/kill. → A2 §3

---

## 7. 네트워크 패킷 도착 → `recv()`

### 7.1 30초 버전

> "NIC가 패킷을 RX 링 버퍼에 DMA하고 인터럽트를 올린다. hardirq 핸들러(virtio-net은 `vring_interrupt` → `skb_recv_done`)는 그 큐의 인터럽트를 끄고 `napi_schedule`로 NET_RX softirq만 예약하고 끝난다. IRQ 복귀 시 softirq `net_rx_action`이 드라이버 `poll`(`virtnet_poll`)을 budget만큼 불러 skb를 만들고 `netif_receive_skb` → `ip_rcv` → `ip_local_deliver` → `udp_rcv`로 올린다. UDP는 4-튜플로 소켓을 찾아 `__udp_enqueue_schedule_skb`로 소켓 수신 큐에 넣고 `sk_data_ready` = `sock_def_readable`로 wait queue를 깨운다. `recvfrom()`에서 `__skb_wait_for_more_packets`로 잠들어 있던 스레드가 스케줄되어 `udp_recvmsg`가 skb에서 유저 버퍼로 복사한다. 부하가 높으면 softirq가 `ksoftirqd`로 넘어간다."

### 7.2 실측 (2회차 부팅, 호스트 → QEMU user-net hostfwd UDP 한 개)

무엇을 확인하는 코드인가: UDP 5000번에 bind하고 `recvfrom()`에서 잠든다.

```c
/* r2_udp: bind UDP :5000 and block in recvfrom() until a packet arrives */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

int main(void)
{
	int s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0) { perror("socket"); exit(1); }
	struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(5000),
				 .sin_addr.s_addr = htonl(INADDR_ANY) };
	if (bind(s, (struct sockaddr *)&a, sizeof(a))) { perror("bind"); exit(1); }
	char buf[256];
	struct sockaddr_in src; socklen_t sl = sizeof(src);
	ssize_t n = recvfrom(s, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&src, &sl);
	if (n < 0) { perror("recvfrom"); exit(1); }
	buf[n] = '\0';
	printf("got %zd bytes from %s:%u: %s", n, inet_ntoa(src.sin_addr), ntohs(src.sin_port), buf);
	close(s);
	return 0;
}
```

```sh
# QEMU: -netdev user,id=n0,hostfwd=udp:127.0.0.1:42201-10.0.2.15:5000 -device virtio-net-device,netdev=n0
# guest
echo 'p:sdr sock_def_readable' >> kprobe_events
echo 'p:udpq __udp_enqueue_schedule_skb len=+112($arg2):u32' >> kprobe_events
echo 'r:udprecv_ret udp_recvmsg ret=$retval' >> kprobe_events
echo 'p:vri vring_interrupt irq=$arg1' >> kprobe_events
echo stacktrace > events/kprobes/sdr/trigger
echo 'vec == 3' > events/irq/softirq_entry/filter      # NET_RX only
# host (VM side)
python3 -c "import socket; socket.socket(socket.AF_INET, socket.SOCK_DGRAM).sendto(b\"r2-hello\\n\", (\"127.0.0.1\", 42201))"
```

`+112`는 `struct sk_buff`의 `len` 오프셋이다 — `gdb -batch -ex "print &((struct sk_buff *)0)->len" vmlinux-trace`가 `0x70`을 돌려줬다.

```text
got 9 bytes from 10.0.2.2:56999: r2-hello
 15:          3 GIC-0  79 Edge      virtio0
      <idle>-0   [000] D.H3.  7.661192: irq_handler_entry: irq=15 name=virtio0
      <idle>-0   [000] D.H3.  7.661210: vri: (vring_interrupt+0x0/0x108) irq=0xf
      <idle>-0   [000] ..s1.  7.661859: napi_poll: napi poll on napi struct 00000000d3d2b280 for device eth0 work 1 budget 64
      <idle>-0   [000] ..s1.  7.662746: netif_receive_skb: dev=eth0 skbaddr=000000003e597424 len=46
      <idle>-0   [000] d.s1.  7.664694: udpq: (__udp_enqueue_schedule_skb+0x0/0x3b0) len=9
      <idle>-0   [000] d.s1.  7.665111: sdr: (sock_def_readable+0x0/0xf8)
      <idle>-0   [000] d.s1.  7.665134: <stack trace>
 => sock_def_readable
 => udp_queue_rcv_one_skb
 => udp_queue_rcv_skb
 => udp_unicast_rcv_skb
 => __udp4_lib_rcv
 => udp_rcv
 => ip_protocol_deliver_rcu
 => ip_local_deliver_finish
 => ip_local_deliver
 => ip_sublist_rcv
 => ip_list_rcv
 => __netif_receive_skb_list_core
 => netif_receive_skb_list_internal
 => napi_complete_done
 => virtnet_poll
 => __napi_poll.constprop.0
 => net_rx_action
 => handle_softirqs
 => __do_softirq
 => ____do_softirq
 => call_on_irq_stack
 => do_softirq_own_stack
 => __irq_exit_rcu
 => irq_exit_rcu
 => el1_interrupt
 => el1h_64_irq_handler
 => el1h_64_irq
 => default_idle_call
      <idle>-0   [000] d.s4.  7.665188: sched_waking: comm=r2_udp pid=80 prio=120 target_cpu=000
      <idle>-0   [000] .Ns1.  7.665453: softirq_exit: vec=3 [action=NET_RX]
 ksoftirqd/0-14  [000] d..2.  7.666044: sched_switch: prev_comm=ksoftirqd/0 prev_pid=14 prev_prio=120 prev_state=S ==> next_comm=r2_udp next_pid=80 next_prio=120
      r2_udp-80  [000] d..1.  7.666514: udprecv_ret: (inet_recvmsg+0x2c/0xe0 <- udp_recvmsg) ret=9
```

(이 앞의 7.654459 virtio IRQ + NET_RX에서 들어온 첫 46바이트 프레임은 UDP 소켓까지 가지 않았다 — slirp의 ARP 요청으로 보이며 확인하지 않았다. 스택 아래쪽 idle 루프와 kprobe 프레임, vri 반복 줄을 지웠다.)

출력에서 볼 것:

- **플래그 `D.H3`**: 이 virtio IRQ는 **softirq 처리 도중**(앞 프레임의 NET_RX)에 들어왔다. hardirq는 softirq를 선점한다. 그 IRQ가 다시 NET_RX를 올렸다.
- `napi_poll ... work 1 budget 64` — poll 한 번에 최대 64개, 이번엔 1개.
- `netif_receive_skb len=46`: Ethernet 헤더를 뗀 길이. UDP 9바이트 + IP 20 + UDP 8 = 37, 최소 프레임 패딩으로 46. 소켓 큐에 들어갈 때(`udpq`)는 페이로드 9바이트.
- 스택: **softirq는 별도 스레드가 아니라 IRQ 출구(`irq_exit_rcu` → `__irq_exit_rcu` → `do_softirq_own_stack`)에서 돈다**. `virtnet_poll` → `napi_complete_done` → GRO 목록 flush → `netif_receive_skb_list_internal` → `ip_list_rcv` → … → `udp_rcv` → `sock_def_readable`. 그다음 줄이 `sched_waking: comm=r2_udp` — §3과 같은 wait queue 깨우기다.
- `sdr` 스택 맨 위가 `udp_queue_rcv_one_skb`다. 실제 직전 호출자 `__udp_enqueue_schedule_skb`는 빠졌지만, 그 함수의 kprobe(`udpq`)가 바로 앞 줄에서 발화해 순서를 보충한다.
- softirq 출구에서 아직 NET_RX가 남아 `ksoftirqd/0`이 깨어났고, 그 뒤 `r2_udp`로 전환 → `udp_recvmsg ret=9`. UDP IRQ에서 반환까지 5.3 ms(TCG).

```svg
<svg viewBox="0 0 680 410" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah8" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="20" y="20" font-size="13">UDP 패킷 → recvfrom() — 실행 문맥 세 층 (6.18 실측 스택)</text>
<rect x="140" y="32" width="530" height="94" fill="#d0564a" fill-opacity="0.06" stroke="#d0564a" stroke-opacity="0.5"/>
<rect x="140" y="132" width="530" height="184" fill="#e08a3c" fill-opacity="0.06" stroke="#e08a3c" stroke-opacity="0.5"/>
<rect x="140" y="322" width="530" height="64" fill="#4a7bd0" fill-opacity="0.06" stroke="#4a7bd0" stroke-opacity="0.5"/>
<g font-size="12">
<text x="10" y="82">hardirq</text>
<text x="10" y="228">softirq NET_RX</text>
<text x="10" y="358">프로세스 문맥</text>
<rect x="150" y="40" width="380" height="24" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="160" y="57">virtio-net 프레임 도착 → GIC SPI 79 (irq 15)</text>
<rect x="150" y="70" width="380" height="24" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="160" y="87">vring_interrupt → skb_recv_done</text>
<rect x="150" y="100" width="380" height="24" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="160" y="117">__napi_schedule → NET_RX softirq 예약</text>
<rect x="150" y="140" width="380" height="24" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="160" y="157">irq_exit_rcu → handle_softirqs → net_rx_action</text>
<rect x="150" y="170" width="380" height="24" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="160" y="187">virtnet_poll → napi_complete_done</text>
<rect x="150" y="200" width="380" height="24" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="160" y="217">netif_receive_skb_list_internal → ip_list_rcv</text>
<rect x="150" y="230" width="380" height="24" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="160" y="247">ip_local_deliver → udp_rcv → __udp4_lib_rcv</text>
<rect x="150" y="260" width="380" height="24" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="160" y="277">udp_queue_rcv_one_skb → __udp_enqueue_schedule_skb</text>
<rect x="150" y="290" width="380" height="24" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="160" y="307">sock_def_readable → wake_up r2_udp</text>
<rect x="150" y="330" width="380" height="24" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="160" y="347">schedule → __skb_wait_for_more_packets 복귀</text>
<rect x="150" y="360" width="380" height="24" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="160" y="377">udp_recvmsg → skb_copy_datagram_msg → 9 B</text>
<text x="545" y="117">7.6612 s</text>
<text x="545" y="277">7.6647 s</text>
<text x="545" y="307">7.6652 s</text>
<text x="545" y="377">7.6665 s</text>
</g>
<g stroke="currentColor" marker-end="url(#ah8)">
<line x1="340" y1="64" x2="340" y2="69"/><line x1="340" y1="94" x2="340" y2="99"/><line x1="340" y1="124" x2="340" y2="139"/>
<line x1="340" y1="164" x2="340" y2="169"/><line x1="340" y1="194" x2="340" y2="199"/><line x1="340" y1="224" x2="340" y2="229"/>
<line x1="340" y1="254" x2="340" y2="259"/><line x1="340" y1="284" x2="340" y2="289"/><line x1="340" y1="314" x2="340" y2="329"/>
<line x1="340" y1="354" x2="340" y2="359"/>
</g>
<text x="20" y="402" font-size="12">실측(TCG): UDP 프레임 IRQ 7.6612 → recvmsg 반환 7.6665 s (5.3 ms)</text>
</svg>
```
그림 8 — 패킷 하나가 거치는 세 문맥. hardirq는 "알림"만 하고, 프로토콜 처리는 softirq, 복사는 받는 스레드가 한다. 부하가 높으면 주황 층이 `ksoftirqd`로 옮겨 간다.

### 7.3 꼬리 질문

- **"NAPI는 왜 쓰나요?"** — 패킷마다 IRQ면 고부하에서 IRQ 폭풍(livelock). 첫 IRQ 후 인터럽트를 끄고 polling으로 budget만큼 처리, 비면 `napi_complete_done`으로 IRQ를 다시 켠다. Don의 SSD 펌웨어 CQ 처리에서 interrupt coalescing과 같은 아이디어. → I6 §5
- **"패킷이 버려지는 지점은?"** — RX 링 가득(드라이버 `rx_dropped`), backlog(`netdev_max_backlog`), 소켓 수신 버퍼(`SO_RCVBUF` 초과 → `UdpRcvbufErrors`), 체크섬. `nstat`/`ethtool -S`로 층별 카운터. → A9 §5, I6 §12
- **"LiDAR UDP 스트림에서 지연을 줄이려면?"** — IRQ affinity와 수신 스레드 CPU 일치(RSS/RPS), `SO_RCVBUF` 확대, busy polling(`SO_BUSY_POLL`), PREEMPT_RT에서 threaded NAPI. → A9 §8, N4
- **"TCP면?"** — `tcp_v4_rcv` → `tcp_rcv_established` → 수신 큐 + ACK 생성, `tcp_recvmsg`. 깨우기는 같은 `sk_data_ready`(TCP는 `sock_def_readable` 계열). → A9 §13

---

## 8. 커널 안에서는 무슨 일이 일어나나 — 일곱 경로의 공통 패턴

일곱 경로를 실측 스택으로 겹쳐 보면 같은 패턴 세 개가 반복된다.

| 패턴 | 어디서 봤나 (실측) | 핵심 함수 | 드라이버 작성 때 다시 만나는 곳 |
|---|---|---|---|
| 예외로 들어와 `eret`으로 나간다 | §2·§4 `el0t_64_sync` → `el0_svc`, §5 `el0_da`, §3·§7 `el1h_64_irq` | `el0t_64_sync_handler`, `el1_interrupt`, `exit_to_user_mode` | 드라이버 코드는 이 셋 중 어느 문맥에서 불리는지로 할 수 있는 일이 정해진다 (F2) |
| 테이블 간접 호출 | `f_op->open`/`->read`, `drv->probe`, `irq_desc->handle_irq`, `sk->sk_data_ready` | `do_dentry_open`, `really_probe`, `handle_irq_desc` | 드라이버가 채우는 ops 구조체 전부 (G4 §5, G1) |
| 잠들고 깨운다 | §3 `prev_state=S` → `sched_waking`, §7 `__skb_wait_for_more_packets` → `sock_def_readable`, §6 `wait4` | `wait_event_interruptible`, `__wake_up`, `try_to_wake_up`, `schedule` | blocking `.read`, `.poll`, completion (F3, G6) |

두 가지를 더 기억한다.

- **트레이스에 보이는 이름 ≠ 소스의 이름.** 이 노트에서만 인라인(`do_anonymous_page`, `of_driver_match_device`, `idempotent_init_module`), 추적 금지 영역(`do_page_fault`, `do_translation_fault`, `el0_da`), GCC 클론 이름(`do_execveat_common.isra.0`, `evdev_pass_values.part.0`), 그리고 함수 진입 kprobe 스택에서 빠지는 직전 호출자(`platform_probe`, `do_page_fault`, `__handle_irq_event_percpu`, `evdev_events`, `__udp_enqueue_schedule_skb`)를 만났다. 면접에서 "트레이스에 X가 안 보이는데요?"라는 질문에 이 네 가지 이유를 댈 수 있으면 실제로 트레이스를 써 본 사람으로 보인다.
- **부트 CPU는 PID 0이 된다.** §3·§7의 IRQ 스택 맨 아래 `do_idle ← cpu_startup_entry ← rest_init ← start_kernel ← __primary_switched`는 §1의 walk 8단계가 끝나지 않고 지금도 돌고 있다는 증거다.

---

## 9. Embedded 실무에서 다시 보기

- **"버튼 눌렀는데 앱 반응이 300 ms 늦다"** (센서 키트의 E-stop 버튼): §3의 타임라인을 실제 보드에서 그대로 찍는다. `irq_handler_entry` → (debounce) → `sched_waking` → `sched_switch` → 앱의 `read` 반환. 지연이 debounce 설정(`debounce-interval`)인지, 앱 스레드가 SCHED_OTHER라 늦게 스케줄되는지(F1 §8), IRQ가 다른 CPU에 가서 IPI가 끼는지(F6 §7) 구간별로 갈린다.
- **"부팅 후 `/dev/imu0`가 없다"**: §4의 walk 순서대로 본다 — `of_platform_populate`가 장치를 만들었나 → 모듈이 로드됐나(modalias) → `platform_match`가 맞나 → `really_probe`에서 `-EPROBE_DEFER`인가 → `device_create`가 불렸나(§2의 devtmpfs). 각 단계에 sysfs 증거가 있다(D3 §6, G3 §9).
- **"카메라 프레임 처리 루프가 처음 몇 프레임만 느리다"**: §5. 버퍼를 처음 건드릴 때 page fault(THP면 2 MiB 0 채우기 830 µs도 실측)가 루프 안에서 일어난다. `mlockall` + prefault, 또는 dma-buf로 커널이 미리 할당한 버퍼를 `mmap`(A5 §12, H9).
- **"LiDAR 패킷이 가끔 빠진다"**: §7의 층별 드롭 카운터 + `napi_poll` 이벤트의 `work`/`budget`. budget을 계속 다 쓰면 softirq가 `ksoftirqd`로 밀려 수신 스레드와 같은 CPU에서 경쟁한다.
- **"부팅 3초 목표"**: §1 그림 2의 막대 순서대로. M10이 실측한 bootdelay 2 s, 이 노트가 실측한 apparmor·udev coldplug 대기가 제일 크다.

---

## 10. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `set_graph_function`에 소스 함수 이름을 그대로 씀 | `write error: Invalid argument` | 함수가 인라인됐거나 `.isra.0`/`.part.0` 클론 이름 | `grep 이름 available_filter_functions`로 실제 심볼 확인 |
| 스택 트레이스에 중간 함수가 없다고 "그 경로가 아니다"라고 결론 | 잘못된 분석 | `+0x0` kprobe 시점엔 직전 호출자가 LR에만 있음, 또는 인라인 | function_graph와 스택을 함께 보고 소스로 확인 |
| fault 횟수를 재는데 `printf` 첫 호출을 측정 구간에 둠 | `delta=2` | stdio 버퍼가 malloc되며 fault | 측정 전에 한 번 써서 버퍼를 만든다 |
| `madvise`에 malloc 포인터를 그대로 넘김 | `madvise: Invalid argument` | 주소가 페이지 정렬이 아님 | `& ~4095` 정렬, 또는 `mmap`을 직접 사용 |
| 드라이버 fops에 `.owner` 누락 | open 중 rmmod 성공 후 oops | `chrdev_open`의 `try_module_get`이 아무것도 안 잡음 | `.owner = THIS_MODULE` |
| hardirq ISR에서 I2C 읽기 | `BUG: scheduling while atomic` | hardirq는 잠들 수 없다 | `request_threaded_irq` + `IRQF_ONESHOT` (§3.4) |
| wake_up 전에 조건 변수를 안 바꿈 | 스레드가 깨어났다가 다시 잠듦(영원히) | `wait_event`가 조건을 재확인 | 데이터·플래그를 먼저 쓰고 깨운다 (F3 §5) |
| initcall 순서에 기대는 probe | 보드마다 가끔 probe 실패 | 공급자 드라이버가 아직 없음 | `-EPROBE_DEFER` 반환, devm·fw_devlink에 맡김 (G3 §9) |
| BusyBox `insmod` 결과로 "insmod는 init_module"이라고 일반화 | 면접에서 틀린 답 | kmod는 `finit_module`을 쓴다 | 실측 §4.4: 두 syscall은 `load_module`에서 합류 |

---

## 11. 면접에서 이렇게 말한다

**Q.** "Walk me through what happens from power-on until you get a shell prompt on an ARM64 Linux board."

**A.** BootROM → TF-A BL1/BL2/BL31 → U-Boot → `booti`(x0=DTB) → `head.S`가 MMU를 켜고 `start_kernel` → `rest_init`이 PID 1 `kernel_init` → initcall로 드라이버 probe → rootfs 마운트 → `/sbin/init` exec → systemd → getty → login → 셸. 실측으로 커널 2.2 s, 유저 공간 31 s였고 대부분 apparmor·udev 대기였다고 덧붙인다.

> "The boot ROM picks the boot device from straps or fuses and loads the first stage — on our ARM64 boards that's TF-A BL1, which loads BL2. BL2 brings up DRAM and loads BL31, the EL3 runtime that provides PSCI, plus U-Boot as BL33. U-Boot loads the kernel Image, the DTB and the initramfs, patches the chosen node, and jumps to the kernel with the DTB address in x0 and the MMU off. head.S builds the identity map, enables the MMU, relocates, and calls start_kernel, which parses the device tree, sets up memory, the scheduler, the GIC and the timer, and then rest_init creates PID 1, kernel_init. kernel_init runs the initcalls — that's where built-in drivers register and get probed against the device tree — mounts the root filesystem, frees init memory and execs /sbin/init. systemd then walks its dependency graph to getty.target; agetty execs login, and login starts the shell. When I measured it in QEMU, the kernel took about two seconds and user space thirty, almost all of it waiting on AppArmor and udev coldplug — which is typical: the kernel is rarely the bottleneck."

**꼬리 질문**: "콘솔에 아무것도 안 나오면?" → earlycon, DTB 주소·정렬, `__log_buf` JTAG 덤프 (§1.5).

**Q.** "What happens in the kernel when a user calls open() on /dev/mydevice?"

**A.** syscall → `do_sys_openat2` → fd 번호 예약 → `path_openat`의 dcache 탐색 → char device inode의 `def_chr_fops` → `chrdev_open`이 `dev_t`로 `cdev`를 찾아 `f_op`을 드라이버 fops로 교체 → 드라이버 `.open` → `fd_install`. 실측에서 `struct file` 주소가 open 결과와 read의 `fdget_pos`에서 같았다.

> "The svc instruction traps into el0_svc, which dispatches to do_sys_openat2. It first reserves a free fd number, then path_openat walks the path component by component through the dentry cache, mostly lock-free in RCU mode. The device node's inode only carries the dev_t, and its file operations are the generic def_chr_fops, so do_dentry_open calls chrdev_open. chrdev_open looks up the cdev registered for that major/minor in cdev_map, takes a reference on the owning module, swaps the file's f_op to the driver's file_operations, and calls the driver's open. On success fd_install puts the struct file into the fd table and the number goes back to user space. After that, read() is cheap: fd to struct file to f_op->read, straight into the driver — I verified with function_graph that the struct file pointer returned by path_openat is the same one fdget_pos finds on read."

**꼬리 질문**: "노드는 있는데 드라이버가 없으면?" → `-ENXIO`, 노드가 없으면 `-ENOENT` (§2.4).

**Q.** "A hardware interrupt fires. How does a user thread blocked in read() end up running again?"

**A.** 잠: `wait_event_interruptible` = 상태 변경 + wait queue + `schedule()`. 깨움: GIC → `gic_handle_irq` → irq_domain → flow handler → ISR → bottom half → `wake_up` → `try_to_wake_up` → need_resched → IRQ 출구 또는 다음 스케줄 지점에서 전환 → 조건 재확인 → `copy_to_user` → `eret`.

> "The reader went to sleep in the driver's read with wait_event_interruptible: it set itself TASK_INTERRUPTIBLE, added itself to the device's wait queue and called schedule, so it's off the run queue. When the line toggles, the GPIO controller raises an SPI on the GIC; the CPU takes the IRQ exception, gic_handle_irq reads the acknowledge register, and generic_handle_domain_irq maps the hardware number through the irq domain — in my trace it went through two domains, the GIC and then the PL061 GPIO chip's chained handler — to the edge flow handler and the driver's ISR. The ISR does the minimum and defers; in gpio-keys that's a 5 ms debounce hrtimer, in an I2C sensor driver it would be a threaded IRQ. The deferred part pushes the event into the buffer and calls wake_up_interruptible, which runs try_to_wake_up: the task goes back on the run queue and need_resched is set. On the way out of the interrupt, or at the next scheduling point, schedule switches to the reader, which rechecks its condition, copies the data to user space and returns. In QEMU I measured about 11 ms from IRQ to read returning, of which 5 ms was the debounce."

**꼬리 질문**: "lost wakeup은?" → 상태 먼저 바꾸고 조건 확인, 깨우는 쪽은 조건 먼저 쓰기 (§3.5).

**Q.** "You run insmod on your driver. What has to happen before your probe() is called?"

**A.** `finit_module` → `load_module`(서명·vermagic·재배치·심볼) → `do_init_module` → `module_init` = `platform_driver_register` → `bus_add_driver` → `driver_attach`가 버스 장치를 순회하며 `platform_match`(OF compatible) → 맞으면 `really_probe` → `platform_probe` → `probe()`. 실측: 45개 장치 중 1개 매치.

> "insmod opens the .ko and calls finit_module — BusyBox uses init_module with a buffer, but both end in load_module. load_module checks the signature and vermagic, allocates module memory, copies the sections, resolves symbols against the exported symbol table, applies relocations, sets page permissions and creates the sysfs entries. Then do_init_module calls the module's init function through do_one_initcall. With module_platform_driver that's platform_driver_register, which goes driver_register, bus_add_driver, driver_attach. driver_attach iterates over every device already on the platform bus and calls platform_match on each — the OF match compares the device tree compatible strings against the driver's of_match_table. In my trace it tried 45 devices and only my node matched. For the match it calls driver_probe_device, really_probe — which binds the device, applies the default pinctrl state and DMA configuration — then platform_probe, and finally my probe. If the device shows up later instead, device_add triggers the same match from the other side and ends in the same really_probe."

**꼬리 질문**: "probe가 안 불리면 무엇부터?" → 장치 존재 → compatible → deferred → probe 에러 (§4.5).

**Q.** "What happens on the first write to memory you just got from malloc?"

**A.** malloc은 VMA만 만든다 → 첫 store에서 translation fault → `el0_da` → `do_mem_abort` → `do_translation_fault` → `lock_vma_under_rcu` → `handle_mm_fault` → 0으로 채운 페이지 할당 + PTE → store 재실행. THP면 2 MiB, 주소 정렬에 따라 달라진다.

> "A large malloc is just an anonymous mmap: the kernel creates a VMA but no page and no page-table entry. The first store takes a translation fault from EL0; el0_da calls do_mem_abort, which uses the fault status in ESR to dispatch to do_translation_fault and the page-fault handler. It finds the VMA under the per-VMA RCU lock, checks permissions, and calls handle_mm_fault, which allocates a zeroed page, charges it to the memory cgroup, installs the PTE and puts the page on the LRU. Then we return and the same store re-executes and succeeds — that's one minor fault. One thing I saw in traces: with THP set to always, if the 2 MB-aligned region fits in the VMA you get a 2 MB huge page instead, and zeroing it took ten times longer — and whether that happens depended on the randomized address. That's why real-time code locks and pre-faults its memory."

**꼬리 질문**: "커널이 유저 포인터에서 fault 나면?" → 예외 테이블 fixup → `-EFAULT` (§5.5).

**Q.** "Explain fork() and exec() at the kernel level."

**A.** `kernel_clone` → `copy_process`(task_struct, fd 테이블, `dup_mmap` + COW) → `alloc_pid` → `wake_up_new_task`; 자식은 `ret_from_fork`에서 0 반환. exec는 `bprm_execve` → `load_elf_binary` → `begin_new_exec`(옛 mm 버림, CLOEXEC) → `START_THREAD`. 실측: fork의 절반이 `dup_mmap`.

> "fork is clone with SIGCHLD; kernel_clone calls copy_process, which duplicates the task_struct and kernel stack, copies the fd table and fs info, and dup_mmap copies the VMAs and write-protects private pages in both parent and child for copy-on-write. It allocates a PID and wake_up_new_task puts the child on a run queue; the child returns from ret_from_fork with zero in x0. exec goes through do_execveat_common and bprm_execve to the ELF loader, which builds a new mm, and begin_new_exec is the point of no return: the old address space is dropped — ending any COW sharing — close-on-exec fds are closed, the segments are mapped, argv and the aux vector go on the new stack, and start_thread points the PC at the entry. In my trace about half of fork's time was dup_mmap, which is why posix_spawn or vfork is preferred for big processes."

**Q.** "A UDP packet arrives at the NIC. How does it get to recv()?"

**A.** DMA → IRQ → 드라이버 ISR이 `napi_schedule` → softirq `net_rx_action` → 드라이버 poll → `netif_receive_skb` → `ip_rcv` → `udp_rcv` → 소켓 큐 + `sock_def_readable` → 잠든 `recvfrom` 깨어남 → `udp_recvmsg` 복사.

> "The NIC DMAs the frame into a pre-posted RX buffer and raises an interrupt. The hard IRQ handler does almost nothing — it masks that queue's interrupt and schedules NAPI, which raises the NET_RX softirq. On interrupt exit, net_rx_action calls the driver's poll function with a budget; the driver builds skbs and hands them up through netif_receive_skb, IP receive, and udp_rcv, which looks up the socket by its four-tuple, appends the skb to the socket's receive queue and calls sk_data_ready — sock_def_readable — which wakes the thread sleeping in recvfrom. That thread then copies the payload out in udp_recvmsg. In my trace on virtio-net the whole softirq stack ran on the interrupt exit path, and a second interrupt even preempted the softirq; under load that work moves to ksoftirqd."

**꼬리 질문**: "패킷이 어디서 버려질 수 있나?" → 링, backlog, 소켓 버퍼 (§7.3).

**Q.** "How would you prove any of this on a real board?"

**A.** ftrace `function_graph` + `set_graph_function`, kprobe + `stacktrace` 트리거, sched/irq tracepoint. 이 노트의 모든 경로를 그렇게 찍었다.

> "I'd trace it. function_graph with set_graph_function scoped to the entry point and a PID filter gives the call tree with durations; a kprobe with a stacktrace trigger on the leaf — the driver's probe, the ISR, sock_def_readable — gives the whole path from the exception vector; and the sched_waking and sched_switch tracepoints give the wakeup timing. The two gotchas are that inlined functions and compiler clones like .isra.0 don't show up under their source names, and that a kprobe on a function's first instruction fires before the prologue, so the stack trace skips the immediate caller — I always cross-check against the source."

---

## 12. 직접 해보기

1. (개념) §2의 open 경로에서 `-ENOENT`, `-EACCES`, `-ENXIO`, `-EBUSY`가 각각 몇 번 단계에서 나올 수 있는지 표로 정리하라. 정답/힌트: 5단계(마지막 lookup), 6단계(`may_open`), 8단계(`chrdev_open`의 `kobj_lookup` 실패), 드라이버 `.open`이 단독 사용 장치에서 돌려주는 값.
2. (트레이스) §3의 r2b 설정에서 `gpio-keys` 노드에 `debounce-interval`을 바꾼 DT로 다시 부팅해 `gk_isr` → `gk_tmr` 간격이 바뀌는지 확인하라. 정답/힌트: `-dtb`로 `poweroff` 노드에 `debounce-interval = <20>;` 추가, 간격 약 20 ms + TCG 오차.
3. (코드) `r2_demo`에 `wait_queue_head_t`와 debugfs `fire` 파일을 추가해, `read()`가 잠들고 `echo 1 > fire`가 깨우게 만들어라. 그리고 §3처럼 `sched_waking`/`sched_switch`로 깨어남을 확인하라. 정답/힌트: F3 §2의 `f3_queue`와 같은 구조, 깨우는 문맥이 프로세스 문맥이라 `h` 플래그가 없다.
4. (관찰) `r2_pf`를 `nothp` 없이 10번 돌려 THP 경로(`do_huge_pmd_anonymous_page`)가 몇 번 나오는지 세라. 정답/힌트: 주소의 2 MiB 정렬 시작이 VMA 시작(`p - 16`) 이상인 경우만 THP. `/proc/<pid>/smaps`의 `AnonHugePages`로도 확인.
5. (트레이스) `insmod` 대신 장치를 나중에 만드는 경로를 찍어라: 모듈을 먼저 넣고 DT overlay(D6)나 `driver_override` + `bind`로 장치를 붙여, `r2_probe` 스택이 `device_add` → `bus_probe_device` → `__device_attach` 쪽으로 바뀌는지 본다. 정답/힌트: 스택에 `driver_attach` 대신 `__device_attach_driver`가 보인다.
6. (말하기) §11의 일곱 질문을 각각 30초 타이머로 소리 내어 답하고 녹음하라. 함수 이름을 하나라도 틀리면 해당 절의 실측 줄을 다시 본다.
7. (트레이스) §7을 TCP로 바꿔(`nc` 대신 작은 TCP 서버) `sock_def_readable` 대신 무엇이 `sk_data_ready`인지 스택으로 확인하라. 정답/힌트: TCP도 기본은 `sock_def_readable`이고, 그 위가 `tcp_data_queue` → `tcp_rcv_established`.

---

## 13. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| BL1/BL2/BL31/BL33 | TF-A 부트 단계 | ROM 로더 / DRAM·이미지 로더 / EL3 상주 런타임(PSCI) / 일반 세계 부트로더(U-Boot) |
| `booti` | U-Boot arm64 커널 부트 명령 | x0=DTB로 `Image` 진입점에 점프 |
| `rest_init` | 커널 초기화 마지막 함수 | PID 1·2를 만들고 자신은 idle(PID 0)이 된다 |
| initcall | 레벨별 초기화 함수 표 | 링커 섹션에 모인 `module_init` 등을 순서대로 호출 |
| `def_chr_fops` | char device 기본 fops | `.open = chrdev_open`만 있다 |
| `cdev_map` | `dev_t` → `cdev` 매핑 | `cdev_add`가 등록, `chrdev_open`이 조회 |
| `replace_fops` | `file->f_op` 교체 매크로 | 첫 open 때 드라이버 fops로 바꿔 끼움 |
| irq_domain | hwirq ↔ Linux IRQ 번호 변환표 | GIC, GPIO 칩마다 하나씩, 겹칠 수 있다 |
| chained handler | 하위 IRQ 칩을 위한 상위 핸들러 | `pl061_irq_handler`처럼 다시 `generic_handle_domain_irq`를 부른다 |
| bottom half | IRQ 처리의 미룬 부분 | softirq, tasklet, threaded IRQ, workqueue, hrtimer 콜백 |
| need_resched | "곧 스케줄하라" 플래그 | 트레이스의 `N` 플래그 |
| `really_probe` | 드라이버 코어의 probe 실행부 | 장치 결합, pinctrl, DMA 설정 후 `drv->probe` |
| `load_module` | 모듈 적재 본체 | 서명·vermagic·재배치·심볼 해결·sysfs |
| THP | Transparent Huge Page | 익명 메모리를 2 MiB 단위로 매핑 |
| minor fault | 디스크 I/O 없는 page fault | 첫 접근 익명 페이지, COW |
| COW | Copy-on-Write | fork 후 쓰기 시점에만 페이지 복사 |
| `begin_new_exec` | exec의 되돌릴 수 없는 지점 | 옛 mm 해제, CLOEXEC fd 닫기 |
| NAPI | IRQ + polling 혼합 RX | 첫 IRQ 후 poll로 budget만큼 처리 |
| `sk_data_ready` | 소켓 "데이터 왔음" 콜백 | 기본은 `sock_def_readable` → wait queue 깨움 |
| `.isra.0` / `.part.0` | GCC 함수 클론 접미사 | 트레이스·kprobe에 이 이름으로 걸어야 한다 |

---

## 14. 요약 & 체크리스트

일곱 "무슨 일이 일어나나" 질문은 같은 부품의 조합이다: **예외로 들어와서**(svc, data abort, IRQ), **테이블을 따라 간접 호출하고**(`f_op`, `drv->probe`, `handle_irq`, `sk_data_ready`), **잠들거나 깨우고**(wait queue + `try_to_wake_up` + `schedule`), **`eret`으로 나간다**. 이 노트는 각 경로를 6.18.55 커널에서 function_graph·kprobe 스택·sched 이벤트로 실제 찍었다: `alloc_fd ret=0x3`과 같은 `struct file` 주소, PL061 체인 핸들러를 거치는 두 겹 irq_domain, 45개 장치 중 하나만 `platform_match = 1`, `delta=1` page fault와 주소에 따라 달라지는 THP, `dup_mmap`이 절반인 fork, softirq가 IRQ 출구에서 도는 UDP 수신. 면접에서는 30초 버전으로 끝까지 간 다음, 면접관이 멈춘 번호에서 이 실측 줄 하나를 근거로 내려가면 된다.

- [ ] 전원 ON → 셸을 BootROM부터 login까지 30초 안에 끊김 없이 말할 수 있다
- [ ] `head.S`에서 `start_kernel`까지, `rest_init` → `kernel_init` → `run_init_process`를 함수 이름으로 말할 수 있다
- [ ] `open("/dev/x")`에서 `def_chr_fops` → `chrdev_open` → `replace_fops`가 왜 필요한지 설명할 수 있다
- [ ] fd 번호 예약과 `fd_install`의 순서, `struct file`과 fd의 관계를 실측으로 보여 줄 수 있다
- [ ] GIC → irq_domain → flow handler → ISR → bottom half → `wake_up` → `schedule`을 그림 없이 설명할 수 있다
- [ ] threaded IRQ와 workqueue와 hrtimer bottom half 중 무엇을 언제 쓰는지 말할 수 있다
- [ ] `insmod`에서 `probe()`까지, 그리고 장치가 나중에 생기는 반대 방향 경로를 설명할 수 있다
- [ ] "probe가 안 불린다"를 5단계로 진단할 수 있다
- [ ] 첫 접근 page fault의 arm64 경로와 THP 갈래를 설명할 수 있다
- [ ] fork의 COW가 어디서 시작해 어디서 끝나는지 말할 수 있다
- [ ] 패킷 수신의 hardirq / softirq / 프로세스 문맥 분담을 설명할 수 있다
- [ ] ftrace로 위 경로 중 하나를 직접 찍고, 인라인·클론·tail call 때문에 안 보이는 함수를 설명할 수 있다

---

## 참고 자료

- 랩 소스 (6.18.55): `init/main.c`(`kernel_init`, `run_init_process`), `arch/arm64/kernel/head.S`, `arch/arm64/kernel/entry-common.c`, `arch/arm64/mm/fault.c`, `fs/open.c`, `fs/namei.c`, `fs/char_dev.c`, `fs/read_write.c`, `kernel/module/main.c`, `drivers/base/dd.c`, `drivers/base/platform.c`, `drivers/irqchip/irq-gic.c`, `drivers/gpio/gpio-pl061.c`, `drivers/input/keyboard/gpio_keys.c`, `drivers/input/evdev.c`, `mm/memory.c`, `kernel/fork.c`, `fs/exec.c`, `fs/binfmt_elf.c`, `drivers/net/virtio_net.c`, `net/core/dev.c`, `net/ipv4/udp.c`, `net/core/sock.c` — 같은 경로를 https://elixir.bootlin.com/linux/v6.18/source 에서 볼 수 있다
- docs.kernel.org: "ftrace - Function Tracer" (https://docs.kernel.org/trace/ftrace.html), "Kprobe-based Event Tracing" (https://docs.kernel.org/trace/kprobetrace.html), "Driver Binding" (https://docs.kernel.org/driver-api/driver-model/binding.html), "The Linux Kernel Driver Interface / Platform Devices and Drivers" (https://docs.kernel.org/driver-api/driver-model/platform.html), "Booting AArch64 Linux" (https://docs.kernel.org/arch/arm64/booting.html)
- Trusted Firmware-A 문서: "Firmware Design" (https://trustedfirmware-a.readthedocs.io/en/latest/design/firmware-design.html)
- Michael Kerrisk, "The Linux Programming Interface" — 24장(프로세스 생성), 27장(exec), 49장(메모리 매핑)
- OSTEP — "Mechanism: Limited Direct Execution", "Paging: Faster Translations (TLBs)", "Beyond Physical Memory: Mechanisms" 장
- 이 사이트의 원 노트: C1, C5, C6, C7, M10(§1.4 부트 로그), L1, L2, A2, A3, A5, A9, E3, E6, F1, F3, F6, F7, F11, F12, D3, G2, G3, G4, G6, I3, I6, M5
