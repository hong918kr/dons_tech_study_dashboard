# F5. 유저 모드 격리 — 한 태스크의 버그가 비행 제어를 죽이지 못하게

> **이 노트를 다 읽으면**: Zephyr의 memory partition · memory domain · 커널 객체 권한 · 앱 정의 시스템 콜을 직접 짜서 "누가 무엇을 만질 수 있나"를 설계할 수 있다 · FreeRTOS-MPU restricted 태스크에 리전과 ACL을 주고, MemManage 폴트가 난 태스크만 지우고 다시 만드는 감독(supervisor) 구조를 짤 수 있다 · 격리의 비용(앱 수준 처리량, RAM 정렬 낭비)을 직접 잰 숫자로 말하고 배치 · 경계 배치로 줄이는 법을 안다 · 드론 FC · 로봇 관절 컨트롤러에서 무엇을 user 모드로 보낼지와 재시작 전략을 설계할 수 있다
> **왜 필요한가**: 국방 · 차량 · 드론 회사 면접에서 "How would you keep a buggy telemetry parser from taking down the flight controller?", "What does Zephyr userspace give you and what does it cost?"가 나온다. 안전 인증(ISO 26262 · DO-178C)의 "freedom from interference" 근거, 보안 위협 모델의 "한 태스크가 뚫려도 나머지는 지킨다"가 실제 요구사항으로 떨어질 때 바로 쓰는 지식이다
> **Don 기준 난이도**: MPU 레지스터 · 예외 · 링커 섹션 · 폴트 레지스터(CFSR · MMFAR)는 SSD 펌웨어에서 이미 안다 / 새로 배울 부분은 RTOS가 MPU 위에 쌓은 추상화(파티션 · 도메인 · 객체 권한 · ACL), 시스템 콜 검증 규칙(값 오류 vs 포인터 오류, TOCTOU), "죽은 태스크만 정리하고 계속 가는" 폴트 정책, 격리 경계를 어디에 그을지의 설계 감각
> **선행 노트**: B1(CONTROL.nPRIV · 특권), C8(커널 진입 경로 — FreeRTOS-MPU v2 빌드, Zephyr `__syscall` 생성 코드, 진입 비용을 이미 측정했다), C11 §5(ARINC 653 시간 분할), F4(MPU 리전 · 정렬 · subregion — 레지스터 수준은 F4), EL L6(Linux 런타임 보안 — 비교용). 이어지는 노트: F6(TrustZone-M), F9(MMU를 쓰는 RTOS), G4(FreeRTOS-MPU 실무), O(안전 · 분할)

---

## 0. 큰 그림 — 이게 왜 필요한가

### 0.1 직관 — 같은 방을 쓰는 다섯 명

기본 FreeRTOS · Zephyr에서 모든 태스크는 **한 주소 공간, 같은 특권**으로 돈다(C8 §1). 비유하면 다섯 사람이 칸막이 없는 한 방에서 일하는 것이다. 비행 제어 담당, 무선 패킷 파서 담당, 페이로드 통신 담당, 외주 업체가 준 라이브러리, 그리고 커널(방의 관리인)이 다 같은 책상을 쓴다. 파서 담당이 실수로 책상 위 서류를 쏟으면(버퍼 오버런) 비행 제어 담당의 서류(자세 추정 상태)와 관리인의 장부(TCB · ready list)가 같이 젖는다. 기체가 떨어진다.

유저 모드 격리는 방에 **칸막이와 열쇠**를 다는 일이다.

- 칸막이 = **MPU**: 각 태스크는 자기 칸(스택 + 허락받은 메모리)만 만질 수 있다. 칸 밖을 만지면 하드웨어가 즉시 MemManage 폴트를 낸다.
- 열쇠 = **특권 레벨 + 시스템 콜**: 관리인(커널)에게 일을 시키려면 창구(SVC)로 가서 신청서를 내야 하고, 관리인은 신청서(인자)를 검사한다(C8).
- 사고 처리 규정 = **폴트 정책**: 한 칸에서 사고가 나면 그 칸 사람만 내보내고 새 사람을 앉힌다(재시작). 나머지는 계속 일한다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="165" y="20" font-size="13" text-anchor="middle" font-weight="bold">격리 없음 (기본 빌드)</text>
<text x="510" y="20" font-size="13" text-anchor="middle" font-weight="bold">유저 모드 격리</text>
<rect x="10" y="32" width="310" height="214" rx="6" fill="#888" fill-opacity="0.08" stroke="#888"/>
<text x="165" y="52" font-size="12" text-anchor="middle">한 주소 공간 · 전부 특권</text>
<rect x="22" y="62" width="140" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="92" y="84" font-size="12" text-anchor="middle">비행 제어 루프</text>
<rect x="170" y="62" width="142" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="241" y="84" font-size="12" text-anchor="middle">텔레메트리 파서</text>
<rect x="22" y="104" width="140" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="92" y="126" font-size="12" text-anchor="middle">페이로드 통신</text>
<rect x="170" y="104" width="142" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="241" y="126" font-size="12" text-anchor="middle">서드파티 라이브러리</text>
<rect x="22" y="146" width="290" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="167" y="168" font-size="12" text-anchor="middle">커널 데이터 · TCB · ready list</text>
<rect x="22" y="188" width="290" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="167" y="210" font-size="12" text-anchor="middle">드라이버 상태 · 주변장치 레지스터</text>
<line x1="300" y1="96" x2="300" y2="186" stroke="#d0564a" stroke-width="1.6" stroke-dasharray="5,3" marker-end="url(#ah1)"/>
<text x="165" y="266" font-size="12" text-anchor="middle">파서 오버런 한 번 → 무엇이든 덮어쓸 수 있다</text>
<rect x="350" y="32" width="320" height="104" rx="6" fill="#3f9a6b" fill-opacity="0.08" stroke="#3f9a6b"/>
<text x="510" y="52" font-size="12" text-anchor="middle">특권 (supervisor 스레드 · 특권 태스크)</text>
<rect x="362" y="62" width="140" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="432" y="84" font-size="12" text-anchor="middle">비행 제어 루프</text>
<rect x="510" y="62" width="150" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="585" y="84" font-size="12" text-anchor="middle">커널 · 드라이버</text>
<text x="510" y="122" font-size="12" text-anchor="middle">감독 태스크: 죽은 태스크를 다시 만든다</text>
<line x1="350" y1="148" x2="670" y2="148" stroke="#d0564a" stroke-width="1.4" stroke-dasharray="6,4"/>
<text x="510" y="166" font-size="12" text-anchor="middle">MPU 경계 + 시스템 콜 검사</text>
<rect x="350" y="176" width="100" height="50" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="400" y="197" font-size="12" text-anchor="middle">파서</text>
<text x="400" y="215" font-size="12" text-anchor="middle">자기 파티션</text>
<rect x="460" y="176" width="100" height="50" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="510" y="197" font-size="12" text-anchor="middle">페이로드</text>
<text x="510" y="215" font-size="12" text-anchor="middle">자기 파티션</text>
<rect x="570" y="176" width="100" height="50" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="620" y="197" font-size="12" text-anchor="middle">서드파티</text>
<text x="620" y="215" font-size="12" text-anchor="middle">자기 파티션</text>
<line x1="400" y1="176" x2="400" y2="110" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah1)"/>
<text x="510" y="250" font-size="12" text-anchor="middle">파서 폴트 → 파서만 abort · 재시작</text>
<text x="510" y="268" font-size="12" text-anchor="middle">비행 제어 주기는 그대로 (§5 · §6.2 측정)</text>
<text x="340" y="304" font-size="12" text-anchor="middle">대가: 시스템 콜 진입 비용 · 컨텍스트 스위치마다 MPU 재설정 · v7-M의 2의 거듭제곱 정렬 낭비 · 설계 복잡도</text>
</svg>
```

그림 1 — 격리가 없는 빌드(왼쪽)에서는 파서의 오버런이 커널 데이터 · 드라이버 상태까지 닿는다. 격리 빌드(오른쪽)에서는 신뢰하기 어려운 코드만 MPU 경계 아래(user 모드)로 내리고, 각자 자기 파티션만 쓰게 한다. 폴트가 나면 그 태스크만 정리한다.

### 0.2 MCU에서 격리하는 세 가지 이유

| 이유 | 장면 | 격리가 주는 것 | 격리로 안 되는 것 |
|---|---|---|---|
| **혼합 중요도(mixed-criticality)** | 비행 제어(치명적)와 텔레메트리 · 로그 · 페이로드(비치명적)가 한 MCU에 | 낮은 중요도 코드가 높은 중요도 코드의 메모리를 못 깬다 = 공간 간섭 없음 | 시간 간섭(폭주 태스크가 CPU를 먹음)은 우선순위 · 시간 분할(C11 §5)로 따로 막아야 |
| **결함 격리(fault containment)** | 외부 입력 파서, 서드파티 라이브러리처럼 버그가 나기 쉬운 코드 | 폴트가 그 태스크 안에서 멈추고 재시작으로 복구 | 리전 **안쪽**의 손상(§6.2에서 직접 본다), 논리 오류(틀린 값을 정상 경로로 보냄) |
| **보안** | 원격 공격자가 파서 버그로 코드 실행을 얻음 | 키 · 부트 상태 · 다른 태스크 데이터를 못 읽고 못 쓴다, 커널에 엉뚱한 일을 못 시킨다(시스템 콜 검사) | 사이드 채널, 하드웨어 공격, 커널 자체의 버그 — TrustZone(F6) · 보안 부트(L)와 함께 간다 |

비용도 세 가지다. 이 노트에서 전부 직접 잰다.

- **시간**: 비특권 코드의 커널 API 호출은 SVC 경로가 된다(C8에서 호출 1번 약 6–10 us, QEMU icount 기준). 그리고 컨텍스트 스위치마다 MPU를 다시 설정한다. → §7에서 **앱 수준** 처리량으로 잰다.
- **메모리**: v7-M MPU는 리전 크기가 2의 거듭제곱이고 크기에 정렬되어야 한다(F4). 300 B짜리 버퍼가 512 B를 먹는다. 시스템 콜 전용 스택도 스레드마다 붙는다. → §7.3에서 v7-M vs v8-M로 잰다.
- **복잡도**: 누가 어떤 메모리 · 어떤 객체를 쓰는지 전부 적어 줘야 한다. 전역 변수 하나 읽는 데도 허락이 필요하다(C8 예제 6에서 이미 부딪혔다).

### 0.3 C8과 이 노트의 분담

C8은 **문(커널 진입 경로)** 자체를 다뤘다: FreeRTOS-MPU v2 빌드 만들기(링커 섹션 · 설정), `MPU_` 래퍼 → `svc` → Impl 경로, Zephyr `__syscall` 생성 코드, API 호출 한 번의 비용, 비특권 태스크가 하면 안 되는 일(시스템이 멈추는 것까지). 이 노트는 그 문 위에 **방 배치와 사고 처리 규정**을 짠다.

| 주제 | C8 (이미 함) | F5 (이 노트) |
|---|---|---|
| FreeRTOS-MPU 빌드 · 링커 스크립트 | 만들고 설명 | 그대로 재사용 (`init_array` 심볼만 추가) |
| SVC 진입 경로 · 호출 1회 비용 | gdb로 추적, 측정 | 참조만. 대신 **앱 수준 처리량**과 배치 효과 |
| Zephyr 커널 API의 `__syscall` 생성 코드 | `k_sem_give` | **앱이 정의하는** 시스템 콜을 처음부터 만든다 |
| 메모리 격리 | 기본 도메인에 파티션 1개 | **도메인 여러 개 · 공유 파티션 · `app_smem` 링커 · 정렬 비용** |
| 객체 권한 | `k_object_access_grant` 1번 | 권한 비트맵, `K_INHERIT_PERMS`, 객체가 아닌 포인터, `k_thread_user_mode_enter` |
| 위반했을 때 | 시스템이 멈춘다(기본 fatal 핸들러) | **그 스레드만 죽이고 계속 간다 + 감독이 재시작** |
| FreeRTOS ACL | grant 전후 | grant · **revoke**, 공유 리전 RW/RO, **리전 반올림 구멍** |

### 실습 환경 한 줄

QEMU `mps2-an385`(Cortex-M3, v7-M MPU 8리전) + Zephyr v4.4.2(`mps2/an385`, `CONFIG_USERSPACE=y`) + FreeRTOS V11.3.2(`GCC/ARM_CM3_MPU` 포트, MPU 래퍼 v2 — C8의 빌드를 복사), 정렬 비교만 `mps2/an521/cpu0`(Cortex-M33, v8-M MPU). 시간 수치는 QEMU `-icount shift=5`(FreeRTOS는 `ICOUNT=1`, 명령 1개 = 32 ns)에서 MPS2 FPGAIO COUNTER(0x40028018, 25 MHz, 1 count = 40 ns)로 쟀다. **QEMU icount 기준이고 실제 칩과 절대값은 다르다**(캐시 · 플래시 wait state · 버스 · 예외 스태킹 사이클을 QEMU가 모델링하지 않는다 — 특히 SVC 경로와 폴트 경로는 실칩에서 예외마다 최소 12 cycle 이상 더 든다). 같은 조건의 **상대 비교**로 읽는다. Zephyr는 QEMU를 직접 띄우고(`-semihosting-config enable=on`) 앱이 semihosting `SYS_EXIT`로 끝낸다.

```sh
#!/bin/bash
# F5: zrun.sh <app> <tag> [board] [west args...] — Zephyr 빌드 후 QEMU(icount shift=5, semihosting) 실행
A=/Users/donh/.rtos_scratch/F5/$1; T=$2; BRD=${3:-mps2/an385}; shift 3 2>/dev/null || shift $#
D=/var/tmp/rtos/F5/z_$T
/var/rtoslab/bin/rtos-zbuild $BRD $A $D "$@" || { echo BUILD FAILED; exit 1; }
case $BRD in
  mps2/an385) M="-machine mps2-an385 -cpu cortex-m3" ;;
  mps2/an521/cpu0) M="-machine mps2-an521 -cpu cortex-m33" ;;
esac
timeout 20 qemu-system-arm $M -nographic -monitor none -serial stdio -semihosting-config enable=on,target=native \
  -icount shift=5,align=off,sleep=off -kernel $D/zephyr/zephyr.elf 2>&1 | tr -d '\r'
```

Zephyr 예제들이 같이 쓰는 헤더. 핵심은 맨 아래의 **fatal 핸들러 재정의**다 — 이게 "한 스레드의 버그로 시스템이 멈추지 않게" 하는 첫 단추다(§5에서 자세히).

```c
/* F5 공용: QEMU 종료(semihosting SYS_EXIT), FPGAIO 카운터, "죽은 스레드만 정리" fatal 핸들러 */
#ifndef F5Z_H
#define F5Z_H
#include <zephyr/kernel.h>
#include <zephyr/fatal.h>
#define FPGAIO_COUNTER (*(volatile uint32_t *)0x40028018u)   /* 25 MHz, 1 tick = 40 ns (supervisor만 읽음) */
static inline void f5_exit(void)                               /* qemu -semihosting 필요 */
{
    register uint32_t r0 __asm__("r0") = 0x18, r1 __asm__("r1") = 0x20026;
    __asm volatile("bkpt 0xab" : : "r"(r0), "r"(r1) : "memory");
}
#ifndef F5_DEFAULT_FATAL
/* 기본 핸들러는 시스템을 멈춘다. 이 재정의는 panic이 아니면 return → 커널이 그 스레드만 abort 한다 */
void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
    ARG_UNUSED(esf);
    printk("[fatal] reason=%u thread=%s -> abort only this thread\n", reason, k_thread_name_get(k_current_get()));
    if (reason == K_ERR_KERNEL_PANIC) {
        k_fatal_halt(reason);
    }
}
#endif
#endif
```

`prj.conf`(예제 1–6 공통, 예제마다 한두 줄 추가):

```text
CONFIG_USERSPACE=y
CONFIG_PRINTK=y
CONFIG_LOG=y
CONFIG_LOG_MODE_MINIMAL=y
CONFIG_THREAD_NAME=y
CONFIG_MAIN_STACK_SIZE=2048
```

---

## 1. 격리를 세 축으로 나눠 보기

"태스크를 격리한다"는 말은 사실 세 가지 다른 일을 섞어 부른다. 셋을 나눠야 설계 회의에서 말이 엇갈리지 않는다.

| 축 | 질문 | 하드웨어 · 커널 장치 | 이 노트 |
|---|---|---|---|
| **공간(메모리)** | 이 태스크가 어느 주소를 읽고 쓸 수 있나 | MPU 리전, 특권 레벨(CONTROL.nPRIV, B1) | §2(Zephyr 파티션 · 도메인), §6(FreeRTOS 리전) |
| **객체(커널 자원)** | 이 태스크가 어느 세마포어 · 큐 · 스레드를 쓸 수 있나 | 시스템 콜 검사 — Zephyr 객체 권한 비트맵, FreeRTOS v2 ACL | §3, §4, §6.3 |
| **시간** | 이 태스크가 CPU를 얼마나 쓸 수 있나 | 우선순위, 시간 분할(ARINC 653식 창), 워치독 | C11 §5, E8 — 이 노트 범위 밖 |

그리고 두 가지 반응을 구분한다.

- **결함 방지**: 나쁜 접근을 **처음부터 못 하게** 막는다 — MPU 폴트, 시스템 콜 거절.
- **결함 복구**: 막은 다음 **시스템을 계속 돌린다** — 그 태스크만 정리 · 재시작, 상태를 안전하게 되돌림.

MPU 격리는 방지의 도구이고, 복구는 **앱이 설계해야 하는 정책**이다. Zephyr의 기본 정책은 "시스템 정지", FreeRTOS는 정책 자체가 없다(MemManage 핸들러를 포트가 제공하지 않는다 — §6.2에서 직접 짠다).

두 RTOS의 용어를 맞춰 놓는다.

| 개념 | Zephyr (v4.4.2) | FreeRTOS-MPU (V11.3.2, 래퍼 v2) |
|---|---|---|
| 비특권 실행 단위 | user 스레드 (`K_USER`, `k_thread_user_mode_enter`) | restricted 태스크 (`xTaskCreateRestricted`, 우선순위에 `portPRIVILEGE_BIT` 없음) |
| 특권 실행 단위 | supervisor 스레드 (기본) | 특권 태스크 (우선순위 값에 `portPRIVILEGE_BIT`를 비트 OR) |
| 메모리 허락 단위 | memory partition (시작 · 크기 · 속성) | `MemoryRegion_t` (`pvBaseAddress`, `ulLengthInBytes`, `ulParameters`) |
| 허락의 묶음 | memory domain (파티션 묶음, 스레드는 한 도메인 소속) | 태스크마다 `xRegions[3]` + 자기 스택 |
| 같은 메모리를 여럿이 | 같은 파티션을 여러 도메인에 넣는다 | 같은 리전을 여러 태스크의 `xRegions`에 준다 |
| 객체 권한 | 객체마다 스레드 권한 비트맵 (`k_object_access_grant`) | 태스크마다 ACL 비트맵 (`vGrantAccessToQueue` 등) |
| 권한 위반 | **kernel oops** → 스레드 종료 (기본 정책은 시스템 정지) | API가 조용히 실패 (`pdFAIL`/0) |
| 메모리 위반 | MPU FAULT → fatal 핸들러 | MemManage → **앱이 쓴** 핸들러 |


---

## 2. Zephyr userspace ① — 메모리: partition과 domain

### 2.1 직관 — 방과 열쇠 꾸러미

- **partition** = 방 하나. "0x20000840부터 32바이트, user RW" 같은 **연속 메모리 + 속성**. 구조체 `struct k_mem_partition { uintptr_t start; size_t size; k_mem_partition_attr_t attr; }`.
- **memory domain** = 열쇠 꾸러미. 파티션 여러 개의 묶음. **스레드는 정확히 한 도메인에 속하고**, user 스레드는 (자기 스택 + 자기 도메인의 파티션)만 만질 수 있다.
- 같은 방 열쇠를 여러 꾸러미에 넣을 수 있다 → **공유 메모리**.
- 도메인을 지정하지 않은 스레드는 만든 스레드(부모)의 도메인을 물려받는다. 맨 처음에는 모두 `k_mem_domain_default`(`mem_domain.h` 109행)에 있다.

SSD 펌웨어로 말하면, 코어마다 접근 가능한 SRAM 창을 버스 방화벽으로 나누던 것과 같다. 차이는 **스레드가 바뀔 때마다** 그 창을 다시 짠다는 것이다 — Zephyr는 컨텍스트 스위치 때 `z_arm_configure_dynamic_mpu_regions()`(`arch/arm/core/cortex_m/swap_helper.S` 222행에서 호출)로 새 스레드의 도메인 파티션 + 스택을 MPU에 올린다.

### 2.2 API — 정의, 배치, 묶기

**파티션 만들기 (두 가지)**

| 매크로 | 용도 | 크기 · 정렬은 누가 정하나 |
|---|---|---|
| `K_APPMEM_PARTITION_DEFINE(name)` + `K_APP_DMEM(name)` / `K_APP_BMEM(name)` | **전역 변수**를 파티션에 모은다. DMEM = 초기값 있는 변수(.data 성격), BMEM = 0 초기화(.bss 성격) | 빌드 시스템이 자동 (`gen_app_partitions.py` → 링커 스크립트 생성) |
| `K_MEM_PARTITION_DEFINE(name, start, size, attr)` | 주소가 이미 정해진 영역(MMIO 페이지, 특정 SRAM 뱅크) | 개발자가 직접 — v7-M이면 크기는 2의 거듭제곱, 시작은 크기 정렬 |

속성(v7-M, `include/zephyr/arch/arm/mpu/arm_mpu_v7m.h`): `K_MEM_PARTITION_P_RW_U_RW`(특권 RW, user RW — `K_APPMEM_PARTITION_DEFINE`의 기본), `K_MEM_PARTITION_P_RW_U_RO`, `K_MEM_PARTITION_P_RW_U_NA`, `K_MEM_PARTITION_P_RO_U_RO`, 그리고 각각의 `_NOCACHE` 변형. C8 예제 12에서 FPGAIO 카운터 페이지를 `K_MEM_PARTITION_P_RW_U_RO`로 user에게 읽기만 허락했다.

**도메인 API** (`include/zephyr/app_memory/mem_domain.h`, 전부 supervisor에서만 부른다)

- `int k_mem_domain_init(struct k_mem_domain *domain, uint8_t num_parts, struct k_mem_partition *parts[])`
- `int k_mem_domain_add_partition(domain, part)` / `int k_mem_domain_remove_partition(domain, part)`
- `int k_mem_domain_add_thread(domain, thread)` — 스레드를 이 도메인으로 **옮긴다**(이전 도메인에서 빠진다)
- `int k_mem_domain_deinit(domain)` — v4.4.2 헤더 148행에 있다

### 2.3 예제 1 — 파티션 3개, 도메인 2개: 공유는 되고 침범은 안 된다

무엇을 확인하나: A와 B가 `part_sh`를 같이 보고 각자의 private 파티션만 쓰는지, A가 B의 파티션이나 파티션 밖 전역을 건드리면 A만 죽고 B와 main은 계속 도는지.

```c
/* F5 예제 1: 파티션 3개, 도메인 2개 — A와 B는 shared만 같이 보고, 서로의 private는 못 본다 */
#include <zephyr/kernel.h>
#include <zephyr/app_memory/app_memdomain.h>
#include "f5z.h"
K_APPMEM_PARTITION_DEFINE(part_a);
K_APPMEM_PARTITION_DEFINE(part_b);
K_APPMEM_PARTITION_DEFINE(part_sh);
K_APP_DMEM(part_a) uint32_t a_private[8] = { 0xA0 };     /* 초기값 있음 → .data 쪽 */
K_APP_BMEM(part_b) uint32_t b_private[8];                /* 0 초기화 → .bss 쪽 */
K_APP_BMEM(part_sh) volatile uint32_t shared_seq;        /* 두 도메인에 다 들어가는 파티션 */
uint32_t kernel_side;                                    /* 어느 파티션에도 없는 보통 전역 */
static struct k_mem_domain dom_a, dom_b;
K_THREAD_STACK_DEFINE(st_a, 1024);
K_THREAD_STACK_DEFINE(st_b, 1024);
static struct k_thread th_a, th_b;

static void fn_a(void *p1, void *p2, void *p3)
{
    for (uint32_t i = 1; i <= 3; i++) { shared_seq = i; a_private[1]++; k_msleep(10); }
    printk("[A] wrote shared_seq up to 3, a_private[1]=%u\n", a_private[1]);
#if F5_BAD == 1
    printk("[A] BAD1: write b_private[0] (B's partition)\n");
    b_private[0] = 0xdead;
#elif F5_BAD == 2
    printk("[A] BAD2: read kernel_side (no partition)\n");
    printk("[A] kernel_side=%u\n", kernel_side);
#endif
    printk("[A] done\n");
}

static void fn_b(void *p1, void *p2, void *p3)
{
    for (int i = 0; i < 6; i++) { b_private[0] += shared_seq; k_msleep(10); }
    printk("[B] still alive, sum of shared_seq seen=%u\n", b_private[0]);
}

int main(void)
{
    struct k_mem_partition *pa[] = { &part_a, &part_sh }, *pb[] = { &part_b, &part_sh };
    printk("[main] max partitions per domain on this MPU = %d\n", arch_mem_domain_max_partitions_get());
    printk("[main] part_a  start=%#lx size=%u\n", part_a.start, part_a.size);
    printk("[main] part_b  start=%#lx size=%u\n", part_b.start, part_b.size);
    printk("[main] part_sh start=%#lx size=%u\n", part_sh.start, part_sh.size);
    k_mem_domain_init(&dom_a, ARRAY_SIZE(pa), pa);
    k_mem_domain_init(&dom_b, ARRAY_SIZE(pb), pb);
    k_tid_t a = k_thread_create(&th_a, st_a, K_THREAD_STACK_SIZEOF(st_a), fn_a, NULL, NULL, NULL, 5, K_USER, K_FOREVER);
    k_tid_t b = k_thread_create(&th_b, st_b, K_THREAD_STACK_SIZEOF(st_b), fn_b, NULL, NULL, NULL, 5, K_USER, K_FOREVER);
    k_thread_name_set(a, "A"); k_thread_name_set(b, "B");
    k_mem_domain_add_thread(&dom_a, a);
    k_mem_domain_add_thread(&dom_b, b);
    k_thread_start(a); k_thread_start(b);
    k_thread_join(a, K_FOREVER); k_thread_join(b, K_FOREVER);
    printk("[main] both threads ended, system still running (uptime %lld ms)\n", k_uptime_get());
    f5_exit();
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc '/Users/donh/.rtos_scratch/F5/zrun.sh zdom dom0 mps2/an385 -- -DEXTRA_CFLAGS=-DF5_BAD=0'
```

```text
           FLASH:       46768 B         4 MB      1.12%
             RAM:         19 KB         4 MB      0.46%
*** Booting Zephyr OS build v4.4.2 ***
[main] max partitions per domain on this MPU = 5
[main] part_a  start=0x20000880 size=32
[main] part_b  start=0x20000860 size=32
[main] part_sh start=0x20000840 size=32
[A] wrote shared_seq up to 3, a_private[1]=3
[A] done
[B] still alive, sum of shared_seq seen=15
[main] both threads ended, system still running (uptime 120 ms)
```

출력에서 볼 것:

- **공유가 된다**: B는 자기 도메인의 `part_sh`로 A가 쓴 `shared_seq`(1, 2, 3, 3, 3, 3)를 읽어 15를 더했다.
- **도메인당 파티션 최대 5개**: `arch_mem_domain_max_partitions_get()`은 하드웨어가 정한다. 소스(`arch/arm/core/mpu/arm_core_mpu.c` 352행)는 "MPU 리전 수 − 커널이 고정으로 쓰는 정적 리전 − 스레드 스택 리전 1개"를 돌려준다. 8리전 M3에서 5가 나왔으니 정적 리전이 2개라는 뜻이다. `mps2/an521/cpu0`(M33)에서도 같은 프로그램이 5를 찍었다. Kconfig `CONFIG_MAX_DOMAIN_PARTITIONS`의 기본값은 16이지만, 커널은 부팅 때 `max_partitions = arch_mem_domain_max_partitions_get()`(`kernel/mem_domain.c` 403행)로 하드웨어 한도를 쓴다.
- 파티션이 각각 32바이트다. `a_private[8]`은 32바이트라 딱 맞고, `shared_seq` 하나(4바이트)도 32바이트를 먹는다 — v7-M MPU 리전의 최소 크기가 32바이트다(F4).

이제 침범을 시킨다.

```sh
limactl shell elinux -- bash -lc 'for b in 1 2; do echo "######## F5_BAD=$b"; /Users/donh/.rtos_scratch/F5/zrun.sh zdom dom$b mps2/an385 -- -DEXTRA_CFLAGS=-DF5_BAD=$b | grep -v "FLASH\|RAM:\|part_\|max part"; done;
  arm-none-eabi-nm /var/tmp/rtos/F5/z_dom1/zephyr/zephyr.elf | grep -E " (b_private|kernel_side|a_private|shared_seq)$"; arm-none-eabi-nm /var/tmp/rtos/F5/z_dom2/zephyr/zephyr.elf | grep -E " (kernel_side)$"'
```

```text
######## F5_BAD=1
*** Booting Zephyr OS build v4.4.2 ***
[A] wrote shared_seq up to 3, a_private[1]=3
[A] BAD1: write b_private[0] (B's partition)
E: ***** MPU FAULT *****
E:   Data Access Violation
E:   MMFAR Address: 0x20000860
E: r0/a1:  0x00008189  r1/a2:  0x00000000  r2/a3:  0x0000dead
E: r3/a4:  0x20000860 r12/ip:  0x00006499 r14/lr:  0x00000771
E:  xpsr:  0x61000000
E: Faulting instruction address (r15/pc): 0x00000778
E: >>> ZEPHYR FATAL ERROR 19: Unknown error on CPU 0
E: Current thread: 0x20000c40 (A)
[fatal] reason=19 thread=A -> abort only this thread
[B] still alive, sum of shared_seq seen=15
[main] both threads ended, system still running (uptime 120 ms)
######## F5_BAD=2
*** Booting Zephyr OS build v4.4.2 ***
[A] wrote shared_seq up to 3, a_private[1]=3
[A] BAD2: read kernel_side (no partition)
E: ***** MPU FAULT *****
E:   Data Access Violation
E:   MMFAR Address: 0x200010d8
E: r0/a1:  0x0000818e  r1/a2:  0x00000000  r2/a3:  0x00000000
E: r3/a4:  0x200010d8 r12/ip:  0x0000649d r14/lr:  0x00000771
E:  xpsr:  0x61000000
E: Faulting instruction address (r15/pc): 0x00000774
E: >>> ZEPHYR FATAL ERROR 19: Unknown error on CPU 0
E: Current thread: 0x20000c40 (A)
[fatal] reason=19 thread=A -> abort only this thread
[B] still alive, sum of shared_seq seen=15
[main] both threads ended, system still running (uptime 120 ms)
20000880 D a_private
20000860 D b_private
20000840 D shared_seq
200010d8 B kernel_side
```

출력에서 볼 것:

- `MMFAR Address` 0x20000860 = `b_private`(nm), 0x200010d8 = `kernel_side`. 하드웨어가 정확히 그 주소에서 막았다. `r2/a3: 0x0000dead`는 쓰려던 값이다.
- **reason 19**는 `K_ERR_ARM_MEM_DATA_ACCESS`다. ARM 아키텍처 에러 코드는 `K_ERR_ARCH_START`(16)부터 `K_ERR_ARM_MEM_GENERIC`, `..._STACKING`, `..._UNSTACKING`, `..._DATA_ACCESS` 순이라(`include/zephyr/arch/arm/arch.h` 61–64행) 16 + 3 = 19다. 로그가 `Unknown error`라고 찍는 건 공용 `reason_to_str()`(`kernel/fatal.c`)이 아키텍처 코드를 모르기 때문이다.
- **A만 죽었다.** `[fatal]` 줄은 f5z.h의 재정의 핸들러가 찍었다. 핸들러가 return하자 커널이 A를 abort했고, B는 `still alive`, main은 `join`을 마치고 끝까지 갔다. C8 예제 13은 같은 종류의 위반에서 `Halting system`으로 **시스템 전체가 멈췄다** — 차이는 핸들러 하나다(§5).

### 2.4 `app_smem` — 파티션은 링커가 만든다

`K_APP_BMEM(part_b)`는 변수에 `data_smem_part_b_bss` 섹션 이름을 붙일 뿐이다(`app_memdomain.h` 40행). 진짜 배치는 빌드가 **두 번 링크하면서** 만든다: 1차 ELF에서 `gen_app_partitions.py`가 `data_smem_*` 섹션을 모두 찾고, 파티션마다 정렬 규칙을 넣은 링커 스크립트 조각(`app_smem_aligned.ld`)을 생성하고, 그걸로 최종 링크를 한다.

```sh
limactl shell elinux -- bash -lc 'D=/var/tmp/rtos/F5/z_dom0/zephyr; grep -v "^$" $D/include/generated/app_smem_aligned.ld | sed -n 34,46p;
  arm-none-eabi-nm -n -S $D/zephyr.elf | grep -E "_app_smem|z_data_smem_" | grep -E "size|part_start"'
```

```text
            /* Auto generated code do not modify */
            SMEM_PARTITION_ALIGN(z_data_smem_part_sh_bss_end - z_data_smem_part_sh_part_start);
            z_data_smem_part_sh_part_start = .;
            KEEP(*(data_smem_part_sh_data*))
    
            z_data_smem_part_sh_bss_start = .;
            KEEP(*(data_smem_part_sh_bss*))
    
            z_data_smem_part_sh_bss_end = .;
            SMEM_PARTITION_ALIGN(z_data_smem_part_sh_bss_end - z_data_smem_part_sh_part_start);
            z_data_smem_part_sh_part_end = .;
    
            /* Auto generated code do not modify */
00000000 A z_data_smem_part_a_bss_size
00000000 A z_data_smem_z_libc_partition_bss_size
00000000 A z_data_smem_z_malloc_partition_bss_size
00000004 A z_data_smem_part_sh_bss_size
00000020 A z_data_smem_part_a_part_size
00000020 A z_data_smem_part_b_bss_size
00000020 A z_data_smem_part_b_part_size
00000020 A z_data_smem_part_sh_part_size
00000040 A z_data_smem_z_libc_partition_part_size
00000800 A z_data_smem_z_malloc_partition_part_size
000008a0 A _app_smem_size
20000000 D z_data_smem_z_malloc_partition_part_start
20000800 D z_data_smem_z_libc_partition_part_start
20000840 D z_data_smem_part_sh_part_start
20000860 D z_data_smem_part_b_part_start
20000880 D z_data_smem_part_a_part_start
```

(`nm` 출력은 `size`/`part_start` 줄만 남겼다.)

출력에서 볼 것:

- 파티션마다 앞뒤로 `SMEM_PARTITION_ALIGN(크기)`가 들어간다. v7-M에서는 "다음 2의 거듭제곱으로 정렬 + 크기도 그만큼"이 된다. `part_sh`는 실제 데이터 4바이트(`bss_size` 4)인데 파티션은 32바이트(`part_size` 0x20)다.
- 앱이 정의하지 않은 파티션 두 개가 이미 있다. `z_malloc_partition`(2 KB — libc `malloc` 아레나)과 `z_libc_partition`(64 B — libc 전역). `libc-hooks.h`가 설명하듯 user 스레드가 libc를 쓰려면 이 파티션을 자기 도메인에 넣어야 한다. 이 예제의 `dom_a`/`dom_b`에는 넣지 않았는데도 `printk`가 됐다 — `printk`는 문자열 출력 **시스템 콜**(`k_str_out`)로 커널이 대신 찍기 때문이다. user 스레드에서 `snprintf`나 `malloc`을 쓰면 이야기가 다르다.
- 데이터 영역 전체(`_app_smem_size` 0x8a0 = 2208 B)가 RAM 맨 앞 0x20000000에 모인다. 정렬이 큰 파티션부터 놓아 구멍을 줄인다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="160" y="20" font-size="13" text-anchor="middle" font-weight="bold">RAM (예제 1의 nm 주소)</text>
<text x="545" y="20" font-size="13" text-anchor="middle" font-weight="bold">도메인 (열쇠 꾸러미)</text>
<rect x="20" y="32" width="280" height="40" rx="3" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="30" y="50" font-size="12">0x20000000 z_malloc_partition</text>
<text x="30" y="66" font-size="12">2048 B (libc malloc 아레나)</text>
<rect x="20" y="78" width="280" height="26" rx="3" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="30" y="96" font-size="12">0x20000800 z_libc_partition 64 B</text>
<rect x="20" y="110" width="280" height="26" rx="3" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="30" y="128" font-size="12">0x20000840 part_sh 32 B (4 B 사용)</text>
<rect x="20" y="142" width="280" height="26" rx="3" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="30" y="160" font-size="12">0x20000860 part_b 32 B (b_private)</text>
<rect x="20" y="174" width="280" height="26" rx="3" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="30" y="192" font-size="12">0x20000880 part_a 32 B (a_private)</text>
<rect x="20" y="206" width="280" height="40" rx="3" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="30" y="224" font-size="12">… 0x200010d8 kernel_side (.bss)</text>
<text x="30" y="240" font-size="12">어느 파티션에도 없음 → user 접근 불가</text>
<rect x="20" y="252" width="280" height="26" rx="3" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="30" y="270" font-size="12">noinit: st_a · st_b (스레드 스택)</text>
<rect x="430" y="70" width="230" height="74" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="545" y="90" font-size="12" text-anchor="middle" font-weight="bold">dom_a ← 스레드 A</text>
<text x="545" y="110" font-size="12" text-anchor="middle">part_a + part_sh</text>
<text x="545" y="130" font-size="12" text-anchor="middle">+ A의 스택 (리전 1개)</text>
<rect x="430" y="170" width="230" height="74" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="545" y="190" font-size="12" text-anchor="middle" font-weight="bold">dom_b ← 스레드 B</text>
<text x="545" y="210" font-size="12" text-anchor="middle">part_b + part_sh</text>
<text x="545" y="230" font-size="12" text-anchor="middle">+ B의 스택 (리전 1개)</text>
<line x1="302" y1="187" x2="428" y2="100" stroke="#4a7bd0" stroke-width="1.3" marker-end="url(#ah2)"/>
<line x1="302" y1="123" x2="428" y2="112" stroke="#3f9a6b" stroke-width="1.3" marker-end="url(#ah2)"/>
<line x1="302" y1="123" x2="428" y2="200" stroke="#3f9a6b" stroke-width="1.3" marker-end="url(#ah2)"/>
<line x1="302" y1="155" x2="428" y2="212" stroke="#e08a3c" stroke-width="1.3" marker-end="url(#ah2)"/>
<text x="340" y="306" font-size="12" text-anchor="middle">A가 0x20000860(part_b)이나 0x200010d8(kernel_side)를 건드리면 MPU FAULT, A만 abort</text>
</svg>
```

그림 2 — 예제 1의 실제 배치. 초록 `part_sh`는 두 도메인에 모두 들어 있어 공유 메모리가 된다. 컨텍스트 스위치 때 커널은 다음 스레드의 도메인 파티션과 스택만 MPU에 올린다.

### 2.5 흔한 함정 (파티션 · 도메인)

- **파티션 6개째**: 도메인당 하드웨어 한도(이 보드 5)를 넘기면 `k_mem_domain_init`/`add_partition`이 `-EINVAL`/`-ENOSPC`로 실패한다(`kernel/mem_domain.c` 105 · 235행의 `CHECKIF`). 반환값을 안 보면 "왜 이 스레드만 폴트?"로 하루를 쓴다. 파티션은 기능 단위로 크게 묶는다.
- **한 도메인 안의 겹치는 파티션**: 같은 도메인에 겹치는 두 파티션을 넣으면 거절된다(`mem_domain.c` 63–78행 `overlaps existing` 로그). 다른 도메인끼리 같은 파티션을 공유하는 것은 괜찮다(예제 1의 `part_sh`).
- **libc를 쓰는 user 스레드**: `z_libc_partition`을 도메인에 안 넣으면 libc 전역(예: `errno` 저장소, 스택 카나리)을 건드리는 순간 폴트. 기본 도메인에는 들어 있다.
- **`K_APP_BMEM`에 큰 배열 하나와 작은 변수 여러 개를 섞기**: 파티션은 통째로 2의 거듭제곱으로 반올림된다. 5000 B + 4 B = 8 KB(§7.3). 크기를 2의 거듭제곱 바로 아래로 맞추거나, v8-M을 쓴다.
- **도메인 이동은 스레드 단위**: `k_mem_domain_add_thread`는 그 스레드를 **옮긴다**. 그 스레드가 이미 만든 자식은 따라오지 않는다(자식은 만들어질 때 부모 도메인을 물려받았다).

---

## 3. Zephyr userspace ② — 커널 객체 권한

### 3.1 직관과 정의

메모리 격리만으로는 부족하다. user 스레드도 `k_sem_give(&sem)`처럼 **커널 객체의 주소**를 시스템 콜로 넘길 수 있다. 커널은 특권으로 그 주소에 쓰므로, 검사가 없으면 user 스레드가 "커널아, 0x20000100에 있는 세마포어를 줘"라고 속여 커널 메모리를 대신 쓰게 할 수 있다(C8 §3.3의 confused deputy).

Zephyr의 답은 두 겹이다.

1. **이 주소가 정말 커널 객체인가**: 빌드 때 `gen_kobject_list.py`가 ELF에서 모든 커널 객체(`k_sem`, `k_msgq`, `k_thread`, 스레드 스택 …)를 찾아 gperf perfect hash 표를 만든다(C8 §4.1). 런타임에 `k_object_find()`(`kernel/userspace.c` 560행)가 이 표에서 주소를 찾는다. 없으면 "not a known kernel object".
2. **이 스레드가 그 객체의 권한을 가졌나**: 객체마다 **스레드 권한 비트맵**이 있다(스레드마다 0, 1, 2 … 인덱스). 비트가 꺼져 있으면 "does not have permission".

권한을 주는 방법:

| API | 쓰는 곳 | 효과 |
|---|---|---|
| `k_object_access_grant(obj, thread)` | supervisor 또는 권한을 가진 user (syscall) | 그 스레드의 비트를 켠다 |
| `k_thread_access_grant(thread, obj1, obj2, …)` | supervisor | 여러 객체를 한 번에 grant하는 매크로 (`kernel.h` 489행) |
| `K_INHERIT_PERMS` 스레드 옵션 | 스레드를 만들 때 | 부모가 가진 권한을 자식에게 복사 (`kernel/thread.c` 698행 → `k_thread_perms_inherit`) |
| `k_object_access_revoke(obj, thread)` / `k_object_release(obj)` | supervisor / 자기 권한 반납 | 비트를 끈다 |
| `k_object_access_all_grant(obj)` | supervisor | 모든 스레드에 공개 (보안상 신중히) |

### 3.2 예제 2 — grant, 상속, 객체가 아닌 포인터, 그리고 스스로 user로 내려가기

무엇을 확인하나: (1) grant 받은 세마포어는 되고 안 받은 것은 oops, (2) user 스레드가 만든 손자 스레드는 `K_INHERIT_PERMS`가 있어야 부모의 권한을 갖는다, (3) 커널 객체가 아닌 주소를 넘기면?, (4) supervisor로 시작해 초기화 후 `k_thread_user_mode_enter()`로 **한 방향으로** 내려가는 패턴.

```c
/* F5 예제 2: 커널 객체 권한 — grant, K_INHERIT_PERMS, k_thread_user_mode_enter */
#include <zephyr/kernel.h>
#include "f5z.h"
K_SEM_DEFINE(sem_a, 0, 10);                  /* U1에게 grant */
K_SEM_DEFINE(sem_b, 0, 10);                  /* 아무에게도 grant 안 함 */
K_THREAD_STACK_DEFINE(st_u1, 1024);
K_THREAD_STACK_DEFINE(st_u2, 1024);
K_THREAD_STACK_DEFINE(st_s, 1024);
static struct k_thread th_u1, th_u2, th_s;
#if F5_BAD == 2
static uint32_t not_a_sem[8];                  /* 커널 객체가 아닌 그냥 배열 */
#endif

static void fn_u2(void *p1, void *p2, void *p3)          /* U1이 만든 손자 스레드 */
{
    printk("[U2] user=%d, k_sem_give(&sem_a) ...\n", (int)k_is_user_context());
    k_sem_give(&sem_a);                                   /* INHERIT 없으면 여기서 oops */
    printk("[U2] ok, sem_a count=%u\n", k_sem_count_get(&sem_a));
}

static void fn_u1(void *p1, void *p2, void *p3)
{
    k_sem_give(&sem_a);
    printk("[U1] k_sem_give(&sem_a) ok, count=%u\n", k_sem_count_get(&sem_a));
#if F5_BAD == 1
    printk("[U1] BAD1: k_sem_give(&sem_b) (no permission)\n");
    k_sem_give(&sem_b);
#elif F5_BAD == 2
    printk("[U1] BAD2: k_sem_give(not_a_sem) (not a kernel object)\n");
    k_sem_give((struct k_sem *)not_a_sem);
#endif
    uint32_t opt = K_USER | (F5_INHERIT ? K_INHERIT_PERMS : 0);
    k_tid_t t = k_thread_create(&th_u2, st_u2, K_THREAD_STACK_SIZEOF(st_u2), fn_u2, NULL, NULL, NULL,
                                6, opt, K_NO_WAIT);       /* user가 만든 스레드는 항상 user */
    k_thread_join(t, K_FOREVER);
    printk("[U1] child joined\n");
}

static void fn_s_user_part(void *p1, void *p2, void *p3)
{
    printk("[S] after user_mode_enter: user=%d\n", (int)k_is_user_context());
#if F5_BAD == 3
    printk("[S] BAD3: read FPGAIO counter again (now unprivileged)\n");
    printk("[S] counter=%u\n", FPGAIO_COUNTER);
#endif
}

static void fn_s(void *p1, void *p2, void *p3)           /* supervisor로 시작 → 초기화 → 스스로 user로 */
{
    printk("[S] supervisor init: user=%d, counter=%u\n", (int)k_is_user_context(), FPGAIO_COUNTER);
    k_thread_user_mode_enter(fn_s_user_part, NULL, NULL, NULL);   /* 돌아오지 않는다 */
}

int main(void)
{
    k_tid_t u1 = k_thread_create(&th_u1, st_u1, K_THREAD_STACK_SIZEOF(st_u1), fn_u1, NULL, NULL, NULL,
                                 5, K_USER, K_FOREVER);
    k_thread_name_set(u1, "U1");
    k_thread_access_grant(u1, &sem_a, &th_u2, &st_u2);    /* 세마포어 + 자식용 스레드 객체 + 스택 객체 */
    k_thread_start(u1);
    k_thread_join(u1, K_FOREVER);
    k_tid_t s = k_thread_create(&th_s, st_s, K_THREAD_STACK_SIZEOF(st_s), fn_s, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
    k_thread_name_set(s, "S");
    k_thread_join(s, K_FOREVER);
    printk("[main] end, sem_a=%u sem_b=%u\n", k_sem_count_get(&sem_a), k_sem_count_get(&sem_b));
    f5_exit();
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'for v in "0 1" "0 0" "1 1" "2 1" "3 1"; do set -- $v; echo "######## F5_BAD=$1 F5_INHERIT=$2";
  /Users/donh/.rtos_scratch/F5/zrun.sh zperm perm$1$2 mps2/an385 -- "-DEXTRA_CFLAGS=-DF5_BAD=$1 -DF5_INHERIT=$2" | grep -v "FLASH\|RAM:\|Booting"; done'
```

```text
######## F5_BAD=0 F5_INHERIT=1
[U1] k_sem_give(&sem_a) ok, count=1
[U2] user=1, k_sem_give(&sem_a) ...
[U2] ok, sem_a count=2
[U1] child joined
[S] supervisor init: user=0, counter=42092
[S] after user_mode_enter: user=1
[main] end, sem_a=2 sem_b=0
######## F5_BAD=0 F5_INHERIT=0
[U1] k_sem_give(&sem_a) ok, count=1
[U2] user=1, k_sem_give(&sem_a) ...
E: thread 0x20000c38 (1) does not have permission on k_sem 0x200008f4
E: permission bitmap
E: 01 00                   |..      
E: syscall z_vrfy_k_sem_give failed check: access denied
E: r0/a1:  0x00000000  r1/a2:  0x00000000  r2/a3:  0x00000000
E: r3/a4:  0x00000000 r12/ip:  0x00000000 r14/lr:  0x00000000
E:  xpsr:  0x00000000
E: Faulting instruction address (r15/pc): 0x00000000
E: >>> ZEPHYR FATAL ERROR 3: Kernel oops on CPU 0
E: Current thread: 0x20000c38 (unknown)
[fatal] reason=3 thread= -> abort only this thread
[U1] child joined
[S] supervisor init: user=0, counter=61937
[S] after user_mode_enter: user=1
[main] end, sem_a=1 sem_b=0
######## F5_BAD=1 F5_INHERIT=1
[U1] k_sem_give(&sem_a) ok, count=1
[U1] BAD1: k_sem_give(&sem_b) (no permission)
E: thread 0x20000cf0 (0) does not have permission on k_sem 0x20000904
E: permission bitmap
E: 00 00                   |..      
E: syscall z_vrfy_k_sem_give failed check: access denied
...
E: >>> ZEPHYR FATAL ERROR 3: Kernel oops on CPU 0
E: Current thread: 0x20000cf0 (U1)
[fatal] reason=3 thread=U1 -> abort only this thread
[S] supervisor init: user=0, counter=54208
[S] after user_mode_enter: user=1
[main] end, sem_a=1 sem_b=0
######## F5_BAD=2 F5_INHERIT=1
[U1] k_sem_give(&sem_a) ok, count=1
[U1] BAD2: k_sem_give(not_a_sem) (not a kernel object)
E: 0x20001000 is not a valid k_sem
E: address is not a known kernel object
E: syscall z_vrfy_k_sem_give failed check: access denied
...
E: >>> ZEPHYR FATAL ERROR 3: Kernel oops on CPU 0
E: Current thread: 0x20000cf0 (U1)
[fatal] reason=3 thread=U1 -> abort only this thread
[S] supervisor init: user=0, counter=50198
[S] after user_mode_enter: user=1
[main] end, sem_a=1 sem_b=0
######## F5_BAD=3 F5_INHERIT=1
[U1] k_sem_give(&sem_a) ok, count=1
[U2] user=1, k_sem_give(&sem_a) ...
[U2] ok, sem_a count=2
[U1] child joined
[S] supervisor init: user=0, counter=42092
[S] after user_mode_enter: user=1
[S] BAD3: read FPGAIO counter again (now unprivileged)
E: ***** MPU FAULT *****
E:   Data Access Violation
E:   MMFAR Address: 0x40028018
E: r0/a1:  0x000084c4  r1/a2:  0x00000000  r2/a3:  0x00000000
E: r3/a4:  0x40028000 r12/ip:  0x000067f5 r14/lr:  0x0000072d
E:  xpsr:  0x61000000
E: Faulting instruction address (r15/pc): 0x00000730
E: >>> ZEPHYR FATAL ERROR 19: Unknown error on CPU 0
E: Current thread: 0x20000b80 (S)
[fatal] reason=19 thread=S -> abort only this thread
[main] end, sem_a=2 sem_b=0
```

(BAD1 · BAD2의 레지스터 덤프 4줄은 BAD0/INHERIT0과 같아서 `...`로 줄였다.)

출력에서 볼 것:

- **INHERIT=0**: 권한 비트맵이 `01 00`이다. 스레드 인덱스 0(U1)의 비트만 켜져 있고 U2(인덱스 1)는 꺼져 있다. → `Kernel oops`(reason 3), U2만 죽고 U1은 `child joined`으로 계속 간다. U2에 이름을 안 붙여서 `thread=`가 비었다.
- **INHERIT=1**: 같은 코드에서 U2가 세마포어를 쓴다. "필요한 권한을 가진 부모 user 스레드가 일꾼을 만들어 쓰는" 구조에 쓴다. 단, **부모가 가진 모든 권한**이 복사되므로 최소 권한 원칙과 부딪힌다 — 자식이 정말 필요한 것만 쓰게 하려면 상속 대신 supervisor가 grant한다.
- **user 스레드가 스레드를 만들려면** 스레드 객체(`th_u2`)와 스택 객체(`st_u2`)의 권한도 필요하다. 그래서 main이 `k_thread_access_grant(u1, &sem_a, &th_u2, &st_u2)`로 세 개를 줬다. 스택도 커널 객체다.
- **BAD2**: 그냥 배열 주소를 세마포어로 넘기면 `address is not a known kernel object`. 커널은 그 주소를 건드리지도 않고 거절한다(gperf 표 조회에서 끝남). FreeRTOS v2는 같은 문제를 "핸들 = 인덱스"로 풀었다(C8 예제 9 BAD5).
- **BAD3**: `k_thread_user_mode_enter` 전에는 FPGAIO 카운터를 읽었는데(supervisor), 내려간 뒤에는 같은 주소에서 MPU FAULT. **특권은 한 번 내리면 스스로 못 올린다** — 이게 "초기화는 특권으로, 그 다음 평생 user로" 패턴의 핵심이다. 드라이버를 열고 하드웨어를 설정한 뒤 내려가면, 공격자가 이 스레드를 장악해도 하드웨어 설정을 바꿀 수 없다.

### 3.3 두 종류의 죽음 — oops와 MPU fault

| | kernel oops (reason 3) | MPU fault (reason 19 등) |
|---|---|---|
| 누가 감지 | **소프트웨어**: `z_vrfy_*`의 `K_OOPS(...)` 검사 | **하드웨어**: MPU가 MemManage 예외 |
| 언제 | 시스템 콜 인자 검사 실패 (객체 · 권한 · 버퍼) | user 코드가 직접 허락 안 된 주소를 읽고 씀 |
| 로그 특징 | `syscall z_vrfy_... failed check`, 레지스터 덤프가 전부 0 | `MPU FAULT`, `MMFAR Address`, 진짜 PC |
| 원인 찾기 | 어떤 syscall · 어떤 검사인지 로그가 알려 줌 | MMFAR을 nm/map과 대조 (예제 1) |
| 경로 | `arch_syscall_oops()` (`arch/arm/core/fatal.c` 158행) → `z_arm_fatal_error()` | `z_arm_fault()` (`arch/arm/core/cortex_m/fault.c` 1025행) → `z_arm_fatal_error()` |
| 마지막 | 둘 다 `z_fatal_error()` → `k_sys_fatal_error_handler()` → (return하면) 스레드 abort | 같음 |

레지스터 덤프가 0인 oops 로그를 보고 "스택이 깨졌나?" 하고 헤매지 않는다. 하드웨어 예외가 아니라서 덤프할 예외 프레임이 없는 것이다.

---

## 4. Zephyr userspace ③ — 나만의 시스템 콜

### 4.1 왜 앱이 시스템 콜을 만드나

user 스레드가 직접 못 하는 일이 있다: 특권 전용 하드웨어(보정 데이터가 든 OTP, 암호 엔진, 모터 출력) 접근, 커널 쪽 데이터 읽기. 그렇다고 그 스레드를 supervisor로 올리면 격리가 사라진다. 해법은 **좁은 문을 하나 더 내는 것**이다 — 정확히 필요한 일만 하고, 인자를 철저히 검사하는 시스템 콜. 드라이버 API(`i2c_transfer` 등)도 Zephyr에서는 이렇게 `__syscall`로 감싸여 있다.

만드는 순서 (v4.4.2, `samples/userspace/prod_consumer`의 `app_syscall.c`와 같은 방식):

1. 헤더에 `__syscall` 선언. 헤더 끝에 `#include <zephyr/syscalls/<헤더 이름>.h>` (빌드가 생성).
2. `prj.conf`에 `CONFIG_APPLICATION_DEFINED_SYSCALL=y` — 앱 소스 폴더도 `__syscall`을 찾게 한다(`Kconfig.zephyr` 1015행). 앱 밖 폴더면 CMake의 `zephyr_syscall_header()`(`cmake/modules/extensions.cmake` 1781행).
3. `z_impl_<이름>()`: 진짜 일. supervisor는 이걸 바로 부른다.
4. `z_vrfy_<이름>()`: user에서 왔을 때만 거치는 검사. 그리고 `#include <zephyr/syscalls/<이름>_mrsh.c>`.

### 4.2 예제 3 — 보정값 읽기(쓰기 포인터)와 CRC(읽기 버퍼) 시스템 콜

무엇을 확인하나: 앱 정의 syscall 두 개가 좋은 포인터에서는 동작하고, 커널 주소를 넘기면 `z_vrfy_`가 막는지. 그리고 "값이 틀린 것"과 "포인터가 틀린 것"을 다르게 처리하는 규칙.

```c
/* F5 예제 3: 앱이 정의하는 시스템 콜 두 개 — 보정값 읽기(쓰기 포인터), CRC 계산(읽기 버퍼) */
#ifndef F5_CAL_H
#define F5_CAL_H
#include <zephyr/kernel.h>
#define F5_CAL_N 4
__syscall int f5_cal_read(uint32_t idx, uint32_t *out);
__syscall uint32_t f5_crc32(const void *buf, size_t len);
#include <zephyr/syscalls/f5_cal.h>       /* 빌드가 생성하는 파일 (헤더 이름 그대로) */
#endif
```

```c
/* F5 예제 3: z_impl_(진짜 일) + z_vrfy_(user에서 왔을 때만 검사) */
#include <zephyr/internal/syscall_handler.h>
#include "f5_cal.h"
const uint32_t *f5_cal_table_addr(void);
static uint32_t cal_table[F5_CAL_N] = { 1000, 2000, 3000, 4000 };   /* 커널 쪽 데이터(파티션 밖) */
const uint32_t *f5_cal_table_addr(void) { return cal_table; }

int z_impl_f5_cal_read(uint32_t idx, uint32_t *out)
{
    if (idx >= F5_CAL_N) {
        return -EINVAL;                    /* 값이 틀린 것은 에러 코드로 */
    }
    *out = cal_table[idx];
    return 0;
}
static inline int z_vrfy_f5_cal_read(uint32_t idx, uint32_t *out)
{
    K_OOPS(K_SYSCALL_MEMORY_WRITE(out, sizeof(*out)));   /* 포인터가 틀린 것은 oops로 */
    return z_impl_f5_cal_read(idx, out);
}
#include <zephyr/syscalls/f5_cal_read_mrsh.c>

uint32_t z_impl_f5_crc32(const void *buf, size_t len)
{
    const uint8_t *p = buf; uint32_t c = 0xFFFFFFFFu;
    while (len--) { c ^= *p++; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & -(c & 1u)); }
    return ~c;
}
static inline uint32_t z_vrfy_f5_crc32(const void *buf, size_t len)
{
    K_OOPS(K_SYSCALL_MEMORY_READ(buf, len));
    return z_impl_f5_crc32(buf, len);
}
#include <zephyr/syscalls/f5_crc32_mrsh.c>
```

```c
/* F5 예제 3: user 스레드가 앱 정의 syscall을 좋은 포인터 / 나쁜 포인터로 부른다 */
#include <zephyr/kernel.h>
#include <zephyr/app_memory/app_memdomain.h>
#include "f5z.h"
#include "f5_cal.h"
const uint32_t *f5_cal_table_addr(void);
K_APPMEM_PARTITION_DEFINE(part_u);
K_APP_BMEM(part_u) uint8_t msg[16];
K_APP_BMEM(part_u) const uint32_t *kptr;                 /* main이 커널 주소를 알려 준다(공격 흉내) */
K_THREAD_STACK_DEFINE(st_u, 1024);
static struct k_thread th_u;

static void fn_u(void *p1, void *p2, void *p3)
{
    uint32_t v = 0;
    int r = f5_cal_read(2, &v);                           /* 자기 스택 주소: OK */
    printk("[U] f5_cal_read(2, &stack_var) -> %d, v=%u\n", r, v);
    r = f5_cal_read(9, &v);                               /* 값 범위 오류 → -EINVAL */
    printk("[U] f5_cal_read(9, &stack_var) -> %d\n", r);
    for (int i = 0; i < 16; i++) msg[i] = (uint8_t)i;
    printk("[U] f5_crc32(msg in my partition) -> 0x%08x\n", f5_crc32(msg, sizeof(msg)));
#if F5_BAD == 1
    printk("[U] BAD1: f5_cal_read(0, kernel address %p)\n", (void *)kptr);
    r = f5_cal_read(0, (uint32_t *)kptr);                 /* 커널에게 커널 메모리를 쓰게 시키기 */
    printk("[U] returned %d (should not get here)\n", r);
#elif F5_BAD == 2
    printk("[U] BAD2: f5_crc32(kernel address, 16)\n");
    printk("[U] crc=0x%08x (should not get here)\n", f5_crc32(kptr, 16));
#endif
}

int main(void)
{
    kptr = f5_cal_table_addr();
    printk("[main] supervisor call: f5_crc32(cal_table) = 0x%08x\n", f5_crc32(kptr, 16));
    k_mem_domain_add_partition(&k_mem_domain_default, &part_u);
    k_tid_t t = k_thread_create(&th_u, st_u, K_THREAD_STACK_SIZEOF(st_u), fn_u, NULL, NULL, NULL, 5, K_USER, K_NO_WAIT);
    k_thread_name_set(t, "U");
    k_thread_join(t, K_FOREVER);
    printk("[main] user thread gone, system alive\n");
    f5_exit();
    return 0;
}
```

`CMakeLists.txt`는 `target_sources(app PRIVATE src/main.c src/f5_cal.c)`, `prj.conf`에는 공통 6줄 + `CONFIG_APPLICATION_DEFINED_SYSCALL=y`.

```sh
limactl shell elinux -- bash -lc 'for b in 0 1 2; do echo "######## F5_BAD=$b"; /Users/donh/.rtos_scratch/F5/zrun.sh zsys sys$b mps2/an385 -- -DEXTRA_CFLAGS=-DF5_BAD=$b | grep -v "FLASH\|RAM:\|Booting"; done'
```

```text
######## F5_BAD=0
[main] supervisor call: f5_crc32(cal_table) = 0xdfcd39af
[U] f5_cal_read(2, &stack_var) -> 0, v=3000
[U] f5_cal_read(9, &stack_var) -> -22
[U] f5_crc32(msg in my partition) -> 0xcecee288
[main] user thread gone, system alive
######## F5_BAD=1
[main] supervisor call: f5_crc32(cal_table) = 0xdfcd39af
[U] f5_cal_read(2, &stack_var) -> 0, v=3000
[U] f5_cal_read(9, &stack_var) -> -22
[U] f5_crc32(msg in my partition) -> 0xcecee288
[U] BAD1: f5_cal_read(0, kernel address 0x2000086c)
E: syscall z_vrfy_f5_cal_read failed check: Memory region 0x2000086c (size 4) write access denied
E: r0/a1:  0x00000000  r1/a2:  0x00000000  r2/a3:  0x00000000
E: r3/a4:  0x00000000 r12/ip:  0x00000000 r14/lr:  0x00000000
E:  xpsr:  0x00000000
E: Faulting instruction address (r15/pc): 0x00000000
E: >>> ZEPHYR FATAL ERROR 3: Kernel oops on CPU 0
E: Current thread: 0x20000b10 (U)
[fatal] reason=3 thread=U -> abort only this thread
[main] user thread gone, system alive
######## F5_BAD=2
[main] supervisor call: f5_crc32(cal_table) = 0xdfcd39af
[U] f5_cal_read(2, &stack_var) -> 0, v=3000
[U] f5_cal_read(9, &stack_var) -> -22
[U] f5_crc32(msg in my partition) -> 0xcecee288
[U] BAD2: f5_crc32(kernel address, 16)
E: syscall z_vrfy_f5_crc32 failed check: Memory region 0x2000086c (size 16) read access denied
E: r0/a1:  0x00000000  r1/a2:  0x00000000  r2/a3:  0x00000000
E: r3/a4:  0x00000000 r12/ip:  0x00000000 r14/lr:  0x00000000
E:  xpsr:  0x00000000
E: Faulting instruction address (r15/pc): 0x00000000
E: >>> ZEPHYR FATAL ERROR 3: Kernel oops on CPU 0
E: Current thread: 0x20000b10 (U)
[fatal] reason=3 thread=U -> abort only this thread
[main] user thread gone, system alive
```

출력에서 볼 것:

- **같은 함수, 다른 대접**: main(supervisor)은 `f5_crc32(cal_table)`을 문제없이 계산했다. user 스레드가 같은 주소를 넘기자 `read access denied`. 검사는 "이 주소가 나쁜가"가 아니라 **"이 호출자가 이 주소를 읽을 수 있나"**다.
- **BAD1이 confused deputy 공격의 모양이다**: user가 "결과를 커널 주소 0x2000086c(`cal_table` 자신)에 써 줘"라고 요청한다. `z_impl_`은 특권이라 그대로 썼다면 보정 테이블을 덮어썼을 것이다. `K_SYSCALL_MEMORY_WRITE`가 막았다.
- **값 오류 vs 포인터 오류**: `idx=9`는 `-22`(`-EINVAL`)로 돌려주고 스레드는 계속 산다. 나쁜 포인터는 oops로 스레드를 죽인다. Zephyr 문서의 원칙과 같다 — 잘못된 **값**은 정상적인 에러이고, 접근할 수 없는 **메모리나 객체**를 넘기는 것은 버그 또는 공격이므로 그 스레드를 끝낸다.

### 4.3 생성된 코드와 기계어로 확인

```sh
limactl shell elinux -- bash -lc 'D=/var/tmp/rtos/F5/z_sys0/zephyr; G=$D/include/generated/zephyr;
  grep -n "F5_" $G/syscall_list.h; grep -n "f5_cal_read" -A13 $G/syscalls/f5_cal.h | sed -n 3,16p;
  cat $G/syscalls/f5_cal_read_mrsh.c; grep -n "F5_" $G/syscall_dispatch.c'
```

```text
11:#define K_SYSCALL_F5_CAL_READ 5
12:#define K_SYSCALL_F5_CRC32 6
25-__pinned_func
26:static inline int f5_cal_read(uint32_t idx, uint32_t * out)
27-{
28-#ifdef CONFIG_USERSPACE
29-	if (z_syscall_trap()) {
30-		union { uintptr_t x; uint32_t val; } parm0 = { .val = idx };
31-		union { uintptr_t x; uint32_t * val; } parm1 = { .val = out };
32-		return (int) arch_syscall_invoke2(parm0.x, parm1.x, K_SYSCALL_F5_CAL_READ);
33-	}
34-#endif
35-	compiler_barrier();
36:	return z_impl_f5_cal_read(idx, out);
37-}
38-
/* auto-generated by gen_syscalls.py, don't edit */

#include <zephyr/syscalls/f5_cal.h>

extern int z_vrfy_f5_cal_read(uint32_t idx, uint32_t * out);
uintptr_t z_mrsh_f5_cal_read(uintptr_t arg0, uintptr_t arg1, uintptr_t arg2,
		uintptr_t arg3, uintptr_t arg4, uintptr_t arg5, void *ssf)
{
	_current->syscall_frame = ssf;
	(void) arg2;	/* unused */
	(void) arg3;	/* unused */
	(void) arg4;	/* unused */
	(void) arg5;	/* unused */
	union { uintptr_t x; uint32_t val; } parm0;
	parm0.x = arg0;
	union { uintptr_t x; uint32_t * val; } parm1;
	parm1.x = arg1;
	int ret = z_vrfy_f5_cal_read(parm0.val, parm1.val);
	_current->syscall_frame = NULL;
	return (uintptr_t) ret;
}

804:	[K_SYSCALL_F5_CAL_READ] = z_mrsh_f5_cal_read,
805:	[K_SYSCALL_F5_CRC32] = z_mrsh_f5_crc32,
```

출력에서 볼 것: 앱 syscall도 커널 syscall과 **완전히 같은 기계**에 올라탄다. 번호(5, 6)를 받고, `_k_syscall_table` 804–805행에 마샬링 함수가 들어갔다. 번호는 빌드마다 이름순으로 다시 매겨지므로 **바이너리 호환 ABI가 아니다** — 커널과 앱을 항상 같이 빌드하는 MCU라서 괜찮은 설계다.

user 쪽 진입과 검사 쪽을 기계어로 본다.

```sh
limactl shell elinux -- bash -lc 'D=/var/tmp/rtos/F5/z_sys0/zephyr; O="arm-none-eabi-objdump -d --no-show-raw-insn $D/zephyr.elf";
  $O | awk "/<f5_cal_read>:/,/^$/"; $O | awk "/<z_mrsh_f5_cal_read>:/,/^$/" | sed -n 1,9p; echo "    ..."; $O | awk "/<z_mrsh_f5_cal_read>:/,/^$/" | sed -n 26,33p;
  arm-none-eabi-nm $D/zephyr.elf | grep -E " (arch_buffer_validate|cal_table)$"'
```

```text
00005f50 <f5_cal_read>:
    5f50:	stmdb	sp!, {r3, r4, r5, r6, r8, lr}
    5f54:	mov	r4, r0
    5f56:	mov	r5, r1
    5f58:	bl	5f42 <arch_is_user_context>
    5f5c:	cbnz	r0, 5f6a <f5_cal_read+0x1a>
    5f5e:	mov	r1, r5
    5f60:	mov	r0, r4
    5f62:	ldmia.w	sp!, {r3, r4, r5, r6, r8, lr}
    5f66:	b.w	898 <z_impl_f5_cal_read>
    5f6a:	mov	r0, r4
    5f6c:	mov	r1, r5
    5f6e:	movs	r6, #5
    5f70:	svc	3
    5f72:	ldmia.w	sp!, {r3, r4, r5, r6, r8, pc}

000008b4 <z_mrsh_f5_cal_read>:
     8b4:	push	{r0, r1, r4, r5, r6, lr}
     8b6:	ldr	r5, [pc, #96]	@ (918 <z_mrsh_f5_cal_read+0x64>)
     8b8:	ldr	r2, [sp, #32]
     8ba:	ldr	r3, [r5, #8]
     8bc:	mov	r6, r0
     8be:	str.w	r2, [r3, #152]	@ 0x98
     8c2:	adds	r3, r1, #5
     8c4:	mov	r4, r1
    ...
     8ec:	ldr.w	r0, [r3, #152]	@ 0x98
     8f0:	bl	6612 <arch_syscall_oops>
     8f4:	movs	r2, #1
     8f6:	movs	r1, #4
     8f8:	mov	r0, r4
     8fa:	bl	669c <arch_buffer_validate>
     8fe:	mov	r2, r0
     900:	cmp	r0, #0
0000669c T arch_buffer_validate
2000086c d cal_table
```

출력에서 볼 것:

- user 쪽: `arch_is_user_context()`가 0이면 `b.w z_impl_f5_cal_read`(꼬리 호출), 1이면 `r6 = 5`(syscall 번호), `svc 3`. C8 §4.3의 `k_sem_give`와 같은 모양이다.
- 검사 쪽: `z_vrfy_`가 인라인되어 `z_mrsh_` 안에 들어갔다. `arch_buffer_validate(out, 4, write=1)`이 0이 아니면 로그를 찍고 `arch_syscall_oops`로 간다. (생략한 가운데 부분은 실패 로그 문자열을 준비하는 코드다.)
- **`arch_buffer_validate`는 표를 보지 않고 MPU 레지스터를 본다.** v7-M 구현(`arch/arm/core/mpu/arm_mpu_v7_internal.h` 172행 `mpu_buffer_validate`)은 인터럽트를 잠그고 MPU 리전을 높은 번호부터 훑어, 그 버퍼를 덮는 리전이 **user 접근을 허락하는지** 본다. 지금 MPU에는 호출한 스레드의 도메인과 스택이 올라와 있으므로, "이 스레드가 직접 접근할 수 있었을 메모리인가"를 하드웨어 규칙 그대로 판정하는 셈이다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="190" y="10" width="300" height="36" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340" y="33" font-size="13" text-anchor="middle">f5_cal_read(idx, out) — 생성된 인라인</text>
<line x1="340" y1="46" x2="340" y2="66" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
<rect x="190" y="68" width="300" height="36" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="340" y="91" font-size="13" text-anchor="middle">user 컨텍스트인가? (IPSR, CONTROL)</text>
<line x1="490" y1="86" x2="560" y2="86" stroke="currentColor" stroke-width="1.2"/>
<line x1="560" y1="86" x2="560" y2="292" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
<text x="568" y="106" font-size="12">아니오</text>
<text x="568" y="122" font-size="12">(supervisor)</text>
<line x1="340" y1="104" x2="340" y2="124" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
<text x="348" y="118" font-size="12">예</text>
<rect x="190" y="126" width="300" height="36" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340" y="149" font-size="13" text-anchor="middle">svc 3, r6 = 5 → _k_syscall_table[5]</text>
<line x1="340" y1="162" x2="340" y2="182" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
<rect x="150" y="184" width="380" height="50" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340" y="205" font-size="13" text-anchor="middle">z_vrfy: K_SYSCALL_MEMORY_WRITE(out, 4)</text>
<text x="340" y="224" font-size="12" text-anchor="middle">arch_buffer_validate — 지금 MPU 리전이 user 쓰기를 허락하나</text>
<line x1="150" y1="209" x2="100" y2="209" stroke="currentColor" stroke-width="1.2"/>
<line x1="100" y1="209" x2="100" y2="292" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
<text x="20" y="250" font-size="12">아니오</text>
<line x1="340" y1="234" x2="340" y2="292" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
<text x="348" y="266" font-size="12">예</text>
<rect x="20" y="294" width="160" height="50" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="100" y="315" font-size="12" text-anchor="middle">K_OOPS → reason 3</text>
<text x="100" y="333" font-size="12" text-anchor="middle">이 스레드 abort</text>
<rect x="230" y="294" width="380" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="420" y="315" font-size="12" text-anchor="middle">z_impl: idx 범위 밖이면 -EINVAL (스레드는 산다)</text>
<text x="420" y="333" font-size="12" text-anchor="middle">아니면 out에 보정값을 쓰고 0</text>
<text x="340" y="372" font-size="12" text-anchor="middle">규칙: 틀린 값 → 에러 코드 · 접근할 수 없는 포인터나 객체 → oops</text>
<text x="340" y="390" font-size="12" text-anchor="middle">검사는 z_vrfy에만, 진짜 일은 z_impl에만 — supervisor는 검사 없이 z_impl로 직행</text>
</svg>
```

그림 3 — 예제 3의 `f5_cal_read`가 지나는 두 길. 검사는 user에서 들어온 경우에만 하고, 그 기준은 "호출한 스레드의 MPU 설정"이다.

### 4.4 검증 함수를 쓸 때의 규칙

1. **모든 포인터 인자를 검사한다.** 읽을 버퍼는 `K_SYSCALL_MEMORY_READ`, 쓸 버퍼는 `K_SYSCALL_MEMORY_WRITE`, 배열은 `K_SYSCALL_MEMORY_ARRAY_READ/WRITE`(개수 × 크기 곱셈 오버플로까지 본다), 커널 객체는 `K_SYSCALL_OBJ(ptr, K_OBJ_SEM)`, 초기화 전 객체는 `K_SYSCALL_OBJ_INIT`, 드라이버는 `K_SYSCALL_DRIVER_OP`(전부 `include/zephyr/internal/syscall_handler.h`).
2. **TOCTOU를 막는다.** 검사한 뒤 `z_impl_`이 user 메모리를 다시 읽으면, 그 사이에 같은 도메인의 다른 user 스레드가 값을 바꿀 수 있다(검사 시점과 사용 시점이 다르다 — Time Of Check to Time Of Use). 길이 필드처럼 **검사에 쓰인 값**은 `k_usermode_from_copy()`로 커널 쪽 지역 변수에 복사한 뒤 그 복사본을 쓴다. 공식 샘플 `prod_consumer/src/app_syscall.c`가 정확히 이렇게 한다(`cookie_copy`). 예제 3의 `f5_crc32`는 결과만 계산하므로 TOCTOU가 문제되지 않지만, "길이를 읽고 그 길이만큼 복사"하는 syscall이라면 반드시 복사본을 써야 한다.
3. **문자열**은 `k_usermode_string_copy()` / `k_usermode_string_nlen()`로 길이를 제한해서 다룬다.
4. **`z_impl_`은 신뢰하는 쪽에서도 불린다.** 그러니 `z_impl_`에는 값 범위 검사(예제의 `idx`)를 두고, 포인터 접근성 검사는 `z_vrfy_`에만 둔다.
5. **문을 넓히지 않는다.** "주소와 길이를 받아 아무 메모리나 읽어 주는 디버그 syscall"은 격리를 통째로 무너뜨린다. syscall 하나하나가 공격 표면이다.

---

## 5. 결함 격리와 재시작 — Zephyr

### 5.1 fatal 핸들러가 정책이다

`z_fatal_error()`(`kernel/fatal.c` 84행 이후)의 흐름:

1. 로그 `>>> ZEPHYR FATAL ERROR <reason>: ...`
2. `k_sys_fatal_error_handler(reason, esf)` 호출 — **기본 구현은 `__weak`이고 `arch_system_halt()`로 시스템을 멈춘다**(같은 파일 37–46행).
3. 핸들러가 **return하면** "시스템을 멈추지 않기로 정책을 정했다"고 보고 현재 스레드를 abort한다(119–133행 주석 그대로: "If the system fatal error handler returns, then kill the faulting thread"). 단 ISR 안에서 난 폴트는 무조건 panic이다.

그래서 f5z.h의 재정의는 `K_ERR_KERNEL_PANIC`이 아니면 return한다. 실무 정책은 보통 이렇게 나뉜다.

| reason | 뜻 | 권장 정책 |
|---|---|---|
| `K_ERR_KERNEL_OOPS` (3) | syscall 검사 실패, `k_oops()` | 그 user 스레드만 정리 → 재시작 |
| `K_ERR_ARM_MEM_*` (16–21) | MPU 위반 | user 스레드면 정리 → 재시작. **supervisor 스레드면 시스템 리셋**(커널 쪽 코드의 버그라 상태를 믿을 수 없다) |
| `K_ERR_STACK_CHK_FAIL` (2) | 스택 오버플로 (F3) | 위와 같음 |
| `K_ERR_KERNEL_PANIC` (4) | 복구 불가 | 시스템 리셋 (워치독 · `sys_reboot`) |
| ISR 안의 폴트 | 인터럽트 문맥 | 시스템 리셋 |

### 5.2 예제 4 — 파서가 죽으면 감독이 다시 만든다, 비행 제어는 계속 돈다

무엇을 확인하나: (1) 외부에서 온 패킷의 길이 필드를 믿는 user 파서가 오버런으로 MPU 폴트, (2) 감독(supervisor) 스레드가 `k_thread_join`으로 죽음을 알아채고 같은 스레드 객체 · 스택으로 다시 만든다, (3) 최고 우선순위 2 ms 비행 제어 루프의 주기가 폴트 없을 때와 같은지. `prj.conf`에 `CONFIG_SYS_CLOCK_TICKS_PER_SEC=1000`을 더했다.

```c
/* F5 예제 4: 파서 user 스레드가 버퍼 오버런으로 죽으면 supervisor가 다시 만든다. 비행 제어 루프는 계속 돈다 */
#include <zephyr/kernel.h>
#include <zephyr/app_memory/app_memdomain.h>
#define F5_DEFAULT_FATAL                                 /* f5z.h의 핸들러 대신 아래 것을 쓴다 */
#include "f5z.h"
struct pkt { uint32_t len; uint8_t data[60]; };
K_MSGQ_DEFINE(rxq, sizeof(struct pkt), 4, 4);
K_APPMEM_PARTITION_DEFINE(part_p);
K_APP_BMEM(part_p) uint8_t frame[32];                    /* 파서의 작업 버퍼 */
K_APP_BMEM(part_p) uint32_t parsed;
static struct k_mem_domain dom_p;
K_THREAD_STACK_DEFINE(st_p, 1024);
K_THREAD_STACK_DEFINE(st_sup, 1024);
static struct k_thread th_p, th_sup;
static volatile uint32_t fault_at, faults, fc_iter, fc_dmax, fc_dmin = UINT32_MAX;
void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
    ARG_UNUSED(esf);
    fault_at = FPGAIO_COUNTER; faults++;
    printk("[fatal] reason=%u thread=%s\n", reason, k_thread_name_get(k_current_get()));
    if (reason == K_ERR_KERNEL_PANIC) k_fatal_halt(reason);
}                                                         /* return → 커널이 이 스레드만 abort */
static void parser(void *p1, void *p2, void *p3)         /* user 스레드: 길이 필드를 믿는 버그 */
{
    for (struct pkt p;;) {
        k_msgq_get(&rxq, &p, K_FOREVER);
        for (uint32_t i = 0; i < p.len; i++) frame[i] = p.data[i % 60];   /* len > 32 → 오버런 */
        parsed++;
    }
}
static void start_parser(void)
{
    k_tid_t t = k_thread_create(&th_p, st_p, K_THREAD_STACK_SIZEOF(st_p), parser, NULL, NULL, NULL, 6, K_USER, K_FOREVER);
    k_thread_name_set(t, "parser");
    k_mem_domain_add_thread(&dom_p, t);                   /* 재시작할 때마다 도메인 · 권한을 다시 준다 */
    k_object_access_grant(&rxq, t);
    k_thread_start(t);
}
static void supervisor(void *p1, void *p2, void *p3)     /* supervisor 스레드, 우선순위 3 */
{
    for (int n = 1; ; n++) {
        k_thread_join(&th_p, K_FOREVER);                  /* 파서가 abort되면 깨어난다 */
        start_parser();
        printk("[sup] parser died (parsed=%u), restart #%d, fault->restart %u us\n",
               parsed, n, (FPGAIO_COUNTER - fault_at) / 25u);
    }
}
static void flight_ctrl(void *p1, void *p2, void *p3)    /* supervisor, 최고 우선순위, 2 ms 주기 */
{
    int64_t next = k_uptime_ticks() + 1;
    k_sleep(K_TIMEOUT_ABS_TICKS(next));
    for (uint32_t prev = FPGAIO_COUNTER; fc_iter < 100; fc_iter++) {
        next += 2;
        k_sleep(K_TIMEOUT_ABS_TICKS(next));
        uint32_t now = FPGAIO_COUNTER, d = now - prev; prev = now;   /* 주기 길이, 2 ms = 50000 */
        fc_dmax = MAX(fc_dmax, d); fc_dmin = MIN(fc_dmin, d);
    }
}
K_THREAD_DEFINE(fc_tid, 1024, flight_ctrl, NULL, NULL, NULL, 1, 0, 0);
int main(void)
{
    struct k_mem_partition *pp[] = { &part_p };
    k_mem_domain_init(&dom_p, 1, pp);
    start_parser();
    k_thread_create(&th_sup, st_sup, K_THREAD_STACK_SIZEOF(st_sup), supervisor, NULL, NULL, NULL, 3, 0, K_NO_WAIT);
    struct pkt p = { 0 };
    for (int i = 1; i <= 12; i++, k_msleep(10)) {       /* 4, 9번째 패킷의 길이 필드가 손상 */
        p.len = (!F5_NOFAULT && (i == 4 || i == 9)) ? 200 : 24;
        k_msgq_put(&rxq, &p, K_FOREVER);
    }
    k_thread_join(fc_tid, K_FOREVER);
    printk("[main] sent=12 parsed=%u faults=%u | flight_ctrl iterations=%u, period min/max=%u/%u us\n",
           parsed, faults, fc_iter, fc_dmin / 25u, fc_dmax / 25u);
    f5_exit();
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'for n in 1 0; do echo "######## F5_NOFAULT=$n"; /Users/donh/.rtos_scratch/F5/zrun.sh zrst rst$n mps2/an385 -- -DEXTRA_CFLAGS=-DF5_NOFAULT=$n | grep -v "FLASH\|RAM:\|Booting"; done;
  arm-none-eabi-nm -S /var/tmp/rtos/F5/z_rst0/zephyr/zephyr.elf | grep -E " (frame|parsed)$|part_p_part_(start|end)"'
```

```text
######## F5_NOFAULT=1
[main] sent=12 parsed=12 faults=0 | flight_ctrl iterations=100, period min/max=1944/2062 us
######## F5_NOFAULT=0
E: ***** MPU FAULT *****
E:   Data Access Violation
E:   MMFAR Address: 0x20000880
E: r0/a1:  0x000000c8  r1/a2:  0x200043d8  r2/a3:  0x20000880
E: r3/a4:  0x0000003d r12/ip:  0x00000805 r14/lr:  0x000007f5
E:  xpsr:  0x01000000
E: Faulting instruction address (r15/pc): 0x00000832
E: >>> ZEPHYR FATAL ERROR 19: Unknown error on CPU 0
E: Current thread: 0x20000d30 (parser)
[fatal] reason=19 thread=parser
[sup] parser died (parsed=3), restart #1, fault->restart 120 us
E: ***** MPU FAULT *****
...
[fatal] reason=19 thread=parser
[sup] parser died (parsed=7), restart #2, fault->restart 120 us
[main] sent=12 parsed=10 faults=2 | flight_ctrl iterations=100, period min/max=1944/2062 us
20000844 00000020 D frame
20000840 00000004 D parsed
20000880 D z_data_smem_part_p_part_end
20000840 D z_data_smem_part_p_part_start
```

(두 번째 폴트의 덤프는 첫 번째와 같아 `...`로 줄였다.)

출력에서 볼 것:

- **폴트 주소가 파티션 끝이다.** 파티션은 0x20000840–0x2000087F(64 B: `parsed` 4 B + `frame` 32 B → 64로 반올림), 폴트는 0x20000880. `r0 = 0xc8`(200)은 길이 필드, `r3 = 0x3d`(61)은 루프 인덱스다. 즉 `frame[32]`부터 `frame[59]`까지 28바이트는 **폴트 없이 덮어썼다**. 파티션의 반올림된 빈 공간이었기에 이번에는 피해가 없었지만, 그 자리에 같은 파티션의 다른 변수가 있었다면 조용히 깨졌을 것이다. **MPU는 경계만 지킨다.**
- **파서만 죽고 다시 태어났다**: 12개 중 10개 파싱(손상 패킷 2개만 버림), 재시작 2번. 폴트부터 새 파서가 준비되기까지 120 us(QEMU icount, 폴트 로그를 UART로 찍는 시간 포함).
- **비행 제어 주기는 그대로**: 폴트가 있을 때와 없을 때 주기 min/max가 1944/2062 us로 **똑같다**. 폴트 처리는 우선순위 6짜리 파서의 문맥에서 일어나고, 감독은 우선순위 3이라 우선순위 1인 비행 제어를 밀어내지 않는다. (±60 us 흔들림 자체는 폴트와 무관한 기본 지터다 — 이 QEMU 구성의 Zephyr SysTick 타임키핑에서 주기마다 약 1 us씩 밀리는 현상도 함께 보였다. FreeRTOS 예제 7은 같은 측정에서 1998/2001 us였다.)
- 한계 하나: Zephyr의 fatal 처리 경로(`z_fatal_error`)는 `arch_irq_lock()`을 잡고 로그를 찍는다. 로그가 길면 그동안 **모든 인터럽트가 막힌다**. 이 실행에서는 마침 비행 제어의 깨어날 시각과 겹치지 않았지만, 실제 제품에서는 폴트 덤프 수준(`CONFIG_FAULT_DUMP`)과 로그 백엔드를 줄이거나, 고주파 제어를 인터럽트(ISR) 쪽에 두어야 한다. 이 노트는 그 최악 경우를 따로 재지 않았다.

재시작 설계에서 지킨 것:

- **같은 객체 재사용**: `k_thread_join`이 돌아왔다면 그 스레드는 완전히 죽었으므로 `th_p`/`st_p`를 다시 `k_thread_create`에 넣어도 된다.
- **권한 · 도메인은 스레드에 붙는다**: 새 스레드는 새 권한 인덱스로 시작하므로 `k_mem_domain_add_thread`와 `k_object_access_grant`를 매번 다시 한다(`start_parser`).
- **상태 초기화**: 파서의 작업 버퍼는 어차피 다음 패킷이 덮어쓰지만, 상태 기계가 있는 태스크라면 재시작 때 자기 파티션을 지우고(supervisor가 `memset`) 시작해야 한다. 죽기 전 상태를 믿지 않는다.
- **재시작 폭주 방지**: 실제로는 "N초 안에 M번 이상 죽으면 그 기능을 끄고 안전 모드" 같은 백오프가 필요하다(E8의 리셋 루프 → safe mode와 같은 생각).

```svg
<svg viewBox="0 0 680 290" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" font-size="13" font-weight="bold">폴트 하나의 처리 순서 (예제 4 · 예제 7, 시간축은 개념도)</text>
<text x="120" y="52" font-size="12" text-anchor="end">비행 제어 (최고)</text>
<text x="120" y="92" font-size="12" text-anchor="end">감독 (중간)</text>
<text x="120" y="132" font-size="12" text-anchor="end">파서 (user)</text>
<text x="120" y="172" font-size="12" text-anchor="end">예외 (Handler)</text>
<line x1="130" y1="190" x2="670" y2="190" stroke="currentColor" marker-end="url(#ah4)"/>
<text x="664" y="208" font-size="12" text-anchor="end">시간</text>
<rect x="140" y="38" width="30" height="20" fill="#4a7bd0" fill-opacity="0.6"/>
<rect x="400" y="38" width="30" height="20" fill="#4a7bd0" fill-opacity="0.6"/>
<rect x="640" y="38" width="26" height="20" fill="#4a7bd0" fill-opacity="0.6"/>
<rect x="180" y="118" width="90" height="20" fill="#e08a3c" fill-opacity="0.6"/>
<text x="225" y="113" font-size="12" text-anchor="middle">오버런</text>
<rect x="270" y="158" width="60" height="20" fill="#d0564a" fill-opacity="0.6"/>
<text x="300" y="153" font-size="12" text-anchor="middle">MPU 폴트</text>
<rect x="330" y="78" width="60" height="20" fill="#3f9a6b" fill-opacity="0.6"/>
<text x="360" y="73" font-size="12" text-anchor="middle">재생성</text>
<rect x="440" y="118" width="40" height="20" fill="#e08a3c" fill-opacity="0.3" stroke="#e08a3c"/>
<text x="460" y="113" font-size="12" text-anchor="middle">새 파서</text>
<line x1="330" y1="168" x2="345" y2="98" stroke="currentColor" stroke-width="1.1" marker-end="url(#ah4)"/>
<text x="20" y="232" font-size="12">Zephyr: z_fatal_error → 핸들러 return → k_thread_abort → join이 깨움 → 재생성 = 120 us</text>
<text x="20" y="252" font-size="12">FreeRTOS: 앱의 MemManage → FromISR 알림 + 복귀 PC 교체 → vTaskDelete · 재생성 = 57 us</text>
<text x="20" y="274" font-size="12">비행 제어 주기: 폴트 있을 때와 없을 때 같음 (Zephyr 1944/2062 us, FreeRTOS 1998/2001 us)</text>
</svg>
```

그림 4 — 예제 4(Zephyr)와 예제 7(FreeRTOS)의 폴트 처리 순서. 핵심은 폴트 처리와 재생성이 **비행 제어보다 낮은 우선순위**에서 일어난다는 것이다. 숫자는 QEMU icount 측정값(폴트 로그 출력 시간 포함).

---

## 6. FreeRTOS-MPU — restricted 태스크로 같은 일을 한다

### 6.1 C8 빌드 위에서 시작

빌드는 C8 §3.5를 그대로 복사했다(`ARM_CM3_MPU` 포트, 래퍼 v2, `configPROTECTED_KERNEL_OBJECT_POOL_SIZE 16`, `configENABLE_ACCESS_CONTROL_LIST 1`, 링커 섹션 `privileged_functions`/`freertos_system_calls`/`privileged_data`). 바뀐 것 하나: 랩 템플릿 `startup.c`가 10-25 수정으로 `.preinit_array`/`.init_array`를 돌게 되어, C8의 링커 스크립트로는 `undefined reference to '__preinit_array_start'`로 링크가 실패했다. 두 줄을 더했다.

```text
    .preinit_array : { __preinit_array_start = .; KEEP(*(.preinit_array*)) __preinit_array_end = .; } > FLASH
    .init_array : { __init_array_start = .; KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array*)) __init_array_end = .; } > FLASH
```

restricted 태스크를 만드는 재료(V11.3.2 소스에서 확인):

- `TaskParameters_t`(`include/task.h` 151행): `pvTaskCode`, `pcName`, `usStackDepth`, `pvParameters`, `uxPriority`, `puxStackBuffer`, `xRegions[portNUM_CONFIGURABLE_REGIONS]`, 그리고 정적 할당이 켜져 있으면 `pxTaskBuffer`. **지정 초기화(`.field =`)로 쓰자** — 위치 초기화로 쓰면 `pxTaskBuffer`를 빠뜨려 `-Wextra`가 `missing initializer` 경고를 낸다(예제 8 첫 빌드에서 겪었다).
- `xRegions` 원소 `MemoryRegion_t { pvBaseAddress, ulLengthInBytes, ulParameters }`. ARM_CM3_MPU에서 `portNUM_CONFIGURABLE_REGIONS`는 3이고, 스택이 리전 3(`portSTACK_REGION`)을 쓴다.
- `ulParameters`(`ARM_CM3_MPU/portmacro.h` 81–87행): `portMPU_REGION_READ_WRITE`, `portMPU_REGION_READ_ONLY`, `portMPU_REGION_PRIVILEGED_READ_ONLY`, `portMPU_REGION_PRIVILEGED_READ_WRITE`, `portMPU_REGION_PRIVILEGED_READ_WRITE_UNPRIV_READ_ONLY`, `portMPU_REGION_CACHEABLE_BUFFERABLE`, `portMPU_REGION_EXECUTE_NEVER`.
- 만들기: `xTaskCreateRestricted(&params, &handle)`(TCB는 힙, 스택은 `puxStackBuffer`) 또는 `xTaskCreateRestrictedStatic`(TCB도 `pxTaskBuffer`). 실행 중 리전 바꾸기: `vTaskAllocateMPURegions(handle, regions)`.
- ACL(`include/mpu_wrappers.h` 252–277행): `vGrantAccessToQueue`, `vRevokeAccessToQueue`, `...Semaphore`, `...Task`, `...EventGroup`, `...StreamBuffer`, `...MessageBuffer`, `...Timer`, `...QueueSet`. 전부 `vGrantAccessToKernelObject(task, (int32_t)obj)` / `vRevokeAccessToKernelObject`의 매크로다(`mpu_wrappers_v2.c` 348 · 383행 → `port.c` 1503 · 1522행의 `vPortGrantAccessToKernelObject`).

### 6.2 예제 7 — MemManage 핸들러를 직접 쓰고, 감독 태스크가 지우고 다시 만든다

FreeRTOS 포트는 MemManage 핸들러를 **제공하지 않는다**(`ARM_CM3_MPU/port.c`에서 `portNVIC_MEM_FAULT_ENABLE`로 폴트를 켜기만 한다, 1183행). 정책은 앱 몫이다. 설계:

1. MemManage 핸들러(Handler 모드 = 특권)는 CFSR · MMFAR · 쌓인 PC를 기록하고, MMFSR 비트를 지운다(write-1-to-clear).
2. 쌓인 예외 프레임의 **복귀 PC를 `parked()`로 바꾼다**. 그대로 복귀하면 같은 명령을 다시 실행해 무한 폴트가 난다.
3. 감독 태스크에 `vTaskNotifyGiveFromISR`로 알린다. 이 호출이 되는 이유가 중요하다: v2에서 `...FromISR` API는 SVC 래퍼가 아니라 **특권 전용 래퍼**(`mpu_wrappers.h` 74–80행, `mpu_wrappers_v2.c` 2142행 `MPU_vTaskGenericNotifyGiveFromISR`, `PRIVILEGED_FUNCTION`)라서 Handler 모드에서 그냥 함수로 불린다. C8 §3.11의 경고("폴트 핸들러에서 API를 부르면 HardFault로 번진다")는 SVC를 쓰는 **일반 API** 이야기다.
4. 감독(특권, 파서보다 높은 우선순위)이 `vTaskDelete`로 파서를 지우고 같은 스택 버퍼로 `xTaskCreateRestricted`, ACL을 다시 준다.

```c
/* F5 예제 7: restricted 파서가 오버런으로 MemManage → 핸들러가 감독 태스크에 알림 → 지우고 다시 만든다 */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "board.h"
#define CNT REG32(0x40028018u)                            /* FPGAIO 25 MHz */
struct pkt { uint32_t len; uint8_t data[60]; };
static struct { uint8_t frame[32]; uint32_t parsed; } pz __attribute__((aligned(64)));   /* 파서 리전 64 B */
static StackType_t pstack[256] __attribute__((aligned(1024)));
static QueueHandle_t rxq;
static TaskHandle_t parser_h, sup_h;
static volatile uint32_t f_addr, f_pc, f_at, faults, fc_dmin = 0xFFFFFFFFu, fc_dmax;

static void parser(void *arg)                             /* 비특권: 자기 스택 + pz 64 B만 쓸 수 있다 */
{
    QueueHandle_t q = (QueueHandle_t)arg;                 /* 전역 rxq는 못 읽는다 → 인자로 */
    for (struct pkt p;;) {
        xQueueReceive(q, &p, portMAX_DELAY);
        for (uint32_t i = 0; i < p.len; i++) pz.frame[i] = p.data[i % 60];  /* len 검사 없음 */
        pz.parsed++;
    }
}
static void parked(void) { for (;;) vTaskSuspend(NULL); }   /* 폴트 난 태스크가 돌아갈 자리 */
void mm_c(uint32_t *frame)                                /* MemManage (Handler, 특권) */
{
    f_addr = REG32(0xE000ED34u); f_pc = frame[6]; f_at = CNT; faults++;
    REG32(0xE000ED28u) = 0xFFu;                           /* MMFSR 지우기 (write-1-to-clear) */
    frame[6] = (uint32_t)parked & ~1u; frame[7] = 0x01000000u;   /* 예외 복귀 지점을 바꾼다 */
    BaseType_t w = pdFALSE;
    vTaskNotifyGiveFromISR(sup_h, &w);                    /* FromISR = 특권 함수 직접 호출 (SVC 아님) */
    portYIELD_FROM_ISR(w);
}
__attribute__((naked)) void MemManage_Handler(void) { __asm volatile(" mrs r0, psp\n b mm_c\n"); }

static TaskParameters_t pp = {
    .pvTaskCode = parser, .pcName = "parser", .usStackDepth = 256, .uxPriority = 2, .puxStackBuffer = pstack,
    .xRegions = { { &pz, sizeof(pz), portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER }, { 0, 0, 0 }, { 0, 0, 0 } },
};
static void start_parser(void) { pp.pvParameters = (void *)rxq; xTaskCreateRestricted(&pp, &parser_h); vGrantAccessToQueue(parser_h, rxq); }
static void supervisor(void *arg)                         /* 특권, 우선순위 3 */
{
    (void)arg;
    for (int n = 1; ; n++) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        vTaskDelete(parser_h);
        start_parser();
        board_printf_raw("[sup] MemManage addr=0x%x pc=0x%x, parsed=%u, restart #%d (handle %u), fault->restart %u us\n",
                         f_addr, f_pc, pz.parsed, n, (unsigned)(uintptr_t)parser_h, (CNT - f_at) / 25u);
    }
}
static void flight_ctrl(void *arg)                        /* 특권, 최고 우선순위, 2 ms 주기 */
{
    (void)arg; TickType_t last = xTaskGetTickCount(); uint32_t prev = CNT;
    for (int i = 0; i < 100; i++) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(2));
        uint32_t now = CNT, d = now - prev; prev = now;
        if (i) { if (d < fc_dmin) fc_dmin = d; if (d > fc_dmax) fc_dmax = d; }
    }
    board_printf_raw("[fc] sent=12 parsed=%u faults=%u | 100 periods, min/max=%u/%u us\n",
                     pz.parsed, faults, fc_dmin / 25u, fc_dmax / 25u);
    board_exit(0);
}
static void feeder(void *arg)                             /* 특권, 우선순위 1: 10 ms마다 패킷 */
{
    struct pkt p = { 0 }; (void)arg;
    for (int i = 1; i <= 12; i++) { p.len = (!F5_NOFAULT && (i == 4 || i == 9)) ? 200 : 24;
                                    xQueueSend(rxq, &p, portMAX_DELAY); vTaskDelay(pdMS_TO_TICKS(10)); }
    vTaskSuspend(NULL);
}
int main(void)
{
    rxq = xQueueCreate(4, sizeof(struct pkt));
    start_parser();
    xTaskCreate(supervisor, "sup", 512, NULL, 3 | portPRIVILEGE_BIT, &sup_h);
    xTaskCreate(flight_ctrl, "fc", 512, NULL, 4 | portPRIVILEGE_BIT, NULL);
    xTaskCreate(feeder, "feed", 512, NULL, 1 | portPRIVILEGE_BIT, NULL);
    vTaskStartScheduler();
    return 1;
}
```

```sh
limactl shell elinux -- bash -lc 'for n in 1 0; do echo "######## F5_NOFAULT=$n"; make -s -B -f /Users/donh/.rtos_scratch/F5/fr/Makefile SRCS=/Users/donh/.rtos_scratch/F5/fr/rst.c \
  OUT=/var/tmp/rtos/F5/fr_rst$n EXTRA_CFLAGS=-DF5_NOFAULT=$n >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/F5/fr_rst$n/app.elf; done;
  arm-none-eabi-nm -S /var/tmp/rtos/F5/fr_rst0/app.elf | grep -E " (pz|pstack)$"'
```

```text
######## F5_NOFAULT=1
[fc] sent=12 parsed=12 faults=0 | 100 periods, min/max=1998/2001 us
[board_exit] code=0
######## F5_NOFAULT=0
[sup] MemManage addr=0x20009040 pc=0x10df2, parsed=0, restart #1 (handle 2), fault->restart 57 us
[sup] MemManage addr=0x20009040 pc=0x10df2, parsed=0, restart #2 (handle 2), fault->restart 57 us
[fc] sent=12 parsed=3 faults=2 | 100 periods, min/max=1998/2001 us
[board_exit] code=0
20008c00 00000400 b pstack
20009000 00000024 b pz
```

출력에서 볼 것:

- **격리 · 재시작은 됐다**: 폴트 2번, 재시작 2번, 폴트 → 새 파서까지 57 us. 비행 제어 주기는 폴트가 있든 없든 1998/2001 us로 같다. 새 파서의 핸들은 다시 2다 — `vTaskDelete`가 커널 객체 풀의 칸을 돌려줬고, 다음 생성이 같은 칸을 받았다.
- **그런데 `parsed`가 이상하다.** 폴트가 없으면 12인데, 폴트가 있으면 재시작 때 0, 끝에 3이다. 손상 패킷 2개만 버렸다면 10이어야 한다. 이유: `pz`는 `frame[32]` 바로 뒤에 `parsed`가 붙은 36바이트 구조체(nm 크기 0x24)이고 리전은 64바이트다. 길이 200짜리 패킷이 `frame[32..35]` = **`parsed` 자리를 0으로 덮어쓴 뒤** 리전 끝 0x20009040에서 폴트가 났다. 리전 **안**의 손상은 MPU가 못 막는다. 예제 4의 Zephyr 버전은 우연히 `parsed`가 `frame` **앞**에 배치되어(0x20000840 vs 0x20000844) 피해를 피했을 뿐이다.
- 교훈 두 개: (1) 재시작한 태스크는 **자기 리전의 상태를 믿으면 안 된다** — 감독이 리전을 초기화하거나, 지켜야 할 카운터는 다른 리전(또는 커널 쪽)에 둔다. (2) 같은 리전 안에서도 버퍼 뒤에 중요한 변수를 두지 않는다(카나리 · 길이 검사는 여전히 필요하다 — 격리는 길이 검사를 대신하지 않는다).
- 첫 시도에서 파서가 전역 `rxq`를 읽다가 **매 패킷마다**(0x2000881c = `rxq`) 폴트 → 재시작을 수백 번 반복했다. C8 예제 6과 같은 함정이고, 재시작 루프가 있으면 증상이 "멈춤"이 아니라 "조용히 끝없이 재시작"으로 바뀐다. 재시작 횟수 감시가 필요한 이유다.

### 6.3 예제 8 — 공유 리전, ACL grant/revoke, 금지 API, 그리고 리전 반올림 구멍

무엇을 확인하나: R1 · R2 두 restricted 태스크가 `shared`를 각각 RW · RO로 공유하고, R1만 `r1_priv`를 가진다. 큐 `qa`는 둘 다 grant, `qb`는 아무도 안 줌, 실행 중에 R1의 `qa` 권한을 회수. R2의 스택은 일부러 600워드(2400 B) — 2의 거듭제곱이 아니다. BAD 변형으로 금지된 일을 하나씩.

```c
/* F5 예제 8: restricted 태스크 두 개 — 공유 리전(RW/RO), ACL grant/revoke, 비특권 금지 API, 리전 반올림 구멍 */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "board.h"
static uint32_t shared[8] __attribute__((aligned(32)));          /* R1: RW, R2: RO */
static uint32_t r1_priv[8] __attribute__((aligned(32)));         /* R1만 */
#define R2_DEPTH (F5_FIX ? 1024 : 600)                           /* 600워드 = 2400 B → 리전은 4096 B */
static struct { StackType_t stk[R2_DEPTH]; uint32_t victim; } hole __attribute__((aligned(4096)));
static StackType_t s1[256] __attribute__((aligned(1024)));
static QueueHandle_t qa, qb;
static TaskHandle_t h1, h2;
struct args { QueueHandle_t qa, qb; } ar;                        /* 핸들은 R1의 스택으로 복사해 받는다 */

static void r1(void *arg)
{
    struct args a = *(struct args *)arg; uint32_t v = 7;
    shared[0] = 0x5A5A; r1_priv[0] = 1;
    board_printf_raw("[R1] send qa -> %d, send qb (no grant) -> %d\n", (int)xQueueSend(a.qa, &v, 0), (int)xQueueSend(a.qb, &v, 0));
    vTaskDelay(5);                                                /* 그동안 priv가 qa 권한을 회수 */
    board_printf_raw("[R1] after revoke: send qa -> %d\n", (int)xQueueSend(a.qa, &v, 0));
#if F5_BAD == 1
    board_printf_raw("[R1] BAD1: xQueueCreate() from unprivileged\n"); (void)xQueueCreate(1, 4);
#elif F5_BAD == 4
    board_printf_raw("[R1] BAD4: vTaskSuspendAll() (not wrapped in v2)\n"); vTaskSuspendAll();
#endif
    vTaskSuspend(NULL);
}
static void r2(void *arg)
{
    (void)arg; vTaskDelay(2);
    board_printf_raw("[R2] read shared[0]=0x%x (RO region)\n", (unsigned)shared[0]);
#if !F5_FIX
    hole.victim = 0xBAD;                                          /* 스택 리전 반올림으로 열린 곳 */
    board_printf_raw("[R2] wrote hole.victim (past my 2400 B stack) -> no fault! victim=0x%x\n", (unsigned)hole.victim);
    board_printf_raw("[R2] but xQueueSend(qa, &hole.victim) -> %d (syscall check uses exact size)\n",
                     (int)xQueueSend((QueueHandle_t)1, (const void *)&hole.victim, 0));
#endif
#if F5_BAD == 2
    board_printf_raw("[R2] BAD2: write shared[1] (RO)\n"); shared[1] = 1;
#elif F5_BAD == 3
    board_printf_raw("[R2] BAD3: read r1_priv[0]\n"); board_printf_raw("%u\n", (unsigned)r1_priv[0]);
#endif
    vTaskSuspend(NULL);
}
static void priv(void *arg)
{
    (void)arg; vGrantAccessToQueue(h1, qa); vGrantAccessToQueue(h2, qa);
    vTaskDelay(3); vRevokeAccessToQueue(h1, qa);
    board_printf_raw("[priv] revoked qa from R1\n");
    vTaskDelay(20); board_printf_raw("[priv] end, msgs in qa=%u\n", (unsigned)uxQueueMessagesWaiting(qa)); board_exit(0);
}
void mm_c(uint32_t *f)
{
    board_printf_raw("[MemManage] CFSR=0x%x MMFAR=0x%x PC=0x%x\n", REG32(0xE000ED28u), REG32(0xE000ED34u), f[6]);
    board_exit(3);
}
__attribute__((naked)) void MemManage_Handler(void) { __asm volatile(" mrs r0, psp\n b mm_c\n"); }
int main(void)
{
    qa = xQueueCreate(4, 4); qb = xQueueCreate(4, 4); ar.qa = qa; ar.qb = qb;
    TaskParameters_t p1 = { .pvTaskCode = r1, .pcName = "R1", .usStackDepth = 256, .pvParameters = &ar,
        .uxPriority = 2, .puxStackBuffer = s1, .xRegions = { { shared, 32, portMPU_REGION_READ_WRITE },
        { r1_priv, 32, portMPU_REGION_READ_WRITE }, { &ar, sizeof(ar), portMPU_REGION_READ_ONLY } } };
    TaskParameters_t p2 = { .pvTaskCode = r2, .pcName = "R2", .usStackDepth = R2_DEPTH, .uxPriority = 2,
        .puxStackBuffer = hole.stk, .xRegions = { { shared, 32, portMPU_REGION_READ_ONLY } } };
    xTaskCreateRestricted(&p1, &h1); xTaskCreateRestricted(&p2, &h2);
    xTaskCreate(priv, "priv", 512, NULL, 3 | portPRIVILEGE_BIT, NULL);
    board_printf_raw("[main] qa=%u qb=%u h1=%u h2=%u, hole.stk=0x%x victim=0x%x\n", (unsigned)(uintptr_t)qa, (unsigned)(uintptr_t)qb,
                     (unsigned)(uintptr_t)h1, (unsigned)(uintptr_t)h2, (unsigned)(uintptr_t)hole.stk, (unsigned)(uintptr_t)&hole.victim);
    vTaskStartScheduler();
    return 1;
}
```

먼저 기본 실행과 BAD 1–4(`F5_FIX=0`).

```sh
limactl shell elinux -- bash -lc 'for b in 0 1 2 3 4; do echo "######## F5_BAD=$b F5_FIX=0"; make -s -B -f /Users/donh/.rtos_scratch/F5/fr/Makefile SRCS=/Users/donh/.rtos_scratch/F5/fr/acl.c \
  OUT=/var/tmp/rtos/F5/fr_acl$b "EXTRA_CFLAGS=-DF5_BAD=$b -DF5_FIX=0" | grep -v "^\.\(priv\|free\|text\|data\|bss\)"; ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/F5/fr_acl$b/app.elf; done'
```

```text
######## F5_BAD=0 F5_FIX=0
[main] qa=1 qb=2 h1=3 h2=4, hole.stk=0x2000a000 victim=0x2000a960
[R1] send qa -> 1, send qb (no grant) -> 0
[R2] read shared[0]=0x5a5a (RO region)
[R2] wrote hole.victim (past my 2400 B stack) -> no fault! victim=0xbad
[R2] but xQueueSend(qa, &hole.victim) -> 0 (syscall check uses exact size)
[priv] revoked qa from R1
[R1] after revoke: send qa -> 0
[priv] end, msgs in qa=1
[board_exit] code=0
######## F5_BAD=1 F5_FIX=0
...
[R1] after revoke: send qa -> 0
[R1] BAD1: xQueueCreate() from unprivileged
[MemManage] CFSR=0x1 MMFAR=0x0 PC=0x785c
[board_exit] code=3
######## F5_BAD=2 F5_FIX=0
...
[R2] BAD2: write shared[1] (RO)
[priv] revoked qa from R1
[R1] after revoke: send qa -> 0
[priv] end, msgs in qa=1
[board_exit] code=0
######## F5_BAD=3 F5_FIX=0
...
[R2] BAD3: read r1_priv[0]
1
[priv] revoked qa from R1
[R1] after revoke: send qa -> 0
[priv] end, msgs in qa=1
[board_exit] code=0
######## F5_BAD=4 F5_FIX=0
...
[R1] after revoke: send qa -> 0
[R1] BAD4: vTaskSuspendAll() (not wrapped in v2)
[MemManage] CFSR=0x1 MMFAR=0x0 PC=0xd90
[board_exit] code=3
```

(BAD 1–4의 앞부분 5줄은 BAD0과 같아 `...`로 줄였다.)

```sh
limactl shell elinux -- bash -lc 'E=/var/tmp/rtos/F5/fr_acl1/app.elf; arm-none-eabi-addr2line -f -e $E 0x785c | head -1; arm-none-eabi-addr2line -f -e /var/tmp/rtos/F5/fr_acl4/app.elf 0xd90 | head -1;
  arm-none-eabi-nm -n -S /var/tmp/rtos/F5/fr_acl2/app.elf | grep -E " (shared|r1_priv|hole|ar)$"'
```

```text
MPU_xQueueGenericCreate
vTaskSuspendAll
20009000 00000008 B ar
2000a000 00000964 b hole
2000a980 00000020 b r1_priv
2000a9a0 00000020 b shared
```

출력에서 볼 것 — 정상 동작:

- **ACL**: `qb`(grant 안 함) 송신 0, `qa` 1. `vRevokeAccessToQueue` 뒤 `qa`도 0. 권한 회수는 즉시 먹고 예외 없이 조용히 실패한다. 실패를 반환값으로만 알 수 있으니 **restricted 태스크의 모든 API 반환값을 검사**해야 한다.
- **공유 리전**: R2가 RO 리전으로 R1이 쓴 `0x5a5a`를 읽었다.

출력에서 볼 것 — 놀라운 것 두 가지:

- **BAD1 `xQueueCreate`는 NULL을 돌려주지 않고 MemManage로 죽는다.** PC 0x785c = `MPU_xQueueGenericCreate`, CFSR 0x1 = IACCVIOL(명령 접근 위반). v2에서 생성 API(`xQueueCreate`, `xTaskCreate`, …)는 SVC 래퍼가 아니라 `privileged_functions` 섹션에 있는 **특권 전용 래퍼**(`mpu_wrappers_v2.c` 2897행, `/* PRIVILEGED_FUNCTION */`)라서, 비특권 코드가 부르면 그 함수의 첫 명령을 가져오는 순간 리전 6(커널 코드 특권 전용)에 걸린다. **C8 §9 표의 "생성 실패(NULL)"는 틀렸다** — 실제로는 MemManage다. BAD4 `vTaskSuspendAll`(v2에서는 래핑되지 않음, PC 0xd90)도 같은 방식으로 죽는다.
- **BAD2(RO 리전에 쓰기)와 BAD3(R1의 private 읽기)가 폴트 없이 성공했다.** R2가 RO인 `shared[1]`에 썼고, 남의 `r1_priv[0]=1`을 읽었다. nm을 보면 이유가 나온다: `hole`은 `aligned(4096)`이지만 크기는 0x964(2404 B)뿐이라, 링커가 그 바로 뒤 0x2000a980에 `r1_priv`, 0x2000a9a0에 `shared`를 넣었다. 그런데 R2의 스택 리전은 2400 B를 **4096 B로 반올림**해 0x2000a000–0x2000afff를 연다(`port.c` 1191행 `prvGetMPURegionSizeSetting`이 "요청 크기 이상인 가장 작은 2의 거듭제곱"을 고른다). 스택 리전은 리전 3이고 겹칠 때는 **번호가 큰 리전이 이긴다** — 리전 0의 "`shared`는 RO"보다 리전 3의 "여기 전부 RW"가 우선한다. 그래서 `victim`(0x2000a960)뿐 아니라 다른 태스크의 비밀(`r1_priv`)과 공유 데이터까지 R2의 손에 열렸다.
- **그런데 시스템 콜 검사는 막았다**: 같은 `&hole.victim`을 `xQueueSend`의 버퍼로 넘기자 0. v2의 `xPortIsAuthorizedToAccessBuffer()`(`port.c` 1389행)는 MPU 레지스터가 아니라 TCB에 저장한 **정확한 시작 · 끝 주소**(`xRegionSettings[].ulRegionStartAddress/EndAddress`, 1330–1331행에서 `uxStackDepth × sizeof(StackType_t) − 1`로 계산)로 판정하기 때문이다. Zephyr의 `arch_buffer_validate`는 반대로 **실제 MPU 리전**을 본다(§4.3). 하드웨어와 소프트웨어 검사의 기준이 다르면 이런 틈이 생긴다.

스택을 1024워드(4096 B, 2의 거듭제곱)로 고치면(`F5_FIX=1`):

```sh
limactl shell elinux -- bash -lc 'for b in 2 3; do echo "######## F5_BAD=$b F5_FIX=1"; make -s -B -f /Users/donh/.rtos_scratch/F5/fr/Makefile SRCS=/Users/donh/.rtos_scratch/F5/fr/acl.c \
  OUT=/var/tmp/rtos/F5/fr_aclf$b "EXTRA_CFLAGS=-DF5_BAD=$b -DF5_FIX=1" | grep -v "^\.\(priv\|free\|text\|data\|bss\)"; ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/F5/fr_aclf$b/app.elf | grep -v "R1\]\|priv\]";
  arm-none-eabi-nm -n -S /var/tmp/rtos/F5/fr_aclf$b/app.elf | grep -E " (shared|r1_priv|hole)$"; done'
```

```text
######## F5_BAD=2 F5_FIX=1
[main] qa=1 qb=2 h1=3 h2=4, hole.stk=0x2000a000 victim=0x2000b000
[R2] read shared[0]=0x5a5a (RO region)
[R2] BAD2: write shared[1] (RO)
[MemManage] CFSR=0x82 MMFAR=0x2000b044 PC=0x1054c
[board_exit] code=3
2000a000 00001004 b hole
2000b020 00000020 b r1_priv
2000b040 00000020 b shared
######## F5_BAD=3 F5_FIX=1
[main] qa=1 qb=2 h1=3 h2=4, hole.stk=0x2000a000 victim=0x2000b000
[R2] read shared[0]=0x5a5a (RO region)
[R2] BAD3: read r1_priv[0]
[MemManage] CFSR=0x82 MMFAR=0x2000b020 PC=0x1054c
[board_exit] code=3
2000a000 00001004 b hole
2000b020 00000020 b r1_priv
2000b040 00000020 b shared
```

(R1 · priv 줄은 `grep -v`로 뺐다.)

이번에는 둘 다 MemManage(CFSR 0x82 = MMARVALID + DACCVIOL). BAD2의 MMFAR 0x2000b044 = `shared[1]`(RO 리전에 쓰기), BAD3의 MMFAR 0x2000b020 = `r1_priv[0]`(리전 없음). 스택 리전이 정확히 4096 B가 되자 `hole` 크기가 0x1004가 되어 두 변수가 리전 밖(0x2000b000 이후)으로 밀려났고, 이제야 리전 0의 RO 설정과 "R1만" 설정이 의도대로 먹는다. `victim`(0x2000b000)도 리전 밖이라 `F5_FIX=1`에서는 그 줄을 빼고 빌드했다(쓰면 폴트).

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<text x="340" y="20" font-size="13" text-anchor="middle" font-weight="bold">R2 스택 600워드(2400 B) → MPU 리전 4096 B: 반올림된 꼬리에 남의 변수가 들어왔다</text>
<rect x="40" y="40" width="600" height="150" rx="4" fill="#d0564a" fill-opacity="0.06" stroke="#d0564a" stroke-dasharray="6,4"/>
<text x="634" y="56" font-size="12" text-anchor="end">리전 3 (스택): 0x2000a000–0x2000afff, RW, 번호가 가장 커서 겹치면 이긴다</text>
<rect x="50" y="66" width="352" height="50" rx="3" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<text x="226" y="88" font-size="12" text-anchor="middle">hole.stk — R2의 진짜 스택 2400 B</text>
<text x="226" y="106" font-size="12" text-anchor="middle">0x2000a000 – 0x2000a95f</text>
<rect x="404" y="66" width="70" height="50" rx="3" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c"/>
<text x="439" y="88" font-size="12" text-anchor="middle">victim</text>
<text x="439" y="106" font-size="12" text-anchor="middle">a960</text>
<rect x="476" y="66" width="74" height="50" rx="3" fill="#d0564a" fill-opacity="0.15" stroke="#d0564a"/>
<text x="513" y="88" font-size="12" text-anchor="middle">r1_priv</text>
<text x="513" y="106" font-size="12" text-anchor="middle">a980</text>
<rect x="552" y="66" width="80" height="50" rx="3" fill="#3f9a6b" fill-opacity="0.15" stroke="#3f9a6b"/>
<text x="592" y="88" font-size="12" text-anchor="middle">shared</text>
<text x="592" y="106" font-size="12" text-anchor="middle">a9a0 (RO?)</text>
<text x="340" y="140" font-size="12" text-anchor="middle">하드웨어: 이 4 KB 전체를 R2에게 RW로 연다 → victim 쓰기 · r1_priv 읽기 · shared 쓰기 전부 폴트 없음</text>
<text x="340" y="160" font-size="12" text-anchor="middle">리전 0이 shared를 RO로 줬지만 리전 3(RW)이 덮어쓴다</text>
<text x="340" y="180" font-size="12" text-anchor="middle">… 0x2000afff까지 열려 있다</text>
<rect x="40" y="206" width="600" height="56" rx="4" fill="#3f9a6b" fill-opacity="0.10" stroke="#3f9a6b"/>
<text x="340" y="228" font-size="12" text-anchor="middle">소프트웨어(v2 syscall 검사): TCB의 정확한 범위 0x2000a000–0x2000a95f로 판정</text>
<text x="340" y="248" font-size="12" text-anchor="middle">→ xQueueSend(qa, &amp;hole.victim)은 거절 (0)</text>
<text x="340" y="280" font-size="12" text-anchor="middle">고치는 법: 스택 · 리전 크기 = 2의 거듭제곱, 시작 주소 = 크기 정렬</text>
<text x="340" y="296" font-size="12" text-anchor="middle">(F5_FIX=1: shared · r1_priv가 0x2000b000 뒤로 밀려 폴트가 난다)</text>
</svg>
```

그림 5 — 예제 8에서 실제로 일어난 일(nm 주소). 2의 거듭제곱이 아닌 스택이 만든 4 KB 리전의 꼬리에 링커가 다른 태스크의 변수를 넣었고, 번호가 큰 스택 리전이 RO 리전까지 이겼다.

### 6.4 restricted 태스크가 못 하는 일 — 실제로 어떻게 막히나

C8 예제 9와 이 노트 예제 7 · 8에서 실행으로 확인한 것을 한 표에 모은다.

| 시도 | 결과 | 막는 장치 | 근거 |
|---|---|---|---|
| 리전 밖 전역 읽기 · 쓰기 | MemManage (DACCVIOL) | MPU, 배경 맵은 특권 전용 | C8 예제 9 BAD1, 이 노트 예제 7 첫 시도(`rxq`) |
| 커널 데이터(`pxCurrentTCB`) 읽기 | MemManage | 리전 7 `privileged_data` | C8 BAD2 |
| 커널 함수 직접 호출 | MemManage (IACCVIOL) | 리전 6 `privileged_functions` | C8 BAD4 |
| `xQueueCreate`, `xTaskCreate` 등 생성 API | **MemManage (IACCVIOL)** — NULL 반환이 아니다 | 생성 API의 v2 래퍼가 특권 전용 섹션에 있음 | 예제 8 BAD1 (PC = `MPU_xQueueGenericCreate`) |
| `vTaskSuspendAll` | MemManage (IACCVIOL) | v2에서 래핑 안 됨 → 커널 함수 직접 호출 | 예제 8 BAD4 |
| grant 안 받은 큐 · revoke된 큐 | `pdFAIL` (예외 없음) | ACL `xPortIsAuthorizedToAccessKernelObject` | 예제 8 (`qb`, revoke 뒤 `qa`) |
| 날 포인터를 핸들로 | 0 반환 | 핸들 = 인덱스 검사 | C8 BAD5 |
| 내 리전 밖 버퍼를 syscall에 | `pdFAIL` | `xPortIsAuthorizedToAccessBuffer` (정확한 범위) | 예제 8 (`&hole.victim`) |
| 앱 코드에서 직접 `svc` | 무시 | SVC 발신 PC가 syscall 섹션 안인지 검사 | C8 BAD3 |
| CONTROL로 특권 올리기 | 무시 | 하드웨어 | C8 BAD6 |
| 크리티컬 섹션(`taskENTER_CRITICAL`) | 불가 (v2는 `configALLOW_UNPRIVILEGED_CRITICAL_SECTIONS` 0 강제) | 포트 `#error` | C8 §3.3 |
| **반올림된 리전 꼬리의 남의 변수** | **막지 못함** | — | 예제 8 BAD2 · BAD3 (`F5_FIX=0`) |

마지막 줄이 이 노트에서 가장 중요한 발견이다. 격리는 **설정이 맞을 때만** 격리다. 리뷰할 때 "스택 · 리전 크기가 2의 거듭제곱인가, 시작 주소가 크기에 정렬됐나, 리전 꼬리에 무엇이 링크됐나(map 파일)"를 반드시 본다.

---

## 7. 비용 — API 한 번이 아니라 앱 전체로 잰다

C8은 API **한 번**의 비용을 쟀다(비특권 `xQueueSend` 13.2 us vs 특권 6.9 us, Zephyr user `k_msgq_put` 12.4 us vs supervisor 2.7 us). 그런데 실제 질문은 "내 데이터 흐름이 얼마나 느려지나"다. 그래서 **같은 producer/consumer 작업**을 격리 없음 / 격리 빌드의 특권 쪽 / 격리 빌드의 비특권 쪽으로 돌리고, 메시지 하나에 항목 1개(배치 1)와 16개(배치 16), 항목당 계산 0회와 100회(work)를 바꿔 본다. 소비자 우선순위가 높아서 배치 1이면 **항목마다 컨텍스트 스위치 2번**이 일어나는 최악 형태다.

### 7.1 예제 5 — Zephyr: supervisor vs user, 배치 1 vs 16

```c
/* F5 예제 5: 같은 producer/consumer 작업을 supervisor 스레드 / user 스레드로, 배치 1 / 16으로 돌려 잰다 */
#include <zephyr/kernel.h>
#include <zephyr/app_memory/app_memdomain.h>
#include "f5z.h"
#define N 2048
K_MSGQ_DEFINE(q1, sizeof(uint32_t), 16, 4);              /* 항목 1개 = 메시지 1개 */
K_MSGQ_DEFINE(q16, 16 * sizeof(uint32_t), 4, 4);         /* 항목 16개 = 메시지 1개 */
K_APPMEM_PARTITION_DEFINE(part_w);
K_APP_BMEM(part_w) volatile uint32_t sink;
K_APP_BMEM(part_w) uint32_t cfg_batch, cfg_work;
K_THREAD_STACK_DEFINE(st_p, 1024);
K_THREAD_STACK_DEFINE(st_c, 1024);
static struct k_thread th_p, th_c;

static uint32_t work(uint32_t x, uint32_t w)             /* 필터 계산 흉내: w번 정수 연산 */
{
    for (uint32_t i = 0; i < w; i++) x = x * 1103515245u + 12345u;
    return x;
}
static void producer(void *p1, void *p2, void *p3)
{
    uint32_t b[16];
    for (uint32_t i = 0; i < N; i += cfg_batch) {
        for (uint32_t k = 0; k < cfg_batch; k++) b[k] = i + k;
        k_msgq_put(cfg_batch == 1 ? &q1 : &q16, b, K_FOREVER);
    }
}
static void consumer(void *p1, void *p2, void *p3)
{
    uint32_t b[16], acc = 0;
    for (uint32_t i = 0; i < N; i += cfg_batch) {
        k_msgq_get(cfg_batch == 1 ? &q1 : &q16, b, K_FOREVER);
        for (uint32_t k = 0; k < cfg_batch; k++) acc += work(b[k], cfg_work);
    }
    sink = acc;
}
static void run(uint32_t opt, uint32_t batch, uint32_t w)
{
    cfg_batch = batch; cfg_work = w;
    k_tid_t c = k_thread_create(&th_c, st_c, K_THREAD_STACK_SIZEOF(st_c), consumer, NULL, NULL, NULL, 5, opt, K_FOREVER);
    k_tid_t p = k_thread_create(&th_p, st_p, K_THREAD_STACK_SIZEOF(st_p), producer, NULL, NULL, NULL, 6, opt, K_FOREVER);
    k_thread_access_grant(c, &q1, &q16);
    k_thread_access_grant(p, &q1, &q16);
    uint32_t t0 = FPGAIO_COUNTER;
    k_thread_start(c); k_thread_start(p);
    k_thread_join(c, K_FOREVER); k_thread_join(p, K_FOREVER);
    uint32_t ns = (FPGAIO_COUNTER - t0) * 40u / N;
    printk("RESULT us=%d %-4s batch=%2u work=%3u : %6u ns/item  sink=%08x\n", IS_ENABLED(CONFIG_USERSPACE),
           opt ? "user" : "supv", batch, w, ns, sink);
}
int main(void)
{
#ifdef CONFIG_USERSPACE
    k_mem_domain_add_partition(&k_mem_domain_default, &part_w);
#endif
    static const uint32_t works[] = { 0, 100 }, batches[] = { 1, 16 };
    for (int wi = 0; wi < 2; wi++)
        for (int bi = 0; bi < 2; bi++) {
            run(0, batches[bi], works[wi]);
            if (IS_ENABLED(CONFIG_USERSPACE)) run(K_USER, batches[bi], works[wi]);
        }
    f5_exit();
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'echo "## USERSPACE=y"; /Users/donh/.rtos_scratch/F5/zrun.sh zcost cost mps2/an385 | grep -v Booting;
  echo "## USERSPACE=n"; /Users/donh/.rtos_scratch/F5/zrun.sh zcost costn mps2/an385 -- -DCONFIG_USERSPACE=n | grep -v Booting'
```

```text
## USERSPACE=y
           FLASH:       49060 B         4 MB      1.17%
             RAM:         19 KB         4 MB      0.46%
RESULT us=1 supv batch= 1 work=  0 :  53785 ns/item  sink=001ffc00
RESULT us=1 user batch= 1 work=  0 :  77323 ns/item  sink=001ffc00
RESULT us=1 supv batch=16 work=  0 :   4345 ns/item  sink=001ffc00
RESULT us=1 user batch=16 work=  0 :   5953 ns/item  sink=001ffc00
RESULT us=1 supv batch= 1 work=100 :  69785 ns/item  sink=9c981c00
RESULT us=1 user batch= 1 work=100 :  93323 ns/item  sink=9c981c00
RESULT us=1 supv batch=16 work=100 :  20345 ns/item  sink=9c981c00
RESULT us=1 user batch=16 work=100 :  21953 ns/item  sink=9c981c00
## USERSPACE=n
           FLASH:       15096 B         4 MB      0.36%
             RAM:       10016 B         4 MB      0.24%
RESULT us=0 supv batch= 1 work=  0 :  39990 ns/item  sink=001ffc00
RESULT us=0 supv batch=16 work=  0 :   3450 ns/item  sink=001ffc00
RESULT us=0 supv batch= 1 work=100 :  55990 ns/item  sink=9c981c00
RESULT us=0 supv batch=16 work=100 :  19450 ns/item  sink=9c981c00
```

`sink`(검산값)가 구성마다 같아서 모두 같은 일을 했다는 것이 확인된다. 시간에는 스레드 생성 · join 비용도 포함된다(2048항목으로 나누면 작다).

### 7.2 예제 9 — FreeRTOS: 기본 포트 vs MPU 특권 vs restricted

같은 작업을 FreeRTOS로. 생산자 · 소비자는 둘 다 `xTaskCreateRestricted`로 만들고 `uxPriority`에 `portPRIVILEGE_BIT`를 넣느냐만 바꾼다(그래서 특권 쪽도 리전 · ACL 설정이 같다). 기본 포트 빌드는 같은 소스를 랩 템플릿(`ARM_CM3`)으로 빌드한다 — `portUSING_MPU_WRAPPERS`가 0이면 `xTaskCreate`로 간다. 기본 포트용 소스는 MPU용 `FreeRTOSConfig.h`가 없는 다른 폴더에 복사했다(랩 템플릿은 **소스 폴더의 `FreeRTOSConfig.h`를 우선 쓰므로**, 같은 폴더에서 빌드하면 MPU 설정이 섞여 `vApplicationGetIdleTaskMemory` 링크 오류가 난다 — 한 번 겪었다).

```c
/* F5 예제 9: 같은 producer/consumer를 특권 태스크 / restricted 태스크로, 배치 1 / 16으로 (FreeRTOS) */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "board.h"
#define CNT REG32(0x40028018u)
#define N 2048
struct cfg { QueueHandle_t q, dq; uint32_t batch, work; };
static struct cfg cf __attribute__((aligned(32)));               /* 태스크에 RO 리전으로 준다 */
#if portUSING_MPU_WRAPPERS == 1
static StackType_t sp_[256] __attribute__((aligned(1024))), sc_[256] __attribute__((aligned(1024)));
#endif
static uint32_t work(uint32_t x, uint32_t w) { for (uint32_t i = 0; i < w; i++) x = x * 1103515245u + 12345u; return x; }

static void producer(void *arg)
{
    struct cfg c = *(struct cfg *)arg; uint32_t b[16];
    for (uint32_t i = 0; i < N; i += c.batch) {
        for (uint32_t k = 0; k < c.batch; k++) b[k] = i + k;
        xQueueSend(c.q, b, portMAX_DELAY);
    }
    vTaskSuspend(NULL);
}
static void consumer(void *arg)
{
    struct cfg c = *(struct cfg *)arg; uint32_t b[16], acc = 0;
    for (uint32_t i = 0; i < N; i += c.batch) {
        xQueueReceive(c.q, b, portMAX_DELAY);
        for (uint32_t k = 0; k < c.batch; k++) acc += work(b[k], c.work);
    }
    xQueueSend(c.dq, &acc, portMAX_DELAY);
    vTaskSuspend(NULL);
}
static void run(QueueHandle_t q, QueueHandle_t dq, UBaseType_t priv, uint32_t batch, uint32_t w)
{
    TaskHandle_t hc, hp; uint32_t acc;
    cf = (struct cfg){ q, dq, batch, w };
#if portUSING_MPU_WRAPPERS == 1
    TaskParameters_t pc = { .pvTaskCode = consumer, .pcName = "c", .usStackDepth = 256, .pvParameters = &cf,
        .uxPriority = 3 | priv, .puxStackBuffer = sc_, .xRegions = { { &cf, 32, portMPU_REGION_READ_ONLY } } };
    TaskParameters_t pp = pc; pp.pvTaskCode = producer; pp.uxPriority = 2 | priv; pp.puxStackBuffer = sp_;
    uint32_t t0 = CNT;
    xTaskCreateRestricted(&pc, &hc); xTaskCreateRestricted(&pp, &hp);
    vGrantAccessToQueue(hc, q); vGrantAccessToQueue(hp, q); vGrantAccessToQueue(hc, dq);
#else
    uint32_t t0 = CNT;
    xTaskCreate(consumer, "c", 256, &cf, 3, &hc); xTaskCreate(producer, "p", 256, &cf, 2, &hp);
#endif
    xQueueReceive(dq, &acc, portMAX_DELAY);
    uint32_t ns = (CNT - t0) * 40u / N;
    vTaskDelete(hc); vTaskDelete(hp); vTaskDelay(1);              /* idle이 TCB를 정리하게 */
    board_printf_raw("RESULT mpu=%d %s batch=%u work=%u : %u ns/item  acc=%x\n", portUSING_MPU_WRAPPERS,
                     priv ? "priv" : (portUSING_MPU_WRAPPERS ? "rstr" : "task"), batch, w, ns, acc);
}
static void ctrl(void *arg)                                         /* 특권, 우선순위 4 */
{
    (void)arg;
    QueueHandle_t q1 = xQueueCreate(16, 4), q16 = xQueueCreate(4, 64), dq = xQueueCreate(1, 4);
    static const uint32_t works[] = { 0, 100 }, batches[] = { 1, 16 };
    for (int wi = 0; wi < 2; wi++)
        for (int bi = 0; bi < 2; bi++) {
            run(batches[bi] == 1 ? q1 : q16, dq, portPRIVILEGE_BIT, batches[bi], works[wi]);
            if (portUSING_MPU_WRAPPERS) run(batches[bi] == 1 ? q1 : q16, dq, 0, batches[bi], works[wi]);
        }
    board_exit(0);
}
int main(void)
{
    xTaskCreate(ctrl, "ctrl", 512, NULL, 4 | portPRIVILEGE_BIT, NULL);
    vTaskStartScheduler();
    return 1;
}
```

```sh
limactl shell elinux -- bash -lc 'echo "## FreeRTOS-MPU v2 (ARM_CM3_MPU)"; make -s -B -f /Users/donh/.rtos_scratch/F5/fr/Makefile SRCS=/Users/donh/.rtos_scratch/F5/fr/cost.c OUT=/var/tmp/rtos/F5/fr_cost >/dev/null \
  && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/F5/fr_cost/app.elf 60;
  echo "## plain FreeRTOS (ARM_CM3)"; make -s -B -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/F5/frplain/cost.c OUT=/var/tmp/rtos/F5/fr_costplain 2>&1 | grep -i warn;
  ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/F5/fr_costplain/app.elf 60'
```

```text
## FreeRTOS-MPU v2 (ARM_CM3_MPU)
RESULT mpu=1 priv batch=1 work=0 : 33326 ns/item  acc=1ffc00
RESULT mpu=1 rstr batch=1 work=0 : 46018 ns/item  acc=1ffc00
RESULT mpu=1 priv batch=16 work=0 : 2504 ns/item  acc=1ffc00
RESULT mpu=1 rstr batch=16 work=0 : 3298 ns/item  acc=1ffc00
RESULT mpu=1 priv batch=1 work=100 : 46177 ns/item  acc=9c981c00
RESULT mpu=1 rstr batch=1 work=100 : 58865 ns/item  acc=9c981c00
RESULT mpu=1 priv batch=16 work=100 : 15385 ns/item  acc=9c981c00
RESULT mpu=1 rstr batch=16 work=100 : 16179 ns/item  acc=9c981c00
[board_exit] code=0
## plain FreeRTOS (ARM_CM3)
RESULT mpu=0 task batch=1 work=0 : 29018 ns/item  acc=1ffc00
RESULT mpu=0 task batch=16 work=0 : 2220 ns/item  acc=1ffc00
RESULT mpu=0 task batch=1 work=100 : 41867 ns/item  acc=9c981c00
RESULT mpu=0 task batch=16 work=100 : 15101 ns/item  acc=9c981c00
[board_exit] code=0
```

(첫 시도에서 `board_printf_raw`에 `%-4s`, `%2u`를 넣었더니 `-4s`, `2u`가 그대로 찍혔다. 랩의 `board_printf_raw`는 폭 지정자를 지원하지 않는다 — 형식을 단순하게 바꿨다.)

### 7.3 숫자를 읽는 법 — 차트 두 장

```svg
<svg viewBox="0 0 680 201" xmlns="http://www.w3.org/2000/svg">
<text x="168" y="47" font-size="12" text-anchor="end">Zephyr USERSPACE=n</text>
<text x="168" y="65" font-size="12" text-anchor="end">Zephyr =y supervisor</text>
<text x="168" y="83" font-size="12" text-anchor="end">Zephyr =y user</text>
<text x="168" y="101" font-size="12" text-anchor="end">FreeRTOS 기본 포트</text>
<text x="168" y="119" font-size="12" text-anchor="end">FreeRTOS-MPU 특권</text>
<text x="168" y="137" font-size="12" text-anchor="end">FreeRTOS-MPU restricted</text>
<text x="276" y="22" font-size="13" text-anchor="middle" font-weight="bold">배치 1 (항목마다 메시지 1개)</text>
<rect x="176" y="36" width="100.0" height="14" fill="#3f9a6b" fill-opacity="0.8"/>
<text x="280.0" y="47" font-size="12">40.0</text>
<rect x="176" y="54" width="134.5" height="14" fill="#4a7bd0" fill-opacity="0.8"/>
<text x="314.5" y="65" font-size="12">53.8</text>
<rect x="176" y="72" width="193.3" height="14" fill="#e08a3c" fill-opacity="0.8"/>
<text x="373.3" y="83" font-size="12">77.3</text>
<rect x="176" y="90" width="72.5" height="14" fill="#3f9a6b" fill-opacity="0.8"/>
<text x="252.5" y="101" font-size="12">29.0</text>
<rect x="176" y="108" width="83.3" height="14" fill="#4a7bd0" fill-opacity="0.8"/>
<text x="263.3" y="119" font-size="12">33.3</text>
<rect x="176" y="126" width="115.0" height="14" fill="#e08a3c" fill-opacity="0.8"/>
<text x="295.0" y="137" font-size="12">46.0</text>
<line x1="176" y1="32" x2="176" y2="146" stroke="currentColor"/>
<line x1="176.0" y1="146" x2="176.0" y2="150" stroke="currentColor"/>
<text x="176.0" y="163" font-size="12" text-anchor="middle">0</text>
<line x1="226.0" y1="146" x2="226.0" y2="150" stroke="currentColor"/>
<text x="226.0" y="163" font-size="12" text-anchor="middle">20</text>
<line x1="276.0" y1="146" x2="276.0" y2="150" stroke="currentColor"/>
<text x="276.0" y="163" font-size="12" text-anchor="middle">40</text>
<line x1="326.0" y1="146" x2="326.0" y2="150" stroke="currentColor"/>
<text x="326.0" y="163" font-size="12" text-anchor="middle">60</text>
<line x1="376.0" y1="146" x2="376.0" y2="150" stroke="currentColor"/>
<text x="376.0" y="163" font-size="12" text-anchor="middle">80</text>
<text x="545" y="22" font-size="13" text-anchor="middle" font-weight="bold">배치 16</text>
<rect x="470" y="36" width="64.7" height="14" fill="#3f9a6b" fill-opacity="0.8"/>
<text x="538.7" y="47" font-size="12">3.45</text>
<rect x="470" y="54" width="81.5" height="14" fill="#4a7bd0" fill-opacity="0.8"/>
<text x="555.5" y="65" font-size="12">4.34</text>
<rect x="470" y="72" width="111.6" height="14" fill="#e08a3c" fill-opacity="0.8"/>
<text x="585.6" y="83" font-size="12">5.95</text>
<rect x="470" y="90" width="41.6" height="14" fill="#3f9a6b" fill-opacity="0.8"/>
<text x="515.6" y="101" font-size="12">2.22</text>
<rect x="470" y="108" width="47.0" height="14" fill="#4a7bd0" fill-opacity="0.8"/>
<text x="521.0" y="119" font-size="12">2.50</text>
<rect x="470" y="126" width="61.8" height="14" fill="#e08a3c" fill-opacity="0.8"/>
<text x="535.8" y="137" font-size="12">3.30</text>
<line x1="470" y1="32" x2="470" y2="146" stroke="currentColor"/>
<line x1="470.0" y1="146" x2="470.0" y2="150" stroke="currentColor"/>
<text x="470.0" y="163" font-size="12" text-anchor="middle">0</text>
<line x1="507.5" y1="146" x2="507.5" y2="150" stroke="currentColor"/>
<text x="507.5" y="163" font-size="12" text-anchor="middle">2</text>
<line x1="545.0" y1="146" x2="545.0" y2="150" stroke="currentColor"/>
<text x="545.0" y="163" font-size="12" text-anchor="middle">4</text>
<line x1="582.5" y1="146" x2="582.5" y2="150" stroke="currentColor"/>
<text x="582.5" y="163" font-size="12" text-anchor="middle">6</text>
<line x1="620.0" y1="146" x2="620.0" y2="150" stroke="currentColor"/>
<text x="620.0" y="163" font-size="12" text-anchor="middle">8</text>
<text x="340" y="185" font-size="12" text-anchor="middle">us / item (work=0, 2048 항목, QEMU icount)</text>
</svg>
```

그림 6 — 예제 5 · 9에서 잰 항목 하나당 시간(work=0, QEMU icount, 실제 칩과 절대값은 다르다). 초록 = 격리 없는 빌드, 파랑 = 격리 빌드의 특권 쪽, 주황 = 격리 빌드의 비특권 쪽. 왼쪽과 오른쪽은 축 눈금이 10배 다르다.

```svg
<svg viewBox="0 0 680 304" xmlns="http://www.w3.org/2000/svg">
<text x="340" y="20" font-size="13" text-anchor="middle" font-weight="bold">격리 비용 = item당 추가 시간 (us) — 괄호는 기준 대비 %</text>
<rect x="150" y="32" width="12" height="10" fill="#e08a3c" fill-opacity="0.8"/><text x="166" y="41" font-size="12">Zephyr user − USERSPACE=n</text>
<rect x="380" y="32" width="12" height="10" fill="#4a7bd0" fill-opacity="0.8"/><text x="396" y="41" font-size="12">FreeRTOS restricted − 기본 포트</text>
<text x="142" y="76" font-size="12" text-anchor="end">배치 1, work 0</text>
<rect x="150" y="56" width="373.3" height="14" fill="#e08a3c" fill-opacity="0.8"/>
<text x="527.3" y="67" font-size="12">37.33 (93%)</text>
<rect x="150" y="73" width="170.0" height="14" fill="#4a7bd0" fill-opacity="0.8"/>
<text x="324.0" y="84" font-size="12">17.00 (59%)</text>
<text x="142" y="126" font-size="12" text-anchor="end">배치 16, work 0</text>
<rect x="150" y="106" width="25.0" height="14" fill="#e08a3c" fill-opacity="0.8"/>
<text x="179.0" y="117" font-size="12">2.50 (73%)</text>
<rect x="150" y="123" width="10.8" height="14" fill="#4a7bd0" fill-opacity="0.8"/>
<text x="164.8" y="134" font-size="12">1.08 (49%)</text>
<text x="142" y="176" font-size="12" text-anchor="end">배치 1, work 100</text>
<rect x="150" y="156" width="373.3" height="14" fill="#e08a3c" fill-opacity="0.8"/>
<text x="527.3" y="167" font-size="12">37.33 (67%)</text>
<rect x="150" y="173" width="170.0" height="14" fill="#4a7bd0" fill-opacity="0.8"/>
<text x="324.0" y="184" font-size="12">17.00 (41%)</text>
<text x="142" y="226" font-size="12" text-anchor="end">배치 16, work 100</text>
<rect x="150" y="206" width="25.0" height="14" fill="#e08a3c" fill-opacity="0.8"/>
<text x="179.0" y="217" font-size="12">2.50 (13%)</text>
<rect x="150" y="223" width="10.8" height="14" fill="#4a7bd0" fill-opacity="0.8"/>
<text x="164.8" y="234" font-size="12">1.08 (7%)</text>
<line x1="150" y1="52" x2="150" y2="256" stroke="currentColor"/>
<line x1="150.0" y1="256" x2="150.0" y2="260" stroke="currentColor"/>
<text x="150.0" y="274" font-size="12" text-anchor="middle">0</text>
<line x1="250.0" y1="256" x2="250.0" y2="260" stroke="currentColor"/>
<text x="250.0" y="274" font-size="12" text-anchor="middle">10</text>
<line x1="350.0" y1="256" x2="350.0" y2="260" stroke="currentColor"/>
<text x="350.0" y="274" font-size="12" text-anchor="middle">20</text>
<line x1="450.0" y1="256" x2="450.0" y2="260" stroke="currentColor"/>
<text x="450.0" y="274" font-size="12" text-anchor="middle">30</text>
<line x1="550.0" y1="256" x2="550.0" y2="260" stroke="currentColor"/>
<text x="550.0" y="274" font-size="12" text-anchor="middle">40</text>
<text x="350.0" y="292" font-size="12" text-anchor="middle">us / item</text>
</svg>
```

그림 7 — 격리 비용을 "격리 없는 빌드 대비 항목당 추가 시간"으로 본 것(예제 5 · 9의 차이를 계산). 배치 16이 추가 시간을 약 15분의 1로 줄이고, 계산이 많을수록 비율이 작아진다.

읽는 법:

1. **비특권 쪽 추가 비용의 정체는 시스템 콜 2번이다.** 배치 1에서 user − supervisor = 77323 − 53785 = 23.5 us(Zephyr), restricted − 특권 = 46018 − 33326 = 12.7 us(FreeRTOS). 항목 하나가 `put` 한 번 + `get` 한 번이므로 시스템 콜 하나에 약 11.8 us / 6.3 us다. C8에서 API 단독으로 잰 차이(Zephyr `k_msgq_put` +9.8 us, FreeRTOS `xQueueSend` +6.3 us)와 맞는다.
2. **격리 빌드는 특권 쪽도 느리다.** Zephyr `USERSPACE=y`의 supervisor 스레드가 `=n`보다 34% 느리다(53.8 vs 40.0 us). 이유: 컨텍스트 스위치마다 `swap_helper.S`가 `z_arm_configure_dynamic_mpu_regions()`를 불러 MPU 리전을 다시 쓰고(`CONFIG_USERSPACE`면 항상), 모든 API 인라인이 `z_syscall_trap()` 검사를 한다(C8 §4.3). FreeRTOS-MPU 특권 쪽은 기본 포트보다 15% 느리다(33.3 vs 29.0 us) — 특권 태스크도 `MPU_*Impl`의 인덱스 변환을 거치고(C8 §5.2), 스위치마다 태스크 리전을 MPU에 올린다.
3. **배치가 가장 큰 무기다.** 배치 16으로 추가 비용이 Zephyr 37.3 → 2.5 us, FreeRTOS 17.0 → 1.1 us로 줄었다. 시스템 콜과 컨텍스트 스위치 수가 1/16이 되니까. 격리 경계를 넘는 데이터는 **묶어서** 넘긴다.
4. **진짜 일이 크면 비율이 작아진다.** 배치 16 + work 100이면 추가 비용이 Zephyr 13%, FreeRTOS 7%다. 비특권으로 보낼 코드는 "경계를 드물게 넘고, 안에서 오래 계산하는" 코드가 좋다(파서, 퓨전, 압축, 프로토콜 스택).

### 7.4 예제 6 — RAM 비용: v7-M의 2의 거듭제곱 vs v8-M의 32바이트 단위

무엇을 확인하나: 같은 크기 요청(파티션 40/300/1100/5000 B, 스택 1000/3000 B)이 v7-M(M3, `mps2/an385`)과 v8-M(M33, `mps2/an521/cpu0`)에서 실제로 얼마를 차지하는지. user 스레드가 각 버퍼의 **마지막 바이트**를 건드려 파티션이 정말 다 덮는지도 확인한다.

```c
/* F5 예제 6: 같은 크기의 파티션 · 스택이 v7-M(2의 거듭제곱)과 v8-M(32바이트 단위)에서 얼마를 차지하나 */
#include <zephyr/kernel.h>
#include <zephyr/app_memory/app_memdomain.h>
#include "f5z.h"
K_APPMEM_PARTITION_DEFINE(p40);   K_APP_BMEM(p40)   uint8_t b40[40];
K_APPMEM_PARTITION_DEFINE(p300);  K_APP_BMEM(p300)  uint8_t b300[300];
K_APPMEM_PARTITION_DEFINE(p1100); K_APP_BMEM(p1100) uint8_t b1100[1100];
K_APPMEM_PARTITION_DEFINE(p5000); K_APP_BMEM(p5000) uint8_t b5000[5000];
K_THREAD_STACK_DEFINE(st1000, 1000);
K_THREAD_STACK_DEFINE(st3000, 3000);
static struct k_thread th;
static struct k_mem_domain dom;

static void user_fn(void *p1, void *p2, void *p3)
{
    b40[39] = 1; b300[299] = 1; b1100[1099] = 1; b5000[4999] = 1;   /* 각 버퍼의 마지막 바이트 */
    printk("[user] touched last byte of all 4 partitions OK\n");
}
int main(void)
{
    struct k_mem_partition *ps[] = { &p40, &p300, &p1100, &p5000 };
    const unsigned want[] = { 40, 300, 1100, 5000 };
    for (int i = 0; i < 4; i++)
        printk("PART want=%4u start=%#lx size=%5u\n", want[i], ps[i]->start, ps[i]->size);
    printk("STACK want=1000 sizeof(object)=%u  usable=%u\n", (unsigned)sizeof(st1000), (unsigned)K_THREAD_STACK_SIZEOF(st1000));
    printk("STACK want=3000 sizeof(object)=%u  usable=%u\n", (unsigned)sizeof(st3000), (unsigned)K_THREAD_STACK_SIZEOF(st3000));
    k_mem_domain_init(&dom, 4, ps);
    k_tid_t t = k_thread_create(&th, st3000, K_THREAD_STACK_SIZEOF(st3000), user_fn, NULL, NULL, NULL, 5, K_USER, K_FOREVER);
    k_mem_domain_add_thread(&dom, t);
    k_thread_start(t);
    k_thread_join(t, K_FOREVER);
    f5_exit();
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'echo "## an385 (v7-M)"; /Users/donh/.rtos_scratch/F5/zrun.sh zalign al385 mps2/an385 | grep -v Booting;
  echo "## an521 (v8-M)"; /Users/donh/.rtos_scratch/F5/zrun.sh zalign al521 mps2/an521/cpu0 | grep -v Booting'
```

```text
## an385 (v7-M)
           FLASH:       57036 B         4 MB      1.36%
             RAM:         35 KB         4 MB      0.85%
PART want=  40 start=0x20003240 size=   64
PART want= 300 start=0x20003000 size=  512
PART want=1100 start=0x20002800 size= 2048
PART want=5000 start=0x20000000 size= 8192
STACK want=1000 sizeof(object)=1024  usable=1024
STACK want=3000 sizeof(object)=4096  usable=4096
[user] touched last byte of all 4 partitions OK
## an521 (v8-M)
           FLASH:       53384 B         4 MB      1.27%
             RAM:       22496 B         4 MB      0.54%
PART want=  40 start=0x38001980 size=   64
PART want= 300 start=0x38001800 size=  320
PART want=1100 start=0x380013a0 size= 1120
PART want=5000 start=0x38000000 size= 5024
STACK want=1000 sizeof(object)=1024  usable=1024
STACK want=3000 sizeof(object)=3008  usable=3008
[user] touched last byte of all 4 partitions OK
```

섹션 크기도 본다.

```sh
limactl shell elinux -- bash -lc 'for b in al385 al521; do E=/var/tmp/rtos/F5/z_$b/zephyr/zephyr.elf; echo "== $b";
  arm-none-eabi-nm -S -n $E | grep -E " (st1000|st3000|priv_stacks)$"; arm-none-eabi-size -A $E | grep -E "app_smem|^noinit|priv_stacks"; done'
```

```text
== al385
20006000 00001000 B st3000
20007000 00000400 B st1000
20008000 00000c00 b priv_stacks
app_smem                 12928   536870912
noinit                   16384   536887296
priv_stacks_noinit        3072   536903680
== al521
38003420 00000bc0 B st3000
38003fe0 00000400 B st1000
38004be0 00000c00 b priv_stacks
app_smem                  6592   939524096
noinit                   10432   939533088
priv_stacks_noinit        3072   939543520
```

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
<text x="340" y="20" font-size="13" text-anchor="middle" font-weight="bold">요청 크기 vs 실제 차지한 크기 (바이트, 예제 6 실행 출력)</text>
<rect x="110" y="32" width="12" height="10" fill="#888" fill-opacity="0.8"/><text x="126" y="41" font-size="12">요청</text>
<rect x="300" y="32" width="12" height="10" fill="#d0564a" fill-opacity="0.8"/><text x="316" y="41" font-size="12">v7-M an385 (2의 거듭제곱)</text>
<rect x="490" y="32" width="12" height="10" fill="#3f9a6b" fill-opacity="0.8"/><text x="506" y="41" font-size="12">v8-M an521 (32 B 단위)</text>
<text x="112" y="75" font-size="12" text-anchor="end">파티션 40 B</text>
<rect x="120" y="56" width="2.1" height="11" fill="#888" fill-opacity="0.8"/>
<text x="126.1" y="66" font-size="12">40</text>
<rect x="120" y="68" width="3.4" height="11" fill="#d0564a" fill-opacity="0.8"/>
<text x="127.4" y="78" font-size="12">64</text>
<rect x="120" y="80" width="3.4" height="11" fill="#3f9a6b" fill-opacity="0.8"/>
<text x="127.4" y="90" font-size="12">64</text>
<text x="112" y="117" font-size="12" text-anchor="end">파티션 300 B</text>
<rect x="120" y="98" width="16.1" height="11" fill="#888" fill-opacity="0.8"/>
<text x="140.1" y="108" font-size="12">300</text>
<rect x="120" y="110" width="27.5" height="11" fill="#d0564a" fill-opacity="0.8"/>
<text x="151.5" y="120" font-size="12">512</text>
<rect x="120" y="122" width="17.2" height="11" fill="#3f9a6b" fill-opacity="0.8"/>
<text x="141.2" y="132" font-size="12">320</text>
<text x="112" y="159" font-size="12" text-anchor="end">파티션 1100 B</text>
<rect x="120" y="140" width="59.1" height="11" fill="#888" fill-opacity="0.8"/>
<text x="183.1" y="150" font-size="12">1100</text>
<rect x="120" y="152" width="110.0" height="11" fill="#d0564a" fill-opacity="0.8"/>
<text x="234.0" y="162" font-size="12">2048</text>
<rect x="120" y="164" width="60.2" height="11" fill="#3f9a6b" fill-opacity="0.8"/>
<text x="184.2" y="174" font-size="12">1120</text>
<text x="112" y="201" font-size="12" text-anchor="end">파티션 5000 B</text>
<rect x="120" y="182" width="268.6" height="11" fill="#888" fill-opacity="0.8"/>
<text x="392.6" y="192" font-size="12">5000</text>
<rect x="120" y="194" width="440.0" height="11" fill="#d0564a" fill-opacity="0.8"/>
<text x="564.0" y="204" font-size="12">8192</text>
<rect x="120" y="206" width="269.8" height="11" fill="#3f9a6b" fill-opacity="0.8"/>
<text x="393.8" y="216" font-size="12">5024</text>
<text x="112" y="243" font-size="12" text-anchor="end">스택 1000 B</text>
<rect x="120" y="224" width="53.7" height="11" fill="#888" fill-opacity="0.8"/>
<text x="177.7" y="234" font-size="12">1000</text>
<rect x="120" y="236" width="55.0" height="11" fill="#d0564a" fill-opacity="0.8"/>
<text x="179.0" y="246" font-size="12">1024</text>
<rect x="120" y="248" width="55.0" height="11" fill="#3f9a6b" fill-opacity="0.8"/>
<text x="179.0" y="258" font-size="12">1024</text>
<text x="112" y="285" font-size="12" text-anchor="end">스택 3000 B</text>
<rect x="120" y="266" width="161.1" height="11" fill="#888" fill-opacity="0.8"/>
<text x="285.1" y="276" font-size="12">3000</text>
<rect x="120" y="278" width="220.0" height="11" fill="#d0564a" fill-opacity="0.8"/>
<text x="344.0" y="288" font-size="12">4096</text>
<rect x="120" y="290" width="161.6" height="11" fill="#3f9a6b" fill-opacity="0.8"/>
<text x="285.6" y="300" font-size="12">3008</text>
<line x1="120" y1="52" x2="120" y2="308" stroke="currentColor"/>
<line x1="120.0" y1="308" x2="120.0" y2="312" stroke="currentColor"/>
<text x="120.0" y="326" font-size="12" text-anchor="middle">0</text>
<line x1="230.0" y1="308" x2="230.0" y2="312" stroke="currentColor"/>
<text x="230.0" y="326" font-size="12" text-anchor="middle">2048</text>
<line x1="340.0" y1="308" x2="340.0" y2="312" stroke="currentColor"/>
<text x="340.0" y="326" font-size="12" text-anchor="middle">4096</text>
<line x1="450.0" y1="308" x2="450.0" y2="312" stroke="currentColor"/>
<text x="450.0" y="326" font-size="12" text-anchor="middle">6144</text>
<line x1="560.0" y1="308" x2="560.0" y2="312" stroke="currentColor"/>
<text x="560.0" y="326" font-size="12" text-anchor="middle">8192</text>
<text x="340" y="346" font-size="12" text-anchor="middle">합계: 요청 10440 B → v7-M 15936 B (+53%) · v8-M 10560 B (+1%)</text>
</svg>
```

그림 8 — 예제 6의 실행 출력. 회색 = 요청, 빨강 = v7-M(an385), 초록 = v8-M(an521). v7-M은 300 B가 512 B, 5000 B가 8192 B가 된다.

출력에서 볼 것:

- **v7-M**: 파티션 4개 6440 B 요청 → 10816 B, 스택 4000 B → 5120 B. 합계 10440 → 15936 B(+53%). 거기에 **배치 구멍**이 더해진다 — `app_smem` 섹션은 12928 B(파티션 합보다 2 KB 더), 스택이 든 `noinit`은 16384 B다. 큰 정렬(8 KB, 4 KB)을 맞추느라 사이사이가 빈다.
- **v8-M**: 32바이트 단위 반올림이라 40 → 64(32의 배수로 올림), 300 → 320, 5000 → 5024. 합계 10560 B(+1%). `app_smem` 6592 B(v7-M의 51%), `noinit` 10432 B. 전체 RAM 사용량은 35 KB → 22.5 KB. v8-M MPU는 base/limit 레지스터로 리전을 정해서 2의 거듭제곱 제약이 없다(F4).
- **특권 스택**: user 모드가 가능한 스레드 스택마다 `CONFIG_PRIVILEGED_STACK_SIZE`(기본 1024 B) 특권 스택이 따로 생긴다(`priv_stacks` 0xc00 = 3 KB = 스택 객체 3개분: `st1000`, `st3000`, 그리고 `kernel/init.c` 55행의 `K_THREAD_PINNED_STACK_DEFINE(z_main_stack, ...)` — main 스레드도 user로 내려갈 수 있어서다). 시스템 콜이 이 스택에서 돈다(C8 §4.4). 스레드 10개면 10 KB다.
- FreeRTOS-MPU 쪽 RAM 비용은 C8 예제 10이 쟀다: TCB 96 → 788 B(대부분 512 B syscall 스택), 커널 데이터 리전 32 KB 고정, 커널 코드 리전 64 KB(실사용 33.5 KB). 스택도 2의 거듭제곱이어야 한다는 것은 예제 8에서 "안 지키면 구멍"으로 확인했다.

v7-M에서 줄이는 법: (1) 파티션 크기를 2의 거듭제곱 **바로 아래**로 설계한다(1100 B가 아니라 1000 B면 1024 B), (2) 같은 도메인이 쓰는 작은 변수들을 한 파티션에 모은다, (3) v7-M MPU의 **subregion**(리전을 8등분해 일부를 끄는 기능)으로 2^n의 7/8 · 6/8 크기를 만든다 — Zephyr Cortex-M 포트는 파티션에 subregion을 쓰지 않는다(이 빌드에서 확인한 크기가 전부 정확히 2의 거듭제곱이다). subregion 레지스터 자체는 F4.

---

## 8. 설계 — 무엇을 user 모드로 보내고, 죽으면 어떻게 하나

### 8.1 경계를 긋는 원칙

1. **신뢰 경계를 따라 긋는다.** 외부 바이트를 처음 만지는 코드(무선 링크 파서, CAN 진단, USB, 네트워크 스택), 서드파티 · 바이너리 라이브러리, 현장 업데이트되는 모듈이 user 쪽이다. 우리가 쓰고 검증한 제어 코드는 특권 쪽이다.
2. **고주파 경로에는 경계를 두지 않는다.** IMU ISR → rate loop → 모터 출력처럼 kHz로 도는 경로는 특권(또는 ISR)에 둔다. §7에서 경계 한 번 넘는 비용이 수 us였다. 8 kHz(125 us 주기)에서 그건 예산의 수 %씩이다.
3. **경계는 좁고 드물게.** user와 특권은 큐 하나 · 시스템 콜 몇 개로만 만나고, 데이터는 배치로 넘긴다(배치 16 → 추가 비용 1/15).
4. **공유 메모리는 단방향 · 읽기 전용부터.** 상태를 공유해야 하면 쓰는 쪽 하나, 읽는 쪽은 RO 파티션/리전(예제 1 · 8). 양방향 공유는 동기화와 TOCTOU 문제를 다시 불러온다.
5. **권한은 최소로, 상속은 신중히.** `K_INHERIT_PERMS`는 편하지만 부모 권한 전부를 넘긴다. 객체마다 grant가 감사(audit)하기 쉽다.
6. **설정을 검증한다.** v7-M이면 모든 리전 · 스택 크기가 2의 거듭제곱이고 크기 정렬인지, map 파일에서 리전 꼬리에 무엇이 붙었는지 CI에서 검사한다(예제 8의 구멍).

### 8.2 드론 비행 제어기의 배치 예

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340" y="20" font-size="13" text-anchor="middle" font-weight="bold">STM32H7급 비행 제어기 — 특권 / user 배치 예</text>
<rect x="10" y="32" width="660" height="132" rx="6" fill="#3f9a6b" fill-opacity="0.07" stroke="#3f9a6b"/>
<text x="20" y="50" font-size="12" font-weight="bold">특권 (ISR · supervisor 스레드 · 특권 태스크)</text>
<rect x="20" y="60" width="150" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.14" stroke="#4a7bd0"/>
<text x="95" y="85" font-size="12" text-anchor="middle">IMU ISR 8 kHz</text>
<rect x="180" y="60" width="150" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.14" stroke="#4a7bd0"/>
<text x="255" y="85" font-size="12" text-anchor="middle">rate · 자세 루프</text>
<rect x="340" y="60" width="150" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.14" stroke="#4a7bd0"/>
<text x="415" y="85" font-size="12" text-anchor="middle">모터 출력 DShot</text>
<rect x="500" y="60" width="160" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.14" stroke="#3f9a6b"/>
<text x="580" y="85" font-size="12" text-anchor="middle">감독 · 워치독 (E8)</text>
<rect x="20" y="110" width="310" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.14" stroke="#3f9a6b"/>
<text x="175" y="135" font-size="12" text-anchor="middle">커널 · 드라이버 · 상태 추정(EKF)</text>
<rect x="340" y="110" width="320" height="40" rx="4" fill="#888" fill-opacity="0.14" stroke="#888"/>
<text x="500" y="135" font-size="12" text-anchor="middle">명령 큐 · 텔레메트리 큐 (경계의 문)</text>
<line x1="10" y1="178" x2="670" y2="178" stroke="#d0564a" stroke-width="1.4" stroke-dasharray="6,4"/>
<text x="664" y="194" font-size="12" text-anchor="end">MPU 경계 — 큐 · 시스템 콜로만 통과</text>
<rect x="10" y="204" width="210" height="96" rx="6" fill="#e08a3c" fill-opacity="0.10" stroke="#e08a3c"/>
<text x="115" y="224" font-size="12" text-anchor="middle" font-weight="bold">도메인 A: 링크 파서</text>
<text x="115" y="244" font-size="12" text-anchor="middle">MAVLink · CRSF 프레임</text>
<text x="115" y="262" font-size="12" text-anchor="middle">RX 링버퍼 파티션 (RO)</text>
<text x="115" y="280" font-size="12" text-anchor="middle">죽으면: 재시작, 링크 유지</text>
<rect x="235" y="204" width="210" height="96" rx="6" fill="#e08a3c" fill-opacity="0.10" stroke="#e08a3c"/>
<text x="340" y="224" font-size="12" text-anchor="middle" font-weight="bold">도메인 B: 로그 · 텔레메트리</text>
<text x="340" y="244" font-size="12" text-anchor="middle">SD 파일시스템 · 압축</text>
<text x="340" y="262" font-size="12" text-anchor="middle">상태 스냅샷 파티션 (RO)</text>
<text x="340" y="280" font-size="12" text-anchor="middle">죽으면: 로그만 끊김</text>
<rect x="460" y="204" width="210" height="96" rx="6" fill="#e08a3c" fill-opacity="0.10" stroke="#e08a3c"/>
<text x="565" y="224" font-size="12" text-anchor="middle" font-weight="bold">도메인 C: 페이로드</text>
<text x="565" y="244" font-size="12" text-anchor="middle">짐벌 · 카메라 · 3rd-party</text>
<text x="565" y="262" font-size="12" text-anchor="middle">자기 파티션만</text>
<text x="565" y="280" font-size="12" text-anchor="middle">죽으면: 기능 끄고 계속 비행</text>
<line x1="115" y1="204" x2="430" y2="152" stroke="currentColor" stroke-width="1.1" marker-end="url(#ah6)"/>
<line x1="500" y1="152" x2="340" y2="202" stroke="currentColor" stroke-width="1.1" marker-end="url(#ah6)"/>
<text x="340" y="326" font-size="12" text-anchor="middle">반복해서 죽는 도메인은 백오프 후 비활성 · 상위로 경보 (재시작 폭주 방지)</text>
<text x="340" y="346" font-size="12" text-anchor="middle">특권 쪽 폴트 = 커널 상태를 믿을 수 없음 → 안전 동작(착륙 · 모터 정지) 후 시스템 리셋</text>
<text x="340" y="366" font-size="12" text-anchor="middle">시간 간섭은 우선순위 · 예산으로 따로 막는다 (C11 §5)</text>
</svg>
```

그림 9 — 드론 FC에서 흔히 생각할 수 있는 배치. 외부 입력을 처음 만지는 파서, 크고 복잡한 로그 · 파일시스템, 서드파티 페이로드 코드가 user 쪽이다. 제어 경로 전체와 상태 추정은 특권 쪽이고, 둘은 큐 두 개로만 만난다.

같은 원칙을 다른 제품에 적용하면:

| 제품 | 특권 쪽 | user 쪽 | 재시작 단위 |
|---|---|---|---|
| 로봇 관절 모터 컨트롤러 | FOC 전류 루프(20 kHz ADC ISR), 위치 · 속도 루프, PWM, 과전류 보호 | CAN 명령 파서, 진단 · 파라미터 서비스, 부트로더 통신 | CAN 서비스 태스크. 모터는 안전 상태(토크 0)로 유지 |
| 센서 허브 MCU | IMU · 타임스탬프 드라이버, SoC와의 SPI 링크 | 벤더 퓨전 라이브러리(바이너리), 보정 알고리즘 | 퓨전 스레드. 원시 데이터 스트림은 끊기지 않음 |
| 차량 ECU(ASIL 혼재) | ASIL-D 기능, 안전 감시 | QM(비안전) 기능, 통신 스택 | QM 파티션. 안전 기능은 영향 없음 |
| SSD 컨트롤러 (Don의 경험) | FTL 메타데이터, NAND 스케줄러 | 벤더별 진단 명령, 로그 압축 | 진단 명령 처리기 |

### 8.3 재시작 전략

| 전략 | 언제 | 구현 (이 노트) |
|---|---|---|
| **태스크 재시작** | 상태가 작고 다시 만들 수 있다 (파서, 로거) | 예제 4(Zephyr `k_thread_join` → 재생성), 예제 7(FreeRTOS MemManage → `vTaskDelete` → 재생성) |
| **기능 비활성 (degraded mode)** | N번 이상 반복해서 죽는다 | 재시작 카운터 + 시간 창. 상위(GCS · 로그)에 알림 |
| **도메인 초기화 후 재시작** | 상태 기계가 있다 | 재시작 전에 그 파티션을 supervisor가 `memset` (예제 7의 `parsed` 교훈) |
| **시스템 리셋** | 특권 쪽 폴트, panic, ISR 폴트, 재시작 실패 | 워치독 · `sys_reboot` · `NVIC_SystemReset`, 리셋 전 `.noinit` 기록 (E8, F8) |

재시작이 되려면 "죽은 태스크가 쥐고 있던 것"을 정리해야 한다. 뮤텍스를 쥔 채 죽으면? Zephyr `k_thread_abort`는 스레드를 대기 큐에서 빼지만, 그 스레드가 **소유한 `k_mutex`를 자동으로 풀어 주지 않는다**고 가정하고 설계하는 게 안전하다(이 노트에서 확인하지 않았다). 그래서 user 쪽과 특권 쪽 사이에는 **뮤텍스를 두지 않고 큐만 둔다** — C11 §5가 ARINC 653 파티션 사이에 뮤텍스를 금지하는 것과 같은 이유다.

### 8.4 ARINC 653, 안전 인증과의 관계

- **ARINC 653**은 파티션을 **공간 분할(메모리)과 시간 분할(시간 창)** 두 축으로 정의한다. C11 §5에서 시간 분할만 FreeRTOS 위에 직접 만들었고, 이 노트의 MPU 도메인이 공간 분할 쪽이다. 둘을 합치면 "IMA식 파티션"의 MCU 버전이 된다. 진짜 ARINC 653 RTOS(VxWorks 653, PikeOS, Deos 등)는 이것을 MMU · 하이퍼바이저 수준에서 인증 가능한 형태로 제공한다(A6, O6).
- **ISO 26262 · IEC 61508의 freedom from interference**: 서로 다른 안전 등급의 소프트웨어를 한 MCU에 올리려면 메모리 간섭(이 노트), 시간 간섭(C11, N), 통신 간섭(큐 · 메시지 무결성)이 없음을 보여야 한다. MPU 격리는 메모리 쪽 근거이고, 인증 RTOS(SAFERTOS, Zephyr의 안전 인증 작업 등)는 그 장치가 맞게 동작한다는 증거까지 제공한다. 자세한 것은 O 모듈.
- 이 노트의 발견을 안전 관점에서 다시 쓰면: **격리 장치의 설정 오류(예제 8의 리전 반올림) 자체가 간섭 경로**다. 안전 분석에서 "MPU를 켰다"는 근거가 아니다. "모든 리전이 의도한 범위만 연다"를 검증해야 근거가 된다.

---

## 9. RTOS 커널 안에서는 무슨 일이 일어나나

이 노트의 실험이 지나간 커널 코드를 소스 위치로 정리한다(진입 경로 asm은 C8 §7, MPU 레지스터는 F4).

| 단계 | Zephyr v4.4.2 | FreeRTOS V11.3.2 (ARM_CM3_MPU, v2) |
|---|---|---|
| 메모리 허락 정의 | `struct k_mem_partition`, `K_APPMEM_PARTITION_DEFINE` (`include/zephyr/app_memory/app_memdomain.h` 127행), 링커 조각 `scripts/build/gen_app_partitions.py` | `MemoryRegion_t xRegions[]` (`include/task.h` 151행) |
| 스레드에 붙이기 | `k_mem_domain_add_thread()` (`kernel/mem_domain.c`), 한도 `arch_mem_domain_max_partitions_get()` (`arch/arm/core/mpu/arm_core_mpu.c` 352행) | `vPortStoreTaskMPUSettings()` (`port.c` 1256행) — 리전 레지스터 값 + 정확한 범위를 TCB에 저장 |
| 2의 거듭제곱 반올림 | `SMEM_PARTITION_ALIGN` (생성 `app_smem_aligned.ld`), `CONFIG_MPU_REQUIRES_POWER_OF_TWO_ALIGNMENT` | `prvGetMPURegionSizeSetting()` (`port.c` 1191행) |
| 컨텍스트 스위치 때 | `swap_helper.S` → `z_arm_configure_dynamic_mpu_regions()` (`arm_core_mpu.c`) — 도메인 파티션 + 스택 | PendSV에서 TCB의 `xMPU_SETTINGS` 리전 4개를 MPU에 씀 |
| 객체 확인 | `k_object_find()` (`kernel/userspace.c` 560행) → gperf 표 `z_object_gperf_find` | 인덱스 → 포인터 `MPU_GetQueueHandleAtIndex` 등 (`mpu_wrappers_v2.c`) |
| 객체 권한 | 객체마다 스레드 비트맵, `k_object_access_grant`, 상속 `k_thread_perms_inherit()` (`kernel/thread.c` 716행) | 태스크마다 ACL, `xPortIsAuthorizedToAccessKernelObject()` (`port.c` 1543 · 1582행) |
| 버퍼 검사 | `K_SYSCALL_MEMORY_*` → `arch_buffer_validate()` → v7-M `mpu_buffer_validate()` (`arm_mpu_v7_internal.h` 172행): **실제 MPU 리전**을 훑는다 | `xPortIsAuthorizedToAccessBuffer()` (`port.c` 1389행): **TCB의 정확한 범위**로 판정 |
| 시스템 콜 검사 실패 | `K_OOPS` → `arch_syscall_oops()` (`arch/arm/core/fatal.c` 158행) | API가 `pdFAIL`/0 반환 |
| 하드웨어 폴트 | `z_arm_fault()` (`arch/arm/core/cortex_m/fault.c` 1025행) → `z_arm_fatal_error()` | 앱의 `MemManage_Handler` (포트는 제공 안 함) |
| 정책 | `z_fatal_error()` → `k_sys_fatal_error_handler()` (weak, 기본 = 정지) → return이면 `k_thread_abort` (`kernel/fatal.c`) | 앱이 전부 결정 |

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="170" y="20" font-size="13" text-anchor="middle" font-weight="bold">Zephyr</text>
<text x="510" y="20" font-size="13" text-anchor="middle" font-weight="bold">FreeRTOS-MPU</text>
<rect x="10" y="32" width="150" height="40" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="85" y="50" font-size="12" text-anchor="middle">MPU 위반</text>
<text x="85" y="66" font-size="12" text-anchor="middle">z_arm_fault</text>
<rect x="180" y="32" width="150" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="255" y="50" font-size="12" text-anchor="middle">z_vrfy 검사 실패</text>
<text x="255" y="66" font-size="12" text-anchor="middle">arch_syscall_oops</text>
<line x1="85" y1="72" x2="160" y2="98" stroke="currentColor" stroke-width="1.1" marker-end="url(#ah7)"/>
<line x1="255" y1="72" x2="180" y2="98" stroke="currentColor" stroke-width="1.1" marker-end="url(#ah7)"/>
<rect x="60" y="100" width="220" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="170" y="118" font-size="12" text-anchor="middle">z_arm_fatal_error → z_fatal_error</text>
<text x="170" y="134" font-size="12" text-anchor="middle">(irq lock, 로그, coredump)</text>
<line x1="170" y1="140" x2="170" y2="162" stroke="currentColor" stroke-width="1.1" marker-end="url(#ah7)"/>
<rect x="40" y="164" width="260" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="170" y="182" font-size="12" text-anchor="middle">k_sys_fatal_error_handler (앱 재정의)</text>
<text x="170" y="198" font-size="12" text-anchor="middle">기본: arch_system_halt</text>
<line x1="170" y1="204" x2="170" y2="226" stroke="currentColor" stroke-width="1.1" marker-end="url(#ah7)"/>
<text x="178" y="220" font-size="12">return</text>
<rect x="60" y="228" width="220" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="170" y="246" font-size="12" text-anchor="middle">k_thread_abort(현재 스레드)</text>
<text x="170" y="262" font-size="12" text-anchor="middle">join 대기자를 깨움 → 감독</text>
<rect x="370" y="32" width="290" height="40" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="515" y="50" font-size="12" text-anchor="middle">MPU 위반 → MemManage 예외</text>
<text x="515" y="66" font-size="12" text-anchor="middle">포트에 핸들러 없음</text>
<line x1="515" y1="72" x2="515" y2="98" stroke="currentColor" stroke-width="1.1" marker-end="url(#ah7)"/>
<rect x="370" y="100" width="290" height="56" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="515" y="118" font-size="12" text-anchor="middle">앱의 MemManage_Handler (예제 7)</text>
<text x="515" y="134" font-size="12" text-anchor="middle">CFSR · MMFAR 기록, 복귀 PC 교체</text>
<text x="515" y="150" font-size="12" text-anchor="middle">vTaskNotifyGiveFromISR(감독)</text>
<line x1="515" y1="156" x2="515" y2="178" stroke="currentColor" stroke-width="1.1" marker-end="url(#ah7)"/>
<rect x="370" y="180" width="290" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="515" y="198" font-size="12" text-anchor="middle">감독 태스크: vTaskDelete</text>
<text x="515" y="214" font-size="12" text-anchor="middle">xTaskCreateRestricted + vGrantAccessTo*</text>
<rect x="370" y="236" width="290" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="515" y="254" font-size="12" text-anchor="middle">ACL · 인덱스 · 버퍼 검사 실패</text>
<text x="515" y="270" font-size="12" text-anchor="middle">→ 예외 없이 pdFAIL / 0 반환</text>
<text x="340" y="304" font-size="12" text-anchor="middle">Zephyr는 "검사 실패 = 그 스레드의 버그"로 보고 죽인다 · FreeRTOS는 반환값으로 알린다</text>
<text x="340" y="322" font-size="12" text-anchor="middle">정책(멈출지, 그 스레드만 정리할지)은 두 RTOS 모두 결국 앱이 정한다</text>
</svg>
```

그림 10 — 위반이 났을 때 두 커널 안에서 지나는 길. Zephyr는 하드웨어 폴트와 소프트웨어 검사 실패가 같은 fatal 경로로 모이고, 앱은 weak 핸들러 하나로 정책을 바꾼다. FreeRTOS-MPU는 폴트 처리 자체가 앱 코드다.

두 커널의 철학 차이 하나를 짚는다. **검증 기준**이 다르다 — Zephyr는 "지금 MPU가 이 스레드에게 허락하는가"(하드웨어와 같은 답), FreeRTOS v2는 "태스크를 만들 때 준 정확한 범위 안인가"(소프트웨어 장부). 예제 8에서 이 차이가 실제로 드러났다: 하드웨어는 반올림된 꼬리를 열었지만 v2의 syscall 검사는 닫았다. Zephyr에서는 반대 상황이 생길 수 있다 — 파티션이 반올림되어 열린 꼬리 주소를 syscall 버퍼로 넘기면 `arch_buffer_validate`도 허락한다. 다만 Zephyr는 파티션 크기 자체를 2의 거듭제곱으로 **링커가 맞춰 주므로**(§2.4), 그 꼬리에 다른 파티션의 변수가 들어오지는 않는다. FreeRTOS는 정렬 · 크기를 개발자 손에 맡기기 때문에 예제 8 같은 구멍이 생긴다.

---

## 10. 실무에서 다시 보기

### 10.1 드론 비행 제어기 — "파서가 죽어도 기체는 난다"를 시험으로 만든다

격리를 했다고 끝나지 않는다. 예제 4 · 7을 그대로 **HIL · CI 시험**으로 만든다: 퍼저(fuzzer)가 링크 파서에 손상 패킷을 넣고, 시험은 (1) 파서 재시작 횟수, (2) 비행 제어 주기 min/max(예제의 `fc_dmin`/`fc_dmax`), (3) 자세 추정 상태의 CRC가 바뀌지 않았는지를 본다. 면접에서 "격리했다"보다 "격리를 이렇게 시험했다"가 훨씬 강하다.

### 10.2 로봇 관절 컨트롤러 — 경계의 위치가 곧 성능

20 kHz FOC 루프는 ADC ISR 안에서 끝나므로 격리 비용이 0이다(ISR은 Handler 모드 = 특권). CAN 명령 파서를 user 쪽에 두면, 파서가 만든 목표값은 큐로 특권 쪽에 넘어간다 — §7의 교훈대로 **명령 하나마다가 아니라 CAN 프레임 묶음 단위**로 넘긴다. 목표값에는 **범위 검사**를 특권 쪽에서 다시 한다. 격리는 메모리를 지키지 "토크 1000 Nm" 같은 틀린 값까지 막지는 않는다.

### 10.3 센서 허브 — 바이너리 라이브러리에 파티션 하나

벤더가 `.a`로 준 퓨전 라이브러리의 전역 변수는 소스를 못 고친다. Zephyr에서는 링커 단계에서 그 라이브러리의 `.data`/`.bss`를 파티션에 넣을 수 있다 — 생성된 `app_smem_aligned.ld`에 `"*libc.a:*"(.data .data.* ...)`가 들어간 것처럼(§2.4), CMake 함수 `zephyr_library_app_memory(<partition>)`(`cmake/modules/extensions.cmake` 626행)가 Zephyr 라이브러리 단위로 그 라이브러리의 전역을 파티션에 넣어 준다(이 노트에서 직접 돌려 보지는 않았다). FreeRTOS에서는 링커 스크립트에서 그 `.a`의 섹션을 별도 정렬 블록에 모으고 그 블록을 `xRegions`로 준다.

### 10.4 Don의 SSD 펌웨어와 연결

SSD 컨트롤러의 호스트 명령 처리에서 "명령의 PRP 주소가 이 네임스페이스의 버퍼 범위 안인가"를 FTL이 검사한 것이 정확히 `K_SYSCALL_MEMORY_WRITE`다. 그리고 "검사한 길이를 다시 읽지 말고 지역 변수에 복사해서 쓴다"는 TOCTOU 규칙은, DMA가 쓰는 중인 명령 버퍼를 펌웨어가 두 번 읽어 다른 값을 본 버그와 같은 뿌리다. 다른 점은 RTOS에서는 공격자가 **같은 칩 안의 다른 스레드**일 수 있다는 것이다.

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| Zephyr 기본 fatal 핸들러 그대로 | user 스레드 하나의 oops로 `Halting system`, 기체 정지 | `k_sys_fatal_error_handler` 기본 = 정지 | 재정의해서 user 스레드 폴트는 return (예제 1 · 4), 특권 폴트는 리셋 |
| v7-M에서 2의 거듭제곱이 아닌 스택 · 리전 (FreeRTOS) | 폴트가 **안 난다** — 다른 태스크의 변수 읽기 · RO 영역 쓰기가 성공 | 리전이 다음 2의 거듭제곱으로 반올림, 꼬리에 남의 변수 | 크기 = 2의 거듭제곱, 정렬 = 크기, map 파일 검사 (예제 8) |
| 같은 리전 안에서 버퍼 뒤에 중요한 변수 | 재시작 후 카운터 · 상태가 0이나 쓰레기 | MPU는 리전 경계만 지킨다 | 버퍼와 상태를 다른 리전에, 재시작 때 리전 초기화 (예제 7) |
| restricted 태스크에서 `xQueueCreate` | MemManage IACCVIOL (NULL 반환 아님) | v2 생성 API는 특권 전용 섹션 | 객체는 특권 초기화 코드에서 만들고 ACL로 준다 |
| ACL 회수 · 미부여를 모름 | 예외 없이 `pdFAIL`, 데이터가 사라짐 | v2 ACL 실패는 조용함 | restricted 태스크의 모든 반환값 검사, 초기화 때 grant 목록 감사 |
| MemManage 핸들러에서 PC를 안 바꾸고 복귀 | 같은 명령에서 폴트 무한 반복, 시스템 정지처럼 보임 | 복귀하면 폴트 명령을 다시 실행 | 복귀 PC를 park 함수로, 감독이 태스크 삭제 (예제 7) |
| 재시작 시 권한 · 도메인 재부여 누락 | 재시작된 스레드가 첫 API에서 oops | 권한 · 도메인은 스레드 인스턴스에 붙음 | 생성 함수 하나(`start_parser`)에 생성 · 도메인 · grant를 묶음 |
| 도메인에 파티션 6개 이상 | `-EINVAL`/`-ENOSPC`, 무시하면 이상한 폴트 | 하드웨어 리전 한도(이 보드 5) | 반환값 검사, 파티션을 기능 단위로 합침 |
| `K_INHERIT_PERMS` 남용 | 일꾼 스레드가 필요 이상 권한을 가짐 | 부모 권한 전부 복사 | 필요한 객체만 supervisor가 grant |
| 앱 syscall에서 user 버퍼를 검사 후 다시 읽음 | 가끔 범위를 넘는 복사 (공격 가능) | TOCTOU | `k_usermode_from_copy`로 복사본 사용 |
| 고주파 경로를 user로 | 주기 초과 · 지터 | 시스템 콜 2번 + 컨텍스트 스위치마다 MPU 재설정 | 고주파는 특권, 경계는 배치로 (§7) |

---

## 12. 면접에서 이렇게 말한다

**Q.** "How would you keep a buggy telemetry parser from taking down the flight controller?"

**A.** 파서를 비특권(user) 태스크로 만들고 자기 스택과 RX 버퍼 파티션만 주고, 제어 루프와는 큐 하나로만 연결한다. 폴트 정책은 "그 태스크만 정리 후 재시작"으로 하고, 반복되면 기능을 끈다. 제어 루프 · 상태 추정 · 모터 출력은 특권에 둔다. QEMU에서 손상 패킷을 넣어 파서가 두 번 죽고 재시작되는 동안 2 ms 제어 주기의 min/max가 폴트 없을 때와 똑같은 것을 확인했다.

> I'd run the parser as an unprivileged task — a Zephyr user thread or a FreeRTOS-MPU restricted task — that can touch only its own stack and its RX buffer partition, and connect it to the control side through a single message queue. The control loop, state estimation and motor output stay privileged. Then I'd make the fault policy explicit: on an MPU fault in that task, kill just that task and have a supervisor recreate it, with a restart counter that disables the feature if it keeps dying. I built exactly this on QEMU: corrupted length fields made the parser fault twice, it was recreated in about 60 to 120 microseconds, and the 2 ms control loop's min and max period were identical to the fault-free run.

**꼬리 질문**: "What does the MPU NOT protect you from?" → 리전 **안쪽**의 손상(예제 7에서 카운터가 덮어써졌다), 틀린 값(범위 검사는 따로), 시간 간섭(우선순위 · 예산).

**Q.** "Explain Zephyr memory domains and partitions."

**A.** 파티션은 시작 · 크기 · 속성을 가진 연속 메모리 하나, 도메인은 파티션 묶음이다. 스레드는 한 도메인에 속하고, user 스레드는 자기 스택과 도메인의 파티션만 접근한다. 전역 변수는 `K_APP_BMEM`/`K_APP_DMEM`으로 파티션에 넣고 링커가 정렬해 배치한다. 같은 파티션을 두 도메인에 넣으면 공유 메모리다. 컨텍스트 스위치 때 커널이 다음 스레드의 도메인을 MPU에 올리고, 도메인당 파티션 수는 MPU 리전 수가 정한다 — M3 QEMU 보드에서 5개였다.

> A partition is one contiguous range with access attributes; a domain is a set of partitions. Every thread belongs to one domain, and a user thread can access only its own stack plus its domain's partitions. You place globals into a partition with `K_APP_BMEM` or `K_APP_DMEM`, and the build does a two-pass link to generate the aligned layout. Putting the same partition in two domains gives you shared memory. On every context switch the kernel reprograms the MPU with the incoming thread's partitions, so the number of partitions per domain is bounded by free MPU regions — five on the Cortex-M3 board I used.

**Q.** "How do you add a system call in Zephyr, and what must the verification function do?"

**A.** 헤더에 `__syscall`로 선언하고, `z_impl_`(진짜 일)과 `z_vrfy_`(검사)를 쓰고, 생성된 `_mrsh.c`를 include한다. 검사 함수는 모든 포인터를 `K_SYSCALL_MEMORY_READ/WRITE`로, 커널 객체는 `K_SYSCALL_OBJ`로 확인하고, 실패하면 `K_OOPS`로 그 스레드를 끝낸다. 틀린 **값**은 에러 코드로 돌려준다. 검사에 쓴 user 데이터는 복사본으로 써서 TOCTOU를 막는다.

> Declare it with `__syscall` in a header, write `z_impl_` for the real work and `z_vrfy_` for validation, and include the generated marshalling file. The verifier must check every pointer with `K_SYSCALL_MEMORY_READ` or `WRITE` and every kernel object with `K_SYSCALL_OBJ`, and on failure `K_OOPS` kills the calling thread. Bad values, like an out-of-range index, should just return an error code. And anything the check depends on — a length, say — should be copied into kernel memory with `k_usermode_from_copy` first, otherwise another thread can change it between the check and the use. In my test, passing a kernel address as the output pointer produced "write access denied" and an oops, while the supervisor could call the same function directly.

**Q.** "In FreeRTOS-MPU, how do you recover from a MemManage fault in one task?"

**A.** 포트가 핸들러를 주지 않으니 직접 쓴다. 핸들러는 CFSR · MMFAR · 쌓인 PC를 기록하고 MMFSR을 지우고, 쌓인 프레임의 PC를 park 함수로 바꿔 무한 폴트를 막고, `vTaskNotifyGiveFromISR`로 특권 감독 태스크를 깨운다 — v2에서 FromISR 계열은 SVC가 아닌 특권 함수라 핸들러에서 불러도 된다. 감독은 `vTaskDelete` 후 같은 스택 버퍼로 `xTaskCreateRestricted`, ACL을 다시 준다.

> The port doesn't provide a handler, so you write one. It records CFSR, MMFAR and the stacked PC, clears the MMFSR bits, rewrites the stacked PC to a parking function so the exception return doesn't re-execute the faulting instruction, and notifies a privileged supervisor task with `vTaskNotifyGiveFromISR` — in the v2 wrappers the FromISR APIs are plain privileged functions, not SVC calls, so that's legal from handler mode. The supervisor deletes the task, recreates it with `xTaskCreateRestricted` on the same stack buffer and re-grants its queue access. On QEMU that took about 57 microseconds and the control task's period didn't move.

**꼬리 질문**: "What about a fault in a privileged task?" → 커널 상태를 믿을 수 없으니 안전 동작 후 리셋.

**Q.** "What does isolation cost, and how do you reduce it?"

**A.** 내 측정(QEMU)에서 항목마다 메시지를 주고받는 producer/consumer가 Zephyr user 쪽에서 격리 없는 빌드보다 93%, FreeRTOS restricted에서 59% 느렸다. 원인은 항목마다 시스템 콜 2번과 컨텍스트 스위치마다 MPU 재설정이다. 16개씩 묶어 보내자 추가 비용이 15분의 1로 줄었고, 계산이 많은 작업에서는 7–13%였다. 그리고 v7-M은 RAM을 2의 거듭제곱으로 먹어서 같은 요청이 53% 더 컸다(v8-M은 1%).

> On QEMU, a producer-consumer that passes one item per message was 93 percent slower in Zephyr user mode and 59 percent slower with FreeRTOS restricted tasks than with no isolation at all. That's two system calls per item plus MPU reprogramming on every context switch — even the privileged side of an isolated build was 15 to 34 percent slower. Batching sixteen items per message cut the extra cost per item by about fifteen times, and with real computation per item the overhead fell to 7 to 13 percent. Memory is the other cost: on ARMv7-M the same partitions and stacks took 53 percent more RAM because of power-of-two regions, versus about 1 percent on ARMv8-M.

**Q.** "What's a subtle pitfall with ARMv7-M MPU regions?"

**A.** 리전이 2의 거듭제곱으로 반올림된다는 것. FreeRTOS에서 600워드(2400 B) 스택을 주면 리전이 4 KB가 되고, 링커가 그 꼬리에 다른 태스크의 변수를 넣으면 그 태스크가 폴트 없이 읽고 쓴다. 스택 리전 번호가 커서 다른 리전의 RO 설정까지 이긴다. 실제로 재현했다. 크기를 2의 거듭제곱으로 맞추고 map 파일을 CI에서 검사한다.

> Regions round up to a power of two. When I gave a FreeRTOS-MPU task a 600-word stack, its region became 4 KB, and the linker placed another task's private buffer and a read-only shared buffer in that tail. The task could read the private buffer and write the read-only one with no fault, because the stack region has the highest number and wins on overlap. Interestingly the v2 system-call buffer check still rejected that address, since it uses the exact size stored in the TCB, not the hardware region. The fix is power-of-two sizes and alignment, and a CI check over the map file.

**Q.** "How does this relate to ARINC 653 or ISO 26262?"

**A.** ARINC 653 파티션은 공간 분할과 시간 분할 둘 다다. MPU 도메인은 공간 쪽이고, 시간 쪽은 시간 창 스케줄링(직접 만들어 본 C11)이나 예산 · 우선순위로 한다. ISO 26262의 freedom from interference는 메모리 · 시간 · 통신 간섭이 없음을 보이라는 것인데, MPU 격리는 메모리 쪽 근거이고 그 설정이 맞다는 검증까지 있어야 근거가 된다.

> ARINC 653 partitioning has two halves: spatial and temporal. MPU domains give you the spatial half on an MCU; the temporal half needs fixed time windows or at least budgets and priorities — I've built a two-level window scheduler on FreeRTOS for that. For ISO 26262 freedom from interference you have to argue memory, timing and communication separately. The MPU is the memory argument, but only if you can show every region opens exactly what you intended — my power-of-two finding is exactly the kind of thing that breaks that argument.

---

## 13. 직접 해보기

1. **개념**: user 스레드가 `printk`는 되는데 `snprintf`는 MPU 폴트가 날 수 있다. 왜인가? 어떻게 고치나?
   정답/힌트: `printk`는 시스템 콜(`k_str_out`)이라 커널이 대신 찍는다. `snprintf`는 libc 전역(`z_libc_partition`)을 user 문맥에서 직접 만진다 → 그 파티션을 도메인에 추가(§2.4).
2. **코드**: 예제 1에 도메인 C를 추가해 `part_sh`를 **RO**로 보게 하라. `K_APPMEM_PARTITION_DEFINE`은 RW가 기본이므로 같은 주소 범위를 `K_MEM_PARTITION_DEFINE(..., K_MEM_PARTITION_P_RW_U_RO)`로 다시 정의해야 한다. 시작 · 크기는 `z_data_smem_part_sh_part_start`/`_size` 심볼로 얻는다.
   정답/힌트: 같은 도메인 안에서만 겹침이 금지이므로 다른 도메인에서 RO로 보는 것은 된다. C 스레드가 쓰면 MPU FAULT.
3. **코드**: 예제 3에 `__syscall int f5_log_write(const char *s, size_t maxlen)`을 추가하고 `k_usermode_string_copy()`로 커널 버퍼에 복사한 뒤 출력하라. user가 커널 주소 문자열을 넘기면?
   정답/힌트: 복사 함수가 접근 검사를 하므로 실패를 반환한다. 반환값을 보고 `K_OOPS` 할지 에러 코드를 돌려줄지 정책을 정하라.
4. **측정**: 예제 5를 배치 4, 8, 32로 늘려 "항목당 추가 비용 vs 배치 크기" 곡선을 그려라. 어디서 꺾이나?
   정답/힌트: 추가 비용 ≈ (시스템 콜 2번 + 스위치 비용) / 배치 + 복사 비용 × 1. 배치가 커지면 메시지 복사(`k_msgq` 메모리 복사) 쪽이 남는다.
5. **측정**: 예제 4에서 `CONFIG_FAULT_DUMP=0`과 `CONFIG_LOG=n`(+ `CONFIG_LOG_MODE_MINIMAL` 제거)으로 바꾸고 "fault → restart" 시간을 다시 재라. 그리고 폴트 시각을 비행 제어 깨어남 직전으로 맞춰(패킷 전송 시각 조정) 최악의 주기 지연을 재라.
   정답/힌트: fatal 경로는 irq lock 상태로 로그를 찍는다. 로그가 짧을수록 최악 지연이 줄어든다.
6. **코드**: 예제 8의 `F5_FIX=0` 구멍을 **빌드 단계에서** 잡는 스크립트를 써라: `arm-none-eabi-nm -S`로 restricted 태스크 스택 심볼의 크기가 2의 거듭제곱인지, 주소가 크기에 정렬됐는지 검사.
   정답/힌트: `size & (size - 1) == 0` 그리고 `addr % size == 0`. 리전 꼬리 `[addr+size, roundup)`에 다른 심볼이 있으면 실패.
7. **설계**: 로봇 관절 컨트롤러에서 "파라미터 서비스(CAN으로 게인 변경)"를 user로 보냈다. 파라미터가 특권 쪽 제어 루프로 넘어가는 경로를 설계하라 — 공유 메모리, 큐, 시스템 콜 중 무엇을 쓰고, 어떤 검사를 어디서 하나?
   정답/힌트: 큐(또는 앱 syscall) + 특권 쪽 범위 검사 + 제어 주기 경계에서 원자적 교체(더블 버퍼). user가 쓴 값을 특권이 검사 없이 쓰면 격리의 의미가 없다.

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| mixed-criticality | 혼합 중요도 | 중요도(안전 등급)가 다른 소프트웨어가 한 CPU에 함께 도는 것 |
| fault containment | 결함 격리 | 한 구성 요소의 결함이 다른 구성 요소로 번지지 않게 막는 것 |
| freedom from interference | 간섭 없음 | 안전 표준의 요구 — 낮은 등급 SW가 높은 등급 SW를 메모리 · 시간 · 통신으로 방해하지 않음 |
| user thread | user 스레드 | Zephyr의 비특권 스레드, `K_USER` 또는 `k_thread_user_mode_enter` |
| supervisor thread | supervisor 스레드 | Zephyr의 특권 스레드 (기본) |
| restricted task | 제한 태스크 | FreeRTOS-MPU의 비특권 태스크, `xTaskCreateRestricted` |
| memory partition | 메모리 파티션 | 시작 · 크기 · 속성을 가진 연속 메모리 구역 (`struct k_mem_partition`) |
| memory domain | 메모리 도메인 | 파티션 묶음, 스레드는 한 도메인에 속한다 |
| `K_APP_BMEM` / `K_APP_DMEM` | 파티션 배치 매크로 | 전역 변수를 지정 파티션의 bss/data 섹션에 넣는다 |
| app_smem | 앱 공유 메모리 영역 | 모든 앱 파티션이 링커로 모이는 RAM 영역 (`_app_smem_start`) |
| kernel object permission | 객체 권한 | 객체마다 "어느 스레드가 써도 되나" 비트맵 |
| `K_INHERIT_PERMS` | 권한 상속 | 새 스레드가 부모의 객체 권한을 복사받게 하는 옵션 |
| ACL | 접근 제어 목록 | FreeRTOS v2에서 태스크마다 허락된 커널 객체 비트맵 |
| `z_impl_` / `z_vrfy_` | 구현 / 검증 | Zephyr syscall의 진짜 일 / user 호출 때만 하는 인자 검사 |
| `K_OOPS` | 커널 oops | 검사 실패 시 호출 스레드를 reason 3으로 끝내는 매크로 |
| TOCTOU | 검사-사용 시점 차 | 검사한 값과 실제로 쓰는 값이 그 사이 바뀌는 경쟁 (Time Of Check to Time Of Use) |
| confused deputy | 혼동된 대리인 | 권한 있는 코드가 권한 없는 쪽의 인자를 믿고 대신 나쁜 일을 하는 문제 |
| fatal error handler | 치명 오류 핸들러 | Zephyr `k_sys_fatal_error_handler`, return하면 그 스레드만 abort |
| MemManage | 메모리 관리 폴트 | MPU 위반 예외, CFSR(MMFSR) · MMFAR로 원인 확인 |
| power-of-two region | 2의 거듭제곱 리전 | v7-M MPU 리전의 크기 · 정렬 규칙 (F4) |
| privileged stack | 특권 스택 | user 스레드마다 따로 있는 syscall 실행 스택 (`CONFIG_PRIVILEGED_STACK_SIZE`) |
| supervisor (감독) task | 감독 태스크 | 다른 태스크의 죽음을 감지하고 재시작하는 특권 태스크 |

---

## 15. 요약 & 체크리스트

유저 모드 격리는 MCU 한 칩 안에서 **신뢰 수준이 다른 코드를 칸막이로 나누는 일**이다. Zephyr는 메모리를 파티션 · 도메인으로, 커널 객체를 스레드 권한 비트맵으로 나누고, 모든 시스템 콜 인자를 `z_vrfy_`에서 검사해 실패하면 그 스레드를 oops로 끝낸다. 앱도 `__syscall`로 자기 문을 낼 수 있고, 검사 규칙(포인터는 oops, 값은 에러 코드, TOCTOU는 복사본)이 곧 보안이다. FreeRTOS-MPU v2는 restricted 태스크마다 리전 3개 + 스택, 객체마다 ACL을 주고, 위반은 MemManage나 조용한 `pdFAIL`로 나타난다. 두 RTOS 모두 **폴트 뒤의 정책은 앱 몫**이다 — Zephyr는 fatal 핸들러 재정의, FreeRTOS는 MemManage 핸들러 + 감독 태스크로 "그 태스크만 정리하고 다시 만든다"를 직접 만들었고, 비행 제어 주기는 흔들리지 않았다. 격리의 대가는 이 노트 측정으로 항목당 시스템 콜 2번(배치 1에서 59–93% 느림, 배치 16 + 계산 많은 작업에서 7–13%), 격리 빌드 특권 쪽도 15–34%, v7-M RAM +53%였다. 그리고 격리는 설정이 맞을 때만 격리다: MPU는 리전 **안쪽** 손상을 못 막고, 2의 거듭제곱이 아닌 스택은 남의 변수를 열어 버린다.

- [ ] 혼합 중요도 · 결함 격리 · 보안이라는 세 가지 격리 이유와, 격리가 막지 못하는 것을 말할 수 있다
- [ ] Zephyr partition / domain / `K_APP_BMEM` / `app_smem` 링커 생성 과정을 설명하고 도메인당 파티션 한도가 어디서 오는지 안다
- [ ] 두 도메인이 한 파티션을 공유하는 코드를 쓸 수 있다
- [ ] `k_object_access_grant`, `k_thread_access_grant`, `K_INHERIT_PERMS`, `k_thread_user_mode_enter`의 쓰임을 안다
- [ ] kernel oops(reason 3)와 MPU fault(reason 19)의 로그를 구분하고 원인을 찾을 수 있다
- [ ] 앱 정의 Zephyr syscall을 `__syscall` · `z_impl_` · `z_vrfy_` · `_mrsh.c`로 처음부터 만들 수 있다
- [ ] `K_SYSCALL_MEMORY_READ/WRITE`가 무엇을 기준으로 판정하는지(실제 MPU 리전) 안다
- [ ] Zephyr `k_sys_fatal_error_handler`를 재정의해 user 스레드만 죽이고, 감독이 재시작하게 할 수 있다
- [ ] FreeRTOS-MPU MemManage 핸들러에서 복귀 PC 교체 + FromISR 알림 + 감독 재생성을 구현할 수 있다
- [ ] FreeRTOS v2 ACL grant/revoke와 그 실패 방식(조용한 `pdFAIL`)을 안다
- [ ] restricted 태스크에서 생성 API가 MemManage로 죽는다는 것을 안다
- [ ] 2의 거듭제곱이 아닌 스택이 만든 구멍을 재현 · 설명 · 검출할 수 있다
- [ ] 격리 비용을 앱 수준으로 재고, 배치와 경계 배치로 줄이는 설계를 말할 수 있다

---

## 참고 자료

- Zephyr v4.4.2 소스(랩 `/var/rtoslab/zephyrproject/zephyr`): `include/zephyr/app_memory/app_memdomain.h`(`K_APPMEM_PARTITION_DEFINE`, `K_APP_DMEM`, `K_APP_BMEM`), `include/zephyr/app_memory/mem_domain.h`(`k_mem_domain_*`, `k_mem_domain_default`), `kernel/mem_domain.c`, `include/zephyr/sys/kobject.h`(`k_object_access_grant` 등), `kernel/userspace.c`(`k_object_find`), `kernel/thread.c`(`K_INHERIT_PERMS`, `k_thread_perms_inherit`), `include/zephyr/internal/syscall_handler.h`(`K_OOPS`, `K_SYSCALL_MEMORY_*`, `K_SYSCALL_OBJ`, `k_usermode_from_copy`), `kernel/fatal.c`(`z_fatal_error`, weak `k_sys_fatal_error_handler`), `include/zephyr/arch/arm/arch.h`(ARM fatal reason 코드), `arch/arm/core/mpu/arm_core_mpu.c`(`arch_mem_domain_max_partitions_get`, `z_arm_configure_dynamic_mpu_regions`), `arch/arm/core/mpu/arm_mpu_v7_internal.h`(`mpu_buffer_validate`), `arch/arm/core/cortex_m/swap_helper.S`, `arch/arm/core/cortex_m/fault.c`, `arch/arm/core/fatal.c`(`arch_syscall_oops`), `scripts/build/gen_app_partitions.py`, `scripts/build/gen_syscalls.py`, `Kconfig.zephyr`(`CONFIG_APPLICATION_DEFINED_SYSCALL`), `cmake/modules/extensions.cmake`(`zephyr_syscall_header`), `samples/userspace/prod_consumer`(앱 syscall · TOCTOU 복사 예), `samples/userspace/shared_mem`
- Zephyr 문서 "User Mode": https://docs.zephyrproject.org/latest/kernel/usermode/index.html — 하위 페이지 Memory Domain, Kernel Objects, System Calls, Memory Protection Design
- FreeRTOS-Kernel V11.3.2 소스(랩 `/var/rtoslab/src/FreeRTOS-Kernel`): `include/task.h`(`TaskParameters_t`, `xTaskCreateRestricted`, `vGrantAccessToKernelObject`), `include/mpu_wrappers.h`(v2 래퍼 매핑, `vGrantAccessTo*` 매크로, 특권 전용 래퍼 목록), `portable/Common/mpu_wrappers_v2.c`(`MPU_xQueueGenericCreate` 등 특권 전용 래퍼, `MPU_vTaskGenericNotifyGiveFromISR`), `portable/GCC/ARM_CM3_MPU/port.c`(`prvGetMPURegionSizeSetting`, `vPortStoreTaskMPUSettings`, `xPortIsAuthorizedToAccessBuffer`, `xPortIsAuthorizedToAccessKernelObject`, `vPortGrantAccessToKernelObject`), `portable/GCC/ARM_CM3_MPU/portmacro.h`(`portMPU_REGION_*`, `portSTACK_REGION`)
- FreeRTOS 공식 사이트 https://www.freertos.org/ — FreeRTOS-MPU 문서(정확한 하위 URL은 사이트 개편이 잦아 적지 않는다), GitHub https://github.com/FreeRTOS/FreeRTOS-Kernel
- Arm, "ARMv7-M Architecture Reference Manual" (DDI 0403) — PMSAv7 MPU(리전 크기 · 정렬 · 겹칠 때 번호가 큰 리전 우선), MemManage Fault Status Register, CONTROL; "Armv8-M Architecture Reference Manual" (DDI 0553) — PMSAv8 base/limit 리전
- ARINC Specification 653 (Avionics Application Software Standard Interface) — 공간 · 시간 분할 개념(C11 §5 참고 자료와 같음)
- 이 사이트의 관련 노트: C8(커널 진입 경로와 진입 비용), C11 §5(ARINC 653식 시간 분할), F4(MPU 리전), E8(워치독 · 리셋 루프 · safe mode), EL L6(Linux 런타임 보안 — seccomp · capabilities와의 비교 대상)
