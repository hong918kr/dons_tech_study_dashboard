# 부록 B. 가상 머신 모니터 — OS 아래에 OS를 하나 더

> 📖 원문: [B. Virtual Machine Monitors](../book-md/C0B_virtual_machine_monitors.md) · [PDF p.608](../Operating%20Systems%20-%20Three%20Easy%20Pieces.pdf#page=608) · ⏱️ 읽기 약 40분 · 🔗 선행: [Ch.06 제한적 직접 실행](2026-09-30_C06_limited_direct_execution.md), [Ch.19 TLB](2026-09-30_C19_tlb.md), [Ch.23 VAX/VMS](2026-09-30_C23_vax_vms.md)

## 0. 한눈에 보기

> **THE CRUX: How to virtualize the machine underneath the OS** — the virtual machine monitor must transparently virtualize the machine underneath the OS; what are the techniques required to do so?
> (OS 밑에서 기계를 어떻게 가상화하나 — VMM은 OS가 눈치채지 못하게 기계를 가상화해야 한다. 어떤 기법이 필요한가?)

- **VMM(= hypervisor)**은 OS와 HW 사이에 끼어 "각 OS가 기계 전체를 가졌다"는 착각을 만든다. **OS를 위한 OS**. 최대 목표는 **투명성(transparency)**.
- CPU 가상화: **limited direct execution을 한 층 더**. 게스트 OS를 덜 특권적인 모드에서 직접 실행하고, 특권 명령은 **trap → VMM이 흉내(emulate)** 낸다. 시스템 콜도 VMM이 먼저 받아 게스트 OS의 trap 핸들러로 **반사(reflect)**한다.
- 메모리 가상화: 주소 변환이 **VA → PA("물리") → MA(machine)** 의 2단계가 된다. SW TLB면 게스트의 TLB 쓰기를 가로채 VPN→MFN을 대신 설치하고, HW TLB면 **shadow page table**을 유지한다.
- **정보 격차(information gap)**: VMM은 게스트 OS가 무엇을 하는지 모른다(idle loop, 이중 zeroing). 해법은 **추론(implicit information)** 또는 OS를 고치는 **para-virtualization**.
- 오늘날엔 Intel VT-x/AMD-V/ARM EL2 같은 **HW 지원(nested paging 등)**이 이 장의 SW 기법 상당수를 대체했다 — 이 노트에서 현대 SoC와 연결한다.

## 1. 5분 복습표

| 용어 | 한 줄 뜻 | 예시 / 비유 |
|---|---|---|
| VMM / hypervisor | 여러 OS 아래에서 HW를 다중화하는 SW 층 | VMware ESXi, KVM, Xen, Hyper-V |
| 투명성(transparency) | 게스트 OS가 가상화된 줄 모르게 하는 것 | 수정 없는 OS가 그대로 부팅 |
| machine switch | VM 간 전환 — 레지스터 + **특권 HW 상태**까지 저장/복원 | 프로세스 context switch의 VM판 |
| trap-and-emulate | 특권 명령을 trap으로 받아 VMM이 효과를 흉내 | 게스트의 TLB 쓰기 가로채기 |
| reflect | VMM이 받은 trap을 게스트 OS 핸들러로 넘김 | 시스템 콜 전달 |
| supervisor mode | MIPS의 user와 kernel 사이 중간 모드 (Disco가 게스트 OS용으로 사용) | ARM EL1, x86 ring 1 |
| physical vs machine memory | 게스트가 "물리"라 믿는 주소 vs 진짜 DRAM 주소 | PFN vs MFN |
| per-VM pmap | VMM이 VM마다 유지하는 PFN→MFN 표 | Figure B.1의 VMM Page Table |
| software TLB (Disco) | VMM이 본 VPN→MFN 매핑을 캐시 | 게스트 핸들러 왕복 생략 |
| shadow page table | VA→MA를 직접 담은 표, HW가 이걸 walk | HW-managed TLB용 |
| information gap | VMM이 게스트의 의도를 모르는 문제 | idle loop, 이중 zero |
| para-virtualization | 게스트 OS를 고쳐 VMM과 협력 | Xen, virtio, 하이퍼콜 |
| implicit information | 인터페이스 변경 없이 관찰로 추론 | 저전력 모드 진입 = idle |
| nested paging (2단계 변환) | HW가 VA→IPA→PA 두 표를 모두 walk | Intel EPT, AMD NPT, ARM stage-2 |

## 2. 도입 (B.1)

옛날 IBM 메인프레임 고객은 한 비싼 기계에서 **여러 OS를 동시에** 돌리고 싶었다. IBM의 답은 또 하나의 간접 층(level of indirection), **VMM**이었다.

재밌는 상황: 지금까지 OS는 "마술사"였다 — 앱에게 사설 CPU와 거대한 메모리라는 환상을 보여 줬다. 이제 같은 마술을 **OS 밑에서**, 늘 주인 행세를 하던 OS를 상대로 해야 한다. VMM 입장에서 OS는 "그냥 좀 특별한 프로세스"다.

## 3. 왜 VMM인가 (B.2)

- **서버 통합(server consolidation)**: 서로 다른 OS를 쓰는, 활용률 낮은 서버 여러 대를 소수의 기계로 합쳐 비용·관리 부담을 줄인다.
- **데스크톱**: Linux/macOS를 쓰면서 Windows 앱도 쓰고 싶다.
- **테스트·디버깅**: 한 기계에서 여러 OS/버전을 돌린다.
- 역사: 1990년대 후반 Stanford의 Mendel Rosenblum 그룹이 MIPS용 VMM **Disco**(1997)로 부흥을 이끌었고, 이 그룹이 **VMware**(1998)를 창업했다. 이 장은 Disco의 기술을 통해 가상화를 본다.

현대 추가 동기(원문 밖): 클라우드 멀티테넌시, 보안 격리(예: 모바일의 보호 VM), 자동차 SoC에서 안전 등급이 다른 OS(계기판 RTOS + 인포테인먼트 Linux/Android)를 한 칩에 함께 돌리기.

## 4. CPU 가상화 (B.3)

### 기본: limited direct execution 재활용

새 OS를 VMM 위에서 "부팅"하려면 그냥 OS의 첫 명령 주소로 점프해서 **직접 실행**시킨다(거의 그게 전부).

VM 두 개를 한 CPU에서 번갈아 돌리려면 **machine switch**가 필요하다. 프로세스 context switch와 비슷하지만, 레지스터·PC뿐 아니라 **특권 HW 상태**(페이지 테이블 베이스, trap 벡터, 인터럽트 마스크 등)까지 저장·복원해야 한다. 복원할 PC는 게스트 OS 안(시스템 콜 처리 중)일 수도, 게스트의 유저 프로세스 안일 수도 있다.

### 문제: 게스트 OS가 특권 명령을 실행하면?

예: SW-managed TLB에서 OS는 TLB miss 후 **특권 명령으로 TLB에 엔트리를 쓴다**. 게스트 OS가 이걸 진짜로 하면 VMM이 아니라 게스트가 기계를 지배하게 된다. 그러므로 VMM은 **특권 연산을 가로채야** 한다.

### 시스템 콜이 VMM을 거치는 과정

FreeBSD의 `open()` 라이브러리 코드(x86): 인자 3개(mode, flags, path)를 스택에 push, 시스템 콜 번호 5를 push, `int 80h`.

```text
open:
    push  dword mode
    push  dword flags
    push  dword path
    mov   eax, 5        ; 5 = FreeBSD의 open() 시스템 콜 번호
    push  eax
    int   80h           ; trap! (Intel은 이걸 "interrupt"라고 부른다)
```

**가상화 없이 (Table B.2)**

```text
Process                    Operating System
1. 시스템 콜: OS로 trap
                           2. OS trap 핸들러: 디코드, 시스템 콜 실행, return-from-trap
3. trap 다음 PC부터 재개
```

**VMM 위에서 (Table B.3)** — 진짜 trap 핸들러는 VMM이 설치했다.

```text
Process              Operating System            VMM
1. 시스템 콜: trap
                                                 2. 프로세스가 trap함:
                                                    게스트 OS의 trap 핸들러 호출 (권한 낮춰서)
                     3. OS trap 핸들러: 디코드,
                        시스템 콜 실행, 끝나면
                        return-from-trap 실행
                                                 4. OS가 return-from-trap 시도 (특권 명령!) → trap:
                                                    VMM이 진짜 return-from-trap 수행
5. 재개
```

VMM은 게스트 OS의 시스템 콜 의미를 모른다. 대신 **게스트의 trap 핸들러 위치는 안다** — 게스트가 부팅 때 trap 핸들러를 설치하려 했고(특권 연산이라 trap), 그때 VMM이 주소를 기록해 두었기 때문이다.

> 비용: 시스템 콜 하나에 **trap이 2번 더**(VMM 진입 + 게스트의 return-from-trap). 가상화는 시스템 콜을 느리게 할 수 있다.

### 그럼 게스트 OS는 어느 모드에서 도나?

커널 모드는 안 된다(HW 전권). 유저 모드면 게스트 OS 자기 자료구조를 프로세스로부터 보호해야 한다.

- **Disco의 해법**: MIPS의 **supervisor mode** — 특권 명령은 못 쓰지만 유저 모드보다 **조금 더 많은 메모리**에 접근 가능. 게스트 OS 자료구조를 거기 둔다.
- 그런 모드가 없으면: 게스트 OS를 유저 모드에서 돌리고 **페이지 테이블 보호**로 OS 메모리를 지킨다. 게스트 OS로 전환할 때 OS 메모리를 접근 가능하게, 앱으로 돌아갈 때 다시 막는다(비싸다).

```svg
<svg viewBox="0 0 700 270" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
<text x="350" y="20" text-anchor="middle" font-weight="bold" fill="currentColor">누가 어느 특권 레벨에서 도나</text>
<text x="115" y="48" text-anchor="middle" fill="currentColor" font-weight="bold">① 네이티브</text>
<text x="350" y="48" text-anchor="middle" fill="currentColor" font-weight="bold">② Disco (MIPS)</text>
<text x="585" y="48" text-anchor="middle" fill="currentColor" font-weight="bold">③ 현대 ARMv8 (HW 지원)</text>
<rect x="30" y="65" width="170" height="40" rx="6" fill="none" stroke="currentColor"/>
<text x="115" y="90" text-anchor="middle" fill="currentColor">앱 — user</text>
<rect x="30" y="185" width="170" height="40" rx="6" style="fill:var(--accent-soft);stroke:var(--accent)" stroke-width="2"/>
<text x="115" y="210" text-anchor="middle" fill="currentColor">OS — kernel (전권)</text>
<rect x="265" y="65" width="170" height="40" rx="6" fill="none" stroke="currentColor"/>
<text x="350" y="90" text-anchor="middle" fill="currentColor">앱 — user</text>
<rect x="265" y="125" width="170" height="40" rx="6" fill="none" stroke="currentColor" stroke-dasharray="5 3"/>
<text x="350" y="143" text-anchor="middle" fill="currentColor">게스트 OS — supervisor</text>
<text x="350" y="158" text-anchor="middle" fill="currentColor" font-size="11">특권 명령 실행 → trap</text>
<rect x="265" y="185" width="170" height="40" rx="6" style="fill:var(--accent-soft);stroke:var(--accent)" stroke-width="2"/>
<text x="350" y="210" text-anchor="middle" fill="currentColor">VMM — kernel (전권)</text>
<rect x="500" y="60" width="170" height="34" rx="6" fill="none" stroke="currentColor"/>
<text x="585" y="82" text-anchor="middle" fill="currentColor">앱 — EL0</text>
<rect x="500" y="100" width="170" height="34" rx="6" fill="none" stroke="currentColor" stroke-dasharray="5 3"/>
<text x="585" y="122" text-anchor="middle" fill="currentColor">게스트 커널 — EL1</text>
<rect x="500" y="140" width="170" height="34" rx="6" style="fill:var(--accent-soft);stroke:var(--accent)" stroke-width="2"/>
<text x="585" y="162" text-anchor="middle" fill="currentColor">Hypervisor — EL2</text>
<rect x="500" y="180" width="170" height="34" rx="6" fill="none" stroke="currentColor"/>
<text x="585" y="202" text-anchor="middle" fill="currentColor">Secure monitor — EL3</text>
<line x1="245" y1="60" x2="245" y2="235" stroke="currentColor" stroke-opacity="0.3"/>
<line x1="480" y1="60" x2="480" y2="235" stroke="currentColor" stroke-opacity="0.3"/>
<text x="350" y="255" text-anchor="middle" fill="currentColor" font-size="12">아래로 갈수록 특권 ↑ · 점선 = "자기가 커널이라 믿지만 한 단계 낮은" 게스트 OS</text>
</svg>
```

### 원문 밖 보충: Popek–Goldberg와 x86의 골칫거리

trap-and-emulate가 성립하려면 **"민감한(sensitive) 명령은 모두 특권(privileged) 명령이어야 한다"** — 즉 유저/낮은 모드에서 실행하면 반드시 trap해야 한다(Popek & Goldberg, 1974). 옛 x86은 이를 위반했다. 예: `POPF`는 유저 모드에서 인터럽트 플래그를 바꾸려 하면 **trap 없이 조용히 무시**한다. 그래서 VMware는 **binary translation**(게스트 커널 코드를 실행 전에 다시 써서 문제 명령을 VMM 호출로 바꿈)을 썼고, 결국 Intel VT-x/AMD-V가 "게스트 전용 모드"를 HW로 추가했다(원문 참고문헌 [AA06]이 이 이야기).

## 5. 메모리 가상화 (B.4)

OS는 물리 메모리를 페이지의 선형 배열로 보고 자기와 프로세스에 나눠 준다. 이제 여러 OS가 진짜 메모리를 **투명하게** 나눠 써야 하므로 층이 하나 더 생긴다.

- 게스트 OS: 프로세스별 페이지 테이블로 **VA → PA**("physical", 게스트가 믿는 물리 주소)
- VMM: VM별 표로 **PA → MA**(machine address, 진짜 DRAM)

```svg
<svg viewBox="0 0 700 300" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
<defs><marker id="C0B-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="350" y="20" text-anchor="middle" font-weight="bold" fill="currentColor">Figure B.1: 주소 변환이 두 층이 된다</text>
<text x="90" y="48" text-anchor="middle" fill="currentColor" font-weight="bold">Virtual (프로세스)</text>
<text x="350" y="48" text-anchor="middle" fill="currentColor" font-weight="bold">"Physical" (게스트 OS가 믿는)</text>
<text x="610" y="48" text-anchor="middle" fill="currentColor" font-weight="bold">Machine (진짜 DRAM)</text>
<rect x="50" y="60" width="80" height="30" fill="none" stroke="currentColor"/><text x="90" y="80" text-anchor="middle" fill="currentColor">VPN 0</text>
<rect x="50" y="90" width="80" height="30" fill="none" stroke="currentColor" stroke-opacity="0.4"/><text x="90" y="110" text-anchor="middle" fill="currentColor" fill-opacity="0.5">VPN 1 ✗</text>
<rect x="50" y="120" width="80" height="30" fill="none" stroke="currentColor"/><text x="90" y="140" text-anchor="middle" fill="currentColor">VPN 2</text>
<rect x="50" y="150" width="80" height="30" fill="none" stroke="currentColor"/><text x="90" y="170" text-anchor="middle" fill="currentColor">VPN 3</text>
<rect x="310" y="80" width="80" height="30" style="fill:var(--accent-soft)" stroke="currentColor"/><text x="350" y="100" text-anchor="middle" fill="currentColor">PFN 3</text>
<rect x="310" y="140" width="80" height="30" style="fill:var(--accent-soft)" stroke="currentColor"/><text x="350" y="160" text-anchor="middle" fill="currentColor">PFN 8</text>
<rect x="310" y="200" width="80" height="30" style="fill:var(--accent-soft)" stroke="currentColor"/><text x="350" y="220" text-anchor="middle" fill="currentColor">PFN 10</text>
<rect x="570" y="70" width="80" height="30" fill="none" style="stroke:var(--accent)" stroke-width="2"/><text x="610" y="90" text-anchor="middle" fill="currentColor">MFN 5</text>
<rect x="570" y="130" width="80" height="30" fill="none" style="stroke:var(--accent)" stroke-width="2"/><text x="610" y="150" text-anchor="middle" fill="currentColor">MFN 6</text>
<rect x="570" y="190" width="80" height="30" fill="none" style="stroke:var(--accent)" stroke-width="2"/><text x="610" y="210" text-anchor="middle" fill="currentColor">MFN 10</text>
<line x1="130" y1="75" x2="308" y2="212" stroke="currentColor" marker-end="url(#C0B-arrow)"/>
<line x1="130" y1="135" x2="308" y2="97" stroke="currentColor" marker-end="url(#C0B-arrow)"/>
<line x1="130" y1="165" x2="308" y2="155" stroke="currentColor" marker-end="url(#C0B-arrow)"/>
<line x1="390" y1="95" x2="568" y2="143" style="stroke:var(--accent)" stroke-width="2" marker-end="url(#C0B-arrow)"/>
<line x1="390" y1="155" x2="568" y2="205" style="stroke:var(--accent)" stroke-width="2" marker-end="url(#C0B-arrow)"/>
<line x1="390" y1="215" x2="568" y2="87" style="stroke:var(--accent)" stroke-width="2" marker-end="url(#C0B-arrow)"/>
<text x="220" y="262" text-anchor="middle" fill="currentColor" font-size="12">게스트 OS 페이지 테이블 (프로세스별)</text>
<text x="480" y="262" text-anchor="middle" fill="currentColor" font-size="12">VMM 페이지 테이블 (VM별 pmap)</text>
<text x="350" y="288" text-anchor="middle" fill="currentColor" font-size="12">shadow page table = 두 화살표를 합친 VPN → MFN (0→5, 2→6, 3→10)</text>
</svg>
```

### 원문 예제 (Figure B.1) 계산

```text
게스트 OS 페이지 테이블       VMM 페이지 테이블 (per-VM)       결과 (VA → MA)
VPN 0 → PFN 10               PFN 10 → MFN 5                  VPN 0 → MFN 5
VPN 1 → (invalid)            -                               invalid
VPN 2 → PFN 3                PFN 3  → MFN 6                  VPN 2 → MFN 6
VPN 3 → PFN 8                PFN 8  → MFN 10                 VPN 3 → MFN 10

예: 4 KB 페이지, 게스트 프로세스가 VA 0x2ABC 를 읽는다.
  VPN = 0x2ABC >> 12 = 2,  offset = 0xABC
  게스트: VPN 2 → PFN 3   → "물리" 주소 0x3ABC  (게스트는 여기를 읽는다고 믿음)
  VMM   : PFN 3 → MFN 6   → machine 주소 0x6ABC  (실제로 DRAM에서 읽는 곳)
```

실제 시스템이라면 OS가 V개(VMM 페이지 테이블 V개), 각 OS 위에 프로세스 P_i개(프로세스별 페이지 테이블 P_i개)가 있다.

### SW-managed TLB에서의 TLB miss

**가상화 없이 (Table B.4)**: 프로세스 load → TLB miss → OS로 trap → OS가 VA에서 VPN 추출, 페이지 테이블 조회, 유효하면 PFN을 TLB에 설치 → return-from-trap → 명령 재실행 → TLB hit.

**VMM 위에서 (Table B.5)**

```text
Process            Operating System                 VMM
1. load: TLB miss → trap
                                                    2. VMM TLB miss 핸들러:
                                                       게스트 OS TLB 핸들러 호출 (권한 낮춤)
                   3. OS TLB miss 핸들러:
                      VPN 추출, 페이지 테이블 조회,
                      유효하면 PFN 얻고 TLB 갱신 시도
                                                    4. trap 핸들러: 비특권 코드가 TLB 갱신 시도!
                                                       OS는 VPN→PFN을 넣으려 함
                                                       → 대신 VPN→MFN 을 설치 (특권)
                                                       → OS로 복귀 (권한 낮춤)
                   5. return-from-trap
                                                    6. trap 핸들러: 비특권 코드가 return-from-trap
                                                       → 진짜 return-from-trap
7. 명령 재실행 → TLB hit (MFN에서 데이터)
```

```svg
<svg viewBox="0 0 700 330" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
<defs><marker id="C0B-arrow2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="350" y="20" text-anchor="middle" font-weight="bold" fill="currentColor">가상화된 TLB miss 한 번 (SW-managed TLB, Table B.5)</text>
<text x="110" y="46" text-anchor="middle" fill="currentColor" font-weight="bold">Process (user)</text>
<text x="350" y="46" text-anchor="middle" fill="currentColor" font-weight="bold">Guest OS (supervisor)</text>
<text x="590" y="46" text-anchor="middle" fill="currentColor" font-weight="bold">VMM (kernel)</text>
<line x1="110" y1="55" x2="110" y2="315" stroke="currentColor" stroke-opacity="0.3" stroke-dasharray="3 3"/>
<line x1="350" y1="55" x2="350" y2="315" stroke="currentColor" stroke-opacity="0.3" stroke-dasharray="3 3"/>
<line x1="590" y1="55" x2="590" y2="315" stroke="currentColor" stroke-opacity="0.3" stroke-dasharray="3 3"/>
<line x1="110" y1="75" x2="586" y2="85" stroke="#d9534f" stroke-width="2" marker-end="url(#C0B-arrow2)"/>
<text x="200" y="70" fill="currentColor" font-size="12">① load → TLB miss → trap (HW)</text>
<line x1="590" y1="110" x2="354" y2="125" stroke="currentColor" marker-end="url(#C0B-arrow2)"/>
<text x="480" y="106" text-anchor="middle" fill="currentColor" font-size="11">② 게스트 핸들러로 반사</text>
<rect x="250" y="135" width="200" height="34" rx="5" style="fill:var(--accent-soft)" stroke="currentColor"/>
<text x="350" y="150" text-anchor="middle" fill="currentColor" font-size="11">③ PT 조회: VPN 2 → PFN 3</text>
<text x="350" y="164" text-anchor="middle" fill="currentColor" font-size="11">tlbwr(VPN2, PFN3) 시도</text>
<line x1="450" y1="170" x2="586" y2="190" stroke="#d9534f" stroke-width="2" marker-end="url(#C0B-arrow2)"/>
<text x="475" y="168" fill="currentColor" font-size="11">trap!</text>
<rect x="520" y="192" width="170" height="34" rx="5" fill="none" style="stroke:var(--accent)" stroke-width="2"/>
<text x="605" y="207" text-anchor="middle" fill="currentColor" font-size="11">④ pmap: PFN3 → MFN6</text>
<text x="605" y="221" text-anchor="middle" fill="currentColor" font-size="11">HW TLB ← VPN2 → MFN6</text>
<line x1="590" y1="232" x2="354" y2="245" stroke="currentColor" marker-end="url(#C0B-arrow2)"/>
<text x="360" y="262" fill="currentColor" font-size="11">⑤ 게스트: return-from-trap</text>
<line x1="350" y1="270" x2="586" y2="282" stroke="#d9534f" stroke-width="2" marker-end="url(#C0B-arrow2)"/>
<text x="475" y="270" fill="currentColor" font-size="11">trap!</text>
<line x1="590" y1="292" x2="114" y2="305" stroke="currentColor" marker-end="url(#C0B-arrow2)"/>
<text x="250" y="320" fill="currentColor" font-size="11">⑥ VMM이 진짜 rett → ⑦ 명령 재실행, TLB hit (MFN 6)</text>
</svg>
```

핵심 트릭은 4단계: **게스트가 넣으려는 VPN→PFN 대신 VMM이 VPN→MFN을 넣는다.** 이를 위해 VMM은 VM별 PFN→MFN 표를 갖고, 필요하면 그 "물리" 페이지가 **지금 machine 메모리에 있는지**(VMM이 디스크로 스왑했을 수도)까지 확인한다.

### 비싸다 → Disco의 software TLB

가상화된 TLB miss는 trap이 여러 번 왕복해 훨씬 비싸다. Disco는 VMM 안에 **software TLB**를 두었다: 게스트가 설치하려던 매핑을 전부 기록해 두고, 다음 TLB miss 때 거기서 찾으면 **게스트 핸들러를 부르지 않고** VPN→MFN을 바로 설치한다.

> **ASIDE — Hypervisor와 HW-managed TLB**
> x86처럼 HW가 페이지 테이블을 walk하면 VMM이 TLB miss마다 끼어들 기회가 없다. 대신 VMM은 게스트 OS의 페이지 테이블 변경을 **감시**하고(보통 게스트 페이지 테이블 페이지를 쓰기 금지로 만들어 쓰기를 trap), **VA → MA를 직접 담은 shadow page table**을 유지한다. 게스트가 자기 페이지 테이블 베이스 레지스터(CR3 등)를 바꾸려 하면 VMM은 대신 해당 shadow 테이블을 설치한다. HW는 shadow 테이블로 신나게 변환하고 게스트는 전혀 모른다.

### 원문 밖 보충: shadow paging vs nested paging

Intel EPT, AMD NPT, ARM **stage-2 translation**은 HW가 **게스트 페이지 테이블(VA→IPA)과 VMM 페이지 테이블(IPA→PA)을 둘 다** walk한다. shadow 테이블 동기화(게스트 PTE 쓰기마다 trap)가 사라지는 대신, TLB miss 한 번의 walk가 길어진다.

```text
4단계 게스트 페이지 테이블 × 4단계 nested 테이블 (x86-64, ARM64 4 KB 기준)
  게스트 PTE 각 단계(4번)의 주소가 "게스트 물리 주소"라서, 읽기 전에 각각 nested walk(4번) 필요
  + 최종 데이터 주소도 nested walk(4번)
  메모리 참조 수 = 4 × (4 + 1) + 4 = 24   (네이티브는 4)
→ 그래서 TLB와 page-walk cache, 큰 페이지(2 MB/1 GB)가 가상화 성능에 결정적이다.
```

## 6. 정보 격차 (B.5)

OS가 앱의 의도를 잘 모르듯, VMM도 게스트 OS의 의도를 잘 모른다 — **information gap**.

- **Idle loop**: 할 일이 없는 OS는 `while (1) ;` 로 돌며 인터럽트를 기다리기도 한다. 기계를 혼자 쓴다면 문제없지만, VMM 위에서는 한 OS가 헛돌고 있는 동안 **다른 OS에 CPU를 줘야** 한다. VMM은 그걸 모른다.
- **이중 zeroing**: 보안 때문에 VMM이 OS에 페이지를 줄 때 zero하고, OS도 프로세스에 줄 때 또 zero한다 → **두 번** 지운다. Disco 저자들은 마땅한 해법이 없어 게스트 OS(IRIX)를 고쳐 "VMM이 이미 지운 페이지는 다시 안 지우게" 했다.

> **ASIDE — Para-virtualization**
> 게스트 OS를 고칠 수 있다면(경쟁사 OS가 아니라면) VMM과 협력하도록 수정해 훨씬 효율적으로 돌 수 있다. 이것이 para-virtualization(용어는 Denali, 2002). Xen(2003)은 잘 설계된 para-virtualized 시스템이 **가상화 없는 시스템에 가까운 효율**을 낼 수 있음을 보였다.

해법 두 갈래:

1. **추론(implicit information)**: 예) OS가 **저전력 모드로 들어가면** idle이라고 판단.
2. **명시적 협력(para-virtualization)**: OS를 고쳐 하이퍼콜로 알려 줌. 배포는 어렵지만 효과적.

> **TIP — 암묵적 정보를 활용하라 (Use Implicit Information)**
> 계층 간 인터페이스를 바꾸기 어려울 때, 관찰로 다른 계층의 상태를 추론할 수 있다. 예: 블록 장치가 그 위 파일 시스템의 동작(파일 삭제 등)을 추론하는 semantically-smart disk, 앱이 OS 페이지 캐시에 어떤 페이지가 있는지 확률적으로 알아내는 gray-box 기법.

### 원문 밖 보충: 현대 HW가 메운 격차들

- **Idle loop** → ARM의 `WFI`/x86의 `HLT`를 **trap하도록 설정**(ARM `HCR_EL2.TWI`, Intel VMX의 HLT exiting)하면 게스트가 쉬는 순간 VMM이 다른 vCPU를 스케줄한다. 원문의 "저전력 모드 진입으로 추론"이 아키텍처 기능이 된 것.
- **메모리 회수** → VMware의 **balloon driver**(게스트 안에 드라이버를 넣어 게스트 스스로 메모리를 내놓게 함, Waldspurger 2002) — para-virtual 협력의 대표 사례.
- **I/O** → 원문이 다루지 않은 부분. 에뮬레이션(느림) → para-virtual 장치(**virtio**) → **장치 직접 할당**(IOMMU + SR-IOV)으로 발전.

## 7. 요약 (B.6)

VMM은 **limited direct execution을 확장**해, HW가 핵심 이벤트(trap)에서 VMM에 제어를 넘기게 만들고, 그 사이에 기계 자원 배분을 완전히 통제하면서도 OS가 필요로 하는 환상을 유지한다.

OS와 VMM 모두 HW를 가상화하지만 결정적 차이가 있다:

| | OS | VMM |
|---|---|---|
| 위에 보여 주는 인터페이스 | 새 추상화(프로세스, 파일, 소켓) | **HW와 똑같은** 인터페이스 |
| 목표 | HW를 쓰기 쉽게 | 여러 OS가 투명하게 공유 |
| 예 | Linux, macOS | ESXi, KVM, Xen |

원문이 다루지 않은 주제: I/O 가상화, **hosted** 구성(기존 OS 위에서 도는 VMM, VMware Workstation), 메모리 overcommit, 그리고 Intel·AMD의 HW 가상화 지원.

## 8. 직접 해보기

이 부록에는 OSTEP 시뮬레이터가 없다. 그래서 **trap-and-emulate 장난감 VMM**을 C로 짰다. 실제 하이퍼바이저 API(macOS `Hypervisor.framework`)는 entitlement와 코드 서명이 필요해 이 노트 범위를 넘으므로, 메커니즘만 정확히 재현하는 시뮬레이터로 대신한다.

`code/C0B_toy_vmm.c` — SW-managed TLB(4엔트리) + Figure B.1의 매핑으로:

- A) 네이티브 OS의 TLB miss 처리 (Table B.4)
- B) VMM 위에서: TLB miss를 게스트로 반사, 게스트의 `tlbwr`(VPN→PFN)를 trap으로 가로채 VPN→MFN 설치, 게스트의 `rett`도 trap (Table B.5)
- C) machine switch로 TLB가 비워지는 상황을 10번 반복 — Disco의 software TLB 유무 비교
- D) HW-walked TLB용 shadow page table 합성

```c
/*
 * C0B_toy_vmm.c
 * OSTEP 부록 B (Virtual Machine Monitors) — trap-and-emulate 장난감 시뮬레이터
 *
 * 하드웨어: software-managed TLB (MIPS 스타일, 4 엔트리), 모드 = USER / GUEST_KERNEL / VMM.
 *   - TLB 쓰기(tlbwr)는 privileged 명령 → VMM 모드가 아니면 trap.
 *   - 게스트 OS는 "자기가 커널" 이라고 믿지만 실제로는 덜 특권적인 모드에서 돈다.
 *
 * 메모리 매핑은 책 Figure B.1 그대로:
 *   게스트 OS page table : VPN0->PFN10, VPN2->PFN3, VPN3->PFN8  (VPN1은 invalid)
 *   VMM pmap (PFN->MFN)  : PFN3->MFN6,  PFN8->MFN10, PFN10->MFN5
 *
 * 실험
 *   A) native: OS가 직접 하드웨어 위에서 TLB miss 처리
 *   B) virtualized: VMM이 TLB miss를 받아 게스트 핸들러로 반사(reflect),
 *      게스트의 tlbwr(VPN->PFN)를 trap으로 가로채 VPN->MFN 으로 바꿔 설치 (Table B.5)
 *   C) B + Disco식 VMM "software TLB" (machine switch 후 TLB가 비었을 때 효과)
 *   D) hardware-walked TLB를 위한 shadow page table 합성 (VPN->MFN)
 *
 * build: cc -Wall -Wextra -O0 code/C0B_toy_vmm.c -o .work/bin/C0B_toy_vmm
 */
#include <stdio.h>
#include <string.h>

#define NVPN 4
#define NPFN 16
#define TLBN 4
#define INVALID -1

static const int guest_pt[NVPN] = { 10, INVALID, 3, 8 };  /* VPN -> PFN */
static int pmap[NPFN];                                      /* PFN -> MFN */

struct tlbe { int valid, vpn, frame; };
static struct tlbe tlb[TLBN];
static int tlb_next;

/* 소프트웨어 TLB (Disco): VMM이 본 VPN->MFN 매핑 캐시 */
static int stlb[NVPN];

/* 통계 */
static int n_tlb_miss, n_vmm_traps, n_guest_handler, n_world_switch;
static int verbose;

#define LOG(...) do { if (verbose) printf(__VA_ARGS__); } while (0)

static void tlb_flush(void)
{
    memset(tlb, 0, sizeof tlb);
    tlb_next = 0;
}

static int tlb_lookup(int vpn)
{
    for (int i = 0; i < TLBN; i++)
        if (tlb[i].valid && tlb[i].vpn == vpn) return tlb[i].frame;
    return INVALID;
}

/* 진짜 하드웨어 TLB에 쓰는 동작 (privileged) */
static void hw_tlbwr(int vpn, int frame)
{
    tlb[tlb_next] = (struct tlbe){ 1, vpn, frame };
    tlb_next = (tlb_next + 1) % TLBN;
}

/* ---------------- A) native ---------------- */
static int native_access(int vpn)
{
    int f = tlb_lookup(vpn);
    if (f != INVALID) { LOG("    VPN%d: TLB hit -> PFN%d\n", vpn, f); return f; }
    n_tlb_miss++;
    LOG("    VPN%d: TLB miss -> trap to OS handler\n", vpn);
    n_guest_handler++;
    int pfn = guest_pt[vpn];
    if (pfn == INVALID) { LOG("      OS: VPN%d invalid -> segfault\n", vpn); return INVALID; }
    hw_tlbwr(vpn, pfn);                         /* OS가 커널 모드라 바로 설치 */
    LOG("      OS: tlbwr VPN%d->PFN%d, rett; retry -> hit\n", vpn, pfn);
    return pfn;
}

/* ---------------- B/C) virtualized ---------------- */
/* 게스트 OS가 tlbwr를 실행하면: 게스트는 특권이 없으므로 trap → VMM이 대신 처리 */
static void guest_tlbwr_traps(int vpn, int pfn)
{
    n_vmm_traps++;
    int mfn = pmap[pfn];
    LOG("      [trap] guest tried tlbwr VPN%d->PFN%d; VMM installs VPN%d->MFN%d\n",
        vpn, pfn, vpn, mfn);
    hw_tlbwr(vpn, mfn);
    stlb[vpn] = mfn;                            /* 소프트웨어 TLB에 기록 */
}

static int virt_access(int vpn, int use_stlb)
{
    int f = tlb_lookup(vpn);
    if (f != INVALID) { LOG("    VPN%d: TLB hit -> MFN%d\n", vpn, f); return f; }
    n_tlb_miss++;
    n_vmm_traps++;                              /* 하드웨어 trap은 항상 VMM으로 */
    LOG("    VPN%d: TLB miss -> trap to VMM\n", vpn);

    if (use_stlb && stlb[vpn] != INVALID) {
        hw_tlbwr(vpn, stlb[vpn]);
        LOG("      VMM: software-TLB hit, install VPN%d->MFN%d directly, rett\n",
            vpn, stlb[vpn]);
        return stlb[vpn];
    }
    /* 게스트의 TLB miss 핸들러로 "반사" (권한을 낮춰서 점프) */
    n_world_switch++;
    n_guest_handler++;
    LOG("      VMM: reflect to guest OS TLB handler (reduced privilege)\n");
    int pfn = guest_pt[vpn];
    if (pfn == INVALID) {
        LOG("      guest OS: VPN%d invalid -> deliver segfault to its process\n", vpn);
        n_vmm_traps++;                          /* 게스트의 rett도 trap */
        return INVALID;
    }
    guest_tlbwr_traps(vpn, pfn);
    n_vmm_traps++;                              /* 게스트가 rett 실행 → 또 trap */
    LOG("      [trap] guest rett; VMM does real return-from-trap; retry -> hit\n");
    return pmap[pfn];
}

static void reset_stats(void)
{
    n_tlb_miss = n_vmm_traps = n_guest_handler = n_world_switch = 0;
}

static void stats(const char *name)
{
    printf("  -> %-32s TLB misses=%d, traps into VMM=%d, guest handler runs=%d\n",
           name, n_tlb_miss, n_vmm_traps, n_guest_handler);
}

int main(void)
{
    for (int i = 0; i < NPFN; i++) pmap[i] = INVALID;
    pmap[3] = 6; pmap[8] = 10; pmap[10] = 5;   /* Figure B.1 */

    const int refs[] = { 0, 2, 3, 0, 2, 3, 1 };
    const int nrefs = (int)(sizeof refs / sizeof refs[0]);

    printf("Reference string (VPN): 0 2 3 0 2 3 1   (VPN1 is invalid)\n\n");

    /* A */
    printf("== A. Native OS on bare hardware ==\n");
    verbose = 1; tlb_flush(); reset_stats();
    for (int i = 0; i < nrefs; i++) native_access(refs[i]);
    stats("native");

    /* B */
    printf("\n== B. Same OS on a VMM (trap-and-emulate, Table B.5) ==\n");
    tlb_flush(); reset_stats();
    for (int i = 0; i < NVPN; i++) stlb[i] = INVALID;
    for (int i = 0; i < nrefs; i++) virt_access(refs[i], 0);
    stats("virtualized");

    /* C: machine switch가 TLB를 비운 뒤 다시 실행 */
    printf("\n== C. After a machine switch (TLB flushed), rerun refs 0 2 3 x10 ==\n");
    verbose = 0;
    const char *names[] = { "virtualized, no software TLB", "virtualized + software TLB" };
    for (int use = 0; use <= 1; use++) {
        reset_stats();
        for (int i = 0; i < NVPN; i++) stlb[i] = INVALID;
        tlb_flush();
        for (int i = 0; i < 3; i++) virt_access(refs[i], 0);   /* 1회 워밍업: stlb 채워짐 */
        reset_stats();
        for (int round = 0; round < 10; round++) {
            tlb_flush();                        /* 다른 VM이 돌다 돌아옴 */
            for (int i = 0; i < 3; i++) virt_access(refs[i], use);
        }
        stats(names[use]);
    }

    /* D: shadow page table */
    printf("\n== D. Shadow page table for a hardware-walked TLB ==\n");
    printf("  VPN | guest PT (VPN->PFN) | VMM pmap (PFN->MFN) | shadow PT (VPN->MFN)\n");
    for (int v = 0; v < NVPN; v++) {
        int pfn = guest_pt[v];
        if (pfn == INVALID)
            printf("   %d  |       invalid       |          -          |   invalid\n", v);
        else
            printf("   %d  |       PFN %2d        |       MFN %2d        |   MFN %2d\n",
                   v, pfn, pmap[pfn], pmap[pfn]);
    }
    printf("  (HW walks only the shadow PT; VMM write-protects the guest PT so every\n"
           "   guest PTE update traps and the VMM can patch the shadow entry.)\n");
    return 0;
}
```

```text
cc -Wall -Wextra -O0 code/C0B_toy_vmm.c -o .work/bin/C0B_toy_vmm
.work/bin/C0B_toy_vmm
```

실제 출력 (경고 0개):

```text
Reference string (VPN): 0 2 3 0 2 3 1   (VPN1 is invalid)

== A. Native OS on bare hardware ==
    VPN0: TLB miss -> trap to OS handler
      OS: tlbwr VPN0->PFN10, rett; retry -> hit
    VPN2: TLB miss -> trap to OS handler
      OS: tlbwr VPN2->PFN3, rett; retry -> hit
    VPN3: TLB miss -> trap to OS handler
      OS: tlbwr VPN3->PFN8, rett; retry -> hit
    VPN0: TLB hit -> PFN10
    VPN2: TLB hit -> PFN3
    VPN3: TLB hit -> PFN8
    VPN1: TLB miss -> trap to OS handler
      OS: VPN1 invalid -> segfault
  -> native                           TLB misses=4, traps into VMM=0, guest handler runs=4

== B. Same OS on a VMM (trap-and-emulate, Table B.5) ==
    VPN0: TLB miss -> trap to VMM
      VMM: reflect to guest OS TLB handler (reduced privilege)
      [trap] guest tried tlbwr VPN0->PFN10; VMM installs VPN0->MFN5
      [trap] guest rett; VMM does real return-from-trap; retry -> hit
    VPN2: TLB miss -> trap to VMM
      VMM: reflect to guest OS TLB handler (reduced privilege)
      [trap] guest tried tlbwr VPN2->PFN3; VMM installs VPN2->MFN6
      [trap] guest rett; VMM does real return-from-trap; retry -> hit
    VPN3: TLB miss -> trap to VMM
      VMM: reflect to guest OS TLB handler (reduced privilege)
      [trap] guest tried tlbwr VPN3->PFN8; VMM installs VPN3->MFN10
      [trap] guest rett; VMM does real return-from-trap; retry -> hit
    VPN0: TLB hit -> MFN5
    VPN2: TLB hit -> MFN6
    VPN3: TLB hit -> MFN10
    VPN1: TLB miss -> trap to VMM
      VMM: reflect to guest OS TLB handler (reduced privilege)
      guest OS: VPN1 invalid -> deliver segfault to its process
  -> virtualized                      TLB misses=4, traps into VMM=11, guest handler runs=4

== C. After a machine switch (TLB flushed), rerun refs 0 2 3 x10 ==
  -> virtualized, no software TLB     TLB misses=30, traps into VMM=90, guest handler runs=30
  -> virtualized + software TLB       TLB misses=30, traps into VMM=30, guest handler runs=0

== D. Shadow page table for a hardware-walked TLB ==
  VPN | guest PT (VPN->PFN) | VMM pmap (PFN->MFN) | shadow PT (VPN->MFN)
   0  |       PFN 10        |       MFN  5        |   MFN  5
   1  |       invalid       |          -          |   invalid
   2  |       PFN  3        |       MFN  6        |   MFN  6
   3  |       PFN  8        |       MFN 10        |   MFN 10
  (HW walks only the shadow PT; VMM write-protects the guest PT so every
   guest PTE update traps and the VMM can patch the shadow entry.)
```

읽는 법:

- **A vs B**: 같은 참조열에서 TLB miss 수는 4번으로 같다. 하지만 네이티브는 OS 핸들러 4번이면 끝, 가상화에서는 **VMM으로의 trap이 11번**(miss 4번 + 게스트 `tlbwr` 3번 + 게스트 `rett` 4번). 유효한 miss 하나당 trap 3번 — Table B.5의 2·4·6단계 그대로다.
- 설치된 TLB 엔트리가 **VPN0→MFN5, VPN2→MFN6, VPN3→MFN10** — 게스트는 PFN 10/3/8을 넣었다고 믿지만 실제 HW TLB에는 machine frame이 들어갔다.
- **C**: machine switch마다 TLB가 비는 상황(VM이 번갈아 돌 때 흔함)에서 30번의 miss가 software TLB 없이 trap 90번 + 게스트 핸들러 30번, software TLB가 있으면 **trap 30번 + 게스트 핸들러 0번**. Disco가 software TLB를 넣은 이유다.
- **D**: shadow 테이블은 단순히 `pmap[guest_pt[vpn]]` 합성이다. 어려운 부분은 합성이 아니라 **동기화** — 게스트가 PTE를 바꿀 때마다 알아채야 한다(쓰기 보호 trap). nested paging이 HW로 없앤 비용이 바로 이것.

## 9. 펌웨어 엔지니어의 눈으로

- **ARM 예외 레벨로 이 장을 다시 읽기.** EL0(앱) / EL1(OS 커널) / EL2(hypervisor) / EL3(secure monitor, TrustZone). Disco가 MIPS supervisor mode로 했던 "OS를 한 단계 낮추기"를 ARM은 처음부터 EL1/EL2로 분리해 두었다. `HCR_EL2`의 비트로 어떤 연산을 trap할지(WFI, 시스템 레지스터 접근, 페이지 테이블 레지스터 쓰기 등) 고르는 것이 이 장의 trap-and-emulate를 HW로 구현한 형태다.
- **Stage-2 변환 = 이 장의 "VMM 페이지 테이블".** VA→IPA(게스트 stage-1)→PA(stage-2, `VTTBR_EL2`). 디바이스 쪽에는 **SMMU/IOMMU**의 stage-2가 같은 역할을 해서, 패스스루된 장치의 DMA도 VM의 machine 메모리 밖으로 못 나간다. Apple SoC의 **DART**도 장치별 IOMMU로 DMA 범위를 제한한다 — 다만 DART는 장치 격리 장치이지 VM용 2단계 변환을 위한 것은 아니다.
- **모바일·자동차 SoC의 하이퍼바이저.** Android의 **pKVM**(protected KVM)은 EL2에서 돌며 호스트 Android 커널로부터도 보호되는 VM(pVM)을 만든다. Qualcomm은 **Gunyah** 하이퍼바이저를, 자동차 SoC(NVIDIA DRIVE 등)는 안전 등급이 다른 OS를 분리하려고 Type-1 하이퍼바이저를 쓴다. "같은 칩에서 RTOS와 Linux를 격리"는 펌웨어 면접 단골 주제다.
- **Apple의 격리 방식은 결이 다르다(정확히 구분할 것).** Apple Silicon의 SEP(Secure Enclave), AOP, ANE, GPU 등 **코프로세서는 별도 코어에서 자기 펌웨어**를 돌린다 — VMM으로 한 코어를 나누는 게 아니라 **물리적으로 다른 프로세서 + 메일박스 + DART**로 격리한다. 한편 최근 Apple 플랫폼은 XNU의 페이지 테이블 변경을 **SPTM**(Secure Page Table Monitor)이라는 더 높은 권한의 모니터를 통해서만 하게 만들었다고 Apple 플랫폼 보안 문서에 공개되어 있다. "커널이 페이지 테이블을 직접 못 쓰고 상위 모니터에 요청한다"는 점에서 Xen의 para-virtual MMU, 이 장의 "VMM이 게스트 페이지 테이블 변경을 감시"와 같은 구조다. **Exclave**는 XNU 밖에서 도는 격리 실행 영역으로, 세부 구현(어떤 권한 레벨·커널에서 도는지)은 공개 문서가 제한적이라 면접에서는 "추정"과 "공개 사실"을 분리해서 말하는 게 안전하다. Mac에서 VM을 돌리는 공식 경로는 ARM EL2 가상화를 쓰는 `Hypervisor.framework` / `Virtualization.framework`다.
- **AI 가속기 가상화.** 데이터센터 GPU는 SR-IOV / MIG(Multi-Instance GPU) / vGPU로 한 장치를 여러 VM에 나눈다. 이 장의 교훈이 그대로 적용된다: 커맨드 큐 doorbell은 VM에 직접 매핑(빠른 경로, direct execution), 장치 전역 설정은 hypervisor/호스트 드라이버가 가로챔(trap-and-emulate), 장치의 DMA는 IOMMU stage-2로 격리(memory virtualization).
- **SSD 쪽 대응.** NVMe **SR-IOV**의 VF(Virtual Function)와 네임스페이스는 컨트롤러 FW가 하는 "장치 수준 가상화"다. VF마다 독립된 큐·네임스페이스를 주고 PF(관리 함수)만 전역 설정을 바꾸게 하는 것은 VMM의 특권 분리와 같은 설계다.

## 10. 면접 질문

### Q1. trap-and-emulate 가상화가 성립하는 조건은? x86은 왜 처음에 어려웠나?
<details>
<summary>답 보기</summary>

- Popek–Goldberg 조건: **모든 sensitive 명령(기계 상태를 바꾸거나 상태에 따라 동작이 달라지는 명령)이 privileged 명령**이어야 한다. 그래야 낮은 모드의 게스트가 실행할 때 반드시 trap이 나고 VMM이 흉내 낼 수 있다.
- 옛 x86에는 trap 없이 조용히 다르게 동작하는 명령이 있었다(예: `POPF`가 유저 모드에서 IF 변경을 무시, `SGDT` 등은 특권 없이 실행 가능).
- 해결: VMware의 **binary translation**, Xen의 **para-virtualization**, 그리고 **Intel VT-x / AMD-V**(root/non-root 모드, VM exit)라는 HW 지원.

</details>

### Q2. shadow page table과 nested paging(EPT/NPT/stage-2)을 비교하라.
<details>
<summary>답 보기</summary>

- **Shadow**: VMM이 VA→MA 테이블을 직접 유지, HW는 그것만 walk. TLB miss 비용은 네이티브와 같지만, 게스트 PTE 변경마다 **쓰기 보호 trap으로 동기화**해야 해서 fork/exec·페이지 폴트가 많은 워크로드에서 비싸다. 메모리도 추가로 든다.
- **Nested**: HW가 게스트 테이블(VA→GPA)과 nested 테이블(GPA→HPA)을 **둘 다 walk**. 동기화 trap 없음, VMM 단순. 대신 TLB miss walk가 최악 **24번 메모리 참조**(4단계×4단계).
- 완화: 큰 페이지, page-walk cache, VPID/ASID+VMID 태그로 VM 전환 시 TLB flush 회피.

</details>

### Q3. VMM 위에서 시스템 콜 하나가 처리되는 흐름을 설명하라.
<details>
<summary>답 보기</summary>

- 프로세스의 trap 명령 → HW는 **VMM의** trap 핸들러로 점프(진짜 특권 모드).
- VMM은 게스트 OS가 부팅 때 등록하려던 trap 핸들러 주소를 알고 있으므로, **권한을 낮춰 그 핸들러로 반사**.
- 게스트 OS가 시스템 콜 처리 후 return-from-trap(특권 명령) → **다시 trap** → VMM이 진짜 return-from-trap으로 유저 모드 복귀.
- 결과: trap 2번 추가. 현대 HW(VT-x 등)에서는 게스트 내부 시스템 콜은 VM exit 없이 게스트 커널로 바로 가도록 설계되어 이 오버헤드가 거의 없다.

</details>

### Q4. 정보 격차(information gap)의 예와 해결책은?
<details>
<summary>답 보기</summary>

- 예: 게스트의 **idle loop**(VMM은 CPU를 낭비하는지 모름), **이중 zeroing**, 게스트가 안 쓰는 메모리를 VMM이 모름, 게스트의 스핀락 보유 vCPU를 VMM이 선점(lock-holder preemption).
- 해결: **추론**(저전력 진입, WFI/HLT trap, PAUSE-loop exiting으로 스핀 감지), **para-virtualization**(하이퍼콜, virtio, balloon driver, PV spinlock).
- 트레이드오프: 추론은 OS 수정 불필요하지만 부정확, para-virtualization은 정확하지만 배포가 어렵다.

</details>

### Q5. 같은 SoC에서 안전 RTOS와 Linux를 격리하려면 어떤 HW 기능이 필요한가?
<details>
<summary>답 보기</summary>

- **CPU**: EL2 하이퍼바이저(또는 코어를 정적으로 나누는 partitioning hypervisor), 필요시 TrustZone(EL3/S-EL1).
- **메모리**: stage-2 변환으로 각 OS의 RAM 영역 고정.
- **장치/DMA**: **IOMMU/SMMU** stage-2로 각 OS 장치의 DMA를 자기 영역으로 제한. 공유 장치는 hypervisor가 중재하거나 para-virtual 장치로.
- **인터럽트**: GIC 가상화(가상 인터럽트 주입), 실시간 OS 쪽 인터럽트 지연 보장.
- **실시간성**: 캐시·메모리 대역폭 간섭 → 캐시 파티셔닝, QoS 레귤레이션. 이 부분은 MMU만으로 해결되지 않는다는 점이 면접 포인트.

</details>

## 11. 자가 점검 & 숙제

### 퀴즈

**Q1.** Figure B.1 매핑에서 게스트 프로세스가 VA `0x3010`(4 KB 페이지)을 읽으면 machine 주소는?
<details>
<summary>정답</summary>

VPN 3, offset 0x010 → 게스트: PFN 8 → "물리" 0x8010 → VMM: PFN 8 → MFN 10 = 0xA → machine 주소 **0xA010**.

</details>

**Q2.** VMM이 machine switch 때 저장해야 하지만 프로세스 context switch에서는 저장하지 않는 것은?
<details>
<summary>정답</summary>

**특권 HW 상태**: 페이지 테이블 베이스 레지스터(게스트가 설정한 값), trap 벡터 주소, 인터럽트 마스크/상태, TLB 관련 상태, 타이머 설정 등 — 게스트 OS가 "자기 것"이라 믿는 모든 시스템 레지스터.

</details>

**Q3.** HW-managed TLB에서 VMM은 왜 SW TLB 방식을 쓸 수 없고 shadow page table이 필요한가?
<details>
<summary>정답</summary>

HW가 TLB miss 때 페이지 테이블을 **직접 walk**하므로 VMM이 miss마다 끼어들 기회가 없다. 그러니 HW가 walk할 테이블 자체를 **VA→MA로 미리 만들어 둔 것**(shadow)으로 바꿔치기해야 한다.

</details>

**Q4.** 이 노트의 토이 VMM 실험 C에서 software TLB가 trap 수를 90에서 30으로 줄였다. 남은 30번은 왜 없앨 수 없나?
<details>
<summary>정답</summary>

SW-managed TLB에서는 **모든 TLB miss가 HW trap**이고, 진짜 특권 모드인 VMM이 받아야 한다. software TLB는 그 뒤의 게스트 핸들러 왕복(반사, tlbwr trap, rett trap)을 없앨 뿐 첫 trap은 없앨 수 없다. 없애려면 TLB miss를 HW가 처리해야 한다(HW page walk + nested paging).

</details>

### 꼭 해볼 것 (원문에 Homework 없음 — 대신)

- 토이 VMM에 **게스트가 PTE를 바꾸는 이벤트**(예: VPN 1을 PFN 4에 매핑)를 추가하고, shadow 테이블을 갱신하는 "쓰기 보호 trap" 경로를 구현해 보자 — shadow paging의 동기화 비용 체감.
- Disco 논문(Bugnion et al., SOSP '97)의 software TLB와 IRIX 수정 부분, Adams & Agesen(ASPLOS '06)의 "HW 지원이 생각보다 이득이 작았다"는 결론 부분을 읽어 보자.
- ARM 아키텍처 매뉴얼에서 `HCR_EL2`의 TWI/TWE, TVM 비트를 찾아, 이 장의 어떤 trap-and-emulate 단계에 대응하는지 표로 정리해 보자.

## 12. 다음으로

- 같은 아이디어의 OS 버전: [Ch.06 제한적 직접 실행](2026-09-30_C06_limited_direct_execution.md)(trap, 커널 모드), [Ch.19 TLB](2026-09-30_C19_tlb.md)(SW-managed TLB, ASID), [Ch.23 VAX/VMS](2026-09-30_C23_vax_vms.md)(보호 비트로 trap 받아 흉내 내기).
- 다음 부록 노트: [부록 D 모니터](2026-09-30_C0D_monitors.md), [부록 I Flash SSD](2026-09-30_C0I_flash_ssd.md).
