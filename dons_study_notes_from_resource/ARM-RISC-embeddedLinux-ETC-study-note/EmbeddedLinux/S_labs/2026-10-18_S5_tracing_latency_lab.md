# S5. 트레이싱 실습 — IRQ→유저 지연을 끝에서 끝까지 재고, ftrace로 증명하고, PREEMPT vs PREEMPT_RT를 비교하기

> **이 노트를 다 읽으면**: "알려진 시각에 IRQ를 일으키고 hard IRQ에서 타임스탬프를 찍는" 측정 모듈과 SCHED_FIFO 대기 스레드를 직접 만들어 IRQ→핸들러→깨어남→유저 구간을 쪼개 잴 수 있다 · 그 경로를 trace-cmd 이벤트, function_graph, kprobe + hist 트리거 + synthetic event로 "실제로 그 길로 갔다"고 증명할 수 있다 · cyclictest·rtla timerlat으로 Image-trace(PREEMPT)와 Image-rt(PREEMPT_RT)를 idle/stress-ng/나쁜 드라이버 부하, CPU 격리 유무로 같은 조건에서 비교하고 그 숫자를 정직하게 설명할 수 있다 · 이 전부를 GitHub 저장소 `irq-latency-lab` + 3분 데모로 보여 줄 수 있다
> **왜 필요한가**: 로보틱스·드론·국방·차량 면접에서 "PREEMPT_RT 써 봤나요? 지연은 얼마였고 어떻게 쟀나요? 어디서 시간이 샜는지 어떻게 알았나요?"는 거의 정해진 질문 순서다. 이 프로젝트는 그 세 질문에 "직접 만든 측정기 + 트레이스 증거 + 비교 실험"으로 답하게 해 준다
> **Don 기준 난이도**: SSD 펌웨어에서 ISR 진입·태스크 깨어남 시각을 GPIO 토글과 로직 분석기로 쟀던 감각, "최악값이 스펙"이라는 감각은 이미 있다 / 새로 손에 붙일 것은 Linux 쪽 계약(hrtimer HARD 모드, swait, threaded IRQ, PREEMPT_RT에서 `spinlock_t`가 잠드는 락이 되는 것)과 ftrace의 고급 기능(hist 트리거, synthetic event, kprobe event)이다
> **선행 노트**: M5(ftrace — §5.5 hist 트리거, §5.6 synthetic event, §6 kprobe, §11 trace-cmd), N1(지연 사슬 §3, PREEMPT_RT가 바꾸는 것 §5), N2(CPU 격리 §6, IRQ affinity §8), N3(cyclictest·rtla 측정법, TCG 경고 §0.1, 공정 비교 §10), F6(인터럽트 처리), F8(hrtimer), F3(sleep/wakeup), F4(locking), G7(misc device), H4(GPIO), S1(QEMU 랩), S2(첫 char driver)

---

## 0. 큰 그림 — 무엇을 만들고 무엇을 증명하나

### 0.1 목표 한 줄

"타이머가 정확히 t에 터지도록 예약했다. 내 RT 스레드는 유저 공간에서 언제 돌기 시작했나? 그 차이는 어느 구간에서 생겼나? PREEMPT_RT는 그중 어느 구간을 줄이나?" — 이것을 **직접 만든 측정기**로 재고, **ftrace로 경로를 증명**하고, **표준 도구(cyclictest, rtla timerlat)로 교차 확인**한다.

Don의 SSD 경험으로 바꾸면 이렇다. 펌웨어에서 "NVMe doorbell → ISR 진입 → 명령 처리 태스크 깨어남"을 잴 때 ISR 첫 줄과 태스크 첫 줄에서 GPIO를 토글하고 로직 분석기로 봤다. 여기서는 GPIO 대신 `ktime_get_ns()`(CLOCK_MONOTONIC) 타임스탬프를 4곳에서 찍고, 로직 분석기 대신 ftrace가 "그 사이에 CPU가 무엇을 했나"를 보여 준다.

### 0.2 최종 결과물

| 결과물 | 위치 | 면접에서 보여 줄 것 |
|---|---|---|
| Git 저장소 `irq-latency-lab` | `~/.elinux_scratch/S5/irq-latency-lab/` (커밋 2개) | README, 모듈 2개, 유저 프로그램 2개, 게스트 스크립트, `plot.py` |
| 측정 모듈 `s5_irqlat.ko` | `kmod/` | hrtimer HARD + swait로 hard IRQ에서 깨우기 |
| 나쁜 드라이버 `s5_hog.ko` | `kmod/` | `spinlock_t` 4 ms 보유 — PREEMPT_RT가 없애는 지연원 |
| RT 대기 스레드 `s5_waiter`, GPIO 경로 `s5_gpio` | `user/` | SCHED_FIFO 90 + `mlockall` + CPU 고정 |
| ftrace 증명 스크립트 | `guest/phase_ftrace.sh` | trace-cmd, function_graph, kprobe + hist + synthetic |
| 결과 표와 SVG 8개 | `results/summary.txt`, `docs/*.svg` | "어느 구간이 움직였나" 그림 |

먼저 결과부터 보자. 아래 그림이 이 프로젝트의 한 장 요약이다(실측, QEMU TCG).

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="24" font-size="13">idle, 각 구간의 p50 (us)</text>
<text x="122" y="53" font-size="12" text-anchor="end">PREEMPT</text>
<rect x="130.0" y="38" width="177.7" height="22" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="218.8" y="53" font-size="12" text-anchor="middle">irq 174</text>
<rect x="307.7" y="38" width="89.0" height="22" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<text x="352.2" y="53" font-size="12" text-anchor="middle">wake 87</text>
<rect x="396.7" y="38" width="29.2" height="22" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<text x="431.8" y="53" font-size="12">= 290 us</text>
<text x="122" y="83" font-size="12" text-anchor="end">PREEMPT_RT</text>
<rect x="130.0" y="68" width="223.8" height="22" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="241.9" y="83" font-size="12" text-anchor="middle">irq 219</text>
<rect x="353.8" y="68" width="85.8" height="22" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<text x="396.7" y="83" font-size="12" text-anchor="middle">wake 84</text>
<rect x="439.6" y="68" width="17.4" height="22" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<text x="463.0" y="83" font-size="12">= 321 us</text>
<line x1="130.0" y1="96" x2="130.0" y2="100" stroke="currentColor"/>
<text x="130.0" y="113" font-size="12" text-anchor="middle">0</text>
<line x1="232.0" y1="96" x2="232.0" y2="100" stroke="currentColor"/>
<text x="232.0" y="113" font-size="12" text-anchor="middle">100</text>
<line x1="334.0" y1="96" x2="334.0" y2="100" stroke="currentColor"/>
<text x="334.0" y="113" font-size="12" text-anchor="middle">200</text>
<line x1="436.0" y1="96" x2="436.0" y2="100" stroke="currentColor"/>
<text x="436.0" y="113" font-size="12" text-anchor="middle">300</text>
<line x1="538.0" y1="96" x2="538.0" y2="100" stroke="currentColor"/>
<text x="538.0" y="113" font-size="12" text-anchor="middle">400</text>
<line x1="640.0" y1="96" x2="640.0" y2="100" stroke="currentColor"/>
<text x="640.0" y="113" font-size="12" text-anchor="middle">500</text>
<line x1="130" y1="96" x2="640" y2="96" stroke="currentColor"/>
<text x="10" y="142" font-size="13">s5_hog 부하, total 상위 1% 표본의 구간별 평균 (us)</text>
<text x="122" y="171" font-size="12" text-anchor="end">PREEMPT</text>
<rect x="130.0" y="156" width="86.6" height="22" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="173.3" y="171" font-size="12" text-anchor="middle">irq 849</text>
<rect x="216.6" y="156" width="386.5" height="22" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<text x="409.8" y="171" font-size="12" text-anchor="middle">wake 3789</text>
<rect x="603.1" y="156" width="11.4" height="22" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<text x="122" y="201" font-size="12" text-anchor="end">PREEMPT_RT</text>
<rect x="130.0" y="186" width="195.4" height="22" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="227.7" y="201" font-size="12" text-anchor="middle">irq 1916</text>
<rect x="325.4" y="186" width="69.0" height="22" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<text x="359.9" y="201" font-size="12" text-anchor="middle">wake 676</text>
<rect x="394.4" y="186" width="10.1" height="22" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<text x="410.5" y="201" font-size="12">= 2691 us</text>
<line x1="130.0" y1="214" x2="130.0" y2="218" stroke="currentColor"/>
<text x="130.0" y="231" font-size="12" text-anchor="middle">0</text>
<line x1="232.0" y1="214" x2="232.0" y2="218" stroke="currentColor"/>
<text x="232.0" y="231" font-size="12" text-anchor="middle">1000</text>
<line x1="334.0" y1="214" x2="334.0" y2="218" stroke="currentColor"/>
<text x="334.0" y="231" font-size="12" text-anchor="middle">2000</text>
<line x1="436.0" y1="214" x2="436.0" y2="218" stroke="currentColor"/>
<text x="436.0" y="231" font-size="12" text-anchor="middle">3000</text>
<line x1="538.0" y1="214" x2="538.0" y2="218" stroke="currentColor"/>
<text x="538.0" y="231" font-size="12" text-anchor="middle">4000</text>
<line x1="640.0" y1="214" x2="640.0" y2="218" stroke="currentColor"/>
<text x="640.0" y="231" font-size="12" text-anchor="middle">5000</text>
<line x1="130" y1="214" x2="640" y2="214" stroke="currentColor"/>
</svg>
```

그림 1 — 측정한 지연 경로의 구간별 시간(실측). 위: idle에서 각 구간의 p50. 아래: 나쁜 드라이버(`s5_hog`) 부하에서 end-to-end 상위 1% 표본만 골라 구간별 평균을 쌓았다(PREEMPT 행의 합계는 약 4750 µs라 오른쪽 라벨이 생략됨). PREEMPT에서는 꼬리가 거의 전부 **wake 구간**(hard IRQ → 대기 스레드가 다시 실행)에 있고, PREEMPT_RT에서는 그 구간이 사라진다. 숫자 자체는 에뮬레이터 숫자다(아래 경고).

### 0.3 설계 결정 — 테스트 모듈인가, gpio-sim인가

과제는 "알려진 시각에 IRQ를 일으키고 hard IRQ 핸들러에서 타임스탬프를 찍는 것"이다. 후보는 둘이었고, **둘 다 만들었다**. 역할이 다르기 때문이다.

| 기준 | A. 자체 모듈 `s5_irqlat` (hrtimer) | B. gpio-sim + GPIO chardev v2 |
|---|---|---|
| IRQ 발생 시각 | **정확히 안다** — 예약한 만료 시각 `hrtimer_get_expires()` | 모른다 — sysfs `pull` write 직전 유저 시각이 대용 |
| hard IRQ 타임스탬프 | 직접 찍음 (`s5_timer_fn`) | gpiolib-cdev가 `edge_irq_handler()`에서 찍어 `timestamp_ns`로 넘김 |
| 커널 코드 작성 | 108줄 모듈 | 0줄 (gpio-sim은 Image-trace/rt에 built-in) |
| 실제 드라이버 경로와의 닮음 | 타이머 IRQ → 깨우기. timerlat/cyclictest와 같은 구조 | 센서 DRDY GPIO와 같은 경로: irq_sim → hard IRQ → **threaded IRQ** → kfifo → 깨우기 |
| 잴 수 있는 구간 | irq / wake / ret / total 4개 | trig→hardirq, hardirq→user 2개 |

결론: **"IRQ latency"의 정확한 원점이 필요하면 A**, **실제 드라이버 경로(threaded IRQ 포함)를 보여 주려면 B**. 면접 설명도 이 표 그대로 하면 된다. 실제 보드에서는 B의 "트리거"가 진짜 GPIO 엣지가 되고, 원점은 scope로 잰다(M11).

### 0.4 꼭 기억할 경고 — TCG 숫자

> **TCG 경고**: 모든 측정은 Mac(M2) → Lima VM `elinux`(호스트 커널 `7.0.0-38-generic`) → QEMU 10.2.1 TCG 게스트에서 했다. CPU가 소프트웨어로 에뮬레이션되고 vCPU는 호스트 스레드다. 그래서 p50이 수백 µs, max가 수십 ms인 것은 **실제 하드웨어 성능이 아니다**. 이 노트에서 믿을 것은 (1) 측정 방법, (2) 같은 파라미터에서의 **상대 비교와 분포의 모양**, (3) 트레이스가 원인을 가리키는 방식이다. 실제 i.MX8·AM62·Orin이라면 같은 명령으로 수 µs~수십 µs가 나온다. N3 §0.1, N1 §6과 같은 결론이고, 이 노트 §10에서 그 결론을 이번 데이터로 다시 확인한다.

---

## 1. 준비물과 저장소 레이아웃

### 1.1 준비물

| 항목 | 이 노트에서 쓴 것 | 확인 방법 |
|---|---|---|
| VM | Lima `elinux`, aarch64, kernel `7.0.0-38-generic`, gcc 15.2 | `uname -r` |
| 커널 | `/var/lab/out/Image-trace` (6.18.55-trace), `Image-rt` (6.18.55-rt) | 아래 예제 1 |
| 모듈 빌드 트리 | `/var/lab/out/linux-build-trace`, `linux-build-rt` | vermagic이 다르다 |
| 게스트 rootfs | `rootfs-trace.ext4` 위의 **qcow2 overlay** (복사 금지) | trace-cmd 3.2.0, cyclictest 2.50, rtla 6.18.55, stress-ng 0.17.06 |
| QEMU | `qemu-system-aarch64` 10.2.1, `-smp 2 -m 1G` | `--version` |

### 1.2 예제 1 — 두 커널의 차이를 먼저 확인한다

무엇을 확인하나: 비교 대상 두 커널이 무엇이 같고 무엇이 다른지. 비교 실험은 "다른 것이 정확히 무엇인가"부터 적는다(N3 §10.1).

```sh
uname -r
for k in trace rt; do echo "== $k"; grep -E "^CONFIG_(PREEMPT|PREEMPT_RT|HZ|NO_HZ_FULL|RT_GROUP_SCHED|GPIO_SIM|9P_FS|HIST_TRIGGERS|SYNTH_EVENTS|KPROBE_EVENTS|TIMERLAT_TRACER)=" /var/lab/out/linux-build-$k/.config | tr "\n" " "; echo; done
```

```text
7.0.0-38-generic
== trace
CONFIG_PREEMPT=y CONFIG_RT_GROUP_SCHED=y CONFIG_HZ=250 CONFIG_GPIO_SIM=y CONFIG_9P_FS=y CONFIG_TIMERLAT_TRACER=y CONFIG_KPROBE_EVENTS=y CONFIG_SYNTH_EVENTS=y CONFIG_HIST_TRIGGERS=y 
== rt
CONFIG_NO_HZ_FULL=y CONFIG_PREEMPT=y CONFIG_PREEMPT_RT=y CONFIG_HZ=1000 CONFIG_GPIO_SIM=y CONFIG_9P_FS=y CONFIG_TIMERLAT_TRACER=y CONFIG_KPROBE_EVENTS=y CONFIG_SYNTH_EVENTS=y CONFIG_HIST_TRIGGERS=y 
```

출력에서 볼 것: 차이는 **PREEMPT_RT, HZ(250 vs 1000), NO_HZ_FULL(rt만), RT_GROUP_SCHED(trace만)** 넷이다. `PREEMPT_DYNAMIC`이 없으므로 trace 커널을 부트 인자로 voluntary로 바꿀 수는 없다 — 이 노트의 "non-RT"는 full PREEMPT다. 보고서에는 "PREEMPT_RT 효과"가 아니라 "이 네 가지 차이의 합"이라고 적어야 정직하다. gpio-sim, 9p, hist/synthetic/kprobe 이벤트는 둘 다 built-in이라 추가 모듈이 필요 없다.

### 1.3 저장소 레이아웃

```
irq-latency-lab/
  README.md
  kmod/     s5_irqlat.c  s5_hog.c  Kbuild  Makefile     (make K=trace / K=rt)
  user/     s5_waiter.c  s5_gpio.c                      (static build in the VM)
  guest/    init.sh (PID 1)  lib_load.sh  phase_smoke.sh  phase_matrix.sh
            phase_ftrace.sh  phase_gpio.sh  phase_rtgroup.sh  phase_isocheck.sh
  host/     prepare.sh  run_guest.sh  run_matrix.sh  run_extra.sh
            initramfs_init.sh  plot.py
  results/  summary.txt  matrix.log  raw_outputs.tgz
  docs/     8 SVG charts
```

소스는 Mac 쪽 `~/.elinux_scratch/S5/irq-latency-lab/`(VM에서 같은 절대 경로로 읽기 전용)에 있고, 빌드 산출물·게스트 이미지·결과는 VM의 `/var/tmp/S5/`에 만든다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<rect x="10" y="30" width="200" height="280" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="110" y="22" font-size="13" text-anchor="middle">Lima VM (elinux, 호스트 역할)</text>
<rect x="20" y="45" width="180" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="110" y="70" font-size="12" text-anchor="middle">prepare.sh (빌드)</text>
<rect x="20" y="100" width="180" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="110" y="125" font-size="12" text-anchor="middle">run_guest.sh (QEMU)</text>
<rect x="20" y="155" width="180" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="110" y="180" font-size="12" text-anchor="middle">plot.py (표 + SVG)</text>
<rect x="20" y="220" width="180" height="75" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="110" y="240" font-size="12" text-anchor="middle">9p 공유 /var/tmp/S5/share</text>
<text x="110" y="260" font-size="12" text-anchor="middle">bin/ ko/ guest/</text>
<text x="110" y="280" font-size="12" text-anchor="middle">out/ (결과)</text>
<rect x="250" y="30" width="420" height="280" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="460" y="22" font-size="13" text-anchor="middle">QEMU 게스트 (TCG, -smp 2, -m 1G)</text>
<rect x="265" y="45" width="185" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="357" y="70" font-size="12" text-anchor="middle">Image-trace 또는 Image-rt</text>
<rect x="470" y="45" width="185" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="562" y="70" font-size="12" text-anchor="middle">initramfs /init</text>
<rect x="470" y="105" width="185" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="562" y="130" font-size="12" text-anchor="middle">switch_root → PID 1</text>
<rect x="265" y="105" width="185" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="357" y="122" font-size="12" text-anchor="middle">/dev/vda = qcow2</text>
<text x="357" y="140" font-size="12" text-anchor="middle">overlay of rootfs-trace</text>
<rect x="265" y="170" width="390" height="125" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="460" y="190" font-size="12" text-anchor="middle">phase_*.sh — CPU1에서 측정</text>
<text x="280" y="215" font-size="12">insmod s5_irqlat.ko · (s5_hog.ko)</text>
<text x="280" y="237" font-size="12">s5_waiter / s5_gpio — FIFO 90, mlockall</text>
<text x="280" y="259" font-size="12">cyclictest · rtla timerlat · stress-ng</text>
<text x="280" y="281" font-size="12">trace-cmd · tracefs hist/synthetic/kprobe</text>
<line x1="195" y1="120" x2="262" y2="70" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="450" y1="65" x2="467" y2="65" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="562" y1="85" x2="562" y2="102" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="562" y1="145" x2="562" y2="167" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="262" y1="255" x2="198" y2="255" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="110" y1="85" x2="110" y2="97" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="110" y1="217" x2="110" y2="198" stroke="currentColor" marker-end="url(#ah1)"/>
</svg>
```

그림 2 — 실험 장치 전체. VM이 호스트 역할을 하고, 게스트는 매번 새로 부팅해 측정 스크립트를 PID 1로 돌린 뒤 결과를 9p 공유 디렉터리에 쓰고 스스로 꺼진다.

---

## 2. Step 1 — 측정 모듈 `s5_irqlat`: 알려진 시각의 hard IRQ

### 2.1 무엇을 만드나

- misc device `/dev/s5_irqlat` (G7). `open()`하면 hrtimer가 `period_us`(기본 1000) 주기로 돈다.
- 타이머 콜백 `s5_timer_fn()`은 **hard IRQ 문맥**에서 돈다. 여기서 `t_hardirq`를 찍고, 예약 만료 시각 `t_expire`와 함께 저장한 뒤 대기자 하나를 깨운다.
- `read()`는 다음 만료까지 잠들고, 깨어나 커널 안에서 다시 돌기 시작한 시각 `t_kwake`를 찍어 `struct s5_sample`을 돌려준다. 유저는 `read()`가 돌아온 직후 `t_user`를 찍는다.

구간 정의는 이것이 전부다.

| 구간 | 식 | 이 구간에 들어 있는 것 |
|---|---|---|
| irq | `t_hardirq − t_expire` | 타이머 HW → GIC → 예외 진입 → `hrtimer_interrupt` → 콜백 호출. **IRQ off 구간**이 여기를 늘린다 |
| wake | `t_kwake − t_hardirq` | `try_to_wake_up` → IRQ 종료 → (선점 가능해질 때까지 대기) → `__schedule` → 대기자 재개. **preempt off 구간, 비선점 락**이 여기를 늘린다 |
| ret | `t_user − t_kwake` | 샘플 복사, `copy_to_user`, syscall 복귀 |
| total | `t_user − t_expire` | cyclictest가 재는 것과 같은 의미의 end-to-end |

### 2.2 코드 (1/2) — 타이머 콜백

무엇을 확인하는 코드인가: hard IRQ 문맥에서 타임스탬프를 찍고, PREEMPT_RT에서도 합법적인 방법으로 대기자를 깨운다.

```c
// SPDX-License-Identifier: GPL-2.0
/* s5_irqlat: an hrtimer fires at a KNOWN absolute time, its callback runs in
 * hard-IRQ context (HRTIMER_MODE_ABS_PINNED_HARD keeps it there even on
 * PREEMPT_RT), stamps itself and wakes ONE reader blocked in read(). */
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/hrtimer.h>
#include <linux/swait.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

struct s5_sample { u64 seq, t_expire, t_hardirq, t_kwake; u32 cpu, missed; };

static unsigned int period_us = 1000;
module_param(period_us, uint, 0444);

static struct hrtimer s5_timer;
static DECLARE_SWAIT_QUEUE_HEAD(s5_wq);	/* raw lock: legal in hardirq on RT */
static DEFINE_RAW_SPINLOCK(s5_lock);
static struct s5_sample s5_last;
static u64 s5_seq, s5_read_seq;
static atomic_t s5_busy = ATOMIC_INIT(0);

static noinline enum hrtimer_restart s5_timer_fn(struct hrtimer *t)
{
	u64 now = ktime_get_ns();		/* == CLOCK_MONOTONIC */

	raw_spin_lock(&s5_lock);
	if (s5_seq == s5_read_seq) {	/* keep the OLDEST unread expiry */
		s5_last.t_expire = ktime_to_ns(hrtimer_get_expires(t));
		s5_last.t_hardirq = now;
		s5_last.cpu = smp_processor_id();
	}
	s5_seq++;
	raw_spin_unlock(&s5_lock);
	swake_up_one(&s5_wq);
	hrtimer_forward_now(t, us_to_ktime(period_us));
	return HRTIMER_RESTART;
}
```

세 가지 결정이 핵심이다.

1. **`HRTIMER_MODE_ABS_PINNED_HARD`**. PREEMPT_RT에서는 모드에 `_HARD`가 없는 hrtimer 콜백이 hard IRQ가 아니라 softirq(`ktimers/N` 스레드 문맥)로 밀려난다. 그러면 "hard IRQ 타임스탬프"가 거짓말이 된다. `_HARD`는 두 커널 모두에서 콜백을 hard IRQ에 둔다. `_PINNED`는 타이머를 `open()`한 CPU에 고정한다(대기자를 먼저 CPU1에 고정하므로 타이머도 CPU1).
2. **`wait_queue_head_t`가 아니라 swait**. 일반 wait queue는 `spinlock_t`로 보호되는데, PREEMPT_RT에서 `spinlock_t`는 잠들 수 있는 rt_mutex 기반 락이라 hard IRQ에서 잡으면 안 된다. swait(simple wait queue)는 `raw_spinlock_t`라 hard IRQ에서 깨워도 된다. completion이 내부에서 swait를 쓰는 이유도 이것이다(F3, N1 §5).
3. **가장 오래된 미처리 만료를 보존**. 대기자가 늦어서 여러 주기를 놓치면 "처음 놓친 만료"부터 재야 진짜 지연이다. 이 줄을 처음에 안 넣었다가 실제로 틀린 결과를 얻었다(§2.6).

`ktime_get_ns()`는 CLOCK_MONOTONIC과 같은 시계다. 그래서 유저가 `clock_gettime(CLOCK_MONOTONIC)`으로 찍은 값과 그대로 뺄 수 있다. gpiolib-cdev의 `timestamp_ns`도 기본값이 같은 시계다.

### 2.3 코드 (2/2) — read()와 open()

```c
static ssize_t s5_read(struct file *f, char __user *buf, size_t len, loff_t *o)
{
	struct s5_sample s;
	u64 kwake;
	int ret;

	if (len < sizeof(s))
		return -EINVAL;
	ret = swait_event_interruptible_exclusive(s5_wq,
			READ_ONCE(s5_seq) != s5_read_seq);
	if (ret)
		return ret;
	raw_spin_lock_irq(&s5_lock);
	kwake = ktime_get_ns();			/* reader is running again */
	s = s5_last;
	s.seq = s5_seq;
	s.missed = s5_seq - s5_read_seq - 1;	/* periods we slept through */
	s5_read_seq = s5_seq;
	raw_spin_unlock_irq(&s5_lock);
	s.t_kwake = kwake;
	return copy_to_user(buf, &s, sizeof(s)) ? -EFAULT : sizeof(s);
}

static int s5_open(struct inode *i, struct file *f)
{
	if (atomic_cmpxchg(&s5_busy, 0, 1))
		return -EBUSY;			/* one waiter at a time */
	s5_seq = s5_read_seq = 0;
	/* PINNED: the timer stays on the opener's CPU (pin the waiter first) */
	hrtimer_start(&s5_timer, ktime_add_us(ktime_get(), period_us),
		      HRTIMER_MODE_ABS_PINNED_HARD);
	return 0;
}

static int s5_release(struct inode *i, struct file *f)
{
	hrtimer_cancel(&s5_timer);
	atomic_set(&s5_busy, 0);
	return 0;
}
```

나머지(`file_operations`, `miscdevice`, `hrtimer_setup()` 호출, `module_init/exit`)는 S2·G7의 misc 드라이버와 같아서 생략했다(저장소 `kmod/s5_irqlat.c` 전체 108줄). 6.18에서는 타이머 초기화가 `hrtimer_init()` + `.function` 대입이 아니라 `hrtimer_setup(&s5_timer, s5_timer_fn, CLOCK_MONOTONIC, HRTIMER_MODE_ABS_PINNED_HARD)` 한 번이다(6.18 소스에는 `hrtimer_init()`이 더 이상 없다 — `hrtimer.h`를 grep해 확인).

`kwake`를 **락을 잡고 IRQ를 끈 뒤에** 찍는 것도 일부러다. 락 밖에서 찍으면 찍은 직후 다음 hard IRQ가 끼어들어 `s5_last`를 바꿀 수 있고, 그러면 `t_kwake < t_hardirq`가 되어 음수(부호 없는 정수라 거대한 수)가 나온다. 이것도 실제로 밟았다(§2.6).

### 2.4 빌드

`Makefile`은 소스를 `/var/tmp/S5/build-<k>`로 복사해 빌드한다(Mac 홈은 VM에서 읽기 전용이라 `M=`을 소스 디렉터리로 줄 수 없다).

```make
# Build for one lab kernel: make K=trace  (or K=rt). Output goes to /var/tmp/S5/build-$(K)
K ?= trace
KDIR ?= /var/lab/out/linux-build-$(K)
OUT ?= /var/tmp/S5/build-$(K)
all:
	mkdir -p $(OUT) && cp Kbuild s5_irqlat.c s5_hog.c $(OUT)/
	$(MAKE) -C $(KDIR) M=$(OUT) modules
clean:
	rm -rf $(OUT)
```

```sh
cd /Users/donh/.elinux_scratch/S5/irq-latency-lab/kmod && make K=rt clean && make K=rt
modinfo /var/tmp/S5/build-rt/s5_irqlat.ko | grep -E "^(name|vermagic|parm|description)"
```

```text
mkdir -p /var/tmp/S5/build-rt && cp Kbuild s5_irqlat.c s5_hog.c /var/tmp/S5/build-rt/
make -C /var/lab/out/linux-build-rt M=/var/tmp/S5/build-rt modules
make[1]: Entering directory '/var/lab/build/linux-rt'
make[2]: Entering directory '/var/tmp/S5/build-rt'
  CC [M]  s5_irqlat.o
  CC [M]  s5_hog.o
  MODPOST Module.symvers
  CC [M]  s5_irqlat.mod.o
  CC [M]  .module-common.o
  LD [M]  s5_irqlat.ko
  CC [M]  s5_hog.mod.o
  LD [M]  s5_hog.ko
make[2]: Leaving directory '/var/tmp/S5/build-rt'
make[1]: Leaving directory '/var/lab/build/linux-rt'
description:    S5 known-time hard-IRQ -> reader wakeup latency probe
name:           s5_irqlat
vermagic:       6.18.55-rt SMP preempt_rt mod_unload aarch64
parm:           period_us:uint
```

같은 소스를 `K=trace`로 빌드하면 vermagic이 `6.18.55-trace SMP preempt mod_unload aarch64`가 된다. 두 빌드 모두 경고 0개였다.

**체크포인트**: 이 단계가 끝나면 `/var/tmp/S5/build-trace/`와 `build-rt/`에 `s5_irqlat.ko`, `s5_hog.ko`가 있고, `modinfo -F vermagic`이 각각 `preempt`, `preempt_rt`를 보여야 한다.

### 2.5 흔한 실패

| 증상 | 원인 | 고치는 법 |
|---|---|---|
| `insmod: Invalid module format`, dmesg에 `version magic` | trace용 .ko를 rt 커널에 넣음 | 커널마다 따로 빌드, `ko/<uname -r>/`로 나눠 두기 |
| `make: ... Permission denied` (M= 경로) | Mac 홈은 VM에서 읽기 전용 | `/var/tmp/S5/build-<k>`로 복사해서 빌드 |
| `implicit declaration of function 'hrtimer_init'` | 6.18에서 제거된 API | `hrtimer_setup()` |
| RT 커널에서 `BUG: sleeping function called from invalid context` | hard IRQ에서 `wake_up()`(spinlock_t) 사용 | swait 또는 `wake_up_process()` |

### 2.6 실제로 밟은 버그 두 개 — 측정기가 틀리면 결론이 틀린다

**버그 1: 음수 wake.** 첫 버전은 `kwake = ktime_get_ns()`를 락 밖에서 찍었다. 첫 스모크 실행(Image-trace, 2000 표본)의 실제 출력:

```text
samples 2000 on cpu 1 prio 90, missed periods 20
irq       p50  183.4  p99  1164.2  p99.9  6202.9  max  25581.3 us
wake      p50   93.1  p99   535.9  p99.9   942.8  max 18446744073709528.0 us
ret       p50   27.3  p99   198.9  p99.9   553.1  max    666.6 us
total     p50  313.2  p99  1578.5  p99.9  6850.1  max  26397.8 us
```

wake max가 2^64에 가까운 수다 — `t_kwake − t_hardirq`가 음수가 되어 부호 없는 정수로 감겼다. 원인: `kwake`를 찍은 뒤 락을 잡기 전 사이에 다음 hard IRQ가 들어와 `s5_last`를 새 값으로 바꿨다. 고침: IRQ를 끄고 락을 잡은 뒤에 `kwake`를 찍는다(§2.3 코드).

**버그 2: 최악을 못 보는 측정기.** 둘째 버전은 콜백이 매번 `s5_last`를 덮어썼다. 대기자가 4 ms 늦으면 그동안 만료가 4번 쌓이는데, 대기자는 **가장 최근** 만료 기준으로 wake를 계산한다. 그래서 `s5_hog`(4 ms 락 보유)를 걸어도 이렇게 나왔다(Image-trace, 30000 표본, 실제 출력):

```text
wake      p50  166.2  p99   899.1  p99.9  2739.1  max  38376.6 us
```

같은 조건에서 "가장 오래된 미처리 만료 보존"으로 고친 뒤:

```text
samples 30000 on cpu 1 prio 90, missed periods 2135
wake      p50   76.1  p99  3894.3  p99.9  4109.4  max  11708.5 us
```

p99가 899 µs에서 **3894 µs**로 바뀌었다 — 4 ms 락이 비로소 보인다. 그리고 `missed periods 2135`가 "대기자가 주기를 통째로 놓쳤다"는 사실을 따로 알려 준다. 교훈: **측정기는 "늦은 표본"을 버리거나 덮어쓰는 순간 최악값을 숨긴다.** cyclictest가 다음 예정 시각을 절대 시각으로 들고 다니며 늦은 만큼을 그대로 보고하는 이유가 이것이다(N3 §2). 같은 함정이 ftrace synthetic event에서도 다시 나온다(§6.5).

---

## 3. Step 2 — 유저 RT 대기 스레드 `s5_waiter`

### 3.1 코드

무엇을 확인하는 코드인가: SCHED_FIFO 90, `mlockall`, CPU1 고정으로 `read()`에 잠든 스레드가 표본마다 4구간을 계산하고, 끝에 백분위를 출력하며 원자료를 CSV로 남긴다.

```c
// s5_waiter: SCHED_FIFO + mlockall thread pinned to one CPU, blocked in
// read(/dev/s5_irqlat). Prints percentiles of 4 segments, writes CSV.
#define _GNU_SOURCE
#include <fcntl.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

struct s5_sample { uint64_t seq, t_expire, t_hardirq, t_kwake; uint32_t cpu, missed; };
#define DIE(m) do { perror(m); exit(1); } while (0)
static int cmp(const void *a, const void *b)
{ uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return x > y ? 1 : x < y ? -1 : 0; }
static void pct(const char *name, uint64_t *v, int n)
{
	qsort(v, n, sizeof(*v), cmp);
	printf("%-9s p50 %6.1f  p99 %7.1f  p99.9 %7.1f  max %8.1f us\n", name,
	       v[n / 2] / 1e3, v[n * 99 / 100] / 1e3, v[n * 999 / 1000] / 1e3, v[n - 1] / 1e3);
}
int main(int argc, char **argv)
{
	int cpu = argc > 1 ? atoi(argv[1]) : 1, prio = argc > 2 ? atoi(argv[2]) : 90;
	int n = argc > 3 ? atoi(argv[3]) : 10000, missed = 0;
	cpu_set_t set; CPU_ZERO(&set); CPU_SET(cpu, &set);
	struct sched_param sp = { .sched_priority = prio };
	if (sched_setaffinity(0, sizeof(set), &set)) DIE("affinity");
	if (prio && sched_setscheduler(0, SCHED_FIFO, &sp)) DIE("SCHED_FIFO");
	if (mlockall(MCL_CURRENT | MCL_FUTURE)) DIE("mlockall");
	uint64_t *seg[4];
	for (int k = 0; k < 4; k++)
		if (!(seg[k] = calloc(n, sizeof(uint64_t)))) DIE("calloc");
	int fd = open("/dev/s5_irqlat", O_RDONLY);	/* starts the timer on this CPU */
	if (fd < 0) DIE("open /dev/s5_irqlat");
	for (int i = 0; i < n; i++) {
		struct s5_sample s; struct timespec ts;
		if (read(fd, &s, sizeof(s)) != sizeof(s)) DIE("read");
		clock_gettime(CLOCK_MONOTONIC, &ts);
		uint64_t u = ts.tv_sec * 1000000000ull + ts.tv_nsec;
		seg[0][i] = s.t_hardirq - s.t_expire;	/* IRQ latency         */
		seg[1][i] = s.t_kwake - s.t_hardirq;	/* wakeup + switch     */
		seg[2][i] = u - s.t_kwake;		/* copy + syscall exit */
		seg[3][i] = u - s.t_expire;		/* end to end          */
		missed += s.missed;
	}
	close(fd);
	printf("samples %d on cpu %d prio %d, missed periods %d\n", n, cpu, prio, missed);
	pct("irq", seg[0], n); pct("wake", seg[1], n); pct("ret", seg[2], n); pct("total", seg[3], n);
	return 0;
}
```

지면 때문에 CSV 출력 부분(인자 4번째 = 파일 경로, `fprintf` 한 줄)을 뺐다. 저장소의 `user/s5_waiter.c`(62줄)에는 들어 있다. 순서가 중요하다: **고정 → FIFO → mlockall → open**. `open()`이 타이머를 그 CPU에 고정해 시작하므로, 고정을 먼저 해야 타이머와 대기자가 같은 CPU에 있다. 버퍼는 측정 루프 전에 `calloc`하고 `mlockall(MCL_FUTURE)`로 잠가 루프 중 page fault가 없게 한다(N1 §8, N2 §9).

### 3.2 빌드 — 왜 static인가

```sh
gcc -std=gnu11 -Wall -Wextra -O2 -static user/s5_waiter.c -o /var/tmp/S5/share/bin/s5_waiter
gcc -std=gnu11 -Wall -Wextra -O2 -static -pthread user/s5_gpio.c -o /var/tmp/S5/share/bin/s5_gpio
ls -la /var/tmp/S5/share/bin
```

```text
total 1488
drwxrwxr-x 2 donh donh   4096 Oct 18 16:29 .
drwxrwxr-x 3 donh donh   4096 Oct 18 16:29 ..
-rwxrwxr-x 1 donh donh 800592 Oct 18 16:29 s5_gpio
-rwxrwxr-x 1 donh donh 716288 Oct 18 16:29 s5_waiter
```

경고 0개. VM은 Ubuntu 26.04(glibc가 더 새것), 게스트는 Ubuntu 24.04다. 동적 링크 바이너리는 게스트에서 `GLIBC_2.xx not found`로 실패할 수 있다. static으로 만들면 게스트 glibc와 무관하고, TCG 게스트 안에서 gcc를 돌리는 것보다 훨씬 빠르다.

**체크포인트**: `share/bin/`에 static 바이너리 2개(`file`로 보면 `statically linked`).

---

## 4. Step 3 — 게스트 부팅 장치: initramfs → switch_root → PID 1, 9p 공유

### 4.1 왜 이렇게 하나

- **systemd 없이 PID 1로 측정 스크립트를 돌린다.** systemd 게스트는 부하 걸린 호스트에서 부팅에 수 분이 걸리고, 서비스들이 측정에 소음을 더한다(N3 §1). 그리고 아래 §4.4의 RT_GROUP_SCHED 함정도 피한다.
- **rootfs는 qcow2 overlay.** `rootfs-trace.ext4`는 공용 읽기 전용 이미지다. 쓰기는 overlay 파일에만 간다.
- **입출력은 virtio-9p 공유 디렉터리.** overlay를 mount하지 않고도(VM에 nbd 모듈을 올리지 않고도) 게스트에 파일을 넣고 결과를 꺼낸다. 두 커널 모두 `CONFIG_9P_FS=y`, `NET_9P_VIRTIO=y`다.
- **2파일 initramfs.** 커널은 rootfs의 `/sbin/init` 대신 initramfs의 `/init`을 먼저 실행한다. 여기서 vda와 9p를 mount한 뒤 `switch_root`로 9p 위의 스크립트를 PID 1로 넘긴다.

### 4.2 코드 — `/init`, 게스트 `init.sh`, 실행기

```sh
#!/bin/busybox sh
# /init of a 2-file initramfs: mount the qcow2 rootfs + 9p share, then hand PID 1
# to the share's guest/init.sh (no systemd -> no service noise, no cgroup setup).
/bin/busybox --install -s /bin
mkdir -p /proc /dev /newroot
mount -t proc proc /proc
mount -t devtmpfs dev /dev
mount -o rw /dev/vda /newroot || exec sh
mount -t 9p -o trans=virtio,version=9p2000.L,msize=262144 s5 /newroot/mnt || exec sh
umount /proc; umount /dev
exec switch_root /newroot /bin/bash /mnt/guest/init.sh
```

```sh
#!/bin/bash
# PID 1 in the guest. Kernel cmdline: s5phase=a,b,c  s5tag=<label>
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
mount -t proc proc /proc; mount -t sysfs sys /sys; mount -t devtmpfs dev /dev
mkdir -p /dev/pts /dev/shm; mount -t devpts pts /dev/pts
mount -t tmpfs tmp /tmp; mount -t tmpfs shm /dev/shm
mount -t tracefs none /sys/kernel/tracing; mount -t debugfs none /sys/kernel/debug
mount -t cgroup2 none /sys/fs/cgroup; mount -t configfs none /sys/kernel/config
hostname s5
PHASE=$(sed -n 's/.*s5phase=\([^ ]*\).*/\1/p' /proc/cmdline)
TAG=$(sed -n 's/.*s5tag=\([^ ]*\).*/\1/p' /proc/cmdline)
OUT=/mnt/out/$(uname -r)_${TAG:-x}; mkdir -p $OUT
{ echo "S5START $(uname -r) phase=$PHASE tag=$TAG"; cat /proc/cmdline; nproc
  cat /sys/devices/system/cpu/isolated /sys/devices/system/cpu/nohz_full 2>/dev/null; } | tee $OUT/env.txt
cp -a /mnt/bin /tmp/bin   # run from tmpfs: no 9p page faults in the measured path
insmod /mnt/ko/$(uname -r)/s5_irqlat.ko && echo "s5_irqlat loaded: $(ls -l /dev/s5_irqlat)"
for p in ${PHASE//,/ }; do
  mkdir -p $OUT/$p
  bash /mnt/guest/phase_$p.sh $OUT/$p 2>&1 | tee $OUT/$p/log.txt; sync
done
echo S5END; sync; mount -o remount,ro / ; sleep 1
echo o > /proc/sysrq-trigger; sleep 60
```

```sh
#!/bin/bash
# usage: run_guest.sh <trace|rt> <phases> <tag> [extra kernel args...]
set -e
K=$1; P=$2; T=$3; shift 3; X="$*"
W=/var/tmp/S5
[ -f $W/s5.qcow2 ] || qemu-img create -q -f qcow2 -F raw -b /var/lab/out/rootfs-trace.ext4 $W/s5.qcow2
timeout ${S5_TIMEOUT:-1800} qemu-system-aarch64 -no-reboot -machine virt -cpu cortex-a57,pmu=on \
  -smp 2 -m 1G -nographic -kernel /var/lab/out/Image-$K -initrd $W/s5_initramfs.cpio.gz \
  -drive file=$W/s5.qcow2,if=virtio,format=qcow2 \
  -virtfs local,path=$W/share,mount_tag=s5,security_model=none,id=s5fs \
  -append "console=ttyAMA0 earlycon=pl011,0x9000000 panic=1 quiet s5phase=$P s5tag=$T $X" \
  > $W/console_${K}_$T.log 2>&1 || true
echo "guest $K/$T done: $(tail -c 300 $W/console_${K}_$T.log | grep -a -c S5END) S5END marker(s)"
```

`host/prepare.sh`가 모듈 두 벌, static 바이너리, initramfs(`/var/lab/build/rootfs/bin/busybox` + 위 `/init`을 `cpio -H newc`로 묶음)를 만들고 게스트 스크립트를 공유 디렉터리에 복사한다. 커널이 모르는 `이름=값` 인자(`S5D=30` 같은)는 init의 환경 변수가 되므로 단계 스크립트의 매개변수를 커맨드라인으로 바꿀 수 있다(N3 §1.1과 같은 기법).

### 4.3 예제 2 — 스모크 부팅

```sh
/Users/donh/.elinux_scratch/S5/irq-latency-lab/host/prepare.sh
cd /var/tmp/S5 && time S5_TIMEOUT=400 /Users/donh/.elinux_scratch/S5/irq-latency-lab/host/run_guest.sh trace smoke smoke
grep -a -v "^\[" console_trace_smoke.log
```

첫 스모크(버그 1을 고치기 전) 실행의 시간과 콘솔 끝부분:

```text
guest trace/smoke done: 1 S5END marker(s)

real	0m8.091s
...
S5START 6.18.55-trace phase=smoke tag=smoke
console=ttyAMA0 earlycon=pl011,0x9000000 panic=1 quiet s5phase=smoke s5tag=smoke 
2

s5_irqlat loaded: crw------- 1 root root 10, 258 Oct 18 23:30 /dev/s5_irqlat
Linux s5 6.18.55-trace #1 SMP PREEMPT Sat Oct 17 23:37:47 PDT 2026 aarch64 aarch64 aarch64 GNU/Linux
0.00 0.00 0.00 1/66 105
...
cyclictest V 2.50

rtla version 6.18.55

trace-cmd version 3.2.0 (not-a-git-repo)
stress-ng, version 0.17.06 (gcc 13.2.0, aarch64 Linux 6.18.55-trace)
S5END
[    7.188829][    T1] sysrq: Power Off
[    7.194842][   T64] reboot: Power down
```

(가운데 `...`는 생략. 측정 줄은 §2.6의 버그 1 출력이다.) 버그 1을 고친 뒤 같은 스모크의 측정 줄:

```text
samples 2000 on cpu 1 prio 90, missed periods 83
irq       p50  163.6  p99  2341.6  p99.9  5438.1  max  37272.1 us
wake      p50  135.7  p99  1014.4  p99.9  2256.1  max   2340.1 us
ret       p50   33.7  p99   322.7  p99.9  2185.7  max   2459.5 us
total     p50  374.9  p99  2565.4  p99.9  5827.8  max  38340.3 us
```

출력에서 볼 것: 부팅부터 전원 꺼짐까지 **8초**. `systemd` 부팅(수 분)보다 훨씬 빨라 실험을 수십 번 돌릴 수 있다. `/dev/s5_irqlat`은 misc major 10이다. 측정 줄의 값은 실행마다 다르다.

**체크포인트**: 콘솔 로그에 `S5START` → 단계 출력 → `S5END` → `reboot: Power down`이 보이고, `/var/tmp/S5/share/out/6.18.55-trace_smoke/`에 결과가 있어야 한다.

### 4.4 예제 3 — Image-trace의 RT_GROUP_SCHED 함정을 systemd 없이 재현

N1·N3에서 본 함정: Image-trace는 `CONFIG_RT_GROUP_SCHED=y`라서, systemd가 만든 세션 cgroup(cpu 컨트롤러 켜짐) 안의 셸에서는 RT 런타임 예산이 0이라 `chrt -f`가 실패한다. 처방은 `echo $$ > /sys/fs/cgroup/cgroup.procs`(루트 cgroup으로 이동). 같은 메커니즘을 PID 1 게스트에서 cgroup 하나로 재현했다.

```sh
C=/sys/fs/cgroup; zcat /proc/config.gz | grep -E "^CONFIG_RT_GROUP_SCHED=" || echo "CONFIG_RT_GROUP_SCHED is not set"
echo +cpu > $C/cgroup.subtree_control; mkdir -p $C/s5_session; ls $C/s5_session | grep -E "^cpu\.rt" || echo "(no cpu.rt_* files)"
sh -c "echo \$\$ > $C/s5_session/cgroup.procs; grep 0:: /proc/self/cgroup; chrt -f 90 true && echo 'chrt -f 90: OK' || echo 'chrt -f 90: FAILED'"
sh -c "echo \$\$ > $C/s5_session/cgroup.procs; echo \$\$ > $C/cgroup.procs; grep 0:: /proc/self/cgroup; chrt -f 90 true && echo 'after moving to root cgroup: OK'"
```

Image-trace:

```text
CONFIG_RT_GROUP_SCHED=y
(no cpu.rt_* files)
0::/s5_session
chrt: failed to set pid 0's policy: Operation not permitted
chrt -f 90: FAILED
0::/
after moving to root cgroup: OK
```

Image-rt:

```text
CONFIG_RT_GROUP_SCHED is not set
(no cpu.rt_* files)
0::/s5_session
chrt -f 90: OK
0::/
after moving to root cgroup: OK
```

출력에서 볼 것: 같은 명령이 trace 커널에서만 EPERM이다. cgroup v2에는 `cpu.rt_runtime_us` 파일이 보이지 않는다 — 조절 손잡이가 없으니 하위 cgroup의 RT 예산은 0으로 남고, "권한 문제"처럼 보이는 에러가 난다. 실무에서 "root인데 SCHED_FIFO가 안 된다"는 질문의 답 중 하나가 이것이다. 이 노트의 측정은 모두 루트 cgroup의 PID 1 아래에서 돌아 이 함정에 걸리지 않는다.

### 4.5 흔한 실패

| 증상 | 원인 | 고치는 법 |
|---|---|---|
| `Kernel panic - not syncing: Attempted to kill init!` | sysrq `o`는 비동기라 PID 1이 먼저 끝남 | `echo o > /proc/sysrq-trigger; sleep 60` + `panic=1` + `-no-reboot` |
| 결과 디렉터리가 비어 있음 | QEMU를 kill해서 page cache 손실 | 단계마다 `sync`, 끝에 `remount,ro` |
| `mv: failed to preserve ownership ... Operation not permitted` | 9p `security_model=none`은 chown 불가 | 무해. 원하면 `cp`로 |
| 측정 중 대기자가 `D` 상태로 `kworker/u8:1`을 깨움 (§4.6) | 바이너리가 9p 위에 있어 `mlockall`의 page-in이 9p I/O | `/tmp`(tmpfs)로 복사해서 실행 |
| `qemu-img: Failed to get "write" lock` | 같은 overlay를 두 QEMU가 염 | 게스트는 순서대로, 또는 overlay를 하나 더 |

### 4.6 예제 4 — 9p가 측정 경로에 끼어든 모습 (실제로 밟은 함정)

초기 버전은 `/mnt/bin/s5_waiter`(9p)를 직접 실행했다. trace-cmd 이벤트를 보니 대기자가 표본마다 이랬다(일부 발췌):

```text
          <idle>-0     [001]     4.761770: sched_switch:         swapper/1:0 [120] R ==> s5_waiter:108 [9]
       s5_waiter-108   [001]     4.761843: sched_waking:         comm=kworker/u8:1 pid=33 prio=120 target_cpu=000
       s5_waiter-108   [001]     4.762079: sched_switch:         s5_waiter:108 [9] D ==> swapper/1:0 [120]
          <idle>-0     [001]     4.762305: irq_handler_entry:    irq=2 name=IPI
          <idle>-0     [001]     4.762328: irq_handler_exit:     irq=2 ret=handled
          <idle>-0     [001]     4.762369: sched_switch:         swapper/1:0 [120] R ==> s5_waiter:108 [9]
```

출력에서 볼 것: 대기자가 `S`(우리 swait)가 아니라 **`D`**(uninterruptible, I/O 대기)로 잠들고, 매번 CPU0의 `kworker/u8:1`을 깨운다. `mlockall(MCL_CURRENT)`이 700 KB static 바이너리의 모든 페이지를 9p에서 끌어오는 중이었다. 측정 루프가 시작된 뒤에도 CSV를 9p에 쓰면 같은 일이 생긴다. 그래서 `init.sh`는 바이너리를 `/tmp`로 복사하고, 단계 스크립트는 CSV를 `/tmp`에 쓴 뒤 끝나고 옮긴다. **측정 경로에 네트워크/공유 파일시스템을 두지 말 것** — 실제 장비에서 NFS root로 RT 테스트를 하면 똑같이 당한다.

---

## 5. Step 4 — 첫 측정: idle / stress-ng / 나쁜 드라이버

### 5.1 부하 세 가지

| 이름 | 무엇 | 왜 |
|---|---|---|
| idle | 아무것도 없음 | 기준선. 단, idle은 "좋은" 조건이 아니다 — idle 탈출 비용이 있다 |
| stress | `stress-ng --cpu 2 --vm 1 --vm-bytes 64M --io 1` | 유저 연산 + syscall + page fault + I/O 부하 (과제 지정) |
| hog | `s5_hog.ko`: CPU1에 묶인 FIFO 1 kthread가 40 ms마다 `spinlock_t`를 4 ms 쥠 | "폴링하며 락을 오래 쥐는 나쁜 드라이버". **PREEMPT_RT가 없애도록 설계된 지연원** |

hog를 따로 만든 이유: TCG에서는 호스트 소음이 커서 일반 부하로는 두 커널 차이가 묻힌다(N3 §10.2의 결론). 원인을 아는 지연원을 넣으면 "RT가 무엇을 고치는가"를 TCG에서도 분명히 보일 수 있다. N3의 `n3_spike`(IRQ off / preempt off)와 N1 §5의 락 보유 실험의 측정 버전이다.

```c
// SPDX-License-Identifier: GPL-2.0
/* s5_hog: a "badly written driver" — a FIFO-1 kthread bound to one CPU that
 * holds a spinlock_t for hog_us every period_ms. On PREEMPT (non-RT)
 * spin_lock() disables preemption, so even a FIFO-95 task on that CPU waits.
 * On PREEMPT_RT spinlock_t is an rt_mutex: the holder can be preempted. */
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/sched.h>

static int cpu = 1, hog_us = 4000, period_ms = 40;
module_param(cpu, int, 0444);
module_param(hog_us, int, 0444);
module_param(period_ms, int, 0444);

static DEFINE_SPINLOCK(s5_hog_lock);
static struct task_struct *s5_hog_task;
static unsigned long s5_hogs;

static int s5_hog_fn(void *unused)
{
	sched_set_fifo_low(current);			/* SCHED_FIFO prio 1 */
	while (!kthread_should_stop()) {
		msleep_interruptible(period_ms);
		spin_lock(&s5_hog_lock);
		udelay(hog_us);				/* "polling a status bit" */
		spin_unlock(&s5_hog_lock);
		s5_hogs++;
	}
	return 0;
}

static int __init s5_hog_init(void)
{
	s5_hog_task = kthread_create(s5_hog_fn, NULL, "s5_hog/%d", cpu);
	if (IS_ERR(s5_hog_task))
		return PTR_ERR(s5_hog_task);
	kthread_bind(s5_hog_task, cpu);
	wake_up_process(s5_hog_task);
	pr_info("s5_hog: cpu=%d hog_us=%d period_ms=%d\n", cpu, hog_us, period_ms);
	return 0;
}

static void __exit s5_hog_exit(void)
{
	kthread_stop(s5_hog_task);
	pr_info("s5_hog: %lu lock holds\n", s5_hogs);
}
module_init(s5_hog_init);
module_exit(s5_hog_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("S5 spinlock_t holder (latency source PREEMPT_RT removes)");
```

PREEMPT에서 `spin_lock()`은 `preempt_disable()`을 포함하므로 4 ms 동안 CPU1에서는 우선순위와 상관없이 아무도 선점하지 못한다. IRQ는 켜져 있으니 타이머 hard IRQ는 제때 돈다 — 그래서 **irq 구간은 멀쩡하고 wake 구간만 늘어날 것**이라고 예측할 수 있다. PREEMPT_RT에서 `spinlock_t`는 rt_mutex라 쥔 채로도 선점되고, FIFO 90 대기자가 바로 CPU를 가져간다(N1 §5).

### 5.2 측정 단계 스크립트

```sh
# lib_load.sh — background "scenarios", identical on every kernel
scen_on() {
  case $1 in
    stress) stress-ng --cpu 2 --vm 1 --vm-bytes 64M --io 1 --timeout 600s -q & LOADPID=$!; sleep 2 ;;
    hog)    insmod /mnt/ko/$(uname -r)/s5_hog.ko cpu=1 hog_us=4000 period_ms=40 ;;
  esac
}
scen_off() {
  case $1 in
    stress) kill $LOADPID 2>/dev/null; pkill stress-ng; wait 2>/dev/null; sleep 1 ;;
    hog)    rmmod s5_hog; dmesg | grep s5_hog | tail -1 ;;
  esac
}
```

```sh
# phase_matrix.sh — per scenario: s5_waiter, cyclictest, rtla timerlat, all on CPU1
O=$1; . /mnt/guest/lib_load.sh; D=${S5D:-20}; N=$((D*1000))
CT="cyclictest -m -a 1 -t 1 -p 95 -i 1000 -h 20000 -q -D $D"
TL="/usr/local/bin/rtla timerlat hist -c 1 -d ${D}s -P f:95 -b 10 -E 2000 --no-aa"
for s in ${S5SCEN:-idle stress hog}; do
  echo "######## scenario $s  loadavg(guest) $(cat /proc/loadavg)"; scen_on $s
  /tmp/bin/s5_waiter 1 90 $N /tmp/lat_$s.csv; mv /tmp/lat_$s.csv $O/
  $CT --histfile=$O/cyc_$s.hist > $O/cyc_$s.txt 2>&1; grep -E "^# (Min|Avg|Max) Lat|Overflows" $O/cyc_$s.hist
  $TL > $O/tl_$s.txt 2>&1; grep -m4 -E "^(count|min|avg|max):" $O/tl_$s.txt
  scen_off $s
done
```

세 측정기 모두 **CPU1, 1 ms 주기, 30초**(`S5D=30`)로 맞췄다. `/usr/local/bin/rtla`를 쓴 이유: `/usr/bin/rtla`는 Ubuntu 래퍼라 6.18 커널과 맞지 않는다(랩 README). 우선순위는 s5_waiter 90, cyclictest·timerlat 95다.

### 5.3 예제 5 — Image-trace (PREEMPT) 결과

```sh
S5_TIMEOUT=1500 host/run_guest.sh trace matrix,gpio base S5D=30
```

```text
######## scenario idle  loadavg(guest) 0.08 0.02 0.01 2/66 106
samples 30000 on cpu 1 prio 90, missed periods 153
irq       p50  174.2  p99   764.6  p99.9  3879.7  max  43070.9 us
wake      p50   87.2  p99   513.7  p99.9  1658.6  max   6769.9 us
ret       p50   28.6  p99   208.5  p99.9   768.6  max  26804.1 us
total     p50  288.3  p99  1335.9  p99.9  5242.2  max  43536.4 us
# Min Latencies: 00073
# Avg Latencies: 00373
# Max Latencies: 64392
# Histogram Overflows: 00013
count:    29582     29582     29581 
min:          2        48       112 
avg:        210       295       364 
max:      17796     20021     20526 
######## scenario stress  loadavg(guest) 0.52 0.15 0.05 1/67 117
samples 30000 on cpu 1 prio 90, missed periods 211
irq       p50  135.4  p99  1276.0  p99.9  7759.1  max  43143.2 us
wake      p50  137.7  p99   500.2  p99.9  1265.7  max  26699.3 us
ret       p50   39.7  p99   140.0  p99.9   477.3  max   5383.7 us
total     p50  323.7  p99  1754.2  p99.9  8982.1  max  43587.2 us
...
######## scenario hog  loadavg(guest) 3.76 1.67 0.63 1/67 136
samples 30000 on cpu 1 prio 90, missed periods 2135
irq       p50  137.7  p99   366.8  p99.9  2663.2  max  22873.4 us
wake      p50   76.1  p99  3894.3  p99.9  4109.4  max  11708.5 us
ret       p50   26.1  p99    89.8  p99.9   229.3  max   6239.5 us
total     p50  236.2  p99  4037.7  p99.9  4548.6  max  23733.5 us
# Min Latencies: 00061
# Avg Latencies: 00325
# Max Latencies: 27256
# Histogram Overflows: 00001
count:    26661     26661     26661 
min:          3        54       114 
avg:        186       388       498 
max:      57489     65345     65517 
```

(`...`는 stress의 cyclictest/timerlat 줄 생략 — 전체는 `results/raw_outputs.tgz`.) timerlat의 세 열은 IRQ / Thr(커널 스레드) / Usr(유저 스레드) 단계다(N3 §7).

### 5.4 예제 6 — Image-rt (PREEMPT_RT) 결과, 같은 명령

```text
######## scenario idle  loadavg(guest) 0.00 0.00 0.00 2/85 126
samples 30000 on cpu 1 prio 90, missed periods 37
irq       p50  219.4  p99   429.7  p99.9  3049.1  max  17206.5 us
wake      p50   84.1  p99   319.2  p99.9   788.4  max   4280.5 us
ret       p50   17.1  p99   210.0  p99.9   399.3  max   5011.0 us
total     p50  323.9  p99   750.1  p99.9  3502.3  max  18063.1 us
...
######## scenario hog  loadavg(guest) 4.30 1.96 0.75 1/86 156
samples 30000 on cpu 1 prio 90, missed periods 116
irq       p50  354.6  p99   816.0  p99.9  3578.4  max  20353.8 us
wake      p50  117.7  p99   388.0  p99.9  1469.6  max  10761.6 us
ret       p50   21.6  p99   111.5  p99.9   363.0  max   2703.7 us
total     p50  478.7  p99  1408.2  p99.9  4503.1  max  20875.7 us
# Min Latencies: 00056
# Avg Latencies: 00457
# Max Latencies: 53013
# Histogram Overflows: 00003
count:    29109     29109     29109 
min:          25        57       103 
avg:        230       396       474 
max:      30445     30541     30644 
```

출력에서 볼 것 (두 커널 비교):

- **hog에서 wake p99: PREEMPT 3894 µs vs PREEMPT_RT 388 µs.** 4 ms 락 보유가 PREEMPT에서는 wake 구간에 그대로 나타나고 RT에서는 사라진다. irq 구간 p99는 PREEMPT에서 오히려 367 µs로 작다 — 예측대로 IRQ는 막히지 않았다.
- **missed periods: 2135 vs 116.** PREEMPT에서 대기자는 30000주기 중 약 7%를 통째로 놓쳤다(4 ms / 40 ms의 비율 10%와 같은 자릿수).
- hog 아래 PREEMPT의 `loadavg`가 3.76인 것은 FIFO 1 kthread가 매 40 ms마다 4 ms씩 돌기 때문이다.
- idle의 max는 두 커널 모두 수십 ms다 — 이것은 호스트 쪽 vCPU 정지다(N3 §8). PREEMPT_RT는 OS 밖의 정지를 줄이지 못한다.

**체크포인트**: `share/out/6.18.55-<k>_base/matrix/`에 `lat_{idle,stress,hog}.csv`, `cyc_*.hist`, `tl_*.txt` 9개 파일이 있고, PREEMPT hog의 wake p99가 수 ms, PREEMPT_RT hog의 wake p99가 그보다 한 자릿수 작아야 한다.

---

## 6. Step 5 — ftrace로 경로를 증명한다

측정기가 "wake 구간이 4 ms"라고 말해도, 면접관은 "그 4 ms가 정말 그 락 때문인지 어떻게 아나?"라고 묻는다. 측정 숫자를 트레이스 증거로 받쳐야 한다. 각 지점을 무엇으로 관찰하는지부터 지도를 그린다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<text x="85" y="30" font-size="13" text-anchor="middle">경로의 지점</text>
<text x="245" y="30" font-size="13" text-anchor="middle">tracepoint</text>
<text x="405" y="30" font-size="13" text-anchor="middle">동적 / graph</text>
<text x="520" y="30" font-size="13" text-anchor="middle">모듈 시각</text>
<rect x="10" y="44" width="150" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="85" y="63" font-size="12" text-anchor="middle">타이머 만료 (예정 시각)</text>
<text x="245" y="63" font-size="12" text-anchor="middle">—</text>
<text x="405" y="63" font-size="12" text-anchor="middle">—</text>
<text x="520" y="63" font-size="12" text-anchor="middle">t_expire</text>
<line x1="160" y1="58" x2="168" y2="58" stroke="currentColor" stroke-opacity="0.4"/>
<rect x="10" y="80" width="150" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="85" y="99" font-size="12" text-anchor="middle">arch_timer IRQ</text>
<text x="245" y="99" font-size="12" text-anchor="middle">irq_handler_entry</text>
<text x="405" y="99" font-size="12" text-anchor="middle">—</text>
<text x="520" y="99" font-size="12" text-anchor="middle">—</text>
<line x1="160" y1="94" x2="168" y2="94" stroke="currentColor" stroke-opacity="0.4"/>
<rect x="10" y="116" width="150" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="85" y="135" font-size="12" text-anchor="middle">콜백 s5_timer_fn</text>
<text x="245" y="135" font-size="12" text-anchor="middle">hrtimer_expire_entry</text>
<text x="405" y="135" font-size="12" text-anchor="middle">kprobe s5/hardirq</text>
<text x="520" y="135" font-size="12" text-anchor="middle">t_hardirq</text>
<line x1="160" y1="130" x2="168" y2="130" stroke="currentColor" stroke-opacity="0.4"/>
<rect x="10" y="152" width="150" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="85" y="171" font-size="12" text-anchor="middle">swake_up_one()</text>
<text x="245" y="171" font-size="12" text-anchor="middle">sched_waking</text>
<text x="405" y="171" font-size="12" text-anchor="middle">graph s5_timer_fn</text>
<text x="520" y="171" font-size="12" text-anchor="middle">—</text>
<line x1="160" y1="166" x2="168" y2="166" stroke="currentColor" stroke-opacity="0.4"/>
<rect x="10" y="188" width="150" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="85" y="207" font-size="12" text-anchor="middle">→ s5_waiter 전환</text>
<text x="245" y="207" font-size="12" text-anchor="middle">sched_switch</text>
<text x="405" y="207" font-size="12" text-anchor="middle">—</text>
<text x="520" y="207" font-size="12" text-anchor="middle">—</text>
<line x1="160" y1="202" x2="168" y2="202" stroke="currentColor" stroke-opacity="0.4"/>
<rect x="10" y="224" width="150" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="85" y="243" font-size="12" text-anchor="middle">s5_read 안에서 재개</text>
<text x="245" y="243" font-size="12" text-anchor="middle">—</text>
<text x="405" y="243" font-size="12" text-anchor="middle">graph s5_read</text>
<text x="520" y="243" font-size="12" text-anchor="middle">t_kwake</text>
<line x1="160" y1="238" x2="168" y2="238" stroke="currentColor" stroke-opacity="0.4"/>
<rect x="10" y="260" width="150" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="85" y="279" font-size="12" text-anchor="middle">read() 반환 (유저)</text>
<text x="245" y="279" font-size="12" text-anchor="middle">—</text>
<text x="405" y="279" font-size="12" text-anchor="middle">—</text>
<text x="520" y="279" font-size="12" text-anchor="middle">t_user</text>
<line x1="160" y1="274" x2="168" y2="274" stroke="currentColor" stroke-opacity="0.4"/>
<path d="M569,58 L575,58 L575,130 L569,130" fill="none" stroke="#4a7bd0" stroke-width="1.6"/>
<text x="581" y="98" font-size="12">irq</text>
<path d="M569,130 L575,130 L575,238 L569,238" fill="none" stroke="#e08a3c" stroke-width="1.6"/>
<text x="581" y="188" font-size="12">wake</text>
<path d="M569,238 L575,238 L575,274 L569,274" fill="none" stroke="#3f9a6b" stroke-width="1.6"/>
<text x="581" y="260" font-size="12">ret</text>
<path d="M622,166 L628,166 L628,202 L622,202" fill="none" stroke="#d0564a" stroke-width="1.6"/>
<text x="642" y="184" font-size="12" text-anchor="middle" transform="rotate(-90 642 184)">wakeup_latency</text>
<path d="M646,130 L652,130 L652,202 L646,202" fill="none" stroke="#888" stroke-width="1.6"/>
<text x="666" y="166" font-size="12" text-anchor="middle" transform="rotate(-90 666 166)">irq_to_run</text>
<text x="10" y="312" font-size="12">파랑·주황·초록 괄호 = s5_waiter 구간 · 빨강 = wakeup_latency · 회색 = irq_to_run (synthetic)</text>
</svg>
```

그림 3 — ftrace 이벤트 지도. 경로의 각 지점(왼쪽)을 어떤 tracepoint·동적 probe·function_graph가 관찰하고, 모듈이 어디서 타임스탬프를 찍는지. 오른쪽 괄호는 s5_waiter의 세 구간과, 아래 §6.4에서 만드는 synthetic event 두 개가 각각 어디서 어디까지를 재는지 보여 준다.

### 6.1 단계 스크립트 (앞부분)

```sh
O=$1; T=/sys/kernel/tracing; . /mnt/guest/lib_load.sh; W=/tmp/bin/s5_waiter
EV="-e irq:irq_handler_entry -e irq:irq_handler_exit -e timer:hrtimer_expire_entry -e timer:hrtimer_expire_exit -e sched:sched_waking -e sched:sched_switch"
echo "== (a) trace-cmd events, CPU1 only"
trace-cmd record -q -o /tmp/ev.dat -M 2 $EV $W 1 90 300 > $O/ev_waiter.txt
trace-cmd report -i /tmp/ev.dat > $O/ev.txt 2>/dev/null; wc -l $O/ev.txt
echo "== (b) function_graph"
trace-cmd record -q -o /tmp/fg.dat -M 2 -p function_graph -g s5_timer_fn -g s5_read --max-graph-depth 3 $W 1 90 50 > /dev/null
trace-cmd report -i /tmp/fg.dat > $O/fg.txt 2>/dev/null; wc -l $O/fg.txt
```

`-M 2`는 CPU 마스크(비트 1 = CPU1)다. trace 파일을 9p가 아니라 `/tmp`에 쓰는 이유는 §4.6과 같다.

### 6.2 예제 7 — 이벤트 한 주기 (trace-cmd report)

Image-trace, idle, 200번째 `s5_timer_fn` 만료 주변(실제 출력, 열 일부 자름):

```text
       s5_waiter-108   [001]     3.641331: sched_switch:         s5_waiter:108 [9] S ==> swapper/1:0 [120]
          <idle>-0     [001]     3.642124: irq_handler_entry:    irq=11 name=arch_timer
          <idle>-0     [001]     3.642134: hrtimer_expire_entry: hrtimer=0xffffc0d8d25a3098 now=3575252864 function=s5_timer_fn/0x0
          <idle>-0     [001]     3.642139: sched_waking:         comm=s5_waiter pid=108 prio=9 target_cpu=001
          <idle>-0     [001]     3.642153: hrtimer_expire_exit:  hrtimer=0xffffc0d8d25a3098
          <idle>-0     [001]     3.642171: irq_handler_exit:     irq=11 ret=handled
          <idle>-0     [001]     3.642198: sched_switch:         swapper/1:0 [120] R ==> s5_waiter:108 [9]
       s5_waiter-108   [001]     3.642325: sched_switch:         s5_waiter:108 [9] S ==> swapper/1:0 [120]
```

Image-rt, 같은 위치:

```text
     ksoftirqd/1-30    [001]     2.961547: sched_switch:         ksoftirqd/1:30 [120] S ==> swapper/1:0 [120]
          <idle>-0     [001]     2.962356: irq_handler_entry:    irq=11 name=arch_timer
          <idle>-0     [001]     2.962363: hrtimer_expire_entry: hrtimer=0xffffd1a3830b9098 now=2910006720 function=s5_timer_fn/0x0
          <idle>-0     [001]     2.962368: sched_waking:         comm=s5_waiter pid=128 prio=9 target_cpu=001
          <idle>-0     [001]     2.962379: hrtimer_expire_exit:  hrtimer=0xffffd1a3830b9098
          <idle>-0     [001]     2.962381: hrtimer_expire_entry: hrtimer=0xffff00003fdd3a78 now=2910006720 function=tick_nohz_handler/0x0
          <idle>-0     [001]     2.962404: hrtimer_expire_exit:  hrtimer=0xffff00003fdd3a78
          <idle>-0     [001]     2.962417: irq_handler_exit:     irq=11 ret=handled
          <idle>-0     [001]     2.962424: sched_waking:         comm=ksoftirqd/1 pid=30 prio=120 target_cpu=001
          <idle>-0     [001]     2.962454: sched_switch:         swapper/1:0 [120] R ==> s5_waiter:128 [9]
       s5_waiter-128   [001]     2.962525: sched_switch:         s5_waiter:128 [9] S ==> ksoftirqd/1:30 [120]
```

출력에서 볼 것:

- 경로가 그림 3 그대로다: `irq_handler_entry arch_timer` → `hrtimer_expire_entry function=s5_timer_fn` → **IRQ 안에서** `sched_waking comm=s5_waiter` → `irq_handler_exit` → `sched_switch ==> s5_waiter`. trace 커널에서 IRQ 진입부터 전환까지 74 µs(3.642124 → 3.642198).
- `[9]`는 커널 내부 우선순위 표기다: FIFO 90 → `99 − 90 = 9`. `[120]`은 nice 0 SCHED_OTHER.
- RT 커널에서도 `s5_timer_fn`은 `<idle>-0` 문맥, 즉 **hard IRQ 안**에서 돈다 — `_HARD` 모드가 먹었다. 반면 tick 처리 뒤 `ksoftirqd/1`이 깨어난다. PREEMPT_RT에서는 softirq 처리 대부분이 스레드로 밀려나기 때문이다. 대기자(FIFO 90)가 먼저 돌고, `ksoftirqd`(SCHED_OTHER)는 대기자가 잠든 뒤에야 돈다.
- `irq=11`은 Linux IRQ 번호다. 게스트의 `/proc/interrupts`에서 확인하면 `11: ... GIC-0  27 Level  arch_timer`(PPI 27 = EL1 virtual timer)다.

### 6.3 예제 8 — function_graph: 콜백 안에서 시간이 어디 쓰였나

Image-trace (실제 출력, 열 일부 자름):

```text
          <idle>-0     [001]    10.172881: funcgraph_entry:                   |  s5_timer_fn() {
          <idle>-0     [001]    10.172886: funcgraph_entry:                   |    ktime_get() {
          <idle>-0     [001]    10.172887: funcgraph_entry:        1.664 us   |      arch_counter_read();
          <idle>-0     [001]    10.172892: funcgraph_exit:         6.096 us   |    }
          <idle>-0     [001]    10.172893: funcgraph_entry:                   |    _raw_spin_lock() {
          <idle>-0     [001]    10.172895: funcgraph_entry:        1.248 us   |      preempt_count_add();
          <idle>-0     [001]    10.172898: funcgraph_exit:         4.448 us   |    }
          <idle>-0     [001]    10.172900: funcgraph_entry:                   |    _raw_spin_unlock() {
          <idle>-0     [001]    10.172901: funcgraph_entry:        1.168 us   |      preempt_count_sub();
          <idle>-0     [001]    10.172904: funcgraph_exit:         3.920 us   |    }
          <idle>-0     [001]    10.172905: funcgraph_entry:                   |    swake_up_one() {
          <idle>-0     [001]    10.172907: funcgraph_entry:        1.840 us   |      _raw_spin_lock_irqsave();
          <idle>-0     [001]    10.172911: funcgraph_entry:      + 37.088 us  |      try_to_wake_up();
          <idle>-0     [001]    10.172950: funcgraph_entry:        1.968 us   |      _raw_spin_unlock_irqrestore();
          <idle>-0     [001]    10.172953: funcgraph_exit:       + 47.872 us  |    }
          <idle>-0     [001]    10.172955: funcgraph_entry:                   |    hrtimer_cb_get_time() {
          <idle>-0     [001]    10.172957: funcgraph_entry:        2.752 us   |      __hrtimer_cb_get_time();
          <idle>-0     [001]    10.172961: funcgraph_exit:         5.760 us   |    }
          <idle>-0     [001]    10.172963: funcgraph_entry:        1.504 us   |    hrtimer_forward();
          <idle>-0     [001]    10.172966: funcgraph_exit:       + 85.280 us  |  }
       s5_waiter-114   [001]    10.173063: funcgraph_entry:        8.256 us   |      finish_task_switch.isra.0();
       s5_waiter-114   [001]    10.173073: funcgraph_entry:        1.664 us   |      preempt_count_sub();
       s5_waiter-114   [001]    10.173076: funcgraph_exit:       ! 862.672 us |    }
       s5_waiter-114   [001]    10.173078: funcgraph_entry:                   |    prepare_to_swait_event() {
```

Image-rt의 같은 콜백(일부):

```text
          <idle>-0     [001]     8.723265: funcgraph_entry:                   |  s5_timer_fn() {
          <idle>-0     [001]     8.723273: funcgraph_entry:        1.248 us   |    _raw_spin_lock();
          <idle>-0     [001]     8.723275: funcgraph_entry:        1.248 us   |    _raw_spin_unlock();
          <idle>-0     [001]     8.723279: funcgraph_entry:                   |    swake_up_one() {
          <idle>-0     [001]     8.723284: funcgraph_entry:      + 17.456 us  |      try_to_wake_up();
          <idle>-0     [001]     8.723305: funcgraph_exit:       + 26.784 us  |    }
          <idle>-0     [001]     8.723318: funcgraph_exit:       + 52.288 us  |  }
```

출력에서 볼 것:

- 콜백 85 µs 중 **`try_to_wake_up()`이 37 µs**로 가장 크다 — 대기자를 run queue에 넣고 선점 필요 표시(`TIF_NEED_RESCHED`)를 하는 일이다. 나머지는 function_graph 자신의 오버헤드가 크게 섞인 숫자다(TCG에서 함수 하나 기록에 1~2 µs). 이 표시 깃발이 IRQ 복귀 경로에서 `__schedule()`로 이어진다.
- `s5_waiter` 쪽의 `! 862.672 us` 닫는 괄호는 `schedule()`에서 잠들어 있던 시간이다(1 ms 주기에서 일하지 않는 시간). `!`는 100 µs 이상, `+`는 10 µs 이상 표시(M5 §4).
- RT 커널에서는 `_raw_spin_lock()` 아래에 `preempt_count_add()`가 안 보인다. 디버그 옵션 차이다 — trace 커널은 `CONFIG_DEBUG_PREEMPT=y`, `PREEMPT_TRACER=y`라 `preempt_count_add()`가 추적 가능한 실제 함수가 되고, rt 커널에는 이 옵션들이 없다 — 두 커널 설정이 "PREEMPT_RT만 다른 것은 아니다"라는 §1.2의 경고가 여기서도 보인다.
- `-g`로 그래프를 모듈 함수에 걸 수 있다는 것 자체가 확인 포인트다: 모듈 함수도 ftrace 패치 지점(`patchable-function-entry`)을 갖는다(M5 §2).

### 6.4 예제 9 — kprobe + hist 트리거 + synthetic event로 wakeup latency 히스토그램

트레이스 파일을 다 받아 사후 분석하는 대신, **커널 안에서 바로 히스토그램을 만든다**. 장시간 테스트에서 버퍼 넘침 없이 분포만 모을 때 쓰는 방법이다(M5 §5.5–5.6).

```sh
cd /sys/kernel/tracing; echo 0 > tracing_on; echo > trace
echo 'p:s5/hardirq s5_irqlat:s5_timer_fn' >> kprobe_events
echo 'wakeup_latency u64 lat; pid_t pid; int prio' >> synthetic_events
echo 'irq_to_run u64 lat' >> synthetic_events
echo 'hist:keys=pid:ts0=common_timestamp.usecs if comm=="s5_waiter"' >> events/sched/sched_waking/trigger
echo 'hist:keys=next_pid:wlat=common_timestamp.usecs-$ts0:onmatch(sched.sched_waking).trace(wakeup_latency,$wlat,next_pid,next_prio):onmax($wlat).save(prev_comm,prev_prio) if next_comm=="s5_waiter"' >> events/sched/sched_switch/trigger
echo 'hist:keys=common_cpu:t1=common_timestamp.usecs' >> events/s5/hardirq/trigger
echo 'hist:keys=common_cpu:ilat=common_timestamp.usecs-$t1:onmatch(s5.hardirq).trace(irq_to_run,$ilat) if next_comm=="s5_waiter"' >> events/sched/sched_switch/trigger
echo 'hist:keys=lat.log2:sort=lat' >> events/synthetic/wakeup_latency/trigger
echo 'hist:keys=lat.log2:sort=lat' >> events/synthetic/irq_to_run/trigger
echo 1 > events/synthetic/wakeup_latency/enable; echo 1 > events/synthetic/irq_to_run/enable
```

한 줄씩 읽는 법:

1. `p:s5/hardirq s5_irqlat:s5_timer_fn` — 모듈 함수 입구에 kprobe event를 만든다. 형식은 `p:[그룹/]이름 [모듈:]심볼`. 이제 `events/s5/hardirq`가 tracepoint처럼 생긴다.
2. synthetic event 두 개를 선언한다. 필드 타입은 C 선언처럼 쓴다.
3. `sched_waking`에서, 깨어나는 태스크가 `s5_waiter`이면 pid를 키로 시각을 변수 `ts0`에 저장한다.
4. `sched_switch`에서, 다음 태스크가 `s5_waiter`이면 같은 pid(`next_pid`) 키로 `ts0`을 찾아 차이 `wlat`를 계산하고(`onmatch`), synthetic event `wakeup_latency`를 발생시키며(`.trace`), 최댓값이 갱신될 때 그 순간의 `prev_comm`, `prev_prio`를 저장한다(`onmax().save()`). **"최악일 때 직전에 CPU를 쥐고 있던 놈"을 커널이 알아서 기억한다.**
5. kprobe 지점에서 CPU 번호를 키로 시각 `t1`을 저장하고, `sched_switch`에서 같은 CPU 키로 차이를 내 `irq_to_run`을 만든다.
6. synthetic event 위에 다시 hist를 걸어 `lat.log2`(2의 거듭제곱 구간)로 센다.

그다음 대기자를 5000표본 돌리고 hist 파일을 읽는다. 시나리오를 바꿀 때는 트리거를 `'!'` 접두어로 같은 문자열을 써서 역순으로 지우고 다시 건다(`:clear`로 지우려던 시도는 `onmax` 액션이 있는 트리거에서 `Invalid argument`로 실패했다 — 실제로 밟음).

Image-trace, **hog** (실제 출력에서 Totals 블록 일부 생략):

```text
-- wakeup_latency
{ lat: ~ 2^5  } hitcount:          4
{ lat: ~ 2^6  } hitcount:       3834
{ lat: ~ 2^7  } hitcount:        985
{ lat: ~ 2^8  } hitcount:         19
{ lat: ~ 2^9  } hitcount:          4
{ lat: ~ 2^12 } hitcount:        112
    Hits: 4958
-- irq_to_run
{ lat: ~ 2^6  } hitcount:        220
{ lat: ~ 2^7  } hitcount:       4543
{ lat: ~ 2^8  } hitcount:         73
{ lat: ~ 2^9  } hitcount:          8
{ lat: ~ 2^10 } hitcount:        110
    Hits: 4954
{ next_pid:        143 } hitcount:       4958
	max:       3972  prev_comm: s5_hog/1                          prev_prio:         98
          <idle>-0       [001] d..5.    31.448550: wakeup_latency: lat=51 pid=143 prio=9
          <idle>-0       [001] d..5.    31.448571: irq_to_run: lat=87
          <idle>-0       [001] d..5.    31.449502: wakeup_latency: lat=46 pid=143 prio=9
```

Image-rt, **hog**:

```text
-- wakeup_latency
{ lat: ~ 2^5  } hitcount:        277
{ lat: ~ 2^6  } hitcount:        589
{ lat: ~ 2^7  } hitcount:       2827
{ lat: ~ 2^8  } hitcount:       1226
{ lat: ~ 2^9  } hitcount:         70
{ lat: ~ 2^10 } hitcount:          9
{ lat: ~ 2^11 } hitcount:          1
{ lat: ~ 2^12 } hitcount:          1
    Hits: 5000
{ next_pid:        163 } hitcount:       5000
	max:       3675  prev_comm: swapper/1                         prev_prio:        120
```

비교용 Image-trace, **idle**:

```text
-- wakeup_latency
{ lat: ~ 2^6  } hitcount:        597
{ lat: ~ 2^7  } hitcount:       2994
{ lat: ~ 2^8  } hitcount:       1195
{ lat: ~ 2^9  } hitcount:        200
{ lat: ~ 2^10 } hitcount:          1
{ lat: ~ 2^11 } hitcount:          2
    Hits: 4989
{ next_pid:        118 } hitcount:       4989
	max:       1419  prev_comm: swapper/1                         prev_prio:        120
```

출력에서 볼 것 — **이것이 이 프로젝트의 증거 한 장**이다:

- PREEMPT + hog: `2^12`(4096~8191 µs) 칸에 **112번**. idle에는 이 칸이 없다. 5000표본 중 112번 ≈ 2.2%. `onmax`가 기억한 최악 3972 µs 순간의 `prev_comm`은 **`s5_hog/1`**, `prev_prio 98`(= FIFO 1의 커널 표기 `99 − 1`). 측정기가 본 wake p99 ≈ 3.9 ms의 범인이 커널 안에서 이름으로 지목됐다.
- PREEMPT_RT + hog: `2^12` 칸은 1번뿐이고, 최악의 `prev_comm`은 `swapper/1`(idle) — 락 보유자가 아니라 호스트 쪽 정지다.
- synthetic event가 트레이스 버퍼에도 한 줄씩 남는다(`wakeup_latency: lat=51 pid=143 prio=9`). 플래그 `d..5.`는 IRQ off, preempt 깊이 5 — `sched_switch` tracepoint 안에서 발생했기 때문이다.

### 6.5 함정 — `irq_to_run`이 4 ms를 못 본 이유

같은 hog 실행에서 `irq_to_run`(kprobe → sched_switch)은 `2^12` 칸이 없고 `2^10`(1024~2047 µs)에 110번이 있다. 4 ms 막혔는데 왜 1~2 ms인가? 키가 `common_cpu`인 변수 `t1`은 **같은 키의 다음 이벤트가 오면 덮어써진다**. 대기자가 4 ms 막혀 있는 동안 타이머 hard IRQ가 1 ms마다 와서 `t1`을 갱신했고, 결국 "마지막 hard IRQ부터 전환까지"(1주기 이하)만 쟀다. §2.6의 버그 2와 **정확히 같은 함정**이다. `wakeup_latency`는 `sched_waking`이 대기자가 실제로 잠들어 있을 때 한 번만 발생하므로 이 문제가 없다. 교훈: synthetic event의 시작 이벤트는 "구간마다 한 번만 일어나는 이벤트"를 고르거나, 시작 쪽에 필터를 걸어야 한다.

**체크포인트**: `events/synthetic/wakeup_latency/hist`가 보이고, PREEMPT + hog에서 `2^12` 칸과 `prev_comm: s5_hog/1`이 나와야 한다. 끝나면 `kprobe_events`, `synthetic_events`를 비우고(`echo '!wakeup_latency' >> synthetic_events`, `echo '-:s5/hardirq' >> kprobe_events`) 트리거가 0개인지 `error_log`로 확인한다. 이 노트의 스크립트는 트리거만 지우고 이벤트 정의는 남겨 둔 채 전원을 껐다(출력 마지막의 `events after teardown`).

---

## 7. Step 6 — gpio-sim 경로: 실제 GPIO 드라이버의 길

### 7.1 gpio-sim 칩 만들기 (configfs)

```sh
G=/sys/kernel/config/gpio-sim/s5_sim
mkdir -p $G/gpio-bank0 && echo 8 > $G/gpio-bank0/num_lines && echo 1 > $G/live
DEV=$(cat $G/dev_name); CHIP=$(cat $G/gpio-bank0/chip_name)
PULL=/sys/devices/platform/$DEV/$CHIP/sim_gpio0/pull
echo "chip $CHIP dev $DEV pull=$PULL"; gpiodetect 2>/dev/null | grep -i sim
```

```text
chip gpiochip1 dev gpio-sim.0 pull=/sys/devices/platform/gpio-sim.0/gpiochip1/sim_gpio0/pull
gpiochip1 [gpio-sim.0:node0] (8 lines)
```

칩 이름에 `s5_` 접두어를 붙여 다른 작성자와 충돌하지 않게 했다. `pull`에 `pull-up`/`pull-down`을 쓰면 입력 라인의 값이 바뀌고, 엣지 인터럽트가 걸려 있으면 IRQ가 난다(H4).

### 7.2 코드 — GPIO v2 uAPI로 엣지 이벤트 기다리기

libgpiod 없이 커널 uAPI(`linux/gpio.h`)를 직접 썼다. 의존성이 없고, 커널이 무엇을 주는지(`timestamp_ns`, `line_seqno`)가 그대로 보인다. 아래는 핵심 부분(전체 81줄 중 백분위 함수와 인자 처리 일부 생략).

```c
static int n; static const char *pull; static volatile uint64_t *t0;

static void *toggler(void *arg)
{
	(void)arg;
	int fd = open(pull, O_WRONLY);
	if (fd < 0) DIE("open pull");
	for (int i = 1; i <= n; i++) {
		const char *v = (i & 1) ? "pull-up" : "pull-down";
		usleep(2000);
		t0[i] = now();				/* T0: "the sensor asserts DRDY" */
		if (write(fd, v, strlen(v)) < 0) DIE("write pull");
	}
	close(fd);
	return NULL;
}

int main(int argc, char **argv)
{
	/* ... args, calloc t0/a/b/c ... */
	int chip = open(argv[1], O_RDONLY);
	struct gpio_v2_line_request req = { .offsets = { 0 }, .num_lines = 1, .event_buffer_size = 64,
		.config.flags = GPIO_V2_LINE_FLAG_INPUT | GPIO_V2_LINE_FLAG_EDGE_RISING | GPIO_V2_LINE_FLAG_EDGE_FALLING };
	strcpy(req.consumer, "s5_gpio");
	if (ioctl(chip, GPIO_V2_GET_LINE_IOCTL, &req)) DIE("GET_LINE");
	cpu_set_t set; CPU_ZERO(&set); CPU_SET(1, &set);
	struct sched_param sp = { .sched_priority = 90 };
	if (sched_setaffinity(0, sizeof(set), &set) || sched_setscheduler(0, SCHED_FIFO, &sp)) DIE("rt");
	if (mlockall(MCL_CURRENT | MCL_FUTURE)) DIE("mlockall");
	pthread_t th; pthread_attr_t at; pthread_attr_init(&at);
	CPU_ZERO(&set); CPU_SET(0, &set);		/* toggler: CPU0, SCHED_OTHER */
	pthread_attr_setaffinity_np(&at, sizeof(set), &set);
	pthread_attr_setinheritsched(&at, PTHREAD_EXPLICIT_SCHED);
	struct sched_param op = { 0 }; pthread_attr_setschedpolicy(&at, SCHED_OTHER);
	pthread_attr_setschedparam(&at, &op);
	if (pthread_create(&th, &at, toggler, NULL)) DIE("pthread");
	for (int i = 0; i < n; i++) {
		struct gpio_v2_line_event ev;
		if (read(req.fd, &ev, sizeof(ev)) != sizeof(ev)) DIE("read event");
		uint64_t u = now(), s = ev.line_seqno;
		a[i] = ev.timestamp_ns - t0[s];		/* write() -> hard IRQ   */
		b[i] = u - ev.timestamp_ns;		/* hard IRQ -> user      */
		c[i] = u - t0[s];			/* end to end            */
	}
	pthread_join(th, NULL);
	/* ... print percentiles ... */
}
```

`line_seqno`는 라인 요청 이후 1부터 세는 이벤트 번호라 토글 번호와 그대로 짝이 맞는다(양 엣지를 모두 요청했으므로 토글 1번 = 이벤트 1개). `PTHREAD_EXPLICIT_SCHED`를 빼면 토글러 스레드가 부모의 FIFO 90을 물려받는다 — 비교 조건이 바뀌므로 명시적으로 SCHED_OTHER로 만들었다.

### 7.3 커널 안의 길

```svg
<svg viewBox="0 0 680 350" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<text x="205" y="24" font-size="13" text-anchor="middle">CPU0 (toggler, IRQ 처리)</text>
<text x="555" y="24" font-size="13" text-anchor="middle">CPU1 (RT waiter)</text>
<rect x="10" y="40" width="390" height="32" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="205" y="61" font-size="12" text-anchor="middle">T0 기록 → write("pull-up") (sysfs)</text>
<rect x="10" y="88" width="390" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="205" y="109" font-size="12" text-anchor="middle">gpio_sim_apply_pull() → irq_work_queue()</text>
<line x1="205" y1="72" x2="205" y2="86" stroke="currentColor" marker-end="url(#ah3)"/>
<rect x="10" y="136" width="390" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="205" y="157" font-size="12" text-anchor="middle">self-IPI → irq_sim_handle_irq() (IRQ_WORK_INIT_HARD)</text>
<line x1="205" y1="120" x2="205" y2="134" stroke="currentColor" marker-end="url(#ah3)"/>
<rect x="10" y="184" width="390" height="32" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="205" y="205" font-size="12" text-anchor="middle">edge_irq_handler(): timestamp_ns 기록 (hard IRQ)</text>
<line x1="205" y1="168" x2="205" y2="182" stroke="currentColor" marker-end="url(#ah3)"/>
<rect x="10" y="232" width="390" height="32" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="205" y="253" font-size="12" text-anchor="middle">IRQ_WAKE_THREAD → irq/29-s5_gpio (FIFO 50)</text>
<line x1="205" y1="216" x2="205" y2="230" stroke="currentColor" marker-end="url(#ah3)"/>
<rect x="10" y="280" width="390" height="32" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="205" y="301" font-size="12" text-anchor="middle">edge_irq_thread(): kfifo_in + wake_up_poll()</text>
<line x1="205" y1="264" x2="205" y2="278" stroke="currentColor" marker-end="url(#ah3)"/>
<rect x="440" y="40" width="230" height="32" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="555" y="61" font-size="12" text-anchor="middle">read(line fd)에서 잠듦</text>
<rect x="440" y="280" width="230" height="32" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="555" y="301" font-size="12" text-anchor="middle">깨어남 → read() 반환, T3 기록</text>
<line x1="400" y1="296" x2="437" y2="296" stroke="currentColor" marker-end="url(#ah3)"/>
<text x="418" y="330" font-size="12" text-anchor="middle">wakeup IPI</text>
<line x1="455" y1="72" x2="455" y2="277" stroke="#3f9a6b" stroke-dasharray="4 3"/>
<text x="468" y="120" font-size="12">trig→hardirq</text>
<text x="468" y="140" font-size="12">= timestamp_ns − T0</text>
<text x="468" y="200" font-size="12">hardirq→user</text>
<text x="468" y="220" font-size="12">= T3 − timestamp_ns</text>
</svg>
```

그림 4 — gpio-sim 경로(소스 `drivers/gpio/gpio-sim.c`, `kernel/irq/irq_sim.c`, `drivers/gpio/gpiolib-cdev.c` 6.18.55 기준). sysfs write가 `irq_work_queue()`로 IRQ를 "주입"하고, irq_sim의 irq_work는 `IRQ_WORK_INIT_HARD`라 RT에서도 hard IRQ(self-IPI)로 돈다. gpiolib-cdev는 `request_threaded_irq()`로 hard 핸들러(타임스탬프만)와 스레드 핸들러(kfifo에 넣고 깨우기)를 나눈다.

### 7.4 예제 10 — 두 커널에서

대기 중 스레드 목록과 IRQ 줄(Image-trace):

```text
== threads while waiting
  162  FF     90   1 s5_gpio
  162  TS      -   0 s5_gpio
  164  FF     50   0 irq/29-s5_gpio
 29:       1156          0  irq_sim   0 Edge      s5_gpio
```

Image-rt에서는 같은 줄 위에 `irq_work/0`, `irq_work/1`(FF 1) 스레드가 더 보인다(PREEMPT_RT는 HARD가 아닌 irq_work를 스레드에서 처리한다).

```text
   23  FF      1   0 irq_work/0
   26  FF      1   1 irq_work/1
  182  FF     90   1 s5_gpio
  182  TS      -   0 s5_gpio
  184  FF     50   0 irq/29-s5_gpio
 29:       1273          0  irq_sim   0 Edge      s5_gpio
```

측정(3000 이벤트, idle / stress):

| 커널 | 조건 | trig→hardirq p50 / p99 | hardirq→user p50 / p99 | total p99 / max |
|---|---|---|---|---|
| PREEMPT | idle | 88.8 / 311.1 | 301.6 / 1006.6 | 1254.3 / 11402.6 |
| PREEMPT | stress | 94.0 / 312.7 | 351.2 / 4393.2 | 4575.6 / 21475.0 |
| PREEMPT_RT | idle | 61.2 / 201.0 | 174.4 / 560.8 | 656.1 / 10429.7 |
| PREEMPT_RT | stress | 93.6 / 824.3 | 338.0 / 1156.8 | 1677.4 / 23844.3 |

(단위 µs, 원출력은 `s5_gpio`가 찍은 `trig->hardirq p50 ... max ... us` 줄들.)

출력에서 볼 것:

- **irq 스레드 `irq/29-s5_gpio`가 FIFO 50으로 CPU0에 있다.** threaded IRQ의 기본값이다(`irq/<번호>-<이름>`, SCHED_FIFO 50). hard IRQ가 CPU0(토글러가 write한 CPU, self-IPI)에서 났으니 스레드도 CPU0에서 돌고, 그다음 CPU1의 대기자를 **CPU 간 IPI**로 깨운다. 그래서 이 경로의 hardirq→user가 hrtimer 경로의 wake보다 길다(스레드 전환 + 원격 깨우기 두 단계).
- stress에서 PREEMPT의 hardirq→user p99가 4.4 ms로 뛴다. stress-ng가 CPU0에 있고, irq 스레드가 거기서 같이 경쟁하기 때문이다. 실무 처방은 IRQ affinity로 이 IRQ를 RT CPU 쪽(또는 조용한 CPU)으로 옮기고, irq 스레드 우선순위를 `chrt`로 조정하는 것이다(N2 §4, §8).
- `/proc/interrupts`의 `irq_sim 0 Edge s5_gpio` — 칩이 `irq_sim` 도메인, 이름은 우리가 준 consumer 문자열이다.
- 이 경로의 원점(T0)은 sysfs write 직전 유저 시각이라 "IRQ latency"가 아니라 "소프트웨어 트리거 → hard IRQ"다. 그래서 RT vs non-RT 판단은 §5의 hrtimer 경로로 하고, 이 표는 "실제 드라이버 경로의 모양"을 보여 주는 용도다.

---

## 8. Step 7 — cyclictest + rtla timerlat으로 교차 확인

### 8.1 왜 직접 만든 측정기만으로는 안 되나

내 측정기가 맞는지, 표준 도구와 같은 경향을 보이는지 확인해야 한다(오실로스코프를 믿기 전에 기준 신호를 넣는 것과 같다). cyclictest는 업계 표준 숫자(유저 깨어남 지연), timerlat은 같은 것을 IRQ / Thr / Usr 단계로 쪼갠다(N3 §2, §7). 세 측정기 모두 같은 CPU, 같은 주기, 같은 시간이다.

### 8.2 결과 — 두 번 돌렸다

같은 매트릭스를 두 번 돌렸다: 1차(16:50~17:08, VM 호스트 load average 1.8~7.0 — 다른 작성자들이 동시에 빌드 중), 2차(17:15~17:26, load average 0.7~1.2). 아래는 `host/plot.py`가 원자료에서 계산한 p99(µs)다.

| 측정기 · 커널 | idle 1차 / 2차 | stress 1차 / 2차 | hog 1차 / 2차 |
|---|---|---|---|
| s5_waiter total · PREEMPT | 1336 / 482 | 1754 / 521 | **4038 / 3966** |
| s5_waiter total · PREEMPT_RT | 750 / 1474 | 3761 / 381 | 1408 / 534 |
| s5_waiter wake · PREEMPT | 514 / 268 | 500 / 230 | **3894 / 3773** |
| s5_waiter wake · PREEMPT_RT | 319 / 521 | 1630 / 135 | 388 / 254 |
| cyclictest · PREEMPT | 1568 / 899 | 3148 / 340 | **3434 / 3756** |
| cyclictest · PREEMPT_RT | 636 / 1219 | 2914 / 273 | 1719 / 350 |
| timerlat Thr · PREEMPT | 610 / 690 | 1570 / 400 | **3960 / 3870** |
| timerlat Thr · PREEMPT_RT | 710 / 670 | 2410 / 550 | 1290 / 740 |

missed periods(s5_waiter, 30000주기 중): PREEMPT hog 2135 / 2013, PREEMPT_RT hog 116 / 13.

cyclictest 30초 히스토그램을 같은 축에 겹치면 이렇다(1차 실행).

```svg
<svg viewBox="0 0 660 360" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="24" font-size="14">cyclictest 30 s, CPU1, FIFO 95 — PREEMPT vs PREEMPT_RT (QEMU TCG)</text>
<line x1="70" y1="290" x2="600" y2="290" stroke="currentColor"/>
<line x1="70" y1="60" x2="70" y2="290" stroke="currentColor"/>
<line x1="70.0" y1="290" x2="70.0" y2="294" stroke="currentColor"/>
<text x="70.0" y="308" font-size="12" text-anchor="middle">10us</text>
<line x1="202.5" y1="290" x2="202.5" y2="294" stroke="currentColor"/>
<text x="202.5" y="308" font-size="12" text-anchor="middle">100us</text>
<line x1="335.0" y1="290" x2="335.0" y2="294" stroke="currentColor"/>
<text x="335.0" y="308" font-size="12" text-anchor="middle">1ms</text>
<line x1="467.5" y1="290" x2="467.5" y2="294" stroke="currentColor"/>
<text x="467.5" y="308" font-size="12" text-anchor="middle">10ms</text>
<line x1="600.0" y1="290" x2="600.0" y2="294" stroke="currentColor"/>
<text x="600.0" y="308" font-size="12" text-anchor="middle">100ms</text>
<line x1="66" y1="251.7" x2="70" y2="251.7" stroke="currentColor"/>
<text x="62" y="255.7" font-size="12" text-anchor="end">1e0</text>
<line x1="66" y1="213.3" x2="70" y2="213.3" stroke="currentColor"/>
<text x="62" y="217.3" font-size="12" text-anchor="end">1e1</text>
<line x1="66" y1="175.0" x2="70" y2="175.0" stroke="currentColor"/>
<text x="62" y="179.0" font-size="12" text-anchor="end">1e2</text>
<line x1="66" y1="136.7" x2="70" y2="136.7" stroke="currentColor"/>
<text x="62" y="140.7" font-size="12" text-anchor="end">1e3</text>
<line x1="66" y1="98.3" x2="70" y2="98.3" stroke="currentColor"/>
<text x="62" y="102.3" font-size="12" text-anchor="end">1e4</text>
<line x1="66" y1="60.0" x2="70" y2="60.0" stroke="currentColor"/>
<text x="62" y="64.0" font-size="12" text-anchor="end">1e5</text>
<text x="335" y="326" font-size="12" text-anchor="middle">latency (log)</text>
<text x="20" y="48" font-size="12">count (log)</text>
<polyline points="70.0,290.0 86.6,290.0 86.6,290.0 103.1,290.0 103.1,290.0 119.7,290.0 119.7,290.0 136.2,290.0 136.2,290.0 152.8,290.0 152.8,290.0 169.4,290.0 169.4,251.7 185.9,251.7 185.9,194.0 202.5,194.0 202.5,175.0 219.1,175.0 219.1,135.7 235.6,135.7 235.6,108.5 252.2,108.5 252.2,96.9 268.8,96.9 268.8,104.1 285.3,104.1 285.3,124.1 301.9,124.1 301.9,144.1 318.4,144.1 318.4,149.7 335.0,149.7 335.0,164.0 351.6,164.0 351.6,175.2 368.1,175.2 368.1,182.7 384.7,182.7 384.7,186.9 401.2,186.9 401.2,191.1 417.8,191.1 417.8,198.1 434.4,198.1 434.4,204.5 450.9,204.5 450.9,206.6 467.5,206.6 467.5,228.6 484.1,228.6 484.1,213.3 500.6,213.3 500.6,240.1 517.2,240.1 517.2,290.0 533.8,290.0 533.8,290.0 550.3,290.0 550.3,290.0 566.9,290.0 566.9,290.0 583.4,290.0 583.4,290.0 600.0,290.0" fill="none" stroke="#4a7bd0" stroke-width="1.8" stroke-dasharray="5 3"/>
<line x1="420" y1="70" x2="442" y2="70" stroke="#4a7bd0" stroke-width="2" stroke-dasharray="5 3"/>
<text x="448" y="74" font-size="12">trace idle</text>
<polyline points="70.0,290.0 86.6,290.0 86.6,290.0 103.1,290.0 103.1,290.0 119.7,290.0 119.7,290.0 136.2,290.0 136.2,290.0 152.8,290.0 152.8,217.0 169.4,217.0 169.4,187.6 185.9,187.6 185.9,159.8 202.5,159.8 202.5,133.1 219.1,133.1 219.1,130.9 235.6,130.9 235.6,105.3 252.2,105.3 252.2,88.6 268.8,88.6 268.8,144.1 285.3,144.1 285.3,135.8 301.9,135.8 301.9,155.3 318.4,155.3 318.4,182.7 335.0,182.7 335.0,196.2 351.6,196.2 351.6,206.6 368.1,206.6 368.1,210.3 384.7,210.3 384.7,206.6 401.2,206.6 401.2,211.7 417.8,211.7 417.8,224.9 434.4,224.9 434.4,217.0 450.9,217.0 450.9,251.7 467.5,251.7 467.5,228.6 484.1,228.6 484.1,251.7 500.6,251.7 500.6,251.7 517.2,251.7 517.2,290.0 533.8,290.0 533.8,290.0 550.3,290.0 550.3,290.0 566.9,290.0 566.9,290.0 583.4,290.0 583.4,290.0 600.0,290.0" fill="none" stroke="#e08a3c" stroke-width="1.8" stroke-dasharray="5 3"/>
<line x1="420" y1="88" x2="442" y2="88" stroke="#e08a3c" stroke-width="2" stroke-dasharray="5 3"/>
<text x="448" y="92" font-size="12">rt idle</text>
<polyline points="70.0,290.0 86.6,290.0 86.6,290.0 103.1,290.0 103.1,290.0 119.7,290.0 119.7,290.0 136.2,290.0 136.2,290.0 152.8,290.0 152.8,290.0 169.4,290.0 169.4,228.6 185.9,228.6 185.9,193.5 202.5,193.5 202.5,142.6 219.1,142.6 219.1,106.4 235.6,106.4 235.6,96.0 252.2,96.0 252.2,110.8 268.8,110.8 268.8,122.1 285.3,122.1 285.3,140.3 301.9,140.3 301.9,163.8 318.4,163.8 318.4,172.5 335.0,172.5 335.0,173.1 351.6,173.1 351.6,180.7 368.1,180.7 368.1,197.4 384.7,197.4 384.7,198.1 401.2,198.1 401.2,144.2 417.8,144.2 417.8,199.5 434.4,199.5 434.4,219.3 450.9,219.3 450.9,240.1 467.5,240.1 467.5,251.7 484.1,251.7 484.1,233.4 500.6,233.4 500.6,290.0 517.2,290.0 517.2,290.0 533.8,290.0 533.8,290.0 550.3,290.0 550.3,290.0 566.9,290.0 566.9,290.0 583.4,290.0 583.4,290.0 600.0,290.0" fill="none" stroke="#4a7bd0" stroke-width="1.8"/>
<line x1="420" y1="106" x2="442" y2="106" stroke="#4a7bd0" stroke-width="2"/>
<text x="448" y="110" font-size="12">trace hog</text>
<polyline points="70.0,290.0 86.6,290.0 86.6,290.0 103.1,290.0 103.1,290.0 119.7,290.0 119.7,290.0 136.2,290.0 136.2,290.0 152.8,290.0 152.8,251.7 169.4,251.7 169.4,173.0 185.9,173.0 185.9,151.3 202.5,151.3 202.5,145.6 219.1,145.6 219.1,138.2 235.6,138.2 235.6,125.0 252.2,125.0 252.2,109.3 268.8,109.3 268.8,112.7 285.3,112.7 285.3,99.3 301.9,99.3 301.9,109.3 318.4,109.3 318.4,151.8 335.0,151.8 335.0,169.8 351.6,169.8 351.6,173.9 368.1,173.9 368.1,175.3 384.7,175.3 384.7,177.1 401.2,177.1 401.2,197.4 417.8,197.4 417.8,204.5 434.4,204.5 434.4,217.0 450.9,217.0 450.9,219.3 467.5,219.3 467.5,240.1 484.1,240.1 484.1,240.1 500.6,240.1 500.6,290.0 517.2,290.0 517.2,290.0 533.8,290.0 533.8,290.0 550.3,290.0 550.3,290.0 566.9,290.0 566.9,290.0 583.4,290.0 583.4,290.0 600.0,290.0" fill="none" stroke="#e08a3c" stroke-width="1.8"/>
<line x1="420" y1="124" x2="442" y2="124" stroke="#e08a3c" stroke-width="2"/>
<text x="448" y="128" font-size="12">rt hog</text>
<text x="20" y="350" font-size="12">dashed = idle, solid = s5_hog (spinlock_t 4 ms / 40 ms on CPU1)</text>
</svg>
```

그림 5 — cyclictest 히스토그램, PREEMPT(파랑) vs PREEMPT_RT(주황), 점선 idle / 실선 hog(실측, 로그-로그). PREEMPT + hog에는 2~5 ms에 **섬**(봉우리 밖의 작은 무리)이 생긴다 — 섬의 위치는 범인의 길이(4 ms), 높이는 빈도(40 ms마다)다(N3 §4.5). PREEMPT_RT + hog에는 그 섬이 없다. 20 ms 이상의 꼬리는 네 곡선 모두에 있다 — 호스트 정지다.

s5_waiter의 구간을 나눠 그리면 "어느 구간이 움직였나"가 바로 보인다.

```svg
<svg viewBox="0 0 660 360" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="24" font-size="14">s5_waiter segments under s5_hog — which segment moves?</text>
<line x1="70" y1="290" x2="600" y2="290" stroke="currentColor"/>
<line x1="70" y1="60" x2="70" y2="290" stroke="currentColor"/>
<line x1="70.0" y1="290" x2="70.0" y2="294" stroke="currentColor"/>
<text x="70.0" y="308" font-size="12" text-anchor="middle">10us</text>
<line x1="202.5" y1="290" x2="202.5" y2="294" stroke="currentColor"/>
<text x="202.5" y="308" font-size="12" text-anchor="middle">100us</text>
<line x1="335.0" y1="290" x2="335.0" y2="294" stroke="currentColor"/>
<text x="335.0" y="308" font-size="12" text-anchor="middle">1ms</text>
<line x1="467.5" y1="290" x2="467.5" y2="294" stroke="currentColor"/>
<text x="467.5" y="308" font-size="12" text-anchor="middle">10ms</text>
<line x1="600.0" y1="290" x2="600.0" y2="294" stroke="currentColor"/>
<text x="600.0" y="308" font-size="12" text-anchor="middle">100ms</text>
<line x1="66" y1="251.7" x2="70" y2="251.7" stroke="currentColor"/>
<text x="62" y="255.7" font-size="12" text-anchor="end">1e0</text>
<line x1="66" y1="213.3" x2="70" y2="213.3" stroke="currentColor"/>
<text x="62" y="217.3" font-size="12" text-anchor="end">1e1</text>
<line x1="66" y1="175.0" x2="70" y2="175.0" stroke="currentColor"/>
<text x="62" y="179.0" font-size="12" text-anchor="end">1e2</text>
<line x1="66" y1="136.7" x2="70" y2="136.7" stroke="currentColor"/>
<text x="62" y="140.7" font-size="12" text-anchor="end">1e3</text>
<line x1="66" y1="98.3" x2="70" y2="98.3" stroke="currentColor"/>
<text x="62" y="102.3" font-size="12" text-anchor="end">1e4</text>
<line x1="66" y1="60.0" x2="70" y2="60.0" stroke="currentColor"/>
<text x="62" y="64.0" font-size="12" text-anchor="end">1e5</text>
<text x="335" y="326" font-size="12" text-anchor="middle">latency (log)</text>
<text x="20" y="48" font-size="12">count (log)</text>
<polyline points="70.0,290.0 86.6,290.0 86.6,290.0 103.1,290.0 103.1,251.7 119.7,251.7 119.7,221.8 136.2,221.8 136.2,183.5 152.8,183.5 152.8,144.9 169.4,144.9 169.4,93.3 185.9,93.3 185.9,99.6 202.5,99.6 202.5,117.3 219.1,117.3 219.1,130.1 235.6,130.1 235.6,141.4 252.2,141.4 252.2,157.2 268.8,157.2 268.8,180.7 285.3,180.7 285.3,190.7 301.9,190.7 301.9,206.6 318.4,206.6 318.4,213.3 335.0,213.3 335.0,224.9 351.6,224.9 351.6,224.9 368.1,224.9 368.1,228.6 384.7,228.6 384.7,210.3 401.2,210.3 401.2,143.7 417.8,143.7 417.8,221.8 434.4,221.8 434.4,233.4 450.9,233.4 450.9,240.1 467.5,240.1 467.5,251.7 484.1,251.7 484.1,290.0 500.6,290.0 500.6,290.0 517.2,290.0 517.2,290.0 533.8,290.0 533.8,290.0 550.3,290.0 550.3,290.0 566.9,290.0 566.9,290.0 583.4,290.0 583.4,290.0 600.0,290.0" fill="none" stroke="#4a7bd0" stroke-width="1.8"/>
<line x1="420" y1="70" x2="442" y2="70" stroke="#4a7bd0" stroke-width="2"/>
<text x="448" y="74" font-size="12">trace hog: wake</text>
<polyline points="70.0,206.6 86.6,206.6 86.6,209.0 103.1,209.0 103.1,221.8 119.7,221.8 119.7,210.3 136.2,210.3 136.2,198.8 152.8,198.8 152.8,180.5 169.4,180.5 169.4,150.4 185.9,150.4 185.9,119.7 202.5,119.7 202.5,98.9 219.1,98.9 219.1,92.7 235.6,92.7 235.6,124.3 252.2,124.3 252.2,148.7 268.8,148.7 268.8,172.0 285.3,172.0 285.3,180.9 301.9,180.9 301.9,185.6 318.4,185.6 318.4,192.5 335.0,192.5 335.0,200.2 351.6,200.2 351.6,201.0 368.1,201.0 368.1,204.5 384.7,204.5 384.7,209.0 401.2,209.0 401.2,221.8 417.8,221.8 417.8,228.6 434.4,228.6 434.4,228.6 450.9,228.6 450.9,228.6 467.5,228.6 467.5,251.7 484.1,251.7 484.1,233.4 500.6,233.4 500.6,233.4 517.2,233.4 517.2,290.0 533.8,290.0 533.8,290.0 550.3,290.0 550.3,290.0 566.9,290.0 566.9,290.0 583.4,290.0 583.4,290.0 600.0,290.0" fill="none" stroke="#4a7bd0" stroke-width="1.8" stroke-dasharray="5 3"/>
<line x1="420" y1="88" x2="442" y2="88" stroke="#4a7bd0" stroke-width="2" stroke-dasharray="5 3"/>
<text x="448" y="92" font-size="12">trace hog: irq</text>
<polyline points="70.0,290.0 86.6,290.0 86.6,290.0 103.1,290.0 103.1,290.0 119.7,290.0 119.7,210.3 136.2,210.3 136.2,175.3 152.8,175.3 152.8,149.1 169.4,149.1 169.4,115.3 185.9,115.3 185.9,103.8 202.5,103.8 202.5,107.4 219.1,107.4 219.1,106.8 235.6,106.8 235.6,111.4 252.2,111.4 252.2,128.2 268.8,128.2 268.8,156.7 285.3,156.7 285.3,176.6 301.9,176.6 301.9,189.8 318.4,189.8 318.4,194.0 335.0,194.0 335.0,187.6 351.6,187.6 351.6,211.7 368.1,211.7 368.1,219.3 384.7,219.3 384.7,224.9 401.2,224.9 401.2,233.4 417.8,233.4 417.8,224.9 434.4,224.9 434.4,251.7 450.9,251.7 450.9,290.0 467.5,290.0 467.5,251.7 484.1,251.7 484.1,290.0 500.6,290.0 500.6,290.0 517.2,290.0 517.2,290.0 533.8,290.0 533.8,290.0 550.3,290.0 550.3,290.0 566.9,290.0 566.9,290.0 583.4,290.0 583.4,290.0 600.0,290.0" fill="none" stroke="#e08a3c" stroke-width="1.8"/>
<line x1="420" y1="106" x2="442" y2="106" stroke="#e08a3c" stroke-width="2"/>
<text x="448" y="110" font-size="12">rt hog: wake</text>
<polyline points="70.0,290.0 86.6,290.0 86.6,251.7 103.1,251.7 103.1,290.0 119.7,290.0 119.7,190.7 136.2,190.7 136.2,162.3 152.8,162.3 152.8,165.7 169.4,165.7 169.4,157.0 185.9,157.0 185.9,148.8 202.5,148.8 202.5,138.2 219.1,138.2 219.1,132.3 235.6,132.3 235.6,126.9 252.2,126.9 252.2,118.9 268.8,118.9 268.8,88.4 285.3,88.4 285.3,117.4 301.9,117.4 301.9,159.9 318.4,159.9 318.4,179.4 335.0,179.4 335.0,180.2 351.6,180.2 351.6,181.7 368.1,181.7 368.1,187.6 384.7,187.6 384.7,201.0 401.2,201.0 401.2,207.7 417.8,207.7 417.8,209.0 434.4,209.0 434.4,233.4 450.9,233.4 450.9,251.7 467.5,251.7 467.5,233.4 484.1,233.4 484.1,240.1 500.6,240.1 500.6,240.1 517.2,240.1 517.2,290.0 533.8,290.0 533.8,290.0 550.3,290.0 550.3,290.0 566.9,290.0 566.9,290.0 583.4,290.0 583.4,290.0 600.0,290.0" fill="none" stroke="#e08a3c" stroke-width="1.8" stroke-dasharray="5 3"/>
<line x1="420" y1="124" x2="442" y2="124" stroke="#e08a3c" stroke-width="2" stroke-dasharray="5 3"/>
<text x="448" y="128" font-size="12">rt hog: irq</text>
<text x="20" y="350" font-size="12">solid = hardirq -> reader running (wake), dashed = expire -> hardirq (irq)</text>
</svg>
```

그림 6 — s5_hog 부하에서 s5_waiter 구간별 히스토그램(실측). 실선 = wake, 점선 = irq. PREEMPT의 wake(파랑 실선)만 4 ms 근처에 섬이 있다. irq 구간(점선)은 두 커널이 비슷하다 — 락 보유는 IRQ를 막지 않았다.

### 8.3 읽는 법

- **세 측정기가 같은 결론을 낸다**: PREEMPT + hog에서 p99가 3.4~4.0 ms로 뛰고(1차·2차 모두, 오차 10% 안), PREEMPT_RT + hog에서는 그 섬이 없다. timerlat은 그 증가가 **IRQ 단계가 아니라 Thr 단계**라고 말한다(IRQ p99 860 / 280 µs, Thr p99 3960 / 3870 µs) — 우리 측정기의 "irq는 그대로, wake만 증가"와 같은 결론이다. 이것이 서로 다른 세 측정기의 교차 확인이다.
- **stress와 idle의 차이는 주장할 수 없다.** PREEMPT_RT stress의 p99가 1차 3761, 2차 381 — **열 배**가 같은 커널, 같은 명령에서 달라졌다. 1차 때 호스트 load가 높았다. 실행 간 흔들림이 커널 간 차이보다 크다. N3 §10.2의 결론과 같다.
- **TCG에서는 idle이 stress보다 나쁠 때가 많다**(2차: PREEMPT_RT idle 1474 vs stress 381). vCPU가 idle(WFI)로 들어가면 QEMU가 호스트 스레드를 재우고, 깨어날 때 호스트 스케줄러를 거친다. 바쁜 vCPU는 그 비용이 없다. 실제 보드의 cpuidle 탈출 비용과 같은 모양이라, cyclictest가 기본으로 `/dev/cpu_dma_latency`에 0을 쓰는 이유도 여기 있다(N1 §3, N5). s5_waiter는 그 설정을 하지 않으므로 idle에서 cyclictest와 숫자가 다를 수 있다 — 확장 과제다.

---

## 9. Step 8 — CPU 격리 (isolcpus / nohz_full)

### 9.1 부트 인자

| 커널 | 추가 인자 |
|---|---|
| Image-trace | `isolcpus=1 irqaffinity=0` (NO_HZ_FULL, RCU_NOCB_CPU 없음) |
| Image-rt | `isolcpus=1 nohz_full=1 rcu_nocbs=1 irqaffinity=0` |

```sh
S5_TIMEOUT=1500 host/run_guest.sh trace matrix iso S5D=30 isolcpus=1 irqaffinity=0
S5_TIMEOUT=1500 host/run_guest.sh rt    matrix iso S5D=30 isolcpus=1 nohz_full=1 rcu_nocbs=1 irqaffinity=0
```

### 9.2 예제 11 — 격리가 "실제로 먹혔는지" 먼저 증명한다

결과를 보기 전에 격리 자체를 확인한다(N2 §6에서 강조한 순서). `phase_isocheck.sh`는 init의 affinity, stress-ng 작업자가 도는 CPU, CPU별 IRQ 수를 본다. Image-rt, 격리 부팅:

```text
isolated=1 nohz_full=1
Cpus_allowed_list:	0
      1   0 stress-ng
      2   0 stress-ng-cpu
      1   0 stress-ng-io
      2   0 stress-ng-vm
           CPU0       CPU1       
 11:       7741        205 GIC-0  27 Level     arch_timer
 19:       3464          0 GICv2m-PCI-MSIX-0000:00:03.0   1 
 20:          0          0 GICv2m-PCI-MSIX-0000:00:03.0   2 
IPI0:        26         18       Rescheduling interrupts
IPI1:         0         18       Function call interrupts
```

같은 커널, 격리 인자 없이:

```text
isolated= nohz_full=(null)
Cpus_allowed_list:	0-1
      1   0 stress-ng-cpu
      2   0 stress-ng-vm
      1   1 stress-ng
      1   1 stress-ng-cpu
      1   1 stress-ng-io
           CPU0       CPU1       
 11:       6897       6285 GIC-0  27 Level     arch_timer
 19:       2667          0 GICv2m-PCI-MSIX-0000:00:03.0   1 
 20:          0       1976 GICv2m-PCI-MSIX-0000:00:03.0   2 
IPI0:      2408       2625       Rescheduling interrupts
IPI1:       977       1069       Function call interrupts
```

(`/proc/interrupts`는 virtio 줄 일부만 남김.) 출력에서 볼 것:

- **init(PID 1)의 허용 CPU가 `0`뿐** → 그 자식인 stress-ng 작업자가 전부 CPU0에 있다. 격리하지 않으면 CPU1에도 3개가 있다. `isolcpus=`는 "명시적으로 고정하지 않은 태스크는 격리 CPU에 오지 않는다"이다(N2 §6). 그래서 게스트 `nproc`이 격리 부팅에서 1을 출력했다(`nproc`은 affinity를 센다).
- **CPU1의 arch_timer IRQ가 205번 vs 6285번** — `nohz_full=1`로 CPU1의 주기 tick이 멈췄다(HZ=1000이면 약 6초에 6000번이어야 한다).
- **IRQ 20(virtio 큐 하나)이 CPU1에서 1976번 → 0번** — `irqaffinity=0`이 옮겼다. 다만 이것은 이 게스트에서 그 큐가 managed IRQ가 아니었기 때문이다. NVMe·멀티큐 NIC 같은 managed IRQ는 `irqaffinity`로 못 옮긴다(N2 §8).

### 9.3 결과

| 측정기 p99 (µs) | PREEMPT 비격리 1차 / 2차 | PREEMPT 격리 | PREEMPT_RT 비격리 1차 / 2차 | PREEMPT_RT 격리 |
|---|---|---|---|---|
| s5_waiter total, idle | 1336 / 482 | 1865 | 750 / 1474 | 1420 |
| s5_waiter total, stress | 1754 / 521 | 769 | 3761 / 381 | **280** |
| s5_waiter total, hog | 4038 / 3966 | 3510 | 1408 / 534 | 413 |
| cyclictest, stress | 3148 / 340 | 355 | 2914 / 273 | 307 |
| timerlat Thr, stress | 1570 / 400 | 360 | 2410 / 550 | 480 |

```svg
<svg viewBox="0 0 660 360" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="24" font-size="14">cyclictest 30 s under stress-ng — without vs with isolcpus=1</text>
<line x1="70" y1="290" x2="600" y2="290" stroke="currentColor"/>
<line x1="70" y1="60" x2="70" y2="290" stroke="currentColor"/>
<line x1="70.0" y1="290" x2="70.0" y2="294" stroke="currentColor"/>
<text x="70.0" y="308" font-size="12" text-anchor="middle">10us</text>
<line x1="202.5" y1="290" x2="202.5" y2="294" stroke="currentColor"/>
<text x="202.5" y="308" font-size="12" text-anchor="middle">100us</text>
<line x1="335.0" y1="290" x2="335.0" y2="294" stroke="currentColor"/>
<text x="335.0" y="308" font-size="12" text-anchor="middle">1ms</text>
<line x1="467.5" y1="290" x2="467.5" y2="294" stroke="currentColor"/>
<text x="467.5" y="308" font-size="12" text-anchor="middle">10ms</text>
<line x1="600.0" y1="290" x2="600.0" y2="294" stroke="currentColor"/>
<text x="600.0" y="308" font-size="12" text-anchor="middle">100ms</text>
<line x1="66" y1="251.7" x2="70" y2="251.7" stroke="currentColor"/>
<text x="62" y="255.7" font-size="12" text-anchor="end">1e0</text>
<line x1="66" y1="213.3" x2="70" y2="213.3" stroke="currentColor"/>
<text x="62" y="217.3" font-size="12" text-anchor="end">1e1</text>
<line x1="66" y1="175.0" x2="70" y2="175.0" stroke="currentColor"/>
<text x="62" y="179.0" font-size="12" text-anchor="end">1e2</text>
<line x1="66" y1="136.7" x2="70" y2="136.7" stroke="currentColor"/>
<text x="62" y="140.7" font-size="12" text-anchor="end">1e3</text>
<line x1="66" y1="98.3" x2="70" y2="98.3" stroke="currentColor"/>
<text x="62" y="102.3" font-size="12" text-anchor="end">1e4</text>
<line x1="66" y1="60.0" x2="70" y2="60.0" stroke="currentColor"/>
<text x="62" y="64.0" font-size="12" text-anchor="end">1e5</text>
<text x="335" y="326" font-size="12" text-anchor="middle">latency (log)</text>
<text x="20" y="48" font-size="12">count (log)</text>
<polyline points="70.0,290.0 86.6,290.0 86.6,290.0 103.1,290.0 103.1,290.0 119.7,290.0 119.7,290.0 136.2,290.0 136.2,290.0 152.8,290.0 152.8,290.0 169.4,290.0 169.4,290.0 185.9,290.0 185.9,240.1 202.5,240.1 202.5,192.5 219.1,192.5 219.1,148.6 235.6,148.6 235.6,114.9 252.2,114.9 252.2,103.3 268.8,103.3 268.8,104.8 285.3,104.8 285.3,116.0 301.9,116.0 301.9,132.9 318.4,132.9 318.4,132.2 335.0,132.2 335.0,140.5 351.6,140.5 351.6,154.3 368.1,154.3 368.1,156.8 384.7,156.8 384.7,164.3 401.2,164.3 401.2,175.9 417.8,175.9 417.8,181.7 434.4,181.7 434.4,191.1 450.9,191.1 450.9,204.5 467.5,204.5 467.5,203.5 484.1,203.5 484.1,209.0 500.6,209.0 500.6,228.6 517.2,228.6 517.2,290.0 533.8,290.0 533.8,290.0 550.3,290.0 550.3,290.0 566.9,290.0 566.9,290.0 583.4,290.0 583.4,290.0 600.0,290.0" fill="none" stroke="#4a7bd0" stroke-width="1.8"/>
<line x1="420" y1="70" x2="442" y2="70" stroke="#4a7bd0" stroke-width="2"/>
<text x="448" y="74" font-size="12">trace stress</text>
<polyline points="70.0,290.0 86.6,290.0 86.6,290.0 103.1,290.0 103.1,290.0 119.7,290.0 119.7,290.0 136.2,290.0 136.2,290.0 152.8,290.0 152.8,199.5 169.4,199.5 169.4,162.2 185.9,162.2 185.9,166.8 202.5,166.8 202.5,102.1 219.1,102.1 219.1,89.3 235.6,89.3 235.6,117.1 252.2,117.1 252.2,146.0 268.8,146.0 268.8,160.2 285.3,160.2 285.3,180.9 301.9,180.9 301.9,199.5 318.4,199.5 318.4,204.5 335.0,204.5 335.0,209.0 351.6,209.0 351.6,206.6 368.1,206.6 368.1,215.1 384.7,215.1 384.7,215.1 401.2,215.1 401.2,213.3 417.8,213.3 417.8,224.9 434.4,224.9 434.4,251.7 450.9,251.7 450.9,251.7 467.5,251.7 467.5,251.7 484.1,251.7 484.1,240.1 500.6,240.1 500.6,251.7 517.2,251.7 517.2,290.0 533.8,290.0 533.8,290.0 550.3,290.0 550.3,290.0 566.9,290.0 566.9,290.0 583.4,290.0 583.4,290.0 600.0,290.0" fill="none" stroke="#3f9a6b" stroke-width="1.8"/>
<line x1="420" y1="88" x2="442" y2="88" stroke="#3f9a6b" stroke-width="2"/>
<text x="448" y="92" font-size="12">trace-iso stress</text>
<polyline points="70.0,290.0 86.6,290.0 86.6,290.0 103.1,290.0 103.1,290.0 119.7,290.0 119.7,290.0 136.2,290.0 136.2,290.0 152.8,290.0 152.8,290.0 169.4,290.0 169.4,290.0 185.9,290.0 185.9,211.7 202.5,211.7 202.5,154.9 219.1,154.9 219.1,114.5 235.6,114.5 235.6,118.3 252.2,118.3 252.2,151.6 268.8,151.6 268.8,123.6 285.3,123.6 285.3,102.8 301.9,102.8 301.9,107.8 318.4,107.8 318.4,128.3 335.0,128.3 335.0,149.4 351.6,149.4 351.6,149.2 368.1,149.2 368.1,161.7 384.7,161.7 384.7,149.3 401.2,149.3 401.2,176.2 417.8,176.2 417.8,187.6 434.4,187.6 434.4,198.1 450.9,198.1 450.9,201.8 467.5,201.8 467.5,215.1 484.1,215.1 484.1,228.6 500.6,228.6 500.6,290.0 517.2,290.0 517.2,290.0 533.8,290.0 533.8,290.0 550.3,290.0 550.3,290.0 566.9,290.0 566.9,290.0 583.4,290.0 583.4,290.0 600.0,290.0" fill="none" stroke="#e08a3c" stroke-width="1.8"/>
<line x1="420" y1="106" x2="442" y2="106" stroke="#e08a3c" stroke-width="2"/>
<text x="448" y="110" font-size="12">rt stress</text>
<polyline points="70.0,290.0 86.6,290.0 86.6,290.0 103.1,290.0 103.1,290.0 119.7,290.0 119.7,290.0 136.2,290.0 136.2,290.0 152.8,290.0 152.8,290.0 169.4,290.0 169.4,290.0 185.9,290.0 185.9,211.7 202.5,211.7 202.5,132.2 219.1,132.2 219.1,106.9 235.6,106.9 235.6,86.5 252.2,86.5 252.2,125.6 268.8,125.6 268.8,171.4 285.3,171.4 285.3,179.8 301.9,179.8 301.9,206.6 318.4,206.6 318.4,211.7 335.0,211.7 335.0,224.9 351.6,224.9 351.6,251.7 368.1,251.7 368.1,219.3 384.7,219.3 384.7,240.1 401.2,240.1 401.2,213.3 417.8,213.3 417.8,228.6 434.4,228.6 434.4,228.6 450.9,228.6 450.9,233.4 467.5,233.4 467.5,240.1 484.1,240.1 484.1,251.7 500.6,251.7 500.6,290.0 517.2,290.0 517.2,290.0 533.8,290.0 533.8,290.0 550.3,290.0 550.3,290.0 566.9,290.0 566.9,290.0 583.4,290.0 583.4,290.0 600.0,290.0" fill="none" stroke="#d0564a" stroke-width="1.8"/>
<line x1="420" y1="124" x2="442" y2="124" stroke="#d0564a" stroke-width="2"/>
<text x="448" y="128" font-size="12">rt-iso stress</text>
<text x="20" y="350" font-size="12">iso: isolcpus=1 irqaffinity=0 (+nohz_full=1 rcu_nocbs=1 on rt)</text>
</svg>
```

그림 7 — stress-ng 부하에서 cyclictest 히스토그램, 비격리(1차) vs 격리(실측). 격리 곡선이 왼쪽으로 몰려 보인다.

```svg
<svg viewBox="0 0 660 360" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="24" font-size="14">s5_waiter end-to-end p99 (expire -> user), per kernel / scenario</text>
<line x1="70" y1="290" x2="600" y2="290" stroke="currentColor"/>
<line x1="70" y1="60" x2="70" y2="290" stroke="currentColor"/>
<line x1="66" y1="290.0" x2="600" y2="290.0" stroke="currentColor" stroke-opacity="0.2"/>
<text x="62" y="294.0" font-size="12" text-anchor="end">100us</text>
<line x1="66" y1="213.3" x2="600" y2="213.3" stroke="currentColor" stroke-opacity="0.2"/>
<text x="62" y="217.3" font-size="12" text-anchor="end">1ms</text>
<line x1="66" y1="136.7" x2="600" y2="136.7" stroke="currentColor" stroke-opacity="0.2"/>
<text x="62" y="140.7" font-size="12" text-anchor="end">10ms</text>
<line x1="66" y1="60.0" x2="600" y2="60.0" stroke="currentColor" stroke-opacity="0.2"/>
<text x="62" y="64.0" font-size="12" text-anchor="end">100ms</text>
<text x="158.3" y="308" font-size="12" text-anchor="middle">idle</text>
<rect x="87.7" y="203.7" width="33.3" height="86.3" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<rect x="123.0" y="222.9" width="33.3" height="67.1" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<rect x="158.3" y="192.6" width="33.3" height="97.4" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<rect x="193.7" y="201.7" width="33.3" height="88.3" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<text x="335.0" y="308" font-size="12" text-anchor="middle">stress</text>
<rect x="264.3" y="194.6" width="33.3" height="95.4" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<rect x="299.7" y="169.2" width="33.3" height="120.8" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<rect x="335.0" y="222.1" width="33.3" height="67.9" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<rect x="370.3" y="255.8" width="33.3" height="34.2" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<text x="511.7" y="308" font-size="12" text-anchor="middle">hog</text>
<rect x="441.0" y="166.9" width="33.3" height="123.1" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<rect x="476.3" y="201.9" width="33.3" height="88.1" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<rect x="511.7" y="171.5" width="33.3" height="118.5" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<rect x="547.0" y="242.8" width="33.3" height="47.2" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<rect x="80" y="320" width="12" height="12" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="98" y="331" font-size="12">trace</text>
<rect x="220" y="320" width="12" height="12" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<text x="238" y="331" font-size="12">rt</text>
<rect x="360" y="320" width="12" height="12" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<text x="378" y="331" font-size="12">trace-iso</text>
<rect x="500" y="320" width="12" height="12" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<text x="518" y="331" font-size="12">rt-iso</text>
<text x="20" y="48" font-size="12">p99 (log)</text>
</svg>
```

그림 8 — s5_waiter end-to-end p99를 커널·격리·부하별로(실측, 로그 축, 1차 실행 기준). 파랑 trace, 주황 rt, 초록 trace-iso, 빨강 rt-iso.

### 9.4 정직하게 읽기

- 그림 7만 보면 "격리가 stress 아래 p99를 3148 → 355 µs로 줄였다"고 말하고 싶어진다. 그런데 **격리하지 않은 2차 실행이 340 µs**다. 1차의 나쁜 숫자는 격리 부재가 아니라 그때의 호스트 부하 탓일 가능성이 크다. 이 데이터로 주장할 수 있는 것은 "격리 시 stress 부하에서도 p99가 조용한 호스트의 비격리 수준(약 300 µs대)을 유지했다"까지다.
- **격리는 hog를 못 막는다**: PREEMPT 격리 + hog의 p99는 3510 µs. `s5_hog`는 `kthread_bind()`로 CPU1에 **명시적으로 고정**됐기 때문이다. isolcpus는 스케줄러가 "자동으로" 옮기는 것만 막는다. 실무에서는 per-CPU kworker, 드라이버가 CPU에 고정한 kthread, 그 CPU로 라우팅된 IRQ가 이런 식으로 격리를 뚫는다 — 확인 방법이 §9.2의 `ps -o psr`과 `/proc/interrupts`다.
- 격리의 효과를 TCG에서 분명히 보이려면 호스트 부하를 고정하거나(조용한 시간대), 같은 조건을 여러 번 반복해 분포의 분포를 봐야 한다. 실제 보드에서는 격리 효과가 호스트 소음 없이 바로 보인다(N2 §11.2의 TCG 측정도 같은 한계를 적었다).

---

## 10. Step 9 — 분석 템플릿: 숫자를 어떻게 설명하나

### 10.1 순서

1. **측정기를 먼저 검증한다.** 원인을 아는 지연원(s5_hog 4 ms)을 넣었을 때 측정기가 그 크기(3.9 ms)를 보는가? 처음 버전은 못 봤다(§2.6).
2. **세 측정기로 교차 확인한다.** 자체 측정기, cyclictest, timerlat이 같은 방향인가? (§8.3: 셋 다 PREEMPT + hog p99 ≈ 3.4~4.0 ms.)
3. **트레이스로 원인을 지목한다.** hist `onmax().save(prev_comm)`가 `s5_hog/1`을 가리켰다(§6.4).
4. **실행 간 흔들림을 먼저 잰다.** 같은 조건 두 번(1차/2차)의 차이가 커널 간 차이보다 크면 주장하지 않는다(§8.3의 stress, §9.4의 격리).
5. **설정 차이를 다 적는다.** PREEMPT_RT 외에도 HZ, NO_HZ_FULL, RT_GROUP_SCHED, 디버그 옵션이 다르다(§1.2).
6. **TCG 한계를 적는다.** 절대값은 에뮬레이터 숫자, 상대 모양만.

### 10.2 N1·N3와 비교

| 노트 | 무엇을 쟀나 | 결론 | 이 노트와의 관계 |
|---|---|---|---|
| N1 §6 | cyclictest idle/부하, trace vs rt, 20초 | TCG에서는 섞인 결과, RT 우위 주장 불가 | 같은 결론(stress, idle) |
| N1 §5 | FIFO 1 kthread가 락을 20 ms × 20회 보유, 막힌 횟수 | PREEMPT + `spinlock_t`만 약 20번 막힘, RT + `spinlock_t`은 기준선 | **같은 메커니즘을 분포로 재현**: 이 노트는 4 ms × 40 ms 주기, p99와 hist 섬으로 |
| N3 §10.2 | 60초 × 3회(trace, rt, trace 재측정) | 실행 간 차이 > 커널 간 차이 | 이 노트의 1차/2차 비교가 같은 현상을 다시 보여 줌 (rt stress p99 3761 vs 381) |
| N3 §4.4 | icount 모드 | 호스트 소음 제거, bimodal | 이 노트는 실시간 TCG만 — icount 반복은 확장 과제 |

차이점 하나: N3는 "TCG에서 두 커널 차이를 주장할 수 없다"로 끝났고, 이 노트는 **원인을 아는 지연원(spinlock_t 보유)을 넣었을 때는 TCG에서도 차이가 재현 가능하게 보인다**를 보였다. 일반 부하에서의 차이는 여전히 주장할 수 없다 — 두 결론은 모순이 아니다. "RT는 OS 안의 비선점 구간을 없앤다. 호스트 정지 같은 OS 밖 지연은 그대로다."

### 10.3 보고서 템플릿 (이 실험으로 채운 예)

```text
Latency Report - S5 irq-latency-lab, 2026-10-18
Question    : does PREEMPT_RT remove the wakeup delay caused by a driver holding
              a spinlock_t for 4 ms every 40 ms on the RT CPU?
Platform    : QEMU 10.2.1 'virt' TCG (cortex-a57 model), -smp 2, -m 1G, Lima VM on Apple M2
              host load average 1.8-7.0 (run 1) and 0.7-1.2 (run 2)   <-- dominant noise
Kernels     : 6.18.55-trace (PREEMPT, HZ=250, RT_GROUP_SCHED)
              6.18.55-rt    (PREEMPT_RT, HZ=1000, NO_HZ_FULL)
Boot        : init=PID-1 script (no systemd); isolation runs add isolcpus=1 irqaffinity=0
              (+ nohz_full=1 rcu_nocbs=1 on rt)
Probes      : s5_irqlat (hrtimer ABS_PINNED_HARD, 4 timestamps), cyclictest 2.50
              (-m -a1 -t1 -p95 -i1000 -h20000 -D30), rtla timerlat hist (-c1 -P f:95)
Load        : idle / stress-ng cpu2 vm1 io1 / s5_hog (FIFO1, spinlock_t 4 ms per 40 ms, CPU1)
Results p99 (us), run1/run2      PREEMPT        PREEMPT_RT
  s5_waiter wake, hog            3894 / 3773     388 / 254
  cyclictest, hog                3434 / 3756    1719 / 350
  timerlat Thr, hog              3960 / 3870    1290 / 740
  missed periods, hog            2135 / 2013     116 / 13
Root cause  : hist trigger onmax(): prev_comm=s5_hog/1 at max 3972 us (PREEMPT only)
Not claimed : idle-vs-stress and isolation effects (run-to-run spread > effect)
Conclusion  : PREEMPT_RT removes the spinlock_t-induced 4 ms tail (all 3 probes, 2 runs).
Next        : repeat on target HW with GPIO+scope (M11), 1 h+ runs, cpu_dma_latency=0
```

이 템플릿 구조는 N3 §11과 같다. 면접에서 "어떻게 측정하고 보고했나"에 이 한 장을 그대로 말하면 된다.

---

## 11. 커널 안에서는 무슨 일이 일어나나

hrtimer 경로 한 표본이 지나가는 길(arm64, 6.18 기준, 핵심 함수만):

```
arch timer (CNTV, PPI 27) fires at t_expire
  -> GIC -> el1_interrupt -> gic_handle_irq
  -> handle_percpu_devid_irq -> arch_timer_handler_virt
  -> hrtimer_interrupt -> __hrtimer_run_queues
       -> s5_timer_fn()            t_hardirq = ktime_get_ns()
            -> swake_up_one -> try_to_wake_up   (sched_waking, set NEED_RESCHED)
  <- irq exit: preemptible?  PREEMPT: only if preempt_count == 0
                             (s5_hog holds spin_lock -> count > 0 -> WAIT 4 ms)
                             PREEMPT_RT: spinlock_t = rt_mutex, count == 0 -> switch now
  -> __schedule -> context_switch            (sched_switch ==> s5_waiter)
  -> s5_read resumes after swait_event       t_kwake
  -> copy_to_user -> ret_to_user             t_user (clock_gettime in userspace)
```

드라이버 작성 때 다시 만나는 지점:

| 이 노트에서 | 드라이버 실무에서 | 참고 노트 |
|---|---|---|
| hrtimer `_HARD` 모드 | RT 커널에서 타이머 콜백이 어느 문맥인지 — softirq로 밀리면 지연이 스레드 우선순위를 따른다 | F8, N1 §5 |
| swait / `wake_up_process` | hard IRQ에서 깨울 때 RT에서도 안전한 API | F3, F6 |
| `request_threaded_irq` + `IRQ_WAKE_THREAD` | gpiolib-cdev처럼 hard 핸들러는 타임스탬프만, 무거운 일은 스레드에서. RT에서는 모든 IRQ가 기본으로 스레드화 | F6, F7, H4 |
| `spinlock_t` vs `raw_spinlock_t` | RT에서 잠드는 락과 진짜 스핀락. hard IRQ 경로에는 raw만 | F4, N1 §5 |
| `udelay` 폴링 | 락을 쥔 채 폴링하는 코드가 지연 꼬리를 만든다 — `readx_poll_timeout` + 락 밖 폴링, 또는 IRQ 대기 | H1, N1 |
| misc device + blocking read | 센서 데이터 준비 → 유저 깨우기의 표준 형태 | G4, G6, G7, S2 |

---

## 12. Embedded 실무에서 다시 보기

- **센서 키트의 IMU DRDY 지연**: IMU가 1 kHz로 DRDY GPIO를 올리고, 드라이버는 threaded IRQ에서 SPI로 읽어 IIO 버퍼에 넣는다(S4). "가끔 샘플이 늦다"는 보고가 오면, 이 노트의 순서대로 한다 — gpiolib/IIO 타임스탬프(hard IRQ 시각)와 유저 수신 시각의 차이를 분포로 재고, wakeup_latency hist를 `onmax().save(prev_comm)`과 함께 걸어 범인을 이름으로 찾는다. 범인이 다른 드라이버의 락 보유면 RT 커널 또는 그 드라이버 수정, 범인이 IRQ 스레드 경쟁이면 IRQ affinity와 `chrt`.
- **1 kHz 모터 제어 루프의 CI**: `irq-latency-lab`의 `phase_matrix.sh`를 그대로 CI 잡으로 만들 수 있다. 실제 보드에서 커널 업데이트마다 hog 없는 stress 시나리오를 1시간 돌리고, p99.99와 max가 기준을 넘으면 실패시킨다(N3 §13.2).
- **"벤더 BSP 드라이버가 RT를 망친다"**: 벤더 드라이버가 `spin_lock_irqsave` 안에서 수 ms 폴링하면, PREEMPT_RT에서도 `raw_spinlock_t`나 `local_irq_save`라면 지연이 남는다. 이 노트의 hist 트리거 한 줄이 벤더에게 보낼 증거가 된다.
- **Don의 SSD 경험과의 연결**: SSD에서 "GC 태스크가 FTL 락을 오래 쥐어 호스트 명령 처리 태스크가 늦다"를 QoS 꼬리(p99.99)로 봤던 것과 같은 그림이다. 차이는 Linux가 그 락을 RT 커널에서 선점 가능한 락으로 바꿔 준다는 것, 그리고 ftrace가 로직 분석기 없이 범인 이름을 준다는 것이다.

---

## 13. 흔한 실수와 증상 — 이 노트에서 실제로 재현한 것

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 타임스탬프를 락 밖에서 찍음 | wake max 18446744073709528 µs | 다음 hard IRQ가 사이에 끼어 음수 → 부호 없는 수 | IRQ off + 락 안에서 찍기 (§2.6) |
| 늦은 표본을 최신 값으로 덮어씀 | 4 ms 락을 걸어도 p99 899 µs | 최악을 놓치는 측정기 | 가장 오래된 미처리 만료 보존 + missed 카운트 |
| synthetic 시작 변수를 매 이벤트가 덮어씀 | irq_to_run이 4 ms 대신 1~2 ms | 같은 키의 다음 이벤트가 변수 갱신 | 구간당 한 번 일어나는 시작 이벤트 사용 (§6.5) |
| 측정 바이너리·로그를 9p/NFS에 둠 | 대기자가 D 상태, kworker 깨움 | page-in, write가 원격 FS I/O | tmpfs로 복사, 로그는 끝나고 저장 (§4.6) |
| systemd 세션에서 chrt (Image-trace) | `Operation not permitted` | RT_GROUP_SCHED, 하위 cgroup RT 예산 0 | `echo $$ > /sys/fs/cgroup/cgroup.procs` (§4.4) |
| 한 번 재고 결론 | rt stress p99 3761 vs 381 | 호스트 부하에 따른 실행 간 흔들림 | 같은 조건 반복, 흔들림 > 차이면 주장 안 함 (§8.3) |
| 격리만 믿음 | 격리 CPU에서도 p99 3.5 ms | CPU에 bind된 kthread는 isolcpus를 무시 | `ps -o psr`, `/proc/interrupts`로 격리 CPU 손님 확인 (§9.4) |
| `:clear`로 onmax 트리거 초기화 | `echo: write error: Invalid argument` | 같은 스펙 매칭 실패 | `!`로 지우고 다시 걸기 |
| RT 커널에서 hrtimer 기본 모드 | "hard IRQ" 타임스탬프가 사실은 ktimers 스레드 시각 | RT는 비-HARD hrtimer를 softirq로 | `HRTIMER_MODE_*_HARD` |

---

## 14. 확장 과제 — 포트폴리오를 한 단계 더

1. **icount 모드로 반복**: `-icount shift=3,sleep=off`로 같은 매트릭스를 돌려 호스트 소음 없이 stress·격리 효과가 보이는지(N3 §4.4).
2. **s5_waiter에 `/dev/cpu_dma_latency` 0 쓰기 옵션** 추가 후 idle 결과 비교.
3. **자체 tracepoint**: `TRACE_EVENT(s5_sample, ...)`를 모듈에 넣어(M5 §5.7) kprobe 대신 쓰고, synthetic event를 tracepoint 필드로 만들기.
4. **IRQ 스레드 우선순위 실험**: gpio 경로에서 `chrt -f -p 95 <irq/29 pid>`와 IRQ affinity를 CPU1로 바꿔 hardirq→user가 어떻게 바뀌는지.
5. **hog 변형**: `raw_spinlock_t`, `local_irq_save`, `preempt_disable` 버전을 만들어 "PREEMPT_RT가 못 고치는 것"을 같은 그림으로(N1 §10, N3 `n3_spike`).
6. **실제 보드**: Raspberry Pi 5 / BeaglePlay(S3)에서 GPIO 루프백(출력 핀 → 입력 핀)으로 진짜 엣지를 만들고, scope로 원점을 재서(M11) 소프트웨어 타임스탬프와 비교.
7. **rtla timerlat `-a`**(auto-analysis)로 hog 첫 스파이크에서 멈추고 범인 분석(N3 §7.4).

---

## 15. GitHub와 면접에서 보여 주기

### 15.1 README 구성 (저장소에 이미 있음)

1. 한 줄 요약 + TCG 경고(맨 위)
2. 측정 구간 그림(ASCII 또는 `docs/s5_timeline.svg`)
3. 설계 결정: hrtimer 모듈 vs gpio-sim, HARD 모드, swait, 가장 오래된 표본 보존
4. 실행법 4줄(`prepare.sh`, `run_guest.sh trace smoke smoke`, `run_matrix.sh`, `plot.py`)
5. 결과 표(§10.3) + 그림 5, 6
6. "측정기 버그 두 개와 교훈"(§2.6) — 면접관이 가장 좋아하는 부분
7. 한계와 다음 단계(실제 HW, scope)

### 15.2 3분 데모 스크립트

1. (20초) 문제 정의: "IRQ에서 내 RT 스레드까지 얼마나 걸리고, 어디서 새나?"
2. (40초) 구간 그림(그림 1)과 모듈의 세 결정(HARD, swait, oldest sample).
3. (40초) `run_guest.sh trace matrix base S5SCEN=hog S5D=10` 라이브 실행 → wake p99가 수 ms로 뜨는 것.
4. (40초) ftrace hist 출력: `2^12` 칸, `prev_comm: s5_hog/1`.
5. (40초) 같은 명령을 `rt`로 → 섬이 사라짐. TCG 한계와 "실행 간 흔들림을 먼저 쟀다"는 문장으로 마무리.

### 15.3 보여 줄 숫자

- PREEMPT + hog wake p99 **3.9 ms** vs PREEMPT_RT **0.25~0.39 ms** (두 번 측정)
- missed periods **2135 vs 116** (30000주기 중)
- hist 최악 **3972 µs, prev_comm=s5_hog/1**
- 격리 확인: CPU1 arch_timer IRQ **205 vs 6285** (nohz_full), stress-ng 작업자 전부 CPU0
- 그리고 "이 숫자는 TCG 숫자다, 실제 보드에서는 µs 단위"라는 한 문장

---

## 16. 면접에서 이렇게 말한다

**Q.** "How did you measure interrupt-to-userspace latency, and how do you know where the time went?"

**A.** 알려진 시각에 만료하는 hrtimer를 HARD 모드로 걸어 hard IRQ에서 타임스탬프를 찍고, 유저 RT 스레드가 깨어난 시각까지 네 지점으로 쪼갰다. 그리고 sched_waking/sched_switch에 hist 트리거와 synthetic event를 걸어 커널 안에서 분포를 만들고, 최댓값일 때 직전 태스크 이름을 저장해 범인을 지목했다.

> I built a small kernel module whose hrtimer expires at a known absolute time in hard-IRQ context, so I get four timestamps: expiry, handler, back-in-kernel, and back-in-userspace. That splits latency into IRQ, wakeup, and return segments. To prove the path I used trace-cmd with irq, timer and sched events, function_graph on the callback, and an in-kernel histogram: a synthetic wakeup_latency event from sched_waking to sched_switch with an onmax action that saves prev_comm. Under an injected 4 ms spinlock holder, the max entry named the offending kthread directly.

**꼬리 질문**: "Why not just use cyclictest?" → 쓴다. 교차 확인용으로 같은 CPU·주기에서 cyclictest와 timerlat을 같이 돌렸고 같은 결론이었다. 자체 측정기는 "어느 구간"인지를 주고, timerlat도 IRQ/thread 단계로 같은 답을 줬다.

**Q.** "What exactly does PREEMPT_RT fix, and what doesn't it fix?"

**A.** 비선점 구간을 없앤다 — `spinlock_t`가 잠들 수 있는 rt_mutex가 되고, IRQ 핸들러와 softirq가 스레드로 간다. 그래서 락을 오래 쥔 드라이버 때문에 생기는 4 ms 꼬리가 사라졌다. 하지만 `raw_spinlock_t`, `local_irq_save`, 펌웨어·하이퍼바이저 정지는 그대로다. 측정에서도 20 ms 이상의 꼬리는 두 커널에 다 있었고, 그건 호스트 정지였다.

> PREEMPT_RT shrinks non-preemptible regions: spinlock_t becomes an rt_mutex-based sleeping lock, and interrupt handlers and softirqs run in threads. In my lab, a FIFO-1 kthread holding a spinlock_t for 4 ms every 40 ms pushed the p99 wakeup latency to about 3.9 ms on PREEMPT, and on PREEMPT_RT the island disappeared. What RT does not fix is anything outside its model: raw spinlocks, explicit irq-off sections, firmware or hypervisor stalls. Both kernels still showed tens-of-ms tails from the emulator host.

**Q.** "Your numbers are from QEMU. Are they meaningful?"

**A.** 절대값은 의미 없다고 먼저 말한다. 의미 있는 것은 같은 파라미터에서의 상대 비교와 모양이다. 그리고 같은 매트릭스를 두 번 돌려 실행 간 흔들림을 쟀다 — stress에서는 그 흔들림이 열 배라 커널 차이를 주장하지 않았고, hog 효과는 두 번 다 10% 안에서 재현돼서 주장했다.

> The absolute values are emulator numbers, so I only claim relative shape under identical parameters. I ran the whole matrix twice to measure run-to-run variance. For generic stress load the variance was about 10x, larger than the kernel difference, so I don't claim anything there. The spinlock effect reproduced within about 10 percent in both runs across three different probes, so that one I do claim. On real hardware I'd confirm with a GPIO loopback and a scope.

**Q.** "Why did you use a simple wait queue instead of a normal wait queue in the IRQ handler?"

**A.** PREEMPT_RT에서 일반 wait queue의 락은 `spinlock_t`라 잠들 수 있는 락이고, hard IRQ에서 잡으면 안 된다. swait는 raw spinlock이라 hard IRQ에서 안전하다. completion도 내부에 swait를 쓴다. 타이머도 `_HARD` 모드여야 RT에서 콜백이 hard IRQ에 남는다.

> On PREEMPT_RT a regular wait_queue_head is protected by a spinlock_t, which is a sleeping lock there, so you must not take it from hard-IRQ context. A swait queue uses a raw spinlock and is safe to wake from hard IRQ; completions use it internally. For the same reason the hrtimer is started in HRTIMER_MODE_ABS_PINNED_HARD, otherwise RT would defer the callback to the softirq thread and my "hard-IRQ timestamp" would be wrong.

**Q.** "You isolated a CPU but still saw 3.5 ms latency on it. Why?"

**A.** isolcpus는 스케줄러가 자동으로 옮기는 태스크만 막는다. 그 CPU에 명시적으로 bind된 kthread, per-CPU 작업, 그 CPU로 라우팅된 IRQ는 그대로 온다. `ps -o psr`와 `/proc/interrupts`로 격리 CPU에 누가 남아 있는지 확인했고, 범인은 그 CPU에 bind된 hog 스레드였다.

> isolcpus only removes the CPU from load balancing and default affinity. Anything explicitly bound to that CPU — a driver's kthread, per-CPU workers, IRQs routed there — still runs on it. I verified isolation with ps psr, the init task's Cpus_allowed_list and per-CPU interrupt counts, and the remaining offender was a kthread bound with kthread_bind.

**Q.** "Your gpio path shows a threaded IRQ. Where does that latency come from and how would you tune it?"

**A.** gpiolib-cdev는 hard 핸들러에서 타임스탬프만 찍고 IRQ_WAKE_THREAD를 반환한다. 그다음 `irq/29-s5_gpio`(FIFO 50) 스레드가 이벤트를 kfifo에 넣고 대기자를 깨운다. 그 스레드가 CPU0에서 stress-ng와 경쟁하고, 다른 CPU의 대기자를 IPI로 깨우니 두 단계가 생긴다. 튜닝은 IRQ affinity, IRQ 스레드 우선순위, 대기자와의 CPU 배치다.

> gpiolib-cdev timestamps the edge in the hard handler and returns IRQ_WAKE_THREAD; the irq thread, SCHED_FIFO 50 by default, then pushes the event into a kfifo and wakes the reader. In my setup that thread ran on CPU0 next to the stress load and had to wake the reader on CPU1 with an IPI, which doubled the hops. I'd tune it with IRQ affinity, the irq thread's priority via chrt, and by placing the consumer thread deliberately relative to the irq thread.

---

## 17. 직접 해보기

1. (개념) `s5_timer_fn`이 `s5_last`를 무조건 덮어쓰면 어떤 지연이 숨겨지나? 정답/힌트: 대기자가 N주기 늦을 때 wake가 최신 hard IRQ 기준이 되어 최대 1주기로 잘린다 — §2.6 버그 2.
2. (코드) `s5_hog`에 `mode=raw` 파라미터를 추가해 `raw_spinlock_t`로 같은 실험을 하라. PREEMPT_RT에서 섬이 다시 생겨야 한다. 정답/힌트: `DEFINE_RAW_SPINLOCK` + `raw_spin_lock()` — RT도 선점하지 못한다.
3. (관찰) PREEMPT_RT 게스트에서 hrtimer 모드를 `HRTIMER_MODE_ABS_PINNED`(HARD 없음)로 바꾸고 trace-cmd로 `s5_timer_fn`이 어느 태스크 문맥에서 도는지 보라. 정답/힌트: `<idle>-0`(hard IRQ)이 아니라 softirq 처리 스레드(6.18 RT에서는 `ktimers/1`) 문맥이 보이고, irq 구간에 스레드 스케줄링 지연이 더해진다.
4. (ftrace) `irq_to_run`을 §6.5 함정 없이 고쳐라. 정답/힌트: 시작 이벤트를 `sched_waking`으로 바꾸거나, 모듈 tracepoint에 seq를 넣어 키로 쓴다.
5. (분석) 1차/2차 실행 표(§8.2)에서 "주장할 수 있는 것"과 "없는 것"을 각각 두 줄로 써라. 정답/힌트: hog 효과는 주장 가능(재현 10% 안, 세 측정기 일치), stress·격리 효과는 불가(흔들림 열 배).
6. (확장) `irqaffinity=0` 없이 격리만 했을 때 `/proc/interrupts`에서 CPU1에 남는 IRQ를 찾아라. 정답/힌트: virtio 큐 IRQ 하나가 CPU1에 남는다(§9.2의 IRQ 20).

---

## 18. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| IRQ latency | 하드웨어 사건 → 핸들러 첫 줄 | 이 노트의 irq 구간. IRQ off 구간이 늘린다 |
| wakeup(scheduling) latency | 깨우기 → 그 태스크가 CPU에서 실행 | wake 구간. preempt off·비선점 락이 늘린다 |
| hrtimer HARD 모드 | `HRTIMER_MODE_*_HARD` | PREEMPT_RT에서도 콜백을 hard IRQ 문맥에 유지 |
| swait | simple wait queue | raw spinlock 기반, hard IRQ에서 깨워도 RT 안전 |
| threaded IRQ | `request_threaded_irq` | hard 핸들러 + `irq/N-name` 커널 스레드(기본 FIFO 50) |
| irq_sim | IRQ 시뮬레이터 | gpio-sim이 쓰는 소프트웨어 IRQ 도메인, irq_work로 주입 |
| hist trigger | `hist:keys=...` | 이벤트를 커널 안에서 키별로 집계하는 ftrace 트리거 |
| synthetic event | `synthetic_events` | 두 이벤트 사이 값(지연)으로 만든 새 이벤트 |
| onmatch / onmax | hist 액션 | 짝이 맞으면 이벤트 발생 / 최댓값 갱신 때 필드 저장 |
| kprobe event | `kprobe_events` | 임의 함수 입구에 만든 동적 이벤트 |
| isolcpus | 부트 인자 | 스케줄러 자동 배치에서 CPU 제외 |
| nohz_full | 부트 인자 | 태스크 하나만 있을 때 그 CPU의 주기 tick 정지 |
| RT_GROUP_SCHED | 커널 옵션 | cgroup별 RT 시간 예산. 하위 cgroup 기본 0 → FIFO 실패 |
| missed periods | 이 노트의 지표 | 대기자가 통째로 놓친 타이머 주기 수 |
| TCG | QEMU Tiny Code Generator | 소프트웨어 CPU 에뮬레이션. 타이밍이 실제 HW가 아님 |

---

## 19. 요약 & 체크리스트

알려진 시각에 만료하는 HARD hrtimer와 swait로 만든 측정 모듈, SCHED_FIFO 대기 스레드로 IRQ→유저 지연을 irq / wake / ret 구간으로 쪼개 쟀다. trace-cmd 이벤트와 function_graph로 경로를 확인하고, kprobe + hist 트리거 + synthetic event로 wakeup latency 분포를 커널 안에서 만들었으며, `onmax().save(prev_comm)`이 4 ms `spinlock_t` 보유자를 이름으로 지목했다. cyclictest·rtla timerlat이 같은 결론을 냈다: PREEMPT에서 그 락이 wake 구간에 3.4~4.0 ms 섬을 만들고, PREEMPT_RT에서는 사라진다(두 번 측정, 재현). 반면 일반 stress 부하와 CPU 격리의 효과는 TCG의 실행 간 흔들림(열 배)보다 작아서 주장하지 않았다. 측정기 버그 두 개(음수 wake, 최악 숨김)와 같은 모양의 synthetic event 함정을 실제로 밟고 고쳤다.

- [ ] hrtimer `_HARD` 모드와 swait를 고른 이유를 PREEMPT_RT 기준으로 설명할 수 있다
- [ ] irq / wake / ret 구간을 정의하고 각 구간을 늘리는 원인(IRQ off, preempt off, 비선점 락)을 짝지을 수 있다
- [ ] 늦은 표본을 덮어쓰는 측정기가 왜 최악값을 숨기는지 설명할 수 있다
- [ ] initramfs → switch_root → PID 1 + 9p로 측정 게스트를 8초 만에 돌릴 수 있다
- [ ] RT_GROUP_SCHED 때문에 `chrt`가 EPERM인 상황을 재현하고 고칠 수 있다
- [ ] trace-cmd 이벤트 한 주기를 읽고 `[9]`, `<idle>-0`, `ksoftirqd`의 의미를 말할 수 있다
- [ ] kprobe event, hist 트리거, synthetic event, `onmax().save()`를 직접 걸고 지울 수 있다
- [ ] synthetic 시작 변수 덮어쓰기 함정을 설명할 수 있다
- [ ] gpio-sim + GPIO v2 uAPI로 threaded IRQ 경로를 재고 irq 스레드를 찾을 수 있다
- [ ] 격리가 먹혔는지 `Cpus_allowed_list`, `ps -o psr`, `/proc/interrupts`로 증명할 수 있다
- [ ] 실행 간 흔들림을 재고, 흔들림보다 작은 차이는 주장하지 않는 보고서를 쓸 수 있다
- [ ] 이 프로젝트를 3분 데모와 README로 보여 줄 수 있다

### 자체 점검 (이 노트를 쓰며 확인한 것)

- [ ] 모든 출력 블록은 VM/QEMU에서 실제로 실행한 결과다(생략은 `...`로 표시)
- [ ] 코드 블록은 60줄 이내, 경고 0개로 빌드됨(모듈 2개 × 커널 2개, 유저 2개)
- [ ] SVG 8개 모두 XML 파싱 통과, 텍스트 fill 지정 없음
- [ ] 표 셀에 파이프 문자 없음

---

## 참고 자료

- Linux kernel documentation, "Event Histograms" — `hist` 트리거, synthetic event, `onmatch`/`onmax`: https://docs.kernel.org/trace/histogram.html
- Linux kernel documentation, "Kprobe-based Event Tracing": https://docs.kernel.org/trace/kprobetrace.html
- Linux kernel documentation, "ftrace - Function Tracer": https://docs.kernel.org/trace/ftrace.html
- Linux kernel documentation, "Timerlat tracer": https://docs.kernel.org/trace/timerlat-tracer.html
- Linux kernel documentation, "Real-Time preemption" (PREEMPT_RT): https://docs.kernel.org/core-api/real-time/index.html
- Linux kernel documentation, "GPIO Character Device Userspace API (v2)": https://docs.kernel.org/userspace-api/gpio/chardev.html
- Linux kernel documentation, "Configfs GPIO Simulator": https://docs.kernel.org/admin-guide/gpio/gpio-sim.html
- Linux kernel documentation, "The kernel's command-line parameters" (isolcpus, nohz_full, irqaffinity): https://docs.kernel.org/admin-guide/kernel-parameters.html
- rt-tests (cyclictest) 소스: https://git.kernel.org/pub/scm/utils/rt-tests/rt-tests.git
- 커널 소스 (elixir.bootlin.com, v6.18): `kernel/time/hrtimer.c`, `kernel/sched/swait.c`, `kernel/irq/irq_sim.c`, `drivers/gpio/gpio-sim.c`, `drivers/gpio/gpiolib-cdev.c`, `kernel/trace/trace_events_hist.c`
- 관련 노트: M5, N1, N2, N3, F3, F4, F6, F8, G7, H4, S1, S2, S4, M11
