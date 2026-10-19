# P6. 테스트와 CI — KUnit · kselftest · LAVA · HIL · 생산 테스트, 그리고 "부팅해서 TAP을 읽는" CI 잡

> **이 노트를 다 읽으면**: 임베디드 Linux 테스트 피라미드(KUnit → kselftest/LTP → 부트 테스트 → HIL → 생산 테스트)를 층마다 "무엇을 잡고 얼마나 걸리나"로 설명할 수 있다 · KUnit 테스트 스위트(`KUNIT_CASE`, `EXPECT`/`ASSERT`, 파라미터 테스트, init/exit, 자동 해제 자원)를 직접 짜서 모듈로 돌리고 KTAP 실패 리포트를 읽을 수 있다 · kselftest를 크로스/네이티브로 빌드해 `run_kselftest.sh`로 돌리고 TAP 결과를 파싱할 수 있다 · QEMU를 부팅해 테스트를 돌리고 pass/fail exit code를 돌려주는 CI 잡을 짤 수 있고, 같은 구조를 LAVA·HIL·공장 스테이션으로 확장하는 설계를 말할 수 있다
> **왜 필요한가**: "드라이버를 어떻게 테스트하나?", "보드 없이 CI는 어떻게 돌리나?", "flaky 테스트는 어떻게 다루나?", "공장 테스트는 무엇을 재고 얼마 안에 끝나야 하나?"는 시니어 임베디드 면접의 품질·프로세스 질문 단골이고, 입사 후에는 "내 패치가 다른 보드를 깨뜨리지 않았다"를 증명하는 매일의 도구다
> **Don 기준 난이도**: SSD 양산 펌웨어의 회귀 테스트·MP(양산) 테스트·불량 분석 루프, scope·JTAG 기반 bring-up은 이미 몸에 있다 / 새로 배울 부분은 커널 안에서 도는 단위 테스트(KUnit)의 구조, KTAP/TAP 형식, kselftest 빌드·설치 관례, QEMU를 자동 운전하는 법, LAVA라는 공용 보드 팜 어휘
> **선행 노트**: E2(커널 config·빌드), E3(모듈·out-of-tree 빌드), G8(debugfs), H2(regmap — `regmap-kunit` 스위트), H4(gpio-mockup·gpio-sim), K3·K4(이미지 빌드), L4(OTA·bootcount), M1(printk 레벨), M3(Oops 해독), M8 §3(KASAN KUnit 스위트) — P1(bring-up 순서)·P5(업그레이드 회귀)와 이어진다

---

## 0. 큰 그림 — 이게 왜 필요한가

Don이 SSD 펌웨어 팀에서 겪은 품질 루프를 떠올려 보자. 개발자는 FTL의 매핑 함수 같은 것을 호스트에서 단위 테스트로 돌리고, 펌웨어 빌드는 랙에 꽂힌 드라이브 수십 대에 자동으로 올라가 회귀 스크립트(전원 차단, 장시간 I/O, 에러 주입)를 돌고, 출시 전에는 온도 챔버·전원 사이클 랙에서 며칠씩 돌고, 공장에서는 드라이브 한 대마다 몇 분짜리 MP 테스트로 "양품/불량"을 가르고 시리얼 번호에 결과를 붙였다. 층마다 **잡는 버그가 다르고, 걸리는 시간과 비용이 다르다**.

임베디드 Linux도 똑같은 피라미드를 가진다. 다만 각 층에 이름이 붙은 표준 도구가 있다.

```svg
<svg viewBox="0 0 660 370" xmlns="http://www.w3.org/2000/svg">
  <text x="20" y="24" font-size="13">위로 갈수록 느리고 비싸며 실제에 가깝다 · 아래일수록 빠르고 많다</text>
  <text x="505" y="44" font-size="12">언제 도나</text>
  <polygon points="195,50 305,50 339,106 161,106" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="250" y="83" font-size="13" text-anchor="middle">생산(공장) 테스트</text>
  <text x="505" y="83" font-size="12">양산 유닛마다</text>
  <line x1="328" y1="78" x2="498" y2="78" stroke="currentColor" stroke-dasharray="3 3" stroke-opacity="0.5"/>
  <polygon points="161,106 339,106 373,162 127,162" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="250" y="130" font-size="13" text-anchor="middle">HIL 리그</text>
  <text x="250" y="147" font-size="12" text-anchor="middle">실 센서 · 전원 · 버스</text>
  <text x="505" y="139" font-size="12">야간 · 릴리스 후보</text>
  <line x1="362" y1="134" x2="498" y2="134" stroke="currentColor" stroke-dasharray="3 3" stroke-opacity="0.5"/>
  <polygon points="127,162 373,162 407,218 93,218" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="250" y="186" font-size="13" text-anchor="middle">부트 테스트</text>
  <text x="250" y="203" font-size="12" text-anchor="middle">QEMU · 보드 팜(LAVA)</text>
  <text x="505" y="195" font-size="12">머지마다</text>
  <line x1="396" y1="190" x2="498" y2="190" stroke="currentColor" stroke-dasharray="3 3" stroke-opacity="0.5"/>
  <polygon points="93,218 407,218 441,274 59,274" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="250" y="242" font-size="13" text-anchor="middle">kselftest · LTP</text>
  <text x="250" y="259" font-size="12" text-anchor="middle">드라이버 uAPI 테스트</text>
  <text x="505" y="251" font-size="12">커밋마다(QEMU)</text>
  <line x1="430" y1="246" x2="498" y2="246" stroke="currentColor" stroke-dasharray="3 3" stroke-opacity="0.5"/>
  <polygon points="59,274 441,274 475,330 25,330" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="250" y="298" font-size="13" text-anchor="middle">KUnit</text>
  <text x="250" y="315" font-size="12" text-anchor="middle">커널 안 단위 테스트</text>
  <text x="505" y="307" font-size="12">커밋 전 · 수 초</text>
  <line x1="464" y1="302" x2="498" y2="302" stroke="currentColor" stroke-dasharray="3 3" stroke-opacity="0.5"/>
  <text x="250" y="356" font-size="12" text-anchor="middle">넓이 = 테스트 개수 (아래가 가장 많다)</text>
</svg>
```
그림 1 — 임베디드 Linux 테스트 피라미드. 아래층은 HW 없이 수 초 안에 수백 개를 돌리고, 위층은 실제 보드·센서·지그가 필요하지만 "진짜 제품"에 가장 가깝다.

| 층 | 대표 도구 | 무엇을 잡나 | HW 필요 | 이 노트에서 실제로 돌린 것 |
|---|---|---|---|---|
| 단위 (커널 안) | KUnit | 순수 로직 버그: CRC, 링 버퍼 인덱스, 파서, regmap 캐시, 리스트 조작 | 없음 (UML/QEMU) | §2 — in-tree 4개 모듈 + 직접 짠 스위트 |
| 커널 uAPI | kselftest, LTP | syscall·ioctl·sysfs 동작, 서브시스템 회귀 (timers, gpio, sync …) | 대부분 없음 | §3 — 7개 kselftest를 게스트에서 |
| 드라이버 레벨 | 가짜 장치(gpio-mockup, i2c-stub, vivid, vcan) + 유저 테스트 | probe·uAPI·에러 경로 | 없음 → 있음 | §3.5 gpio-mockup.sh |
| 부트 | QEMU 부팅 스크립트, LAVA, KernelCI | "부팅이 끝까지 되나", 콘솔 로그의 회귀, 모듈 로드 | QEMU 또는 보드 팜 | §4.3 — pexpect CI 잡 |
| HIL | 전원 릴레이·시리얼·GPIO 자극·scope 자동화 | 타이밍, 전원 시퀀스, 실 센서 데이터, 장시간 안정성 | 실 보드 + 지그 | §6 가이드 |
| 생산 | 테스트 이미지 + 스테이션 SW | 조립·부품 불량, 보정 값, 추적성 | 지그·픽스처 | §7 — 한계 비교 스크립트 |

이 노트의 실행 환경은 두 가지다. (1) Mac 위 Lima VM `elinux`(aarch64, `uname -r` = `7.0.0-38-generic`) — 빌드, kunit.py, 유저 공간 테스트 일부. (2) 그 VM 안의 QEMU `virt` 게스트 — 랩 커널 `Image-dom`(`6.18.55-dom`, KUnit + debugfs + 테스트 모듈 `=m`)과 Ubuntu 24.04 rootfs `rootfs-dom`(qcow2 overlay로 부팅). **QEMU는 KVM 없이 TCG로 돌고, 같은 VM을 19명이 동시에 쓰는 중이라(load average 16~24) 아래 모든 시간 숫자는 실제 HW 수치가 아니다.** 상대 비교로만 읽자.

---

## 1. 테스트를 층으로 나누는 이유 — "어디서 잡으면 제일 싼가"

### 1.1 버그를 잡는 비용

Don의 SSD 경험으로 말하면, FTL 매핑 테이블 인덱스의 off-by-one은 호스트 단위 테스트에서 잡으면 1분, 랙 회귀에서 잡으면 하루(재현 → 덤프 → 분석), 필드에서 잡으면 RMA와 고객 에스컬레이션이다. 피라미드의 원칙은 단순하다: **잡을 수 있는 가장 낮은 층에서 잡는다**. 링 버퍼의 경계 조건을 HIL 리그로 검증하는 것은 낭비고, 반대로 "전원 레일이 늦게 올라오면 센서 probe가 실패한다"는 KUnit으로는 절대 못 잡는다.

### 1.2 임베디드 Linux에서 각 층의 한계

- **KUnit**: 커널 안에서 돌지만 HW는 없다. 레지스터 대신 regmap RAM 백엔드나 static stub으로 바꿔 끼운다. 순수 함수·상태 머신·파서에 최적.
- **kselftest/LTP**: 유저 공간에서 커널 인터페이스를 두드린다. 커널 설정(config)에 따라 결과가 달라지고(`# SKIP` 다수), 타이밍 테스트는 부하에 민감하다(§4.3에서 실제로 실패한다).
- **부트 테스트**: "커널이 이 보드에서 셸까지 가나"를 매 머지마다 확인. QEMU는 SoC 고유 HW를 모르므로 실제 보드 팜(LAVA)으로 보완한다.
- **HIL**: 진짜 센서·진짜 버스·진짜 전원. 비싸고 느리고 공유 자원이라 스케줄링이 필요하다.
- **생산 테스트**: 목적이 다르다. 설계 검증이 아니라 "**이 한 대**가 조립이 제대로 됐나"를 정해진 시간(takt time) 안에 판정하고, 결과를 시리얼 번호에 묶어 남긴다.

### 1.3 공통 언어 — TAP과 KTAP

층마다 도구는 다르지만 결과 형식은 대부분 **TAP(Test Anything Protocol)** 계열로 모인다. 그래서 CI는 "어느 도구든 TAP을 읽는 파서 하나"로 결과를 합칠 수 있다.

```text
TAP version 13          <- kselftest (유저 공간)
1..2                    <- 계획: 테스트 2개
ok 1 test_a
not ok 2 test_b # exit=1

KTAP version 1          <- KUnit (커널 안). 들여쓰기 4칸 = 하위 테스트
1..1
    KTAP version 1
    # Subtest: suite
    1..2
    ok 1 case_a
    not ok 2 case_b
not ok 1 suite
```

KTAP은 커널이 TAP 13을 다듬은 버전이다(`Documentation/dev-tools/ktap.rst`). 차이는 "하위 테스트를 들여쓰기로 중첩한다", "`# SKIP` 지시어", "`#`로 시작하는 줄은 진단(diagnostic)"이라는 규칙을 명확히 한 것이다.

---

## 2. KUnit — 커널 안에서 도는 단위 테스트

### 2.1 직관과 정의

KUnit은 "커널 코드를 커널 안에서 테스트하는 xUnit 프레임워크"다(`CONFIG_KUNIT`, 소스 `lib/kunit/`). bare-metal로 비유하면, 펌웨어 이미지 안에 테스트 함수 테이블을 같이 링크해 두고, 부팅 직후(또는 명령을 받으면) 테이블을 순회하며 각 함수를 실행하고 결과를 UART로 찍는 **내장 자가 시험(BIST) 프레임워크**다. 다른 점은 (1) 각 테스트 케이스를 **별도 kthread**에서 돌려서 한 케이스가 Oops를 내도 나머지는 계속 돈다는 것, (2) 결과가 표준 형식(KTAP)이라 기계가 읽는다는 것이다.

용어 정리:

| 용어 | 뜻 |
|---|---|
| test case | `void f(struct kunit *test)` 함수 하나 |
| suite | 케이스 배열 + `init`/`exit`(케이스마다) + `suite_init`/`suite_exit`(한 번) |
| `KUNIT_EXPECT_*` | 실패를 기록하고 **계속 진행** |
| `KUNIT_ASSERT_*` | 실패를 기록하고 **그 케이스를 즉시 중단** (뒤 코드가 의미 없을 때: NULL 포인터 등) |
| 파라미터 테스트 | 같은 케이스를 입력 배열의 원소마다 반복 (`KUNIT_CASE_PARAM`) |
| managed resource | `kunit_kzalloc()`, `kunit_add_action()` — 케이스가 끝나면 자동 해제 (`devm_*`의 테스트판) |
| KTAP | 결과 출력 형식. dmesg와 debugfs에 동시에 남는다 |

### 2.2 예제 1 — 랩 커널의 KUnit 설정 확인

무엇을 확인하나: `Image-dom`이 KUnit 코어를 built-in으로, 테스트들을 모듈로 갖고 있는지. 모듈로 둔 테스트는 **insmod하는 순간 실행**된다.

```sh
grep -E "^CONFIG_(KUNIT|KUNIT_DEBUGFS|KUNIT_TEST|KUNIT_EXAMPLE_TEST|LIST_KUNIT_TEST|REGMAP_KUNIT|KUNIT_DEFAULT_ENABLED|KUNIT_DEFAULT_TIMEOUT|KUNIT_AUTORUN_ENABLED|KUNIT_ALL_TESTS|IIO_FORMAT_KUNIT_TEST|TEST_BITMAP|GPIO_SIM|GPIO_MOCKUP)=" \
    /var/lab/out/linux-build-dom/.config
```

```text
CONFIG_REGMAP_KUNIT=m
CONFIG_GPIO_MOCKUP=m
CONFIG_GPIO_SIM=y
CONFIG_IIO_FORMAT_KUNIT_TEST=m
CONFIG_KUNIT=y
CONFIG_KUNIT_DEBUGFS=y
CONFIG_KUNIT_TEST=m
CONFIG_KUNIT_EXAMPLE_TEST=m
CONFIG_KUNIT_DEFAULT_ENABLED=y
CONFIG_KUNIT_DEFAULT_TIMEOUT=300
CONFIG_TEST_BITMAP=m
CONFIG_LIST_KUNIT_TEST=m
```

출력에서 볼 것: `KUNIT=y`(코어와 executor는 커널 본체), `KUNIT_DEBUGFS=y`(결과를 `/sys/kernel/debug/kunit/<suite>/results`로 다시 읽을 수 있다), 테스트는 전부 `=m`. `KUNIT_ALL_TESTS`는 꺼져 있다 — 켜면 의존성이 맞는 모든 `*_KUNIT_TEST`가 자동으로 켜진다. 케이스당 기본 타임아웃은 300초다(`kunit.timeout=` 모듈 파라미터로 변경, `lib/kunit/test.c`).

### 2.3 예제 2 — in-tree KUnit 모듈 4개를 Image-dom에서 돌리기

무엇을 확인하나: 업스트림이 들고 있는 KUnit 스위트를 그대로 insmod했을 때 KTAP 결과가 어떻게 찍히고 얼마나 걸리는지. 빠른 반복을 위해 systemd rootfs 대신 랩의 BusyBox initramfs에 `.ko`만 넣어 부팅했다(E3의 레시피와 같다).

```sh
# VM: initramfs 복사본에 테스트 모듈 넣기
cd /var/tmp/P6; M=/var/lab/out/modules-dom/lib/modules/6.18.55-dom/kernel
rm -rf r && mkdir r && cp -a /var/lab/build/rootfs/. r/ && mkdir -p r/ko
cp $M/lib/kunit/kunit-test.ko $M/lib/kunit/kunit-example-test.ko $M/lib/tests/list-test.ko \
   $M/drivers/base/regmap/regmap-ram.ko $M/drivers/base/regmap/regmap-raw-ram.ko \
   $M/drivers/base/regmap/regmap-kunit.ko kmod/p6_kunit_test.ko r/ko/
cat > r/t1.sh <<"EOF"
#!/bin/sh
cd /ko
for m in kunit-test kunit-example-test list-test regmap-ram regmap-raw-ram regmap-kunit; do
  t0=$(cut -d" " -f1 /proc/uptime); insmod $m.ko; t1=$(cut -d" " -f1 /proc/uptime)
  echo "P6TIME $m $t0 $t1"
done
ls /sys/kernel/debug/kunit/
cat /proc/sys/kernel/tainted
EOF
chmod +x r/t1.sh; (cd r && find . | cpio -o -H newc --quiet | gzip -1) > i1.gz
time timeout 900 qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 1536M -nographic \
  -kernel /var/lab/out/Image-dom -initrd i1.gz \
  -append "console=ttyAMA0 log_buf_len=8M LAB_CMD=/t1.sh" > ex1.log 2>&1
grep -E "P6TIME|^\[.*\] (ok|not ok) [0-9]+ |# Totals" ex1.log
```

```text
real	0m39.577s
[   19.424107][   T79] # Totals: pass:2 fail:0 skip:0 total:2
[   19.424213][   T79] ok 1 kunit-try-catch-test
[   19.668120][   T79] # Totals: pass:12 fail:0 skip:0 total:12
[   19.668153][   T79] ok 2 kunit-resource-test
[   19.726358][   T79] # Totals: pass:0 fail:0 skip:2 total:2
[   19.726398][   T79] ok 3 kunit-log-test # SKIP 
[   19.764025][   T79] # Totals: pass:2 fail:0 skip:0 total:2
[   19.764076][   T79] ok 4 kunit_status
[   19.803946][   T79] # Totals: pass:2 fail:0 skip:0 total:2
[   19.803977][   T79] ok 5 kunit_current
[   19.877131][   T79] # Totals: pass:3 fail:0 skip:0 total:3
[   19.877151][   T79] ok 6 kunit_device
[   19.939891][   T79] ok 7 kunit_fault
[   19.965239][   T79] ok 8 kunit_stub
P6TIME kunit-test 16.77 17.68
[   20.378538][  T135] ok 1 example_init
[   20.944622][  T135] # Totals: pass:18 fail:0 skip:4 total:22
[   20.944641][  T135] ok 2 example
P6TIME kunit-example-test 17.81 18.81
[   22.168114][  T184] # Totals: pass:39 fail:0 skip:0 total:39
[   22.168154][  T184] ok 1 list-kunit-test
[   22.916482][  T184] # Totals: pass:18 fail:0 skip:0 total:18
[   22.916518][  T184] ok 2 hlist
[   23.203963][  T184] # Totals: pass:8 fail:0 skip:0 total:8
[   23.203988][  T184] ok 3 klist
P6TIME list-test 18.91 20.96
P6TIME regmap-ram 21.05 21.28
P6TIME regmap-raw-ram 21.38 21.57
[   36.909797][  T323] # Totals: pass:408 fail:0 skip:0 total:408
[   36.909849][  T323] ok 1 regmap
P6TIME regmap-kunit 21.68 34.53
```

출력에서 볼 것:
- 부팅부터 poweroff까지 40초 안에 스위트 13개가 돈다. `regmap` 스위트 하나가 **408 케이스**(캐시 종류 flat/rbtree/maple × 엔디언 × 시나리오의 파라미터 조합)이고 TCG에서 약 13초. H2에서 본 regmap 캐시 동작이 실제로 이렇게 검증된다.
- `kunit-log-test # SKIP`: 스위트 전체가 건너뛰어도 `ok`다. TAP에서 SKIP은 실패가 아니다.
- `P6TIME`(uptime)과 printk 타임스탬프가 2초쯤 어긋나는 것은 콘솔 출력이 밀려서다(TCG + 부하). 시간 측정에는 한쪽 시계만 쓰자.

`kunit_fault` 스위트는 일부러 NULL 역참조를 하는 테스트다. 그 순간 로그는 이렇다.

```text
[   19.898570][  T129] Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
[   19.910820][  T129] Internal error: Oops: 0000000096000046 [#1]  SMP
[   19.915653][  T129] CPU: 1 UID: 0 PID: 129 Comm: kunit_try_catch Tainted: G                 N  6.18.55-dom #1 PREEMPT 
[   19.917683][  T129] Tainted: [N]=TEST
[   19.939891][   T79] ok 7 kunit_fault
```

출력에서 볼 것: Oops가 난 태스크 이름이 `kunit_try_catch`다 — 케이스가 별도 kthread에서 돌기 때문에 Oops가 나도 그 kthread만 죽고, 바깥의 실행기(T79)는 "예상한 대로 죽었다"고 판정해 `ok 7`을 찍는다(§9). 그리고 커널 taint에 `N`(TEST)이 붙는다. 마지막에 읽은 `/proc/sys/kernel/tainted` 값은 `262272` = `0x40080` = bit 18(`N`, KUnit 테스트 실행) + bit 7(`D`, Oops 발생)이었다. **KUnit을 돌린 커널은 taint된다** — 양산 이미지에 KUnit 모듈을 넣지 않는 이유 중 하나이고, 버그 리포트에서 `N`이 보이면 "테스트 커널"이라는 신호다.

### 2.4 예제 3 — 직접 짠 KUnit 스위트 (CRC-8 + 링 버퍼, 실패 하나 포함)

Don이 펌웨어에서 수없이 짜 본 두 가지 — SMBus PEC용 CRC-8과 2의 거듭제곱 크기 링 버퍼 — 를 테스트 대상으로 쓴다. 링 버퍼의 `space()`에는 **일부러 off-by-one 버그를 심었다**("한 칸 비움" 방식인데 `-1`을 빼먹음). 펌웨어에서 실제로 흔한 버그다.

테스트 대상 헤더(순수 함수만):

```c
/* p6_ring.h - 테스트 대상: 순수 함수 (CRC-8/SMBus PEC, 2^n 링 버퍼) */
#ifndef P6_RING_H
#define P6_RING_H
#include <linux/types.h>
#include <linux/errno.h>

static inline u8 p6_crc8(const u8 *p, size_t n)	/* poly 0x07, init 0 */
{
	u8 crc = 0;

	while (n--) {
		crc ^= *p++;
		for (int i = 0; i < 8; i++)
			crc = (crc & 0x80) ? (u8)((crc << 1) ^ 0x07) : (u8)(crc << 1);
	}
	return crc;
}

/* size는 2의 거듭제곱, "한 칸 비움" 방식 -> 최대 size-1개 저장 */
struct p6_ring {
	u32 head, tail, size;
	u8 *buf;
};

static inline u32 p6_ring_count(const struct p6_ring *r)
{
	return (r->head - r->tail) & (r->size - 1);
}

static inline u32 p6_ring_space(const struct p6_ring *r)
{
	return r->size - p6_ring_count(r);	/* 일부러 심은 버그: -1 빠짐 */
}

static inline int p6_ring_put(struct p6_ring *r, u8 v)
{
	u32 next = (r->head + 1) & (r->size - 1);

	if (next == r->tail)
		return -ENOSPC;
	r->buf[r->head] = v;
	r->head = next;
	return 0;
}

static inline int p6_ring_get(struct p6_ring *r, u8 *v)
{
	if (r->head == r->tail)
		return -EAGAIN;
	*v = r->buf[r->tail];
	r->tail = (r->tail + 1) & (r->size - 1);
	return 0;
}
#endif
```

테스트 스위트 앞부분 — 파라미터 테스트와 케이스별 `init`:

```c
// SPDX-License-Identifier: GPL-2.0
/* p6_kunit_test.c - p6_ring.h 의 KUnit 테스트 스위트 */
#include <kunit/test.h>
#include "p6_ring.h"

struct crc_case { const char *name; const u8 *data; size_t len; u8 want; };
static const struct crc_case crc_cases[] = {
	{ "empty",     (const u8 *)"",          0, 0x00 },
	{ "check_str", (const u8 *)"123456789", 9, 0xF4 },	/* CRC-8/SMBUS check */
	{ "one_ff",    (const u8 *)"\xff",      1, 0xF3 },
	{ "bytes_123", (const u8 *)"\x01\x02\x03", 3, 0x48 },
};
KUNIT_ARRAY_PARAM_DESC(crc, crc_cases, name);	/* -> crc_gen_params() */

static void p6_crc8_test(struct kunit *test)
{
	const struct crc_case *c = test->param_value;

	KUNIT_EXPECT_EQ(test, p6_crc8(c->data, c->len), c->want);
}

static int p6_ring_init(struct kunit *test)	/* 케이스마다 새 링 */
{
	struct p6_ring *r = kunit_kzalloc(test, sizeof(*r), GFP_KERNEL);

	KUNIT_ASSERT_NOT_NULL(test, r);
	r->size = 8;
	r->buf = kunit_kzalloc(test, r->size, GFP_KERNEL);	/* 끝나면 자동 해제 */
	KUNIT_ASSERT_NOT_NULL(test, r->buf);
	test->priv = r;
	return 0;
}
```

뒷부분 — 링 버퍼 케이스 2개와 suite 등록:

```c
static void p6_ring_fifo_wrap_test(struct kunit *test)
{
	struct p6_ring *r = test->priv;
	u8 v;

	for (int i = 0; i < 20; i++) {		/* 8칸 링에서 20번 -> wrap 여러 번 */
		KUNIT_ASSERT_EQ(test, p6_ring_put(r, i), 0);
		KUNIT_ASSERT_EQ(test, p6_ring_get(r, &v), 0);
		KUNIT_EXPECT_EQ_MSG(test, v, (u8)i, "order broken at i=%d", i);
	}
	KUNIT_EXPECT_EQ(test, p6_ring_get(r, &v), -EAGAIN);
}

static void p6_ring_full_test(struct kunit *test)
{
	struct p6_ring *r = test->priv;
	int n = 0;

	while (p6_ring_put(r, 0xAA) == 0)
		n++;
	KUNIT_EXPECT_EQ(test, n, 7);			/* size-1 */
	KUNIT_EXPECT_EQ(test, p6_ring_count(r), 7U);
	KUNIT_EXPECT_EQ(test, p6_ring_space(r), 0U);	/* 버그가 여기서 잡힌다 */
}

static struct kunit_case p6_cases[] = {
	KUNIT_CASE_PARAM(p6_crc8_test, crc_gen_params),
	KUNIT_CASE(p6_ring_fifo_wrap_test),
	KUNIT_CASE(p6_ring_full_test),
	{}
};

static struct kunit_suite p6_suite = {
	.name = "p6_ring", .init = p6_ring_init, .test_cases = p6_cases,
};
kunit_test_suite(p6_suite);
MODULE_DESCRIPTION("P6 KUnit demo: crc8 + ring buffer");
MODULE_LICENSE("GPL");
```

읽는 법:
- `KUNIT_ARRAY_PARAM_DESC(crc, crc_cases, name)`는 `crc_gen_params()`라는 생성기 함수를 만들어 준다. 배열 원소 하나가 파라미터 하나이고, 구조체의 `name` 필드가 결과 줄의 이름이 된다. 기대값(0xF4, 0xF3, 0x48)은 Python으로 같은 알고리즘을 계산해 미리 확인했다 — "123456789" → 0xF4는 CRC-8/SMBUS의 표준 check 값이다.
- `.init`은 **케이스마다** 불린다. `kunit_kzalloc()`으로 잡은 메모리는 케이스가 끝나면(성공이든 ASSERT 중단이든) 자동으로 해제된다. 그래서 테스트 코드에 `kfree`가 없다.
- 루프 안의 `put/get`은 `ASSERT`다. 넣기가 실패하면 그 뒤의 비교는 의미가 없으니 케이스를 끊는다. 순서 비교는 `EXPECT_..._MSG` — 실패해도 나머지 반복을 계속 보고 싶다.

빌드(VM, `linux-build-dom` 트리 대상)와 실행 스크립트:

```sh
printf 'obj-m := p6_kunit_test.o\n' > Kbuild
make -C /var/lab/out/linux-build-dom M=$PWD modules 2>&1 | tail -3
modinfo p6_kunit_test.ko | grep -E "vermagic|desc"
cat > r/t2.sh <<"EOF"
#!/bin/sh
cd /ko
insmod p6_kunit_test.ko; echo "P6 insmod rc=$?"
echo "=== results file"; cat /sys/kernel/debug/kunit/p6_ring/results
echo "=== rerun via debugfs"; echo go > /sys/kernel/debug/kunit/p6_ring/run
EOF
```

```text
  CC [M]  .module-common.o
  LD [M]  p6_kunit_test.ko
make: Leaving directory '/var/lab/build/linux-dom'
description:    P6 KUnit demo: crc8 + ring buffer
vermagic:       6.18.55-dom SMP preempt mod_unload aarch64
```

게스트에서 insmod한 순간의 dmesg(콘솔):

```text
[   10.941036][   T76] p6_kunit_test: loading out-of-tree module taints kernel.
[   10.951686][   T76] KTAP version 1
[   10.951729][   T76] 1..1
[   10.957137][   T76]     KTAP version 1
[   10.957177][   T76]     # Subtest: p6_ring
[   10.957339][   T76]     # module: p6_kunit_test
[   10.957684][   T76]     1..3
[   10.957856][   T76]         KTAP version 1
[   10.957886][   T76]         # Subtest: p6_crc8_test
[   10.965961][   T76]         ok 1 empty
[   10.970171][   T76]         ok 2 check_str
[   10.982188][   T76]         ok 3 one_ff
[   10.993692][   T76]         ok 4 bytes_123
[   10.994010][   T76]     # p6_crc8_test: pass:4 fail:0 skip:0 total:4
[   10.994127][   T76]     ok 1 p6_crc8_test
[   11.002746][   T76]     ok 2 p6_ring_fifo_wrap_test
[   11.006644][   T87]     # p6_ring_full_test: EXPECTATION FAILED at p6_kunit_test.c:56
[   11.006644][   T87]     Expected p6_ring_space(r) == 0U, but
[   11.006644][   T87]         p6_ring_space(r) == 1 (0x1)
[   11.006644][   T87]         0U == 0 (0x0)
[   11.014841][   T76]     not ok 3 p6_ring_full_test
[   11.014968][   T76] # p6_ring: pass:2 fail:1 skip:0 total:3
[   11.015038][   T76] # Totals: pass:5 fail:1 skip:0 total:6
[   11.015144][   T76] not ok 1 p6_ring
P6 insmod rc=0
```

출력에서 볼 것:
- 3단 중첩: 최상위(모듈의 스위트 묶음) → `p6_ring` 스위트(들여쓰기 4) → 파라미터 케이스(들여쓰기 8). 파라미터 하나하나가 이름을 가진 하위 테스트다.
- 실패 리포트는 **파일:줄, 식의 원문, 양쪽 값(10진·16진)** 을 준다. "공간이 0이어야 하는데 1이다" → 꽉 찬 링에서 `space()`가 한 칸 더 있다고 거짓말한다 → 생산자가 그 말을 믿고 쓰면 덮어쓰기. 펌웨어였다면 몇 시간짜리 데이터 깨짐 디버깅이 이 한 줄로 끝난다.
- 실패한 줄의 스레드 ID(T87)가 실행기(T76)와 다르다 — 케이스마다 새 kthread(§9).
- `insmod rc=0`: 테스트가 실패해도 **모듈 로드는 성공**이다. CI는 exit code가 아니라 KTAP을 읽어야 한다(§11 함정).

### 2.5 예제 4 — debugfs로 결과 다시 읽기, 다시 돌리기

같은 스크립트의 뒷부분 출력이다. `results` 파일은 dmesg와 별개로 suite별 KTAP 문서를 보관한다.

```text
=== results file
KTAP version 1
1..1
    KTAP version 1
    # Subtest: p6_ring
    1..3
        KTAP version 1
        # Subtest: p6_crc8_test
        ok 1 empty
        ok 2 check_str
        ok 3 one_ff
        ok 4 bytes_123
    # p6_crc8_test: pass:4 fail:0 skip:0 total:4
    ok 1 p6_crc8_test
    ok 2 p6_ring_fifo_wrap_test
    # p6_ring_full_test: EXPECTATION FAILED at p6_kunit_test.c:56
    Expected p6_ring_space(r) == 0U, but
        p6_ring_space(r) == 1 (0x1)
        0U == 0 (0x0)
    not ok 3 p6_ring_full_test
    # module: p6_kunit_test
# p6_ring: pass:2 fail:1 skip:0 total:3
# Totals: pass:5 fail:1 skip:0 total:6
not ok 1 p6_ring
=== rerun via debugfs
[   11.072217][   T75]     KTAP version 1
[   11.072263][   T75]     # Subtest: p6_ring
...
[   11.131733][   T75]     not ok 3 p6_ring_full_test
[   11.143767][   T75] not ok 1 p6_ring
```

출력에서 볼 것: `results`는 타임스탬프 없는 깨끗한 KTAP이라 **CI가 읽기에 dmesg보다 좋다**(커널 로그 링 버퍼가 넘쳐도 남아 있고, 다른 printk와 섞이지 않는다). `run` 파일에 아무 문자열이나 쓰면 스위트를 다시 돈다(재실행 부분은 길어서 `...`로 줄였다). 단, `lib/kunit/debugfs.c`를 보면 부팅 init 단계에서 도는 스위트(`kunit_test_init_section_suites`)에는 `run` 파일을 만들지 않는다 — init 메모리가 이미 해제됐기 때문이다.

### 2.6 필터 — 일부 케이스만 돌리기

KUnit executor는 `kunit.filter_glob=<suite_glob>.<test_glob>`(그리고 속성 필터 `kunit.filter=`, 예: `speed>slow`)를 받는다. built-in 테스트는 커널 커맨드라인으로, 모듈 테스트는 insmod 전에 `/sys/module/kunit/parameters/filter_glob`에 쓰면 된다(파라미터 권한 0600, `lib/kunit/executor.c`). 여기서 실제로 함정 하나를 밟았다: `echo "example.example_simple*" > .../filter_glob`(줄바꿈 포함)으로 쓰고 insmod하면 **아무 테스트도 돌지 않고 아무 메시지도 없다**. 문자열 끝의 `\n`까지 glob에 들어가 어떤 이름과도 맞지 않기 때문이다. `echo -n`으로 다시 하면:

```text
[    8.555767][   T77] KTAP version 1
[    8.555848][   T77] 1..1
[    8.557690][   T77]     # example: initializing suite
[    8.557943][   T77]     KTAP version 1
[    8.557964][   T77]     # Subtest: example
[    8.559563][   T77]     # module: kunit_example_test
[    8.559723][   T77]     1..1
[    8.569155][   T78]     # example_simple_test: initializing
[    8.575787][   T79]     # example_simple_test: cleaning up
[    8.581312][   T77]     ok 1 example_simple_test
[    8.581457][   T77]     # example: exiting suite
[    8.581578][   T77] ok 1 example
rc=0
```

출력에서 볼 것: 22개 케이스 중 1개만 돌았다. CI에서 "0개 실행"을 **실패로 취급**하지 않으면 이런 실수가 녹색으로 통과한다(§4.3의 파서는 `pass == 0`이면 실패로 본다).

### 2.7 예제 5 — `kunit.py run --arch=arm64` (공식 래퍼)

KUnit의 정석 사용법은 `tools/testing/kunit/kunit.py`다. 이 스크립트가 (1) `.kunitconfig`로 최소 커널 config를 만들고, (2) 빌드하고, (3) UML 또는 QEMU로 부팅해서, (4) KTAP을 파싱해 보기 좋게 보여 준다. 소스 트리를 더럽히지 않도록 `--build_dir`을 내 디렉터리로 줬다(랩 소스 트리는 읽기 전용으로 다룬다).

```sh
S=/var/lab/src/linux-6.18.55
time nice -n 10 python3 $S/tools/testing/kunit/kunit.py run --arch=arm64 \
    --build_dir=/var/tmp/P6/kunit --kunitconfig=$S/lib/kunit/.kunitconfig --jobs=2
```

```text
[09:00:10] Configuring KUnit Kernel ...
Generating .config ...
Populating config with:
$ make ARCH=arm64 O=/var/tmp/P6/kunit olddefconfig
[09:00:33] Building KUnit Kernel ...
Building with:
$ make all compile_commands.json scripts_gdb ARCH=arm64 O=/var/tmp/P6/kunit --jobs=2
[09:17:19] Starting KUnit Kernel (1/1)...
[09:17:19] ============================================================
Running tests with:
$ qemu-system-aarch64 -nodefaults -m 1024 -kernel /var/tmp/P6/kunit/arch/arm64/boot/Image.gz -append 'kunit.enable=1 console=ttyAMA0 kunit_shutdown=reboot' -no-reboot -nographic -accel kvm -accel hvf -accel tcg -serial stdio -machine virt -cpu max
[09:17:20] ================= example_init (1 subtest) =================
[09:17:20] [PASSED] example_init_test
[09:17:20] ================== [PASSED] example_init ===================
...
[09:17:20] =================== example_params_test  ===================
[09:17:20] [SKIPPED] example value 3
[09:17:20] [PASSED] example value 2
[09:17:20] [PASSED] example value 1
[09:17:20] [SKIPPED] example value 0
[09:17:20] =============== [PASSED] example_params_test ===============
...
[09:17:20] [PASSED] example_slow_test
[09:17:20] ===================== [PASSED] example =====================
[09:17:20] ============================================================
[09:17:20] Testing complete. Ran 85 tests: passed: 80, skipped: 5
[09:17:20] Elapsed time: 1030.372s total, 23.281s configuring, 1006.059s building, 1.024s running


real	17m11.298s
```

출력에서 볼 것(가운데 스위트 목록은 `...`로 줄였다):
- `lib/kunit/.kunitconfig`는 단 3줄(`CONFIG_KUNIT=y`, `CONFIG_KUNIT_TEST=y`, `CONFIG_KUNIT_EXAMPLE_TEST=y`)이다. 그 위에 `qemu_configs/arm64.py`가 PL011 콘솔 옵션을 더하고, 나머지는 `olddefconfig` 기본값이다. 그래서 전체 defconfig 커널보다 훨씬 작다(결과 `Image.gz` 2.3 MB, 빌드 디렉터리 132 MB).
- **빌드 1006초, 실행 1초**. -j2 + 19명 공유 부하 탓이 크지만, 구조적으로도 KUnit 비용의 거의 전부는 빌드다. 그래서 실무에서는 빌드 디렉터리를 캐시하고 증분 빌드한다.
- QEMU 인자에 `-accel kvm -accel hvf -accel tcg`가 있다 — 쓸 수 있는 가속을 차례로 시도한다. 이 VM에는 KVM이 없어 TCG로 떨어졌다.
- 커맨드라인의 `kunit_shutdown=reboot` + `-no-reboot`: 테스트가 끝나면 커널이 스스로 재부팅 → QEMU가 종료된다. 부트 테스트 자동화의 기본 패턴이다.

이미 빌드된 커널로 테스트만 다시 돌리는 `exec`와, KTAP 원문을 보는 `--raw_output`:

```sh
time python3 $S/tools/testing/kunit/kunit.py exec --arch=arm64 --build_dir=/var/tmp/P6/kunit "example.example_simple*"
python3 $S/tools/testing/kunit/kunit.py exec --arch=arm64 --build_dir=/var/tmp/P6/kunit --raw_output=kunit "example.example_simple*"
```

```text
[09:18:00] =================== example (1 subtest) ====================
[09:18:00] [PASSED] example_simple_test
[09:18:00] ===================== [PASSED] example =====================
[09:18:00] ============================================================
[09:18:00] Testing complete. Ran 1 tests: passed: 1
[09:18:00] Elapsed time: 2.734s

real	0m3.251s
...
KTAP version 1
1..1
    # example: initializing suite
    KTAP version 1
    # Subtest: example
    # module: kunit_example_test
    1..1
    # example_simple_test: initializing
    # example_simple_test: cleaning up
    ok 1 example_simple_test
    # example: exiting suite
ok 1 example
reboot: Restarting system
```

출력에서 볼 것: 빌드가 없으면 **3초**. 필터는 커맨드라인 `kunit.filter_glob=`으로 전달된다. 이것이 "커밋 전 로컬에서 돌리는" 피라미드 바닥층의 감각이다.

### 2.8 예제 6 — `kunit.py parse`로 아무 로그나 파싱하기 (그리고 함정)

`kunit.py parse <file>`은 빌드·실행 없이 로그 파일의 KTAP만 파싱한다. 실제 보드의 시리얼 로그도 이걸로 요약할 수 있다. 그런데 예제 3의 콘솔 로그를 그대로 넣으면:

```text
[09:20:47] ============================================================
[09:20:47] [ERROR] Test: main: 0 tests run!
[09:20:47] ============================================================
[09:20:47] Testing complete. Ran 0 tests: errors: 1
exit=1
```

0개다. 원인은 로그 앞부분, 부팅 중 built-in executor가 찍은 **빈 KTAP 문서**(`KTAP version 1` / `1..0`)다. 파서는 처음 만난 문서 하나만 읽는다. 모듈 출력 구간만 잘라서 다시 넣으면:

```sh
tr -d "\r" < ex2.log > ex2u.log                     # 시리얼 로그의 CR 제거
sed -n "/Run \/init/,/=== results/p" ex2u.log > ex2m.log
python3 $S/tools/testing/kunit/kunit.py parse /var/tmp/P6/ex2m.log; echo "exit=$?"
```

```text
[09:21:19] =================== p6_ring (3 subtests) ===================
[09:21:19] ====================== p6_crc8_test  =======================
[09:21:19] [PASSED] empty
[09:21:19] [PASSED] check_str
[09:21:19] [PASSED] one_ff
[09:21:19] [PASSED] bytes_123
[09:21:19] ================== [PASSED] p6_crc8_test ===================
[09:21:19] [PASSED] p6_ring_fifo_wrap_test
[09:21:19] # p6_ring_full_test: EXPECTATION FAILED at p6_kunit_test.c:56
[09:21:19] Expected p6_ring_space(r) == 0U, but
[09:21:19]     p6_ring_space(r) == 1 (0x1)
[09:21:19]     0U == 0 (0x0)
[09:21:19] [FAILED] p6_ring_full_test
[09:21:19]     # module: p6_kunit_test
[09:21:19] # p6_ring: pass:2 fail:1 skip:0 total:3
[09:21:19] # Totals: pass:5 fail:1 skip:0 total:6
[09:21:19] ===================== [FAILED] p6_ring =====================
[09:21:19] ============================================================
[09:21:19] Testing complete. Ran 6 tests: passed: 5, failed: 1
exit=1
```

출력에서 볼 것: 이번엔 exit code 1 — CI 게이트로 쓸 수 있다. 같은 방법으로 예제 2 로그(모듈 4개)를 넣으면 `Ran 25 tests: passed: 23, skipped: 2`가 나온다. 즉 **첫 모듈(kunit-test)의 문서만** 셌다. insmod할 때마다 새 KTAP 문서가 시작되기 때문이다. 결론: 모듈을 여러 개 돌리는 CI에서는 dmesg 전체가 아니라 **스위트별 debugfs `results` 파일을 따로따로** 모으는 것이 안전하다. §4.3의 CI 잡이 그렇게 한다.

### 2.9 KUnit API 한눈에

| 하고 싶은 것 | API (6.18 `include/kunit/`) |
|---|---|
| 값 비교 | `KUNIT_EXPECT_EQ/NE/LT/LE/GT/GE`, `_TRUE/_FALSE` |
| 포인터·에러 | `KUNIT_EXPECT_NULL/NOT_NULL`, `KUNIT_EXPECT_NOT_ERR_OR_NULL`, `KUNIT_EXPECT_PTR_EQ` |
| 문자열·메모리 | `KUNIT_EXPECT_STREQ`, `KUNIT_EXPECT_MEMEQ` (실패 시 hexdump) |
| 메시지 추가 | 모든 매크로의 `_MSG` 변형 (`printf` 형식) |
| 즉시 중단 | 위 전부의 `KUNIT_ASSERT_*` |
| 건너뛰기 | `kunit_skip(test, "why")`, `kunit_mark_skipped()` |
| 느린 테스트 표시 | `KUNIT_CASE_SLOW` (속성 필터 `speed`) |
| 파라미터 | `KUNIT_CASE_PARAM`, `KUNIT_ARRAY_PARAM`, `KUNIT_ARRAY_PARAM_DESC` |
| 자동 해제 자원 | `kunit_kzalloc`, `kunit_kmalloc_array`, `kunit_add_action` |
| 가짜 struct device | `kunit_device_register()`, `kunit_platform_device_alloc()` |
| 함수 바꿔치기 | `KUNIT_STATIC_STUB_REDIRECT` + `kunit_activate_static_stub()` |
| static 함수 테스트 | `VISIBLE_IF_KUNIT`, `EXPORT_SYMBOL_IF_KUNIT` (`include/kunit/visibility.h`, 네임스페이스 `EXPORTED_FOR_KUNIT_TESTING`) |

드라이버 코드를 KUnit으로 테스트하는 현실적인 방법은 "**하드웨어와 닿지 않는 부분을 순수 함수로 떼어 내는 것**"이다. 예를 들어 IMU 드라이버라면 레지스터 원시값 → SI 단위 변환, FIFO 프레임 파서, 타임스탬프 보간이 그렇다. 레지스터 접근이 필요하면 regmap을 RAM 백엔드로 바꾼다 — `regmap-kunit`이 정확히 그 방법을 쓴다(H2).

---

## 3. kselftest — 유저 공간에서 커널 인터페이스를 두드리기

### 3.1 정의와 구조

kselftest는 커널 소스 트리 `tools/testing/selftests/` 안에 있는 **유저 공간 테스트 모음**이다. 서브시스템마다 디렉터리(`timers/`, `gpio/`, `net/`, `mm/`, `kcmp/`, `size/` …)가 있고, 각 디렉터리의 `Makefile`이 `lib.mk`를 include해서 `TEST_GEN_PROGS`(빌드해야 하는 C 테스트)와 `TEST_PROGS`(셸 스크립트)를 선언한다. 디렉터리마다 `config` 파일이 있으면 "이 테스트가 요구하는 커널 옵션"이다(예: `gpio/config`에 `CONFIG_GPIO_MOCKUP=m`). 결과는 TAP 13이다.

KUnit과의 경계: KUnit은 **커널 내부 함수**를 직접 부르고, kselftest는 **syscall·ioctl·sysfs라는 공개 인터페이스(uAPI)** 만 쓴다. 그래서 kselftest는 "유저가 보는 동작"의 회귀를 잡고, 커널을 바꿔도(업그레이드, P5) 같은 바이너리로 돌 수 있다.

### 3.2 예제 7 — 하위 집합만 빌드·설치

무엇을 확인하나: 전체 kselftest(수백 개)가 아니라 필요한 컬렉션만 빌드해 "설치 디렉터리"를 만드는 법. 커널 uAPI 헤더가 필요하므로 먼저 `make headers`를 내 출력 디렉터리(`O=`)로 한다. VM이 arm64라 크로스 컴파일 없이 네이티브로 빌드했다 — 빌드 결과는 같은 arm64 Ubuntu 게스트에서 그대로 돈다.

```sh
S=/var/lab/src/linux-6.18.55
make -s -C $S O=/var/tmp/P6/ksb headers -j2
make -C $S/tools/testing/selftests O=/var/tmp/P6/ksb \
     TARGETS="timers kcmp size gpio sync breakpoints" -j2 install INSTALL_PATH=/var/tmp/P6/kst
ls /var/tmp/P6/kst; cat /var/tmp/P6/kst/kselftest-list.txt
```

```text
Emit Tests for timers
...
**Kselftest Installation is complete: /var/tmp/P6/kst**

real	0m8.173s
breakpoints
gpio
kcmp
kselftest
kselftest-list.txt
run_kselftest.sh
size
sync
timers
timers:posix_timers
timers:nanosleep
timers:nsleep-lat
timers:set-timer-lat
timers:mqueue-lat
timers:inconsistency-check
timers:raw_skew
timers:threadtest
timers:rtcpie
kcmp:kcmp_test
size:get_size
gpio:gpio-mockup.sh
gpio:gpio-sim.sh
gpio:gpio-aggregator.sh
sync:sync_test
breakpoints:step_after_suspend_test
breakpoints:breakpoint_test_arm64
```

출력에서 볼 것: 설치 디렉터리(2.3 MB)는 **그대로 타깃에 복사해서 돌리는 단위**다. 실행기 `run_kselftest.sh`와 `컬렉션:테스트` 목록이 함께 들어간다. 크로스 빌드라면 `ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-`를 붙이면 된다(B1). `make headers` 산출물은 14 MB였다. 전체 소스 트리를 깨끗하게 유지하는 것이 `O=` 빌드의 전제다 — 소스 트리에 `.config`가 있으면 `O=` 빌드가 거부된다(E2).

`run_kselftest.sh` 옵션(실제 `-h` 출력 일부):

```text
  -s | --summary		Print summary with detailed log in output.log (conflict with -p)
  -p | --per-test-log		Print test log in /tmp with each test name (conflict with -s)
  -t | --test COLLECTION:TEST	Run TEST from COLLECTION
  -c | --collection COLLECTION	Run all tests from COLLECTION
  -l | --list			List the available collection:test entries
  -d | --dry-run		Don't actually run any tests
  -n | --netns			Run each test in namespace
  -o | --override-timeout	Number of seconds after which we timeout
```

각 컬렉션의 `settings` 파일이 기본 타임아웃(기본 45초, `timers`는 `timeout=0` 즉 무제한)을 정한다. CI에서는 `-o`로 상한을 강제하는 게 좋다 — 타임아웃 없는 테스트 하나가 잡 전체를 멈춘다.

### 3.3 예제 8 — kselftest 하네스로 테스트 직접 짜기

kselftest에는 Google Test 비슷한 헤더 전용 하네스 `kselftest_harness.h`가 있다(`TEST`, `TEST_F`, `FIXTURE`, `ASSERT_*`/`EXPECT_*`, `TH_LOG`). 무엇을 확인하나: eventfd의 의미 세 가지(쓰기 값 누적, 빈 상태 non-blocking 읽기 → `EAGAIN`, 8바이트 미만 읽기 → `EINVAL`)와, 일부러 틀린 기대값 하나(`EFD_SEMAPHORE`는 읽을 때마다 1씩만 준다 — A4).

```c
// p6_eventfd_test.c - kselftest_harness.h 로 쓴 유저 공간 테스트 (eventfd 의미 검증)
#include <errno.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/eventfd.h>
#include "kselftest_harness.h"

FIXTURE(efd) { int fd; };				/* 케이스마다 새 fd */
FIXTURE_SETUP(efd)    { self->fd = eventfd(0, EFD_NONBLOCK); ASSERT_GE(self->fd, 0); }
FIXTURE_TEARDOWN(efd) { close(self->fd); }

TEST_F(efd, counter_adds)
{
	uint64_t v = 3, out;

	ASSERT_EQ(write(self->fd, &v, 8), 8);
	ASSERT_EQ(write(self->fd, &v, 8), 8);
	ASSERT_EQ(read(self->fd, &out, 8), 8);
	EXPECT_EQ(6ULL, out);				/* 쓰기 값이 누적된다 */
}

TEST_F(efd, empty_read_eagain)
{
	uint64_t out;

	EXPECT_EQ(read(self->fd, &out, 8), -1);
	EXPECT_EQ(errno, EAGAIN);			/* NONBLOCK + 0 -> EAGAIN */
}

TEST_F(efd, short_read_einval)
{
	uint32_t small;

	EXPECT_EQ(read(self->fd, &small, 4), -1);
	EXPECT_EQ(errno, EINVAL);			/* 8바이트 미만 읽기 금지 */
}

TEST(semaphore_mode_wrong_expectation)		/* 일부러 틀린 기대값 */
{
	uint64_t v = 5, out;
	int fd = eventfd(0, EFD_SEMAPHORE);

	ASSERT_GE(fd, 0);
	ASSERT_EQ(write(fd, &v, 8), 8);
	ASSERT_EQ(read(fd, &out, 8), 8);
	EXPECT_EQ(5ULL, out) TH_LOG("EFD_SEMAPHORE read returns %llu", (unsigned long long)out);
	close(fd);
}

TEST_HARNESS_MAIN
```

```sh
gcc -std=gnu11 -Wall -Wextra -O2 -I$S/tools/testing/selftests p6_eventfd_test.c -o /tmp/P6/p6_eventfd_test
/tmp/P6/p6_eventfd_test; echo "exit=$?"
```

```text
TAP version 13
1..4
# Starting 4 tests from 2 test cases.
#  RUN           global.semaphore_mode_wrong_expectation ...
# /Users/donh/.elinux_scratch/P6/ksh/p6_eventfd_test.c:46:semaphore_mode_wrong_expectation:Expected 5ULL (5) == out (1)
# /Users/donh/.elinux_scratch/P6/ksh/p6_eventfd_test.c:46:semaphore_mode_wrong_expectation:EFD_SEMAPHORE read returns 1
# semaphore_mode_wrong_expectation: Test failed
#          FAIL  global.semaphore_mode_wrong_expectation
not ok 1 global.semaphore_mode_wrong_expectation
#  RUN           efd.counter_adds ...
#            OK  efd.counter_adds
ok 2 efd.counter_adds
#  RUN           efd.empty_read_eagain ...
#            OK  efd.empty_read_eagain
ok 3 efd.empty_read_eagain
#  RUN           efd.short_read_einval ...
#            OK  efd.short_read_einval
ok 4 efd.short_read_einval
# FAILED: 3 / 4 tests passed.
# Totals: pass:3 fail:1 xfail:0 xpass:0 skip:0 error:0
exit=1
```

출력에서 볼 것: 하네스는 **각 테스트를 fork한 자식 프로세스**에서 돌린다(그래서 한 테스트가 segfault해도 나머지는 돈다 — KUnit의 kthread와 같은 발상). 실패 시 `파일:줄`, 식 원문, 양쪽 값, `TH_LOG` 메시지가 나오고, 프로세스 exit code가 1이다. 하네스 매크로의 인자 순서는 `EXPECT_EQ(expected, seen)`이다 — 처음에 `EXPECT_EQ(out, 6)`으로 썼더니 `uint64_t`와 `int` 비교로 `-Wsign-compare` 경고가 났고 메시지도 뒤집혀 나왔다. `6ULL`로 타입을 맞춰 경고 0개로 만들었다. 이 예제는 VM 호스트 커널(7.0)에서 실행했다 — uAPI 테스트라 커널 버전만 다를 뿐 같은 코드다.

### 3.4 LTP — Linux Test Project (가이드, 실행 안 함)

LTP는 커널 트리 밖의 별도 프로젝트(github.com/linux-test-project/ltp)로, syscall 하나하나의 POSIX 의미·에러 코드를 수천 개 테스트로 검증한다. kselftest가 "커널 개발자가 자기 기능을 위해 쓴 테스트"라면 LTP는 "배포판·벤더가 시스템 전체를 인증하는 테스트"에 가깝다. 랩 rootfs에는 설치돼 있지 않아 실행하지 않았다. 형태만 기억해 두자(기억 기반 — LTP 문서로 확인 필요):

```sh
# 가이드(실행 안 함): 빌드 후 /opt/ltp 에 설치, 런타임 파일(runtest/syscalls 등) 단위로 실행
./configure && make -j"$(nproc)" && make install
cd /opt/ltp && ./kirk -f ltp -r syscalls      # 최근 실행기 kirk (예전엔 ./runltp -f syscalls)
```

임베디드에서 LTP를 전부 돌리는 일은 드물다. 보통 `syscalls`, `fs`, `mm`, `sched`의 일부를 골라 릴리스 후보 때만 돌린다. 몇 시간이 걸리기 때문이다.

### 3.5 드라이버 레벨 테스트 — 가짜 장치를 붙여서 진짜 드라이버를 돌린다

"드라이버 테스트"의 가장 현실적인 형태는 **커널이 제공하는 가짜 장치 + 진짜 서브시스템 코드 + 유저 공간 테스트**다. 랩 커널에도 있는 예:

| 가짜 장치 | 무엇을 흉내 내나 | 같이 쓰는 테스트 |
|---|---|---|
| `gpio-mockup`, `gpio-sim`(configfs) | GPIO 칩, 라인 값·이름·풀업 | kselftest `gpio/`, libgpiod 테스트 |
| `i2c-stub` | 레지스터를 가진 I2C 칩 → 진짜 센서 드라이버가 probe | i2c-tools + IIO sysfs 읽기 (Q5) |
| `iio_dummy` | IIO 채널·버퍼·트리거 | libiio `iio_readdev` |
| `vivid`, `vimc` | V4L2 카메라·media graph | `v4l2-compliance` (Q2) |
| `vcan` | CAN 인터페이스 | can-utils `cangen`/`candump` (Q4) |
| `netdevsim` | NIC·PTP 클럭 | kselftest `drivers/net/netdevsim` |

§4.3의 CI 잡이 돌린 `gpio-mockup.sh`가 이 유형이다. 스크립트가 `modprobe gpio-mockup gpio_mockup_ranges=...`로 칩을 만들고, 라인을 읽고 쓰고, **일부러 잘못된 파라미터로 probe 실패를 유도**한 뒤("Module load error tests") 그 실패가 올바른 에러 코드인지 본다. 그래서 테스트가 통과하는 동안 dmesg에 `probe with driver gpio-mockup failed with error -22`가 찍힌다 — 실패 로그가 곧 정상이다. 에러 경로 테스트는 bring-up에서 가장 놓치기 쉬운 부분이다.

---

## 4. 부트 테스트 — "커널이 셸까지 가나"를 기계가 확인하게

### 4.1 무엇을 자동화하나

사람이 bring-up 할 때 하는 일을 그대로 스크립트로 옮긴다: 전원 넣기 → 시리얼 콘솔에서 특정 문자열 기다리기 → 로그인 → 명령 보내기 → 결과 읽기 → 전원 끄기. 핵심은 세 가지다.

- **expect 패턴**: "이 정규식이 나올 때까지 최대 N초 기다린다". 나오면 다음 단계, 안 나오면 그 단계 이름으로 실패(`timeout at login`). 단계 이름이 곧 1차 진단이다.
- **고유 프롬프트**: 셸 프롬프트를 `P6PROMPT# ` 같은 절대 테스트 출력에 안 나올 문자열로 바꿔서, "명령이 끝났다"를 오판하지 않는다.
- **깨끗한 시작 상태**: 매 실행마다 새 rootfs. QEMU에서는 qcow2 overlay를 새로 만들면 된다(원본은 읽기 전용 backing file). 실 보드에서는 매번 이미지를 다시 쓰거나(LAVA deploy), 읽기 전용 rootfs + tmpfs(J3).

### 4.2 직접 짠 CI 잡의 구조

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" fill="currentColor"/>
    </marker>
  </defs>
  <rect x="10" y="30" width="110" height="64" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="65" y="57" font-size="13" text-anchor="middle">git push</text>
  <text x="65" y="77" font-size="12" text-anchor="middle">MR · 패치</text>
  <rect x="145" y="30" width="165" height="64" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="227" y="52" font-size="13" text-anchor="middle">빌드</text>
  <text x="227" y="70" font-size="12" text-anchor="middle">Image · modules</text>
  <text x="227" y="86" font-size="12" text-anchor="middle">test .ko · kselftest</text>
  <rect x="330" y="30" width="150" height="64" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="405" y="52" font-size="13" text-anchor="middle">이미지 준비</text>
  <text x="405" y="70" font-size="12" text-anchor="middle">qcow2 overlay 새로</text>
  <text x="405" y="86" font-size="12" text-anchor="middle">payload.ext4 (RO)</text>
  <rect x="505" y="30" width="165" height="64" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="587" y="52" font-size="13" text-anchor="middle">QEMU 부팅</text>
  <text x="587" y="70" font-size="12" text-anchor="middle">expect: Linux version</text>
  <text x="587" y="86" font-size="12" text-anchor="middle">expect: root@ 프롬프트</text>
  <line x1="120" y1="62" x2="143" y2="62" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="310" y1="62" x2="328" y2="62" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="480" y1="62" x2="503" y2="62" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="587" y1="94" x2="587" y2="138" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="505" y="140" width="165" height="80" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="587" y="162" font-size="13" text-anchor="middle">게스트에서 테스트</text>
  <text x="587" y="180" font-size="12" text-anchor="middle">KUnit → debugfs</text>
  <text x="587" y="196" font-size="12" text-anchor="middle">run_kselftest.sh</text>
  <text x="587" y="212" font-size="12" text-anchor="middle">공장 스크립트</text>
  <line x1="505" y1="180" x2="482" y2="180" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="320" y="140" width="160" height="80" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="400" y="162" font-size="13" text-anchor="middle">TAP 파싱</text>
  <text x="400" y="180" font-size="12" text-anchor="middle">문서별 최상위 ok 수</text>
  <text x="400" y="196" font-size="12" text-anchor="middle">quarantine 분리</text>
  <text x="400" y="212" font-size="12" text-anchor="middle">0개 실행 = 실패</text>
  <line x1="320" y1="180" x2="297" y2="180" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="145" y="140" width="150" height="80" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="220" y="162" font-size="13" text-anchor="middle">게이트</text>
  <text x="220" y="182" font-size="12" text-anchor="middle">exit 0 → 머지 가능</text>
  <text x="220" y="200" font-size="12" text-anchor="middle">exit 1 → 차단</text>
  <line x1="145" y1="180" x2="122" y2="180" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="10" y="140" width="110" height="80" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="65" y="162" font-size="13" text-anchor="middle">아티팩트</text>
  <text x="65" y="180" font-size="12" text-anchor="middle">console.log</text>
  <text x="65" y="196" font-size="12" text-anchor="middle">tests.log</text>
  <text x="65" y="212" font-size="12" text-anchor="middle">summary.json</text>
  <rect x="145" y="255" width="525" height="56" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-dasharray="5 4"/>
  <text x="407" y="278" font-size="13" text-anchor="middle">야간 · 릴리스: 같은 테스트 payload를 실 보드에 (LAVA 잡 · HIL 리그)</text>
  <text x="407" y="298" font-size="12" text-anchor="middle">QEMU가 모르는 것: SoC 클럭·핀먹스·실제 DDR·센서·전원 시퀀스</text>
  <line x1="220" y1="220" x2="220" y2="253" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah2)"/>
</svg>
```
그림 2 — 이 노트의 CI 잡(§4.3)이 하는 일. 위 줄은 준비, 아래 줄은 결과를 읽어 게이트를 여닫는 단계다. 점선 상자는 같은 payload를 실제 보드로 보내는 다음 층이다.

### 4.3 예제 9 — QEMU 부팅 테스트 CI 잡 (pexpect)

무엇을 확인하나: CI 러너가 할 일 전체 — 새 overlay로 Image-dom + rootfs-dom 부팅, 로그인 대기, 테스트 payload 디스크 마운트, KUnit·kselftest·공장 스크립트 실행, 문서별 TAP 파싱, 아티팩트 3개 저장, **exit code로 pass/fail**. VM 호스트에 있는 Python `pexpect` 4.9를 썼다(expect의 Python판).

테스트 payload(두 번째 virtio 디스크, 읽기 전용 ext4)는 이렇게 만든다. rootfs를 건드리지 않고 "테스트 내용"만 바꿔 끼울 수 있다.

```sh
cd /var/tmp/P6; rm -rf pl && mkdir -p pl/ko
cp -a kst pl/kst && cp kmod/p6_kunit_test.ko pl/ko/ && cp run_tests.sh p6_factory.py pl/
mkfs.ext4 -q -F -L p6payload -d pl payload.ext4 32M
```

게스트 안 진입점 `run_tests.sh`(KTAP/TAP 문서마다 `P6_DOC` 경계 줄을 찍는다):

```sh
#!/bin/bash
# run_tests.sh - 게스트 안에서 도는 테스트 진입점 (payload 디스크 /dev/vdb -> /mnt/p6)
P=/mnt/p6
mountpoint -q /sys/kernel/debug || mount -t debugfs none /sys/kernel/debug
echo "P6_STAGE kunit $(cut -d' ' -f1 /proc/uptime)"
for m in kunit-example-test list-test regmap-kunit; do modprobe "$m" || echo "P6_MODFAIL $m"; done
insmod "$P/ko/p6_kunit_test.ko" || echo "P6_MODFAIL p6_kunit_test"
for d in /sys/kernel/debug/kunit/*/; do
  echo "P6_DOC kunit:$(basename "$d")"
  cat "$d/results"
done
echo "P6_STAGE kselftest $(cut -d' ' -f1 /proc/uptime)"
cp -a "$P/kst" /tmp/kst && cd /tmp/kst || exit 2   # 테스트가 cwd에 파일을 쓴다 (kcmp)
T="-t timers:posix_timers -t timers:nanosleep -t kcmp:kcmp_test -t size:get_size"
T="$T -t sync:sync_test -t gpio:gpio-mockup.sh -t breakpoints:breakpoint_test_arm64"
echo "P6_DOC kselftest"
./run_kselftest.sh -o 120 $T 2>&1
echo "P6_DOC factory"
if python3 "$P/p6_factory.py" SN-QEMU-0001; then echo "ok 1 factory"; else echo "not ok 1 factory"; fi
echo "P6_STAGE done $(cut -d' ' -f1 /proc/uptime)"
```

러너 앞부분 — QEMU 운전:

```python
#!/usr/bin/env python3
"""ci_boot_test.py - QEMU 부팅 -> 게스트 테스트 -> KTAP/TAP 파싱 -> exit code (CI 잡 한 개)"""
import json, re, subprocess, sys, time
import pexpect

W, OUT = "/var/tmp/P6", "/var/tmp/P6/artifacts"
PROMPT = "P6PROMPT# "                      # 테스트 출력과 섞이지 않는 고유 프롬프트
QEMU = ("qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 1536M -nographic"
        " -kernel /var/lab/out/Image-dom"
        f" -drive file={W}/ci.qcow2,if=virtio,format=qcow2"
        f" -drive file={W}/payload.ext4,if=virtio,format=raw,readonly=on"
        " -netdev user,id=n0 -device virtio-net-pci,netdev=n0 -append"
        " 'console=ttyAMA0 root=/dev/vda rw rootwait systemd.default_device_timeout_sec=900'")

def stage(name, t0):
    print(f"[ci] {name:<10} +{time.time() - t0:6.1f}s", flush=True)

def main():
    subprocess.run(["mkdir", "-p", OUT], check=True)
    subprocess.run(["qemu-img", "create", "-q", "-f", "qcow2", "-F", "raw", "-b",
                    "/var/lab/out/rootfs-dom.ext4", f"{W}/ci.qcow2"], check=True)  # 매번 새 overlay
    t0 = time.time()
    con = pexpect.spawn("/bin/sh", ["-c", QEMU], encoding="utf-8", codec_errors="replace",
                        timeout=900)
    con.logfile_read = open(f"{OUT}/console.log", "w")   # 아티팩트 1: 전체 콘솔
    con.expect(r"Linux version (\S+)"); kver = con.match.group(1); stage("kernel", t0)
    con.expect(r"root@\S+:~# "); stage("login", t0)       # rootfs-dom 은 ttyAMA0 autologin
    con.sendline(f"export PS1='{PROMPT}'; stty -echo cols 200")
    con.expect_exact(PROMPT); con.expect_exact(PROMPT)
    con.sendline("mkdir -p /mnt/p6 && mount -o ro /dev/vdb /mnt/p6 && /mnt/p6/run_tests.sh")
    con.expect_exact(PROMPT, timeout=1800); log = con.before; stage("tests", t0)
    con.sendline("poweroff"); con.expect(pexpect.EOF, timeout=300); stage("poweroff", t0)
    open(f"{OUT}/tests.log", "w").write(log)              # 아티팩트 2: 테스트 출력만
    res = parse(log.replace("\r", ""))
    res.update(kernel=kver, wall_s=round(time.time() - t0, 1))
    json.dump(res, open(f"{OUT}/summary.json", "w"), indent=1)  # 아티팩트 3: 요약
    print(json.dumps({k: res[k] for k in ("kernel", "docs", "pass", "fail", "skip", "known", "wall_s")}))
    for f in res["failed"]:
        print("[ci]", f)
    return 1 if res["fail"] or not res["pass"] else 0
```

러너 뒷부분 — TAP 파서:

```python
QUARANTINE = {"selftests: timers: posix_timers", "selftests: sync: sync_test"}  # 티켓 달린 flaky
LEAF = re.compile(r"^(\s*)(not ok|ok) \d+ (.+?)(?:\s+#\s*(SKIP|TODO).*)?$")

def parse(text):
    """여러 KTAP/TAP 문서를 줄 단위로 센다. P6_DOC 줄이 문서 경계."""
    res, doc = {"docs": 0, "pass": 0, "fail": 0, "skip": 0, "known": 0, "failed": []}, "?"
    for line in text.splitlines():
        if line.startswith("P6_DOC "):
            doc, res["docs"] = line.split(None, 1)[1], res["docs"] + 1
            continue
        m = LEAF.match(line)
        if not m:
            continue
        if m.group(1):                        # 들여쓴 줄 = 하위 결과: 실패만 기록, 개수는 최상위만
            if m.group(2) == "not ok":
                res["failed"].append(f"[sub]   {doc}: {m.group(3)}")
            continue
        key = "skip" if m.group(4) == "SKIP" else ("fail" if m.group(2) == "not ok" else "pass")
        if key == "fail" and m.group(3).split(" #")[0] in QUARANTINE:
            key = "known"                     # 실패지만 게이트는 막지 않음, 따로 보고
        res[key] += 1
        if key in ("fail", "known"):
            res["failed"].append(f"[{key}]  {doc}: {m.group(3)}")
    return res

if __name__ == "__main__":
    sys.exit(main())
```

읽는 법:
- `expect_exact(PROMPT)`를 두 번 하는 이유: 첫 번째는 내가 보낸 `export PS1='P6PROMPT# '` 명령의 **에코**(그 안에 프롬프트 문자열이 들어 있다)에 걸리고, 두 번째가 진짜 새 프롬프트다. 그 뒤로는 `stty -echo`라 에코가 없다. 이런 자기 매칭은 expect 스크립트의 고전적인 버그다.
- 파서는 들여쓰기 없는 `ok`/`not ok`만 센다. kselftest의 하위 결과는 `# ok 2 ...`처럼 `#`로 시작하고, KUnit의 하위 결과는 들여쓰기돼 있어 자동으로 빠진다. 커널 printk가 콘솔에 섞여도(`[  846.07] gpio-mockup ...`) 줄 맨 앞이 `[`라서 걸리지 않는다.
- 실패는 suite 단위로 세지만, 들여쓴 `not ok`(어느 케이스가 깨졌나)는 진단용으로 따로 모은다.

**첫 번째 실행** — 버그 있는 `p6_ring`, kselftest를 읽기 전용 payload에서 바로 실행, 타임아웃 300초, quarantine 없음(아래는 그 당시 버전의 출력이다):

```text
[ci] kernel     +  22.3s
[ci] login      + 660.5s
[ci] tests      +1225.6s
[ci] poweroff   +1260.4s
{"kernel": "6.18.55-dom", "docs": 8, "pass": 10, "fail": 4, "skip": 0, "wall_s": 1260.4}
[ci] FAIL kunit:p6_ring:   sub p6_ring_full_test
[ci] FAIL kunit:p6_ring: p6_ring
[ci] FAIL kselftest: selftests: kcmp: kcmp_test # exit=1
[ci] FAIL kselftest: selftests: sync: sync_test # TIMEOUT 300 seconds
[ci] FAIL kselftest: selftests: timers: posix_timers # exit=1

real	21m3.011s
EXIT=1
```

출력에서 볼 것 — 실패 4개가 **네 가지 서로 다른 종류**라서 교과서처럼 좋다:

**(1)** `p6_ring` — **진짜 버그**(심어 둔 off-by-one). 게이트가 막아야 하는 실패.

**(2)** `kcmp_test` — **테스트 환경 문제**. `tests.log`에 원인이 있다:

```text
# selftests: kcmp: kcmp_test
# Can't create file: Read-only file system
# # Totals: pass:0 fail:0 xfail:0 xpass:0 skip:0 error:0
not ok 1 selftests: kcmp: kcmp_test # exit=1
```

kselftest 중 일부는 **현재 디렉터리에 임시 파일을 만든다**. payload를 읽기 전용으로 마운트했기 때문에 실패했다. 고치는 법: 설치 디렉터리를 `/tmp`(쓰기 가능)로 복사해서 실행 — 위 `run_tests.sh`의 `cp -a` 줄이 그 수정이다.

**(3)** `sync_test # TIMEOUT 300 seconds` — **부하에 민감한 스트레스 테스트**. 10개 중 7번째까지 진행하다 멈췄다. 19명이 쓰는 TCG 환경에서 스레드 경쟁 테스트가 300초를 넘겼다(두 번째 실행에서 이것이 단순 부하 문제가 아닐 수 있다는 단서가 나온다).

**(4)** `posix_timers # exit=1` — **타이밍 테스트의 거짓 실패**. 테스트 자신이 경고한다:

```text
# # Testing posix timers. False negative may happen on CPU execution 
# # based timers if other threads run on the CPU...
# ok 1 check timer create exact
# Diff too high: 3391076..not ok 2 ITIMER_VIRTUAL
# Diff too high: 2836221..not ok 3 ITIMER_PROF
# ok 4 ITIMER_REAL
...
# not ok 7 check signal distribution
...
# # Totals: pass:16 fail:3 xfail:0 xpass:0 skip:0 error:0
```

CPU 시간 기반 타이머(`ITIMER_VIRTUAL`/`PROF`)는 "프로세스가 실제로 CPU를 쓴 시간"을 재는데, 호스트가 과부하라 vCPU 자체가 밀리면서 오차 한계를 넘었다. 실 보드였다면 통과했을 가능성이 높다. 이런 테스트는 **quarantine 목록**(티켓 번호와 함께, 결과는 계속 보고하되 게이트는 막지 않음)으로 관리한다.

또 하나: `login +660.5s`. systemd 부팅에 11분 — `systemd-logind` 기동이 7분 넘게 걸렸다(콘솔 로그의 `Job systemd-logind.service/start running (7min 8s / 8min 14s)`). TCG + 공유 부하의 결과이고, expect 타임아웃(900초)을 넉넉히 둔 이유다. 실제 CI에서는 부팅 시간 자체를 지표로 기록해 회귀를 본다(M10).

**두 번째 실행** — 고친 것 세 가지: (1) `p6_ring.h`의 `space()`를 `r->size - 1 - p6_ring_count(r)`로 수정해 다시 빌드, (2) kselftest를 `/tmp`로 복사해 실행, (3) `-o 120` 타임아웃 + 부하 민감 테스트 2개를 `QUARANTINE`에 등록. 그리고 공장 스크립트를 게스트에서 실행.

```sh
cd /var/tmp/P6 && time python3 ci_boot_test.py; echo "EXIT=$?"
```

```text
[ci] kernel     +   4.0s
[ci] login      + 184.4s
[ci] tests      + 477.6s
[ci] poweroff   + 496.1s
{"kernel": "6.18.55-dom", "docs": 9, "pass": 13, "fail": 0, "skip": 0, "known": 2, "wall_s": 496.1}
[ci] [known]  kselftest: selftests: sync: sync_test # TIMEOUT 120 seconds
[ci] [known]  kselftest: selftests: timers: posix_timers # exit=1

real	8m16.550s
EXIT=0
```

출력에서 볼 것:
- **exit 0** — 게이트 통과. 하지만 `known` 2개가 결과에 **계속 남는다**. quarantine은 "숨기기"가 아니라 "게이트에서 빼고 따로 추적하기"다.
- 9개 문서(KUnit 스위트 7 + kselftest 1 + factory 1), 최상위 13개 통과. `p6_ring`은 이제 `ok 3 p6_ring_full_test` / `# p6_ring: pass:3 fail:0`.
- 부하가 조금 줄어 로그인까지 184초(1차 660초). **같은 잡의 부팅 시간이 3.6배 흔들린다** — QEMU CI의 타이밍 숫자를 성능 지표로 쓰면 안 되는 이유다.
- `sync_test`는 두 번 다 **같은 지점**(8번째 `test_stress_two_threads_shared_timeline`, 두 스레드가 2^16번 핑퐁하는 스트레스 테스트)에서 멈췄다. 무작위 flaky라기보다 "이 환경에서는 결정적으로 느리다"에 가깝다. 이런 차이를 구분해 적어 두는 것이 quarantine 티켓의 내용이다 — 실 보드에서 돌려 보고 통과하면 "환경 한정", 거기서도 멈추면 진짜 버그.

`tests.log`에서 고친 두 항목:

```text
# selftests: kcmp: kcmp_test
# TAP version 13
# 1..3
# pid1:   1900 pid2:   1901 FD:  1 FILES:  2 VM:  1 FS:  2 SIGHAND:  1 IO:  0 SYSVSEM:  0 INV: -1
# PASS: 0 returned as expected
# PASS: 0 returned as expected
# PASS: 0 returned as expected
# # Totals: pass:3 fail:0 xfail:0 xpass:0 skip:0 error:0
ok 1 selftests: kcmp: kcmp_test
P6_DOC factory
ddr_mib           1450.391 MiB     [1400, 1600] PASS
cpu_count            2.000 cores   [2, 2] PASS
rootfs_free_gb       2.565 GB      [2.0, None] PASS
rail_3v3             3.312 V       [3.135, 3.465] PASS
loopback_err         0.000 errors  [0, 0] PASS
RESULT PASS -> /tmp/P6/SN-QEMU-0001_b11107f7.json
ok 1 factory
```

(두 블록은 `tests.log`의 서로 떨어진 두 구간을 이어 붙였다.) 공장 스크립트가 "맞는 유닛"(1536 MiB 중 커널이 보고한 1450 MiB, 2코어)에서는 PASS다. 게스트의 rootfs 여유 2.565 GB는 하한 2.0 GB에 꽤 가깝다 — 실제 라인이라면 이런 값이 분포로 쌓이면서 한계 쪽으로 밀리는지를 본다(§7.2).

아티팩트 `summary.json`(실제 파일):

```text
{
 "docs": 9,
 "pass": 13,
 "fail": 0,
 "skip": 0,
 "known": 2,
 "failed": [
  "[known]  kselftest: selftests: sync: sync_test # TIMEOUT 120 seconds",
  "[known]  kselftest: selftests: timers: posix_timers # exit=1"
 ],
 "kernel": "6.18.55-dom",
 "wall_s": 496.1
}
```

### 4.4 CI 서비스에 올리기 — GitHub Actions / GitLab CI (가이드, 실행 안 함)

위 러너는 "명령 하나 → exit code"이므로 어떤 CI 서비스에도 그대로 들어간다. 아래 YAML은 구조를 보여 주는 가이드이며 이 랩에서 실행하지 않았다(사내 러너 이름·캐시 경로는 예시다).

```text
# .github/workflows/kernel-ci.yml  (가이드)
name: kernel-ci
on: [pull_request]
jobs:
  kunit:                                   # 바닥층: 빠름, 모든 PR
    runs-on: [self-hosted, linux, arm64]
    steps:
      - uses: actions/checkout@v4
      - run: ./tools/testing/kunit/kunit.py run --arch=arm64 --build_dir=.kunit --jobs=8 --json=kunit.json
      - uses: actions/upload-artifact@v4
        with: { name: kunit, path: kunit.json }
  boot-test:                               # 다음 층: 부팅 + kselftest + 모듈 KUnit
    needs: kunit
    runs-on: [self-hosted, linux, arm64]
    timeout-minutes: 60
    steps:
      - uses: actions/checkout@v4
      - run: make O=out ARCH=arm64 defconfig ci.config && make O=out -j8 Image modules
      - run: make O=out headers && make -C tools/testing/selftests O=$PWD/out TARGETS="timers gpio size" install INSTALL_PATH=$PWD/kst
      - run: ./ci/build_payload.sh && python3 ./ci/ci_boot_test.py
      - if: always()
        uses: actions/upload-artifact@v4
        with: { name: boot-logs, path: artifacts/ }
```

```text
# .gitlab-ci.yml  (가이드) — 같은 구조, 야간 HIL 단계 추가
stages: [unit, boot, hil]
kunit:
  stage: unit
  script: ./tools/testing/kunit/kunit.py run --arch=arm64 --jobs=8
boot-qemu:
  stage: boot
  script: [make O=out -j8 Image modules, ./ci/build_payload.sh, python3 ci/ci_boot_test.py]
  artifacts: { when: always, paths: [artifacts/], expire_in: 2 weeks }
hil-nightly:
  stage: hil
  rules: [{ if: '$CI_PIPELINE_SOURCE == "schedule"' }]
  tags: [hil-rig-01]                       # 리그가 붙은 전용 러너
  script: python3 ci/submit_lava_job.py --device-type my-board --payload artifacts/
```

설계 포인트: (1) 아티팩트는 실패했을 때 가장 필요하므로 `if: always()`/`when: always`. (2) 빌드 캐시(ccache, `.kunit` 디렉터리)가 KUnit 층을 "수 분"에서 "수십 초"로 만든다 — 예제 5의 빌드 1006초 vs 실행 1초를 기억하자. (3) HIL처럼 공유 자원은 전용 태그 러너나 LAVA 큐로 보낸다.

---

## 5. LAVA — 보드 팜을 위한 표준 (가이드, 실행 안 함)

LAVA(Linaro Automated Validation Architecture)는 "실제 보드 여러 대를 랙에 꽂아 두고, CI가 잡을 던지면 보드를 골라 이미지를 쓰고 부팅해 테스트를 돌려 결과를 돌려주는" 오픈소스 시스템이다. KernelCI의 여러 랩이 LAVA로 돈다. 이 랩에는 LAVA가 없어 실행하지 않았다. 아래 내용은 LAVA 문서(docs.lavasoftware.org)에서 기억한 구조다 — 필드 이름은 사용하는 LAVA 버전 문서로 확인하자.

| 개념 | 뜻 | Don 경험으로 |
|---|---|---|
| lava-server | 웹 UI, 스케줄러, 결과 DB | 랙 관리 서버 |
| lava-dispatcher (worker) | 보드에 물리적으로 붙은 PC. 시리얼·전원·TFTP 담당 | 랙 PC |
| device-type | 보드 종류별 Jinja2 템플릿 (부트로더 명령, 콘솔 프롬프트, 부트 방법) | "이 모델은 이렇게 부팅한다" 문서 |
| device dictionary | 보드 한 대의 고유 정보: 시리얼 연결 명령, 전원 on/off/reset 명령(PDU 포트) | 슬롯별 배선표 |
| job (YAML) | deploy → boot → test 액션의 나열 + 타임아웃 | 테스트 시퀀스 파일 |
| test definition | git 저장소의 YAML + 스크립트, `lava-test-case`로 결과 보고 | 회귀 스크립트 |

잡의 형태(가이드):

```text
# LAVA job (가이드) — U-Boot + TFTP 로 부팅해서 kselftest 실행
device_type: my-imx8mp-board
job_name: p6 kselftest timers
timeouts: { job: { minutes: 30 }, action: { minutes: 10 } }
priority: medium
visibility: public
actions:
- deploy:
    to: tftp
    kernel: { url: https://ci.example/artifacts/Image, type: image }
    dtb:    { url: https://ci.example/artifacts/board.dtb }
    nfsrootfs: { url: https://ci.example/artifacts/rootfs.tar.xz, compression: xz }
- boot:
    method: u-boot
    commands: nfs
    prompts: ['root@board:~#']
    auto_login: { login_prompt: 'login:', username: root }
- test:
    definitions:
    - repository: https://git.example/ci/test-definitions.git
      from: git
      path: kselftest/timers.yaml
      name: kselftest-timers
```

LAVA가 해 주는 일의 핵심은 **"보드를 망가진 상태에서도 되살리는 능력"** 이다. 잡마다 PDU로 전원을 끊었다 켜고(hard reset), 부트로더 프롬프트를 가로채서 TFTP로 새 커널을 올리므로, 이전 잡이 커널을 패닉시켜도 다음 잡은 정상으로 시작한다. 직접 만든 리그(§6)도 이 성질을 반드시 가져야 한다.

---

## 6. HIL 테스트 벤치 — 실제 보드를 자동으로 다루는 장치들

### 6.1 리그의 구성

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" fill="currentColor"/>
    </marker>
  </defs>
  <rect x="20" y="130" width="150" height="100" rx="8" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="95" y="160" font-size="13" text-anchor="middle">테스트 호스트</text>
  <text x="95" y="180" font-size="12" text-anchor="middle">CI 러너 · LAVA</text>
  <text x="95" y="198" font-size="12" text-anchor="middle">pexpect · pyvisa</text>
  <text x="95" y="216" font-size="12" text-anchor="middle">TFTP · NFS</text>
  <rect x="440" y="120" width="200" height="120" rx="8" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="540" y="150" font-size="13" text-anchor="middle">DUT (시험 대상 보드)</text>
  <text x="540" y="172" font-size="12" text-anchor="middle">SoC + Linux</text>
  <text x="540" y="192" font-size="12" text-anchor="middle">UART · GPIO · I2C · CAN</text>
  <text x="540" y="212" font-size="12" text-anchor="middle">boot 핀 · reset 핀</text>
  <rect x="230" y="20" width="160" height="44" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="310" y="47" font-size="12" text-anchor="middle">네트워크 PDU · 릴레이</text>
  <rect x="230" y="80" width="160" height="44" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="310" y="107" font-size="12" text-anchor="middle">USB-UART (콘솔)</text>
  <rect x="230" y="140" width="160" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="310" y="167" font-size="12" text-anchor="middle">GPIO 자극 (MCU)</text>
  <rect x="230" y="200" width="160" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="310" y="227" font-size="12" text-anchor="middle">CAN 어댑터 · 센서 에뮬</text>
  <rect x="230" y="260" width="160" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="310" y="287" font-size="12" text-anchor="middle">scope · LA (SCPI/LAN)</text>
  <rect x="440" y="270" width="200" height="56" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="540" y="293" font-size="12" text-anchor="middle">프로그래머블 PSU</text>
  <text x="540" y="311" font-size="12" text-anchor="middle">전압 설정 · 전류 측정</text>
  <line x1="170" y1="150" x2="228" y2="45" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="170" y1="165" x2="228" y2="102" stroke="currentColor" marker-start="url(#ah3)" marker-end="url(#ah3)"/>
  <line x1="170" y1="180" x2="228" y2="162" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="170" y1="200" x2="228" y2="222" stroke="currentColor" marker-start="url(#ah3)" marker-end="url(#ah3)"/>
  <line x1="170" y1="220" x2="228" y2="282" stroke="currentColor" marker-start="url(#ah3)" marker-end="url(#ah3)"/>
  <line x1="390" y1="42" x2="438" y2="128" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="390" y1="102" x2="438" y2="150" stroke="currentColor" marker-start="url(#ah3)" marker-end="url(#ah3)"/>
  <line x1="390" y1="162" x2="438" y2="185" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="390" y1="222" x2="438" y2="208" stroke="currentColor" marker-start="url(#ah3)" marker-end="url(#ah3)"/>
  <line x1="390" y1="282" x2="438" y2="232" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah3)"/>
  <line x1="540" y1="270" x2="540" y2="242" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="170" y1="228" x2="438" y2="296" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah3)"/>
  <text x="20" y="345" font-size="12">실선 = 제어·데이터, 점선 = 측정(프로브·SCPI). 모든 장치는 호스트가 스크립트로 제어한다</text>
</svg>
```
그림 3 — HIL 리그의 전형적인 구성. 사람이 손으로 하던 "전원 껐다 켜기, 콘솔 보기, 핀 건드리기, scope 보기"를 각각 프로그래밍 가능한 장치로 바꾼 것이다.

| 장치 | 하는 일 | 자주 쓰는 것 (예시) | 자동화 인터페이스 |
|---|---|---|---|
| 전원 제어 | 하드 리셋, 전원 사이클, 브라운아웃 재현 | 네트워크 PDU, USB 릴레이 보드 | SNMP·HTTP·시리얼 명령 |
| 시리얼 콘솔 | 부트 로그·로그인·명령 | FTDI USB-UART, ser2net으로 TCP 공유 | `/dev/ttyUSBn`, telnet |
| 부트 모드·리셋 핀 | USB 복구 모드 진입(C2), 리셋 | 릴레이·GPIO로 boot 핀 당기기 | GPIO |
| 저장 매체 전환 | SD 카드를 호스트↔DUT로 전환해 이미지 쓰기 | SD 먹스 보드 | USB |
| GPIO 자극 | 인터럽트 핀 토글, 버튼, 센서 data-ready 흉내 | 작은 MCU, FT232H, 호스트 GPIO | libgpiod, 시리얼 명령 |
| 버스 상대 | CAN 노드·I2C 센서 에뮬레이터 | gs_usb CAN 어댑터, MCU I2C 슬레이브 | SocketCAN, 시리얼 |
| 측정 | 타이밍·전압·전류·주파수 | 오실로스코프, 로직 분석기, PSU | SCPI over LAN (VISA) |

### 6.2 HIL 테스트 스크립트의 모양 (가이드, 실행 안 함)

랩에는 실 장치가 없으므로 아래는 구조만 보여 준다. SCPI 명령 문법은 장비마다 다르다 — `*IDN?`, `*RST`는 IEEE 488.2 공통 명령이지만 `:MEASure:...` 계열은 벤더 매뉴얼을 따라야 한다(기억 기반).

```python
# hil_irq_latency.py (가이드) - GPIO 자극 -> DUT 인터럽트 -> 응답 GPIO 지연을 scope로 측정
import pyvisa, gpiod, time, statistics

rm = pyvisa.ResourceManager()
scope = rm.open_resource("TCPIP::10.0.0.50::INSTR")   # LAN 연결 scope
print(scope.query("*IDN?"))                            # 장비 확인 -> 로그에 남긴다
scope.write("*RST")
# 채널1 = 자극 핀, 채널2 = DUT 응답 핀, 채널1 상승 에지 트리거 (명령은 벤더 매뉴얼대로)

chip = gpiod.Chip("/dev/gpiochip0")                   # 호스트 쪽 자극 핀
req = chip.request_lines({17: gpiod.LineSettings(direction=gpiod.line.Direction.OUTPUT)},
                         consumer="hil")
lat = []
for _ in range(1000):
    req.set_value(17, gpiod.line.Value.ACTIVE); time.sleep(0.002)
    req.set_value(17, gpiod.line.Value.INACTIVE); time.sleep(0.008)
    lat.append(float(scope.query(":MEASure:DELay? CHAN1,CHAN2")))   # 예시 명령
print("p50 %.1f us, max %.1f us" % (statistics.median(lat) * 1e6, max(lat) * 1e6))
```

Don에게 이건 새 기술이 아니다 — RF bring-up에서 scope로 버스 타이밍을 보던 일을 **반복 가능하게** 만든 것뿐이다(M11). 차이는 결과를 사람이 보는 게 아니라 TAP 한 줄(`ok 1 irq_latency_p99_under_200us`)로 바꿔 CI에 넣는다는 점이다.

### 6.3 HIL에서만 잡히는 것

- 전원 시퀀스: 레귤레이터가 늦게 올라와 센서 probe가 `-EPROBE_DEFER` 루프에 빠진다(H3, G3).
- 콜드 부트 vs 웜 리셋 차이: 리셋으로는 안 나고 전원 차단 후에만 나는 DDR 트레이닝 실패.
- 장시간 안정성: 72시간 카메라 스트리밍 중 메모리 누수, 1만 번 전원 사이클 중 파일시스템 손상(J5).
- 실 센서 타이밍: IMU data-ready 인터럽트 지터, CAN 버스 부하 80%에서의 프레임 드롭(Q4, Q7).

---

## 7. 생산(공장) 테스트 설계

### 7.1 목적이 다르다

개발 테스트는 "설계가 맞나"를 묻고, 생산 테스트는 "**이 한 대가 설계대로 조립됐나**"를 묻는다. 그래서 질문이 달라진다.

| 항목 | 개발·CI 테스트 | 생산 테스트 |
|---|---|---|
| 대상 | 코드 변경 | 유닛 한 대 (시리얼 번호) |
| 실패의 의미 | 버그 | 부품 불량·납땜·조립 오류·보정 실패 |
| 시간 | 넉넉 (분~시간) | **takt time** 안 (보통 수십 초~수 분, 라인이 정함) |
| 판정 | pass/fail + 로그 | 측정값 vs **한계(limit)** + 측정값 자체를 저장 |
| 소프트웨어 | 개발 이미지 | 전용 **테스트 이미지**(빠른 부팅, 진단 도구, 보정 쓰기 권한) |
| 기록 | CI 아티팩트 | **추적성**: 시리얼·스테이션·지그·테스트 SW 버전·한계 버전·시각 → MES/DB |

### 7.2 설계 원칙

- **테스트 이미지**: initramfs 하나로 부팅(수 초), 필요한 드라이버와 진단 스크립트만. 끝나면 양산 이미지를 eMMC에 쓰고 보정 데이터(MAC, 센서 오프셋)를 별도 파티션·OTP에 기록한다(C2 공장 플래싱, L4 A/B 파티션).
- **한계는 데이터다**: 코드에 하드코딩하지 않고 버전 관리되는 파일. 레코드에 한계 리비전을 같이 남겨야 "그때 그 한계로 합격한 유닛"을 나중에 찾을 수 있다.
- **측정값을 저장한다**: pass/fail만 남기면 공정이 서서히 한계 쪽으로 밀리는 것(drift)을 못 본다. 값 분포를 보면 불량이 나기 전에 안다(Cpk).
- **시간 예산**: 단계마다 예산을 두고 넘으면 경고. 라인 전체 처리량이 테스트 스테이션에서 막히기 쉽다.
- **결정적이어야 한다**: flaky 생산 테스트는 양품을 버리거나(수율 손실) 재시험으로 시간을 버린다. 개발 CI의 quarantine 같은 것은 생산에 없다.

### 7.3 예제 10 — 한계 비교 + 추적 레코드 스크립트

무엇을 확인하나: 측정 → 한계 비교 → JSON 추적 레코드 → exit code의 뼈대. 측정 5개 중 3개(DDR 크기, CPU 수, rootfs 여유)는 실제로 시스템에서 읽고, 전압은 고정값(실제라면 IIO ADC, I2), 루프백은 pipe로 흉내 냈다.

```python
#!/usr/bin/env python3
"""p6_factory.py - 생산 테스트 스테이션 뼈대: 측정 -> 한계 비교 -> 추적 레코드 -> exit code"""
import json, os, platform, sys, time, uuid

LIMITS = {   # 이름: (하한, 상한, 단위, 시간 예산 s)  - 실제로는 버전 관리되는 파일
    "ddr_mib":      (1400, 1600, "MiB", 0.5),   # 1.5 GiB 보드 가정
    "cpu_count":    (2, 2, "cores", 0.1),   # 듀얼 코어 SoC 가정
    "rootfs_free_gb": (2.0, None, "GB", 0.2),
    "rail_3v3":     (3.135, 3.465, "V", 0.5),   # 3.3 V +-5 %
    "loopback_err": (0, 0, "errors", 1.0),
}

def meas_ddr_mib():
    kb = int(next(l for l in open("/proc/meminfo") if l.startswith("MemTotal")).split()[1])
    return kb / 1024

def meas_cpu_count():   return os.cpu_count()
def meas_rootfs_free_gb():
    st = os.statvfs("/"); return st.f_bavail * st.f_frsize / 1e9
def meas_rail_3v3():    return 3.312            # 실제: IIO ADC in_voltageN_raw x scale (I2 참고)
def meas_loopback_err():                        # 실제: UART/CAN loopback 지그로 N 바이트 왕복
    data = os.urandom(4096); r, w = os.pipe(); os.write(w, data)
    return sum(a != b for a, b in zip(data, os.read(r, 4096)))

def run(serial):
    rec = {"serial": serial, "station": platform.node(), "kernel": platform.release(),
           "test_image": "p6-factory-0.3", "limits_rev": "L-2026-10-18a",
           "run_id": str(uuid.uuid4()), "start": time.strftime("%FT%T%z"), "steps": []}
    for name, (lo, hi, unit, budget) in LIMITS.items():
        t = time.monotonic(); v = globals()["meas_" + name](); dt = time.monotonic() - t
        ok = (lo is None or v >= lo) and (hi is None or v <= hi)
        rec["steps"].append({"name": name, "value": round(v, 3), "lo": lo, "hi": hi,
                             "unit": unit, "pass": ok, "t_s": round(dt, 4),
                             "over_budget": dt > budget})
        print(f"{name:<15} {v:10.3f} {unit:<7} [{lo}, {hi}] {'PASS' if ok else 'FAIL'}")
    rec["pass"] = all(s["pass"] for s in rec["steps"])
    os.makedirs("/tmp/P6", exist_ok=True)
    with open(f"/tmp/P6/{serial}_{rec['run_id'][:8]}.json", "w") as f:
        json.dump(rec, f, indent=1)               # MES/DB 로 올릴 추적 레코드
    print("RESULT", "PASS" if rec["pass"] else "FAIL", "->", f.name)
    return 0 if rec["pass"] else 1

if __name__ == "__main__":
    sys.exit(run(sys.argv[1] if len(sys.argv) > 1 else "SN-TEST-0001"))
```

먼저 **틀린 유닛**에 돌린 경우 — 이 스크립트를 VM 호스트(12 GB, 6코어)에서 실행했다:

```text
ddr_mib          11924.863 MiB     [1400, 1600] FAIL
cpu_count            6.000 cores   [2, 2] FAIL
rootfs_free_gb      16.672 GB      [2.0, None] PASS
rail_3v3             3.312 V       [3.135, 3.465] PASS
loopback_err         0.000 errors  [0, 0] PASS
RESULT FAIL -> /tmp/P6/SN-P6-000123_aa675c5f.json
exit=1
```

출력에서 볼 것: 메모리와 코어 수가 한계 밖이다. 공장에서 이런 결과는 "다른 SKU 보드가 이 스테이션에 들어왔다" 또는 "DDR 칩이 다른 용량으로 실장됐다(BOM 오류)"를 뜻한다. 한계에 **상한**도 두는 이유다 — "충분히 크면 OK"가 아니다. 레코드 JSON의 한 단계는 이렇게 생겼다(앞 버전 실행에서 읽은 실제 내용):

```text
{'serial': 'SN-P6-000123', 'kernel': '7.0.0-38-generic', 'limits_rev': 'L-2026-10-18a', 'pass': False}
{'name': 'ddr_mib', 'value': 11924.863, 'lo': 1400, 'hi': 1600, 'unit': 'MiB', 'pass': False, 't_s': 0.0002, 'over_budget': False}
```

같은 스크립트를 **맞는 유닛**(QEMU 게스트, 1536 MiB, 2코어)에서 돌린 결과는 §4.3 두 번째 CI 실행의 `factory` 문서에 있다.

---

## 8. 측정으로 본 피라미드 — 층별 피드백 시간

지금까지 이 노트에서 실제로 잰 시간을 한 그림에 모았다.

```svg
<svg viewBox="0 0 680 328" xmlns="http://www.w3.org/2000/svg">
  <text x="20" y="22" font-size="13">층별 피드백 시간 (초, 로그 눈금) — TCG + 공유 부하, 실 HW 수치 아님</text>
  <text x="240" y="57" font-size="12" text-anchor="end">kunit.py exec (빌드 없음)</text>
  <rect x="250" y="44" width="51.0" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
  <text x="307.0" y="58" font-size="12" text-anchor="start">2.7 s</text>
  <text x="240" y="91" font-size="12" text-anchor="end">KUnit 13 스위트 + initramfs 부팅</text>
  <rect x="250" y="78" width="188.8" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
  <text x="444.8" y="92" font-size="12" text-anchor="start">39.6 s</text>
  <text x="240" y="125" font-size="12" text-anchor="end">CI 잡: KUnit 단계</text>
  <rect x="250" y="112" width="180.5" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
  <text x="436.5" y="126" font-size="12" text-anchor="start">33.7 s</text>
  <text x="240" y="159" font-size="12" text-anchor="end">CI 잡: kselftest 7개 단계</text>
  <rect x="250" y="146" width="284.7" height="20" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
  <text x="540.7" y="160" font-size="12" text-anchor="start">257 s</text>
  <text x="240" y="193" font-size="12" text-anchor="end">CI 잡 전체 (2차, 부하 중간)</text>
  <rect x="250" y="180" width="318.5" height="20" fill="#888" fill-opacity="0.35" stroke="#888"/>
  <text x="574.5" y="194" font-size="12" text-anchor="start">496 s</text>
  <text x="240" y="227" font-size="12" text-anchor="end">kunit.py run (빌드 포함)</text>
  <rect x="250" y="214" width="356.0" height="20" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
  <text x="600.0" y="228" font-size="12" text-anchor="end">1030 s</text>
  <text x="240" y="261" font-size="12" text-anchor="end">CI 잡 전체 (1차, 부하 최고)</text>
  <rect x="250" y="248" width="366.3" height="20" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
  <text x="610.3" y="262" font-size="12" text-anchor="end">1260 s</text>
  <line x1="250" y1="284" x2="640" y2="284" stroke="currentColor"/>
  <line x1="250.0" y1="284" x2="250.0" y2="289" stroke="currentColor"/>
  <line x1="250.0" y1="40" x2="250.0" y2="284" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="250.0" y="304" font-size="12" text-anchor="middle">1</text>
  <line x1="368.1" y1="284" x2="368.1" y2="289" stroke="currentColor"/>
  <line x1="368.1" y1="40" x2="368.1" y2="284" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="368.1" y="304" font-size="12" text-anchor="middle">10</text>
  <line x1="486.3" y1="284" x2="486.3" y2="289" stroke="currentColor"/>
  <line x1="486.3" y1="40" x2="486.3" y2="284" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="486.3" y="304" font-size="12" text-anchor="middle">100</text>
  <line x1="604.4" y1="284" x2="604.4" y2="289" stroke="currentColor"/>
  <line x1="604.4" y1="40" x2="604.4" y2="284" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="604.4" y="304" font-size="12" text-anchor="middle">1000</text>
</svg>
```
그림 4 — 이 노트의 실행에서 잰 피드백 시간(초, 로그 눈금). 출처: 예제 2(`real 39.6 s`), 예제 5(`kunit.py` 빌드 포함 1030 s, `exec` 2.7 s), 예제 9의 1차·2차 실행(`P6_STAGE` 타임스탬프 차이: KUnit 단계 217.33 − 183.65 = 33.7 s, kselftest 단계 474.55 − 217.33 = 257 s).

읽는 법:
- 로그 눈금으로 봐야 할 만큼 층 사이 차이가 크다. 같은 KUnit이라도 "빌드된 커널로 다시 돌리기"(2.7 s)와 "처음부터 빌드"(1030 s)는 400배 차이다. **CI 설계의 대부분은 캐시 설계**다.
- kselftest 단계 257 s 중 120 s는 `sync_test` 타임아웃 하나가 먹었다. 테스트 하나의 타임아웃 정책이 잡 전체 시간을 좌우한다.
- 같은 CI 잡이 1260 s → 496 s로 흔들렸다(공유 VM 부하 차이). 시간 숫자는 같은 조건끼리만 비교하고, 성능 회귀는 전용 HW에서 잰다(N3).

---

## 9. 커널 안에서는 무슨 일이 일어나나 — KUnit 실행 경로

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" fill="currentColor"/>
    </marker>
  </defs>
  <rect x="20" y="20" width="200" height="56" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="120" y="44" font-size="13" text-anchor="middle">insmod p6_kunit_test.ko</text>
  <text x="120" y="63" font-size="12" text-anchor="middle">.kunit_test_suites 섹션</text>
  <rect x="250" y="20" width="200" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="350" y="44" font-size="13" text-anchor="middle">kunit_module_notify</text>
  <text x="350" y="63" font-size="12" text-anchor="middle">MODULE_STATE_LIVE</text>
  <rect x="480" y="20" width="180" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="570" y="44" font-size="13" text-anchor="middle">kunit_module_init</text>
  <text x="570" y="63" font-size="12" text-anchor="middle">filter_glob · filter</text>
  <line x1="220" y1="48" x2="248" y2="48" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="450" y1="48" x2="478" y2="48" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="570" y1="76" x2="570" y2="108" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="440" y="110" width="220" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="550" y="134" font-size="13" text-anchor="middle">kunit_run_tests(suite)</text>
  <text x="550" y="153" font-size="12" text-anchor="middle">add_taint(TAINT_TEST)</text>
  <line x1="440" y1="138" x2="372" y2="138" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="150" y="110" width="220" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="260" y="134" font-size="13" text-anchor="middle">케이스(·파라미터)마다</text>
  <text x="260" y="153" font-size="12" text-anchor="middle">kunit_try_catch_run</text>
  <line x1="260" y1="166" x2="260" y2="198" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="60" y="200" width="400" height="96" rx="8" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="260" y="222" font-size="13" text-anchor="middle">kthread "kunit_try_catch"</text>
  <text x="260" y="244" font-size="12" text-anchor="middle">suite→init(test) → case(test) → suite→exit(test)</text>
  <text x="260" y="264" font-size="12" text-anchor="middle">EXPECT 실패: test→status = FAILURE, 계속</text>
  <text x="260" y="284" font-size="12" text-anchor="middle">ASSERT 실패 · Oops: kthread 종료 → catch</text>
  <rect x="490" y="200" width="170" height="96" rx="8" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="575" y="222" font-size="13" text-anchor="middle">부모 실행기</text>
  <text x="575" y="244" font-size="12" text-anchor="middle">wait_for_completion</text>
  <text x="575" y="262" font-size="12" text-anchor="middle">_timeout (기본 300 s)</text>
  <text x="575" y="282" font-size="12" text-anchor="middle">자원 정리 (LIFO)</text>
  <line x1="460" y1="248" x2="488" y2="248" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="260" y1="296" x2="260" y2="328" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="60" y="330" width="240" height="52" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="180" y="352" font-size="13" text-anchor="middle">printk: KTAP 줄</text>
  <text x="180" y="370" font-size="12" text-anchor="middle">dmesg · 시리얼 콘솔</text>
  <rect x="330" y="330" width="330" height="52" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="495" y="352" font-size="13" text-anchor="middle">suite→log (string_stream)</text>
  <text x="495" y="370" font-size="12" text-anchor="middle">/sys/kernel/debug/kunit/p6_ring/results</text>
  <line x1="300" y1="356" x2="328" y2="356" stroke="currentColor" stroke-dasharray="4 3"/>
</svg>
```
그림 5 — `insmod`한 KUnit 모듈이 실행되는 경로(`lib/kunit/test.c`, `try-catch.c`, `debugfs.c`, 6.18.55 기준).

소스를 따라가면:

1. `kunit_test_suite(p6_suite)`는 suite 포인터 배열을 ELF 섹션 `.kunit_test_suites`에 넣는다(`include/kunit/test.h`의 `__section(".kunit_test_suites")`). built-in이면 vmlinux의 그 섹션을 부팅 중 executor가 순회하고(예제 2의 부팅 로그 `KTAP version 1` / `1..0`이 그것 — built-in 스위트가 0개였다), 모듈이면 모듈 로더가 그 섹션 위치를 `struct module`의 `kunit_suites`에 기록한다.
2. KUnit 코어는 `register_module_notifier()`로 모듈 상태 변화를 구독한다. 모듈이 살아나면 `kunit_module_notify` → `kunit_module_init`이 `filter_glob`·`filter`로 걸러 낸 뒤 `kunit_run_tests()`를 부른다. 그래서 **insmod가 곧 테스트 실행**이고 KTAP이 insmod 호출 스레드(예제 3의 T76) 이름으로 찍힌다.
3. `kunit_run_tests()`는 시작하면서 `add_taint(TAINT_TEST)` — 예제 2에서 본 `N`이다. 모듈 로더도 `MODULE_INFO(test, "Y")`가 붙은 모듈(modpost가 `tools/testing/` 아래 모듈, 즉 kselftest용 테스트 모듈에 자동으로 붙인다)을 로드할 때 같은 taint를 건다(`kernel/module/main.c`, `scripts/mod/modpost.c`).
4. 케이스마다 `kunit_try_catch_run()`이 **새 kthread**(`kunit_try_catch`)를 만들고 `wait_for_completion_timeout()`으로 기다린다. kthread 안에서 `init` → 케이스 → `exit`가 돈다. `KUNIT_ASSERT_*` 실패는 `__kunit_abort()` → `kunit_try_catch_throw()` → `kthread_exit()`로 **그 kthread만 끝낸다**. C에는 예외가 없으니 "스레드를 끝내는 것"이 throw이고, 부모가 completion을 받는 것이 catch다. NULL 역참조 같은 Oops도 그 kthread를 죽이므로 부모는 계속 간다(예제 2의 `kunit_fault`). 타임아웃이면 부모가 `kthread_stop()`한다.
5. `kunit_kzalloc()`·`kunit_add_action()`으로 등록한 자원은 케이스가 끝난 뒤 등록 역순으로 해제된다 — `devm_*`(G3)와 같은 LIFO 정리다.
6. 결과는 두 곳으로 간다: `printk`(KTAP 줄)와, `CONFIG_KUNIT_DEBUGFS`이면 suite·케이스별 `string_stream` 로그. 후자가 `debugfs_print_results()`로 `results` 파일이 된다.

드라이버를 쓸 때 다시 만나는 지점: 드라이버의 순수 로직을 `VISIBLE_IF_KUNIT`로 노출해 테스트하고(E3의 `EXPORT_SYMBOL` 네임스페이스와 같은 장치), 가짜 `struct device`가 필요하면 `kunit_device_register()`, 레지스터가 필요하면 regmap RAM 백엔드(H2). 테스트를 `.init` 섹션 코드(부팅 1회용 파서 등)에 대해 돌리려면 `kunit_test_init_section_suites()`를 쓰는데, 이 경우 debugfs `run` 재실행은 없다(예제 4).

---

## 10. Embedded 실무에서 다시 보기

### 10.1 센서 키트 드라이버에 KUnit 붙이기

예를 들어 센서 키트의 IMU 드라이버(I2, Q5)라면, HW 없이 테스트할 수 있는 부분이 생각보다 많다.

- FIFO 프레임 파서: "헤더 1바이트 + 가속 6 + 자이로 6" 프레임 스트림에서 프레임 경계가 어긋났을 때 재동기화하는가.
- 원시값 → SI 변환: 풀스케일 레인지(±2g/±16g)마다 `scale`이 맞는가, 음수 경계(−32768)에서 부호 확장이 맞는가.
- 타임스탬프 보간: 인터럽트 타임스탬프 하나와 FIFO 샘플 N개로 각 샘플 시각을 계산할 때, ODR 오차와 wrap-around가 맞는가.

이것들을 `static` 함수로 두되 `VISIBLE_IF_KUNIT`로 표시하고, 드라이버 디렉터리에 `xxx_kunit.c` + Kconfig `XXX_KUNIT_TEST`(`depends on KUNIT`, `default KUNIT_ALL_TESTS`)를 둔다. 업스트림 리뷰어가 좋아하는 형태이고, 리뷰에서 "이 파서 테스트했나요?"에 KTAP 한 줄로 답할 수 있다. 레지스터 접근 경로까지 보고 싶으면 regmap을 RAM 백엔드로 갈아 끼운다 — 예제 2의 `regmap` 408 케이스가 바로 그 방식이다.

### 10.2 BSP 업그레이드 회귀 (P5와 연결)

벤더 커널 5.15 → 6.6 LTS 이동 같은 작업에서 가장 무서운 것은 "조용히 바뀐 동작"이다. 최소 세트를 정해 두자: (1) 부트 테스트 — 모든 보드 변형이 셸까지, `dmesg`에 새 `WARNING`/`BUG` 없음, 기대 드라이버가 전부 probe(`/sys/bus/*/drivers/*/` 목록 diff). (2) kselftest 중 제품이 쓰는 서브시스템(gpio, timers, net, rtc). (3) 자체 KUnit 스위트. (4) HIL 스모크(카메라 스트림 1분, CAN 왕복, IMU 샘플레이트). 업그레이드 전 커널로 같은 세트를 돌려 **기준선**을 먼저 만들어야 "원래부터 실패하던 테스트"와 회귀를 구분할 수 있다.

### 10.3 "probe 됐나" 부트 테스트를 셸 한 줄로

부트 테스트에서 가장 값싼 고가치 검사는 "기대한 장치가 다 있나"다. 예를 들어 센서 키트 보드라면:

```sh
# 가이드: 기대 목록과 실제 바인딩을 비교해 TAP으로 출력 (보드마다 목록 파일)
n=0; while read -r path; do n=$((n+1))
  if [ -e "$path" ]; then echo "ok $n $path"; else echo "not ok $n $path"; fi
done < /etc/expected_devices.txt; echo "1..$n"
```

목록 예: `/sys/bus/i2c/drivers/bmi160_i2c/1-0068`, `/dev/video0`, `/sys/class/net/can0`, `/sys/class/pps/pps0`. TAP의 계획 줄(`1..N`)은 끝에 와도 된다. 이런 검사는 DT를 고친 뒤(D4, P4) 노드 하나가 `status = "disabled"`로 남은 실수를 바로 잡는다.

### 10.4 공장 테스트와 OTA의 만남

생산 테스트 이미지는 마지막에 양산 이미지를 A/B 슬롯 양쪽에 쓰고(L4), 첫 부팅 확인(`bootcount` 리셋)까지 하고 끝나는 경우가 많다. 그래서 생산 테스트 스테이션은 사실상 "업데이트 시스템의 첫 사용자"다. OTA 경로의 버그(예: B 슬롯 서명 검증 실패, L5)가 공장에서 처음 드러나는 일이 흔하다.

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 테스트 모듈 insmod의 exit code로 판정 | 실패한 KUnit이 CI에서 녹색 | 테스트 실패와 모듈 로드는 별개(예제 3 `rc=0`) | KTAP(`results` 파일)을 파싱해서 판정 |
| dmesg 전체를 `kunit.py parse`에 넣기 | `0 tests run!` 또는 첫 모듈만 집계 | 부팅 시 빈 KTAP 문서 + 모듈마다 새 문서(예제 6) | 스위트별 debugfs `results`를 따로 수집 |
| `echo` 줄바꿈 포함으로 `filter_glob` 설정 | 아무 테스트도 안 돌고 메시지도 없음 | `\n`이 glob에 포함(§2.6) | `echo -n`, 그리고 "0개 실행 = 실패" 규칙 |
| kselftest를 읽기 전용 위치에서 실행 | `Can't create file: Read-only file system` | 일부 테스트가 cwd에 임시 파일 생성(kcmp) | 쓰기 가능한 곳(`/tmp`)으로 복사 후 실행 |
| 타임아웃 없이 kselftest 실행 | 잡 하나가 몇 시간 멈춤 | `timers/settings`가 `timeout=0` | `run_kselftest.sh -o N`, CI 잡 전체에도 상한 |
| 타이밍 테스트를 공유·가상 러너에서 게이트로 사용 | 매번 다른 테스트가 랜덤 실패 → 아무도 CI를 안 믿음 | CPU 시간 타이머·지연 측정이 호스트 부하에 좌우(§4.3 `posix_timers`) | quarantine(티켓·만료일) + 전용 HW에서 측정 |
| expect가 자기가 보낸 명령 에코에 매칭 | 명령이 끝나기 전에 "완료"로 판단, 출력이 잘림 | 프롬프트 문자열이 명령 안에 들어 있음 | 고유 프롬프트 + 에코 한 번 소비 또는 `stty -echo` |
| 이전 실행의 rootfs 재사용 | 어제는 되던 테스트가 오늘 실패, 재현 불가 | 상태가 누적(로그, 설정, 반쯤 쓴 파일) | 매 실행 새 qcow2 overlay / 이미지 재기록 |
| 생산 테스트에 하한만 둠 | 다른 SKU·BOM 오류가 통과 | "크면 OK" 논리(예제 10의 12 GB) | 상한·하한 모두, 한계 리비전 기록 |
| 양산 이미지에 KUnit/테스트 모듈 포함 | 커널 taint `N`, 테스트 코드가 공격 표면 | `KUNIT_ALL_TESTS` 같은 설정이 제품 config로 샘 | 제품 config에서 `CONFIG_KUNIT` 끔, config diff를 CI에서 검사 |

---

## 12. 면접에서 이렇게 말한다

**Q.** How do you test a Linux device driver?

**A.** 층으로 나눠 답한다. HW 없는 로직(파서·변환·상태 머신)은 KUnit으로, 레지스터 경로는 regmap RAM 백엔드나 가짜 장치(i2c-stub, gpio-sim)로, 유저 인터페이스는 kselftest 스타일의 uAPI 테스트로, 실제 타이밍·전원은 HIL 리그로. 각 층은 TAP으로 결과를 내고 CI가 합친다.

> I split it by what each layer can catch. Pure logic — frame parsers, unit conversion, timestamp math — goes into KUnit, which runs in seconds under QEMU. For register paths I swap in a regmap RAM backend or use fake devices like i2c-stub or gpio-sim so the real driver probes. The user-facing ABI gets kselftest-style tests, and anything timing- or power-related goes to a HIL rig with relay power control, serial console and a scope. Everything reports TAP, so one CI parser gates the merge.

**꼬리 질문**: "KUnit 테스트가 Oops를 내면?" → 케이스가 별도 kthread에서 돌아서 그 케이스만 실패하고 나머지는 계속, 커널은 taint된다(§9).

**Q.** What's the difference between KUNIT_EXPECT and KUNIT_ASSERT?

**A.** EXPECT는 실패를 기록하고 계속, ASSERT는 그 케이스를 즉시 끝낸다. 구현은 kthread 종료다 — C에 예외가 없어서 KUnit은 케이스를 별도 kthread에서 돌리고, ASSERT 실패 시 그 스레드를 `kthread_exit`으로 끝내 부모가 completion으로 "catch"한다. 뒤 코드가 의미 없을 때(할당 실패, NULL)만 ASSERT를 쓴다.

> EXPECT records the failure and keeps going; ASSERT aborts the current case. Since C has no exceptions, KUnit runs each case in its own kthread and an ASSERT failure just exits that thread — the parent waits on a completion with a timeout, so it acts as the catch. I use ASSERT only when continuing makes no sense, like a failed allocation, so one test can still report several independent EXPECT failures.

**Q.** How would you set up CI for an embedded Linux product without enough boards?

**A.** QEMU로 커버할 수 있는 것은 전부 QEMU로: 커널+rootfs 빌드, 매 실행 새 overlay로 부팅, KUnit·kselftest·자체 테스트를 돌려 TAP 파싱 후 exit code로 게이트. 보드는 공유 팜(LAVA)에 묶어 야간·릴리스 후보에만 같은 테스트 payload를 보낸다. QEMU가 모르는 것 — 핀먹스, 클럭, DDR, 실 센서 — 은 그 층에서만 본다.

> Everything QEMU can cover runs per merge request: build kernel and rootfs, boot a fresh qcow2 overlay, run KUnit, kselftest and our own tests, parse TAP, and gate on the exit code with console logs as artifacts. Real boards sit in a shared farm — LAVA or a small home-grown rig with PDU and serial servers — and get the same test payload nightly and for release candidates. That keeps scarce hardware for what QEMU can't model: pinmux, clocks, DDR, real sensors and power sequencing.

**Q.** How do you handle flaky tests?

**A.** 먼저 진짜 flaky인지 결정적인지 구분한다(같은 지점에서 멈추나 매번 다른가). 확인된 것만 quarantine에 넣되, 결과는 계속 수집하고 티켓과 만료일을 단다. 타이밍 테스트는 공유 가상 러너에서 게이트로 쓰지 않고 전용 HW로 옮긴다. 이 노트에서 posix_timers는 부하 민감, sync_test는 두 번 다 같은 지점에서 멈춰 성격이 달랐다.

> First I classify: does it fail randomly, or deterministically in one environment? I've seen a timer test fail only under host overload, and a stress test hang at the same subtest twice — those are different problems. Confirmed flaky tests go on a quarantine list with an owner and an expiry date; they still run and get reported, they just don't block merges. Timing-sensitive tests move to dedicated hardware, because a CI that fails randomly trains people to ignore red.

**Q.** What does a good production (factory) test look like?

**A.** 목표는 설계 검증이 아니라 "이 유닛이 제대로 조립됐나"를 takt time 안에 판정하는 것. 빠른 부팅 테스트 이미지, 버전 관리되는 상·하한 한계, pass/fail만이 아니라 측정값 저장, 시리얼·스테이션·SW·한계 리비전을 묶은 추적 레코드. 결정적이어야 하고, 마지막에 양산 이미지와 보정 데이터를 쓴다. SSD 양산에서 MP 테스트를 운영한 경험이 그대로 이어진다.

> It answers "was this unit built correctly" within the line's takt time, not "is the design right". A fast-booting test image, versioned upper and lower limits, and we store the measured values, not just pass/fail, so we can watch process drift. Every record carries serial number, station, test-image version and limits revision for traceability. It must be deterministic, and it usually ends by writing the production image and calibration data. That's the same loop I ran for SSD manufacturing tests.

**Q.** What is KTAP and why does it matter for CI?

**A.** KTAP은 커널 쪽 TAP 변형으로, 하위 테스트를 들여쓰기로 중첩하고 SKIP 같은 지시어를 정의한다. KUnit과 kselftest가 모두 TAP 계열이라 CI는 파서 하나로 층별 결과를 합칠 수 있다. 다만 모듈마다 문서가 새로 시작되므로 문서 단위로 수집해야 한다.

> KTAP is the kernel's variant of TAP: nested subtests by indentation, explicit SKIP and diagnostic lines. KUnit emits it and kselftest emits TAP 13, so one parser can merge results across layers. The practical catch is that every loaded test module starts a new document, so I collect per-suite results from debugfs instead of scraping the whole dmesg.

**Q.** What would you put in a HIL rig for a robot sensor board?

**A.** 원격 전원(PDU/릴레이)으로 하드 리셋과 전원 사이클, 시리얼 콘솔 서버, boot 핀·리셋 핀 제어(복구 모드), GPIO 자극용 MCU(센서 인터럽트 흉내), CAN 어댑터, LAN scope와 프로그래머블 PSU. 핵심 요구는 "패닉한 보드를 사람 없이 되살릴 수 있는가"다.

> Remote power through a PDU or relays for hard resets and power cycling, a serial console server, control of reset and boot-mode pins so a bricked board can go into recovery mode, a small MCU for GPIO stimulus to fake sensor interrupts, a CAN adapter, plus a LAN scope and a programmable supply for timing and current. The key requirement is unattended recovery — a board that panicked must come back without someone walking to the lab.

---

## 13. 직접 해보기

1. (개념) 다음 버그 각각을 피라미드의 어느 층에서 잡는 게 가장 싼가: (a) CRC 테이블 생성 오류, (b) 레귤레이터 지연으로 인한 센서 probe 실패, (c) `ioctl`의 에러 코드가 `-EINVAL` 대신 `-EFAULT`, (d) 커넥터 냉납, (e) DT에서 노드 하나가 `disabled`. 정답/힌트: (a) KUnit, (b) HIL(전원 시퀀스), (c) kselftest 스타일 uAPI 테스트, (d) 생산 테스트, (e) 부트 테스트의 "기대 장치 목록"(§10.3).
2. (코드) 예제 3의 스위트에 `KUNIT_CASE` 하나를 추가해 `p6_ring_put()`이 꽉 찬 링에서 `-ENOSPC`를 돌려주고 **기존 데이터를 덮어쓰지 않는지** 검사하라. `KUNIT_EXPECT_MEMEQ`로 버퍼 전체를 비교하면 실패 시 hexdump가 나온다. 정답/힌트: put 7번 후 8번째가 `-ENOSPC`인지, 그 뒤 get 7번이 원래 순서인지.
3. (코드) 예제 3에 `.suite_init`을 추가해 suite 전체에서 한 번만 쓰는 lookup table을 만들고, 케이스에서 `kunit_skip()`으로 "테이블이 없으면 건너뛰기"를 구현하라. KTAP에 `# SKIP` 줄이 어떻게 나오는지 확인. 정답/힌트: `struct kunit_suite`의 `.suite_init`은 `int (*)(struct kunit_suite *)`.
4. (관찰) KUnit 모듈을 하나 insmod한 직후 `cat /proc/sys/kernel/tainted`를 읽고 비트를 해석하라. 예제 2의 `262272`에서 `D` 비트가 왜 켜졌는지 설명하라. 정답/힌트: `kunit_fault` 스위트의 의도된 NULL 역참조 Oops. 비트 표는 `Documentation/admin-guide/tainted-kernels.rst`.
5. (CI) 예제 9의 파서를 고쳐 KUnit 스위트 안의 **케이스 수**(들여쓰기 4칸 `ok`)도 세고, JUnit XML을 하나 더 아티팩트로 쓰게 하라(대부분의 CI 웹 UI가 JUnit을 표로 보여 준다). 정답/힌트: 들여쓰기 깊이로 suite/case를 구분, `xml.etree.ElementTree`로 `testsuite`/`testcase`/`failure`.
6. (kselftest) `gpio:gpio-sim.sh`를 같은 방식으로 게스트에서 돌려 보라. 무엇이 필요한가? 정답/힌트: configfs 마운트(`/sys/kernel/config`), `CONFIG_GPIO_SIM`(랩 커널은 `=y`), gpio-sim 칩 이름은 다른 작성자와 겹치지 않게.
7. (설계) 하루 200대를 테스트해야 하는 공장 라인이 있다. 스테이션 2대, 3교대 중 1교대(8시간)만 가동. 유닛당 테스트 시간 예산은 최대 몇 초인가? 플래싱이 90초 걸린다면 남는 시간은? 정답/힌트: 2 × 8 × 3600 / 200 = 288초, 플래싱 후 198초 — 여기서 로딩·언로딩 시간과 재시험 비율을 빼야 한다.

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| KUnit | Kernel unit testing framework | 커널 안에서 도는 xUnit, 결과는 KTAP |
| suite / case | 테스트 묶음 / 테스트 함수 | `struct kunit_suite` / `struct kunit_case` |
| EXPECT / ASSERT | 계속 / 중단 | ASSERT는 케이스 kthread를 끝낸다 |
| KTAP | Kernel TAP | 들여쓰기로 중첩하는 커널판 TAP |
| TAP | Test Anything Protocol | `ok N name` / `not ok N name` 줄 형식 |
| kunit.py | KUnit 래퍼 | config → 빌드 → UML/QEMU 실행 → 파싱 |
| kselftest | 커널 셀프테스트 | `tools/testing/selftests`의 유저 공간 uAPI 테스트 |
| kselftest_harness.h | 테스트 하네스 | `TEST`/`FIXTURE`/`ASSERT_*`, 테스트마다 fork |
| LTP | Linux Test Project | syscall·POSIX 의미의 대규모 테스트 모음 |
| LAVA | Linaro Automated Validation Architecture | 보드 팜 스케줄러·디스패처 |
| device-type / device dictionary | 보드 종류 템플릿 / 한 대의 배선 정보 | LAVA의 보드 기술 방식 |
| HIL | Hardware-in-the-loop | 실 HW와 자동화 장비를 엮은 테스트 |
| PDU | Power Distribution Unit | 네트워크로 콘센트 전원을 켜고 끄는 장치 |
| SCPI | Standard Commands for Programmable Instruments | scope·PSU를 텍스트 명령으로 제어 |
| DUT | Device Under Test | 시험 대상 보드 |
| quarantine | 격리 목록 | 실패해도 게이트를 막지 않지만 계속 보고 |
| takt time | 공정 박자 | 라인이 유닛 하나를 내보내는 목표 간격 |
| traceability | 추적성 | 시리얼 ↔ 측정값·SW·한계 버전 연결 |
| taint `N` | TAINT_TEST | 테스트를 돌린 커널이라는 표시 |
| qcow2 overlay | 쓰기 시 복사 디스크 | 원본은 읽기 전용, 매 실행 깨끗한 상태 |

---

## 15. 요약 & 체크리스트

임베디드 Linux의 테스트는 피라미드다. 바닥의 KUnit은 커널 안에서 순수 로직을 수 초 만에 검증하고(케이스마다 kthread, EXPECT는 계속·ASSERT는 중단, 결과는 KTAP으로 dmesg와 debugfs에), 그 위의 kselftest·LTP는 uAPI를, 부트 테스트는 "셸까지 가는가"를, HIL은 실제 전원·타이밍·센서를, 생산 테스트는 유닛 한 대의 조립 품질을 시간 예산 안에 판정한다. CI의 핵심은 세 가지다: 매번 깨끗한 상태(qcow2 overlay)에서 시작하고, 결과를 문서 단위로 TAP 파싱해 exit code로 게이트를 여닫고, 실패를 종류별로 구분한다 — 이 노트의 첫 CI 실행에서는 진짜 버그(링 버퍼 off-by-one), 환경 문제(읽기 전용 cwd), 부하 민감 타이밍, 결정적으로 느린 스트레스 테스트가 한꺼번에 나왔고, 각각 다르게 처리했다.

- [ ] 피라미드의 각 층이 무엇을 잡고 무엇을 못 잡는지, 실행 시간과 HW 요구를 설명할 수 있다
- [ ] `KUNIT_CASE`·`KUNIT_CASE_PARAM`·`.init`·`kunit_kzalloc`을 쓴 KUnit 스위트를 짜서 out-of-tree 모듈로 빌드할 수 있다
- [ ] EXPECT와 ASSERT의 차이와 그 구현(kthread + completion)을 설명할 수 있다
- [ ] KTAP 실패 리포트(파일:줄, 식, 양쪽 값)를 읽고 버그를 짚을 수 있다
- [ ] debugfs `results`/`run`과 `filter_glob`으로 결과를 다시 읽고 일부만 돌릴 수 있다
- [ ] `kunit.py run/exec/parse`의 역할과 비용 구조(빌드 ≫ 실행)를 말할 수 있다
- [ ] kselftest 하위 집합을 빌드·설치하고 `run_kselftest.sh -t/-o`로 돌릴 수 있다
- [ ] `kselftest_harness.h`로 유저 공간 uAPI 테스트를 짤 수 있다
- [ ] QEMU를 expect 방식으로 운전해 TAP을 파싱하고 exit code를 돌려주는 CI 잡을 짤 수 있다
- [ ] flaky 테스트를 분류하고 quarantine 정책을 설명할 수 있다
- [ ] LAVA의 device-type·device dictionary·job 구조와 HIL 리그의 구성 요소를 설명할 수 있다
- [ ] 생산 테스트의 한계·추적성·시간 예산을 설계할 수 있다

---

## 참고 자료

- docs.kernel.org — KUnit: https://docs.kernel.org/dev-tools/kunit/index.html (소스 트리 `Documentation/dev-tools/kunit/`: `start.rst`, `usage.rst`, `run_manual.rst`, `running_tips.rst`, `architecture.rst`)
- docs.kernel.org — KTAP 형식: https://docs.kernel.org/dev-tools/ktap.html (`Documentation/dev-tools/ktap.rst`)
- docs.kernel.org — kselftest: https://docs.kernel.org/dev-tools/kselftest.html (`Documentation/dev-tools/kselftest.rst`)
- docs.kernel.org — Tainted kernels: https://docs.kernel.org/admin-guide/tainted-kernels.html
- 커널 소스 6.18.55: `lib/kunit/test.c`(executor, 모듈 notifier, taint, 타임아웃), `lib/kunit/try-catch.c`(kthread try/catch), `lib/kunit/debugfs.c`(results/run), `lib/kunit/kunit-example-test.c`(API 예제 모음), `include/kunit/test.h`·`visibility.h`·`static_stub.h`, `drivers/base/regmap/regmap-kunit.c`, `tools/testing/kunit/kunit.py`·`qemu_configs/arm64.py`, `tools/testing/selftests/kselftest_harness.h`·`run_kselftest.sh`·`kselftest/runner.sh` — elixir.bootlin.com 에서 같은 경로로 볼 수 있다
- LAVA 문서: https://docs.lavasoftware.org/lava/ (job 정의, device-type, device dictionary)
- KernelCI: https://kernelci.org/ (LAVA 랩들을 묶은 업스트림 커널 CI)
- LTP: https://github.com/linux-test-project/ltp
- pexpect 문서: https://pexpect.readthedocs.io/
- 관련 노트: E3(모듈), G8(debugfs), H2(regmap), H4(gpio-mockup/sim), L4(OTA), M3(Oops), M8 §3(KASAN KUnit), M10(부팅 분석), M11(HW 연계 디버깅), N3(지연 측정), P1(bring-up), P5(업그레이드)

