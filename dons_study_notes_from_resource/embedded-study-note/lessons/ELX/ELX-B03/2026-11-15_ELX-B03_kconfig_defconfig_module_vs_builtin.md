---
id: ELX-B03
time: 70분
updated: 2026-11-15
---
# ELX-B03 커널 빌드와 Kconfig: defconfig, 모듈 vs built-in

## 학습 목표

- `make defconfig` → 옵션 변경(`menuconfig` / `scripts/config` / fragment) → `olddefconfig` 흐름을 직접 돌리고, 결과 `.config` 를 **`scripts/diffconfig` 와 똑같이 출력하는 도구**로 비교한다.
- "내가 요청한 값이 정말 들어갔나?"를 프로그램으로 판정한다. `select`·프롬프트 없는 심볼·`depends on` 때문에 **조용히 바뀐 값**을 이유와 함께 찾는다.
- 산출물 `vmlinux` · `Image` · `*.ko` · `dtbs` 가 각각 무엇이고 어디로 가는지, `y`(built-in)와 `m`(module)이 빌드 트리(`modules.builtin` / `modules.order`)에 어떻게 남는지 설명한다.
- 돌아가는 커널에서 설정을 되찾는다: QEMU 게스트의 `/proc/config.gz` 가 빌드 트리 `.config` 와 같음을 보이고, `modinfo` 의 `vermagic` 을 `.config` 로부터 예측한다.

## 1. 문제

보드 bring-up과 BSP 작업에서 가장 흔한 질문은 "그 드라이버 커널에 들어가 있어요? 모듈이에요, 내장이에요? 그 커널 `.config` 있어요?"다. 면접에서도 *"vmlinux와 Image의 차이는?"*, *"y와 m은 언제 고르나?"*, *"설정을 바꿨는데 반영이 안 됐다. 어떻게 찾나?"* 가 단골이다. 이 레슨은 그 답을 말로 외우지 않고, 실제 6.18.55 커널 트리에서 설정을 바꿔 보고 **프로그램으로 검증**한다.

> 환경: Lima VM `elinux`(aarch64 Ubuntu, gcc 15.2). 커널 소스는 랩의 `/var/lab/src/linux-6.18.55` 를 **읽기 전용**으로 쓰고, 출력은 모두 `make O=<내 작업 디렉터리>` 로 따로 둔다. 게스트는 **QEMU TCG cortex-a57 -smp 1, 랩 커널 Linux 6.18.55**(`/var/lab/out/Image`, defconfig + IKCONFIG_PROC·GPIO_SIM 등). 모듈은 게스트 안에서만 올린다. 빌드 시간은 같은 VM을 다른 작업과 나눠 쓴 값이라 참고용이다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">설정 → 빌드 → 산출물 → 되찾기 (이 레슨에서 실제로 돈 경로)</text>
<rect x="10" y="40" width="150" height="52" class="f-mut" stroke="currentColor"/>
<text x="20" y="60" fill="currentColor">arch/arm64/configs/</text>
<text x="20" y="80" fill="currentColor">defconfig (1824줄)</text>
<rect x="200" y="40" width="160" height="52" class="f-acc" stroke="currentColor"/>
<text x="210" y="60" fill="currentColor">make O=… defconfig</text>
<text x="210" y="80" fill="currentColor" font-size="12">.config 11752줄, 9636개</text>
<rect x="400" y="40" width="150" height="52" class="f-warn" stroke="currentColor"/>
<text x="410" y="60" fill="currentColor">내 요청 3개</text>
<text x="410" y="80" fill="currentColor" font-size="12">menuconfig / fragment</text>
<rect x="590" y="40" width="140" height="52" class="f-acc" stroke="currentColor"/>
<text x="600" y="60" fill="currentColor">olddefconfig</text>
<text x="600" y="80" fill="currentColor" font-size="12">depends/select 해결</text>
<line x1="160" y1="66" x2="198" y2="66" stroke="currentColor"/>
<line x1="360" y1="66" x2="398" y2="66" stroke="currentColor"/>
<line x1="550" y1="66" x2="588" y2="66" stroke="currentColor"/>
<line x1="660" y1="92" x2="660" y2="120" stroke="currentColor"/>
<rect x="400" y="120" width="330" height="40" class="f-ok" stroke="currentColor"/>
<text x="410" y="145" fill="currentColor">최종 .config → diffconfig + "안 들어간 요청" 검사</text>
<line x1="400" y1="140" x2="362" y2="140" stroke="currentColor"/>
<rect x="200" y="120" width="160" height="40" class="f-acc" stroke="currentColor"/>
<text x="210" y="145" fill="currentColor">make Image modules</text>
<line x1="280" y1="160" x2="280" y2="185" stroke="currentColor"/>
<rect x="10" y="185" width="170" height="60" class="f-mut" stroke="currentColor"/>
<text x="20" y="205" fill="currentColor">vmlinux (ELF)</text>
<text x="20" y="225" fill="currentColor" font-size="12">심볼·디버그 정보, 보드엔</text>
<text x="20" y="240" fill="currentColor" font-size="12">안 올림, Oops 해석용 보관</text>
<rect x="190" y="185" width="170" height="60" class="f-ok" stroke="currentColor"/>
<text x="200" y="205" fill="currentColor">Image (raw)</text>
<text x="200" y="225" fill="currentColor" font-size="12">objcopy -O binary vmlinux</text>
<text x="200" y="240" fill="currentColor" font-size="12">부트로더가 DRAM에 적재</text>
<rect x="370" y="185" width="170" height="60" class="f-ok" stroke="currentColor"/>
<text x="380" y="205" fill="currentColor">*.ko (=m)</text>
<text x="380" y="225" fill="currentColor" font-size="12">modules.order 에 기록</text>
<text x="380" y="240" fill="currentColor" font-size="12">rootfs /lib/modules/&lt;rel&gt;</text>
<rect x="550" y="185" width="180" height="60" class="f-mut" stroke="currentColor"/>
<text x="560" y="205" fill="currentColor">*.dtb (make dtbs)</text>
<text x="560" y="225" fill="currentColor" font-size="12">보드 기술, 부트로더가</text>
<text x="560" y="240" fill="currentColor" font-size="12">x0 레지스터로 주소 전달</text>
<rect x="10" y="270" width="720" height="44" class="f-warn" stroke="currentColor"/>
<text x="20" y="290" fill="currentColor">되찾기: 게스트 /proc/config.gz (IKCONFIG_PROC) · scripts/extract-ikconfig Image ·</text>
<text x="20" y="306" fill="currentColor">modinfo *.ko 의 vermagic "6.18.55 SMP preempt mod_unload aarch64" ← .config 로 예측 가능</text>
</svg>
```

바꿀 옵션 세 개와 이유:

| 요청 | arm64 defconfig 값 | 바꾸는 이유 (실무 장면) |
|---|---|---|
| `CONFIG_GPIO_SIM=m` | `# … is not set` | 보드 없이 GPIO 사용자공간 코드를 시험할 가상 칩 (ELX-B10이 씀). 필요할 때만 올리도록 모듈 |
| 디버그 정보 끄기 (`DEBUG_INFO`) | `y` (`DWARF_TOOLCHAIN_DEFAULT` + `REDUCED`) | 양산 빌드 크기·시간 줄이기 |
| `CONFIG_IKCONFIG=m` | `y` (`IKCONFIG_PROC=y`) | 설정 사본을 Image 밖(모듈)으로 빼서 Image를 줄이되, 필요하면 `/proc/config.gz` 를 되살린다 |

### Part A — `.config` 읽기와 `diffconfig` 복제

- A1. `parse_config(text)` — `CONFIG_X=y|m|숫자|0x…|"문자열"` 과 `# CONFIG_X is not set`(= `n`)을 읽는다. CRLF, 마지막 줄 개행 없음, 쓰레기 줄, 중복(뒤가 이김), 문자열 안의 `=` 를 견딘다.
- A2. `format_diff(a, b, merge)` — 진짜 `scripts/diffconfig` 와 **한 글자도 다르지 않은** 출력(`-X old`, ` X old -> new`, `+X new`, `-m` 이면 config 줄).
- A3. 실제 6.18.55 `.config` 네 개(defconfig, 요청 3종 적용본)로 골든 비교. `check.sh` 는 매번 VM에서 새로 만든 `.config` 로 다시 비교한다.

### Part B — 내 요청이 살아남았나

동료가 위 세 가지를 이렇게 적어 왔다 (`req_naive.config`):

```text
CONFIG_GPIO_SIM=m
# CONFIG_DEBUG_INFO is not set
CONFIG_IKCONFIG=m
```

`check_requests(req, final, known)` 는 요청 중 최종 `.config` 에서 다른 값이 된 것을 `(이름, 원한 값, 실제 값, 이유)` 로 돌려준다. 이유는 `unknown`(없는 심볼) · `dropped`(의존성 미충족으로 사라짐) · `capped`(y를 원했는데 m) · `forced`(n을 원했는데 select 등으로 켜짐) · `changed`. 최종 파일에 없는 심볼은 `n` 으로 친다.

### Part C — bring-up: 커널이 실제로 무엇으로 빌드됐나

- C1. `expected_vermagic(cfg, release)` — `.config` 로부터 모듈의 `vermagic` 문자열을 만든다 (`include/linux/vermagic.h` 규칙).
- C2. `module_kind(name, modules.builtin, modules.order)` — 드라이버가 내장(y)인지 모듈(m)인지 빌드 트리로 판정 (`gpio_sim` == `gpio-sim`).
- 게스트에서: `/proc/config.gz` == 빌드 트리 `.config`, 예측한 vermagic == `modinfo`, `insmod` 성공.
- 그리고 `req_bringup.config`: "Image를 줄이려고 UART를 모듈로, 콘솔은 유지, i2c-stub 내장, GPIO 시뮬레이터 모듈" — 여기 함정 세 개가 있다. 도구가 셋 다 찾아야 한다.

## 2. Problem (English)

> You maintain the kernel config for an arm64 board. Write a small tool (Python 3, stdlib only):
>
> ```python
> def parse_config(text) -> dict            # 'CONFIG_X=v' -> {'X': 'v'}, '# CONFIG_X is not set' -> {'X': 'n'}
> def format_diff(a, b, merge=False) -> list[str]   # byte-identical to scripts/diffconfig [-m]
> def check_requests(req, final, known=None) -> list[tuple]  # (name, wanted, got, reason)
> def expected_vermagic(cfg, release, arch="aarch64") -> str
> def module_kind(modname, builtin_text, order_text) -> str  # 'y' | 'm' | 'n'
> ```
>
> Constraints: values are kept raw (`"str"`, `0x10`, `250`); a symbol missing from a .config counts as `n`; split lines on `\n` only and tolerate CRLF / no trailing newline; module names treat `-` and `_` as equal.
>
> Example (real 6.18.55 tree, defconfig + `req_naive.config`, then `olddefconfig`):
> ```text
> $ kconfdiff diff defconfig.config final_naive.config
>  GPIO_SIM n -> m
>  IKCONFIG y -> m
> +DEV_SYNC_PROBE m
> +IRQ_SIM y
> $ kconfdiff stuck req_naive.config final_naive.config
> NOT STUCK DEBUG_INFO wanted=n got=y reason=forced
> ```
>
> Follow-ups: why did `DEBUG_INFO` come back? Why did two symbols you never asked for appear? Where does `vermagic` come from and when does a module fail to load?

## 3. 시작 코드

파일: `starter.py`(TODO 5개 + 하네스), `fixtures/`(실제 `.config` 4개를 묶은 `configs.tar.xz` 59 KB + 진짜 `scripts/diffconfig` 출력 골든 6개), 요청 조각 `req_naive.config` · `req_good.config` · `req_bringup.config`, 그리고 VM 안에서 모든 것을 돌리는 `check.sh`.

```bash
# Mac에서도 self-test는 돈다 (fixtures만 씀)
python3 -I starter.py          # 5/32 passed, exit 1
# 전체 검사: VM에서 defconfig/olddefconfig 를 새로 돌리고, 랩 커널을 QEMU로 부팅한다
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B03/check.sh starter
```

<!-- code: starter.py -->

<!-- code: req_bringup.config -->

<!-- code: check.sh -->

`check.sh` 의 판정은 전부 **정확한 텍스트 비교와 개수**다: 내 `diff` 출력 == 진짜 `scripts/diffconfig` 출력(`cmp`), "안 들어간 요청" 줄 == 기대 줄, 게스트 설정 심볼 수 == 빌드 트리 심볼 수, 예측 vermagic == `modinfo -F vermagic`. 시간은 보지 않는다.

## 4. 힌트

<details><summary>힌트 1 — 진짜 diffconfig는 어떻게 읽나</summary>

`/var/lab/src/linux-6.18.55/scripts/diffconfig` 의 `readconfig()` 는 8줄이다: `CONFIG_` 로 시작하면 첫 `=` 에서 한 번만 자르고(`split("=", 1)`), 끝이 `" is not set"` 이면 `line[9:-11]` 이 이름이다. 값은 **해석하지 않는다** — `"str"` 의 따옴표도 그대로다. 출력 순서는 "없어진 것 → 바뀐 것 → 새로 생긴 것", 각 묶음 안은 이름순.
</details>

<details><summary>힌트 2 — `splitlines()` 를 쓰면 안 되는 이유</summary>

`str.splitlines()` 는 `\n` 말고도 `\x0b \x0c \x1c-\x1e \x85  ` 에서 자른다. 값 안에 그런 바이트가 있으면 한 줄이 둘로 쪼개진다. Kconfig 리더(`scripts/kconfig/confdata.c`)는 `\n` 기준으로 읽는다. `text.split("\n")` + `rstrip("\r")`.
</details>

<details><summary>힌트 3 — "없음"과 "n"</summary>

`olddefconfig` 는 **보이지 않는(의존성이 안 맞는) 심볼을 아예 쓰지 않는다**. 그래서 n을 원했는데 최종 파일에 줄이 없으면 그건 성공이다. 반대로 y/m을 원했는데 줄이 없으면 "의존성 때문에 사라졌다"(`dropped`). 이름 자체가 Kconfig에 없으면(`known` 집합에 없음) 오타다(`unknown`) — 이 판정을 먼저 한다.
</details>

<details><summary>힌트 4 — vermagic 규칙</summary>

`include/linux/vermagic.h`: `UTS_RELEASE " "` + (`SMP ` if SMP) + (`preempt_rt ` if PREEMPT_RT, 아니면 `preempt ` if PREEMPT_BUILD) + (`mod_unload ` if MODULE_UNLOAD) + (`modversions ` if MODVERSIONS) + `MODULE_ARCH_VERMAGIC`(arm64 = `"aarch64"`). release는 `.config` 에 없다 — `uname -r` 이나 빌드 트리의 `include/config/kernel.release` 에서 온다.
</details>

## 5. 풀이

### A. defconfig에서 요청 적용까지 — 실제로 돈 명령

```bash
cd /var/lab/src/linux-6.18.55            # 읽기 전용으로만 쓴다
make -s O=/var/tmp/ELX-B03_work/explore/def defconfig        # real 0m2.4s
wc -l /var/tmp/ELX-B03_work/explore/def/.config               # 11752
# (1) scripts/config 로 요청 그대로 적용 → olddefconfig
scripts/config --file $O/.config -m GPIO_SIM -d DEBUG_INFO -m IKCONFIG
make -s O=$O olddefconfig
scripts/diffconfig $DEF/.config $O/.config
```

```text
== naive requested vs def          (scripts/config 직후, olddefconfig 전)
 DEBUG_INFO y -> n
 GPIO_SIM n -> m
 IKCONFIG y -> m
== naive final vs def              (olddefconfig 후)
 GPIO_SIM n -> m
 IKCONFIG y -> m
+DEV_SYNC_PROBE m
+IRQ_SIM y
```

`scripts/config` 는 **텍스트 편집기일 뿐**이다. 파일에 `DEBUG_INFO y -> n` 이 분명히 적혔지만, `olddefconfig` 가 Kconfig 규칙으로 다시 계산하자 사라졌다. 대신 요청하지 않은 `DEV_SYNC_PROBE`, `IRQ_SIM` 이 생겼다. `check.sh` 는 같은 일을 `cat defconfig req_X.config > .config && make olddefconfig` 로 한다 (뒤 줄이 앞 줄을 덮는다. Kconfig가 `.config:11755:warning: override: reassigning to symbol DEBUG_INFO` 라고 알려 준다). 이 방법과 `scripts/kconfig/merge_config.sh` 결과가 바이트 단위로 같음을 `cmp` 로 확인했다.

`menuconfig` 로도 해 볼 수 있다 (대화형이라 check.sh에는 넣지 않았다):

```bash
limactl shell elinux -- bash -lc 'cd /var/lab/src/linux-6.18.55 && mkdir -p /var/tmp/ELX-B03_work/m && make O=/var/tmp/ELX-B03_work/m defconfig menuconfig'
```

`/` 로 검색하면 Kconfig가 왜 그렇게 계산했는지 그대로 보인다 (tmux로 캡처한 실제 화면, `menuconfig_search.txt`):

```text
 Symbol: DEBUG_INFO [=y]
 Type  : bool
 Defined at lib/Kconfig.debug:227
 Selected by [y]:
   - DEBUG_INFO_DWARF_TOOLCHAIN_DEFAULT [=y] && DEBUG_KERNEL [=y] && (!CC_IS_CLANG [=n] || A …
 …
 Symbol: GPIO_SIM [=m]
 Type  : tristate
 Defined at drivers/gpio/Kconfig:2009
   Prompt: GPIO Simulator Module
   Depends on: GPIOLIB [=y]
 Selects: IRQ_SIM [=y] && CONFIGFS_FS [=y] && DEV_SYNC_PROBE [=m]
```

### A1/A2. 파서와 diff — 표로 보는 규칙

| 입력 줄 | `parse_config` | 진짜 diffconfig |
|---|---|---|
| `CONFIG_HZ=250` | `HZ: '250'` | 같음 |
| `CONFIG_CMDLINE="console=ttyAMA0 root=/dev/vda"` | 첫 `=` 에서만 자름, 따옴표 유지 | 같음 (`split("=", 1)`) |
| `# CONFIG_GPIO_SIM is not set` | `GPIO_SIM: 'n'` | 같음 (`line[9:-11]`) |
| `# Automatically generated …`, 빈 줄 | 무시 | 무시 |
| 마지막 줄 `CONFIG_B=y` 에 개행 없음 | `B: 'y'` | **`B: ''`** — `line[:-1]` 이 마지막 글자를 자른다 (실측: ` B y -> `) |
| CRLF | `rstrip("\r")` | 텍스트 모드 universal newline이 처리 (실측: 차이 없음) |

진짜 도구의 "개행 없는 마지막 줄" 버그는 손으로 편집한 fragment에서 실제로 나온다. 커널이 만든 `.config` 는 항상 개행으로 끝나므로 골든 비교에는 영향이 없다.

`-m`(merge) 형식은 "바뀐 것 + 새로 생긴 것"을 다시 config 줄로 쓴다. 없어진 것은 아무것도 출력하지 않는다(`print_config` 에서 `new_value` 가 `None`). 그래서 `-m` 출력은 **fragment로 다시 쓸 수 있는** 형태다.

### B. 세 요청이 각각 어떻게 계산됐나

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">Kconfig 값 계산: depends on = 상한, select = 하한 (n &lt; m &lt; y)</text>
<text x="10" y="50" fill="currentColor">GPIO_SIM=m 요청</text>
<rect x="170" y="35" width="160" height="26" class="f-ok" stroke="currentColor"/>
<text x="180" y="53" fill="currentColor">GPIO_SIM = m (그대로)</text>
<line x1="330" y1="48" x2="380" y2="48" stroke="currentColor"/>
<text x="345" y="40" fill="currentColor" font-size="12">select</text>
<rect x="380" y="35" width="350" height="26" class="f-warn" stroke="currentColor"/>
<text x="390" y="53" fill="currentColor">DEV_SYNC_PROBE ≥ m → m,  IRQ_SIM(bool) ≥ m → y</text>
<text x="10" y="100" fill="currentColor">DEBUG_INFO=n 요청</text>
<rect x="170" y="85" width="160" height="26" class="f-mut" stroke="currentColor"/>
<text x="180" y="103" fill="currentColor">프롬프트 없는 bool</text>
<line x1="330" y1="98" x2="380" y2="98" stroke="currentColor"/>
<rect x="380" y="85" width="350" height="26" class="f-warn" stroke="currentColor"/>
<text x="390" y="103" fill="currentColor">DWARF_TOOLCHAIN_DEFAULT=y 가 select → y (forced)</text>
<text x="10" y="150" fill="currentColor">IKCONFIG=m 요청</text>
<rect x="170" y="135" width="160" height="26" class="f-ok" stroke="currentColor"/>
<text x="180" y="153" fill="currentColor">IKCONFIG = m</text>
<line x1="330" y1="148" x2="380" y2="148" stroke="currentColor"/>
<rect x="380" y="135" width="350" height="26" class="f-ok" stroke="currentColor"/>
<text x="390" y="153" fill="currentColor">IKCONFIG_PROC(bool, depends on IKCONFIG) = y 유지</text>
<text x="10" y="200" fill="currentColor">PL011_CONSOLE=y</text>
<rect x="170" y="185" width="160" height="26" class="f-mut" stroke="currentColor"/>
<text x="180" y="203" fill="currentColor">depends on PL011=y</text>
<line x1="330" y1="198" x2="380" y2="198" stroke="currentColor"/>
<rect x="380" y="185" width="350" height="26" class="f-warn" stroke="currentColor"/>
<text x="390" y="203" fill="currentColor">PL011=m 이므로 보이지 않음 → 줄 자체가 사라짐 (dropped)</text>
<text x="10" y="250" fill="currentColor">I2C_STUB=y</text>
<rect x="170" y="235" width="160" height="26" class="f-mut" stroke="currentColor"/>
<text x="180" y="253" fill="currentColor">depends on m</text>
<line x1="330" y1="248" x2="380" y2="248" stroke="currentColor"/>
<rect x="380" y="235" width="350" height="26" class="f-warn" stroke="currentColor"/>
<text x="390" y="253" fill="currentColor">상한이 m → m (capped)</text>
<text x="10" y="290" fill="currentColor" font-size="12">GPIO_SIMULATOR=m: 어떤 Kconfig에도 없는 이름(오타) → 조용히 무시 (unknown). 전부 실측 결과.</text>
</svg>
```

1. **`GPIO_SIM=m`** — 들어갔다. `drivers/gpio/Kconfig:2009` 의 `select IRQ_SIM / CONFIGFS_FS / DEV_SYNC_PROBE` 때문에 세 심볼의 **하한**이 m으로 올라간다. `DEV_SYNC_PROBE` 는 프롬프트 없는 tristate라 정확히 m이 되고, `IRQ_SIM` 은 bool이라 m이 y로 올림된다. `CONFIGFS_FS` 는 이미 y. diff에 `+DEV_SYNC_PROBE m`, `+IRQ_SIM y` 가 생긴 이유다 (`+` = 전에는 줄 자체가 없던 보이지 않는 심볼).
2. **`DEBUG_INFO`** — `lib/Kconfig.debug:227` 의 `config DEBUG_INFO` 에는 **프롬프트가 없다** (`bool` 한 줄). 사용자가 정할 수 있는 값이 아니고, "Debug information" choice의 `DEBUG_INFO_DWARF_TOOLCHAIN_DEFAULT` 가 `select DEBUG_INFO` 한다. 그래서 n을 써 넣어도 다시 y. 고친 요청은 **choice 쪽**을 바꾼다 (`req_good.config`): `CONFIG_DEBUG_INFO_NONE=y` + `# CONFIG_DEBUG_INFO_DWARF_TOOLCHAIN_DEFAULT is not set`. 결과 diff는 17줄 — `DEBUG_INFO`, `DEBUG_INFO_REDUCED`, `GDB_SCRIPTS` 등 10개가 `-` 로 **사라진다**(의존 대상이 꺼져 보이지 않게 됨).
3. **`IKCONFIG=m`** — 들어갔다. `IKCONFIG_PROC` 는 `depends on IKCONFIG && PROC_FS` 인 bool인데 그대로 y다. bool이 m짜리 의존을 가지면 y까지 허용된다 (`scripts/kconfig/symbol.c` 의 visibility 계산에서 bool의 `mod` 는 `yes` 로 올림). 대신 `/proc/config.gz` 는 `configs.ko` 가 올라와야 생긴다 (§C의 빌드 실측).

`savedefconfig` 로 줄여서 보면 "내가 바꾼 것"만 남는다. 고친 요청(+`LOCALVERSION`)을 적용한 `.config` 와 순수 defconfig를 각각 `savedefconfig` 한 뒤 비교했다:

```text
$ make O=def savedefconfig; make O=sd savedefconfig; diff def/defconfig sd/defconfig
0a1
> CONFIG_LOCALVERSION="-b03"
15c16
< CONFIG_IKCONFIG=y
---
> CONFIG_IKCONFIG=m
664a666
> CONFIG_GPIO_SIM=m
1742,1743d1743
< CONFIG_DEBUG_INFO_DWARF_TOOLCHAIN_DEFAULT=y
< CONFIG_DEBUG_INFO_REDUCED=y
```

`DEBUG_INFO_NONE=y` 줄이 없는 것은 그것이 그 choice의 **첫(기본) 항목**이라 savedefconfig가 생략하기 때문이다. 참고로 트리 안의 `arch/arm64/configs/defconfig` 는 1,824줄이고 그것을 `savedefconfig` 로 되돌리면 1,755줄 — 원본 defconfig 파일은 최소형도, 정렬된 형태도 아니다. 그래서 두 defconfig를 비교할 때는 **둘 다 savedefconfig를 거친 뒤** diff한다.

### B. `check_requests` — 판정 순서가 중요하다

| 순서 | 조건 | reason | 실측 예 |
|---|---|---|---|
| 0 | 실제 == 원함, 또는 원함 n & 줄 없음 | (통과) | `DEBUG_INFO_REDUCED` n 요청 → 줄 없음 = 성공 |
| 1 | `known` 이 있고 이름이 그 안에 없음 | `unknown` | `GPIO_SIMULATOR` (진짜 이름은 `GPIO_SIM`) |
| 2 | 줄 없음 | `dropped` | `SERIAL_AMBA_PL011_CONSOLE` |
| 3 | y 원함, m 나옴 | `capped` | `I2C_STUB` (`depends on m`) |
| 4 | n 원함, y/m 나옴 | `forced` | `DEBUG_INFO` |
| 5 | 그 밖 | `changed` | int `range` 밖 값, choice 충돌 |

1을 2보다 먼저 봐야 한다: 오타도 "줄 없음"이기 때문이다. `known` 없이 부르면 오타가 `dropped` 로 보인다 (self-test가 이것도 확인한다). check.sh는 `known` 을 소스에서 직접 만든다: `find … -name 'Kconfig*' | xargs grep -hoE '^\s*(menu)?config\s+NAME'` → 21,397개.

`merge_config.sh` 도 마지막에 "Value requested for CONFIG_X not in final .config" 를 찍는다. 그런데 그 스크립트는 **조각만이 아니라 base까지 합친 파일의 모든 줄**을 검사하고 줄 텍스트를 그대로 비교한다. 그래서 `req_good` 에서는 우리가 요청하지 않은 `DEBUG_INFO_REDUCED`, `GDB_SCRIPTS` 등 **10개**를 경고했다 (실측). 우리 도구는 조각의 줄만 보고, "없음 = n" 을 성공으로 쳐서 0개다. merge_config와 같은 엄격 모드는 추가 연습 P1이다.

### C. 게스트에서 되찾기 — `/proc/config.gz`, vermagic, y/m

check.sh가 랩 커널을 부팅해서 실행하는 게스트 스크립트(`/test.sh`)는 `uname -r`, `zcat /proc/config.gz`(+ md5), `insmod /gpio-aggregator.ko` 를 한다. 실측(solution):

```text
PASS: guest booted (6.18.55), /proc/config.gz captured intact
PASS: guest /proc/config.gz == /var/lab/out/linux-build/.config (9632 symbols, 0 diff lines)
PASS: vermagic from guest .config == modinfo (6.18.55 SMP preempt mod_unload aarch64)
PASS: guest insmod gpio-aggregator.ko (intree=Y)
PASS: kind gpio-sim = y (CONFIG_GPIO_SIM=y)
PASS: kind gpio_aggregator = m (CONFIG_GPIO_AGGREGATOR=m)
PASS: kind configs = y (CONFIG_IKCONFIG=y)
PASS: kind gpio-mockup = n (CONFIG_GPIO_MOCKUP=n)
```

`/proc/config.gz` 는 `kernel/configs.c` 가 만든다: 빌드 때 `.config` 를 gzip해서 `.rodata` 에 `"IKCFG_ST"` … `"IKCFG_ED"` 사이로 `.incbin` 하고, `IKCONFIG_PROC=y` 면 `proc_create("config.gz", …)`. 같은 표식 덕분에 `scripts/extract-ikconfig` 가 **부팅하지 않고도** Image 파일에서 설정을 꺼낸다 (추가 연습 P5, 11,748줄 일치).

빌드 트리에서 y/m을 아는 법:

| 파일 (`O=` 디렉터리) | 내용 | 실측 (랩 트리) |
|---|---|---|
| `modules.builtin` | `=y` 로 vmlinux에 들어간 "모듈이 될 수 있었던" 드라이버 | 1,347줄, `kernel/drivers/gpio/gpio-sim.ko`, `kernel/kernel/configs.ko` |
| `modules.order` | `=m` 으로 만든 `.ko` 목록 (빌드 순서) | 1,423줄, `drivers/gpio/gpio-aggregator.o` |
| `modules.builtin.modinfo` | 내장 모듈의 modinfo (파라미터 등) | `/sys/module/<name>/parameters` 의 근거 |

`modinfo` (VM 도구) 로 랩 `.ko` 를 본 실제 출력:

```text
$ modinfo /var/lab/out/linux-build/drivers/gpio/gpio-aggregator.ko
filename:       /var/lab/out/linux-build/drivers/gpio/gpio-aggregator.ko
license:        GPL v2
description:    GPIO Aggregator
…
depends:        
intree:         Y
name:           gpio_aggregator
vermagic:       6.18.55 SMP preempt mod_unload aarch64
```

`vermagic` 의 단어는 전부 `.config` 에서 온다: `SMP=y` → `SMP`, `PREEMPT=y` 가 `select PREEMPT_BUILD` → `preempt`, `MODULE_UNLOAD=y` → `mod_unload`, `MODVERSIONS` 는 n이라 없음, arm64 → `aarch64`. 로더는 `kernel/module/main.c` `check_modinfo()` 에서 `same_magic()`(`kernel/module/version.c`)으로 문자열 전체를 `strcmp` 하고, 다르면 `"%s: version magic '%s' should be '%s'"` 를 찍고 `-ENOEXEC`. 단 `MODVERSIONS` 로 CRC가 있으면 **첫 공백까지(release)는 건너뛰고** 비교한다.

⟪BUILD⟫

### C. bring-up 조각의 함정 셋

```text
$ python3 -I solution.py stuck req_bringup.config final_bringup.config known.txt
NOT STUCK GPIO_SIMULATOR wanted=m got=(absent) reason=unknown
NOT STUCK I2C_STUB wanted=y got=m reason=capped
NOT STUCK SERIAL_AMBA_PL011_CONSOLE wanted=y got=(absent) reason=dropped
```

가장 위험한 것은 `SERIAL_AMBA_PL011_CONSOLE` 이다. QEMU virt와 많은 Arm 보드의 콘솔 UART가 PL011인데, `depends on SERIAL_AMBA_PL011=y` 라서 드라이버를 m으로 바꾸는 순간 콘솔 지원이 **경고 없이** 빠진다 (diffconfig에는 `-SERIAL_AMBA_PL011_CONSOLE y` 한 줄뿐). 그 커널은 "Starting kernel ..." 뒤로 아무것도 안 찍는 것처럼 보인다 — bring-up에서 가장 시간을 많이 잡아먹는 종류의 증상이다. 부팅에 필요한 것(콘솔 UART, rootfs가 있는 블록 장치·파일시스템 드라이버)은 y이거나 initramfs 안의 모듈이어야 한다. 이 부팅 실패 자체는 이 레슨에서 돌려 보지 않았다 (설정 판정까지만 실측).

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```bash
python3 -I solution.py; python3 -I -W error -X dev solution.py      # 32/32 passed, exit 0
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B03/check.sh solution
```

실측 `check.sh solution`: `49/49 passed`, `RESULT: PASS (solution)` (self-test 32 + live Kconfig 9 + 게스트 8). starter는 `9/49 passed`, `RESULT: FAIL (starter)`.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 방법 |
|---|---|---|
| `scripts/config -d DEBUG_INFO` 후 확인 없이 빌드 | 프롬프트 없는 심볼, choice가 `select` → olddefconfig가 y로 되돌림 | choice 항목을 바꾸고(`DEBUG_INFO_NONE=y`) 최종 `.config` 를 검사 |
| `.config` 를 손으로 고치고 `make` | 빌드 시작 때 `syncconfig` 가 같은 계산을 하므로 결과는 olddefconfig와 같다 — 하지만 **무엇이 바뀌었는지 모른 채** 빌드한다 | 편집 → `olddefconfig` → `diffconfig` → 빌드 |
| `.config` 전체를 Git에 넣고 리뷰 | 11,752줄, 툴체인 감지 값(`CC_VERSION_TEXT`, `RUSTC_VERSION`)까지 섞여 리뷰 불가 | `savedefconfig` 결과를 `arch/arm64/configs/<board>_defconfig` 로 |
| 두 defconfig 파일을 바로 `diff` | 원본 defconfig는 최소형도 정렬형도 아님 (1,824 vs 1,755줄) | 둘 다 `defconfig`→`savedefconfig` 를 거쳐 비교, 또는 전체 `.config` 를 `diffconfig` |
| 콘솔 UART 드라이버를 `=m` | `PL011_CONSOLE` 이 `depends on …=y` 라 사라짐, 부팅 로그가 안 보임 | 부팅 필수 드라이버는 `y` |
| `#ifdef CONFIG_FOO` 로 기능 분기 | `=m` 이면 `CONFIG_FOO_MODULE` 만 정의된다 | `IS_ENABLED(CONFIG_FOO)` (y 또는 m), `IS_BUILTIN`, `IS_MODULE` |
| 다른 빌드 트리로 만든 `.ko` 를 올림 | vermagic 불일치 → `version magic … should be …`, 같아도 구조체 배치가 다를 수 있음 | 보드 커널과 **같은 빌드 트리**(`make -C <O=트리> M=$PWD`) |
| `LOCALVERSION` 없이 여러 커널 빌드 | 모두 `6.18.55` → `/lib/modules/6.18.55` 가 겹치고 vermagic도 같아 엉뚱한 모듈이 로드됨 | `CONFIG_LOCALVERSION="-b03"` 처럼 빌드마다 이름 |
| 파서에서 `splitlines()` | `\x0c \x85` 등에서도 줄을 자름 | `split("\n")` + `rstrip("\r")` |

## 8. 면접 Q&A

<details><summary>Q1. vmlinux, Image, Image.gz는 무엇이 다른가요? 보드에는 무엇을 올리나요?</summary>

`vmlinux` 는 링커가 만든 **ELF** 다 (랩 트리 162 MB, `with debug_info, not stripped`). 섹션·심볼·디버그 정보가 있어서 gdb, `addr2line`, crash, Oops 디코드에 쓰지만 부트로더가 바로 실행하지는 않는다. `Image` 는 `arch/arm64/boot/Makefile` 의 `OBJCOPYFLAGS_Image := -O binary -R .note -R .note.gnu.build-id -R .comment -S` 로 vmlinux에서 **메모리 이미지만 뽑은 raw 바이너리**다 (42.7 MB, `file`: "Linux kernel ARM64 boot executable Image"). 앞 64바이트에 arm64 boot header(text_offset, image_size, magic `ARM\x64`)가 있어 U-Boot `booti` 가 읽는다. `Image.gz` 는 그 gzip이고 arm64의 기본 `make` 타깃이다 (`KBUILD_IMAGE := $(boot)/Image.gz`, `EFI_ZBOOT` 가 꺼져 있을 때).

**꼬리질문: vmlinux를 왜 보관하나요?** 필드에서 받은 Oops의 주소를 함수·줄로 바꾸려면 **그 빌드의** vmlinux(와 System.map, `.config`)가 있어야 한다. Image에는 심볼이 없다.
</details>

<details><summary>Q2. y와 m은 어떻게 고르나요?</summary>

부팅에 필요한 것 — 콘솔 UART, rootfs가 있는 저장 장치·파일시스템, (initramfs 없이) 첫 마운트 전에 필요한 모든 것 — 은 `y`. 선택적이거나, 보드 변형마다 다르거나, 크거나, 개발 중이라 자주 다시 올릴 드라이버는 `m`. `m` 은 Image가 작아지고 부팅이 빨라지지만, rootfs에 `/lib/modules/<release>/` 와 `depmod` 결과가 있어야 하고 vermagic이 맞아야 한다. 일부 심볼은 `depends on m`(예: `I2C_STUB`)이라 y가 불가능하다.

**꼬리질문: 코드에서 y/m을 구분해야 하면?** `IS_ENABLED(CONFIG_FOO)` 는 y 또는 m, `IS_BUILTIN` 은 y만, `IS_MODULE` 은 m만. m이면 `autoconf.h` 에 `CONFIG_FOO` 가 아니라 `CONFIG_FOO_MODULE` 이 정의되기 때문에 `#ifdef CONFIG_FOO` 는 거짓이 된다.
</details>

<details><summary>Q3. depends on과 select의 차이는? 왜 내 설정이 안 먹었나요?</summary>

`depends on X` 는 **상한**: X가 n이면 심볼이 보이지 않고(최종 `.config` 에서 줄이 사라짐), X가 m이면 tristate는 최대 m. `select Y` 는 **하한**: 이 심볼이 켜지면 Y를 최소한 그 값까지 강제로 올리고, **Y의 depends on을 보지 않는다** (그래서 남용하면 "unmet direct dependencies" 경고). 이 레슨의 실측: `DEBUG_INFO` 는 프롬프트가 없고 choice 항목이 select해서 n을 써도 y로 복귀, `PL011_CONSOLE` 은 depends 미충족으로 사라짐, `I2C_STUB` 은 `depends on m` 으로 m.

**꼬리질문: 그걸 어떻게 빨리 찾나요?** menuconfig에서 `/심볼` 검색 — "Depends on:", "Selected by [y]:", "Selects:" 가 현재 값과 함께 나온다. CI에서는 요청 조각과 최종 `.config` 를 비교하는 이 레슨의 검사(또는 `merge_config.sh` 의 경고)를 돌린다.
</details>

<details><summary>Q4. defconfig, savedefconfig, olddefconfig, oldconfig, menuconfig를 구분해 주세요.</summary>

`make <name>_defconfig` (또는 `defconfig`) — 최소 설정 파일 + Kconfig 기본값으로 전체 `.config` 생성. `savedefconfig` — 반대 방향: 기본값과 같은 줄을 빼서 최소 `defconfig` 를 만든다 (이 레슨에서 11,752줄 → 1,755줄). `olddefconfig` — 기존 `.config` 를 현재 Kconfig로 다시 계산, 새 심볼은 묻지 않고 기본값. `oldconfig` — 같지만 새 심볼마다 **질문**한다(스크립트에서 멈춤). `menuconfig`/`nconfig` — ncurses UI(`libncurses-dev` 필요), 저장하면 `.config` (이전 것은 `.config.old`).

**꼬리질문: BSP 저장소에는 무엇을 커밋하나요?** `savedefconfig` 결과(보드 defconfig)와 기능별 fragment. 전체 `.config` 는 빌드 산출물로 보고, 빌드 번호와 함께 아카이브한다.
</details>

<details><summary>Q5. 보드에서 지금 돌고 있는 커널의 설정을 어떻게 아나요?</summary>

1) `CONFIG_IKCONFIG_PROC=y` 면 `zcat /proc/config.gz` (이 레슨 게스트에서 9,632개 심볼이 빌드 트리와 diff 0줄). 2) `IKCONFIG=m` 이면 `modprobe configs` 후 같은 파일이 생긴다. 3) 커널 파일만 있으면 `scripts/extract-ikconfig Image` (표식 `IKCFG_ST` + gzip 매직을 찾아 푼다). 4) 배포판은 `/boot/config-$(uname -r)`. 5) 다 없으면 빌드 시스템의 아카이브 — 그래서 `.config` 보관이 규칙이다.

**꼬리질문: IKCONFIG를 끄는 이유는?** 크기(이 설정에서 압축본 약 68 KB)와 정보 노출. 끄면 산출물 관리가 더 중요해진다.
</details>

<details><summary>Q6. "version magic ... should be ..." 에러는 왜 나나요? vermagic만 맞으면 안전한가요?</summary>

모듈의 `.modinfo` 에 박힌 `vermagic` 문자열(`UTS_RELEASE` + SMP/preempt/mod_unload/modversions + arch)을 로더가 자기 문자열과 `strcmp` 한다(`same_magic()`). release(LOCALVERSION 포함)나 선점 모델 등이 다르면 `-ENOEXEC`. **충분조건은 아니다**: 같은 문자열이어도 다른 `.config` 로 빌드했으면 구조체 배치가 달라 조용히 메모리를 깰 수 있다. `CONFIG_MODVERSIONS` 는 export 심볼마다 CRC를 붙여 그런 ABI 차이를 잡는다 (그때는 release 부분을 무시하고 비교).

**꼬리질문: out-of-tree 드라이버는 어떻게 빌드해야 하나요?** 보드 커널을 만든 **바로 그 빌드 트리**에 `make -C <O=트리> M=$PWD modules` (ELX-B06). 그 트리의 `.config`, `Module.symvers`, 생성 헤더가 기준이 된다.
</details>

<details><summary>Q7. 설정을 fragment로 관리할 때 조심할 점은?</summary>

Yocto의 `.cfg`, Buildroot의 `BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES`, `merge_config.sh` 는 모두 "base + 조각 → olddefconfig/alldefconfig" 이다. 조각의 값은 **의존성 때문에 조용히 버려질 수 있다** (`-m` 모드나 `cat`+`olddefconfig` 는 아무 말도 안 한다). 그래서 빌드 후 조각의 각 줄이 최종 `.config` 에 있는지 검사하는 단계를 CI에 둔다. 단 `merge_config.sh` 식 줄 비교는 "줄 없음 = n" 을 실패로 보고 base 줄까지 검사해서 이 레슨 `req_good` 에서 거짓 경보 10개를 냈다.

**꼬리질문: 오타 난 심볼은?** Kconfig는 모르는 심볼을 그냥 무시한다. 오타를 잡으려면 소스의 `config NAME` 목록과 대조해야 한다 (`reason=unknown`).
</details>

<details><summary>Q8. dtbs는 커널 설정과 어떤 관계인가요?</summary>

`make dtbs` 는 `arch/arm64/boot/dts/` 아래 DTS를 dtc로 컴파일한다. 어떤 DTB를 만들지는 `CONFIG_ARCH_*`(플랫폼 선택)에 따라 Makefile이 고르고, `CONFIG_OF_EARLY_FLATTREE=y` 면 최상위 Makefile의 `all: dtbs` 때문에 그냥 `make` 에도 포함된다 (랩 트리 1,577개). DTB는 커널 이미지에 들어가지 않고, 부트로더가 메모리에 올려 **x0 레지스터로 주소를 넘긴다** (ELX-B01, ELX-B05).

**꼬리질문: 드라이버를 m으로 바꾸면 DT도 바꿔야 하나요?** 아니다. DT는 하드웨어 기술이고, 모듈이 나중에 로드되면 `compatible` 로 매칭되어 probe된다 (ELX-M01). 모듈 자동 로드는 modalias(`of:N*T*C…`) + udev/modprobe가 한다.
</details>

## 9. 추가 연습

`practice.py` 는 `solution.py` 에 아래 다섯 가지를 더한 것이다. `check.sh practice` 는 self-test 40개 + 공통 검사 + 진짜 도구와의 비교 9개를 돈다.

**P1. merge_config.sh 호환 엄격 모드.** `check_requests(..., strict=True)` 에서는 n을 원했는데 줄이 없는 것도 보고하라 (`reason=absent`). 진짜 `merge_config.sh` 경고 중 조각 심볼만 골라 같은 집합인지 비교하라.

<details><summary>답</summary>

조건 하나를 바꾼다: `want == "n" and got == ABSENT and not strict` 일 때만 통과. 실측 비교: naive 1개(`DEBUG_INFO`), good 0개, bringup 4개(`DEBUG_INFO_REDUCED` 포함) — `merge_config.sh` 와 일치. 주의: `merge_config.sh` 는 `mktemp ./.tmp.config.XXXX` 로 **현재 디렉터리**에 임시 파일을 만들고 `make` 를 현재 디렉터리에서 부른다. 읽기 전용 소스 트리를 지키려고 check.sh는 `O=` 디렉터리(생성된 `Makefile` 이 있다) 안에서 실행한다.
</details>

**P2. "redefined by fragment" 재현.** base와 조각을 받아, 조각이 base와 **다른 값으로** 덮어쓰는 심볼 목록을 내라.

<details><summary>답</summary>

`sorted(k for k in frag if k in base and base[k] != frag[k])`. base에 없는 심볼(`GPIO_SIMULATOR`)과 같은 값(`PL011_CONSOLE=y`)은 나오지 않는다. 실측: naive 3, good 5, bringup 5 — `merge_config.sh` 의 "Value of CONFIG_X is redefined by fragment" 줄과 일치.
</details>

**P3. y/m/n 개수.** `.config` 하나에서 y, m, n(is not set), 그 밖(int/hex/string) 개수를 세라.

<details><summary>답</summary>

arm64 defconfig: `y=3295 m=1273 n=4951 other=117` (합 9,636). check.sh가 `grep -c` 결과와 비교한다. `m=1273` 은 `.ko` 개수와 같지 않다 — 심볼 하나가 `.ko` 여러 개를 만들거나(드라이버 + 헬퍼), 아무 `.ko` 도 안 만드는(메뉴용 tristate) 경우가 있다. 랩 트리의 실제 `.ko` 는 `modules.order` 1,423줄이다.
</details>

**P4. vermagic 불일치 설명.** 모듈 쪽/커널 쪽 vermagic을 받아 다른 점을 사람 말로 내라.

<details><summary>답</summary>

첫 단어(release)를 비교하고, 나머지 단어는 집합 차로 "missing in module / extra in module". 실제 에러 문자열로 테스트한다 (§C 빌드 실측의 `version magic '6.18.55-b03 …' should be '6.18.55 …'`) → `release: module 6.18.55-b03 != kernel 6.18.55`.
</details>

**P5. extract-ikconfig 재구현.** 커널 Image 파일에서 `.config` 를 꺼내라.

<details><summary>답</summary>

`kernel/configs.c` 가 넣은 `IKCFG_ST` 바로 뒤에 gzip 매직 `1f 8b 08` 이 온다. 그 위치부터 `zlib.decompressobj(16 + MAX_WBITS)` 로 풀면 스트림 끝에서 멈춘다 (뒤의 `IKCFG_ED` 는 무시됨). 랩 `/var/lab/out/Image` 에서 꺼낸 11,748줄이 `scripts/extract-ikconfig` 출력과 바이트 단위로 같다. `IKCONFIG=m` 커널의 Image에는 표식이 없고, 대신 `configs.ko` 안에 있다.
</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
python3 -I practice.py                                   # 40/40 passed
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B03/check.sh practice
```

실측: `66/66 passed`, `RESULT: PASS (practice)`.

## 10. 핵심 정리

- 흐름: `make O=… defconfig` → 요청(menuconfig / `scripts/config` / fragment) → **`olddefconfig`** → `diffconfig` 로 확인 → `make Image modules dtbs`.
- `scripts/config` 와 fragment는 텍스트일 뿐, 최종 값은 Kconfig가 다시 계산한다: `depends on` = 상한(안 맞으면 줄이 사라짐), `select` = 하한(depends 무시), 프롬프트 없는 심볼은 못 바꾼다.
- 그래서 빌드 전에 "요청 → 최종" 을 검사한다. 이번 실측: `DEBUG_INFO` forced, `PL011_CONSOLE` dropped, `I2C_STUB` capped, `GPIO_SIMULATOR` unknown.
- `vmlinux` = ELF(디버그·심볼, 보관용), `Image` = objcopy raw(부트로더가 적재), `.ko` = `=m`(`modules.order`), `=y` 는 `modules.builtin`, `dtb` = 보드 기술(x0로 전달).
- 리뷰·커밋은 `savedefconfig` 결과로. 전체 `.config` 끼리는 `diffconfig`, defconfig끼리는 둘 다 savedefconfig 후 diff.
- 보드의 설정은 `/proc/config.gz`(IKCONFIG_PROC), `extract-ikconfig`, 또는 보관한 `.config` 로 되찾는다.
- 모듈 로드 조건 1번은 vermagic(release + SMP/preempt/mod_unload/modversions + arch), 진짜 조건은 "같은 빌드 트리". 빌드마다 `LOCALVERSION`.

## 참고자료

- Linux 6.18.55 소스 (`/var/lab/src/linux-6.18.55`, 실제로 읽음): `scripts/diffconfig`, `scripts/kconfig/merge_config.sh`, `scripts/extract-ikconfig`, `kernel/configs.c`, `kernel/Makefile`, `include/linux/vermagic.h`, `kernel/module/main.c` (`check_modinfo`), `kernel/module/version.c` (`same_magic`), `init/Kconfig` (IKCONFIG), `lib/Kconfig.debug` (DEBUG_INFO choice), `drivers/gpio/Kconfig` (GPIO_SIM), `drivers/i2c/Kconfig` (I2C_STUB), `drivers/tty/serial/Kconfig` (PL011_CONSOLE), `arch/arm64/Makefile`, `arch/arm64/boot/Makefile`, 최상위 `Makefile` (`all:` 타깃)
- [Documentation/kbuild/kconfig-language.rst](https://docs.kernel.org/kbuild/kconfig-language.html) — depends on / select / imply, 프롬프트 없는 심볼
- [Documentation/kbuild/kconfig.rst](https://docs.kernel.org/kbuild/kconfig.html) — olddefconfig, savedefconfig, KCONFIG_ALLCONFIG
- [Documentation/kbuild/modules.rst](https://docs.kernel.org/kbuild/modules.html) — 외부 모듈 빌드, `modules.order`
- [Documentation/arch/arm64/booting.rst](https://docs.kernel.org/arch/arm64/booting.html) — Image 헤더, x0 = DTB 주소
- 배경 노트: EmbeddedLinux `E_kernel_basics/2026-10-10_E2_kernel_config_build.md` (Kconfig 의미론, fixdep, 산출물 지도), `E3` (모듈)
- 교차 참조: ELX-B01 (부트 플로우), ELX-B05 (Device Tree), ELX-B06 (모듈 빌드·modprobe), ELX-B10 (gpio-sim 사용), ELX-M01 (DT 매칭), ELX-M13/M14 (Buildroot/Yocto의 fragment)
