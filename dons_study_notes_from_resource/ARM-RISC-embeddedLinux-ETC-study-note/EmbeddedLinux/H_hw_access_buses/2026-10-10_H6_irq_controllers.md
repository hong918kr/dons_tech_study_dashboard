# H6. 인터럽트 컨트롤러 (커널 내부) — GIC, irq_domain, irqchip, chained/nested IRQ, GPIO 확장기 IRQ

> **이 노트를 다 읽으면**: `irq_chip`·`irq_desc`·`irq_domain`이 각각 무엇을 맡는지 그림으로 설명할 수 있다 · DT의 `interrupts = <7 IRQ_TYPE_EDGE_RISING>`이 xlate → hwirq → virq로 바뀌는 길을 따라간다 · GICv2/GICv2m과 GICv3/ITS의 domain 계층을 `/sys/kernel/debug/irq`에서 직접 읽는다 · 2차 인터럽트 컨트롤러 드라이버를 chained(hardirq)와 nested(threaded) 두 방식으로 짜고, I2C GPIO 확장기가 왜 nested + `irq_bus_lock`이어야 하는지 커널 BUG 출력으로 증명한다
> **왜 필요한가**: "GPIO 확장기 뒤에 붙은 센서 인터럽트가 안 들어와요", "`/proc/interrupts`에 숫자가 안 올라가요", "PCIe 장치 MSI가 안 잡혀요" 같은 보드 bring-up 문제는 거의 다 **인터럽트 provider 쪽**(컨트롤러 드라이버, DT 연결, domain 계층)에서 난다. 시니어 드라이버 면접에서도 "chained vs nested handler", "irq_domain이 뭔가", "GICv3 ITS가 하는 일"은 단골이다
> **Don 기준 난이도**: NVIC·EXTI 공유 벡터·W1C 상태 레지스터·I2C 지연은 이미 몸으로 안다 / genirq의 provider API(`irq_chip` 콜백, flow handler 선택, `irq_domain_ops`), hierarchy domain, bus_lock 패턴, GICv3 ITS는 새로 배운다
> **선행 노트**: F6(consumer 쪽 — `request_threaded_irq`, GIC 경로, `/proc/interrupts` 읽기), D4(DT `interrupts`·`interrupt-parent`), F2(hardirq vs process 컨텍스트), F4(mutex vs spinlock), H4(gpiolib), H5(I2C)

---

## 0. 큰 그림 — 이게 왜 필요한가

F6은 **consumer**(소비자) 쪽이었다. 드라이버가 `platform_get_irq()`로 번호를 받고 `request_threaded_irq()`로 handler를 건다. 번호가 어디서 왔는지, 인터럽트가 왔을 때 handler까지 누가 데려다주는지는 "커널이 알아서"였다.

이 노트는 그 "알아서"를 만드는 **provider**(공급자) 쪽이다. 인터럽트 컨트롤러 드라이버가 커널에 세 가지를 등록한다.

- **`struct irq_chip`**: "이 컨트롤러의 한 줄을 mask/unmask/ack/EOI/트리거 설정하는 법". 하드웨어 레지스터를 만지는 콜백 묶음이다.
- **`struct irq_domain`**: "이 컨트롤러 안 번호(hwirq) ↔ 커널 전역 번호(virq)" 변환표 + DT 해석기(xlate).
- **부모와의 연결**: 이 컨트롤러 자체도 결국 **부모 컨트롤러의 한 줄**(예: GIC SPI 하나)에 매달려 있다. 부모 인터럽트가 왔을 때 "내 칩의 상태 레지스터를 읽어 어느 자식인지 알아내고 자식 handler를 불러 주는" 코드가 필요하다 — 이것이 **chained handler** 또는 **nested threaded handler**다.

Don이 아는 말로 하면: STM32의 `EXTI9_5_IRQHandler`는 NVIC 벡터 하나로 EXTI 핀 5~9를 받는다. ISR 안에서 `EXTI->PR`을 읽고, 켜진 비트마다 해당 콜백을 부르고, W1C로 비트를 지운다. **이게 정확히 chained handler다.** Linux는 이 "벡터 하나 → 상태 레지스터 읽기 → 자식으로 분배" 패턴을 GPIO 컨트롤러, PMIC, I2C GPIO 확장기, PCIe MSI까지 공통 프레임워크(genirq)로 일반화해서, 자식 인터럽트도 GIC 인터럽트와 **똑같이** `request_irq()`·`/proc/interrupts`·affinity·spurious 감지를 쓰게 만든다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="250" y="16" width="180" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="262" y="38" font-size="13">CPU (IRQ 예외 1개)</text>
  <text x="262" y="56" font-size="12">gic_handle_irq</text>
  <rect x="200" y="100" width="280" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="212" y="122" font-size="13">GIC (root irqchip)</text>
  <text x="212" y="140" font-size="12">SGI · PPI · SPI · (v3) LPI</text>
  <line x1="340" y1="100" x2="340" y2="69" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="10" y="200" width="200" height="66" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="20" y="222" font-size="13">SoC GPIO 컨트롤러</text>
  <text x="20" y="240" font-size="12">MMIO, 핀 32개 → SPI 1개</text>
  <text x="20" y="258" font-size="12">chained (hardirq)</text>
  <rect x="240" y="200" width="200" height="66" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="250" y="222" font-size="13">UART · I2C 컨트롤러</text>
  <text x="250" y="240" font-size="12">SPI 직결 (자식 없음)</text>
  <text x="250" y="258" font-size="12">handle_fasteoi_irq</text>
  <rect x="470" y="200" width="200" height="66" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="480" y="222" font-size="13">PCIe MSI (v3: ITS)</text>
  <text x="480" y="240" font-size="12">메모리 쓰기 → LPI</text>
  <text x="480" y="258" font-size="12">hierarchy domain</text>
  <line x1="110" y1="200" x2="250" y2="152" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="340" y1="200" x2="340" y2="153" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="570" y1="200" x2="430" y2="152" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="10" y="310" width="200" height="66" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="20" y="332" font-size="13">I2C GPIO 확장기</text>
  <text x="20" y="350" font-size="12">PCA9555: INT → GPIO 핀</text>
  <text x="20" y="368" font-size="12">nested (threaded)</text>
  <line x1="110" y1="310" x2="110" y2="269" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="240" y="310" width="200" height="66" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="250" y="332" font-size="13">센서 (IMU · 버튼)</text>
  <text x="250" y="350" font-size="12">확장기 핀 3에 INT 연결</text>
  <text x="250" y="368" font-size="12">consumer 드라이버</text>
  <line x1="240" y1="343" x2="213" y2="343" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="470" y="320" font-size="12">화살표 = 인터럽트가 올라가는 방향</text>
  <text x="470" y="340" font-size="12">각 상자 = irq_domain 하나</text>
  <text x="470" y="360" font-size="12">자식 번호는 부모와 무관</text>
</svg>
```
그림 1 — 실제 보드의 인터럽트 트리. 센서 INT가 CPU까지 가려면 확장기(I2C) → GPIO 컨트롤러(MMIO) → GIC 세 단계의 컨트롤러를 거친다. 각 단계가 자기 `irq_chip` + `irq_domain`을 가진 provider다.

| Cortex-M / bare-metal (Don이 아는 것) | Linux provider 쪽 | 이 노트 절 |
|---|---|---|
| NVIC 레지스터 직접 쓰기 (`ISER`, `ICER`, `ICPR`) | `irq_chip` 콜백 (`irq_unmask`, `irq_mask`, `irq_ack`) | §1 |
| EXTI 공유 벡터 + `EXTI->PR` 읽고 분배 | chained handler (`irq_set_chained_handler_and_data`) | §5, §6 |
| I2C 확장기 INT → 태스크에서 I2C 읽고 분배 | nested threaded handler (`handle_nested_irq`) | §7 |
| `IRQn` enum (컴파일 타임 상수) | virq (런타임 할당), hwirq ↔ virq 변환 = `irq_domain` | §3 |
| 엣지/레벨 처리를 ISR 안에서 손으로 | flow handler (`handle_level_irq`, `handle_edge_irq` …) | §2 |
| mask 레지스터를 I2C로 쓰는 건 태스크에서만 | `irq_bus_lock` / `irq_bus_sync_unlock` | §7.2 |

---

## 1. provider의 세 구조체 — `irq_chip`, `irq_desc`, `irq_domain`

### 1.1 직관

F6 §11에서 consumer 입장으로 본 그림을 provider 입장으로 다시 그리면 이렇다. 핵심은 **"virq 하나 = `irq_desc` 하나"**이고, 그 `irq_desc` 안의 `irq_data`가 "나를 만든 domain, 그 안에서의 hwirq, 나를 조작할 `irq_chip`"을 가리킨다는 점이다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="16" width="300" height="200" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="20" y="38" font-size="13">struct irq_desc (virq 30)</text>
  <text x="20" y="62" font-size="12">handle_irq = handle_edge_irq</text>
  <text x="20" y="82" font-size="12">  ← provider가 고른 flow handler</text>
  <text x="20" y="104" font-size="12">action → irqaction "h6c7"</text>
  <text x="20" y="124" font-size="12">  ← consumer의 request_irq</text>
  <text x="20" y="146" font-size="12">kstat_irqs, depth, istate, lock</text>
  <rect x="20" y="158" width="280" height="50" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="30" y="178" font-size="12">irq_data: irq=30, hwirq=7</text>
  <text x="30" y="198" font-size="12">domain, chip, chip_data, parent_data</text>
  <rect x="370" y="16" width="300" height="140" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="380" y="38" font-size="13">struct irq_chip "h6_mux"</text>
  <text x="380" y="62" font-size="12">irq_mask / irq_unmask</text>
  <text x="380" y="82" font-size="12">irq_ack / irq_eoi</text>
  <text x="380" y="102" font-size="12">irq_set_type / irq_set_affinity</text>
  <text x="380" y="122" font-size="12">irq_bus_lock / irq_bus_sync_unlock</text>
  <text x="380" y="142" font-size="12">컨트롤러 종류마다 하나 (const)</text>
  <rect x="370" y="190" width="300" height="140" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="380" y="212" font-size="13">struct irq_domain ":h6-mux"</text>
  <text x="380" y="236" font-size="12">ops: xlate (DT 셀 해석), map</text>
  <text x="380" y="256" font-size="12">linear revmap[8]: hwirq → virq</text>
  <text x="380" y="276" font-size="12">fwnode = DT 노드 h6-mux</text>
  <text x="380" y="296" font-size="12">parent (hierarchy일 때만)</text>
  <text x="380" y="316" font-size="12">컨트롤러 인스턴스마다 하나</text>
  <rect x="10" y="260" width="300" height="70" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="20" y="282" font-size="13">부모 irq_desc (virq 22, GIC)</text>
  <text x="20" y="302" font-size="12">handle_irq = h6_chained</text>
  <text x="20" y="320" font-size="12">  ← chained handler 가 대신 앉음</text>
  <line x1="300" y1="178" x2="367" y2="90" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="300" y1="198" x2="367" y2="250" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="160" y1="260" x2="160" y2="219" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="10" y="360" font-size="12">부모(GIC SPI)가 오면 부모 desc의 handle_irq = h6_chained 가 돌고,</text>
  <text x="10" y="380" font-size="12">그 안에서 domain으로 hwirq 7 → virq 30을 찾아 자식 desc의 flow handler를 부른다</text>
</svg>
```
그림 2 — provider가 만드는 것들. 숫자(virq 22·30, hwirq 7)는 §6 예제 7의 실제 값이다. `irq_chip`은 "어떻게 조작하나"(코드), `irq_domain`은 "번호를 어떻게 바꾸나"(표), `irq_desc`는 "이 번호 하나의 상태"(데이터)다.

### 1.2 `struct irq_chip` 콜백 — 누가, 언제 부르나

`irq_chip`의 콜백은 **드라이버가 직접 부르지 않는다**. genirq 코어와 flow handler가 상황에 맞춰 부른다. 인자는 전부 `struct irq_data *d`이고, 칩 안 번호는 `irqd_to_hwirq(d)`, 드라이버 private 데이터는 `irq_data_get_irq_chip_data(d)`로 꺼낸다.

| 콜백 | 하드웨어에서 하는 일 | 누가 부르나 (대표) | 컨텍스트 |
|---|---|---|---|
| `irq_unmask` | MASK 비트 해제 (`ISER` 같은 것) | `request_irq` → `irq_startup`, `enable_irq`, level flow 끝 | 원자적 (desc->lock, IRQ off) |
| `irq_mask` | MASK 비트 설정 | `disable_irq`, `handle_level_irq` 시작, ONESHOT | 원자적 |
| `irq_ack` | pending/status 비트 지우기 (W1C) | `handle_edge_irq`·`handle_level_irq` 시작 | hardirq |
| `irq_mask_ack` | 위 둘을 한 번에 (선택) | `handle_level_irq` (있으면 우선) | hardirq |
| `irq_eoi` | "처리 끝" 통지 (GIC EOIR) | `handle_fasteoi_irq` 끝, `chained_irq_exit` | hardirq |
| `irq_set_type` | 엣지/레벨·극성 레지스터 설정 | DT 매핑 때(`irq_create_fwspec_mapping`), `irq_set_irq_type` | 원자적 |
| `irq_set_affinity` | 대상 CPU 라우팅 (GIC `ITARGETSR`/`IROUTER`) | `/proc/irq/N/smp_affinity` 쓰기 | 원자적 |
| `irq_set_wake` | suspend 중 깨우기 소스 지정 | `enable_irq_wake` | 원자적 |
| `irq_bus_lock` | (하드웨어 접근 없음) 느린 버스용 mutex 잡기 | 위 "원자적" 콜백들을 부르기 **전** | process (sleep 가능) |
| `irq_bus_sync_unlock` | 모아 둔 mask/type 변경을 버스로 한 번에 쓰기 | 위 콜백들을 부른 **후** | process (sleep 가능) |
| `irq_print_chip` | `/proc/interrupts`의 칩 이름 칸 출력 (선택) | `show_interrupts` | process |

"원자적"이라고 쓴 칸이 중요하다. `irq_mask`·`irq_unmask`·`irq_set_type`은 `desc->lock`(raw spinlock)을 잡고 IRQ를 끈 채로 불린다. 그래서 **I2C 확장기의 `irq_mask`에서 I2C 전송을 할 수 없다**. 그 문제를 푸는 게 `irq_bus_lock`/`irq_bus_sync_unlock` 쌍이다(§7.2).

`irq_chip`은 요즘 `const`로 선언한다(gpiolib는 `IRQCHIP_IMMUTABLE` 플래그 + `gpio_irq_chip_set_chip()`를 쓰라고 하고, 아니면 "not an immutable chip, please consider fixing it!" 경고를 찍는다 — 랩 소스 `gpiolib.c`). 예전 드라이버는 `irq_chip`을 런타임에 고쳐 썼는데, 여러 인스턴스가 하나를 공유하며 꼬이는 문제 때문에 바뀌었다.

### 1.3 `irq_desc`·`irq_data` — provider가 채우는 칸

provider는 domain의 `.map`(또는 hierarchy면 `.alloc`) 콜백 안에서 새 virq의 `irq_desc`를 꾸민다.

```c
irq_set_chip_and_handler(virq, &my_chip, handle_level_irq);	/* chip + flow handler */
irq_set_chip_data(virq, priv);					/* 콜백에서 꺼낼 private */
irq_set_nested_thread(virq, true);				/* (nested 칩만) */
irq_set_noprobe(virq);						/* 옛 autoprobe 대상에서 제외 */
```

부모 쪽 virq에는 `irq_set_chained_handler_and_data(parent_virq, my_demux, priv)`를 한다. 이 호출은 부모 `irq_desc`의 `handle_irq`를 **flow handler 대신 내 함수로 갈아끼우고**, 부모를 `_IRQ_NOREQUEST`(다른 드라이버가 `request_irq` 못 함)·`_IRQ_NOPROBE`·`_IRQ_NOTHREAD`로 표시한 뒤 바로 켠다(startup). §6 예제 7의 debugfs 출력에서 이 세 플래그를 실제로 본다.

---

## 2. flow handler — 트리거마다 `irq_chip` 콜백을 부르는 순서

F6 §4.1에서 flow handler 이름만 봤다. provider 입장에서는 **"내 칩에 어떤 flow handler를 붙이느냐"**가 곧 **"어떤 콜백을 어떤 순서로 불러 달라"**는 계약이다. `kernel/irq/chip.c`(랩 소스 6.18.55) 기준으로 정리하면:

| flow handler | 부르는 순서 (요약) | 어울리는 하드웨어 |
|---|---|---|
| `handle_level_irq` | `mask_ack_irq`(= `irq_mask_ack` 또는 `irq_mask` + `irq_ack`) → action들 → (ONESHOT 아니면) `irq_unmask` | 레벨 트리거 GPIO. 처리 중 다시 들어오지 않게 mask가 필수 |
| `handle_edge_irq` | `irq_ack` → action들 → 처리 중에 새 엣지가 또 오면(`IRQS_PENDING`) 루프 | 엣지 GPIO. mask하지 않으므로 처리 중 엣지를 잃지 않는다 |
| `handle_fasteoi_irq` | (ONESHOT이면 `irq_mask`) → action들 → `irq_eoi` | GIC처럼 하드웨어가 ack/우선순위를 알아서 하고 EOI만 받는 칩 |
| `handle_simple_irq` | 콜백 호출 없음 → action들 | 드라이버가 상태를 이미 지운 디멀티플렉스 자식 |
| `handle_edge_eoi_irq` | 엣지 + EOI (드묾) | 일부 PowerPC/XICS |
| `handle_percpu_devid_irq` | CPU별 dev_id로 action → `irq_eoi` | GIC PPI(타이머 등) |
| `handle_nested_irq` | **flow handler가 아님** — 부모 스레드가 직접 부르는 함수. `might_sleep()` → 자식 action의 `thread_fn` 호출 | I2C/SPI 확장기 자식 |

`handle_nested_irq`는 `irq_desc->handle_irq`에 꽂지 않는다는 점이 함정이다. nested 자식의 `handle_irq`에는 보통 `handle_simple_irq`(또는 아무거나)를 넣고, 실제 분배는 부모의 thread 함수가 `handle_nested_irq(virq)`를 **직접 호출**해서 한다. 랩 소스의 본체는 이렇다 (발췌):

```c
void handle_nested_irq(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);
	...
	might_sleep();				/* hardirq에서 부르면 바로 BUG */
	scoped_guard(raw_spinlock_irq, &desc->lock) {
		...
		kstat_incr_irqs_this_cpu(desc);	/* /proc/interrupts 카운트 */
		atomic_inc(&desc->threads_active);
	}
	action_ret = IRQ_NONE;
	for_each_action_of_desc(desc, action)
		action_ret |= action->thread_fn(action->irq, action->dev_id);	/* hard handler 아님! */
	...
	note_interrupt(desc, action_ret);
	wake_threads_waitq(desc);
}
```

자식 consumer의 **`thread_fn`을 부모 스레드 안에서 그대로 호출**한다. 자식마다 `irq/N-xxx` 스레드를 따로 만들지 않는다. 그래서 nested 자식에 hard handler만 등록하면(`request_irq`) 부를 함수가 없어서 커널이 거부한다 — §7.3 예제 9에서 `-EINVAL`을 실제로 받는다.

> 정리: flow handler 선택 규칙 — "하드웨어가 EOI만 원하면 fasteoi, 레벨이면 level(mask 필수), 엣지면 edge(ack 먼저), 내가 상태를 다 지웠으면 simple, 슬립이 필요한 자식이면 nested". `irq_set_type` 콜백 안에서 `irq_set_handler_locked(d, handle_edge_irq)`처럼 **트리거가 정해질 때 flow handler를 갈아끼우는** 것이 GPIO 드라이버의 관용구다(pl061, 우리 h6_mux 모두).

---

## 3. `irq_domain` — 번호표와 DT 해석기

### 3.1 domain 종류

| 종류 | 생성 API (6.18) | revmap | 쓰는 곳 |
|---|---|---|---|
| linear | `irq_domain_create_linear(fwnode, size, ops, data)` | 배열 `revmap[size]` | hwirq가 0..N-1로 촘촘한 칩. GPIO, PMIC, 우리 h6_mux |
| tree | `irq_domain_create_tree(fwnode, ops, data)` | radix tree | hwirq가 크고 드문드문 (예: 큰 번호 공간) |
| nomap | `irq_domain_create_nomap(...)` | 없음 (hwirq = virq) | 일부 PowerPC 레거시 |
| simple | `irq_domain_create_simple(fwnode, size, first_irq, ops, data)` | linear + (선택) 미리 할당 | 레거시 보드 파일 호환 |
| hierarchy | `irq_domain_create_hierarchy(parent, flags, size, fwnode, ops, data)` | linear 또는 tree + **parent 포인터** | GIC ← ITS ← PCI-MSI, GIC ← GPIO (hierarchical GPIO) |

이름 주의: 옛 `irq_domain_add_linear(of_node, …)`는 6.18 헤더에 아직 있지만 `fwnode` 기반 `irq_domain_create_*`가 표준이다. 그리고 GICv3 domain은 `size: 0`으로 나오는데(§4 예제 2), size 0 = linear 배열 없이 tree를 쓴다는 뜻이다.

**계층(hierarchy)과 cascade(chained)의 차이**가 면접 포인트다.

- **cascade (chained/nested)**: 자식 domain과 부모 domain이 **따로 논다**. 부모 쪽에는 virq가 하나(부모 SPI) 따로 있고, 자식 virq 8개가 따로 있다. 자식 인터럽트 하나가 오면 `irq_desc`가 **두 번** 돈다(부모 desc → 자식 desc). §6 ftrace에서 `generic_handle_domain_irq`가 두 번 중첩되는 것이 그 증거다.
- **hierarchy**: 자식 virq **하나**가 `irq_data` 사슬(`parent_data`)로 부모 domain들의 `irq_data`를 함께 가진다. 인터럽트가 오면 **root(GIC)에서 바로** 그 virq의 desc가 돈다(디멀티플렉스 단계 없음). 자식 domain은 "번호 할당과 설정 전달(mask → 부모 mask, set_type → 부모 set_type)"만 맡는다. 1:1로 핀이 GIC SPI에 매핑되는 GPIO 컨트롤러, MSI가 이 구조다.

### 3.2 DT 해석: `#interrupt-cells`와 `xlate`

consumer 노드의 `interrupts`는 **부모 컨트롤러가 정한 셀 개수**(`#interrupt-cells`)만큼의 숫자 묶음이다. 그 숫자의 뜻은 **부모 드라이버의 `xlate`(또는 `translate`) 콜백**이 정한다.

| 컨트롤러 | `#interrupt-cells` | 셀의 뜻 | xlate |
|---|---|---|---|
| GIC (v2/v3) | 3 | `<종류(0=SPI,1=PPI) 번호 플래그>` | `gic_irq_domain_translate` — SPI면 +32, PPI면 +16 |
| 대부분 GPIO | 2 | `<핀 트리거>` | `irq_domain_xlate_twocell` (또는 gpiolib 기본) |
| 우리 h6-mux | 2 | `<hwirq 트리거>` | `irq_domain_xlate_twocell` |
| 단순 칩 | 1 | `<hwirq>` | `irq_domain_xlate_onecell` |

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="16" width="300" height="66" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="20" y="38" font-size="13">DT: h6-cons 노드</text>
  <text x="20" y="58" font-size="12">interrupt-parent = &lt;&amp;h6mux&gt;</text>
  <text x="20" y="74" font-size="12">interrupts = ... &lt;7 IRQ_TYPE_EDGE_RISING&gt;</text>
  <rect x="10" y="110" width="300" height="66" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="20" y="132" font-size="13">of_irq_parse_one → of_phandle_args</text>
  <text x="20" y="152" font-size="12">np = h6-mux, args_count = 2</text>
  <text x="20" y="170" font-size="12">args = {7, 1}</text>
  <rect x="10" y="204" width="300" height="66" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="20" y="226" font-size="13">irq_find_matching_fwspec → :h6-mux</text>
  <text x="20" y="246" font-size="12">ops->xlate = irq_domain_xlate_twocell</text>
  <text x="20" y="264" font-size="12">→ hwirq 7, type EDGE_RISING</text>
  <rect x="370" y="110" width="300" height="160" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="380" y="132" font-size="13">irq_create_fwspec_mapping</text>
  <text x="380" y="156" font-size="12">1. irq_find_mapping(dom, 7) = 0 ?</text>
  <text x="380" y="176" font-size="12">2. 빈 virq 30 할당 (irq_desc 생성)</text>
  <text x="380" y="196" font-size="12">3. ops->map(dom, 30, 7)</text>
  <text x="380" y="214" font-size="12">   chip = h6_mux, handle_level_irq</text>
  <text x="380" y="234" font-size="12">4. irq_set_irq_type(30, EDGE)</text>
  <text x="380" y="252" font-size="12">   → h6_set_type → handle_edge_irq</text>
  <rect x="370" y="16" width="300" height="66" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="380" y="38" font-size="13">consumer: platform_get_irq(pdev, 7)</text>
  <text x="380" y="58" font-size="12">→ 30 을 돌려받는다</text>
  <text x="380" y="74" font-size="12">(domain 없으면 -EPROBE_DEFER)</text>
  <line x1="160" y1="82" x2="160" y2="107" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="160" y1="176" x2="160" y2="201" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="310" y1="237" x2="367" y2="200" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="520" y1="110" x2="520" y2="85" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="10" y="306" font-size="12">이미 매핑이 있으면(1번이 0이 아니면) 같은 virq를 재사용한다.</text>
  <text x="10" y="326" font-size="12">예제 7: probe에서 irq_create_mapping(dom, 5) = 23 을 먼저 만들었더니</text>
  <text x="10" y="346" font-size="12">DT의 hwirq 5 소비자도 virq 23 을 받았다 (나머지는 24부터 새로 할당)</text>
</svg>
```
그림 3 — DT 인터럽트 지정자 → `xlate` → hwirq → virq. consumer의 `platform_get_irq()`가 처음 불리는 순간 이 매핑이 생긴다(`of_irq_get` → `irq_create_of_mapping` → `irq_create_fwspec_mapping`). 숫자는 §6 예제 7의 실제 값.

### 3.3 매핑 API 세 개

| API | 하는 일 | 언제 |
|---|---|---|
| `irq_create_mapping(dom, hwirq)` | hwirq에 virq를 할당하고 `.map` 호출. 이미 있으면 그 virq 반환 | DT 없이 provider가 직접 자식을 만들 때 (MFD 등) |
| `irq_find_mapping(dom, hwirq)` | 매핑 조회만. 없으면 0 | **디멀티플렉스 handler 안**에서 hwirq → virq (nested 경로) |
| `generic_handle_domain_irq(dom, hwirq)` | 조회 + 그 desc의 flow handler 실행 | chained handler 안 (hardirq) |
| `irq_dispose_mapping(virq)` | 매핑 해제 | 드라이버 제거 |

`generic_handle_domain_irq()`는 내부에서 `__irq_resolve_mapping()`으로 찾고 `handle_irq_desc()`를 부른다. 6.x 이전 드라이버에서는 `generic_handle_irq(irq_find_mapping(dom, hwirq))` 두 줄로 썼다.

---

## 4. GICv2 vs GICv3 — 그리고 MSI가 오는 길 (v2m vs ITS)

### 4.1 INTID 범위와 구성 요소

| 항목 | GICv2 (QEMU virt 기본) | GICv3 (`gic-version=3`) |
|---|---|---|
| SGI | 0–15 | 0–15 |
| PPI | 16–31 | 16–31 (+ Extended PPI 1056–1119, v3.1) |
| SPI | 32–1019 | 32–1019 (+ Extended SPI 4096–5119, v3.1) |
| LPI | 없음 | 8192– (메시지 기반, 설정은 **메모리 테이블**) |
| CPU 쪽 창구 | GICC (MMIO, CPU interface) | ICC 시스템 레지스터 (`ICC_IAR1_EL1` 등) |
| CPU별 장치 | 없음 (GICD가 SGI/PPI까지 bank로 처리) | **Redistributor (GICR)** — CPU마다 하나. SGI/PPI 설정과 LPI pending 테이블 |
| CPU 수 한계 | 8 (`ITARGETSR`가 8비트) | 사실상 무제한 (`IROUTER`에 affinity 값) |
| MSI | 없음 (QEMU는 **GICv2m** 프레임을 붙여 MSI → SPI 변환) | **ITS**가 (DeviceID, EventID) → LPI 번역 |

**ITS (Interrupt Translation Service)**: PCIe 장치가 MSI로 "주소 X에 값 Y를 쓰기"를 하면, 그 주소가 ITS의 `GITS_TRANSLATER` 레지스터다. ITS는 **누가 썼는지(DeviceID — PCIe Requester ID에서 옴)**와 **쓴 값(EventID)**을 조합해 메모리에 있는 Device Table → Interrupt Translation Table을 찾아 **LPI 번호와 대상 Redistributor(collection)**를 정한다. 그래서 장치마다 EventID가 0부터 시작해도 겹치지 않고, 커널은 MSI마다 SPI를 소모하지 않는다. 테이블은 커널이 부팅 때 메모리에 할당한다(예제 3의 "allocated 8192 Devices").

**GICv2m**: v2에는 LPI가 없어서, MSI 쓰기 주소(`MSI_SETSPI_NS`)에 **SPI 번호를 값으로** 쓰면 그 SPI가 pending 되는 작은 프레임을 둔다. MSI 하나 = SPI 하나다. QEMU virt에서는 SPI 80–143 64개가 이 용도로 예약된다(예제 3).

### 4.2 예제 1·2 — 같은 장치, 두 가지 GIC에서 domain 계층 보기

무엇을 확인하나: `virtio-rng-pci` 하나를 붙여 GICv2와 GICv3+ITS로 각각 부팅하고, `/proc/interrupts`, `/sys/kernel/debug/irq/domains/`, 첫 MSI의 `/sys/kernel/debug/irq/irqs/N`을 비교한다. (랩 디버그 커널 `6.18.55-dbg`, `CONFIG_GENERIC_IRQ_DEBUGFS=y`. Lima VM 호스트 커널은 `7.0.0-38-generic`이지만 이 노트의 모든 실행은 QEMU 게스트 안이다.)

```sh
#!/bin/sh
# g_hier.sh — IRQ domain 계층 관찰 (GICv2/v3 공통)
D=/sys/kernel/debug/irq
uname -r
echo "=== /proc/interrupts ==="; cat /proc/interrupts
echo "=== domains ==="; ls $D/domains
for f in $D/domains/*; do echo "--- ${f##*/}"; cat "$f"; done
U=$(grep pl011 /proc/interrupts | cut -d: -f1 | tr -d ' ')
echo "=== irqs/$U (uart) ==="; cat $D/irqs/$U
M=$(grep -i -E "virtio0-|PCI-MSI" /proc/interrupts | head -1 | cut -d: -f1 | tr -d ' ')
echo "=== irqs/$M (first MSI) ==="; cat $D/irqs/$M
```

```sh
#!/bin/sh
# boot_hier.sh v2|v3 [스크립트] — GIC 버전별로 부팅 (VM 안에서 실행)
set -e
W=/var/tmp/H6; S=/Users/donh/.elinux_scratch/H6
rm -rf $W/rh && mkdir -p $W/rh && cp -a /var/lab/build/rootfs/. $W/rh/ && cp $S/g_hier.sh $S/g_gicmsg.sh $W/rh/
(cd $W/rh && find . | cpio -o -H newc --quiet | gzip) > $W/ih.gz
if [ "$1" = v3 ]; then M="virt,gic-version=3,its=on"; else M="virt,gic-version=2"; fi
timeout 120 qemu-system-aarch64 -machine $M -cpu cortex-a57 -smp 2 -m 512M -nographic \
  -device virtio-rng-pci -kernel /var/lab/out/Image-dbg -initrd $W/ih.gz \
  -append "console=ttyAMA0 quiet LAB_CMD=sh~/${2:-g_hier.sh}"
```

GICv2 (`sh boot_hier.sh v2`) 출력에서 `/proc/interrupts`:

```text
lab initramfs: 6.18.55-dbg up 4.09s
6.18.55-dbg
=== /proc/interrupts ===
           CPU0       CPU1       
 11:        875        922 GIC-0  27 Level     arch_timer
 13:          0          0 GIC-0  33 Level     uart-pl011
 17:          0          0 GICv2m-PCI-MSIX-0000:00:02.0   0 Edge      virtio1-config
 18:          3          0 GICv2m-PCI-MSIX-0000:00:02.0   1 Edge      virtio1-input
 19:          0          0 GICv2m-PCI-MSIX-0000:00:01.0   0 Edge      virtio0-config
 20:          0          0 GICv2m-PCI-MSIX-0000:00:01.0   1 Edge      virtio0-input.0
 21:          0          0 GICv2m-PCI-MSIX-0000:00:01.0   2 Edge      virtio0-output.0
 22:          0          0 GIC-0  34 Level     rtc-pl031
 23:          0          0 GIC-0  23 Level     arm-pmu
 24:          0          0 9030000.pl061   3 Edge      GPIO Key Poweroff
IPI0:        24         50       Rescheduling interrupts
...
```

(IPI1–7·Err 줄을 `...`로 생략했다.)

GICv2의 첫 MSI(virq 17)의 debugfs — **hierarchy 사슬**이 들여쓰기로 보인다:

```text
=== irqs/17 (first MSI) ===
handler:  handle_fasteoi_irq
device:   0000:00:02.0
...
domain:  GICv2m-PCI-MSIX-0000:00:02.0-12
 hwirq:   0x0
 chip:    GICv2m-PCI-MSIX-0000:00:02.0
  flags:   0x20
             IRQCHIP_ONESHOT_SAFE

  address_hi: 0x00000000
  address_lo: 0x08020040
  msg_data:   0x00000050
 parent:
    domain:  :intc@8000000:v2m@8020000-5
     hwirq:   0x50
     chip:    GICv2m
      flags:   0x0
     parent:
        domain:  :intc@8000000
         hwirq:   0x50
         chip:    GIC-0
          flags:   0x15
                     IRQCHIP_SET_TYPE_MASKED
                     IRQCHIP_MASK_ON_SUSPEND
                     IRQCHIP_SKIP_SET_WAKE
```

(`status`/`dstate` 줄 일부를 `...`로 생략했다.) GICv3+ITS (`sh boot_hier.sh v3`)의 같은 부분:

```text
 11:       1070       1150    GICv3  27 Level     arch_timer
 13:          0          0    GICv3  33 Level     uart-pl011
 17:          0          0 ITS-PCI-MSIX-0000:00:02.0   0 Edge      virtio1-config
 18:          0          3 ITS-PCI-MSIX-0000:00:02.0   1 Edge      virtio1-input
...
=== irqs/17 (first MSI) ===
...
domain:  ITS-PCI-MSIX-0000:00:02.0-12
 hwirq:   0x0
 chip:    ITS-PCI-MSIX-0000:00:02.0
  flags:   0x20
             IRQCHIP_ONESHOT_SAFE

  address_hi: 0x00000000
  address_lo: 0x08090040
  msg_data:   0x00000000
 parent:
    domain:  :intc@8000000:its@8080000-5
     hwirq:   0x2000
     chip:    ITS
      flags:   0x0
     parent:
        domain:  :intc@8000000-1
         hwirq:   0x2000
         chip:    GICv3
          flags:   0x15
                     IRQCHIP_SET_TYPE_MASKED
                     IRQCHIP_MASK_ON_SUSPEND
                     IRQCHIP_SKIP_SET_WAKE
```

domain 목록 비교 (v3쪽 `ITS` 부모 domain 설명 일부):

```text
=== domains === (v2)
:intc@8000000                    GICv2m-PCI-MSIX-0000:00:01.0-12
:intc@8000000:v2m@8020000-5      GICv2m-PCI-MSIX-0000:00:02.0-12
:pl061@9030000                   default
=== domains === (v3)
:intc@8000000-1               ITS-PCI-MSIX-0000:00:01.0-12
:intc@8000000:its@8080000-5   ITS-PCI-MSIX-0000:00:02.0-12
:intc@8000000:its@8080000-7   default
:pl061@9030000
--- :intc@8000000:its@8080000-5   (v3)
name:   :intc@8000000:its@8080000-5
 size:   0
 mapped: 5
 flags:  0x00000923
            IRQ_DOMAIN_FLAG_HIERARCHY
            IRQ_DOMAIN_NAME_ALLOCATED
            IRQ_DOMAIN_FLAG_ISOLATED_MSI
            IRQ_DOMAIN_FLAG_MSI_PARENT
 parent: :intc@8000000-1
    ...
--- :pl061@9030000
name:   :pl061@9030000
 size:   8
 mapped: 1
 flags:  0x00000002
            IRQ_DOMAIN_NAME_ALLOCATED
```

(두 부팅의 출력에서 비교할 줄만 모았고, 생략한 줄은 `...`로 표시했다. `=== domains === (v2)` 같은 꼬리표만 붙였고 나머지는 그대로다.)

출력에서 볼 것:

- **MSI 하나의 virq(17)가 domain 세 개에 걸친 `irq_data` 사슬**을 가진다: PCI-MSIX domain(hwirq 0 = 장치 안 MSI-X 벡터 번호) → v2m 또는 ITS domain → GIC domain. 이것이 hierarchy domain이다. 디멀티플렉스 단계가 없으므로 인터럽트가 오면 GIC에서 바로 virq 17의 desc가 돈다.
- **v2**: `address_lo 0x08020040` = v2m 프레임(0x08020000) + 0x40(`MSI_SETSPI_NS`), `msg_data 0x50` = 80 → GIC hwirq 0x50 = INTID 80 = **SPI 48**. 장치가 SPI 번호를 직접 쓴다.
- **v3**: `address_lo 0x08090040` = ITS 기준 주소 0x08080000 + 0x10040(`GITS_TRANSLATER`), `msg_data 0` = **EventID 0**. GIC hwirq 0x2000 = 8192 = **첫 LPI**. 두 장치(00:01.0, 00:02.0)의 EventID는 각각 0부터 시작하지만 ITS가 DeviceID로 구분해 서로 다른 LPI로 번역한다.
- `IRQ_DOMAIN_FLAG_ISOLATED_MSI`는 v3 ITS에만 있다. 장치 ID로 MSI를 구분하므로 한 장치가 자기에게 할당되지 않은 인터럽트를 울릴 수 없다는 뜻이다 — 랩 소스 `kernel/irq/msi.c`의 `msi_device_has_isolated_msi()` 주석이 VFIO(유저스페이스·VM에 장치 할당) 보안을 위한 것이라고 설명한다.
- `:pl061@9030000`은 `size 8`, 부모 표시 없음 — **hierarchy가 아니라 cascade(chained)**인 GPIO 컨트롤러다. §9에서 소스를 본다.
- GICv3의 `/proc/interrupts` 칩 이름은 `GICv3`, v2는 `GIC-0`. GIC domain 이름이 v3에서 `size: 0`(tree)이다.

### 4.3 예제 3 — 부팅 로그의 GIC 초기화

```sh
#!/bin/sh
# g_gicmsg.sh — 부팅 로그의 GIC/ITS 초기화 줄
dmesg | grep -i -E "gic|its\b|its@|redistrib|v2m|LPI"
```

```text
##### v2
[    0.000000] Root IRQ handler: gic_handle_irq
[    0.000000] GICv2m: range[mem 0x08020000-0x08020fff], SPI[80:143]
##### v3
[    0.000000] CPU features: detected: GICv3 CPU interface
[    0.000000] GICv3: 256 SPIs implemented
[    0.000000] GICv3: 0 Extended SPIs implemented
[    0.000000] Root IRQ handler: gic_handle_irq
[    0.000000] GICv3: GICv3 features: 16 PPIs
[    0.000000] GICv3: GICD_CTLR.DS=1, SCR_EL3.FIQ=0
[    0.000000] GICv3: CPU0: found redistributor 0 region 0:0x00000000080a0000
[    0.000000] ITS [mem 0x08080000-0x0809ffff]
[    0.000000] ITS@0x0000000008080000: allocated 8192 Devices @44620000 (indirect, esz 8, psz 64K, shr 1)
[    0.000000] ITS@0x0000000008080000: allocated 8192 Interrupt Collections @44630000 (flat, esz 8, psz 64K, shr 1)
[    0.000000] GICv3: using LPI property table @0x0000000044640000
[    0.000000] GICv3: CPU0: using allocated LPI pending table @0x0000000044650000
[    0.260694] fsl-mc MSI: its@8080000 domain created
[    0.314320] GICv3: CPU1: found redistributor 1 region 0:0x00000000080c0000
[    0.314888] GICv3: CPU1: using allocated LPI pending table @0x0000000044660000
```

(grep 패턴에 걸린 커널 커맨드라인·`sched_clock`·`pci-host-generic` 줄은 GIC와 무관해서 지웠다.) 출력에서 볼 것: v3는 **CPU마다 redistributor**(0x080a0000, 0x080c0000 — 0x20000 간격)를 찾고, LPI용 **property table(전체 공용, 우선순위·enable)**과 **pending table(CPU별)**을 DRAM에 할당한다. LPI 설정이 레지스터가 아니라 메모리에 있다는 것이 v3의 큰 변화다. v2m은 "SPI 80–143을 MSI용으로 쓴다"고 알린다.

> **함정 (F6과 연결)**: QEMU virt GICv2에서 SPI 48–111(INTID 80–143)은 v2m MSI 풀이다. F6은 데모 장치를 `GIC_SPI 80`(INTID 112)에 두었는데 이 범위 안이다 — MSI가 적어서 충돌이 안 났을 뿐이다. 이 노트는 범위 밖인 `GIC_SPI 120`(INTID 152)을 쓴다. 실제 SoC에서도 "아무도 안 쓰는 SPI"는 데이터시트의 SPI 맵과 MSI 프레임 범위를 둘 다 보고 고른다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="20" y="22" font-size="13">GICv2 + v2m</text>
  <text x="360" y="22" font-size="13">GICv3 + ITS</text>
  <rect x="10" y="36" width="310" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="20" y="56" font-size="12">PCI-MSIX domain (장치별, hwirq 0..)</text>
  <text x="20" y="74" font-size="12">MSI 쓰기: addr 0x08020040, data 80</text>
  <rect x="10" y="116" width="310" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="20" y="136" font-size="12">v2m domain: data = SPI 번호</text>
  <text x="20" y="154" font-size="12">MSI 1개 = SPI 1개 (80–143, 64개)</text>
  <rect x="10" y="196" width="310" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="20" y="216" font-size="12">GIC domain: hwirq 0x50 (SPI)</text>
  <text x="20" y="234" font-size="12">GICD → GICC(MMIO) → CPU</text>
  <rect x="360" y="36" width="310" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="370" y="56" font-size="12">PCI-MSIX domain (장치별, hwirq 0..)</text>
  <text x="370" y="74" font-size="12">MSI 쓰기: addr 0x08090040, data 0</text>
  <rect x="360" y="116" width="310" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="370" y="136" font-size="12">ITS domain: (DeviceID, EventID)</text>
  <text x="370" y="154" font-size="12">메모리 테이블로 → LPI + collection</text>
  <rect x="360" y="196" width="310" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="370" y="216" font-size="12">GICv3 domain: hwirq 0x2000 (LPI)</text>
  <text x="370" y="234" font-size="12">Redistributor(CPU별) → ICC sysreg</text>
  <line x1="165" y1="86" x2="165" y2="113" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="165" y1="166" x2="165" y2="193" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="515" y1="86" x2="515" y2="113" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="515" y1="166" x2="515" y2="193" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="10" y="270" width="660" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="20" y="290" font-size="12">공통: virq 17 하나가 세 domain의 irq_data 사슬(parent_data)을 가진다 = hierarchy</text>
  <text x="20" y="308" font-size="12">mask/unmask/affinity 요청은 사슬을 따라 부모로 전달된다 (irq_chip_mask_parent 등)</text>
</svg>
```
그림 4 — 같은 virtio MSI-X 벡터가 GIC 버전에 따라 다른 domain 계층을 탄다. 주소·data·hwirq 값은 예제 1·2의 debugfs 출력 그대로다.

---

## 5. 실습 준비 — 가짜 2차 컨트롤러 `h6-mux`와 소비자 `h6-cons`

### 5.1 무엇을 만드나

- **h6_mux**: 부모 인터럽트 **하나**(GIC SPI 120, edge)를 받아 **자식 8개**로 나눠 주는 가짜 인터럽트 컨트롤러. 하드웨어가 없으니 STATUS(1=pending, W1C)·MASK(1=masked, 리셋값 0xff) 레지스터를 RAM 변수로 흉내 낸다. debugfs `fire`에 비트마스크를 쓰면 STATUS에 비트를 세우고 F6처럼 `irq_set_irqchip_state(parent, IRQCHIP_STATE_PENDING, true)`로 GIC 부모를 울린다.
- 모듈 파라미터 `nested=0`이면 **chained**(부모가 hardirq에서 디멀티플렉스, MMIO GPIO 컨트롤러 흉내), `nested=1`이면 **nested**(부모가 threaded IRQ, I2C 확장기 흉내 — `/proc/interrupts` 칩 이름도 `h6_nested`로 바뀐다). `sleepy=1`이면 STATUS 읽기가 `mutex_lock` + `usleep_range(200,300)`을 한다(I2C 전송 흉내).
- **h6_cons**: DT에서 자식 8개를 받아 등록하고 횟수를 센다. `thr=0`이면 `request_irq`(hard handler), `thr=1`이면 `request_threaded_irq(irq, NULL, fn, IRQF_ONESHOT)`.

### 5.2 예제 4 — DT 조각과 dtb

무엇을 확인하나: `h6-mux`가 `interrupt-controller` + `#interrupt-cells = <2>`로 **자기도 컨트롤러**라고 선언하고, `h6-cons`가 `interrupt-parent = <&h6mux>`로 GIC 대신 거기에 매달린다.

```dts
/* h6_node.dtsi — virt(-smp 2) DT 사본 끝에 #include: 가짜 2차 인터럽트 컨트롤러 + 소비자 */
#include <dt-bindings/interrupt-controller/arm-gic.h>
#include <dt-bindings/interrupt-controller/irq.h>

/ {
	h6mux: h6-mux {
		compatible = "dons,h6-mux";
		interrupts = <GIC_SPI 120 IRQ_TYPE_EDGE_RISING>;	/* 부모: GIC (INTID 152) */
		interrupt-controller;					/* 나도 컨트롤러다 */
		#interrupt-cells = <2>;					/* <hwirq type> */
	};

	h6-cons {
		compatible = "dons,h6-cons";
		interrupt-parent = <&h6mux>;				/* GIC가 아니라 h6mux에 연결 */
		interrupts = <0 IRQ_TYPE_LEVEL_HIGH>, <1 IRQ_TYPE_LEVEL_HIGH>,
			     <2 IRQ_TYPE_LEVEL_HIGH>, <3 IRQ_TYPE_LEVEL_HIGH>,
			     <4 IRQ_TYPE_LEVEL_HIGH>, <5 IRQ_TYPE_LEVEL_HIGH>,
			     <6 IRQ_TYPE_LEVEL_HIGH>, <7 IRQ_TYPE_EDGE_RISING>;
	};
};
```

```sh
#!/bin/sh
# mkdtb.sh — -smp 2 virt DT 덤프 + h6 노드 → /var/tmp/H6/h6.dtb
set -e
W=/var/tmp/H6; K=/var/lab/src/linux-6.18.55/include
qemu-system-aarch64 -machine virt,dumpdtb=$W/virt2.dtb -cpu cortex-a57 -smp 2 -m 512M -nographic
dtc -q -I dtb -O dts -o $W/virt2.dts $W/virt2.dtb
echo '#include "/Users/donh/.elinux_scratch/H6/h6_node.dtsi"' >> $W/virt2.dts
cpp -nostdinc -I $K -undef -x assembler-with-cpp $W/virt2.dts > $W/h6.pp.dts
dtc -q -I dts -O dtb -o $W/h6.dtb $W/h6.pp.dts
fdtget -t x $W/h6.dtb /h6-mux interrupts
fdtget -t x $W/h6.dtb /h6-mux phandle
fdtget -t x $W/h6.dtb /h6-cons interrupt-parent
fdtget -t x $W/h6.dtb /h6-cons interrupts
```

```text
0 78 1
1
1
0 4 1 4 2 4 3 4 4 4 5 4 6 4 7 1
```

출력에서 볼 것: 부모 `<0 0x78 1>` = SPI 120, rising. 라벨 `h6mux`가 참조되어 dtc가 phandle 1을 붙였고, 소비자의 `interrupt-parent`가 그 1이다. 소비자 `interrupts`는 **2셀씩 8묶음**(`IRQ_TYPE_LEVEL_HIGH` = 4, `IRQ_TYPE_EDGE_RISING` = 1) — 셀 수는 부모의 `#interrupt-cells`가 정한다. 셀 수가 안 맞으면 `of_irq_parse_one`이 실패해 consumer가 `-EINVAL`을 받는다(§11 표).

### 5.3 예제 5 — 드라이버 `h6_mux` (네 조각을 합쳐 한 파일)

첫 조각: 상태와 가짜 레지스터, "버스" 접근 함수.

```c
// SPDX-License-Identifier: GPL-2.0
/* h6_mux (1/4): 상태, 가짜 레지스터, "버스" 접근 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/debugfs.h>
#include <linux/delay.h>

static int nested;	module_param(nested, int, 0444);	/* 0=chained(hardirq) 1=nested(thread) */
static int sleepy;	module_param(sleepy, int, 0444);	/* 1=STATUS 읽기가 sleep (I2C 흉내) */

#define NR 8
static struct h6 {
	struct irq_domain *dom;
	int parent;			/* 부모(GIC) virq */
	u8 status;			/* 가짜 STATUS 레지스터: 1=pending */
	u8 mask;			/* 가짜 MASK 레지스터: 1=masked (리셋값 0xff) */
	u8 mask_cache;			/* nested: bus_lock 구간에서 모으는 값 */
	struct mutex bus;		/* 가짜 I2C 버스 락 */
	struct mutex irq_lock;		/* irq_bus_lock 용 */
	unsigned int n_parent, n_mask, n_unmask, n_ack, n_type, n_bus_rd, n_bus_wr;
} h6 = { .mask = 0xff, .mask_cache = 0xff };

/* "STATUS 읽기": MMIO 칩이면 readb 한 번, I2C 칩이면 수백 us 걸리고 sleep 한다 */
static u8 h6_read_status(void)
{
	u8 v;

	h6.n_bus_rd++;
	if (sleepy) {
		mutex_lock(&h6.bus);		/* i2c_transfer 안에서 하는 일을 흉내 */
		usleep_range(200, 300);
	}
	v = READ_ONCE(h6.status) & ~READ_ONCE(h6.mask);
	if (sleepy)
		mutex_unlock(&h6.bus);
	return v;
}

/* "레지스터 쓰기" (I2C 흉내: 항상 sleep) — process context 에서만 부른다 */
static void h6_bus_write(u8 *reg, u8 val)
{
	mutex_lock(&h6.bus);
	usleep_range(200, 300);
	WRITE_ONCE(*reg, val);
	h6.n_bus_wr++;
	mutex_unlock(&h6.bus);
}
```

두 번째 조각: `irq_chip` 두 개. MMIO형(`h6_fast_chip`)은 mask를 즉시 "레지스터"에 쓰고, 버스형(`h6_slow_chip`)은 `mask_cache`에 모았다가 `irq_bus_sync_unlock`에서 한 번에 쓴다.

```c
/* h6_mux (2/4): irq_chip 두 개 — MMIO형(빠름) / 버스형(느림, bus_lock) */
static void h6_mask(struct irq_data *d)
{
	h6.n_mask++;
	if (nested)
		h6.mask_cache |= BIT(irqd_to_hwirq(d));	/* 모으기만 (atomic OK) */
	else
		WRITE_ONCE(h6.mask, h6.mask | BIT(irqd_to_hwirq(d)));
}

static void h6_unmask(struct irq_data *d)
{
	h6.n_unmask++;
	if (nested)
		h6.mask_cache &= ~BIT(irqd_to_hwirq(d));
	else
		WRITE_ONCE(h6.mask, h6.mask & ~BIT(irqd_to_hwirq(d)));
}

static void h6_ack(struct irq_data *d)		/* W1C: pending 비트 지우기 */
{
	h6.n_ack++;
	WRITE_ONCE(h6.status, h6.status & ~BIT(irqd_to_hwirq(d)));
}

static int h6_set_type(struct irq_data *d, unsigned int type)
{
	h6.n_type++;
	if (!nested)	/* 트리거에 맞는 flow handler로 갈아끼운다 */
		irq_set_handler_locked(d, (type & IRQ_TYPE_EDGE_BOTH) ?
				       handle_edge_irq : handle_level_irq);
	return 0;
}

static void h6_bus_lock(struct irq_data *d)
{
	mutex_lock(&h6.irq_lock);
}

static void h6_bus_sync_unlock(struct irq_data *d)	/* 모은 값을 한 번에 버스로 */
{
	if (h6.mask_cache != h6.mask)
		h6_bus_write(&h6.mask, h6.mask_cache);
	mutex_unlock(&h6.irq_lock);
}

static const struct irq_chip h6_fast_chip = {
	.name = "h6_mux", .irq_mask = h6_mask, .irq_unmask = h6_unmask,
	.irq_ack = h6_ack, .irq_set_type = h6_set_type,
};
static const struct irq_chip h6_slow_chip = {
	.name = "h6_nested", .irq_mask = h6_mask, .irq_unmask = h6_unmask,
	.irq_set_type = h6_set_type,
	.irq_bus_lock = h6_bus_lock, .irq_bus_sync_unlock = h6_bus_sync_unlock,
};
```

세 번째 조각: domain ops와 **부모 쪽 handler 두 가지**. 이 노트의 핵심 코드다.

```c
/* h6_mux (3/4): irq_domain + 부모 쪽 handler 두 가지 */
static int h6_map(struct irq_domain *d, unsigned int virq, irq_hw_number_t hw)
{
	irq_set_chip_data(virq, &h6);
	if (nested) {
		irq_set_chip_and_handler(virq, &h6_slow_chip, handle_simple_irq);
		irq_set_nested_thread(virq, true);	/* 자식은 부모 스레드 안에서 돈다 */
	} else {
		irq_set_chip_and_handler(virq, &h6_fast_chip, handle_level_irq);
	}
	irq_set_noprobe(virq);
	return 0;
}

static const struct irq_domain_ops h6_dom_ops = {
	.map = h6_map,
	.xlate = irq_domain_xlate_twocell,	/* <hwirq type> 2셀 해석 */
};

/* (A) chained: GIC flow handler 대신 이 함수가 hardirq 안에서 바로 불린다 */
static void h6_chained(struct irq_desc *desc)
{
	struct irq_chip *pc = irq_desc_get_chip(desc);
	unsigned long pend;
	int bit;

	chained_irq_enter(pc, desc);		/* GIC: 할 일 없음(fasteoi) */
	h6.n_parent++;
	pend = h6_read_status();
	for_each_set_bit(bit, &pend, NR)
		generic_handle_domain_irq(h6.dom, bit);	/* → 자식 flow handler */
	chained_irq_exit(pc, desc);		/* GIC: EOI */
}

/* (B) nested: 부모는 threaded IRQ, 그 스레드 안에서 자식 thread_fn 을 직접 호출 */
static irqreturn_t h6_parent_thread(int irq, void *data)
{
	unsigned long pend;
	int bit;

	h6.n_parent++;
	pend = h6_read_status();			/* sleep 해도 된다 */
	if (!pend)
		return IRQ_NONE;
	h6_bus_write(&h6.status, h6.status & ~pend);	/* W1C, 역시 버스 */
	for_each_set_bit(bit, &pend, NR)
		handle_nested_irq(irq_find_mapping(h6.dom, bit));
	return IRQ_HANDLED;
}
```

네 번째 조각: debugfs와 probe. probe는 (1) 부모 virq 얻기 → (2) linear domain 만들기 → (3) 부모에 chained handler 또는 threaded IRQ 걸기 → (4) 데모로 `irq_create_mapping`/`irq_find_mapping` 호출.

```c
/* h6_mux (4/4): debugfs(fire, regs) + probe */
static struct dentry *h6_dir;

static int fire_set(void *data, u64 bits)	/* STATUS 비트 세우고 부모 GIC SPI pending */
{
	WRITE_ONCE(h6.status, h6.status | (u8)bits);
	return irq_set_irqchip_state(h6.parent, IRQCHIP_STATE_PENDING, true);
}
DEFINE_DEBUGFS_ATTRIBUTE(fire_fops, NULL, fire_set, "%llu\n");

static int regs_show(struct seq_file *m, void *v)
{
	seq_printf(m, "status=0x%02x mask=0x%02x parent=%u mask_ops=%u unmask=%u ack=%u "
		   "set_type=%u bus_rd=%u bus_wr=%u\n", h6.status, h6.mask, h6.n_parent,
		   h6.n_mask, h6.n_unmask, h6.n_ack, h6.n_type, h6.n_bus_rd, h6.n_bus_wr);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(regs);

static int h6_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	unsigned int v5;
	int ret = 0;

	mutex_init(&h6.bus);
	mutex_init(&h6.irq_lock);
	h6.parent = platform_get_irq(pdev, 0);
	if (h6.parent < 0)
		return h6.parent;
	h6.dom = irq_domain_create_linear(of_fwnode_handle(dev->of_node), NR, &h6_dom_ops, &h6);
	if (!h6.dom)
		return -ENOMEM;
	if (nested)
		ret = devm_request_threaded_irq(dev, h6.parent, NULL, h6_parent_thread,
						IRQF_ONESHOT, "h6_parent", &h6);
	else
		irq_set_chained_handler_and_data(h6.parent, h6_chained, &h6);
	v5 = irq_create_mapping(h6.dom, 5);		/* DT 없이 직접 매핑 */
	dev_info(dev, "nested=%d sleepy=%d parent virq=%d hwirq=%lu; map(5)=%u find(5)=%u find(6)=%u ret=%d\n",
		 nested, sleepy, h6.parent, irqd_to_hwirq(irq_get_irq_data(h6.parent)),
		 v5, irq_find_mapping(h6.dom, 5), irq_find_mapping(h6.dom, 6), ret);
	h6_dir = debugfs_create_dir("h6_mux", NULL);
	debugfs_create_file_unsafe("fire", 0200, h6_dir, NULL, &fire_fops);
	debugfs_create_file("regs", 0400, h6_dir, NULL, &regs_fops);
	return ret;
}

static const struct of_device_id h6_of_match[] = { { .compatible = "dons,h6-mux" }, { } };
MODULE_DEVICE_TABLE(of, h6_of_match);
static struct platform_driver h6_driver = {
	.probe = h6_probe,
	.driver = { .name = "h6_mux", .of_match_table = h6_of_match,
		    .suppress_bind_attrs = true },	/* 언바인드 경로는 생략 */
};
module_platform_driver(h6_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("H6 fake secondary interrupt controller");
```

코드 읽는 법:

- `chained_irq_enter/exit`는 **부모 칩**(GIC) 종류에 맞춰 알아서 한다. 랩 소스 `include/linux/irqchip/chained_irq.h`를 보면 부모 칩에 `irq_eoi`가 있으면(GIC) enter는 아무것도 안 하고 exit에서 EOI를 쓴다. `irq_eoi`가 없는 부모면 enter에서 mask(+ack), exit에서 unmask한다. 이 둘을 빼먹으면 GIC가 EOI를 못 받아 **그 SPI가 active로 남고 다시는 안 온다**(§12 표).
- chained handler는 `request_irq`를 쓰지 않는다. 그래서 `irqaction`이 없고, 부모 virq는 `/proc/interrupts`에 **안 나온다**(예제 7에서 확인).
- `.suppress_bind_attrs = true`와 remove 생략은 노트 길이를 위한 단순화다. 실제 드라이버는 remove에서 `irq_set_chained_handler_and_data(parent, NULL, NULL)` → 자식 매핑 `irq_dispose_mapping` → `irq_domain_remove` 순서로 정리한다.
- 측정용 카운터에 락이 없다. chained 쪽은 부모 desc가 한 CPU에서만 돌고, nested 쪽은 부모 스레드 하나라서 데모에서는 충분하다.

### 5.4 예제 6 — 소비자 `h6_cons`와 빌드

```c
// SPDX-License-Identifier: GPL-2.0
/* h6_cons: h6-mux 자식 IRQ 8개를 DT로 받아 등록하고 횟수를 센다 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/debugfs.h>

static int thr;	module_param(thr, int, 0444);	/* 0=request_irq(hard) 1=threaded(NULL, fn) */
static unsigned int cnt[8];
static struct debugfs_u32_array cnt_arr = { .array = cnt, .n_elements = 8 };

static irqreturn_t h6c_handler(int irq, void *data)
{
	cnt[(long)data]++;
	return IRQ_HANDLED;
}

static int h6c_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	long i;
	int irq, ret;

	for (i = 0; i < 8; i++) {
		irq = platform_get_irq(pdev, i);	/* DT 2셀 → h6 domain xlate → virq */
		if (irq < 0)
			return irq;
		if (thr)
			ret = devm_request_threaded_irq(dev, irq, NULL, h6c_handler, IRQF_ONESHOT,
							devm_kasprintf(dev, GFP_KERNEL, "h6c%ld", i),
							(void *)i);
		else
			ret = devm_request_irq(dev, irq, h6c_handler, 0,
					       devm_kasprintf(dev, GFP_KERNEL, "h6c%ld", i), (void *)i);
		dev_info(dev, "child %ld: virq=%d hwirq=%lu type=%u ret=%d\n", i, irq,
			 irqd_to_hwirq(irq_get_irq_data(irq)), irq_get_trigger_type(irq), ret);
		if (ret)
			return ret;
	}
	debugfs_create_u32_array("h6_cons_cnt", 0444, NULL, &cnt_arr);
	return 0;
}

static const struct of_device_id h6c_of_match[] = { { .compatible = "dons,h6-cons" }, { } };
MODULE_DEVICE_TABLE(of, h6c_of_match);
static struct platform_driver h6c_driver = {
	.probe = h6c_probe,
	.driver = { .name = "h6_cons", .of_match_table = h6c_of_match,
		    .suppress_bind_attrs = true },
};
module_platform_driver(h6c_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("H6 consumer of h6-mux child IRQs");
```

소비자 코드에는 **h6_mux에 대한 지식이 하나도 없다**. GIC 인터럽트를 받는 F6 드라이버와 똑같이 `platform_get_irq` + `request_irq`다. 이것이 irq_domain 추상화의 목적이다. (처음에는 `debugfs_create_u32_array`에 복합 리터럴 `&(struct debugfs_u32_array){...}`를 넘겼다 — 함수가 끝나면 사라지는 스택 객체라 `static`으로 고쳤다. 컴파일러는 경고하지 않는다.)

```sh
#!/bin/sh
# build.sh — 네 조각을 합쳐 h6_mux.ko + h6_cons.ko (dbg 커널용)
set -e
S=/Users/donh/.elinux_scratch/H6
mkdir -p /var/tmp/H6/mod && cd /var/tmp/H6/mod
cat $S/m_a.c $S/m_b.c $S/m_c.c $S/m_d.c > h6_mux.c
cp $S/h6_cons.c .
echo "obj-m := h6_mux.o h6_cons.o" > Kbuild
make -j2 -C /var/lab/out/linux-build-dbg M=$PWD modules
```

```text
make: Entering directory '/var/lab/build/linux-dbg'
make[1]: Entering directory '/var/tmp/H6/mod'
  CC [M]  h6_mux.o
  CC [M]  h6_cons.o
  MODPOST Module.symvers
  CC [M]  h6_mux.mod.o
  CC [M]  .module-common.o
  CC [M]  h6_cons.mod.o
  LD [M]  h6_mux.ko
  LD [M]  h6_cons.ko
make[1]: Leaving directory '/var/tmp/H6/mod'
make: Leaving directory '/var/lab/build/linux-dbg'
```

경고 0개. 부팅 스크립트(VM 안에서 `sh boot.sh g_xxx.sh [인자]`):

```sh
#!/bin/sh
# boot.sh <게스트 스크립트> [인자] — dbg 커널 + -smp 2 + h6.dtb, 스크립트 실행 후 종료
set -e
W=/var/tmp/H6; S=/Users/donh/.elinux_scratch/H6
rm -rf $W/r && mkdir $W/r && cp -a /var/lab/build/rootfs/. $W/r/
cp $W/mod/h6_mux.ko $W/mod/h6_cons.ko $S/g_*.sh $W/r/
(cd $W/r && find . | cpio -o -H newc --quiet | gzip) > $W/i.gz
timeout 240 qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 512M -nographic \
  -kernel /var/lab/out/Image-dbg -initrd $W/i.gz -dtb $W/h6.dtb \
  -append "console=ttyAMA0 quiet LAB_CMD=sh~/$1~$2"
```

> **측정 환경 주의**: QEMU TCG(에뮬레이션) + lockdep 켠 디버그 커널 + 같은 VM에서 다른 작업 10여 개. ftrace의 µs 숫자는 실제 SoC보다 수십~수백 배 크다. **호출 순서와 구조**만 본다.

---

## 6. chained handler — MMIO GPIO 컨트롤러 방식

### 6.1 예제 7 — 등록, 10번 발생, 관찰

무엇을 확인하나: `nested=0`으로 올리고 소비자 8개를 붙인 뒤, `fire`에 1,2,4,…,128, 255, 129를 써서 자식 인터럽트를 18번(1×8 + 8 + 2) 일으킨다. 칩 콜백 횟수, `/proc/interrupts`, 부모·자식의 debugfs를 본다.

```sh
#!/bin/sh
# g_chain.sh — chained 모드: 등록 → 발생 → /proc/interrupts · debugfs
D=/sys/kernel/debug
insmod /h6_mux.ko nested=0
insmod /h6_cons.ko thr=0
dmesg | grep -E "h6|GICv2m"
echo "=== regs after probe ==="; cat $D/h6_mux/regs
for b in 1 2 4 8 16 32 64 128 255 129; do echo $b > $D/h6_mux/fire; done
sleep 1
echo "=== regs after 10 fires ==="; cat $D/h6_mux/regs
echo "=== counts ==="; cat $D/h6_cons_cnt; echo
echo "=== /proc/interrupts ==="; grep -E "CPU0|h6" /proc/interrupts
P=$(dmesg | sed -n 's/.*parent virq=\([0-9]*\).*/\1/p')
echo "=== debugfs irqs/$P (parent) ==="; cat $D/irq/irqs/$P
C=$(grep h6c7 /proc/interrupts | cut -d: -f1 | tr -d ' ')
echo "=== debugfs irqs/$C (child 7) ==="; cat $D/irq/irqs/$C
echo "=== domain ==="; ls $D/irq/domains; cat $D/irq/domains/:h6-mux
```

```text
lab initramfs: 6.18.55-dbg up 4.94s
[    0.000000] GICv2m: range[mem 0x08020000-0x08020fff], SPI[80:143]
[    5.228841] h6_mux: loading out-of-tree module taints kernel.
[    5.243276] h6_mux h6-mux: nested=0 sleepy=0 parent virq=22 hwirq=152; map(5)=23 find(5)=23 find(6)=0 ret=0
[    5.278597] h6_cons h6-cons: child 0: virq=24 hwirq=0 type=4 ret=0
[    5.279811] h6_cons h6-cons: child 1: virq=25 hwirq=1 type=4 ret=0
[    5.281502] h6_cons h6-cons: child 2: virq=26 hwirq=2 type=4 ret=0
[    5.282582] h6_cons h6-cons: child 3: virq=27 hwirq=3 type=4 ret=0
[    5.283746] h6_cons h6-cons: child 4: virq=28 hwirq=4 type=4 ret=0
[    5.284590] h6_cons h6-cons: child 5: virq=23 hwirq=5 type=4 ret=0
[    5.285696] h6_cons h6-cons: child 6: virq=29 hwirq=6 type=4 ret=0
[    5.286780] h6_cons h6-cons: child 7: virq=30 hwirq=7 type=1 ret=0
=== regs after probe ===
status=0x00 mask=0x00 parent=0 mask_ops=0 unmask=8 ack=0 set_type=8 bus_rd=0 bus_wr=0
=== regs after 10 fires ===
status=0x00 mask=0x00 parent=10 mask_ops=15 unmask=23 ack=18 set_type=8 bus_rd=10 bus_wr=0
=== counts ===
3 2 2 2 2 2 2 3

=== /proc/interrupts ===
           CPU0       CPU1       
 23:          2          0   h6_mux   5 Level     h6c5
 24:          3          0   h6_mux   0 Level     h6c0
 25:          2          0   h6_mux   1 Level     h6c1
 26:          2          0   h6_mux   2 Level     h6c2
 27:          2          0   h6_mux   3 Level     h6c3
 28:          2          0   h6_mux   4 Level     h6c4
 29:          2          0   h6_mux   6 Level     h6c6
 30:          3          0   h6_mux   7 Edge      h6c7
=== debugfs irqs/22 (parent) ===
handler:  h6_chained [h6_mux]
device:   (null)
status:   0x00010c01
            _IRQ_NOPROBE
            _IRQ_NOREQUEST
            _IRQ_NOTHREAD
istate:   0x00004000
ddepth:   0
wdepth:   0
dstate:   0x0b400201
            IRQ_TYPE_EDGE_RISING
            IRQD_ACTIVATED
            IRQD_IRQ_STARTED
            IRQD_SINGLE_TARGET
            IRQD_DEFAULT_TRIGGER_SET
            IRQD_HANDLE_ENFORCE_IRQCTX
node:     -1
affinity: 0-1
effectiv: 0
domain:  :intc@8000000
 hwirq:   0x98
 chip:    GIC-0
  flags:   0x15
             IRQCHIP_SET_TYPE_MASKED
             IRQCHIP_MASK_ON_SUSPEND
             IRQCHIP_SKIP_SET_WAKE
=== debugfs irqs/30 (child 7) ===
handler:  handle_edge_irq
device:   (null)
status:   0x00000401
            _IRQ_NOPROBE
istate:   0x00004000
ddepth:   0
wdepth:   0
dstate:   0x02400201
            IRQ_TYPE_EDGE_RISING
            IRQD_ACTIVATED
            IRQD_IRQ_STARTED
            IRQD_DEFAULT_TRIGGER_SET
node:     -1
affinity: 0-1
effectiv: 
domain:  :h6-mux
 hwirq:   0x7
 chip:    h6_mux
  flags:   0x0
=== domain ===
:h6-mux                          :pl061@9030000
:intc@8000000                    GICv2m-PCI-MSIX-0000:00:01.0-12
:intc@8000000:v2m@8020000-5      default
name:   :h6-mux
 size:   8
 mapped: 8
 flags:  0x00000002
            IRQ_DOMAIN_NAME_ALLOCATED
```

출력에서 볼 것:

- **번호**: 부모 virq 22 = GIC hwirq 152(0x98) = SPI 120 + 32. 자식은 우리 domain의 hwirq 0–7이고 virq 23–30을 받았다. probe에서 `irq_create_mapping(dom, 5)`가 먼저 virq 23을 잡았고 `irq_find_mapping(dom,5)=23`, 아직 아무도 매핑하지 않은 hwirq 6은 `find(6)=0`이다. 나중에 소비자가 DT로 hwirq 5를 요청했을 때 **같은 virq 23을 재사용**했다(child 5: virq=23).
- `type=4`(LEVEL_HIGH)·`type=1`(EDGE_RISING)이 DT 2번째 셀 그대로이고, `set_type=8`은 매핑 때 `irq_set_irq_type`이 8번 우리 `h6_set_type`을 불렀다는 뜻이다. 그 결과 자식 7의 handler가 `handle_edge_irq`로 갈아끼워졌고(debugfs `handler:`), `/proc/interrupts`의 트리거 칸도 `Edge`다.
- **probe 직후 `unmask=8`, `mask=0x00`**: 리셋값 0xff(전부 masked)에서 시작했는데 `request_irq`마다 `irq_startup` → `h6_unmask`가 불렸다. provider는 "누가 request할 때까지 mask해 두는" 것이 정석이다.
- **콜백 횟수 계산이 맞아떨어진다**: 자식 인터럽트 18번 중 level 15번(hwirq 0–6), edge 3번(hwirq 7). `handle_level_irq`는 매번 mask + ack + unmask → mask 15, ack 15, unmask 15(+ probe 때 8 = 23). `handle_edge_irq`는 ack만 → ack 15 + 3 = 18. 우리 칩에 `irq_mask_ack`가 없어서 genirq가 `irq_mask` 다음 `irq_ack`를 따로 불렀다.
- **부모(virq 22)는 `/proc/interrupts`에 없다.** chained 부모는 `irqaction`이 없고 `show_interrupts`가 `irq_desc_is_chained()`이면 건너뛴다(랩 소스 `kernel/irq/proc.c`). debugfs에서는 `handler: h6_chained [h6_mux]`(flow handler 자리에 우리 함수!)와 `_IRQ_NOREQUEST`·`_IRQ_NOTHREAD`가 보인다. 다른 드라이버가 이 SPI를 `request_irq` 하면 거부된다.
- 자식에는 `effectiv:`(effective affinity)가 비어 있다. h6_mux 칩에 `irq_set_affinity`가 없기 때문 — **cascade 자식의 CPU는 부모의 CPU를 따라간다**. 자식 하나만 다른 CPU로 옮길 수 없다(§12).
- domain `:h6-mux`는 `size 8, mapped 8`, 부모 표시 없음(hierarchy 아님). 이름은 DT 노드 이름에서 자동으로 왔다.

### 6.2 예제 8 — function_graph로 본 cascade 한 번

무엇을 확인하나: `fire`에 0x81(자식 0 = level, 자식 7 = edge)을 쓰고, CPU0(부모의 effective affinity)에서 `gic_handle_irq` 아래를 기록한다. 그림 2의 "desc 두 번 돌기"와 §2 표의 콜백 순서를 실제 호출로 확인한다.

```sh
#!/bin/sh
# g_trace.sh — function_graph: gic_handle_irq → h6_chained → 자식 flow → h6c_handler
D=/sys/kernel/debug; T=/sys/kernel/tracing
mount -t tracefs none $T
insmod /h6_mux.ko nested=0; insmod /h6_cons.ko thr=0
echo 1 > $T/tracing_cpumask                      # CPU0만 (부모 effective affinity)
echo gic_handle_irq > $T/set_graph_function
printf '%s\n' '*rcu*' '*lock*' 'preempt_count*' 'irq_can_handle*' 'add_interrupt_randomness' 'stack_trace*' 'unwind*' 'fast_mix' 'add_timer_on' > $T/set_ftrace_notrace
echo 10 > $T/max_graph_depth
echo function_graph > $T/current_tracer
echo 0x81 > $D/h6_mux/fire                        # 자식 0(level) + 7(edge)
sleep 1
echo 0 > $T/tracing_on
cat $T/trace > /trace.txt
cat /trace.txt
```

```text
 0)               |  gic_handle_irq() {
 0)               |    generic_handle_domain_irq() {
 0) + 15.280 us   |      __irq_resolve_mapping();
 0)               |      handle_irq_desc() {
 0)               |        h6_chained [h6_mux]() {
 0) + 30.320 us   |          h6_read_status [h6_mux]();
 0)               |          generic_handle_domain_irq() {
 0) + 13.872 us   |            __irq_resolve_mapping();
 0)               |            handle_irq_desc() {
 0)               |              handle_level_irq() {
 0) + 44.528 us   |                h6_mask [h6_mux]();
 0) + 18.208 us   |                h6_ack [h6_mux]();
 0)               |                handle_irq_event() {
 0)               |                  __handle_irq_event_percpu() {
 0) + 34.752 us   |                    h6c_handler [h6_cons]();
 0) ! 200.464 us  |                  }
 0)   5.296 us    |                  calc_wheel_index();
 0)   2.672 us    |                  enqueue_timer();
 0) + 57.920 us   |                  note_interrupt();
 0) ! 474.864 us  |                }
 0) + 10.336 us   |                h6_unmask [h6_mux]();
 0) # 1207.072 us |              }
 0) # 1221.296 us |            }
 0) # 1248.752 us |          }
 0)               |          generic_handle_domain_irq() {
 0)   8.448 us    |            __irq_resolve_mapping();
 0)               |            handle_irq_desc() {
 0)               |              handle_edge_irq() {
 0)   3.248 us    |                h6_ack [h6_mux]();
 0)               |                handle_irq_event() {
 0)               |                  __handle_irq_event_percpu() {
 0)   1.632 us    |                    h6c_handler [h6_cons]();
 0)   7.280 us    |                  }
 0)   2.080 us    |                  note_interrupt();
 0) + 28.384 us   |                }
 0) ! 109.168 us  |              }
 0) ! 117.744 us  |            }
 0) ! 132.864 us  |          }
 0) + 11.792 us   |          gic_eoi_irq();
 0) # 1552.288 us |        }
 0) # 1571.760 us |      }
 0) # 1603.664 us |    }
 0) # 1628.720 us |  }
```

(전체 1700줄 중 우리 인터럽트가 든 `gic_handle_irq` 블록 하나만 남겼다. 락·RCU·lockdep 스택 저장 함수는 `set_ftrace_notrace`로 뺐다 — 첫 시도에서 lockdep의 `stack_trace_save` 줄 수십 개가 끼어들어서 두 번째 실행에서 필터를 늘렸다. `calc_wheel_index`/`enqueue_timer`는 notrace로 뺀 `add_timer_on`의 자식이다.)

출력에서 볼 것:

- `generic_handle_domain_irq`가 **바깥(GIC domain, hwirq 152) 한 번 + 안쪽(h6 domain) 두 번**. 바깥 `handle_irq_desc`가 부른 것은 flow handler가 아니라 **`h6_chained`**다 — 부모 desc의 `handle_irq` 자리를 차지했기 때문이다.
- `chained_irq_enter`는 흔적이 없다(GIC는 `irq_eoi`가 있어서 enter는 빈 함수, 인라인). `chained_irq_exit`가 마지막에 **`gic_eoi_irq()`**를 불렀다. F6의 `handle_fasteoi_irq`가 하던 EOI를 이제 우리 chained handler가 책임진다.
- 자식 0(level): `h6_mask` → `h6_ack` → handler → `h6_unmask`. 자식 7(edge): `h6_ack` → handler. §2 표 그대로다.
- **모든 것이 하나의 hardirq 안**에서 돌았다(전부 `<idle>-0` 컨텍스트, CPU0). 자식 handler도 hardirq다. 그래서 `h6_read_status`가 sleep하면 안 된다 → §8.
- 자식마다 `note_interrupt`(spurious 감지)가 따로 돈다. 부모에는 없다(§10).

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" font-size="13">(A) chained — 전부 hardirq 하나 안 (CPU0, IRQ off)</text>
  <line x1="10" y1="40" x2="670" y2="40" stroke="currentColor"/>
  <rect x="20" y="52" width="120" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="30" y="74" font-size="12">gic_handle_irq</text>
  <rect x="150" y="52" width="120" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="160" y="74" font-size="12">STATUS readb</text>
  <rect x="280" y="52" width="150" height="34" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="290" y="74" font-size="12">자식 0: mask·ack·fn</text>
  <rect x="440" y="52" width="130" height="34" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="450" y="74" font-size="12">자식 7: ack·fn</text>
  <rect x="580" y="52" width="80" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="590" y="74" font-size="12">GIC EOI</text>
  <text x="20" y="112" font-size="12">장점: 지연 최소, 스레드 없음. 조건: STATUS 읽기와 mask/ack가 sleep 없이 끝나야 함 (MMIO)</text>
  <text x="10" y="160" font-size="13">(B) nested — 부모는 짧은 hardirq, 나머지는 부모 스레드 안 (sleep 가능)</text>
  <line x1="10" y1="180" x2="670" y2="180" stroke="currentColor"/>
  <rect x="20" y="192" width="200" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="30" y="214" font-size="12">hardirq: mask(ONESHOT)·EOI</text>
  <line x1="120" y1="226" x2="120" y2="252" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="130" y="246" font-size="12">wake irq/22-h6_parent (SCHED_FIFO 50)</text>
  <rect x="20" y="256" width="150" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="30" y="278" font-size="12">I2C 읽기 (sleep)</text>
  <rect x="180" y="256" width="150" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="190" y="278" font-size="12">I2C W1C (sleep)</text>
  <rect x="340" y="256" width="150" height="34" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="350" y="278" font-size="12">nested 0: thread_fn</text>
  <rect x="500" y="256" width="160" height="34" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="510" y="278" font-size="12">nested 3: thread_fn</text>
  <rect x="20" y="306" width="160" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="30" y="328" font-size="12">부모 unmask (끝)</text>
  <text x="20" y="366" font-size="12">자식 thread_fn은 같은 스레드에서 차례로 직접 호출된다 (자식별 스레드 없음)</text>
  <text x="20" y="386" font-size="12">자식 hard handler는 존재할 수 없다 → request_irq(hard)는 -EINVAL</text>
  <text x="20" y="406" font-size="12">자식 mask/unmask는 irq_bus_lock 구간에서 모았다가 sync_unlock에서 버스로 1번에 쓴다</text>
</svg>
```
그림 5 — chained(위)와 nested(아래)의 시간 축. 위는 예제 8, 아래는 예제 9의 ftrace에서 본 순서를 그렸다(아래의 자식 번호 0·3은 예제 9의 `fire 0x09`).

---

## 7. nested threaded handler — I2C/SPI GPIO 확장기 방식

### 7.1 왜 확장기는 chained가 될 수 없나

PCA9555 같은 I2C GPIO 확장기를 생각하자. 핀 16개 중 하나가 변하면 INT 핀(open-drain, active-low)을 내린다. 어느 핀인지 알려면 **I2C로 입력 레지스터를 읽어야** 하고, 읽어야 INT가 풀린다.

- I2C 전송(`i2c_transfer`)은 버스 mutex를 잡고, 컨트롤러 인터럽트/DMA 완료를 `wait_for_completion`으로 **기다린다**. 100 kHz에서 2바이트 읽기 = 수백 µs. hardirq에서는 sleep이 불가능하고, 가능하다 해도 수백 µs 동안 CPU 하나를 IRQ off로 묶는 건 시스템 전체 지연을 망친다.
- 그래서 부모 IRQ(SoC GPIO 핀)는 **threaded IRQ**로 받고(`request_threaded_irq(irq, NULL, thread_fn, IRQF_ONESHOT)` — ONESHOT이라 스레드가 끝날 때까지 부모 줄이 mask되어 레벨 INT가 폭풍을 일으키지 않는다), 스레드 안에서 I2C를 읽고, 자식은 `handle_nested_irq()`로 **같은 스레드 안에서** 부른다.
- gpiolib는 이를 규칙으로 막아 둔다. 랩 소스 `drivers/gpio/gpiolib.c`:

```c
	if (gc->irq.parent_handler && gc->can_sleep) {
		gpiochip_err(gc, "you cannot have chained interrupts on a chip that may sleep\n");
		return -EINVAL;
	}
```

`can_sleep`(I2C/SPI 확장기)인 gpio_chip이 chained `parent_handler`를 등록하려 하면 거부된다.

### 7.2 `irq_bus_lock` / `irq_bus_sync_unlock` — mask를 I2C로 쓰는 법

§1.2에서 본 대로 `irq_mask`/`irq_unmask`/`irq_set_type`은 `desc->lock`(raw spinlock) 안에서 불린다. I2C 확장기의 MASK 레지스터를 거기서 쓸 수 없다. genirq의 해법:

1. 느린 버스 칩은 `irq_bus_lock`·`irq_bus_sync_unlock` 콜백을 채운다.
2. genirq는 `request_irq`/`free_irq`/`enable_irq`/`disable_irq`/`irq_set_irq_type`/`irq_set_irq_wake` 같은 **process 컨텍스트 API**에서 `chip_bus_lock(desc)` → (desc->lock 잡고 `irq_mask` 등 호출) → `chip_bus_sync_unlock(desc)` 순서로 감싼다.
3. 드라이버의 `irq_mask`/`irq_unmask`는 **캐시만 고치고**, `irq_bus_sync_unlock`(이제 spinlock 밖, sleep 가능)에서 캐시를 I2C로 **한 번에** 쓴다.

예: 센서 드라이버가 `disable_irq()` → `enable_irq()`를 하면 그때마다 I2C 쓰기 1회. `request_irq` 한 번이면 type 설정과 unmask가 한 번의 sync로 묶인다. 대표 실물이 `drivers/gpio/gpio-pca953x.c`의 `pca953x_irq_bus_lock`/`pca953x_irq_bus_sync_unlock`, 그리고 regmap-irq(`drivers/base/regmap/regmap-irq.c`의 `regmap_irq_lock`/`regmap_irq_sync_unlock`)다.

**hardirq 경로에서는 bus_lock이 불리지 않는다**는 점도 기억하자. nested 경로(`handle_nested_irq`)는 칩의 mask/ack를 부르지 않으므로 문제가 없다. 대신 확장기 드라이버가 스레드 안에서 상태 읽기/지우기를 직접 한다. 그리고 genirq는 `irq_bus_lock`이 있는 칩의 인터럽트를 NMI로 쓸 수 없게 막는다(`kernel/irq/manage.c`의 "Don't support NMIs for chips behind a slow bus").

### 7.3 예제 9 — nested 모드: `request_irq` 거부, threaded 등록, 버스 쓰기 횟수, 스레드 하나

무엇을 확인하나: `nested=1 sleepy=1`로 올린다. (a) 소비자를 hard handler로 등록하면? (b) threaded로 등록하고 예제 7과 같은 10번을 쏘면 콜백·버스 접근 횟수가 어떻게 다른가? (c) 스레드는 몇 개인가? (d) 부모 스레드의 함수 그래프.

```sh
#!/bin/sh
# g_nested.sh — nested 모드: request_irq 거부 → threaded 등록 → 발생 → 관찰
D=/sys/kernel/debug; T=/sys/kernel/tracing
insmod /h6_mux.ko nested=1 sleepy=1
echo "=== (a) consumer thr=0 (request_irq, hard handler) ==="
insmod /h6_cons.ko thr=0; echo "insmod rc=$?"; rmmod h6_cons
echo "=== (b) consumer thr=1 (request_threaded_irq NULL, fn) ==="
dmesg -c > /dev/null; insmod /h6_cons.ko thr=1; dmesg | grep h6
echo "=== regs after probe ==="; cat $D/h6_mux/regs
for b in 1 2 4 8 16 32 64 128 255 129; do echo $b > $D/h6_mux/fire; sleep 0.2; done
sleep 1
echo "=== regs after 10 fires ==="; cat $D/h6_mux/regs
echo "=== counts ==="; cat $D/h6_cons_cnt; echo
echo "=== /proc/interrupts ==="; grep -E "CPU0|h6" /proc/interrupts
echo "=== irq threads ==="; ps -o pid,comm | grep "irq/"
C=$(grep h6c3 /proc/interrupts | cut -d: -f1 | tr -d ' ')
echo "=== debugfs irqs/$C (child 3) ==="; head -8 $D/irq/irqs/$C
echo "=== function_graph h6_parent_thread ==="
mount -t tracefs none $T
echo h6_parent_thread > $T/set_graph_function
printf '%s\n' '*rcu*' '*lock*' 'preempt_count*' 'stack_trace*' 'unwind*' 'lockdep*' > $T/set_ftrace_notrace
echo 3 > $T/max_graph_depth
echo function_graph > $T/current_tracer
echo 0x09 > $D/h6_mux/fire; sleep 1
echo 0 > $T/tracing_on; cat $T/trace
```

```text
lab initramfs: 6.18.55-dbg up 6.66s
=== (a) consumer thr=0 (request_irq, hard handler) ===
[    7.418830] h6_cons h6-cons: error -EINVAL: request_irq(24) h6c_handler [h6_cons] 0x0 h6c0
[    7.420168] h6_cons h6-cons: probe with driver h6_cons failed with error -22
insmod rc=0
=== (b) consumer thr=1 (request_threaded_irq NULL, fn) ===
[    7.607105] h6_cons h6-cons: child 0: virq=24 hwirq=0 type=4 ret=0
[    7.609712] h6_cons h6-cons: child 1: virq=25 hwirq=1 type=4 ret=0
[    7.612068] h6_cons h6-cons: child 2: virq=26 hwirq=2 type=4 ret=0
[    7.614144] h6_cons h6-cons: child 3: virq=27 hwirq=3 type=4 ret=0
[    7.616559] h6_cons h6-cons: child 4: virq=28 hwirq=4 type=4 ret=0
[    7.617622] h6_cons h6-cons: child 5: virq=23 hwirq=5 type=4 ret=0
[    7.620155] h6_cons h6-cons: child 6: virq=29 hwirq=6 type=4 ret=0
[    7.621990] h6_cons h6-cons: child 7: virq=30 hwirq=7 type=1 ret=0
=== regs after probe ===
status=0x00 mask=0x00 parent=0 mask_ops=0 unmask=8 ack=0 set_type=8 bus_rd=0 bus_wr=8
=== regs after 10 fires ===
status=0x00 mask=0x00 parent=10 mask_ops=0 unmask=8 ack=0 set_type=8 bus_rd=10 bus_wr=18
=== counts ===
3 2 2 2 2 2 2 3

=== /proc/interrupts ===
           CPU0       CPU1       
 22:         10          0 GIC-0 152 Edge      h6_parent
 23:          2          0 h6_nested   5 Level     h6c5
 24:          3          0 h6_nested   0 Level     h6c0
 25:          2          0 h6_nested   1 Level     h6c1
 26:          2          0 h6_nested   2 Level     h6c2
 27:          2          0 h6_nested   3 Level     h6c3
 28:          2          0 h6_nested   4 Level     h6c4
 29:          2          0 h6_nested   6 Level     h6c6
 30:          3          0 h6_nested   7 Edge      h6c7
=== irq threads ===
   74 irq/22-h6_paren
=== debugfs irqs/27 (child 3) ===
handler:  handle_simple_irq
device:   (null)
status:   0x00008504
            _IRQ_NOPROBE
            _IRQ_NESTED_THREAD
istate:   0x00004020
            IRQS_ONESHOT
ddepth:   0
=== function_graph h6_parent_thread ===
# tracer: function_graph
#
# CPU  DURATION                  FUNCTION CALLS
# |     |   |                     |   |   |   |
 0)               |  h6_parent_thread [h6_mux]() {
 0)               |    h6_read_status [h6_mux]() {
 0) ! 123.168 us  |      __might_sleep();
 0) # 4127.200 us |      usleep_range_state();
 0) # 4559.872 us |    }
 0)               |    __might_sleep() {
 0)   6.624 us    |      __might_resched();
 0) + 20.592 us   |    }
 0)               |    usleep_range_state() {
 0) ! 799.584 us  |      ktime_get();
 0) + 11.344 us   |      hrtimer_setup_sleeper_on_stack();
 0)   4.544 us    |      ktime_add_safe();
 0) + 83.584 us   |      hrtimer_sleeper_start_expires();
 0) ! 734.912 us  |      schedule();
 0)   9.712 us    |      hrtimer_cancel();
 0) # 1809.968 us |    }
 0) + 22.384 us   |    __irq_resolve_mapping();
 0)               |    handle_nested_irq() {
 0) + 64.544 us   |      irq_to_desc();
 0) + 14.032 us   |      __might_sleep();
 0) + 17.248 us   |      h6c_handler [h6_cons]();
 0)   5.200 us    |      note_interrupt();
 0) ! 103.424 us  |      wake_threads_waitq();
 0) ! 315.632 us  |    }
 0) + 12.784 us   |    __irq_resolve_mapping();
 0)               |    handle_nested_irq() {
 0) + 16.832 us   |      irq_to_desc();
 0)   7.216 us    |      __might_sleep();
 0)   4.800 us    |      h6c_handler [h6_cons]();
 0)   3.712 us    |      note_interrupt();
 0) + 25.840 us   |      wake_threads_waitq();
 0) ! 112.448 us  |    }
 0) # 8218.208 us |  }
```

출력에서 볼 것:

- **(a)** nested 자식에 hard handler만 주면 `__setup_irq`가 `-EINVAL`을 돌려준다(랩 소스 `kernel/irq/manage.c`: `nested = irq_settings_is_nested_thread(desc); if (nested) { if (!new->thread_fn) { ret = -EINVAL; ...`). 부를 함수가 `thread_fn`뿐이기 때문이다. `insmod rc=0`인 점도 함정이다 — 모듈 로드(드라이버 등록)는 성공하고 **probe만 실패**한다. dmesg를 봐야 안다. 소비자 드라이버가 provider 종류를 모를 때 쓰는 API가 `request_any_context_irq()`다: nested면 threaded로, 아니면 hard로 등록해 준다(랩 소스에서 확인).
- **(b) 콜백 횟수**: 예제 7(chained)과 같은 자극인데 `mask_ops=0`, `ack=0`. `handle_nested_irq`는 칩 콜백을 부르지 않는다. 대신 **`bus_wr`**: probe 때 8(자식 8개 `request_irq`마다 unmask가 `irq_bus_sync_unlock`에서 1회씩 버스로 나감), 발생 10회 동안 +10(부모 스레드의 W1C 쓰기). STATUS 읽기는 두 모드 모두 10회.
- **부모 virq 22가 이번엔 `/proc/interrupts`에 있다**(`h6_parent`, 10회). 일반 threaded IRQ로 `request`했기 때문이다. 자식 칩 이름은 `h6_nested`.
- **스레드는 하나**(`irq/22-h6_paren` — 커널 스레드 이름은 15자에서 잘린다). 자식 8개는 스레드를 만들지 않았다. 자식 debugfs의 `_IRQ_NESTED_THREAD`가 그 표시다.
- **function_graph**: `h6_parent_thread` 안에서 `usleep_range_state` → `schedule()`이 실제로 불렸다(sleep). `h6_bus_write`는 인라인되어 두 번째 `__might_sleep` + `usleep_range_state` 덩어리로 보인다. 이어서 `handle_nested_irq`가 자식 0과 3에 대해 한 번씩, 그 안에서 `h6c_handler`(소비자의 `thread_fn`)를 직접 불렀다. 전체 8.2 ms 중 대부분이 가짜 I2C 대기다.

### 7.4 측정 — 같은 자극, 두 provider의 하드웨어 접근 횟수

```svg
<svg viewBox="0 0 640 300" xmlns="http://www.w3.org/2000/svg">
  <line x1="60" y1="230" x2="620" y2="230" stroke="currentColor"/>
  <line x1="60" y1="50" x2="60" y2="230" stroke="currentColor"/>
  <text x="52" y="234" font-size="12" text-anchor="end">0</text>
  <text x="52" y="198" font-size="12" text-anchor="end">5</text>
  <line x1="60" y1="194" x2="620" y2="194" stroke="#888" stroke-opacity="0.3"/>
  <text x="52" y="162" font-size="12" text-anchor="end">10</text>
  <line x1="60" y1="158" x2="620" y2="158" stroke="#888" stroke-opacity="0.3"/>
  <text x="52" y="126" font-size="12" text-anchor="end">15</text>
  <line x1="60" y1="122" x2="620" y2="122" stroke="#888" stroke-opacity="0.3"/>
  <text x="52" y="90" font-size="12" text-anchor="end">20</text>
  <line x1="60" y1="86" x2="620" y2="86" stroke="#888" stroke-opacity="0.3"/>
  <text x="52" y="54" font-size="12" text-anchor="end">25</text>
  <line x1="60" y1="50" x2="620" y2="50" stroke="#888" stroke-opacity="0.3"/>
  <rect x="80" y="122" width="34" height="108" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
  <text x="97" y="117" font-size="12" text-anchor="middle">15</text>
  <text x="135" y="225" font-size="12" text-anchor="middle">0</text>
  <text x="116" y="250" font-size="12" text-anchor="middle">irq_mask</text>
  <rect x="192" y="64" width="34" height="166" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
  <text x="209" y="59" font-size="12" text-anchor="middle">23</text>
  <rect x="230" y="172" width="34" height="58" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
  <text x="247" y="167" font-size="12" text-anchor="middle">8</text>
  <text x="228" y="250" font-size="12" text-anchor="middle">irq_unmask</text>
  <rect x="304" y="100" width="34" height="130" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
  <text x="321" y="95" font-size="12" text-anchor="middle">18</text>
  <text x="359" y="225" font-size="12" text-anchor="middle">0</text>
  <text x="340" y="250" font-size="12" text-anchor="middle">irq_ack</text>
  <rect x="416" y="158" width="34" height="72" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
  <text x="433" y="153" font-size="12" text-anchor="middle">10</text>
  <rect x="454" y="158" width="34" height="72" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
  <text x="471" y="153" font-size="12" text-anchor="middle">10</text>
  <text x="452" y="250" font-size="12" text-anchor="middle">STATUS 읽기</text>
  <text x="545" y="225" font-size="12" text-anchor="middle">0</text>
  <rect x="566" y="100" width="34" height="130" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
  <text x="583" y="95" font-size="12" text-anchor="middle">18</text>
  <text x="564" y="250" font-size="12" text-anchor="middle">버스 쓰기</text>
  <rect x="330" y="14" width="14" height="12" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
  <text x="350" y="25" font-size="12">chained (h6_mux)</text>
  <rect x="480" y="14" width="14" height="12" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
  <text x="500" y="25" font-size="12">nested (h6_nested)</text>
  <text x="60" y="25" font-size="12">호출 횟수 (등록 8개 + fire 10회 후)</text>
  <text x="60" y="285" font-size="12">값: 예제 7·9의 regs 출력 그대로. 가로축 = h6 칩 콜백/가짜 레지스터 접근</text>
</svg>
```
그림 6 — 자식 8개 등록 + 18번의 자식 인터럽트 동안 chained provider는 mask/ack/unmask 콜백을 56번(그중 등록 때 unmask 8번) 부르고(MMIO라 싸다), nested provider는 칩 콜백 대신 버스 쓰기 18번(I2C라 비싸다, 그러나 스레드라 괜찮다)을 한다. 0인 막대는 숫자만 표시했다.

실무 감각: I2C 400 kHz에서 레지스터 쓰기 1회 ≈ 수십~100 µs. 확장기 자식에서 `disable_irq`/`enable_irq`를 샘플마다 부르면 그만큼 I2C 트래픽이 늘어난다. "버스 쓰기는 bus_sync_unlock 한 곳에 모은다"가 확장기 드라이버 설계의 핵심이다.

---

## 8. 잘못된 방법 — chained handler 안에서 sleep하면 (예제 10)

무엇을 확인하나: `nested=0 sleepy=1` — chained(hardirq) 부모가 sleep하는 STATUS 읽기를 한다. "I2C 확장기를 MMIO GPIO 드라이버 흉내 내서 짰을 때"다.

```sh
#!/bin/sh
# g_bad.sh — chained(hardirq) 부모에서 sleep 하는 STATUS 읽기 → 무슨 일이?
D=/sys/kernel/debug
insmod /h6_mux.ko nested=0 sleepy=1
insmod /h6_cons.ko thr=0
echo "firing once..."
echo 1 > $D/h6_mux/fire
sleep 2
echo "still alive?"; cat $D/h6_mux/regs; cat $D/h6_cons_cnt; echo
```

```text
lab initramfs: 6.18.55-dbg up 5.49s
firing once...
[    6.044804] BUG: sleeping function called from invalid context at kernel/locking/mutex.c:575
[    6.044992] in_atomic(): 1, irqs_disabled(): 1, non_block: 0, pid: 0, name: swapper/0
[    6.045036] preempt_count: 10001, expected: 0
[    6.045062] RCU nest depth: 0, expected: 0
[    6.048760] CPU: 0 UID: 0 PID: 0 Comm: swapper/0 Tainted: G           O        6.18.55-dbg #1 PREEMPT 
[    6.048974] Tainted: [O]=OOT_MODULE
[    6.048995] Hardware name: linux,dummy-virt (DT)
[    6.049751] Call trace:
[    6.050148]  show_stack+0x30/0x90 (C)
[    6.050277]  dump_stack_lvl+0x70/0x98
[    6.050297]  dump_stack+0x18/0x24
[    6.050311]  __might_resched+0x174/0x248
[    6.050327]  __might_sleep+0x4c/0x98
[    6.050341]  __mutex_lock+0x74/0x820
[    6.050356]  mutex_lock_nested+0x2c/0x40
[    6.050370]  h6_read_status+0x60/0xc0 [h6_mux]
[    6.050630]  h6_chained+0x48/0x110 [h6_mux]
[    6.050651]  handle_irq_desc+0x3c/0x68
[    6.050669]  generic_handle_domain_irq+0x24/0x40
[    6.050685]  gic_handle_irq+0x40/0xe0
[    6.050698]  call_on_irq_stack+0x30/0x48
[    6.050713]  do_interrupt_handler+0x88/0x98
[    6.050729]  el1_interrupt+0x38/0x58
[    6.050744]  el1h_64_irq_handler+0x18/0x30
[    6.050758]  el1h_64_irq+0x6c/0x70
[    6.050824]  default_idle_call+0xbc/0x2e8 (P)
...
[    6.055777] [ BUG: Invalid wait context ]
...
[    6.056787] swapper/0/0 is trying to lock:
[    6.056985] ffffbedf25ca2100 (&h6.bus){....}-{4:4}, at: h6_read_status+0x60/0xc0 [h6_mux]
[    6.058966] other info that might help us debug this:
[    6.059409] context-{2:2}
...
[    6.072226] BUG: scheduling while atomic: swapper/0/0/0x00010002
[    6.072256] INFO: lockdep is turned off.
[    6.072291] Modules linked in: h6_cons(O) h6_mux(O)
...
still alive?
status=0x01 mask=0x00 parent=1 mask_ops=0 unmask=8 ack=0 set_type=8 bus_rd=1 bus_wr=0
0 0 0 0 0 0 0 0

** 1435215 printk messages dropped **
...
[   32.023949] bad: scheduling from the idle thread!
...
[   32.023371]  h6_chained+0x48/0x110 [h6_mux]
...
[  192.515160]  default_idle_call+0xbc/0x2e8 (P)
[  192.515227]  do_idle+0x284/0x2e0
qemu-system-aarch64: terminating on signal 15 from pid 872089 (bash)
```

(로그는 40 MB였다. 세 BUG의 머리와 첫 call trace만 남기고 `...`로 줄였다. 마지막 줄은 내가 192초에 QEMU를 kill한 흔적이다.)

출력에서 볼 것:

- **세 겹의 경고**가 순서대로 나온다. (1) `BUG: sleeping function called from invalid context` — `CONFIG_DEBUG_ATOMIC_SLEEP`의 `might_sleep()` 검사. `preempt_count: 10001`의 `0x10000`이 HARDIRQ 비트다(F2). (2) `BUG: Invalid wait context` — lockdep: 컨텍스트 레벨 2(hardirq)에서 레벨 4(sleeping lock, mutex)를 잡으려 함. (3) `BUG: scheduling while atomic` — `usleep_range`가 실제로 `schedule()`을 불렀다.
- call trace가 우리가 그린 경로 그대로다: `el1h_64_irq` → `gic_handle_irq` → `generic_handle_domain_irq` → `handle_irq_desc` → **`h6_chained`** → `h6_read_status` → `mutex_lock`.
- 인터럽트가 **idle 태스크(swapper/0) 위에서** 걸렸고, hardirq에서 schedule하니 `bad: scheduling from the idle thread!`가 무한 반복된다. CPU0은 그 뒤로 printk 폭풍에 갇혔다(약 280만 줄 drop). CPU1에서 돌던 셸은 살아서 `still alive?`를 찍었지만 **자식 handler는 한 번도 안 불렸고**(카운트 전부 0, `status=0x01` 그대로), poweroff도 못 하고 멈췄다.
- 디버그 옵션이 없는 양산 커널이었다면 (1)(2)는 안 나오고 (3)만, 혹은 조용히 hang이었을 것이다(실행하지 않음, 추정). **새 provider 드라이버는 반드시 `DEBUG_ATOMIC_SLEEP` + `PROVE_LOCKING` 커널에서 한 번 돌려 본다.**

고치는 법: 예제 9 그대로 — `nested=1`(부모 threaded + `handle_nested_irq` + 자식 `irq_set_nested_thread`). 같은 `sleepy=1`인데 BUG 없이 18번 모두 처리됐다.

---

## 9. 실물에서 보기 — pl061(chained), pca953x(nested), gpiolib·regmap-irq 헬퍼

### 9.1 예제 11 — 랩 커널 소스에서 두 패턴 찾기

무엇을 확인하나: QEMU virt의 GPIO 컨트롤러 PL061(MMIO)과 I2C 확장기 PCA953x 드라이버가 각각 chained/nested를 쓰는지 소스로 확인한다.

```sh
cd /var/lab/src/linux-6.18.55
grep -n "girq\|chained" drivers/gpio/gpio-pl061.c
grep -n "threaded = true\|parent_handler\|request_threaded_irq\|handle_nested_irq\|irq_bus_lock" drivers/gpio/gpio-pca953x.c
grep -n "irq_set_nested_thread\|irq_set_chained_handler_and_data" drivers/gpio/gpiolib.c
grep -n "handle_nested_irq\|request_threaded_irq\|irq_bus_lock\|bus_sync_unlock" drivers/base/regmap/regmap-irq.c
```

```text
21:#include <linux/irqchip/chained_irq.h>
223:	chained_irq_enter(irqchip, desc);
232:	chained_irq_exit(irqchip, desc);
314:	struct gpio_irq_chip *girq;
348:	girq = &pl061->gc.irq;
349:	gpio_irq_chip_set_chip(girq, &pl061_irq_chip);
350:	girq->parent_handler = pl061_irq_handler;
351:	girq->num_parents = 1;
352:	girq->parents = devm_kcalloc(dev, 1, sizeof(*girq->parents),
354:	if (!girq->parents)
356:	girq->parents[0] = irq;
357:	girq->default_type = IRQ_TYPE_NONE;
358:	girq->handler = handle_bad_irq;
843:static void pca953x_irq_bus_lock(struct irq_data *d)
932:	.irq_bus_lock		= pca953x_irq_bus_lock,
1037:			handle_nested_irq(nested_irq);
1086:	girq->parent_handler = NULL;
1091:	girq->threaded = true;
1094:	ret = devm_request_threaded_irq(dev, client->irq, NULL, pca953x_irq_handler,
1829:		irq_set_nested_thread(irq, 1);
1855:		irq_set_nested_thread(irq, 0);
2119:			irq_set_chained_handler_and_data(gc->irq.parents[i],
2154:			irq_set_chained_handler_and_data(irq->parents[i],
300:	.irq_bus_lock		= regmap_irq_lock,
301:	.irq_bus_sync_unlock	= regmap_irq_sync_unlock,
515:			handle_nested_irq(irq_find_mapping(data->domain, i));
930:	ret = request_threaded_irq(irq, NULL, regmap_irq_thread,
```

(네 명령의 출력이 차례로 이어진다: pl061 → pca953x → gpiolib → regmap-irq.) PL061의 chained handler 본체(랩 소스, 발췌 없이 전체):

```c
static void pl061_irq_handler(struct irq_desc *desc)
{
	unsigned long pending;
	int offset;
	struct gpio_chip *gc = irq_desc_get_handler_data(desc);
	struct pl061 *pl061 = gpiochip_get_data(gc);
	struct irq_chip *irqchip = irq_desc_get_chip(desc);

	chained_irq_enter(irqchip, desc);

	pending = readb(pl061->base + GPIOMIS);
	if (pending) {
		for_each_set_bit(offset, &pending, PL061_GPIO_NR)
			generic_handle_domain_irq(gc->irq.domain,
						  offset);
	}

	chained_irq_exit(irqchip, desc);
}
```

우리 `h6_chained`와 **줄 단위로 같은 모양**이다(`readb(GPIOMIS)` = masked interrupt status). 그리고 pl061은 `irq_set_type`에서 `irq_set_handler_locked(d, handle_level_irq / handle_edge_irq)`로 flow handler를 바꾼다(소스 168·177행).

### 9.2 gpiolib irqchip 헬퍼 — `struct gpio_irq_chip`

GPIO 컨트롤러는 domain·map·chained 등록을 직접 하지 않고 `gpio_chip.irq`(`struct gpio_irq_chip`)를 채운 뒤 `devm_gpiochip_add_data()`만 부르면 gpiolib가 대신 해 준다.

| 필드 | 뜻 | MMIO(pl061) | I2C 확장기(pca953x) |
|---|---|---|---|
| `chip` (`gpio_irq_chip_set_chip`) | 자식용 `irq_chip` (`IRQCHIP_IMMUTABLE`) | `pl061_irq_chip` | `pca953x_irq_chip` |
| `parent_handler` | chained handler | `pl061_irq_handler` | `NULL` |
| `parents`, `num_parents` | 부모 virq 목록 | GIC SPI 1개 | 없음 |
| `threaded` | true면 자식에 `irq_set_nested_thread` | false | **true** |
| `handler`, `default_type` | 자식 기본 flow handler·트리거 | `handle_bad_irq`, NONE (set_type이 교체) | `handle_simple_irq`, NONE |
| `parent_domain`, `child_to_parent_hwirq` | hierarchical GPIO (핀마다 GIC SPI 1개인 SoC) | — | — |

nested 확장기는 `parent_handler` 없이 **부모 IRQ를 드라이버가 직접 `devm_request_threaded_irq`**로 받는다(pca953x 1094행, 플래그는 `IRQF_ONESHOT | IRQF_SHARED` — 부모 줄 공유도 허용한다). 그리고 앞서 본 대로 `can_sleep` 칩이 `parent_handler`를 주면 gpiolib가 거부한다.

### 9.3 regmap-irq — 상태/마스크 레지스터 칩용 헬퍼 (H2와 연결)

PMIC, 오디오 코덱, 충전 IC처럼 "STATUS·MASK·ACK 레지스터 묶음 + 비트 하나 = 인터럽트 하나"인 칩은 regmap-irq를 쓰면 이 노트의 `h6_nested` 전체(domain, nested 스레드, bus_lock 캐시, W1C)를 **표 하나로** 대체한다. 내부는 `regmap_irq_thread` + `handle_nested_irq` + `regmap_irq_lock/sync_unlock`(위 grep 515·930·300행) — 우리가 손으로 짠 것과 같은 구조다. 아래는 구조 예시이며 **실행하지 않았다**(레지스터 주소는 가상의 칩):

```c
/* 가상의 PMIC: 0x10=STATUS, 0x11=MASK, 0x12=ACK (실행하지 않은 예시) */
static const struct regmap_irq my_pmic_irqs[] = {
	REGMAP_IRQ_REG(0, 0, BIT(0)),	/* 전원 버튼 */
	REGMAP_IRQ_REG(1, 0, BIT(1)),	/* 충전기 연결 */
	REGMAP_IRQ_REG(2, 0, BIT(2)),	/* 과열 */
};

static const struct regmap_irq_chip my_pmic_irq_chip = {
	.name = "my_pmic",
	.status_base = 0x10,
	.mask_base = 0x11,
	.ack_base = 0x12,
	.num_regs = 1,
	.irqs = my_pmic_irqs,
	.num_irqs = ARRAY_SIZE(my_pmic_irqs),
};

/* probe 안에서 (regmap은 devm_regmap_init_i2c로 만든 것) */
ret = devm_regmap_add_irq_chip(dev, regmap, client->irq, IRQF_ONESHOT, 0,
			       &my_pmic_irq_chip, &irq_data);
virq = regmap_irq_get_virq(irq_data, 1);	/* "충전기 연결" 자식의 virq */
```

`devm_regmap_add_irq_chip()` 시그니처와 `regmap_irq_chip` 필드 이름은 랩 소스 `include/linux/regmap.h`에서 확인했다. MFD 드라이버는 이 `irq_data`의 domain을 하위 셀(충전기, RTC, 버튼 드라이버)에 넘겨 각자 `platform_get_irq`로 받게 한다.

---

## 10. spurious·공유 IRQ — cascade에서 달라지는 점 (예제 12)

무엇을 확인하나: STATUS가 0인 채로 부모만 5번 울린다(노이즈, 이미 다른 경로가 처리한 인터럽트, 확장기 INT 글리치를 흉내). chained와 nested에서 커널이 이를 어떻게 보나?

```sh
#!/bin/sh
# g_spur.sh <nested> — STATUS=0 인 채로 부모만 5번 울리면? (가짜/spurious 부모 인터럽트)
D=/sys/kernel/debug
insmod /h6_mux.ko nested=$1; insmod /h6_cons.ko thr=$1
for i in 1 2 3 4 5; do echo 0 > $D/h6_mux/fire; sleep 0.2; done
cat $D/h6_mux/regs
P=$(grep -E "h6_parent" /proc/interrupts | cut -d: -f1 | tr -d ' '); [ -z "$P" ] && P=22
echo "--- /proc/irq/$P/spurious"; cat /proc/irq/$P/spurious
grep -E "^ *$P:" /proc/interrupts || echo "(irq $P: /proc/interrupts 에 줄 없음)"
```

```text
##### nested=0
lab initramfs: 6.18.55-dbg up 9.01s
status=0x00 mask=0x00 parent=5 mask_ops=0 unmask=8 ack=0 set_type=8 bus_rd=5 bus_wr=0
--- /proc/irq/22/spurious
cat: can't open '/proc/irq/22/spurious': No such file or directory
(irq 22: /proc/interrupts 에 줄 없음)
[   12.079840] reboot: Power down
##### nested=1
lab initramfs: 6.18.55-dbg up 11.05s
status=0x00 mask=0x00 parent=5 mask_ops=0 unmask=8 ack=0 set_type=8 bus_rd=5 bus_wr=8
--- /proc/irq/22/spurious
count 4
unhandled 1
last_unhandled 4294679928 ms
 22:          5          0 GIC-0 152 Edge      h6_parent
[   13.892937] reboot: Power down
```

(`sh boot.sh g_spur.sh 0`, `sh boot.sh g_spur.sh 1` 두 번의 출력. `#####` 줄은 내가 붙인 구분자다.)

출력에서 볼 것:

- **chained 부모는 보이지 않는다**: 5번 울렸고(`parent=5`) 다 헛방이었는데 `/proc/interrupts` 줄도, `/proc/irq/22/` 디렉터리도 없다(그 디렉터리는 `request_irq` 때 만들어진다). 커널의 spurious 감지(`note_interrupt`, F6 §8)는 부모에서 **돌지 않는다**. 고장 난 칩이 INT를 계속 들고 있으면 "irq N: nobody cared"로 끊어 주지 않고 CPU 하나가 chained handler 루프에 묶인다. chained provider는 "STATUS가 0이면 어떻게 하나"를 직접 설계해야 한다(대부분 그냥 return — 레벨 부모라면 폭풍이 된다).
- **nested 부모는 일반 threaded IRQ라서 감지된다**: `IRQ_NONE`이 `note_interrupt`로 간다. `count 4`인 이유: threaded IRQ의 판정은 **한 번 늦게** 한다(첫 인터럽트는 `SPURIOUS_DEFERRED`만 세우고 return — 랩 소스 `kernel/irq/spurious.c`). `unhandled 1`인 이유: 헛방 사이 간격이 `HZ/10`(100 ms)보다 길면 카운터를 1로 리셋한다("odd spurious IRQ caused by bus asynchronicity"는 봐준다). 우리가 0.2 s 간격으로 쐈기 때문이다. 같은 소스에서 10만 번 중 99,900번 이상 헛방이어야 줄을 끈다.
- **공유 IRQ와 cascade**: 확장기 두 개의 INT를 와이어드-OR로 GPIO 핀 하나에 묶으면 부모 줄이 공유된다. 각 확장기 드라이버가 `IRQF_SHARED | IRQF_ONESHOT`으로 threaded 등록하고, 자기 STATUS가 0이면 반드시 `IRQ_NONE`을 반환해야 한다(우리 `h6_parent_thread`처럼). 공유 + ONESHOT은 모든 공유자가 같은 ONESHOT 설정이어야 등록된다. chained handler는 `request_irq`가 아니므로 **공유할 수 없다**(`_IRQ_NOREQUEST`).

---

## 11. 커널 안에서는 무슨 일이 일어나나

### 11.1 provider API → 커널 경로

| provider가 부르는 것 | 커널 안 | 이 노트에서 확인한 곳 |
|---|---|---|
| `irq_domain_create_linear(fwnode, 8, ops, data)` | `irq_domain_instantiate` → `revmap[8]` 할당, domain 리스트·debugfs 등록 | 예제 7 `domains/:h6-mux` size 8 |
| (consumer) `platform_get_irq(pdev, i)` | `of_irq_get` → `of_irq_parse_one`(셀 개수 = 부모 `#interrupt-cells`) → `irq_create_of_mapping` → `irq_create_fwspec_mapping` → `ops->xlate` → `irq_domain_alloc_descs` → `ops->map` → `irq_set_irq_type` | 예제 7 child N: virq·hwirq·type |
| `irq_set_chip_and_handler(virq, chip, handle_level_irq)` | `desc->irq_data.chip = chip`, `desc->handle_irq = handle` | 예제 7 debugfs `handler:`·`chip:` |
| `irq_set_chained_handler_and_data(parent, fn, data)` | 부모 desc `handle_irq = fn`, `NOREQUEST`·`NOPROBE`·`NOTHREAD` 설정, `irq_activate_and_startup` | 예제 7 부모 debugfs |
| (인터럽트) chained | `gic_handle_irq` → `generic_handle_domain_irq`(GIC) → `fn` → `chained_irq_enter` → `generic_handle_domain_irq`(내 domain) → 자식 flow → `chained_irq_exit`(EOI) | 예제 8 |
| `irq_set_nested_thread(virq, true)` | `_IRQ_NESTED_THREAD` 설정 → `__setup_irq`에서 hard handler 거부, 스레드 생성 생략 | 예제 9 (a), 스레드 1개 |
| (인터럽트) nested | 부모 `handle_fasteoi_irq`(ONESHOT mask) → `irq/22` 스레드 → 내 thread_fn → `handle_nested_irq` → 자식 `thread_fn` → 부모 `unmask_threaded_irq` | 예제 9 (d) |
| `.irq_bus_lock`/`.irq_bus_sync_unlock` | `chip_bus_lock(desc)` … `chip_bus_sync_unlock(desc)`가 `__setup_irq`, `enable_irq`, `disable_irq`, `irq_set_irq_type` 등을 감쌈 | 예제 9 `bus_wr=8` |
| `irq_set_irqchip_state(parent, PENDING, true)` | 부모 칩 `irq_set_irqchip_state` → GIC `GICD_ISPENDR` | 모든 예제의 `fire` |

### 11.2 다시 만나는 곳

- **F6**: consumer 쪽 전부. 이 노트의 자식 virq도 F6의 API(`request_threaded_irq`, affinity, `disable_irq`)로 그대로 쓴다. 다만 cascade 자식은 affinity를 바꿀 수 없다.
- **F2·F4**: chained handler 안은 hardirq — mutex·sleep 금지, spinlock만. nested 경로는 process 컨텍스트라 mutex·I2C 가능.
- **D4**: `interrupt-controller`, `#interrupt-cells`, `interrupt-parent`, `interrupts-extended`(여러 부모에 걸친 인터럽트).
- **H2**: regmap-irq. **H4**: gpiolib의 `gpio_irq_chip`, `gpiod_to_irq()`(GPIO 디스크립터 → virq, 내부적으로 `irq_create_mapping`). **H5**: 확장기가 쓰는 I2C 전송.
- **H11**: PCIe MSI/MSI-X — 예제 1·2의 PCI-MSIX domain을 드라이버 입장(`pci_alloc_irq_vectors`)에서 본다.
- **M·N**: PREEMPT_RT에서는 거의 모든 handler가 스레드로 돌지만 **chained handler는 강제 스레드화되지 않는다**(`_IRQ_NOTHREAD`). RT 커널에서 chained provider 안의 spinlock은 `raw_spinlock_t`여야 한다(pl061이 `raw_spin_lock_irqsave`를 쓰는 이유).

---

## 12. Embedded 실무에서 다시 보기

### 12.1 "확장기 뒤 IMU 인터럽트가 한 번 오고 다시 안 와요"

예를 들어 센서 키트에서 IMU INT가 PCA9555 핀 3, PCA9555 INT가 SoC GPIO3_21(level-low)에 연결되어 있다고 하자. 확인 순서:

1. `grep -E "pca|imu" /proc/interrupts` — IMU 자식 줄(칩 이름 `pca953x` 류)의 카운트가 1에서 멈췄나, 부모(`gpio-mxc` 같은 SoC GPIO 칩 이름, `pca953x` 등록 이름) 카운트는?
2. 부모 카운트도 1에서 멈춤 → 부모가 **ONESHOT으로 mask된 채** 스레드가 끝나지 않았다(I2C hang → H5 버스 복구), 또는 확장기의 입력 레지스터를 읽지 않아 INT가 풀리지 않았는데 부모가 **edge**로 설정됐다(레벨 INT는 DT에서 `IRQ_TYPE_LEVEL_LOW`로 둔다).
3. `/sys/kernel/debug/irq/irqs/<부모>`의 `ddepth`(disable 깊이)·`istate`, `ps`의 `irq/N-` 스레드 상태(D 상태면 I2C 대기).
4. 부모 카운트는 오르는데 자식이 0 → 자식이 mask 상태(확장기의 MASK 레지스터, `bus_sync_unlock`이 실패했는지 dmesg), 또는 IMU 드라이버가 `request_irq`로 hard handler만 걸었다(§7.3 (a)의 `-EINVAL` probe 실패를 놓침).

### 12.2 DT 연결 실수

```dts
/* 흔한 실수 예 (실행하지 않은 예시) */
imu@68 {
	compatible = "invensense,icm42605";
	reg = <0x68>;
	interrupt-parent = <&gpio_exp>;		/* 확장기 노드에 interrupt-controller; 가 없으면 매핑 실패 */
	interrupts = <3>;			/* 확장기는 #interrupt-cells = <2> → <3 IRQ_TYPE_LEVEL_LOW> 여야 함 */
};
```

셀 개수가 틀리면 `of_irq_parse_one`이 다음 인터럽트의 셀까지 잘못 읽거나 실패한다. 부모 노드에 `interrupt-controller;`가 없으면 domain을 못 찾아 `-EPROBE_DEFER`가 무한히 반복되고 `/sys/kernel/debug/devices_deferred`에 IMU가 남는다.

### 12.3 MSI가 안 잡힌다 (PCIe Wi-Fi, NVMe, AI 가속기)

`lspci -vvv`에서 `MSI-X: Enable-`이고 `/proc/interrupts`에 `ITS-PCI-MSIX-…` 대신 레거시 INTx(`GIC … Level`)가 보이면: (1) DT의 PCIe 노드에 `msi-map`/`msi-parent`가 ITS를 가리키는지, (2) 부팅 로그에 `ITS [mem …]`, `ITS@…: allocated … Devices` 줄이 있는지(예제 3), (3) 펌웨어(TF-A)가 GICR/ITS 테이블용 메모리를 예약했는지 본다. 예제 1·2처럼 debugfs `irqs/N`의 `address_lo`가 ITS `GITS_TRANSLATER`를 가리키는지가 결정적 증거다. Don의 NVMe/PCIe 경험과 바로 연결된다 — 장치가 쓰는 MSI-X 테이블의 주소/데이터가 이 값이다.

### 12.4 확장기 자식의 지연 예산

센서가 1 kHz로 data-ready를 내는데 확장기 뒤에 있으면: 부모 hardirq → 스레드 깨우기(F6에서 측정한 수십~수백 µs) → I2C 상태 읽기(400 kHz에서 약 100 µs) → W1C 쓰기 → 자식 thread_fn에서 센서 FIFO 읽기(또 I2C). 1 ms 주기에 빠듯하다. 실무 해법: 지연에 민감한 INT는 **SoC GPIO에 직접** 연결하도록 하드웨어 리뷰 때 요청한다. 확장기는 버튼·전원 good·카드 감지처럼 느린 신호용이다.

---

## 13. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| I2C/SPI 확장기를 chained handler로 구현 | `BUG: sleeping function called from invalid context`, `scheduling while atomic`, CPU hang (예제 10) | hardirq에서 버스 전송(sleep) | 부모 threaded + `handle_nested_irq` + `irq_set_nested_thread` (예제 9) |
| `chained_irq_exit` 빠뜨림 | 첫 인터럽트 한 번 뒤로 그 SPI가 다시 안 옴 | GIC에 EOI가 안 가서 active 상태로 남음 | enter/exit를 짝으로 |
| `irq_mask`/`irq_unmask` 안에서 I2C 쓰기 | 같은 sleep BUG, lockdep 경고 | 이 콜백들은 `desc->lock`(raw spinlock) 안 | 캐시만 고치고 `irq_bus_sync_unlock`에서 쓰기 |
| nested 자식에 `request_irq`(hard handler만) | probe `-EINVAL`, 근데 `insmod`는 성공 | nested 자식은 `thread_fn`만 부를 수 있음 | `request_threaded_irq(irq, NULL, fn, IRQF_ONESHOT)` 또는 `request_any_context_irq` |
| level 자식에 `handle_edge_irq` (또는 `set_type`에서 교체 안 함) | 인터럽트 폭풍, 또는 처리 중 잃어버림 | level은 mask가 필요하고 edge는 ack 먼저 | `irq_set_type`에서 `irq_set_handler_locked`로 교체 |
| DT `#interrupt-cells`와 consumer 셀 수 불일치 | consumer probe 실패·엉뚱한 hwirq | `of_irq_parse_one`이 부모 셀 수로 자름 | 부모 binding 문서대로 셀 수 맞추기 |
| provider 노드에 `interrupt-controller;` 누락 | consumer가 영원히 `-EPROBE_DEFER` | domain을 이 노드에서 찾지 못함 | 속성 추가, `devices_deferred` 확인 |
| chained 부모에서 STATUS가 0일 때 처리 없음 (레벨 부모) | CPU 하나 100%, "nobody cared"도 안 나옴 | chained 부모는 `note_interrupt` 대상이 아님 (예제 12) | 원인 칩 레지스터 확인, 필요하면 threaded로 |
| `irq_create_mapping` 후 해제 없이 모듈 제거 | 재로드 시 이상한 virq, `irq_desc` 누수 | 매핑·domain이 남음 | remove에서 chained 해제 → `irq_dispose_mapping` → `irq_domain_remove` |
| 데모·테스트용 SPI를 아무거나 고름 | 가끔 MSI 장치와 충돌 | QEMU virt v2m은 SPI 48–111을 MSI로 씀 (예제 3) | SoC SPI 맵과 MSI 프레임 범위 확인 |

---

## 14. 면접에서 이렇게 말한다

**Q.** What is an irq_domain and why does Linux need two interrupt numbers?

**A.** irq_domain은 인터럽트 컨트롤러마다 하나 있는 "컨트롤러 안 번호(hwirq) ↔ 커널 전역 번호(virq)" 변환표이자 DT 셀 해석기다. SoC에는 GIC, GPIO 컨트롤러, 확장기, MSI 컨트롤러가 계층으로 있고 각자 0번부터 번호를 매기므로 전역 번호가 따로 필요하다. 매핑은 consumer가 `platform_get_irq`를 부를 때 DT → xlate → hwirq → 빈 virq 할당 → `.map` 순서로 만들어지고, 인터럽트가 오면 반대로 hwirq → virq → `irq_desc`로 찾는다.

> An irq_domain is the per-controller translation between the controller's local hardware number, the hwirq, and the global Linux IRQ number, the virq. A real SoC has a tree of controllers — GIC, GPIO banks, I2C expanders, MSI controllers — each numbering from zero, so we need a global namespace. The mapping is created lazily: when a consumer calls platform_get_irq, the DT specifier is translated by the parent domain's xlate into hwirq plus trigger type, a free virq is allocated and the domain's map callback sets the irq_chip and flow handler. At interrupt time it goes the other way: the controller reads the hwirq and generic_handle_domain_irq resolves it to the irq_desc.

**꼬리 질문**: linear vs tree domain? → hwirq 공간이 작고 촘촘하면 배열(linear), 크고 드문드문하면 radix tree. GICv3 domain이 tree다(debugfs `size: 0`).

**Q.** What's the difference between a chained handler and a nested threaded handler? When must you use each?

**A.** 둘 다 부모 인터럽트 하나를 여러 자식으로 디멀티플렉스한다. chained는 부모 desc의 flow handler 자리에 내 함수를 앉혀서 hardirq 안에서 상태 레지스터를 읽고 `generic_handle_domain_irq`로 자식 flow handler를 부른다 — MMIO GPIO 컨트롤러용. nested는 부모를 threaded IRQ로 받고, 스레드 안에서 I2C/SPI로 상태를 읽은 뒤 `handle_nested_irq`로 자식의 thread_fn을 같은 스레드에서 직접 부른다 — 상태를 읽는 데 sleep이 필요한 버스 뒤 칩용. 확장기를 chained로 짜면 hardirq에서 sleep하게 되어 커널이 죽는다. 실제로 디버그 커널에서 돌려 보면 `sleeping function called from invalid context`, `scheduling while atomic`이 나오고 CPU가 멈춘다.

> Both demultiplex one parent line into many child IRQs. A chained handler replaces the parent's flow handler and runs entirely in hardirq context: chained_irq_enter, read the status register with readl, call generic_handle_domain_irq for each set bit, chained_irq_exit which sends the EOI. That's right for memory-mapped GPIO controllers. A nested handler is for chips behind a slow bus like an I2C GPIO expander: the parent is requested as a threaded IRQ with IRQF_ONESHOT, the thread does the I2C read, then calls handle_nested_irq, which runs the child's thread_fn directly in the same thread. You must use nested whenever reading or acknowledging the status can sleep — doing an I2C transfer in a chained handler is sleeping in atomic context, and on a debug kernel you get "scheduling while atomic" and a hung CPU.

**꼬리 질문**: nested 자식에 `request_irq`로 hard handler만 걸면? → `-EINVAL`. `thread_fn`이 필요하다. 드라이버가 provider를 모르면 `request_any_context_irq`.

**Q.** How does an I2C GPIO expander mask an interrupt if irq_mask is called with a spinlock held?

**A.** `irq_bus_lock`/`irq_bus_sync_unlock` 콜백을 쓴다. genirq는 `request_irq`, `enable_irq`, `disable_irq`, `irq_set_irq_type` 같은 sleep 가능한 경로에서 먼저 `irq_bus_lock`(드라이버 mutex)을 부르고, spinlock 안에서 `irq_mask`/`irq_unmask`를 부른 뒤, spinlock을 풀고 `irq_bus_sync_unlock`을 부른다. 드라이버는 mask 콜백에서 캐시만 바꾸고 sync_unlock에서 I2C로 한 번에 쓴다. pca953x와 regmap-irq가 이 패턴이다.

> Through the irq_bus_lock and irq_bus_sync_unlock callbacks. The genirq core wraps the sleepable slow paths — request_irq, enable_irq, disable_irq, set_type, set_wake — with chip_bus_lock and chip_bus_sync_unlock. Inside, under the descriptor's raw spinlock, irq_mask and irq_unmask only update a cached mask in memory. Then sync_unlock, which runs outside the spinlock and may sleep, writes the cached mask to the device over I2C in one transaction. pca953x and regmap-irq both do exactly this. It also batches updates, which matters because each I2C write costs tens to hundreds of microseconds.

**Q.** Walk me through GICv2 versus GICv3. What is the ITS for?

**A.** v2는 Distributor + MMIO CPU interface, CPU 8개 한계, SGI/PPI/SPI만 있다. v3는 CPU interface를 시스템 레지스터(ICC)로 옮기고, CPU마다 Redistributor를 두고, affinity 라우팅으로 CPU 수 제한을 없앴고, 메시지 기반 LPI(8192부터)를 추가했다. ITS는 PCIe 장치의 MSI 쓰기(DeviceID + EventID)를 메모리 테이블로 번역해 LPI와 대상 CPU를 정한다. 그래서 장치마다 EventID가 0부터 시작해도 겹치지 않고, MSI가 SPI를 소모하지 않으며, 장치별 격리가 된다. v2에서 MSI를 쓰려면 v2m 같은 프레임이 MSI 쓰기를 SPI로 바꾼다.

> GICv2 has a distributor and a memory-mapped CPU interface, supports up to eight CPUs and only SGIs, PPIs and SPIs. GICv3 moves the CPU interface to system registers, adds a per-CPU redistributor, uses affinity routing so it scales beyond eight cores, and adds LPIs — message-based interrupts starting at 8192 whose configuration lives in memory tables. The ITS translates an MSI write — the device ID taken from the requester ID plus the event ID in the data — into an LPI and a target redistributor. On QEMU I compared the same virtio device: on v2 with v2m the MSI data was literally the SPI number, on v3 the data was event ID 0 and the ITS turned it into LPI 8192. ITS also gives per-device isolation, which is what VFIO relies on.

**Q.** What's a hierarchical irq_domain, and how is it different from a cascaded controller?

**A.** cascade는 부모 줄 하나에 자식 여러 개가 매달려 인터럽트 하나에 desc가 두 번 돈다(부모 → 디멀티플렉스 → 자식). hierarchy는 자식 virq 하나가 부모 domain들의 `irq_data` 사슬을 함께 가져서, root(GIC)가 바로 그 virq의 desc를 부른다 — 디멀티플렉스 단계가 없다. MSI(PCI-MSI → ITS → GIC)나 핀마다 GIC SPI가 따로 있는 GPIO가 hierarchy다. mask·affinity 같은 요청은 `irq_chip_mask_parent` 같은 헬퍼로 사슬을 따라 부모에 전달된다.

> In a cascade, many child IRQs share one parent line, so each interrupt runs two descriptors: the parent's chained or threaded handler demultiplexes and then calls the child. In a hierarchy, each child virq owns a chain of irq_data, one per domain level, and the root controller dispatches that virq directly — there's no demux step. MSI is the classic case: PCI-MSI domain, then ITS or v2m, then GIC. Operations like mask or set_affinity are forwarded up the chain with helpers such as irq_chip_mask_parent. In debugfs you see it as nested "parent:" blocks under the irq.

**Q.** How would you debug an interrupt that never reaches a driver on a new board?

**A.** 바깥에서 안으로 간다. (1) `/proc/interrupts`에 줄이 있나·카운트가 오르나, (2) 없으면 consumer probe가 defer/실패했나(`devices_deferred`, dmesg의 request 에러), (3) `/sys/kernel/debug/irq/irqs/N`에서 domain·hwirq·chip·트리거·`ddepth`를 보고 DT가 의도한 컨트롤러와 핀에 매핑됐는지, (4) 부모 단계의 카운트가 오르는지 — cascade면 부모 줄을, chained면 debugfs만 가능, (5) 그래도 없으면 하드웨어: 스코프로 INT 선, 극성·트리거, 핀먹스. debugfs `trigger`로 소프트웨어 주입해서 handler 경로를 하드웨어와 분리해 시험할 수도 있다.

> I go from the outside in. First /proc/interrupts: is there a line, does the count move? If not, did the consumer even bind — check dmesg for request errors and devices_deferred. Then /sys/kernel/debug/irq/irqs/N shows the domain, hwirq, chip, trigger type and disable depth, so I can confirm the DT actually mapped it to the controller and pin I intended. Then I check one level up: is the parent line counting? For a chained parent that only shows in debugfs, not in /proc/interrupts. If software looks right, I go to hardware — scope the INT line, check polarity, level versus edge, and pinmux. And with GENERIC_IRQ_INJECTION I can write "trigger" into debugfs to exercise the handler path without the hardware.

**Q.** Why can't you set the affinity of a GPIO expander's child interrupt?

**A.** cascade 자식은 부모 handler(또는 스레드) 안에서 불리므로 부모가 도는 CPU에서만 돈다. 자식 칩에 `irq_set_affinity`가 없어서 `/proc/irq/N/smp_affinity` 쓰기가 실패하거나 무의미하다. 옮기고 싶으면 부모(GIC SPI 또는 부모 스레드)의 affinity를 옮긴다 — 그러면 그 칩의 모든 자식이 함께 옮겨진다.

> Because a cascaded child is dispatched from inside the parent's handler — the chained handler or the parent's IRQ thread — so it always runs wherever the parent runs. The child irq_chip has no irq_set_affinity, and in debugfs the child's effective affinity is simply empty. If you need it elsewhere, you move the parent's affinity, and every child of that controller moves with it.

---

## 15. 직접 해보기

1. **(개념)** GIC SPI 하나에 chained로 매달린 GPIO 컨트롤러가 있다. 핀 5에서 인터럽트가 오면 `irq_desc`는 몇 개가 실행되고, `gic_eoi_irq`는 누가 몇 번 부르나? 정답/힌트: desc 2개(부모 GIC desc → 자식 desc), EOI는 `chained_irq_exit`가 1번(예제 8).
2. **(코드)** `h6_fast_chip`에 `.irq_mask_ack`를 추가해 mask와 ack를 한 콜백에서 하도록 바꾸고, 예제 7의 `mask_ops`·`ack` 숫자가 어떻게 바뀌는지 예측한 뒤 확인하라. 정답/힌트: `mask_ack_irq()`는 `irq_mask_ack`가 있으면 그것만 부른다 → 별도 `h6_mask`/`h6_ack` 호출이 level 15번만큼 줄고, 새 카운터가 15가 된다.
3. **(코드)** `h6_mux`에 remove 경로를 구현하라(`suppress_bind_attrs` 제거). `echo h6-mux > /sys/bus/platform/drivers/h6_mux/unbind` 후 `/sys/kernel/debug/irq/domains/`에서 `:h6-mux`가 사라지는지 확인. 정답/힌트: 소비자를 먼저 unbind, 그다음 `irq_set_chained_handler_and_data(parent, NULL, NULL)` → hwirq 0–7에 `irq_dispose_mapping(irq_find_mapping(dom, i))` → `irq_domain_remove(dom)`.
4. **(관찰)** 예제 1·2를 `-device virtio-net-pci,netdev=n0 -netdev user,id=n0`처럼 MSI-X 벡터가 많은 장치로 바꾸고, v3에서 LPI 번호(0x2000, 0x2001, …)와 EventID가 장치마다 어떻게 붙는지 debugfs로 확인하라. 정답/힌트: 장치마다 PCI-MSIX domain의 hwirq(벡터)는 0부터, ITS domain hwirq(LPI)는 전역에서 연속 할당. `msg_data`가 EventID다.
5. **(코드)** `h6_cons`를 `request_any_context_irq()`로 바꿔 `nested=0`과 `nested=1` 두 경우 모두 probe가 성공하는지, 반환값(`IRQC_IS_HARDIRQ`=0, `IRQC_IS_NESTED`=1)을 dmesg로 찍어 확인하라. 정답/힌트: 랩 소스 `kernel/irq/manage.c`의 `request_any_context_irq`가 `irq_settings_is_nested_thread`를 보고 갈라진다. handler 인자는 nested일 때 thread_fn으로 쓰인다.
6. **(관찰)** 예제 12의 nested 실험에서 `sleep 0.2`를 빼고 `fire 0`을 200번 연속으로 쏘면 `/proc/irq/22/spurious`의 `unhandled`가 어떻게 되나? 정답/힌트: 간격이 100 ms보다 짧으면 리셋되지 않고 누적된다(단, threaded라 판정이 한 번씩 늦다). 10만 번 중 99,900번을 넘겨야 끈다.
7. **(개념)** 센서 INT를 확장기 대신 SoC GPIO에 바로 연결하면 예제 9의 경로에서 무엇이 빠지나? 정답/힌트: 부모 스레드 깨우기 + I2C 상태 읽기 + W1C 쓰기가 빠지고, chained(MMIO) 경로의 hardirq 디멀티플렉스 한 단계만 남는다 — 센서 드라이버 자신의 threaded handler만 남는다.

---

## 16. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| provider / consumer | 인터럽트 공급자 / 사용자 | 컨트롤러 드라이버(irq_chip·domain을 만든다) vs 장치 드라이버(`request_irq`) |
| `irq_chip` | 컨트롤러 조작 콜백 묶음 | mask/unmask/ack/eoi/set_type/set_affinity/bus_lock |
| `irq_desc` | virq 하나의 디스크립터 | flow handler, action 리스트, 통계, 락 |
| `irq_data` | desc 안의 칩 쪽 데이터 | irq, hwirq, domain, chip, chip_data, parent_data(hierarchy) |
| flow handler | 트리거별 공통 절차 | `handle_level_irq`, `handle_edge_irq`, `handle_fasteoi_irq`, `handle_simple_irq` |
| `irq_domain` | hwirq ↔ virq 변환표 | linear/tree/hierarchy, `xlate`·`map`·`alloc` 콜백 |
| hwirq | 컨트롤러 안 번호 | GIC INTID, GPIO 핀 번호, MSI 벡터 |
| virq | Linux IRQ 번호 | 런타임 할당, `/proc/interrupts` 첫 칸 |
| xlate / translate | DT 셀 해석 콜백 | `<7 IRQ_TYPE_EDGE_RISING>` → hwirq 7 + 타입 |
| `#interrupt-cells` | DT 인터럽트 지정자 셀 수 | 부모 컨트롤러가 정한다 (GIC 3, GPIO 보통 2) |
| cascade | 부모 줄 하나에 자식 여럿 | chained 또는 nested로 디멀티플렉스 |
| chained handler | hardirq 디멀티플렉서 | 부모 desc의 handle_irq를 대체, `chained_irq_enter/exit` |
| nested threaded IRQ | 부모 스레드 안의 자식 처리 | `handle_nested_irq`, `irq_set_nested_thread` |
| `irq_bus_lock` | 느린 버스 칩용 락 콜백 | mask 변경을 모아 `irq_bus_sync_unlock`에서 버스로 쓴다 |
| hierarchy domain | 부모 domain 사슬 | MSI → ITS → GIC. 디멀티플렉스 없이 root가 바로 dispatch |
| GICv2m | v2용 MSI 프레임 | MSI 쓰기 data = SPI 번호 |
| ITS | Interrupt Translation Service | (DeviceID, EventID) → LPI + 대상 CPU, 메모리 테이블 |
| LPI | Locality-specific Peripheral Interrupt | v3의 메시지 기반 인터럽트, INTID 8192부터 |
| Redistributor (GICR) | CPU별 GIC 블록 | SGI/PPI 설정, LPI pending 테이블 |
| `gpio_irq_chip` | gpiolib의 irqchip 헬퍼 | `parent_handler`(chained) 또는 `threaded`(nested) |
| regmap-irq | regmap 기반 irqchip 헬퍼 | status/mask/ack 레지스터 표만 주면 nested 칩 완성 |
| `request_any_context_irq` | 컨텍스트 자동 선택 등록 | nested면 threaded로, 아니면 hard로 |

---

## 17. 요약 & 체크리스트

인터럽트의 provider 쪽은 세 조각이다. `irq_chip`은 한 줄을 mask/ack/EOI/설정하는 하드웨어 콜백, `irq_domain`은 hwirq ↔ virq 변환과 DT 셀 해석, 그리고 부모 줄에서 자식을 골라 주는 디멀티플렉서. 디멀티플렉서는 상태 레지스터를 sleep 없이 읽을 수 있으면 **chained**(hardirq, `chained_irq_enter/exit` + `generic_handle_domain_irq` — pl061), 버스 전송이 필요하면 **nested**(부모 threaded + `handle_nested_irq`, 자식 mask는 `irq_bus_lock`/`irq_bus_sync_unlock`으로 모아 쓰기 — pca953x, regmap-irq)다. MSI처럼 1:1 매핑이면 디멀티플렉스 없이 **hierarchy domain**(PCI-MSI → ITS/v2m → GIC)이 된다. QEMU에서 가짜 컨트롤러 `h6_mux`를 두 방식으로 짜서 콜백 순서(ftrace), 숫자(18번 자극에 mask/ack 33 vs 버스 쓰기 18), 그리고 잘못 짰을 때의 커널 BUG를 직접 확인했다. chained 부모는 `/proc/interrupts`와 spurious 감지에서 보이지 않는다는 점, cascade 자식의 affinity는 부모를 따른다는 점이 디버그 때 중요하다.

- [ ] `irq_chip`의 주요 콜백 8개가 각각 언제, 어떤 컨텍스트에서 불리는지 설명할 수 있다
- [ ] `handle_level_irq`/`handle_edge_irq`/`handle_fasteoi_irq`/`handle_simple_irq`가 부르는 콜백 순서를 말할 수 있다
- [ ] DT `interrupts` 셀이 `xlate` → hwirq → virq → `.map`으로 바뀌는 경로를 그릴 수 있다
- [ ] linear domain + `.map` + `.xlate`로 2차 인터럽트 컨트롤러 드라이버를 짤 수 있다
- [ ] chained handler를 `chained_irq_enter/exit`와 함께 올바르게 짤 수 있다
- [ ] I2C 확장기가 nested여야 하는 이유와 `handle_nested_irq`·`irq_set_nested_thread`의 역할을 설명할 수 있다
- [ ] `irq_bus_lock`/`irq_bus_sync_unlock`으로 mask 변경을 버스로 모아 쓰는 패턴을 설명할 수 있다
- [ ] GICv2(+v2m)과 GICv3(+ITS)의 차이, SGI/PPI/SPI/LPI 범위를 말할 수 있다
- [ ] `/sys/kernel/debug/irq/irqs/N`과 `domains/`에서 hierarchy 사슬과 MSI 주소/데이터를 읽을 수 있다
- [ ] cascade와 hierarchy domain의 차이를 설명할 수 있다
- [ ] gpiolib `gpio_irq_chip`의 `parent_handler`/`threaded`, regmap-irq를 언제 쓰는지 안다
- [ ] chained 부모가 `/proc/interrupts`·spurious 감지에서 빠진다는 것과 그 디버그 의미를 안다

## 참고 자료

- docs.kernel.org — "Linux generic IRQ handling" (genirq, flow handler, chained handler): https://docs.kernel.org/core-api/genericirq.html
- docs.kernel.org — "The irq_domain interrupt number mapping library": https://docs.kernel.org/core-api/irq/irq-domain.html
- docs.kernel.org — "GPIO Driver Interface" (gpio_irq_chip, chained vs nested, `can_sleep`): https://docs.kernel.org/driver-api/gpio/driver.html
- 랩 커널 소스 (linux-6.18.55): `kernel/irq/chip.c`(`handle_nested_irq`, `handle_level_irq`, `irq_set_chained_handler_and_data`), `kernel/irq/manage.c`(`__setup_irq`, `request_any_context_irq`), `kernel/irq/spurious.c`(`note_interrupt`), `kernel/irq/irqdomain.c`, `include/linux/irqchip/chained_irq.h`, `drivers/irqchip/irq-gic.c`, `drivers/irqchip/irq-gic-v3.c`, `drivers/irqchip/irq-gic-v3-its.c`, `drivers/irqchip/irq-gic-v2m.c`, `drivers/gpio/gpio-pl061.c`, `drivers/gpio/gpio-pca953x.c`, `drivers/gpio/gpiolib.c`, `drivers/base/regmap/regmap-irq.c`
- elixir.bootlin.com 에서 같은 파일 열람: https://elixir.bootlin.com/linux/v6.18/source/kernel/irq/chip.c
- Arm, "Arm Generic Interrupt Controller Architecture Specification, GIC architecture version 3 and version 4" (IHI 0069) — LPI, ITS, Redistributor
- Arm, "ARM Generic Interrupt Controller Architecture Specification, version 2.0" (IHI 0048)
- Bootlin, "Linux kernel and driver development training" 슬라이드 — interrupt 및 GPIO 장: https://bootlin.com/training/kernel/
