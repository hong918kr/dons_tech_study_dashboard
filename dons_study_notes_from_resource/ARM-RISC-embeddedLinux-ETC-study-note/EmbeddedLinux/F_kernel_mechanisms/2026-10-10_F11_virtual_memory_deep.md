# F11. 가상 메모리 심화 — 페이지 테이블, TLB, page fault 처리, reclaim, OOM killer, swap 없는 임베디드

> **이 노트를 다 읽으면**: arm64 page table을 커널 모듈로 직접 걸어서 descriptor 비트를 해독할 수 있다 · page fault가 커널 안에서 어떤 함수로 갈라지는지(익명/파일/COW/swap) 추적할 수 있다 · watermark → kswapd → direct reclaim → OOM killer로 이어지는 "메모리 부족 사다리"를 실제 로그로 읽을 수 있다 · swap 없는 장비에서 zram, `oom_score_adj`, memory cgroup으로 OOM 정책을 설계할 수 있다
> **왜 필요한가**: "page fault가 나면 커널은 무엇을 하나?", "TLB와 ASID는 왜 있나?", "OOM killer는 누구를 죽이나, 내 데몬은 어떻게 지키나?", "임베디드에서 swap을 왜 끄나?"는 Embedded Linux 면접의 단골 심화 질문이고, 실무에선 "장비가 가끔 죽어요" 버그의 절반이 여기서 끝난다
> **Don 기준 난이도**: MMU·TLB·캐시 유지보수 명령·NAND 수명은 이미 안다 / 새로 배울 것은 Linux가 그 하드웨어를 **어떤 자료구조(`mm_struct`, VMA, folio, LRU)와 정책(reclaim, OOM)** 으로 굴리는가
> **선행 노트**: A5(유저 관점 가상 메모리), E3(커널 모듈), F9(커널 메모리 관리)

---

## 0. 큰 그림 — 이게 왜 필요한가

A5에서는 유저 입장에서 가상 메모리를 봤다. `mmap`은 약속(VMA)만 주고, 처음 만질 때 page fault로 페이지가 붙고, overcommit 때문에 결국 OOM killer가 나올 수 있다는 것까지. F11은 같은 이야기를 **커널 쪽에서** 다시 본다. 질문은 세 개다.

1. **번역**: MMU가 VA를 PA로 바꿀 때 무엇을 읽나? (page table 구조, descriptor 비트, TLB와 ASID)
2. **fault**: 번역이 실패하면 커널은 어떤 함수 경로로 페이지를 만들어 내나? (익명 / 파일 / COW / swap)
3. **부족**: 줄 페이지가 없으면? (reclaim → swap/zram → OOM killer, 그리고 cgroup으로 범위 제한)

Don의 SSD 펌웨어에 빗대면 이렇다. FTL이 LBA → PBA 매핑 테이블을 들고 있듯 커널은 VA → PA 매핑 테이블(page table)을 들고 있다. FTL의 매핑 캐시(DRAM에 올린 일부 L2P)가 MMU의 TLB다. 빈 블록이 모자라면 FTL이 GC를 돌리듯, 빈 페이지가 모자라면 커널은 reclaim을 돈다. GC로도 안 되면 SSD는 write를 거절(read-only 모드)하지만, 커널은 **프로세스 하나를 죽여서** 메모리를 회수한다. 그게 OOM killer다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="20" y="20" width="150" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="95" y="45" font-size="13" text-anchor="middle">load/store (EL0)</text>
<rect x="200" y="20" width="120" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="260" y="45" font-size="13" text-anchor="middle">TLB 조회</text>
<rect x="360" y="20" width="150" height="40" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="435" y="45" font-size="13" text-anchor="middle">hit: 바로 접근</text>
<line x1="170" y1="40" x2="198" y2="40" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="320" y1="40" x2="358" y2="40" stroke="currentColor" marker-end="url(#ah1)"/>
<rect x="160" y="100" width="200" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="260" y="125" font-size="13" text-anchor="middle">miss: page table walk</text>
<rect x="400" y="100" width="170" height="40" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="485" y="125" font-size="13" text-anchor="middle">valid: TLB 채움</text>
<line x1="260" y1="60" x2="260" y2="98" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="360" y1="120" x2="398" y2="120" stroke="currentColor" marker-end="url(#ah1)"/>
<rect x="130" y="180" width="260" height="40" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="260" y="205" font-size="13" text-anchor="middle">invalid: abort → do_mem_abort</text>
<line x1="260" y1="140" x2="260" y2="178" stroke="currentColor" marker-end="url(#ah1)"/>
<rect x="130" y="260" width="260" height="40" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="260" y="285" font-size="13" text-anchor="middle">handle_mm_fault: 새 페이지</text>
<line x1="260" y1="220" x2="260" y2="258" stroke="currentColor" marker-end="url(#ah1)"/>
<rect x="420" y="260" width="240" height="40" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="540" y="285" font-size="13" text-anchor="middle">free 있음: PTE 설치, 재실행</text>
<line x1="390" y1="280" x2="418" y2="280" stroke="currentColor" marker-end="url(#ah1)"/>
<rect x="100" y="340" width="320" height="40" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="260" y="365" font-size="13" text-anchor="middle">free 없음: reclaim (kswapd, direct)</text>
<line x1="260" y1="300" x2="260" y2="338" stroke="currentColor" marker-end="url(#ah1)"/>
<rect x="460" y="340" width="200" height="40" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="560" y="365" font-size="13" text-anchor="middle">실패: OOM killer</text>
<line x1="420" y1="360" x2="458" y2="360" stroke="currentColor" marker-end="url(#ah1)"/>
<text x="600" y="125" font-size="12" text-anchor="middle">§1–3</text>
<text x="450" y="205" font-size="12" text-anchor="middle">§5</text>
<text x="70" y="365" font-size="12" text-anchor="middle">§7–10</text>
</svg>
```

그림 1 — load 명령 하나가 겪을 수 있는 모든 길. 위쪽 두 줄은 하드웨어(MMU·TLB)가 처리하고, 오렌지부터는 커널 소프트웨어, 빨강은 "메모리가 진짜 모자랄 때"다. 오른쪽 숫자는 이 노트의 절 번호.

Embedded Linux 엔지니어의 일에서 이 주제가 나오는 장면:

- **"장비가 가끔 재부팅돼요"**: 로그에 `Out of memory: Killed process`가 있고, 죽은 게 하필 센서 데몬이었다. 누구를 먼저 죽일지 설계하지 않은 탓이다 (§9).
- **"제어 루프가 가끔 수 ms 늦어요"**: 메모리 압박 때 할당하던 스레드가 direct reclaim에 걸렸거나, 실행 파일 페이지가 page cache에서 밀려나 major fault가 났다 (§5, §7).
- **"swap 켜면 되지 않나요?"**: eMMC/UFS 수명과 지연 때문에 대부분 끈다. 대신 zram을 쓴다 (§8).
- **드라이버 개발**: `vm_operations_struct.fault`, `get_user_pages`, shrinker, `GFP_KERNEL`이 reclaim에 들어갈 수 있다는 사실 — 전부 이 노트의 내용이 드라이버 API로 나타난 것이다 (§11).

### 0.1 실습 환경

이 노트는 두 곳에서 실행했다.

| 어디 | 커널 | 용도 |
|---|---|---|
| Lima VM `elinux` (호스트) | `7.0.0-38-generic` (Ubuntu) | 빌드, 소스 grep, 설정 비교 |
| QEMU `virt` (cortex-a57, TCG) | `6.18.55` 랩 커널 `out/Image` | OOM·zram·dirty 실험 (`-m 256M`) |
| QEMU `virt` (cortex-a57, TCG) | `6.18.55-dbg` 디버그 커널 `out/Image-dbg` | 커널 모듈 `f11_walk`, kprobe 추적 |

QEMU는 KVM 없이 TCG(소프트웨어 에뮬레이션)로 돈다. 그래서 **시간 측정값은 실제 보드보다 수십 배 느리고 흔들린다**. 이 노트에서 시간은 "자릿수 비교"로만 쓰고, 개수(fault 수, 페이지 수, MiB)를 주로 본다.

### 예제 1: 랩 커널과 Ubuntu 커널의 VM 관련 설정 비교

설정이 다르면 보이는 것이 달라진다. 먼저 확인한다.

```sh
grep -E "^(CONFIG_PER_VMA_LOCK|CONFIG_LRU_GEN|# CONFIG_LRU_GEN|CONFIG_ZRAM|# CONFIG_ZRAM|CONFIG_ARM64_VA_BITS|CONFIG_PGTABLE_LEVELS|CONFIG_ARM64_LPA2|CONFIG_MEMCG|# CONFIG_MEMCG_V1|CONFIG_SWAP)( |=)" /var/lab/out/linux-build/.config
echo ---host; uname -r; cat /sys/kernel/mm/lru_gen/enabled
grep -E "CONFIG_LRU_GEN=|CONFIG_ZRAM=|CONFIG_PGTABLE_LEVELS" /boot/config-$(uname -r)
```

```text
CONFIG_MEMCG=y
# CONFIG_MEMCG_V1 is not set
CONFIG_PGTABLE_LEVELS=5
CONFIG_ARM64_VA_BITS=52
CONFIG_ARM64_LPA2=y
CONFIG_SWAP=y
# CONFIG_LRU_GEN is not set
CONFIG_PER_VMA_LOCK=y
# CONFIG_ZRAM is not set
---host
7.0.0-38-generic
0x0001
CONFIG_PGTABLE_LEVELS=4
CONFIG_LRU_GEN=y
CONFIG_ZRAM=m
```

출력에서 볼 것: 랩 커널(6.18 arm64 defconfig)은 **52비트 VA + LPA2, 5단계 page table**로 빌드돼 있다. 하지만 QEMU의 cortex-a57은 LPA2가 없으니 부팅 때 4단계로 **접힌다**(§2에서 직접 확인). Ubuntu는 처음부터 4단계다. MGLRU(`LRU_GEN`)와 zram은 Ubuntu에만 켜져 있고 랩 커널에는 없다 — 그래서 §8에서는 zram을 따로 모듈로 빌드했다. `MEMCG_V1`이 꺼져 있으니 cgroup v2만 쓴다(§10).

### 0.2 OSTEP과 연결

Don이 같이 읽는 OSTEP(Operating Systems: Three Easy Pieces)의 장과 이 노트의 절을 맞춰 둔다. OSTEP은 원리를, 이 노트는 "Linux arm64에서 실제로는"을 담당한다.

| OSTEP 장 | 이 노트 | 연결 포인트 |
|---|---|---|
| Paging: Introduction | §1, §2 | PTE 비트(valid, protection, present, dirty, reference)가 arm64의 V, AP, AF, DBM/SW dirty로 대응 |
| Paging: Faster Translations (TLBs) | §3 | ASID, context switch 때 flush를 피하는 법, TLB miss를 하드웨어가 처리(arm64는 hardware-managed walk) |
| Paging: Smaller Tables | §1, §2, §4 | multi-level page table — 쓰지 않는 영역은 하위 표를 아예 안 만든다 |
| Beyond Physical Memory: Mechanisms | §5, §8 | page fault, present bit, swap space |
| Beyond Physical Memory: Policies | §7 | LRU 근사, clock 알고리즘 → Linux의 active/inactive 리스트와 MGLRU |
| Complete Virtual Memory Systems | §4, §5 | Linux VM 절(커널 주소 공간, demand zeroing, COW)이 F11의 요약판 |

---

## 1. arm64 translation regime — TTBR0/TTBR1, 4K granule, 4단계

### 1.1 직관: 주소 공간을 위아래 두 덩어리로 나눈다

arm64 EL1&0 변환 체계(translation regime)는 VA 공간을 둘로 나눈다. 위쪽 비트(63..48)가 전부 0이면 **TTBR0_EL1**이 가리키는 표로, 전부 1이면 **TTBR1_EL1**이 가리키는 표로 번역한다. 그 사이 값은 무조건 fault다.

- **TTBR0_EL1**: 유저 공간(`0x0000_xxxx_xxxx_xxxx`). 프로세스마다 다르다. context switch 때 커널이 바꿔 낀다.
- **TTBR1_EL1**: 커널 공간(`0xffff_xxxx_xxxx_xxxx`). 모든 프로세스가 공유한다. 부팅 후 거의 안 바뀐다.

bare-metal Cortex-A를 다뤄 봤다면 "TTBR 하나에 flat 매핑 하나"였을 것이다. Linux는 TTBR0만 갈아 끼우면 주소 공간 전체가 바뀌도록 이렇게 나눴다. 커널 매핑을 매번 복사할 필요가 없다.

### 1.2 VA 비트 → 표 인덱스, 그리고 Linux 이름

4 KiB granule, 48비트 VA에서 VA는 9비트씩 네 조각 + 12비트 offset으로 잘린다. 각 표는 512 엔트리 x 8바이트 = 4 KiB, 즉 정확히 한 페이지다. A5의 그림 2가 이 구조다. 여기서는 **Linux가 각 단계를 부르는 이름**과 descriptor의 비트를 붙인다.

| 하드웨어 단계 | VA 비트 | 한 엔트리가 덮는 범위 | Linux 이름 (5단계 config) | Linux 이름 (4단계 config) |
|---|---|---|---|---|
| (LPA2일 때만) L-1 | 51..48 | 256 TiB | `pgd` | 없음 |
| L0 | 47..39 | 512 GiB | `p4d` (LPA2 없으면 `pgd`와 같은 표) | `pgd` (`p4d`는 컴파일 시 접힘) |
| L1 | 38..30 | 1 GiB | `pud` | `pud` |
| L2 | 29..21 | 2 MiB | `pmd` | `pmd` |
| L3 | 20..12 | 4 KiB | `pte` | `pte` |

Linux 코어 코드는 항상 5단계 이름(`pgd → p4d → pud → pmd → pte`)으로 쓰고, 아키텍처가 단계를 덜 쓰면 그 단계를 **folding**(접기)한다. 접힌 단계는 "아래 단계 표 포인터를 그대로 돌려주는" 가짜 단계다. 랩 커널은 `CONFIG_PGTABLE_LEVELS=5`인데 CPU에 LPA2가 없으면 **부팅 시점에** p4d 단계를 접는다(런타임 folding). §2의 출력에서 이걸 눈으로 본다.

```svg
<svg viewBox="0 0 680 430" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340" y="18" font-size="13" text-anchor="middle">VA = 0xffff9f955000 (§2 실습의 유저 주소, 48-bit, 4K granule)</text>
<rect x="20" y="28" width="110" height="44" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="75" y="46" font-size="12" text-anchor="middle">63..48</text><text x="75" y="63" font-size="12" text-anchor="middle">TTBR 선택</text>
<rect x="130" y="28" width="105" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="182" y="46" font-size="12" text-anchor="middle">47..39 = 511</text><text x="182" y="63" font-size="12" text-anchor="middle">L0 · p4d</text>
<rect x="235" y="28" width="105" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="287" y="46" font-size="12" text-anchor="middle">38..30 = 510</text><text x="287" y="63" font-size="12" text-anchor="middle">L1 · pud</text>
<rect x="340" y="28" width="105" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="392" y="46" font-size="12" text-anchor="middle">29..21 = 252</text><text x="392" y="63" font-size="12" text-anchor="middle">L2 · pmd</text>
<rect x="445" y="28" width="105" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="497" y="46" font-size="12" text-anchor="middle">20..12 = 341</text><text x="497" y="63" font-size="12" text-anchor="middle">L3 · pte</text>
<rect x="550" y="28" width="110" height="44" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="605" y="46" font-size="12" text-anchor="middle">11..0</text><text x="605" y="63" font-size="12" text-anchor="middle">offset</text>
<rect x="20" y="110" width="110" height="56" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="75" y="134" font-size="12" text-anchor="middle">TTBR0_EL1</text><text x="75" y="152" font-size="12" text-anchor="middle">= pa(mm-&gt;pgd)</text>
<rect x="145" y="110" width="90" height="56" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="190" y="134" font-size="12" text-anchor="middle">L0 표</text><text x="190" y="152" font-size="12" text-anchor="middle">[511]</text>
<rect x="250" y="110" width="90" height="56" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="295" y="134" font-size="12" text-anchor="middle">L1 표</text><text x="295" y="152" font-size="12" text-anchor="middle">[510]</text>
<rect x="355" y="110" width="90" height="56" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="400" y="134" font-size="12" text-anchor="middle">L2 표</text><text x="400" y="152" font-size="12" text-anchor="middle">[252]</text>
<rect x="460" y="110" width="90" height="56" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="505" y="134" font-size="12" text-anchor="middle">L3 표</text><text x="505" y="152" font-size="12" text-anchor="middle">[341]</text>
<rect x="565" y="110" width="95" height="56" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="612" y="134" font-size="12" text-anchor="middle">4K 프레임</text><text x="612" y="152" font-size="12" text-anchor="middle">PFN 0x42d21</text>
<line x1="130" y1="138" x2="143" y2="138" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="235" y1="138" x2="248" y2="138" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="340" y1="138" x2="353" y2="138" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="445" y1="138" x2="458" y2="138" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="550" y1="138" x2="563" y2="138" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="182" y1="72" x2="188" y2="108" stroke="#888" stroke-dasharray="3 3" marker-end="url(#ah2)"/>
<line x1="287" y1="72" x2="293" y2="108" stroke="#888" stroke-dasharray="3 3" marker-end="url(#ah2)"/>
<line x1="392" y1="72" x2="398" y2="108" stroke="#888" stroke-dasharray="3 3" marker-end="url(#ah2)"/>
<line x1="497" y1="72" x2="503" y2="108" stroke="#888" stroke-dasharray="3 3" marker-end="url(#ah2)"/>
<text x="340" y="192" font-size="12" text-anchor="middle">table descriptor 0800000045f7e403 = 다음 표 PA 0x45f7e000 + table + PXNTable</text>
<text x="340" y="222" font-size="13" text-anchor="middle">L3 page descriptor (PTE) 64비트 — Linux가 쓰는 필드</text>
<rect x="17" y="235" width="70" height="44" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="52" y="253" font-size="12" text-anchor="middle">58..55</text><text x="52" y="270" font-size="12" text-anchor="middle">SW</text>
<rect x="87" y="235" width="44" height="44" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="109" y="253" font-size="12" text-anchor="middle">54</text><text x="109" y="270" font-size="12" text-anchor="middle">UXN</text>
<rect x="131" y="235" width="44" height="44" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="153" y="253" font-size="12" text-anchor="middle">53</text><text x="153" y="270" font-size="12" text-anchor="middle">PXN</text>
<rect x="175" y="235" width="48" height="44" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="199" y="253" font-size="12" text-anchor="middle">52</text><text x="199" y="270" font-size="12" text-anchor="middle">CONT</text>
<rect x="223" y="235" width="44" height="44" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="245" y="253" font-size="12" text-anchor="middle">51</text><text x="245" y="270" font-size="12" text-anchor="middle">DBM</text>
<rect x="267" y="235" width="140" height="44" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="337" y="253" font-size="12" text-anchor="middle">47..12</text><text x="337" y="270" font-size="12" text-anchor="middle">PFN (출력 주소)</text>
<rect x="407" y="235" width="40" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="427" y="253" font-size="12" text-anchor="middle">11</text><text x="427" y="270" font-size="12" text-anchor="middle">nG</text>
<rect x="447" y="235" width="40" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="467" y="253" font-size="12" text-anchor="middle">10</text><text x="467" y="270" font-size="12" text-anchor="middle">AF</text>
<rect x="487" y="235" width="44" height="44" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="509" y="253" font-size="12" text-anchor="middle">9..8</text><text x="509" y="270" font-size="12" text-anchor="middle">SH</text>
<rect x="531" y="235" width="40" height="44" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="551" y="253" font-size="12" text-anchor="middle">7</text><text x="551" y="270" font-size="12" text-anchor="middle">AP2</text>
<rect x="571" y="235" width="40" height="44" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="591" y="253" font-size="12" text-anchor="middle">6</text><text x="591" y="270" font-size="12" text-anchor="middle">AP1</text>
<rect x="611" y="235" width="52" height="44" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="637" y="253" font-size="12" text-anchor="middle">4..0</text><text x="637" y="270" font-size="12" text-anchor="middle">Attr,V</text>
<text x="340" y="310" font-size="12" text-anchor="middle">SW 55 = PTE_DIRTY, 56 = PTE_SPECIAL (하드웨어는 무시, 커널 전용)   DBM 51 = PTE_WRITE</text>
<text x="340" y="332" font-size="12" text-anchor="middle">AP2 = PTE_RDONLY (1이면 쓰기 금지)   AP1 = PTE_USER (EL0 접근 허용)</text>
<rect x="60" y="350" width="560" height="66" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340" y="372" font-size="12" text-anchor="middle">00e8000042d21f43 → UXN PXN DBM SWDIRTY, RDONLY=0: 쓰기 가능하고 이미 쓴 페이지</text>
<text x="340" y="392" font-size="12" text-anchor="middle">00e0000042d21bc3 → DBM=0, RDONLY=1, AF=0: fork 직후 자식 쪽 COW 대기 페이지</text>
<text x="340" y="410" font-size="12" text-anchor="middle">01600000434b6fc3 → SPECIAL, RDONLY=1: 읽기만 한 익명 페이지 = 공유 zero page</text>
</svg>
```

그림 2 — 위: §2 실습에서 쓴 실제 VA가 표 인덱스 511/510/252/341로 잘리는 모습. 아래: L3 descriptor에서 Linux가 쓰는 비트(50번 GP, 5번 NS 등은 생략)와 실습에서 읽은 실제 PTE 값 세 개의 해독.

### 1.3 descriptor 비트 표

`arch/arm64/include/asm/pgtable-hwdef.h`와 `pgtable-prot.h`에서 확인한 6.18 정의다.

| 비트 | 하드웨어 이름 | Linux 매크로 | 뜻 | OSTEP 용어 |
|---|---|---|---|---|
| 0 | Valid | `PTE_VALID` | 0이면 이 엔트리는 없는 것 → translation fault | valid / present |
| 1 | Type | `PTE_TYPE_PAGE` | L3에선 1(page), 상위 단계에선 1=table, 0=block | — |
| 4..2 | AttrIndx | `PTE_ATTRINDX(t)` | MAIR_EL1의 몇 번 슬롯 속성(Normal, Device…)을 쓸지 | — |
| 6 | AP[1] | `PTE_USER` | 1이면 EL0(유저)도 접근 가능 | user/supervisor |
| 7 | AP[2] | `PTE_RDONLY` | 1이면 쓰기 금지 | protection(R/W) |
| 9..8 | SH | `PTE_SHARED` | 공유 도메인 (Inner Shareable = 3) | — |
| 10 | AF | `PTE_AF` | Access Flag. 0인 페이지를 만지면 Access flag fault (HW AF 기능이 있으면 하드웨어가 직접 세팅) | reference bit |
| 11 | nG | `PTE_NG` | not Global. 1이면 TLB 엔트리가 ASID에 묶인다 (유저 페이지는 전부 1) | — |
| 51 | DBM | `PTE_WRITE` | Dirty Bit Modifier. Linux는 이 비트를 "논리적으로 쓰기 가능"의 뜻으로 쓴다 | — |
| 52 | Contiguous | `PTE_CONT` | 연속된 16개 PTE가 같은 속성 → TLB 엔트리 하나로 합칠 수 있다는 힌트 | — |
| 53 | PXN | `PTE_PXN` | 커널(EL1)이 여기서 실행 금지 | — |
| 54 | UXN | `PTE_UXN` | 유저(EL0)가 여기서 실행 금지 (데이터 페이지는 1) | — |
| 55 | (SW) | `PTE_DIRTY` | 소프트웨어 dirty 비트 | dirty bit |
| 56 | (SW) | `PTE_SPECIAL` | `struct page`로 관리하지 않는 특수 매핑 (zero page, PFN 매핑) | — |

핵심 트릭 하나: arm64 v8.0 하드웨어(cortex-a57 포함)는 dirty 비트를 하드웨어가 세팅해 주지 않는다. 그래서 Linux는 **"쓰기 가능하지만 아직 안 쓴 페이지"를 RDONLY로 매핑**해 두고, 첫 쓰기 때 permission fault를 받아 그때 RDONLY를 지우고 SW dirty를 세운다. v8.1의 HW DBM 기능이 있으면 하드웨어가 "DBM=1이고 RDONLY=1인 페이지에 쓰면 RDONLY를 지우는" 일을 직접 해서 이 fault가 없어진다. OSTEP의 "dirty bit는 하드웨어가 세팅"이라는 가정이 모든 CPU에서 참은 아니라는 좋은 예다.

### 1.4 흔한 함정

- **"4단계라고 외웠는데 config는 5단계"**: 커널 설정의 `PGTABLE_LEVELS`는 최대치이고, 실제 단계 수는 CPU 기능(LPA2)에 따라 부팅 때 정해질 수 있다. 랩 커널이 그렇다.
- **table descriptor와 page descriptor의 bit 1 의미가 다르다**: 상위 단계(L0~L2)에서 bit1..0=3이면 "다음 표", =1이면 "block"(1 GiB/2 MiB huge 매핑)이다. L3에서 3은 "page"다. 그래서 walk 코드는 각 단계에서 `pmd_leaf()` 같은 검사로 huge 매핑을 먼저 걸러야 한다.
- **커널 주소는 TTBR1**: 커널 모듈에서 `mm->pgd`로 커널 주소(`0xffff...`)를 찾으면 안 된다. 커널 매핑은 `init_mm`(swapper_pg_dir)에 있다.

---

## 2. 실습 1 — `f11_walk` 모듈로 page table을 직접 걸어 보기

A5에서는 `/proc/self/pagemap`으로 "이 VA의 PFN이 뭔지"를 유저 쪽에서 봤다. 이번에는 커널 모듈이 **현재 프로세스(`current`)의 page table을 단계별로 걸어서** 각 단계의 descriptor 원본 값을 찍는다. 그리고 같은 순간 pagemap 결과와 맞춰 본다.

구조는 간단하다. 모듈이 debugfs 파일 `/sys/kernel/debug/f11_walk`를 만든다. 유저 프로그램이 그 파일에 자기 VA를 문자열로 쓰면, `.write` 핸들러는 **그 프로그램의 문맥(process context)에서** 실행되므로 `current->mm`이 바로 그 프로그램의 주소 공간이다.

### 2.1 예제 2: 모듈 소스 (`f11_walk.c`)

무엇을 확인하는 코드인가: `pgd_offset → p4d_offset → pud_offset → pmd_offset → pte`를 따라가며 각 단계 값을 찍고, PTE의 비트를 해독한다.

```c
// f11_walk: echo <user VA> > /sys/kernel/debug/f11_walk -> walk current->mm
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/mm.h>
#include <linux/pgtable.h>
#include <linux/uaccess.h>
#define B(v, n) (((v) >> (n)) & 1)
static struct dentry *f11_dent;

static ssize_t f11_write(struct file *f, const char __user *ubuf, size_t len, loff_t *off)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	pgd_t *pgd; p4d_t *p4d; pud_t *pud; pmd_t *pmd; pte_t pte;
	unsigned long va; char buf[32]; u64 v;

	if (!mm || len >= sizeof(buf) || copy_from_user(buf, ubuf, len)) return -EINVAL;
	buf[len] = '\0';
	if (kstrtoul(strim(buf), 0, &va)) return -EINVAL;
	mmap_read_lock(mm);
	pr_info("f11: pid=%d va=%#lx l5=%d l4=%d asid=%llu ttbr0=%#llx pa(mm->pgd)=%#llx\n",
		current->pid, va, pgtable_l5_enabled(), pgtable_l4_enabled(),
		read_sysreg(ttbr1_el1) >> 48, read_sysreg(ttbr0_el1), virt_to_phys(mm->pgd));
	vma = vma_lookup(mm, va);
	if (vma) pr_info("f11: vma %lx-%lx flags=%#lx map_count=%d\n",
			 vma->vm_start, vma->vm_end, vma->vm_flags, mm->map_count);
	pgd = pgd_offset(mm, va);
	p4d = p4d_offset(pgd, va);
	pr_info("f11: pgd[%lu]=%016llx p4d[%lu]=%016llx\n", pgd_index(va),
		pgd_val(*pgd), p4d_index(va), p4d_val(*p4d));
	if (p4d_none(*p4d)) goto out;
	pud = pud_offset(p4d, va);
	if (pud_none(*pud) || pud_leaf(*pud)) goto out;
	pmd = pmd_offset(pud, va);
	pr_info("f11: pud[%lu]=%016llx pmd[%lu]=%016llx\n", pud_index(va),
		pud_val(*pud), pmd_index(va), pmd_val(*pmd));
	if (pmd_none(*pmd) || pmd_leaf(*pmd) || pmd_bad(*pmd)) goto out;
	pte = ptep_get(pte_offset_kernel(pmd, va)); /* pte_offset_map() not exported */
	v = pte_val(pte);
	pr_info("f11: pte[%lu]=%016llx V=%llu AF=%llu nG=%llu RDONLY=%llu USER=%llu DBM=%llu "
		"CONT=%llu PXN=%llu UXN=%llu SWDIRTY=%llu SPECIAL=%llu\n", pte_index(va), v,
		B(v, 0), B(v, 10), B(v, 11), B(v, 7), B(v, 6), B(v, 51), B(v, 52),
		B(v, 53), B(v, 54), B(v, 55), B(v, 56));
	if (pte_present(pte)) pr_info("f11: pfn=%#lx young=%d dirty=%d write=%d\n",
		(unsigned long)pte_pfn(pte), pte_young(pte), pte_dirty(pte), pte_write(pte));
out:
	mmap_read_unlock(mm);
	return len;
}

static const struct file_operations f11_fops = { .write = f11_write };
static int __init f11_init(void)
{
	f11_dent = debugfs_create_file("f11_walk", 0200, NULL, NULL, &f11_fops);
	return 0;
}
static void __exit f11_exit(void) { debugfs_remove(f11_dent); }
module_init(f11_init); module_exit(f11_exit);
MODULE_LICENSE("GPL"); MODULE_DESCRIPTION("F11 page table walker");
```

읽는 포인트:

- `mmap_read_lock(mm)`: walk 도중 VMA나 상위 표가 사라지지 않게 mmap lock을 읽기로 잡는다. 엄밀히 PTE 자체는 PTE lock(split ptlock)이 보호하지만, 값을 한 번 읽어 찍기만 하는 디버그 용도라 생략했다. 실제 커널 코드에서 PTE를 **바꾸려면** `pte_offset_map_lock()`으로 PTE lock을 잡아야 한다.
- `vma_lookup()`: VA가 속한 VMA를 maple tree에서 찾는다 (§4).
- `read_sysreg(ttbr1_el1) >> 48`: Linux arm64는 TCR_EL1.A1=1로 설정해서 **ASID를 TTBR1의 상위 16비트**에 둔다 (§3).

처음에는 교과서대로 `pte_offset_map()`을 썼는데 빌드가 실패했다. 실제 출력:

```text
WARNING: modpost: missing MODULE_DESCRIPTION() in f11_walk.o
ERROR: modpost: "___pte_offset_map" [f11_walk.ko] undefined!
```

6.x에서 `pte_offset_map()`은 `___pte_offset_map()`이라는 비공개 함수를 부르는데 이 함수가 모듈에 export되지 않는다. 커널 개발자들이 "모듈이 page table을 직접 만지지 말라"는 뜻으로 막아 둔 것이다. arm64에는 HIGHPTE(PTE 표를 highmem에 두는 기능)가 없으니 디버그 용도로는 `pte_offset_kernel()`(상위 엔트리의 PA → 선형 매핑 VA + 인덱스)로 충분하다. **드라이버가 진짜로 유저 페이지가 필요하면 page table을 걷지 말고 `pin_user_pages()`나 `vm_insert_page()`/`remap_pfn_range()` 같은 정식 API를 쓴다** (§11).

### 2.2 예제 3: 테스트 프로그램 (`pt_test.c`)

무엇을 확인하는 코드인가: 익명 페이지 하나를 mmap한 뒤 (1) 안 만짐 (2) 읽기 (3) 쓰기 (4) fork 후 자식 (5) 자식 쓰기 (6) 부모 (7) 부모 쓰기 — 각 시점의 커널 walk 결과와 `/proc/self/pagemap`을 같이 찍는다.

```c
// pt_test: VA -> f11_walk (kernel view) + /proc/self/pagemap (user view)
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

static void show(const char *tag, volatile char *p)
{
	char buf[32];
	uint64_t e;
	int fd = open("/sys/kernel/debug/f11_walk", O_WRONLY);
	int pm = open("/proc/self/pagemap", O_RDONLY);
	if (fd < 0 || pm < 0) { perror("open"); exit(1); }
	printf("== [%d] %s\n", getpid(), tag);
	fflush(stdout);
	int n = snprintf(buf, sizeof(buf), "%p", (void *)p);
	if (write(fd, buf, n) != n) { perror("write"); exit(1); }
	if (pread(pm, &e, 8, ((uintptr_t)p / 4096) * 8) != 8) { perror("pread"); exit(1); }
	printf("   pagemap: present=%d excl=%d pfn=%#llx\n", (int)(e >> 63),
	       (int)((e >> 56) & 1), (unsigned long long)(e & ((1ULL << 55) - 1)));
	fflush(stdout);
	if (system("dmesg -c | grep -o 'f11: .*'") < 0) exit(1);
	close(fd); close(pm);
}

int main(void)
{
	volatile char *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED) { perror("mmap"); return 1; }
	show("1 after mmap (untouched)", p);
	char c = p[0];
	show("2 after READ (zero page?)", p);
	p[0] = c + 1;
	show("3 after WRITE", p);
	pid_t pid = fork();
	if (pid < 0) { perror("fork"); return 1; }
	if (pid == 0) {
		show("4c child after fork", p);
		p[0] = 42;
		show("5c child after WRITE (COW copy)", p);
		_exit(0);
	}
	waitpid(pid, NULL, 0);
	show("6p parent after child exit", p);
	p[0] = 7;
	show("7p parent after WRITE (reuse)", p);
	return 0;
}
```

pagemap 엔트리는 8바이트이고 bit 63 = present, bit 56 = "이 프로세스만 매핑(exclusively mapped)", bit 0..54 = PFN이다 (root일 때만 PFN이 보인다).

### 2.3 빌드와 실행

모듈은 디버그 커널 트리로 빌드하고(vermagic `6.18.55-dbg`), 테스트 프로그램은 initramfs에 glibc가 없으니 `-static`으로 빌드한다. 랩 rootfs를 `/var/tmp/F11/`에 복사해 두 파일과 실행 스크립트를 넣고 initramfs로 다시 묶는다.

```sh
# VM 안 (/var/tmp/F11/walk)
make -C /var/lab/out/linux-build-dbg M=$PWD modules
gcc -static -std=gnu11 -Wall -Wextra -O2 pt_test.c -o pt_test
# rootfs 복사 + /f11/f11_walk.ko, /f11/pt_test, /f11_run.sh 추가 → cpio | gzip → rd.gz
timeout 120 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -kernel /var/lab/out/Image-dbg -initrd rd.gz \
  -append "console=ttyAMA0 quiet LAB_CMD=sh~/f11_run.sh"
```

게스트 안의 `/f11_run.sh`는 `insmod /f11/f11_walk.ko; dmesg -c > /dev/null; /f11/pt_test`다. 실제 출력(PID, 주소, PFN은 실행마다 다르다):

```text
lab initramfs: 6.18.55-dbg up 5.54s
6.18.55-dbg
== [68] 1 after mmap (untouched)
   pagemap: present=0 excl=0 pfn=0
f11: pid=68 va=0xffff9f955000 l5=0 l4=1 asid=64 ttbr0=0x45f77000 pa(mm->pgd)=0x45f77000
f11: vma ffff9f955000-ffff9f956000 flags=0x100073 map_count=9
f11: pgd[0]=0800000045f5c403 p4d[511]=0800000045f7e403
f11: pud[510]=0800000045f80403 pmd[252]=0800000045f9a403
f11: pte[341]=0000000000000000 V=0 AF=0 nG=0 RDONLY=0 USER=0 DBM=0 CONT=0 PXN=0 UXN=0 SWDIRTY=0 SPECIAL=0
== [68] 2 after READ (zero page?)
   pagemap: present=1 excl=0 pfn=0x434b6
f11: pid=68 va=0xffff9f955000 l5=0 l4=1 asid=64 ttbr0=0x45f77000 pa(mm->pgd)=0x45f77000
f11: vma ffff9f955000-ffff9f956000 flags=0x100073 map_count=9
f11: pgd[0]=0800000045f5c403 p4d[511]=0800000045f7e403
f11: pud[510]=0800000045f80403 pmd[252]=0800000045f9a403
f11: pte[341]=01600000434b6fc3 V=1 AF=1 nG=1 RDONLY=1 USER=1 DBM=0 CONT=0 PXN=1 UXN=1 SWDIRTY=0 SPECIAL=1
f11: pfn=0x434b6 young=1 dirty=0 write=0
== [68] 3 after WRITE
   pagemap: present=1 excl=1 pfn=0x42d21
f11: pid=68 va=0xffff9f955000 l5=0 l4=1 asid=64 ttbr0=0x45f77000 pa(mm->pgd)=0x45f77000
f11: vma ffff9f955000-ffff9f956000 flags=0x100073 map_count=9
f11: pgd[0]=0800000045f5c403 p4d[511]=0800000045f7e403
f11: pud[510]=0800000045f80403 pmd[252]=0800000045f9a403
f11: pte[341]=00e8000042d21f43 V=1 AF=1 nG=1 RDONLY=0 USER=1 DBM=1 CONT=0 PXN=1 UXN=1 SWDIRTY=1 SPECIAL=0
f11: pfn=0x42d21 young=1 dirty=1 write=1
== [78] 4c child after fork
   pagemap: present=1 excl=0 pfn=0x42d21
f11: pid=78 va=0xffff9f955000 l5=0 l4=1 asid=96 ttbr0=0x45fc6000 pa(mm->pgd)=0x45fc6000
f11: vma ffff9f955000-ffff9f956000 flags=0x100073 map_count=9
f11: pgd[0]=0800000045fbc403 p4d[511]=0800000045fbd403
f11: pud[510]=0800000045fcc403 pmd[252]=0800000045fcb403
f11: pte[341]=00e0000042d21bc3 V=1 AF=0 nG=1 RDONLY=1 USER=1 DBM=0 CONT=0 PXN=1 UXN=1 SWDIRTY=1 SPECIAL=0
f11: pfn=0x42d21 young=0 dirty=1 write=0
== [78] 5c child after WRITE (COW copy)
   pagemap: present=1 excl=1 pfn=0x42d06
f11: pid=78 va=0xffff9f955000 l5=0 l4=1 asid=96 ttbr0=0x45fc6000 pa(mm->pgd)=0x45fc6000
f11: vma ffff9f955000-ffff9f956000 flags=0x100073 map_count=9
f11: pgd[0]=0800000045fbc403 p4d[511]=0800000045fbd403
f11: pud[510]=0800000045fcc403 pmd[252]=0800000045fcb403
f11: pte[341]=00e8000042d06f43 V=1 AF=1 nG=1 RDONLY=0 USER=1 DBM=1 CONT=0 PXN=1 UXN=1 SWDIRTY=1 SPECIAL=0
f11: pfn=0x42d06 young=1 dirty=1 write=1
== [68] 6p parent after child exit
   pagemap: present=1 excl=1 pfn=0x42d21
f11: pid=68 va=0xffff9f955000 l5=0 l4=1 asid=64 ttbr0=0x45f77000 pa(mm->pgd)=0x45f77000
f11: vma ffff9f955000-ffff9f956000 flags=0x100073 map_count=9
f11: pgd[0]=0800000045f5c403 p4d[511]=0800000045f7e403
f11: pud[510]=0800000045f80403 pmd[252]=0800000045f9a403
f11: pte[341]=00e0000042d21fc3 V=1 AF=1 nG=1 RDONLY=1 USER=1 DBM=0 CONT=0 PXN=1 UXN=1 SWDIRTY=1 SPECIAL=0
f11: pfn=0x42d21 young=1 dirty=1 write=0
== [68] 7p parent after WRITE (reuse)
   pagemap: present=1 excl=1 pfn=0x42d21
f11: pid=68 va=0xffff9f955000 l5=0 l4=1 asid=64 ttbr0=0x45f77000 pa(mm->pgd)=0x45f77000
f11: vma ffff9f955000-ffff9f956000 flags=0x100073 map_count=9
f11: pgd[0]=0800000045f5c403 p4d[511]=0800000045f7e403
f11: pud[510]=0800000045f80403 pmd[252]=0800000045f9a403
f11: pte[341]=00e8000042d21f43 V=1 AF=1 nG=1 RDONLY=0 USER=1 DBM=1 CONT=0 PXN=1 UXN=1 SWDIRTY=1 SPECIAL=0
f11: pfn=0x42d21 young=1 dirty=1 write=1
[    7.160024] reboot: Power down
```

### 2.4 출력 해독 — 단계별로

**공통 줄부터.**

- `l5=0 l4=1`: 5단계 config지만 `pgtable_l5_enabled()`가 거짓이다. cortex-a57에 LPA2가 없어서 부팅 때 4단계로 접혔다.
- `pgd[0]` vs `p4d[511]`: 접힌 상태에서 `pgd_index(va)`는 VA의 51..48비트(유저 주소라 0)로 계산되니 의미 없는 0번 엔트리를 가리킨다. 진짜 하드웨어 L0 엔트리는 `p4d_offset()`이 같은 표 페이지에서 47..39비트 인덱스(511)로 다시 고른 쪽이다. arm64 `pgd_to_folded_p4d()`가 바로 "같은 페이지 + `p4d_index(addr)`"를 돌려준다. 0번 엔트리 값이 따로 있는 이유는 L0[0]이 실행 파일이 놓인 낮은 주소(`0x400000` 근처)를 덮는 진짜 엔트리이기 때문이다.
- 인덱스 511/510/252/341: `0xffff9f955000`을 9비트씩 자른 값과 정확히 같다 (그림 2).
- `ttbr0=0x45f77000 pa(mm->pgd)=0x45f77000`: TTBR0_EL1이 정말로 이 프로세스의 `mm->pgd` 물리 주소다. 자식(pid 78)은 `0x45fc6000` — fork는 page table을 **통째로 새로 만든다**(상위 표 주소가 전부 다르다).
- `asid=64` / `asid=96`: 프로세스마다 ASID가 다르다 (§3).
- `flags=0x100073`: `VM_READ|VM_WRITE|VM_MAYREAD|VM_MAYWRITE|VM_MAYEXEC`(0x73) + `VM_ACCOUNT`(0x100000, overcommit 계산에 포함된 private 쓰기 매핑).
- table descriptor `...403`: bit1..0=3(table) + bit10(`PMD_TABLE_AF`, FEAT_HAFT 없으면 무시) + 맨 위 `0x08`은 bit59 = **PXNTable**. "이 표 아래 전부 커널 실행 금지"를 상위 단계에서 한 번에 거는 것이다.

**단계별 PTE 변화.**

| 시점 | PFN | RDONLY | DBM(WRITE) | AF | SPECIAL | 무슨 일이 있었나 |
|---|---|---|---|---|---|---|
| 1 mmap 직후 | 없음 | — | — | — | — | VMA만 있고 PTE는 0. pagemap도 present=0 |
| 2 읽기 | 0x434b6 | 1 | 0 | 1 | 1 | `do_anonymous_page`의 읽기 경로: 전역 **zero page**를 읽기 전용으로 매핑. 메모리 소비 0 |
| 3 쓰기 | 0x42d21 | 0 | 1 | 1 | 0 | zero page는 쓸 수 없으니 write fault → 새 페이지 할당 후 교체. pagemap excl=1 |
| 4 fork 직후 자식 | 0x42d21 (같음) | 1 | 0 | 0 | 0 | fork가 부모·자식 PTE를 둘 다 RDONLY로 바꿈(COW 준비). 자식 PTE는 AF=0(old)로 복사 |
| 5 자식 쓰기 | 0x42d06 (새것) | 0 | 1 | 1 | 0 | write fault → `do_wp_page` → 공유 중이니 `wp_page_copy`로 **복사** |
| 6 부모 (자식 종료 후) | 0x42d21 | 1 | 0 | 1 | 0 | 부모 PTE는 여전히 RDONLY. 하지만 이제 이 페이지를 쓰는 건 부모뿐 |
| 7 부모 쓰기 | 0x42d21 (그대로) | 0 | 1 | 1 | 0 | write fault → `do_wp_page`가 "혼자 쓰는 중"을 확인 → 복사 없이 `wp_page_reuse` |

볼 것 세 가지:

1. **읽기 fault는 메모리를 쓰지 않는다**: 2단계의 PFN `0x434b6`은 시스템 전체가 공유하는 zero page다. `SPECIAL=1`은 "이 PFN에 대해 `struct page` 참조 카운트를 만지지 말라"는 표시다.
2. **COW는 PFN 변화로 보인다**: 자식이 쓰는 순간 자식 PFN만 `0x42d21 → 0x42d06`으로 바뀌고 부모는 그대로다. 커널 walk와 pagemap이 같은 값을 보여 준다.
3. **COW가 항상 복사는 아니다**: 7단계에서 부모는 RDONLY에 막혀 fault가 났지만 PFN은 그대로다. `do_wp_page`가 folio 참조 카운트를 보고 "이 익명 페이지를 쓰는 건 나 하나뿐"이라고 판단하면(`wp_can_reuse_anon_folio`) 복사 대신 쓰기 권한만 돌려준다. fork 후 자식이 곧장 `exec`하는 흔한 패턴에서 부모가 쓸데없이 복사하지 않는 이유다.

### 2.5 흔한 함정

- **모듈에서 page table을 바꾸지 말 것**: 읽기도 위처럼 export 제약이 있고, 바꾸려면 PTE lock, TLB flush, rmap, 참조 카운트를 전부 맞춰야 한다. 드라이버가 할 일은 정식 API(`remap_pfn_range`, `vm_insert_page`, `vmf_insert_pfn`)다.
- **`dmesg -c`는 게스트 링버퍼를 비운다**: 실습용 게스트라서 썼다. 공유 장비에서 쓰면 다른 사람의 로그를 지운다.
- **pagemap PFN은 root만**: 일반 유저는 PFN 필드가 0으로 보인다 (Rowhammer 대책).

---

## 3. TLB와 ASID — 그리고 TLB를 비우는 법

### 3.1 직관: 번역 결과 캐시, 그런데 "누구의" 번역인지 태그가 붙는다

TLB(Translation Lookaside Buffer)는 "VA 페이지 → PFN + 속성"의 캐시다. 4단계 walk는 메모리를 최대 4번 읽어야 하니(캐시에 걸려도 수십 사이클) TLB 없이는 모든 load/store가 몇 배 느려진다.

문제는 프로세스마다 같은 VA가 다른 PFN을 뜻한다는 것이다. §2에서 부모와 자식은 둘 다 `0xffff9f955000`을 썼지만 PFN이 달랐다. TLB가 VA만으로 찾으면 context switch 때마다 TLB를 몽땅 비워야 한다. 그래서 arm64는 TLB 엔트리에 **ASID(Address Space ID)** 태그를 붙인다.

- PTE의 **nG=1**(not Global)인 엔트리는 TLB에 "ASID X의 것"으로 저장된다. 유저 페이지는 전부 nG=1이다 (§2 출력에서 모든 PTE가 `nG=1`).
- nG=0인 엔트리(커널 매핑)는 global이라 모든 ASID에서 맞는다.
- 현재 ASID는 TTBR(Linux arm64에서는 **TTBR1_EL1**의 63..48비트)에 있다. context switch는 TTBR0와 ASID만 바꾸고 TLB는 그대로 둔다. 이전 프로세스 엔트리는 ASID가 달라서 안 맞을 뿐 남아 있다가, 그 프로세스로 돌아오면 다시 맞는다.

Don의 SSD 경험으로 말하면, 캐시 라인에 namespace ID 태그를 붙여서 namespace 전환 때 캐시를 flush하지 않는 것과 같다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<text x="190" y="20" font-size="13" text-anchor="middle">TLB (개념도)</text>
<rect x="20" y="30" width="70" height="30" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="55" y="50" font-size="12" text-anchor="middle">ASID</text>
<rect x="90" y="30" width="130" height="30" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="155" y="50" font-size="12" text-anchor="middle">VA 페이지</text>
<rect x="220" y="30" width="90" height="30" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="265" y="50" font-size="12" text-anchor="middle">PFN</text>
<rect x="310" y="30" width="50" height="30" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="335" y="50" font-size="12" text-anchor="middle">nG</text>
<rect x="20" y="60" width="70" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="55" y="80" font-size="12" text-anchor="middle">64</text>
<rect x="90" y="60" width="130" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="155" y="80" font-size="12" text-anchor="middle">ffff9f955</text>
<rect x="220" y="60" width="90" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="265" y="80" font-size="12" text-anchor="middle">42d21</text>
<rect x="310" y="60" width="50" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="335" y="80" font-size="12" text-anchor="middle">1</text>
<rect x="20" y="90" width="70" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="55" y="110" font-size="12" text-anchor="middle">96</text>
<rect x="90" y="90" width="130" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="155" y="110" font-size="12" text-anchor="middle">ffff9f955</text>
<rect x="220" y="90" width="90" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="265" y="110" font-size="12" text-anchor="middle">42d06</text>
<rect x="310" y="90" width="50" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="335" y="110" font-size="12" text-anchor="middle">1</text>
<rect x="20" y="120" width="70" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="55" y="140" font-size="12" text-anchor="middle">(무시)</text>
<rect x="90" y="120" width="130" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="155" y="140" font-size="12" text-anchor="middle">ffff8000802e1</text>
<rect x="220" y="120" width="90" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="265" y="140" font-size="12" text-anchor="middle">커널 코드</text>
<rect x="310" y="120" width="50" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="335" y="140" font-size="12" text-anchor="middle">0</text>
<text x="190" y="172" font-size="12" text-anchor="middle">같은 VA라도 ASID가 다르면 다른 엔트리 · nG=0은 모두에게 맞음</text>
<rect x="390" y="30" width="270" height="44" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="525" y="57" font-size="12" text-anchor="middle">TTBR1_EL1[63:48] = 현재 ASID</text>
<rect x="390" y="88" width="270" height="62" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="525" y="112" font-size="12" text-anchor="middle">context switch: TTBR0 + ASID만 교체</text><text x="525" y="134" font-size="12" text-anchor="middle">TLB 전체 flush 불필요</text>
<rect x="20" y="200" width="200" height="56" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="120" y="222" font-size="13" text-anchor="middle">tlbi vale1is</text><text x="120" y="243" font-size="12" text-anchor="middle">VA + ASID 한 페이지</text>
<rect x="240" y="200" width="200" height="56" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="340" y="222" font-size="13" text-anchor="middle">tlbi aside1is</text><text x="340" y="243" font-size="12" text-anchor="middle">ASID 하나 전체</text>
<rect x="460" y="200" width="200" height="56" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="560" y="222" font-size="13" text-anchor="middle">tlbi vmalle1is</text><text x="560" y="243" font-size="12" text-anchor="middle">EL1&amp;0 엔트리 전부</text>
<text x="340" y="285" font-size="12" text-anchor="middle">flush_tlb_page / flush_tlb_range → vale1is · flush_tlb_mm → aside1is · flush_tlb_all → vmalle1is</text>
<text x="340" y="310" font-size="12" text-anchor="middle">접미사 is = Inner Shareable: 같은 도메인의 모든 코어 TLB에 방송된다</text>
</svg>
```

그림 3 — TLB 엔트리는 ASID 태그로 구분된다(값은 §2 실습의 부모 64·자식 96). 아래는 Linux가 쓰는 세 가지 TLB 무효화 명령과 그걸 감싼 커널 함수.

### 3.2 예제 4: ASID가 실제로 어디에 들어가나 (소스 확인)

`arch/arm64/mm/context.c`의 `cpu_do_switch_mm()`이 context switch 때 TTBR을 바꾸는 함수다. 랩 소스에서 그대로 뽑았다.

```sh
sed -n 349,372p /var/lab/src/linux-6.18.55/arch/arm64/mm/context.c
```

```text
void cpu_do_switch_mm(phys_addr_t pgd_phys, struct mm_struct *mm)
{
	unsigned long ttbr1 = read_sysreg(ttbr1_el1);
	unsigned long asid = ASID(mm);
	unsigned long ttbr0 = phys_to_ttbr(pgd_phys);

	/* Skip CNP for the reserved ASID */
	if (system_supports_cnp() && asid)
		ttbr0 |= TTBR_CNP_BIT;

	/* SW PAN needs a copy of the ASID in TTBR0 for entry */
	if (IS_ENABLED(CONFIG_ARM64_SW_TTBR0_PAN))
		ttbr0 |= FIELD_PREP(TTBR_ASID_MASK, asid);

	/* Set ASID in TTBR1 since TCR.A1 is set */
	ttbr1 &= ~TTBR_ASID_MASK;
	ttbr1 |= FIELD_PREP(TTBR_ASID_MASK, asid);

	cpu_set_reserved_ttbr0_nosync();
	write_sysreg(ttbr1, ttbr1_el1);
	write_sysreg(ttbr0, ttbr0_el1);
	isb();
	post_ttbr_update_workaround();
}
```

출력에서 볼 것: `ttbr0`에는 pgd의 PA만, ASID는 `ttbr1`에 넣는다(주석 "TCR.A1 is set"). §2에서 `read_sysreg(ttbr1_el1) >> 48`로 ASID를 읽은 이유다. TTBR0를 바꾸기 전에 `cpu_set_reserved_ttbr0_nosync()`로 잠시 "빈 표"를 끼우는 것도 볼 만하다 — TTBR1(ASID)과 TTBR0를 따로 쓰는 사이에 CPU가 "새 ASID + 옛 표" 조합으로 투기적 walk를 해서 TLB를 오염시키지 않게 막는 것이다.

ASID는 16비트(CPU에 따라 8비트)라 프로세스 수보다 적을 수 있다. 다 쓰면 커널은 **generation**을 올리고 전 TLB를 flush한 뒤 ASID를 처음부터 다시 나눠 준다(`arch/arm64/mm/context.c`의 `new_context()`/`flush_context()`). ASID(mm)은 `mm->context.id`의 하위 16비트다(`#define ASID(mm) (atomic64_read(&(mm)->context.id) & 0xffff)`, `asm/mmu.h`).

### 3.3 예제 5: TLB를 비우는 커널 함수와 실제 기계어

page table을 바꿨는데 TLB에 옛 번역이 남아 있으면, CPU는 옛 PFN으로 계속 접근한다. 그래서 **PTE를 지우거나 권한을 줄이면 반드시 TLB를 무효화**해야 한다. `arch/arm64/include/asm/tlbflush.h`에서 핵심 함수:

```sh
sed -n 372,408p /var/lab/src/linux-6.18.55/arch/arm64/include/asm/tlbflush.h
```

```text
static inline void flush_tlb_mm(struct mm_struct *mm)
{
	unsigned long asid;

	dsb(ishst);
	asid = __TLBI_VADDR(0, ASID(mm));
	__tlbi(aside1is, asid);
	__tlbi_user(aside1is, asid);
	__tlbi_sync_s1ish(mm);
	mmu_notifier_arch_invalidate_secondary_tlbs(mm, 0, -1UL);
}

static inline void __flush_tlb_page_nosync(struct mm_struct *mm,
					   unsigned long uaddr)
{
	unsigned long addr;

	dsb(ishst);
	addr = __TLBI_VADDR(uaddr, ASID(mm));
	__tlbi(vale1is, addr);
	__tlbi_user(vale1is, addr);
	mmu_notifier_arch_invalidate_secondary_tlbs(mm, uaddr & PAGE_MASK,
						(uaddr & PAGE_MASK) + PAGE_SIZE);
}

static inline void flush_tlb_page_nosync(struct vm_area_struct *vma,
					 unsigned long uaddr)
{
	return __flush_tlb_page_nosync(vma->vm_mm, uaddr);
}

static inline void flush_tlb_page(struct vm_area_struct *vma,
				  unsigned long uaddr)
{
	flush_tlb_page_nosync(vma, uaddr);
	__tlbi_sync_s1ish(vma->vm_mm);
}
```

순서가 Don에게 익숙한 캐시 유지보수와 똑같다.

1. `dsb(ishst)`: 앞서 메모리에 쓴 PTE 변경이 다른 코어의 walker에게 보이도록 store를 끝낸다.
2. `tlbi vale1is, <ASID<<48 | VA>>12>`: 모든 코어에 "이 ASID의 이 페이지 번역을 버려라"를 방송한다.
3. `dsb(ish)`(`__tlbi_sync_s1ish` 안): 방송이 모든 코어에서 완료될 때까지 기다린다.
4. 필요하면 `isb`로 이 코어의 파이프라인을 비운다.

`__tlbi_user`는 KPTI(커널 페이지 테이블 격리)가 켜졌을 때 유저용 ASID(ASID | 1)에 대해 한 번 더 무효화하는 것이다. COW에서 PTE를 갈아 끼울 때 부르는 `ptep_clear_flush()`(mm/pgtable-generic.c)를 디스어셈블하면 이 순서가 그대로 보인다.

```sh
objdump -d --no-show-raw-insn --disassemble=ptep_clear_flush /var/lab/build/linux/vmlinux
```

```text
ffff8000802e10c0 <ptep_clear_flush>:
...
ffff8000802e10f8:	ldxr	x3, [x4]
ffff8000802e10fc:	stxr	w1, x2, [x4]
ffff8000802e1100:	cbnz	w1, ffff8000802e10f8 <ptep_clear_flush+0x38>
...
ffff8000802e1114:	dsb	ishst
ffff8000802e1118:	ldr	x2, [x0, #1096]
ffff8000802e111c:	ubfx	x4, x7, #12, #44
ffff8000802e1120:	orr	x2, x4, x2, lsl #48
ffff8000802e1124:	tlbi	vale1is, x2
...
ffff8000802e1138:	dsb	ish
...
ffff8000802e1188:	orr	x2, x2, #0x1000000000000
ffff8000802e118c:	tlbi	vale1is, x2
```

(전체 출력 중 핵심 줄만 남기고 `...`로 생략했다.) 출력에서 볼 것: `ldxr/stxr` 루프가 PTE를 원자적으로 0으로 바꾸고(`ptep_get_and_clear`), `ldr x2, [x0, #1096]`이 `mm->context.id`(ASID)를 읽어 `ubfx`로 뽑은 VA>>12와 `ASID<<48`로 합쳐 `tlbi vale1is`의 피연산자를 만든다. 아래쪽 `orr ... #0x1000000000000`(bit 48 = `USER_ASID_FLAG`) 뒤의 두 번째 `tlbi`가 KPTI용 유저 ASID 무효화다. 이 경로는 부팅 때 alternatives로 패치되어 KPTI가 꺼진 CPU에서는 `nop`로 건너뛴다.

### 3.4 Linux가 TLB를 비우는 때

| 사건 | 함수 (대표) | 범위 |
|---|---|---|
| COW로 PTE 교체, 페이지 migration | `ptep_clear_flush()` → `flush_tlb_page()` | 한 페이지 |
| `munmap`, `mprotect`, `madvise(DONTNEED)` | `tlb_gather_mmu` / `tlb_finish_mmu` (mmu_gather가 모아서) → `__flush_tlb_range` | 범위 (너무 크면 ASID 전체로 승격) |
| reclaim이 여러 페이지를 unmap | `arch_tlbbatch_add_pending` → `arch_tlbbatch_flush` | 페이지마다 TLBI를 보내고 마지막에 DSB 한 번 |
| ASID generation 소진 | `flush_context()` | 전체 |
| 커널 매핑 변경 (`vunmap` 등) | `flush_tlb_kernel_range()` | 커널 범위 |

`__flush_tlb_range_nosync()`는 페이지 수가 일정 한도를 넘으면 페이지별 `tlbi` 대신 `flush_tlb_mm()`(ASID 전체)으로 바꾼다 — 소스에 `if (__flush_tlb_range_limit_excess(...)) { flush_tlb_mm(mm); ...}`가 있다. 수천 페이지를 하나씩 무효화하는 것보다 싸기 때문이다.

### 3.5 흔한 함정

- **"PTE만 바꾸면 끝"**: TLB flush를 빼먹으면 다른 코어가 옛 번역으로 계속 접근한다. 증상은 간헐적 데이터 손상이라 찾기 매우 어렵다. 드라이버는 직접 하지 말고 `zap_vma_ptes()`, `unmap_mapping_range()` 같은 API를 쓴다(내부에서 flush한다).
- **break-before-make**: arm64는 유효한 PTE를 다른 유효한 PTE로 "그냥 덮어쓰는" 것을 일부 경우(출력 주소·크기 변경) 금지한다. 먼저 invalid로 만들고(break) TLB를 flush한 뒤 새 값을 쓴다(make). `ptep_clear_flush()` 후 `set_pte_at()` 순서가 그 규칙이다.
- **ASID가 TLB를 "깨끗하게" 해 주지는 않는다**: ASID는 flush를 줄여 줄 뿐, PTE를 바꾸면 여전히 그 ASID에 대한 무효화가 필요하다.

---

## 4. `mm_struct`와 `vm_area_struct` — 주소 공간을 표현하는 자료구조

### 4.1 직관: "토지 대장"과 "실제 측량도"

- **VMA(`struct vm_area_struct`)** = 토지 대장. "0xffff9f955000부터 4 KiB는 읽기·쓰기 가능한 익명 메모리"처럼 **의도**를 적는다. `/proc/<pid>/maps`의 한 줄이 VMA 하나다.
- **page table** = 실제 측량도. 지금 이 순간 **실제로 붙어 있는** 페이지만 적는다. 비어 있어도 VMA가 있으면 합법이다(demand paging).
- **`mm_struct`** = 둘을 묶는 주소 공간 객체. `task_struct->mm`이 가리킨다. 스레드들은 같은 `mm`을 공유하고, 커널 스레드는 `mm`이 NULL이다.

page fault 처리는 결국 "대장(VMA)에는 있는데 측량도(PTE)에 없는 것을 채우는" 일이다. 대장에도 없으면 SIGSEGV다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="20" y="20" width="200" height="36" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="120" y="43" font-size="13" text-anchor="middle">task_struct (스레드마다)</text>
<line x1="120" y1="56" x2="120" y2="88" stroke="currentColor" marker-end="url(#ah4)"/><text x="160" y="77" font-size="12" text-anchor="middle">-&gt;mm</text>
<rect x="20" y="90" width="200" height="160" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="120" y="112" font-size="13" text-anchor="middle">mm_struct</text>
<text x="120" y="136" font-size="12" text-anchor="middle">pgd (TTBR0에 들어감)</text>
<text x="120" y="158" font-size="12" text-anchor="middle">mm_mt (maple tree)</text>
<text x="120" y="180" font-size="12" text-anchor="middle">map_count, rss_stat</text>
<text x="120" y="202" font-size="12" text-anchor="middle">context.id (ASID)</text>
<text x="120" y="224" font-size="12" text-anchor="middle">mmap_lock</text>
<rect x="290" y="40" width="200" height="40" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="390" y="65" font-size="12" text-anchor="middle">VMA: 실행 파일 r-x</text>
<rect x="290" y="105" width="200" height="40" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="390" y="130" font-size="12" text-anchor="middle">VMA: heap rw- (익명)</text>
<rect x="290" y="170" width="200" height="40" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="390" y="195" font-size="12" text-anchor="middle">VMA: mmap rw- (익명)</text>
<line x1="220" y1="158" x2="288" y2="62" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="220" y1="158" x2="288" y2="125" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="220" y1="158" x2="288" y2="190" stroke="currentColor" marker-end="url(#ah4)"/>
<rect x="520" y="35" width="145" height="50" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="592" y="56" font-size="12" text-anchor="middle">address_space</text><text x="592" y="74" font-size="12" text-anchor="middle">page cache</text>
<rect x="520" y="140" width="145" height="50" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="592" y="161" font-size="12" text-anchor="middle">anon_vma</text><text x="592" y="179" font-size="12" text-anchor="middle">익명 rmap</text>
<line x1="490" y1="60" x2="518" y2="60" stroke="currentColor" marker-end="url(#ah4)"/><text x="504" y="30" font-size="12" text-anchor="middle">vm_file</text>
<line x1="490" y1="125" x2="518" y2="160" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="490" y1="190" x2="518" y2="170" stroke="currentColor" marker-end="url(#ah4)"/>
<rect x="20" y="300" width="300" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="170" y="325" font-size="13" text-anchor="middle">pgd → p4d → pud → pmd → pte</text>
<line x1="60" y1="250" x2="60" y2="298" stroke="currentColor" marker-end="url(#ah4)"/><text x="100" y="280" font-size="12" text-anchor="middle">-&gt;pgd</text>
<rect x="400" y="300" width="265" height="40" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="532" y="325" font-size="13" text-anchor="middle">물리 페이지 (struct folio)</text>
<line x1="320" y1="320" x2="398" y2="320" stroke="currentColor" marker-end="url(#ah4)"/><text x="359" y="312" font-size="12" text-anchor="middle">PFN</text>
<line x1="592" y1="85" x2="592" y2="138" stroke="#888" stroke-dasharray="4 3"/>
<line x1="620" y1="190" x2="620" y2="298" stroke="#888" stroke-dasharray="4 3" marker-end="url(#ah4)"/>
<text x="560" y="255" font-size="12" text-anchor="middle">rmap: 이 페이지를</text><text x="560" y="272" font-size="12" text-anchor="middle">누가 매핑하나?</text>
<text x="340" y="380" font-size="12" text-anchor="middle">VMA = 의도(대장), page table = 현재 상태(측량도), folio = 실제 RAM</text>
</svg>
```

그림 4 — 주소 공간 자료구조. 정방향(VA → PFN)은 `mm->pgd`의 page table이, 역방향(페이지 → 이걸 매핑한 PTE들)은 `address_space`(파일)와 `anon_vma`(익명)의 rmap이 담당한다. reclaim이 페이지를 빼앗을 때 역방향이 필요하다(§7, §11).

### 4.2 예제 6: 6.x에서 VMA는 maple tree에 있다 (소스 확인)

예전 커널(5.x까지)은 VMA를 red-black tree + 연결 리스트(`mm->mmap`)로 관리했다. 6.1부터 **maple tree**(범위 검색에 특화된 B-tree 계열, RCU 친화적)로 바뀌었다. 인터넷의 옛 글에 나오는 `mm->mmap`, `vma->vm_next`는 이제 없다.

```sh
S=/var/lab/src/linux-6.18.55
grep -n "mm_mt\|struct maple_tree" $S/include/linux/mm_types.h | head
grep -n "lock_vma_under_rcu\|handle_mm_fault\|static int do_page_fault\|do_translation_fault\|do_mem_abort" $S/arch/arm64/mm/fault.c | head
```

```text
963:		struct maple_tree mm_mt;
1361:			.tree = &(__mm)->mm_mt,				\
1371:	mas_init(&vmi->mas, &mm->mm_mt, addr);
629:	vma = lock_vma_under_rcu(mm, addr);
657:	fault = handle_mm_fault(vma, addr, mm_flags | FAULT_FLAG_VMA_LOCK, regs);
700:	fault = handle_mm_fault(vma, addr, mm_flags, regs);
786:static int __kprobes do_translation_fault(unsigned long far,
928:void do_mem_abort(unsigned long far, unsigned long esr, struct pt_regs *regs)
```

출력에서 볼 것: `mm_struct`에 `struct maple_tree mm_mt`가 있고, VMA 순회는 `VMA_ITERATOR`/`vma_iterator`(`mas_init(&vmi->mas, &mm->mm_mt, addr)`)로 한다. 그리고 arm64 fault 경로에 `handle_mm_fault` 호출이 **두 군데**(657행, 700행) 있다. 첫 번째는 **per-VMA lock**(`CONFIG_PER_VMA_LOCK=y`) 경로다.

### 4.3 per-VMA lock — mmap_lock 병목 줄이기

옛날에는 page fault마다 `mmap_lock`(예전 이름 `mmap_sem`)을 읽기로 잡았다. 스레드 수십 개가 동시에 fault를 내면 이 lock 하나에 캐시 라인이 몰린다. 6.4 무렵부터 fault 경로는 먼저 `lock_vma_under_rcu()`로 **그 VMA 하나만** 잠그고 처리한다. VMA가 바뀌는 중이거나 I/O 대기가 필요해서 실패(`VM_FAULT_RETRY`)하면 그때 `mmap_lock`을 잡고 다시 한다(`lock_mmap:` 레이블). §5.3의 추적에서 이 두 단계가 flags 값으로 보인다.

### 4.4 `vm_flags` 한 번 읽어 보기

§2의 `flags=0x100073`을 `include/linux/mm.h` 정의로 풀면:

| 비트 | 매크로 | 뜻 |
|---|---|---|
| 0x1 | `VM_READ` | 지금 읽기 허용 |
| 0x2 | `VM_WRITE` | 지금 쓰기 허용 |
| 0x10 | `VM_MAYREAD` | `mprotect`로 읽기를 켤 수 있음 |
| 0x20 | `VM_MAYWRITE` | 쓰기를 켤 수 있음 |
| 0x40 | `VM_MAYEXEC` | 실행을 켤 수 있음 |
| 0x100000 | `VM_ACCOUNT` | overcommit 계산(`Committed_AS`)에 포함 |

드라이버의 `.mmap`에서 `vm_flags_set(vma, VM_IO | VM_DONTEXPAND | VM_DONTDUMP)`처럼 플래그를 붙이는 게 바로 이 필드다(6.3부터는 직접 대입 대신 `vm_flags_set()`/`vm_flags_clear()`를 쓴다). G5, H9에서 다시 나온다.

### 4.5 흔한 함정

- **`mmap_lock`을 잡은 채 `copy_to_user`**: `copy_to_user`가 page fault를 내면 fault 경로가 같은 `mmap_lock`을 잡으려다 데드락이나 lockdep 경고가 난다. 드라이버의 `.mmap`이나 fault 핸들러 안에서 유저 메모리를 만지지 않는다.
- **옛 API로 VMA 순회**: `for (vma = mm->mmap; vma; vma = vma->vm_next)`는 6.1 이후 컴파일이 안 된다. `VMA_ITERATOR(vmi, mm, 0); for_each_vma(vmi, vma)`를 쓴다.
- **VMA 수 제한**: `vm.max_map_count`(기본 65530)를 넘으면 `mmap`이 `ENOMEM`이다. 작은 `mmap`을 수만 개 만드는 할당기에서 "메모리는 남는데 ENOMEM"의 원인이 된다.

---

## 5. page fault 처리 경로 (arm64)

### 5.1 직관: abort 핸들러가 "정상 업무"를 하는 곳

bare-metal에서 data abort는 거의 항상 버그였다. Don의 SSD 펌웨어에서 abort 핸들러는 레지스터를 덤프하고 assert로 멈췄을 것이다. Linux에서 유저 공간의 translation fault는 **하루에 수백만 번 일어나는 정상 업무**다. 커널은 ESR_EL1(Exception Syndrome Register)을 보고 원인을 가른 뒤, VMA를 확인하고, 페이지를 만들어 PTE를 채우고, 그 명령으로 돌아가 **다시 실행**시킨다.

진입부는 arm64 전용이고, `handle_mm_fault()`부터는 모든 아키텍처 공통(`mm/memory.c`)이다.

1. EL0에서 data abort → 예외 벡터 `el0t_64_sync` → `el0t_64_sync_handler` → `el0_da`
2. `do_mem_abort(far, esr, regs)`: ESR의 DFSC(fault status code)로 `fault_info[]` 표를 인덱싱해 핸들러를 고른다
3. translation fault(레벨 0~3)면 `do_translation_fault()` → 유저 주소면 `do_page_fault()`. permission fault(쓰기 금지 위반 등)와 access flag fault도 `do_page_fault()`로 간다
4. `do_page_fault()`: 먼저 `lock_vma_under_rcu()`로 VMA를 찾고 `vm_flags`로 권한을 확인 → `handle_mm_fault()`
5. `__handle_mm_fault()`: 필요하면 상위 표(pud/pmd)를 할당하고, THP 조건이면 huge 경로로, 아니면 `handle_pte_fault()`
6. `handle_pte_fault()`가 PTE 상태에 따라 갈라진다 (그림 5)

`fault_info[]` 표의 앞부분을 랩 소스에서 확인한다.

```sh
grep -n "translation fault" /var/lab/src/linux-6.18.55/arch/arm64/mm/fault.c | head -4
```

```text
866:	{ do_translation_fault,	SIGSEGV, SEGV_MAPERR,	"level 0 translation fault"	},
867:	{ do_translation_fault,	SIGSEGV, SEGV_MAPERR,	"level 1 translation fault"	},
868:	{ do_translation_fault,	SIGSEGV, SEGV_MAPERR,	"level 2 translation fault"	},
869:	{ do_translation_fault,	SIGSEGV, SEGV_MAPERR,	"level 3 translation fault"	},
```

출력에서 볼 것: "레벨 N translation fault"는 walk가 **몇 번째 단계에서** invalid 엔트리를 만났는지다. §2의 1단계(mmap 직후)는 pmd까지는 있고 pte가 0이었으니 level 3 translation fault가 났을 것이다. 처리가 안 되면(VMA 없음) 같은 줄의 `SIGSEGV, SEGV_MAPERR`이 유저에게 간다.

실제 call trace는 §9의 OOM 로그에 공짜로 찍혀 있다. 익명 페이지에 처음 쓰다가 메모리가 없어 OOM이 난 순간의 스택이다(아래에서 위로 읽는다):

```text
[   35.667761]  vma_alloc_zeroed_movable_folio+0x34/0x40
[   35.667775]  __handle_mm_fault+0xdb8/0x1880
[   35.667788]  handle_mm_fault+0x88/0x29c
[   35.667800]  do_page_fault+0x108/0x6e0
[   35.667812]  do_translation_fault+0x5c/0x6c
[   35.667824]  do_mem_abort+0x40/0x98
[   35.667836]  el0_da+0x30/0xb8
[   35.667871]  el0t_64_sync_handler+0xd0/0xe4
[   35.667882]  el0t_64_sync+0x198/0x19c
```

`handle_pte_fault`와 `do_anonymous_page`가 스택에 없는 이유는 둘 다 `static`이라 컴파일러가 `__handle_mm_fault` 안으로 **인라인**했기 때문이다. `vma_alloc_zeroed_movable_folio`가 `do_anonymous_page`의 "0으로 채운 새 페이지 할당" 단계다.

```svg
<svg viewBox="0 0 680 500" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="170" y="15" width="340" height="36" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="340" y="38" font-size="13" text-anchor="middle">EL0 data/instruction abort (ESR)</text>
<rect x="110" y="75" width="460" height="36" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="340" y="98" font-size="12" text-anchor="middle">do_mem_abort → do_translation_fault → do_page_fault</text>
<rect x="170" y="135" width="320" height="36" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="330" y="158" font-size="12" text-anchor="middle">VMA 있나? (lock_vma_under_rcu)</text>
<rect x="520" y="135" width="150" height="36" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="595" y="158" font-size="12" text-anchor="middle">SIGSEGV MAPERR</text>
<rect x="170" y="195" width="320" height="36" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="330" y="218" font-size="12" text-anchor="middle">권한 맞나? (vm_flags)</text>
<rect x="520" y="195" width="150" height="36" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="595" y="218" font-size="12" text-anchor="middle">SIGSEGV ACCERR</text>
<rect x="170" y="255" width="320" height="36" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="330" y="278" font-size="12" text-anchor="middle">handle_mm_fault → handle_pte_fault</text>
<line x1="340" y1="51" x2="340" y2="73" stroke="currentColor" marker-end="url(#ah5)"/>
<line x1="330" y1="111" x2="330" y2="133" stroke="currentColor" marker-end="url(#ah5)"/>
<line x1="330" y1="171" x2="330" y2="193" stroke="currentColor" marker-end="url(#ah5)"/>
<line x1="330" y1="231" x2="330" y2="253" stroke="currentColor" marker-end="url(#ah5)"/>
<line x1="490" y1="153" x2="518" y2="153" stroke="currentColor" marker-end="url(#ah5)"/><text x="504" y="146" font-size="12" text-anchor="middle">없음</text>
<line x1="490" y1="213" x2="518" y2="213" stroke="currentColor" marker-end="url(#ah5)"/><text x="504" y="206" font-size="12" text-anchor="middle">위반</text>
<line x1="330" y1="291" x2="90" y2="328" stroke="currentColor" marker-end="url(#ah5)"/>
<line x1="330" y1="291" x2="257" y2="328" stroke="currentColor" marker-end="url(#ah5)"/>
<line x1="330" y1="291" x2="424" y2="328" stroke="currentColor" marker-end="url(#ah5)"/>
<line x1="330" y1="291" x2="591" y2="328" stroke="currentColor" marker-end="url(#ah5)"/>
<rect x="15" y="330" width="150" height="110" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="90" y="352" font-size="12" text-anchor="middle">PTE 없음 · 익명</text><text x="90" y="374" font-size="12" text-anchor="middle">do_anonymous_page</text><text x="90" y="396" font-size="12" text-anchor="middle">읽기: zero page</text><text x="90" y="418" font-size="12" text-anchor="middle">쓰기: 새 페이지</text>
<rect x="182" y="330" width="150" height="110" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="257" y="352" font-size="12" text-anchor="middle">PTE 없음 · 파일</text><text x="257" y="374" font-size="12" text-anchor="middle">do_fault</text><text x="257" y="396" font-size="12" text-anchor="middle">read/cow/shared</text><text x="257" y="418" font-size="12" text-anchor="middle">→ filemap_fault</text>
<rect x="349" y="330" width="150" height="110" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="424" y="352" font-size="12" text-anchor="middle">PTE = swap entry</text><text x="424" y="374" font-size="12" text-anchor="middle">do_swap_page</text><text x="424" y="396" font-size="12" text-anchor="middle">swap/zram 읽기</text><text x="424" y="418" font-size="12" text-anchor="middle">보통 major</text>
<rect x="516" y="330" width="150" height="110" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="591" y="352" font-size="12" text-anchor="middle">쓰기 + RDONLY</text><text x="591" y="374" font-size="12" text-anchor="middle">do_wp_page</text><text x="591" y="396" font-size="12" text-anchor="middle">혼자: reuse</text><text x="591" y="418" font-size="12" text-anchor="middle">공유: wp_page_copy</text>
<text x="340" y="470" font-size="12" text-anchor="middle">I/O를 기다렸으면 VM_FAULT_MAJOR (major fault), 아니면 minor fault</text>
<text x="340" y="490" font-size="12" text-anchor="middle">파일 캐시 hit이면 fault-around(filemap_map_pages)로 주변 16페이지까지 한 번에 매핑</text>
</svg>
```

그림 5 — page fault 결정 트리. 위 세 단계는 arm64 `arch/arm64/mm/fault.c`, 아래 네 갈래는 공통 `mm/memory.c`의 `handle_pte_fault()`. §2 실습은 1번(zero page → 새 페이지)과 4번(COW 복사, reuse) 갈래를 직접 밟았다.

네 갈래를 정리하면:

| 갈래 | 조건 | 함수 | 결과 | §2/§5 실습에서 |
|---|---|---|---|---|
| 익명 첫 접근 | PTE 비어 있음 + VMA에 `vm_ops` 없음 | `do_anonymous_page` | 읽기: zero page 매핑 / 쓰기: 0으로 채운 새 folio | 2단계, 3단계 |
| 파일 첫 접근 | PTE 비어 있음 + 파일 VMA | `do_fault` → `do_read_fault` / `do_cow_fault` / `do_shared_fault` → `vm_ops->fault` = `filemap_fault` | page cache hit면 minor, miss면 I/O → major | §5.3 T2 |
| swap 돼 있음 | PTE가 swap entry(present 아님) | `do_swap_page` | swap/zram에서 읽어 옴, 보통 major | §8 zram 실험의 `pswpin` |
| COW / 쓰기 권한 회복 | 쓰기인데 PTE가 RDONLY | `do_wp_page` → `wp_page_reuse` 또는 `wp_page_copy` | 혼자면 권한만, 공유면 복사 | 5단계(copy), 7단계(reuse) |

드라이버가 `vm_operations_struct.fault`를 구현하면 "파일 첫 접근" 갈래에서 `filemap_fault` 대신 **드라이버 함수가 불린다**. 장치 메모리를 lazy하게 매핑하는 드라이버(G5, H9)가 여기에 걸린다.

### 5.2 예제 7: arm64에서 무엇을 추적할 수 있나

x86에는 `exceptions:page_fault_user`라는 tracepoint가 있다. arm64에는? 디버그 커널 게스트에서 직접 확인했다.

```sh
mount -t tracefs none /sys/kernel/tracing
cd /sys/kernel/tracing
echo "--- events with 'fault' in name:"
grep -i fault available_events
echo "--- filterable fault-path functions:"
grep -wE "do_mem_abort|do_translation_fault|do_page_fault|handle_mm_fault|__handle_mm_fault|handle_pte_fault|do_anonymous_page|do_wp_page|wp_page_copy|wp_page_reuse|do_fault|do_read_fault|do_cow_fault|do_shared_fault|filemap_fault|do_swap_page|filemap_map_pages|lock_vma_under_rcu|do_numa_page|set_pte_range|finish_fault" available_filter_functions | sort
```

```text
--- events with 'fault' in name:
kvm:kvm_access_fault
kvm:kvm_guest_fault
filemap:mm_filemap_fault
iommu:io_page_fault
--- filterable fault-path functions:
__handle_mm_fault
do_mem_abort
do_swap_page
do_wp_page
filemap_fault
filemap_map_pages
finish_fault
handle_mm_fault
lock_vma_under_rcu
set_pte_range
```

출력에서 볼 것:

- arm64에는 **`exceptions:page_fault_user` 같은 이벤트가 없다**. fault 관련 이벤트는 `filemap:mm_filemap_fault`(파일 fault) 정도다.
- `do_translation_fault`, `do_page_fault`는 목록에 없다. `__kprobes`/`NOKPROBE_SYMBOL`로 표시돼 추적이 금지된 함수다(예외 처리 중 kprobe가 다시 예외를 내면 재귀하기 때문).
- `do_anonymous_page`, `handle_pte_fault`, `wp_page_copy`도 없다 — 인라인돼서 함수로 존재하지 않는다.
- 결론: arm64에서 fault를 하나하나 보려면 **`handle_mm_fault`에 kprobe**를 거는 것이 가장 확실하다. 개수만 필요하면 `/proc/vmstat`의 `pgfault`/`pgmajfault`, 프로세스별로는 `getrusage()`나 `/proc/<pid>/stat`의 10·12번째 필드면 된다.

### 5.3 예제 8: kprobe로 fault 하나하나 보기 — 익명, COW, 파일 major

arm64 호출 규약에서 인자는 x0, x1, x2… 순서다. `handle_mm_fault(vma, address, flags, regs)`이므로 x1 = 주소, x2 = flags다. 리턴값은 kretprobe의 `$retval`로 본다.

```sh
# 게스트 안 (디버그 커널). /f11/faults는 §5.4의 프로그램
mount -o ro,noload /dev/vda /mnt 2>/dev/null
mount -t tracefs none /sys/kernel/tracing; cd /sys/kernel/tracing
echo 'p:f11/hmf handle_mm_fault addr=%x1:x64 flags=%x2:x32' >> kprobe_events
echo 'r:f11/hmf_ret handle_mm_fault ret=$retval:x32' >> kprobe_events
echo 1 > options/event-fork
echo 1 > events/f11/enable; echo 1 > events/filemap/mm_filemap_fault/enable
echo 3 > /proc/sys/vm/drop_caches
echo "=== T1 cow"
sh -c 'echo $$ > /sys/kernel/tracing/set_event_pid; exec /f11/faults cow 2'
grep -v "^#" trace | grep -A1 "000 flags=0x12[0-9a-f]5" | grep -v "^--"
echo 0 > set_event_pid; echo > trace; echo "=== T2 file"
sh -c 'echo $$ > /sys/kernel/tracing/set_event_pid; exec /f11/faults file /mnt/small'
grep -v "^#" trace | grep -B2 -A3 "mm_filemap_fault" | head -14
```

`set_event_pid`에 셸 자신의 PID를 넣고 `exec`하면 그 프로그램만 추적된다. `options/event-fork`를 켜면 fork한 자식도 따라간다. T1의 `grep`은 "페이지 경계 주소(…000)에 대한 쓰기 fault"만 고른다. 실제 출력:

```text
=== T1 cow
anon  pid=69 pages=2 minflt=4 majflt=0  2488.0 us/page
cow   pid=70 pages=2 minflt=5 majflt=0  2020.2 us/page
          faults-69      [000] d....    26.940457: hmf: (handle_mm_fault+0x0/0x3b0) addr=0x630000 flags=0x1255
          faults-69      [000] d..1.    26.940645: hmf_ret: (do_page_fault+0x154/0x7a0 <- handle_mm_fault) ret=0x200
          faults-69      [000] d....    27.201010: hmf: (handle_mm_fault+0x0/0x3b0) addr=0xffff8654c000 flags=0x1255
          faults-69      [000] d..1.    27.201295: hmf_ret: (do_page_fault+0x154/0x7a0 <- handle_mm_fault) ret=0x0
          faults-69      [000] d....    27.201414: hmf: (handle_mm_fault+0x0/0x3b0) addr=0xffff8654d000 flags=0x1255
          faults-69      [000] d..1.    27.205682: hmf_ret: (do_page_fault+0x154/0x7a0 <- handle_mm_fault) ret=0x0
          faults-70      [000] d....    27.329142: hmf: (handle_mm_fault+0x0/0x3b0) addr=0xffff8654c000 flags=0x1255
          faults-70      [000] d..1.    27.331910: hmf_ret: (do_page_fault+0x154/0x7a0 <- handle_mm_fault) ret=0x0
          faults-70      [000] d....    27.332579: hmf: (handle_mm_fault+0x0/0x3b0) addr=0xffff8654d000 flags=0x1255
          faults-70      [000] d..1.    27.332980: hmf_ret: (do_page_fault+0x154/0x7a0 <- handle_mm_fault) ret=0x0
=== T2 file
file  pid=74 pages=3 minflt=2 majflt=3  6178.3 us/page
          faults-74      [000] d..1.    28.168359: hmf_ret: (do_page_fault+0x1e0/0x7a0 <- handle_mm_fault) ret=0x100
          faults-74      [000] d....    28.168612: hmf: (handle_mm_fault+0x0/0x3b0) addr=0xffffb422f000 flags=0x1254
          faults-74      [000] .....    28.168860: mm_filemap_fault: dev=254:0 ino=e ofs=0
          faults-74      [000] d..1.    28.182664: hmf_ret: (do_page_fault+0x154/0x7a0 <- handle_mm_fault) ret=0x404
          faults-74      [000] d....    28.182736: hmf: (handle_mm_fault+0x0/0x3b0) addr=0xffffb422f000 flags=0x274
          faults-74      [000] d..1.    28.182912: hmf_ret: (do_page_fault+0x1e0/0x7a0 <- handle_mm_fault) ret=0x100
          faults-74      [000] d....    28.183071: hmf: (handle_mm_fault+0x0/0x3b0) addr=0xffffb4230000 flags=0x1254
          faults-74      [000] .....    28.183132: mm_filemap_fault: dev=254:0 ino=e ofs=4096
          faults-74      [000] d..1.    28.184981: hmf_ret: (do_page_fault+0x154/0x7a0 <- handle_mm_fault) ret=0x404
          faults-74      [000] d....    28.185019: hmf: (handle_mm_fault+0x0/0x3b0) addr=0xffffb4230000 flags=0x274
          faults-74      [000] d..1.    28.185111: hmf_ret: (do_page_fault+0x1e0/0x7a0 <- handle_mm_fault) ret=0x100
          faults-74      [000] d....    28.185223: hmf: (handle_mm_fault+0x0/0x3b0) addr=0xffffb4231000 flags=0x1254
          faults-74      [000] .....    28.185298: mm_filemap_fault: dev=254:0 ino=e ofs=8192
          faults-74      [000] d..1.    28.186785: hmf_ret: (do_page_fault+0x154/0x7a0 <- handle_mm_fault) ret=0x404
```

flags와 리턴값을 `include/linux/mm_types.h`의 `enum fault_flag`, `enum vm_fault_reason`으로 풀면:

| 값 | 비트 해석 | 뜻 |
|---|---|---|
| flags `0x1255` | VMA_LOCK(0x1000) + INTERRUPTIBLE(0x200) + USER(0x40) + KILLABLE(0x10) + ALLOW_RETRY(0x4) + WRITE(0x1) | 유저 쓰기 fault, per-VMA lock 경로 |
| flags `0x1254` | 위와 같고 WRITE 없음 | 유저 읽기 fault, per-VMA lock 경로 |
| flags `0x274` | INTERRUPTIBLE + USER + TRIED(0x20) + KILLABLE + ALLOW_RETRY, VMA_LOCK 없음 | 재시도: 이번엔 `mmap_lock`을 잡고 |
| ret `0x0` | 없음 | 정상 처리 (새 페이지 설치 또는 COW 복사) |
| ret `0x100` | `VM_FAULT_NOPAGE` | PTE를 이미 설치했음 (fault-around 등) |
| ret `0x200` | `VM_FAULT_LOCKED` | 페이지를 잠근 채 돌려줌 (파일 페이지 경로) |
| ret `0x404` | `VM_FAULT_RETRY` + `VM_FAULT_MAJOR` | I/O를 시작했으니 lock 풀고 다시 오라, 그리고 이건 major |

출력에서 볼 것:

- **T1 익명 쓰기**: 부모(pid 69)가 페이지 두 개(`...c000`, `...d000`)에 쓰며 fault 2번, ret=0. 첫 줄 `addr=0x630000`은 glibc 초기화의 쓰기 fault다(파일 매핑의 쓰기라 `LOCKED` 경로).
- **T1 COW**: 자식(pid 70)이 **같은 주소**에 쓰자 다시 fault 2번. 이번엔 PTE가 있는데 RDONLY라서 `do_wp_page` → 복사 경로다. `getrusage`로 본 자식 `minflt=5`에는 이 2번과 exit 경로 fault가 섞여 있다.
- **T2 파일 major**: page cache를 비운 뒤(`drop_caches`) 파일 3페이지를 읽는다. 페이지마다 패턴이 똑같다. (1) per-VMA lock 경로로 시도(`0x1254`) → `mm_filemap_fault` 이벤트 → I/O를 걸고 lock을 풀며 `ret=0x404`(RETRY|MAJOR). (2) `TRIED` 플래그를 달고 `mmap_lock` 경로로 재시도(`0x274`) → 이제 page cache에 있으니 바로 설치, `ret=0x100`. **major fault 한 번 = handle_mm_fault 두 번**이다. 디스크 I/O 동안 lock을 쥐고 있지 않으려는 설계다.

### 5.4 예제 9: minor vs major, THP, fault-around를 숫자로

무엇을 확인하는 코드인가: `anon N`은 익명 N페이지에 쓰기, `cow N`은 그 뒤 fork해서 자식이 다시 쓰기, `file PATH`는 파일을 mmap해서 페이지마다 1바이트 읽기(`MADV_RANDOM`으로 readahead 끔). 각 구간의 minor/major fault 수를 `getrusage()`로 잰다.

```c
// faults: count minor/major faults for anon write, COW after fork, file read
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static struct rusage r0; static struct timespec t0;
static void begin(void) { getrusage(RUSAGE_SELF, &r0); clock_gettime(CLOCK_MONOTONIC, &t0); }
static void end(const char *tag, long pages)
{
	struct rusage r1; struct timespec t1;
	clock_gettime(CLOCK_MONOTONIC, &t1); getrusage(RUSAGE_SELF, &r1);
	double us = (t1.tv_sec - t0.tv_sec) * 1e6 + (t1.tv_nsec - t0.tv_nsec) / 1e3;
	long mn = r1.ru_minflt - r0.ru_minflt, mj = r1.ru_majflt - r0.ru_majflt;
	printf("%-5s pid=%d pages=%ld minflt=%ld majflt=%ld  %.1f us/page\n",
	       tag, getpid(), pages, mn, mj, us / pages);
}

int main(int argc, char **argv)
{
	if (argc < 3) { fprintf(stderr, "usage: %s anon|cow N | file PATH\n", argv[0]); return 1; }
	long ps = sysconf(_SC_PAGESIZE), n = atol(argv[2]);
	if (strcmp(argv[1], "file") == 0) {
		int fd = open(argv[2], O_RDONLY); struct stat st;
		if (fd < 0 || fstat(fd, &st) < 0) { perror("open"); return 1; }
		n = st.st_size / ps;
		volatile char *p = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
		if (p == MAP_FAILED) { perror("mmap"); return 1; }
		madvise((void *)p, st.st_size, MADV_RANDOM);   /* no readahead */
		begin(); for (long i = 0; i < n; i++) (void)p[i * ps]; end("file", n);
		return 0;
	}
	volatile char *p = mmap(NULL, n * ps, PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED) { perror("mmap"); return 1; }
	begin(); for (long i = 0; i < n; i++) p[i * ps] = 1; end("anon", n);
	if (strcmp(argv[1], "cow") == 0) {
		pid_t pid = fork();
		if (pid < 0) { perror("fork"); return 1; }
		if (pid == 0) { begin(); for (long i = 0; i < n; i++) p[i * ps] = 2; end("cow", n); _exit(0); }
		waitpid(pid, NULL, 0);
	}
	return 0;
}
```

비디버그 랩 커널, `-m 1024M` 게스트에서 실행했다. `/mnt/data`는 16 MiB 랜덤 파일이 든 ext4 이미지(virtio 디스크, 읽기 전용 마운트)다. 게스트 스크립트:

```sh
F=/f11/faults
vm() { grep -E "^(pgfault|pgmajfault|thp_fault_alloc) " /proc/vmstat | tr '\n' ' '; echo; }
uname -r; grep MemTotal /proc/meminfo
for m in always never; do
  echo $m > /sys/kernel/mm/transparent_hugepage/enabled
  echo "### THP: $(cat /sys/kernel/mm/transparent_hugepage/enabled)"
  vm; $F anon 25600; vm
done
$F cow 25600
mount -o ro,noload /dev/vda /mnt
echo 3 > /proc/sys/vm/drop_caches
echo "### cold file"; vm; $F file /mnt/data; vm
echo "### warm file"; $F file /mnt/data
```

```text
lab initramfs: 6.18.55 up 4.05s
6.18.55
MemTotal:         987128 kB
### THP: [always] madvise never
pgfault 1284 pgmajfault 0 thp_fault_alloc 0 
anon  pid=70 pages=25600 minflt=51 majflt=0  17.3 us/page
pgfault 1568 pgmajfault 0 thp_fault_alloc 50 
### THP: always madvise [never]
pgfault 1755 pgmajfault 0 thp_fault_alloc 50 
anon  pid=76 pages=25600 minflt=25601 majflt=0  207.2 us/page
pgfault 27653 pgmajfault 0 thp_fault_alloc 50 
anon  pid=79 pages=25600 minflt=25601 majflt=0  230.8 us/page
cow   pid=80 pages=25600 minflt=25603 majflt=0  124.2 us/page[   21.798285] EXT4-fs: Mount option(s) incompatible with ext2

### cold file
pgfault 79197 pgmajfault 0 thp_fault_alloc 50 
file  pid=85 pages=4096 minflt=2 majflt=4096  1132.0 us/page
pgfault 83607 pgmajfault 4096 thp_fault_alloc 50 
### warm file
file  pid=88 pages=4096 minflt=258 majflt=0  5.5 us/page
```

(EXT4 줄은 mount가 ext2 드라이버로 먼저 시도하며 남긴 커널 메시지가 출력 중간에 끼어든 것이다.)

출력에서 볼 것:

- **THP always**: 100 MiB(25600페이지)를 만졌는데 fault는 **51번**, `thp_fault_alloc`이 50 늘었다. fault 한 번에 2 MiB(512페이지)짜리 huge page를 통째로 붙였다. pmd 단계 block 매핑이라 PTE 표 자체가 없다.
- **THP never**: 정확히 페이지당 1번(25601). `pgfault`도 그만큼 늘었다.
- **COW**: 자식도 페이지당 1번(25603). fork 후 첫 쓰기마다 `do_wp_page` → 복사.
- **cold file**: 4096페이지 전부 **major**. readahead를 껐으니 페이지마다 디스크 I/O.
- **warm file**: 이번엔 major 0, minor **258** = 4096 / 16 + 2. 페이지가 page cache에 있으면 `filemap_map_pages`가 **fault-around**로 주변 16페이지(64 KiB, `fault_around_pages = 65536 >> PAGE_SHIFT`)를 한꺼번에 매핑한다. `MADV_RANDOM`은 readahead만 끄고 fault-around는 끄지 않는다.
- **시간 값은 무시해도 된다**: TCG 에뮬레이션에 다른 작업자들이 같은 호스트를 쓰는 중이라 같은 실험의 반복도 2배씩 흔들렸다. 자릿수만 보면 cold major(ms 단위) ≫ 익명 minor(수백 µs, 0으로 채우기가 에뮬레이션에서 비쌈) > warm 파일(수 µs, 페이지 할당이 없음)이다. 실제 보드의 minor fault는 A5에서 잰 것처럼 1 µs 안팎이다.

참고로 `-m 512M` 이하 게스트에서는 THP가 처음부터 `[never]`였다. `mm/huge_memory.c`의 `hugepage_init()`에 "`totalram_pages() < MB_TO_PAGES(512)`이면 THP 기본 끔"이 있다. RAM이 작은 임베디드 보드에서 THP가 꺼져 있는 이유다.

### 5.5 흔한 함정

- **"page fault = 성능 문제"라고 단정**: minor fault는 정상 동작이다. 문제는 RT 루프 안의 fault(지터)와 major fault(I/O 대기)다. RT 스레드는 `mlockall(MCL_CURRENT|MCL_FUTURE)` + 스택 미리 만지기(A5 §8)로 루프 중 fault를 없앤다.
- **major fault를 "swap 때문"으로만 생각**: swap이 없어도 major fault는 난다. 실행 파일과 라이브러리 코드 페이지는 page cache에 있고, 메모리 압박에 밀려나면 다음 실행 때 eMMC에서 다시 읽는다. swap 없는 장비가 OOM 직전에 버벅이는(thrashing) 이유가 이것이다(§7).
- **THP 지연**: THP `always`는 처음 만질 때 2 MiB를 0으로 채우고, 조각난 메모리에서 huge page를 만들려고 compaction을 돌 수 있다. RT 장비는 `madvise`나 `never`로 둔다.

---

## 6. Page cache와 dirty writeback

### 6.1 직관: 커널이 들고 있는 거대한 디스크 캐시

파일을 `read()`하든 `mmap()`하든 데이터는 먼저 **page cache**(파일별 `struct address_space` 안의 xarray에 매달린 folio들)로 들어온다. 다시 읽으면 디스크를 안 간다. §5.4의 warm file이 그 효과다. 쓰기도 마찬가지로 page cache 페이지를 고치고 **dirty** 표시만 한 뒤 바로 돌아온다. 실제 저장장치 쓰기(writeback)는 나중에 **flusher 스레드**(`kworker/u*:*-flush-<dev>`)가 한다.

Don의 SSD 펌웨어로 치면 page cache = 호스트 쪽의 write-back 캐시, flusher = 주기적 flush 커맨드다. SSD 안의 쓰기 버퍼가 전원 차단에 취약하듯(그래서 PLP 커패시터가 있다) page cache의 dirty 데이터도 전원 차단에 취약하다. 그래서 `fsync()`가 있다.

언제 writeback이 시작되나 — `/proc/sys/vm`의 손잡이:

| sysctl | 기본값 | 뜻 |
|---|---|---|
| `dirty_background_ratio` | 10 | dirty가 "dirty 가능 메모리"의 10%를 넘으면 flusher가 백그라운드로 쓰기 시작 |
| `dirty_ratio` | 20 | 20%를 넘으면 **쓰는 프로세스 자신이** `balance_dirty_pages()`에서 멈춰 기다린다 (쓰기 지연 발생) |
| `dirty_expire_centisecs` | 3000 | dirty가 된 지 30초 넘은 페이지는 무조건 쓴다 |
| `dirty_writeback_centisecs` | 500 | flusher가 5초마다 깨어 확인 |

### 6.2 예제 10: dirty가 쌓이고 빠지는 것 보기

`-m 256M` 게스트에서 쓰기 가능한 ext4 디스크(이미지 복사본)에 12 MiB, 이어서 48 MiB를 쓰며 `/proc/meminfo`의 `Dirty`/`Writeback`을 본다.

```sh
mount /dev/vda /mnt 2>/dev/null
for k in dirty_background_ratio dirty_ratio dirty_expire_centisecs dirty_writeback_centisecs; do echo -n "$k=$(cat /proc/sys/vm/$k) "; done; echo
d() { echo "t=$1 $(grep -E '^(Dirty|Writeback|Cached):' /proc/meminfo | tr -s ' ' | tr '\n' ' ')"; }
d 0
dd if=/dev/zero of=/mnt/log.bin bs=1M count=12 2>/dev/null
d after-12M
sleep 2; d 2s
dd if=/dev/zero of=/mnt/big.bin bs=1M count=48 2>/dev/null
d after-48M
sleep 1; d 1s
sync; d after-sync
grep -E "^nr_dirty |^nr_writeback |^nr_dirtied |^nr_written " /proc/vmstat | tr '\n' ' '; echo
```

```text
dirty_background_ratio=10 dirty_ratio=20 dirty_expire_centisecs=3000 dirty_writeback_centisecs=500 
t=0 Cached: 2192 kB Dirty: 0 kB Writeback: 0 kB 
t=after-12M Cached: 14488 kB Dirty: 12288 kB Writeback: 0 kB 
t=2s Cached: 14488 kB Dirty: 12288 kB Writeback: 0 kB 
t=after-48M Cached: 41120 kB Dirty: 0 kB Writeback: 0 kB 
t=1s Cached: 41120 kB Dirty: 0 kB Writeback: 0 kB 
t=after-sync Cached: 41120 kB Dirty: 0 kB Writeback: 0 kB 
nr_dirty 0 nr_writeback 0 nr_dirtied 10759 nr_written 9735 
```

출력에서 볼 것:

- 12 MiB를 쓴 직후 `Dirty: 12288 kB` — **전부 메모리에만 있다**. 2초 뒤에도 그대로다. 이 순간 전원이 나가면 12 MiB가 사라진다. 이 게스트의 dirty 가능 메모리는 약 200 MiB라 background 임계(10%)가 약 20 MiB이고, 12 MiB는 그 아래라서 flusher가 30초 만료를 기다리는 중이다.
- 48 MiB를 더 쓰면 임계를 넘어 flusher가 깨어나고 쓰는 프로세스도 throttle되어, `dd`가 끝났을 때 이미 `Dirty: 0`이다. `dd`가 "빨리 끝난 것처럼" 보이다가 큰 파일에서 갑자기 느려지는 이유가 이 임계값이다.
- `Cached`가 60 MiB가 아니라 약 40 MiB인 것은, 256 MiB 게스트에서 이미 쓰기가 끝난 clean 페이지 일부가 reclaim으로 밀려났기 때문으로 보인다(이 실험에서 따로 확인하지는 않았다).

### 6.3 Embedded에서의 의미

- **센서 로그를 쓰는 데몬**: 로그를 계속 append하면 dirty가 쌓였다가 임계를 넘는 순간 한꺼번에 writeback되고, `dirty_ratio`를 넘으면 쓰는 스레드가 멈춘다. 같은 프로세스 안의 RT 스레드는 영향이 없지만, 로그 스레드가 lock을 쥔 채 멈추면 우선순위 역전이 된다. 해결: 로그 스레드를 분리, `dirty_background_bytes`를 작게(예: 수 MiB) 해서 자주 조금씩 쓰기, 중요한 레코드는 `fdatasync()`.
- **eMMC 수명**: writeback이 잦으면 작은 쓰기가 많아져 write amplification이 커진다. 반대로 너무 오래 모으면 전원 차단 손실이 커진다. Don이 SSD에서 하던 trade-off와 같다. 이 주제는 J 모듈(파일시스템)에서 다시 다룬다.

---

## 7. Reclaim — 빈 페이지가 모자랄 때 커널이 하는 일

### 7.1 직관: 수위계 세 눈금

커널은 zone(물리 메모리 구역)마다 빈 페이지 수에 **watermark**(수위 눈금) 세 개를 둔다.

- **high**: 여기 위면 평화. kswapd는 잔다.
- **low**: 빈 페이지가 이 아래로 떨어지면 할당 경로가 **kswapd**(노드마다 하나 있는 커널 스레드)를 깨운다. kswapd는 백그라운드에서 페이지를 회수해 high까지 채운다. 할당하는 쪽은 기다리지 않는다.
- **min**: kswapd가 못 따라가서 여기까지 떨어지면, 할당하려던 태스크가 **직접** 회수한다(**direct reclaim**). 이게 응용 프로그램에 보이는 지연이다. min 아래 예비분은 `GFP_ATOMIC`이나 메모리 해제 중인 태스크 같은 특수 경우만 쓴다.

회수(reclaim) 대상은 "다시 만들 수 있는 페이지"다.

| 페이지 종류 | 회수 방법 | 비용 |
|---|---|---|
| clean 파일 페이지 (실행 코드, 읽은 파일) | 그냥 버린다. 다음에 필요하면 저장장치에서 다시 읽는다 (major fault) | 버릴 땐 싸고, 다시 필요해지면 비쌈 |
| dirty 파일 페이지 | writeback으로 저장한 뒤 버린다 | I/O |
| 익명 페이지 (heap, stack) | **swap 공간이 있을 때만** swap out | swap 없으면 회수 불가 |
| 커널 slab (dentry, inode 캐시 등) | shrinker가 줄인다 | 캐시 미스 증가 |

**swap이 없으면 익명 메모리는 절대 회수되지 않는다.** 그래서 swap 없는 임베디드 장비는 익명 메모리가 차면 남은 파일 페이지(실행 코드까지)를 계속 버리고 다시 읽는 **thrashing** 끝에 OOM으로 간다.

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="65" y="22" font-size="12" text-anchor="middle">빈 페이지 수</text>
<rect x="30" y="30" width="70" height="120" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<rect x="30" y="150" width="70" height="50" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<rect x="30" y="200" width="70" height="50" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<rect x="30" y="250" width="70" height="110" fill="#d0564a" fill-opacity="0.3" stroke="#d0564a"/>
<line x1="30" y1="150" x2="220" y2="150" stroke="currentColor" stroke-dasharray="4 3"/><text x="160" y="145" font-size="12" text-anchor="middle">high 687</text>
<line x1="30" y1="200" x2="220" y2="200" stroke="currentColor" stroke-dasharray="4 3"/><text x="160" y="195" font-size="12" text-anchor="middle">low 573</text>
<line x1="30" y1="250" x2="220" y2="250" stroke="currentColor" stroke-dasharray="4 3"/><text x="160" y="245" font-size="12" text-anchor="middle">min 459</text>
<rect x="230" y="40" width="435" height="56" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="447" y="63" font-size="13" text-anchor="middle">high 위: 평화</text><text x="447" y="84" font-size="12" text-anchor="middle">할당은 per-CPU 리스트와 버디에서 즉시</text>
<rect x="230" y="120" width="435" height="56" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="447" y="143" font-size="13" text-anchor="middle">low 아래: kswapd 깨움</text><text x="447" y="164" font-size="12" text-anchor="middle">백그라운드로 high까지 회수 (LRU 꼬리부터)</text>
<rect x="230" y="200" width="435" height="56" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="447" y="223" font-size="13" text-anchor="middle">min 아래: direct reclaim</text><text x="447" y="244" font-size="12" text-anchor="middle">할당하던 태스크가 직접 회수 (allocstall, 지연 발생)</text>
<rect x="230" y="290" width="435" height="56" fill="#d0564a" fill-opacity="0.3" stroke="#d0564a"/><text x="447" y="313" font-size="13" text-anchor="middle">회수 실패 반복: OOM killer</text><text x="447" y="334" font-size="12" text-anchor="middle">out_of_memory → badness 최대 프로세스에 SIGKILL</text>
<line x1="447" y1="96" x2="447" y2="118" stroke="currentColor" marker-end="url(#ah6)"/>
<line x1="447" y1="176" x2="447" y2="198" stroke="currentColor" marker-end="url(#ah6)"/>
<line x1="447" y1="256" x2="447" y2="288" stroke="currentColor" marker-end="url(#ah6)"/>
<text x="340" y="385" font-size="12" text-anchor="middle">회수 대상: clean 파일 → 버림 · dirty 파일 → writeback 후 · 익명 → swap/zram 있을 때만</text>
<text x="340" y="405" font-size="12" text-anchor="middle">값은 -m 256M 게스트의 실제 /proc/zoneinfo (단위: 페이지)</text>
</svg>
```

그림 6 — 메모리 부족 사다리. 빈 페이지가 줄어들수록 대응이 "공짜(백그라운드)" → "지연(direct reclaim)" → "살생(OOM)"으로 올라간다.

### 7.2 watermark 값은 어디서 오나

`mm/page_alloc.c`의 `__setup_per_zone_wmarks()`:

```sh
grep -n "watermark_scale_factor, 10000" -B6 -A8 /var/lab/src/linux-6.18.55/mm/page_alloc.c
```

```text
6499-		 * Set the kswapd watermarks distance according to the
6500-		 * scale factor in proportion to available memory, but
6501-		 * ensure a minimum size on small systems.
6502-		 */
6503-		tmp = max_t(u64, tmp >> 2,
6504-			    mult_frac(zone_managed_pages(zone),
6505:				      watermark_scale_factor, 10000));
6506-
6507-		zone->watermark_boost = 0;
6508-		zone->_watermark[WMARK_LOW]  = min_wmark_pages(zone) + tmp;
6509-		zone->_watermark[WMARK_HIGH] = low_wmark_pages(zone) + tmp;
6510-		zone->_watermark[WMARK_PROMO] = high_wmark_pages(zone) + tmp;
6511-		trace_mm_setup_per_zone_wmarks(zone);
6512-
6513-		spin_unlock_irqrestore(&zone->lock, flags);
```

즉 min은 `vm.min_free_kbytes`에서 오고(zone 크기 비율로 나눔), low = min + 간격, high = low + 간격, 간격 = max(min/4, managed × `watermark_scale_factor`/10000)이다. 아래 §8 실험의 게스트 값으로 검산하면: min 459, min/4 = 114, managed 54112 × 10/10000 = 54 → 간격 114 → low 573, high 687. 출력과 정확히 맞는다.

작은 장비에서 의미: RAM 256 MiB면 kswapd가 깨어나는 지점(low)과 잠드는 지점(high) 사이가 114페이지(456 KiB)뿐이다. 카메라 프레임 하나(수 MB)를 할당하는 순간 바로 min 아래로 떨어져 direct reclaim에 걸린다. `watermark_scale_factor`를 키우면(예: 100 → 1%) kswapd가 더 일찍, 더 많이 미리 회수한다. 큰 버스트 할당이 있는 장비에서 쓰는 손잡이다.

### 7.3 LRU 리스트와 MGLRU

어떤 페이지를 먼저 버릴지는 LRU(Least Recently Used) 근사로 정한다. OSTEP "Beyond Physical Memory: Policies"의 clock 알고리즘처럼, 진짜 LRU는 비싸니 **reference bit(arm64의 AF)** 로 근사한다.

- **전통적 LRU(2Q 계열)**: 노드×memcg마다(`struct lruvec`) 다섯 리스트 — `inactive_anon`, `active_anon`, `inactive_file`, `active_file`, `unevictable`. 새 페이지는 inactive로 들어가고, inactive에 있는 동안 다시 참조되면 active로 승격된다. reclaim은 inactive 꼬리부터 보고, AF가 서 있으면(최근 사용) 한 번 봐주고 지운다. active가 너무 커지면 꼬리를 inactive로 강등한다. OOM 로그의 `active_anon:514 inactive_anon:49262` 가 이 리스트의 크기다.
- **MGLRU(Multi-Gen LRU, 6.1+, `CONFIG_LRU_GEN`)**: 두 단계 대신 여러 "세대(generation)"로 나이를 매기고, page table을 직접 훑어 AF를 모아 오는 방식으로 스캔 비용을 줄였다. Android·ChromeOS의 메모리 압박 환경에서 효과가 컸고 Ubuntu도 켠다. 예제 1에서 본 것처럼 **랩 커널(defconfig)은 꺼져 있고**(`# CONFIG_LRU_GEN is not set`), 호스트 Ubuntu는 `/sys/kernel/mm/lru_gen/enabled`가 `0x0001`(켜짐)이다. 그래서 이 노트의 reclaim 실험은 전통적 LRU 기준이다.

### 7.4 kswapd·direct reclaim 카운터 읽기

§8의 zram 실험(256M 게스트, zram 256M, text 데이터)에서 hog 실행 전후의 `/proc/vmstat`:

```text
workingset_refault_anon 0 pswpin 0 pswpout 0 allocstall_normal 0 allocstall_movable 0 pgsteal_kswapd 0 pgsteal_direct 0 pgscan_kswapd 0 pgscan_direct 0 pageoutrun 0 oom_kill 0 
workingset_refault_anon 33 pswpin 33 pswpout 65527 allocstall_normal 4 allocstall_movable 13 pgsteal_kswapd 64911 pgsteal_direct 633 pgscan_kswapd 105496 pgscan_direct 44760 pageoutrun 183 oom_kill 1 
```

| 카운터 | 값 | 읽는 법 |
|---|---|---|
| `pgscan_kswapd` / `pgsteal_kswapd` | 105496 / 64911 | kswapd가 LRU에서 10만 페이지를 훑어 6.5만 페이지를 회수 (효율 약 62%) |
| `pgscan_direct` / `pgsteal_direct` | 44760 / 633 | direct reclaim은 4.5만 페이지를 훑고 633개만 건짐 → 거의 헛수고, 막바지 신호 |
| `allocstall_normal` + `allocstall_movable` | 4 + 13 | 할당이 direct reclaim에 빠진 횟수. **지연의 직접 증거** |
| `pageoutrun` | 183 | kswapd가 회수 루프(`balance_pgdat`)를 돈 횟수 |
| `pswpout` / `pswpin` | 65527 / 33 | swap(zram)으로 나간/들어온 페이지. 65527 × 4 KiB = 256 MiB = zram 크기를 꽉 채움 |
| `workingset_refault_anon` | 33 | 내보냈다가 금방 다시 필요해진 익명 페이지 (thrashing 지표) |
| `oom_kill` | 1 | 결국 OOM 한 번 |

실무 팁: 장비 모니터링에 `allocstall_*`과 `pgscan_direct`를 넣어 두면 "OOM 나기 전의 경고"를 받을 수 있다. 더 좋은 지표는 PSI(`/proc/pressure/memory`, `CONFIG_PSI`)인데 랩 커널은 꺼져 있다(`# CONFIG_PSI is not set` 확인). Android의 lmkd와 systemd-oomd는 PSI를 보고 커널 OOM보다 **먼저** 사용자 공간에서 프로세스를 정리한다.

---

## 8. Swap과 임베디드 — 왜 대부분 swap을 끄나, 그리고 zram

### 8.1 swap 파티션이 임베디드에서 환영받지 못하는 이유

Don의 SSD 경력이 바로 답이다.

1. **flash 수명**: swap은 4 KiB 단위의 작고 랜덤한 쓰기를 끝없이 만든다. eMMC/UFS/SD의 NAND는 블록당 P/E(program/erase) 횟수가 정해져 있고(TLC는 수천 회 수준, 저가 SD는 더 나쁠 수 있다), 작은 랜덤 쓰기는 FTL의 write amplification을 키운다. 메모리 압박이 지속되는 장비가 swap에 매일 수 GB를 쓰면 몇 달 만에 저장장치가 수명을 다한다. 컨트롤러가 좋은 엔터프라이즈 SSD와 달리 eMMC는 over-provisioning도 작다.
2. **지연이 예측 불가**: swap-in은 major fault다(§5). eMMC 랜덤 읽기는 수백 µs ~ 수 ms이고, GC 중이면 수십 ms까지 튈 수 있다. 제어 루프가 쓰는 페이지가 swap으로 나가 있으면 데드라인을 놓친다. RT 시스템은 `mlockall()`로 막지만 시스템 전체를 막을 수는 없다.
3. **용량이 작다**: 8 GB eMMC에 rootfs, A/B 업데이트 슬롯, 로그를 넣고 나면 swap 둘 자리가 없다.
4. **보안**: 메모리의 키·인증 정보가 flash에 평문으로 남을 수 있다.

그래서 대부분의 Embedded Linux 장비는 swap을 끄고, **"메모리 예산을 설계 단계에서 맞추고, 넘치면 정해진 순서로 죽인다"** 를 택한다. 그 중간 해법이 zram이다.

### 8.2 zram: RAM을 압축해서 swap으로 쓰기

**zram**은 RAM 안에 압축된 블록 장치(`/dev/zram0`)를 만든다. 여기에 `mkswap`/`swapon`하면 swap out되는 익명 페이지가 **압축되어 RAM에 남는다**. flash를 전혀 건드리지 않고, swap-in은 압축 해제(µs 단위)라 저장장치 major fault보다 훨씬 빠르다. 대가는 CPU 시간과, 압축이 잘 안 되는 데이터에선 이득이 없다는 점이다. Android, ChromeOS, 많은 Yocto 제품이 zram을 쓴다.

비슷한 것으로 **zswap**(swap 앞에 압축 캐시를 두는 것, 뒤에 실제 swap 장치 필요)이 있지만 swap 장치가 없는 임베디드에서는 zram이 일반적이다. 랩 커널은 둘 다 꺼져 있다(`# CONFIG_ZRAM is not set`, `# CONFIG_ZSWAP is not set`).

### 8.3 예제 11: 랩 커널용 zram 모듈 만들기 (실습용 우회)

랩 커널은 `CONFIG_ZRAM`이 꺼져 있고 그 의존성인 zsmalloc(압축 객체 할당기)도 없다. 커널을 다시 빌드하는 대신, 랩 소스에서 `mm/zsmalloc.c`와 `drivers/block/zram/`을 `/var/tmp/F11/zram`으로 복사해 **out-of-tree 모듈로** 빌드했다. 한 가지 손을 댔다: zsmalloc은 `NR_ZSPAGES`라는 vmstat 항목을 갱신하는데, 이 enum 값은 커널이 `CONFIG_ZSMALLOC`으로 빌드됐을 때만 존재한다. 랩 커널엔 없으니 그 두 줄을 주석으로 바꿨다(통계 한 줄이 빠질 뿐 동작은 같다). **이건 학습용 우회이고 제품에서는 커널 config에서 `CONFIG_ZRAM=m`을 켜는 게 정답이다.**

```sh
S=/var/lab/src/linux-6.18.55; D=/var/tmp/F11/zram; mkdir -p $D; cd $D
cp $S/mm/zsmalloc.c $S/mm/zpdesc.h .
cp $S/drivers/block/zram/{zcomp.c,zcomp.h,zram_drv.c,zram_drv.h} $S/drivers/block/zram/backend_*.[ch] .
sed -i "s/^\t\(inc\|dec\)_zone_page_state(zpdesc_page(zpdesc), NR_ZSPAGES);/\t\/* f11: NR_ZSPAGES stat not present in host kernel config *\//" zsmalloc.c
cat Kbuild
make -C /var/lab/out/linux-build M=$PWD -j2 modules 2>&1 | grep -E "warning|error|LD|ERROR"; ls *.ko
```

```text
obj-m := zsmalloc.o zram.o
zram-y := zcomp.o zram_drv.o backend_lzorle.o backend_lzo.o
ccflags-y := -DCONFIG_ZRAM_BACKEND_LZO=1 -DCONFIG_ZRAM_DEF_COMP=\"lzo-rle\" -DCONFIG_ZSMALLOC_CHAIN_SIZE=8
  LD [M]  zram.o
  LD [M]  zsmalloc.ko
  LD [M]  zram.ko
zram.ko
zsmalloc.ko
```

Kconfig 대신 `ccflags-y`로 `CONFIG_ZRAM_BACKEND_LZO`(압축 백엔드 lzo/lzo-rle만), 기본 알고리즘, zspage 체인 길이를 정의했다. LZO 압축 라이브러리는 랩 커널에 이미 내장(`CONFIG_LZO_COMPRESS=y`)이라 따로 필요 없다. (처음엔 `zcomp.c`가 모든 백엔드 헤더를 include해서 `fatal error: backend_lz4.h: No such file or directory`가 났고, 헤더를 다 복사해 해결했다.) 이 모듈은 비디버그 `out/Image`용이다.

### 8.4 예제 12: hog가 OOM 전까지 얼마나 가져가나 — swap 없음 vs zram

무엇을 확인하는 코드인가: `memuser`는 1 MiB씩 익명 메모리를 mmap해서 데이터로 채우며 끝없이 커진다. 데이터는 두 종류다. `text`는 센서 로그 같은 ASCII 줄(압축이 잘 됨, 하지만 페이지 전체가 같은 값은 아님), `rand`는 xorshift 난수(압축 불가). 시작할 때 자기 `oom_score_adj`를 설정한다(§9에서도 같은 프로그램을 쓴다).

```c
// memuser NAME MIB ADJ text|rand : set oom_score_adj, touch MIB MiB (0 = grow forever)
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

static uint64_t s = 88172645463325252ULL;
static uint64_t xr(void) { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }

static void fill(char *p, size_t len, int rnd)
{
	if (rnd) { for (size_t i = 0; i < len; i += 8) *(uint64_t *)(p + i) = xr(); return; }
	size_t off = 0;   /* sensor-log-like text: compresses well but not same-filled */
	while (off + 64 < len)
		off += sprintf(p + off, "imu t=%llu ax=%d ay=%d az=%d\n",
			       (unsigned long long)off, (int)(xr() % 100), -3, 981);
	memset(p + off, '\n', len - off);
}

int main(int argc, char **argv)
{
	if (argc < 5) { fprintf(stderr, "usage: %s NAME MIB ADJ text|rand\n", argv[0]); return 1; }
	long mib = atol(argv[2]);
	int rnd = strcmp(argv[4], "rand") == 0;
	FILE *f = fopen("/proc/self/oom_score_adj", "w");
	if (!f || fprintf(f, "%s", argv[3]) < 0 || fclose(f)) { perror("oom_score_adj"); return 1; }
	for (long i = 1; mib == 0 || i <= mib; i++) {
		char *p = mmap(NULL, 1 << 20, PROT_READ | PROT_WRITE,
			       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (p == MAP_FAILED) { perror("mmap"); return 1; }
		fill(p, 1 << 20, rnd);
		if (i % 16 == 0) { printf("%s: %ld MiB\n", argv[1], i); fflush(stdout); }
	}
	printf("%s: ready (pid %d)\n", argv[1], getpid()); fflush(stdout);
	pause();
	return 0;
}
```

게스트 스크립트(`f11_run.sh <zram 크기|none> <text|rand>`):

```sh
SZ=$1; MODE=$2
echo "### swap=$SZ data=$MODE"
if [ "$SZ" != none ]; then
  insmod /f11/zsmalloc.ko && insmod /f11/zram.ko
  echo lzo-rle > /sys/block/zram0/comp_algorithm
  echo $SZ > /sys/block/zram0/disksize
  mkswap /dev/zram0 > /dev/null && swapon /dev/zram0
fi
cat /proc/swaps
echo "--- zoneinfo (pages):"
awk '/^Node/{z=$4} /^ +pages free/{f=$3} /^ +min /{mn=$2} /^ +low /{lo=$2} /^ +high /{hi=$2} /^ +managed /{print "zone", z, "free", f, "min", mn, "low", lo, "high", hi, "managed", $2}' /proc/zoneinfo
echo "min_free_kbytes=$(cat /proc/sys/vm/min_free_kbytes) watermark_scale_factor=$(cat /proc/sys/vm/watermark_scale_factor) swappiness=$(cat /proc/sys/vm/swappiness)"
V="pgscan_kswapd|pgsteal_kswapd|pgscan_direct|pgsteal_direct|allocstall_normal|allocstall_movable|pswpin|pswpout|oom_kill|workingset_refault_anon|pageoutrun"
grep -wE "$V" /proc/vmstat | tr '\n' ' '; echo
/f11/memuser hog 0 0 $MODE | tail -2
grep -wE "$V" /proc/vmstat | tr '\n' ' '; echo
dmesg | grep "Killed process"
[ "$SZ" != none ] && echo "mm_stat: $(cat /sys/block/zram0/mm_stat)"
```

`-m 256M`, 비디버그 커널로 `256M text` 조합을 돌린 실제 출력:

```text
lab initramfs: 6.18.55 up 1.91s
### swap=256M data=text
Filename				Type		Size		Used		Priority
/dev/zram0                              partition	262140		0		-2
--- zoneinfo (pages):
zone DMA free 48286 min 459 low 573 high 687 managed 54112
zone DMA32 free 0 min 0 low 0 high 0 managed 0
zone Normal free 0 min 0 low 0 high 0 managed 0
zone Movable free 0 min 32 low 32 high 32 managed 0
min_free_kbytes=1839 watermark_scale_factor=10 swappiness=60
workingset_refault_anon 0 pswpin 0 pswpout 0 allocstall_normal 0 allocstall_movable 0 pgsteal_kswapd 0 pgsteal_direct 0 pgscan_kswapd 0 pgscan_direct 0 pageoutrun 0 oom_kill 0 
[   59.040728] Out of memory: Killed process 74 (memuser) total-vm:386880kB, anon-rss:121472kB, file-rss:0kB, shmem-rss:456kB, UID:0 pgtables:784kB oom_score_adj:0
hog: 352 MiB
hog: 368 MiB
Killed
workingset_refault_anon 33 pswpin 33 pswpout 65527 allocstall_normal 4 allocstall_movable 13 pgsteal_kswapd 64911 pgsteal_direct 633 pgscan_kswapd 105496 pgscan_direct 44760 pageoutrun 183 oom_kill 1 
[   59.040728] Out of memory: Killed process 74 (memuser) total-vm:386880kB, anon-rss:121472kB, file-rss:0kB, shmem-rss:456kB, UID:0 pgtables:784kB oom_score_adj:0
mm_stat:   483328   326044  1085440        0 76029952        0        0       39       39
```

출력에서 볼 것:

- `zone DMA ... managed 54112`: QEMU virt의 RAM은 물리 주소 `0x4000_0000`부터라 4 GiB 아래 전부가 arm64의 **ZONE_DMA**에 들어간다. DMA32/Normal은 비어 있다.
- `Killed process ... total-vm:386880kB, anon-rss:121472kB`: hog가 약 378 MiB까지 커졌는데 RAM에 남은 건 119 MiB뿐이고 나머지는 zram에 압축돼 있었다. 게스트 RAM(관리 가능 약 211 MiB)보다 훨씬 많다.
- `mm_stat` 필드(문서 `Documentation/admin-guide/blockdev/zram.rst` 순서): `orig_data_size compr_data_size mem_used_total mem_limit mem_used_max same_pages pages_compacted huge_pages huge_pages_since`. hog가 죽은 뒤라 현재값은 작고, **`mem_used_max` = 76029952 바이트(72.5 MiB)** 가 피크 때 zram이 쓴 RAM이다. 256 MiB(`pswpout` 65527페이지)를 72.5 MiB에 담았으니 압축률 약 3.5배.

같은 스크립트로 여섯 조합을 돌리고 OOM 줄만 모았다.

```sh
for f in z_*.txt; do echo "$f: $(grep -m1 "Killed process" $f | grep -o "total-vm:[0-9]*kB, anon-rss:[0-9]*kB")"; done
```

```text
z_128M_text.txt: total-vm:292952kB, anon-rss:159488kB
z_256M_rand.txt: total-vm:196644kB, anon-rss:35968kB
z_256M_text.txt: total-vm:386880kB, anon-rss:121472kB
z_512M_text.txt: total-vm:570720kB, anon-rss:45440kB
z_none_rand.txt: total-vm:197720kB, anon-rss:195712kB
z_none_text.txt: total-vm:196976kB, anon-rss:193920kB
```

각 실행의 `mm_stat` 다섯째 필드(`mem_used_max`)는 128M text 38547456, 256M text 76029952, 512M text 151003136, 256M rand 164155392 바이트였다. 차트로 그리면:

```svg
<svg viewBox="0 0 680 350" xmlns="http://www.w3.org/2000/svg">
<text x="340" y="22" font-size="13" text-anchor="middle">OOM 직전 hog 크기 (total-vm, MiB) — 256 MiB 게스트, 괄호는 zram 피크 RAM 사용 (MiB)</text>
<text x="140" y="58" font-size="12" text-anchor="end">swap 없음 · text</text>
<rect x="150" y="40" width="128.2" height="26" fill="#888" fill-opacity="0.35" stroke="#888"/>
<text x="284.2" y="58" font-size="12">192 MiB</text>
<text x="140" y="100" font-size="12" text-anchor="end">swap 없음 · rand</text>
<rect x="150" y="82" width="128.7" height="26" fill="#888" fill-opacity="0.35" stroke="#888"/>
<text x="284.7" y="100" font-size="12">193 MiB</text>
<text x="140" y="142" font-size="12" text-anchor="end">zram 128M · text</text>
<rect x="150" y="124" width="190.7" height="26" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="346.7" y="142" font-size="12">286 MiB (zram 36.8)</text>
<text x="140" y="184" font-size="12" text-anchor="end">zram 256M · text</text>
<rect x="150" y="166" width="251.9" height="26" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="407.9" y="184" font-size="12">378 MiB (zram 72.5)</text>
<text x="140" y="226" font-size="12" text-anchor="end">zram 512M · text</text>
<rect x="150" y="208" width="371.6" height="26" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="527.6" y="226" font-size="12">557 MiB (zram 144.0)</text>
<text x="140" y="268" font-size="12" text-anchor="end">zram 256M · rand</text>
<rect x="150" y="250" width="128.0" height="26" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<text x="284.0" y="268" font-size="12">192 MiB (zram 156.6)</text>
<line x1="150" y1="292" x2="550" y2="292" stroke="currentColor"/>
<line x1="150" y1="34" x2="150" y2="292" stroke="currentColor"/>
<line x1="150.0" y1="292" x2="150.0" y2="297" stroke="currentColor"/><text x="150.0" y="312" font-size="12" text-anchor="middle">0</text>
<line x1="216.7" y1="292" x2="216.7" y2="297" stroke="currentColor"/><text x="216.7" y="312" font-size="12" text-anchor="middle">100</text>
<line x1="283.3" y1="292" x2="283.3" y2="297" stroke="currentColor"/><text x="283.3" y="312" font-size="12" text-anchor="middle">200</text>
<line x1="350.0" y1="292" x2="350.0" y2="297" stroke="currentColor"/><text x="350.0" y="312" font-size="12" text-anchor="middle">300</text>
<line x1="416.7" y1="292" x2="416.7" y2="297" stroke="currentColor"/><text x="416.7" y="312" font-size="12" text-anchor="middle">400</text>
<line x1="483.3" y1="292" x2="483.3" y2="297" stroke="currentColor"/><text x="483.3" y="312" font-size="12" text-anchor="middle">500</text>
<line x1="550.0" y1="292" x2="550.0" y2="297" stroke="currentColor"/><text x="550.0" y="312" font-size="12" text-anchor="middle">600</text>
<text x="350" y="338" font-size="12" text-anchor="middle">회색 = swap 없음 · 파랑 = zram + 압축 잘 되는 데이터 · 주황 = zram + 난수 데이터</text>
</svg>
```

그림 7 — 직접 측정한 값. 압축되는 데이터에선 zram 크기만큼 거의 그대로 더 쓸 수 있고(192 → 286 → 378 → 557 MiB), 압축 안 되는 데이터에선 zram이 RAM을 1:1 가까이 먹어서 이득이 0이다.

결론과 실무 해석:

- **압축되는 데이터라면 zram은 거의 공짜 RAM 증설**이다. 512M zram이면 256 MiB 장비에서 2.9배를 썼다. 대가로 zram이 RAM 144 MiB를 썼지만 그 안에 512 MiB가 들어 있었다.
- **난수·암호문·이미 압축된 데이터(JPEG, H.264 프레임)** 에는 zram이 쓸모없다. 오히려 swap out/in에 CPU만 쓴다. 카메라 장비라면 "프레임 버퍼는 zram에 안 간다"고 생각해야 한다(대부분 DMA 버퍼라 애초에 swap 대상이 아니기도 하다).
- **disksize는 "압축 전 용량" 한도**다. text 데이터로 128M zram은 RAM 37 MiB만 쓰고도 꽉 찼다. 흔한 설정은 RAM의 50% ~ 100%이고, `mem_limit`로 zram이 쓸 RAM 상한을 따로 걸 수 있다.
- **`swappiness`**: 0~200. zram처럼 swap이 싸면 높게(Android는 100 이상) 둬서 파일 캐시(실행 코드) 대신 익명 메모리를 zram으로 보내는 편이 thrashing을 줄인다.

### 8.5 흔한 함정

- **SD 카드에 swap 파일**: 라즈베리 파이 계열 장비에서 흔하다. 수명이 짧아지고, swap-in이 수십 ms까지 튄다. zram으로 바꾸거나 메모리 예산을 다시 짠다.
- **zram 압축률을 데이터 없이 가정**: 위 실험처럼 데이터에 따라 3.5배에서 1배까지 바뀐다. 실제 워크로드로 `mm_stat`을 재야 한다.
- **zram이 있으니 OOM은 안 난다고 생각**: zram도 꽉 차면(`pswpout`이 disksize에 도달) 결국 OOM이다. 오히려 그 직전까지 압축·해제로 CPU를 태우며 느려지는 구간이 길어진다.

---

## 9. OOM killer

### 9.1 언제 불리나

페이지 할당기의 느린 경로(`__alloc_pages_slowpath`)가 kswapd를 깨우고, direct reclaim(`__alloc_pages_direct_reclaim`)과 compaction을 여러 번 재시도해도 실패하면 `__alloc_pages_may_oom()` → `out_of_memory()`가 불린다. 여기서 `select_bad_process()`가 모든 프로세스의 `oom_badness()`를 계산해 가장 큰 것을 고르고, `oom_kill_process()`가 SIGKILL을 보낸다. 죽은 프로세스가 메모리를 빨리 돌려주도록 `oom_reaper` 커널 스레드가 그 프로세스의 익명 메모리를 미리 해제하기도 한다.

### 9.2 누구를 고르나 — `oom_badness()`

랩 소스 `mm/oom_kill.c`에서 그대로:

```sh
grep -n "^long oom_badness" -A40 /var/lab/src/linux-6.18.55/mm/oom_kill.c | sed -n 18,39p
```

```text
219-	adj = (long)p->signal->oom_score_adj;
220-	if (adj == OOM_SCORE_ADJ_MIN ||
221-			mm_flags_test(MMF_OOM_SKIP, p->mm) ||
222-			in_vfork(p)) {
223-		task_unlock(p);
224-		return LONG_MIN;
225-	}
226-
227-	/*
228-	 * The baseline for the badness score is the proportion of RAM that each
229-	 * task's rss, pagetable and swap space use.
230-	 */
231-	points = get_mm_rss(p->mm) + get_mm_counter(p->mm, MM_SWAPENTS) +
232-		mm_pgtables_bytes(p->mm) / PAGE_SIZE;
233-	task_unlock(p);
234-
235-	/* Normalize to oom_score_adj units */
236-	adj *= totalpages / 1000;
237-	points += adj;
238-
239-	return points;
240-}
```

정리하면 **badness = RSS 페이지 + swap 엔트리 + page table 페이지 + `oom_score_adj` × (총 페이지/1000)**. `oom_score_adj = -1000`(`OOM_SCORE_ADJ_MIN`)이면 `LONG_MIN`을 돌려 **후보에서 아예 빠진다**. "오래 돈 프로세스 우대", "root 우대" 같은 옛 규칙은 지금은 없다. 메모리를 많이 쓰는 프로세스가 죽는다. 그리고 swap(zram)에 나가 있는 페이지(`MM_SWAPENTS`)도 점수에 들어간다.

유저에게 보이는 `/proc/<pid>/oom_score`는 `fs/proc/base.c`의 `proc_oom_score()`가 `(1000 + badness × 1000 / totalpages) × 2 / 3`으로 0~2000 범위를 0~1333 근처로 바꾼 값이다. §9.4 실험의 uploader로 검산하면: RSS 7794페이지 + page table 98304바이트/4096 = 24페이지 → badness 7818, totalpages = RAM 54112(swap 없음) → 7818 × 1000 / 54112 = 144 → (1000 + 144) × 2/3 = **762**. 출력의 `oom_score=762`와 같다.

### 9.3 예제 13: 누가 죽나 — `oom_score_adj` 없을 때와 있을 때

무엇을 확인하는 실험인가: 256 MiB 게스트에서 "센서 데몬"(sensord, 90 MiB), "로그 업로더"(uploader, 30 MiB)를 띄운 뒤 끝없이 커지는 hog를 돌린다. `plain`은 셋 다 `oom_score_adj=0`, `protect`는 sensord만 `-1000`.

```sh
M=/f11/memuser
echo "### $1"; grep -E "MemTotal|MemAvailable" /proc/meminfo; cat /proc/swaps
[ "$1" = protect ] && SADJ=-1000 || SADJ=0
$M sensord 90 $SADJ text & sleep 3
$M uploader 30 0 text & sleep 2
for p in $(pidof memuser); do echo "pid $p $(tr '\0' ' ' < /proc/$p/cmdline) oom_score=$(cat /proc/$p/oom_score) adj=$(cat /proc/$p/oom_score_adj)"; done
$M hog 0 0 text
echo "hog exit status=$?"; sleep 1
echo "survivors: $(for p in $(pidof memuser); do tr '\0' ' ' < /proc/$p/cmdline; echo -n '/ '; done)"
dmesg | grep -E "invoked oom-killer|Out of memory|oom_reaper|Killed process|oom-kill:"
```

`plain` 실제 출력:

```text
lab initramfs: 6.18.55 up 2.01s
### plain
MemTotal:         216448 kB
MemAvailable:     192320 kB
Filename				Type		Size		Used		Priority
sensord: 16 MiB
sensord: 32 MiB
pid 67 /f11/memuser uploader 30 0 text  oom_score=697 adj=0
pid 65 /f11/memuser sensord 90 0 text  oom_score=774 adj=0
uploader: 16 MiB
sensord: 48 MiB
hog: 16 MiB
uploader: ready (pid 67)
sensord: 64 MiB
hog: 32 MiB
sensord: 80 MiB
hog: 48 MiB
sensord: ready (pid 65)
hog: 64 MiB
[   28.693245] Out of memory: Killed process 65 (memuser) total-vm:94084kB, anon-rss:92160kB, file-rss:0kB, shmem-rss:456kB, UID:0 pgtables:216kB oom_score_adj:0
hog: 80 MiB
hog: 96 MiB
hog: 112 MiB
hog: 128 MiB
hog: 144 MiB
hog: 160 MiB
[   41.940773] Out of memory: Killed process 76 (memuser) total-vm:169548kB, anon-rss:165888kB, file-rss:0kB, shmem-rss:456kB, UID:0 pgtables:360kB oom_score_adj:0
Killed
hog exit status=137
survivors: /f11/memuser uploader 30 0 text / 
[   28.675091] memuser invoked oom-killer: gfp_mask=0x140dca(GFP_HIGHUSER_MOVABLE|__GFP_ZERO|__GFP_COMP), order=0, oom_score_adj=0
[   28.688314] oom-kill:constraint=CONSTRAINT_NONE,nodemask=(null),cpuset=/,mems_allowed=0,global_oom,task_memcg=/,task=memuser,pid=65,uid=0
[   28.693245] Out of memory: Killed process 65 (memuser) total-vm:94084kB, anon-rss:92160kB, file-rss:0kB, shmem-rss:456kB, UID:0 pgtables:216kB oom_score_adj:0
[   41.939062] memuser invoked oom-killer: gfp_mask=0x440dc0(GFP_KERNEL_ACCOUNT|__GFP_ZERO|__GFP_COMP), order=0, oom_score_adj=0
[   41.940498] oom-kill:constraint=CONSTRAINT_NONE,nodemask=(null),cpuset=/,mems_allowed=0,global_oom,task_memcg=/,task=memuser,pid=76,uid=0
[   41.940773] Out of memory: Killed process 76 (memuser) total-vm:169548kB, anon-rss:165888kB, file-rss:0kB, shmem-rss:456kB, UID:0 pgtables:360kB oom_score_adj:0
```

`protect` 실제 출력:

```text
lab initramfs: 6.18.55 up 1.81s
### protect
MemTotal:         216448 kB
MemAvailable:     192260 kB
Filename				Type		Size		Used		Priority
sensord: 16 MiB
pid 67 /f11/memuser uploader 30 0 text  oom_score=691 adj=0
pid 65 /f11/memuser sensord 90 -1000 text  oom_score=0 adj=-1000
sensord: 32 MiB
uploader: 16 MiB
hog: 16 MiB
sensord: 48 MiB
uploader: ready (pid 67)
hog: 32 MiB
sensord: 64 MiB
hog: 48 MiB
sensord: 80 MiB
hog: 64 MiB
sensord: ready (pid 65)
[   30.836364] Out of memory: Killed process 76 (memuser) total-vm:73708kB, anon-rss:72448kB, file-rss:0kB, shmem-rss:456kB, UID:0 pgtables:180kB oom_score_adj:0
Killed
hog exit status=137
survivors: /f11/memuser uploader 30 0 text / /f11/memuser sensord 90 -1000 text / 
[   30.829153] memuser invoked oom-killer: gfp_mask=0x140dca(GFP_HIGHUSER_MOVABLE|__GFP_ZERO|__GFP_COMP), order=0, oom_score_adj=0
[   30.834732] oom-kill:constraint=CONSTRAINT_NONE,nodemask=(null),cpuset=/,mems_allowed=0,global_oom,task_memcg=/,task=memuser,pid=76,uid=0
[   30.836364] Out of memory: Killed process 76 (memuser) total-vm:73708kB, anon-rss:72448kB, file-rss:0kB, shmem-rss:456kB, UID:0 pgtables:180kB oom_score_adj:0
```

출력에서 볼 것:

- **plain**: TCG가 느려서 sensord가 아직 커지는 중에 hog가 출발했다. 첫 OOM(28.69초) 때 가장 큰 것은 90 MiB의 **sensord**(pid 65)였고 그게 죽었다. 센서 데몬이 죽은 장비 — 바로 현장에서 보는 사고다. hog는 그 메모리를 먹고 160 MiB까지 더 크다가 두 번째 OOM으로 죽었다. 최종 생존자는 uploader뿐.
- **protect**: sensord의 `oom_score`가 0(후보 제외)이다. OOM은 hog(pid 76, 72 MiB)를 골랐고 sensord와 uploader 둘 다 살았다.
- `exit status=137` = 128 + 9(SIGKILL). 셸의 `Killed`도 같은 뜻이다. OOM kill은 SIGKILL이라 프로세스가 막거나 정리할 수 없다. 데몬은 **죽을 수 있다고 가정**하고 systemd `Restart=`로 되살리는 설계가 필요하다.

### 9.4 예제 14: OOM 리포트 한 줄씩 읽기

sensord(60 MiB, adj -1000)와 uploader(30 MiB)를 먼저 다 채운 뒤(25초 대기) hog를 돌려서 OOM 리포트 전체를 얻었다. 스크립트 핵심은 `dmesg | sed -n '/invoked oom-killer/,/oom_reaper/p'`다.

```text
[   35.666009] memuser invoked oom-killer: gfp_mask=0x140dca(GFP_HIGHUSER_MOVABLE|__GFP_ZERO|__GFP_COMP), order=0, oom_score_adj=0
[   35.666547] CPU: 0 UID: 0 PID: 81 Comm: memuser Not tainted 6.18.55 #1 PREEMPT 
[   35.666718] Hardware name: linux,dummy-virt (DT)
[   35.666888] Call trace:
[   35.667047]  show_stack+0x28/0x7c (C)
[   35.667585]  dump_stack_lvl+0x60/0x80
[   35.667628]  dump_stack+0x18/0x24
[   35.667640]  dump_header+0x40/0x168
[   35.667651]  oom_kill_process+0x294/0x310
[   35.667675]  out_of_memory+0x21c/0x580
[   35.667687]  __alloc_frozen_pages_noprof+0xcc0/0xd90
[   35.667717]  alloc_pages_mpol+0xb4/0x1a4
[   35.667735]  folio_alloc_mpol_noprof+0x14/0x68
[   35.667748]  vma_alloc_folio_noprof+0x80/0xd0
[   35.667761]  vma_alloc_zeroed_movable_folio+0x34/0x40
[   35.667775]  __handle_mm_fault+0xdb8/0x1880
[   35.667788]  handle_mm_fault+0x88/0x29c
[   35.667800]  do_page_fault+0x108/0x6e0
[   35.667812]  do_translation_fault+0x5c/0x6c
[   35.667824]  do_mem_abort+0x40/0x98
[   35.667836]  el0_da+0x30/0xb8
[   35.667871]  el0t_64_sync_handler+0xd0/0xe4
[   35.667882]  el0t_64_sync+0x198/0x19c
[   35.668274] Mem-Info:
[   35.668938] active_anon:514 inactive_anon:49262 isolated_anon:0
[   35.668938]  active_file:0 inactive_file:0 isolated_file:0
[   35.668938]  unevictable:0 dirty:0 writeback:0
[   35.668938]  slab_reclaimable:231 slab_unreclaimable:1479
[   35.668938]  mapped:537 shmem:741 pagetables:159
[   35.668938]  sec_pagetables:0 bounce:0
[   35.668938]  kernel_misc_reclaimable:0
[   35.668938]  free:268 free_pcp:1018 free_cma:1
[   35.669085] Node 0 active_anon:2056kB inactive_anon:197048kB active_file:0kB inactive_file:0kB unevictable:0kB isolated(anon):0kB isolated(file):0kB mapped:2148kB dirty:0kB writeback:0kB shmem:2964kB shmem_thp:0kB shmem_pmdmapped:0kB anon_thp:0kB kernel_stack:832kB pagetables:636kB sec_pagetables:0kB all_unreclaimable? yes Balloon:0kB
[   35.669186] Node 0 DMA free:1072kB boost:0kB min:1840kB low:2300kB high:2760kB reserved_highatomic:0KB free_highatomic:0KB active_anon:2056kB inactive_anon:197048kB active_file:0kB inactive_file:0kB unevictable:0kB writepending:0kB zspages:0kB present:262144kB managed:216448kB mlocked:0kB bounce:0kB free_pcp:4072kB local_pcp:4072kB free_cma:4kB
[   35.669724] lowmem_reserve[]: 0 0 0 0
[   35.669872] Node 0 DMA: 0*4kB 1*8kB (U) 2*16kB (UM) 2*32kB (U) 1*64kB (M) 1*128kB (M) 1*256kB (M) 1*512kB (M) 0*1024kB 0*2048kB 0*4096kB = 1064kB
[   35.670123] Node 0 hugepages_total=0 hugepages_free=0 hugepages_surp=0 hugepages_size=1048576kB
[   35.670154] Node 0 hugepages_total=0 hugepages_free=0 hugepages_surp=0 hugepages_size=32768kB
[   35.670162] Node 0 hugepages_total=0 hugepages_free=0 hugepages_surp=0 hugepages_size=2048kB
[   35.670168] Node 0 hugepages_total=0 hugepages_free=0 hugepages_surp=0 hugepages_size=64kB
[   35.670207] 741 total pagecache pages
[   35.670222] 0 pages in swap cache
[   35.670231] Free swap  = 0kB
[   35.670239] Total swap = 0kB
[   35.670285] 65536 pages RAM
[   35.670292] 0 pages HighMem/MovableOnly
[   35.670298] 11424 pages reserved
[   35.670306] 8192 pages cma reserved
[   35.670313] 0 pages hwpoisoned
[   35.670352] Tasks state (memory values in pages):
[   35.670360] [  pid  ]   uid  tgid total_vm      rss rss_anon rss_file rss_shmem pgtables_bytes swapents oom_score_adj name
[   35.670490] [     62]     0    62      655      362        0        0       362    40960        0             0 sh
[   35.670520] [     63]     0    63    15714    15474    15360        0       114   159744        0         -1000 memuser
[   35.670534] [     64]     0    64     8377     7794     7680        0       114    98304        0             0 memuser
[   35.670546] [     81]     0    81    26509    26066    25952        0       114   245760        0             0 memuser
[   35.670577] oom-kill:constraint=CONSTRAINT_NONE,nodemask=(null),cpuset=/,mems_allowed=0,global_oom,task_memcg=/,task=memuser,pid=81,uid=0
[   35.671569] Out of memory: Killed process 81 (memuser) total-vm:106036kB, anon-rss:103808kB, file-rss:0kB, shmem-rss:456kB, UID:0 pgtables:240kB oom_score_adj:0
```

| 줄 | 해석 |
|---|---|
| `memuser invoked oom-killer: gfp_mask=0x140dca(GFP_HIGHUSER_MOVABLE...), order=0` | **OOM을 일으킨 쪽**(희생자가 아닐 수 있다)과 실패한 할당. 유저 페이지(HIGHUSER_MOVABLE), 0으로 채움, order 0 = 4 KiB 한 장. order가 크면(예: 3 이상) 메모리 총량이 아니라 **단편화**가 원인일 수 있다 |
| `CPU: 0 ... Comm: memuser` + `Call trace` | 할당하던 경로. 여기선 §5의 page fault 경로 그대로(`el0_da → do_mem_abort → ... → vma_alloc_zeroed_movable_folio`). 드라이버 버그면 여기에 드라이버 함수가 보인다 |
| `active_anon:514 inactive_anon:49262 ... active_file:0 inactive_file:0` | LRU 리스트 크기(페이지). **파일 페이지가 0** = 버릴 캐시가 하나도 안 남았다. 익명이 거의 전부 |
| `all_unreclaimable? yes` | 노드 전체에서 더 회수할 게 없다고 판정됨 |
| `Node 0 DMA free:1072kB ... min:1840kB low:2300kB high:2760kB` | 빈 메모리가 **min 아래**. §7 그림 6의 맨 아래 칸 |
| `Node 0 DMA: 0*4kB 1*8kB (U) 2*16kB ...` | 버디 할당기의 order별 빈 블록 수. 큰 order가 0이면 단편화 상태 |
| `Free swap = 0kB` / `Total swap = 0kB` | swap 없음 → 익명 메모리는 회수 불가가 확정 |
| `65536 pages RAM` / `11424 pages reserved` / `8192 pages cma reserved` | 256 MiB 중 커널 이미지·memmap 등 예약과 CMA 32 MiB(F10). CMA는 movable 할당에만 빌려줄 수 있어 여유가 더 적다 |
| `Tasks state (memory values in pages)` 표 | 후보들의 `total_vm`, `rss`(= anon + file + shmem), `pgtables_bytes`, `swapents`, `oom_score_adj`. pid 63(sensord)은 rss 15474로 둘째로 크지만 adj -1000이라 후보 제외 |
| `oom-kill:constraint=CONSTRAINT_NONE ... global_oom, task_memcg=/` | 시스템 전체 OOM(cgroup 한도 때문이 아님). §10의 memcg OOM은 여기가 `CONSTRAINT_MEMCG`로 바뀐다 |
| `Out of memory: Killed process 81 (memuser) total-vm:106036kB, anon-rss:103808kB ...` | 희생자. 이번엔 OOM을 일으킨 hog 자신이 가장 컸다 |

### 9.5 OOM 정책 손잡이

| 손잡이 | 어디 | 효과 |
|---|---|---|
| `oom_score_adj` | `/proc/<pid>/oom_score_adj`, systemd `OOMScoreAdjust=` | -1000(면제) ~ +1000(1순위). fork로 상속된다 |
| `vm.panic_on_oom` | `/proc/sys/vm/panic_on_oom` | 0(기본: 하나 죽임), 1(전역 OOM이면 panic, memcg OOM은 제외), 2(무조건 panic). panic + watchdog 재부팅을 "알려진 상태로 복귀"로 쓰는 장비가 있다 |
| `vm.oom_kill_allocating_task` | `/proc/sys/vm/` | 1이면 점수 계산 없이 할당한 태스크를 죽인다 (프로세스 수천 개일 때 계산 비용 절감) |
| `memory.oom.group` | cgroup v2 | 1이면 그 cgroup을 **통째로** 죽인다 (반쯤 죽은 서비스 방지) |
| `memory.oom_control`의 `oom_kill_disable` | **cgroup v1 전용** | v1에서 그 cgroup의 OOM kill을 끄고 태스크를 멈춰 세운다. 랩 커널은 `MEMCG_V1`이 꺼져 있어 파일 자체가 없다 (§10 출력의 파일 목록 참고) |
| 사용자 공간 OOM 데몬 | systemd-oomd, Android lmkd, earlyoom | PSI·free 메모리를 보고 커널 OOM 전에 정책대로 정리 |

### 9.6 흔한 함정

- **`oom_score_adj=-1000`을 남발**: 모두가 면제되면 OOM killer는 고를 게 없어 panic하거나(`Out of memory and no killable processes...`) 엉뚱한 작은 프로세스만 죽인다. 진짜 핵심 1~2개만 면제하고 나머지는 계층을 둔다.
- **면제된 프로세스가 누수**: -1000 데몬이 메모리를 새면 OOM은 다른 프로세스를 하나씩 다 죽인다. 면제 데몬엔 반드시 memcg 한도(§10)를 같이 건다.
- **"OOM을 일으킨 프로세스 = 범인"**: 리포트 첫 줄의 `invoked oom-killer`는 마지막 한 페이지를 요청한 운 나쁜 프로세스일 뿐이다. 범인은 Tasks state 표에서 rss가 큰 쪽이다.

---

## 10. Memory cgroup으로 서비스 한도 걸기

### 10.1 직관: 프로세스 묶음에 RAM 예산 주기

`oom_score_adj`는 "누가 먼저 죽나"만 정한다. **"이 서비스는 최대 32 MiB"** 처럼 한도를 거는 건 memory cgroup(memcg)이다. 한도를 넘으면 먼저 **그 cgroup 안에서만** reclaim하고, 그래도 안 되면 **그 cgroup 안에서만** OOM kill한다. 시스템의 나머지(센서 데몬)는 영향을 받지 않는다. RTOS에서 태스크마다 고정 크기 메모리 풀을 주던 것의 Linux 판이다.

cgroup v2의 주요 파일:

| 파일 | 뜻 |
|---|---|
| `memory.max` | 하드 한도. 넘으면 cgroup 내 reclaim → 실패 시 cgroup 내 OOM kill |
| `memory.high` | 소프트 한도. 넘으면 강하게 reclaim하고 할당을 **느리게(throttle)** 한다. 죽이지는 않는다 |
| `memory.low` / `memory.min` | 보호. 이만큼은 다른 cgroup 때문에 회수당하지 않게 (min은 절대, low는 최선) |
| `memory.swap.max` | 이 cgroup이 쓸 수 있는 swap(zram) 한도 |
| `memory.events` | `low`, `high`, `max`, `oom`, `oom_kill` 발생 횟수 |
| `memory.peak` | 지금까지 최대 사용량 |

### 10.2 예제 15: uploader를 32 MiB에 가두기

§9.4와 같은 부팅에서 이어서 실행했다(sensord와 uploader는 계속 살아 있음). 새 uploader를 `f11_uploader` cgroup에 넣고 끝없이 키운다.

```sh
mount -t cgroup2 none /sys/fs/cgroup
echo +memory > /sys/fs/cgroup/cgroup.subtree_control
mkdir /sys/fs/cgroup/f11_uploader
ls /sys/fs/cgroup/f11_uploader | grep -E "^memory\." | tr '\n' ' '; echo
echo 32M > /sys/fs/cgroup/f11_uploader/memory.max
dmesg -c > /dev/null
sh -c 'echo $$ > /sys/fs/cgroup/f11_uploader/cgroup.procs; exec /f11/memuser uploader 0 0 text'
echo "exit status=$?"
echo "memory.events: $(cat /sys/fs/cgroup/f11_uploader/memory.events | tr '\n' ' ')"
echo "memory.peak: $(cat /sys/fs/cgroup/f11_uploader/memory.peak)"
dmesg | grep -E "invoked oom-killer|Memory cgroup out of memory|oom-kill:|Killed process|memory: usage|^\[ *[0-9.]*\] (anon|file) [0-9]"
for p in $(pidof memuser); do echo "still alive: $p $(tr '\0' ' ' < /proc/$p/cmdline)"; done
```

```text
memory.current memory.events memory.events.local memory.high memory.low memory.max memory.min memory.numa_stat memory.oom.group memory.peak memory.reclaim memory.stat memory.swap.current memory.swap.events memory.swap.high memory.swap.max memory.swap.peak 
uploader: 16 MiB
[   39.540154] Memory cgroup out of memory: Killed process 91 (memuser) total-vm:34652kB, anon-rss:32640kB, file-rss:0kB, shmem-rss:456kB, UID:0 pgtables:100kB oom_score_adj:0
Killed
exit status=137
memory.events: low 0 high 0 max 35 oom 1 oom_kill 1 oom_group_kill 0 
memory.peak: 33554432
[   39.538441] memuser invoked oom-killer: gfp_mask=0xcc0(GFP_KERNEL), order=0, oom_score_adj=0
[   39.538815] memory: usage 32768kB, limit 32768kB, failcnt 18
[   39.539665] anon 33439744
[   39.539718] file 0
[   39.540014] oom-kill:constraint=CONSTRAINT_MEMCG,nodemask=(null),cpuset=/,mems_allowed=0,oom_memcg=/f11_uploader,task_memcg=/f11_uploader,task=memuser,pid=91,uid=0
[   39.540154] Memory cgroup out of memory: Killed process 91 (memuser) total-vm:34652kB, anon-rss:32640kB, file-rss:0kB, shmem-rss:456kB, UID:0 pgtables:100kB oom_score_adj:0
still alive: 64 /f11/memuser uploader 30 0 text 
still alive: 63 /f11/memuser sensord 60 -1000 text 
```

출력에서 볼 것:

- 파일 목록에 `memory.oom_control`(v1의 `oom_kill_disable`)이 **없다**. v2는 "OOM을 끄고 멈춰 세우기" 대신 `memory.high` throttle과 `memory.oom.group`을 준다.
- uploader가 32 MiB를 넘자 `CONSTRAINT_MEMCG, oom_memcg=/f11_uploader`로 **그 cgroup 안에서만** OOM이 났다. 커널 메시지도 `Memory cgroup out of memory:`로 시작해 전역 OOM과 구별된다.
- `memory.events`: `max 35`(한도에 35번 부딪힘) → `oom 1` → `oom_kill 1`. `memory.peak`가 정확히 33554432(32 MiB).
- 마지막 두 줄: 밖에 있던 sensord와 uploader(30 MiB짜리)는 **멀쩡히 살아 있다**.

실제 장비에서는 cgroup을 손으로 만들지 않고 systemd 유닛에 쓴다(이 VM에서 실행하지 않은 예시 설정이다).

```text
# /etc/systemd/system/log-uploader.service (예시)
[Service]
ExecStart=/usr/bin/log-uploader
MemoryHigh=24M
MemoryMax=32M
OOMScoreAdjust=500
Restart=on-failure
```

---

## 11. 커널 안에서는 무슨 일이 일어나나

reclaim의 중심 객체는 **folio**(하나 이상의 연속 페이지를 묶은 단위, 6.x에서 `struct page`를 대체 중)다. folio 하나에 세 가지 연결이 붙는다.

1. **LRU**: 어느 `lruvec`(memcg × 노드)의 어느 리스트에 있나 → reclaim이 "누구를 버릴지" 고를 때
2. **rmap(역방향 매핑)**: 익명이면 `anon_vma`, 파일이면 `address_space->i_mmap`을 통해 "이 folio를 매핑한 VMA·PTE들" → 버리기 전에 `try_to_unmap()`으로 모든 PTE를 지우고 TLB flush
3. **swap**: 익명 folio를 내보내면 PTE 자리에 **swap entry**(swap 장치 번호 + 오프셋, present 비트 없음)가 들어간다 → 다음 접근 때 `do_swap_page`

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah8" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="20" y="20" width="200" height="56" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="120" y="43" font-size="13" text-anchor="middle">kswapd / direct reclaim</text><text x="120" y="63" font-size="12" text-anchor="middle">balance_pgdat, try_to_free_pages</text>
<rect x="20" y="110" width="200" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="120" y="135" font-size="12" text-anchor="middle">shrink_node → shrink_lruvec</text>
<rect x="20" y="180" width="200" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="120" y="205" font-size="12" text-anchor="middle">shrink_folio_list</text>
<rect x="20" y="250" width="200" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="120" y="275" font-size="12" text-anchor="middle">shrink_slab → 드라이버 shrinker</text>
<line x1="120" y1="76" x2="120" y2="108" stroke="currentColor" marker-end="url(#ah8)"/>
<line x1="120" y1="150" x2="120" y2="178" stroke="currentColor" marker-end="url(#ah8)"/>
<line x1="60" y1="150" x2="60" y2="248" stroke="currentColor" marker-end="url(#ah8)"/>
<rect x="270" y="20" width="390" height="56" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="465" y="43" font-size="13" text-anchor="middle">lruvec (memcg x 노드)</text><text x="465" y="63" font-size="12" text-anchor="middle">inactive/active x anon/file + unevictable</text>
<line x1="220" y1="130" x2="268" y2="60" stroke="currentColor" marker-end="url(#ah8)"/>
<rect x="340" y="160" width="160" height="50" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="420" y="183" font-size="13" text-anchor="middle">folio</text><text x="420" y="201" font-size="12" text-anchor="middle">회수 후보</text>
<line x1="420" y1="76" x2="420" y2="158" stroke="currentColor" marker-end="url(#ah8)"/>
<line x1="220" y1="200" x2="338" y2="190" stroke="currentColor" marker-end="url(#ah8)"/>
<rect x="530" y="110" width="135" height="50" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="597" y="132" font-size="12" text-anchor="middle">rmap</text><text x="597" y="150" font-size="12" text-anchor="middle">try_to_unmap</text>
<line x1="500" y1="175" x2="528" y2="145" stroke="currentColor" marker-end="url(#ah8)"/>
<rect x="530" y="200" width="135" height="50" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="597" y="222" font-size="12" text-anchor="middle">PTE들 지움</text><text x="597" y="240" font-size="12" text-anchor="middle">+ TLB flush</text>
<line x1="597" y1="160" x2="597" y2="198" stroke="currentColor" marker-end="url(#ah8)"/>
<rect x="270" y="270" width="120" height="56" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="330" y="293" font-size="12" text-anchor="middle">clean 파일</text><text x="330" y="312" font-size="12" text-anchor="middle">그냥 해제</text>
<rect x="405" y="270" width="120" height="56" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="465" y="293" font-size="12" text-anchor="middle">dirty 파일</text><text x="465" y="312" font-size="12" text-anchor="middle">pageout 후</text>
<rect x="540" y="270" width="125" height="56" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="602" y="293" font-size="12" text-anchor="middle">익명 → swap</text><text x="602" y="312" font-size="12" text-anchor="middle">PTE = swap entry</text>
<line x1="400" y1="210" x2="335" y2="268" stroke="currentColor" marker-end="url(#ah8)"/>
<line x1="430" y1="210" x2="465" y2="268" stroke="currentColor" marker-end="url(#ah8)"/>
<line x1="597" y1="250" x2="600" y2="268" stroke="currentColor" marker-end="url(#ah8)"/>
<text x="340" y="365" font-size="12" text-anchor="middle">회수 실패가 반복되면 __alloc_pages_may_oom → out_of_memory → select_bad_process → oom_kill_process</text>
<text x="340" y="388" font-size="12" text-anchor="middle">(함수 이름은 랩 소스 6.18.55 mm/vmscan.c, mm/rmap.c, mm/oom_kill.c, mm/page_alloc.c에서 확인)</text>
</svg>
```

그림 8 — 커널 reclaim 경로. LRU에서 folio를 고르고, rmap으로 그 folio를 매핑한 모든 PTE를 찾아 지운 뒤(§3의 TLB flush), 종류에 따라 버리거나 저장하거나 swap으로 보낸다.

유저가 보는 것과 커널 객체, 드라이버에서 다시 만나는 곳:

| 유저/관측 | 커널 객체·함수 | 드라이버에서 다시 만나는 곳 |
|---|---|---|
| page fault | `do_mem_abort` → `do_page_fault` → `handle_mm_fault` → `handle_pte_fault` | `vm_operations_struct.fault` / `.map_pages` (G5, H9) |
| `/proc/<pid>/maps` | `mm_struct.mm_mt`(maple tree)의 `vm_area_struct` | `.mmap(file, vma)`에서 `vm_flags_set`, `remap_pfn_range` (G5) |
| TLB 일관성 | `flush_tlb_page/range/mm`, `ptep_clear_flush` | `zap_vma_ptes`, `unmap_mapping_range`로 장치 매핑 회수 |
| DMA 버퍼에 유저 메모리 사용 | `pin_user_pages()` (FOLL_PIN, reclaim·migration에서 제외) | 유저 버퍼 zero-copy DMA (H 모듈) |
| 메모리 부족 시 지연 | `__alloc_pages_slowpath` → direct reclaim | `GFP_KERNEL` 할당은 **reclaim으로 잠들 수 있다** → atomic 컨텍스트에선 `GFP_ATOMIC` (F2, F9) |
| 드라이버 캐시 메모리 | `shrink_slab` → shrinker | `shrinker_alloc()` + `shrinker_register()` (6.7+ API)로 메모리 압박 때 캐시 반납 |
| OOM | `out_of_memory`, `oom_badness` | 드라이버가 커널 메모리를 새면 OOM 리포트의 `slab_unreclaimable`이 크게 보인다 (M 모듈) |
| 버퍼를 swap에서 제외 | `VM_LOCKED`, `VM_IO`, `VM_PFNMAP` | 장치 메모리 매핑(`remap_pfn_range`)은 `VM_PFNMAP`이라 reclaim 대상이 아니다 |

드라이버 관점 핵심 세 가지:

1. **`GFP_KERNEL`은 "잠들 수 있다"의 다른 이름**이다. 메모리가 모자라면 그 할당 안에서 direct reclaim(writeback I/O 대기 포함)이 돈다. 그래서 spinlock 안, IRQ 핸들러에서는 `GFP_ATOMIC`(reclaim 안 함, min 아래 예비분 사용)만 쓴다. reclaim 경로가 다시 파일시스템/블록 I/O로 들어가는 재귀를 막으려면 `GFP_NOFS`/`GFP_NOIO`(또는 `memalloc_noio_save()`)를 쓴다.
2. **유저 페이지를 오래 쥘 땐 pin**: DMA 중에 reclaim이나 migration이 페이지를 옮기면 장치가 엉뚱한 메모리에 쓴다. `get_user_pages`보다 `pin_user_pages`가 DMA 용도의 정식 API다.
3. **큰 캐시를 들고 있는 드라이버는 shrinker로 협조**: 그래야 OOM 대신 캐시가 먼저 줄어든다.

---

## 12. Embedded 실무에서 다시 보기

### 12.1 센서 키트 장비의 메모리 정책 설계 (예: 512 MiB i.MX8 보드)

1. **예산표를 먼저 쓴다**: 커널+CMA(카메라 64 MiB) / 제어·센서 데몬 / 인식 앱 / 로그·업로더·웹 UI. 각 서비스에 `MemoryMax=`를 건다(§10).
2. **죽는 순서를 정한다**: 제어 루프·센서 데몬은 `OOMScoreAdjust=-900`~`-1000` + 자체 `MemoryMax`, 업로더·웹 UI는 `+500`. §9.3 plain 실험처럼 "제일 큰 게 하필 센서 데몬"인 사고를 막는다.
3. **swap 대신 zram**: RAM의 25~50% 크기, `lzo-rle` 또는 `zstd`. 실제 데이터로 `mm_stat` 압축률을 잰다(§8.4).
4. **OOM 이후**: `Restart=on-failure`로 되살릴지, `panic_on_oom=1` + watchdog으로 전체 재부팅할지 결정한다. 안전이 걸린 제어기는 "부분 동작"보다 "알려진 상태로 재시작"이 나을 때가 많다.

### 12.2 1 kHz 제어 루프와 메모리

- `mlockall(MCL_CURRENT|MCL_FUTURE)`: 루프 중 page fault 0 (A5 §8). zram이 있어도 잠긴 페이지는 swap되지 않는다.
- 실행 파일·라이브러리 코드 페이지도 `mlockall`로 잠긴다. 메모리 압박 때 코드 페이지가 page cache에서 밀려나 major fault가 나는 지터(§5.5)를 막는다.
- THP는 `madvise` 또는 `never` (§5.4).
- 메모리 할당은 시작 단계에 끝내고 루프 안에서 `malloc` 안 함 — direct reclaim에 걸릴 수 있다.

### 12.3 "장비가 가끔 재부팅돼요" 디버깅 순서

1. `journalctl -k -b -1`(이전 부팅의 커널 로그)에서 `Out of memory`, `invoked oom-killer`를 찾는다.
2. `CONSTRAINT_MEMCG`인지 `global_oom`인지 본다 — 서비스 한도 문제냐, 시스템 전체 부족이냐.
3. Tasks state 표에서 rss 큰 프로세스와 `slab_unreclaimable`(커널 누수 의심)을 본다.
4. 평소 `/proc/vmstat`의 `allocstall_*`, `pgscan_direct`를 모니터링해 OOM 전 경고를 받는다(§7.4).

---

## 13. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 핵심 데몬에 `oom_score_adj` 미설정 | 메모리 압박 때 센서 데몬이 죽음 | badness = 메모리 사용량. 큰 데몬이 1순위 | `OOMScoreAdjust=-900`, 부가 서비스는 +값 (§9.3) |
| 모든 데몬을 -1000으로 | OOM 때 panic 또는 엉뚱한 프로세스만 죽음 | 후보가 없음 | 면제는 1~2개, 나머지는 memcg 한도 |
| SD/eMMC에 swap | 몇 달 뒤 저장장치 고장, 간헐 지연 | flash 수명·GC 지연 | swap 끄고 zram (§8) |
| zram 압축률을 가정 | zram 켰는데 OOM 시점 그대로 | 압축 안 되는 데이터 | `mm_stat`으로 실측 (§8.4 rand 결과) |
| 모듈에서 `pte_offset_map` 사용 | modpost `undefined!` | 6.x에서 export 안 됨 | 정식 API(`pin_user_pages`, `vm_insert_page`) 사용 (§2) |
| PTE 변경 후 TLB flush 누락 | 다른 코어에서 간헐 데이터 손상 | 옛 번역이 TLB에 남음 | `ptep_clear_flush`, `zap_vma_ptes` 등 flush 포함 API (§3) |
| atomic 컨텍스트에서 `GFP_KERNEL` | `BUG: sleeping function called from invalid context` | 할당이 reclaim으로 잠듦 | `GFP_ATOMIC` 또는 할당을 밖으로 (§11) |
| RT 스레드에서 런타임 `malloc` | 가끔 수 ms 지연 | direct reclaim, page fault | 미리 할당 + `mlockall` (§12.2) |
| 로그 데몬이 큰 파일을 버퍼링 없이 append | 주기적으로 쓰기 스레드가 멈춤 | `dirty_ratio` throttle | `dirty_background_bytes` 축소, 스레드 분리 (§6) |
| 옛 글 따라 `mm->mmap` 순회 | 6.1+에서 컴파일 에러 | maple tree로 변경 | `VMA_ITERATOR` + `for_each_vma` (§4) |

---

## 14. 면접에서 이렇게 말한다

**Q.** Walk me through what happens on a page fault in Linux on ARM64.

**A.** 유저가 PTE 없는 주소를 만지면 MMU가 translation fault를 내고 `el0_da` → `do_mem_abort`가 ESR로 원인을 보고 `do_page_fault`로 보낸다. 거기서 per-VMA lock으로 VMA를 찾고 권한을 확인한 뒤(없으면 SIGSEGV) `handle_mm_fault` → `handle_pte_fault`가 갈라진다. 익명이면 `do_anonymous_page`(읽기는 zero page), 파일이면 `filemap_fault`(캐시 miss면 I/O, major), swap entry면 `do_swap_page`, 쓰기 금지 PTE에 쓰기면 `do_wp_page`(COW). PTE를 설치하고 돌아가 명령을 재실행한다.

> The MMU raises a translation or permission fault, the exception vector calls do_mem_abort, which uses the ESR fault status to dispatch to do_page_fault. That looks up the VMA — first under a per-VMA lock, falling back to mmap_lock — checks vm_flags, and sends SIGSEGV if there is no VMA or the access isn't allowed. Otherwise handle_mm_fault walks down to handle_pte_fault, which branches: do_anonymous_page for first-touch anonymous memory, the VMA's fault handler such as filemap_fault for file mappings, do_swap_page for swapped-out pages, and do_wp_page for copy-on-write. The kernel installs the PTE and returns, and the faulting instruction is re-executed. If I/O was needed it counts as a major fault.

**꼬리 질문**: "major fault 하나에 handle_mm_fault가 몇 번 불리나?" → 보통 두 번. 첫 시도가 I/O를 걸고 lock을 풀며 `VM_FAULT_RETRY`를 돌려주고, 재시도가 설치한다 (§5.3).

**Q.** What is a TLB and why do we need ASIDs?

**A.** TLB는 VA→PA 번역 캐시다. 프로세스마다 같은 VA가 다른 PA를 가리키니 태그가 없으면 context switch마다 TLB를 다 비워야 한다. arm64는 nG=1인 유저 엔트리에 ASID를 붙이고, Linux는 ASID를 TTBR1에 둔다. 전환 때 TTBR0와 ASID만 바꾸면 되고 flush는 필요 없다. PTE를 바꿀 땐 `tlbi vale1is`(VA+ASID) 같은 명령을 DSB와 함께 써서 모든 코어에 무효화를 방송한다.

> The TLB caches virtual-to-physical translations. Since every process uses the same virtual addresses for different physical pages, without tags we would have to flush the whole TLB on every context switch. On ARM64, non-global entries are tagged with an ASID; Linux keeps the current ASID in TTBR1 and only swaps TTBR0 and the ASID on a switch. When a PTE changes, the kernel issues a broadcast TLBI by VA and ASID, bracketed by DSBs, so every core drops the stale entry. When ASIDs run out, it starts a new generation and flushes everything once.

**Q.** How does copy-on-write work after fork?

**A.** fork는 페이지를 복사하지 않고 page table만 복사하면서 부모·자식 PTE를 둘 다 읽기 전용으로 바꾼다. 누가 쓰면 permission fault → `do_wp_page`. 페이지를 아직 공유 중이면 새 페이지에 복사(`wp_page_copy`)하고 그 PTE만 바꾼다. 상대가 이미 exit해서 혼자 남았으면 복사 없이 쓰기 권한만 돌려준다. 실제로 커널 모듈로 PTE를 찍어 보면 자식의 PFN만 바뀌고 부모는 그대로였다.

> fork copies the page tables, not the pages, and write-protects the private writable PTEs in both parent and child. The first write takes a permission fault into do_wp_page. If the page is still shared, the kernel allocates a new page, copies it, and points only the writer's PTE at the copy. If the writer turns out to be the only remaining user, it just restores write permission without copying. I verified this with a small kernel module that walked the page tables: after the child's write only the child's PFN changed.

**Q.** How does the OOM killer choose a victim, and how do you protect a critical daemon?

**A.** badness = RSS + swap 엔트리 + page table 페이지 + oom_score_adj × (총 페이지/1000). 가장 큰 프로세스에 SIGKILL. 오래 돈 프로세스 우대 같은 건 없다. 보호는 `oom_score_adj`(-1000이면 후보 제외, systemd `OOMScoreAdjust`)로 하고, 면제한 데몬이 누수해도 시스템을 잡아먹지 않게 memcg `MemoryMax`를 같이 건다. 부가 서비스는 자기 cgroup 안에서 OOM 나게 한도를 건다.

> The kernel computes a badness score — resident pages plus swap entries plus page-table pages, shifted by oom_score_adj scaled to total memory — and kills the highest one with SIGKILL. To protect a critical daemon I set oom_score_adj low, -1000 makes it ineligible, usually through systemd's OOMScoreAdjust. But I pair that with a memory cgroup limit on the daemon itself, and I put non-critical services in their own cgroups with MemoryMax so they hit a memcg-constrained OOM and die alone instead of triggering a global one.

**꼬리 질문**: "oom_kill_disable은?" → cgroup v1 전용이다. v2에는 없고 `memory.high` throttle과 `memory.oom.group`을 쓴다.

**Q.** Why do most embedded Linux systems run without swap? What do you use instead?

**A.** flash 수명(작은 랜덤 쓰기, write amplification), 예측 불가한 지연(swap-in은 major fault, eMMC GC 시 수십 ms), 용량, 보안 때문이다. 대신 메모리 예산 + cgroup 한도 + OOM 순서를 설계하고, 여유가 필요하면 zram을 쓴다. 256 MiB 게스트에서 재 보니 압축되는 데이터는 512M zram으로 192 MiB → 557 MiB까지 썼지만 난수 데이터는 이득이 0이었다.

> Swapping to eMMC or SD generates small random writes that wear out the flash and inflate write amplification, and swap-ins become major faults with unpredictable latency, which is bad for control loops. Storage is also small and swapped memory may leak secrets to flash. So we budget memory explicitly, use cgroup limits and OOM priorities, and if we need headroom we use zram, compressed swap in RAM. In a 256 MB test guest, compressible data grew from about 192 MB to 557 MB before OOM with a 512 MB zram device, while random data gained nothing.

**Q.** What are the zone watermarks and what is direct reclaim?

**A.** zone마다 min/low/high. low 아래로 떨어지면 kswapd가 깨어 백그라운드로 high까지 회수한다. kswapd가 못 따라가 min 근처까지 오면 할당하는 태스크가 직접 회수하는데 이게 direct reclaim이고 응용 지연으로 보인다. `/proc/vmstat`의 `allocstall`, `pgscan_direct`로 본다. 큰 버스트 할당이 있으면 `watermark_scale_factor`를 키워 kswapd가 일찍 일하게 한다.

> Each zone has min, low and high watermarks derived from min_free_kbytes and watermark_scale_factor. When free pages drop below low, the allocator wakes kswapd, which reclaims in the background until high. If kswapd can't keep up and free memory approaches min, the allocating task reclaims synchronously — that's direct reclaim, and it shows up as latency, visible as allocstall and pgscan_direct in /proc/vmstat. Raising watermark_scale_factor makes kswapd start earlier, which helps on devices with bursty large allocations.

**Q.** In a driver, why can't you call a GFP_KERNEL allocation while holding a spinlock?

**A.** `GFP_KERNEL`은 메모리가 모자라면 direct reclaim으로 들어가 writeback I/O를 기다리며 잠들 수 있다. spinlock을 쥔 채 잠들면 데드락이나 스케줄링 버그다. `GFP_ATOMIC`은 reclaim을 안 하고 예비분을 쓰지만 실패할 수 있으니 처리해야 한다. 가능하면 락 밖에서 미리 할당한다.

> GFP_KERNEL allows the allocator to sleep — under memory pressure it enters direct reclaim and may wait on writeback I/O. Sleeping while holding a spinlock or in interrupt context is a bug. GFP_ATOMIC never sleeps or reclaims and can dip into reserves below the min watermark, but it fails more easily, so the caller must handle NULL. The better fix is usually to allocate before taking the lock.

---

## 15. 직접 해보기

1. **개념**: 4K granule, 48비트 VA에서 VA `0x0000_aaaa_bbbb_c123`의 L0/L1/L2/L3 인덱스와 offset을 손으로 구하라.
   정답/힌트: `(va >> 39) & 511`, `(va >> 30) & 511`, `(va >> 21) & 511`, `(va >> 12) & 511`, `va & 0xfff`. Python으로 검산.
2. **코드**: `f11_walk`를 고쳐 `pmd_leaf()`가 참일 때(THP 2 MiB 매핑) pmd 값과 PFN을 찍게 하고, `-m 1024M` 게스트에서 THP `always` + 4 MiB 익명 매핑으로 확인하라.
   정답/힌트: `pmd_pfn(*pmd)`. 512M 이하 게스트는 THP가 기본 꺼짐이니 `-m 1024M`.
3. **관찰**: §5.3의 kprobe에 `do_wp_page`도 걸어 COW 쓰기 때 몇 번 불리는지, 부모가 자식 exit 후 쓸 때도 불리는지 확인하라.
   정답/힌트: `p:f11/wp do_wp_page`. 둘 다 불린다. 복사 여부는 PFN(§2)으로 구분한다.
4. **관찰**: 256M 게스트에서 `watermark_scale_factor`를 10 → 200으로 바꾸고 `/proc/zoneinfo`의 low/high와 hog 실행 후 `allocstall_*`를 비교하라.
   정답/힌트: 간격 = max(min/4, managed × 200/10000) = 1082페이지로 커진다. allocstall이 줄어드는지 본다.
5. **실험**: zram을 `memory.swap.max`와 같이 써서, 한 cgroup만 zram을 쓰고 다른 cgroup은 `memory.swap.max=0`으로 막아 보라.
   정답/힌트: 막힌 cgroup은 swap 없이 `memory.max`에서 바로 OOM, 다른 쪽은 zram까지 쓴다.
6. **설계**: 512 MiB 드론 보드(제어, 카메라 인식, 텔레메트리 업로더, 웹 UI)의 `MemoryMax`/`OOMScoreAdjust`/zram 설정표를 만들고 이유를 한 줄씩 적어라.
   정답/힌트: §12.1 순서. 제어는 -1000 + mlockall, 인식은 0 + MemoryMax, 업로더·웹 UI는 +500.

---

## 16. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| TTBR0_EL1 / TTBR1_EL1 | Translation Table Base Register | 유저(프로세스별) / 커널(공유) page table 시작 PA |
| granule | 변환 단위 페이지 크기 | 4K/16K/64K. Linux arm64 기본 4K |
| pgd/p4d/pud/pmd/pte | Linux의 5단계 표 이름 | 하드웨어 단계 수가 적으면 folding |
| folding | 단계 접기 | 가짜 단계가 아래 표 포인터를 그대로 돌려줌. 랩 커널은 런타임 folding |
| AF | Access Flag | reference bit. 0이면 첫 접근에 fault 또는 HW가 세팅 |
| nG | not Global | 1이면 TLB 엔트리가 ASID에 묶임 |
| DBM | Dirty Bit Modifier | Linux에선 `PTE_WRITE` 의미 |
| ASID | Address Space ID | TLB 엔트리의 프로세스 태그, Linux arm64는 TTBR1에 |
| TLBI | TLB Invalidate 명령 | `vale1is`, `aside1is`, `vmalle1is` |
| break-before-make | arm64 PTE 교체 규칙 | invalid → flush → 새 값 |
| VMA | `vm_area_struct` | 주소 범위 + 권한 + 백킹(파일/익명) |
| maple tree | 6.1+ VMA 자료구조 | `mm->mm_mt` |
| per-VMA lock | VMA 단위 fault 락 | `lock_vma_under_rcu`, 실패 시 mmap_lock |
| minor / major fault | I/O 필요 여부 | major = 저장장치/swap에서 읽음 |
| fault-around | 주변 페이지 동시 매핑 | 캐시 hit 시 16페이지(64 KiB) |
| COW | Copy-On-Write | fork 후 첫 쓰기에 복사 (`do_wp_page`) |
| zero page | 공유 0 페이지 | 익명 읽기 fault의 무료 매핑 |
| folio | 페이지 묶음 단위 | 6.x에서 `struct page`를 대체 중 |
| page cache | 파일 데이터 캐시 | `address_space`의 folio들 |
| watermark | min/low/high | kswapd·direct reclaim 경계 |
| kswapd | 백그라운드 회수 스레드 | low 아래에서 깨어나 high까지 |
| direct reclaim | 동기 회수 | 할당한 태스크가 직접, 지연 원인 |
| MGLRU | Multi-Gen LRU | 6.1+ `CONFIG_LRU_GEN`, 랩 커널엔 꺼짐 |
| rmap | reverse mapping | 페이지 → 매핑한 PTE들 |
| swap entry | 내보낸 페이지 위치 | present 아닌 PTE에 저장 |
| zram | 압축 RAM 블록 장치 | swap으로 쓰면 flash 마모 없음 |
| OOM killer | 메모리 고갈 시 프로세스 처형 | `out_of_memory` → `oom_kill_process` |
| oom_score_adj | 희생 우선순위 조정 | -1000 면제 ~ +1000 1순위 |
| memcg | memory cgroup | `memory.max`/`high`/`low`/`min` |
| PSI | Pressure Stall Information | `/proc/pressure/memory`, 랩 커널엔 꺼짐 |

---

## 17. 요약 & 체크리스트

arm64 Linux는 VA를 TTBR0(유저)/TTBR1(커널)로 나누고 4K granule 4단계 표로 번역한다(랩 커널은 5단계 config가 런타임에 4단계로 접힘). PTE의 AF·AP·nG·UXN/PXN·DBM·SW dirty 비트가 접근·권한·TLB 태그·dirty 추적을 담당하고, TLB는 ASID로 프로세스를 구분해 context switch flush를 피하며, PTE를 바꿀 땐 `dsb; tlbi; dsb`로 모든 코어에 무효화를 방송한다. 주소 공간은 `mm_struct`(maple tree에 VMA, `pgd`, ASID)로 표현되고, page fault는 `do_mem_abort → do_page_fault → handle_mm_fault → handle_pte_fault`에서 익명/파일/swap/COW로 갈라진다. 메모리가 모자라면 watermark에 따라 kswapd → direct reclaim → OOM killer로 올라가고, swap 없는 임베디드는 익명 메모리를 회수할 수 없어 OOM이 빠르다. 그래서 zram(압축되는 데이터라면 측정상 최대 2.9배), `oom_score_adj`로 죽는 순서, memory cgroup으로 서비스별 한도를 설계한다.

- [ ] arm64 48비트 VA를 L0~L3 인덱스로 자르고 Linux 이름(pgd/p4d/pud/pmd/pte)과 folding을 설명할 수 있다
- [ ] PTE 값(예: `00e8000042d21f43`)을 보고 AF, RDONLY, DBM, UXN, SW dirty를 해독할 수 있다
- [ ] 커널 모듈로 `current->mm`의 page table을 걸어 PFN을 찍고 pagemap과 맞출 수 있다
- [ ] ASID가 왜 필요한지, Linux arm64가 ASID를 어디에 두는지 설명할 수 있다
- [ ] `flush_tlb_page`/`flush_tlb_mm`이 어떤 TLBI 명령과 배리어로 구현되는지 말할 수 있다
- [ ] `mm_struct`, VMA(maple tree), page table, folio, rmap의 관계를 그릴 수 있다
- [ ] arm64 page fault 경로와 네 갈래(익명/파일/swap/COW)를 함수 이름으로 설명할 수 있다
- [ ] kprobe로 `handle_mm_fault`를 추적하고 flags·리턴값을 해독할 수 있다
- [ ] min/low/high watermark와 kswapd·direct reclaim의 관계를 `/proc/zoneinfo`·`/proc/vmstat`로 보여 줄 수 있다
- [ ] 임베디드에서 swap을 끄는 이유와 zram의 이득·한계를 측정값으로 설명할 수 있다
- [ ] OOM 리포트를 한 줄씩 읽고 `oom_score`를 손으로 검산할 수 있다
- [ ] `oom_score_adj`와 memcg `memory.max`로 핵심 데몬을 보호하는 정책을 짤 수 있다

## 참고 자료

- OSTEP (Remzi & Andrea Arpaci-Dusseau, "Operating Systems: Three Easy Pieces") — "Paging: Introduction", "Paging: Faster Translations (TLBs)", "Paging: Smaller Tables", "Beyond Physical Memory: Mechanisms", "Beyond Physical Memory: Policies", "Complete Virtual Memory Systems". [https://pages.cs.wisc.edu/~remzi/OSTEP/](https://pages.cs.wisc.edu/~remzi/OSTEP/)
- docs.kernel.org: [Memory Layout on AArch64 Linux](https://docs.kernel.org/arch/arm64/memory.html), [Memory Management (mm) 문서 모음](https://docs.kernel.org/mm/index.html), [Multi-Gen LRU](https://docs.kernel.org/admin-guide/mm/multigen_lru.html), [zram](https://docs.kernel.org/admin-guide/blockdev/zram.html), [Control Group v2](https://docs.kernel.org/admin-guide/cgroup-v2.html), [Documentation for /proc/sys/vm/](https://docs.kernel.org/admin-guide/sysctl/vm.html), [Transparent Hugepage Support](https://docs.kernel.org/admin-guide/mm/transhuge.html), [Kprobe-based Event Tracing](https://docs.kernel.org/trace/kprobetrace.html)
- man7.org: [proc_pid_pagemap(5)](https://man7.org/linux/man-pages/man5/proc_pid_pagemap.5.html), [proc_pid_oom_score_adj(5)](https://man7.org/linux/man-pages/man5/proc_pid_oom_score_adj.5.html), [getrusage(2)](https://man7.org/linux/man-pages/man2/getrusage.2.html), [mlock(2)](https://man7.org/linux/man-pages/man2/mlock.2.html)
- 랩 소스(elixir.bootlin.com에서도 같은 경로): `arch/arm64/mm/fault.c`(`do_mem_abort`, `do_page_fault`), `arch/arm64/mm/context.c`(`cpu_do_switch_mm`, ASID), `arch/arm64/include/asm/tlbflush.h`, `arch/arm64/include/asm/pgtable-hwdef.h`, `pgtable-prot.h`, `mm/memory.c`(`handle_mm_fault`, `do_anonymous_page`, `do_wp_page`), `mm/filemap.c`(`filemap_fault`), `mm/vmscan.c`, `mm/oom_kill.c`, `mm/page_alloc.c`, `mm/zsmalloc.c`, `drivers/block/zram/`
- Arm Architecture Reference Manual for A-profile (DDI 0487) — VMSAv8-64 translation table descriptor 형식, TLB maintenance 명령
- Michael Kerrisk, "The Linux Programming Interface" — 49·50장 (Memory Mappings, Virtual Memory Operations)
- Mel Gorman, "Understanding the Linux Virtual Memory Manager" (2.6 기준이라 오래됐지만 zone·watermark·reclaim 개념 설명은 여전히 유효)
- 선행·연관 노트: A5(유저 관점 VM), F9(커널 메모리 관리), F10(CMA), G5/H9(드라이버 mmap), M 모듈(OOM·Oops 디버깅)
