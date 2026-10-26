# C11. 다른 스케줄러 구현 — cyclic executive · Pont TTC · EDF · ARINC 653 파티션을 직접 만들어 돌리고, 같은 task set에서 무엇이 달라지는지 잰다

> **이 노트를 다 읽으면**: task set에서 minor frame 크기를 Baker & Shaw 세 조건으로 고르고, Python 도구로 시간표(job slicing 포함)를 만들어 QEMU bare-metal에서 TIMER IRQ로 돌리며 오버런을 잡을 수 있다 · Pont식 TTC 스케줄러를 offset · 시간 예산 · tick 오버런 감지까지 넣어 짤 수 있다 · RM으로는 안 되고 EDF로는 되는 task set을 RTA로 증명하고 Zephyr `CONFIG_SCHED_DEADLINE`으로 실제로 돌려 miss 0을 보이며, 과부하에서 EDF domino 효과를 측정 차트로 보여 줄 수 있다 · FreeRTOS 위에 ARINC 653식 2단계(시간 창 + 파티션 안 FP) 스케줄러를 만들어 한 파티션의 폭주가 다른 파티션에 번지지 않음을 간트로 증명할 수 있다
> **왜 필요한가**: 드론·국방·항공·차량 면접에서 "FreeRTOS 말고 다른 스케줄링을 아나? EDF는 왜 잘 안 쓰나? 항공 소프트웨어는 왜 시간표로 도나?"가 나온다. "이론상 EDF가 최적"에서 멈추지 않고 **"직접 돌려 보니 U = 0.92에서 RM은 nav가 1초에 42번 놓쳤고 EDF는 0번, 그런데 U가 1을 살짝 넘자 EDF는 세 task가 전부 90% 이상 놓쳤다"**처럼 숫자로 말해야 시니어 대화가 된다
> **Don 기준 난이도**: 표 기반 루프, 타이머 인터럽트, SSD executive의 고정 슬롯은 이미 안다 / 새로 배울 부분은 frame 크기 조건의 근거(2f − gcd ≤ D), job slicing, Zephyr deadline의 정확한 의미(같은 정적 우선순위 안에서만), EDF 과부하의 domino, 파티션 창이 만드는 지연과 격리
> **선행 노트**: A1(release · deadline · jitter 정의, RM 배치 실험), A2(협력형 table-driven 스케줄러 · time slicing — 여기서는 그 다음 단계), C2(문맥 전환), C4(FP · round-robin 정책). 이론 증명은 N2(RM · Liu-Layland), N3(RTA), N5(EDF)가 주인이고 이 노트는 **구현과 측정**만 한다. 이어지는 노트: C12(커널 소스 투어), N9(혼합 중요도), O6(시간 · 공간 분할), F4 · F5(MPU · 유저 모드 격리)
> 🔁 **Bare-Metal 겹침**: BM K5(협력형 · time-triggered 스케줄러) — 이 노트가 주인(태스크 표, Pont TTC, 시간 예산 · 오버런 감지, cyclic executive 전부). Bare-Metal 노트는 이 노트를 링크하고 "실칩에서 SysTick + WFI 저전력 sleep + 하드웨어 watchdog을 엮은 최소 TTC 템플릿(레지스터 수준), M0급 작은 MCU에서의 코드 크기"만 더한다

---

## 0. 큰 그림 — "다음에 누구를 돌릴지"를 언제, 무엇으로 정하나

지금까지 본 FreeRTOS · Zephyr의 기본 스케줄러는 **고정 우선순위 선점(FP, fixed-priority preemptive)** 하나였다. 설계자가 task마다 숫자(우선순위)를 하나 주면, 커널은 실행 중 매 순간 "ready인 것 중 숫자가 가장 높은 것"을 돌린다. 그런데 세상의 실시간 시스템이 전부 이렇게 도는 것은 아니다.

- **항공 레거시 · FlexRay · 많은 안전 펌웨어**는 "누가 언제 돌지"를 **설계 때 시간표 한 장**으로 다 정해 두고, 실행 중에는 표를 읽기만 한다 → **cyclic executive / time-triggered**.
- **Zephyr · RTEMS · Linux `SCHED_DEADLINE`** 은 우선순위를 job마다 바꾼다. "deadline이 가장 가까운 job"이 가장 급하다 → **EDF(Earliest Deadline First)**.
- **IMA(통합 모듈형 항공전자) 컴퓨터**는 한 CPU에 중요도가 다른 소프트웨어 여러 개를 올리고, 각자에게 **고정된 시간 창**을 준다. 창 안에서는 각자 FP로 돈다 → **ARINC 653 파티션 스케줄링**(2단계).

```svg
<svg viewBox="0 0 680 262" xmlns="http://www.w3.org/2000/svg">
  <text x="10" y="18" font-size="13">스케줄러 네 가족 — "다음에 누구를 돌릴지"를 언제, 무엇으로 정하나</text>
  <rect x="10" y="34" width="150" height="160" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="85" y="56" font-size="13" text-anchor="middle">시간표 (cyclic)</text>
  <text x="20" y="82" font-size="12">결정: 설계 때</text>
  <text x="20" y="102" font-size="12">근거: 표 한 장</text>
  <text x="20" y="122" font-size="12">실행: 표 순서대로</text>
  <text x="20" y="142" font-size="12">끝까지 (비선점)</text>
  <text x="20" y="166" font-size="12">예: Pont TTC,</text>
  <text x="20" y="184" font-size="12">FlexRay 정적 구간</text>
  <rect x="180" y="34" width="150" height="160" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="255" y="56" font-size="13" text-anchor="middle">고정 우선순위 (FP)</text>
  <text x="190" y="82" font-size="12">결정: 실행 중 매번</text>
  <text x="190" y="102" font-size="12">근거: task 우선순위</text>
  <text x="190" y="122" font-size="12">(설계 때 고정)</text>
  <text x="190" y="142" font-size="12">선점형</text>
  <text x="190" y="166" font-size="12">예: FreeRTOS,</text>
  <text x="190" y="184" font-size="12">Zephyr 기본</text>
  <rect x="350" y="34" width="150" height="160" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="425" y="56" font-size="13" text-anchor="middle">동적 우선순위 (EDF)</text>
  <text x="360" y="82" font-size="12">결정: 실행 중 매번</text>
  <text x="360" y="102" font-size="12">근거: job의 deadline</text>
  <text x="360" y="122" font-size="12">(job마다 바뀜)</text>
  <text x="360" y="142" font-size="12">선점형</text>
  <text x="360" y="166" font-size="12">예: Zephyr</text>
  <text x="360" y="184" font-size="12">SCHED_DEADLINE</text>
  <rect x="520" y="34" width="150" height="160" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="595" y="56" font-size="13" text-anchor="middle">ARINC 653</text>
  <text x="530" y="82" font-size="12">1단계: 시간 창 표</text>
  <text x="530" y="102" font-size="12">(설계 때 고정)</text>
  <text x="530" y="122" font-size="12">2단계: 창 안에서</text>
  <text x="530" y="142" font-size="12">파티션별 FP</text>
  <text x="530" y="166" font-size="12">예: VxWorks 653,</text>
  <text x="530" y="184" font-size="12">PikeOS</text>
  <text x="10" y="222" font-size="12">왼쪽일수록 결정이 설계 때로 옮겨 간다 → 검증·인증이 쉽고, 바뀌는 부하에는 약하다.</text>
  <text x="10" y="242" font-size="12">EDF는 이용률 100%까지 쓰지만 과부하에 약하고, ARINC 653은 FP 위에 "시간 울타리"를 친다.</text>
</svg>
```

그림 1 — 이 노트에서 직접 구현하는 네 가족. 결정이 "설계 때"로 갈수록 검증은 쉬워지고 유연성은 줄어든다.

Don의 SSD 펌웨어에 비유하면 이렇다. 엔터프라이즈 SSD의 executive가 "매 주기 host 명령 처리 → FTL 백그라운드 → GC 조금 → 다시 처음"처럼 **고정된 순서와 예산**으로 돌았다면 그것이 cyclic executive에 가깝다. 반대로 "가장 오래 기다린 host 명령부터(타임아웃이 가장 가까운 것부터)" 처리하는 큐 정책은 EDF의 사촌이다. 컨트롤러에서 펌웨어 코어마다 일을 고정 배정한 것은 파티션(공간)에 가깝고, 한 코어 안에서 시간을 잘라 나눈 것은 시간 파티션이다.

### 0.1 이 노트의 지도

| 절 | 무엇을 만드나 | 무엇을 재나 |
|---|---|---|
| §1 | frame 크기 고르는 도구, 시간표 생성기(Python), TIMER IRQ로 도는 bare-metal cyclic executive | frame 경계 지터, 오버런 감지, catch-up vs skip 정책 |
| §2 | Pont식 TTC 스케줄러 (`SCH_Update` / `SCH_Dispatch_Tasks`) | offset 유무에 따른 tick 부하 · 오버런 · ctrl 지연, 시간 예산 위반 |
| §3 | (조사) 분산 시스템의 정적 시간표: TTA · TTEthernet · FlexRay · TSN | — |
| §4 | RTA로 "RM 실패 · EDF 성공" task set 증명, Zephyr EDF vs FP, 과부하 스윕, FreeRTOS EDF 흉내 | deadline miss 수, 응답 시간, 과부하 실패 비율, 우선순위 재배치 비용 |
| §5 | FreeRTOS 위 ARINC 653식 2단계 스케줄러 | 폭주 파티션의 영향 범위(간트), 창 전환 비용 |
| §6 | 네 방식 비교표 | 실측 숫자 모음 |
| §7~ | 커널 안, 실무, 실수, 면접, 연습 | — |

### 실습 환경 한 줄

MCU 쪽은 Lima VM `elinux` 안의 **QEMU `mps2-an385`(Cortex-M3 @ 25 MHz)** 에서 랩 bare-metal 템플릿, **FreeRTOS Kernel V11.3.2**(ARM_CM3 포트, 1 kHz tick), **Zephyr v4.4.2**(`mps2/an385`, SysTick 25 MHz cycle, 10 kHz tick)를 쓰고 전부 **`ICOUNT=1`**(`-icount shift=5`: 명령 1개 = 32 ns 가상 시간, 매번 같은 숫자)로 돌렸다. 시간표 생성 · RTA · 그림은 VM 호스트의 Python 3.14. **숫자는 QEMU icount 기준이라 실제 칩과 절대값은 다르다**(캐시 · 플래시 wait state · 버스 · 예외 스태킹 사이클을 QEMU가 모델링하지 않는다). 비교와 경향을 보는 용도다.

---

## 1. cyclic executive — 시간표를 설계 때 다 만들어 두기

> 🔁 이 절은 Bare-Metal BM K5와 겹친다 (주인: 이 노트). §1 전체(시간표 · frame · 오버런 감지)가 해당한다.

### 1.1 직관 — 기차 시간표

기차역은 "지금 어느 기차가 가장 급한가"를 실시간으로 판단하지 않는다. **시간표**가 있고, 역무원은 시계를 보고 시간표대로 출발시킨다. 시간표를 짤 때 이미 "이 노선은 이 시간에 이 선로를 쓴다"를 모두 검사했으니, 운행 중에는 충돌이 날 수가 없다.

cyclic executive(순환 실행부)가 정확히 이것이다.

- 설계 때: task set(주기 T, 실행 시간 C, deadline D)을 보고 **한 주기 분량의 시간표**를 만든다. 표의 한 칸 = "이 frame에서 이 순서로 이 함수들을 부른다".
- 실행 때: 주기 타이머 인터럽트가 frame 시작을 알리면 표의 그 칸을 위에서부터 **끝까지(run-to-completion)** 실행한다. 다 끝나면 다음 인터럽트까지 잔다.
- 커널도, task별 스택도, 우선순위도, 락도 없다. 실행 순서가 표로 고정되어 있으니 **공유 데이터 레이스가 원천적으로 없다**(ISR 제외).

A2 §4의 협력형 스케줄러와 다른 점: A2는 "지금 release된 것 중 표 위쪽에 있는 것"을 **실행 중에 고른다**. cyclic executive는 고를 일이 없다. **무엇을 언제 돌릴지가 이미 표에 다 적혀 있다**.

### 1.2 정의 — major cycle, minor frame, slice

| 용어 | 뜻 | 이 노트의 값 |
|---|---|---|
| hyperperiod H | 모든 주기의 최소공배수. 이 시간이 지나면 release 패턴이 똑같이 반복된다 | 20 ms |
| major cycle | 시간표 한 장의 길이 = H (보통) | 20 ms |
| minor frame f | 시간표의 한 칸. frame 경계마다 타이머 인터럽트 하나 | 2 ms |
| frame 수 | H / f | 10 |
| slice | 한 job을 여러 frame에 나눠 실행할 때의 조각 | telem 3000 µs → 4조각 |
| frame overrun | frame 안의 일이 다음 frame 경계까지 안 끝남 | §1.8에서 일부러 일으킨다 |

드론 센서 허브 예를 계속 쓴다. task 4개(단위 µs):

| task | 주기 T | 실행 C | deadline D | 하는 일 |
|---|---|---|---|---|
| ctrl | 2000 | 500 | 2000 | IMU 읽기 + rate 제어 |
| att | 4000 | 500 | 4000 | 자세 추정 |
| nav | 10000 | 1200 | 10000 | 위치 · 속도 필터 |
| telem | 20000 | 3000 | 20000 | 텔레메트리 패킷 포맷 · 전송 |

U = 0.25 + 0.125 + 0.12 + 0.15 = **0.645**.

### 1.3 frame 크기 f를 고르는 세 조건 (Baker & Shaw)

frame 크기는 아무렇게나 정하면 안 된다. T. P. Baker와 A. Shaw가 1989년 논문 『The Cyclic Executive Model and Ada』(Real-Time Systems 저널)에서 정리하고, J. W. S. Liu의 교재 『Real-Time Systems』(2000) 5장(clock-driven scheduling)이 표준 형태로 쓴 조건이 세 개 있다. task i의 주기를 Tᵢ, 실행 시간을 Cᵢ, 상대 deadline을 Dᵢ라 하자.

1. **f ≥ max Cᵢ** — 어떤 job도 frame 하나 안에서 끝나야 한다. frame 중간에 선점이 없으니(비선점), job이 frame 경계를 넘으면 다음 frame의 일을 밀어낸다.
2. **f가 적어도 한 Tᵢ를 나눈다** (⌊Tᵢ / f⌋ − Tᵢ / f = 0인 i가 있다) — 그래야 f가 H도 나누고, major cycle 안에 frame이 정수 개 들어가 표가 깔끔하게 반복된다.
3. **2f − gcd(Tᵢ, f) ≤ Dᵢ (모든 i)** — job의 release와 deadline 사이에 **frame 하나가 통째로** 들어가야 한다. 스케줄러는 frame 경계에서만 일을 시작하므로, release 직후에 시작한 frame에는 그 job을 넣을 수 없다(release 전 시각에 이미 그 frame이 시작됐으니까).

조건 (3)이 가장 헷갈린다. 그림으로 보자.

```svg
<svg viewBox="0 0 680 214" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" font-size="13">조건 (3) 2f − gcd(T, f) ≤ D 는 어디서 나오나 — 가장 나쁜 release 위치</text>
  <rect x="60" y="70" width="180" height="40" fill="#888" fill-opacity="0.12" stroke="currentColor"/>
  <rect x="240" y="70" width="180" height="40" fill="#3f9a6b" fill-opacity="0.12" stroke="currentColor"/>
  <rect x="420" y="70" width="180" height="40" fill="#888" fill-opacity="0.12" stroke="currentColor"/>
  <text x="150" y="95" font-size="12" text-anchor="middle">frame k</text>
  <text x="330" y="95" font-size="12" text-anchor="middle">frame k+1 (통째로 씀)</text>
  <text x="510" y="95" font-size="12" text-anchor="middle">frame k+2</text>
  <text x="60" y="128" font-size="12" text-anchor="middle">kf</text>
  <text x="240" y="128" font-size="12" text-anchor="middle">(k+1)f</text>
  <text x="420" y="128" font-size="12" text-anchor="middle">(k+2)f</text>
  <line x1="80" y1="40" x2="80" y2="68" stroke="#d0564a" stroke-width="2" marker-end="url(#ah3)"/>
  <text x="86" y="50" font-size="12">release (frame 시작 직후: kf + gcd(T, f))</text>
  <line x1="420" y1="40" x2="420" y2="68" stroke="#d0564a" stroke-width="2" marker-end="url(#ah3)"/>
  <text x="426" y="62" font-size="12">늦어도 여기까지 deadline</text>
  <line x1="80" y1="146" x2="240" y2="146" stroke="currentColor" marker-start="url(#ah3)" marker-end="url(#ah3)"/>
  <text x="160" y="164" font-size="12" text-anchor="middle">f − gcd(T, f) 기다림</text>
  <line x1="240" y1="146" x2="420" y2="146" stroke="currentColor" marker-start="url(#ah3)" marker-end="url(#ah3)"/>
  <text x="330" y="164" font-size="12" text-anchor="middle">f (frame 하나)</text>
  <text x="10" y="190" font-size="12">release가 frame 경계와 가장 가깝게 어긋나도 gcd(T, f)만큼은 떨어져 있다(정수 격자). 그러므로</text>
  <text x="10" y="208" font-size="12">release ~ deadline 사이에 frame 하나가 통째로 들어가려면 (f − gcd) + f ≤ D 이어야 한다.</text>
</svg>
```

그림 2 — 조건 (3)의 근거. release가 frame k 시작 직후에 오면, 스케줄러는 다음 경계까지 기다렸다가 frame k+1 하나를 통째로 써야 한다. release와 frame 경계 사이의 가장 작은 양(0이 아닌) 간격이 gcd(T, f)이므로 최악 대기 = f − gcd(T, f).

조건이 서로 싸운다는 점이 핵심이다. (1)은 f를 **크게** 하라고 하고, (3)은 f를 **작게** 하라고 한다. 긴 job(C가 큰 telem)과 짧은 deadline(D가 작은 ctrl)이 한 시스템에 같이 있으면 둘을 동시에 만족하는 f가 없다. 그때 쓰는 기술이 **job slicing**(§1.5)이다.

### 1.4 예제 1 — frame 크기 후보를 거르는 도구

무엇을 확인하는 코드인지: 세 조건을 그대로 코드로 옮겨, Liu 교재의 고전 예제와 드론 task set에서 가능한 f를 찾는다.

```python
#!/usr/bin/env python3
"""cyclic executive: frame size f 후보를 3가지 조건(Baker & Shaw 1989, Liu 2000 5장)으로 거른다"""
from math import gcd, lcm

def check(tasks):
    H = lcm(*[t[1] for t in tasks])                       # hyperperiod (= major cycle)
    maxC = max(t[2] for t in tasks)
    print(f"tasks={[(n, T, C, D) for n, T, C, D in tasks]}  H={H}  maxC={maxC}")
    ok = []
    for f in range(1, H + 1):
        if H % f:                                         # f는 H를 나눠야 (major cycle 안에 정수 개)
            continue
        c1 = f >= maxC                                    # (1) 어떤 job도 frame 하나 안에서 끝나야
        c2 = any(T % f == 0 for _, T, _, _ in tasks)      # (2) f가 적어도 한 주기를 나눔
        bad3 = [n for n, T, C, D in tasks if 2 * f - gcd(T, f) > D]   # (3) release~deadline 사이에 frame 하나가 통째로
        mark = "OK " if c1 and c2 and not bad3 else "   "
        print(f"  {mark}f={f:2}  (1)f>=maxC:{'y' if c1 else 'n'}  (2)divides:{'y' if c2 else 'n'}"
              f"  (3)2f-gcd<=D 실패: {bad3 if bad3 else '-'}")
        if mark == "OK ":
            ok.append(f)
    print("  → 가능한 f:", ok if ok else "없음 (job slicing 필요)")
    return ok

# Liu 교재의 고전 예: (이름, 주기 T, 실행 C, 상대 deadline D)
liu = [("T1", 4, 1, 4), ("T2", 5, 2, 7), ("T3", 20, 5, 20)]
# T3를 1+3+1로 자른 버전
liu_sliced = [("T1", 4, 1, 4), ("T2", 5, 2, 7), ("T31", 20, 1, 20), ("T32", 20, 3, 20), ("T33", 20, 1, 20)]
# 드론 센서 허브(단위 0.5 ms): ctrl 2 ms/0.5, att 4 ms/0.5, nav 10 ms/1.5, telem 20 ms/3.0
drone = [("ctrl", 4, 1, 4), ("att", 8, 1, 8), ("nav", 20, 3, 20), ("telem", 40, 6, 40)]
for name, ts in (("liu", liu), ("liu_sliced", liu_sliced), ("drone(0.5ms 단위)", drone)):
    print(f"== {name} ==")
    check(ts)
```

```sh
limactl shell elinux -- python3 /Users/donh/.rtos_scratch/C11/py/ce_frames.py
```

```text
== liu ==
tasks=[('T1', 4, 1, 4), ('T2', 5, 2, 7), ('T3', 20, 5, 20)]  H=20  maxC=5
     f= 1  (1)f>=maxC:n  (2)divides:y  (3)2f-gcd<=D 실패: -
     f= 2  (1)f>=maxC:n  (2)divides:y  (3)2f-gcd<=D 실패: -
     f= 4  (1)f>=maxC:n  (2)divides:y  (3)2f-gcd<=D 실패: -
     f= 5  (1)f>=maxC:y  (2)divides:y  (3)2f-gcd<=D 실패: ['T1']
     f=10  (1)f>=maxC:y  (2)divides:y  (3)2f-gcd<=D 실패: ['T1', 'T2']
     f=20  (1)f>=maxC:y  (2)divides:y  (3)2f-gcd<=D 실패: ['T1', 'T2']
  → 가능한 f: 없음 (job slicing 필요)
== liu_sliced ==
tasks=[('T1', 4, 1, 4), ('T2', 5, 2, 7), ('T31', 20, 1, 20), ('T32', 20, 3, 20), ('T33', 20, 1, 20)]  H=20  maxC=3
     f= 1  (1)f>=maxC:n  (2)divides:y  (3)2f-gcd<=D 실패: -
     f= 2  (1)f>=maxC:n  (2)divides:y  (3)2f-gcd<=D 실패: -
  OK f= 4  (1)f>=maxC:y  (2)divides:y  (3)2f-gcd<=D 실패: -
     f= 5  (1)f>=maxC:y  (2)divides:y  (3)2f-gcd<=D 실패: ['T1']
     f=10  (1)f>=maxC:y  (2)divides:y  (3)2f-gcd<=D 실패: ['T1', 'T2']
     f=20  (1)f>=maxC:y  (2)divides:y  (3)2f-gcd<=D 실패: ['T1', 'T2']
  → 가능한 f: [4]
== drone(0.5ms 단위) ==
tasks=[('ctrl', 4, 1, 4), ('att', 8, 1, 8), ('nav', 20, 3, 20), ('telem', 40, 6, 40)]  H=40  maxC=6
     f= 1  (1)f>=maxC:n  (2)divides:y  (3)2f-gcd<=D 실패: -
     f= 2  (1)f>=maxC:n  (2)divides:y  (3)2f-gcd<=D 실패: -
     f= 4  (1)f>=maxC:n  (2)divides:y  (3)2f-gcd<=D 실패: -
     f= 5  (1)f>=maxC:n  (2)divides:y  (3)2f-gcd<=D 실패: ['ctrl', 'att']
     f= 8  (1)f>=maxC:y  (2)divides:y  (3)2f-gcd<=D 실패: ['ctrl']
     f=10  (1)f>=maxC:y  (2)divides:y  (3)2f-gcd<=D 실패: ['ctrl', 'att']
     f=20  (1)f>=maxC:y  (2)divides:y  (3)2f-gcd<=D 실패: ['ctrl', 'att']
     f=40  (1)f>=maxC:y  (2)divides:y  (3)2f-gcd<=D 실패: ['ctrl', 'att', 'nav']
  → 가능한 f: 없음 (job slicing 필요)
```

출력에서 볼 것:

- **Liu 예제**(T1 = (4, 1), T2 = (5, 2, D 7), T3 = (20, 5))는 가능한 f가 **없다**. f ≥ 5여야 T3가 들어가는데, f = 5면 T1이 조건 (3)에서 떨어진다(2·5 − gcd(4, 5) = 9 > 4).
- T3를 1 + 3 + 1로 **잘라 세 개의 task처럼** 다루면(max C = 3) **f = 4**가 유일한 답이 된다. Liu 교재가 같은 예에서 내는 답과 같다.
- **드론 set**(0.5 ms 단위)도 답이 없다. telem(C = 3 ms)이 f ≥ 3 ms를 요구하는데 ctrl(D = 2 ms)이 f ≤ 1 ms 근처를 요구한다. → telem을 자른다. ctrl의 주기 2 ms를 frame으로 하면(f = 4 단위 = 2 ms) 조건 (2)(3)은 모두 만족하고(2·4 − gcd(4, 4) = 4 ≤ 4), (1)만 깨진다. 즉 "**telem만 2 ms 이하 조각으로 자르면 된다**".

### 1.5 job slicing — 긴 일을 frame에 맞게 자르기

job slicing은 긴 job을 여러 개의 짧은 조각(slice)으로 나눠, 각 조각을 서로 다른 frame에 배치하는 것이다. 코드로는 A2 §4.4에서 본 "상태 머신으로 조각내기"와 같다. telem이라면 "패킷 40줄 중 다음 n줄만 포맷하고 return"이 된다.

공짜가 아니다.

- **코드가 바뀐다**: 함수가 중간 상태(`tel_line` 같은)를 들고 있어야 하고, 조각 사이에 다른 task가 데이터를 바꿀 수 있다(조각 경계가 사실상의 선점 지점).
- **조각 경계를 어디에 둘지**는 데이터 의존성을 보고 사람이 정한다. 아무 데서나 자를 수 없다.
- 표를 다시 만들 때마다 조각 크기가 바뀔 수 있다.

그래서 Liu 교재는 **f를 고를 때 "되도록 덜 자르는" 쪽을 고르라**고 한다. 실무에서는 조각 단위를 미리 정해 두고(예: "telem은 1회 호출에 최대 1 ms") 그 단위로 표를 짠다.

### 1.6 예제 2 — 시간표를 오프라인으로 만들고 C 헤더로 내보내기

무엇을 확인하는 코드인지: task set을 받아 job 단위로 frame에 배치하고(잘라도 되는 task만 slicing), 결과를 펌웨어가 `#include`할 C 표로 내보낸다. 각 frame에 100 µs를 스케줄러 · ISR 몫으로 비워 둔다.

```python
#!/usr/bin/env python3
"""task set → cyclic executive 시간표(frame별 slice 목록)를 오프라인으로 만들고 C 헤더로 낸다"""
from math import lcm
import sys
F, MARGIN = 2000, 100                       # minor frame 2 ms, frame마다 스케줄러 몫 100 us 비움
# (이름, T us, C us, D us, 잘라도 되나)
TASKS = [("ctrl", 2000, 500, 2000, False), ("att", 4000, 500, 4000, False),
         ("nav", 10000, 1200, 10000, False), ("telem", 20000, 3000, 20000, True)]
H = lcm(*[t[1] for t in TASKS]); NF = H // F
cap = [F - MARGIN] * NF
frames = [[] for _ in range(NF)]
jobs = sorted(((T, r + D, r, i) for i, (_, T, C, D, _) in enumerate(TASKS) for r in range(0, H, T)))
for _, d, r, i in jobs:                     # 짧은 주기 먼저, 같은 주기는 deadline 순
    name, T, C, D, sliceable = TASKS[i]
    ok = [k for k in range(NF) if k * F >= r and (k + 1) * F <= d]   # release 뒤 시작, deadline 전 끝
    fit = [k for k in ok if cap[k] >= C]
    if fit:                                 # 통째로 들어가는 가장 이른 frame
        k = fit[0]; frames[k].append((i, r // T, C)); cap[k] -= C; continue
    if not sliceable:
        sys.exit(f"실패: {name} job{r // T} 를 넣을 frame이 없다 (slicing 필요)")
    left = C
    for k in ok:                            # 잘라서 앞 frame부터 남는 칸에
        s = min(left, cap[k])
        if s > 0:
            frames[k].append((i, r // T, s)); cap[k] -= s; left -= s
        if not left: break
    if left: sys.exit(f"실패: {name} job{r // T} 남은 {left} us")
print(f"H={H} us, f={F} us, frames={NF}, U={sum(t[2] / t[1] for t in TASKS):.3f}")
for k, fr in enumerate(frames):
    used = F - MARGIN - cap[k]
    print(f"frame {k}: [{k * F:5}..{(k + 1) * F:5}) used={used:4} us  " +
          " ".join(f"{TASKS[i][0]}#{j}:{s}" for i, j, s in fr))
with open(sys.argv[1] if len(sys.argv) > 1 else "ce_table.h", "w") as h:  # C 시간표 생성
    h.write(f"/* generated by ce_table.py */\n#define CE_FRAME_US {F}u\n#define CE_NFRAMES {NF}\n")
    h.write("enum { " + ", ".join(f"J_{t[0].upper()}" for t in TASKS) + ", J_N };\n")
    h.write('static const char *const ce_name[] = { ' + ", ".join(f'"{t[0]}"' for t in TASKS) + " };\n")
    h.write("typedef struct { uint8_t job; uint16_t budget_us; } ce_slice_t;\n")
    mx = max(len(f) for f in frames)
    h.write(f"#define CE_MAXS {mx}\nstatic const ce_slice_t ce_table[CE_NFRAMES][CE_MAXS + 1] = {{\n")
    for fr in frames:
        h.write("  { " + "".join(f"{{{i}, {s}}}, " for i, _, s in fr) + "{0xFF, 0} },\n")
    h.write("};\n")
print("wrote", h.name)
```

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/rtos/C11 && python3 /Users/donh/.rtos_scratch/C11/py/ce_table.py /var/tmp/rtos/C11/ce_table.h && cat ce_table.h'
```

```text
H=20000 us, f=2000 us, frames=10, U=0.645
frame 0: [    0.. 2000) used=1900 us  ctrl#0:500 att#0:500 telem#0:900
frame 1: [ 2000.. 4000) used=1900 us  ctrl#1:500 nav#0:1200 telem#0:200
frame 2: [ 4000.. 6000) used=1900 us  ctrl#2:500 att#1:500 telem#0:900
frame 3: [ 6000.. 8000) used=1500 us  ctrl#3:500 telem#0:1000
frame 4: [ 8000..10000) used=1000 us  ctrl#4:500 att#2:500
frame 5: [10000..12000) used=1700 us  ctrl#5:500 nav#1:1200
frame 6: [12000..14000) used=1000 us  ctrl#6:500 att#3:500
frame 7: [14000..16000) used= 500 us  ctrl#7:500
frame 8: [16000..18000) used=1000 us  ctrl#8:500 att#4:500
frame 9: [18000..20000) used= 500 us  ctrl#9:500
wrote /var/tmp/rtos/C11/ce_table.h
/* generated by ce_table.py */
#define CE_FRAME_US 2000u
#define CE_NFRAMES 10
enum { J_CTRL, J_ATT, J_NAV, J_TELEM, J_N };
static const char *const ce_name[] = { "ctrl", "att", "nav", "telem" };
typedef struct { uint8_t job; uint16_t budget_us; } ce_slice_t;
#define CE_MAXS 3
static const ce_slice_t ce_table[CE_NFRAMES][CE_MAXS + 1] = {
  { {0, 500}, {1, 500}, {3, 900}, {0xFF, 0} },
  { {0, 500}, {2, 1200}, {3, 200}, {0xFF, 0} },
  { {0, 500}, {1, 500}, {3, 900}, {0xFF, 0} },
  { {0, 500}, {3, 1000}, {0xFF, 0} },
  { {0, 500}, {1, 500}, {0xFF, 0} },
  { {0, 500}, {2, 1200}, {0xFF, 0} },
  { {0, 500}, {1, 500}, {0xFF, 0} },
  { {0, 500}, {0xFF, 0} },
  { {0, 500}, {1, 500}, {0xFF, 0} },
  { {0, 500}, {0xFF, 0} },
};
```

출력에서 볼 것: ctrl은 10개 frame 전부의 **맨 앞**에 있다(frame 시작 = IRQ 직후라 지터가 가장 작은 자리). telem 3000 µs는 frame 0~3에 **900 + 200 + 900 + 1000**으로 잘렸다. frame 0~2는 1900 µs를 꽉 채웠고 frame 7 · 9는 500 µs만 쓴다.

이 도구를 처음 짤 때 두 번 실패했다. 실패 자체가 공부거리라 그대로 적는다.

- **처음 버전**은 job을 deadline 순(EDF 순)으로 채웠고 nav의 C가 1500 µs였다. 결과는 `실패: ctrl job4 를 넣을 frame이 없다 (slicing 필요)`. deadline이 같은 10 ms인 nav job0과 ctrl job4 중 nav가 먼저 채워져 frame 4를 먹어 버렸기 때문이다. 탐욕(greedy) 배치는 순서에 민감하다. 그래서 "짧은 주기부터, 같은 주기 안에서는 deadline 순"으로 바꿨다.
- 순서를 바꿔도 nav 1500 µs는 들어갈 자리가 없었다. ctrl(500)이 매 frame에 있으니 frame의 남는 칸은 최대 1900 − 500 = 1400 µs. nav는 자를 수 없는 task로 정했으니 C를 1200 µs로 줄였다(실무라면 "nav를 자를 수 있게 고치거나, frame을 키우거나, CPU를 바꾸거나"의 선택이다).

탐욕 배치는 답이 있는데도 못 찾을 수 있다. Liu 교재는 이 문제를 **network flow(최대 유량)** 로 푼다: source → job(용량 C) → 들어갈 수 있는 frame(용량 f) → sink. 최대 유량이 ΣC와 같으면 시간표가 존재하고, 유량이 job을 여러 frame에 나눈 모양이 곧 slicing이다. 실무 도구(항공용 시간표 생성기)는 여기에 "조각 수 최소화", "frame 부하 고르게" 같은 목적을 더한 최적화를 쓴다.

```svg
<svg viewBox="0 0 680 196" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="18" font-size="13">ce_table.py가 만든 시간표 (계획): major cycle 20 ms = minor frame 2 ms × 10</text>
<text x="10" y="36" font-size="12">frame 안에서는 표 순서대로 끝까지 실행, telem(3000 us)은 4조각(slice)으로 잘렸다</text>
<rect x="20.0" y="60" width="64.0" height="44" fill="none" stroke="currentColor" stroke-width="0.8"/>
<text x="52.0" y="120" font-size="12" text-anchor="middle">f0</text>
<rect x="20.0" y="64" width="16.0" height="36" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="36.0" y="64" width="16.0" height="36" fill="#3f9a6b" fill-opacity="0.85"/>
<rect x="52.0" y="64" width="28.8" height="36" fill="#888" fill-opacity="0.85"/>
<text x="81.0" y="56" font-size="12" text-anchor="end">100%</text>
<rect x="84.0" y="60" width="64.0" height="44" fill="none" stroke="currentColor" stroke-width="0.8"/>
<text x="116.0" y="120" font-size="12" text-anchor="middle">f1</text>
<rect x="84.0" y="64" width="16.0" height="36" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="100.0" y="64" width="38.4" height="36" fill="#e08a3c" fill-opacity="0.85"/>
<rect x="138.4" y="64" width="6.4" height="36" fill="#888" fill-opacity="0.85"/>
<text x="145.0" y="56" font-size="12" text-anchor="end">100%</text>
<rect x="148.0" y="60" width="64.0" height="44" fill="none" stroke="currentColor" stroke-width="0.8"/>
<text x="180.0" y="120" font-size="12" text-anchor="middle">f2</text>
<rect x="148.0" y="64" width="16.0" height="36" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="164.0" y="64" width="16.0" height="36" fill="#3f9a6b" fill-opacity="0.85"/>
<rect x="180.0" y="64" width="28.8" height="36" fill="#888" fill-opacity="0.85"/>
<text x="209.0" y="56" font-size="12" text-anchor="end">100%</text>
<rect x="212.0" y="60" width="64.0" height="44" fill="none" stroke="currentColor" stroke-width="0.8"/>
<text x="244.0" y="120" font-size="12" text-anchor="middle">f3</text>
<rect x="212.0" y="64" width="16.0" height="36" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="228.0" y="64" width="32.0" height="36" fill="#888" fill-opacity="0.85"/>
<text x="273.0" y="56" font-size="12" text-anchor="end">78%</text>
<rect x="276.0" y="60" width="64.0" height="44" fill="none" stroke="currentColor" stroke-width="0.8"/>
<text x="308.0" y="120" font-size="12" text-anchor="middle">f4</text>
<rect x="276.0" y="64" width="16.0" height="36" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="292.0" y="64" width="16.0" height="36" fill="#3f9a6b" fill-opacity="0.85"/>
<text x="337.0" y="56" font-size="12" text-anchor="end">52%</text>
<rect x="340.0" y="60" width="64.0" height="44" fill="none" stroke="currentColor" stroke-width="0.8"/>
<text x="372.0" y="120" font-size="12" text-anchor="middle">f5</text>
<rect x="340.0" y="64" width="16.0" height="36" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="356.0" y="64" width="38.4" height="36" fill="#e08a3c" fill-opacity="0.85"/>
<text x="401.0" y="56" font-size="12" text-anchor="end">89%</text>
<rect x="404.0" y="60" width="64.0" height="44" fill="none" stroke="currentColor" stroke-width="0.8"/>
<text x="436.0" y="120" font-size="12" text-anchor="middle">f6</text>
<rect x="404.0" y="64" width="16.0" height="36" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="420.0" y="64" width="16.0" height="36" fill="#3f9a6b" fill-opacity="0.85"/>
<text x="465.0" y="56" font-size="12" text-anchor="end">52%</text>
<rect x="468.0" y="60" width="64.0" height="44" fill="none" stroke="currentColor" stroke-width="0.8"/>
<text x="500.0" y="120" font-size="12" text-anchor="middle">f7</text>
<rect x="468.0" y="64" width="16.0" height="36" fill="#4a7bd0" fill-opacity="0.85"/>
<text x="529.0" y="56" font-size="12" text-anchor="end">26%</text>
<rect x="532.0" y="60" width="64.0" height="44" fill="none" stroke="currentColor" stroke-width="0.8"/>
<text x="564.0" y="120" font-size="12" text-anchor="middle">f8</text>
<rect x="532.0" y="64" width="16.0" height="36" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="548.0" y="64" width="16.0" height="36" fill="#3f9a6b" fill-opacity="0.85"/>
<text x="593.0" y="56" font-size="12" text-anchor="end">52%</text>
<rect x="596.0" y="60" width="64.0" height="44" fill="none" stroke="currentColor" stroke-width="0.8"/>
<text x="628.0" y="120" font-size="12" text-anchor="middle">f9</text>
<rect x="596.0" y="64" width="16.0" height="36" fill="#4a7bd0" fill-opacity="0.85"/>
<text x="657.0" y="56" font-size="12" text-anchor="end">26%</text>
<line x1="20" y1="140" x2="660" y2="140" stroke="currentColor" marker-start="url(#ah2)" marker-end="url(#ah2)"/>
<text x="340.0" y="156" font-size="12" text-anchor="middle">major cycle (hyperperiod) H = 20 ms — 이 표를 끝없이 반복</text>
<rect x="20" y="172" width="12" height="12" fill="#4a7bd0" fill-opacity="0.85"/>
<text x="36" y="182" font-size="12">ctrl</text>
<rect x="84.0" y="172" width="12" height="12" fill="#3f9a6b" fill-opacity="0.85"/>
<text x="100.0" y="182" font-size="12">att</text>
<rect x="140.5" y="172" width="12" height="12" fill="#e08a3c" fill-opacity="0.85"/>
<text x="156.5" y="182" font-size="12">nav</text>
<rect x="197.0" y="172" width="12" height="12" fill="#888" fill-opacity="0.85"/>
<text x="213.0" y="182" font-size="12">telem</text>
<text x="660" y="182" font-size="12" text-anchor="end">위 숫자 = frame 사용률 (1900 us 기준)</text>
</svg>
```

그림 3 — 위 시간표를 그대로 그린 것(계획). 탐욕 배치가 telem을 앞쪽 frame에 몰아서 frame 0~2의 여유가 0이 됐다. 이런 표는 **앞쪽 frame에서 오버런 위험이 크다**. 좋은 시간표 도구는 부하를 고르게 편다.

### 1.7 예제 3 — TIMER0 IRQ로 도는 bare-metal cyclic executive

무엇을 확인하는 코드인지: 생성된 `ce_table.h`를 그대로 include해서, TIMER0 인터럽트(2 ms)를 frame 경계로 쓰는 cyclic executive를 50 major cycle(1 s) 돌린다. ISR은 frame 번호만 올리고, "frame 경계가 왔는데 아직 이전 frame이 일하는 중(`g_busy`)"이면 오버런으로 센다.

```c
/* cyclic executive: TIMER0 IRQ = minor frame(2 ms), 오프라인 시간표(ce_table.h)대로 slice 실행 */
#include <stdint.h>
#include "board.h"
#include "ce_table.h"                         /* ce_table.py가 만든 시간표 */
#ifndef POLICY
#define POLICY 0                              /* 0 = 밀린 frame도 순서대로 실행(catch-up), 1 = 현재 frame으로 건너뜀(skip) */
#endif
#define MAJORS 50                             /* 50 major cycle = 1 s */
static volatile uint32_t g_tick, g_busy, g_overrun, g_ovr_frame = 0xFFFFFFFFu;
static uint32_t t0, done, skipped, maxlate[J_N], runs[J_N], late_ctrl_max, used_max;
void TIMER0_IRQHandler(void)
{
    TIMER_INTCLR(TIMER0_BASE) = 1;
    g_tick++;                                 /* frame 경계 */
    if (g_busy) { g_overrun++; if (g_ovr_frame == 0xFFFFFFFFu) g_ovr_frame = g_tick - 2; }   /* 넘친 frame 번호 */
}
static void spin_us(uint32_t us) { uint32_t s = board_now_us(); while (board_now_us() - s < us) { } }
static void job(int j, uint32_t frame, uint32_t budget)
{
    uint32_t c = budget; (void)j; (void)frame;
#ifdef INJECT                                 /* major cycle 3의 frame 5에서 nav가 1300 us 더 걸린다 */
    if (j == J_NAV && frame == 3u * CE_NFRAMES + 5u) c += 1300;
#endif
    spin_us(c);                               /* 실제라면 여기서 제어·필터·포맷 계산 */
}
int main(void)
{
    TIMER_RELOAD(TIMER0_BASE) = CE_FRAME_US * 25u - 1u;
    NVIC_IPR(8) = 0x40; NVIC_ISER0 = 1u << 8;
    t0 = board_now_us();
    TIMER_CTRL(TIMER0_BASE) = 1u | 8u;        /* EN + IRQ EN: 첫 IRQ = t0 + 2 ms (frame 0 시작) */
    t0 += CE_FRAME_US;
    while (done < MAJORS * CE_NFRAMES) {
        while (g_tick <= done) __asm volatile("wfi");          /* 다음 frame 경계까지 잔다 */
        uint32_t k = done, fi = k % CE_NFRAMES, nominal = t0 + k * CE_FRAME_US;
        g_busy = 1;
        for (const ce_slice_t *s = ce_table[fi]; s->job != 0xFF; s++) {
            uint32_t st = board_now_us();
            if (s->job == J_CTRL && st - nominal > late_ctrl_max) late_ctrl_max = st - nominal;
            job(s->job, k, s->budget_us);
            uint32_t en = board_now_us(), late = en > nominal + CE_FRAME_US ? en - nominal - CE_FRAME_US : 0;
            if (late > maxlate[s->job]) maxlate[s->job] = late;
            runs[s->job]++;
            if (k >= 3 * CE_NFRAMES + 3 && k < 3 * CE_NFRAMES + 9)               /* 간트용 창 */
                board_printf_raw("SEG %s %u %u\n", ce_name[s->job], st - t0, en - t0);
        }
        uint32_t used = board_now_us() - nominal;
        if (used > used_max) used_max = used;
        g_busy = 0;
        done++;
        if (POLICY == 1 && g_tick > done) { skipped += g_tick - done; done = g_tick; }
    }
    TIMER_CTRL(TIMER0_BASE) = 0;
    board_printf_raw("policy=%s frames=%u overrun=%u first_ovr_frame=%d skipped=%u\n",
                     POLICY ? "skip" : "catch-up", done, g_overrun, (int)g_ovr_frame, skipped);
    board_printf_raw("ctrl start late max=%u us, frame used max=%u us\n", late_ctrl_max, used_max);
    for (int j = 0; j < J_N; j++)
        board_printf_raw("  %s runs=%u  finish-after-frame-end max=%u us\n", ce_name[j], runs[j], maxlate[j]);
    return 0;
}
```

```sh
# 생성 헤더는 VM의 /var/tmp/rtos/C11/gen 에 두고 -I로 준다
make -B -s -f /var/rtoslab/templates/baremetal-mps2/Makefile SRCS=$S/ce_exec.c OUT=$A/ce_base EXTRA_CFLAGS="-I$A/gen"
ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 $A/ce_base/app.elf
```

```text
... (실행 중에 먼저 찍힌 간트용 SEG 11줄 생략)
policy=catch-up frames=500 overrun=0 first_ovr_frame=-1 skipped=0
ctrl start late max=1 us, frame used max=1905 us
  ctrl runs=500  finish-after-frame-end max=0 us
  att runs=250  finish-after-frame-end max=0 us
  nav runs=100  finish-after-frame-end max=0 us
  telem runs=200  finish-after-frame-end max=0 us
[board_exit] code=0
```

출력에서 볼 것:

- **ctrl 시작 늦음 최대 1 µs.** frame 경계(IRQ) → `wfi`에서 깨어남 → 표 첫 칸 실행까지가 전부다. 1 s 동안 500번 모두 같다. FP 커널이었다면 여기에 tick 처리 · 문맥 전환 · 다른 task의 크리티컬 섹션이 더해진다(A2 · C7에서 잰 값). cyclic executive의 지터가 작은 이유는 "고를 일이 없기" 때문이다.
- frame 사용 최대 1905 µs = 시간표가 계획한 1900 µs + 측정 오차. **계획 = 실측**이 된다는 것이 time-triggered 설계의 매력이다(실칩에서는 캐시 · 버스 때문에 차이가 생기고, 그래서 frame마다 여유를 둔다).
- 오버런 0, 모든 job이 자기 frame 안에서 끝났다(`finish-after-frame-end max=0`).
- 실행 횟수: ctrl 500 = 1 s / 2 ms, att 250, nav 100, telem 200(= 50 job × 4 slice).

### 1.8 예제 4 — 오버런을 일으키고 두 정책 비교 (catch-up vs skip)

무엇을 확인하는 코드인지: major cycle 3의 frame 5(= 전체 frame 35)에서 nav가 1300 µs 더 걸리게 해(`-DINJECT`) frame 오버런을 만든다. 정책 두 가지를 비교한다.

- `POLICY=0` **catch-up**: 늦게 끝나도 밀린 frame을 순서대로 다 실행한다(아무것도 버리지 않지만 늦는다).
- `POLICY=1` **skip**: 늦게 끝나면 이미 지나간 frame은 건너뛰고 **지금 시각의 frame**부터 다시 맞춘다(시간은 지키지만 일을 버린다).

```sh
for p in 0 1; do
  make -B -s -f /var/rtoslab/templates/baremetal-mps2/Makefile SRCS=$S/ce_exec.c OUT=$A/ce_inj$p EXTRA_CFLAGS="-I$A/gen -DINJECT -DPOLICY=$p"
  echo "### $([ $p = 0 ] && echo catch-up || echo skip) (-DINJECT -DPOLICY=$p)"
  ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 $A/ce_inj$p/app.elf | grep -v "SEG\|board_exit"
done
```

```text
### catch-up (-DINJECT -DPOLICY=0)
policy=catch-up frames=500 overrun=2 first_ovr_frame=35 skipped=0
ctrl start late max=1029 us, frame used max=3027 us
  ctrl runs=500  finish-after-frame-end max=0 us
  att runs=250  finish-after-frame-end max=43 us
  nav runs=100  finish-after-frame-end max=1015 us
  telem runs=200  finish-after-frame-end max=0 us
### skip (-DINJECT -DPOLICY=1)
policy=skip frames=500 overrun=1 first_ovr_frame=35 skipped=1
ctrl start late max=1 us, frame used max=3027 us
  ctrl runs=499  finish-after-frame-end max=0 us
  att runs=249  finish-after-frame-end max=0 us
  nav runs=100  finish-after-frame-end max=1015 us
  telem runs=200  finish-after-frame-end max=0 us
```

출력에서 볼 것:

- 두 정책 모두 ISR이 **frame 35의 오버런을 정확히 잡았다**(`first_ovr_frame=35`). 감지 비용은 ISR 안의 `if (g_busy)` 한 줄이다.
- **catch-up**: 오버런이 2번이다. frame 35가 넘쳐서 frame 36이 1029 µs 늦게 시작했고, frame 36(ctrl + att = 1000 µs)이 다시 frame 37 경계를 43 µs 넘었다(`att ... max=43`). 늦음이 **다음 frame으로 번지다가** 여유가 있는 frame 37(ctrl만, 500 µs)에서 흡수됐다. job 수는 그대로(ctrl 500).
- **skip**: 오버런 1번, 늦음은 nav 하나뿐이고 ctrl 시작 늦음 최대는 다시 1 µs다. 대신 frame 36을 통째로 버려서 **ctrl 1번, att 1번이 실행되지 않았다**(499 · 249).

```svg
<svg viewBox="0 0 680 206" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="18" font-size="13">cyclic executive 실측 간트 — minor frame 2 ms (점선 = TIMER0 IRQ = frame 경계)</text>
<text x="10" y="36" font-size="12">frame 35의 nav가 1300 us 초과 → frame 36이 밀림(catch-up) 또는 버려짐(skip)</text>
<line x1="130.0" y1="44" x2="130.0" y2="70" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<line x1="216.7" y1="44" x2="216.7" y2="70" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<line x1="303.3" y1="44" x2="303.3" y2="70" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<line x1="390.0" y1="44" x2="390.0" y2="70" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<line x1="476.7" y1="44" x2="476.7" y2="70" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<line x1="563.3" y1="44" x2="563.3" y2="70" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<line x1="650.0" y1="44" x2="650.0" y2="70" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<text x="10" y="59" font-size="12">정상</text>
<line x1="130" y1="68" x2="650" y2="68" stroke="currentColor" stroke-width="0.4" opacity="0.4"/>
<rect x="130.0" y="47" width="21.7" height="20" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="152.3" y="47" width="43.4" height="20" fill="#888" fill-opacity="0.85"/>
<rect x="216.7" y="47" width="21.7" height="20" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="238.9" y="47" width="21.7" height="20" fill="#3f9a6b" fill-opacity="0.85"/>
<rect x="303.4" y="47" width="21.7" height="20" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="325.6" y="47" width="52.0" height="20" fill="#e08a3c" fill-opacity="0.85"/>
<text x="351.6" y="59" font-size="12" text-anchor="middle">nav</text>
<rect x="390.0" y="47" width="21.7" height="20" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="412.3" y="47" width="21.7" height="20" fill="#3f9a6b" fill-opacity="0.85"/>
<rect x="476.7" y="47" width="21.7" height="20" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="563.4" y="47" width="21.7" height="20" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="585.6" y="47" width="21.7" height="20" fill="#3f9a6b" fill-opacity="0.85"/>
<line x1="130.0" y1="84" x2="130.0" y2="110" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<line x1="216.7" y1="84" x2="216.7" y2="110" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<line x1="303.3" y1="84" x2="303.3" y2="110" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<line x1="390.0" y1="84" x2="390.0" y2="110" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<line x1="476.7" y1="84" x2="476.7" y2="110" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<line x1="563.3" y1="84" x2="563.3" y2="110" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<line x1="650.0" y1="84" x2="650.0" y2="110" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<text x="10" y="99" font-size="12">catch-up</text>
<line x1="130" y1="108" x2="650" y2="108" stroke="currentColor" stroke-width="0.4" opacity="0.4"/>
<rect x="130.1" y="87" width="21.7" height="20" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="152.3" y="87" width="43.4" height="20" fill="#888" fill-opacity="0.85"/>
<rect x="216.8" y="87" width="21.7" height="20" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="238.9" y="87" width="21.7" height="20" fill="#3f9a6b" fill-opacity="0.85"/>
<rect x="303.4" y="87" width="21.7" height="20" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="325.6" y="87" width="108.4" height="20" fill="#e08a3c" fill-opacity="0.85"/>
<text x="379.8" y="99" font-size="12" text-anchor="middle">nav</text>
<rect x="434.6" y="87" width="21.7" height="20" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="456.8" y="87" width="21.7" height="20" fill="#3f9a6b" fill-opacity="0.85"/>
<rect x="479.1" y="87" width="21.7" height="20" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="563.4" y="87" width="21.7" height="20" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="585.6" y="87" width="21.7" height="20" fill="#3f9a6b" fill-opacity="0.85"/>
<line x1="390.0" y1="84" x2="390.0" y2="108" stroke="#d0564a" stroke-width="2"/>
<text x="394.0" y="82" font-size="12" text-anchor="start">IRQ 때 busy → 오버런</text>
<line x1="130.0" y1="124" x2="130.0" y2="150" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<line x1="216.7" y1="124" x2="216.7" y2="150" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<line x1="303.3" y1="124" x2="303.3" y2="150" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<line x1="390.0" y1="124" x2="390.0" y2="150" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<line x1="476.7" y1="124" x2="476.7" y2="150" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<line x1="563.3" y1="124" x2="563.3" y2="150" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<line x1="650.0" y1="124" x2="650.0" y2="150" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<text x="10" y="139" font-size="12">skip</text>
<line x1="130" y1="148" x2="650" y2="148" stroke="currentColor" stroke-width="0.4" opacity="0.4"/>
<rect x="130.0" y="127" width="21.7" height="20" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="152.3" y="127" width="43.4" height="20" fill="#888" fill-opacity="0.85"/>
<rect x="216.7" y="127" width="21.7" height="20" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="238.9" y="127" width="21.7" height="20" fill="#3f9a6b" fill-opacity="0.85"/>
<rect x="303.4" y="127" width="21.7" height="20" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="325.6" y="127" width="108.4" height="20" fill="#e08a3c" fill-opacity="0.85"/>
<text x="379.8" y="139" font-size="12" text-anchor="middle">nav</text>
<rect x="476.7" y="127" width="21.7" height="20" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="563.4" y="127" width="21.7" height="20" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="585.6" y="127" width="21.7" height="20" fill="#3f9a6b" fill-opacity="0.85"/>
<line x1="390.0" y1="124" x2="390.0" y2="148" stroke="#d0564a" stroke-width="2"/>
<text x="394.0" y="122" font-size="12" text-anchor="start">IRQ 때 busy → 오버런</text>
<line x1="130" y1="164" x2="650" y2="164" stroke="currentColor"/>
<line x1="130.0" y1="164" x2="130.0" y2="170" stroke="currentColor"/>
<text x="130.0" y="182" font-size="12" text-anchor="middle">66</text>
<line x1="216.7" y1="164" x2="216.7" y2="170" stroke="currentColor"/>
<text x="216.7" y="182" font-size="12" text-anchor="middle">68</text>
<line x1="303.3" y1="164" x2="303.3" y2="170" stroke="currentColor"/>
<text x="303.3" y="182" font-size="12" text-anchor="middle">70</text>
<line x1="390.0" y1="164" x2="390.0" y2="170" stroke="currentColor"/>
<text x="390.0" y="182" font-size="12" text-anchor="middle">72</text>
<line x1="476.7" y1="164" x2="476.7" y2="170" stroke="currentColor"/>
<text x="476.7" y="182" font-size="12" text-anchor="middle">74</text>
<line x1="563.3" y1="164" x2="563.3" y2="170" stroke="currentColor"/>
<text x="563.3" y="182" font-size="12" text-anchor="middle">76</text>
<line x1="650.0" y1="164" x2="650.0" y2="170" stroke="currentColor"/>
<text x="650.0" y="182" font-size="12" text-anchor="middle">78</text>
<text x="650" y="198" font-size="12" text-anchor="end">시각 (ms)</text>
<rect x="10" y="188" width="12" height="12" fill="#4a7bd0" fill-opacity="0.85"/>
<text x="26" y="198" font-size="12">ctrl</text>
<rect x="70.0" y="188" width="12" height="12" fill="#3f9a6b" fill-opacity="0.85"/>
<text x="86.0" y="198" font-size="12">att</text>
<rect x="122.5" y="188" width="12" height="12" fill="#e08a3c" fill-opacity="0.85"/>
<text x="138.5" y="198" font-size="12">nav</text>
<rect x="175.0" y="188" width="12" height="12" fill="#888" fill-opacity="0.85"/>
<text x="191.0" y="198" font-size="12">telem</text>
</svg>
```

그림 4 — 실측 간트(66~78 ms, 출력의 SEG 줄에서 그림). 빨간 선 = frame 36 경계에서 ISR이 busy를 본 순간. 가운데 줄은 frame 36의 ctrl · att가 밀려서 실행되고, 아래 줄은 frame 36이 비어 있다. 각 조각 사이의 약 12 µs 틈은 간트용 `printf` 한 줄 비용이다.

### 1.9 오버런 처리 정책 — 무엇을 버릴지는 시스템이 정한다

| 정책 | 동작 | 장점 | 단점 | 어울리는 곳 |
|---|---|---|---|---|
| catch-up (밀어서 실행) | 늦게라도 표 순서대로 다 실행 | 일을 잃지 않는다 | 늦음이 다음 frame으로 번진다 | 로그 · 적산처럼 빠뜨리면 안 되는 일 |
| skip (현재 frame으로 점프) | 지나간 frame을 버리고 시계에 맞춘다 | 시간 축이 바로 회복된다 | job을 잃는다 (ctrl 1회 누락) | 제어 루프: 늦은 출력보다 한 번 건너뛰는 게 낫다 |
| abort (예산 초과 job 강제 종료) | 타이머로 예산을 넘은 job을 끊는다 | 번짐이 0 | 비선점 구조에서 구현이 어렵다 (longjmp, 상태 복구) | 인증 시스템의 budget enforcement |
| 모드 전환 | 오버런이 반복되면 축소 시간표(degraded mode)로 | 시스템 전체가 살아남는다 | 시간표가 여러 장 필요 | 항공 · 차량의 안전 모드 |
| 리셋 (watchdog) | 오버런 = 고장으로 보고 재시작 | 단순, 확실 | 서비스 중단 | 작은 안전 MCU |

어느 쪽이든 **감지가 먼저**다. cyclic executive에서 감지는 이렇게 싸다(ISR에서 깃발 하나). FP 커널에서는 "deadline을 넘었다"를 알려면 job마다 시각을 재야 한다(§4의 예제들이 그렇게 한다).

### 1.10 cyclic executive의 장단점 정리

장점:

- **결정성**: 실행 순서와 시각이 표로 고정 — 시험한 그대로 돈다. 최악 응답 시간 = 표에서 읽는 값.
- **분석 · 인증이 쉽다**: "이 표가 모든 deadline을 지킨다"를 오프라인에서 한 번 증명하면 끝. 항공 소프트웨어(DO-178)에서 오래 쓰인 이유.
- **커널이 없다**: 스택 하나, 락 없음, 코드 수 KB(예제 12).
- **지터가 최소**: ctrl 시작 늦음 1 µs.

단점:

- **비주기(sporadic) 이벤트에 약하다**: 언제 올지 모르는 이벤트는 "폴링 slot"을 표에 넣어 처리하므로 최악 지연 = 폴링 주기.
- **바꾸기 비싸다**: task 하나의 주기나 C가 바뀌면 표 전체를 다시 만들고 다시 검증한다. 주기가 서로 소(5 ms, 7 ms)면 H가 커져 표가 길어진다.
- **job slicing이 코드를 망가뜨린다**: 긴 일을 상태 머신으로 쪼개야 한다.
- **CPU 낭비**: 주기를 조화(harmonic)로 맞추려고 실제보다 자주 돌리는 일이 생긴다.

### 1.11 흔한 함정

- 시간표의 C를 **평균** 실행 시간으로 잡는다 → 가끔 오버런. C는 WCET(최악 실행 시간, N7) + 여유로 잡는다.
- frame 시작 IRQ와 같은 우선순위 이상의 다른 ISR을 많이 둔다 → ISR 시간이 frame 예산을 갉아먹는다. frame마다 ISR 몫을 비워 둔다(예제 2의 `MARGIN`).
- 오버런을 감지만 하고 정책이 없다 → 늦음이 무한히 번진다(catch-up이 회복되려면 여유 frame이 있어야 한다).

---

## 2. Pont의 time-triggered cooperative(TTC) 스케줄러

> 🔁 이 절은 Bare-Metal BM K5(태스크 표, Pont TTC, 시간 예산 · 오버런 감지)와 겹친다 (주인: 이 노트).

### 2.1 직관 — cyclic executive를 "실행 중에 계산하는" 버전

Michael J. Pont의 책 『Patterns for Time-Triggered Embedded Systems』(ACM Press / Addison-Wesley, 2001)는 8051 같은 작은 MCU에서 쓰는 time-triggered 협력형 스케줄러를 패턴으로 정리했다. 구조는 단순하다.

- **task 표** `SCH_tasks_G[]`: 한 줄 = { 함수 포인터 `pTask`, 첫 실행까지 남은 tick `Delay`, 주기 `Period`, 실행할 차례 수 `RunMe` }.
- **`SCH_Update()`** — tick ISR: 모든 task의 `Delay`를 하나씩 줄이고, 0이 되면 `RunMe`를 올리고 `Delay = Period`로 되돌린다. **ISR은 task를 실행하지 않는다**.
- **`SCH_Dispatch_Tasks()`** — `main`의 무한 루프: `RunMe > 0`인 task를 표 순서대로 실행하고 `RunMe`를 내린다.
- **`SCH_Go_To_Sleep()`** — 할 일이 없으면 다음 tick까지 잔다(idle 모드).
- **`SCH_Add_Task(fn, DELAY, PERIOD)`** — 시작 전에 표를 채운다. `DELAY`가 **offset**이다.

cyclic executive와 다른 점은 표가 "frame별 실행 목록"이 아니라 "task별 주기 · offset"이라는 것이다. 어느 tick에 무엇이 도는지는 `SCH_Update`가 **실행 중에 센다**. 그래서 task를 추가하기 쉽고, 결과는 offset을 잘 고르면 cyclic executive와 같은 고정 패턴이 된다. A2 §4의 표 스케줄러와 비교하면, A2는 "release된 것 중 표 위쪽 하나를 고르고 다시 처음부터"였고 Pont TTC는 "tick마다 release를 세고, tick 사이에 밀린 것을 모두 실행"이다.

Pont 설계의 핵심 규칙은 하나다: **한 tick 안에 그 tick에 release된 task가 모두 끝나야 한다.** 그래야 다음 tick의 task가 제때 시작한다. 이것을 지키는 도구가 **offset**이다. 주기가 10 ms인 task 둘을 같은 tick에 시작시키지 말고 tick 하나씩 어긋나게 둔다.

### 2.2 예제 5 — Pont TTC: offset, 시간 예산, tick 오버런 감지

무엇을 확인하는 코드인지: Pont의 구조(`sTask`, `SCH_Update` = SysTick ISR, `SCH_Dispatch_Tasks`, `SCH_Add_Task`)를 그대로 따라 다시 쓰고, task마다 **시간 예산(budget)** 과 관측 최대 실행 시간을 붙였다. 1 ms tick에 ctrl(1 ms, 200 µs) · filt(5 ms, 400 µs) · log(10 ms, 500 µs) · telem(10 ms, 300 µs)을 3초 돌린다. 세 가지로 빌드한다: offset 없음 / offset 있음 / offset 있음 + log가 50번에 한 번 900 µs(예산 600 µs 위반).

```c
/* Pont 방식 time-triggered cooperative(TTC) 스케줄러 + 시간 예산(budget)·tick 오버런 감지 */
#include <stdint.h>
#include "board.h"
#ifndef OFS
#define OFS 1                                   /* 1 = 주기가 겹치지 않게 offset(첫 Delay)을 준다 */
#endif
typedef struct { void (*pTask)(void); uint32_t Delay, Period, RunMe, budget_us, wcet_us, over; const char *name; } sTask;
#define SCH_MAX 4
static sTask SCH_tasks_G[SCH_MAX];
static volatile uint32_t g_ticks, g_tick_us, g_in_dispatch, g_tick_overrun, g_first_ovr = 0xFFFFFFFFu;
static uint32_t load_us, load_max, ctrl_late_max;   /* tick 하나 안에서 쓴 시간, ctrl 시작 늦음 */
static void spin_us(uint32_t us) { uint32_t s = board_now_us(); while (board_now_us() - s < us) { } }
static void t_ctrl(void)  { spin_us(200); }    static void t_filt(void)  { spin_us(400); }
static uint32_t n_log; static void t_log(void) { spin_us(++n_log % 50 == 0 && INJECT ? 900 : 500); }   /* 50번에 한 번 길어짐(INJECT) */
static void t_telem(void) { spin_us(300); }
static void SCH_Add_Task(void (*f)(void), uint32_t delay, uint32_t period, uint32_t budget, const char *nm)
{ for (int i = 0; i < SCH_MAX; i++) if (!SCH_tasks_G[i].pTask) {
      SCH_tasks_G[i] = (sTask){ f, delay, period, 0, budget, 0, 0, nm }; return; } }
void SysTick_Handler(void)                      /* = Pont의 SCH_Update(): tick마다 Delay를 센다 */
{
    g_ticks++; g_tick_us = board_now_us();
    if (g_in_dispatch) { g_tick_overrun++; if (g_first_ovr == 0xFFFFFFFFu) g_first_ovr = g_ticks; }
    for (int i = 0; i < SCH_MAX; i++) {
        sTask *t = &SCH_tasks_G[i];
        if (!t->pTask) continue;
        if (t->Delay == 0) { t->RunMe++; t->Delay = t->Period; }
        t->Delay--;
    }
}
static void SCH_Dispatch_Tasks(void)            /* main 루프: RunMe인 task를 표 순서대로 끝까지 실행 */
{
    g_in_dispatch = 1;
    uint32_t s0 = board_now_us();
    for (int i = 0; i < SCH_MAX; i++) {
        sTask *t = &SCH_tasks_G[i];
        if (t->RunMe == 0) continue;
        uint32_t s = board_now_us();
        if (i == 0 && s - g_tick_us > ctrl_late_max) ctrl_late_max = s - g_tick_us;
        t->pTask();
        uint32_t c = board_now_us() - s;
        if (c > t->wcet_us) t->wcet_us = c;     /* 관측 최대 실행 시간 */
        if (c > t->budget_us) t->over++;        /* 시간 예산 위반 */
        t->RunMe--;
    }
    load_us = board_now_us() - s0;
    if (load_us > load_max) load_max = load_us;
    g_in_dispatch = 0;
}
int main(void)
{
    SCH_Add_Task(t_ctrl, 0, 1, 250, "ctrl");    /* (함수, 첫 Delay tick, 주기 tick, 예산 us) */
    SCH_Add_Task(t_filt, OFS ? 1 : 0, 5, 450, "filt");
    SCH_Add_Task(t_log, OFS ? 2 : 0, 10, 600, "log");
    SCH_Add_Task(t_telem, OFS ? 3 : 0, 10, 350, "telem");
    REG32(0xE000E014) = 25000 - 1; REG32(0xE000E018) = 0; REG32(0xE000E010) = 7;   /* SysTick 1 kHz */
    uint32_t seen = 0;
    while (g_ticks < 3000) {                    /* 3 s */
        while (g_ticks == seen) __asm volatile("wfi");   /* SCH_Go_To_Sleep() */
        seen = g_ticks;
        SCH_Dispatch_Tasks();
        if (g_ticks != seen) seen = g_ticks - 1; /* 밀린 tick이 있으면 바로 한 번 더 dispatch */
    }
    REG32(0xE000E010) = 0;
    board_printf_raw("OFS=%d INJECT=%d ticks=%u tick_overrun=%u (first at tick %d) max load/tick=%u us ctrl_late_max=%u us\n",
                     OFS, INJECT, g_ticks, g_tick_overrun, (int)g_first_ovr, load_max, ctrl_late_max);
    for (int i = 0; i < SCH_MAX; i++)
        board_printf_raw("  %s budget=%u wcet=%u over=%u\n", SCH_tasks_G[i].name,
                         SCH_tasks_G[i].budget_us, SCH_tasks_G[i].wcet_us, SCH_tasks_G[i].over);
    return 0;
}
```

```sh
for v in "0 0" "1 0" "1 1"; do set -- $v
  make -B -s -f /var/rtoslab/templates/baremetal-mps2/Makefile SRCS=$S/ttc.c OUT=$A/ttc_$1$2 EXTRA_CFLAGS="-DOFS=$1 -DINJECT=$2"
  ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 $A/ttc_$1$2/app.elf | grep -v board_exit
done
```

```text
OFS=0 INJECT=0 ticks=3000 tick_overrun=300 (first at tick 2) max load/tick=1406 us ctrl_late_max=409 us
  ctrl budget=250 wcet=201 over=0
  filt budget=450 wcet=400 over=0
  log budget=600 wcet=501 over=0
  telem budget=350 wcet=300 over=0
OFS=1 INJECT=0 ticks=3000 tick_overrun=0 (first at tick -1) max load/tick=704 us ctrl_late_max=3 us
  ctrl budget=250 wcet=201 over=0
  filt budget=450 wcet=400 over=0
  log budget=600 wcet=501 over=0
  telem budget=350 wcet=301 over=0
OFS=1 INJECT=1 ticks=3000 tick_overrun=6 (first at tick 494) max load/tick=1406 us ctrl_late_max=409 us
  ctrl budget=250 wcet=201 over=0
  filt budget=450 wcet=400 over=0
  log budget=600 wcet=901 over=6
  telem budget=350 wcet=301 over=0
```

출력에서 볼 것:

- **offset 없음**: 첫 tick에 네 task가 모두 release → 200 + 400 + 500 + 300 = 1400 µs가 1 ms tick 하나에 몰린다. 10 ms마다 이렇게 되므로 3초에 **300번 tick 오버런**, ctrl 시작이 최대 **409 µs** 늦는다. task 하나하나는 예산을 지켰는데(over = 0) **시스템은 규칙을 어겼다**. 개별 예산만 보면 이 문제를 못 본다.
- **offset 있음**(filt 1, log 2, telem 3 tick 어긋남): 한 tick 최대 부하 704 µs(ctrl + log), 오버런 0, ctrl 늦음 **3 µs**. 코드는 한 글자(`DELAY`)만 바꿨다.
- **offset + log 가끔 900 µs**: log의 예산 위반 6번(`over=6`, wcet 901)을 task 단위로 정확히 잡았고, 그때마다 tick 오버런도 6번 났다. 두 감지기가 서로 다른 것을 본다: **예산 감지는 "누가" 범인인지**, **tick 오버런 감지는 "시스템 규칙이 깨졌는지"** 를 알려 준다.

```svg
<svg viewBox="0 0 680 202" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="18" font-size="13">Pont TTC: tick 하나 안의 최대 부하와 ctrl 시작 늦음 (실측, 3 s)</text>
<text x="10" y="36" font-size="12">파랑 = tick 안 최대 dispatch 시간, 주황 = ctrl 최대 시작 늦음. 빨간 점선 = tick 1 ms</text>
<text x="10" y="71" font-size="12">offset 없음</text>
<rect x="230" y="56" width="256.4" height="11" fill="#4a7bd0" fill-opacity="0.8"/>
<text x="490.4" y="66" font-size="12">1406 us, 오버런 300</text>
<rect x="230" y="68" width="74.6" height="11" fill="#e08a3c" fill-opacity="0.8"/>
<text x="308.6" y="78" font-size="12">409 us</text>
<text x="10" y="105" font-size="12">offset 있음</text>
<rect x="230" y="90" width="128.4" height="11" fill="#4a7bd0" fill-opacity="0.8"/>
<text x="362.4" y="100" font-size="12">704 us, 오버런 0</text>
<rect x="230" y="102" width="0.5" height="11" fill="#e08a3c" fill-opacity="0.8"/>
<text x="234.5" y="112" font-size="12">3 us</text>
<text x="10" y="139" font-size="12">offset + log 가끔 900 us</text>
<rect x="230" y="124" width="256.4" height="11" fill="#4a7bd0" fill-opacity="0.8"/>
<text x="490.4" y="134" font-size="12">1406 us, 오버런 6</text>
<rect x="230" y="136" width="74.6" height="11" fill="#e08a3c" fill-opacity="0.8"/>
<text x="308.6" y="146" font-size="12">409 us</text>
<line x1="230" y1="50" x2="230" y2="158" stroke="currentColor"/>
<line x1="412.4" y1="50" x2="412.4" y2="158" stroke="#d0564a" stroke-width="1.5" stroke-dasharray="4 3"/>
<text x="412.4" y="174" font-size="12" text-anchor="middle">tick 1000 us</text>
<text x="610" y="188" font-size="12" text-anchor="end">us</text>
</svg>
```

그림 5 — Pont TTC 실측. offset 하나로 tick 최대 부하가 1406 → 704 µs, ctrl 지연이 409 → 3 µs가 된다. 예산을 넘는 task가 하나 생기면 다시 tick(빨간 점선)을 넘는다.

### 2.3 시간 예산 · 오버런 감지 방법 비교

| 방법 | 어디서 보나 | 무엇을 잡나 | 비용 | 한계 |
|---|---|---|---|---|
| tick ISR의 busy 깃발 (예제 4 · 5) | 다음 tick ISR | "tick 안에 못 끝남" (시스템 규칙) | ISR에 비교 1개 | 범인을 모른다, 사후 감지 |
| task별 실행 시간 측정 (예제 5의 `over`) | dispatch 루프 | 누가 예산을 넘었나 | task마다 시계 읽기 2번 | task가 끝나야 안다 (무한 루프는 못 잡음) |
| 하드웨어 watchdog | WDT 타이머 | 시스템이 통째로 멈춤 | 거의 0 | 리셋밖에 못 한다, 해상도가 거침 |
| 예산 타이머 (task 시작 때 one-shot 타이머 장전) | 타이머 ISR | 예산 초과 **그 순간** | 타이머 1개 + ISR | 협력형에서 끊어 내기(abort)가 어렵다 |

Pont는 이후 책(『The Engineering of Reliable Embedded Systems』, SafeTTy Systems)에서 task 실행 시간 감시를 더 체계적으로 다룬다(세부 메커니즘 이름은 여기서 확인하지 않았다). 실무 감각으로는 **"예산 측정 + tick 오버런 깃발 + watchdog"** 세 겹이 기본이다.

### 2.4 TTC의 한계와 hybrid

TTC는 "모든 task가 tick 하나보다 짧아야 한다"는 강한 가정을 한다. 긴 task(telem 5 ms)가 하나라도 있으면 쪼개야 한다(§1.5와 같은 문제). Pont의 책은 이를 위해 **hybrid scheduler**(협력형 task들 + tick ISR 안에서 도는 선점형 task 하나)도 패턴으로 다룬다. 가장 급한 짧은 일(ctrl) 하나만 ISR에서 선점적으로 돌리고 나머지는 협력형으로 두는 구조다. A2의 M2(foreground/background)와 같은 생각이다. 그 이상 선점이 필요해지면 그때가 RTOS(FP)로 갈 때다(A7).

---

## 3. 분산 시스템의 정적 시간표 — TTA · TTEthernet · FlexRay (맥락)

시간표 기반 설계는 CPU 하나에서 끝나지 않는다. **네트워크 전체가 같은 시간표로 움직이게** 만든 것이 time-triggered architecture다.

| 기술 | 무엇을 시간표로 고정하나 | 쓰이는 곳 | 메모 |
|---|---|---|---|
| TTA / TTP/C | 모든 노드의 메시지 송신 slot (TDMA), 전역 동기 시계 | 항공 · 철도 제어 | H. Kopetz(빈 공대)의 time-triggered architecture. 노드 고장이 버스를 망치지 못하게 bus guardian을 둔다 |
| TTEthernet | 이더넷 위의 time-triggered 프레임 + rate-constrained + best-effort | 항공 · 우주 | SAE AS6802로 표준화 |
| FlexRay | 통신 사이클의 **정적 구간**(TDMA slot) + 동적 구간 | 차량 섀시 · 브레이크 계열 | ISO 17458. 정적 구간 = 네트워크판 cyclic executive |
| TSN 802.1Qbv | 스위치 큐의 열림/닫힘 시간표(gate control list) | 산업 · 차량 이더넷 | time-aware shaper. 시간 동기는 802.1AS |
| AUTOSAR OS schedule table | 한 ECU 안에서 정해진 시각(expiry point)에 task 활성화 | 차량 ECU | AUTOSAR OS가 OSEK에 더한 time-triggered 기능, 전역 시간에 동기 가능 |

공통 원리는 §1과 같다: **설계 때 충돌 없는 시간표를 증명하고, 실행 때는 시계만 본다.** 노드의 소프트웨어 시간표(cyclic executive)와 버스 slot 시간표를 맞물리게 설계하면(센서 읽기 frame → 송신 slot → 수신 노드의 제어 frame) 종단 간 지연이 상수가 된다. 차량 회사(Rivian 같은) 면접에서 "FlexRay 정적 구간이나 TSN을 RTOS task 주기와 어떻게 맞추나"가 나오면 이 그림을 말하면 된다. 자세한 버스 이야기는 J 모듈 참조.

---
## 4. EDF — deadline이 가장 가까운 job부터

### 4.1 직관과 정의

병원 응급실을 생각하자. FP는 "환자 종류마다 순번표 색이 정해진" 방식이다(심장 환자는 항상 빨강). EDF는 "**지금 이 순간 가장 먼저 위험해질 환자**부터" 보는 방식이다. 같은 심장 환자라도 방금 온 사람은 아직 여유가 있고, 오래 기다린 골절 환자는 곧 한계일 수 있다.

- **EDF(Earliest Deadline First)**: ready인 job 중 **절대 deadline**(release 시각 + 상대 deadline D)이 가장 이른 것을 실행한다. 새 job이 release될 때마다 순서가 바뀔 수 있다.
- **동적 우선순위**: 우선순위가 task가 아니라 **job**에 붙는다. 한 job 안에서는 deadline이 변하지 않으니 "job-level fixed priority"라고도 한다.
- 고전 결과(증명은 N5): 단일 CPU, 선점 가능, 독립 task, D = T이면 **U ≤ 1 ⇔ EDF로 스케줄 가능**(Liu & Layland 1973). EDF는 이런 조건에서 **최적**이다 — 어떤 알고리즘이 지킬 수 있는 task set이면 EDF도 지킨다.
- 비교: RM(주기 짧을수록 높은 고정 우선순위)은 U ≤ n(2^(1/n) − 1)이면 **충분**(n = 3이면 0.7798, n → ∞이면 ln 2 ≈ 0.693). 그 위는 RTA(N3)로 하나씩 확인해야 한다.

즉 **U가 0.78~1.0 사이인 task set** 중에 "RM으로는 안 되는데 EDF로는 되는" 것이 있다. 이 절은 그런 set 하나를 골라 계산으로 증명하고, 실제 커널에서 돌려 확인한다.

### 4.2 예제 6 — RM은 실패, EDF는 성공함을 계산으로 보이기

무엇을 확인하는 코드인지: 세 task(rate C 1 ms / T 4 ms, att 2.5 / 6, nav 2 / 8, D = T, U = 0.9167)에 Liu-Layland 상한, hyperbolic 상한(Bini · Buttazzo 2003: Π(Uᵢ + 1) ≤ 2면 RM OK), RTA를 적용하고, 이상적인 단일 CPU 시뮬레이션(오버헤드 0, 50 µs 단위)으로 1초 동안 miss를 센다.

```python
#!/usr/bin/env python3
"""task set: RM(고정 우선순위)은 실패, EDF는 성공함을 계산(RTA)과 이상적 시뮬레이션으로 보인다"""
import math, sys
TS = [("rate", 1000, 4000), ("att", 2500, 6000), ("nav", 2000, 8000)]   # (이름, C us, T us), D = T

def rta(ts):                                      # 주기 짧은 순 = RM 우선순위
    out = []
    for i, (n, C, T) in enumerate(ts):
        R = C
        while True:                               # R = C + Σ ceil(R/Tj)·Cj  (고정점 반복)
            Rn = C + sum(math.ceil(R / Tj) * Cj for _, Cj, Tj in ts[:i])
            if Rn == R or Rn > T: break
            R = Rn
        out.append((n, Rn, T, "OK" if Rn <= T else "MISS"))
    return out

def simulate(ts, policy, horizon, step=50):       # 선점형 단일 CPU, 늦은 job도 끝까지 실행(abort 없음)
    rem, dl, rel = [0] * len(ts), [0] * len(ts), [0] * len(ts)
    miss, jobs, worst = [0] * len(ts), [0] * len(ts), [0] * len(ts)
    queue = [[] for _ in ts]                      # task별 밀린 job의 (release, deadline)
    for t in range(0, horizon, step):
        for i, (_, C, T) in enumerate(ts):
            if t % T == 0: queue[i].append([t, t + T, C]); jobs[i] += 1
        ready = [i for i in range(len(ts)) if queue[i]]
        if not ready: continue
        k = min(ready) if policy == "FP" else min(ready, key=lambda i: (queue[i][0][1], i))
        queue[k][0][2] -= step
        if queue[k][0][2] <= 0:                   # job 끝
            r, d, _ = queue[k].pop(0); f = t + step
            worst[k] = max(worst[k], f - r)
            if f > d: miss[k] += 1
    return jobs, miss, worst

if __name__ == "__main__":
    U = sum(C / T for _, C, T in TS); n = len(TS)
    print(f"U = {U:.4f}   Liu&Layland bound n(2^(1/n)-1) = {n * (2 ** (1 / n) - 1):.4f}"
          f"   hyperbolic Π(Ui+1) = {math.prod(C / T + 1 for _, C, T in TS):.4f} (<=2 이면 RM OK)")
    for row in rta(TS): print("  RTA %-5s R=%5d us  D=%5d  %s" % row)
    print(f"  EDF (D=T): U = {U:.4f} <= 1  →  schedulable")
    H = math.lcm(*[T for _, _, T in TS])
    for pol in ("FP", "EDF"):
        jobs, miss, worst = simulate(TS, pol, 1_000_000)
        print(f"  sim 1 s {pol:3}: " + "  ".join(f"{TS[i][0]} miss {miss[i]}/{jobs[i]} worstR {worst[i]}"
                                                 for i in range(n)))
    print(f"  hyperperiod = {H} us")
```

```sh
limactl shell elinux -- python3 /Users/donh/.rtos_scratch/C11/py/rta_edf.py
```

```text
U = 0.9167   Liu&Layland bound n(2^(1/n)-1) = 0.7798   hyperbolic Π(Ui+1) = 2.2135 (<=2 이면 RM OK)
  RTA rate  R= 1000 us  D= 4000  OK
  RTA att   R= 3500 us  D= 6000  OK
  RTA nav   R= 9000 us  D= 8000  MISS
  EDF (D=T): U = 0.9167 <= 1  →  schedulable
  sim 1 s FP : rate miss 0/250 worstR 1000  att miss 0/167 worstR 3500  nav miss 42/125 worstR 10000
  sim 1 s EDF: rate miss 0/250 worstR 1000  att miss 0/167 worstR 4000  nav miss 0/125 worstR 6500
  hyperperiod = 24000 us
```

출력에서 볼 것:

- U = 0.9167은 Liu-Layland 상한 0.7798도, hyperbolic 상한(2.2135 > 2)도 넘는다 — 상한 검사로는 "모른다". 상한은 **충분조건**일 뿐이라 넘었다고 실패라는 뜻은 아니다. 그래서 RTA를 돌린다.
- **RTA로 nav를 손으로 따라가면**: R⁰ = 2000 → 2000 + ⌈2000/4000⌉·1000 + ⌈2000/6000⌉·2500 = 5500 → 2000 + 2·1000 + 1·2500 = 6500 → 2000 + 2·1000 + 2·2500 = **9000 > D = 8000** → MISS. 코드는 D를 넘는 순간 멈췄고, 끝까지 반복하면 고정점은 2000 + 3·1000 + 2·2500 = **10000 µs**다. 시뮬레이션의 `worstR 10000`과 정확히 같다.
- 이상 시뮬레이션: FP에서 nav는 1초에 **42번** 놓친다(24 ms hyperperiod마다 한 번 → 1000 / 24 ≈ 41.7). EDF는 **0번**, nav 최악 응답 6500 µs.
- EDF는 U ≤ 1이라 계산 한 줄로 끝난다. 이 간단함이 EDF의 이론적 매력이다.

### 4.3 Zephyr의 EDF — `CONFIG_SCHED_DEADLINE`가 실제로 하는 일

Zephyr v4.4.2 소스를 직접 읽으면 Kconfig 설명부터 조심스럽다(`kernel/Kconfig`).

```text
config SCHED_DEADLINE
	bool "Earliest-deadline-first scheduling"
	help
	  This enables a simple "earliest deadline first" scheduling
	  mode where threads can set "deadline" deltas measured in
	  k_cycle_get_32() units.  Priority decisions within (!!) a
	  single priority will choose the next expiring deadline and
	  not simply the least recently added thread.
```

핵심은 "**within (!!) a single priority**"다. 비교 함수 `z_sched_prio_cmp()`(`kernel/include/priority_q.h`)를 보면 그대로다.

```c
	int32_t b1 = thread_1->base.prio;
	int32_t b2 = thread_2->base.prio;

	if (b1 != b2) {
		return b2 - b1;          /* 정적 우선순위가 다르면 여기서 끝 — deadline은 안 본다 */
	}

#ifdef CONFIG_SCHED_DEADLINE
	uint32_t d1 = thread_1->base.prio_deadline;
	uint32_t d2 = thread_2->base.prio_deadline;

	if (d1 != d2) {
		return (int32_t)(d2 - d1);   /* 같은 우선순위면 deadline이 이른 쪽이 앞 */
	}
#endif /* CONFIG_SCHED_DEADLINE */
	return 0;
```

그리고 deadline을 넣는 API(`kernel/sched.c`)는 이렇다.

```c
void z_impl_k_thread_absolute_deadline_set(k_tid_t tid, int deadline)
{
	struct k_thread *thread = tid;
	K_SPINLOCK(&_sched_spinlock) {
		if (z_is_thread_queued(thread)) {
			dequeue_thread(thread);
			thread->base.prio_deadline = deadline;
			queue_thread(thread);
		} else {
			thread->base.prio_deadline = deadline;
		}
	}
}

void z_impl_k_thread_deadline_set(k_tid_t tid, int deadline)
{
	deadline = clamp(deadline, 0, INT_MAX);
	int32_t newdl = k_cycle_get_32() + deadline;
	z_impl_k_thread_absolute_deadline_set(tid, newdl);
}
```

소스에서 확인한 사실을 정리하면:

| 사실 | 근거 | 실무 의미 |
|---|---|---|
| deadline은 **같은 정적 우선순위 안에서만** 순서를 정한다 | `z_sched_prio_cmp`가 prio부터 비교 | EDF로 돌릴 thread는 **전부 같은 우선순위**에 둔다. 다른 우선순위의 thread는 그대로 FP |
| 단위는 `k_cycle_get_32()` cycle (mps2/an385: 25 MHz) | `k_thread_deadline_set` 문서 · 구현 | ms가 아니다. `k_ticks_to_cyc_floor32()` 등으로 바꿔 넣는다 |
| 상대 버전은 **호출 순간의 now**를 기준으로 한다 | `newdl = k_cycle_get_32() + deadline` | job이 늦게 시작하면 deadline도 밀린다 → 주기 task는 **absolute 버전**으로 release 시각 + D를 넣어야 정확하다 |
| deadline을 바꿔도 **reschedule을 하지 않는다** | 함수 안에 reschedule 호출 없음 | 순서는 다음 스케줄 지점(sleep, ISR 끝, yield)에서 반영된다 |
| 큐 안 deadline 차는 2^31 cycle 미만이어야 한다 | API 문서 note, `(int32_t)(d2 − d1)` 비교 | 25 MHz면 약 86초. 그보다 먼 deadline을 섞지 말 것 |
| admission test · 예산 강제 없음 | API 문서 "makes no guarantees", Linux `sched_setattr`과 비교 문구 | 과부하 보호는 앱 몫 (§4.6) |
| `CONFIG_SCHED_MULTIQ`와 함께 못 쓴다 | `config SCHED_MULTIQ ... depends on !SCHED_DEADLINE` | ready queue는 정렬된 dlist(SIMPLE, 삽입 O(n)) 또는 rbtree(SCALABLE) |
| 새 thread의 deadline 초기값은 0 | `kernel/thread.c`: `new_thread->base.prio_deadline = 0` | 처음부터 맞는 deadline을 넣어 주지 않으면 엉뚱한 순서 |

### 4.4 예제 7 — 같은 task set을 Zephyr에서 FP와 EDF로

무엇을 확인하는 코드인지: §4.2의 세 task를 Zephyr thread로 만들고 1초씩 돌린다. FP는 RM 순서로 우선순위 2 · 3 · 4, EDF는 셋 다 우선순위 3에 두고 매 job 전에 `k_thread_absolute_deadline_set(self, release + T)`을 넣는다. job의 일은 `burn()` — **자기 CPU 시간 C만큼** 돈다(4 µs 넘는 시계 틈 = 선점당한 구간으로 보고 빼고 센다, 그 틈을 간트 구간으로 기록). 한 바이너리가 실험 12개(정책 2 × 부하 6)를 차례로 돌린다.

```c
/* 같은 task set을 FP(RM 우선순위)와 EDF(같은 우선순위 + k_thread_absolute_deadline_set)로 돌려 miss를 센다 */
#include <zephyr/kernel.h>
#define NT 3
static const char *nm[NT] = { "rate", "att", "nav" };
static const uint32_t C_us[NT] = { 1000, 2500, 2000 }, T_us[NT] = { 4000, 6000, 8000 };
static int edf, scale;                                   /* 실험 조건: 정책, C 배율(%) */
static int64_t t0;                                       /* 실험 시작 tick */
static uint32_t c0, jobs[NT], miss[NT], worst[NT];
#define CYC_US 25u                                       /* 25 MHz: 1 us = 25 cycle */
#define RUN_US 1000000u
struct seg { uint8_t id; uint32_t s, e; };
static struct seg tr[96]; static int ntr, trace_on;
static void seg(int id, uint32_t s, uint32_t e)          /* 간트 창 [24 ms, 48 ms) 안만 기록 */
{
    s = (s - c0) / CYC_US; e = (e - c0) / CYC_US;
    if (trace_on && e > 24000 && s < 48000 && ntr < 96) tr[ntr++] = (struct seg){ id, s, e };
}
static void burn(int id, uint32_t cyc)                   /* 자기 CPU 시간 cyc만큼 소비, 선점 구간은 빼고 센다 */
{
    uint32_t last = k_cycle_get_32(), st = last, got = 0;
    while (got < cyc) {
        uint32_t now = k_cycle_get_32(), d = now - last;
        if (d > 100) { seg(id, st, last); st = now; } else got += d;   /* 4 us 넘는 틈 = 선점당함 */
        last = now;
    }
    seg(id, st, last);
}
static void worker(void *a, void *b, void *c)
{
    int id = (int)(intptr_t)a; ARG_UNUSED(b); ARG_UNUSED(c);
    uint32_t Tt = T_us[id] / 100u, cyc = C_us[id] * CYC_US * (uint32_t)scale / 100u;
    for (uint32_t k = 0;; k++) {
        int64_t rel = t0 + (int64_t)k * Tt;               /* k번째 release (tick) */
        if ((rel - t0) * 100 >= RUN_US) return;
        if (edf)                                         /* 다음 job의 절대 deadline = release + T (cycle) */
            k_thread_absolute_deadline_set(k_current_get(), (int)k_ticks_to_cyc_floor32(rel + Tt));
        k_sleep(K_TIMEOUT_ABS_TICKS(rel));               /* release까지 잔다 (이미 지났으면 바로) */
        burn(id, cyc);
        uint32_t resp = (k_cycle_get_32() - (uint32_t)k_ticks_to_cyc_floor32(rel)) / CYC_US;
        jobs[id]++; if (resp > T_us[id]) miss[id]++;
        if (resp > worst[id]) worst[id] = resp;
    }
}
K_THREAD_STACK_ARRAY_DEFINE(stk, NT, 1024);
static struct k_thread th[NT];
static void experiment(int e, int sc, int trace)
{
    edf = e; scale = sc; trace_on = trace; ntr = 0;
    for (int i = 0; i < NT; i++) jobs[i] = miss[i] = worst[i] = 0;
    t0 = k_uptime_ticks() + 10; c0 = k_ticks_to_cyc_floor32(t0);
    for (int i = 0; i < NT; i++)                         /* FP: 2,3,4 (RM 순) / EDF: 모두 3 */
        k_thread_create(&th[i], stk[i], 1024, worker, (void *)(intptr_t)i, NULL, NULL,
                        e ? 3 : 2 + i, 0, K_NO_WAIT);
    k_sleep(K_TIMEOUT_ABS_TICKS(t0 + RUN_US / 100 + 200)); /* 1 s + 20 ms 뒤 정리 */
    for (int i = 0; i < NT; i++) k_thread_abort(&th[i]);
    printk("%s U=%d.%03d", e ? "EDF" : "FP ", 9167 * sc / 1000000, 9167 * sc / 1000 % 1000);
    for (int i = 0; i < NT; i++) printk("  %s miss %u/%u of %u worst %u", nm[i], miss[i], jobs[i], (RUN_US + T_us[i] - 1) / T_us[i], worst[i]);
    printk("\n");
    for (int i = 0; i < ntr; i++) printk("SEG %s %u %u\n", nm[tr[i].id], tr[i].s, tr[i].e);
}
int main(void)
{
    static const int sc[] = { 100, 104, 109, 115, 120, 131 };   /* U ≈ 0.92 ~ 1.20 */
    for (unsigned j = 0; j < ARRAY_SIZE(sc); j++)
        for (int e = 0; e < 2; e++) experiment(e, sc[j], sc[j] == 100 || sc[j] == 131);
    register uint32_t r0 __asm__("r0") = 0x18, r1 __asm__("r1") = 0x20026;   /* semihosting SYS_EXIT */
    __asm volatile("bkpt 0xab" : : "r"(r0), "r"(r1) : "memory");
    return 0;
}
```

```text
# prj.conf
CONFIG_SCHED_DEADLINE=y
CONFIG_SYS_CLOCK_TICKS_PER_SEC=10000
CONFIG_TIMESLICING=n
CONFIG_MAIN_THREAD_PRIORITY=0
CONFIG_PRINTK=y
```

```sh
/var/rtoslab/bin/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/C11/zedf /var/tmp/rtos/C11/zedf_build
# icount로 결정적 실행, 끝나면 semihosting SYS_EXIT로 QEMU 종료
timeout 120 qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -nographic -monitor none -serial stdio \
  -semihosting-config enable=on,target=native -icount shift=5,align=off,sleep=off \
  -kernel /var/tmp/rtos/C11/zedf_build/zephyr/zephyr.elf
```

```text
*** Booting Zephyr OS build v4.4.2 ***
FP  U=0.916  rate miss 0/250 of 250 worst 1025  att miss 0/167 of 167 worst 3548  nav miss 42/125 of 125 worst 10138
EDF U=0.916  rate miss 0/250 of 250 worst 2598  att miss 0/167 of 167 worst 3717  nav miss 0/125 of 125 worst 5571
FP  U=0.953  rate miss 0/250 of 250 worst 1064  att miss 0/167 of 167 worst 3685  nav miss 84/125 of 125 worst 10532
EDF U=0.953  rate miss 0/250 of 250 worst 3193  att miss 0/167 of 167 worst 4346  nav miss 0/125 of 125 worst 5789
FP  U=0.999  rate miss 0/250 of 250 worst 1128  att miss 0/167 of 167 worst 3870  nav miss 125/125 of 125 worst 78140
EDF U=0.999  rate miss 226/250 of 250 worst 19346  att miss 148/167 of 167 worst 20478  nav miss 101/125 of 125 worst 21432
FP  U=1.054  rate miss 0/250 of 250 worst 1173  att miss 0/167 of 167 worst 5242  nav miss 101/101 of 125 worst 259045
EDF U=1.054  rate miss 232/238 of 250 worst 70057  att miss 155/158 of 167 worst 70877  nav miss 114/119 of 125 worst 72474
FP  U=1.100  rate miss 0/250 of 250 worst 1224  att miss 0/167 of 167 worst 5470  nav miss 84/84 of 125 worst 395748
EDF U=1.100  rate miss 226/229 of 250 worst 110428  att miss 150/152 of 167 worst 111197  nav miss 111/114 of 125 worst 110319
FP  U=1.200  rate miss 0/250 of 250 worst 1344  att miss 0/167 of 167 worst 5974  nav miss 50/50 of 125 worst 664775
EDF U=1.200  rate miss 208/209 of 250 worst 183321  att miss 137/139 of 167 worst 185980  nav miss 103/105 of 125 worst 185972
... (간트용 SEG 80줄 생략 — U=0.916 두 실험과 U=1.200 두 실험의 통계 줄 바로 뒤에 찍힌다)
```

이 절에서는 처음 두 줄(U = 0.9167, C 배율 1.00)만 본다. `U=0.916`은 정수 계산에서 잘린 표기이고 실제는 0.9167이다. 나머지 줄은 §4.6에서 본다.

출력에서 볼 것:

- **FP: nav 42/125 miss** — §4.2의 RTA · 이상 시뮬레이션과 **같은 숫자**다. nav 최악 응답 10138 µs = RTA 고정점 10000 µs + 커널 오버헤드 약 138 µs.
- **EDF: miss 0**, nav 최악 5571 µs. 계산(U ≤ 1)이 실제 커널에서도 맞았다.
- 대가: **rate의 최악 응답이 1025 → 2598 µs**로 늘었다. FP에서 rate는 언제나 최고 우선순위라 C + 25 µs에 끝나지만, EDF에서는 deadline이 더 이른(또는 같은) 다른 job이 있으면 기다린다. 아래 간트의 28 ms 지점: rate job(deadline 32 ms)과 nav job(deadline 32 ms)이 **동점**이고, 큐에 먼저 있던 nav가 계속 돈다(`z_sched_prio_cmp`가 0 → 먼저 들어온 쪽). **EDF는 짧은 주기 task의 지터를 키운다** — 제어 루프에는 중요한 단점이다.

```svg
<svg viewBox="0 0 680 286" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="18" font-size="13">U = 0.917 task set, 한 hyperperiod(24 ms) 실측 간트 — QEMU icount, Zephyr v4.4.2</text>
<text x="10" y="36" font-size="12">같은 3개 thread, 바뀐 것은 고르는 규칙뿐. 빨간 선 = nav의 24 ms job deadline</text>
<text x="10" y="56" font-size="13">FP (RM: rate > att > nav) — nav의 24 ms job이 34.1 ms에 끝남 → MISS</text>
<line x1="130.0" y1="64" x2="130.0" y2="130" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<line x1="216.7" y1="64" x2="216.7" y2="130" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<line x1="303.3" y1="64" x2="303.3" y2="130" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<line x1="390.0" y1="64" x2="390.0" y2="130" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<line x1="476.7" y1="64" x2="476.7" y2="130" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<line x1="563.3" y1="64" x2="563.3" y2="130" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<line x1="650.0" y1="64" x2="650.0" y2="130" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<text x="10" y="79" font-size="12">rate C1 T4</text>
<line x1="130" y1="84" x2="650" y2="84" stroke="currentColor" stroke-width="0.4" opacity="0.4"/>
<rect x="130.5" y="67" width="21.7" height="16" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="216.9" y="67" width="21.7" height="16" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="303.6" y="67" width="21.7" height="16" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="390.4" y="67" width="21.7" height="16" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="477.0" y="67" width="21.7" height="16" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="563.6" y="67" width="21.7" height="16" fill="#4a7bd0" fill-opacity="0.85"/>
<text x="10" y="101" font-size="12">att C2.5 T6</text>
<line x1="130" y1="106" x2="650" y2="106" stroke="currentColor" stroke-width="0.4" opacity="0.4"/>
<rect x="152.5" y="89" width="54.2" height="16" fill="#3f9a6b" fill-opacity="0.85"/>
<rect x="260.3" y="89" width="43.0" height="16" fill="#3f9a6b" fill-opacity="0.85"/>
<rect x="325.6" y="89" width="11.2" height="16" fill="#3f9a6b" fill-opacity="0.85"/>
<rect x="412.4" y="89" width="54.2" height="16" fill="#3f9a6b" fill-opacity="0.85"/>
<rect x="520.3" y="89" width="43.0" height="16" fill="#3f9a6b" fill-opacity="0.85"/>
<rect x="585.6" y="89" width="11.2" height="16" fill="#3f9a6b" fill-opacity="0.85"/>
<text x="10" y="123" font-size="12">nav C2 T8</text>
<line x1="130" y1="128" x2="650" y2="128" stroke="currentColor" stroke-width="0.4" opacity="0.4"/>
<rect x="207.0" y="111" width="9.7" height="16" fill="#e08a3c" fill-opacity="0.85"/>
<rect x="238.9" y="111" width="21.1" height="16" fill="#e08a3c" fill-opacity="0.85"/>
<rect x="337.0" y="111" width="12.5" height="16" fill="#e08a3c" fill-opacity="0.85"/>
<rect x="351.3" y="111" width="38.7" height="16" fill="#e08a3c" fill-opacity="0.85"/>
<rect x="466.9" y="111" width="4.6" height="16" fill="#e08a3c" fill-opacity="0.85"/>
<rect x="499.0" y="111" width="21.0" height="16" fill="#e08a3c" fill-opacity="0.85"/>
<rect x="597.0" y="111" width="22.4" height="16" fill="#e08a3c" fill-opacity="0.85"/>
<line x1="303.3" y1="108" x2="303.3" y2="128" stroke="#d0564a" stroke-width="2"/>
<text x="10" y="156" font-size="13">EDF (같은 우선순위 + absolute deadline) — 같은 job이 29.6 ms에 끝남 → OK</text>
<line x1="130.0" y1="164" x2="130.0" y2="230" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<line x1="216.7" y1="164" x2="216.7" y2="230" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<line x1="303.3" y1="164" x2="303.3" y2="230" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<line x1="390.0" y1="164" x2="390.0" y2="230" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<line x1="476.7" y1="164" x2="476.7" y2="230" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<line x1="563.3" y1="164" x2="563.3" y2="230" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<line x1="650.0" y1="164" x2="650.0" y2="230" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<text x="10" y="179" font-size="12">rate C1 T4</text>
<line x1="130" y1="184" x2="650" y2="184" stroke="currentColor" stroke-width="0.4" opacity="0.4"/>
<rect x="130.5" y="167" width="21.7" height="16" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="251.0" y="167" width="9.0" height="16" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="260.2" y="167" width="12.7" height="16" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="328.2" y="167" width="21.7" height="16" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="394.3" y="167" width="21.7" height="16" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="477.0" y="167" width="21.7" height="16" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="597.7" y="167" width="21.7" height="16" fill="#4a7bd0" fill-opacity="0.85"/>
<text x="10" y="201" font-size="12">att C2.5 T6</text>
<line x1="130" y1="206" x2="650" y2="206" stroke="currentColor" stroke-width="0.4" opacity="0.4"/>
<rect x="152.6" y="189" width="54.2" height="16" fill="#3f9a6b" fill-opacity="0.85"/>
<rect x="273.2" y="189" width="30.1" height="16" fill="#3f9a6b" fill-opacity="0.85"/>
<rect x="303.7" y="189" width="24.1" height="16" fill="#3f9a6b" fill-opacity="0.85"/>
<rect x="416.3" y="189" width="54.2" height="16" fill="#3f9a6b" fill-opacity="0.85"/>
<rect x="543.0" y="189" width="20.4" height="16" fill="#3f9a6b" fill-opacity="0.85"/>
<rect x="563.6" y="189" width="33.8" height="16" fill="#3f9a6b" fill-opacity="0.85"/>
<text x="10" y="223" font-size="12">nav C2 T8</text>
<line x1="130" y1="228" x2="650" y2="228" stroke="currentColor" stroke-width="0.4" opacity="0.4"/>
<rect x="207.1" y="211" width="9.6" height="16" fill="#e08a3c" fill-opacity="0.85"/>
<rect x="216.9" y="211" width="33.7" height="16" fill="#e08a3c" fill-opacity="0.85"/>
<rect x="350.2" y="211" width="39.8" height="16" fill="#e08a3c" fill-opacity="0.85"/>
<rect x="390.3" y="211" width="3.6" height="16" fill="#e08a3c" fill-opacity="0.85"/>
<rect x="499.0" y="211" width="21.0" height="16" fill="#e08a3c" fill-opacity="0.85"/>
<rect x="520.3" y="211" width="22.4" height="16" fill="#e08a3c" fill-opacity="0.85"/>
<line x1="303.3" y1="208" x2="303.3" y2="228" stroke="#d0564a" stroke-width="2"/>
<line x1="130" y1="244" x2="650" y2="244" stroke="currentColor"/>
<line x1="130.0" y1="244" x2="130.0" y2="250" stroke="currentColor"/>
<text x="130.0" y="262" font-size="12" text-anchor="middle">24</text>
<line x1="173.3" y1="244" x2="173.3" y2="247" stroke="currentColor"/>
<line x1="216.7" y1="244" x2="216.7" y2="250" stroke="currentColor"/>
<text x="216.7" y="262" font-size="12" text-anchor="middle">28</text>
<line x1="260.0" y1="244" x2="260.0" y2="247" stroke="currentColor"/>
<line x1="303.3" y1="244" x2="303.3" y2="250" stroke="currentColor"/>
<text x="303.3" y="262" font-size="12" text-anchor="middle">32</text>
<line x1="346.7" y1="244" x2="346.7" y2="247" stroke="currentColor"/>
<line x1="390.0" y1="244" x2="390.0" y2="250" stroke="currentColor"/>
<text x="390.0" y="262" font-size="12" text-anchor="middle">36</text>
<line x1="433.3" y1="244" x2="433.3" y2="247" stroke="currentColor"/>
<line x1="476.7" y1="244" x2="476.7" y2="250" stroke="currentColor"/>
<text x="476.7" y="262" font-size="12" text-anchor="middle">40</text>
<line x1="520.0" y1="244" x2="520.0" y2="247" stroke="currentColor"/>
<line x1="563.3" y1="244" x2="563.3" y2="250" stroke="currentColor"/>
<text x="563.3" y="262" font-size="12" text-anchor="middle">44</text>
<line x1="606.7" y1="244" x2="606.7" y2="247" stroke="currentColor"/>
<line x1="650.0" y1="244" x2="650.0" y2="250" stroke="currentColor"/>
<text x="650.0" y="262" font-size="12" text-anchor="middle">48</text>
<text x="650" y="278" font-size="12" text-anchor="end">시각 (ms)</text>
</svg>
```

그림 6 — 실측 간트(24~48 ms, `burn()`이 기록한 실행 구간). 위(FP): nav의 24 ms job은 rate · att에 계속 밀려 34.1 ms에 끝난다(deadline 32 ms, 빨간 선). 아래(EDF): 같은 job이 28 ms에 rate보다 먼저 돌아 29.6 ms에 끝나고, rate가 대신 29.6 ms까지 기다린다(그래도 deadline 32 ms 안).

### 4.5 결과 정리 — 계산 · 시뮬레이션 · 실측

| 항목 | RTA / 계산 | 이상 시뮬레이션 | Zephyr 실측 (icount) |
|---|---|---|---|
| FP nav miss / 1 s | R = 10000 > D = 8000 → 실패 | 42 / 125 | **42 / 125** |
| FP nav 최악 응답 | 10000 µs | 10000 µs | 10138 µs |
| EDF miss (전체) | U = 0.917 ≤ 1 → 성공 | 0 | **0** |
| EDF nav 최악 응답 | — | 6500 µs | 5571 µs |
| rate 최악 응답 FP → EDF | — | 1000 → 1000 µs | 1025 → **2598 µs** |

시뮬레이션의 EDF rate가 1000 µs인 것은 동점을 task 번호로 깼기 때문이고(rate 우선), Zephyr는 큐 도착 순으로 깬다. **동점 처리 규칙 하나가 지터를 2.5배 바꾼다** — 실제 커널을 돌려 봐야 아는 부분이다.

### 4.6 예제 8 — 과부하(U > 1): EDF의 domino 효과 vs FP의 우아한 저하

무엇을 확인하는 코드인지: 예제 7의 같은 바이너리가 C를 1.00 · 1.04 · 1.09 · 1.15 · 1.20 · 1.31배로 키워 U = 0.917 → 1.20을 훑는다(출력 12줄 전체). 늦은 job은 버리지 않고 끝까지 실행한다(가장 흔한 구현). 지표 = (늦게 끝난 job + 1초 안에 못 끝난 job) / release 수.

출력 읽는 법: `nav miss 101/101 of 125`는 "125번 release됐고, 101개가 끝났는데 101개 모두 늦었다(나머지 24개는 끝나지도 못함)"이다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="18" font-size="13">과부하 스윕 실측: task별 deadline 실패 비율 (늦게 끝남 + 1 s 안에 못 끝남) / release</text>
<text x="10" y="36" font-size="12">Zephyr v4.4.2, QEMU icount, 각 점 = 1 s 실행. C를 1.00~1.31배로 키워 U를 올렸다</text>
<text x="60" y="58" font-size="13">FP</text>
<line x1="60" y1="250" x2="310" y2="250" stroke="currentColor"/>
<line x1="60" y1="250" x2="60" y2="70" stroke="currentColor"/>
<line x1="56" y1="250.0" x2="310" y2="250.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/>
<text x="54" y="254.0" font-size="12" text-anchor="end">0%</text>
<line x1="56" y1="160.0" x2="310" y2="160.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/>
<text x="54" y="164.0" font-size="12" text-anchor="end">50%</text>
<line x1="56" y1="70.0" x2="310" y2="70.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/>
<text x="54" y="74.0" font-size="12" text-anchor="end">100%</text>
<text x="75.0" y="266" font-size="12" text-anchor="middle">0.92</text>
<text x="119.0" y="266" font-size="12" text-anchor="middle">0.95</text>
<text x="163.0" y="266" font-size="12" text-anchor="middle">1.00</text>
<text x="207.0" y="266" font-size="12" text-anchor="middle">1.05</text>
<text x="251.0" y="266" font-size="12" text-anchor="middle">1.10</text>
<text x="295.0" y="266" font-size="12" text-anchor="middle">1.20</text>
<polyline points="75.0,250.0 119.0,250.0 163.0,250.0 207.0,250.0 251.0,250.0 295.0,250.0" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<circle cx="75.0" cy="250.0" r="3" fill="#4a7bd0"/>
<circle cx="119.0" cy="250.0" r="3" fill="#4a7bd0"/>
<circle cx="163.0" cy="250.0" r="3" fill="#4a7bd0"/>
<circle cx="207.0" cy="250.0" r="3" fill="#4a7bd0"/>
<circle cx="251.0" cy="250.0" r="3" fill="#4a7bd0"/>
<circle cx="295.0" cy="250.0" r="3" fill="#4a7bd0"/>
<polyline points="75.0,250.0 119.0,250.0 163.0,250.0 207.0,250.0 251.0,250.0 295.0,250.0" fill="none" stroke="#3f9a6b" stroke-width="2" stroke-dasharray="5 3"/>
<circle cx="75.0" cy="250.0" r="3" fill="#3f9a6b"/>
<circle cx="119.0" cy="250.0" r="3" fill="#3f9a6b"/>
<circle cx="163.0" cy="250.0" r="3" fill="#3f9a6b"/>
<circle cx="207.0" cy="250.0" r="3" fill="#3f9a6b"/>
<circle cx="251.0" cy="250.0" r="3" fill="#3f9a6b"/>
<circle cx="295.0" cy="250.0" r="3" fill="#3f9a6b"/>
<polyline points="75.0,189.5 119.0,129.0 163.0,70.0 207.0,70.0 251.0,70.0 295.0,70.0" fill="none" stroke="#e08a3c" stroke-width="2"/>
<circle cx="75.0" cy="189.5" r="3" fill="#e08a3c"/>
<circle cx="119.0" cy="129.0" r="3" fill="#e08a3c"/>
<circle cx="163.0" cy="70.0" r="3" fill="#e08a3c"/>
<circle cx="207.0" cy="70.0" r="3" fill="#e08a3c"/>
<circle cx="251.0" cy="70.0" r="3" fill="#e08a3c"/>
<circle cx="295.0" cy="70.0" r="3" fill="#e08a3c"/>
<text x="380" y="58" font-size="13">EDF</text>
<line x1="380" y1="250" x2="630" y2="250" stroke="currentColor"/>
<line x1="380" y1="250" x2="380" y2="70" stroke="currentColor"/>
<line x1="376" y1="250.0" x2="630" y2="250.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/>
<text x="374" y="254.0" font-size="12" text-anchor="end">0%</text>
<line x1="376" y1="160.0" x2="630" y2="160.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/>
<text x="374" y="164.0" font-size="12" text-anchor="end">50%</text>
<line x1="376" y1="70.0" x2="630" y2="70.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/>
<text x="374" y="74.0" font-size="12" text-anchor="end">100%</text>
<text x="395.0" y="266" font-size="12" text-anchor="middle">0.92</text>
<text x="439.0" y="266" font-size="12" text-anchor="middle">0.95</text>
<text x="483.0" y="266" font-size="12" text-anchor="middle">1.00</text>
<text x="527.0" y="266" font-size="12" text-anchor="middle">1.05</text>
<text x="571.0" y="266" font-size="12" text-anchor="middle">1.10</text>
<text x="615.0" y="266" font-size="12" text-anchor="middle">1.20</text>
<polyline points="395.0,250.0 439.0,250.0 483.0,87.3 527.0,74.3 571.0,72.2 615.0,70.7" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<circle cx="395.0" cy="250.0" r="3" fill="#4a7bd0"/>
<circle cx="439.0" cy="250.0" r="3" fill="#4a7bd0"/>
<circle cx="483.0" cy="87.3" r="3" fill="#4a7bd0"/>
<circle cx="527.0" cy="74.3" r="3" fill="#4a7bd0"/>
<circle cx="571.0" cy="72.2" r="3" fill="#4a7bd0"/>
<circle cx="615.0" cy="70.7" r="3" fill="#4a7bd0"/>
<polyline points="395.0,250.0 439.0,250.0 483.0,90.5 527.0,73.2 571.0,72.2 615.0,72.2" fill="none" stroke="#3f9a6b" stroke-width="2" stroke-dasharray="5 3"/>
<circle cx="395.0" cy="250.0" r="3" fill="#3f9a6b"/>
<circle cx="439.0" cy="250.0" r="3" fill="#3f9a6b"/>
<circle cx="483.0" cy="90.5" r="3" fill="#3f9a6b"/>
<circle cx="527.0" cy="73.2" r="3" fill="#3f9a6b"/>
<circle cx="571.0" cy="72.2" r="3" fill="#3f9a6b"/>
<circle cx="615.0" cy="72.2" r="3" fill="#3f9a6b"/>
<polyline points="395.0,250.0 439.0,250.0 483.0,104.6 527.0,77.2 571.0,74.3 615.0,72.9" fill="none" stroke="#e08a3c" stroke-width="2"/>
<circle cx="395.0" cy="250.0" r="3" fill="#e08a3c"/>
<circle cx="439.0" cy="250.0" r="3" fill="#e08a3c"/>
<circle cx="483.0" cy="104.6" r="3" fill="#e08a3c"/>
<circle cx="527.0" cy="77.2" r="3" fill="#e08a3c"/>
<circle cx="571.0" cy="74.3" r="3" fill="#e08a3c"/>
<circle cx="615.0" cy="72.9" r="3" fill="#e08a3c"/>
<text x="380" y="286" font-size="12" text-anchor="middle">U (CPU 이용률, 커널 오버헤드 제외)</text>
<line x1="60" y1="282" x2="78" y2="282" stroke="#4a7bd0" stroke-width="2"/>
<text x="82" y="286" font-size="12">rate</text>
<line x1="130" y1="282" x2="148" y2="282" stroke="#3f9a6b" stroke-width="2"/>
<text x="152" y="286" font-size="12">att</text>
<line x1="200" y1="282" x2="218" y2="282" stroke="#e08a3c" stroke-width="2"/>
<text x="222" y="286" font-size="12">nav</text>
</svg>
```

그림 7 — 과부하 스윕 실측. FP(왼쪽)는 부하가 늘어도 **가장 낮은 우선순위 nav만** 망가지고 rate · att는 0%다(두 선이 0%에서 겹쳐 있다). EDF(오른쪽)는 U ≈ 1에서 **셋 다 한꺼번에** 80~90% 넘게 실패한다.

왜 이렇게 되나:

- **FP**: 우선순위가 고정이라 부하가 넘치면 "누가 희생될지"가 정해져 있다 — 맨 아래 task. rate와 att의 응답 시간은 nav가 얼마나 밀렸든 상관없다(rate 최악 1344 µs, U = 1.2에서도). 이것을 **우아한 저하(graceful degradation)** 라고 부른다. 중요한 일을 높은 우선순위에 두기만 하면 된다.
- **EDF**: 늦은 job의 deadline은 **이미 지나간 과거 시각**이라 큐에서 가장 이르다. 그 job이 CPU를 잡는 동안 다른 job도 늦어지고, 그 job들의 deadline도 곧 과거가 되어 줄을 선다. 결국 모든 task가 "이미 늦은 job"을 차례로 처리하며 **모두 늦는다** — 도미노처럼 넘어진다(domino effect, Buttazzo 교재의 표현). 그림 8 아래쪽은 rate · att · nav가 돌아가며 수 ms씩 도는 모습이다.
- **U = 0.999에서 이미 무너졌다**: 이론상 U ≤ 1이면 되지만, 실제 커널에는 문맥 전환 · 타이머 ISR · `burn()` 측정 오버헤드가 더해져 **실효 U가 1을 넘었다**. FP에서는 같은 조건에서 nav만 100% 실패. EDF를 쓸 때 "U ≤ 1"이 아니라 **"U + 오버헤드 + 여유 ≤ 1"** 로 설계해야 하는 이유다(오버헤드를 넣은 분석은 N10).
- 응답 시간도 극단적으로 다르다: U = 1.2에서 FP의 rate 최악 1.3 ms vs EDF의 rate 최악 **183 ms**.

```svg
<svg viewBox="0 0 680 286" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="18" font-size="13">과부하(U > 1)에서 두 정책의 실측 간트 — 24~48 ms 구간</text>
<text x="10" y="36" font-size="12">FP: rate·att는 매 주기 제때, nav는 거의 못 돈다 / EDF: 이미 지난 deadline 순으로 돌며 셋 다 늦는다</text>
<text x="10" y="56" font-size="13">FP, U = 1.20 — 가장 낮은 nav만 굶는다</text>
<line x1="130.0" y1="64" x2="130.0" y2="130" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<line x1="216.7" y1="64" x2="216.7" y2="130" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<line x1="303.3" y1="64" x2="303.3" y2="130" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<line x1="390.0" y1="64" x2="390.0" y2="130" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<line x1="476.7" y1="64" x2="476.7" y2="130" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<line x1="563.3" y1="64" x2="563.3" y2="130" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<line x1="650.0" y1="64" x2="650.0" y2="130" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<text x="10" y="79" font-size="12">rate C1 T4</text>
<line x1="130" y1="84" x2="650" y2="84" stroke="currentColor" stroke-width="0.4" opacity="0.4"/>
<rect x="130.4" y="67" width="28.4" height="16" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="216.9" y="67" width="28.4" height="16" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="303.6" y="67" width="28.4" height="16" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="390.4" y="67" width="28.4" height="16" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="476.9" y="67" width="28.4" height="16" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="563.6" y="67" width="28.4" height="16" fill="#4a7bd0" fill-opacity="0.85"/>
<text x="10" y="101" font-size="12">att C2.5 T6</text>
<line x1="130" y1="106" x2="650" y2="106" stroke="currentColor" stroke-width="0.4" opacity="0.4"/>
<rect x="159.1" y="89" width="57.5" height="16" fill="#3f9a6b" fill-opacity="0.85"/>
<rect x="245.7" y="89" width="13.4" height="16" fill="#3f9a6b" fill-opacity="0.85"/>
<rect x="260.5" y="89" width="42.8" height="16" fill="#3f9a6b" fill-opacity="0.85"/>
<rect x="332.3" y="89" width="28.2" height="16" fill="#3f9a6b" fill-opacity="0.85"/>
<rect x="419.1" y="89" width="57.5" height="16" fill="#3f9a6b" fill-opacity="0.85"/>
<rect x="505.7" y="89" width="13.4" height="16" fill="#3f9a6b" fill-opacity="0.85"/>
<rect x="520.5" y="89" width="42.8" height="16" fill="#3f9a6b" fill-opacity="0.85"/>
<rect x="592.3" y="89" width="28.2" height="16" fill="#3f9a6b" fill-opacity="0.85"/>
<text x="10" y="123" font-size="12">nav C2 T8</text>
<line x1="130" y1="128" x2="650" y2="128" stroke="currentColor" stroke-width="0.4" opacity="0.4"/>
<rect x="259.4" y="111" width="0.8" height="16" fill="#e08a3c" fill-opacity="0.85"/>
<rect x="360.8" y="111" width="29.2" height="16" fill="#e08a3c" fill-opacity="0.85"/>
<rect x="519.4" y="111" width="0.8" height="16" fill="#e08a3c" fill-opacity="0.85"/>
<rect x="620.8" y="111" width="24.0" height="16" fill="#e08a3c" fill-opacity="0.85"/>
<rect x="646.2" y="111" width="3.8" height="16" fill="#e08a3c" fill-opacity="0.85"/>
<text x="10" y="156" font-size="13">EDF, U = 1.20 — 모두가 돌아가며 늦는다 (domino)</text>
<line x1="130.0" y1="164" x2="130.0" y2="230" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<line x1="216.7" y1="164" x2="216.7" y2="230" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<line x1="303.3" y1="164" x2="303.3" y2="230" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<line x1="390.0" y1="164" x2="390.0" y2="230" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<line x1="476.7" y1="164" x2="476.7" y2="230" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<line x1="563.3" y1="164" x2="563.3" y2="230" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<line x1="650.0" y1="164" x2="650.0" y2="230" stroke="#888" stroke-width="0.6" stroke-dasharray="2 3"/>
<text x="10" y="179" font-size="12">rate C1 T4</text>
<line x1="130" y1="184" x2="650" y2="184" stroke="currentColor" stroke-width="0.4" opacity="0.4"/>
<rect x="214.3" y="167" width="0.8" height="16" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="215.3" y="167" width="27.6" height="16" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="245.1" y="167" width="28.4" height="16" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="401.5" y="167" width="1.5" height="16" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="403.2" y="167" width="26.9" height="16" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="502.2" y="167" width="0.8" height="16" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="503.2" y="167" width="27.6" height="16" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="588.7" y="167" width="0.8" height="16" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="589.8" y="167" width="27.6" height="16" fill="#4a7bd0" fill-opacity="0.85"/>
<text x="10" y="201" font-size="12">att C2.5 T6</text>
<line x1="130" y1="206" x2="650" y2="206" stroke="currentColor" stroke-width="0.4" opacity="0.4"/>
<rect x="142.7" y="189" width="0.8" height="16" fill="#3f9a6b" fill-opacity="0.85"/>
<rect x="143.7" y="189" width="70.2" height="16" fill="#3f9a6b" fill-opacity="0.85"/>
<rect x="243.3" y="189" width="1.5" height="16" fill="#3f9a6b" fill-opacity="0.85"/>
<rect x="273.9" y="189" width="1.3" height="16" fill="#3f9a6b" fill-opacity="0.85"/>
<rect x="275.4" y="189" width="68.2" height="16" fill="#3f9a6b" fill-opacity="0.85"/>
<rect x="430.6" y="189" width="0.8" height="16" fill="#3f9a6b" fill-opacity="0.85"/>
<rect x="431.6" y="189" width="70.2" height="16" fill="#3f9a6b" fill-opacity="0.85"/>
<rect x="617.8" y="189" width="1.8" height="16" fill="#3f9a6b" fill-opacity="0.85"/>
<rect x="619.9" y="189" width="30.1" height="16" fill="#3f9a6b" fill-opacity="0.85"/>
<text x="10" y="223" font-size="12">nav C2 T8</text>
<line x1="130" y1="228" x2="650" y2="228" stroke="currentColor" stroke-width="0.4" opacity="0.4"/>
<rect x="130.0" y="211" width="12.2" height="16" fill="#e08a3c" fill-opacity="0.85"/>
<rect x="344.0" y="211" width="0.8" height="16" fill="#e08a3c" fill-opacity="0.85"/>
<rect x="345.1" y="211" width="56.0" height="16" fill="#e08a3c" fill-opacity="0.85"/>
<rect x="531.3" y="211" width="1.7" height="16" fill="#e08a3c" fill-opacity="0.85"/>
<rect x="533.2" y="211" width="55.1" height="16" fill="#e08a3c" fill-opacity="0.85"/>
<line x1="130" y1="244" x2="650" y2="244" stroke="currentColor"/>
<line x1="130.0" y1="244" x2="130.0" y2="250" stroke="currentColor"/>
<text x="130.0" y="262" font-size="12" text-anchor="middle">24</text>
<line x1="173.3" y1="244" x2="173.3" y2="247" stroke="currentColor"/>
<line x1="216.7" y1="244" x2="216.7" y2="250" stroke="currentColor"/>
<text x="216.7" y="262" font-size="12" text-anchor="middle">28</text>
<line x1="260.0" y1="244" x2="260.0" y2="247" stroke="currentColor"/>
<line x1="303.3" y1="244" x2="303.3" y2="250" stroke="currentColor"/>
<text x="303.3" y="262" font-size="12" text-anchor="middle">32</text>
<line x1="346.7" y1="244" x2="346.7" y2="247" stroke="currentColor"/>
<line x1="390.0" y1="244" x2="390.0" y2="250" stroke="currentColor"/>
<text x="390.0" y="262" font-size="12" text-anchor="middle">36</text>
<line x1="433.3" y1="244" x2="433.3" y2="247" stroke="currentColor"/>
<line x1="476.7" y1="244" x2="476.7" y2="250" stroke="currentColor"/>
<text x="476.7" y="262" font-size="12" text-anchor="middle">40</text>
<line x1="520.0" y1="244" x2="520.0" y2="247" stroke="currentColor"/>
<line x1="563.3" y1="244" x2="563.3" y2="250" stroke="currentColor"/>
<text x="563.3" y="262" font-size="12" text-anchor="middle">44</text>
<line x1="606.7" y1="244" x2="606.7" y2="247" stroke="currentColor"/>
<line x1="650.0" y1="244" x2="650.0" y2="250" stroke="currentColor"/>
<text x="650.0" y="262" font-size="12" text-anchor="middle">48</text>
<text x="650" y="278" font-size="12" text-anchor="end">시각 (ms)</text>
</svg>
```

그림 8 — U = 1.20에서의 실측 간트. 위(FP): rate · att는 제 주기에 돌고 nav는 거의 CPU를 못 받는다. 아래(EDF): 세 task가 "가장 오래 밀린 job"을 차례로 몇 ms씩 처리한다 — 누구도 제때 끝나지 않는다.

**EDF를 실전에서 쓰려면 과부하 보호가 필수다.** 대표 방법:

| 방법 | 아이디어 | 예 |
|---|---|---|
| 예산 강제(CBS, Constant Bandwidth Server) | task마다 (runtime, period) 예산을 주고 넘으면 deadline을 뒤로 미룬다 → 한 task의 폭주가 다른 task를 못 밀어낸다 | Linux `SCHED_DEADLINE`(EL F1 · N2), RTEMS의 CBS 스케줄러 |
| admission control | 새 task를 받을 때 ΣU ≤ 1(여유 포함)을 검사해 거절 | Linux `sched_setattr`이 bandwidth 검사 |
| 늦은 job 버리기 / skip | deadline을 넘으면 그 job을 버리고 다음 release를 기다린다 | 제어 루프 (§1.9의 skip과 같은 생각) |
| 혼합 | 핵심 task는 높은 정적 우선순위(FP), 나머지만 같은 우선순위에서 EDF | Zephyr에서 자연스럽게 된다 (deadline은 같은 우선순위 안에서만) |

Zephyr의 "deadline은 같은 우선순위 안에서만"이라는 설계는 마지막 줄을 쉽게 만든다. 예를 들어 드론 FC라면 rate 제어는 우선순위 2(FP, EDF 밖), 나머지 비제어 task 여러 개는 우선순위 5에서 EDF로 이용률을 끌어올리는 식이다.

### 4.7 예제 9 — FreeRTOS에서 EDF 흉내 내기 (왜 어색하고 얼마나 드나)

FreeRTOS V11.3.2에는 deadline 개념이 없다(TCB에 deadline 필드가 없다). EDF를 하려면 **job이 release될 때마다 모든 task의 우선순위를 deadline 순으로 다시 매기는** 수밖에 없다. 어색한 지점이 바로 보인다.

- release된 task는 **자기 우선순위를 스스로 올릴 수 없다** — 올리려면 먼저 실행돼야 하는데, 실행되려면 우선순위가 높아야 한다. 그래서 release를 대신 처리하고 우선순위를 바꿔 줄 **최고 우선순위 관리 task**가 필요하다(문맥 전환이 release마다 추가).
- `vTaskPrioritySet()`은 ISR에서 못 부른다(FromISR 버전이 없다) → tick hook 안에서 할 수 없다.
- task 수만큼 **서로 다른 우선순위 단계**가 필요하다(`configMAX_PRIORITIES` ≥ N + 관리자).
- 재정렬 비용이 N에 비례(이 구현은 순위 계산 O(N²) + `vTaskPrioritySet` N번).

무엇을 확인하는 코드인지: 관리 task(우선순위 15)가 1 ms마다 깨어 release를 확인하고, release가 있으면 task notification으로 worker를 깨우고(`xTaskNotifyGive`), EDF 모드면 deadline 순위대로 `vTaskPrioritySet`을 부른다. 같은 세 task(+ 정렬 대상 수를 늘리는 더미 task)를 1초 돌리고 관리 task의 실행 시간을 FPGAIO 25 MHz 카운터로 잰다.

```c
/* FreeRTOS에서 EDF 흉내: 최고 우선순위 관리 task가 release마다 deadline 순으로 vTaskPrioritySet */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#ifndef NDUMMY
#define NDUMMY 0                                    /* 정렬 대상 수를 늘리는 더미 task (주기 1 s) */
#endif
#define NT (3 + NDUMMY)
#define CNT() REG32(0x40028018u)                    /* FPGAIO COUNTER 25 MHz */
static uint32_t C_us[NT] = { 1000, 2500, 2000 }, T_ms[NT] = { 4, 6, 8 };
static TaskHandle_t h[NT];
static volatile TickType_t rel[NT][4], dl[NT]; static volatile uint32_t head[NT], tail[NT];
static uint32_t jobs[NT], miss[NT], worst[NT], mgr_n, mgr_max, mgr_sum;
static void burn(uint32_t us)                       /* 자기 CPU 시간 us만큼 (선점 구간은 빼고) */
{
    uint32_t last = CNT(), got = 0, need = us * 25u;
    while (got < need) { uint32_t now = CNT(), d = now - last; if (d < 125) got += d; last = now; }
}
static void worker(void *p)
{
    int i = (int)(intptr_t)p;
    for (;;) {
        ulTaskNotifyTake(pdFALSE, portMAX_DELAY);   /* 관리 task가 release를 알려 준다 */
        TickType_t r = rel[i][tail[i]++ & 3];
        burn(C_us[i]);
        uint32_t resp = (xTaskGetTickCount() - r);  /* ms 단위 응답 시간 (tick 해상도) */
        jobs[i]++; if (r + T_ms[i] < xTaskGetTickCount()) miss[i]++;
        if (resp > worst[i]) worst[i] = resp;
    }
}
static void manager(void *p)
{
    (void)p; TickType_t last = xTaskGetTickCount(), start = last;
    TickType_t next[NT]; for (int i = 0; i < NT; i++) next[i] = start;
    while (last - start < 1000) {
        uint32_t s = CNT(); int any = 0;
        for (int i = 0; i < NT; i++)
            if (last >= next[i]) { rel[i][head[i]++ & 3] = next[i]; dl[i] = next[i] + T_ms[i];
                                   next[i] += T_ms[i]; xTaskNotifyGive(h[i]); any = 1; }
        if (any && EDF) {                           /* deadline 순위 = 우선순위 (빠를수록 높게) */
            for (int i = 0; i < NT; i++) {
                UBaseType_t rank = 0;
                for (int j = 0; j < NT; j++) rank += dl[j] < dl[i] || (dl[j] == dl[i] && j < i);
                vTaskPrioritySet(h[i], (UBaseType_t)(NT - rank));
            }
        }
        if (any) { uint32_t c = (CNT() - s) / 25u; mgr_n++; mgr_sum += c; if (c > mgr_max) mgr_max = c; }
        vTaskDelayUntil(&last, 1);                  /* 1 tick마다 release 확인 */
    }
    vTaskSuspendAll();
    board_printf_raw("%s NT=%d  mgr activations=%u avg=%u us max=%u us (CPU %u.%u%%)\n", EDF ? "EDF-hack" : "FP",
                     NT, mgr_n, mgr_sum / mgr_n, mgr_max, mgr_sum / 10000, mgr_sum / 1000 % 10);
    for (int i = 0; i < 3; i++)
        board_printf_raw("  task%d C=%u T=%u  miss %u/%u  worst %u ms\n", i, C_us[i], T_ms[i], miss[i], jobs[i], worst[i]);
    board_exit(0);
}
int main(void)
{
    for (int i = 3; i < NT; i++) { C_us[i] = 10; T_ms[i] = 1000; }
    for (int i = 0; i < NT; i++)                    /* FP 초기값 = RM 순위 (주기 짧을수록 높게) */
        xTaskCreate(worker, "w", 256, (void *)(intptr_t)i, (UBaseType_t)(NT - i), &h[i]);
    xTaskCreate(manager, "mgr", 512, NULL, configMAX_PRIORITIES - 1, NULL);
    vTaskStartScheduler();
    return 0;
}
```

```sh
for v in "0 0" "1 0" "1 3" "1 9"; do set -- $v
  make -B -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=$S/fedf.c OUT=$A/fedf_$1_$2 \
       EXTRA_CFLAGS="-DEDF=$1 -DNDUMMY=$2 -DconfigMAX_PRIORITIES=16"
  ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 $A/fedf_$1_$2/app.elf 30 | grep -v board_exit
done
```

```text
FP NT=3  mgr activations=333 avg=6 us max=11 us (CPU 0.2%)
  task0 C=1000 T=4  miss 0/250  worst 1 ms
  task1 C=2500 T=6  miss 0/167  worst 3 ms
  task2 C=2000 T=8  miss 42/125  worst 10 ms
EDF-hack NT=3  mgr activations=333 avg=18 us max=21 us (CPU 0.6%)
  task0 C=1000 T=4  miss 0/250  worst 1 ms
  task1 C=2500 T=6  miss 0/167  worst 4 ms
  task2 C=2000 T=8  miss 0/125  worst 6 ms
EDF-hack NT=6  mgr activations=333 avg=33 us max=40 us (CPU 1.1%)
  task0 C=1000 T=4  miss 0/250  worst 1 ms
  task1 C=2500 T=6  miss 0/166  worst 4 ms
  task2 C=2000 T=8  miss 0/125  worst 6 ms
EDF-hack NT=12  mgr activations=333 avg=87 us max=105 us (CPU 2.9%)
  task0 C=1000 T=4  miss 0/250  worst 1 ms
  task1 C=2500 T=6  miss 0/166  worst 4 ms
  task2 C=2000 T=8  miss 0/125  worst 7 ms
```

출력에서 볼 것:

- **기능은 된다**: FP(RM 순위)는 nav(task2) 42/125 miss — Zephyr 실측 · 계산과 같은 숫자. EDF 흉내는 **miss 0**. (miss 판정과 응답 시간은 tick = 1 ms 해상도다.)
- **비용**: release 1회당 관리 task 시간이 FP 6 µs(알림만) → EDF 흉내 N = 3에서 18 µs, N = 6에서 33 µs, N = 12에서 **87 µs(최대 105 µs, CPU 2.9%)**. 25 MHz M3에서 task 12개면 매 ms 0.1 ms를 "순서 바꾸기"에 쓴다. 이 비용은 그대로 task들의 실효 U에 더해진다 — §4.6에서 본 것처럼 EDF는 그 여유가 가장 아쉬운 방식이다.
- 비교: Zephyr는 deadline이 바뀐 thread **하나만** 큐에서 빼서 정렬 위치에 다시 넣는다(O(n) 삽입 한 번). 커널이 deadline을 알고 있느냐가 이 차이를 만든다.

```svg
<svg viewBox="0 0 680 236" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="18" font-size="13">FreeRTOS EDF 흉내의 비용: release 확인 1회당 관리 task 실행 시간 (실측)</text>
<text x="10" y="36" font-size="12">파랑 = 평균, 주황 = 최대. FP는 release 알림만, EDF 흉내는 + 순위 계산 + vTaskPrioritySet N번</text>
<text x="10" y="71" font-size="12">FP (N = 3)</text>
<rect x="230" y="56" width="14.3" height="11" fill="#4a7bd0" fill-opacity="0.8"/>
<text x="248.3" y="66" font-size="12">6 us</text>
<rect x="230" y="68" width="26.2" height="11" fill="#e08a3c" fill-opacity="0.8"/>
<text x="260.2" y="78" font-size="12">11 us</text>
<text x="10" y="105" font-size="12">EDF 흉내 (N = 3)</text>
<rect x="230" y="90" width="42.9" height="11" fill="#4a7bd0" fill-opacity="0.8"/>
<text x="276.9" y="100" font-size="12">18 us</text>
<rect x="230" y="102" width="50.1" height="11" fill="#e08a3c" fill-opacity="0.8"/>
<text x="284.1" y="112" font-size="12">21 us</text>
<text x="10" y="139" font-size="12">EDF 흉내 (N = 6)</text>
<rect x="230" y="124" width="78.7" height="11" fill="#4a7bd0" fill-opacity="0.8"/>
<text x="312.7" y="134" font-size="12">33 us</text>
<rect x="230" y="136" width="95.4" height="11" fill="#e08a3c" fill-opacity="0.8"/>
<text x="329.4" y="146" font-size="12">40 us</text>
<text x="10" y="173" font-size="12">EDF 흉내 (N = 12)</text>
<rect x="230" y="158" width="207.5" height="11" fill="#4a7bd0" fill-opacity="0.8"/>
<text x="441.5" y="168" font-size="12">87 us (CPU 2.9%)</text>
<rect x="230" y="170" width="250.4" height="11" fill="#e08a3c" fill-opacity="0.8"/>
<text x="484.4" y="180" font-size="12">105 us</text>
<line x1="230" y1="50" x2="230" y2="192" stroke="currentColor"/>
<text x="610" y="222" font-size="12" text-anchor="end">us / 1회</text>
</svg>
```

그림 9 — FreeRTOS EDF 흉내의 실측 비용(release 확인 1회당). N에 대해 대략 선형보다 빠르게 는다(순위 계산이 O(N²)이기 때문, 정렬로 바꾸면 O(N log N) + `vTaskPrioritySet` N번).

정리: FreeRTOS에서 EDF가 꼭 필요하면 (1) 관리 task 방식으로 몇 개 task에만 쓰거나, (2) 애초에 Zephyr · RTEMS처럼 deadline을 아는 커널을 고른다. 대부분의 MCU 제품은 **RM/DM 우선순위 + RTA**로 충분하고, 그게 업계가 FP를 쓰는 이유다(§6).

---
## 5. ARINC 653 파티션 스케줄링 — 시간 울타리 치기

### 5.1 직관 — 회의실 예약표

회사 회의실 하나를 팀 둘이 쓴다. 예약표에 "A팀 9:00~9:30, B팀 9:30~10:00"이 고정돼 있다. 각 팀은 자기 시간 안에서 안건 순서를 마음대로 정한다. A팀 회의가 길어지면? 9:30에 **B팀이 들어오고 A팀은 나가야 한다**. A팀이 아무리 망가져도 B팀의 시간은 지켜진다. 이것이 **시간 분할(temporal partitioning)** 이다.

항공 IMA(Integrated Modular Avionics) 컴퓨터는 이 방식으로 비행 관리, 디스플레이, 정비 기록처럼 **중요도(DAL)가 다른 소프트웨어**를 한 CPU에 올린다. 규격이 ARINC Specification 653(『Avionics Application Software Standard Interface』, 흔히 APEX API라 부른다)이다.

| 용어 | 뜻 |
|---|---|
| partition | 독립된 응용 하나. 자기 메모리 영역(공간 분할)과 자기 시간 창(시간 분할)을 가진다 |
| major time frame (MAF) | 파티션 시간표 한 장의 길이. 끝없이 반복된다 (이 노트: 20 ms) |
| partition window | MAF 안에서 한 파티션에 준 구간 (offset, 길이). 한 파티션이 여러 창을 가질 수 있다 |
| 2단계 스케줄링 | 1단계 = 파티션 사이: 고정 창 표(cyclic executive와 같은 구조) / 2단계 = 파티션 안: process 우선순위 기반 선점 |
| process | ARINC 653에서 파티션 안의 실행 단위 (RTOS의 task와 같다) |
| health monitor (HM) | 오류(deadline miss, 예외 등)를 감지해 process · 파티션 · 모듈 수준에서 대응(재시작 등) |
| sampling / queuing port | 파티션 사이 통신 채널. 파티션끼리 메모리를 직접 공유하지 않는다 |

실제 제품: Wind River VxWorks 653, SYSGO PikeOS, Lynx LynxOS-178, Green Hills INTEGRITY-178, DDC-I Deos 같은 상용 인증 RTOS와 우주 분야에서 쓰이는 하이퍼바이저 XtratuM 등이 ARINC 653 API를 제공한다(지형도는 A6 · I4, 인증은 O2). 이 노트는 **시간 분할의 스케줄링 부분만** 직접 구현한다. 공간 분할(MPU/MMU)은 F4 · F5, 하이퍼바이저 기반 분할은 O6, 혼합 중요도 이론은 N9.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" font-size="13">ARINC 653식 2단계 스케줄링 — 이 노트 예제 11의 구성</text>
  <text x="10" y="44" font-size="12">1단계: 파티션 스케줄러 (major time frame 20 ms, 설계 때 고정된 창 표를 반복)</text>
  <rect x="40" y="54" width="180" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <rect x="220" y="54" width="120" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <rect x="340" y="54" width="180" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <rect x="520" y="54" width="120" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="130" y="74" font-size="12" text-anchor="middle">P0 창 0~6 ms</text>
  <text x="280" y="74" font-size="12" text-anchor="middle">P1 6~10</text>
  <text x="430" y="74" font-size="12" text-anchor="middle">P0 10~16</text>
  <text x="580" y="74" font-size="12" text-anchor="middle">P1 16~20</text>
  <line x1="130" y1="86" x2="150" y2="124" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="580" y1="86" x2="520" y2="124" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="340" y="114" font-size="12" text-anchor="middle">2단계: 창 안에서는 파티션 자기 FP</text>
  <rect x="40" y="128" width="280" height="96" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="54" y="148" font-size="13">P0 비행 제어 (높은 중요도)</text>
  <text x="54" y="172" font-size="12">rate: T 10 ms, C 2 ms, 지역 prio 높음</text>
  <text x="54" y="192" font-size="12">att: T 20 ms, C 3 ms, 지역 prio 낮음</text>
  <text x="54" y="212" font-size="12">창 밖에서는 우선순위 1로 강등</text>
  <rect x="360" y="128" width="280" height="96" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="374" y="148" font-size="13">P1 통신 · 로그 (낮은 중요도)</text>
  <text x="374" y="172" font-size="12">comms: T 10 ms, C 1 ms (100 ms에 폭주)</text>
  <text x="374" y="192" font-size="12">log: T 20 ms, C 2 ms</text>
  <text x="374" y="212" font-size="12">창 밖에서는 우선순위 1로 강등</text>
  <rect x="40" y="240" width="600" height="46" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="54" y="260" font-size="12">시간 분할만 구현했다. 진짜 ARINC 653은 공간 분할(MPU/MMU, F4 · F5 · O6),</text>
  <text x="54" y="278" font-size="12">파티션 간 통신 포트, health monitor(오류 시 파티션 재시작)까지 묶는다.</text>
</svg>
```

그림 10 — 이 노트 예제 11의 구성. 파티션 스케줄러가 MAF 20 ms 동안 창 네 개를 돌리고, 창 안에서는 그 파티션의 task만 자기 우선순위대로 돈다.

### 5.2 FreeRTOS 위에 어떻게 얹나 — 우선순위 띠(band)

FreeRTOS는 FP 하나뿐이니, 2단계를 **우선순위 띠**로 만든다.

| 우선순위 | 누구 | 역할 |
|---|---|---|
| 15 | 파티션 관리자 `pm` | 창 경계마다 깨어 창 전환 (1단계 스케줄러) |
| 4 ~ 6 | **활성** 파티션의 task (3 + 지역 우선순위) | 2단계: 파티션 안 FP |
| 2 | `spare` (wfi 루프) | 창 안에서 활성 파티션이 할 일이 없을 때 CPU를 쥐고 잔다 |
| 1 | **비활성** 파티션의 task 전부 | ready여도 spare(2)에 막혀 절대 못 돈다 |
| 0 | FreeRTOS idle | spare가 항상 ready라 사실상 안 돈다 |

창이 바뀌면 `pm`이 나가는 파티션 task를 우선순위 1로 내리고 들어오는 파티션 task를 4~6으로 올린다. **ready/blocked 상태는 건드리지 않는다** — 주기 대기(`vTaskDelayUntil`) 중인 task는 계속 자기 시각을 기다린다. spare가 없으면 활성 파티션이 놀 때 비활성 파티션 task(우선순위 1)가 idle(0)보다 높아서 남의 창에서 돌아 버린다.

"왜 `vTaskSuspend`/`vTaskResume`으로 파티션을 끄고 켜지 않나?" — 그러면 delay가 사라진다. 직접 확인한다.

### 5.3 예제 10 — `vTaskSuspend`는 delay를 지운다

무엇을 확인하는 코드인지: `vTaskDelay(10)` 중인 task를 2 ms에 suspend, 4 ms에 resume하면 언제 깨어나는지 본다.

```c
/* vTaskSuspend → vTaskResume 하면 vTaskDelay 중이던 task의 남은 delay가 사라지는가? */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
static TaskHandle_t hw;
static void sleeper(void *p)
{
    (void)p;
    board_printf_raw("[%u ms] sleeper: vTaskDelay(10) 시작\n", (unsigned)xTaskGetTickCount());
    vTaskDelay(10);                                   /* 10 ms 뒤에 깨야 정상 */
    board_printf_raw("[%u ms] sleeper: 깨어남 (기대: 10 ms)\n", (unsigned)xTaskGetTickCount());
    for (;;) vTaskDelay(1000);
}
static void ctl(void *p)
{
    (void)p;
    vTaskDelay(2);  vTaskSuspend(hw); board_printf_raw("[%u ms] ctl: vTaskSuspend(sleeper), state=%d\n", (unsigned)xTaskGetTickCount(), eTaskGetState(hw));
    vTaskDelay(2);  vTaskResume(hw);  board_printf_raw("[%u ms] ctl: vTaskResume(sleeper)\n", (unsigned)xTaskGetTickCount());
    vTaskDelay(20); board_exit(0);
}
int main(void)
{
    xTaskCreate(sleeper, "s", 256, NULL, 2, &hw);
    xTaskCreate(ctl, "c", 256, NULL, 3, NULL);
    vTaskStartScheduler();
    return 0;
}
```

```sh
make -B -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=$S/susp.c OUT=$A/susp
ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 $A/susp/app.elf
```

```text
[0 ms] sleeper: vTaskDelay(10) 시작
[2 ms] ctl: vTaskSuspend(sleeper), state=3
[4 ms] ctl: vTaskResume(sleeper)
[4 ms] sleeper: 깨어남 (기대: 10 ms)
[board_exit] code=0
```

출력에서 볼 것: 10 ms에 깨어야 할 task가 **resume 즉시(4 ms)** 깨어났다(`state=3` = `eSuspended`). V11.3.2 `tasks.c`의 `vTaskSuspend()`는 task의 `xStateListItem`을 delayed list에서 빼서(`uxListRemove`) `xSuspendedTaskList`에 넣고, `vTaskResume()`은 그것을 ready list로 옮긴다 — **남은 timeout을 기억하는 곳이 없다**. 파티션을 suspend로 끄면 주기 task들이 창이 열리자마자 한꺼번에 일찍 깨어나 주기가 깨진다. 그래서 우선순위 띠 방식을 쓴다.

### 5.4 예제 11 — 2파티션 시간 창 스케줄러와 폭주 파티션

무엇을 확인하는 코드인지: P0(비행 제어: rate 10 ms / 2 ms, att 20 ms / 3 ms)와 P1(통신 · 로그: comms 10 ms / 1 ms, log 20 ms / 2 ms)을 MAF 20 ms(P0 0~6, P1 6~10, P0 10~16, P1 16~20)로 돌린다. **100 ms에 P1의 comms가 무한 루프에 빠진다**(버그). 같은 코드를 `PARTITIONED=0`으로 빌드하면 파티션 없이 전역 FP가 되는데, comms는 "통신 deadline이 짧다"는 이유로 최고 우선순위(6)를 받았다고 가정한다. 문맥 전환 시각은 `traceTASK_SWITCHED_IN()` 훅으로 기록한다.

```c
/* FreeRTOSConfig.h (앱 폴더) */
/* C11 a653: 랩 기본 설정 + 문맥 전환 trace 훅 + 우선순위 16단계 */
#ifndef C11_CFG_H
#define C11_CFG_H
#define configMAX_PRIORITIES 16
#include "/var/rtoslab/templates/freertos-mps2/FreeRTOSConfig.h"
void c11_trace_in(void *tcb);
#define traceTASK_SWITCHED_IN() c11_trace_in((void *)pxCurrentTCB)
#endif
```

```c
/* ARINC 653식 2단계 스케줄링: 1단계 = 고정 시간 창(major frame 20 ms), 2단계 = 파티션 안 FP */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#ifndef PARTITIONED
#define PARTITIONED 1                               /* 0 = 파티션 없이 전역 FP */
#endif
#define CNT() REG32(0x40028018u)
typedef struct { const char *nm; int part, lprio, fprio; uint32_t C_us, T_ms; } tdef_t;
static const tdef_t td[] = {                        /* P0 = 비행 제어, P1 = 통신·로그 */
    { "rate", 0, 2, 5, 2000, 10 }, { "att", 0, 1, 4, 3000, 20 },     /* (이름, 파티션, 지역 prio, FLAT prio, C, T) */
    { "comms", 1, 3, 6, 1000, 10 }, { "log", 1, 1, 3, 2000, 20 } };
#define NT 4
static const struct { int part; TickType_t len; } win[] = { {0, 6}, {1, 4}, {0, 6}, {1, 4} };   /* MAF 20 ms */
static TaskHandle_t h[NT], hspare, hpm;
static uint32_t jobs[NT], miss[NT], t0c, nsw, sw_max;
static struct { uint32_t t; void *tcb; } ev[400]; static volatile uint32_t nev;
void c11_trace_in(void *tcb)                        /* 문맥 전환 시각 기록 (80~140 ms 창) */
{ uint32_t t = (CNT() - t0c) / 25u; if (t0c && t >= 80000 && t < 140000 && nev < 400) ev[nev++] = (typeof(ev[0])){ t, tcb }; }
static void burn(uint32_t us)
{ uint32_t last = CNT(), got = 0; while (got < us * 25u) { uint32_t n = CNT(), d = n - last; if (d < 125) got += d; last = n; } }
static void worker(void *p)
{
    int i = (int)(intptr_t)p; TickType_t last = 0;
    for (;;) {
        TickType_t r = last;
        if (i == 2 && r >= 100) for (;;) { }        /* 버그: 100 ms부터 comms가 무한 루프 */
        burn(td[i].C_us);
        jobs[i]++; if (xTaskGetTickCount() > r + td[i].T_ms) miss[i]++;
        vTaskDelayUntil(&last, td[i].T_ms);
    }
}
static void spare(void *p) { (void)p; for (;;) __asm volatile("wfi"); }   /* 창 안에서 남는 시간 = 빈 시간 */
static void pm(void *p)                             /* 1단계: 파티션 관리자 (최고 우선순위) */
{
    (void)p; TickType_t last = xTaskGetTickCount();
    for (unsigned k = 0; last < 200; k++) {
        int a = win[k % 4].part; uint32_t s = CNT();
        for (int i = 0; PARTITIONED && i < NT; i++) /* 활성 파티션 = 3 + 지역 우선순위, 나머지 = 1 (spare 2 아래) */
            vTaskPrioritySet(h[i], td[i].part == a ? (UBaseType_t)(3 + td[i].lprio) : 1);
        uint32_t c = (CNT() - s) / 25u; nsw++; if (c > sw_max) sw_max = c;
        vTaskDelayUntil(&last, win[k % 4].len);
    }
    vTaskSuspendAll();
    board_printf_raw("%s  window switches=%u max cost=%u us\n", PARTITIONED ? "PARTITIONED" : "FLAT FP", nsw, sw_max);
    for (int i = 0; i < NT; i++) board_printf_raw("  P%d %s released %u done %u late %u\n", td[i].part, td[i].nm, 200 / td[i].T_ms, jobs[i], miss[i]);
    for (uint32_t e = 0; e + 1 < nev; e++) {        /* 전환 기록 → 구간 */
        const char *n = ev[e].tcb == hspare ? "spare" : ev[e].tcb == hpm ? "pm" : "idle";
        for (int i = 0; i < NT; i++) if (ev[e].tcb == h[i]) n = td[i].nm;
        board_printf_raw("SEG %s %u %u\n", n, ev[e].t, ev[e + 1].t);
    }
    board_exit(0);
}
int main(void)
{
    for (int i = 0; i < NT; i++)                    /* FLAT: comms가 "통신 deadline이 짧다"며 최고 우선순위 */
        xTaskCreate(worker, td[i].nm, 256, (void *)(intptr_t)i, PARTITIONED ? 1 : (UBaseType_t)td[i].fprio, &h[i]);
    xTaskCreate(spare, "spare", 128, NULL, 2, &hspare);
    xTaskCreate(pm, "pm", 512, NULL, configMAX_PRIORITIES - 1, &hpm);
    t0c = CNT();
    vTaskStartScheduler();
    return 0;
}
```

```sh
for p in 1 0; do
  make -B -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=$S/a653.c OUT=$A/a653_$p EXTRA_CFLAGS="-DPARTITIONED=$p"
  ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 $A/a653_$p/app.elf 30
done
```

```text
PARTITIONED  window switches=40 max cost=16 us
  P0 rate released 20 done 20 late 0
  P0 att released 10 done 10 late 0
  P1 comms released 20 done 10 late 0
  P1 log released 10 done 5 late 0
... (간트용 SEG 35줄 생략)
[board_exit] code=0
FLAT FP  window switches=40 max cost=0 us
  P0 rate released 20 done 10 late 0
  P0 att released 10 done 5 late 0
  P1 comms released 20 done 10 late 0
  P1 log released 10 done 5 late 0
... (간트용 SEG 29줄 생략)
[board_exit] code=0
```

출력에서 볼 것:

- **파티션 있음**: P0의 rate 20/20, att 10/10, 늦음 0 — comms가 폭주한 100 ms 이후에도 **P0는 아무 영향이 없다**. P1은 comms가 10번 하고 멈췄고(무한 루프), 같은 파티션의 log도 5번에서 멈췄다. **격리는 파티션 사이에서만 된다** — 파티션 안의 task끼리는 여전히 FP라서 comms가 log를 굶긴다(그걸 잡는 것이 health monitor의 일).
- **파티션 없음(전역 FP)**: comms가 최고 우선순위로 무한 루프 → 100 ms 이후 **rate · att가 한 번도 돌지 못했다**(10/20, 5/10). 드론이라면 통신 버그 하나로 자세 제어가 멈춘 것이다.
- 창 전환 비용 최대 **16 µs**(task 4개에 `vTaskPrioritySet` 4번). MAF당 4번이니 20 ms에 약 64 µs = 0.3%.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="18" font-size="13">100 ms에 P1의 comms가 무한 루프에 빠진다 (빨간 선) — FreeRTOS 실측 간트</text>
<text x="10" y="36" font-size="12">위: P0(비행 제어)는 자기 창에서 계속 제때 돈다 / 아래: rate·att가 100 ms 이후 한 번도 못 돈다</text>
<text x="10" y="56" font-size="13">파티션 스케줄 (배경 파랑 = P0 창, 주황 = P1 창)</text>
<rect x="110.0" y="64" width="55.0" height="88" fill="#4a7bd0" fill-opacity="0.10"/>
<rect x="165.0" y="64" width="36.7" height="88" fill="#e08a3c" fill-opacity="0.10"/>
<rect x="201.7" y="64" width="55.0" height="88" fill="#4a7bd0" fill-opacity="0.10"/>
<rect x="256.7" y="64" width="36.7" height="88" fill="#e08a3c" fill-opacity="0.10"/>
<rect x="293.3" y="64" width="55.0" height="88" fill="#4a7bd0" fill-opacity="0.10"/>
<rect x="348.3" y="64" width="36.7" height="88" fill="#e08a3c" fill-opacity="0.10"/>
<rect x="385.0" y="64" width="55.0" height="88" fill="#4a7bd0" fill-opacity="0.10"/>
<rect x="440.0" y="64" width="36.7" height="88" fill="#e08a3c" fill-opacity="0.10"/>
<rect x="476.7" y="64" width="55.0" height="88" fill="#4a7bd0" fill-opacity="0.10"/>
<rect x="531.7" y="64" width="36.7" height="88" fill="#e08a3c" fill-opacity="0.10"/>
<rect x="568.3" y="64" width="55.0" height="88" fill="#4a7bd0" fill-opacity="0.10"/>
<rect x="623.3" y="64" width="36.7" height="88" fill="#e08a3c" fill-opacity="0.10"/>
<text x="10" y="79" font-size="12">P0 rate</text>
<line x1="110" y1="84" x2="660" y2="84" stroke="currentColor" stroke-width="0.4" opacity="0.4"/>
<rect x="110.9" y="67" width="18.4" height="16" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="202.6" y="67" width="18.4" height="16" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="294.3" y="67" width="18.4" height="16" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="385.9" y="67" width="18.4" height="16" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="477.6" y="67" width="18.4" height="16" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="569.2" y="67" width="18.4" height="16" fill="#4a7bd0" fill-opacity="0.85"/>
<text x="10" y="101" font-size="12">P0 att</text>
<line x1="110" y1="106" x2="660" y2="106" stroke="currentColor" stroke-width="0.4" opacity="0.4"/>
<rect x="129.4" y="89" width="27.6" height="16" fill="#3f9a6b" fill-opacity="0.85"/>
<rect x="312.7" y="89" width="27.6" height="16" fill="#3f9a6b" fill-opacity="0.85"/>
<rect x="496.0" y="89" width="27.6" height="16" fill="#3f9a6b" fill-opacity="0.85"/>
<text x="10" y="123" font-size="12">P1 comms</text>
<line x1="110" y1="128" x2="660" y2="128" stroke="currentColor" stroke-width="0.4" opacity="0.4"/>
<rect x="165.9" y="111" width="9.3" height="16" fill="#e08a3c" fill-opacity="0.85"/>
<rect x="257.5" y="111" width="9.3" height="16" fill="#e08a3c" fill-opacity="0.85"/>
<rect x="349.2" y="111" width="36.5" height="16" fill="#e08a3c" fill-opacity="0.85"/>
<rect x="440.9" y="111" width="36.5" height="16" fill="#e08a3c" fill-opacity="0.85"/>
<rect x="532.5" y="111" width="36.5" height="16" fill="#e08a3c" fill-opacity="0.85"/>
<text x="10" y="145" font-size="12">P1 log</text>
<line x1="110" y1="150" x2="660" y2="150" stroke="currentColor" stroke-width="0.4" opacity="0.4"/>
<rect x="175.1" y="133" width="18.4" height="16" fill="#888" fill-opacity="0.85"/>
<line x1="293.3" y1="108" x2="293.3" y2="128" stroke="#d0564a" stroke-width="2"/>
<text x="10" y="178" font-size="13">파티션 없는 전역 FP (comms가 최고 우선순위)</text>
<text x="10" y="201" font-size="12">P0 rate</text>
<line x1="110" y1="206" x2="660" y2="206" stroke="currentColor" stroke-width="0.4" opacity="0.4"/>
<rect x="120.1" y="189" width="18.4" height="16" fill="#4a7bd0" fill-opacity="0.85"/>
<rect x="211.7" y="189" width="18.4" height="16" fill="#4a7bd0" fill-opacity="0.85"/>
<text x="10" y="223" font-size="12">P0 att</text>
<line x1="110" y1="228" x2="660" y2="228" stroke="currentColor" stroke-width="0.4" opacity="0.4"/>
<rect x="138.5" y="211" width="27.1" height="16" fill="#3f9a6b" fill-opacity="0.85"/>
<rect x="165.7" y="211" width="0.8" height="16" fill="#3f9a6b" fill-opacity="0.85"/>
<text x="10" y="245" font-size="12">P1 comms</text>
<line x1="110" y1="250" x2="660" y2="250" stroke="currentColor" stroke-width="0.4" opacity="0.4"/>
<rect x="110.8" y="233" width="9.3" height="16" fill="#e08a3c" fill-opacity="0.85"/>
<rect x="202.4" y="233" width="9.3" height="16" fill="#e08a3c" fill-opacity="0.85"/>
<rect x="294.1" y="233" width="54.8" height="16" fill="#e08a3c" fill-opacity="0.85"/>
<rect x="349.1" y="233" width="36.6" height="16" fill="#e08a3c" fill-opacity="0.85"/>
<rect x="385.7" y="233" width="54.9" height="16" fill="#e08a3c" fill-opacity="0.85"/>
<rect x="440.7" y="233" width="36.6" height="16" fill="#e08a3c" fill-opacity="0.85"/>
<rect x="477.4" y="233" width="54.9" height="16" fill="#e08a3c" fill-opacity="0.85"/>
<rect x="532.4" y="233" width="36.6" height="16" fill="#e08a3c" fill-opacity="0.85"/>
<rect x="569.1" y="233" width="54.9" height="16" fill="#e08a3c" fill-opacity="0.85"/>
<text x="10" y="267" font-size="12">P1 log</text>
<line x1="110" y1="272" x2="660" y2="272" stroke="currentColor" stroke-width="0.4" opacity="0.4"/>
<rect x="166.3" y="255" width="18.4" height="16" fill="#888" fill-opacity="0.85"/>
<line x1="293.3" y1="230" x2="293.3" y2="250" stroke="#d0564a" stroke-width="2"/>
<line x1="110" y1="288" x2="660" y2="288" stroke="currentColor"/>
<line x1="110.0" y1="288" x2="110.0" y2="294" stroke="currentColor"/>
<text x="110.0" y="306" font-size="12" text-anchor="middle">80</text>
<line x1="201.7" y1="288" x2="201.7" y2="294" stroke="currentColor"/>
<text x="201.7" y="306" font-size="12" text-anchor="middle">90</text>
<line x1="293.3" y1="288" x2="293.3" y2="294" stroke="currentColor"/>
<text x="293.3" y="306" font-size="12" text-anchor="middle">100</text>
<line x1="385.0" y1="288" x2="385.0" y2="294" stroke="currentColor"/>
<text x="385.0" y="306" font-size="12" text-anchor="middle">110</text>
<line x1="476.7" y1="288" x2="476.7" y2="294" stroke="currentColor"/>
<text x="476.7" y="306" font-size="12" text-anchor="middle">120</text>
<line x1="568.3" y1="288" x2="568.3" y2="294" stroke="currentColor"/>
<text x="568.3" y="306" font-size="12" text-anchor="middle">130</text>
<line x1="660.0" y1="288" x2="660.0" y2="294" stroke="currentColor"/>
<text x="660.0" y="306" font-size="12" text-anchor="middle">140</text>
<text x="660" y="322" font-size="12" text-anchor="end">시각 (ms)</text>
</svg>
```

그림 11 — 실측 간트(80~140 ms, 문맥 전환 기록에서 그림). 위: 배경 파랑 = P0 창, 주황 = P1 창. 100 ms 이후 comms(주황 막대)가 P1 창을 꽉 채우지만 P0 창의 rate · att는 제자리에서 돈다. 아래: 같은 버그가 전역 FP에서는 CPU 전체를 먹는다. 기록은 140 ms 직전의 마지막 문맥 전환까지라 끝 구간 일부가 비어 보인다.

### 5.5 시간 분할의 대가와 설계 규칙

- **창 위상이 지연을 만든다**: comms는 10 ms마다 release되지만(80, 90, … ms) P1 창은 6 ms 뒤에 열린다. 간트에서 comms는 항상 release + 약 6.1 ms에 시작한다(86.094 ms). 파티션 안 task의 응답 시간 = (창이 열릴 때까지 기다림) + (창 안에서의 RTA). 그래서 **창 배치는 그 파티션의 가장 급한 task의 release 직후에 오도록** 정한다 — 파티션 수준의 cyclic executive 설계다(§1).
- **창 안의 일은 창 크기에 맞춰야 한다**: 창 밖에서는 아무리 급해도 못 돈다. 파티션 안 스케줄 가능성은 "공급이 일정하지 않은 CPU"에 대한 분석(periodic resource model, Shin & Lee 2003 등)으로 한다 — N9 · O6.
- **파티션 사이 뮤텍스 금지**: P1 task가 뮤텍스를 쥔 채 우선순위 1로 강등되면 P0 task는 **P1의 다음 창까지** 막힌다(우선순위 상속도 창 밖에서는 소용없다). ARINC 653이 파티션 간 공유 메모리 대신 port(메시지)만 허용하는 이유다.
- **빈 시간(spare)도 설계 대상이다**: 이 예제는 창을 꽉 채웠지만, 실제 MAF에는 확장용 빈 창을 남긴다.
- 이 구현은 **커널이 협조해 주는 시간 분할**이다. 폭주 task가 `taskDISABLE_INTERRUPTS()`로 인터럽트를 끄거나 `vTaskPrioritySet`을 스스로 부르면 울타리를 넘는다. 진짜 분할은 그런 권한을 MPU/특권 모드로 막는다(F5, FreeRTOS-MPU · Zephyr userspace).

---

## 6. 네 방식 비교 — 무엇을 언제 고르나

### 6.1 비교표

| 항목 | FP (RM/DM) | EDF | cyclic executive · TTC | 파티션 (ARINC 653식) |
|---|---|---|---|---|
| 결정 시점 | 실행 중, task 우선순위로 | 실행 중, job deadline으로 | 설계 때 (표) | 1단계 설계 때 · 2단계 실행 중(FP) |
| 결정성 · 지터 (이 노트 실측) | 높은 우선순위는 좋다: rate 응답 1025 µs (C 1000) | 짧은 주기 task 지터 증가: rate 최악 2598 µs | 최고: ctrl 시작 늦음 1 µs (CE) · 3 µs (TTC) | 창 위상만큼 지연 (comms +6 ms), 창 안은 FP |
| 이용률 상한 | 충분조건 69~78% (넘으면 RTA로 확인), 조화 주기면 100% | 100% (D = T, 오버헤드 0일 때) | 표가 존재하면 100% 가까이, slicing 필요할 수 있음 | 파티션별 창 비율로 제한 |
| 과부하 행동 (실측) | 우아한 저하: 맨 아래 task만 실패 | domino: U ≈ 1에서 전 task 80~99% 실패 | 오버런 = 감지 후 정책(catch-up · skip) | 폭주가 자기 파티션 안에 갇힘 |
| 비주기 이벤트 | 우선순위 하나 주면 끝 | deadline 하나 주면 끝 | 폴링 slot (지연 = 폴링 주기) | 자기 창이 올 때까지 기다림 |
| 바꾸기 비용 | task 추가 = 우선순위 정하고 RTA | U 합만 다시 확인 | 표 전체 재생성 · 재검증 | 창 표 재설계 + 파티션별 분석 |
| 인증 친화성 | 높음 (RTA, 업계 표준) | 중간 (분석 쉬움, 과부하 증명 어려움, 상용 인증 사례 적음) | 매우 높음 (항공 레거시) | 매우 높음 (IMA, DO-178C 환경의 표준 구조) |
| 구현 비용 | RTOS 기본 | Zephyr · RTEMS · Linux만 기본, FreeRTOS는 흉내 (N = 12에서 CPU 2.9%) | 커널 불필요, 코드 수백 바이트 | 인증 RTOS · 하이퍼바이저, 여기 흉내는 16 µs/전환 |
| 대표 사용처 | 거의 모든 MCU RTOS 제품 | 멀티미디어, Linux `SCHED_DEADLINE`, 연구 | 작은 안전 MCU, 엔진 · 브레이크 제어, 버스 시간표 | 항공 IMA, 국방 임무 컴퓨터 |

### 6.2 고르는 법 (한 문단씩)

- **대부분의 드론 · 로봇 MCU**: FP + RM/DM 우선순위 + RTA(N3). 이론 이용률이 조금 낮아도, 과부하 때 누가 희생될지 정해져 있다는 성질이 제품에 더 중요하다. Buttazzo의 논문 『Rate Monotonic vs. EDF: Judgment Day』(Real-Time Systems, 2005)는 EDF가 이용률 · 문맥 전환 수에서 오히려 유리한 경우가 많다고 반박하지만, 산업은 여전히 FP가 다수다 — 커널 지원, 도구, 인증 사례 때문이다.
- **연산이 작고 주기가 고정이며 인증이 무거운 장치**(엔진 제어, 작은 안전 MCU): cyclic executive · TTC. 커널이 없으니 인증 범위도 작다.
- **CPU를 100% 가까이 써야 하고 부하가 소프트한** 경우: EDF + 예산 강제(CBS). MCU에서는 Zephyr에서 "중요 task는 높은 고정 우선순위, 나머지만 EDF"가 현실적이다.
- **중요도가 다른 소프트웨어를 한 CPU에**: 시간 · 공간 분할(ARINC 653 RTOS, 하이퍼바이저). 드론에서는 비행 제어와 임무 · AI 페이로드를 아예 다른 칩(MCU vs SoC)으로 나누는 것이 가장 흔한 "공간 분할"이다(A7).

### 6.3 예제 12 — 크기로 보는 구현 비용

무엇을 확인하는 코드인지: 이 노트에서 만든 바이너리들의 크기와, 스케줄러 핵심 함수의 크기를 본다.

```sh
cd /var/tmp/rtos/C11
arm-none-eabi-size ce_base/app.elf ttc_10/app.elf fedf_1_0/app.elf a653_1/app.elf zedf_build/zephyr/zephyr.elf
arm-none-eabi-nm --size-sort -S zedf_build/zephyr/zephyr.elf | grep -E "deadline|ready_thread|unready_thread|z_swap_irqlock"
arm-none-eabi-nm --size-sort -S ce_base/app.elf | grep -E " (main|TIMER0_IRQHandler|ce_table)$"
arm-none-eabi-nm --size-sort -S fedf_1_0/app.elf | grep -E " (vTaskPrioritySet|manager)$"
```

```text
   text	   data	    bss	    dec	    hex	filename
   5056	    100	    388	   5544	   15a8	ce_base/app.elf
   4976	    100	    488	   5564	   15bc	ttc_10/app.elf
  13324	    100	 135704	 149128	  24688	fedf_1_0/app.elf
  13068	    100	 138832	 152000	  251c0	a653_1/app.elf
  16376	    164	  10630	  27170	   6a22	zedf_build/zephyr/zephyr.elf
--- Zephyr: deadline · ready queue 관련 심볼
000033a8 0000001e T z_ready_thread
00001950 00000034 t z_swap_irqlock
00001a08 0000004c t unready_thread
00001984 00000084 t ready_thread
00001b54 0000008c T z_impl_k_thread_absolute_deadline_set
--- cyclic executive / FreeRTOS EDF 흉내
000000c0 00000040 T TIMER0_IRQHandler
00001308 000000a0 t ce_table
00000100 000001f8 T main
00000cc0 0000010c T vTaskPrioritySet
000000c0 000001f8 t manager
```

출력에서 볼 것:

- cyclic executive(`ce_base`)와 Pont TTC(`ttc_10`)는 text **약 5 KB** — 그나마 대부분이 시작 코드 · UART 포맷터다. 스케줄러 본체는 `main` 504 B + `TIMER0_IRQHandler` 64 B + 시간표 `ce_table` 160 B(`.rodata`)다.
- FreeRTOS 둘은 text 약 13 KB, bss 약 135 KB(대부분 heap_4의 128 KB 힙). Zephyr EDF 앱은 text 16 KB, RAM 10.6 KB.
- Zephyr에서 EDF를 위해 더해진 코드는 `z_impl_k_thread_absolute_deadline_set` 140 B와 비교 함수 안의 몇 줄(인라인)뿐이다. FreeRTOS 흉내의 관리 task는 504 B + `vTaskPrioritySet` 268 B를 **매 release마다** 부른다. 코드 크기보다 **실행 비용**이 차이다.

---

## 7. RTOS 커널 안에서는 무슨 일이 일어나나

```svg
<svg viewBox="0 0 680 316" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" font-size="13">Zephyr v4.4.2: deadline이 ready queue 순서에 들어가는 경로</text>
  <rect x="10" y="32" width="300" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="20" y="50" font-size="12">k_thread_deadline_set(tid, Δcycles)</text>
  <text x="20" y="66" font-size="12">→ now = k_cycle_get_32(), d = now + Δ</text>
  <rect x="10" y="92" width="300" height="56" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="20" y="110" font-size="12">z_impl_k_thread_absolute_deadline_set</text>
  <text x="20" y="126" font-size="12">queued면 dequeue → prio_deadline = d</text>
  <text x="20" y="142" font-size="12">→ queue_thread (재정렬). reschedule 없음</text>
  <rect x="10" y="168" width="300" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="20" y="186" font-size="12">z_priq_simple_add: dlist를 앞에서부터</text>
  <text x="20" y="202" font-size="12">훑어 cmp &gt; 0인 자리 앞에 끼움 (O(n))</text>
  <rect x="10" y="228" width="300" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="20" y="246" font-size="12">next_up() → _priq_run_best = 리스트 머리</text>
  <text x="20" y="262" font-size="12">다음 스케줄 지점(ISR 끝, sleep, yield)에서</text>
  <line x1="160" y1="72" x2="160" y2="90" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="160" y1="148" x2="160" y2="166" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="160" y1="208" x2="160" y2="226" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="312" y1="188" x2="348" y2="120" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="350" y="32" width="320" height="236" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="362" y="52" font-size="13">z_sched_prio_cmp(t1, t2)</text>
  <text x="362" y="78" font-size="12">① base.prio가 다르면 → 정적 우선순위로 끝</text>
  <text x="362" y="96" font-size="12">(deadline은 보지도 않는다)</text>
  <text x="362" y="122" font-size="12">② 같으면 (int32_t)(d2 − d1)</text>
  <text x="362" y="140" font-size="12">→ deadline이 이른 쪽이 앞</text>
  <text x="362" y="158" font-size="12">(32비트 wrap: 큐 안 deadline 차 &lt; 2^31)</text>
  <text x="362" y="184" font-size="12">③ deadline도 같으면 0 → 먼저 들어온 쪽</text>
  <text x="362" y="210" font-size="12">kernel/include/priority_q.h</text>
  <text x="362" y="228" font-size="12">kernel/sched.c (deadline_set)</text>
  <text x="362" y="246" font-size="12">CONFIG_SCHED_MULTIQ와 함께 못 쓴다</text>
  <text x="10" y="294" font-size="12">EDF로 쓰려면: EDF thread를 전부 같은 정적 우선순위에 두고, job마다 절대 deadline을 다시 넣는다.</text>
  <text x="10" y="310" font-size="12">admission test, 예산 강제(CBS), 과부하 처리는 커널이 하지 않는다 — 앱 몫.</text>
</svg>
```

그림 12 — Zephyr v4.4.2에서 deadline이 스케줄링 결정에 들어가는 경로. 정적 우선순위가 먼저, deadline은 동점일 때만.

**Zephyr** (`zephyr/kernel/`):

- `struct _thread_base`에 `int prio_deadline`이 `CONFIG_SCHED_DEADLINE`일 때만 생긴다(`include/zephyr/kernel/thread.h`). 새 thread는 `kernel/thread.c`에서 0으로 초기화.
- `k_thread_deadline_set()` → `z_impl_k_thread_absolute_deadline_set()`(`kernel/sched.c`): `_sched_spinlock` 안에서 큐에 있으면 `dequeue_thread()` → 값 변경 → `queue_thread()`. 정렬된 자료구조(rbtree)를 깨지 않으려고 빼고 다시 넣는다는 주석이 달려 있다.
- ready queue 백엔드(`kernel/include/priority_q.h`): `CONFIG_SCHED_SIMPLE`이면 `z_priq_simple_add()`가 dlist를 앞에서부터 훑어 `z_sched_prio_cmp() > 0`인 자리 앞에 끼운다(O(n)). `CONFIG_SCHED_SCALABLE`이면 rbtree. `SCHED_MULTIQ`(우선순위별 리스트 배열 + 비트맵, C1)는 deadline과 함께 못 쓴다 — 리스트 하나 안에서 deadline 정렬을 할 수 없는 구조라서.
- 다음 thread 선택: `next_up()`(`kernel/sched.c`) → `_priq_run_best()` = 리스트 머리. 선점 여부는 `should_preempt()`(`kernel/include/kthread.h`)가 정한다. 협력형(음수 우선순위) thread는 deadline이 아무리 급해도 선점되지 않는다(C4).

**FreeRTOS** (`FreeRTOS-Kernel/tasks.c`, V11.3.2):

- deadline 필드가 없다. ready list는 우선순위별 `pxReadyTasksLists[]`와 `uxTopReadyPriority` 비트맵(ARM_CM3 포트는 CLZ로 최고 우선순위 찾기, `taskSELECT_HIGHEST_PRIORITY_TASK()`, C1).
- `vTaskPrioritySet()`(2844행 부근): 뮤텍스 상속 중이면 `uxBasePriority`만 바꾸고, 아니면 `uxPriority`도 바꾼 뒤 ready 상태면 **리스트 사이를 옮기고** 필요하면 yield한다. EDF 흉내와 파티션 흉내가 모두 이 함수 하나에 기대고 있다 — 그래서 비용이 "task 수 × 이 함수".
- `vTaskSuspend()`(3197행 부근)는 상태 리스트에서 빼서 `xSuspendedTaskList`로 옮긴다 — delayed list의 깨울 시각이 사라지는 이유(예제 10).
- `traceTASK_SWITCHED_IN()`는 `vTaskSwitchContext()` 안에서 새 `pxCurrentTCB`가 정해진 직후 불린다. 예제 11의 간트가 이 훅에서 나왔다(Tracealyzer · SystemView 같은 도구도 이 trace 매크로들을 쓴다, P 모듈).
- `xTaskDelayUntil()`(2414행 부근)이 주기 release의 기본 도구다. 시각은 tick 단위라 release 정밀도 = tick(C5).

**bare-metal(cyclic · TTC)**: 커널 자료구조가 없다. "ready queue" = 시간표(`ce_table`) 또는 `RunMe` 카운터, "스케줄 결정" = 배열 인덱스, "문맥 전환" = 함수 호출과 return. 결정이 설계 때 끝났으니 실행 때 할 일이 이것뿐이다.

더 깊은 내용: ready list와 비트맵은 C1, 정책별 선점 규칙은 C4, tick과 timeout은 C5, 소스 읽기 순서는 C12.

### 7.1 예제 13 — 실행 기록에서 그림 만들기 (charts.py 발췌)

무엇을 확인하는 코드인지: 이 노트의 간트 · 측정 차트는 모두 예제 출력(SEG 줄, 통계 줄)을 Python으로 읽어 좌표를 계산한 것이다. 전체 203줄 중 Zephyr 출력을 실험별로 나누고 실패 비율을 계산하는 부분만 발췌한다.

```python
def segs(lines):
    return [(m.group(1), int(m.group(2)), int(m.group(3))) for m in (re.match(r"SEG (\S+) (\d+) (\d+)", l) for l in lines) if m]
# ---- Zephyr FP vs EDF: 실험별로 SEG 나누기 ----
exps, cur = {}, None
for l in open(f"{A}/zedf.out"):
    m = re.match(r"(FP |EDF) U=(\S+)\s+(.*)", l)
    if m:
        cur = (m.group(1).strip(), m.group(2)); exps[cur] = {"stat": m.group(3), "seg": []}; continue
    if cur and l.startswith("SEG"): exps[cur]["seg"] += segs([l])
# ---- 측정 차트: 과부하 스윕 — deadline 실패 비율 = (늦게 끝남 + 못 끝남) / release ----
def ratio(stat):
    out = {}
    for n, m, d, r in re.findall(r"(\w+) miss (\d+)/(\d+) of (\d+)", stat):
        out[n] = (int(m) + int(r) - int(d)) / int(r)
    return out
```

```sh
limactl shell elinux -- python3 /Users/donh/.rtos_scratch/C11/py/charts.py
```

```text
wrote g_ce.svg H= 206
wrote g_fp_edf.svg H= 286
wrote g_overload.svg H= 286
wrote m_overload.svg
('FP', '0.916') {'rate': 0.0, 'att': 0.0, 'nav': 0.336}
('EDF', '0.916') {'rate': 0.0, 'att': 0.0, 'nav': 0.0}
('FP', '0.953') {'rate': 0.0, 'att': 0.0, 'nav': 0.672}
('EDF', '0.953') {'rate': 0.0, 'att': 0.0, 'nav': 0.0}
('FP', '0.999') {'rate': 0.0, 'att': 0.0, 'nav': 1.0}
('EDF', '0.999') {'rate': 0.904, 'att': 0.886, 'nav': 0.808}
('FP', '1.054') {'rate': 0.0, 'att': 0.0, 'nav': 1.0}
('EDF', '1.054') {'rate': 0.976, 'att': 0.982, 'nav': 0.96}
('FP', '1.100') {'rate': 0.0, 'att': 0.0, 'nav': 1.0}
('EDF', '1.100') {'rate': 0.988, 'att': 0.988, 'nav': 0.976}
('FP', '1.200') {'rate': 0.0, 'att': 0.0, 'nav': 1.0}
('EDF', '1.200') {'rate': 0.996, 'att': 0.988, 'nav': 0.984}
wrote g_a653.svg H= 330
wrote m_edfhack.svg
wrote m_ttc.svg
wrote p_table.svg
```

출력에서 볼 것: 실패 비율 표가 그림 7의 점들이다. EDF는 U = 0.999에서 rate 0.904 · att 0.886 · nav 0.808로 한 번에 무너진다.

---

## 8. 실무에서 다시 보기

### 8.1 드론 비행 제어기

- 오픈소스 FC 펌웨어에도 이 노트의 스케줄러들이 다 있다. ArduPilot의 `AP_Scheduler`는 task 표의 한 줄에 함수, 주기(Hz), **최대 실행 시간 예산(µs)** 을 적는 협력형 스케줄러다 — 구조가 §2의 Pont TTC + 예산과 같다(고속 IMU · rate 루프는 별도 스레드/인터럽트로 돈다). PX4는 NuttX 위의 FP 스레드와 work queue를 쓴다.
- 설계 패턴: **IMU data-ready 인터럽트를 frame 경계로 삼는 time-triggered 루프**. 8 kHz 자이로 → 4 kHz rate 루프 → 1 kHz 자세 → 50 Hz 위치처럼 주기를 조화(2의 거듭제곱 배)로 맞추면 hyperperiod가 짧고 표가 단순하다. 조화 주기면 RM으로도 이용률 100%까지 된다(N2) — EDF가 필요 없어지는 가장 흔한 이유다.
- 임무 컴퓨터(Linux SoC)와 FC(MCU)를 나누는 것 자체가 공간 분할이다. 한 SoC 안에서 둘을 섞어야 하면 하이퍼바이저나 ARINC 653식 RTOS를 쓴다(국방 드론 · 미사일 임무 컴퓨터에서 VxWorks 653 · PikeOS 같은 제품이 쓰인다).

### 8.2 로봇 관절 모터 컨트롤러

- FOC 전류 루프(20 kHz)는 대개 **PWM 타이머의 업데이트 인터럽트**에서 돈다. ADC 샘플 시점도 PWM 중앙에 하드웨어로 묶여 있다. 이것은 하드웨어가 frame 경계를 만드는 cyclic executive다 — 전류 루프 1회 = frame, 속도 루프는 4~10 frame마다 한 번(slot).
- CAN의 SYNC 메시지(CANopen)로 여러 관절의 frame 경계를 맞추면 분산 time-triggered 시스템이 된다(§3).
- 실수 패턴: 전류 루프 ISR 안에서 가끔 긴 일(파라미터 저장, 로그)을 하면 **frame 오버런** — 예제 4처럼 ISR 시작 때 "이전 루프가 아직 실행 중?" 깃발을 보는 것이 가장 싼 감지기다.

### 8.3 차량 ECU (Rivian 같은 회사)

- AUTOSAR OS의 **schedule table**: "0 ms에 Task_A 활성, 5 ms에 Task_B 활성, 10 ms마다 반복"을 정적으로 적고, 전역 시간(FlexRay · TSN 시계)에 동기할 수 있다. task 자체는 OSEK 고정 우선순위로 돈다 — §1의 표 + §4의 FP를 섞은 것.
- 실리콘 검증 · bring-up 펌웨어에서도 "정해진 순서로 정해진 시각에 레지스터를 찌르고 읽는" 시험 시퀀서는 사실상 cyclic executive다. 측정 지터가 결과를 망치지 않게 하려면 이 구조가 가장 단순하다.

### 8.4 Don의 SSD 펌웨어

- SSD executive가 "host 처리 → 매핑 갱신 → GC 한 조각 → 로그"를 고정 예산으로 돌렸다면 그것이 TTC + job slicing이다. GC를 "한 번에 블록 하나만" 처리하게 쪼갠 이유가 §1.5 그대로다 — 긴 일이 frame(= host 응답 시간 예산)을 넘지 않게.
- host 명령 큐를 "timeout이 가장 가까운 명령부터" 처리하는 정책은 EDF다. 그리고 SSD가 과부하(쓰기 폭주)일 때 모든 명령의 지연이 함께 늘어나는 현상(QoS 붕괴)은 §4.6의 domino와 같은 모양이다. 엔터프라이즈 SSD가 "읽기 QoS를 지키려고 GC · 쓰기에 예산(throttling)을 거는 것"은 CBS와 같은 생각이다.

---

## 9. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| cyclic 표의 C를 평균 실행 시간으로 잡음 | 가끔 frame 오버런, 다음 frame 지터 | 최악이 평균보다 길다 | C = 측정 WCET + 여유, frame마다 ISR 몫 비우기 (예제 2 `MARGIN`) |
| frame 크기를 f ≥ max C만 보고 고름 | 어떤 task가 표상으로는 들어가는데 deadline을 넘음 | 조건 (3) 2f − gcd(T, f) ≤ D 무시 | 세 조건을 모두 검사하는 도구 (예제 1) |
| 오버런 감지만 있고 정책이 없음 | 늦음이 몇 frame씩 번짐 | catch-up이 회복할 여유 frame이 없다 | skip · abort · 모드 전환 중 하나를 정해 둔다 (§1.9) |
| Pont TTC에서 같은 주기 task를 같은 offset에 둠 | 10 ms마다 tick 오버런, 짧은 주기 task 지연 수백 µs | 한 tick에 부하가 몰림 | offset(첫 `Delay`)을 어긋나게 (예제 5: 409 → 3 µs) |
| Zephyr에서 EDF thread들의 우선순위가 서로 다름 | `k_thread_deadline_set`을 불러도 효과 없음 | deadline은 같은 정적 우선순위 안에서만 비교 | EDF 대상 thread를 전부 같은 우선순위에 |
| 주기 task에 상대 `k_thread_deadline_set` 사용 | deadline이 job 시작 시각 기준으로 밀림 | `now + Δ`가 호출 시각 기준 | release 시각으로 `k_thread_absolute_deadline_set` |
| EDF를 U ≤ 1만 보고 설계 | U ≈ 1에서 모든 task가 한꺼번에 놓침 | 커널 오버헤드로 실효 U > 1, domino | 오버헤드 포함 여유, 예산 강제(CBS), 핵심 task는 FP로 분리 |
| FreeRTOS에서 `vTaskSuspend`로 파티션 끄기 | 창이 열리자마자 주기 task가 일찍 깨어남 | suspend가 delayed list에서 빼면서 깨울 시각을 지운다 | 우선순위 띠로 강등 (예제 10 · 11) |
| 파티션 사이에 뮤텍스 공유 | 높은 중요도 파티션이 다른 파티션 창까지 막힘 | 강등된 소유자가 창 밖에서 못 돈다 | 파티션 간에는 메시지(port)만 |
| "중요한 task = 높은 우선순위"로 통신 task를 최고에 | 통신 버그 하나로 제어가 멈춤 (예제 11 FLAT) | 우선순위는 중요도가 아니라 deadline 순서 | DM 순서 배치 + 중요도는 격리(파티션 · 별도 코어)로 |

---

## 10. 면접에서 이렇게 말한다

**Q.** What is a cyclic executive, and how do you choose the minor frame size?

**A.** 설계 때 hyperperiod 한 주기 분량의 시간표를 만들고, 타이머 인터럽트가 frame 경계마다 그 칸의 함수들을 순서대로 끝까지 실행하는 구조다. frame 크기는 세 조건: 가장 긴 job보다 크거나 같고, 적어도 한 주기를 나누고, 모든 task에 대해 2f − gcd(T, f) ≤ D. 셋이 충돌하면 긴 job을 자른다.

> A cyclic executive runs a precomputed table: a timer interrupt marks each minor frame, and the executive runs that frame's functions to completion in a fixed order, then sleeps. The major cycle is the hyperperiod. The frame size f must satisfy three constraints from Baker and Shaw: f is at least the longest job, f divides at least one period so it divides the hyperperiod, and for every task 2f minus gcd of the period and f is at most the deadline, so a full frame always fits between release and deadline. When those conflict, typically a long job and a short deadline, you slice the long job. In my QEMU experiment the frame-start jitter was about one microsecond, because there's nothing to decide at runtime.

**꼬리 질문**: "What happens on a frame overrun?" → 감지는 ISR에서 busy 깃발 하나. 정책은 catch-up(일은 안 잃지만 늦음이 번짐)과 skip(시간은 지키지만 job을 잃음) — 제어 루프면 보통 skip.

**Q.** EDF is optimal. Why do most RTOS products still use fixed priorities?

**A.** 정상 부하에서는 EDF가 이용률 100%까지 되지만, 과부하에서 누가 희생될지 정할 수 없다. 늦은 job의 deadline은 과거라서 가장 급해 보이고, 결국 모두가 늦는다. FP는 맨 아래 task만 굶는다. 그리고 커널 지원 · 분석 도구 · 인증 사례가 FP 쪽에 압도적이다.

> Optimality only holds when utilization is at most one and the model is exact. Under overload EDF degrades badly: a late job's deadline is already in the past, so it looks most urgent and pushes everyone else late too, the domino effect. I measured it on Zephyr: at nominal utilization 0.999, kernel overhead pushed it effectively over one, and all three tasks missed 80 to 90 percent of their deadlines, while fixed priority only lost the lowest-priority task. Fixed priority also gives you predictable jitter for the highest-rate loop, which matters for control, and it's what the tools and certification evidence support. If I need EDF, I'd pair it with budget enforcement like a constant bandwidth server.

**꼬리 질문**: "Give me a task set RM can't schedule but EDF can." → (1, 4), (2.5, 6), (2, 8): U = 0.917, nav의 RTA 고정점 10 ms > 8 ms.

**Q.** How does Zephyr's deadline scheduling work exactly?

**A.** `CONFIG_SCHED_DEADLINE`를 켜면 thread에 `prio_deadline`이 생기고, 비교 함수는 정적 우선순위를 먼저 보고 같을 때만 deadline을 본다. 단위는 cycle이고, 상대 API는 호출 순간 기준이다. admission control이나 예산 강제는 없다.

> It's EDF within a priority level. The comparison function checks the static priority first and only breaks ties by deadline, so all EDF threads must share one priority; threads at other priorities still preempt them by fixed priority. Deadlines are in hardware cycle units, the relative call adds the current cycle count, so for periodic work I set the absolute deadline from the release time. Setting it doesn't reschedule by itself, there's no admission control or budget enforcement, and it can't be combined with the multi-queue backend. A nice pattern is to keep the critical control loop at a higher fixed priority and run the softer tasks under EDF.

**Q.** What is ARINC 653 partition scheduling?

**A.** 2단계 스케줄링이다. 1단계는 major time frame 안에 파티션별 고정 창을 두는 시간표, 2단계는 창 안에서 파티션 자기 process를 우선순위로 스케줄한다. 목적은 시간 격리 — 한 파티션이 폭주해도 다른 파티션의 창은 줄지 않는다. 공간 분할(MMU)과 health monitor, 파티션 간 port 통신이 함께 간다.

> It's a two-level scheme. The module scheduler runs a static table of partition windows over a major time frame, and inside its window each partition schedules its own processes by priority. The point is temporal isolation: a partition can only consume its own windows. I built a two-partition version on FreeRTOS by priority bands, and when I made the comms task spin forever at 100 milliseconds, the flight-control partition kept every deadline, whereas in a flat priority design the same bug starved the control tasks completely. The costs are window-phase latency, a few milliseconds in my setup, and the need to analyze each partition against an intermittent supply. Real systems add spatial partitioning with an MMU or MPU, a health monitor and port-based communication instead of shared locks.

**Q.** In a time-triggered cooperative scheduler, how do you detect and handle task overruns?

**A.** 세 겹이다. task마다 실행 시간을 재서 예산과 비교(누가 범인인지), tick ISR에서 "이전 dispatch가 아직 실행 중"인지 보는 깃발(시스템 규칙 위반), 그리고 watchdog(완전히 멈춤). offset으로 한 tick에 부하가 몰리지 않게 하는 것이 예방이다.

> Three layers. First, measure each task's execution time against its budget, which tells you who the culprit is. Second, in the tick ISR check whether the dispatcher is still running from the previous tick, which tells you the timing rule of the whole system was broken. Third, a hardware watchdog for a full hang. Prevention is mostly offsets: in my experiment, releasing four tasks in the same tick caused an overrun every 10 milliseconds and 409 microseconds of jitter on the 1 kHz control task; staggering the offsets brought it down to 3 microseconds with zero overruns.

**Q.** How would you implement EDF on FreeRTOS, and would you?

**A.** 커널에 deadline이 없으니 최고 우선순위 관리 task가 release마다 모든 task 우선순위를 deadline 순으로 다시 매긴다. release된 task는 스스로 우선순위를 못 올리고, `vTaskPrioritySet`은 ISR에서 못 부른다. 비용이 task 수에 비례해 커서 보통은 하지 않는다.

> FreeRTOS has no deadline field, so you need a highest-priority manager task that handles releases and reassigns every task's priority in deadline order with vTaskPrioritySet; a released task can't promote itself because it isn't running yet, and that call isn't ISR-safe. It works, I got zero misses on a set where rate-monotonic missed 42 times a second, but on a 25 MHz Cortex-M3 the manager took 18 microseconds per release with 3 tasks and 87 with 12. I'd rather fix the priorities with deadline-monotonic analysis, or choose a kernel with native deadline support.

**Q.** Fixed priority, EDF, cyclic, partitioned — which would you pick for a drone flight controller?

**A.** FC MCU 안은 FP + RM/DM + RTA, 고속 루프는 IMU 인터럽트에 묶은 time-triggered 구조. 비행 제어와 임무 소프트웨어는 칩을 나누거나(공간 분할), 한 SoC면 하이퍼바이저 · 파티션 RTOS. EDF는 이점이 작다(주기가 조화라 RM으로도 100%).

> Inside the flight-control MCU: fixed priorities assigned by rate or deadline monotonic, verified with response-time analysis, and the fast loops time-triggered off the IMU data-ready interrupt with harmonic rates, which lets rate monotonic reach full utilization anyway, so EDF buys little. Mission and payload software goes on a separate processor; if it must share silicon, I'd use a partitioning RTOS or hypervisor so a payload bug can't eat the control loop's time.

---

## 11. 직접 해보기

1. **개념** — task set (T, C, D) = (5, 1, 5), (10, 3, 10), (25, 4, 25)에서 frame 크기 후보를 손으로 구하라. 정답/힌트: H = 50. f ≥ 4이고 50을 나누는 후보는 5, 10, 25, 50. f = 5: 2·5 − gcd(5, 5) = 5 ≤ 5, 2·5 − gcd(10, 5) = 5 ≤ 10, 2·5 − gcd(25, 5) = 5 ≤ 25 → OK. f = 10은 첫 task에서 20 − 5 = 15 > 5로 탈락. 예제 1 코드에 넣어 확인하라.
2. **코드** — 예제 2의 탐욕 배치를 Liu식 max-flow로 바꿔라(source → job 용량 C → 가능한 frame → sink 용량 f − MARGIN). 정답/힌트: Edmonds-Karp 30줄이면 된다. 처음 실패한 nav 1500 µs 경우에 flow가 ΣC를 채우는지(즉 탐욕만 실패했는지, 정말 불가능했는지) 확인하라 — nav를 자를 수 없다는 조건은 "job → frame 간선 하나만 쓰기"라 flow만으로는 표현이 안 된다는 점도 생각해 보라.
3. **측정** — 예제 4에 "abort" 정책을 넣어라: frame 경계 IRQ에서 busy면 nav의 `spin_us`가 보는 전역 깃발을 세워 조기 return. 정답/힌트: 협력형 abort는 결국 "job이 깃발을 폴링"하는 것. ctrl 늦음, 잃은 job 수를 catch-up/skip과 표로 비교하라.
4. **측정** — 예제 7에서 rate만 우선순위 2(FP), att · nav는 우선순위 3에서 EDF로 하는 **혼합** 모드를 추가하고 U = 0.917 · 1.054 · 1.2에서 돌려라. 정답/힌트: rate는 FP처럼 0% 실패 · 최악 약 1 ms로 돌아오고, 과부하에서는 att · nav 둘만 domino에 빠질 것이다. 그림 7에 세 번째 패널로 그려 보라.
5. **코드** — 예제 7의 worker에 "deadline을 넘은 job은 버리고 다음 release로"(skip)를 넣고 과부하 스윕을 다시 하라. 정답/힌트: 늦은 job이 큐에서 사라지니 domino가 끊긴다. EDF의 실패 비율이 대략 (U − 1)/U 근처로 내려오는지 보라.
6. **개념 + 측정** — 예제 11에서 P1 창을 [6, 10)에서 [0, 4)로 옮기고 P0를 [4, 10)으로 바꾸면 comms의 응답 시간과 rate의 응답 시간이 어떻게 변하나? 정답/힌트: 창 위상이 release 직후로 오는 파티션의 지연이 준다. 간트를 다시 그려 확인하라.
7. **조사** — FreeRTOS SMP나 Zephyr SMP에서 파티션을 "코어 하나 = 파티션 하나"로 하면 무엇이 쉬워지고 무엇이 남나? 정답/힌트: 시간 창이 필요 없어지지만 공유 캐시 · 버스 간섭이 남는다(N8, C10).

---

## 12. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| cyclic executive | 순환 실행부 | 설계 때 만든 시간표를 타이머 frame마다 실행하는 커널 없는 스케줄러 |
| time-triggered | 시간 구동 | 일의 시작을 이벤트가 아니라 시계(시간표)가 정한다 |
| major cycle | 주 사이클 | 시간표 한 장의 길이, 보통 hyperperiod |
| minor frame | 부 프레임 | 시간표의 한 칸, frame 경계마다 타이머 인터럽트 |
| hyperperiod | 초주기 | 모든 주기의 최소공배수, release 패턴이 반복되는 길이 |
| job slicing | job 자르기 | 긴 job을 여러 frame에 나눠 실행하는 조각들로 만들기 |
| frame overrun | 프레임 초과 | frame 안의 일이 다음 경계까지 안 끝남 |
| TTC | time-triggered cooperative | Pont식 tick 기반 협력형 스케줄러 (`SCH_Update` / `SCH_Dispatch_Tasks`) |
| offset | 위상, 첫 지연 | 같은 주기 task들의 release를 tick 단위로 어긋나게 두는 값 |
| time budget | 시간 예산 | task 1회 실행에 허용한 최대 시간, 넘으면 위반으로 기록 |
| EDF | earliest deadline first | 절대 deadline이 가장 이른 job부터 실행하는 동적 우선순위 정책 |
| domino effect | 도미노 효과 | EDF 과부하에서 늦은 job들이 서로를 밀어 전부 늦어지는 현상 |
| graceful degradation | 우아한 저하 | 과부하 때 정해진(낮은 우선순위) 일만 희생되는 성질 |
| CBS | constant bandwidth server | task에 (runtime, period) 예산을 주고 넘으면 deadline을 미뤄 격리하는 기법 |
| hyperbolic bound | 쌍곡선 상한 | Π(Uᵢ + 1) ≤ 2이면 RM 스케줄 가능 (Liu-Layland보다 덜 비관적) |
| ARINC 653 | 항공 응용 표준 인터페이스 | 파티션 · APEX API · 시간/공간 분할을 정한 항공 소프트웨어 규격 |
| partition window | 파티션 창 | major time frame 안에서 한 파티션에 고정 배정된 시간 구간 |
| MAF | major time frame | 파티션 창 시간표 한 장의 길이 |
| health monitor | 건강 감시 | 오류를 감지해 process · 파티션 · 모듈 단위로 대응하는 ARINC 653 기능 |
| temporal partitioning | 시간 분할 | 각 파티션이 자기 시간 창만 쓰도록 보장하는 것 |
| priority band | 우선순위 띠 | FP 커널에서 계층 스케줄링을 흉내 내려고 우선순위 범위를 나눠 쓰는 기법 |
| schedule table | 스케줄 테이블 | AUTOSAR OS에서 정해진 시각에 task를 활성화하는 정적 표 |

---

## 13. 요약 & 체크리스트

스케줄러는 "다음에 누구를 돌릴지"를 **언제, 무엇으로** 정하느냐로 갈린다. cyclic executive와 Pont TTC는 그 결정을 **설계 때** 끝낸다: frame 크기를 세 조건(f ≥ max C, f가 주기를 나눔, 2f − gcd(T, f) ≤ D)으로 고르고, 안 되면 긴 job을 잘라 시간표를 만들면 실행 때는 표만 읽는다 — QEMU에서 frame 시작 지터 1 µs, 오버런 감지는 ISR 깃발 하나였다. FP는 실행 중에 고정 우선순위로, EDF는 job deadline으로 정한다. U = 0.917 task set에서 RM은 nav가 1초에 42번 놓쳤고(RTA 고정점 10 ms > 8 ms) Zephyr EDF는 0번이었지만, EDF는 짧은 주기 task의 지터를 2.5배 키웠고 U ≈ 1(오버헤드 포함 실효 U > 1)에서 세 task가 동시에 80~99% 놓치는 domino를 보였다. FP는 같은 과부하에서 맨 아래 task만 잃었다. Zephyr의 deadline은 같은 정적 우선순위 안에서만 의미가 있고, FreeRTOS에서 EDF를 흉내 내면 release마다 우선순위 재배치 비용(task 12개에 87 µs)이 든다. ARINC 653식 2단계 스케줄링은 FP 위에 시간 울타리를 쳐서, 통신 파티션의 무한 루프가 비행 제어 파티션의 deadline을 하나도 깨지 못하게 했다 — 대가는 창 위상 지연(약 6 ms)과 창 전환 비용(16 µs)이었다.

- [ ] hyperperiod, major cycle, minor frame, slice를 한 문장씩 정의할 수 있다
- [ ] frame 크기 세 조건을 쓰고, 조건 (3)이 왜 2f − gcd(T, f)인지 그림으로 설명할 수 있다
- [ ] 세 조건이 충돌할 때 job slicing으로 푸는 과정과 slicing의 코드 비용을 말할 수 있다
- [ ] cyclic executive에서 frame 오버런을 감지하는 가장 싼 방법과 catch-up / skip의 차이를 실측 숫자로 말할 수 있다
- [ ] Pont TTC의 구조(`SCH_Update`, `SCH_Dispatch_Tasks`, `Delay`, `Period`, `RunMe`)와 offset의 효과를 설명할 수 있다
- [ ] 예산 위반 감지와 tick 오버런 감지가 서로 다른 것을 잡는다는 점을 설명할 수 있다
- [ ] RM으로 안 되고 EDF로 되는 task set을 하나 들고 RTA로 증명할 수 있다
- [ ] Zephyr `CONFIG_SCHED_DEADLINE`의 의미(같은 우선순위 안, cycle 단위, reschedule 없음, admission control 없음)를 소스 근거와 함께 말할 수 있다
- [ ] EDF domino 효과와 FP의 우아한 저하를 측정 결과로 설명하고, 과부하 대책(CBS, skip, 혼합)을 댈 수 있다
- [ ] FreeRTOS에서 EDF 흉내가 어색한 이유 세 가지를 말할 수 있다
- [ ] ARINC 653의 MAF · partition window · 2단계 스케줄링 · health monitor를 설명하고, `vTaskSuspend` 대신 우선순위 띠를 쓴 이유를 말할 수 있다
- [ ] FP · EDF · cyclic · 파티션을 결정성, 이용률, 과부하, 인증, 구현 비용으로 비교하는 표를 그릴 수 있다

---

## Bare-Metal 노트와의 관계

| BM ID | BM 주제 | 이 노트의 해당 절 | Bare-Metal 쪽에 남길 내용 |
|---|---|---|---|
| BM K5 | 협력형 · time-triggered 스케줄러 — 태스크 표 | §1.2 · §1.6(시간표 생성기 · C 표), §1.7(예제 3) | 이 노트를 링크하고, 실칩에서 SysTick 대신 저전력 타이머(LPTIM)로 frame을 만드는 레지스터 코드만 |
| BM K5 | Pont TTC | §2.1 · §2.2(예제 5: `SCH_Update` / `SCH_Dispatch_Tasks` / offset) | 링크 + M0급 MCU에서의 코드 크기와 "WFI로 tick 사이 잠자기"의 실칩 전류 측정 한 문단 |
| BM K5 | 시간 예산 · 오버런 감지 | §1.8 · §1.9(frame 오버런 정책), §2.2 · §2.3(예산 · tick 오버런 · watchdog 비교) | 링크 + 하드웨어 watchdog(IWDG/WWDG) 설정과 window watchdog으로 "너무 빨리 끝남"까지 잡는 bare-metal 관점 |
| BM K5 | (관련) job slicing · 상태 머신 조각내기 | §1.5, A2 §4.4 | 링크만 — 상태 머신 조각내기의 코드 패턴은 BM K4/K6(이벤트 구동 · protothread) 쪽 관점으로 한 줄 |

---

## 참고 자료

- T. P. Baker, A. Shaw, "The Cyclic Executive Model and Ada", 『Real-Time Systems』 1(1), 1989 — frame 크기 조건의 원전
- J. W. S. Liu, 『Real-Time Systems』, Prentice Hall, 2000 — 5장 clock-driven scheduling(frame 크기 조건, job slicing, network flow로 시간표 만들기, 고전 예제 T1 = (4, 1), T2 = (5, 2, 7), T3 = (20, 5))
- C. L. Liu, J. W. Layland, "Scheduling Algorithms for Multiprogramming in a Hard-Real-Time Environment", 『Journal of the ACM』 20(1), 1973 — RM 상한과 EDF의 U ≤ 1 조건
- E. Bini, G. C. Buttazzo, G. M. Buttazzo, "Rate Monotonic Analysis: The Hyperbolic Bound", 『IEEE Transactions on Computers』 52(7), 2003
- G. C. Buttazzo, 『Hard Real-Time Computing Systems』, Springer (3rd ed., 2011) — EDF, 과부하와 domino 효과, CBS
- G. C. Buttazzo, "Rate Monotonic vs. EDF: Judgment Day", 『Real-Time Systems』 29, 2005
- M. J. Pont, 『Patterns for Time-Triggered Embedded Systems』, ACM Press / Addison-Wesley, 2001 — TTC 스케줄러(`SCH_Update`, `SCH_Dispatch_Tasks`, `SCH_Add_Task`), hybrid scheduler
- H. Kopetz, 『Real-Time Systems: Design Principles for Distributed Embedded Applications』, Springer (2nd ed., 2011) — time-triggered architecture
- I. Shin, I. Lee, "Periodic Resource Model for Compositional Real-Time Guarantees", IEEE RTSS 2003 — 파티션 안 스케줄 가능성 분석
- ARINC Specification 653, 『Avionics Application Software Standard Interface』 (Part 1 Required Services) — 파티션, major time frame, partition window, health monitor, port
- SAE AS6802 (TTEthernet), ISO 17458 (FlexRay) — 분산 time-triggered 통신 규격
- Zephyr v4.4.2 소스: `kernel/Kconfig`(`SCHED_DEADLINE`, `SCHED_MULTIQ`), `kernel/sched.c`(`z_impl_k_thread_absolute_deadline_set`, `z_impl_k_thread_deadline_set`, `next_up`), `kernel/include/priority_q.h`(`z_sched_prio_cmp`, `z_priq_simple_add`), `kernel/include/kthread.h`(`should_preempt`), `include/zephyr/kernel.h`(`k_thread_deadline_set` API 문서), `kernel/thread.c`
- Zephyr 문서 — Scheduling: https://docs.zephyrproject.org/latest/kernel/services/scheduling/index.html
- FreeRTOS Kernel V11.3.2 소스: `tasks.c`(`vTaskPrioritySet`, `vTaskSuspend`, `vTaskResume`, `xTaskDelayUntil`, `vTaskSwitchContext`의 `traceTASK_SWITCHED_IN`), `include/FreeRTOS.h`(trace 매크로 기본 정의)
- Linux `SCHED_DEADLINE` 문서: https://docs.kernel.org/scheduler/sched-deadline.html (EL F1 · N2에서 다룸)
- ArduPilot 소스 `libraries/AP_Scheduler/` — task 표(주기 Hz, 최대 실행 µs 예산) 기반 협력형 스케줄러
