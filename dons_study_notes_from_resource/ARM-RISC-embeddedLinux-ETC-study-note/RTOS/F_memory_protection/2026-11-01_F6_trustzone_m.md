# F6. TrustZone-M — 한 코어 안의 두 세계

> **이 노트를 다 읽으면**: Cortex-M33에서 "지금 Secure인가"가 실행 중인 코드의 **주소**로 정해진다는 것을 SAU · IDAU · MPC/PPC 세 층으로 설명할 수 있다 · Secure 이미지와 Non-secure 이미지를 따로 빌드해(`-mcmse`, `--cmse-implib`, `.gnu.sgstubs`) QEMU mps2-an505에서 SG veneer로 서로 부르고, 잘못된 접근 8가지가 각각 어떤 SFSR · CFSR 비트(또는 조용한 0)로 드러나는지 직접 보일 수 있다 · confused deputy 문제를 `cmse_check_address_range()`로 막고, BXNS/BLXNS 전에 컴파일러가 레지스터를 지우는 이유를 디스어셈블리로 보여 줄 수 있다 · FreeRTOS ARM_CM33(TrustZone) 포트가 태스크마다 Secure 스택을 따로 두는 방식과 그 비용(태스크 전환 2배)을 실측으로 말하고, TF-M(SPE · SPM · 파티션 · 격리 레벨)과 Zephyr `mps2/an521/cpu0/ns` 구성을 그릴 수 있다
> **왜 필요한가**: 2020년대 범용 MCU의 기본값이 Armv8-M(M33 · M55 · M85)이 되면서 "TrustZone-M이 뭔가, Cortex-A TrustZone과 뭐가 다른가, RTOS는 어느 쪽에서 도나, NS 태스크가 Secure 함수를 부르다 선점되면 어떻게 되나"가 드론 · 로봇 · 차량 펌웨어 면접에 나온다. 실무에서는 "veneer를 불렀는데 SecureFault", "주변장치 레지스터가 0만 읽힌다", "NS 인터럽트가 안 들어온다"를 이 층에서 디버깅한다
> **Don 기준 난이도**: 예외 진입 · EXC_RETURN · NVIC 우선순위 · MPU · 부트로더 → 앱 점프는 이미 안다(B2, B3, F4) / 새로 배울 부분은 보안 속성(attribution) 3층 구조, SG · BXNS · BLXNS · TT 명령, CMSE 컴파일러 확장과 import library, 뱅크 레지스터 목록, ITNS · AIRCR.PRIS, 태스크별 Secure context, TF-M 구조
> **선행 노트**: B2(예외 진입 · EXC_RETURN), B3(NVIC 우선순위 · BASEPRI), B7(v8-M 기능 · `_NTZ` 포트), F4(MPU — S/NS 각자 MPU가 있다는 것만 이 노트에서), EL C5(Cortex-A의 TrustZone · TF-A · OP-TEE), EL L5(secure boot 체인). 이어지는 노트: L4(신뢰 루트 · OTP 키), L5(TF-M과 PSA Certified 심화)
> 🔁 **Bare-Metal 겹침**: BM C12(TrustZone-M: Secure / Non-secure, SAU · IDAU, NSC veneer, 보안 상태 전환, bare-metal 두 이미지 구성) — 이 노트가 주인. Bare-Metal 노트는 이 노트를 링크하고 "TF-M 없이 직접 쓰는 최소 Secure 이미지" 관점(칩별 SAU/MPC 초기화 순서, 양산 체크리스트)만 더한다

---

## 0. 큰 그림 — 한 코어를 둘로 나누는 이유

### 0.1 직관

SSD 컨트롤러를 떠올려 보자. Don이 짜던 펌웨어에는 "암호화 키를 만지는 코드"와 "NVMe 명령을 해석하는 코드"가 같은 코어, 같은 주소 공간에서 돌았다. 호스트가 보낸 이상한 명령 하나가 파서의 버그를 건드려 임의 코드 실행으로 이어지면, 그 코드는 키가 있는 RAM도 읽을 수 있다. 막을 방법은 "키 코드와 파서 코드를 하드웨어로 갈라놓기"뿐이다.

TrustZone-M은 이 칸막이를 **코어 하나 안에** 만든다. 코어는 두 가지 보안 상태 중 하나로 돈다.

- **Secure(S)**: 리셋 직후의 상태다. 모든 메모리 · 주변장치를 볼 수 있다. 키, 부트 검증, 암호 서비스처럼 작고 믿을 수 있어야 하는 코드가 여기 산다.
- **Non-secure(NS)**: Secure가 "여기는 써도 된다"고 열어 준 메모리 · 주변장치만 볼 수 있다. RTOS 커널, 앱 태스크, 통신 스택처럼 크고 버그가 있을 수 있는 코드가 여기 산다.

NS 코드가 Secure 기능을 쓰려면 정해진 문(**NSC 영역의 SG 명령**)으로만 들어와야 한다. 문 밖의 Secure 주소로 점프하거나 Secure RAM을 읽으면 하드웨어가 즉시 **SecureFault**를 낸다.

```svg
<svg viewBox="0 0 660 300" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<rect x="20" y="20" width="260" height="210" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="150" y="44" font-size="14" text-anchor="middle">Secure 세계 (작고 믿을 코드)</text>
<rect x="40" y="62" width="220" height="36" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="150" y="85" font-size="13" text-anchor="middle">리셋 · 부트 코드 · SAU 설정</text>
<rect x="40" y="108" width="220" height="36" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="150" y="131" font-size="13" text-anchor="middle">키 · 암호 · 보안 저장소</text>
<rect x="40" y="154" width="220" height="36" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="150" y="177" font-size="13" text-anchor="middle">TF-M (SPM + 파티션)</text>
<text x="150" y="216" font-size="12" text-anchor="middle">Secure RAM · Secure 주변장치</text>
<rect x="380" y="20" width="260" height="210" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="510" y="44" font-size="14" text-anchor="middle">Non-secure 세계 (크고 복잡)</text>
<rect x="400" y="62" width="220" height="36" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="510" y="85" font-size="13" text-anchor="middle">RTOS 커널 (FreeRTOS, Zephyr)</text>
<rect x="400" y="108" width="220" height="36" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="510" y="131" font-size="13" text-anchor="middle">앱 태스크 · 드라이버</text>
<rect x="400" y="154" width="220" height="36" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="510" y="177" font-size="13" text-anchor="middle">통신 스택 (공격 표면)</text>
<text x="510" y="216" font-size="12" text-anchor="middle">NS RAM · NS 주변장치</text>
<rect x="296" y="100" width="68" height="50" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="330" y="122" font-size="13" text-anchor="middle">NSC</text>
<text x="330" y="140" font-size="12" text-anchor="middle">SG 문</text>
<line x1="398" y1="112" x2="368" y2="112" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="292" y1="112" x2="264" y2="112" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="264" y1="140" x2="396" y2="140" stroke="#888" stroke-dasharray="4 3" marker-end="url(#ah1)"/>
<text x="330" y="94" font-size="12" text-anchor="middle">NS → S 호출</text>
<text x="330" y="165" font-size="12" text-anchor="middle">BXNS 복귀</text>
<rect x="20" y="246" width="620" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="330" y="271" font-size="13" text-anchor="middle">같은 Cortex-M33 코어, 같은 버스 — 주소마다 S / NSC / NS가 정해진다</text>
</svg>
```

그림 1 — TrustZone-M의 두 세계. 왼쪽 Secure 쪽은 리셋 직후부터 돌고, 오른쪽 NS 쪽은 Secure가 열어 준 것만 쓴다. 두 세계 사이의 유일한 정문은 NSC(Non-secure callable) 영역의 SG 명령이다. RTOS는 보통 오른쪽에서 돈다.

Don의 SSD 경험으로 바꾸면 이렇다. "부트 ROM + 키 관리자"가 Secure 쪽, "FTL + NVMe 명령 처리 + 호스트 인터페이스"가 NS 쪽이다. 호스트 쪽 버그로 NS가 털려도 키는 Secure RAM에 있어서 못 읽는다. NS는 "이 블록 암호화해 줘" 같은 정해진 함수만 부를 수 있다.

드론 비행 제어기라면 Secure 쪽에 펌웨어 서명 검증, 기기 인증 키(기체 ID), 암호화된 텔레메트리 세션 키가 있고, NS 쪽에서 FreeRTOS나 Zephyr가 자세 제어 · MAVLink · GNSS를 돌린다. 무선 링크로 들어온 패킷 파서가 털려도 기체 인증 키는 안전하다는 것이 설계 목표다.

### 0.2 Cortex-A의 TrustZone과 무엇이 다른가 (EL C5 재사용)

EL C5에서 Cortex-A의 TrustZone을 봤다. 이름은 같지만 구조가 꽤 다르다. 차이만 표로 정리한다.

| 항목 | Cortex-A (Armv8-A, EL C5) | Cortex-M (Armv8-M, 이 노트) |
|---|---|---|
| 보안 상태를 정하는 것 | EL3의 `SCR_EL3.NS` 비트 (모드 레지스터) | **실행 중인 명령의 주소**가 S인지 NS인지 (SAU/IDAU 판정) |
| 세계 전환 | `SMC` → EL3 모니터(TF-A BL31)가 문맥을 바꿔 줌 | 하드웨어 명령 `SG`(들어갈 때) · `BXNS`/`BLXNS`(나갈 때), 모니터 없음 |
| 전환 비용 | 모니터 코드가 레지스터를 저장 · 복원 (수백 명령 이상) | 함수 호출 + 몇 개 명령 (§8에서 실측 12 명령) |
| 메모리 구분 | MMU 페이지 테이블의 NS 비트 + TZASC | SAU(코어 안) + IDAU(칩 고정) + MPC/PPC(버스) |
| Secure OS | OP-TEE 같은 TEE OS가 EL1 S에서 | TF-M(SPM + 파티션) 또는 직접 짠 작은 Secure 이미지 |
| 인터럽트 | GIC 그룹(Group 0/1S/1NS) | NVIC `ITNS` 비트로 인터럽트마다 목표 상태 지정 |
| 예외 레벨 | EL0~EL3 × S/NS | Thread/Handler × 특권/비특권 × S/NS (EL 없음) |

가장 중요한 한 줄: **M 프로파일에는 "모니터"가 없다.** Secure 함수는 NS가 직접 BL로 부르는 보통 함수이고, 하드웨어가 "그 주소가 NSC이고 첫 명령이 SG인가"만 확인한다. 그래서 빠르지만, 그만큼 Secure 함수 작성자가 할 일(인자 검사, 레지스터 청소)이 많다.

### 0.3 이 노트의 길

1. 보안 상태는 주소가 정한다 — SAU · IDAU (§1)
2. 두 이미지를 만들어 QEMU에서 띄운다 (§2)
3. NS → S 호출의 정체: SG veneer, import library (§3)
4. S → NS 호출과 레지스터 청소 (§4)
5. TT 명령과 포인터 검사 — confused deputy (§5)
6. 잘못된 전환은 어떻게 터지나 — SecureFault 실험 8가지 (§6)
7. 뱅크 레지스터와 인터럽트 — ITNS, PRIS (§7)
8. 비용 실측 (§8)
9. RTOS가 NS 쪽에서 도는 구조 — FreeRTOS ARM_CM33 포트 (§9)
10. TF-M과 Zephyr (§10)

### 실습 환경 한 줄

QEMU 10.2.1 `mps2-an505`(Cortex-M33 + TrustZone, SYSCLK 20 MHz, Arm IoT Kit = SSE-200 이전 세대 서브시스템) 위에서 **Secure 이미지와 Non-secure 이미지를 따로 빌드**해 함께 올린다(arm-none-eabi-gcc 14.2.1, bare-metal + FreeRTOS V11.3.2 ARM_CM33 포트). Zephyr v4.4.2와 TF-M v2.3.1은 소스로 읽었다(TF-M 모듈이 Zephyr west 워크스페이스에 없어 `mps2/an521/cpu0/ns`는 빌드하지 않았다). 타이밍은 `-icount shift=5`(명령 1개 = 32 ns)로 잰 **QEMU icount 기준 값이며, 실제 칩과 절대값은 다르다**(캐시 · 플래시 wait state · SG/BXNS의 파이프라인 비용 미모델링).

빌드 · 실행 스크립트는 하나다. Secure 소스(`S`)와 NS 소스(`NS`)를 받아 두 ELF를 만들고, QEMU에 `-kernel s.elf -device loader,file=ns.elf`로 둘 다 올린다.

```sh
#!/bin/bash
# mk.sh <name>  — S="secure srcs" NS="ns srcs" → /var/tmp/rtos/F6/<name>/{s.elf,ns.elf}; RUN=1 → QEMU mps2-an505 (icount shift=5)
# SCFL/NSCFL = 추가 플래그. NS가 비어 있으면 Secure 이미지 하나만.
set -e
name=$1; C=/Users/donh/.rtos_scratch/F6/common; O=/var/tmp/rtos/F6/$name; mkdir -p $O
CF="-mthumb -mcpu=cortex-m33 -mfloat-abi=soft -O2 -g -std=gnu11 -Wall -Wextra -ffreestanding -fno-builtin -I$C"
IMPL=; [ -n "$NS" ] && IMPL="-Wl,--cmse-implib -Wl,--out-implib=$O/s_veneer.o"   # 진입 함수가 없으면 ld가 실패
arm-none-eabi-gcc $CF -mcmse -DSIDE_S $SCFL $S $C/start.c $C/semi.c -nostdlib -T $C/s.ld \
  $IMPL -Wl,-Map=$O/s.map -lgcc -o $O/s.elf
LOADNS=
if [ -n "$NS" ]; then
  arm-none-eabi-gcc $CF -DSIDE_NS $NSCFL $NS $C/start.c $C/semi.c $O/s_veneer.o -nostdlib -T $C/ns.ld \
    -Wl,-Map=$O/ns.map -lgcc -o $O/ns.elf
  LOADNS="-device loader,file=$O/ns.elf"
fi
if [ -n "$RUN" ]; then
  timeout --foreground ${TO:-20} qemu-system-arm -machine mps2-an505 -cpu cortex-m33 -nographic -monitor none \
    -serial null -semihosting-config enable=on,target=native -icount shift=5,align=off,sleep=off \
    -kernel $O/s.elf $LOADNS; echo "[qemu rc=$?]"
fi
```

눈여겨볼 플래그 세 개: Secure 쪽만 `-mcmse`(CMSE = Cortex-M Security Extensions 컴파일러 지원), Secure 링크에 `--cmse-implib --out-implib=s_veneer.o`(NS가 링크할 import library를 만든다), NS 링크에 `s_veneer.o`를 그냥 오브젝트처럼 넣는다. 출력은 semihosting으로 낸다. QEMU의 semihosting은 **NS 특권 코드에서도 동작**했다(예제 2) — 실칩 디버거에서는 다를 수 있다.

공용 시작 코드는 랩 템플릿과 같은 구조(벡터 테이블 → `.data` 복사 → `.bss` 0 → `main`)이고, Secure · NS 양쪽에 같은 파일을 쓴다. 벡터 7번이 SecureFault라는 점만 다르다.

```c
/* F6 공용 시작 코드 (발췌): 벡터 테이블(16 + 96 IRQ), .data/.bss 초기화. -DSIDE_S / -DSIDE_NS 로 양쪽에 쓴다 */
__attribute__((section(".isr_vector"), used))
void (*const g_vectors[16 + 96])(void) = {
    (void (*)(void))&_estack, Reset_Handler, F, HardFault_Handler, MemManage_Handler, BusFault_Handler,
    UsageFault_Handler, SecureFault_Handler, 0, 0, 0, SVC_Handler, F, 0, PendSV_Handler, SysTick_Handler,
    /* IRQ 0.. */ F, F, F, IRQ3_Handler, IRQ4_Handler, F, F, F, F, F, IRQ10_Handler, IRQ11_Handler, F, F, F, F,
    /* ... 나머지 IRQ는 기본 핸들러 F ... */
};
void Reset_Handler(void)
{
    uint32_t *s = &_sidata, *d = &_sdata;
    while (d < &_edata) *d++ = *s++;
    for (d = &_sbss; d < &_ebss; ) *d++ = 0;
    REG32(0xE000ED08u) = (uint32_t)g_vectors;   /* VTOR — 지금 상태(S 또는 NS)의 뱅크에 써진다 */
    semi_exit(main());
}
```

`REG32(0xE000ED08)`이 Secure에서 실행되면 VTOR_S에, NS에서 실행되면 VTOR_NS에 써진다는 것이 이 노트의 첫 번째 "뱅크" 사례다(§7).

---

## 1. 보안 상태는 "주소"가 정한다 — SAU와 IDAU

> 🔁 이 절은 Bare-Metal BM C12와 겹친다 (주인: 이 노트).

### 1.1 직관 — 건물의 층마다 붙은 출입 등급

Cortex-A는 "사람(CPU)이 지금 어떤 등급의 출입증을 차고 있나"로 보안을 따진다(SCR_EL3.NS). Cortex-M은 반대다. **건물의 방(주소)마다 등급이 붙어 있고**, 사람이 어느 방에서 일하고 있느냐로 그 사람의 등급이 정해진다.

- 코어가 Secure 주소의 명령을 실행하고 있으면 → Secure 상태
- NS 주소의 명령을 실행하고 있으면 → NS 상태
- NS 상태에서 Secure 주소로 그냥 점프하면 → 문지기가 막는다(SecureFault INVEP)
- 단, NSC 주소이고 그 자리 명령이 `SG`이면 → 통과, 그 순간부터 Secure 상태

데이터 접근도 같다. NS 상태의 코드가 Secure 주소를 읽거나 쓰면 SecureFault(AUVIOL)다. Secure 상태의 코드는 S, NS 주소 모두 접근할 수 있다(NS 주소로 가는 접근은 버스에 NS 트랜잭션으로 나간다).

### 1.2 정의 — 세 가지 속성과 두 판정기

모든 주소는 세 속성 중 하나를 받는다.

| 속성 | 뜻 | NS 코드가 할 수 있는 것 |
|---|---|---|
| S (Secure) | Secure 전용 | 아무것도 못 한다 (읽기 · 쓰기 · 실행 모두 fault) |
| NSC (Non-secure callable) | Secure이지만 NS가 **SG 명령 위치로만** 들어올 수 있음 | SG가 있는 주소로 분기 |
| NS (Non-secure) | 누구나 | 읽기 · 쓰기 · 실행 |

이 속성을 정하는 판정기가 둘이다.

- **IDAU(Implementation Defined Attribution Unit)**: 칩 설계자가 하드웨어로 고정한 판정기다. 보통 주소 비트 하나로 S/NS 별칭(alias)을 만든다. AN505(IoT Kit)는 **주소 bit 28**이 1이면 S, 0이면 NS다. 그래서 같은 SSRAM이 `0x0000_0000`(NS 별칭)과 `0x1000_0000`(S 별칭) 두 주소에 보인다. 칩마다 다르다(Nordic은 SPU, ST는 GTZC가 이 역할에 가깝다 — 칩 매뉴얼에서 확인).
- **SAU(Security Attribution Unit)**: Secure 소프트웨어가 런타임에 설정하는 판정기다. 코어 안에 있고, 영역 개수는 구현마다 다르다(QEMU M33은 8개 — 예제 1에서 `SAU_TYPE=8`). 각 영역은 시작 · 끝 주소(32바이트 단위)와 "NS인가 NSC인가" 비트를 가진다.

둘의 결과는 **더 안전한 쪽**으로 합쳐진다. 순서는 S > NSC > NS다.

| IDAU \ SAU | SAU: 영역 없음(=S) | SAU: NSC | SAU: NS |
|---|---|---|---|
| IDAU: S | S | **S** (NSC 안 됨!) | S |
| IDAU: NSC | S | NSC | NSC |
| IDAU: NS | S | NSC | NS |
| IDAU: 면제(exempt) | 면제 — 현재 상태 그대로 | 면제 | 면제 |

이 표의 굵은 칸이 실무 함정이다. AN505에서 Secure 코드 영역(0x1xxx_xxxx)은 IDAU가 S로 본다. 거기에 SAU로 NSC 영역을 만들어도 결과는 S다. **IoT Kit 보안 제어 블록의 `NSCCFG.CODENSC` 비트를 켜야** IDAU가 0x1xxx_xxxx을 NSC로 보고, 그제야 SAU의 NSC가 살아난다. 예제 5의 N8에서 이 비트를 끄자 정상 veneer 호출이 SecureFault로 바뀌는 것을 보인다. TF-M의 AN521 포팅 코드도 똑같이 `spctrl->nsccfg |= NSCCFG_CODENSC;`를 한다(§10 예제 13).

면제 영역은 System Control Space(`0xE000_0000` 근처, NVIC · SCB · SysTick)다. 이 주소는 접근하는 쪽의 현재 상태로 처리된다. 그래서 같은 `0xE000ED08`(VTOR)이 S에서는 VTOR_S, NS에서는 VTOR_NS가 된다.

QEMU 구현을 읽어 보면 위 표가 그대로 코드다(`target/arm/ptw.c`의 `v8m_security_lookup()`). SAU 영역을 다 본 다음 마지막에 "IDAU가 NS가 아니면, SAU가 NS였거나 (SAU가 NSC인데 IDAU가 NSC가 아니면) S로 덮어쓴다"는 줄이 있다. AN505의 IDAU는 `hw/arm/armsse.c`의 `armsse_idau_check()`에서 `region = address[31:28]`, `ns = !(region & 1)`, `nsc = (region == 1 && NSCCFG bit0) or (region == 3 && NSCCFG bit1)`, 면제 = `0xE000_0000~0xE00F_FFFF`과 `0xF000_0000~0xF00F_FFFF`로 정의된다.

```svg
<svg viewBox="0 0 660 400" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<rect x="190" y="10" width="280" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="330" y="32" font-size="13" text-anchor="middle">코어가 주소 A에 접근 (fetch 또는 데이터)</text>
<line x1="330" y1="44" x2="330" y2="62" stroke="currentColor" marker-end="url(#ah2)"/>
<rect x="190" y="64" width="280" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="330" y="86" font-size="13" text-anchor="middle">면제 영역인가? (SCS 0xE000_0000 근처)</text>
<line x1="470" y1="81" x2="508" y2="81" stroke="currentColor" marker-end="url(#ah2)"/>
<text x="489" y="74" font-size="12" text-anchor="middle">예</text>
<rect x="510" y="64" width="140" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="580" y="86" font-size="12" text-anchor="middle">현재 상태 뱅크로</text>
<line x1="270" y1="98" x2="160" y2="128" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="390" y1="98" x2="500" y2="128" stroke="currentColor" marker-end="url(#ah2)"/>
<text x="330" y="118" font-size="12" text-anchor="middle">아니오 — 두 판정기가 동시에 본다</text>
<rect x="30" y="130" width="260" height="56" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="160" y="153" font-size="13" text-anchor="middle">IDAU (칩 고정)</text>
<text x="160" y="173" font-size="12" text-anchor="middle">an505: 주소 bit 28 = 1이면 S</text>
<rect x="370" y="130" width="260" height="56" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="500" y="153" font-size="13" text-anchor="middle">SAU (Secure SW가 설정)</text>
<text x="500" y="173" font-size="12" text-anchor="middle">영역 8개, 32 B 단위, NS 또는 NSC</text>
<line x1="160" y1="186" x2="290" y2="214" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="500" y1="186" x2="370" y2="214" stroke="currentColor" marker-end="url(#ah2)"/>
<rect x="190" y="216" width="280" height="34" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="330" y="238" font-size="13" text-anchor="middle">더 안전한 쪽을 고른다: S &gt; NSC &gt; NS</text>
<line x1="330" y1="250" x2="330" y2="270" stroke="currentColor" marker-end="url(#ah2)"/>
<rect x="60" y="272" width="160" height="34" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="140" y="294" font-size="13" text-anchor="middle">S</text>
<rect x="250" y="272" width="160" height="34" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="330" y="294" font-size="13" text-anchor="middle">NSC (SG만 입구)</text>
<rect x="440" y="272" width="160" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="520" y="294" font-size="13" text-anchor="middle">NS</text>
<text x="330" y="330" font-size="12" text-anchor="middle">NS 상태가 S 주소를 만지면 여기서 SecureFault (코어 안)</text>
<rect x="60" y="346" width="540" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="330" y="365" font-size="13" text-anchor="middle">통과한 접근은 버스로: MPC(메모리 블록) · PPC(주변장치 포트)가</text>
<text x="330" y="381" font-size="12" text-anchor="middle">트랜잭션의 S/NS 비트를 한 번 더 검사 → 막히면 RAZ/WI 또는 bus error</text>
</svg>
```

그림 2 — 주소 하나의 보안 속성이 정해지는 순서. 코어 안의 판정(IDAU + SAU)이 1차 관문이고, 버스의 MPC/PPC가 2차 관문이다. 1차에서 걸리면 SecureFault, 2차에서 걸리면 BusFault 또는 "조용히 0 읽힘"(RAZ/WI)이다.

### 1.3 예제 1 — 리셋 직후의 Secure 세계 들여다보기

무엇을 확인하나: 코어가 보안 확장을 갖고 있는지, SAU 영역이 몇 개인지, 리셋 직후 SAU가 꺼져 있을 때 각 주소가 어떻게 판정되는지(TT 명령, §5에서 자세히).

```c
/* 예제 1: 리셋 직후 Secure 상태 확인 — 보안 확장 유무, SAU 영역 수, 뱅크 레지스터, IDAU 판정 */
#include <arm_cmse.h>
#include "semi.h"
int main(void)
{
    uint32_t ctrl, msp, msp_ns, ctrl_ns;
    __asm volatile("mrs %0, control\n mrs %1, msp\n mrs %2, msp_ns\n mrs %3, control_ns"
                   : "=r"(ctrl), "=r"(msp), "=r"(msp_ns), "=r"(ctrl_ns));
    semi_printf("ID_PFR1=0x%08x  (Security field [7:4] = %u)\n", REG32(0xE000ED44u), (REG32(0xE000ED44u) >> 4) & 0xF);
    semi_printf("SAU_TYPE=%u regions  SAU_CTRL=0x%x  AIRCR=0x%08x\n", REG32(0xE000EDD4u), REG32(0xE000EDD0u), REG32(0xE000ED0Cu));
    semi_printf("VTOR_S=0x%08x  VTOR_NS=0x%08x (alias 0xE002ED08)\n", REG32(0xE000ED08u), REG32(0xE002ED08u));
    semi_printf("CONTROL_S=0x%x CONTROL_NS=0x%x  MSP_S=0x%08x MSP_NS=0x%08x\n", ctrl, ctrl_ns, msp, msp_ns);
    semi_printf("NSCCFG=0x%x SECRESPCFG=0x%x  MPC(SSRAM0) CTRL=0x%x BLK_MAX=%u BLK_CFG=%u\n",
                REG32(0x50080014u), REG32(0x50080010u), REG32(0x58007000u), REG32(0x58007010u), REG32(0x58007014u));
    static const uint32_t a[] = { 0x00200000, 0x10000000, 0x20000000, 0x28200000, 0x30000000,
                                  0x38000000, 0x40000000, 0x50000000, 0xE000ED00 };
    semi_printf("addr        TT(raw)     S  IRVALID IREGION\n");
    for (unsigned i = 0; i < sizeof a / sizeof a[0]; i++) {
        cmse_address_info_t t = cmse_TT((void *)a[i]);      /* TT 명령: 이 주소의 보안 판정 */
        semi_printf("0x%08x  0x%08x  %s  %u       %u\n", a[i], t.value, t.flags.secure ? "S " : "NS",
                    t.flags.idau_region_valid, t.flags.idau_region);
    }
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/F6/ex1/s_probe.c RUN=1 /Users/donh/.rtos_scratch/F6/mk.sh ex1'
```

```text
ID_PFR1=0x00000210  (Security field [7:4] = 1)
SAU_TYPE=8 regions  SAU_CTRL=0x0  AIRCR=0xfa050000
VTOR_S=0x10000000  VTOR_NS=0x00000000 (alias 0xE002ED08)
CONTROL_S=0x0 CONTROL_NS=0x0  MSP_S=0x381fffd8 MSP_NS=0x00000000
NSCCFG=0x0 SECRESPCFG=0x0  MPC(SSRAM0) CTRL=0x100 BLK_MAX=127 BLK_CFG=5
addr        TT(raw)     S  IRVALID IREGION
0x00200000  0x00cc0000  S   1       0
0x10000000  0x01cc0000  S   1       1
0x20000000  0x02cc0000  S   1       2
0x28200000  0x02cc0000  S   1       2
0x30000000  0x03cc0000  S   1       3
0x38000000  0x03cc0000  S   1       3
0x40000000  0x04cc0000  S   1       4
0x50000000  0x05cc0000  S   1       5
0xe000ed00  0x004c0000  S   0       0
[qemu rc=0]
```

출력에서 볼 것:

- `ID_PFR1[7:4] = 1` — Security Extension이 구현돼 있다. 칩 bring-up 때 "이 M33에 TrustZone이 들어갔나"를 확인하는 레지스터다(B7: TrustZone은 칩 설계자가 고르는 옵션).
- `SAU_CTRL=0x0` — ENABLE=0, ALLNS=0. 이 상태에서는 **모든 주소가 S**다. IDAU가 NS라고 말하는 0x0020_0000, 0x2820_0000, 0x4000_0000도 표에서 전부 S다. 리셋 직후에는 NS 세계가 존재하지 않는다.
- IREGION 열이 정확히 주소의 위 4비트(0, 1, 2, 3, 4, 5)다. AN505 IDAU가 `address[31:28]`을 영역 번호로 쓴다는 QEMU 소스와 일치한다. SCB(0xE000ED00)는 IRVALID=0, 면제 영역이다.
- `VTOR_S=0x10000000` — 리셋 벡터는 Secure 별칭에서 읽혔다. `VTOR_NS=0`, `MSP_NS=0` — NS 쪽은 아직 아무것도 설정되지 않았다. Secure 부트 코드가 채워 줘야 한다(§2).
- `AIRCR=0xfa050000` — 읽을 때 위 16비트는 0xFA05(VECTKEYSTAT)이고 PRIS · BFHFNMINS 비트(§7)는 0이다.
- MPC: `BLK_MAX=127`(LUT 워드 128개 × 32비트 = 블록 4096개), `BLK_CFG=5`(블록 크기 = 2^(5+5) = 1 KB). 1 KB × 4096 = 4 MB = SSRAM0 전체. `CTRL=0x100`은 AUTOINC 비트다.

함정: 예제 1을 처음 짤 때 엔트리 함수 없이 `--cmse-implib`을 붙였더니 링커가 실패했다. Secure 이미지에 `cmse_nonsecure_entry` 함수가 하나도 없으면 import library를 만들 수 없다.

```text
ld: /var/tmp/rtos/F6/ex1/s_veneer.o: no symbol found for import library
ld: /var/tmp/rtos/F6/ex1/s_veneer.o: failed to generate import library
ld: final link failed: no symbols
```

(경로 앞부분 `/usr/lib/gcc/arm-none-eabi/14.2.1/../../../arm-none-eabi/bin/`은 줄였다.) 그래서 `mk.sh`는 NS 이미지가 있을 때만 이 플래그를 붙인다.

### 1.4 Don 경험과 연결

SSD 컨트롤러의 "보안 모드"는 보통 별도 보안 코어나 ROM 코드 + 레지스터 잠금으로 구현했을 것이다. TrustZone-M은 그 칸막이를 같은 코어의 주소 공간에 그린다. 가장 가까운 비유는 **MPU를 두 겹으로 겹친 것**이다. 다만 MPU(F4)는 "특권/비특권"을 나누고 소프트웨어(OS)가 태스크마다 바꾸는 반면, SAU는 "S/NS"를 나누고 **부팅 때 한 번 정하면 보통 바꾸지 않는다**. 그리고 MPU 자체도 S용과 NS용이 따로 있다(MPU_S, MPU_NS — §7). RTOS(NS)는 자기 MPU_NS로 태스크를 격리하고, Secure 쪽은 자기 MPU_S로 Secure 파티션을 격리한다(TF-M 격리 레벨 2/3, §10).

---

## 2. 두 이미지 만들기 — Secure 부트가 NS 세계를 연다

> 🔁 이 절은 Bare-Metal BM C12(bare-metal 두 이미지 구성)와 겹친다 (주인: 이 노트).

### 2.1 그림으로 — 이 노트의 메모리 배치

AN505의 메모리는 같은 물리 RAM이 S 별칭과 NS 별칭 두 주소에 보인다(IDAU bit 28). 그리고 각 물리 RAM 앞에 **MPC(Memory Protection Controller)** 가 있어서, 1 KB 블록마다 "S 트랜잭션만 통과" 또는 "NS 트랜잭션만 통과"를 정한다. QEMU `info mtree`로 확인한 AN505의 RAM은 SSRAM0(4 MB, `0x0000_0000`), SSRAM1(2 MB, `0x2800_0000`), SSRAM2(2 MB, `0x2820_0000`)이고, MPC 레지스터는 각각 `0x5800_7000`, `0x5800_8000`, `0x5800_9000`이다(QEMU `hw/arm/mps2-tz.c`의 `an505_raminfo`와 `apb_ppcexp0` 포트 표에서도 같은 값).

이 노트의 배치는 이렇게 정했다.

| 용도 | 주소(별칭) | 물리 메모리 | MPC 설정 | SAU |
|---|---|---|---|---|
| Secure 코드 · 벡터 | `0x1000_0000` (S) 1 MB | SSRAM0 아래 절반 | S (리셋값) | 영역 없음 → S |
| NSC veneer | `0x1010_0000` (S) 4 KB | SSRAM0 아래 절반 | S | 영역 2: NSC + `NSCCFG.CODENSC=1` |
| Secure 데이터 · 스택 | `0x3800_0000` (S) 2 MB | SSRAM1 | S (리셋값) | 영역 없음 → S |
| NS 코드 · 벡터 | `0x0020_0000` (NS) 2 MB | SSRAM0 위 절반 | NS로 바꿈 | 영역 0: NS |
| NS 데이터 · 스택 | `0x2820_0000` (NS) 2 MB | SSRAM2 | NS로 바꿈 | 영역 1: NS |
| 주변장치 NS 별칭 | `0x4000_0000~0x4FFF_FFFF` | APB/AHB 장치 | PPC가 포트마다 결정 | 영역 3: NS |

```svg
<svg viewBox="0 0 660 330" xmlns="http://www.w3.org/2000/svg">
<text x="110" y="22" font-size="13" text-anchor="middle">NS 별칭 주소</text>
<text x="330" y="22" font-size="13" text-anchor="middle">물리 메모리 (MPC 블록 설정)</text>
<text x="550" y="22" font-size="13" text-anchor="middle">S 별칭 주소</text>
<rect x="230" y="34" width="200" height="50" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="330" y="56" font-size="13" text-anchor="middle">SSRAM0 아래 2 MB</text>
<text x="330" y="74" font-size="12" text-anchor="middle">MPC: S</text>
<rect x="230" y="84" width="200" height="50" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="330" y="106" font-size="13" text-anchor="middle">SSRAM0 위 2 MB</text>
<text x="330" y="124" font-size="12" text-anchor="middle">MPC: NS</text>
<rect x="230" y="150" width="200" height="60" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="330" y="175" font-size="13" text-anchor="middle">SSRAM1 2 MB</text>
<text x="330" y="194" font-size="12" text-anchor="middle">MPC: S (리셋값 그대로)</text>
<rect x="230" y="226" width="200" height="60" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="330" y="251" font-size="13" text-anchor="middle">SSRAM2 2 MB</text>
<text x="330" y="270" font-size="12" text-anchor="middle">MPC: NS</text>
<text x="110" y="64" font-size="12" text-anchor="middle">0x0000_0000 (안 씀)</text>
<text x="110" y="114" font-size="12" text-anchor="middle">0x0020_0000 NS 코드</text>
<text x="110" y="185" font-size="12" text-anchor="middle">0x2800_0000 (막혀 있음)</text>
<text x="110" y="261" font-size="12" text-anchor="middle">0x2820_0000 NS RAM</text>
<text x="550" y="56" font-size="12" text-anchor="middle">0x1000_0000 S 코드</text>
<text x="550" y="74" font-size="12" text-anchor="middle">0x1010_0000 NSC veneer</text>
<text x="550" y="114" font-size="12" text-anchor="middle">0x1020_0000 (막혀 있음)</text>
<text x="550" y="185" font-size="12" text-anchor="middle">0x3800_0000 S RAM</text>
<text x="550" y="261" font-size="12" text-anchor="middle">0x3820_0000 (막혀 있음)</text>
<line x1="185" y1="110" x2="228" y2="110" stroke="#4a7bd0"/>
<line x1="185" y1="257" x2="228" y2="257" stroke="#4a7bd0"/>
<line x1="432" y1="60" x2="480" y2="60" stroke="#3f9a6b"/>
<line x1="432" y1="181" x2="480" y2="181" stroke="#3f9a6b"/>
<text x="330" y="312" font-size="12" text-anchor="middle">같은 물리 블록을 두 주소로 볼 수 있지만, MPC가 맞는 쪽 트랜잭션만 통과시킨다</text>
</svg>
```

그림 3 — 이 노트의 메모리 배치. 가운데가 물리 RAM, 양쪽이 같은 RAM을 보는 두 주소다. 색은 MPC 블록 설정(초록 S, 파랑 NS)이다. "막혀 있음"이라고 쓴 별칭은 주소 자체는 존재하지만 MPC가 그쪽 트랜잭션을 거부한다.

### 2.2 Secure 부트 코드가 하는 일 (순서가 중요하다)

리셋은 항상 Secure로 들어온다. Secure 부트 코드의 일은 NS 세계를 "만들어서" 넘겨주는 것이다.

1. **SAU 영역 설정**: NS 코드, NS RAM, NSC veneer, NS 주변장치 별칭. 그리고 `SAU_CTRL.ENABLE=1`.
2. **MPC 설정**: NS가 쓸 물리 블록을 NS로 바꾼다(그대로 두면 SAU가 열어도 버스에서 막힌다).
3. **PPC 설정**: NS 드라이버가 쓸 주변장치 포트를 NS로 연다(§6 N7).
4. **IDAU 보조 설정**: AN505는 `NSCCFG.CODENSC`(veneer가 0x1xxx_xxxx에 있으므로).
5. **인터럽트 대상**: NS가 처리할 IRQ를 `NVIC_ITNS`로 NS에 넘긴다(§7).
6. **SecureFault 켜기**: `SHCSR.SECUREFAULTENA`. 끄면 모든 보안 위반이 Secure HardFault로 올라가 원인 구분이 어렵다.
7. **NS 벡터 테이블과 스택**: `VTOR_NS` ← NS 이미지 시작, `MSP_NS` ← NS 벡터 0번 값.
8. **NS로 점프**: NS 리셋 핸들러 주소의 bit 0을 **0으로** 만든 뒤 `BLXNS`. bit 0 = 0이 "NS로 전환"의 표시다.

8번이 일반 부트로더(L1)와 다른 점이다. 보통 부트로더는 앱의 리셋 핸들러로 `BX`(bit 0 = 1, Thumb)한다. Secure → NS 점프는 반드시 `BLXNS`/`BXNS`여야 하고, 목적지 bit 0이 0이어야 한다. 이것을 안 지키면 §6 예제 5의 S1처럼 SecureFault(INVTRAN)이다.

이 순서를 코드로 묶은 공용 헤더다.

```c
/* F6 공용: an505 TrustZone 설정 (SAU, MPC, NSCCFG, NS 부팅) — Secure 이미지에서만 쓴다 (발췌) */
#include <arm_cmse.h>
#define SAU_CTRL  REG32(0xE000EDD0u)
#define SAU_RNR   REG32(0xE000EDD8u)
#define SAU_RBAR  REG32(0xE000EDDCu)
#define SAU_RLAR  REG32(0xE000EDE0u)
#define SCB_SHCSR REG32(0xE000ED24u)
#define VTOR_NS   REG32(0xE002ED08u)          /* NS 뱅크 SCB를 Secure에서 보는 별칭 */
#define NVIC_ITNS(n) REG32(0xE000E380u + 4u * ((n) / 32u))
#define SPC_NSCCFG     REG32(0x50080014u)     /* bit0 CODENSC: 0x1xxx_xxxx을 IDAU가 NSC로 */
#define SPC_SECRESPCFG REG32(0x50080010u)     /* bit0: 막힌 접근 → 1이면 bus error, 0이면 RAZ/WI */
#define MPC_SSRAM0 0x58007000u
#define MPC_SSRAM2 0x58009000u
typedef void __attribute__((cmse_nonsecure_call)) ns_fn_t(void);

static inline void sau_region(uint32_t n, uint32_t base, uint32_t limit, int nsc)
{
    SAU_RNR = n; SAU_RBAR = base & ~0x1Fu; SAU_RLAR = (limit & ~0x1Fu) | (nsc ? 2u : 0u) | 1u;
}
static inline void mpc_set_ns(uint32_t mpc, uint32_t off, uint32_t len)   /* off = 메모리 안의 오프셋 */
{
    uint32_t blk = 1u << (REG32(mpc + 0x14) + 5);                     /* BLK_CFG → 블록 크기 */
    REG32(mpc + 0x00) &= ~0x100u;                                       /* CTRL.AUTOINC 끔 */
    for (uint32_t a = off; a < off + len; a += blk) {
        uint32_t b = a / blk;
        REG32(mpc + 0x18) = b / 32u;                                    /* BLK_IDX */
        REG32(mpc + 0x1C) |= 1u << (b % 32u);                           /* BLK_LUT: 1 = NS */
    }
}
static inline void tz_setup(int codensc)
{
    sau_region(0, 0x00200000u, 0x003FFFFFu, 0);   /* NS 코드 (SSRAM0 위 2 MB, NS 별칭) */
    sau_region(1, 0x28200000u, 0x283FFFFFu, 0);   /* NS RAM  (SSRAM2, NS 별칭) */
    sau_region(2, 0x10100000u, 0x10100FFFu, 1);   /* NSC: veneer(.gnu.sgstubs) */
    sau_region(3, 0x40000000u, 0x4FFFFFFFu, 0);   /* NS 주변장치 별칭 */
    SAU_CTRL = 1u;                                 /* ENABLE=1, ALLNS=0 */
    mpc_set_ns(MPC_SSRAM0, 0x200000u, 0x200000u);  /* 물리 SSRAM0 위 절반을 NS로 */
    mpc_set_ns(MPC_SSRAM2, 0, 0x200000u);          /* SSRAM2 전체를 NS로 */
    if (codensc) SPC_NSCCFG |= 1u;                 /* IDAU도 0x1xxx_xxxx을 NSC로 */
    SCB_SHCSR |= (1u << 19);                       /* SECUREFAULTENA */
    __asm volatile("dsb\n isb" ::: "memory");
}
static inline void tz_boot_ns(uint32_t ns_vtor)
{
    const uint32_t *v = (const uint32_t *)ns_vtor;
    VTOR_NS = ns_vtor;
    __asm volatile("msr msp_ns, %0" :: "r"(v[0]));
    ns_fn_t *reset = (ns_fn_t *)cmse_nsfptr_create(v[1]);   /* bit0 = 0 → BLXNS가 NS로 전환 */
    reset();
}
```

몇 가지 설명.

- `SAU_RLAR`의 bit 0은 영역 enable, bit 1은 NSC다. RBAR/RLAR의 아래 5비트는 무시되므로 영역은 32바이트 단위다. `limit`은 "마지막 32바이트 블록의 시작 주소"로 넣어도 되고(하드웨어가 `| 0x1F`로 본다) 이 코드처럼 끝 주소를 넣어도 된다.
- `cmse_nsfptr_create(p)`는 `p & ~1`이다. `typedef ... __attribute__((cmse_nonsecure_call))` 함수 포인터로 부르면 컴파일러가 `BLXNS`를 만든다(§4).
- `msr msp_ns, r` — Secure 상태에서만 쓸 수 있는 "반대편 뱅크" 접근 명령이다. `mrs/msr`에 `_ns` 접미사가 붙은 레지스터 이름(`msp_ns`, `psp_ns`, `control_ns`, `primask_ns`, `basepri_ns` …)이 그 목록이다.
- 리셋값을 믿지 않는 것이 원칙이다. 실칩의 Secure 부트 코드는 SAU를 켜기 전에 모든 영역을 끄고(`RLAR=0`), 마지막에 `DSB; ISB`로 설정을 확정한다. TF-M AN521 코드(§10)도 `__DMB()` → 설정 → `__DSB(); __ISB()` 순서다.

### 2.3 링커 스크립트 두 개

Secure 쪽에는 `.gnu.sgstubs` 출력 섹션이 하나 더 있다. 링커가 만드는 SG veneer가 여기 들어가고, 이 섹션이 정확히 SAU NSC 영역과 겹쳐야 한다.

```text
/* s.ld — Secure 이미지: 코드 = SSRAM0 아래 1 MB(Secure 별칭), NSC = 0x10100000 4 KB, 데이터 = SSRAM1(Secure 별칭) */
MEMORY {
  S_CODE (rx)  : ORIGIN = 0x10000000, LENGTH = 1M
  S_NSC  (rx)  : ORIGIN = 0x10100000, LENGTH = 4K
  S_RAM  (rwx) : ORIGIN = 0x38000000, LENGTH = 2M
}
ENTRY(Reset_Handler)
SECTIONS {
  .isr_vector : { KEEP(*(.isr_vector)) } > S_CODE
  .text : { *(.text*) *(.rodata*) . = ALIGN(4); } > S_CODE
  .ARM.exidx : { *(.ARM.exidx*) } > S_CODE
  .gnu.sgstubs : ALIGN(32) { __sg_start = .; *(.gnu.sgstubs*) . = ALIGN(32); __sg_end = .; } > S_NSC
  _sidata = LOADADDR(.data);
  .data : { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > S_RAM AT > S_CODE
  .bss (NOLOAD) : { . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > S_RAM
  _estack = ORIGIN(S_RAM) + LENGTH(S_RAM);
}

/* ns.ld — Non-secure 이미지: 코드 = 0x00200000(NS 별칭), 데이터 = 0x28200000(NS 별칭) */
MEMORY {
  NS_CODE (rx)  : ORIGIN = 0x00200000, LENGTH = 2M
  NS_RAM  (rwx) : ORIGIN = 0x28200000, LENGTH = 2M
}
/* SECTIONS는 .gnu.sgstubs가 없다는 것만 빼고 s.ld와 같다 */
```

NSC 영역에는 **veneer만** 있어야 한다. 왜냐하면 NSC 영역 안에 우연히 SG와 같은 비트 패턴(`0xE97F E97F`)이 있으면 NS가 그 위치를 입구로 쓸 수 있기 때문이다. 상수 테이블이나 일반 코드가 NSC에 섞이면 안 되는 이유다. Zephyr의 `arch/arm/core/cortex_m/tz/secure_entry_functions.ld`도 `.gnu.sgstubs`를 32바이트 정렬하고 `__sg_start`/`__sg_end`/`__nsc_size` 심볼을 만들어 SAU 설정에 쓰게 한다.

### 2.4 예제 2 — 두 이미지로 첫 호출

무엇을 확인하나: Secure 부트가 NS를 띄우고, NS가 veneer로 Secure 함수 두 개를 부르는 최소 구성. Secure 쪽에서만 볼 수 있는 레지스터를 NS에서 읽으면 어떻게 보이는지도 본다.

```c
/* s_api.h — Secure가 NS에 내보내는 함수 (NS는 이 선언 + import library s_veneer.o로 링크) */
#ifdef SIDE_S
#define S_ENTRY __attribute__((cmse_nonsecure_entry, used))
#else
#define S_ENTRY
#endif
S_ENTRY int32_t s_add(int32_t a, int32_t b);
S_ENTRY uint32_t s_counter(void);

/* 예제 2 Secure 쪽: SAU/MPC 설정 → NS 부팅, 진입 함수 2개 제공 */
#include "tz.h"
#include "s_api.h"
static uint32_t secret_counter = 1000;           /* Secure RAM(0x38000000~)에 산다 */
S_ENTRY int32_t s_add(int32_t a, int32_t b) { secret_counter++; return a + b; }
S_ENTRY uint32_t s_counter(void) { return secret_counter; }
int main(void)
{
    tz_setup(1);
    semi_printf("[S] SAU on, MPC set, NSCCFG=0x%x, &secret_counter=0x%08x, s_add body=0x%08x\n",
                SPC_NSCCFG, (uint32_t)&secret_counter, (uint32_t)&s_add);
    tz_boot_ns(0x00200000u);
    semi_printf("[S] NS returned?\n");
    return 1;
}
```

```c
/* 예제 2 NS 쪽: veneer로 Secure 함수 호출, 자기 상태 확인 */
#include <arm_cmse.h>
#include "semi.h"
#include "s_api.h"
int main(void)
{
    uint32_t ctrl, msp; __asm volatile("mrs %0, control\n mrs %1, msp" : "=r"(ctrl), "=r"(msp));
    semi_printf("[NS] hello from 0x%08x, MSP=0x%08x, CONTROL=0x%x, VTOR=0x%08x\n",
                (uint32_t)&main, msp, ctrl, REG32(0xE000ED08u));
    int32_t r = s_add(40, 2);
    semi_printf("[NS] s_add(40,2)=%d via veneer 0x%08x, s_counter()=%u\n", r, (uint32_t)&s_add, s_counter());
    semi_printf("[NS] SAU_CTRL seen from NS=0x%x, ITNS0 seen from NS=0x%x\n", REG32(0xE000EDD0u), REG32(0xE000E380u));
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'D=/Users/donh/.rtos_scratch/F6/ex2; S=$D/s_main.c NS=$D/ns_main.c RUN=1 /Users/donh/.rtos_scratch/F6/mk.sh ex2'
```

```text
[S] SAU on, MPC set, NSCCFG=0x1, &secret_counter=0x38000000, s_add body=0x100001c1
[NS] hello from 0x002001c1, MSP=0x283fffe0, CONTROL=0x0, VTOR=0x00200000
[NS] s_add(40,2)=42 via veneer 0x10100009, s_counter()=1001
[NS] SAU_CTRL seen from NS=0x0, ITNS0 seen from NS=0x0
[qemu rc=0]
```

출력에서 볼 것:

- Secure 안에서 `&s_add`는 몸통 주소 `0x100001c1`(일반 코드 영역)이고, NS 안에서 `&s_add`는 `0x10100009`(NSC 영역의 veneer)다. **같은 이름이 양쪽에서 다른 주소를 가리킨다.** NS가 링크한 `s_add`는 import library가 준 veneer 주소다.
- NS의 `main`은 `0x002001c1`, MSP는 NS RAM 꼭대기(`0x2840_0000`) 바로 아래다. Secure가 `MSP_NS`에 넣어 준 값이다.
- `s_counter()=1001` — Secure RAM의 값이 `s_add` 호출로 증가했고, NS는 그 값을 **함수를 통해서만** 본다.
- NS에서 `SAU_CTRL`과 `NVIC_ITNS0`을 읽으면 0이다. 이 레지스터들은 Secure 전용이고 NS에서는 RAZ/WI(Read As Zero, Write Ignored)다. NS 코드는 자기가 어떤 세계에 갇혀 있는지조차 볼 수 없다.

### 2.5 SAU 뒤의 두 번째 문 — MPC와 PPC

SAU는 코어 안의 판정이다. 그런데 DMA처럼 코어가 아닌 마스터도 버스에 트랜잭션을 내보낸다. 그래서 TrustZone 칩은 버스 쪽에도 문을 둔다.

- **MPC(Memory Protection Controller)**: 메모리 앞. 블록(AN505는 1 KB)마다 S/NS를 정한다. 블록 설정과 트랜잭션의 보안 비트가 다르면 막는다.
- **PPC(Peripheral Protection Controller)**: 주변장치 앞. 포트(장치 하나)마다 "NS 접근 허용"과 "비특권 접근 허용"을 정한다. AN505에서는 IoT Kit 보안 제어 블록(`0x5008_0000`)의 `APBNSPPC0`(내부 타이머 등), `APBNSPPCEXP0~3`, `AHBNSPPCEXP0~1` 레지스터가 이 스위치다.
- **MSC(Master Security Controller)**: NS 쪽 DMA 같은 마스터의 트랜잭션에 보안 비트를 붙인다. (이 노트에서는 다루지 않는다.)

막혔을 때의 응답은 설정으로 고른다. MPC는 자기 `CTRL.SEC_RESP` 비트, PPC는 보안 제어 블록의 `SECRESPCFG` 비트다. 0이면 RAZ/WI(조용히 0이 읽힘), 1이면 bus error다. 실무 권장은 bus error다 — 조용한 0은 디버깅을 몇 시간씩 잡아먹는다. TF-M AN521 코드도 `spctrl->secrespcfg |= 1U;`로 bus error를 고른다.

이 문이 실제로 어떻게 동작하는지는 §6 예제 5의 N6, N7에서 본다.

---

## 3. NS → S 호출: SG veneer와 import library

> 🔁 이 절은 Bare-Metal BM C12(NSC veneer)와 겹친다 (주인: 이 노트).

### 3.1 직관 — 정문 앞의 접수창구

NS 코드가 Secure 함수 몸통으로 바로 뛰어들 수 있다면 함수 중간(예: 권한 검사 다음 줄)으로 점프해 검사를 건너뛸 수 있다. 그래서 하드웨어는 **NS → S 진입을 딱 한 종류의 명령, `SG`(Secure Gateway)에서만** 허용한다. 조건은 두 가지다.

1. 분기 목적지가 NSC 속성 주소다.
2. 그 주소의 첫 명령이 `SG`다(인코딩 `0xE97F E97F`).

조건이 맞으면 `SG`를 실행하는 순간 코어가 Secure 상태가 되고, LR의 bit 0을 0으로 만들어 "돌아갈 곳은 NS"라고 표시한다. 그다음 명령(보통 `B.W` 몸통)부터는 Secure 코드다.

컴파일러와 링커는 이 일을 자동으로 해 준다.

- `__attribute__((cmse_nonsecure_entry))` 함수 `f`를 `-mcmse`로 컴파일하면, 컴파일러는 몸통에 `__acle_se_f`라는 특수 심볼을 붙이고, 복귀를 `BXNS lr`로 바꾸며, 복귀 직전에 레지스터를 지운다(§4).
- 링커는 `__acle_se_f`가 있는 함수마다 `f:` `SG` + `B.W __acle_se_f` 두 명령짜리 veneer를 `.gnu.sgstubs`에 만든다.
- `--cmse-implib --out-implib=s_veneer.o`는 "veneer 주소만 담은 오브젝트(ABS 심볼)"를 만든다. NS는 이것을 링크해 `f`의 주소를 얻는다. Secure 코드 자체는 NS 빌드에 들어가지 않는다.

### 3.2 예제 3 — veneer · 몸통 · import library를 눈으로 보기

무엇을 확인하나: 예제 2 빌드의 `.gnu.sgstubs` 섹션, import library의 심볼, 진입 함수 몸통, 그리고 NS 쪽에 생긴 링커 veneer.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/rtos/F6/ex2; arm-none-eabi-objdump -d --no-show-raw-insn -j .gnu.sgstubs s.elf | tail -6;
  arm-none-eabi-readelf -s s_veneer.o | tail -2; arm-none-eabi-objdump -d --no-show-raw-insn s.elf | awk "/<__acle_se_s_add>:/,/^$/";
  arm-none-eabi-objdump -d --no-show-raw-insn ns.elf | awk "/_veneer>:/,/word/"'
```

```text
10100004:	b.w	10000228 <__acle_se_s_counter>

10100008 <s_add>:
10100008:	sg
1010000c:	b.w	100001c0 <__acle_se_s_add>
	...
     1: 10100001     8 FUNC    GLOBAL DEFAULT  ABS s_counter
     2: 10100009     8 FUNC    GLOBAL DEFAULT  ABS s_add
100001c0 <__acle_se_s_add>:
100001c0:	ldr	r2, [pc, #20]	@ (100001d8 <__acle_se_s_add+0x18>)
100001c2:	add	r0, r1
100001c4:	ldr	r3, [r2, #0]
100001c6:	mov	r1, lr
100001c8:	adds	r3, #1
100001ca:	str	r3, [r2, #0]
100001cc:	mov	ip, lr
100001ce:	mov	r2, lr
100001d0:	mov	r3, lr
100001d2:	msr	CPSR_fs, lr
100001d6:	bxns	lr
100001d8:	.word	0x38000000

002005c0 <__s_counter_veneer>:
  2005c0:	ldr.w	pc, [pc]	@ 2005c4 <__s_counter_veneer+0x4>
  2005c4:	.word	0x10100001
002005c8 <__s_add_veneer>:
  2005c8:	ldr.w	pc, [pc]	@ 2005cc <__s_add_veneer+0x4>
  2005cc:	.word	0x10100009
```

출력에서 볼 것:

- veneer는 **8바이트, 명령 두 개**(`sg`, `b.w`)다. `s_counter`가 `0x1010_0000`, `s_add`가 `0x1010_0008`. import library의 심볼은 `ABS`(절대 주소)이고 bit 0 = 1(Thumb)이다.
- 몸통 `__acle_se_s_add`의 끝: 반환값 r0만 남기고 r1, r2, r3, ip를 **LR 값으로 덮는다**. LR에는 NS가 이미 아는 복귀 주소가 들어 있으니 아무것도 새지 않는다. `msr CPSR_fs, lr`은 APSR 플래그(N, Z, C, V, Q, GE)를 지운다 — 비교 결과 같은 Secure 계산의 흔적이 플래그에 남지 않게. 마지막이 `bx lr`이 아니라 **`bxns lr`**.
- `.word 0x38000000`은 `secret_counter`의 주소다. 이 리터럴은 Secure 코드 영역에 있으니 NS는 못 읽는다.
- NS 쪽에는 링커가 만든 **long-branch veneer** `__s_add_veneer`(`ldr.w pc, [pc]` + 주소)가 있다. NS 코드는 `0x0020_xxxx`이고 Secure veneer는 `0x1010_xxxx`라 `BL`의 ±16 MB 범위를 넘기 때문이다. 즉 NS → S 호출은 실제로 `BL` → `ldr.w pc` → `SG` → `B.W` → 몸통, 네 번 분기한다(그림 4). 실칩에서는 NS 코드와 NSC를 가까이 두면 이 한 단계가 사라진다.

```svg
<svg viewBox="0 0 660 340" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<rect x="10" y="10" width="200" height="320" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="110" y="32" font-size="13" text-anchor="middle">NS 코드 0x0020_xxxx</text>
<rect x="230" y="10" width="200" height="320" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="330" y="32" font-size="13" text-anchor="middle">NSC 0x1010_xxxx</text>
<rect x="450" y="10" width="200" height="320" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="550" y="32" font-size="13" text-anchor="middle">S 코드 0x1000_xxxx</text>
<rect x="25" y="50" width="170" height="30" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="110" y="70" font-size="12" text-anchor="middle">1. bl __s_add_veneer</text>
<rect x="25" y="100" width="170" height="30" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="110" y="120" font-size="12" text-anchor="middle">2. ldr.w pc, [pc]</text>
<rect x="245" y="150" width="170" height="30" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="330" y="170" font-size="12" text-anchor="middle">3. sg (여기서 S가 됨)</text>
<rect x="245" y="190" width="170" height="30" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="330" y="210" font-size="12" text-anchor="middle">4. b.w __acle_se_s_add</text>
<rect x="465" y="230" width="170" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="550" y="250" font-size="12" text-anchor="middle">5. 계산 + 레지스터 청소</text>
<rect x="465" y="280" width="170" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="550" y="300" font-size="12" text-anchor="middle">6. bxns lr</text>
<line x1="110" y1="80" x2="110" y2="98" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="195" y1="120" x2="270" y2="148" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="330" y1="180" x2="330" y2="188" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="415" y1="210" x2="490" y2="228" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="550" y1="260" x2="550" y2="278" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="465" y1="295" x2="120" y2="295" stroke="#888" stroke-dasharray="5 4" marker-end="url(#ah3)"/>
<text x="330" y="288" font-size="12" text-anchor="middle">7. NS 복귀 (LR bit0 = 0)</text>
</svg>
```

그림 4 — 이 노트의 빌드에서 NS → S 호출 한 번이 지나가는 길. 2번은 주소 거리 때문에 링커가 넣은 것이고, 3~4번이 링커가 만든 SG veneer, 5~6번이 컴파일러가 만든 진입 함수 몸통이다.

### 3.3 왜 veneer를 따로 두나 — "SG 비트 패턴" 문제

몸통 첫 줄에 바로 `SG`를 넣으면 안 되나? 기술적으로는 가능하다(그 경우 몸통 전체가 NSC여야 한다). 하지만 NSC 영역 안의 **모든** `0xE97F E97F` 패턴이 입구가 될 수 있다는 것이 문제다. 일반 코드의 명령 두 개가 우연히 그 패턴을 만들거나, 상수 데이터에 그 값이 들어 있으면 의도하지 않은 입구가 생긴다. veneer를 작은 전용 영역에 몰아 두면 NSC를 최소화할 수 있다. Arm의 CMSE 문서도 이 이유로 veneer 방식을 권한다.

### 3.4 진입 함수에 걸리는 제약

진입 함수는 보통 C 함수처럼 보이지만 몇 가지 제약이 있다. 처음 만난 것은 컴파일 에러였다.

```text
s_main.c:6:1: error: 'cmse_nonsecure_entry' attribute not available to functions with arguments passed on the stack
    6 | ENTRY int32_t s_add5(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e) { return a + b + c + d + e; }  /* 5번째 인자는 스택 */
```

(예제 8을 처음 빌드할 때 나온 GCC 14.2.1 에러. 경로는 줄였다.) 인자가 4개를 넘으면(또는 큰 구조체 값 전달) 5번째부터는 **NS 스택**에 올라간다. Secure 함수가 NS 스택을 읽으려면 그 주소가 정말 NS인지 검사해야 하는데, 컴파일러가 자동으로 해 줄 방법이 없어서 GCC는 아예 거부한다. 해결은 구조체 포인터 하나로 넘기고 Secure 쪽에서 `cmse_check_pointed_object()`로 검사하는 것이다(§5).

나머지 규칙을 표로 모은다.

| 규칙 | 이유 | 지키는 법 |
|---|---|---|
| 인자 · 반환은 레지스터(r0~r3)로 | NS 스택 접근은 검사가 필요 | 인자 4개 이하, 큰 데이터는 포인터 + 검사 |
| 포인터 인자는 NS 메모리인지 확인 | confused deputy(§5) | `cmse_check_address_range()` |
| NS가 준 함수 포인터는 `cmse_nsfptr_create`로 | bit 0 = 0이어야 BLXNS가 NS로 간다 | `cmse_is_nsfptr()`로 확인 후 호출 |
| 진입 함수 안에서 NS 콜백 후 상태 재확인 | 콜백 동안 NS가 메모리를 바꿀 수 있음 | 값은 Secure 메모리에 복사해 두고 쓴다 |
| 재진입 주의 | NS 인터럽트가 Secure 함수 실행 중에 NS로 가서 같은 진입 함수를 다시 부를 수 있음 | Secure 쪽 락 또는 "NS 인터럽트 마스크"(TF-M `CONFIG_TFM_SECURE_THREAD_MASK_NS_INTERRUPT`) |

---

## 4. S → NS 호출과 레지스터 청소

### 4.1 BLXNS — Secure가 NS 함수를 부를 때

Secure 코드가 NS 함수를 불러야 할 때가 있다. 예제 2의 `tz_boot_ns()`(NS 리셋 핸들러 호출)와 "NS가 등록한 콜백"이 대표적이다. 이때 쓰는 명령이 `BLXNS`다. 하드웨어는 이렇게 동작한다.

1. 진짜 복귀 주소(Secure 주소)와 일부 상태를 **Secure 스택**에 push한다.
2. LR에 `FNC_RETURN`이라는 마법 값(`0xFEFF_FFFF` 계열)을 넣는다. NS 코드는 진짜 Secure 복귀 주소를 볼 수 없다.
3. 목적지 bit 0 = 0이면 NS 상태로 바꾸고 분기한다.
4. NS 함수가 끝나서 `BX lr`로 `FNC_RETURN`에 분기하면, 코어가 Secure 스택에서 진짜 복귀 주소를 꺼내 Secure로 돌아온다.

EXC_RETURN(B2)과 아주 비슷한 장치다. "특별한 값으로 분기하면 하드웨어가 숨겨 둔 상태를 꺼낸다."

### 4.2 무엇을 지우나 — 예제 3의 나머지

NS 함수는 신뢰할 수 없으므로 Secure의 레지스터 값이 넘어가면 안 된다. 예제 2의 `tz_boot_ns()`가 만든 코드와 libgcc 도우미를 보자.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/rtos/F6/ex2; arm-none-eabi-objdump -d --no-show-raw-insn s.elf | grep -B8 "bl.*<__gnu_cmse_nonsecure_call>";
  arm-none-eabi-objdump -d --no-show-raw-insn s.elf | awk "/<__gnu_cmse_nonsecure_call>:/,/ldmia/"'
```

```text
100002e8:	ldr	r4, [r5, #4]
100002ea:	bic.w	r4, r4, #1
100002ee:	lsrs	r4, r4, #1
100002f0:	lsls	r4, r4, #1
100002f2:	mov	r0, r4
100002f4:	mov	r1, r4
100002f6:	mov	r2, r4
100002f8:	mov	r3, r4
100002fa:	bl	100006f8 <__gnu_cmse_nonsecure_call>
100006f8 <__gnu_cmse_nonsecure_call>:
100006f8:	stmdb	sp!, {r5, r6, r7, r8, r9, sl, fp, lr}
100006fc:	mov	r7, r4
100006fe:	mov	r8, r4
10000700:	mov	r9, r4
10000702:	mov	sl, r4
10000704:	mov	fp, r4
10000706:	mov	ip, r4
10000708:	sub	sp, #136	@ 0x88
1000070a:	vlstm	sp
1000070e:	msr	CPSR_f, r4
10000712:	mov	r5, r4
10000714:	mov	r6, r4
10000716:	blxns	r4
10000718:	mrs	r5, CONTROL
1000071c:	tst.w	r5, #8
10000720:	it	ne
10000722:	vmovne.f32	s0, s0
10000726:	vlldm	sp
1000072a:	add	sp, #136	@ 0x88
1000072c:	ldmia.w	sp!, {r5, r6, r7, r8, r9, sl, fp, pc}
```

출력에서 볼 것:

- 호출하는 쪽: 목적지 주소(r4)의 bit 0을 지우고(`bic`, `lsrs/lsls`), 쓰지 않는 인자 레지스터 r0~r3를 목적지 주소로 덮는다.
- `__gnu_cmse_nonsecure_call`(libgcc): callee-saved r5~r11을 Secure 스택에 저장한 뒤 **목적지 주소 값으로 전부 덮는다**. `vlstm sp`는 Secure 쪽 FP 레지스터를 스택 공간에 "게으르게" 저장하고 FP 레지스터를 지우는 명령이다(FP를 안 쓰면 거의 공짜). 플래그도 지우고 `blxns r4`.
- 돌아오면 `vlldm`으로 FP 상태를 되돌리고 r5~r11을 복원한다. NS 함수가 callee-saved 규칙을 어겨도 Secure는 영향을 받지 않는다.

### 4.3 예제 3b — FPU를 쓰면 청소가 길어진다, v8.1-M은 CLRM으로 짧아진다

무엇을 확인하나: 같은 진입 함수 · NS 호출을 soft-float(M33), hard-float(M33), hard-float(M55, Armv8.1-M)로 컴파일했을 때 청소 코드의 차이. (빌드 · 디스어셈블만 함 — 실행 결과가 아니라 생성 코드를 보는 예제다.)

```c
/* fp.c — 청소 코드 비교용 */
#include <stdint.h>
__attribute__((cmse_nonsecure_entry)) int32_t s_add(int32_t a, int32_t b) { return a + b; }
__attribute__((cmse_nonsecure_entry)) float s_fmul(float a, float b) { return a * b; }
typedef void __attribute__((cmse_nonsecure_call)) cb_t(void);
void s_call(cb_t *f) { f(); }
```

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/rtos/F6 && for abi in soft hard; do echo "=== -mfloat-abi=$abi";
  arm-none-eabi-gcc -mcpu=cortex-m33 -mthumb -mfloat-abi=$abi -mcmse -O2 -c fp/fp.c -o fp/fp_$abi.o &&
  arm-none-eabi-objdump -d --no-show-raw-insn fp/fp_$abi.o | grep -v "^$\|file format\|Disassembly"; done;
  arm-none-eabi-gcc -mcpu=cortex-m55 -mthumb -mfloat-abi=hard -mcmse -O2 -c fp/fp.c -o fp/fp_m55.o &&
  arm-none-eabi-objdump -d --no-show-raw-insn fp/fp_m55.o | sed -n "/<s_add>/,/^$/p;/<s_call>/,/^$/p"'
```

```text
=== -mfloat-abi=soft
00000000 <s_add>:
   0:	add	r0, r1
   2:	mov	r2, lr
   4:	mov	r1, lr
   6:	mov	r3, lr
   8:	mov	ip, lr
   a:	msr	CPSR_fs, lr
   e:	bxns	lr
...
=== -mfloat-abi=hard
00000000 <s_add>:
   0:	add	r0, r1
   2:	mov	r2, lr
   4:	mov	r1, lr
   6:	mov	r3, lr
   8:	vmov.f32	s0, #112	@ 0x3f800000  1.0
   c:	vmov.f32	s1, #112	@ 0x3f800000  1.0
...
  44:	vmov.f32	s15, #112	@ 0x3f800000  1.0
  48:	msr	CPSR_fs, lr
  4c:	push	{r4}
  4e:	vmrs	ip, fpscr
  52:	movw	r4, #65376	@ 0xff60
  56:	movt	r4, #4095	@ 0xfff
  5a:	and.w	ip, ip, r4
  5e:	vmsr	fpscr, ip
  62:	pop	{r4}
  64:	mov	ip, lr
  66:	bxns	lr
...
00000000 <s_add>:
   0:	vstr	FPCXTNS, [sp, #-4]!
   4:	add	r0, r1
   6:	vscclrm	{s0-s15, VPR}
   a:	clrm	{r1, r2, r3, ip, APSR}
   e:	vldr	FPCXTNS, [sp], #4
  12:	bxns	lr

0000002c <s_call>:
  2c:	push	{r3, lr}
  2e:	lsrs	r0, r0, #1
  30:	lsls	r0, r0, #1
  32:	stmdb	sp!, {r4, r5, r6, r7, r8, r9, sl, fp}
  36:	vpush	{d8-d15}
  3a:	vscclrm	{s0-s31, VPR}
  3e:	clrm	{r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, ip, APSR}
  42:	blxns	r0
  44:	vpop	{d8-d15}
  48:	ldmia.w	sp!, {r4, r5, r6, r7, r8, r9, sl, fp}
  4c:	pop	{r3, pc}
  4e:	nop
```

(hard-float의 `vmov.f32 s2`~`s14` 13줄, soft · hard의 `s_fmul`/`s_call`은 `...`로 줄였다.)

출력에서 볼 것:

- hard-float M33: 정수 레지스터 청소에 더해 **s0~s15를 16개의 `vmov`로 덮고**, FPSCR의 플래그 비트를 지운다. 진입 함수 하나에 명령 약 25개가 붙는다. Secure 쪽 FP를 쓰는 설계라면 이 비용을 계산에 넣어야 한다.
- M55(Armv8.1-M): 같은 일을 `vscclrm {s0-s15, VPR}` 한 명령과 `clrm {r1, r2, r3, ip, APSR}` 한 명령으로 한다. `FPCXTNS`(NS FP 문맥 레지스터)를 저장 · 복원하는 줄도 생겼다. v8.1-M이 TrustZone 경계 비용을 줄이려고 넣은 명령들이다(B7의 v8.1-M 표와 연결).
- soft-float에서도 `s_call`이 부르는 libgcc 도우미는 `vlstm`/`vlldm`을 쓴다(§4.2). 라이브러리가 "FPU가 있을 수도 있는 코어"를 가정하기 때문이다.

### 4.4 왜 컴파일러에게 맡기나

손으로 어셈블리 진입 함수를 짜면 이 청소를 빠뜨리기 쉽다. 예를 들어 r12에 Secure 키의 일부가 남은 채 `bxns lr`하면 NS가 그대로 읽는다. 그래서 규칙은 단순하다: **진입 함수는 C로 쓰고 `cmse_nonsecure_entry`를 붙인다. naked 어셈블리 진입 함수가 꼭 필요하면 청소를 직접 증명한다.** TF-M도 대부분 C 진입 함수(`__tz_c_veneer` = `cmse_nonsecure_entry, noclone`)를 쓰고 일부만 `__tz_naked_veneer`를 쓴다.

---

## 5. TT 명령과 포인터 검사 — confused deputy

### 5.1 TT — "이 주소, 누구 것인가?"

`TT`(Test Target) 명령은 주소 하나를 받아 MPU · SAU · IDAU가 그 주소를 어떻게 판정하는지 32비트로 돌려준다. 변형이 넷이다: `TT`(현재 상태 · 현재 특권), `TTT`(비특권으로 가정), `TTA`(반대 보안 상태의 MPU로 — Secure에서만), `TTAT`. C에서는 `<arm_cmse.h>`의 `cmse_TT()`, `cmse_TTT()`, `cmse_TTA()`, `cmse_TTAT()`로 부른다.

| 비트 | 이름 | 뜻 |
|---|---|---|
| 7:0 | MREGION | MPU 영역 번호 (MRVALID일 때) |
| 15:8 | SREGION | SAU 영역 번호 (SRVALID일 때, Secure에서만) |
| 16 | MRVALID | MPU 영역이 하나 맞았나 |
| 17 | SRVALID | SAU 영역이 하나 맞았나 |
| 18, 19 | R, RW | 현재 MPU 기준으로 읽기 / 쓰기 가능 |
| 20, 21 | NSR, NSRW | NS가 읽기 / 쓰기 가능 (R AND NOT S) |
| 22 | S | 이 주소가 Secure (Secure에서만 의미) |
| 23 | IRVALID | IDAU 영역 번호가 유효 |
| 31:24 | IREGION | IDAU 영역 번호 |

### 5.2 예제 4 — 같은 주소, 양쪽에서 TT

무엇을 확인하나: SAU 설정이 끝난 뒤 열 개의 주소를 Secure와 NS에서 각각 TT로 물으면 무엇이 보이는가.

```c
/* 예제 4 공용 tt_table.h: 같은 주소 목록에 TT를 실행해 표로 찍는다 (Secure/NS 양쪽에서 include) */
#include <arm_cmse.h>
#include "semi.h"
static const uint32_t tt_addr[] = { 0x00200000, 0x10000000, 0x10100000, 0x28200000, 0x38000000,
                                    0x28000000, 0x00000000, 0x40200000, 0x50200000, 0xE000ED00 };
static const char *const tt_name[] = { "NS code", "S code", "NSC veneer", "NS RAM", "S RAM",
                                       "SSRAM1 NS alias", "SSRAM0 NS alias lo", "UART0 NS alias", "UART0 S alias", "SCB" };
static inline void tt_print(const char *side)
{
    semi_printf("--- TT executed in %s state ---\n", side);
    semi_printf("addr        raw         S  SREG(valid) IREG(valid) R RW NSR NSRW  what\n");
    for (unsigned i = 0; i < sizeof tt_addr / sizeof tt_addr[0]; i++) {
        cmse_address_info_t t = cmse_TT((void *)tt_addr[i]);
        semi_printf("0x%08x  0x%08x  %u  %u(%u)        %u(%u)        %u %u  %u   %u     %s\n", tt_addr[i], t.value,
                    (t.value >> 22) & 1, (t.value >> 8) & 0xFF, (t.value >> 17) & 1, t.value >> 24,
                    (t.value >> 23) & 1, (t.value >> 18) & 1, (t.value >> 19) & 1, (t.value >> 20) & 1,
                    (t.value >> 21) & 1, tt_name[i]);
    }
}
/* s_main.c: tz_setup(1); tt_print("Secure"); tz_boot_ns(0x00200000u);   ns_main.c: tt_print("Non-secure"); */
```

```text
--- TT executed in Secure state ---
addr        raw         S  SREG(valid) IREG(valid) R RW NSR NSRW  what
0x00200000  0x00be0000  0  0(1)        0(1)        1 1  1   1     NS code
0x10000000  0x01cc0000  1  0(0)        1(1)        1 1  0   0     S code
0x10100000  0x01ce0200  1  2(1)        1(1)        1 1  0   0     NSC veneer
0x28200000  0x02be0100  0  1(1)        2(1)        1 1  1   1     NS RAM
0x38000000  0x03cc0000  1  0(0)        3(1)        1 1  0   0     S RAM
0x28000000  0x02cc0000  1  0(0)        2(1)        1 1  0   0     SSRAM1 NS alias
0x00000000  0x00cc0000  1  0(0)        0(1)        1 1  0   0     SSRAM0 NS alias lo
0x40200000  0x04be0300  0  3(1)        4(1)        1 1  1   1     UART0 NS alias
0x50200000  0x05cc0000  1  0(0)        5(1)        1 1  0   0     UART0 S alias
0xe000ed00  0x004c0000  1  0(0)        0(0)        1 1  0   0     SCB
--- TT executed in Non-secure state ---
addr        raw         S  SREG(valid) IREG(valid) R RW NSR NSRW  what
0x00200000  0x000c0000  0  0(0)        0(0)        1 1  0   0     NS code
0x10000000  0x000c0000  0  0(0)        0(0)        1 1  0   0     S code
0x10100000  0x000c0000  0  0(0)        0(0)        1 1  0   0     NSC veneer
0x28200000  0x000c0000  0  0(0)        0(0)        1 1  0   0     NS RAM
0x38000000  0x000c0000  0  0(0)        0(0)        1 1  0   0     S RAM
0x28000000  0x000c0000  0  0(0)        0(0)        1 1  0   0     SSRAM1 NS alias
0x00000000  0x000c0000  0  0(0)        0(0)        1 1  0   0     SSRAM0 NS alias lo
0x40200000  0x000c0000  0  0(0)        0(0)        1 1  0   0     UART0 NS alias
0x50200000  0x000c0000  0  0(0)        0(0)        1 1  0   0     UART0 S alias
0xe000ed00  0x000c0000  0  0(0)        0(0)        1 1  0   0     SCB
[qemu rc=0]
```

출력에서 볼 것:

- Secure 쪽 표에서 SAU 영역 0(NS 코드), 1(NS RAM), 3(주변장치)이 맞은 주소만 S=0, NSR=NSRW=1이다. 영역 2(NSC veneer)는 S=1로 나온다. **TT에는 "NSC" 비트가 없다.** NSC인지는 "S=1인데 SREGION이 NSC로 설정된 영역 번호"로 알아낸다.
- `0x2800_0000`(SSRAM1의 NS 별칭)과 `0x0000_0000`(SSRAM0의 NS 별칭 아랫부분)은 IDAU로는 NS인데 SAU 영역이 없어서 S다. §1.2 표의 "SAU 영역 없음 = S" 열이다.
- NS 쪽 표는 전부 같은 값 `0x000c0000`(R=1, RW=1)이다. NS에서 TT는 **자기 MPU 정보만** 돌려준다. MPU_NS를 켜지 않았으니 기본 맵(전부 R/W)이다. S 비트, SAU · IDAU 영역은 NS에게 보이지 않는다. NS가 "이 주소가 Secure인가"를 TT로 알아낼 방법은 없다.
- SCB(`0xE000ED00`)는 IRVALID=0, 면제다.

### 5.3 confused deputy — Secure가 NS의 대리인이 되어 버리는 문제

**confused deputy(혼동된 대리인)** 는 권한 있는 쪽(Secure)이 권한 없는 쪽(NS)의 요청을 받아 일하면서, 요청에 담긴 주소를 확인하지 않아 결과적으로 NS가 원래 못 하는 일을 대신해 주는 문제다.

예: Secure 서비스 `s_sum(p, n)`은 "버퍼 p의 n바이트 합"을 돌려준다. NS가 자기 버퍼를 주면 문제없다. 그런데 NS가 **Secure 키의 주소**를 p로 주면? Secure 코드는 Secure 상태에서 실행되니 그 주소를 읽을 수 있고, 합을 돌려준다. n = 1로 한 바이트씩 물으면 키가 통째로 샌다. 쓰기 방향도 같다. `s_version(dst)`이 dst에 버전 번호를 쓰는데, NS가 dst로 Secure 플래그의 주소를 주면 Secure 상태가 바뀐다.

### 5.4 예제 6 — 키 유출과 플래그 덮어쓰기, 그리고 검사

무엇을 확인하나: 검사하지 않는 서비스(naive)와 `cmse_check_address_range()`/`cmse_check_pointed_object()`로 검사하는 서비스(checked)를 NS가 공격자 역할로 부른다.

```c
/* 예제 6 Secure 쪽: NS가 넘긴 포인터를 믿는 서비스(naive)와 검사하는 서비스(checked) */
#include "tz.h"
#define ENTRY __attribute__((cmse_nonsecure_entry, used))
static const uint8_t key[4] = { 0xA5, 0x3C, 0x0F, 0x77 };     /* Secure 비밀 */
static volatile uint32_t unlocked;                          /* Secure 상태 플래그 */
ENTRY const void *s_key_addr(void) { return key; }
ENTRY const volatile void *s_flag_addr(void) { return &unlocked; }
ENTRY uint32_t s_flag(void) { return unlocked; }
ENTRY int32_t s_sum_naive(const uint8_t *p, uint32_t n)      /* 읽기 대리인 */
{
    uint32_t s = 0; while (n--) s += *p++; return (int32_t)s;
}
ENTRY int32_t s_sum_checked(const uint8_t *p, uint32_t n)
{
    if (cmse_check_address_range((void *)p, n, CMSE_NONSECURE | CMSE_MPU_READ) == NULL) return -1;
    uint32_t s = 0; while (n--) s += *p++; return (int32_t)s;
}
ENTRY int32_t s_version_naive(uint32_t *dst)                 /* 쓰기 대리인 */
{
    *dst = 0x00010002u; return 0;
}
ENTRY int32_t s_version_checked(uint32_t *dst)
{
    uint32_t *d = cmse_check_pointed_object(dst, CMSE_NONSECURE | CMSE_MPU_READWRITE);
    if (d == NULL) return -1;
    *d = 0x00010002u; return 0;
}
int main(void) { tz_setup(1); tz_boot_ns(0x00200000u); return 1; }
```

(실습을 위해 키 · 플래그 주소를 알려 주는 진입 함수를 일부러 넣었다. 실제 공격자는 맵 파일이나 덤프로 주소를 안다고 가정한다.)

```c
/* 예제 6 NS 쪽: 공격자 역할 — Secure 주소를 인자로 넘긴다 */
#include "semi.h"
const void *s_key_addr(void); const volatile void *s_flag_addr(void); uint32_t s_flag(void);
int32_t s_sum_naive(const uint8_t *, uint32_t); int32_t s_sum_checked(const uint8_t *, uint32_t);
int32_t s_version_naive(uint32_t *); int32_t s_version_checked(uint32_t *);
static uint8_t mine[4] = { 1, 2, 3, 4 };
static uint32_t ver;
int main(void)
{
    const uint8_t *k = s_key_addr();
    uint32_t *flag = (uint32_t *)s_flag_addr();
    semi_printf("own buffer      : naive=%d checked=%d\n", s_sum_naive(mine, 4), s_sum_checked(mine, 4));
    semi_printf("key bytes leaked by naive: ");
    for (int i = 0; i < 4; i++) semi_printf("%02x ", (uint32_t)s_sum_naive(k + i, 1));
    semi_printf("\nkey via checked : %d\n", s_sum_checked(k, 4));
    const uint8_t *edge = (const uint8_t *)0x283FFFF8u;      /* NS RAM 끝 8바이트 + 다음 8바이트(Secure) */
    semi_printf("range crossing NS->S end: checked(edge,8)=%d checked(edge,16)=%d\n",
                s_sum_checked(edge, 8), s_sum_checked(edge, 16));
    semi_printf("flag before=%u ", s_flag());
    s_version_checked(flag); semi_printf("after checked write=%u ", s_flag());
    s_version_naive(flag);   semi_printf("after naive write=0x%x\n", s_flag());
    semi_printf("normal use: checked(&ver)=%d ver=0x%x\n", s_version_checked(&ver), ver);
    return 0;
}
```

```text
own buffer      : naive=10 checked=10
key bytes leaked by naive: a5 3c 0f 77 
key via checked : -1
range crossing NS->S end: checked(edge,8)=1289 checked(edge,16)=-1
flag before=0 after checked write=0 after naive write=0x10002
normal use: checked(&ver)=0 ver=0x10002
[qemu rc=0]
```

출력에서 볼 것:

- naive 서비스로 키 4바이트 `a5 3c 0f 77`이 그대로 샜다. NS가 직접 읽으면 SecureFault인 메모리를 **Secure가 대신 읽어 준** 것이다.
- naive 쓰기 서비스로 Secure 플래그가 0 → `0x10002`가 됐다. "잠금 해제" 같은 상태 변수였다면 권한 상승이다.
- checked는 Secure 주소에 -1을 돌려준다. 범위가 **NS에서 시작해 S로 넘어가는** 경우(`edge`, 16바이트)도 거부한다. `cmse_check_address_range()`는 시작과 끝만 보는 게 아니라 TT로 범위가 하나의 SAU/MPU 영역 안에 있는지 확인하기 때문이다(8바이트일 때는 NS RAM 끝 안에 있어서 통과, 값 1289는 NS 스택 꼭대기의 쓰레기 합).
- 플래그 `CMSE_NONSECURE`는 "NS가 접근 가능한가"를 묻는다(내부적으로 `TTA` 계열). `CMSE_MPU_READ`/`CMSE_MPU_READWRITE`는 NS MPU 권한도 확인한다. NS 태스크가 비특권이면 `CMSE_MPU_UNPRIV`를 더해 "그 태스크가 원래 이 메모리를 만질 수 있었나"까지 묻는다. RTOS가 NS 쪽에서 MPU로 태스크를 격리할 때(F5) 이것을 빼면, 비특권 NS 태스크가 Secure 서비스를 통해 **다른 NS 태스크**의 메모리를 읽는 길이 생긴다.

### 5.5 검사 다음의 함정 — TOCTOU와 double fetch

주소 검사를 통과해도 끝이 아니다. NS 메모리는 Secure가 쓰는 동안에도 NS(다른 코어나 DMA, 또는 NS 인터럽트 핸들러)가 바꿀 수 있다. 예를 들어 길이 필드를 검사한 뒤 다시 읽어 memcpy에 쓰면, 두 번째 읽기 사이에 NS가 길이를 키워 버퍼를 넘길 수 있다(**double fetch**, TOCTOU). 규칙: **NS가 준 구조체는 Secure 메모리로 한 번 복사한 뒤 복사본만 검사하고 쓴다.** TF-M의 PSA API가 `psa_invec`/`psa_outvec` 설명자를 SPM이 먼저 검사 · 복사하는 이유다. 그리고 TF-M v2.3.1 릴리스 노트의 TFMV-10은 바로 "mailbox 초기화에서 NS 포인터 검사 누락"이다 — 실제 보안 권고가 나오는 종류의 버그라는 뜻이다.

---

## 6. 잘못된 전환은 어떻게 터지나 — SecureFault 실험

> 🔁 이 절은 Bare-Metal BM C12(보안 상태 전환)와 겹친다 (주인: 이 노트).

### 6.1 SecureFault와 SFSR

**SecureFault**는 Armv8-M에서 새로 생긴 예외(번호 7)다. 보안 규칙 위반을 알리고, 항상 Secure 쪽으로 간다. `SHCSR.SECUREFAULTENA`를 켜지 않으면 Secure HardFault로 올라간다. 원인은 **SFSR**(Secure Fault Status Register, `0xE000_EDE4`)에, 주소는 **SFAR**(`0xE000_EDE8`)에 남는다.

| SFSR 비트 | 이름 | 언제 |
|---|---|---|
| 0 | INVEP | NS가 SG 아닌 곳 / NSC 아닌 곳으로 S에 들어가려 함 (잘못된 entry point) |
| 1 | INVIS | 예외 복귀 때 Secure 스택의 integrity signature가 틀림 (스택 조작) |
| 2 | INVER | 잘못된 예외 복귀 (EXC_RETURN의 보안 비트가 실제와 다름) |
| 3 | AUVIOL | NS가 S 주소에 데이터 접근 (attribution unit violation) |
| 4 | INVTRAN | S 코드가 NS 주소로 일반 분기 (BXNS/BLXNS 없이) |
| 5 | LSPERR | lazy FP 상태 저장 중 SAU/IDAU 위반 |
| 6 | SFARVALID | SFAR에 유효한 주소가 있음 |
| 7 | LSERR | lazy state 활성/비활성 오류 |

### 6.2 예제 5 — 8가지 잘못(+ 정상 호출 1)을 차례로 저지르기

무엇을 확인하나: Secure 쪽 1가지 + NS 쪽 8가지 실험(그중 N5는 비교용 정상 호출). 각 fault를 Secure 핸들러가 받아 SFSR/CFSR을 찍고, 미리 등록된 "재개 지점"으로 돌려보내 다음 실험을 이어 간다.

재개 방법은 예외 프레임 조작이다(B2). 핸들러는 EXC_RETURN의 S 비트(bit 6)로 프레임이 Secure 스택에 있는지 NS 스택에 있는지 보고, `mrs ..., msp_ns`로 NS 스택 포인터를 읽어 프레임의 PC(6번째 워드)를 재개 지점으로 바꾼다.

```c
/* sfault.c — Secure fault 처리: SecureFault/BusFault/HardFault를 받아 원인을 찍고, 등록된 재개 지점으로 돌려보낸다 */
#include "tz.h"
uint32_t g_resume_ns, g_resume_s;                /* 0이면 종료 */
static void decode_sfsr(uint32_t v)
{
    static const char *n[] = { "INVEP", "INVIS", "INVER", "AUVIOL", "INVTRAN", "LSPERR", "SFARVALID", "LSERR" };
    for (int i = 0; i < 8; i++) if (v & (1u << i)) semi_printf(" %s", n[i]);
}
void fault_c(uint32_t exc_return, uint32_t *sp_s)
{
    uint32_t ipsr, *f; __asm volatile("mrs %0, ipsr" : "=r"(ipsr));
    if (exc_return & (1u << 6)) {                 /* S=1: 프레임이 Secure 스택에 */
        if (exc_return & (1u << 2)) __asm volatile("mrs %0, psp" : "=r"(f)); else f = sp_s;
    } else {                                      /* S=0: 프레임이 NS 스택에 */
        if (exc_return & (1u << 2)) __asm volatile("mrs %0, psp_ns" : "=r"(f)); else __asm volatile("mrs %0, msp_ns" : "=r"(f));
    }
    uint32_t sfsr = SCB_SFSR, cfsr = REG32(0xE000ED28u);
    semi_printf("  -> [S handler] exc=%u EXC_RETURN=0x%08x frame@0x%08x PC=0x%08x", ipsr, exc_return, (uint32_t)f, f[6]);
    if (sfsr) { semi_printf(" SFSR=0x%x", sfsr); decode_sfsr(sfsr); if (sfsr & 0x40) semi_printf(" SFAR=0x%08x", SCB_SFAR); }
    if (cfsr) semi_printf(" CFSR=0x%x BFAR=0x%08x", cfsr, REG32(0xE000ED38u));
    semi_printf("\n");
    SCB_SFSR = sfsr; REG32(0xE000ED28u) = cfsr;   /* write-1-to-clear */
    uint32_t r = (exc_return & (1u << 6)) ? g_resume_s : g_resume_ns;
    if (!r) semi_exit(255);
    f[6] = r & ~1u; f[7] = 0x01000000u;           /* 재개 지점, xPSR.T=1 */
}
__attribute__((naked)) static void fault_entry(void)
{
    __asm volatile("mov r0, lr\n mov r1, sp\n push {r0, lr}\n bl fault_c\n pop {r0, pc}");
}
void SecureFault_Handler(void) __attribute__((alias("fault_entry")));
void BusFault_Handler(void) __attribute__((alias("fault_entry")));
void HardFault_Handler(void) __attribute__((alias("fault_entry")));
```

```c
/* 예제 5 Secure 쪽: 잘못된 상태 전환 1건을 직접 일으키고, NS에 실험용 진입 함수를 준다 */
#include "tz.h"
#define ENTRY __attribute__((cmse_nonsecure_entry, used))
extern uint32_t g_resume_ns, g_resume_s;
volatile uint32_t secret = 0x5EC0DE01u;              /* Secure RAM 0x38000000 */
extern void __acle_se_s_hello(void);                 /* 진입 함수의 몸통 (veneer가 b.w로 가는 곳) */
ENTRY uint32_t s_hello(void) { return 7; }
ENTRY void s_set_resume(uint32_t a) { g_resume_ns = a; }
ENTRY uint32_t s_body_addr(void) { return (uint32_t)&__acle_se_s_hello; }
ENTRY uint32_t s_secret_addr(void) { return (uint32_t)&secret; }
ENTRY void s_ppc_open_scc(void) { REG32(0x50080088u) |= 1u; }   /* APBNSPPCEXP2 bit0 = SCC 포트를 NS로 */
ENTRY void s_nsccfg(uint32_t v) { SPC_NSCCFG = v; }
static void continue_boot(void)
{
    semi_printf("[S] resumed after INVTRAN; booting NS\n");
    SCB_SHCSR |= (1u << 17);                          /* BUSFAULTENA (Secure 뱅크) */
    sau_region(4, 0x28000000u, 0x281FFFFFu, 0);       /* 일부러 잘못: SSRAM1(secret이 사는 물리 메모리)의 NS 별칭을 SAU NS로 */
    tz_boot_ns(0x00200000u);
}
int main(void)
{
    tz_setup(1);
    g_resume_s = (uint32_t)&continue_boot;
    semi_printf("[S] SCC ID via Secure alias 0x50300FFC = 0x%08x\n", REG32(0x50300FFCu));
    semi_printf("[S] S1: Secure code calls an NS address with a plain BLX\n");
    void (*plain)(void) = (void (*)(void))0x00200001u;
    plain();                                          /* BLXNS가 아니다 → INVTRAN */
    return 1;
}
```

```c
/* 예제 5 NS 쪽: 금지된 접근을 하나씩 해 보고, Secure 핸들러가 다음 단계(run)로 돌려보낸다 */
#include "semi.h"
uint32_t s_hello(void); void s_set_resume(uint32_t); uint32_t s_body_addr(void);
uint32_t s_secret_addr(void); void s_ppc_open_scc(void); void s_nsccfg(uint32_t);
static volatile int step;
static uint32_t sec, body, alias;                   /* veneer는 main에서 한 번만 부른다 (N8 뒤엔 못 부름) */
static void run(void)
{
    for (;;) {
        switch (step++) {
        case 0: semi_printf("[NS] N1 read Secure RAM 0x%08x\n", sec); semi_printf("     value=0x%x\n", REG32(sec)); break;
        case 1: semi_printf("[NS] N2 write Secure RAM 0x%08x\n", sec); REG32(sec) = 0; break;
        case 2: semi_printf("[NS] N3 call Secure body 0x%08x (no SG there)\n", body); ((uint32_t (*)(void))(body | 1))(); break;
        case 3: semi_printf("[NS] N4 call veneer+4 0x%08x (skips SG)\n", (uint32_t)&s_hello + 3); ((uint32_t (*)(void))((uint32_t)&s_hello + 4))(); break;
        case 4: semi_printf("[NS] N5 call veneer properly: s_hello()=%u\n", s_hello()); break;
        case 5: semi_printf("[NS] N6 read NS alias 0x%08x of the same physical word (SAU NS, MPC Secure)\n", alias);
                semi_printf("     value=0x%x\n", REG32(alias)); break;
        case 6: semi_printf("[NS] N7 read SCC ID 0x40300FFC, PPC port Secure-only: 0x%08x\n", REG32(0x40300FFCu));
                s_ppc_open_scc();
                semi_printf("[NS]    after Secure opens the PPC port:          0x%08x\n", REG32(0x40300FFCu)); break;
        case 7: s_nsccfg(0); semi_printf("[NS] N8 NSCCFG.CODENSC=0, then call veneer s_hello\n"); s_hello(); break;
        default: semi_printf("[NS] done\n"); semi_exit(0);
        }
    }
}
int main(void)
{
    sec = s_secret_addr(); body = s_body_addr(); alias = 0x28000000u + (sec - 0x38000000u);
    s_set_resume((uint32_t)&run); run(); return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'D=/Users/donh/.rtos_scratch/F6/ex5; C=/Users/donh/.rtos_scratch/F6/common;
  S="$D/s_main.c $C/sfault.c" NS=$D/ns_main.c RUN=1 /Users/donh/.rtos_scratch/F6/mk.sh ex5'
```

```text
[S] SCC ID via Secure alias 0x50300FFC = 0x41045050
[S] S1: Secure code calls an NS address with a plain BLX
  -> [S handler] exc=7 EXC_RETURN=0xfffffff9 frame@0x381fffb0 PC=0x00200000 SFSR=0x10 INVTRAN
[S] resumed after INVTRAN; booting NS
[NS] N1 read Secure RAM 0x38000000
  -> [S handler] exc=7 EXC_RETURN=0xffffffb9 frame@0x283fffc8 PC=0x0020026a SFSR=0x8 AUVIOL
[NS] N2 write Secure RAM 0x38000000
  -> [S handler] exc=7 EXC_RETURN=0xffffffb9 frame@0x283fffc0 PC=0x0020025a SFSR=0x8 AUVIOL
[NS] N3 call Secure body 0x1000026d (no SG there)
  -> [S handler] exc=7 EXC_RETURN=0xffffffb9 frame@0x283fffb8 PC=0x1000026c SFSR=0x1 INVEP
[NS] N4 call veneer+4 0x10100004 (skips SG)
  -> [S handler] exc=7 EXC_RETURN=0xffffffb9 frame@0x283fffb0 PC=0x10100004 SFSR=0x1 INVEP
[NS] N5 call veneer properly: s_hello()=7
[NS] N6 read NS alias 0x28000000 of the same physical word (SAU NS, MPC Secure)
  -> [S handler] exc=5 EXC_RETURN=0xffffffb9 frame@0x283fffa8 PC=0x00200218 CFSR=0x8200 BFAR=0x28000000
[NS] N7 read SCC ID 0x40300FFC, PPC port Secure-only: 0x00000000
[NS]    after Secure opens the PPC port:          0x41045050
[NS] N8 NSCCFG.CODENSC=0, then call veneer s_hello
  -> [S handler] exc=7 EXC_RETURN=0xffffffb9 frame@0x283fffa0 PC=0x10100000 SFSR=0x1 INVEP
[NS] done
[qemu rc=0]
```

출력에서 볼 것 — 실험별로 표에 모은다.

| 실험 | 한 일 | 결과 | 막은 곳 |
|---|---|---|---|
| S1 | Secure가 NS 주소로 일반 `BLX` | SecureFault INVTRAN, PC = 0x0020_0000 (첫 fetch에서) | 코어 |
| N1 | NS가 Secure RAM 읽기 | SecureFault AUVIOL, `value=` 줄이 안 찍힘 | 코어 (SAU) |
| N2 | NS가 Secure RAM 쓰기 | SecureFault AUVIOL | 코어 (SAU) |
| N3 | NS가 Secure 함수 몸통으로 직접 호출 | SecureFault INVEP, PC = 몸통 주소 | 코어 (S 영역 진입) |
| N4 | NS가 veneer의 두 번째 명령(`b.w`)으로 호출 | SecureFault INVEP, PC = 0x1010_0004 | 코어 (NSC지만 SG 아님) |
| N5 | 정상 veneer 호출 | 7 | — |
| N6 | SAU를 일부러 잘못 열어 둔 NS 별칭으로 Secure 데이터 읽기 | **BusFault**(CFSR 0x8200 = PRECISERR + BFARVALID), BFAR = 0x2800_0000 | 버스 (MPC) |
| N7 | PPC가 Secure 전용인 SCC를 NS가 읽기 | 오류 없이 **0** (RAZ/WI), 포트를 연 뒤에는 진짜 ID `0x41045050` | 버스 (PPC) |
| N8 | `NSCCFG.CODENSC=0`으로 만든 뒤 정상 veneer 호출 | SecureFault INVEP, PC = 0x1010_0000 | 코어 (IDAU S가 SAU NSC를 덮음) |

이 표에서 기억할 것:

- **N6이 심층 방어의 증거다.** Secure 부트 코드가 SAU를 실수로 넓게 열어도(SAU 영역 4), 물리 메모리 앞의 MPC가 Secure 블록을 지켰다. 그래서 SAU와 MPC 설정은 같은 표(메모리 맵 한 장)에서 만들어야 한다. 둘이 어긋나면 "SAU는 열렸는데 버스에서 막힘"(N6) 또는 그 반대가 된다.
- **N7은 디버깅 함정이다.** PPC가 막은 주변장치는 기본 설정에서 fault 없이 0을 돌려준다. NS 드라이버 입장에서는 "레지스터가 0만 읽힌다, 하드웨어가 죽었나?"로 보인다. Secure 부트에서 PPC를 여는 것을 잊었을 때 생기는 증상이다.
- **N8은 §1.2 표의 굵은 칸을 실제로 본 것이다.** veneer, SAU NSC 영역 둘 다 그대로인데, IDAU 쪽 NSC 허용 비트 하나 때문에 정문이 닫힌다.
- EXC_RETURN 값 두 가지: Secure 상태에서 난 fault는 `0xFFFFFFF9`(S=1, Secure 예외, MSP), NS 상태에서 난 fault는 `0xFFFFFFB9`(bit 6 S=0 → 프레임은 NS 스택, bit 0 ES=1 → Secure 예외로 처리). 프레임 주소도 각각 Secure RAM(`0x381f…`)과 NS RAM(`0x283f…`)이다. B2에서 "보안 쪽 이야기는 F6"이라고 미뤄 둔 비트들이 이것이다(§7.3 표).

### 6.3 QEMU와 실칩의 차이 (정직하게)

이 실험에서 QEMU 10.2.1이 실칩과 다르게 보일 수 있는 점 세 가지를 찾았다.

1. **SFAR**: N1/N2(AUVIOL)에서 SFSR에 SFARVALID가 켜지지 않았다. QEMU `target/arm/tcg/m_helper.c`를 보면 CPU 데이터 접근의 AUVIOL 경로는 `AUVIOL_MASK`만 세운다(스태킹 중 위반 경로는 `SFARVALID`도 세운다). Armv8-M 규격상 데이터 접근 AUVIOL은 SFAR에 주소를 남기는 것이 정상이므로, 실칩에서는 SFAR로 범인 주소를 볼 수 있다.
2. **MPC 차단 응답**: N6은 MPC `CTRL.SEC_RESP=0`(RAZ/WI 설정) 상태였는데 BusFault가 났다. QEMU `hw/misc/tz-mpc.c` 코드의 의도는 SEC_RESP=0이면 RAZ/WI인데, `-trace "tz_mpc_*"`로 보면 `translate ... NS: block`까지만 찍히고 차단 핸들러(`tz_mpc_mem_blocked_read`)는 불리지 않았다. 실칩에서는 SEC_RESP 설정대로 동작한다고 보고, 이 노트의 N6 결과는 "막혔다"는 사실만 믿는다.
3. **PPC 응답**: N7과 같은 차단을 `SECRESPCFG=1`(bus error)로 바꿔 시험했을 때도 QEMU의 확장 PPC는 0을 돌려줬다(실험 중간 버전, `tz_ppc_read_blocked` trace는 찍힘). 실칩은 bus error가 나야 한다.

교훈: **QEMU는 "막히는가"는 잘 보여 주지만 "어떻게 응답하는가"는 실칩 문서(칩 TRM의 MPC/PPC 장)와 실보드로 확인한다.**

---

## 7. 뱅크 레지스터와 인터럽트 — 둘로 나뉜 것, 하나뿐인 것

### 7.1 무엇이 두 벌인가

보안 확장이 있는 Armv8-M 코어에서 많은 레지스터는 **S용과 NS용 두 벌(banked)** 이다. 같은 이름 · 같은 주소로 접근해도 현재 상태에 따라 다른 사본이 보인다. Secure 코드는 `_ns` 이름(`msp_ns`, `control_ns`)이나 SCS의 NS 별칭(`0xE002_xxxx`)으로 반대편 사본을 만질 수 있다. NS는 Secure 사본을 볼 방법이 없다.

| 구분 | 레지스터 | 의미 |
|---|---|---|
| 두 벌 (S/NS 각각) | MSP, PSP, MSPLIM, PSPLIM | 스택 4개 + 한계 4개 (B7의 스택 한계도 세계별) |
| 두 벌 | CONTROL(nPRIV, SPSEL 등), PRIMASK, FAULTMASK, BASEPRI | 각 세계가 자기 마스킹을 한다 |
| 두 벌 | VTOR, SysTick, MPU 전체 | 벡터 테이블 · tick 타이머 · MPU가 세계마다 하나씩 |
| 두 벌 | SHCSR 일부, CFSR의 MemManage · UsageFault 부분, CCR 일부 | MemManage · UsageFault · SVCall · PendSV는 세계별 예외 |
| 하나 (Secure만 설정) | SAU 전체, NVIC_ITNS, AIRCR.PRIS · BFHFNMINS · SYSRESETREQS, NSACR, SFSR · SFAR | 세계의 경계를 정하는 레지스터 |
| 하나 (공유, 보이는 것만 다름) | NVIC ISER/ISPR/IPR | 인터럽트는 하나뿐, ITNS에 따라 NS에게 보이는 비트가 정해진다 |
| 하나 (공유) | R0~R12, LR, PC, xPSR | 범용 레지스터는 공유 → 경계에서 지워야 하는 이유(§4) |

RTOS 관점의 결론: **NS에서 도는 RTOS는 자기 SysTick_NS, PendSV_NS, SVCall_NS, MPU_NS, BASEPRI_NS를 쓴다.** Secure 쪽은 같은 시간에 자기 SysTick_S를 따로 돌릴 수도 있다. 예제 8의 측정은 NS 코드가 NS SysTick(`0xE000E010`을 NS에서 접근)을 독차지해서 했다.

### 7.2 인터럽트 대상 — ITNS

NVIC의 외부 인터럽트는 하나뿐이다. 각 인터럽트를 어느 세계가 처리할지는 Secure가 `NVIC_ITNSn`(Interrupt Target Non-secure, `0xE000_E380`부터)의 비트로 정한다. 1이면 NS 벡터 테이블(VTOR_NS)의 핸들러로, 0이면 Secure 벡터 테이블로 간다. NS 코드는 ITNS=1인 인터럽트만 자기 NVIC 뷰에서 켜고, 펜딩하고, 우선순위를 줄 수 있다.

예외 쪽은 이렇다. SysTick · PendSV · SVCall · MemManage · UsageFault는 세계마다 따로 있다. BusFault · HardFault · NMI의 대상은 **`AIRCR.BFHFNMINS`** 한 비트로 정한다. 0(기본)이면 Secure가 받는다. 그래서 예제 5의 N6에서 NS 코드가 일으킨 BusFault를 Secure 핸들러가 받았다. 1로 하면 NS가 받지만 "Secure가 NMI · HardFault를 잃는" 설정이라 TF-M 같은 SPE는 0으로 둔다.

### 7.3 우선순위 — AIRCR.PRIS

S와 NS의 인터럽트가 우선순위를 다툴 때, NS가 Secure 처리를 마음대로 선점하면 곤란하다. `AIRCR.PRIS`(Prioritize Secure)를 1로 하면 **NS 예외의 우선순위 값 p가 `(p >> 1) + 0x80`으로 바뀌어** NS 예외 전체가 우선순위 공간의 아래 절반으로 밀려난다. NS의 가장 높은 우선순위 0x00도 실제로는 0x80이 된다. FreeRTOS의 ARM_CM33 포트는 스케줄러를 시작할 때 Secure 함수 `SecureInit_DePrioritizeNSExceptions()`를 불러 PRIS를 켠다(§9).

### 7.4 예제 7 — ITNS로 나누고, PRIS로 순서를 바꾸고, 주변장치를 넘겨준다

무엇을 확인하나: (1) NS가 자기 인터럽트를 펜딩하면 NS 핸들러로 간다, (2) NS는 Secure 대상 인터럽트를 건드리지 못한다, (3) Secure IRQ 처리 중 높은 우선순위 NS IRQ가 생기면 PRIS에 따라 선점 여부가 바뀐다, (4) NS에 넘겨준 타이머의 인터럽트를 ITNS로 Secure로 되돌리면 Secure가 받는다.

```c
/* 예제 7 Secure 쪽: 인터럽트 대상(ITNS) 나누기, AIRCR.PRIS, NS 소유 타이머 */
#include "tz.h"
#define ENTRY __attribute__((cmse_nonsecure_entry, used))
#define ISER0 REG32(0xE000E100u)
#define ISPR0 REG32(0xE000E200u)
#define IPR(n) (*(volatile uint8_t *)(0xE000E400u + (n)))
ENTRY void s_pend(uint32_t n) { ISPR0 = 1u << n; __asm volatile("dsb\n isb"); }
ENTRY void s_itns(uint32_t n, uint32_t ns) { if (ns) NVIC_ITNS(n) |= 1u << n; else NVIC_ITNS(n) &= ~(1u << n); }
ENTRY void s_pris(uint32_t on)
{
    SCB_AIRCR = (0x05FAu << 16) | (SCB_AIRCR & 0x0000FFFFu & ~(1u << 14)) | (on ? (1u << 14) : 0);
}
void IRQ10_Handler(void)                             /* Secure 대상, 우선순위 0x40 */
{
    uint32_t lr; __asm volatile("mov %0, lr" : "=r"(lr));
    semi_printf("   [S  IRQ10] enter (EXC_RETURN=0x%08x), pend NS IRQ11 (NS prio 0x00)\n", lr);
    ISPR0 = 1u << 11; __asm volatile("dsb\n isb");
    semi_printf("   [S  IRQ10] exit\n");
}
void IRQ3_Handler(void)
{
    REG32(0x4000000Cu) = 1;                          /* TIMER0 INTCLEAR — NS에 준 포트라 NS 별칭으로 */
    REG32(0x40000000u) = 0;                          /* (Secure 별칭 0x5000000C로 쓰면 PPC가 막는다) */
    semi_printf("   [S  IRQ3 ] TIMER0 interrupt arrived on the Secure side\n");
}
int main(void)
{
    tz_setup(1);
    REG32(0x50080070u) |= 1u;                        /* APBNSPPC0 bit0: TIMER0을 NS에 열어 준다 */
    NVIC_ITNS(0) = (1u << 11) | (1u << 3);           /* IRQ11, IRQ3 → NS.  IRQ10 → Secure */
    IPR(10) = 0x40; ISER0 = (1u << 10) | (1u << 11) | (1u << 3);
    semi_printf("[S] ITNS0=0x%08x  ISER0(S view)=0x%08x\n", NVIC_ITNS(0), ISER0);
    tz_boot_ns(0x00200000u);
    return 1;
}
```

```c
/* 예제 7 NS 쪽 */
#include "semi.h"
void s_pend(uint32_t); void s_itns(uint32_t, uint32_t); void s_pris(uint32_t);
#define ISER0 REG32(0xE000E100u)
#define ISPR0 REG32(0xE000E200u)
#define IPR(n) (*(volatile uint8_t *)(0xE000E400u + (n)))
static volatile uint32_t t0_hits;
void IRQ11_Handler(void)
{
    uint32_t lr; __asm volatile("mov %0, lr" : "=r"(lr));
    semi_printf("   [NS IRQ11] handled (EXC_RETURN=0x%08x)\n", lr);
}
void IRQ3_Handler(void) { REG32(0x4000000Cu) = 1; REG32(0x40000000u) = 0; t0_hits++; }
int main(void)
{
    IPR(11) = 0x00;                                  /* NS 쪽에서 가장 높은 우선순위 */
    semi_printf("[NS] ISER0 (NS view)=0x%08x  IPR(10) read from NS=0x%x\n", ISER0, IPR(10));
    semi_printf("[NS] 1) NS pends IRQ11 itself\n");           ISPR0 = 1u << 11; __asm volatile("isb");
    semi_printf("[NS] 2) NS tries to pend IRQ10 (Secure)\n"); ISPR0 = 1u << 10; __asm volatile("isb");
    semi_printf("[NS]    nothing happened\n");
    semi_printf("[NS] 3) PRIS=0: Secure IRQ10 pends NS IRQ11\n"); s_pris(0); s_pend(10);
    semi_printf("[NS] 4) PRIS=1: same again\n");               s_pris(1); s_pend(10);
    semi_printf("[NS] 5) NS programs TIMER0 (0x40000000), ITNS3=1\n");
    REG32(0x40000004u) = 1000; REG32(0x40000000u) = 0x9;    /* VALUE, CTRL = EN | IRQEN */
    while (t0_hits == 0) { }
    semi_printf("[NS]    TIMER0 interrupt handled on NS side (hits=%u)\n", t0_hits);
    semi_printf("[NS] 6) Secure retargets IRQ3 to Secure (ITNS3=0), NS starts TIMER0 again\n");
    s_itns(3, 0);
    REG32(0x40000004u) = 1000; REG32(0x40000000u) = 0x9;
    for (volatile int i = 0; i < 20000; i++) { }
    semi_printf("[NS]    NS hits still %u\n", t0_hits);
    return 0;
}
```

```text
[S] ITNS0=0x00000808  ISER0(S view)=0x00000c08
[NS] ISER0 (NS view)=0x00000808  IPR(10) read from NS=0x0
[NS] 1) NS pends IRQ11 itself
   [NS IRQ11] handled (EXC_RETURN=0xffffffb8)
[NS] 2) NS tries to pend IRQ10 (Secure)
[NS]    nothing happened
[NS] 3) PRIS=0: Secure IRQ10 pends NS IRQ11
   [S  IRQ10] enter (EXC_RETURN=0xfffffff9), pend NS IRQ11 (NS prio 0x00)
   [NS IRQ11] handled (EXC_RETURN=0xfffffff0)
   [S  IRQ10] exit
[NS] 4) PRIS=1: same again
   [S  IRQ10] enter (EXC_RETURN=0xfffffff9), pend NS IRQ11 (NS prio 0x00)
   [S  IRQ10] exit
   [NS IRQ11] handled (EXC_RETURN=0xfffffff8)
[NS] 5) NS programs TIMER0 (0x40000000), ITNS3=1
[NS]    TIMER0 interrupt handled on NS side (hits=1)
[NS] 6) Secure retargets IRQ3 to Secure (ITNS3=0), NS starts TIMER0 again
   [S  IRQ3 ] TIMER0 interrupt arrived on the Secure side
[NS]    NS hits still 1
[qemu rc=0]
```

출력에서 볼 것:

- **같은 ISER0 레지스터가 S에서는 `0xc08`(IRQ3, 10, 11), NS에서는 `0x808`(IRQ3, 11)로 보인다.** NS는 Secure 대상 IRQ10의 존재조차 모르고, IPR(10)도 0으로 읽힌다. 2)에서 NS가 IRQ10을 펜딩하려 한 쓰기는 조용히 무시됐다.
- 3) PRIS=0: Secure IRQ10(우선순위 0x40) 처리 중에 NS IRQ11(0x00)이 펜딩되자 **즉시 선점**했다. NS 코드가 Secure 핸들러를 끊은 것이다.
- 4) PRIS=1: NS 0x00이 실효 0x80이 되어 0x40인 Secure 핸들러를 못 끊고, Secure가 끝난 뒤에 실행됐다.
- 5) · 6): NS 드라이버가 프로그램한 타이머라도, 인터럽트를 어디로 보낼지는 Secure가 ITNS로 정한다. 실무에서 "NS 드라이버의 인터럽트가 안 들어온다"의 첫 번째 확인 항목이 ITNS다.
- 처음에 Secure IRQ3 핸들러에서 타이머를 Secure 별칭(`0x5000_000C`)으로 지우려다 인터럽트가 영원히 반복됐다. 포트를 NS로 넘긴 순간(`APBNSPPC0` bit 0 = 1) PPC는 **NS 트랜잭션만** 통과시키므로 Secure 별칭 쓰기가 막힌 것이다. Secure 코드는 NS 별칭 주소로 접근하면 NS 트랜잭션을 낼 수 있어서 위 코드처럼 고쳤다. "주변장치는 한 번에 한 세계의 것"이라는 규칙을 몸으로 배운 실수다.

EXC_RETURN 값을 B2의 비트 정의로 풀면 이렇다.

| 값 | 상황 | S(6) | DCRS(5) | Mode(3) | SPSEL(2) | ES(0) |
|---|---|---|---|---|---|---|
| `0xFFFFFFF9` | Secure IRQ가 Secure thread를 끊음 | 1 | 1 | 1 | 0 | 1 |
| `0xFFFFFFB9` | Secure fault가 NS thread를 끊음 | 0 | 1 | 1 | 0 | 1 |
| `0xFFFFFFB8` | NS IRQ가 NS thread를 끊음 | 0 | 1 | 1 | 0 | 0 |
| `0xFFFFFFF0` | NS IRQ가 **Secure 핸들러**를 끊음 (PRIS=0) | 1 | 1 | 0 | 0 | 0 |
| `0xFFFFFFF8` | NS IRQ가 Secure thread(진입 함수 `s_pend` 안)로 돌아감 | 1 | 1 | 1 | 0 | 0 |
| `0xFFFFFFBC` | FreeRTOS NS 태스크의 첫 EXC_RETURN (§9) | 0 | 1 | 1 | 1 | 0 |

`0xFFFFFFF0`과 `0xFFFFFFF8`이 흥미롭다. NS 핸들러가 Secure 코드를 끊으면, 하드웨어는 Secure의 **callee-saved 레지스터(r4~r11)까지 Secure 스택에 넣고 지운 뒤**(그래서 DCRS 규칙이 의미를 가진다) NS 핸들러를 실행한다. NS 핸들러는 Secure 레지스터 값을 하나도 볼 수 없다. 이 추가 프레임에는 integrity signature(정해진 마법 값)가 들어가고, 복귀 때 그 값이 틀리면 SecureFault INVIS다.

```svg
<svg viewBox="0 0 660 260" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="22" font-size="13">PRIS=0 (예제 7의 3)</text>
<text x="20" y="56" font-size="12">S IRQ10 (0x40)</text>
<rect x="140" y="42" width="90" height="20" fill="#3f9a6b" fill-opacity="0.6"/>
<rect x="330" y="42" width="80" height="20" fill="#3f9a6b" fill-opacity="0.6"/>
<text x="20" y="88" font-size="12">NS IRQ11 (0x00)</text>
<rect x="230" y="74" width="100" height="20" fill="#4a7bd0" fill-opacity="0.6"/>
<text x="280" y="118" font-size="12" text-anchor="middle">NS가 Secure 핸들러를 선점</text>
<text x="20" y="152" font-size="13">PRIS=1 (예제 7의 4) — NS 0x00이 실효 0x80</text>
<text x="20" y="186" font-size="12">S IRQ10 (0x40)</text>
<rect x="140" y="172" width="170" height="20" fill="#3f9a6b" fill-opacity="0.6"/>
<text x="20" y="218" font-size="12">NS IRQ11 (0x00)</text>
<rect x="310" y="204" width="100" height="20" fill="#4a7bd0" fill-opacity="0.6"/>
<text x="360" y="248" font-size="12" text-anchor="middle">Secure가 끝난 뒤에야 실행</text>
<line x1="230" y1="34" x2="230" y2="100" stroke="#d0564a" stroke-dasharray="3 3"/>
<text x="236" y="34" font-size="12">IRQ11 pend</text>
<line x1="230" y1="164" x2="230" y2="230" stroke="#d0564a" stroke-dasharray="3 3"/>
<text x="236" y="164" font-size="12">IRQ11 pend</text>
<rect x="440" y="40" width="210" height="80" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="545" y="62" font-size="12" text-anchor="middle">PRIS=1이면 NS 우선순위</text>
<text x="545" y="80" font-size="12" text-anchor="middle">p → (p &gt;&gt; 1) + 0x80</text>
<text x="545" y="98" font-size="12" text-anchor="middle">0x00 → 0x80, 0xFF → 0xFF</text>
</svg>
```

그림 5 — 예제 7의 3)과 4)를 시간축으로 그린 것(길이는 개념적, 순서만 실측). PRIS 한 비트로 "NS가 Secure 처리를 끊을 수 있는가"가 바뀐다.

---

## 8. 비용 — 경계를 넘는 데 얼마나 드나

### 8.1 예제 8 — 호출 1000번씩 재기

무엇을 확인하나: NS 안의 일반 호출, NS → S veneer 호출(인자 2개, 4개), S → NS 콜백 왕복을 각각 1000번 하는 데 걸린 시간. NS SysTick(20 MHz, 1 tick = 50 ns)으로 잰다.

```c
/* 예제 8 Secure 쪽: 비용 측정용 진입 함수 3개 */
#include "tz.h"
#define ENTRY __attribute__((cmse_nonsecure_entry, used))
typedef int32_t __attribute__((cmse_nonsecure_call)) ns_cb_t(int32_t);
ENTRY int32_t s_add(int32_t a, int32_t b) { return a + b; }
ENTRY int32_t s_add4(int32_t a, int32_t b, int32_t c, int32_t d) { return a + b + c + d; }  /* 인자 5개(스택)는 GCC가 거부 */
ENTRY int32_t s_callback(int32_t (*f)(int32_t), int32_t x)                      /* S → NS → S 왕복 */
{
    ns_cb_t *cb = (ns_cb_t *)cmse_nsfptr_create(f);
    if (!cmse_is_nsfptr(cb)) return -1;
    return cb(x) + 1;
}
int main(void) { tz_setup(1); tz_boot_ns(0x00200000u); return 1; }
```

```c
/* 예제 8 NS 쪽: NS SysTick(뱅크된 NS 쪽 SysTick)으로 호출 1000번씩 잰다. ICOUNT: 명령 1개 = 32 ns */
#include "semi.h"
int32_t s_add(int32_t, int32_t); int32_t s_add4(int32_t, int32_t, int32_t, int32_t);
int32_t s_callback(int32_t (*)(int32_t), int32_t);
#define CSR REG32(0xE000E010u)
#define RVR REG32(0xE000E014u)
#define CVR REG32(0xE000E018u)
__attribute__((noinline)) int32_t ns_add(int32_t a, int32_t b) { __asm volatile(""); return a + b; }
__attribute__((noinline)) int32_t ns_inc(int32_t x) { __asm volatile(""); return x + 1; }
volatile int32_t sink;
#define N 1000
#define MEASURE(label, expr) do {                                     \
        CVR = 0; while (CVR == 0) { }                                  \
        uint32_t t0 = CVR;                                             \
        for (int i = 0; i < N; i++) sink = (expr);                     \
        uint32_t t1 = CVR, d = (t0 - t1) & 0xFFFFFF;                   \
        semi_printf("%s: %u ticks / %u calls\n", label, d, N);         \
    } while (0)
int main(void)
{
    RVR = 0xFFFFFF; CVR = 0; CSR = 5;                 /* 프로세서 클럭, 인터럽트 없음 */
    MEASURE("loop only", 0);
    MEASURE("NS->NS call", ns_add(sink, 1));
    MEASURE("NS->S veneer call", s_add(sink, 1));
    MEASURE("NS->S 4 args", s_add4(sink, 1, 2, 3));
    MEASURE("NS->S->NS callback", s_callback(ns_inc, sink));
    return 0;
}
```

```text
loop only: 1922 ticks / 1000 calls
NS->NS call: 5121 ticks / 1000 calls
NS->S veneer call: 9601 ticks / 1000 calls
NS->S 4 args: 12161 ticks / 1000 calls
NS->S->NS callback: 30081 ticks / 1000 calls
[qemu rc=0]
```

출력에서 볼 것: 1 tick = 50 ns, 명령 1개 = 32 ns이므로 tick × 50 / 32 = 명령 수다. 빈 루프 1922 tick = 3003 명령 = 반복당 3개(`subs`, `str`, `bne`) — 디스어셈블리와 정확히 맞는다. 일반 호출은 반복당 8개, veneer 호출은 15개, 콜백 왕복은 47개.

veneer 호출 15개를 세어 보면: 루프 6(`movs`, `ldr`, `bl`, `subs`, `str`, `bne`) + 링커 long-branch 1(`ldr.w pc`) + veneer `b.w` 1 + 몸통 7(§3.2의 `add` ~ `bxns`) = 15. **`sg` 한 개가 빠진다.** QEMU는 SG를 "fetch fault → SG인지 확인 → 실행"하는 예외 경로로 처리해서(`-d int` 로그에 `...really an SG instruction at 0x10100008, executing it`이 찍힌다) icount에 세지 않는 것으로 보인다. 실칩에서 SG · BXNS · BLXNS는 파이프라인을 비우는 분기라 명령 수보다 사이클이 더 든다. 정확한 사이클은 코어 TRM과 실보드 DWT CYCCNT로 재야 한다(QEMU에는 CYCCNT가 없다).

### 8.2 예제 11 — 측정 차트 만들기 (Python)

무엇을 확인하나: 예제 8과 §9 예제 10의 출력 파일을 읽어 명령 수로 바꾸고 SVG 막대 좌표를 계산한다.

```python
# F6 측정 차트: 예제 8(호출 비용) + 예제 10(yield 비용) → SVG 좌표 계산 (발췌: 숫자 변환 부분)
import re
out = "/var/tmp/rtos/F6/out/"
ex8 = dict(re.findall(r"^(.+?): (\d+) ticks / 1000 calls", open(out + "ex8.txt").read(), re.M))
y = [float(re.search(r"-> ([\d.]+) ticks/switch", open(out + f"frtz_y{w}.txt").read()).group(1)) for w in (0, 1)]
NS_PER_TICK, NS_PER_INSN = 50.0, 32.0            # SysTick·TIMER1 = 20 MHz, ICOUNT shift=5
insn = {k: int(v) * NS_PER_TICK / NS_PER_INSN / 1000 for k, v in ex8.items()}
base = insn["loop only"]
calls = [("NS→NS 일반 호출", insn["NS->NS call"] - base), ("NS→S veneer 호출", insn["NS->S veneer call"] - base),
         ("NS→S 인자 4개", insn["NS->S 4 args"] - base), ("NS→S→NS 콜백", insn["NS->S->NS callback"] - base)]
yields = [("yield, Secure context 없음", y[0] * NS_PER_TICK / NS_PER_INSN),
          ("yield, Secure context 있음", y[1] * NS_PER_TICK / NS_PER_INSN)]
for n, v in calls + yields: print(f"{n:28s} {v:7.1f} insns  ({v * NS_PER_INSN / 1000:5.2f} us)")
# ... 이하 막대 좌표 계산: x = 210 + 360 × v / vmax (생략)
```

```sh
limactl shell elinux -- bash -lc 'python3 /Users/donh/.rtos_scratch/F6/py/chart.py'
```

```text
NS→NS 일반 호출                      5.0 insns  ( 0.16 us)
NS→S veneer 호출                  12.0 insns  ( 0.38 us)
NS→S 인자 4개                      16.0 insns  ( 0.51 us)
NS→S→NS 콜백                      44.0 insns  ( 1.41 us)
yield, Secure context 없음        70.4 insns  ( 2.25 us)
yield, Secure context 있음       144.9 insns  ( 4.64 us)
chart.svg written
```

(열 정렬이 어긋난 것은 한글 폭 때문이다. 출력 그대로 옮겼다.)

```svg
<svg viewBox="0 0 640 330" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="22" font-size="13">호출 1회 순비용 (루프 빼고, 명령 수 = QEMU icount)</text>
<line x1="210.0" y1="30" x2="210.0" y2="152" stroke="currentColor" stroke-opacity="0.2"/>
<text x="210.0" y="166" font-size="12" text-anchor="middle">0</text>
<line x1="282.0" y1="30" x2="282.0" y2="152" stroke="currentColor" stroke-opacity="0.2"/>
<text x="282.0" y="166" font-size="12" text-anchor="middle">10</text>
<line x1="354.0" y1="30" x2="354.0" y2="152" stroke="currentColor" stroke-opacity="0.2"/>
<text x="354.0" y="166" font-size="12" text-anchor="middle">20</text>
<line x1="426.0" y1="30" x2="426.0" y2="152" stroke="currentColor" stroke-opacity="0.2"/>
<text x="426.0" y="166" font-size="12" text-anchor="middle">30</text>
<line x1="498.0" y1="30" x2="498.0" y2="152" stroke="currentColor" stroke-opacity="0.2"/>
<text x="498.0" y="166" font-size="12" text-anchor="middle">40</text>
<line x1="570.0" y1="30" x2="570.0" y2="152" stroke="currentColor" stroke-opacity="0.2"/>
<text x="570.0" y="166" font-size="12" text-anchor="middle">50</text>
<text x="202" y="51" font-size="13" text-anchor="end">NS→NS 일반 호출</text>
<rect x="210" y="36" width="36.0" height="20" fill="#888" fill-opacity="0.75"/>
<text x="252.0" y="51" font-size="12">5.0</text>
<text x="202" y="81" font-size="13" text-anchor="end">NS→S veneer 호출</text>
<rect x="210" y="66" width="86.4" height="20" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="302.4" y="81" font-size="12">12.0</text>
<text x="202" y="111" font-size="13" text-anchor="end">NS→S 인자 4개</text>
<rect x="210" y="96" width="115.2" height="20" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="331.2" y="111" font-size="12">16.0</text>
<text x="202" y="141" font-size="13" text-anchor="end">NS→S→NS 콜백</text>
<rect x="210" y="126" width="316.8" height="20" fill="#e08a3c" fill-opacity="0.75"/>
<text x="532.8" y="141" font-size="12">44.0</text>
<text x="20" y="206" font-size="13">FreeRTOS(NS) 태스크 전환 1회 (명령 수, ARM_CM33 TrustZone 포트)</text>
<line x1="210.0" y1="214" x2="210.0" y2="276" stroke="currentColor" stroke-opacity="0.2"/>
<text x="210.0" y="290" font-size="12" text-anchor="middle">0</text>
<line x1="300.0" y1="214" x2="300.0" y2="276" stroke="currentColor" stroke-opacity="0.2"/>
<text x="300.0" y="290" font-size="12" text-anchor="middle">40</text>
<line x1="390.0" y1="214" x2="390.0" y2="276" stroke="currentColor" stroke-opacity="0.2"/>
<text x="390.0" y="290" font-size="12" text-anchor="middle">80</text>
<line x1="480.0" y1="214" x2="480.0" y2="276" stroke="currentColor" stroke-opacity="0.2"/>
<text x="480.0" y="290" font-size="12" text-anchor="middle">120</text>
<line x1="570.0" y1="214" x2="570.0" y2="276" stroke="currentColor" stroke-opacity="0.2"/>
<text x="570.0" y="290" font-size="12" text-anchor="middle">160</text>
<text x="202" y="235" font-size="13" text-anchor="end">yield, Secure context 없음</text>
<rect x="210" y="220" width="158.3" height="20" fill="#888" fill-opacity="0.75"/>
<text x="374.3" y="235" font-size="12">70.4</text>
<text x="202" y="265" font-size="13" text-anchor="end">yield, Secure context 있음</text>
<rect x="210" y="250" width="326.0" height="20" fill="#d0564a" fill-opacity="0.75"/>
<text x="542.0" y="265" font-size="12">144.9</text>
<text x="20" y="318" font-size="12">QEMU mps2-an505 icount 기준(명령 1개 = 32 ns). 실제 칩의 사이클과 절대값은 다르다</text>
</svg>
```

그림 6 — 직접 잰 경계 비용. 위: 호출 1회 순비용(빈 루프 3 명령을 뺌). 아래: FreeRTOS NS 태스크 전환 1회(§9 예제 10). 숫자는 QEMU 명령 수이고 SG가 세어지지 않으므로 실칩보다 작게 나온 쪽이다.

### 8.3 숫자를 읽는 법

- NS → S 호출은 일반 호출보다 **명령 7개 더**(5 → 12). 인자 2개를 더 넘기면 4개 더(인자 준비 `movs` 2개 + 몸통의 덧셈 2개 — 청소 코드는 그대로다). QEMU 시간으로 0.38 µs, 명령당 1사이클로 쳐도 100 MHz M33이면 0.1 µs대다. 1 kHz 제어 루프에서 한 번 부르는 정도는 신경 쓸 일이 아니다.
- S → NS 콜백 왕복은 44개로 비싸다. libgcc 도우미가 r5~r11을 저장 · 청소 · 복원하고 FP lazy 저장(`vlstm`/`vlldm`)을 하기 때문이다. **Secure 서비스 안에서 NS 콜백을 루프로 부르는 설계는 피한다.**
- 진짜 비용은 이것보다 경계 너머의 "작업"이다. TF-M의 `psa_call()`은 veneer 한 번 위에 SPM 디스패치, 파티션 전환(격리 레벨 2/3이면 MPU 재설정), invec/outvec 검사 · 복사가 얹힌다. 이 노트에서 TF-M을 빌드하지 않아 숫자는 없다(확인 못 함). 설계 원칙은 **경계를 자주 넘지 말고, 넘을 때 일을 많이 하라**(배치)다. 드론에서 IMU 샘플마다 Secure 암호 함수를 부르지 말고, 텔레메트리 패킷 단위로 묶어 한 번에 암호화하는 식이다.

---

## 9. RTOS가 NS 쪽에서 도는 구조 — FreeRTOS ARM_CM33 포트

### 9.1 세 가지 배치

TrustZone 칩에서 RTOS를 어디에 둘지는 세 가지다. FreeRTOS `portable/GCC/ARM_CM33/non_secure/port.c` 머리 주석이 정확히 이 세 조합을 적어 둔다.

| 배치 | FreeRTOS 설정 | 포트 디렉터리 | 언제 |
|---|---|---|---|
| RTOS 전체가 Secure에서만 | `configRUN_FREERTOS_SECURE_ONLY=1`, `configENABLE_TRUSTZONE=0` | `ARM_CM33_NTZ` | TrustZone을 쓰지 않는 제품, B7 · C10 · E6의 QEMU 예제 |
| **RTOS는 NS, 태스크가 Secure 함수를 부름** | `configRUN_FREERTOS_SECURE_ONLY=0`, `configENABLE_TRUSTZONE=1` | `ARM_CM33` (`non_secure/` + `secure/`) | TF-M이나 자체 Secure 이미지가 서비스를 줌 — 가장 흔한 TrustZone 제품 구성 |
| RTOS는 NS, Secure 호출 없음 | 둘 다 0 | `ARM_CM33_NTZ` | Secure는 부트만 하고 사라지는 구성 |

두 번째가 이 절의 주제다. 문제는 하나다. **NS 태스크가 Secure 함수 안에 있는 동안 선점되면?** Secure 함수는 Secure 스택(PSP_S)을 쓰고 있다. 다른 NS 태스크로 전환됐는데 그 태스크도 Secure 함수를 부르면, 같은 Secure 스택 위에 쌓여 첫 태스크의 Secure 프레임이 깨진다. 그래서 Secure 스택도 **태스크마다** 있어야 하고, NS의 컨텍스트 스위치가 Secure 스택 포인터도 바꿔 줘야 한다. 그런데 NS 커널은 PSP_S를 직접 만질 수 없다. 해법: NS 커널의 PendSV가 **Secure 함수(veneer)를 불러** Secure 쪽에서 PSP_S/PSPLIM_S를 바꾸게 한다.

```svg
<svg viewBox="0 0 660 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<rect x="10" y="10" width="300" height="310" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="160" y="32" font-size="13" text-anchor="middle">NS: FreeRTOS 커널 (ARM_CM33/non_secure)</text>
<rect x="25" y="46" width="130" height="62" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="90" y="68" font-size="12" text-anchor="middle">태스크 A</text>
<text x="90" y="86" font-size="12" text-anchor="middle">TCB + NS 스택</text>
<text x="90" y="102" font-size="12" text-anchor="middle">xSecureContext=2</text>
<rect x="165" y="46" width="130" height="62" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="230" y="68" font-size="12" text-anchor="middle">태스크 B</text>
<text x="230" y="86" font-size="12" text-anchor="middle">TCB + NS 스택</text>
<text x="230" y="102" font-size="12" text-anchor="middle">xSecureContext=1</text>
<rect x="25" y="130" width="270" height="56" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="160" y="152" font-size="12" text-anchor="middle">PendSV_NS: SaveContext (veneer)</text>
<text x="160" y="172" font-size="12" text-anchor="middle">→ 태스크 전환 → LoadContext</text>
<rect x="25" y="206" width="270" height="56" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="160" y="228" font-size="12" text-anchor="middle">SVC 100: portALLOCATE_SECURE_CONTEXT</text>
<text x="160" y="248" font-size="12" text-anchor="middle">SVC 101: 태스크 삭제 때 해제</text>
<text x="160" y="296" font-size="12" text-anchor="middle">PRIS=1 (스케줄러 시작 때 Secure가 켬)</text>
<rect x="350" y="10" width="300" height="310" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="500" y="32" font-size="13" text-anchor="middle">S: ARM_CM33/secure</text>
<rect x="365" y="46" width="270" height="62" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="500" y="68" font-size="12" text-anchor="middle">xSecureContexts[] (기본 8개)</text>
<text x="500" y="86" font-size="12" text-anchor="middle">PSP · PSPLIM · 스택 시작 · 주인 TCB</text>
<text x="500" y="102" font-size="12" text-anchor="middle">secure_context.c</text>
<rect x="365" y="130" width="130" height="56" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="430" y="152" font-size="12" text-anchor="middle">A의 S 스택</text>
<text x="430" y="172" font-size="12" text-anchor="middle">0x3800_0188~</text>
<rect x="505" y="130" width="130" height="56" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="570" y="152" font-size="12" text-anchor="middle">B의 S 스택</text>
<text x="570" y="172" font-size="12" text-anchor="middle">0x3800_0078~</text>
<rect x="365" y="206" width="270" height="56" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="500" y="228" font-size="12" text-anchor="middle">secure_heap.c (Secure 쪽 pvPortMalloc)</text>
<text x="500" y="248" font-size="12" text-anchor="middle">스택 끝에 seal 0xFEF5EDA5 두 워드</text>
<text x="500" y="296" font-size="12" text-anchor="middle">CONTROL_S.SPSEL=1 → 진입 함수는 PSP_S 사용</text>
<line x1="297" y1="158" x2="362" y2="78" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="297" y1="234" x2="362" y2="234" stroke="currentColor" marker-end="url(#ah4)"/>
</svg>
```

그림 7 — FreeRTOS ARM_CM33(TrustZone) 포트의 구조. NS 커널은 태스크마다 `xSecureContext` 핸들(작은 정수)만 갖고, 실제 Secure 스택은 Secure 쪽 배열과 Secure 힙에 있다. 주소는 예제 9의 실측값.

### 9.2 FreeRTOS 소스에서 확인한 흐름 (V11.3.2)

- `vTaskStartScheduler()` → SVC `portSVC_START_SCHEDULER` → `vPortSVCHandler_C()`가 `SecureInit_DePrioritizeNSExceptions()`(AIRCR.PRIS = 1)와 `SecureContext_Init()`(PSP_S = PSPLIM_S = 0, `CONTROL_S` = 0x02: Secure thread가 PSP 사용)를 부른다. 둘 다 `secureportNON_SECURE_CALLABLE` = `cmse_nonsecure_entry` 함수다.
- 태스크가 `portALLOCATE_SECURE_CONTEXT(size)`를 부르면 `vPortAllocateSecureContext()`가 `svc 100`을 하고, SVC 핸들러가 `SecureContext_AllocateContext(size, pxCurrentTCB)` → `SecureContext_LoadContext()`를 부른다. Secure 쪽은 `pvPortMalloc(size + 8)`(Secure 힙)으로 스택을 만들고 끝에 seal 값 `0xFEF5EDA5`를 두 번 쓴다.
- Secure 함수들은 모두 **IPSR != 0(Handler 모드)일 때만** 일한다. NS 태스크가 thread 모드에서 `SecureContext_LoadContext`를 직접 불러 남의 Secure 스택을 빼앗는 것을 막는 장치다. 소유자 검사(`pvTaskHandle == pxCurrentTCB`)도 한다. `SecureContext_FreeContext`의 주석은 "pvTaskHandle은 NS가 준 값이라 믿을 수 없고, 추가 소유 일치 검사로만 쓴다"고 적어 둔다 — §5의 confused deputy를 커널이 스스로 의식한 흔적이다.
- PendSV(`non_secure/portasm.c`): `xSecureContext`가 0이 아니면 `bl SecureContext_SaveContext` → EXC_RETURN bit 6(S)을 보고(`lsls r3, r3, #25`) 프레임이 **Secure 스택에 있으면 NS 범용 레지스터 저장을 건너뛴다** → `vTaskSwitchContext` → 새 태스크의 `xSecureContext`로 `bl SecureContext_LoadContext`. bit 6이 1이라는 것은 "이 태스크는 Secure 함수 안에서 선점됐다"는 뜻이다. 그때 r4~r11은 하드웨어가 이미 Secure 스택에 넣고 지웠으므로(§7.4의 `0xFFFFFFF0` 설명) NS 커널은 저장할 것이 없다.
- 태스크의 첫 EXC_RETURN은 `portINITIAL_EXC_RETURN = 0xffffffbc`(NS, PSP, thread, 기본 프레임 — §7.4 표 마지막 줄).
- **idle 태스크도 Secure context를 하나 받는다.** `tasks.c`의 `prvIdleTask` 첫 줄이 `portALLOCATE_SECURE_CONTEXT( configMINIMAL_SECURE_STACK_SIZE )`다. 태스크가 자기 자신을 지우면 idle이 그 태스크의 Secure context를 해제해야 하기 때문이다. 그래서 FreeRTOSConfig.h에 `configMINIMAL_SECURE_STACK_SIZE`가 없으면 `tasks.c`가 컴파일되지 않는다(아래 첫 빌드에서 실제로 만난 에러).

### 9.3 빌드 구성

Secure 이미지 = 예제의 `s_main.c`(SAU 설정 + 서비스) + FreeRTOS `secure/` 네 파일. NS 이미지 = FreeRTOS 커널 + `non_secure/port.c` · `portasm.c` + 앱. NS 포트가 부르는 `SecureContext_*`, `SecureInit_*` 진입 함수는 import library로 연결된다.

```sh
#!/bin/bash
# build.sh <name> [NSCFL...] — FreeRTOS V11.3.2 ARM_CM33(TZ) 포트: Secure(secure_*.c) + NS(커널) → an505
set -e
name=$1; shift
K=/var/rtoslab/src/FreeRTOS-Kernel; P=$K/portable/GCC/ARM_CM33; F=/Users/donh/.rtos_scratch/F6/frtz
export S="$F/s_main.c $P/secure/secure_context.c $P/secure/secure_context_port.c $P/secure/secure_heap.c $P/secure/secure_init.c"
export SCFL="-I$F/scfg -I$P/secure -Wno-unused-parameter"   # 포트의 naked 함수 인자 경고만 끈다
export NS="$F/${MAIN:-ns_main.c} $K/tasks.c $K/list.c $K/queue.c $P/non_secure/port.c $P/non_secure/portasm.c $K/portable/MemMang/heap_4.c"
export NSCFL="-I$F -I$K/include -I$P/non_secure -I$P/secure -Wno-unused-parameter $*"
/Users/donh/.rtos_scratch/F6/mk.sh $name
```

`-Wno-unused-parameter`는 FreeRTOS 포트의 naked 어셈블리 함수(`SecureContext_LoadContextAsm(pxSecureContext)`처럼 인자를 r0로만 쓰는 함수)에서 나는 경고 때문이다. 이 노트의 코드에는 경고가 없다.

NS 쪽 FreeRTOSConfig.h에서 TrustZone 관련 줄만 보인다(나머지는 1 kHz tick, heap_4 32 KB 등 평범한 값).

```c
#define configCPU_CLOCK_HZ                      20000000u     /* AN505 SYSCLK */
#define configENABLE_MPU                        0
#define configENABLE_FPU                        0
#define configENABLE_TRUSTZONE                  1      /* 태스크가 Secure 함수를 부를 수 있게 */
#define configMINIMAL_SECURE_STACK_SIZE         128    /* idle 태스크도 Secure context를 하나 받는다 (tasks.c prvIdleTask) */
#define configRUN_FREERTOS_SECURE_ONLY          0
/* Secure 쪽 scfg/FreeRTOSConfig.h: configENABLE_MPU/FPU/MVE 0, secureconfigTOTAL_HEAP_SIZE 4 KB, secureconfigMAX_SECURE_CONTEXTS 4 */
```

`configMINIMAL_SECURE_STACK_SIZE` 없이 처음 빌드했을 때의 에러:

```text
tasks.c: In function 'prvIdleTask':
tasks.c:5920:34: error: 'configMINIMAL_SECURE_STACK_SIZE' undeclared (first use in this function); did you mean 'configMINIMAL_STACK_SIZE'?
 5920 |     portALLOCATE_SECURE_CONTEXT( configMINIMAL_SECURE_STACK_SIZE );
```

(경로는 줄였다.)

### 9.4 예제 9 — 태스크마다 다른 Secure 스택

무엇을 확인하나: 두 NS 태스크가 각자 Secure context를 할당하고 같은 Secure 서비스를 부를 때, Secure 쪽에서 보이는 PSP_S/PSPLIM_S가 태스크마다 다른가.

```c
/* frtz Secure 쪽: FreeRTOS secure context 관리자 + 서비스 하나 + NS 부팅 */
#include "tz.h"
#define ENTRY __attribute__((cmse_nonsecure_entry, used))
static uint32_t calls[4];
ENTRY uint32_t s_work(uint32_t id)                   /* Secure 스택을 실제로 쓰는 서비스 */
{
    volatile uint8_t scratch[64];
    uint32_t psp, psplim, ctrl;
    for (unsigned i = 0; i < sizeof scratch; i++) scratch[i] = (uint8_t)(i ^ id);
    __asm volatile("mrs %0, psp\n mrs %1, psplim\n mrs %2, control" : "=r"(psp), "=r"(psplim), "=r"(ctrl));
    calls[id & 3]++;
    if (calls[id & 3] == 1)
        semi_printf("   [S] s_work(%u): CONTROL_S=0x%x PSP_S=0x%08x PSPLIM_S=0x%08x scratch@0x%08x\n",
                    id, ctrl, psp, psplim, (uint32_t)scratch);
    return calls[id & 3] + scratch[5];
}
int main(void)
{
    tz_setup(1);
    REG32(0x50080070u) |= 2u;                        /* APBNSPPC0 bit1: TIMER1 → NS (측정용) */
    tz_boot_ns(0x00200000u);
    return 1;
}
```

```c
/* frtz NS 쪽: 태스크마다 Secure context를 할당하고 Secure 서비스를 부른다 */
#include "FreeRTOS.h"
#include "task.h"
uint32_t s_work(uint32_t id);
extern volatile uint32_t xSecureContext;             /* port.c: 현재 태스크의 Secure context 핸들 */
static void worker(void *arg)
{
    uint32_t id = (uint32_t)arg;
#ifndef SKIP_ALLOC
    portALLOCATE_SECURE_CONTEXT(256);                 /* Secure 함수를 부르기 전에 반드시 */
#endif
    semi_printf("[NS] task %u: xSecureContext=%u, NS PSP region ~0x%08x\n", id, xSecureContext, (uint32_t)&id);
    for (int i = 0; i < 3; i++) { (void)s_work(id); vTaskDelay(1); }
    semi_printf("[NS] task %u done\n", id);
    if (id == 2) { vTaskDelay(5); semi_exit(0); }
    vTaskDelete(NULL);
}
int main(void)
{
    xTaskCreate(worker, "A", 256, (void *)1, 2, NULL);
    xTaskCreate(worker, "B", 256, (void *)2, 2, NULL);
    vTaskStartScheduler();
    return 1;
}
```

```sh
limactl shell elinux -- bash -lc 'RUN=1 /Users/donh/.rtos_scratch/F6/frtz/build.sh frtz1'
```

```text
[NS] task 2: xSecureContext=1, NS PSP region ~0x2820096c
   [S] s_work(2): CONTROL_S=0x2 PSP_S=0x38000120 PSPLIM_S=0x38000078 scratch@0x38000128
[NS] task 1: xSecureContext=2, NS PSP region ~0x28200514
   [S] s_work(1): CONTROL_S=0x2 PSP_S=0x38000230 PSPLIM_S=0x38000188 scratch@0x38000238
[NS] task 2 done
[NS] task 1 done
[qemu rc=0]
```

출력에서 볼 것:

- 같은 Secure 함수 `s_work` 안에서 본 PSP_S가 태스크 B(id 2)는 `0x3800_0120`, 태스크 A(id 1)는 `0x3800_0230`이다. PSPLIM_S도 각각 `0x3800_0078`, `0x3800_0188` — 정확히 256 + 8(seal) + 힙 헤더만큼 떨어진 두 개의 Secure 힙 블록이다. 각 태스크는 자기 Secure 스택에서 `scratch[64]`를 잡았다.
- `CONTROL_S=0x2` — SPSEL=1. `SecureContext_Init()`이 설정한 값이다. NS 태스크가 Secure 함수를 부르면 Secure thread 모드가 PSP_S를 쓴다.
- 핸들 번호가 B=1, A=2인 것은 같은 우선순위에서 나중에 만든 태스크(B)가 먼저 실행됐기 때문이다(FreeRTOS의 `pxCurrentTCB`가 마지막으로 만든 같은 우선순위 태스크를 가리킨다). idle 태스크는 3번을 받는다.
- Secure 스택 사용량: 스택 꼭대기 = PSPLIM + 256 = `0x3800_0178`, PSP = `0x3800_0120` → 88바이트. `scratch[64]` + 저장 레지스터 몇 개다. `-fstack-usage`(F3)로 Secure 서비스의 최대 깊이를 재서 `portALLOCATE_SECURE_CONTEXT()` 크기를 정한다.

### 9.5 예제 9b — Secure context 없이 Secure 함수를 부르면

무엇을 확인하나: `portALLOCATE_SECURE_CONTEXT()`를 빼먹은 태스크가 Secure 함수를 부르면 무슨 일이 생기나.

```sh
limactl shell elinux -- bash -lc 'RUN=1 /Users/donh/.rtos_scratch/F6/frtz/build.sh frtz_noalloc -DSKIP_ALLOC'
```

```text
[NS] task 2: xSecureContext=0, NS PSP region ~0x2820096c
[S fault] exc=3 EXC_RETURN=0xfffffffd CFSR=0x9200 HFSR=0x40000000 BFAR=0xfffffff4 SFSR=0x0 SFAR=0x0
```

출력에서 볼 것: Secure HardFault(exc 3, HFSR FORCED). EXC_RETURN `0xFFFFFFFD` = Secure thread, PSP 사용 — 즉 **Secure 함수 안에서** 났다. BFAR = `0xFFFF_FFF4`: PSP_S가 `securecontextNO_STACK`(0)인 상태에서 `s_work`의 프롤로그가 push를 했고, 주소가 0 아래로 감겨 `0xFFFF_FFF4`에 쓰려다 bus error(CFSR 0x9200 = BFARVALID + STKERR + PRECISERR)가 났다. PSPLIM_S도 0이라 스택 한계 검사로는 못 잡는다. Secure BusFault를 켜지 않아(이 예제의 Secure 쪽은 `sfault.c`를 링크하지 않았다) HardFault로 올라갔고, 기본 핸들러가 레지스터를 찍고 QEMU를 끝냈다(`[qemu rc]` 줄은 `set -e` 때문에 안 찍혔다).

주의할 점: 스택을 전혀 안 쓰는 아주 작은 진입 함수(예: 예제 2의 `s_add`)는 context 없이도 "우연히" 동작한다. 그래서 이 버그는 Secure 서비스가 커지는 날 갑자기 드러난다. **NS 태스크 함수 첫 줄에 `portALLOCATE_SECURE_CONTEXT()`를 두는 것을 코드 리뷰 규칙으로 만든다.**

### 9.6 예제 10 — Secure context가 태스크 전환을 얼마나 비싸게 하나

무엇을 확인하나: 같은 우선순위 두 태스크가 `taskYIELD()`로 1000번씩 번갈아 돌 때 전환 1회 비용. Secure context가 없을 때와 있을 때(두 태스크 모두 할당). NS에 넘겨준 TIMER1(20 MHz 다운카운터)로 잰다.

```c
/* frtz 측정: 같은 우선순위 두 태스크가 taskYIELD로 1000번씩 번갈아 돈다. Secure context 있음/없음 비교 */
#include "FreeRTOS.h"
#include "task.h"
uint32_t s_work(uint32_t id);
#define T1_CTRL  REG32(0x40001000u)
#define T1_VALUE REG32(0x40001004u)
#define T1_RELOAD REG32(0x40001008u)
#define N 1000
static volatile uint32_t ready;
static void pinger(void *arg)
{
    uint32_t id = (uint32_t)arg;
#if WITH_CTX
    portALLOCATE_SECURE_CONTEXT(256);
    (void)s_work(id);                                /* Secure 스택을 한 번 써 둔다 */
#endif
    ready++;
    while (ready < 2) taskYIELD();
    uint32_t t0 = T1_VALUE;
    for (int i = 0; i < N; i++) taskYIELD();
    uint32_t t1 = T1_VALUE;
    if (id == 1) {
        uint32_t d = t0 - t1;                        /* 다운 카운터, 20 MHz */
        semi_printf("WITH_CTX=%u: %u timer ticks for %u switches -> %u.%02u ticks/switch\n",
                    (uint32_t)WITH_CTX, d, 2 * N, d / (2 * N), (d % (2 * N)) * 100 / (2 * N));
        semi_exit(0);
    }
    for (;;) taskYIELD();
}
int main(void)
{
    T1_RELOAD = 0xFFFFFFFFu; T1_VALUE = 0xFFFFFFFFu; T1_CTRL = 1u;
    xTaskCreate(pinger, "A", 256, (void *)1, 2, NULL);
    xTaskCreate(pinger, "B", 256, (void *)2, 2, NULL);
    vTaskStartScheduler();
    return 1;
}
```

```sh
limactl shell elinux -- bash -lc 'for w in 0 1; do MAIN=ns_yield.c RUN=1 /Users/donh/.rtos_scratch/F6/frtz/build.sh frtz_y$w -DWITH_CTX=$w; done'
```

```text
WITH_CTX=0: 90065 timer ticks for 2000 switches -> 45.03 ticks/switch
[qemu rc=0]
   [S] s_work(2): CONTROL_S=0x2 PSP_S=0x38000120 PSPLIM_S=0x38000078 scratch@0x38000128
   [S] s_work(1): CONTROL_S=0x2 PSP_S=0x38000230 PSPLIM_S=0x38000188 scratch@0x38000238
WITH_CTX=1: 185479 timer ticks for 2000 switches -> 92.73 ticks/switch
[qemu rc=0]
```

출력에서 볼 것: 전환 1회가 45.03 tick(2.25 µs, 약 70 명령) → 92.73 tick(4.64 µs, 약 145 명령)으로 **두 배**가 됐다(그림 6 아래 막대). 늘어난 75 명령은 PendSV가 부르는 `SecureContext_SaveContext` + `SecureContext_LoadContext` 두 번의 NS → S 왕복(veneer, IPSR · 핸들 · 소유자 검사, PSP_S/PSPLIM_S 교체)이다. 다른 노트의 an385(M3) 전환 시간과는 보드 · 포트 · 측정 방법이 달라 직접 비교하지 말고, 같은 보드에서 "context 있음/없음"의 비율만 본다.

설계에서의 뜻: Secure 서비스를 쓰지 않는 태스크(예: 드론의 자세 제어 rate 태스크)는 **context를 할당하지 않으면** 전환 비용이 늘지 않는다(`xSecureContext == 0`이면 PendSV가 veneer를 건너뛴다 — `cbz r0, save_ns_context`). Secure 호출은 몇 개의 전용 태스크(암호 · 인증 · 저장)에 몰아 두는 것이 좋다.

### 9.7 Zephyr는 어떻게 하나 — 태스크별 Secure 스택이 없다

Zephyr는 FreeRTOS와 다른 길을 갔다. NS Zephyr 스레드들은 **Secure context를 각자 갖지 않고**, Secure 호출을 **직렬화**한다.

- TF-M을 부르는 모든 호출은 `modules/trusted-firmware-m/interface/interface.c`의 `tfm_ns_interface_dispatch()`를 지난다. 이 함수는 `K_MUTEX_DEFINE(tfm_mutex)` 뮤텍스를 잡고, `CONFIG_ARM_NONSECURE_PREEMPTIBLE_SECURE_CALLS`가 꺼져 있으면 현재 스레드 우선순위를 `K_HIGHEST_THREAD_PRIO`로 올려 Secure 호출 중 선점되지 않게 한다. `BUILD_WITH_TFM`은 이 옵션을 `imply`한다 — TF-M 쪽(SPM)이 NS 인터럽트로 인한 선점을 스스로 처리할 수 있기 때문이다.
- TF-M 없이 자체 Secure 엔트리를 쓰는 경우를 위해 `arch/arm/include/cortex_m/tz_ns.h`의 `TZ_THREAD_SAFE_NONSECURE_ENTRY_FUNC()` 매크로가 있다. 진입 함수 호출을 `k_sched_lock()` / `k_sched_unlock()`으로 감싸는 naked 래퍼를 만든다.
- NS 스레드의 첫 EXC_RETURN은 `arch/arm/core/cortex_m/thread.c`에서 `CONFIG_ARM_NONSECURE_FIRMWARE`면 `0xBC`, 아니면 `0xFD`다(B2에서 본 줄).

| 항목 | FreeRTOS ARM_CM33 (TZ) | Zephyr NS + TF-M |
|---|---|---|
| Secure 스택 | 태스크마다 (`xSecureContexts[]`) | TF-M 쪽이 관리, NS 스레드는 공유 |
| Secure 호출 중 선점 | 허용 (PendSV가 Secure context도 교체) | 뮤텍스로 직렬화 + 기본은 우선순위 올림 |
| 비용이 드는 곳 | 모든 전환(context 가진 태스크) | Secure 호출마다 뮤텍스 + 우선순위 변경 |
| 위험 | `portALLOCATE_SECURE_CONTEXT` 누락 → HardFault | 긴 Secure 호출이 높은 우선순위 스레드를 막음(우선순위 역전 비슷한 지연) |

---

## 10. TF-M — Secure 쪽의 "OS", 그리고 Zephyr `mps2/an521/cpu0/ns`

### 10.1 무엇인가

**TF-M(Trusted Firmware-M)** 은 Armv8-M용 Secure 쪽 참조 펌웨어다. Arm이 주도하고 TrustedFirmware.org가 관리하는 오픈소스(BSD-3-Clause)이며, Arm의 PSA(Platform Security Architecture) 규격을 구현한다. 이 노트를 쓰는 시점의 최신 릴리스는 **v2.3.1(2026-08-19)** 이고, **v2.3.0(2026-04-28)이 LTS**다(TF-M `docs/releases/index.rst` 표와 TrustedFirmware.org 뉴스 "Trusted Firmware-M v2.3.0, TF-PSA-Crypto 1.1.0 & Mbed TLS 4.1.0 LTS Releases", 2026-05-13 게시로 확인). v2.3.0부터 암호 라이브러리가 Mbed TLS에서 **TF-PSA-Crypto**로 바뀌었다.

용어부터 정리한다.

- **SPE(Secure Processing Environment)**: Secure 쪽 전체. TF-M이 여기다.
- **NSPE(Non-secure Processing Environment)**: NS 쪽 전체. RTOS와 앱.
- **SPM(Secure Partition Manager)**: SPE 안의 작은 커널. 파티션 사이의 호출(PSA 호출)을 중계하고 격리를 강제한다. `secure_fw/spm/core/`.
- **Secure partition**: SPE 안의 서비스 단위. 매니페스트(YAML)로 이름 · 종류 · 모델 · 스택 크기 · 서비스 ID를 선언한다.
- **PSA RoT / Application RoT**: 파티션의 신뢰 등급. PSA RoT(플랫폼 보안의 뿌리: 암호, 키 저장, 증명)가 Application RoT(제품별 보안 서비스)보다 높다.
- **SFN / IPC 모델**: 파티션 실행 모델. SFN(Secure Function)은 호출되면 함수처럼 실행(가볍다), IPC는 자기 스레드와 메시지 루프를 가진 파티션이다.
- **NS agent**: NSPE의 요청을 SPE로 들여오는 특수 파티션. 같은 코어의 TrustZone 경계는 `ns_agent_tz`(veneer 집합), 다른 코어의 NSPE는 `ns_agent_mailbox`(멀티코어 메일박스, P5 노트와 연결).

PSA Functional API(NS 앱이 쓰는 표준 함수)는 네 묶음이 대표적이다: **PSA Crypto**(`psa_crypto_init`, `psa_import_key`, `psa_sign_hash` …), **PSA Internal Trusted Storage**(ITS, 칩 내부 보안 저장소), **PSA Protected Storage**(PS, 외부 플래시를 암호화 · 무결성 보호해서 쓰는 저장소), **PSA Initial Attestation**(기기가 자기 상태를 서명된 토큰으로 증명). 이 API들의 사용법과 PSA Certified 인증은 L5에서 다룬다.

```svg
<svg viewBox="0 0 660 360" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah5" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<rect x="10" y="10" width="230" height="340" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="125" y="32" font-size="13" text-anchor="middle">NSPE (Non-secure)</text>
<rect x="25" y="48" width="200" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="125" y="73" font-size="12" text-anchor="middle">앱 스레드 (Zephyr, FreeRTOS)</text>
<rect x="25" y="102" width="200" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="125" y="127" font-size="12" text-anchor="middle">PSA API (psa_call 등)</text>
<rect x="25" y="156" width="200" height="54" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="125" y="178" font-size="12" text-anchor="middle">TF-M NS 인터페이스</text>
<text x="125" y="196" font-size="12" text-anchor="middle">(Zephyr: tfm_mutex)</text>
<line x1="125" y1="88" x2="125" y2="100" stroke="currentColor" marker-end="url(#ah5)"/>
<line x1="125" y1="142" x2="125" y2="154" stroke="currentColor" marker-end="url(#ah5)"/>
<rect x="270" y="10" width="380" height="340" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="460" y="32" font-size="13" text-anchor="middle">SPE (Secure) — TF-M</text>
<rect x="285" y="156" width="110" height="54" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340" y="178" font-size="12" text-anchor="middle">ns_agent_tz</text>
<text x="340" y="196" font-size="12" text-anchor="middle">(SG veneer)</text>
<line x1="226" y1="183" x2="282" y2="183" stroke="currentColor" marker-end="url(#ah5)"/>
<rect x="410" y="156" width="225" height="54" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="522" y="178" font-size="12" text-anchor="middle">SPM (파티션 관리자)</text>
<text x="522" y="196" font-size="12" text-anchor="middle">invec/outvec 검사 · 격리</text>
<line x1="396" y1="183" x2="408" y2="183" stroke="currentColor" marker-end="url(#ah5)"/>
<rect x="285" y="48" width="350" height="90" rx="4" fill="#3f9a6b" fill-opacity="0.08" stroke="#3f9a6b" stroke-dasharray="4 3"/>
<text x="460" y="66" font-size="12" text-anchor="middle">PSA RoT 파티션</text>
<rect x="295" y="76" width="74" height="28" rx="3" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="332" y="95" font-size="12" text-anchor="middle">Crypto</text>
<rect x="377" y="76" width="74" height="28" rx="3" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="414" y="95" font-size="12" text-anchor="middle">ITS</text>
<rect x="459" y="76" width="74" height="28" rx="3" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="496" y="95" font-size="12" text-anchor="middle">Attest</text>
<rect x="541" y="76" width="84" height="28" rx="3" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="583" y="95" font-size="12" text-anchor="middle">Platform</text>
<text x="460" y="126" font-size="12" text-anchor="middle">FWU 등</text>
<rect x="285" y="226" width="350" height="56" rx="4" fill="#3f9a6b" fill-opacity="0.08" stroke="#3f9a6b" stroke-dasharray="4 3"/>
<text x="460" y="246" font-size="12" text-anchor="middle">Application RoT 파티션</text>
<rect x="400" y="252" width="120" height="24" rx="3" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="460" y="269" font-size="12" text-anchor="middle">PS (보호 저장소)</text>
<line x1="522" y1="155" x2="522" y2="140" stroke="currentColor" marker-end="url(#ah5)"/>
<line x1="522" y1="211" x2="522" y2="224" stroke="currentColor" marker-end="url(#ah5)"/>
<text x="460" y="304" font-size="12" text-anchor="middle">격리 L1: SPE와 NSPE만 분리</text>
<text x="460" y="322" font-size="12" text-anchor="middle">L2: + PSA RoT와 App RoT 분리 (MPU)</text>
<text x="460" y="340" font-size="12" text-anchor="middle">L3: + 파티션끼리도 분리</text>
</svg>
```

그림 8 — TF-M 구조. NS 앱의 PSA 호출은 NS 인터페이스 → TrustZone veneer(ns_agent_tz) → SPM → 서비스 파티션 순으로 간다. 격리 레벨이 올라갈수록 SPE 안의 칸막이가 늘고(Secure MPU 재설정) 호출이 비싸진다.

### 10.2 격리 레벨

PSA Firmware Framework-M(FF-M)이 정의하고 TF-M이 구현하는 격리 레벨이다.

| 레벨 | 무엇을 분리하나 | 하드웨어 수단 | TF-M 기본 |
|---|---|---|---|
| 1 | SPE ↔ NSPE | TrustZone (SAU/IDAU/MPC/PPC) | `config_base.cmake`의 기본값 `TFM_ISOLATION_LEVEL 1` |
| 2 | + PSA RoT ↔ Application RoT, Secure 특권/비특권 | + Secure MPU, Secure 쪽 비특권 실행 | Zephyr Kconfig: MEDIUM 프로파일의 기본 |
| 3 | + 파티션 ↔ 파티션 | + 파티션마다 MPU 영역 교체 | 플랫폼이 `PLATFORM_HAS_ISOLATION_L3_SUPPORT`를 켜야 함 (AN521은 켬) |

레벨 2 · 3은 Secure 쪽에서 F4(MPU) · F5(유저 모드 격리)와 같은 일을 하는 것이다. 즉 TF-M은 "Secure 쪽의 작은 Zephyr userspace"라고 생각하면 가깝다.

### 10.3 예제 13 — TF-M v2.3.1 소스에서 확인한 것

무엇을 확인하나: 버전 태그, 격리 레벨 기본값, 기본 파티션의 종류 · 모델, AN521 포팅의 SAU/NSCCFG/ITNS/SECRESPCFG 설정, veneer 속성. (`/var/tmp/rtos/F6/tfm`에 TF-Mv2.3.1 태그를 `--depth 1`로 받아 읽기만 했다. 155 MB.)

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/rtos/F6 && git clone -q --depth 1 --branch TF-Mv2.3.1 https://git.trustedfirmware.org/TF-M/trusted-firmware-m.git tfm; cd tfm;
  git describe --tags; grep -n "TFM_ISOLATION_LEVEL \|TFM_TZ_REENTRANCY_CHECK " config/config_base.cmake;
  grep -n "PLATFORM_HAS_ISOLATION_L3_SUPPORT" platform/ext/target/arm/mps2/an521/config.cmake;
  grep -n "nsccfg |= \|ITNS\[i\] = \|secrespcfg |= " platform/ext/target/arm/mps2/an521/target_cfg.c;
  grep -n "define __tz_c_veneer" -A1 secure_fw/include/security_defs.h | head -2;
  for f in secure_fw/partitions/*/*.yaml; do n=$(grep -m1 "\"name\"" $f | cut -d\" -f4); t=$(grep -m1 "\"type\"" $f | cut -d\" -f4);
    m=$(grep -m1 "\"model\"" $f | cut -d\" -f4); printf "%-28s %-16s %s\n" $n $t $m; done'
```

```text
TF-Mv2.3.1
86:set(TFM_ISOLATION_LEVEL                 1           CACHE STRING    "Isolation level")
186:set(TFM_TZ_REENTRANCY_CHECK             OFF         CACHE BOOL      "Enable check on Armv8-M integrity signature to prevent reentrancy")
24:set(PLATFORM_HAS_ISOLATION_L3_SUPPORT ON)
252:        NVIC->ITNS[i] = 0xFFFFFFFF;
405:    spctrl->nsccfg |= NSCCFG_CODENSC;
625:    spctrl->secrespcfg |= 1U;
33:#define __tz_c_veneer \
34-        __attribute__((cmse_nonsecure_entry, noclone))
TFM_SP_CRYPTO                PSA-ROT          SFN
TFM_SP_FWU                   PSA-ROT          SFN
TFM_SP_INITIAL_ATTESTATION   PSA-ROT          SFN
TFM_SP_ITS                   PSA-ROT          SFN
TFM_NS_MAILBOX_AGENT         PSA-ROT          IPC
TFM_SP_PLATFORM              PSA-ROT          SFN
TFM_SP_PS                    APPLICATION-ROT  SFN
```

(여러 줄 명령을 읽기 쉽게 줄바꿈했다. 실제로는 한 번에 실행했다.)

출력에서 볼 것:

- AN521 포팅(`target_cfg.c`)이 이 노트에서 손으로 한 것과 같은 세 가지를 한다: **`nsccfg |= NSCCFG_CODENSC`**(예제 5 N8의 비트), **`secrespcfg |= 1U`**(막힌 접근을 bus error로 — §2.5 권장), **`ITNS[i] = 0xFFFFFFFF`**(일단 모든 인터럽트를 NS로 보내고, 바로 다음 줄들에서 MPC · PPC 인터럽트와 Secure UART만 Secure로 되돌린다 — 소스 251~262줄).
- 기본 파티션은 대부분 PSA RoT + SFN 모델이고, Protected Storage만 Application RoT다. 멀티코어용 메일박스 에이전트는 IPC 모델이다.
- veneer는 `cmse_nonsecure_entry` + `noclone`(컴파일러가 함수를 복제해 엉뚱한 사본이 진입점이 되는 것을 막는다).
- `TFM_TZ_REENTRANCY_CHECK`(기본 OFF): "Armv8-M integrity signature로 재진입을 검사" — §3.4 표의 재진입 문제를 TF-M이 선택 기능으로 막는다. v2.3.0 릴리스 노트의 "ns_agent_tz: Introduce reentrancy checks for NSPE calls"가 이것이다.
- `tfm_psa_call_veneer()`(`secure_fw/partitions/ns_agent_tz/psa_api_veneers.c`)는 `CONFIG_TFM_SECURE_THREAD_MASK_NS_INTERRUPT`가 켜져 있으면 Secure 처리 동안 `__set_BASEPRI(SECURE_THREAD_EXECUTION_PRIORITY)`로 NS 인터럽트를 막는다. §7.3의 PRIS와 같은 문제(NS가 Secure를 끊는 것)를 다른 방식으로 다룬다.

### 10.4 예제 12 — Zephyr v4.4.2의 `mps2/an521/cpu0/ns` 보드

Zephyr에서 TrustZone NS 펌웨어를 만드는 표준 길은 `/ns` 보드 변형 + `CONFIG_BUILD_WITH_TFM=y`다. 빌드하면 sysbuild/CMake가 TF-M(Secure 이미지 + MCUboot 기반 BL2)을 먼저 빌드하고, TF-M이 내는 veneer import library를 NS Zephyr에 링크해, 서명된 이미지를 합친다. 이 랩은 west 워크스페이스에 `trusted-firmware-m` 모듈이 없고(프로젝트 필터 밖, 무겁다) 지시대로 추가하지 않았으므로 **빌드하지 않고 소스만 읽었다.**

```sh
limactl shell elinux -- bash -lc 'cd /var/rtoslab/zephyrproject/zephyr;
  grep -n "CONFIG_ARM_TRUSTZONE_M\|ICOUNT" boards/arm/mps2/mps2_an521_cpu0_ns_defconfig;
  grep -n "code-partition\|slot0_ns_partition: \|ram: memory" boards/arm/mps2/mps2_an521_cpu0_ns.dts;
  grep -n "DEFAULT_EXC_RETURN 0x" arch/arm/core/cortex_m/thread.c;
  grep -n "K_MUTEX_DEFINE(tfm_mutex)\|K_HIGHEST_THREAD_PRIO)" modules/trusted-firmware-m/interface/interface.c;
  grep -n "imply ARM_NONSECURE_PREEMPTIBLE_SECURE_CALLS" modules/trusted-firmware-m/Kconfig.tfm;
  grep -n "__TZ_WRAP_FUNC(k_sched_lock" arch/arm/include/cortex_m/tz_ns.h;
  grep -n "trusted-firmware-m" -A1 west.yml | head -2; ls ../modules'
```

```text
8:CONFIG_ARM_TRUSTZONE_M=y
10:CONFIG_QEMU_ICOUNT_SHIFT=6
33:		zephyr,code-partition = &slot0_ns_partition;
105:	psram: memory@80000000 {
139:			slot0_ns_partition: partition@100000 {
162:		ram: memory@28100000 {
40:#define DEFAULT_EXC_RETURN 0xBC;
42:#define DEFAULT_EXC_RETURN 0xFD;
25:K_MUTEX_DEFINE(tfm_mutex);
50:		k_thread_priority_set(k_current_get(), K_HIGHEST_THREAD_PRIO);
54:	imply ARM_NONSECURE_PREEMPTIBLE_SECURE_CALLS
128:		__TZ_WRAP_FUNC(k_sched_lock, nsc_name, k_sched_unlock);                            \
407:    - name: trusted-firmware-m
408-      revision: 9a4cb1a280b63a5e66bd469e82eb75f2b3e05a78
hal
lib
```

출력에서 볼 것:

- NS Zephyr의 코드는 `slot0_ns_partition`(`0x0010_0000`, NS 별칭 — MCUboot 슬롯 구조, L2), RAM은 `0x2810_0000`부터 512 KB다. DTS 주석은 "TF-M이 NS 펌웨어에 할당한 주소와 맞아야 한다"고 적어 둔다. 이 노트의 손 구성(`0x0020_0000`/`0x2820_0000`)과 같은 원리, 다른 숫자다.
- `/ns` 보드의 QEMU icount는 shift 6(명령당 64 ns)이다. `mps2/an385`의 7과도, 이 노트의 5와도 다르다.
- Zephyr v4.4.2의 `west.yml`은 TF-M을 `modules/tee/tf-m/trusted-firmware-m`에 특정 커밋(`9a4cb1a2…`, Zephyr 쪽 포크)으로 고정한다. 그 커밋이 어느 TF-M 릴리스에 해당하는지는 모듈을 받지 않아 **확인 못 함**. 이 랩의 `modules`에는 `hal`, `lib`만 있다.

---

## 11. RTOS 커널 안에서는 무슨 일이 일어나나

### 11.1 예제 14 — FreeRTOS 포트에서 직접 확인

```sh
limactl shell elinux -- bash -lc 'cd /var/rtoslab/src/FreeRTOS-Kernel; P=portable/GCC/ARM_CM33;
  grep -n "portSVC_ALLOCATE_SECURE_CONTEXT \|portSVC_FREE_SECURE_CONTEXT " $P/non_secure/portmacrocommon.h;
  grep -n "SecureInit_DePrioritizeNSExceptions();\|SecureContext_Init();\|SecureContext_AllocateContext( ulR0, pxCurrentTCB )" $P/non_secure/port.c;
  grep -n "bl SecureContext_SaveContext\|bl SecureContext_LoadContext\|lsls r3, r3, #25" $P/non_secure/portasm.c | head -6;
  grep -n "securecontextSTACK_SEAL_VALUE  \|secureconfigMAX_SECURE_CONTEXTS    8UL" $P/secure/secure_context.c;
  grep -n "portALLOCATE_SECURE_CONTEXT( configMINIMAL_SECURE_STACK_SIZE )" tasks.c;
  ls portable/GCC | grep -c "_NTZ"; ls portable/GCC | grep "ARM_CM[0-9]*$" | tr "\n" " "'
```

```text
403:#define portSVC_ALLOCATE_SECURE_CONTEXT    100
404:#define portSVC_FREE_SECURE_CONTEXT        101
1176:                xSecureContext = SecureContext_AllocateContext( ulR0, pxCurrentTCB );
1213:                    SecureInit_DePrioritizeNSExceptions();
1216:                    SecureContext_Init();
300:            "    bl SecureContext_SaveContext                 \n" /* Params are in r0 and r1. r0 = xSecureContext and r1 = pxCurrentTCB. */
305:            "    lsls r3, r3, #25                             \n" /* r3 = r3 << 25. Bit[6] of EXC_RETURN is 1 if secure stack was used, 0 if non-secure stack was used to store stack frame. */
412:            "    bl SecureContext_LoadContext                 \n" /* Params are in r0 and r1. r0 = xSecureContext and r1 = pxCurrentTCB. */
458:            "    bl SecureContext_SaveContext                 \n" /* Params are in r0 and r1. r0 = xSecureContext and r1 = pxCurrentTCB. */
463:            "    lsls r3, r3, #25                             \n" /* r3 = r3 << 25. Bit[6] of EXC_RETURN is 1 if secure stack was used, 0 if non-secure stack was used to store stack frame. */
519:            "    bl SecureContext_LoadContext                 \n" /* Restore the secure context. Params are in r0 and r1. r0 = xSecureContext and r1 = pxCurrentTCB. */
62:#define securecontextSTACK_SEAL_VALUE              0xFEF5EDA5
68:    #define secureconfigMAX_SECURE_CONTEXTS    8UL
5920:    portALLOCATE_SECURE_CONTEXT( configMINIMAL_SECURE_STACK_SIZE );
7
ARM_CM0 ARM_CM23 ARM_CM3 ARM_CM33 ARM_CM52 ARM_CM55 ARM_CM7 ARM_CM85
```

출력에서 볼 것: PendSV가 두 벌(300/412줄은 MPU 버전, 458/519줄은 MPU 없는 버전)이고 둘 다 Save → (EXC_RETURN bit 6 검사) → Load 순서다. TrustZone 포트(`_NTZ`가 없는 쪽)는 v8-M 코어 M23 · M33 · M52 · M55 · M85에 있고, `_NTZ` 변형은 7개다(B7 표와 같다).

### 11.2 그림으로 정리한 커널 경로

FreeRTOS(NS):

- `tasks.c`: `prvIdleTask()` 첫 줄 `portALLOCATE_SECURE_CONTEXT()`, `portCLEAN_UP_TCB()` → `vPortFreeSecureContext()`
- `portable/GCC/ARM_CM33/non_secure/port.c`: `vPortSVCHandler_C()`의 `portSVC_ALLOCATE_SECURE_CONTEXT` · `portSVC_FREE_SECURE_CONTEXT` · `portSVC_START_SCHEDULER` 분기, `pxPortInitialiseStack()`(첫 EXC_RETURN `0xffffffbc`, `xSecureContext` 슬롯 = `portNO_SECURE_CONTEXT`)
- `portable/GCC/ARM_CM33/non_secure/portasm.c`: `PendSV_Handler`, `vPortAllocateSecureContext()`(`svc 100`), `vPortFreeSecureContext()`(`svcne 101`)
- `portable/GCC/ARM_CM33/secure/secure_context.c` · `secure_context_port.c` · `secure_heap.c` · `secure_init.c`: Secure 쪽 진입 함수와 PSP_S/PSPLIM_S 교체 어셈블리

Zephyr(NS):

- `arch/arm/core/cortex_m/tz/CMakeLists.txt`: Secure 펌웨어면 `-mcmse`, `CONFIG_ARM_FIRMWARE_HAS_SECURE_ENTRY_FUNCS`면 `--out-implib` + `--cmse-implib`, NS 펌웨어(`CONFIG_ARM_FIRMWARE_USES_SECURE_ENTRY_FUNCS`)면 그 라이브러리를 링크 — 이 노트의 `mk.sh`와 같은 세 플래그를 CMake로 한 것
- `arch/arm/core/cortex_m/tz/secure_entry_functions.ld`: `.gnu.sgstubs` 배치, `__sg_start`, `__nsc_size`, nRF SPU 정렬 요구
- `arch/arm/core/cortex_m/cmse/arm_core_cmse.c`: `arm_cmse_addr_range_read_ok()`, `arm_cmse_addr_range_nonsecure_readwrite_ok()` 등 TT 기반 검사 함수(§5의 Zephyr판)
- `arch/arm/include/cortex_m/tz_ns.h`: `TZ_THREAD_SAFE_NONSECURE_ENTRY_FUNC()`
- `modules/trusted-firmware-m/interface/interface.c`: `tfm_ns_interface_dispatch()`

더 깊은 내용은: 컨텍스트 스위치 일반은 C2, MPU 리전 교체는 F4, 유저 모드 격리는 F5, MCUboot · 서명 이미지는 L2, 신뢰 루트는 L4, PSA API · 인증은 L5, 멀티코어 메일박스는 P5.

---

## 12. 실무에서 다시 보기

### 12.1 드론 비행 제어기 (STM32U5 / nRF54급 M33)

- **배치**: Secure = 부트 검증(L2 · L4), 기체 인증 키, 텔레메트리 세션 키 · AES-GCM, 파라미터 무결성(PS). NS = Zephyr 또는 FreeRTOS + 자세 제어 + MAVLink + GNSS + 로그.
- **인터럽트**: IMU SPI DMA · 모터 타이머 · UART는 전부 ITNS=1(NS). Secure 쪽에 남기는 것은 MPC/PPC 위반 인터럽트, 보안 워치독 정도다. Secure IRQ를 많이 두면 NS 제어 루프의 지터가 생긴다(Secure ISR이 NS를 선점하는 것은 PRIS와 무관하게 가능).
- **제어 루프 지터**: Secure 서비스는 rate 태스크에서 절대 부르지 않는다. 암호화는 텔레메트리 태스크에서 패킷 단위로. FreeRTOS라면 rate 태스크는 Secure context를 할당하지 않아 전환 비용이 그대로다(예제 10의 왼쪽 막대).
- **Zephyr + TF-M이면**: Secure 호출이 `tfm_mutex`로 직렬화되고 기본으로 호출 스레드 우선순위를 올린다. 긴 `psa_sign_hash()`가 그동안 다른 PSA 호출 스레드를 막는다. 자세 제어 스레드는 PSA를 부르지 않게 설계한다.

### 12.2 로봇 관절 모터 컨트롤러

FOC 전류 루프(20 kHz)는 ISR이고 NS에 있다. 이 MCU에서 TrustZone의 실익은 "펌웨어 업데이트 검증과 CAN 노드 인증 키를 지키는 것"이다. Secure 쪽을 작게 유지하고(TF-M 없이 직접 만든 최소 Secure 이미지 — BM C12의 관점), Secure 함수는 부팅과 업데이트 때만 부른다. 런타임 경계 비용은 사실상 0이다.

### 12.3 센서 허브 MCU

Linux SoC 옆의 센서 허브는 호스트에게 "이 타임스탬프 · 이 데이터는 진짜 이 센서에서 왔다"를 증명해야 할 수 있다. PSA Initial Attestation 토큰이나 Secure 쪽 HMAC 서비스로 측정값에 서명한다. 서명 서비스 입력 버퍼는 반드시 `cmse_check_address_range()`(예제 6)로 검사하고 Secure 메모리로 복사한 뒤 서명한다(§5.5).

### 12.4 Don의 SSD 컨트롤러

SSD의 "보안 서브시스템"(키 관리 · 암호 엔진 제어 · 서명 검증)을 같은 M33 코어의 Secure 쪽에 두고, FTL · NVMe 처리를 NS에 두는 구성으로 볼 수 있다. 이때 실제로 위험한 것은 §5의 confused deputy다: NVMe 명령의 버퍼 주소(호스트가 고른 값이 펌웨어를 거쳐 옴)를 Secure 암호 서비스가 검사 없이 받으면, 버그 하나로 키 영역이 출력 버퍼로 복사될 수 있다. 그리고 DMA 엔진은 코어가 아니어서 SAU의 보호를 받지 않는다 — MPC와 MSC(마스터 보안 속성)로 DMA가 Secure 블록에 못 닿게 해야 한다(§2.5).

```c
/* 실무 패턴: NS가 준 요청 구조체를 Secure로 복사한 뒤 검사하고 쓴다 (빌드만 함 — 예제 6의 확장) */
#include <stdint.h>
#include <arm_cmse.h>
typedef struct { const uint8_t *src; uint8_t *dst; uint32_t len; } s_req_t;
__attribute__((cmse_nonsecure_entry)) int32_t s_encrypt(const s_req_t *ns_req)
{
    const s_req_t *r = cmse_check_pointed_object((s_req_t *)ns_req, CMSE_NONSECURE | CMSE_MPU_READ);
    if (r == NULL) return -1;
    s_req_t q = *r;                                            /* 한 번만 읽는다 (double fetch 방지) */
    if (q.len > 4096u) return -2;
    if (!cmse_check_address_range((void *)q.src, q.len, CMSE_NONSECURE | CMSE_MPU_READ)) return -3;
    if (!cmse_check_address_range(q.dst, q.len, CMSE_NONSECURE | CMSE_MPU_READWRITE)) return -3;
    /* ... q.src → Secure 버퍼로 복사 → 암호화 → q.dst로 복사 ... */
    return 0;
}
```

(`arm-none-eabi-gcc -mcpu=cortex-m33 -mthumb -mcmse -O2 -Wall -Wextra -c`로 컴파일만 해서 경고 0개를 확인했다.)

---

## 13. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| SAU에 NSC 영역만 만들고 IDAU 쪽 NSC 허용을 안 켬 | veneer 호출이 SecureFault INVEP (예제 5 N8) | IDAU S + SAU NSC = S | 칩의 NSC 설정(AN505/AN521 `NSCCFG.CODENSC`, nRF SPU NSC 영역) 확인 |
| SAU만 열고 MPC 블록을 NS로 안 바꿈 | NS 부팅 직후 BusFault 또는 NS 코드가 0만 읽음 | 버스의 MPC가 NS 트랜잭션 차단 (N6) | 메모리 맵 표 한 장에서 SAU · MPC를 같이 생성 |
| PPC 포트를 안 염 | NS 드라이버가 레지스터에서 0만 읽음, 쓰기 무시 (N7) | PPC RAZ/WI | 부트에서 PPC 열기, `SECRESPCFG=1`로 bus error가 나게 |
| NS 리셋 핸들러로 `BX`(bit 0 = 1) 점프 | SecureFault INVTRAN (S1) | Secure → NS는 BXNS/BLXNS + bit 0 = 0 | `cmse_nsfptr_create()` + `cmse_nonsecure_call` 함수 포인터 |
| Secure 서비스가 NS 포인터를 검사 없이 사용 | 키 유출 · Secure 변수 변조 (예제 6) | confused deputy | `cmse_check_address_range()`, 복사 후 사용 |
| NS 태스크가 Secure context 없이 Secure 함수 호출 | Secure HardFault, BFAR `0xFFFFFFxx` (예제 9b) | PSP_S = 0 | 태스크 첫 줄 `portALLOCATE_SECURE_CONTEXT()` |
| NS 주변장치 인터럽트의 ITNS를 안 켬 | NS 핸들러가 안 불림, Secure 기본 핸들러로 감 (예제 7의 6) | NVIC 대상이 Secure | Secure 부트에서 ITNS 설정 (TF-M은 전부 NS로 시작) |
| NS에 넘긴 장치를 Secure가 S 별칭으로 접근 | 인터럽트 클리어가 안 돼 무한 ISR (예제 7 첫 시도) | PPC가 NS만 통과 | Secure도 NS 별칭으로 접근하거나 장치를 넘기지 않음 |
| `--cmse-implib`인데 진입 함수 없음 / 진입 함수에 인자 5개 | 링크 실패 "no symbol found for import library" / 컴파일 에러 | import library가 비어 있음 / NS 스택 인자 금지 | 진입 함수 추가 / 구조체 포인터로 |
| NSC 영역에 일반 코드 · 상수가 섞임 | 평소엔 정상, 보안 감사에서 "의도치 않은 SG 입구" 지적 | `0xE97FE97F` 패턴이 NSC 안에 생길 수 있음 | `.gnu.sgstubs`만 NSC에, 32바이트 정렬 |

---

## 14. 면접에서 이렇게 말한다

**Q.** "What is TrustZone-M and how is it different from TrustZone on Cortex-A?"

**A.** 한 코어를 Secure/Non-secure 두 상태로 나누는 것은 같다. 차이는 상태를 정하는 방법이다. Cortex-A는 EL3 모니터가 SCR.NS를 바꾸고 SMC로 전환한다. Cortex-M에는 모니터가 없고, 실행 중인 명령의 주소가 Secure로 판정되면 Secure 상태다. 판정은 SAU(소프트웨어 설정)와 IDAU(칩 고정)가 하고, NS에서 Secure로는 NSC 영역의 SG 명령으로만 들어간다. 그래서 전환이 함수 호출 수준으로 싸다.

> Both split one core into Secure and Non-secure states, but the mechanism differs. On Cortex-A an EL3 monitor flips SCR.NS and you switch worlds with SMC. Cortex-M has no monitor: the security state follows the address of the instruction being executed. The SAU, configured by Secure software, and the chip's IDAU attribute every address as Secure, Non-secure-callable or Non-secure, and the only way in from Non-secure is a branch to an SG instruction in an NSC region. So a world switch costs about as much as a function call — on QEMU I measured about seven extra instructions per call.

**꼬리 질문**: "What's an IDAU?" → 칩 설계자가 하드웨어로 고정한 판정기. 보통 주소 비트 하나로 S/NS 별칭을 만든다(AN505는 bit 28). SAU와 결과를 합칠 때 더 안전한 쪽이 이긴다.

**Q.** "How does a Non-secure function call a Secure function?"

**A.** Secure 함수에 `cmse_nonsecure_entry`를 붙여 `-mcmse`로 빌드하면 링커가 NSC 영역에 SG + B.W 두 명령짜리 veneer를 만들고, `--out-implib`로 veneer 주소만 담은 import library를 낸다. NS는 그것을 링크해 평범하게 BL한다. SG에서 Secure가 되고, 몸통은 반환 전에 r1~r3 · r12 · 플래그를 지우고 BXNS로 돌아온다. 인자는 레지스터 4개까지만 된다.

> You mark the function cmse_nonsecure_entry and build the Secure image with -mcmse. The linker emits an 8-byte veneer — SG followed by a branch — into the .gnu.sgstubs section, which must sit in an NSC region, and --out-implib writes an import library with just the veneer addresses. The Non-secure image links that and calls it with a normal BL. SG switches to Secure; the compiler-generated epilogue clears the scratch registers and flags and returns with BXNS. GCC refuses entry functions whose arguments spill onto the stack, because the Secure side would have to read the Non-secure stack.

**Q.** "What is the confused deputy problem in TrustZone, and how do you prevent it?"

**A.** Secure 함수는 Secure 상태로 돌기 때문에 NS가 준 포인터가 Secure 메모리를 가리켜도 그대로 읽고 쓴다. 그러면 NS가 Secure 서비스를 통해 키를 읽거나 Secure 변수를 바꾼다. 막는 법은 `cmse_check_address_range()`(TT 명령 기반)로 범위 전체가 NS 접근 가능한지 확인하고, NS가 준 구조체는 Secure로 복사한 뒤 복사본만 쓰는 것이다. 비특권 NS 태스크라면 `CMSE_MPU_UNPRIV`도 붙인다.

> Secure code runs with Secure privileges, so if a Non-secure caller passes a pointer into Secure memory, a naive service will happily read or write it on the caller's behalf. In my QEMU test a summing service leaked a four-byte key one byte at a time, and a version-writing service overwrote a Secure flag. The fix is to validate every pointer and length with cmse_check_address_range, which uses the TT instruction to confirm the whole range is Non-secure accessible, and to copy request structures into Secure memory before checking them to avoid double-fetch races.

**Q.** "If an RTOS runs in the Non-secure world and a task is preempted while executing a Secure function, what happens?"

**A.** 하드웨어가 Secure 레지스터를 Secure 스택에 넣고 지운 뒤 NS 핸들러를 실행한다(EXC_RETURN S 비트 = 1). 문제는 다음 태스크도 Secure를 부르면 같은 Secure 스택을 쓴다는 것이다. FreeRTOS ARM_CM33 포트는 태스크마다 Secure 스택을 할당하고, NS PendSV가 SecureContext_Save/Load 진입 함수를 불러 PSP_S를 바꾼다. 실측으로 전환 비용이 두 배였다. Zephyr + TF-M은 대신 뮤텍스로 Secure 호출을 직렬화한다.

> The core stacks the Secure context — including the callee-saved registers — on the Secure stack, clears the registers and runs the Non-secure handler; EXC_RETURN has the S bit set. The danger is the next task also calling into Secure and reusing that stack. FreeRTOS's ARM_CM33 TrustZone port solves it with a per-task Secure stack: tasks call portALLOCATE_SECURE_CONTEXT, and the Non-secure PendSV calls SecureContext_SaveContext and LoadContext through veneers to swap PSP_S and PSPLIM_S. On QEMU that doubled the yield cost from about 70 to 145 instructions. Zephyr with TF-M takes the other approach and serializes Secure calls behind a mutex.

**꼬리 질문**: "What if a task forgets to allocate a Secure context?" → PSP_S가 0이라 Secure 함수의 첫 push가 `0xFFFFFFF4`에 써져 Secure HardFault(예제 9b). 스택을 안 쓰는 작은 진입 함수는 우연히 동작해서 버그가 숨는다.

**Q.** "How are interrupts split between the two worlds?"

**A.** NVIC는 하나이고 Secure가 ITNS 비트로 인터럽트마다 대상을 정한다. NS는 자기 대상 인터럽트만 보인다. SysTick · PendSV · SVCall은 세계마다 따로 있고, BusFault · HardFault · NMI는 AIRCR.BFHFNMINS로 정한다. AIRCR.PRIS를 켜면 NS 우선순위가 아래 절반으로 밀려서 NS가 Secure 핸들러를 선점하지 못한다 — QEMU에서 PRIS 0/1로 순서가 바뀌는 것을 확인했다.

> There's one NVIC; Secure code sets an ITNS bit per interrupt to route it to the Non-secure vector table, and Non-secure software only sees its own interrupts — the same ISER register read 0xC08 from Secure and 0x808 from Non-secure in my test. SysTick, PendSV and SVCall are banked per world; BusFault, HardFault and NMI follow AIRCR.BFHFNMINS. With AIRCR.PRIS set, Non-secure priorities are compressed into the lower half, so even a priority-zero Non-secure IRQ can't preempt a Secure handler at 0x40. FreeRTOS sets PRIS when the scheduler starts.

**Q.** "Bringing up a new M33 board, the Non-secure driver reads all zeros from its peripheral. Where do you look?"

**A.** 세 층을 차례로 본다. SAU에 그 주소 범위의 NS 영역이 있는가(없으면 SecureFault가 나야 하니 보통 아님), PPC가 그 포트를 NS로 열었는가(안 열었고 응답이 RAZ/WI면 정확히 "0만 읽힘"), 장치의 클럭 · 리셋. 그리고 인터럽트가 안 오면 ITNS. 디버깅을 쉽게 하려면 부트에서 막힌 접근을 bus error로 설정한다.

> I walk the three layers. First the SAU: if the address weren't Non-secure I'd get a SecureFault, not zeros. Then the peripheral protection controller: a port left Secure-only with RAZ/WI response gives exactly "reads as zero, writes ignored" — I reproduced that on QEMU's AN505 with the SCC ID register. Then the usual clock and reset. If interrupts are missing too, check ITNS. On every board I'd configure blocked accesses to raise a bus error so this shows up as a fault instead of silent zeros.

**Q.** "What is TF-M and what are its isolation levels?"

**A.** Armv8-M의 Secure 쪽 참조 펌웨어다. SPM이 Secure partition(Crypto, ITS, PS, Attestation 등)을 관리하고, NS는 PSA API로 부른다. 격리 레벨 1은 SPE와 NSPE만, 2는 PSA RoT와 Application RoT도, 3은 파티션끼리도 MPU로 나눈다. 최신 릴리스는 v2.3.1이고 v2.3.0이 LTS다. Zephyr는 `/ns` 보드와 `CONFIG_BUILD_WITH_TFM`으로 같이 빌드한다.

> TF-M is the reference Secure Processing Environment for Armv8-M. Its Secure Partition Manager hosts partitions such as Crypto, Internal Trusted Storage, Protected Storage and Initial Attestation, and the Non-secure side reaches them through the PSA APIs via TrustZone veneers or a mailbox on multi-core parts. Isolation level 1 separates only SPE from NSPE; level 2 also separates the PSA root of trust from application root-of-trust partitions; level 3 isolates every partition from the others using the Secure MPU. The current release is 2.3.1, with 2.3.0 as the LTS line. In Zephyr you select the /ns board variant and CONFIG_BUILD_WITH_TFM.

---

## 15. 직접 해보기

1. **개념**: AN505에서 IDAU가 NS라고 하는 `0x0000_0000`이 예제 4의 Secure 쪽 TT에서 S로 나왔다. 그 이유를 §1.2 표의 칸 하나로 설명하라. 정답/힌트: SAU에 그 주소를 덮는 영역이 없으면 SAU 판정은 S이고, "더 안전한 쪽"이 S다.
2. **코드**: 예제 5에 N9를 더하라 — NS가 `s_hello` veneer 주소 + 8(다음 veneer의 SG가 아닌 바로 그 다음 veneer 시작)로 분기하면 어떻게 되나? 정답/힌트: 다음 veneer의 시작은 SG이므로 **성공**한다(다른 진입 함수가 불린다). veneer 경계 자체는 보호 단위가 아니다 — 모든 진입 함수가 독립적으로 안전해야 하는 이유.
3. **코드**: 예제 6의 `s_sum_checked`에서 `CMSE_NONSECURE`를 빼고 `CMSE_MPU_READ`만 남기면 키가 다시 새는지 확인하라. 정답/힌트: 새야 한다. 플래그 없이는 "현재(Secure) 상태의 MPU 기준"으로만 보므로 Secure 주소도 읽기 가능으로 판정된다.
4. **측정**: 예제 8의 Secure 쪽을 `-mfloat-abi=hard -mfpu=fpv5-sp-d16`으로 빌드하고 NS의 `NSACR`(CP10/CP11)을 켠 뒤 같은 측정을 하라. NS → S 호출 비용이 얼마나 늘어나나? 정답/힌트: 예제 3b의 `vmov` 16개 + FPSCR 청소가 붙어 약 25 명령 증가가 예상된다. 실제 값을 재서 비교하라.
5. **코드 · 측정**: 예제 10에 태스크 C(Secure context 없음, 같은 우선순위)를 더해 A-B-C 순환으로 만들고 전환 1회 평균을 재라. 정답/힌트: context 없는 태스크로/에서의 전환은 Save 또는 Load 한쪽만 하므로 평균은 두 값 사이에 온다.
6. **설계**: 드론 FC에서 TF-M + Zephyr를 쓸 때, 400 Hz 자세 제어 스레드가 PSA를 절대 부르지 않게 하는 방법 두 가지를 적어라. 정답/힌트: (a) 암호 · 서명은 별도 낮은 우선순위 스레드 + 메시지 큐로 위임(D6), (b) 코드 리뷰 · 링크 단계에서 제어 모듈이 `psa_*` 심볼을 참조하지 않는지 검사(`nm`).
7. **조사**: 쓰고 있는(또는 지원할) 칩 하나를 골라 IDAU 맵, SAU 영역 수, MPC 블록 크기, PPC 설정 레지스터, NSC 허용 방법을 표로 만들어라. 정답/힌트: STM32U5는 GTZC(TZSC · MPCBB), nRF5340/nRF91은 SPU, LPC55S69는 AHB Secure Controller 장을 본다.

---

## 16. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| TrustZone-M | Armv8-M Security Extension | 한 코어를 Secure / Non-secure 상태로 나누는 기능 |
| Secure (S) | 보안 상태 · 보안 속성 | 리셋 직후 상태, 모든 자원 접근 가능 |
| Non-secure (NS) | 비보안 상태 · 속성 | Secure가 열어 준 자원만 접근 |
| NSC | Non-secure callable | Secure 메모리이지만 NS가 SG 위치로만 들어올 수 있는 영역 |
| SAU | Security Attribution Unit | Secure SW가 설정하는 주소 판정기 (영역 수는 구현마다) |
| IDAU | Implementation Defined Attribution Unit | 칩이 고정한 주소 판정기 (AN505는 bit 28) |
| 면제 영역 | exempt region | SCS 등, 현재 상태의 뱅크로 접근되는 주소 |
| MPC | Memory Protection Controller | 버스에서 메모리 블록마다 S/NS 트랜잭션을 거르는 장치 |
| PPC | Peripheral Protection Controller | 버스에서 주변장치 포트마다 NS · 비특권 접근을 거르는 장치 |
| SG | Secure Gateway 명령 | NS → S 진입의 유일한 입구 명령 (`0xE97F E97F`) |
| BXNS / BLXNS | 분기 명령 | S → NS 복귀 / 호출 (목적지 bit 0 = 0) |
| FNC_RETURN | 마법 복귀 값 | BLXNS가 LR에 넣는 값, 진짜 복귀 주소는 Secure 스택에 숨김 |
| veneer | 작은 중계 코드 | `SG` + `B.W 몸통` 8바이트, `.gnu.sgstubs` 섹션 |
| import library | `--out-implib` 결과물 | veneer 주소만 담은 오브젝트, NS가 링크 |
| CMSE | Cortex-M Security Extensions (컴파일러 지원) | `-mcmse`, `<arm_cmse.h>`, `cmse_nonsecure_entry/call` |
| TT / TTA | Test Target 명령 | 주소의 MPU · SAU · IDAU 판정을 돌려줌 |
| confused deputy | 혼동된 대리인 | 권한 있는 쪽이 검사 없이 남의 요청 주소를 써 주는 문제 |
| SecureFault | 예외 7번 | 보안 규칙 위반, SFSR/SFAR에 원인 |
| ITNS | Interrupt Target Non-secure | NVIC 인터럽트마다 NS로 보낼지 정하는 비트 |
| AIRCR.PRIS | Prioritize Secure | NS 예외 우선순위를 아래 절반으로 밀어냄 |
| AIRCR.BFHFNMINS | BusFault/HardFault/NMI Non-secure | 그 세 예외를 NS로 보낼지 |
| 뱅크 레지스터 | banked register | S/NS가 각자 사본을 가진 레지스터 (MSP, VTOR, SysTick, MPU …) |
| Secure context | FreeRTOS 용어 | NS 태스크마다 Secure 쪽에 둔 스택 + PSPLIM |
| TF-M | Trusted Firmware-M | Armv8-M Secure 쪽 참조 펌웨어 (v2.3.1, LTS v2.3.0) |
| SPE / NSPE | 처리 환경 | Secure / Non-secure Processing Environment |
| SPM | Secure Partition Manager | TF-M 안의 파티션 관리 커널 |
| PSA RoT / App RoT | 신뢰 등급 | 플랫폼 보안 뿌리 파티션 / 제품별 보안 파티션 |
| 격리 레벨 1/2/3 | PSA FF-M | SPE↔NSPE / +RoT 사이 / +파티션 사이 분리 |

---

## 17. 요약 & 체크리스트

TrustZone-M은 "코드가 어느 주소에서 실행되는가"로 보안 상태를 정하는 Armv8-M의 기능이다. 주소의 속성(S / NSC / NS)은 칩 고정 IDAU와 Secure 소프트웨어가 설정하는 SAU 중 더 안전한 쪽으로 정해지고, 버스에서는 MPC/PPC가 트랜잭션을 한 번 더 거른다. NS → S는 NSC 영역의 SG veneer로만, S → NS는 BLXNS/BXNS로만 건넌다. 컴파일러(`-mcmse`)가 진입 함수의 레지스터 청소와 BXNS를, 링커가 veneer와 import library를 만든다. Secure 함수 작성자의 몫은 포인터 검사(`cmse_check_address_range`)와 복사 후 사용, 재진입 대비다. NVIC는 하나이고 ITNS로 인터럽트를 나누며, SysTick · PendSV · MPU · 스택 포인터는 세계마다 따로 있다. RTOS는 보통 NS에서 돈다. FreeRTOS ARM_CM33 포트는 태스크마다 Secure 스택을 두고 NS PendSV가 veneer로 바꿔서 Secure 호출 중 선점을 허용하며(전환 비용 2배), Zephyr + TF-M은 Secure 호출을 직렬화한다. TF-M은 SPM과 파티션으로 Secure 쪽 안에도 격리 레벨 1~3을 만든다.

- [ ] Cortex-A TrustZone과 M의 차이를 "모니터가 없다, 주소가 상태를 정한다"로 말할 수 있다
- [ ] IDAU와 SAU의 결과가 합쳐지는 표를 그리고, AN505의 `NSCCFG.CODENSC`가 왜 필요한지 설명할 수 있다
- [ ] Secure 부트가 NS를 띄우는 8단계(SAU, MPC, PPC, NSC, ITNS, SecureFault, VTOR_NS/MSP_NS, BLXNS)를 순서대로 말할 수 있다
- [ ] `-mcmse`, `cmse_nonsecure_entry`, `.gnu.sgstubs`, `--cmse-implib --out-implib`의 역할을 안다
- [ ] veneer 디스어셈블리에서 SG · B.W · 레지스터 청소 · BXNS를 찾을 수 있다
- [ ] S → NS 호출에서 BLXNS · FNC_RETURN · libgcc `__gnu_cmse_nonsecure_call`이 하는 일을 안다
- [ ] confused deputy를 예로 설명하고 `cmse_check_address_range`/`cmse_check_pointed_object` 플래그를 고를 수 있다
- [ ] SFSR 비트(INVEP, AUVIOL, INVTRAN, INVIS, INVER)를 증상에 연결할 수 있다
- [ ] SAU 위반(SecureFault)과 MPC/PPC 차단(BusFault 또는 RAZ/WI)을 구분할 수 있다
- [ ] 뱅크 레지스터 목록과 ITNS · PRIS · BFHFNMINS의 효과를 안다
- [ ] FreeRTOS `portALLOCATE_SECURE_CONTEXT`, `configENABLE_TRUSTZONE`, `configRUN_FREERTOS_SECURE_ONLY`, PendSV의 EXC_RETURN bit 6 검사를 설명할 수 있다
- [ ] TF-M의 SPE/NSPE/SPM/파티션/격리 레벨과 Zephyr `/ns` 보드 · `tfm_ns_interface_dispatch` 직렬화를 그릴 수 있다

---

## Bare-Metal 노트와의 관계

| BM ID | BM 주제 | 이 노트의 해당 절 | Bare-Metal 쪽에 남길 내용 |
|---|---|---|---|
| BM C12 | TrustZone-M: Secure / Non-secure, SAU · IDAU | §1(속성 3종, IDAU+SAU 합성 표, 예제 1 · 4 TT), §7.1(뱅크 레지스터) | 칩별 IDAU 맵 · SAU 영역 수 비교표(STM32U5, nRF5340, LPC55S69, RP2350) |
| BM C12 | NSC veneer, 보안 상태 전환 | §3(SG veneer · import library · 진입 함수 제약), §4(BLXNS · 청소), §6(잘못된 접근 실험 8가지) | 어셈블리로 직접 쓰는 최소 진입 함수와 청소 검증 체크리스트 |
| BM C12 | bare-metal 두 이미지 구성 | §2(Secure 부트 8단계, `tz.h`, 링커 스크립트 두 개, 예제 2), §0.3(`mk.sh`) | TF-M 없이 직접 쓰는 최소 Secure 이미지의 양산 체크리스트(디버그 포트 · 리셋값 불신 · SAU 잠금), 칩별 MPC/PPC 초기화 순서 |

---

## 참고 자료

- Arm, 「Armv8-M Architecture Reference Manual」 (DDI 0553) — Security Extension, SAU, SG/BXNS/BLXNS/TT, EXC_RETURN, SFSR · SFAR, AIRCR.PRIS/BFHFNMINS, NVIC_ITNS
- Arm, 「Armv8-M Security Extensions: Requirements on Development Tools」 (CMSE 규격) — `cmse_nonsecure_entry`, `cmse_nonsecure_call`, `cmse_check_address_range`, import library
- Arm, 「Arm CoreLink SSE-200 Subsystem / IoT Kit」 기술 문서와 「Application Note AN505 · AN521」 — IDAU 맵, 보안 제어 블록(NSCCFG, SECRESPCFG, `APBNSPPC0`, `APBNSPPCEXPn`), MPC/PPC
- GCC 문서 "ARM Options" (`-mcmse`) 와 GNU ld `--cmse-implib` / `--out-implib` / `--in-implib`
- QEMU 10.2 소스(v10.2.0 태그): `hw/arm/mps2-tz.c`(`an505_raminfo`, PPC 포트 표), `hw/arm/armsse.c`(`armsse_idau_check`), `hw/misc/tz-mpc.c`, `hw/misc/tz-ppc.c`, `hw/misc/iotkit-secctl.c`, `target/arm/ptw.c`(`v8m_security_lookup`), `target/arm/tcg/m_helper.c`(SFSR 설정) — https://gitlab.com/qemu-project/qemu
- QEMU 문서 "Arm MPS2 and MPS3 boards" — https://www.qemu.org/docs/master/system/arm/mps2.html
- FreeRTOS Kernel V11.3.2 소스: `portable/GCC/ARM_CM33/non_secure/port.c` · `portasm.c` · `portmacrocommon.h`, `portable/GCC/ARM_CM33/secure/secure_context.c` · `secure_context_port.c` · `secure_heap.c` · `secure_init.c` · `secure_port_macros.h`, `tasks.c`(`prvIdleTask`)
- FreeRTOS 문서: ARMv8-M(Cortex-M23 · M33 · M55 · M85) 포트와 TrustZone 지원 설명 페이지 (freertos.org, Supported devices → ARM Cortex — URL은 사이트 개편이 잦아 생략)
- Zephyr v4.4.2 소스: `arch/arm/core/cortex_m/tz/CMakeLists.txt` · `secure_entry_functions.ld`, `arch/arm/core/cortex_m/cmse/arm_core_cmse.c`, `arch/arm/include/cortex_m/tz_ns.h`, `arch/arm/core/cortex_m/thread.c`, `modules/trusted-firmware-m/interface/interface.c` · `Kconfig.tfm`, `boards/arm/mps2/mps2_an521_cpu0_ns.dts` · `_defconfig`, `west.yml`
- Zephyr 문서 "Trusted Firmware-M" — https://docs.zephyrproject.org/latest/services/tfm/index.html
- TF-M v2.3.1 소스(TF-Mv2.3.1 태그): `docs/releases/index.rst` · `2.3.0.rst` · `2.3.1.rst`, `config/config_base.cmake`, `secure_fw/include/security_defs.h`, `secure_fw/partitions/ns_agent_tz/psa_api_veneers.c`, `secure_fw/partitions/*/*.yaml`, `platform/ext/target/arm/mps2/an521/target_cfg.c` · `config.cmake` — https://git.trustedfirmware.org/TF-M/trusted-firmware-m.git
- TF-M 문서 — https://trustedfirmware-m.readthedocs.io/ (릴리스 노트 v2.2.0 등)
- TrustedFirmware.org 뉴스 "Trusted Firmware-M v2.3.0, TF-PSA-Crypto 1.1.0 & Mbed TLS 4.1.0 LTS Releases" (2026-05-13) — https://www.trustedfirmware.org/blog/
- Arm, 「PSA Firmware Framework for M (FF-M)」 — 격리 레벨 1/2/3, 파티션 모델(SFN/IPC); PSA Certified — https://www.psacertified.org/ (인증 레벨 상세는 L5)
- 관련 노트: B2(EXC_RETURN), B3(NVIC · BASEPRI), B7(v8-M 기능, `_NTZ` 포트, 그림 6), C2(컨텍스트 스위치), C10(an521 듀얼 M33), F3(스택 크기 · `-fstack-usage`), F4(MPU), F5(유저 모드 격리), L2(MCUboot), L4(신뢰 루트), L5(TF-M · PSA), P5(멀티코어 메일박스), EL C5(Cortex-A TrustZone · TF-A · OP-TEE), EL L5(secure boot 체인)
