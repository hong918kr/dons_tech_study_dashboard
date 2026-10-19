# M7. eBPF 도구 — bpftrace · bcc · libbpf CO-RE로 "재부팅 없이, printk 없이" 운영 중인 커널과 앱을 들여다보기

> **이 노트를 다 읽으면**: eBPF 프로그램이 verifier → JIT → hook → map으로 흘러가는 구조를 그림으로 설명할 수 있다 · bpftrace 한 줄로 syscall·파일·kmalloc·블록 I/O·run-queue 지연을 히스토그램으로 뽑을 수 있다 · 내 커널 모듈 함수와 유저 프로그램 함수에 kprobe/fentry/uprobe/USDT를 걸어 지연·인자·리턴값을 잴 수 있다 · bcc가 왜 작은 임베디드 타깃에 안 맞고 libbpf CO-RE 정적 바이너리가 왜 맞는지 실측 숫자(시작 시간·RSS·디스크)로 말할 수 있다
> **왜 필요한가**: "현장 장비에서 가끔 지연 스파이크가 난다, 재부팅도 printk 추가도 안 된다 — 어떻게 하나?"는 로보틱스·카메라 회사 Embedded Linux 면접의 단골 질문이고, 입사 후에는 Jetson·i.MX 장비에서 원인을 찾는 첫 도구가 된다
> **Don 기준 난이도**: "펌웨어에 트레이스 훅을 심고 링버퍼로 빼서 호스트에서 히스토그램을 그린다"는 발상은 이미 익숙하다 / 그 훅을 **실행 중인 커널에 안전하게 주입**하는 방법(verifier, JIT, BTF, CO-RE)과 bpftrace·bcc·libbpf라는 세 가지 배포 형태의 차이가 새로 배울 부분
> **선행 노트**: M5(ftrace · kprobe_events — 같은 훅의 "고정 기능" 버전), M6(perf — 샘플링·PMU), E6(syscall 진입), F1(스케줄러 — runqlat 해석), E3(커널 모듈) — L6(lockdown·seccomp-bpf)은 보안 측면

---

## 0. 큰 그림 — 이게 왜 필요한가

SSD 펌웨어에서 "가끔 커맨드 지연이 튄다"는 버그를 잡을 때 어떻게 했는지 떠올려 보자. 의심 가는 함수 앞뒤에 타임스탬프를 찍는 trace 매크로를 넣고, 링버퍼에 쌓고, vendor-unique 명령으로 빼내서, 호스트에서 히스토그램을 그렸다. 문제는 **매번 FW를 다시 빌드하고 다시 올려야** 한다는 것이었다. 그리고 다시 올리는 순간 재현 조건(열, 수명, 큐 상태)이 사라지기도 했다.

eBPF는 이 과정을 **실행 중인 커널에서, 재빌드·재부팅 없이** 해 준다.

- 원하는 지점(커널 함수 입구·출구, tracepoint, 유저 함수, 타이머, PMU 이벤트)에
- 작은 프로그램(타임스탬프 찍기, 카운트, 히스토그램 bucket 증가)을 **런타임에 주입**하고
- 결과를 커널 안의 **map**(해시·배열·링버퍼)에 모아서
- 유저 공간이 요약본만 읽어 간다.

주입되는 코드가 커널을 망가뜨리면 안 되므로, 커널은 로드 전에 **verifier**(정적 분석기)로 "메모리를 벗어나지 않는가, 반드시 끝나는가, 허락된 함수만 부르는가"를 증명하고, 통과한 것만 **JIT**로 네이티브 arm64 코드로 바꿔 붙인다. 펌웨어로 치면 "서명 대신 **정적 증명**을 통과한 패치만 런타임에 hot-patch 하는 디버그 모니터"다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="20" y="22" font-size="14" font-weight="bold">관찰 도구의 스펙트럼 — 오른쪽으로 갈수록 "운영 중"에 강하다</text>
<line x1="30" y1="250" x2="660" y2="250" stroke="currentColor" marker-end="url(#ah1)"/>
<text x="660" y="272" font-size="12" text-anchor="end">프로그래머블 · 커널 내 집계 · 재부팅 불필요</text>
<rect x="30" y="50" width="140" height="170" rx="6" fill="#888" fill-opacity="0.15" stroke="#888"/>
<text x="42" y="74" font-size="13" font-weight="bold">printk / dev_dbg</text>
<text x="42" y="98" font-size="12">코드 수정 + 재빌드</text>
<text x="42" y="118" font-size="12">대개 재부팅 필요</text>
<text x="42" y="138" font-size="12">텍스트 로그</text>
<text x="42" y="158" font-size="12">dynamic debug는</text>
<text x="42" y="176" font-size="12">켜고 끄기만 가능</text>
<text x="42" y="206" font-size="12" font-weight="bold">M1</text>
<rect x="190" y="50" width="140" height="170" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="202" y="74" font-size="13" font-weight="bold">ftrace</text>
<text x="202" y="98" font-size="12">정해진 tracer 선택</text>
<text x="202" y="118" font-size="12">이벤트를 버퍼에</text>
<text x="202" y="138" font-size="12">다 기록 → 사후 분석</text>
<text x="202" y="158" font-size="12">hist trigger로</text>
<text x="202" y="176" font-size="12">약간의 집계</text>
<text x="202" y="206" font-size="12" font-weight="bold">M5</text>
<rect x="350" y="50" width="140" height="170" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="362" y="74" font-size="13" font-weight="bold">perf</text>
<text x="362" y="98" font-size="12">PMU 카운터</text>
<text x="362" y="118" font-size="12">주기 샘플링</text>
<text x="362" y="138" font-size="12">"어디서 CPU를</text>
<text x="362" y="158" font-size="12">쓰나"에 최강</text>
<text x="362" y="206" font-size="12" font-weight="bold">M6</text>
<rect x="510" y="50" width="150" height="170" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="522" y="74" font-size="13" font-weight="bold">eBPF</text>
<text x="522" y="98" font-size="12">임의 지점에 코드 주입</text>
<text x="522" y="118" font-size="12">커널 안에서 필터·집계</text>
<text x="522" y="138" font-size="12">요약만 유저로</text>
<text x="522" y="158" font-size="12">bpftrace · bcc ·</text>
<text x="522" y="176" font-size="12">libbpf CO-RE</text>
<text x="522" y="206" font-size="12" font-weight="bold">M7 (이 노트)</text>
</svg>
```
그림 1 — printk에서 eBPF까지. 네 도구는 경쟁 관계가 아니라 같은 커널 훅(kprobe, tracepoint, perf_event)을 다른 방식으로 쓰는 층이다. eBPF의 차별점은 "이벤트를 다 내보내지 않고 커널 안에서 걸러서 요약한다"는 것이다.

이 노트에서 쓰는 세 가지 도구 형태를 먼저 구분해 두자. 나중에 임베디드 배포 판단의 핵심이 된다.

| 형태 | 사용자가 쓰는 것 | BPF 바이트코드는 언제 만들어지나 | 타깃에 필요한 것 |
|---|---|---|---|
| **bpftrace** | awk 비슷한 한 줄 스크립트 | 타깃에서 실행할 때 (내장 LLVM) | bpftrace + LLVM/clang 라이브러리 |
| **bcc** | Python(또는 Lua/C++) + 안에 박힌 C 문자열 | 타깃에서 실행할 때 (libclang으로 C 컴파일) | Python + libbcc + LLVM/clang + 커널 헤더 |
| **libbpf CO-RE** | C로 쓴 BPF 프로그램 + C 로더 | **개발 PC에서 미리** (clang -target bpf) | 정적 바이너리 하나 + 커널 BTF |

---

## 1. eBPF 모델 — 커널 안의 작은 RISC 가상 머신

### 1.1 명령어 집합과 레지스터

eBPF는 커널 안에 정의된 **64비트 RISC 명령어 집합**이다. 레지스터는 11개(`r0`~`r10`)다.

| 레지스터 | 용도 | arm64 JIT에서 매핑 (참고) |
|---|---|---|
| `r0` | 리턴값 (helper 호출 결과, 프로그램 종료값) | `x7` |
| `r1`~`r5` | 함수 인자 (helper 호출 시) — 프로그램 시작 때 `r1` = context 포인터 | `x0`~`x4` |
| `r6`~`r9` | callee-saved | `x19`~`x22` |
| `r10` | 읽기 전용 프레임 포인터 (스택 512바이트) | `x25` |

arm64 매핑은 `arch/arm64/net/bpf_jit_comp.c`의 `bpf2a64[]` 표에 있다(6.18 기준). 명령은 8바이트 고정 길이(`opcode:8, dst:4, src:4, off:16, imm:32`)이고, 64비트 상수 로드만 16바이트다. 나중에 §8에서 `llvm-objdump`로 실제 바이트를 본다.

Cortex-M 펌웨어를 쓰던 사람에게 친숙한 비유: **AAPCS처럼 "인자 r1–r5, 리턴 r0, callee-saved r6–r9"인 호출 규약이 고정된 작은 ISA**다. 그래서 JIT가 거의 1:1로 네이티브 명령으로 옮길 수 있다.

### 1.2 프로그램 하나가 붙기까지 — 5단계

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="20" y="22" font-size="14" font-weight="bold">eBPF 파이프라인 — 소스에서 hook까지, 결과는 map으로</text>
<text x="20" y="48" font-size="12" font-weight="bold">유저 공간</text>
<rect x="20" y="58" width="150" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="30" y="80" font-size="13">① 소스</text>
<text x="30" y="100" font-size="12">스크립트 / C 소스</text>
<rect x="190" y="58" width="150" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="200" y="80" font-size="13">② LLVM BPF 백엔드</text>
<text x="200" y="100" font-size="12">→ 바이트코드 + BTF</text>
<rect x="360" y="58" width="150" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="370" y="80" font-size="13">③ 로더 (libbpf)</text>
<text x="370" y="100" font-size="12">CO-RE 재배치 · map</text>
<rect x="530" y="58" width="130" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="540" y="80" font-size="13">⑦ 결과 읽기</text>
<text x="540" y="100" font-size="12">map 읽기 · ringbuf</text>
<line x1="170" y1="86" x2="188" y2="86" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="340" y1="86" x2="358" y2="86" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="20" y1="136" x2="660" y2="136" stroke="currentColor" stroke-dasharray="5,4"/>
<text x="560" y="152" font-size="12">bpf() syscall</text>
<line x1="435" y1="114" x2="435" y2="176" stroke="currentColor" marker-end="url(#ah2)"/>
<text x="20" y="170" font-size="12" font-weight="bold">커널</text>
<rect x="300" y="178" width="270" height="70" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="312" y="200" font-size="13" font-weight="bold">④ verifier (bpf_check)</text>
<text x="312" y="220" font-size="12">모든 경로를 시뮬레이션: 범위·NULL·</text>
<text x="312" y="238" font-size="12">종료·helper 권한 증명 → 실패면 거부</text>
<rect x="300" y="268" width="270" height="48" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="312" y="290" font-size="13" font-weight="bold">⑤ JIT (bpf_int_jit_compile)</text>
<text x="312" y="307" font-size="12">바이트코드 → arm64 네이티브</text>
<line x1="435" y1="248" x2="435" y2="266" stroke="currentColor" marker-end="url(#ah2)"/>
<rect x="20" y="268" width="250" height="110" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="32" y="290" font-size="13" font-weight="bold">⑥ hook에 attach</text>
<text x="32" y="312" font-size="12">kprobe · fentry · tracepoint</text>
<text x="32" y="332" font-size="12">uprobe · USDT · perf_event</text>
<text x="32" y="352" font-size="12">LSM · XDP · cgroup …</text>
<text x="32" y="370" font-size="12">이벤트마다 프로그램 실행</text>
<line x1="300" y1="292" x2="272" y2="292" stroke="currentColor" marker-end="url(#ah2)"/>
<rect x="590" y="178" width="70" height="138" rx="6" fill="#888" fill-opacity="0.15" stroke="#888"/>
<text x="602" y="200" font-size="13" font-weight="bold">maps</text>
<text x="602" y="222" font-size="12">hash</text>
<text x="602" y="240" font-size="12">array</text>
<text x="602" y="258" font-size="12">percpu</text>
<text x="602" y="276" font-size="12">ringbuf</text>
<text x="602" y="294" font-size="12">stack</text>
<path d="M270,340 Q620,360 625,318" fill="none" stroke="currentColor" marker-end="url(#ah2)"/>
<text x="420" y="372" font-size="12">프로그램이 map에 기록</text>
<line x1="625" y1="178" x2="610" y2="116" stroke="currentColor" marker-end="url(#ah2)"/>
</svg>
```
그림 2 — eBPF 파이프라인. ①–③은 bpftrace·bcc에서는 **타깃에서** 일어나고, libbpf CO-RE에서는 ①–②가 **개발 PC에서** 끝난다. ④⑤는 언제나 타깃 커널 안에서 일어난다. 결과는 이벤트 스트림이 아니라 map(요약)으로 유저에게 간다.

단계별로 짧게 정의한다.

- **verifier**: 커널의 `kernel/bpf/verifier.c`(`bpf_check()`). 프로그램의 **모든 분기 경로**를 레지스터 상태(타입, 값 범위)를 들고 시뮬레이션한다. 포인터가 map 값의 범위를 넘을 수 있으면, NULL일 수 있는 포인터를 역참조하면, 종료를 증명할 수 없으면, 허락 안 된 helper를 부르면 **로드 자체를 거부**한다. 실행 시점 검사가 아니라 **로드 시점 증명**이다.
- **JIT**: verifier를 통과한 바이트코드를 네이티브 명령으로 번역(`arch/arm64/net/bpf_jit_comp.c`). `CONFIG_BPF_JIT_ALWAYS_ON`이 아니면 인터프리터도 남아 있다. 랩 커널은 `bpf_jit_enable=1`이다(§4).
- **map**: 커널이 소유하는 key/value 저장소. BPF 프로그램과 유저 공간이 fd(또는 bpffs 경로)로 공유한다. 종류: `HASH`, `ARRAY`, `PERCPU_HASH/ARRAY`(락 없이 CPU별 카운트), `RINGBUF`(이벤트 스트림, 5.8+), `STACK_TRACE`, `LRU_HASH`, `PERF_EVENT_ARRAY` 등.
- **helper**: BPF 프로그램이 부를 수 있는 커널 함수의 **고정 목록**(`bpf_map_lookup_elem`, `bpf_ktime_get_ns`, `bpf_get_current_pid_tgid`, `bpf_probe_read_kernel`, `bpf_get_stackid` …). 프로그램 타입마다 허용 목록이 다르다. 번호가 UAPI로 고정돼 있다(`include/uapi/linux/bpf.h`의 `___BPF_FUNC_MAPPER`).
- **kfunc**: 5.13부터 생긴 "BTF로 시그니처가 기술된 커널 함수를 직접 부르기". helper처럼 UAPI 번호가 없고 **안정성 약속도 없다**(커널 버전 따라 바뀔 수 있음). 새 기능은 helper 대신 kfunc로 추가되는 추세다.

### 1.3 프로그램 타입 — "어디에 붙느냐"가 "무엇을 할 수 있느냐"를 정한다

프로그램 타입마다 context(`r1`이 가리키는 것), 허용 helper, 리턴값 의미가 다르다. 트레이싱 관점에서 중요한 것만 정리한다.

| 타입 (attach) | context | 쓰임 | 안정성 |
|---|---|---|---|
| `kprobe` / `kretprobe` | `struct pt_regs` (레지스터) | 거의 모든 커널 함수 입구·출구 | 함수 이름·인자가 버전마다 바뀔 수 있음 |
| `fentry` / `fexit` (`BPF_PROG_TYPE_TRACING`) | BTF로 타입이 붙은 인자 배열 | kprobe보다 싸고, fexit에서 **인자와 리턴값을 동시에** 봄 | 함수 시그니처에 의존 |
| `tracepoint` | tracepoint별 구조체 (format 파일) | 커널이 공식으로 심어 둔 지점 (`sched:sched_switch` 등) | **안정적** (ABI에 가깝게 취급) |
| `raw_tracepoint` / `tp_btf` | tracepoint의 원시 인자 | tracepoint보다 싸다 (인자 복사 없음) | tracepoint와 같음 |
| `uprobe` / `uretprobe` | `pt_regs` (유저 레지스터) | 유저 바이너리 함수 | 바이너리마다 |
| USDT | uprobe + 노트 섹션의 인자 위치 | 앱이 심어 둔 정적 지점 (`DTRACE_PROBE`) | 앱이 유지하면 안정 |
| `perf_event` | `bpf_perf_event_data` | 주기 샘플링(profile), PMU 오버플로 | 안정 |
| `lsm` (BPF LSM) | LSM 훅 인자 | 보안 정책 — **L6 참조** | 훅 목록 의존 |
| `xdp`, `tc`, `cgroup_skb`, `sk_*` | 패킷 | 네트워크 데이터 경로 — I6 참조, 이 노트에서는 생략 | 안정 |

XDP/tc는 "관찰"이 아니라 "패킷 처리"용이다. 같은 VM·verifier·map을 쓰지만 이 노트의 범위가 아니므로 I6 §6.4(XDP)로 넘긴다. seccomp는 **classic BPF**(cBPF, 옛 패킷 필터 ISA)를 쓰며 eBPF가 아니다 — L6 §9.1.

---

## 2. attach 지점 — kprobe, fentry, tracepoint, uprobe, USDT, perf_event

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="20" y="22" font-size="14" font-weight="bold">어디에 붙일 수 있나 — 앱부터 하드웨어까지</text>
<rect x="20" y="40" width="640" height="80" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="32" y="62" font-size="13" font-weight="bold">유저 프로세스</text>
<rect x="40" y="74" width="190" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="50" y="96" font-size="12">uprobe / uretprobe (함수)</text>
<rect x="250" y="74" width="200" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="260" y="96" font-size="12">USDT (DTRACE_PROBE 지점)</text>
<rect x="470" y="74" width="170" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="480" y="96" font-size="12">libc · 런타임 함수</text>
<line x1="20" y1="134" x2="660" y2="134" stroke="currentColor" stroke-dasharray="5,4"/>
<text x="500" y="150" font-size="12">syscall (svc #0)</text>
<rect x="20" y="160" width="640" height="170" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="32" y="182" font-size="13" font-weight="bold">커널</text>
<rect x="40" y="194" width="190" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="50" y="216" font-size="12">tracepoint: syscalls:*</text>
<rect x="250" y="194" width="200" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="260" y="216" font-size="12">kprobe: 아무 함수 (+offset)</text>
<rect x="470" y="194" width="170" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="480" y="216" font-size="12">fentry/fexit: BTF 함수</text>
<rect x="40" y="240" width="190" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="50" y="262" font-size="12">sched · block · kmem</text>
<rect x="250" y="240" width="200" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="260" y="262" font-size="12">모듈 함수 (m7_do_work)</text>
<rect x="470" y="240" width="170" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="480" y="262" font-size="12">LSM 훅 (L6)</text>
<rect x="40" y="286" width="190" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="50" y="308" font-size="12">net: XDP · tc (I6)</text>
<rect x="250" y="286" width="390" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="260" y="308" font-size="12">software 이벤트: cpu-clock, page-faults, context-switches</text>
<rect x="20" y="346" width="640" height="56" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="32" y="368" font-size="13" font-weight="bold">하드웨어 (PMU)</text>
<text x="32" y="390" font-size="12">perf_event: cycles · instructions · cache-misses 오버플로 → profile:hz, hardware:* (M6)</text>
</svg>
```
그림 3 — attach 지점 지도. 위로 갈수록 "앱이 정의한 의미"(USDT), 아래로 갈수록 "하드웨어 사실"(PMU)이다. 커널 층에서 왼쪽 열(tracepoint)은 안정적이고, 가운데 열(kprobe)은 아무 데나 붙지만 버전에 약하고, 오른쪽(fentry)은 kprobe를 더 싸게 대체한다.

### 2.1 kprobe / kretprobe — 아무 커널 함수에, 브레이크포인트로

**kprobe**는 커널 함수의 첫 명령을 브레이크포인트 명령(arm64: `BRK`)으로 바꿔 놓고, 그 예외에서 등록된 핸들러(여기서는 BPF 프로그램)를 돌린 뒤 원래 명령을 single-step으로 실행한다. x86 같은 일부 아키텍처는 함수 입구 kprobe를 `BRK` 대신 ftrace 진입점으로 처리하는 **ftrace 기반 kprobe**(`KPROBES_ON_FTRACE`)를 지원하지만, **arm64 6.18에는 이 옵션이 없다**(`arch/arm64/Kconfig`에 `HAVE_KPROBES_ON_FTRACE`가 없음 — 랩 소스에서 grep으로 확인). 그래서 arm64의 kprobe는 항상 BRK 예외를 탄다. 어느 쪽이든 원리는 M5에서 본 `kprobe_events`와 **같은 커널 kprobe 인프라**다 — eBPF는 그 위에 "핸들러로 BPF 프로그램"을 붙인 것뿐이다.

**kretprobe**는 함수 입구에서 리턴 주소를 trampoline으로 바꿔치기해서 리턴 시점을 잡는다(동시에 진행 중인 호출 수만큼 인스턴스가 필요하다 — `maxactive`). JTAG로 치면 kprobe = "소프트웨어 브레이크포인트 + 스크립트 + 자동 continue"다.

### 2.2 fentry / fexit — BTF trampoline (arm64 지원 상태)

`-pg`/`-fpatchable-function-entry`로 빌드된 커널은 함수마다 첫머리에 NOP 몇 개를 둔다(dynamic ftrace, M5). **fentry**는 그 NOP을 BPF 전용 **trampoline**으로 가는 직접 분기로 바꾼다. 예외(BRK)를 안 타니 kprobe보다 싸고, BTF로 인자 타입을 알기 때문에 `args->filename`처럼 이름으로 인자를 읽는다. **fexit**는 원래 함수를 trampoline 안에서 호출하고 돌아온 뒤 실행되므로 **인자와 리턴값을 한 프로그램에서 같이** 본다(kretprobe는 리턴 시점에 인자가 이미 사라져 있어서 입구에서 map에 저장해 둬야 한다).

arm64 지원 이력(대략): arm64 BPF trampoline(fentry/fexit)은 2022년(5.19 전후) 메인라인에 들어갔지만 처음에는 분기 거리 제약 등으로 쓸 수 있는 함수가 제한됐고, 6.x 초반에 `DYNAMIC_FTRACE_WITH_CALL_OPS`와 ftrace direct call 지원이 들어오면서 모듈 함수까지 넓어졌다. 정확한 버전보다 중요한 것은 **자기 커널에서 직접 확인하는 것**이다. 랩 커널 6.18.55는 `CONFIG_DYNAMIC_FTRACE_WITH_CALL_OPS=y`, `CONFIG_DYNAMIC_FTRACE_WITH_DIRECT_CALLS=y`, `CONFIG_DYNAMIC_FTRACE_WITH_ARGS=y`다. **실제로 vmlinux 함수와 모듈 함수 둘 다에 붙는지는 §6.3에서 직접 확인한다.** bpftrace 0.20은 fentry/fexit을 `kfunc:` / `kretfunc:`라고 부른다(0.21부터 `fentry:`/`fexit:` 별칭이 추가됨).

### 2.3 tracepoint / raw_tracepoint — 커널이 공식으로 심어 둔 지점

tracepoint는 커널 개발자가 소스에 `trace_sched_switch(...)` 같은 호출을 심어 둔 지점이다(M5 §tracepoint). 꺼져 있을 때는 static key로 거의 0 비용이고, 인자 형식이 `/sys/kernel/tracing/events/<sys>/<name>/format`에 공개된다. 함수 이름이 바뀌어도 tracepoint는 대개 유지되므로, **장기적으로 유지할 도구는 tracepoint를 우선**한다. `raw_tracepoint`(`tp_btf`)는 인자를 구조체로 복사하지 않고 원시 포인터로 넘겨 더 싸다.

### 2.4 uprobe / USDT — 유저 프로그램 안으로

**uprobe**는 유저 바이너리의 특정 오프셋을 `BRK`로 바꾼다. 바이너리 파일(inode)에 대해 걸리므로 그 파일을 실행하는 **모든 프로세스**에 적용된다. 예외 → 커널 진입 → BPF 실행 → 복귀를 거치므로 **비싸다**(§11에서 측정). **USDT**(User Statically-Defined Tracing)는 앱이 `<sys/sdt.h>`의 `DTRACE_PROBE()` 매크로로 심어 둔 NOP 한 개 + ELF 노트(`.note.stapsdt`)다. 노트에 "인자 1은 x19 레지스터, 인자 2는 [sp+8]" 같은 위치가 적혀 있어서 도구가 인자를 이름 없이도 정확히 읽는다. 앱 쪽의 tracepoint라고 보면 된다.

### 2.5 perf_event — 타이머·PMU로 깨우기

`profile:hz:99`는 CPU마다 초당 99번 perf 소프트웨어 타이머(또는 PMU 오버플로)로 BPF 프로그램을 깨워 `kstack`/`ustack`을 기록한다. perf record(M6)와 같은 샘플링이지만 **스택을 커널 안에서 바로 카운트**해서 결과가 작다. 99Hz처럼 홀수를 쓰는 건 주기적인 작업과 lock-step으로 겹치는 것을 피하려는 관례다.

---

## 3. BTF와 CO-RE — "한 번 빌드해서 어느 커널에서나"

### 3.1 문제: 구조체 오프셋은 커널마다 다르다

BPF 프로그램이 `task->real_parent->tgid`를 읽으려면 `task_struct` 안에서 `real_parent`의 **오프셋**을 알아야 한다. 이 오프셋은 커널 버전, config(`CONFIG_` 옵션 하나에 필드가 생기고 사라진다), 컴파일러에 따라 달라진다. 그래서 bcc는 **타깃에서, 타깃 커널의 헤더로** C를 컴파일한다(그래서 LLVM과 커널 헤더가 타깃에 있어야 한다).

### 3.2 해법: BTF + CO-RE

- **BTF**(BPF Type Format): 커널의 모든 타입(구조체, 필드 오프셋, 함수 시그니처)을 DWARF보다 훨씬 작게 압축한 형식. `CONFIG_DEBUG_INFO_BTF=y`면 빌드 때 pahole이 DWARF에서 BTF를 만들어 vmlinux의 `.BTF` 섹션에 넣고, 런타임에 `/sys/kernel/btf/vmlinux`로 노출한다. 모듈은 `CONFIG_DEBUG_INFO_BTF_MODULES`로 `/sys/kernel/btf/<module>`(split BTF — vmlinux BTF를 base로 차분만 담음).
- **CO-RE**(Compile Once – Run Everywhere): clang이 `BPF_CORE_READ(t, real_parent, tgid)` 같은 접근을 컴파일할 때 "개발 PC의 vmlinux.h 기준 오프셋 0x660"을 넣되, **"이건 task_struct.real_parent 접근이다"라는 재배치 기록**(`.BTF.ext`)을 함께 남긴다. 로드할 때 libbpf가 **타깃 커널의 `/sys/kernel/btf/vmlinux`** 를 읽어 실제 오프셋으로 패치한다.

펌웨어 비유: 링커가 심볼 주소를 나중에 채우는 relocation과 같은 아이디어를 **구조체 필드 오프셋**에 적용한 것이다. 그 결과 타깃에는 컴파일러가 필요 없다.

### 3.3 직접 보기 — 커널 BTF 크기, CO-RE 재배치 기록

랩 커널(`6.18.55-trace`)의 BTF는 vmlinux의 `.BTF` 섹션이다. 같은 vmlinux의 DWARF(`.debug_*` 섹션 합계)와 크기를 비교했다(VM 호스트에서 실행).

```sh
readelf -SW /var/lab/out/vmlinux-trace | awk '/\.debug_/{s+=strtonum("0x"$6)} END{printf "debug_total %.1f MiB\n", s/1048576}'
printf "BTF %d\n" 0x84004b      # .BTF 섹션 크기(readelf -S에서 읽은 값)
```

```text
debug_total 502.0 MiB
BTF 8650827
```

**출력에서 볼 것**: DWARF 502 MiB 대 BTF 8.25 MiB — 약 60배 작다. 그래서 BTF는 양산 커널에도 넣을 수 있다(커널 이미지가 그만큼 커지고 RAM에 상주한다는 비용은 있다). 게스트에서 본 `/sys/kernel/btf/vmlinux`도 정확히 8,650,827바이트였다(§4).

개발 PC 쪽에서 CO-RE가 실제로 무엇을 남기는지 보자. §10의 `m7_count.bpf.c`를 clang으로 컴파일한 뒤 재배치 기록을 함께 디스어셈블했다(VM 호스트, clang 21.1.8).

```sh
clang -g -O2 -target bpf -D__TARGET_ARCH_arm64 -I$LIBBPF/include -c m7_count.bpf.c -o m7_count.bpf.o
llvm-objdump -dr --no-show-raw-insn m7_count.bpf.o | sed -n 18,30p
```

```text
      11:	call 0xe
      12:	r0 >>= 0x20
      13:	*(u32 *)(r10 - 0x18) = w0
      14:	r1 = 0x660
		0000000000000070:  CO-RE <byte_off> [33] struct task_struct::real_parent (0:91)
      15:	r6 += r1
      16:	r1 = r10
      17:	r1 += -0x28
      18:	w2 = 0x8
      19:	r3 = r6
      20:	call 0x71
      21:	r1 = 0x654
		00000000000000a8:  CO-RE <byte_off> [33] struct task_struct::tgid (0:89)
```

**출력에서 볼 것**: `r1 = 0x660`은 개발 PC의 vmlinux.h 기준 `real_parent` 오프셋이고, 바로 아래 `CO-RE <byte_off> ... task_struct::real_parent`가 "로드할 때 이 상수를 타깃 커널의 실제 오프셋으로 고쳐라"는 기록이다. `call 0xe`는 helper 14번(`bpf_get_current_pid_tgid`), `call 0x71`은 113번(`bpf_probe_read_kernel`)이다 — `BPF_CORE_READ`가 내부적으로 `bpf_probe_read_kernel`을 부른다.

---

## 4. 랩 환경 — 커널 config와 `bpftool feature probe`

### 4.1 eBPF 트레이싱에 필요한 커널 config

장비 커널을 만들 때 아래가 빠져 있으면 eBPF 도구는 시작도 못 한다. 랩 `Image-trace`의 실제 값이다(`/var/lab/out/linux-build-trace/.config`에서 grep).

| config | 랩 값 | 없으면 |
|---|---|---|
| `CONFIG_BPF_SYSCALL` | y | `bpf()` syscall 자체가 없다 — 모든 도구 실패 |
| `CONFIG_BPF_JIT` | y (`BPF_JIT_ALWAYS_ON`은 not set) | 인터프리터로 돌아 수 배 느림 |
| `CONFIG_DEBUG_INFO_BTF` (+`_MODULES`) | y / y | CO-RE·fentry·bpftrace의 `args->` 불가. 빌드 호스트에 **pahole** 필요 (랩: `PAHOLE_VERSION=131`) |
| `CONFIG_KPROBES`, `CONFIG_KPROBE_EVENTS` | y / y | kprobe 계열 불가 |
| `CONFIG_UPROBES`, `CONFIG_UPROBE_EVENTS` | y / y | uprobe·USDT 불가 |
| `CONFIG_BPF_EVENTS` | y | tracepoint/kprobe에 BPF 붙이기 불가 |
| `CONFIG_FUNCTION_TRACER`, `DYNAMIC_FTRACE_WITH_DIRECT_CALLS` | y / y | fentry/fexit trampoline 불가 |
| `CONFIG_PERF_EVENTS`, `CONFIG_ARM_PMUV3` | y / y | `profile:`, `hardware:` 불가 |
| `CONFIG_IKHEADERS` | y | bcc가 커널 헤더를 못 찾음 (헤더 패키지를 따로 깔아야 함) |
| `CONFIG_BPF_LSM`, `CONFIG_CGROUP_BPF` | y / y | 보안·cgroup 프로그램 (L6, L8) |
| `CONFIG_BPF_UNPRIV_DEFAULT_OFF` | y | (켜져 있는 게 정상) 비특권 BPF 금지 |

### 4.2 게스트에서 확인하기 — 예제 1

**무엇을 확인하나**: 부팅한 커널이 BTF·JIT·kheaders를 노출하는지, 그리고 BPF 관련 sysctl 기본값. 게스트는 QEMU TCG `-cpu cortex-a57,pmu=on -smp 2 -m 2G` + `Image-trace` + `rootfs-trace`(Ubuntu 24.04 + bpftrace 0.20.2 + bcc 0.29.1)이다. bpftool은 Ubuntu 패키지 설치가 실패해서, 커널 소스 `tools/bpf/bpftool`을 VM 호스트에서 **정적 링크로 빌드**해 넣었다(§12.3).

```sh
uname -r; nproc; grep MemTotal /proc/meminfo
ls -la /sys/kernel/btf/vmlinux; ls /sys/kernel/btf | wc -l
for f in net/core/bpf_jit_enable net/core/bpf_jit_harden net/core/bpf_jit_kallsyms \
         kernel/unprivileged_bpf_disabled kernel/perf_event_paranoid; do
  echo "$f = $(cat /proc/sys/$f)"; done
ls -la /sys/kernel/kheaders.tar.xz
```

```text
6.18.55-trace
2
MemTotal:        1992860 kB
-r--r--r-- 1 root root 8650827 Oct 18 06:47 /sys/kernel/btf/vmlinux
1
net/core/bpf_jit_enable = 1
net/core/bpf_jit_harden = 0
net/core/bpf_jit_kallsyms = 1
kernel/unprivileged_bpf_disabled = 2
kernel/perf_event_paranoid = 2
-r--r--r-- 1 root root 4588316 Oct 18 06:47 /sys/kernel/kheaders.tar.xz
```

**출력에서 볼 것**: `/sys/kernel/btf/`에 `vmlinux` 하나뿐인 것은 아직 BTF 있는 모듈을 하나도 안 올렸기 때문이다(§6에서 `m7_lat`을 올리면 늘어난다). `unprivileged_bpf_disabled = 2`는 "비특권 BPF 금지, 단 root가 0으로 되돌릴 수 있음"(1이면 되돌릴 수 없음)이다. `bpf_jit_kallsyms = 1`이라 JIT된 프로그램이 `/proc/kallsyms`에 `bpf_prog_<tag>_<name>`으로 나타나 스택 트레이스에서 이름이 보인다.

### 4.3 `bpftool feature probe` — 예제 2

**무엇을 확인하나**: 이 커널이 실제로 지원하는 프로그램 타입·map 타입·helper를, 작은 테스트 프로그램을 실제로 로드해 보며 탐지한다. 새 보드 bring-up 때 가장 먼저 돌릴 명령이다.

```sh
bpftool feature probe kernel > feat.txt 2>&1; wc -l feat.txt
grep -E "^eBPF program_type .* is available" feat.txt | wc -l
grep -E "^eBPF map_type .* is available" feat.txt | wc -l
grep -E "JIT|CONFIG_BPF_JIT|CONFIG_DEBUG_INFO_BTF|CONFIG_BPF_EVENTS|ISA extension" feat.txt
```

```text
2544 feat.txt
31
32
JIT compiler is enabled
JIT compiler hardening is disabled
JIT compiler kallsyms exports are enabled for root
Global memory limit for JIT compiler for unprivileged users is 69267617742848 bytes
CONFIG_HAVE_EBPF_JIT is set to y
CONFIG_BPF_JIT is set to y
CONFIG_BPF_JIT_ALWAYS_ON is not set
CONFIG_DEBUG_INFO_BTF is set to y
CONFIG_DEBUG_INFO_BTF_MODULES is set to y
CONFIG_BPF_EVENTS is set to y
ISA extension v2 is available
ISA extension v3 is available
ISA extension v4 is available
```

**출력에서 볼 것**: 프로그램 타입 31개, map 타입 32개가 사용 가능하다고 나왔다(`lirc_mode2`만 NOT available — `CONFIG_BPF_LIRC_MODE2` 미설정). bpftool은 `/proc/config.gz`(랩 커널은 `IKCONFIG_PROC`)를 읽어 `CONFIG_*`도 함께 보고한다. `ISA extension v4`는 부호 확장 로드·32비트 점프 등 clang 17+가 `-mcpu=v4`로 내는 명령을 이 커널 verifier가 받아 준다는 뜻이다. 같은 파일에서 kprobe 프로그램 타입에 허용된 helper는 79개로 집계됐다.

### 4.4 BTF 목록과 크기 — 예제 3

```sh
bpftool btf list | head -3
bpftool btf dump file /sys/kernel/btf/vmlinux | wc -l           # raw 형식 줄 수
bpftool btf dump file /sys/kernel/btf/vmlinux format c | wc -l  # vmlinux.h 줄 수
bpftool btf dump file /sys/kernel/btf/vmlinux | grep -m1 "STRUCT 'task_struct'"
```

```text
1: name [vmlinux]  size 8650827B
16: name <anon>  size 44922B
	pids bpftool(458)
463461
242700
[109] STRUCT 'task_struct' size=8960 vlen=260
```

**출력에서 볼 것**: id 16의 `<anon>` BTF는 bpftool 자신이 `pids` 기능을 위해 방금 올린 BPF 프로그램의 BTF다(그래서 `pids bpftool(458)`). `format c` 덤프가 바로 CO-RE 개발에 쓰는 **vmlinux.h**이고 24만 줄이다(VM 호스트에서 vmlinux-trace로 만든 것과 줄 수가 같다: 242700). 이 커널의 `task_struct`는 8,960바이트, 필드 260개 — 디버그 config가 많아서 크다.

### 4.5 붙일 수 있는 지점이 몇 개나 되나 — 예제 4

```sh
for p in 'tracepoint:*' 'kprobe:*' 'kfunc:*' 'rawtracepoint:*' 'software:*' 'hardware:*'; do
  printf "%-18s %s\n" "$p" "$(bpftrace -l "$p" 2>/dev/null | wc -l)"; done
```

```text
tracepoint:*       2180
kprobe:*           70612
kfunc:*            68421
rawtracepoint:*    1536
software:*         14
hardware:*         12
```

**출력에서 볼 것**: kprobe 가능 함수 7만 개 중 fentry(`kfunc:`) 가능 함수가 6만 8천 개 — BTF가 있고 ftrace 계측이 되는 함수만 fentry 대상이다. tracepoint는 2,180개로 훨씬 적지만 안정적이다. 실무 규칙: **tracepoint에 있으면 tracepoint, 없으면 fentry, fentry가 안 되면 kprobe.**

---

## 5. bpftrace — awk처럼 쓰는 커널 트레이싱 언어

### 5.1 문법 한 장

bpftrace 프로그램은 `probe /filter/ { action }` 블록의 나열이다. awk의 `pattern { action }`과 같은 모양이다.

```
probe[,probe...] /filter/ { action; action; }

tracepoint:syscalls:sys_enter_openat /pid == 1234/ { @opens[comm] = count(); }
```

| 요소 | 예 | 의미 |
|---|---|---|
| probe | `kprobe:vfs_read`, `kretprobe:vfs_read`, `kfunc:vmlinux:vfs_read`, `tracepoint:sched:sched_switch`, `uprobe:/bin/bash:readline`, `usdt:/path:prov:name`, `profile:hz:99`, `interval:s:1`, `BEGIN`, `END` | 어디서 실행하나 |
| filter | `/comm == "cat"/`, `/args->bytes_alloc >= 4096/`, `/@start[tid]/` | 조건이 참일 때만 action |
| 내장 변수 | `pid`, `tid`, `comm`, `nsecs`, `cpu`, `uid`, `kstack`, `ustack`, `arg0`..`argN`, `retval`, `args->필드` | 컨텍스트 정보 |
| map | `@name[key] = value` | 커널 BPF map. 종료 시 자동 출력 |
| 집계 함수 | `count()`, `sum(x)`, `avg(x)`, `min/max(x)`, `hist(x)`(2의 거듭제곱 bucket), `lhist(x, min, max, step)`(선형 bucket), `stats(x)` | map 값으로 쓰면 커널 안에서 per-CPU로 집계 |
| 함수 | `printf()`, `str(ptr)`, `kstack(n)`, `ustack(n)`, `delete(@m[k])`, `exit()`, `time()` | 출력·문자열 복사·스택 |

`arg0..`은 kprobe/uprobe에서 **레지스터 값**(arm64: x0..x7)이다. `args->이름`은 tracepoint(format 파일) 또는 fentry(BTF)에서 타입이 붙은 인자다. `retval`은 kretprobe/kretfunc/uretprobe에서만 있다.

### 5.2 인자 목록 보기 — 예제 5

**무엇을 확인하나**: `-lv`로 probe의 인자 이름·타입을 먼저 확인하고 스크립트를 쓴다(BTF·tracepoint format에서 읽는다).

```sh
bpftrace -lv 'kfunc:vmlinux:vfs_read'
bpftrace -lv 'tracepoint:syscalls:sys_enter_openat'
```

```text
kfunc:vmlinux:vfs_read
    struct file * file
    char * buf
    size_t count
    loff_t * pos
    ssize_t retval
tracepoint:syscalls:sys_enter_openat
    int __syscall_nr
    int dfd
    const char * filename
    int flags
    umode_t mode
```

**출력에서 볼 것**: fentry 쪽은 BTF에서 온 C 시그니처 그대로이고 `retval`까지 보인다(kretfunc에서 사용 가능).

### 5.3 bpftrace 시작 비용 — 예제 6

**무엇을 확인하나**: 아무 일도 안 하는 스크립트(`BEGIN { exit(); }`)가 끝나기까지 걸리는 시간. 내장 LLVM이 스크립트를 BPF로 컴파일하고, BTF를 파싱하고, 로드·attach까지 하는 고정비다.

```sh
for i in 1 2 3; do bash -c 'time bpftrace -e "BEGIN { exit(); }"' 2>&1 | grep -E "real"; done
```

```text
real	0m24.154s
real	0m2.176s
real	0m2.132s
```

**출력에서 볼 것**: 첫 실행 24초, 그다음 2.1초. 첫 실행은 bpftrace 바이너리와 **libLLVM(116 MB)·libclang(34 MB)을 디스크에서 page cache로 읽어 들이는** 콜드 스타트 비용이 대부분이다. QEMU TCG(에뮬레이션, 다른 작성자 16명과 VM 공유 중)라 절대값은 실제 하드웨어보다 수 배~수십 배 크다 — **비율만** 의미 있게 보자. 실제 Cortex-A 보드에서도 "첫 bpftrace는 느리다"는 같은 이유로 성립하고, eMMC가 느린 장비일수록 심해진다.

### 5.4 원라이너 모음 — 예제 7 (syscall 수를 프로세스별로)

**무엇을 확인하나**: 4초 동안 프로세스별 syscall 횟수. 장비가 "왠지 바쁘다"일 때 제일 먼저 돌리는 한 줄이다. 배경에서 `find /`를 돌렸다.

```sh
( find / -xdev -type f > /dev/null 2>&1; ) &
bpftrace -B line -e 'tracepoint:raw_syscalls:sys_enter { @[comm] = count(); }
  interval:s:4 { exit(); }' 2>&1 | sort -t: -k2 -n | tail -8
```

```text
Attaching 2 probes...
@[systemd]: 36
@[bpftrace]: 93
@[swupdate-progre]: 684
@[find]: 12869
```

**출력에서 볼 것**: `find`가 압도적이다. 그런데 2등이 **`swupdate-progress`(684회)** — 이 rootfs에서 놀고 있어야 할 OTA 진행 표시 데몬이 4초에 684번 syscall을 하고 있다. 실제 장비라면 "idle인데 배터리가 빨리 닳는다"의 범인 후보가 이렇게 한 줄로 드러난다(L4에서 본 SWUpdate 구성의 부산물). `interval:s:4 { exit(); }`는 Ctrl-C 없이 자동 종료하는 관용구다.

### 5.5 열린 파일을 이름과 함께 — 예제 8

**무엇을 확인하나**: `openat`에 넘어간 파일 이름을 실시간으로 본다. "이 데몬이 설정 파일을 어디서 찾나?", "왜 부팅마다 SD 카드를 긁나?"의 답이다. 첫 시도에서는 부하를 tracer 시작과 동시에 돌렸더니 bpftrace가 attach하기도 전에(시작에 2초 이상 걸린다, §5.3) 부하가 끝나 **아무것도 안 잡혔다**. 그래서 `BEGIN`에서 `READY`를 찍게 하고, 그 줄이 나올 때까지 기다린 뒤 부하를 줬다 — 자동화 스크립트에서 꼭 필요한 패턴이다.

```sh
bpftrace -B line -e 'BEGIN { printf("READY\n"); }
  tracepoint:syscalls:sys_enter_openat /comm == "cat" || comm == "ls"/ {
    printf("%-6d %-6s %s\n", pid, comm, str(args->filename)); }' > bg.out 2>&1 &
BP=$!; until grep -q READY bg.out; do sleep 0.2; done
cat /etc/hostname /etc/os-release > /dev/null; ls /root/m7 > /dev/null; sleep 1
kill -INT $BP; wait $BP; head -14 bg.out
```

```text
Attaching 2 probes...
READY
994    cat    /etc/ld.so.cache
994    cat    /lib/aarch64-linux-gnu/libc.so.6
994    cat    
994    cat    /usr/share/locale/locale.alias
994    cat    /usr/lib/locale/C.UTF-8/LC_IDENTIFICATION
994    cat    /usr/lib/locale/C.utf8/LC_IDENTIFICATION
994    cat    /usr/lib/aarch64-linux-gnu/gconv/gconv-modules.cache
994    cat    /usr/lib/locale/C.UTF-8/LC_MEASUREMENT
994    cat    /usr/lib/locale/C.utf8/LC_MEASUREMENT
994    cat    /usr/lib/locale/C.UTF-8/LC_TELEPHONE
994    cat    /usr/lib/locale/C.utf8/LC_TELEPHONE
994    cat    /usr/lib/locale/C.UTF-8/LC_ADDRESS
```

**출력에서 볼 것**: `cat` 하나가 `/etc/hostname`을 열기 전에 동적 링커 캐시, libc, **로케일 파일 수십 개**를 연다. 세 번째 줄의 빈 파일 이름은 `str()`이 읽는 순간 그 유저 메모리 페이지가 아직 메모리에 없어서(page fault를 BPF 안에서 처리할 수 없다) 빈 문자열이 된 경우다 — `sys_enter`에서 유저 포인터를 읽을 때의 고전적 한계다. 이게 싫으면 커널이 이미 복사해 둔 이름을 `fexit`에서 읽는다(§6.4).

### 5.6 kmalloc 크기 히스토그램과 호출 스택 — 예제 9

**무엇을 확인하나**: 3초 동안 커널 `kmalloc` 할당 크기 분포(`hist`), 그리고 4 KiB 이상 큰 할당이 어디서 오는지 커널 스택별 카운트. 드라이버 메모리 누수·단편화를 의심할 때 첫 단계다(F9의 slab 이야기와 연결).

```sh
( find /usr -xdev > /dev/null 2>&1 ) &
bpftrace -B line -e 'tracepoint:kmem:kmalloc { @bytes = hist(args->bytes_alloc); }
  interval:s:3 { exit(); }'
```

```text
Attaching 2 probes...


@bytes: 
[8, 16)               68 |@@@@@@@@@@@@@                                       |
[16, 32)               4 |                                                    |
[32, 64)             257 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
[64, 128)             13 |@@                                                  |
[128, 256)             8 |@                                                   |
[256, 512)             5 |@                                                   |
[512, 1K)              2 |                                                    |
[1K, 2K)               1 |                                                    |
[2K, 4K)               5 |@                                                   |
[4K, 8K)               1 |                                                    |
[8K, 16K)              0 |                                                    |
[16K, 32K)             0 |                                                    |
[32K, 64K)             2 |                                                    |
```

큰 할당만 스택별로(같은 방식으로 READY를 기다린 뒤 `ls -R /usr/lib`와 `dd`로 부하, 출력 끝부분만):

```sh
bpftrace -B line -e 'BEGIN { printf("READY\n"); }
  tracepoint:kmem:kmalloc /args->bytes_alloc >= 4096/ { @[kstack(7)] = count(); }'
```

```text
@[
    __traceiter_kmalloc+116
    __traceiter_kmalloc+116
    __kmalloc_cache_noprof+732
    proc_cgroup_show+56
    proc_single_show+100
    seq_read_iter+264
    seq_read+244
]: 25
@[
    __traceiter_kmalloc+116
    __traceiter_kmalloc+116
    __kvmalloc_node_noprof+840
    seq_read_iter+668
    kernfs_fop_read_iter+340
    vfs_read+588
    ksys_read+108
]: 56
@[
    __traceiter_kmalloc+116
    __traceiter_kmalloc+116
    __kvmalloc_node_noprof+840
    seq_read_iter+668
    seq_read+244
    vfs_read+204
    ksys_read+108
]: 62
```

**출력에서 볼 것**: 히스토그램은 `[32, 64)`에 몰려 있다 — `find`가 dentry·파일 이름 관련 작은 객체를 많이 만든다. 4 KiB 이상 할당의 대부분은 `seq_read_iter` — `/proc`·`/sys` 파일을 읽을 때 seq_file 버퍼를 `kvmalloc`으로 잡는 경로다. 맨 위 두 줄 `__traceiter_kmalloc`은 tracepoint 자체의 프레임이다(그래서 `kstack(7)`로 깊이를 넉넉히 잡았다). `kstack`·`hist`·`count`가 **전부 커널 안에서** 집계됐고 유저로 온 것은 요약뿐이다.

### 5.7 블록 I/O 지연 — 예제 10 (`biolatency.bt`)

**무엇을 확인하나**: bpftrace에 포함된 도구 스크립트(`/usr/sbin/*.bt`, 39개)를 그대로 쓴다. `biolatency.bt`는 `block:block_bio_queue` → `block:block_bio_complete` tracepoint 사이 시간을 µs 히스토그램으로 낸다. 부하는 virtio 디스크를 `O_DIRECT` 4 KiB로 읽었다.

```sh
bpftrace -B line /usr/sbin/biolatency.bt > bg.out 2>&1 &     # "Tracing" 줄을 기다린 뒤
dd if=/dev/vda of=/dev/null bs=4k count=4000 iflag=direct
kill -INT $BP; wait $BP; cat bg.out
```

```text
16384000 bytes (16 MB, 16 MiB) copied, 6.61505 s, 2.5 MB/s
Attaching 5 probes...
Tracing block device I/O... Hit Ctrl-C to end.



@usecs: 
[64, 128)            829 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@                   |
[128, 256)          1302 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
[256, 512)           613 |@@@@@@@@@@@@@@@@@@@@@@@@                            |
[512, 1K)            289 |@@@@@@@@@@@                                         |
[1K, 2K)             451 |@@@@@@@@@@@@@@@@@@                                  |
[2K, 4K)             378 |@@@@@@@@@@@@@@@                                     |
[4K, 8K)             109 |@@@@                                                |
[8K, 16K)             22 |                                                    |
[16K, 32K)             6 |                                                    |
[32K, 64K)             0 |                                                    |
[64K, 128K)            1 |                                                    |
[128K, 256K)           1 |                                                    |
[256K, 512K)           1 |                                                    |
```

**출력에서 볼 것**: 4,000번 읽기 대부분이 64–512 µs인데, **0.1~0.5초짜리가 3번** 있다. 평균(약 1.6 ms = 6.6 s / 4000)만 보면 절대 모르는 꼬리다. SSD 펌웨어에서 "평균 latency는 좋은데 QoS 99.99%가 나쁘다"를 잡던 것과 같은 그림이다. 여기서는 QEMU 호스트(공유 VM)의 I/O 경합이 원인이고, 실제 eMMC라면 GC·캐시 flush·thermal throttling을 의심한다(J 모듈).

### 5.8 run-queue 지연 — 예제 11 (`runqlat.bt`)

**무엇을 확인하나**: 태스크가 깨어난(`sched_wakeup`) 뒤 실제로 CPU를 받기(`sched_switch`)까지 기다린 시간. `hackbench -g 4 -l 200`(프로세스 160개가 소켓으로 메시지 교환)으로 CPU 2개를 과부하시켰다.

```sh
bpftrace -B line /usr/sbin/runqlat.bt > bg.out 2>&1 &        # "Tracing" 줄을 기다린 뒤
hackbench -g 4 -l 200
kill -INT $BP; wait $BP; cat bg.out
```

```text
Time: 78.011
Attaching 5 probes...
Tracing CPU scheduler... Hit Ctrl-C to end.



@usecs: 
[16, 32)               1 |                                                    |
[32, 64)             313 |@@@                                                 |
[64, 128)           4410 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@   |
[128, 256)          4105 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@      |
[256, 512)          1591 |@@@@@@@@@@@@@@@@@                                   |
[512, 1K)           1377 |@@@@@@@@@@@@@@@                                     |
[1K, 2K)            1279 |@@@@@@@@@@@@@@                                      |
[2K, 4K)            1146 |@@@@@@@@@@@@                                        |
[4K, 8K)            1033 |@@@@@@@@@@@                                         |
[8K, 16K)           1593 |@@@@@@@@@@@@@@@@@                                   |
[16K, 32K)          2538 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@                        |
[32K, 64K)          3545 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@             |
[64K, 128K)         4610 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
[128K, 256K)        3507 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@             |
[256K, 512K)        2003 |@@@@@@@@@@@@@@@@@@@@@@                              |
[512K, 1M)           776 |@@@@@@@@                                            |
[1M, 2M)             161 |@                                                   |
[2M, 4M)              16 |                                                    |
[4M, 8M)               5 |                                                    |
[8M, 16M)              2 |                                                    |
[16M, 32M)             1 |                                                    |
[32M, 64M)             1 |                                                    |
```

**출력에서 볼 것**: **봉우리가 두 개**다(bimodal). 왼쪽(64–256 µs)은 CPU가 비어 있어 바로 실행된 경우, 오른쪽(16–256 ms)은 160개 프로세스가 CPU 2개를 두고 줄 선 경우다. 200 Hz 센서 루프(주기 5 ms)라면 오른쪽 봉우리 전체가 "주기를 놓친" 사건이다. 해법은 우선순위·CPU 격리(N2)이고, 효과를 같은 도구로 다시 재서 증명한다. `Time: 78.011`은 hackbench가 TCG 위에서 78초 걸렸다는 뜻이다(실제 HW에서는 1초 미만).

### 5.9 `lhist`와 `sum` — 예제 12

**무엇을 확인하나**: `kretprobe:vfs_read`의 리턴값(읽은 바이트 수)을 8 KiB 간격 선형 히스토그램으로, 프로세스별 총 바이트를 `sum`으로.

```sh
( sleep 1; cat /usr/bin/* > /dev/null 2>&1; dd if=/dev/zero of=/dev/null bs=512 count=200 2>/dev/null ) &
bpftrace -B line -e 'kretprobe:vfs_read /retval > 0/ { @ret = lhist(retval, 0, 65536, 8192);
  @sum[comm] = sum(retval); } interval:s:4 { exit(); }' 2>&1 | tail -20
```

```text
@ret: 
[0, 8K)              309 |@@@@@@@@@@@@                                        |
[8K, 16K)            117 |@@@@                                                |
[16K, 24K)             7 |                                                    |
[24K, 32K)             1 |                                                    |
[32K, 40K)             0 |                                                    |
[40K, 48K)             1 |                                                    |
[48K, 56K)             0 |                                                    |
[56K, 64K)             0 |                                                    |
[64K, ...)          1318 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|

@sum[systemd]: 64
@sum[dd]: 103232
@sum[cat]: 165207962
```

**출력에서 볼 것**: `cat`은 128 KiB 버퍼로 읽어서 대부분 `[64K, ...)`에 들어간다(4초에 158 MiB). `dd bs=512`는 `[0, 8K)` 쪽. 이 예에서는 `sleep 1`로 bpftrace가 attach할 시간을 벌었고 정상적으로 잡혔다. `lhist`는 "패킷 크기가 MTU 근처에 몰리나", "DMA 전송 크기가 원하는 블록 크기인가" 같은 질문에 좋다.

### 5.10 `profile` — 샘플링으로 "어디서 CPU를 쓰나" — 예제 13

```sh
( stress-ng --cpu 2 --timeout 4 > /dev/null 2>&1 ) &
bpftrace -B line -e 'profile:hz:49 { @[kstack(3), comm] = count(); } interval:s:3 { exit(); }' 2>&1 | tail -16
```

```text
@[
    default_idle_call+72
    do_idle+652
    cpu_startup_entry+64
, swapper/0]: 20
@[
    default_idle_call+72
    do_idle+652
    cpu_startup_entry+64
, swapper/1]: 30
@[
    default_idle_call+72
    do_idle+652
    cpu_startup_entry+60
, swapper/0]: 34
@[, stress-ng-cpu]: 156
```

**출력에서 볼 것**: `stress-ng-cpu` 샘플 156개는 **커널 스택이 비어 있다**(`@[, stress-ng-cpu]`) — 순수 유저 모드 계산이라 샘플 순간 커널에 있지 않았기 때문이다. 유저 스택까지 보려면 `ustack`을 쓰는데, 프레임 포인터 없이 빌드된 바이너리(Ubuntu 24.04 패키지 대부분)는 스택이 잘린다. 처음 몇 초는 stress-ng가 아직 안 떠서 idle(`swapper`) 샘플이 섞였다. 깊은 CPU 프로파일은 perf + flame graph(M6)가 더 낫다.

---

## 6. 내 커널 모듈 함수에 kprobe와 fentry 걸기

### 6.1 측정 대상 모듈 — `m7_lat.ko`

**무엇을 확인하나**: kthread가 5 ms마다 `m7_do_work(level)`을 부른다. level 0/1/3에 따라 `udelay(50 << level)`(50/100/400 µs)만큼 busy-wait 한다. 16번에 한 번 level 3, 4번에 한 번 level 1, 나머지는 level 0이라 **지연 분포에 꼬리가 있는** 함수다. `noinline`이 없으면 컴파일러가 kthread 함수 안으로 인라인해서 probe를 걸 심볼이 사라질 수 있다.

```c
// SPDX-License-Identifier: GPL-2.0
/* m7_lat.c — kthread calling m7_do_work() with 4 different costs, for kprobe/fentry latency demos */
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/delay.h>

static struct task_struct *m7_thread;
static unsigned long m7_calls;

/* noinline: keep a real symbol so kprobe/fentry can attach */
noinline int m7_do_work(unsigned int level)
{
	udelay(50U << level);		/* level 0..3 -> 50,100,200,400 us */
	m7_calls++;
	return level * 10;
}
EXPORT_SYMBOL_GPL(m7_do_work);

static int m7_fn(void *arg)
{
	unsigned int i = 0;

	while (!kthread_should_stop()) {
		/* mostly cheap calls, occasionally an expensive one */
		unsigned int level = (i % 16 == 0) ? 3 : (i % 4 == 0) ? 1 : 0;

		m7_do_work(level);
		i++;
		msleep(5);
	}
	return 0;
}

static int __init m7_init(void)
{
	m7_thread = kthread_run(m7_fn, NULL, "m7_worker");
	if (IS_ERR(m7_thread))
		return PTR_ERR(m7_thread);
	pr_info("m7_lat: worker started\n");
	return 0;
}

static void __exit m7_exit(void)
{
	kthread_stop(m7_thread);
	pr_info("m7_lat: stopped after %lu calls\n", m7_calls);
}

module_init(m7_init);
module_exit(m7_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("M7 eBPF latency demo target");
```

빌드는 trace 커널 빌드 트리로 한다. 마지막 줄 `BTF [M]`이 모듈 BTF 생성이다(VM 호스트에 pahole이 있고, 빌드 트리에 vmlinux가 있어서 가능).

```sh
echo 'obj-m := m7_lat.o' > Makefile
make -C /var/lab/out/linux-build-trace M=$PWD modules 2>&1 | tail -5
readelf -S m7_lat.ko | grep -i btf
```

```text
  CC [M]  .module-common.o
  LD [M]  m7_lat.ko
  BTF [M] m7_lat.ko
make[1]: Leaving directory '/var/tmp/M7/mod'
make: Leaving directory '/var/lab/build/linux-trace'
  [47] .BTF              PROGBITS         0000000000000000  00027aec
  [48] .BTF.base         PROGBITS         0000000000000000  00027bab
```

게스트에서 올리고 심볼과 BTF를 확인했다.

```sh
insmod ./m7_lat.ko; sleep 1; dmesg | tail -2
grep m7_do_work /proc/kallsyms; ls /sys/kernel/btf | grep m7
```

```text
[  842.758227] m7_lat: module verification failed: signature and/or required key missing - tainting kernel
[  842.878253] m7_lat: worker started
ffffb8e9ac9c60ac r __kstrtab_m7_do_work	[m7_lat]
ffffb8e9ac9c60b7 r __kstrtabns_m7_do_work	[m7_lat]
ffffb8e9ac9c6000 r __ksymtab_m7_do_work	[m7_lat]
ffffb8e9ac9c2068 t m7_do_work	[m7_lat]
m7_lat
```

**출력에서 볼 것**: 서명 경고는 `MODULE_SIG`가 켜졌지만 강제는 아닌 랩 커널이라 나온 taint 경고다(L5·L6). `/sys/kernel/btf/m7_lat`이 생겼다 — 이제 이 모듈 함수에 fentry를 걸 수 있다.

### 6.2 kprobe + kretprobe로 지연 히스토그램 — 예제 14

**무엇을 확인하나**: 입구에서 `@s[tid] = nsecs`와 인자(`arg0` = level)를 저장하고, 출구에서 차이를 µs 히스토그램에 넣는다. kretprobe에서는 인자가 사라졌으므로 입구에서 저장해 둔 `@lv[tid]`를 쓴다.

```sh
bpftrace -B line -e 'kprobe:m7_do_work { @s[tid] = nsecs; @lv[tid] = arg0; }
kretprobe:m7_do_work /@s[tid]/ {
  @us = hist((nsecs - @s[tid]) / 1000);
  @avg_by_level[@lv[tid]] = avg((nsecs - @s[tid]) / 1000);
  @ret[retval] = count(); delete(@s[tid]); delete(@lv[tid]); }
interval:s:5 { exit(); }'
```

```text
Attaching 3 probes...


@avg_by_level[0]: 185
@avg_by_level[1]: 242
@avg_by_level[3]: 565

@lv[2833]: 0
@ret[30]: 25
@ret[10]: 75
@ret[0]: 303
@s[2833]: 857079480246
@us: 
[64, 128)            111 |@@@@@@@@@@@@@@@@@@@@@@@                             |
[128, 256)           246 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
[256, 512)            32 |@@@@@@                                              |
[512, 1K)              8 |@                                                   |
[1K, 2K)               5 |@                                                   |
[2K, 4K)               1 |                                                    |
```

**출력에서 볼 것**: 리턴값 카운트가 정확히 3:1(`@ret[0]` 303, `@ret[10]` 75)과 1/16(`@ret[30]` 25)의 비율이라 코드의 분기 비율과 맞는다. 5초 동안 403번 → 약 12 ms 간격(설계는 5 ms + 작업 시간이지만 TCG에서 `msleep(5)`가 늘어진다). level별 평균은 185/242/565 µs로 설계값(50/100/400 µs)보다 크다 — `udelay`는 정확해도 **probe 자체 비용과 TCG의 BRK 예외 에뮬레이션**이 더해진다. 종료 순간 진행 중이던 호출이 `@s`, `@lv`에 한 개 남아 출력된 것도 보인다(`delete` 전에 끝남).

### 6.3 fentry/fexit — arm64 6.18에서 vmlinux와 모듈 둘 다 되나 — 예제 15

**무엇을 확인하나**: §2.2의 질문에 대한 실측 답. 먼저 BTF로 시그니처가 보이는지, 그다음 실제로 attach해서 값이 나오는지.

```sh
bpftrace -lv 'kfunc:m7_lat:m7_do_work'
bpftrace -lv 'kfunc:vmlinux:do_sys_openat2'
```

```text
kfunc:m7_lat:m7_do_work
    unsigned int level
    int retval
kfunc:vmlinux:do_sys_openat2
    int dfd
    const char * filename
    struct open_how * how
    int retval
```

```sh
bpftrace -B line -e 'kfunc:m7_lat:m7_do_work { @s[tid] = nsecs; }
kretfunc:m7_lat:m7_do_work /@s[tid]/ {
  @us[args->level] = hist((nsecs - @s[tid]) / 1000);
  @r[args->level, retval] = count(); delete(@s[tid]); }
interval:s:4 { exit(); }'
```

```text
Attaching 3 probes...


@r[3, 30]: 20
@r[1, 10]: 60
@r[0, 0]: 239
@s[2833]: 886130684780
@us[3]: 
[256, 512)            16 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
[512, 1K)              3 |@@@@@@@@@                                           |
[1K, 2K)               1 |@@@                                                 |

@us[1]: 
[64, 128)              1 |                                                    |
[128, 256)            58 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
[256, 512)             1 |                                                    |

@us[0]: 
[64, 128)            222 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
[128, 256)            13 |@@@                                                 |
[256, 512)             1 |                                                    |
[512, 1K)              2 |                                                    |
[1K, 2K)               1 |                                                    |
```

**출력에서 볼 것**: **arm64 6.18.55에서 모듈 함수에 fentry/fexit가 동작한다.** fexit(`kretfunc`)에서 `args->level`(인자)과 `retval`(리턴값)을 **한 프로그램에서** 같이 읽었다 — `@r[3, 30]`. kretprobe 버전(§6.2)은 입구에서 map에 저장해야 했다. 또 level별 분포가 kprobe 때보다 좁고 낮다(level 0이 거의 다 64–128 µs, level 1이 128–256 µs): BRK 예외가 없어 probe 비용이 작기 때문이다(§11에서 따로 잰다).

### 6.4 fexit로 커널이 복사해 둔 파일 이름 읽기 — 예제 16

**무엇을 확인하나**: §5.5에서 `sys_enter_openat`의 유저 포인터를 읽다 빈 문자열이 나온 문제를 fexit로 피해 본다. `do_sys_openat2`가 **끝난 뒤**에는 커널이 파일 이름을 이미 유저 메모리에서 읽어 간 상태라 페이지가 메모리에 있고, 리턴값(fd 또는 `-errno`)도 같이 볼 수 있다.

```sh
bpftrace -B line -e 'BEGIN { printf("READY\n"); }
  kretfunc:vmlinux:do_sys_openat2 /comm == "cat"/ {
    printf("%s dfd=%d file=%s ret=%d\n", comm, args->dfd, str(args->filename), retval); }' > bg.out 2>&1 &
# READY를 기다린 뒤
cat /etc/hostname > /dev/null; cat /nonexistent 2>/dev/null; sleep 1; kill -INT $BP; wait $BP; head -8 bg.out
```

```text
Attaching 2 probes...
READY
cat dfd=-100 file=/etc/ld.so.cache ret=3
cat dfd=-100 file=/lib/aarch64-linux-gnu/libc.so.6 ret=3
cat dfd=-100 file=/usr/lib/locale/locale-archive ret=-2
cat dfd=-100 file=/usr/share/locale/locale.alias ret=-2
cat dfd=-100 file=/usr/lib/locale/C.UTF-8/LC_IDENTIFICATION ret=-2
cat dfd=-100 file=/usr/lib/locale/C.utf8/LC_IDENTIFICATION ret=3
```

**출력에서 볼 것**: vmlinux 함수에서도 fentry/fexit가 동작한다(§6.3의 모듈 함수와 함께 **arm64 6.18.55에서 둘 다 확인**). `dfd=-100`은 `AT_FDCWD`, `ret=-2`는 `-ENOENT` — **실패한 open이 한 줄에 원인과 함께** 보인다. "설정 파일을 못 찾아 기본값으로 돈다" 같은 현장 버그를 strace 없이(프로세스를 멈추지 않고) 찾는 방법이다. 이번에는 이름이 빈 줄이 없었다.

---

## 7. uprobe와 USDT — 유저 프로그램 안으로

### 7.1 측정 대상 프로그램

**무엇을 확인하나**: `m7_process(id, len)`은 `len × 2000`번 도는 계산 함수(uprobe 대상), `DTRACE_PROBE3(m7, request, ...)`는 USDT 지점이다. `<sys/sdt.h>`는 게스트 rootfs의 `systemtap-sdt-dev` 패키지에서 복사해 VM 호스트에서 정적 빌드했다.

```c
/* m7_app.c — user program for uprobe/uretprobe + USDT demos */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/sdt.h>

struct m7_req { int id; int len; char tag[16]; };

/* noinline + used: keep a stable symbol for uprobe */
__attribute__((noinline, used)) int m7_process(int id, int len)
{
	volatile unsigned long acc = 0;

	for (long i = 0; i < (long)len * 2000; i++)
		acc += (unsigned long)i ^ (unsigned long)id;
	return (int)(acc & 0xff) + len;
}

int main(int argc, char **argv)
{
	int n = argc > 1 ? atoi(argv[1]) : 20;
	struct m7_req r;

	for (int i = 0; i < n; i++) {
		r.id = i;
		r.len = 1 + (i % 5) * 4;	/* 1,5,9,13,17 */
		snprintf(r.tag, sizeof(r.tag), "req-%d", i);
		/* USDT probe: provider m7, name request, 3 args */
#ifdef M7_FIX	/* pass a register-resident local, not a field of the stack struct */
		DTRACE_PROBE3(m7, request, i, r.len, r.tag);
#else
		DTRACE_PROBE3(m7, request, r.id, r.len, r.tag);
#endif
		int ret = m7_process(r.id, r.len);
		DTRACE_PROBE2(m7, done, r.id, ret);
		usleep(100 * 1000);
	}
	printf("m7_app: %d requests done\n", n);
	return 0;
}
```

```sh
gcc -std=gnu11 -Wall -Wextra -O2 -g -static -I/var/tmp/M7/inc m7_app.c -o m7_app
gcc -std=gnu11 -Wall -Wextra -O2 -g -static -DM7_FIX -I/var/tmp/M7/inc m7_app.c -o m7_app_fix
for f in m7_app m7_app_fix; do readelf -n $f | grep -A4 "Provider: m7" | grep Arguments; done
```

```text
    Arguments: -4@[sp] -4@[sp, 4] 8@x19
    Arguments: -4@x6 -4@x0
    Arguments: -4@x23 -4@[sp, 4] 8@x19
    Arguments: -4@x6 -4@x0
```

**출력에서 볼 것**: USDT 노트의 인자 기술 문자열이다. `-4@[sp]`는 "부호 있는 4바이트, 주소 sp의 메모리", `8@x19`는 "8바이트, 레지스터 x19"(문자열 포인터). 컴파일러가 구조체 `r`을 스택에 두었으므로 `r.id`는 `[sp]`, `r.len`은 `[sp, 4]`다. `M7_FIX` 빌드는 첫 인자로 루프 변수 `i`를 넘겨 `x23` 레지스터가 됐다.

### 7.2 uprobe + uretprobe — 인자·리턴값·지연 — 예제 17

**무엇을 확인하나**: `-c`로 bpftrace가 프로그램을 직접 실행하게 하면 attach가 끝난 뒤 프로그램이 시작되므로 첫 호출부터 잡힌다. 입구에서 `arg1`(len, 레지스터 x1)을 저장하고 출구에서 `retval`과 지연을 찍는다. printf의 `id=??`는 인자를 일부러 안 찍은 자리 표시다.

```sh
bpftrace -B line -e 'uprobe:/root/m7/m7_app:m7_process { @t[tid] = nsecs; @len[tid] = arg1; }
uretprobe:/root/m7/m7_app:m7_process /@t[tid]/ {
  printf("id=?? len=%-3d ret=%-4d %6d us\n", @len[tid], retval, (nsecs - @t[tid]) / 1000);
  @us[@len[tid]] = avg((nsecs - @t[tid]) / 1000); delete(@t[tid]); delete(@len[tid]); }' \
  -c '/root/m7/m7_app 10'
```

```text
Attaching 2 probes...
id=?? len=1   ret=153    3130 us
id=?? len=5   ret=253     386 us
id=?? len=9   ret=97      326 us
id=?? len=13  ret=197    2793 us
id=?? len=17  ret=41     1086 us
id=?? len=1   ret=153     389 us
id=?? len=5   ret=253     430 us
id=?? len=9   ret=97      467 us
id=?? len=13  ret=197    1108 us
id=?? len=17  ret=41      438 us
m7_app: 10 requests done




@us[9]: 572
@us[17]: 1606
@us[13]: 2199
@us[1]: 2730
@us[5]: 4485
```

**출력에서 볼 것**: 인자 `len`과 리턴값은 정확하다(같은 len이면 같은 ret — 결정적 함수). 그러나 지연은 len과 **상관이 없다**: len=1이 3,130 µs, len=17이 438 µs. 함수 자체 계산은 수십 µs인데, 그 위에 uprobe 두 번(입구 BRK + 리턴 trampoline)의 TCG 예외 에뮬레이션 비용과 공유 VM 스케줄링 잡음이 얹혀 측정이 묻혔다. 교훈: **uprobe로 µs 단위 함수를 재면 probe 비용이 신호보다 크다.** 실제 HW에서도 uprobe 한 번은 µs 단위라 같은 문제가 작게 나타난다. 반복 실행 평균(`@us`)도 순서가 뒤죽박죽인 것은 샘플 2개짜리 평균이라서다.

### 7.3 USDT — 앱이 심어 둔 정적 지점 — 예제 18

**무엇을 확인하나**: 바이너리 안의 USDT 지점 목록, 그리고 USDT 인자를 실제로 읽어 본다. 두 빌드(`m7_app`: 첫 인자가 `[sp]`, `m7_app_fix`: 첫 인자가 레지스터 `x23`)를 비교한다.

```sh
bpftrace -l 'usdt:/root/m7/m7_app:*'
```

```text
Parse error:
    -4@x1 -4@[x0, #:lo12:.LANCHOR2]
------------------^
...
usdt:/root/m7/m7_app:libc:cond_broadcast
usdt:/root/m7/m7_app:libc:cond_signal
usdt:/root/m7/m7_app:libc:cond_wait
...
usdt:/root/m7/m7_app:libc:wrlock_entry
usdt:/root/m7/m7_app:m7:done
usdt:/root/m7/m7_app:m7:request
```

(`...`는 생략. 실제 목록은 `libc:*` 50개 + `m7:*` 2개였고, 앞에 `Parse error`가 4번 나왔다. 정적 링크로 들어온 **glibc 내부 USDT 지점**의 인자 표현(`:lo12:` 재배치 심볼)을 bpftrace 0.20이 해석하지 못한 것이다 — 우리 `m7:*` 지점과는 무관하다.)

```sh
for f in m7_app m7_app_fix; do echo "== $f"
  bpftrace -B line -e "usdt:/root/m7/$f:m7:request {
    printf(\"request id=%d len=%d tag=%s\n\", arg0, arg1, str(arg2)); }" -c "/root/m7/$f 3" 2>&1 \
    | grep -v -E "Parse error|LANCHOR|\^$"
done
```

```text
== m7_app
Attaching 1 probe...
request id=-363268256 len=1 tag=req-0
request id=-363268256 len=5 tag=req-1
request id=-363268256 len=9 tag=req-2
m7_app: 3 requests done


== m7_app_fix
Attaching 1 probe...
request id=0 len=1 tag=req-0
request id=1 len=5 tag=req-1
request id=2 len=9 tag=req-2
m7_app: 3 requests done
```

**출력에서 볼 것**: 두 번째 인자 `len`(`-4@[sp, 4]`)과 세 번째 `tag`(`8@x19`)는 두 빌드 모두 맞다. 그런데 첫 인자가 `-4@[sp]`(오프셋 없는 sp 메모리)인 `m7_app`에서는 **id가 쓰레기 값**이다 — 이 랩에서 관찰한 bpftrace 0.20.2의 arm64 USDT 인자 해석 문제로 보인다(같은 `[sp, 4]`는 맞게 읽으므로 "오프셋 없는 `[sp]`" 형태가 문제). 인자를 레지스터에 있는 값으로 넘긴 `m7_app_fix`는 0, 1, 2로 정확하다. 교훈 두 가지: **(1) USDT 인자는 단순한 스칼라 지역 변수로 넘기고, (2) 새 도구 조합은 값이 아는 대로 나오는지 먼저 검증한다**(펌웨어에서 로직 분석기 디코더를 믿기 전에 알려진 패턴으로 확인하던 것과 같다). 커널 uprobe 자체 문제가 아니라 유저 공간 도구의 인자 해석 문제라는 점도 구분하자 — 같은 노트를 libbpf의 `usdt.bpf.h`로 읽으면 다르게 동작할 수 있다(이 랩에서 확인하지 않음).

---

## 8. `bpftool` — 지금 커널에 무엇이 붙어 있나

bpftrace·bcc·libbpf 무엇으로 올렸든 결과는 커널 안의 **prog·map·link 객체**다. `bpftool`은 그것을 직접 보는 도구다. 현장 장비에서 "누가 무엇을 붙여 놨나?"(보안 감사, 성능 저하 원인)를 확인할 때도 쓴다.

### 8.1 bpftrace가 올린 프로그램 들여다보기 — 예제 19

**무엇을 확인하나**: bpftrace로 `kprobe:m7_do_work`와 `kretprobe:vfs_read`를 건 상태에서, 프로그램·변환된 바이트코드(xlated)·map·link·perf attach·커널 심볼을 본다.

```sh
insmod ./m7_lat.ko
bpftrace -e 'BEGIN { printf("READY\n"); } kprobe:m7_do_work { @c = count(); }
  kretprobe:vfs_read { @r = hist(retval); }' > bg.out 2>&1 &      # READY까지 기다림
bpftool prog show name m7_do_work; bpftool prog list | grep -c "^[0-9]"
bpftool prog dump xlated name m7_do_work
```

```text
28: kprobe  name m7_do_work  tag 8489de36aaac1fb4  gpl
	loaded_at 2026-10-18T07:44:06+0000  uid 0
	xlated 256B  jited 240B  memlock 4096B  map_ids 5
	btf_id 7
	pids bpftrace(950)
13
int64 kprobe_m7_do_work(int8 * var1):
   0: (b7) r1 = 0
   1: (7b) *(u64 *)(r10 -16) = r1
   2: (18) r1 = map[id:5]
   4: (bf) r2 = r10
   5: (07) r2 += -16
   6: (07) r1 += 312
   7: (61) r0 = *(u32 *)(r2 +0)
   8: (35) if r0 >= 0x1 goto pc+5
   9: (67) r0 <<= 3
  10: (0f) r0 += r1
  11: (79) r0 = *(u64 *)(r0 +0)
  12: (bf) r0 = &(void __percpu *)(r0)
  13: (05) goto pc+1
  14: (b7) r0 = 0
  15: (15) if r0 == 0x0 goto pc+4
  16: (79) r1 = *(u64 *)(r0 +0)
  17: (07) r1 += 1
  18: (7b) *(u64 *)(r0 +0) = r1
  19: (05) goto pc+10
  20: (b7) r1 = 1
  21: (7b) *(u64 *)(r10 -8) = r1
  22: (18) r1 = map[id:5]
  24: (bf) r2 = r10
  25: (07) r2 += -16
  26: (bf) r3 = r10
  27: (07) r3 += -8
  28: (b7) r4 = 1
  29: (85) call array_map_update_elem#373216
  30: (b7) r0 = 0
  31: (95) exit
```

**출력에서 볼 것**: `@c = count()` 한 줄이 32개 명령으로 컴파일됐다. 흥미로운 것은 **verifier가 고쳐 쓴 흔적**이다. 원래 `bpf_map_lookup_elem` helper 호출이던 자리(명령 6–14)가 per-CPU 배열의 주소 계산으로 **인라인**됐다(`r1 += 312`, `if r0 >= 0x1`(키 범위 검사), `&(void __percpu *)` — per-CPU 포인터 변환). 이게 xlated("translated") 덤프가 원본 바이트코드와 다른 이유다. 시스템 전체 프로그램 13개 중 2개가 이 스크립트의 probe이고, 나머지는 다른 주체가 올린 것이다(systemd도 cgroup용 BPF 프로그램을 올린다 — `bpftool prog list`의 `pids` 줄로 주인을 확인한다). `jited 240B` — arm64 네이티브 코드 240바이트.

```sh
bpftool map list | grep -A1 -E "^[0-9]+: (hash|array|percpu)" | head -12
bpftool perf list | head -6
grep -E "bpf_prog_.*m7_do_work|bpf_trampoline" /proc/kallsyms | head -4
```

```text
5: percpu_array  name AT_c  flags 0x0
	key 4B  value 8B  max_entries 1  memlock 336B
--
6: percpu_hash  name AT_r  flags 0x0
	key 8B  value 8B  max_entries 4096  memlock 394176B
--
8: array  name ringbuf_loss_co  flags 0x0
	key 4B  value 8B  max_entries 1  memlock 320B
pid 950  fd 8: prog_id 27  kretprobe  func vfs_read  offset 0
pid 950  fd 9: prog_id 28  kprobe  func m7_do_work  offset 0
ffffd5275186c588 T arch_bpf_trampoline_size
ffffd5275186c650 T arch_alloc_bpf_trampoline
ffffd5275186c688 T arch_free_bpf_trampoline
ffffd5275186c6b8 T arch_protect_bpf_trampoline
```

**출력에서 볼 것**: bpftrace의 `@c`는 `AT_c`라는 **percpu_array**, `@r`(hist)은 `AT_r` **percpu_hash**가 됐다 — CPU마다 따로 세고 읽을 때 합치므로 락이 없다(SSD 펌웨어에서 코어별 통계 카운터를 두고 나중에 합치던 것과 같다). `bpftool perf list`는 **어느 프로세스가 어느 함수에 kprobe를 걸었는지** 보여 준다 — 현장에서 "누가 붙였나"를 찾는 명령이다. `bpftool link list`는 이 bpftrace 0.20이 perf_event 방식으로 붙여서 비어 있었다(출력 생략). kallsyms에는 arm64 trampoline 함수(`arch_*_bpf_trampoline`)가 있다 — fentry용 trampoline 인프라가 이 커널에 있다는 증거다.

### 8.2 JIT 코드 덤프는 왜 안 됐나

```text
Error: No JIT disassembly support
```

`bpftool prog dump jited`는 디스어셈블러로 LLVM이나 libbfd가 필요한데, 이 bpftool은 **정적 링크를 위해 `feature-llvm=0 feature-libbfd=0`으로 빌드**했기 때문이다(§12.3). 이것도 임베디드 트레이드오프다 — 타깃용 bpftool은 작게, JIT 코드 분석은 개발 PC의 full bpftool로. JIT 크기는 `prog show`의 `jited 240B`로 충분히 볼 수 있다.

---

## 9. bcc — 검증된 도구 모음과 그 대가

### 9.1 bcc는 어떻게 동작하나

bcc(BPF Compiler Collection)의 도구 하나는 **Python 스크립트 안에 C 문자열로 된 BPF 프로그램**이 들어 있는 구조다. 실행하면:

1. Python이 C 문자열에 옵션(PID 필터 등)을 텍스트 치환으로 넣는다.
2. `libbcc`가 내장 **libclang**으로 그 C를 컴파일한다 — 이때 **타깃 커널의 헤더**(`/lib/modules/$(uname -r)/build` 또는 `CONFIG_IKHEADERS`의 `/sys/kernel/kheaders.tar.xz`를 `/tmp`에 풀어서)를 include한다.
3. LLVM BPF 백엔드로 바이트코드를 만들고, `bpf()`로 로드·attach한다.
4. Python이 map·perf 버퍼를 읽어 출력한다.

즉 **도구를 시작할 때마다 타깃에서 컴파일러가 돈다.** 서버(수십 코어, 수백 GB RAM)에서는 1~2초라 아무도 신경 안 쓰지만, 작은 ARM 보드에서는 이게 전부다.

### 9.2 도구 지도 — 하위 시스템별

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="22" font-size="14" font-weight="bold">bcc / bpftrace 도구 지도 — 증상에서 도구로</text>
<rect x="20" y="40" width="200" height="104" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="32" y="62" font-size="13" font-weight="bold">프로세스 · 파일</text>
<text x="32" y="84" font-size="12">execsnoop · exitsnoop</text>
<text x="32" y="102" font-size="12">opensnoop · statsnoop</text>
<text x="32" y="120" font-size="12">filelife · vfsstat</text>
<text x="32" y="138" font-size="12">syscount · killsnoop</text>
<rect x="240" y="40" width="200" height="104" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="252" y="62" font-size="13" font-weight="bold">CPU · 스케줄러</text>
<text x="252" y="84" font-size="12">runqlat · runqlen</text>
<text x="252" y="102" font-size="12">runqslower · cpudist</text>
<text x="252" y="120" font-size="12">profile · offcputime</text>
<text x="252" y="138" font-size="12">wakeuptime · hardirqs</text>
<rect x="460" y="40" width="200" height="104" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="472" y="62" font-size="13" font-weight="bold">블록 · 파일시스템</text>
<text x="472" y="84" font-size="12">biolatency · biosnoop</text>
<text x="472" y="102" font-size="12">biotop · bitesize</text>
<text x="472" y="120" font-size="12">ext4slower · ext4dist</text>
<text x="472" y="138" font-size="12">cachestat · mdflush</text>
<rect x="20" y="160" width="200" height="104" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="32" y="182" font-size="13" font-weight="bold">메모리</text>
<text x="32" y="204" font-size="12">memleak · oomkill</text>
<text x="32" y="222" font-size="12">slabratetop</text>
<text x="32" y="240" font-size="12">drsnoop · compactsnoop</text>
<text x="32" y="258" font-size="12">shmsnoop</text>
<rect x="240" y="160" width="200" height="104" rx="6" fill="#888" fill-opacity="0.15" stroke="#888"/>
<text x="252" y="182" font-size="13" font-weight="bold">네트워크 (간단히)</text>
<text x="252" y="204" font-size="12">tcpconnect · tcpaccept</text>
<text x="252" y="222" font-size="12">tcplife · tcpretrans</text>
<text x="252" y="240" font-size="12">tcprtt · tcpdrop</text>
<text x="252" y="258" font-size="12">gethostlatency</text>
<rect x="460" y="160" width="200" height="104" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="472" y="182" font-size="13" font-weight="bold">범용 (임의 함수)</text>
<text x="472" y="204" font-size="12">funclatency · funccount</text>
<text x="472" y="222" font-size="12">trace · argdist</text>
<text x="472" y="240" font-size="12">stackcount · funcslower</text>
<text x="472" y="258" font-size="12">klockstat · deadlock</text>
<rect x="20" y="280" width="640" height="124" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="32" y="302" font-size="13" font-weight="bold">증상 → 첫 도구</text>
<text x="32" y="324" font-size="12">"가끔 늦다" → runqlat, offcputime, funclatency</text>
<text x="32" y="344" font-size="12">"디스크가 느리다" → biolatency, biosnoop</text>
<text x="32" y="364" font-size="12">"누가 계속 뭔가를 띄운다" → execsnoop · "설정을 못 읽는다" → opensnoop</text>
<text x="32" y="384" font-size="12">"메모리가 샌다" → memleak · "CPU가 바쁘다" → profile (+ perf, M6)</text>
</svg>
```
그림 4 — 이 rootfs의 `/usr/sbin/*-bpfcc` 128개 중 자주 쓰는 것들. Ubuntu는 이름 뒤에 `-bpfcc`를 붙인다(`execsnoop-bpfcc`). bpftrace에도 같은 이름의 `.bt` 버전이 39개 있다(`/usr/sbin/execsnoop.bt` 등). libbpf CO-RE로 다시 쓴 버전은 bcc 저장소의 `libbpf-tools/`에 있다.

### 9.3 bcc 도구 실행 — 예제 20 (execsnoop, opensnoop, trace)

**무엇을 확인하나**: 도구를 백그라운드로 띄우고, **헤더 줄이 나올 때까지(= 컴파일·로드·attach 완료)** 걸린 시간과 그 시점의 피크 RSS(`VmHWM`)를 잰 뒤, `ls`/`cat`/`dd` 부하를 주고 출력 앞부분을 본다. 측정 함수 `meas`는 아래와 같다(셸 스크립트, 게스트에서 실행 — 실제 스크립트에는 대기 횟수 상한과 `MemAvailable` 출력이 더 있어서 결과 줄 끝에 `MemAvail_kB=`가 붙는다).

```sh
meas() {   # meas <label> <ready-pattern> <cmd...>
  local label=$1 pat=$2; shift 2
  rm -f m.out; local t0=$(date +%s%N)
  "$@" > m.out 2>&1 & local pid=$!
  until grep -q -- "$pat" m.out 2>/dev/null; do sleep 0.1; kill -0 $pid 2>/dev/null || break; done
  local t1=$(date +%s%N)
  local hwm=$(awk '/VmHWM/{print $2}' /proc/$pid/status 2>/dev/null)
  load; sleep 1                                   # ls, cat, dd 를 3번
  local rss=$(awk '/VmRSS/{print $2}' /proc/$pid/status 2>/dev/null)
  kill -INT $pid; sleep 2; kill $pid 2>/dev/null; wait $pid 2>/dev/null
  printf "MEAS %-26s ready_s=%6.2f VmHWM_kB=%-7s VmRSS_kB=%-7s ...\n" "$label" \
    "$(awk -v a=$t0 -v b=$t1 'BEGIN{print (b-a)/1e9}')" "$hwm" "$rss"
  head -12 m.out
}
export PYTHONUNBUFFERED=1                            # 파이프로 보낼 때 Python 출력 버퍼링 끄기
meas execsnoop-bpfcc#1 PCOMM execsnoop-bpfcc
meas opensnoop-bpfcc PID opensnoop-bpfcc
meas trace-bpfcc PID trace-bpfcc 'do_sys_openat2 "%s", arg2'
```

```text
MEAS execsnoop-bpfcc#1          ready_s=164.56 VmHWM_kB=181308  VmRSS_kB=174588  MemAvail_kB=1757540
--- output (head)
PCOMM            PID     PPID    RET ARGS
date             1894    326       0 /usr/bin/date +%s%N
awk              1896    1895      0 /usr/bin/awk /VmHWM/{print $2} /proc/1018/status
awk              1897    326       0 /usr/bin/awk /MemAvailable/{print $2} /proc/meminfo
ls               1898    326       0 /usr/bin/ls /root
cat              1899    326       0  /etc/hostname
dd               1900    326       0 /usr/bin/dd if=/dev/vda of=/dev/null bs=4k count=500 iflag=direct
sleep            1901    326       0 /usr/bin/sleep 0.3
```

```text
MEAS opensnoop-bpfcc            ready_s=280.68 VmHWM_kB=182112  VmRSS_kB=180252  MemAvail_kB=1739536
--- output (head)
PID    COMM               FD ERR PATH
326    bash                3   0 /dev/null
4170   bash                3   0 /dev/null
4170   grep                3   0 /etc/ld.so.cache
4170   grep                3   0 /lib/aarch64-linux-gnu/libpcre2-8.so.0
4170   grep                3   0 /lib/aarch64-linux-gnu/libc.so.6
4170   grep               -1   2 /usr/lib/locale/locale-archive
4170   grep               -1   2 /usr/share/locale/locale.alias
```

```text
MEAS trace-bpfcc                ready_s= 39.91 VmHWM_kB=213220  VmRSS_kB=175140  MemAvail_kB=1748552
--- output (head)
PID     TID     COMM            FUNC             -
326     326     bash            do_sys_openat2   /dev/null
14255   14255   bash            do_sys_openat2   /dev/null
14255   14255   grep            do_sys_openat2   /etc/ld.so.cache
14255   14255   grep            do_sys_openat2   /lib/aarch64-linux-gnu/libpcre2-8.so.0
```

**출력에서 볼 것**: 

- **시작에 40초~4.7분**, 그 사이 Python 프로세스 RSS가 **177~208 MiB**까지 올라간다(`VmHWM`). 게스트 RAM이 2 GiB라 버텼지만, 512 MB 보드라면 도구 하나가 RAM의 40%를 쓴다. 시간 편차(같은 execsnoop이 165 s / 262 s)는 VM을 다른 16명이 같이 쓰는 부하 때문이다 — TCG 수치는 절대값이 아니라 "bcc는 분 단위, CO-RE는 초 단위"라는 **자릿수**만 보자.
- execsnoop에는 측정 스크립트 자신(`date`, `awk`)이 잡혔다 — 관찰 도구가 관찰 대상에 섞이는 고전적 현상. `cat`의 ARGS가 비어 있는 줄은 §5.5와 같은 이유(유저 페이지가 아직 없음)다.
- opensnoop의 `ERR 2`(ENOENT) 열이 "못 찾은 파일"을 바로 보여 준다.
- `trace-bpfcc`는 bpftrace 원라이너 같은 즉석 프로브를 bcc 문법으로 쓰는 도구다(`'함수 "포맷", 인자'`).

### 9.4 히스토그램 도구 — 예제 21 (biolatency, runqlat, funclatency, argdist)

```sh
meas biolatency-bpfcc Tracing biolatency-bpfcc
meas runqlat-bpfcc Tracing runqlat-bpfcc
meas funclatency-bpfcc Tracing funclatency-bpfcc -u vfs_read
meas argdist-bpfcc "\[" argdist-bpfcc -i 3 -n 1 -H 'r::vfs_read():int:$retval'
```

```text
MEAS biolatency-bpfcc           ready_s=419.40 VmHWM_kB=229720  VmRSS_kB=192036  MemAvail_kB=1728244
MEAS runqlat-bpfcc              ready_s=140.99 VmHWM_kB=250360  VmRSS_kB=210596  MemAvail_kB=1720960
MEAS funclatency-bpfcc          ready_s= 45.69 VmHWM_kB=140188  VmRSS_kB=137184  MemAvail_kB=1784036
MEAS argdist-bpfcc              ready_s= 28.04 VmHWM_kB=140676  VmRSS_kB=        MemAvail_kB=1794020
--- output (head)
[08:25:12]
     $retval             : count     distribution
         0 -> 1          : 38       |**************************              |
         2 -> 3          : 0        |                                        |
         4 -> 7          : 0        |                                        |
         8 -> 15         : 6        |****                                    |
        16 -> 31         : 19       |*************                           |
        32 -> 63         : 0        |                                        |
        64 -> 127        : 0        |                                        |
       128 -> 255        : 0        |                                        |
       256 -> 511        : 0        |                                        |
       512 -> 1023       : 57       |****************************************|
      1024 -> 2047       : 0        |                                        |
      2048 -> 4095       : 19       |*************                           |
```

(네 도구의 MEAS 줄만 모았고, 출력은 argdist 것만 남겼다. argdist 출력 맨 앞의 깨진 문자 한 줄(`sh: 1: ...: not found`)은 생략했다.)

**출력에서 볼 것**: 피크 RSS가 **137~244 MiB**. argdist는 3초 한 번 출력(`-i 3 -n 1`)하고 스스로 끝나서 `VmRSS`가 비어 있다. biolatency·runqlat은 Ctrl-C(SIGINT) 후 2초 안에 히스토그램을 다 찍지 못하고 강제 종료돼 출력이 없었다 — 느린 타깃에서는 **종료 처리(map 읽기 + Python 출력)도 느리다.** 그래서 아래처럼 `interval count` 인자로 도구가 스스로 끝나게 하는 편이 낫다.

```sh
( while true; do dd if=/dev/vda of=/dev/null bs=4k count=200 iflag=direct 2>/dev/null; sleep 0.5; done ) & LP=$!
T0=$(date +%s); timeout 1200 env PYTHONUNBUFFERED=1 biolatency-bpfcc 10 1 2>&1 | tail -25
echo "elapsed $(( $(date +%s) - T0 )) s"; kill $LP
```

```text
Tracing block device I/O... Hit Ctrl-C to end.

     usecs               : count     distribution
         0 -> 1          : 0        |                                        |
         2 -> 3          : 0        |                                        |
         4 -> 7          : 0        |                                        |
         8 -> 15         : 0        |                                        |
        16 -> 31         : 0        |                                        |
        32 -> 63         : 0        |                                        |
        64 -> 127        : 10       |                                        |
       128 -> 255        : 2992     |****************************************|
       256 -> 511        : 107      |*                                       |
       512 -> 1023       : 3        |                                        |
      1024 -> 2047       : 0        |                                        |
      2048 -> 4095       : 1        |                                        |
elapsed 104 s
```

**출력에서 볼 것**: `10 1`(10초 간격, 1회)로 주면 도구가 스스로 출력하고 끝난다. 전체 104초 중 측정은 10초, **나머지 94초가 시작(컴파일) 비용**이었다(이번 부팅은 VM 부하가 낮았다 — 앞의 419초와 비교). 분포는 §5.7 bpftrace 버전과 같은 tracepoint 기반이라 비슷하지만, 이번엔 VM이 조용해서 꼬리가 짧다. bcc는 2의 거듭제곱 구간을 `64 -> 127`처럼, bpftrace는 `[64, 128)`처럼 표기한다.

### 9.5 offcputime — "어디서 잠들었나" — 예제 22

**무엇을 확인하나**: `offcputime-bpfcc -K -f 15`는 15초 동안 태스크가 **CPU에서 내려와 있던 시간**을 커널 스택별로 합산해 folded 형식(`comm;frame;frame... 마이크로초`)으로 출력한다. on-CPU 프로파일(profile, perf)이 못 보는 "기다린 시간"을 보는 도구다. `m7_lat.ko`를 올려 둔 상태에서 실행했다(시작까지 수 분이 걸려서 `timeout 1200`으로 감쌌다).

```sh
insmod ./m7_lat.ko
timeout 1200 offcputime-bpfcc -K -f 15 > offcpu.txt 2>&1; wc -l offcpu.txt
grep -E "m7_worker|hackbench" offcpu.txt | sort -t" " -k2 -n | tail -5 | cut -c1-400
sort -k2 -n -t" " offcpu.txt 2>/dev/null | awk '{print $NF, substr($0,1,300)}' | sort -n | tail -4 | cut -c1-420
```

```text
22 offcpu.txt
m7_worker;ret_from_fork;kthread;m7_fn;msleep;schedule_timeout;schedule;finish_task_switch.isra.0 14636428
14461842 systemd;el0t_64_sync;el0t_64_sync_handler;el0_svc;do_el0_svc;el0_svc_common.constprop.0;invoke_syscall.constprop.0;__arm64_sys_epoll_pwait;do_epoll_pwait.part.0;do_epoll_wait;schedule_hrtimeout_range;schedule_hrtimeout_range_clock;schedule;finish_task_switch.isra.0 14461842
14586228 kworker/0:2;ret_from_fork;kthread;worker_thread;schedule;finish_task_switch.isra.0 14586228
14636428 m7_worker;ret_from_fork;kthread;m7_fn;msleep;schedule_timeout;schedule;finish_task_switch.isra.0 14636428
15008387 offcputime-bpfc;el0t_64_sync;el0t_64_sync_handler;el0_svc;do_el0_svc;el0_svc_common.constprop.0;invoke_syscall.constprop.0;__arm64_sys_ppoll;do_sys_poll;poll_schedule_timeout.constprop.0;schedule_hrtimeout_range;schedule_hrtimeout_range_clock;schedule;finish_task_switch.isra.0 15008387
```

**출력에서 볼 것**: 둘째 줄부터는 값(µs)을 앞에 붙여 정렬한 상위 4개다. `m7_worker`는 15초 중 **14.6초를 `msleep → schedule_timeout`에서** 잤다 — 코드 그대로다(5 ms sleep, 수백 µs 작업). systemd는 `epoll_pwait`에서, offcputime 자신은 `ppoll`에서 잤다. 센서 스레드가 "I2C 완료 대기(`wait_for_completion`)에서 잤나, mutex에서 잤나, page fault(`do_page_fault → ... → io_schedule`)에서 잤나"를 이렇게 가른다. folded 형식은 그대로 `flamegraph.pl`에 넣으면 **off-CPU flame graph**가 된다(M6). 유저 스택(`-U`)은 프레임 포인터 없는 바이너리에서 잘린다.

### 9.6 버전 불일치 — bcc가 새 커널 헤더를 컴파일하지 못한다 — 예제 23

**무엇을 확인하나**: 같은 측정을 `tcpconnect-bpfcc`에 했더니 도구가 시작하지 못했다.

```text
MEAS tcpconnect-bpfcc           ready_s= 62.72 VmHWM_kB=        VmRSS_kB=        MemAvail_kB=1835864
--- output (head)
In file included from /virtual/main.c:15:
In file included from include/net/sock.h:14:
In file included from include/linux/netdevice.h:26:
In file included from include/uapi/linux/neighbour.h:6:
In file included from include/linux/netlink.h:9:
In file included from include/net/scm.h:9:
In file included from include/linux/security.h:15:
include/linux/bpf.h:375:10: error: invalid application of 'sizeof' to an incomplete type 'struct bpf_wq'
  375 |                 return sizeof(struct bpf_wq);
      |                        ^     ~~~~~~~~~~~~~~~
include/linux/bpf.h:375:24: note: forward declaration of 'struct bpf_wq'
  375 |                 return sizeof(struct bpf_wq);
```

**출력에서 볼 것**: bcc 0.29.1(2024년, Ubuntu 24.04 패키지)의 C 코드가 **6.18 커널 헤더**(`include/linux/bpf.h`)를 include하다가 컴파일 에러로 죽었다. 62초 동안 컴파일러를 돌린 끝에 실패한 것이다. bcc는 "타깃 커널 헤더로 매번 컴파일"하므로, **커널은 새것인데 bcc 패키지는 옛것**이면 이런 일이 생긴다(execsnoop·opensnoop은 네트워크 헤더를 안 끌어와서 통과했다). CO-RE 도구는 커널 헤더를 쓰지 않고 BTF만 읽으므로 이 종류의 실패가 없다. 양산 장비는 커널을 BSP 버전에 고정하고 rootfs는 배포판을 따라가는 경우가 많아서, 이 조합 문제가 현실적으로 자주 생긴다.

### 9.7 시작 시간과 메모리 — bcc vs bpftrace vs libbpf

| 도구 | 형태 | 준비까지 (s, TCG) | 피크 RSS (MiB) | 출처 |
|---|---|---|---|---|
| `m7_count` | libbpf CO-RE 정적 | 약 0.6 | 2.6 / 3.3 | §10.4 (`time` 3.599 s − sleep 3 s), RSS는 실행 중 `/proc/<pid>/status` 2회 |
| bpftrace 원라이너 | bpftrace | 1.96 (warm) / 34.25 (drop_caches 직후) | 96 | MEAS |
| `biolatency.bt` | bpftrace | 5.51 | 112 | MEAS |
| `argdist-bpfcc` | bcc | 28.04 | 137 | MEAS |
| `trace-bpfcc` | bcc | 39.91 | 208 | MEAS |
| `funclatency-bpfcc` | bcc | 45.69 | 137 | MEAS |
| `runqlat-bpfcc` | bcc | 140.99 | 244 | MEAS |
| `execsnoop-bpfcc` | bcc | 164.56 / 261.93 | 177 / 179 | MEAS 2회 |
| `opensnoop-bpfcc` | bcc | 280.68 | 178 | MEAS |
| `biolatency-bpfcc` | bcc | 419.40 / 약 94 | 224 | MEAS / 예제 21 |
| `offcputime-bpfcc` | bcc | 959.47 | 157 | MEAS |

게스트에서 실측한 원본 줄(일부, bpftrace와 libbpf):

```text
MEAS bt-oneliner-cold           ready_s= 34.25 VmHWM_kB=98516   VmRSS_kB=98516   MemAvail_kB=1812556
MEAS bt-oneliner                ready_s=  1.96 VmHWM_kB=98292   VmRSS_kB=98292   MemAvail_kB=1806368
MEAS biolatency.bt              ready_s=  5.51 VmHWM_kB=114596  VmRSS_kB=114596  MemAvail_kB=1809632
MEAS offcputime-bpfcc           ready_s=959.47 VmHWM_kB=160284  VmRSS_kB=158460  MemAvail_kB=1753676
run 1: VmHWM: 2620 kB VmRSS: 1916 kB 
run 2: VmHWM: 3424 kB VmRSS: 3424 kB 
```

```svg
<svg viewBox="0 0 680 410" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="22" font-size="14" font-weight="bold">시작 시간(로그 축)과 피크 RSS — 같은 TCG 게스트에서 실측</text>
<text x="180" y="44" font-size="12" font-weight="bold">준비까지 걸린 시간 (s, log)</text>
<text x="480" y="44" font-size="12" font-weight="bold">피크 RSS (MiB)</text>
<text x="20" y="70" font-size="12">libbpf m7_count</text>
<rect x="180" y="58" width="48.6" height="16" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<text x="232.6" y="70" font-size="12">0.6</text>
<rect x="480" y="58" width="1.9" height="16" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<text x="485.9" y="70" font-size="12">3</text>
<text x="20" y="98" font-size="12">bpftrace (warm)</text>
<rect x="180" y="86" width="80.8" height="16" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="264.8" y="98" font-size="12">1.96</text>
<rect x="480" y="86" width="55.4" height="16" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="539.4" y="98" font-size="12">96</text>
<text x="20" y="126" font-size="12">biolatency.bt</text>
<rect x="180" y="114" width="108.8" height="16" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="292.8" y="126" font-size="12">5.51</text>
<rect x="480" y="114" width="64.6" height="16" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="548.6" y="126" font-size="12">112</text>
<text x="20" y="154" font-size="12">bpftrace (cold)</text>
<rect x="180" y="142" width="158.4" height="16" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="342.4" y="154" font-size="12">34.25</text>
<rect x="480" y="142" width="55.5" height="16" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="539.5" y="154" font-size="12">96</text>
<text x="20" y="182" font-size="12">bcc argdist</text>
<rect x="180" y="170" width="153.0" height="16" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="337.0" y="182" font-size="12">28.04</text>
<rect x="480" y="170" width="79.3" height="16" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="563.3" y="182" font-size="12">137</text>
<text x="20" y="210" font-size="12">bcc funclatency</text>
<rect x="180" y="198" width="166.2" height="16" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="350.2" y="210" font-size="12">45.69</text>
<rect x="480" y="198" width="79.0" height="16" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="563.0" y="210" font-size="12">137</text>
<text x="20" y="238" font-size="12">bcc runqlat</text>
<rect x="180" y="226" width="196.8" height="16" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="380.8" y="238" font-size="12">140.99</text>
<rect x="480" y="226" width="141.1" height="16" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="625.1" y="238" font-size="12">244</text>
<text x="20" y="266" font-size="12">bcc execsnoop</text>
<rect x="180" y="254" width="201.0" height="16" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="385.0" y="266" font-size="12">164.56</text>
<rect x="480" y="254" width="102.1" height="16" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="586.1" y="266" font-size="12">177</text>
<text x="20" y="294" font-size="12">bcc biolatency</text>
<rect x="180" y="282" width="226.4" height="16" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="410.4" y="294" font-size="12">419.4</text>
<rect x="480" y="282" width="129.4" height="16" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="613.4" y="294" font-size="12">224</text>
<text x="20" y="322" font-size="12">bcc offcputime</text>
<rect x="180" y="310" width="248.9" height="16" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="432.9" y="322" font-size="12">959.47</text>
<rect x="480" y="310" width="90.3" height="16" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="574.3" y="322" font-size="12">157</text>
<line x1="180" y1="338" x2="430" y2="338" stroke="currentColor"/>
<line x1="180.0" y1="338" x2="180.0" y2="343" stroke="currentColor"/><text x="180.0" y="356" font-size="12" text-anchor="middle">0.1</text>
<line x1="242.5" y1="338" x2="242.5" y2="343" stroke="currentColor"/><text x="242.5" y="356" font-size="12" text-anchor="middle">1</text>
<line x1="305.0" y1="338" x2="305.0" y2="343" stroke="currentColor"/><text x="305.0" y="356" font-size="12" text-anchor="middle">10</text>
<line x1="367.5" y1="338" x2="367.5" y2="343" stroke="currentColor"/><text x="367.5" y="356" font-size="12" text-anchor="middle">100</text>
<line x1="430.0" y1="338" x2="430.0" y2="343" stroke="currentColor"/><text x="430.0" y="356" font-size="12" text-anchor="middle">1000</text>
<line x1="480" y1="338" x2="630" y2="338" stroke="currentColor"/>
<line x1="480.0" y1="338" x2="480.0" y2="343" stroke="currentColor"/><text x="480.0" y="356" font-size="12" text-anchor="middle">0</text>
<line x1="537.7" y1="338" x2="537.7" y2="343" stroke="currentColor"/><text x="537.7" y="356" font-size="12" text-anchor="middle">100</text>
<line x1="595.4" y1="338" x2="595.4" y2="343" stroke="currentColor"/><text x="595.4" y="356" font-size="12" text-anchor="middle">200</text>
<text x="20" y="380" font-size="12">초록 libbpf CO-RE · 파랑 bpftrace · 주황 bcc (Python).</text>
<text x="20" y="398" font-size="12">TCG + 공유 VM 부하라 시간은 실제 HW보다 수십 배 크고 편차도 크다 — 자릿수만 읽자.</text>
</svg>
```
그림 5 — 시작 시간(로그 축)과 피크 RSS. libbpf CO-RE 도구는 bpftrace보다 시작이 3배 이상, bcc보다 50~1600배 빠르고, 메모리는 bpftrace의 약 1/30, bcc의 1/40~1/80이다.

**출력에서 볼 것**:

- **자릿수가 다르다**: CO-RE 1초 미만·3 MiB, bpftrace 수 초·100 MiB, bcc 수십 초~십수 분·137~244 MiB. bcc의 시간은 대부분 libclang이 커널 헤더(`/tmp/kheaders-6.18.55-trace`, 풀어 놓으면 **49 MB**)를 파싱·컴파일하는 시간이다.
- bpftrace도 LLVM으로 스크립트를 컴파일하지만 C 헤더 전체를 파싱하지 않고 BTF를 쓰므로 bcc보다 훨씬 빠르다. 다만 **콜드 스타트(34 s)** 는 LLVM 라이브러리 150 MB를 디스크에서 읽어 들이는 비용이다 — 느린 eMMC에서 그대로 드러난다.
- `m7_count`의 `MEAS` 측정은 실패했다: 정적 바이너리의 stdout이 파일로 갈 때 **full buffering**이라 "attached" 줄이 프로그램 끝에서야 써졌다(그래서 ready_s가 8초대로 나왔다). 측정 도구를 만들 때 `setvbuf(stdout, NULL, _IOLBF, 0)`나 `fflush`를 넣어야 하는 이유다. 그래서 위 표의 값은 `time` 결과와 실행 중 `/proc` 읽기로 대신했다.
- **RAM 512 MB 보드라면**: bcc 도구 하나가 RAM의 절반을 쓴다. 도구를 띄우는 순간 장비의 원래 작업(센서 처리)이 메모리 압박을 받는다 — 관찰이 대상을 바꾸는 최악의 경우다.

### 9.8 bpftrace 도구도 버전에 묶인다 — 예제 24

같은 측정에서 bpftrace 0.20.2의 `.bt` 도구 두 개가 6.18 커널에서 동작하지 않았다.

```text
/root/m7/b4.sh: line 8:   712 Aborted                 (core dumped) "$@" > m.out 2>&1
MEAS execsnoop.bt               ready_s=  8.47 VmHWM_kB=        VmRSS_kB=        MemAvail_kB=1855156
--- output (head)
bpftrace: ./src/types.cpp:271: bpftrace::SizedType bpftrace::CreateInteger(size_t, bool): Assertion `bits == 0 || bits == 1 || bits == 8 || bits == 16 || bits == 32 || bits == 64' failed.
```

```text
MEAS opensnoop.bt               ready_s=  5.87 VmHWM_kB=        VmRSS_kB=        MemAvail_kB=1850248
--- output (head)
/usr/sbin/opensnoop.bt:22-24: WARNING: tracepoint not found: syscalls:sys_enter_open
/usr/sbin/opensnoop.bt:28-30: WARNING: tracepoint not found: syscalls:sys_exit_open
Attaching 6 probes...
ERROR: Code not generated for probe: tracepoint:syscalls:sys_enter_open from: tracepoint:syscalls:sys_enter_open,tracepoint:syscalls:sys_enter_openat
```

**출력에서 볼 것**: (1) `execsnoop.bt`는 bpftrace 내부 assertion으로 **죽었다** — 2024년에 나온 bpftrace 0.20이 6.18 커널의 BTF/tracepoint 인자 중 처리 못 하는 정수 크기를 만난 것으로 보인다(원인은 이 랩에서 더 파지 않았다). (2) `opensnoop.bt`는 `sys_enter_open` tracepoint를 찾지 못했다 — **arm64에는 `open` syscall이 없다**(`openat`만 있다, L6에서 본 `mkdir`/`mkdirat`와 같은 이야기). x86에서 만든 스크립트를 arm64로 가져오면 흔히 생기는 일이다. 이 0.20 버전은 경고 뒤 에러로 끝냈다. 교훈: **트레이싱 도구 버전은 커널 버전과 짝을 맞춰라.** 같은 이유로 우리는 원라이너(§5)를 `sys_enter_openat`으로 썼다.

---

## 10. libbpf CO-RE — 타깃에서 컴파일하지 않는 도구 직접 만들기

### 10.1 BPF 쪽 — `m7_count.bpf.c`

**무엇을 확인하나**: `do_sys_openat2`에 kprobe를 걸고 (tgid, 부모 tgid, comm)별 open 횟수를 해시 map에 센다. 부모 tgid는 `BPF_CORE_READ(t, real_parent, tgid)`로 읽는다 — §3.3에서 본 CO-RE 재배치가 여기서 생긴다. 처음에는 키 구조체 이름을 `struct key`로 했다가 **vmlinux.h의 커널 keyring 구조체 `struct key`와 충돌해 컴파일 에러**가 났다(vmlinux.h는 커널의 모든 타입을 담으므로 흔한 이름은 피하고 접두어를 붙인다).

```c
// SPDX-License-Identifier: GPL-2.0
/* m7_count.bpf.c — CO-RE kprobe: count openat calls per (tgid, parent tgid) */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

struct m7_key { __u32 tgid; __u32 ppid; char comm[16]; };

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1024);
	__type(key, struct m7_key);
	__type(value, __u64);
} counts SEC(".maps");

SEC("kprobe/do_sys_openat2")
int BPF_KPROBE(m7_openat, int dfd, const char *filename)
{
	struct task_struct *t = (struct task_struct *)bpf_get_current_task();
	struct m7_key k = {};
	__u64 one = 1, *v;

	k.tgid = bpf_get_current_pid_tgid() >> 32;
	/* CO-RE: field offsets of task_struct are relocated at load time via BTF */
	k.ppid = BPF_CORE_READ(t, real_parent, tgid);
	bpf_get_current_comm(k.comm, sizeof(k.comm));

	v = bpf_map_lookup_elem(&counts, &k);
	if (v)
		__sync_fetch_and_add(v, 1);
	else
		bpf_map_update_elem(&counts, &k, &one, BPF_NOEXIST);
	return 0;
}

char LICENSE[] SEC("license") = "GPL";
```

### 10.2 유저 쪽 — skeleton을 쓰는 로더 `m7_count.c`

`bpftool gen skeleton`이 `.bpf.o`를 바이트 배열로 내장한 헤더(`m7_count.skel.h`)를 만들고, `m7_count_bpf__open_and_load()`·`__attach()`·`__destroy()` 함수와 `skel->maps.counts` 같은 접근자를 준다. 로더는 오브젝트 파일을 따로 들고 다닐 필요가 없다 — **바이너리 하나로 배포**된다.

```c
/* m7_count.c — libbpf loader using the generated skeleton; prints map after N s */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include "m7_count.skel.h"

struct m7_key { __u32 tgid; __u32 ppid; char comm[16]; };

int main(int argc, char **argv)
{
	int secs = argc > 1 ? atoi(argv[1]) : 3;
	struct m7_count_bpf *skel;
	struct m7_key k, next, *cur = NULL;
	__u64 v;
	int fd;

	skel = m7_count_bpf__open_and_load();	/* parse ELF, CO-RE relocate, verify, JIT */
	if (!skel) { fprintf(stderr, "open_and_load failed\n"); return 1; }
	if (m7_count_bpf__attach(skel)) { fprintf(stderr, "attach failed\n"); return 1; }
	printf("attached; counting openat for %d s\n", secs);
	sleep(secs);

	fd = bpf_map__fd(skel->maps.counts);
	printf("%-7s %-7s %-16s %s\n", "TGID", "PPID", "COMM", "OPENS");
	while (bpf_map_get_next_key(fd, cur, &next) == 0) {
		if (bpf_map_lookup_elem(fd, &next, &v) == 0)
			printf("%-7u %-7u %-16s %llu\n", next.tgid, next.ppid, next.comm,
			       (unsigned long long)v);
		k = next;
		cur = &k;
	}
	m7_count_bpf__destroy(skel);
	return 0;
}
```

### 10.3 빌드 — VM 호스트에서, 정적 링크로

libbpf는 커널 소스 `tools/lib/bpf`에 있고, bpftool을 빌드할 때 함께 정적 라이브러리(`libbpf.a`, v1.7)와 헤더가 만들어진다(§12.3). VM 호스트에 libelf·zlib·zstd의 `.a`가 있어서 완전 정적 링크가 된다.

```sh
B=/var/tmp/M7/bpftool; L=$B/libbpf
$B/bpftool btf dump file /var/lab/out/vmlinux-trace format c > vmlinux.h      # 242700줄
clang -g -O2 -target bpf -D__TARGET_ARCH_arm64 -I$L/include -c m7_count.bpf.c -o m7_count.bpf.o
$B/bpftool gen skeleton m7_count.bpf.o > m7_count.skel.h
gcc -std=gnu11 -Wall -Wextra -O2 -static -I. -I$L/include m7_count.c $L/libbpf.a -lelf -lz -lzstd -o m7_count
strip -o m7_count.stripped m7_count
llvm-strip -g -o m7_count.nodbg.o m7_count.bpf.o      # 비교용: DWARF만 제거
ls -la m7_count.bpf.o m7_count m7_count.stripped m7_count.nodbg.o
```

```text
-rwxrwxr-x 1 donh donh 4371704 Oct 17 23:43 m7_count
-rw-rw-r-- 1 donh donh  833768 Oct 17 23:43 m7_count.bpf.o
-rw-rw-r-- 1 donh donh   31192 Oct 17 23:43 m7_count.nodbg.o
-rwxrwxr-x 1 donh donh 2700936 Oct 17 23:43 m7_count.stripped
```

**출력에서 볼 것**: BPF 오브젝트가 814 KiB인 것은 `-g`로 넣은 DWARF와 BTF 때문이다(BTF는 CO-RE에 필수라 `-g`를 빼면 안 된다; `llvm-strip -g`로 DWARF만 지우면 31 KiB가 된다). 최종 배포물은 **strip한 정적 바이너리 2.6 MiB 하나**이고, 이 중 대부분은 glibc·libelf·libbpf다(musl로 링크하면 더 작아진다 — 가이드). clang도 Python도 커널 헤더도 타깃에 필요 없다.

### 10.4 게스트에서 실행 — 예제 25

```sh
( sleep 1; for i in 1 2 3 4; do cat /etc/hostname > /dev/null; done ) &
bash -c 'time ./m7_count 3'
```

```text
attached; counting openat for 3 s
TGID    PPID    COMM             OPENS
2916    2912    bash             1
2917    2912    cat              31
2916    2912    cat              31
2918    2912    bash             1
2919    2912    cat              31
2919    2912    bash             1
2918    2912    cat              31
2917    2912    bash             1

real	0m3.599s
user	0m0.222s
sys	0m0.186s
```

**출력에서 볼 것**: `real 3.599 s` 중 3초는 `sleep`이므로 **로드·CO-RE 재배치·verifier·JIT·attach가 약 0.6초**에 끝났다(같은 TCG 게스트에서 bpftrace는 빈 스크립트도 2.1초, 콜드 24초). 같은 tgid가 `bash`와 `cat` 두 줄로 나온 것은 `fork` 직후(아직 comm이 bash)에 1번, `exec` 후 `cat`으로 31번 열었기 때문이다 — 키에 comm을 넣으면 exec 전후가 갈린다. `cat` 하나가 31개 파일을 여는 것은 §5.5에서 본 로케일 파일들이다.

---

## 11. 오버헤드 — probe 하나가 이벤트마다 얼마를 더하나

### 11.1 측정 방법 — 예제 26

**무엇을 확인하나**: `getpid()`를 20만 번 부르는 루프의 호출당 시간을 probe 없음 / tracepoint / kprobe / fentry를 건 상태에서 비교한다. 유저 함수 호출(빈 함수) 루프에는 uprobe를 건다. 모든 probe는 `@ = count()`만 한다.

```c
/* m7_bench.c — ns per getpid() syscall or per user-function call (probe overhead) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>

__attribute__((noinline, used)) int m7_fn(int x)
{
	__asm__ volatile("" ::: "memory");
	return x + 1;
}

static double now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1e9 + ts.tv_nsec;
}

int main(int argc, char **argv)
{
	if (argc < 3) { fprintf(stderr, "usage: %s sys|fn N\n", argv[0]); exit(1); }
	long n = atol(argv[2]), acc = 0;
	int sys = strcmp(argv[1], "sys") == 0;
	double t0 = now_ns();

	for (long i = 0; i < n; i++)
		acc += sys ? syscall(SYS_getpid) : m7_fn((int)i);
	double t1 = now_ns();
	printf("%s: %ld calls, %.0f ns/call (acc %ld)\n", argv[1], n, (t1 - t0) / n, acc & 1);
	return 0;
}
```

같은 바이너리를 **VM 호스트(Linux 7.0, M2 위 가상화 — 거의 네이티브 속도)**에서 먼저 돌려 기준을 잡았다.

```text
sys: 200000 calls, 228 ns/call (acc 0)
fn: 2000000 calls, 1 ns/call (acc 0)
```

게스트(TCG)에서 probe 종류별로(각 2회):

```sh
./m7_bench sys 200000; ./m7_bench sys 200000
bpftrace -e 'tracepoint:syscalls:sys_enter_getpid { @ = count(); }' > /dev/null 2>&1 & P=$!
sleep 6; echo "[tracepoint attached]"; ./m7_bench sys 200000; ./m7_bench sys 200000; kill -INT $P; wait $P
# kprobe:__arm64_sys_getpid, kfunc:vmlinux:__arm64_sys_getpid, uprobe:/root/m7/m7_bench:m7_fn 도 같은 방식
```

```text
sys: 200000 calls, 60718 ns/call (acc 0)
sys: 200000 calls, 62206 ns/call (acc 0)
[tracepoint attached]
sys: 200000 calls, 91022 ns/call (acc 0)
sys: 200000 calls, 89540 ns/call (acc 0)
[kprobe attached]
sys: 200000 calls, 110117 ns/call (acc 0)
sys: 200000 calls, 114082 ns/call (acc 0)
[fentry attached]
sys: 200000 calls, 96814 ns/call (acc 0)
sys: 200000 calls, 98155 ns/call (acc 0)
```

```text
fn: 200000 calls, 28 ns/call (acc 0)
fn: 200000 calls, 15 ns/call (acc 0)
[uprobe attached]
fn: 200000 calls, 196974 ns/call (acc 0)
fn: 200000 calls, 199777 ns/call (acc 0)
```

```svg
<svg viewBox="0 0 680 280" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="22" font-size="14" font-weight="bold">getpid() 1회 비용 — probe 종류별 (QEMU TCG, µs/call)</text>
<text x="20" y="64" font-size="13">기준 (probe 없음)</text>
<rect x="170" y="46" width="204.9" height="26" fill="#888" fill-opacity="0.5" stroke="#888"/>
<text x="382.9" y="64" font-size="12">61.5</text>
<text x="20" y="104" font-size="13">tracepoint</text>
<rect x="170" y="86" width="300.9" height="26" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<text x="478.9" y="104" font-size="12">90.3  (+28.8)</text>
<text x="20" y="144" font-size="13">fentry (kfunc)</text>
<rect x="170" y="126" width="324.9" height="26" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="502.9" y="144" font-size="12">97.5  (+36.0)</text>
<text x="20" y="184" font-size="13">kprobe</text>
<rect x="170" y="166" width="373.7" height="26" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="551.7" y="184" font-size="12">112.1  (+50.6)</text>
<line x1="170" y1="206" x2="570" y2="206" stroke="currentColor"/>
<line x1="170.0" y1="206" x2="170.0" y2="211" stroke="currentColor"/><text x="170.0" y="225" font-size="12" text-anchor="middle">0</text>
<line x1="303.3" y1="206" x2="303.3" y2="211" stroke="currentColor"/><text x="303.3" y="225" font-size="12" text-anchor="middle">40</text>
<line x1="436.7" y1="206" x2="436.7" y2="211" stroke="currentColor"/><text x="436.7" y="225" font-size="12" text-anchor="middle">80</text>
<line x1="570.0" y1="206" x2="570.0" y2="211" stroke="currentColor"/><text x="570.0" y="225" font-size="12" text-anchor="middle">120</text>
<text x="20" y="250" font-size="12">TCG + 공유 VM 부하라 절대값은 실제 HW의 수백 배 — 순서만 읽자.</text>
<text x="20" y="268" font-size="12">uprobe는 같은 조건에서 빈 함수 호출 21 ns → 198 µs (약 1만 배) — 막대에 담을 수 없어 따로 적는다.</text>
</svg>
```
그림 6 — getpid() 한 번의 비용(게스트 TCG, 2회 평균). probe 비용의 순서는 tracepoint(+28.8 µs) < fentry(+36.0 µs) < kprobe(+50.6 µs)이고, uprobe는 다른 차원이다.

**출력에서 볼 것**:

- **TCG의 숫자는 실제 HW의 숫자가 아니다.** 같은 syscall이 VM 호스트에서 228 ns, TCG 게스트에서 61 µs — 약 270배다. 그래서 절대값 대신 **순서와 비율**만 읽는다. 실제 Cortex-A에서는 probe 하나가 이벤트당 대략 수십~수백 ns를 더한다는 것이 일반적인 경험치다(이 랩에서는 측정 불가 — KVM이 없다).
- **kprobe가 fentry보다 비싸다**: arm64 kprobe는 BRK 예외를 거치고(§2.1), fentry는 trampoline으로 직접 분기한다. tracepoint는 인자 구조체를 만드는 비용이 있지만 예외가 없다.
- **uprobe는 1만 배**: 빈 함수(15–28 ns)가 197 µs가 됐다. 유저 → BRK 예외 → 커널 → BPF → single-step(XOL) → 유저 복귀의 왕복이다. 초당 수십만 번 불리는 유저 함수(예: 센서 프레임 콜백 안의 작은 helper)에 uprobe를 걸면 앱이 멈춘 것처럼 느려진다 — **유저 쪽은 드문 지점에만, 가능하면 USDT로**.
- **엔지니어링 판단**: 이벤트율 × 이벤트당 비용 = CPU 비용. 초당 1만 이벤트 × 1 µs = CPU 1%. 실제 HW에서 runqlat·biolatency처럼 초당 수천~수만 이벤트 도구는 안전하고, 초당 수백만인 `kprobe:kmalloc` 같은 것은 짧게만 켠다.

---

## 12. 배포 형태별 footprint — 왜 bcc는 작은 타깃에 안 맞나

### 12.1 디스크 — 실제 패키지 크기

게스트 rootfs(Ubuntu 24.04 arm64)에서 `dpkg-query`로 설치 크기를 읽었다(VM 호스트에서 이미지를 mount해 chroot로 실행).

```sh
chroot mnt dpkg-query -W -f='${Package} ${Version} ${Installed-Size}\n' \
  bpftrace bpfcc-tools python3-bpfcc libbpfcc libllvm18 libllvm20 libclang-cpp18 libclang1-18 libbpf1 \
  python3.12-minimal libpython3.12-stdlib libelf1t64 libdw1t64 systemtap-sdt-dev
readelf -d mnt/usr/bin/bpftrace | grep NEEDED
readelf -d mnt/usr/lib/aarch64-linux-gnu/libbcc.so.0.29.1 | grep NEEDED
```

```text
bpfcc-tools 0.29.1+ds-1ubuntu7 2817
bpftrace 0.20.2-1ubuntu4.3 3139
libbpf1 1:1.3.0-2build2 424
libbpfcc 0.29.1+ds-1ubuntu7 2543
libclang-cpp18 1:18.1.3-1ubuntu1 63779
libclang1-18 1:18.1.3-1ubuntu1 33911
libdw1t64 0.190-1.1ubuntu0.1 754
libelf1t64 0.190-1.1ubuntu0.1 216
libllvm18 1:18.1.3-1ubuntu1 116457
libllvm20 1:20.1.2-0ubuntu1~24.04.3 135750
libpython3.12-stdlib 3.12.3-1ubuntu0.17 10471
python3-bpfcc 0.29.1+ds-1ubuntu7 220
python3.12-minimal 3.12.3-1ubuntu0.17 7757
systemtap-sdt-dev 5.0-2ubuntu1 64
 0x0000000000000001 (NEEDED)             Shared library: [libbpf.so.1]
 0x0000000000000001 (NEEDED)             Shared library: [libz.so.1]
 0x0000000000000001 (NEEDED)             Shared library: [libbcc_bpf.so.0]
 0x0000000000000001 (NEEDED)             Shared library: [libdw.so.1]
 0x0000000000000001 (NEEDED)             Shared library: [libclang-18.so.18]
 0x0000000000000001 (NEEDED)             Shared library: [libLLVM.so.18.1]
 0x0000000000000001 (NEEDED)             Shared library: [libstdc++.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [libgcc_s.so.1]
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [ld-linux-aarch64.so.1]
 0x0000000000000001 (NEEDED)             Shared library: [libclang-cpp.so.18.1]
 0x0000000000000001 (NEEDED)             Shared library: [libLLVM.so.18.1]
 0x0000000000000001 (NEEDED)             Shared library: [libelf.so.1]
 0x0000000000000001 (NEEDED)             Shared library: [libbpf.so.1]
 0x0000000000000001 (NEEDED)             Shared library: [libstdc++.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [libm.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [libgcc_s.so.1]
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [ld-linux-aarch64.so.1]
```

(크기 단위는 KiB. 앞 10줄 NEEDED가 bpftrace, 뒤 9줄이 libbcc다. `readelf`는 VM 호스트에서 mount한 이미지 파일에 직접 실행했다.)

```svg
<svg viewBox="0 0 680 350" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="22" font-size="14" font-weight="bold">타깃 디스크 footprint — 이 랩 rootfs의 실제 패키지 크기 (MB)</text>
<rect x="20" y="36" width="14" height="14" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/><text x="40" y="48" font-size="12">LLVM</text>
<rect x="130" y="36" width="14" height="14" fill="#d0564a" fill-opacity="0.5" stroke="#d0564a"/><text x="150" y="48" font-size="12">clang</text>
<rect x="240" y="36" width="14" height="14" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/><text x="260" y="48" font-size="12">Python</text>
<rect x="350" y="36" width="14" height="14" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/><text x="370" y="48" font-size="12">도구·libbpf·libelf</text>
<text x="20" y="85" font-size="13">bcc (Python)</text>
<rect x="190.0" y="66" width="238.3" height="28" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="196.0" y="85" font-size="12">LLVM 18</text>
<rect x="428.3" y="66" width="130.5" height="28" fill="#d0564a" fill-opacity="0.5" stroke="#d0564a"/>
<text x="434.3" y="85" font-size="12">clang-cpp 18</text>
<rect x="558.8" y="66" width="37.3" height="28" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<rect x="596.1" y="66" width="12.7" height="28" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<text x="614.8" y="85" font-size="12" font-weight="bold">199.9</text>
<text x="20" y="133" font-size="13">bpftrace</text>
<rect x="190.0" y="114" width="238.3" height="28" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="196.0" y="133" font-size="12">LLVM 18</text>
<rect x="428.3" y="114" width="69.4" height="28" fill="#d0564a" fill-opacity="0.5" stroke="#d0564a"/>
<rect x="497.7" y="114" width="14.5" height="28" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<text x="518.2" y="133" font-size="12" font-weight="bold">153.8</text>
<text x="20" y="181" font-size="13">bpftool (static)</text>
<rect x="190.0" y="162" width="14.3" height="28" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<text x="210.3" y="181" font-size="12" font-weight="bold">6.8</text>
<text x="20" y="229" font-size="13">libbpf CO-RE m7_count</text>
<rect x="190.0" y="210" width="5.4" height="28" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<text x="201.4" y="229" font-size="12" font-weight="bold">2.6</text>
<line x1="190.0" y1="258" x2="190.0" y2="263" stroke="currentColor"/><text x="190.0" y="277" font-size="12" text-anchor="middle">0</text>
<line x1="294.8" y1="258" x2="294.8" y2="263" stroke="currentColor"/><text x="294.8" y="277" font-size="12" text-anchor="middle">50</text>
<line x1="399.5" y1="258" x2="399.5" y2="263" stroke="currentColor"/><text x="399.5" y="277" font-size="12" text-anchor="middle">100</text>
<line x1="504.3" y1="258" x2="504.3" y2="263" stroke="currentColor"/><text x="504.3" y="277" font-size="12" text-anchor="middle">150</text>
<line x1="609.0" y1="258" x2="609.0" y2="263" stroke="currentColor"/><text x="609.0" y="277" font-size="12" text-anchor="middle">200</text>
<line x1="190" y1="258" x2="630.0" y2="258" stroke="currentColor"/>
<text x="20" y="303" font-size="12">출처: dpkg Installed-Size(bcc 0.29.1, bpftrace 0.20.2, LLVM 18.1.3), ls -l(정적 바이너리, strip).</text>
<text x="20" y="321" font-size="12">bcc는 여기에 커널 헤더(kheaders 압축 해제본)가 더 필요하다.</text>
</svg>
```
그림 7 — 타깃 디스크 footprint. bcc 약 200 MB, bpftrace 약 154 MB의 대부분은 LLVM(114 MB)과 clang 라이브러리다. libbpf CO-RE 도구는 2.6 MB, 정적 bpftool은 6.8 MB.

**출력에서 볼 것**: bpftrace와 libbcc 둘 다 `libLLVM.so.18.1`과 clang 라이브러리를 **직접 링크**한다. bcc는 `libclang-cpp`(62 MB)까지 쓰고 Python 3.12(18 MB)가 더 필요하다. 이 rootfs에는 다른 패키지가 끌어온 `libllvm20`(133 MB)도 따로 깔려 있었다 — 배포판 rootfs에서는 LLVM이 두 벌 들어가는 일도 흔하다. 4 GB eMMC에 A/B 파티션(L4)을 두면 슬롯당 2 GB 미만인데, 진단 도구 하나에 200 MB를 쓰는 것은 받아들이기 어렵다.

### 12.2 타깃용 bpftool을 정적으로 빌드하기 — 예제 27

게스트 rootfs의 Ubuntu `bpftool` 패키지는 설치에 실패했고(`/usr/sbin/bpftool`은 `linux-tools-$(uname -r)`를 찾는 **래퍼 스크립트**뿐이라 랩 커널에서는 동작하지 않는다), 커널 소스의 `tools/bpf/bpftool`을 VM 호스트에서 빌드했다. 랩 소스는 읽기 전용이므로 `tools/`, `include/`, `scripts/`, `arch/arm64/include` 등을 `/var/tmp/M7/src`로 복사해서 빌드했다(156 MB). 디스어셈블러(LLVM·libbfd)와 libcap은 끄고 `-static`으로 링크했다.

```sh
mkdir -p /var/tmp/M7/bpftool; cd /var/tmp/M7/src/tools/bpf/bpftool
make -j2 OUTPUT=/var/tmp/M7/bpftool/ EXTRA_LDFLAGS=-static \
     feature-llvm=0 feature-libbfd=0 feature-libcap=0 2>&1 | tail -8
```

```text
/usr/bin/aarch64-linux-gnu-ld.bfd: /usr/lib/gcc/aarch64-linux-gnu/15/../../../aarch64-linux-gnu/libcrypto.a(libdefault-lib-seed_src_jitter.o): in function `ossl_prov_acquire_entropy_from_jitter':
(.text+0x11c): undefined reference to `jent_entropy_collector_alloc'
/usr/bin/aarch64-linux-gnu-ld.bfd: (.text+0x130): undefined reference to `jent_read_entropy'
/usr/bin/aarch64-linux-gnu-ld.bfd: (.text+0x13c): undefined reference to `jent_entropy_collector_free'
/usr/bin/aarch64-linux-gnu-ld.bfd: /usr/lib/gcc/aarch64-linux-gnu/15/../../../aarch64-linux-gnu/libcrypto.a(libdefault-lib-seed_src_jitter.o): in function `jitter_instantiate':
(.text+0x61c): undefined reference to `jent_entropy_init_ex'
collect2: error: ld returned 1 exit status
make: *** [Makefile:254: /var/tmp/M7/bpftool/bootstrap/bpftool] Error 1
```

**출력에서 볼 것**: 6.18의 bpftool은 `-lcrypto`(OpenSSL — 프로그램 서명 기능)를 링크한다. Ubuntu 26.04의 정적 `libcrypto.a`는 jitterentropy 시드 소스를 참조하는데 그 정적 라이브러리(`libjitterentropy.a`)가 VM에 없었다. bpftool은 이 엔트로피 소스를 쓰지 않으므로, **실패를 리턴하는 스텁 4개**를 링크해 해결했다(정적 링크에서 흔한 우회 — 양산용이라면 jitterentropy를 정식으로 빌드해 넣는다).

```c
/* stubs so static libcrypto.a links without libjitterentropy.a (seed source unused) */
#include <stddef.h>
void *jent_entropy_collector_alloc(unsigned int o, unsigned int f) { (void)o; (void)f; return NULL; }
void jent_entropy_collector_free(void *c) { (void)c; }
long jent_read_entropy(void *c, char *d, size_t l) { (void)c; (void)d; (void)l; return -1; }
int jent_entropy_init_ex(unsigned int o, unsigned int f) { (void)o; (void)f; return -1; }
```

```sh
gcc -c -O2 jent_stub.c -o /var/tmp/M7/jent_stub.o
make -j2 OUTPUT=/var/tmp/M7/bpftool/ EXTRA_LDFLAGS="-static /var/tmp/M7/jent_stub.o" \
     feature-llvm=0 feature-libbfd=0 feature-libcap=0 2>&1 | grep -v "warning: Using\|in function" | tail -3
file /var/tmp/M7/bpftool/bpftool; ls /var/tmp/M7/bpftool/libbpf/
strip -o bpftool.s bpftool/bpftool; ls -la bpftool.s
```

```text
  GEN     /var/tmp/M7/bpftool/pid_iter.skel.h
  CC      /var/tmp/M7/bpftool/pids.o
  LINK    /var/tmp/M7/bpftool/bpftool
/var/tmp/M7/bpftool/bpftool: ELF 64-bit LSB executable, ARM aarch64, version 1 (GNU/Linux), statically linked, BuildID[sha1]=2ab07d10573187c21cec75e22ab0298825e55d67, for GNU/Linux 3.7.0, with debug_info, not stripped
bpf_helper_defs.h
include
libbpf.a
staticobjs
-rwxrwxr-x 1 donh donh 7168144 Oct 17 23:44 bpftool.s
```

**출력에서 볼 것**: strip 후 6.8 MiB 정적 바이너리. 빌드 과정에서 bpftool 자신도 `pid_iter.skel.h` 같은 **skeleton을 쓰는 CO-RE 프로그램**이라는 게 보인다(그래서 빌드에 clang이 필요하다 — 개발 호스트에만). 덤으로 `libbpf/libbpf.a`와 헤더가 생겨서 §10의 CO-RE 도구를 이걸로 링크했다. 게스트에서 `bpftool version`:

```text
bpftool v7.7.0
using libbpf v1.7
features: skeletons
```

`features`에 `llvm`이나 `libbfd`가 없으니 JIT 디스어셈블이 안 되는 것이다(§8.2). Yocto·Buildroot의 bpftool 레시피도 같은 선택지(디스어셈블러 포함 여부)를 가진다.

---

## 13. 안전성 — verifier가 거부하는 코드, 그리고 권한

### 13.1 verifier 로그 읽기 도구 — `m7_vload.c`

**무엇을 확인하나**: BPF 오브젝트를 libbpf로 열고, verifier 로그 레벨을 지정해 로드한 뒤, 걸린 시간과 로그 마지막 몇 줄을 찍는다. 레벨 1은 명령별 레지스터 상태를 모두, 레벨 4(`BPF_LOG_STATS`)는 통계만 준다.

```c
/* m7_vload.c — load a BPF object with a chosen verifier log level, print log tail + time */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <bpf/libbpf.h>

static char logbuf[1 << 20];		/* 1 MiB is plenty for level 4 (stats) */

static int quiet(enum libbpf_print_level l, const char *f, va_list a)
{
	(void)l; (void)f; (void)a;
	return 0;			/* we print the kernel log ourselves */
}

int main(int argc, char **argv)
{
	if (argc < 3) { fprintf(stderr, "usage: %s obj.o loglevel [tail_lines]\n", argv[0]); return 1; }
	int lvl = atoi(argv[2]), tail = argc > 3 ? atoi(argv[3]) : 8;
	struct bpf_object *obj = bpf_object__open_file(argv[1], NULL);
	struct bpf_program *p;
	struct timespec a, b;

	if (!obj) { perror("open"); return 1; }
	libbpf_set_print(quiet);
	bpf_object__for_each_program(p, obj) {
		bpf_program__set_log_level(p, lvl);
		bpf_program__set_log_buf(p, logbuf, sizeof(logbuf));
	}
	clock_gettime(CLOCK_MONOTONIC, &a);
	int err = bpf_object__load(obj);
	clock_gettime(CLOCK_MONOTONIC, &b);
	printf("load %s: err=%d (%s), %.2f s\n", argv[1], err, err ? strerror(-err) : "ok",
	       (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9);
	/* print the last <tail> lines of the verifier log */
	int n = 0;
	char *s = logbuf + strlen(logbuf);
	while (s > logbuf && n <= tail)
		if (*--s == '\n')
			n++;
	printf("%s", s == logbuf ? s : s + 1);
	bpf_object__close(obj);
	return 0;
}
```

거부될 프로그램 세 개는 한 파일(`m7_bad.bpf.c`)에 `#ifdef M7_OOB / M7_NULL / M7_LOOP`로 나눠 두고 `-DM7_OOB` 식으로 세 번 컴파일했다. 공통 머리말은 다음과 같다.

```c
// SPDX-License-Identifier: GPL-2.0
/* m7_bad.bpf.c — programs the verifier must reject (one per -DM7_xxx) */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u64[8]);		/* one 64-byte value: 8 counters */
} arr SEC(".maps");

char LICENSE[] SEC("license") = "GPL";
```

```sh
for v in OOB NULL LOOP; do
  clang -g -O2 -target bpf -D__TARGET_ARCH_arm64 -DM7_$v -I$L/include -c m7_bad.bpf.c -o m7_bad_$v.o; done
```

### 13.2 범위를 벗어난 map 접근 — 예제 28

```c
/* (1) out-of-bounds: index comes from the pid and is never bounds-checked */
SEC("kprobe/do_sys_openat2")
int m7_oob(void *ctx)
{
	__u32 zero = 0;
	__u64 *val = bpf_map_lookup_elem(&arr, &zero);
	__u32 idx = bpf_get_current_pid_tgid() & 0xff;	/* 0..255, but array is 8 */

	if (!val)
		return 0;
	val[idx] += 1;					/* verifier: invalid access */
	return 0;
}
```

```sh
./m7_vload m7_bad_OOB.o 1 6
```

```text
load m7_bad_OOB.o: err=-13 (Permission denied), 0.06 s
11: (67) r0 <<= 3                     ; R0=scalar(smin=smin32=0,smax=umax=smax32=umax32=2040,var_off=(0x0; 0x7f8))
12: (0f) r6 += r0                     ; R0=scalar(smin=smin32=0,smax=umax=smax32=umax32=2040,var_off=(0x0; 0x7f8)) R6=map_value(map=arr,ks=4,vs=64,smin=smin32=0,smax=umax=smax32=umax32=2040,var_off=(0x0; 0x7f8))
13: (79) r1 = *(u64 *)(r6 +0)
invalid access to map value, value_size=64 off=2040 size=8
R6 max value is outside of the allowed memory range
processed 13 insns (limit 1000000) max_states_per_insn 0 total_states 1 peak_states 1 mark_read 0
```

**출력에서 볼 것**: verifier는 `idx`의 **값 범위**를 추적했다. `& 0xff` 뒤 `umax=255`, `<<= 3`(× 8바이트) 뒤 `umax=2040`. map 값은 64바이트(`vs=64`)인데 오프셋이 최대 2040이 될 수 있으니 거부(`-EACCES`). 고치는 법은 `if (idx >= 8) return 0;` 또는 `idx &= 7;` — 그러면 `umax=56`이 되어 통과한다. 이것이 **"실행 시간 검사가 아니라 로드 시간 증명"** 이다. 펌웨어에서 정적 분석기(Coverity, Polyspace)가 "이 인덱스는 배열을 넘을 수 있다"고 경고하던 것과 같은데, 여기서는 경고가 아니라 **거부**다.

### 13.3 NULL 확인 누락 — 예제 29

첫 시도에서는 위와 같은 ARRAY map에 `val[0] += 1`을 NULL 확인 없이 했는데, **verifier가 통과시켰다**. 키가 상수 0이고 ARRAY map의 범위(`max_entries 1`) 안이라, 6.18의 verifier는 lookup 결과가 NULL일 수 없다고 증명해 버린 것이다(배열 map의 상수 키 lookup은 실패할 수 없다 — 최근 커널의 개선). 그래서 NULL이 실제로 가능한 **HASH map**으로 바꿨다.

```c
/* (2) missing NULL check: hash lookup may return NULL */
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 64);
	__type(key, __u32);
	__type(value, __u64);
} h SEC(".maps");

SEC("kprobe/do_sys_openat2")
int m7_nullchk(void *ctx)
{
	__u32 zero = 0;
	__u64 *val = bpf_map_lookup_elem(&h, &zero);

	*val += 1;					/* verifier: map_value_or_null */
	return 0;
}
```

```text
load m7_bad_NULL.o: err=-13 (Permission denied), 0.03 s
4: (18) r1 = 0xffff00000c835800       ; R1=map_ptr(map=h,ks=4,vs=8)
6: (85) call bpf_map_lookup_elem#1    ; R0=map_value_or_null(id=1,map=h,ks=4,vs=8)
; *val += 1;					/* verifier: map_value_or_null */ @ m7_bad.bpf.c:44
7: (79) r1 = *(u64 *)(r0 +0)
R0 invalid mem access 'map_value_or_null'
processed 7 insns (limit 1000000) max_states_per_insn 0 total_states 0 peak_states 0 mark_read 0
```

**출력에서 볼 것**: 리턴 레지스터 R0의 타입이 `map_value_or_null`이다. `if (!val)` 분기를 지나야 그 경로에서만 `map_value`로 바뀐다. 로그에 소스 줄(`@ m7_bad.bpf.c:44`)이 나오는 것은 `-g`로 넣은 BTF line info 덕분이다.

### 13.4 종료를 증명할 수 없는 루프 — 예제 30

```c
/* (3) loop bound comes from runtime data: verifier cannot prove termination */
SEC("kprobe/do_sys_openat2")
int m7_loop(void *ctx)
{
	__u32 zero = 0, n = bpf_get_current_pid_tgid();	/* unknown, up to 4G */
	__u64 *val = bpf_map_lookup_elem(&arr, &zero);

	if (!val)
		return 0;
	for (__u32 i = 0; i < n; i++)
		val[i & 7] += i;
	return 0;
}
```

```sh
./m7_vload m7_bad_LOOP.o 4 6        # 레벨 4: 통계만
```

```text
load m7_bad_LOOP.o: err=-7 (Argument list too long), 6.52 s
arg#0 reference type('UNKNOWN ') size cannot be determined: -22
BPF program is too large. Processed 1000001 insn
verification time 6505743 usec
stack depth 8 max 0
insns processed 1000001
processed 1000001 insns (limit 1000000) max_states_per_insn 4 total_states 9262 peak_states 9262 mark_read 0
```

**출력에서 볼 것**: 이 루프는 문법상 bounded(`i < n`)지만 `n`의 상한이 2^32-1이라, verifier가 반복을 하나씩 따라가다 **처리 명령 100만 개 한계**에 걸렸다(`-E2BIG`). verifier 혼자 6.5초를 썼다(TCG). 처음에 레벨 1(전체 로그)로 bpftool을 썼을 때는 로그가 25만 줄(19 MB)이 되고, 로그 끝(거부 사유)이 libbpf의 출력에서 잘려 보이지 않았다 — 그래서 레벨 4로 통계만 받는 `m7_vload`를 따로 만들었다. 실무 팁: **거부 사유는 보통 로그의 마지막 몇 줄에 있다**, 긴 로그는 `tail`로 본다. 고치는 법은 상한을 상수로 두거나(`for (i = 0; i < 64 && i < n; i++)`), 큰 반복은 `bpf_loop(n, cb, ctx, 0)` helper(5.17+)로 바꾸는 것이다 — `bpf_loop`은 반복 수와 무관하게 콜백 본문만 한 번 검증한다. 첫 줄 `arg#0 reference type('UNKNOWN ')`는 `void *ctx`로 선언해서 BTF 타입을 못 정한다는 정보성 메시지이고 거부 사유가 아니다(세 예제 모두 같은 줄이 나온다).

### 13.5 권한 — 누가 BPF를 로드할 수 있나

- `kernel.unprivileged_bpf_disabled`: 랩 값 2(§4.2). 비특권 사용자는 `bpf(BPF_PROG_LOAD)`를 못 한다. Spectre 계열 공격에 BPF JIT가 gadget으로 쓰일 수 있어서 메인라인 기본값이 금지(`BPF_UNPRIV_DEFAULT_OFF=y`)로 바뀌었다.
- 5.8부터 `CAP_SYS_ADMIN` 대신 **`CAP_BPF`**(로드·map) + **`CAP_PERFMON`**(트레이싱 프로그램, kprobe/perf_event) + 필요 시 `CAP_NET_ADMIN`(네트워크)으로 쪼갰다. 진단 데몬을 root 없이 돌릴 때 쓴다(L6의 capabilities와 같은 원리).
- **lockdown** `confidentiality`면 `bpf_probe_read_kernel` 등 커널 메모리를 읽는 helper가 막힌다(L6 §6). 양산·개발 빌드를 나누는 이유다.
- verifier 한계: 처리 명령 수 100만(`BPF_COMPLEXITY_LIMIT_INSNS`), 스택 512바이트, tail call 깊이 33. bounded loop는 5.3+, `bpf_loop()` helper는 5.17+ — 큰 반복은 `bpf_loop`로 쓴다.
- 그래도 verifier는 소프트웨어다. verifier 버그로 인한 CVE가 매년 나온다 — 비특권 BPF를 끄는 게 기본 방어선인 이유다.

---

## 14. 도구 비교 — printk vs ftrace vs perf vs bpftrace vs bcc vs libbpf

| 항목 | printk / dev_dbg (M1) | ftrace (M5) | perf (M6) | bpftrace | bcc | libbpf CO-RE |
|---|---|---|---|---|---|---|
| 코드 수정·재빌드 | 필요 (dynamic debug면 켜기만) | 불필요 | 불필요 | 불필요 | 불필요 | 도구는 개발 PC에서 빌드 |
| 재부팅 | 대개 필요 | 불필요 | 불필요 | 불필요 | 불필요 | 불필요 |
| 데이터 처리 위치 | 커널 → 로그 버퍼, 사람이 읽음 | 커널 링버퍼에 **전부 기록**, 사후 분석 | 샘플을 파일로, 사후 분석 | **커널 안에서 집계** | 커널 안에서 집계 + Python 후처리 | 커널 안에서 집계 + C 후처리 |
| 임의 함수·인자·리턴 | 넣은 곳만 | kprobe_events / fgraph retval | 제한적 (probe) | 쉬움 | 쉬움 | 쉬움 (코드량 많음) |
| 유저 함수 | 불가 | uprobe_events | uprobe | uprobe/USDT | uprobe/USDT | uprobe/USDT |
| 타깃 런타임 의존성 | 없음 | tracefs (커널만) | perf 바이너리 | LLVM+clang (~150 MB) | Python+LLVM+clang+헤더 (~200 MB) | 정적 바이너리 수 MB + BTF |
| 시작 시간 (이 랩, TCG) | 0 | 즉시 | 즉시 | 2 s (콜드 24 s) | 28 s ~ 16 min | 약 0.6 s |
| 이벤트당 오버헤드 | 포맷팅 비용 큼 | 작음 | 샘플링이라 조절 가능 | 작음 (§11) | 같음 | 같음 |
| 대표 용도 | bring-up, 드라이버 로그 | 지연·순서·함수 그래프 | CPU 핫스팟, PMU | 즉석 진단 원라이너 | 검증된 도구 모음 (서버) | 양산 장비 상주 진단 도구 |

---

## 15. 커널 안에서는 무슨 일이 일어나나

`bpftrace -e 'kprobe:m7_do_work { @c = count(); }'` 한 줄이 커널 안에서 거치는 경로를 따라가 보자(함수 이름은 6.18 기준).

```svg
<svg viewBox="0 0 680 430" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="20" y="22" font-size="14" font-weight="bold">로드 경로와 실행 경로 — kprobe에 붙은 BPF 프로그램</text>
<text x="20" y="46" font-size="13" font-weight="bold">로드 (한 번)</text>
<rect x="20" y="56" width="300" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="30" y="78" font-size="12">bpf(BPF_MAP_CREATE) → map fd</text>
<rect x="20" y="100" width="300" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="30" y="122" font-size="12">bpf(BPF_PROG_LOAD) → bpf_prog_load()</text>
<rect x="20" y="144" width="300" height="34" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="30" y="166" font-size="12">bpf_check() — verifier, 실패 시 log</text>
<rect x="20" y="188" width="300" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="30" y="210" font-size="12">bpf_prog_select_runtime() → JIT</text>
<rect x="20" y="232" width="300" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="30" y="254" font-size="12">perf_event_open(kprobe PMU) + attach</text>
<rect x="20" y="276" width="300" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="30" y="298" font-size="12">register_kprobe() → BRK 또는 ftrace</text>
<line x1="170" y1="90" x2="170" y2="98" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="170" y1="134" x2="170" y2="142" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="170" y1="178" x2="170" y2="186" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="170" y1="222" x2="170" y2="230" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="170" y1="266" x2="170" y2="274" stroke="currentColor" marker-end="url(#ah7)"/>
<text x="360" y="46" font-size="13" font-weight="bold">실행 (이벤트마다)</text>
<rect x="360" y="56" width="300" height="34" rx="4" fill="#888" fill-opacity="0.15" stroke="#888"/>
<text x="370" y="78" font-size="12">kthread가 m7_do_work() 호출</text>
<rect x="360" y="100" width="300" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="370" y="122" font-size="12">kprobe 핸들러 (kprobe_perf_func)</text>
<rect x="360" y="144" width="300" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="370" y="166" font-size="12">trace_call_bpf() → bpf_prog_run()</text>
<rect x="360" y="188" width="300" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="370" y="210" font-size="12">JIT 코드: map lookup, 카운트 증가</text>
<rect x="360" y="232" width="300" height="34" rx="4" fill="#888" fill-opacity="0.15" stroke="#888"/>
<text x="370" y="254" font-size="12">원래 함수 계속 실행</text>
<line x1="510" y1="90" x2="510" y2="98" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="510" y1="134" x2="510" y2="142" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="510" y1="178" x2="510" y2="186" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="510" y1="222" x2="510" y2="230" stroke="currentColor" marker-end="url(#ah7)"/>
<rect x="360" y="290" width="300" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="370" y="312" font-size="12">유저: bpf(BPF_MAP_LOOKUP_ELEM)</text>
<line x1="520" y1="266" x2="520" y2="288" stroke="currentColor" stroke-dasharray="4,3" marker-end="url(#ah7)"/>
<text x="20" y="350" font-size="12">fentry는 아래 두 칸 대신: bpf_trampoline_update() → ftrace direct call로</text>
<text x="20" y="370" font-size="12">함수 첫머리 NOP을 trampoline 분기로 바꾼다 (예외 없음, BRK 없음).</text>
<text x="20" y="400" font-size="12">tracepoint는: 소스에 심어진 trace_xxx() → __traceiter_xxx() → bpf_trace_run*() 경로.</text>
</svg>
```
그림 8 — 로드는 한 번, 실행은 이벤트마다. verifier와 JIT의 비용은 로드 때 한 번만 내고, 이벤트마다 내는 비용은 "훅 진입 + JIT 코드 몇십 명령"뿐이다. §5.6의 kmalloc 스택에 `__traceiter_kmalloc`이 보였던 것이 tracepoint 실행 경로의 흔적이다.

- `kernel/bpf/syscall.c`: `bpf()` syscall의 진입점. `BPF_PROG_LOAD` → `bpf_prog_load()` → `bpf_check()`(`kernel/bpf/verifier.c`) → `bpf_prog_select_runtime()`(`kernel/bpf/core.c`) → arch JIT `bpf_int_jit_compile()`(`arch/arm64/net/bpf_jit_comp.c`).
- `kernel/trace/bpf_trace.c`: tracing 프로그램 타입의 helper 목록(`kprobe_prog_func_proto`)과 `trace_call_bpf()`. kprobe·uprobe·tracepoint가 BPF를 부르는 곳이다.
- `kernel/bpf/trampoline.c`: fentry/fexit trampoline 관리(`bpf_trampoline_update()`), arm64 trampoline 코드 생성은 `arch_prepare_bpf_trampoline()`(`bpf_jit_comp.c`).
- `kernel/events/uprobes.c`: uprobe의 `BRK` 삽입·single-step(XOL 영역) 처리.
- map 구현: `kernel/bpf/hashtab.c`, `arraymap.c`, `ringbuf.c`, `stackmap.c`.

**드라이버 작성과 다시 만나는 지점**:

- 드라이버에 **tracepoint를 직접 정의**하면(`TRACE_EVENT()` 매크로, M5) bpftrace가 `tracepoint:m7drv:*`로 바로 쓴다. 함수 이름이 바뀌어도 도구가 깨지지 않는다 — 양산 드라이버에 "공식 관찰 지점"을 심는 방법이다.
- **`noinline`과 `static`**: 컴파일러가 인라인해 버린 함수에는 kprobe/fentry를 걸 수 없다. `m7_do_work()`에 `noinline`을 붙인 이유다. 반대로 `notrace`가 붙은 함수(인터럽트 진입 등)는 fentry 대상이 아니다.
- **BTF가 있는 모듈**: 모듈을 빌드할 때 빌드 트리에 vmlinux가 있고 pahole이 있으면 `BTF [M] m7_lat.ko`가 찍히며 모듈 BTF가 생긴다(§6.1). 이게 있어야 모듈 함수에 fentry를 걸고 `args->level`처럼 이름으로 인자를 읽는다.
- **error injection**: `ALLOW_ERROR_INJECTION(func, ERRNO)`가 붙은 함수는 `bpf_override_return()`으로 리턴값을 바꿔 에러 경로를 테스트할 수 있다(`CONFIG_BPF_KPROBE_OVERRIDE`, 랩 커널 y). 드라이버의 probe 실패 경로 테스트에 쓴다 — 가이드(이 노트에서 실행 안 함).

---

## 16. Embedded 실무에서 다시 보기

### 16.1 "센서 루프가 가끔 20 ms 늦는다" — 현장 진단 순서

로봇 센서 키트(i.MX8 / Jetson Orin)에서 IMU 읽기 스레드가 200 Hz 루프인데 가끔 한 주기를 놓친다. 재부팅하면 재현 조건이 사라지고, 드라이버에 printk를 넣으면 타이밍이 바뀐다. 이때의 순서:

1. **스케줄링 지연인가?** `runqlat`(§9, §5)으로 run-queue 대기 히스토그램을 본다. 꼬리가 수 ms면 CPU 경합이나 우선순위 문제(→ N2 SCHED_FIFO, CPU isolation).
2. **커널 안에서 막혔나?** `offcputime -p <pid>`로 그 스레드가 **어디서 잠들었는지** 커널 스택별 합계를 본다(I2C 전송 완료 대기? mutex? page fault?).
3. **드라이버 함수가 느린가?** `funclatency`나 bpftrace kprobe/kretprobe로 `i2c_transfer` 같은 함수의 지연 히스토그램(§6과 같은 방법).
4. **앱 쪽인가?** 앱에 USDT를 심어 두었다면 `usdt:...:sample_start/sample_done`으로 앱 레벨 지연, 아니면 uprobe.
5. 원인이 좁혀지면 그때 ftrace function_graph(M5)나 perf(M6)로 정밀하게 본다.

핵심은 **재부팅 없이, 이벤트를 다 쏟아내지 않고, 커널 안에서 요약한다**는 것이다. 200 Hz × 하루 = 1,700만 이벤트를 전부 기록하는 대신 히스토그램 한 장(수십 개 bucket)만 가져온다.

### 16.2 양산 장비에 무엇을 넣나

| 결정 | 개발 빌드 | 양산 빌드 |
|---|---|---|
| `CONFIG_DEBUG_INFO_BTF` | y | 가능하면 y (8 MB RAM·이미지 비용 vs 현장 진단 능력) |
| bpftrace / bcc | rootfs에 설치 | 넣지 않음 (§12 크기) |
| libbpf CO-RE 진단 도구 | 개발 중 | 몇 개를 정적 바이너리로 포함하거나, 필요 시 서명된 패키지로 push |
| lockdown | none | `integrity` 이상 — `confidentiality`면 BPF로 커널 메모리 읽기 금지 (L6) |
| 권한 | root | `CAP_BPF` + `CAP_PERFMON`만 가진 진단 서비스 (5.8+), `unprivileged_bpf_disabled=1` |

### 16.3 크로스 컴파일 — x86 개발 PC에서 arm64 장비용 CO-RE 도구 (가이드)

이 랩은 VM 자체가 arm64라 크로스 컴파일이 필요 없었지만, 실무에서는 x86 PC에서 빌드한다. BPF 오브젝트는 **아키텍처 중립**이라 `clang -target bpf`는 그대로이고, `-D__TARGET_ARCH_arm64`만 맞추면 된다(`BPF_KPROBE` 매크로가 `pt_regs` 레지스터 이름을 arm64식으로 고른다). 유저 로더만 크로스 컴파일한다.

```sh
# 가이드 (실행 안 함) — x86_64 호스트에서 arm64 타깃용
bpftool btf dump file vmlinux-target format c > vmlinux.h      # 타깃 커널 vmlinux 또는 장비의 /sys/kernel/btf/vmlinux 복사본
clang -g -O2 -target bpf -D__TARGET_ARCH_arm64 -c tool.bpf.c -o tool.bpf.o
bpftool gen skeleton tool.bpf.o > tool.skel.h                    # 호스트용 bpftool로 충분
aarch64-linux-gnu-gcc -O2 -static tool.c -I. -Ilibbpf/include \
    libbpf-arm64/libbpf.a -lelf -lz -lzstd -o tool               # 의존 라이브러리도 arm64용 .a
```

Yocto는 `meta-oe`에 `bpftrace`, `bcc`, `libbpf` 레시피가 있고, Buildroot는 `BR2_PACKAGE_LIBBPF`, `BR2_PACKAGE_BPFTOOL`이 있다(bcc/bpftrace는 LLVM 의존 때문에 이미지가 커진다). 이 랩에서는 무거운 Yocto 빌드를 하지 않았다.

### 16.4 Jetson에서

NVIDIA Jetson(L4T)은 Ubuntu 기반이라 `apt install bpftrace bpfcc-tools`가 되지만, **L4T 커널 config에 `CONFIG_DEBUG_INFO_BTF`가 켜져 있는지는 릴리스마다 다르다** — `ls /sys/kernel/btf/vmlinux`부터 확인한다. 없으면 bpftrace의 `kfunc:`/`args->`와 libbpf CO-RE 도구가 동작하지 않고, kprobe + 수동 오프셋이나 커널 재빌드가 필요하다. 배포판 커널을 바꿀 수 없는 경우를 위해 BTFHub(외부 BTF 파일) 방식도 있다. Jetson은 RAM이 8~64 GB라 bcc도 돌아가지만, 같은 제품군의 작은 보조 보드(512 MB, eMMC 4 GB)에서는 §12의 크기 문제가 바로 결정 요인이 된다.

---

## 17. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 장비 커널에 BTF가 없음 | bpftrace `kfunc:` / `args->` 오류, libbpf "failed to find valid kernel BTF" | `CONFIG_DEBUG_INFO_BTF` 미설정, 또는 빌드 호스트에 pahole 없음 | config 켜고 pahole 설치 후 재빌드. `ls /sys/kernel/btf/vmlinux`로 확인 |
| bcc를 작은 타깃에 설치 | rootfs가 200 MB 늘어나고 도구 하나 시작에 수십 초, OOM | 타깃에서 LLVM으로 C를 컴파일 | libbpf-tools(CO-RE) 정적 바이너리로 대체 |
| `kprobe:` 대상 함수가 인라인됨 | "Could not resolve symbol" 또는 호출이 안 잡힘 | 컴파일러가 인라인/이름 변경(`.isra`, `.constprop`) | `bpftrace -l 'kprobe:*name*'`로 실제 심볼 확인, tracepoint나 상위 함수 사용 |
| 커널 버전이 바뀌자 도구가 깨짐 | 함수가 없어지거나 인자 순서 변경 | kprobe는 내부 함수에 의존 | tracepoint 우선, CO-RE + `bpf_core_field_exists()`로 분기 |
| kretprobe에서 인자를 읽음 | 엉뚱한 값 | 리턴 시점 레지스터 x0..x7은 이미 덮였음 | 입구에서 `@arg[tid]`에 저장하거나 fexit 사용 |
| 고빈도 지점에 `printf` | 출력 폭주, 이벤트 손실(lost events), CPU 부하 | 이벤트마다 perf 버퍼로 문자열 전송 | `count()`/`hist()`로 커널 안에서 집계, 필터로 좁히기 |
| map 값을 NULL 확인 없이 접근 | 로드 거부 `R0 invalid mem access 'map_value_or_null'` | verifier가 lookup 결과를 NULL 가능으로 추적 | `if (!val) return 0;` (§13) |
| 배열 인덱스 범위 검사 누락 | 로드 거부 "invalid access to map value" | verifier가 인덱스 최댓값을 증명 못 함 | `if (idx >= N) return 0;` 또는 `idx & (N-1)` |
| uprobe를 초고빈도 유저 함수에 | 앱이 수십 배 느려짐 | uprobe마다 예외 + 커널 왕복 (§11) | USDT를 드문 지점에, 또는 샘플링(profile)으로 |
| lockdown `confidentiality` 장비에서 디버깅 | `bpf_probe_read_kernel` 실패, kprobe 거부 | 커널 메모리 읽기 차단 (L6) | 개발 빌드에서 진단하거나 lockdown 정책을 빌드 종류로 분리 |

---

## 18. 면접에서 이렇게 말한다

**Q.** "A production device has occasional latency spikes. You can't reboot it and you can't add printk. What do you do?"

**A.** 재부팅·재빌드 없이 되는 eBPF로 계층을 좁혀 간다. 먼저 BTF·kprobe가 커널에 있는지 확인하고, run-queue 지연(runqlat)으로 스케줄링 문제인지 보고, 해당 스레드의 off-CPU 스택(offcputime)으로 어디서 잠드는지 보고, 의심 함수에 kprobe/kretprobe(또는 fentry/fexit)로 지연 히스토그램을 건다. 이벤트를 전부 내보내지 않고 커널 안에서 히스토그램으로 집계하니까 오버헤드가 작고 재현 조건을 건드리지 않는다. 원인이 좁혀지면 ftrace function_graph나 perf로 정밀 분석한다. 작은 장비면 bcc 대신 미리 빌드한 libbpf CO-RE 정적 바이너리를 쓴다.

> I'd use eBPF because it needs neither a reboot nor a rebuild. First I check the kernel has BTF and kprobes. Then I narrow it down layer by layer: runqlat to see if it's scheduling delay, offcputime on the affected thread to see where it blocks, then kprobe/kretprobe or fentry/fexit on the suspect driver function to get a latency histogram. Everything is aggregated in-kernel into histograms, so the overhead is small and I don't disturb the timing I'm trying to observe. Once I've localized it, I switch to ftrace function_graph or perf for detail. On a small target I'd ship a prebuilt libbpf CO-RE static binary instead of bcc.

**꼬리 질문**: "오버헤드는 얼마나 되나?" → 이벤트당 수백 ns 수준(kprobe > fentry > tracepoint 순으로 비쌈), uprobe는 µs 단위라 고빈도 유저 함수에는 쓰지 않는다. 이 랩에서는 §11처럼 직접 쟀다.

**Q.** "Why is bcc a bad fit for a small embedded target?"

**A.** bcc는 도구를 실행할 때마다 타깃에서 C 소스를 LLVM/clang으로 컴파일한다. 그래서 타깃에 Python, libbcc, LLVM, clang 라이브러리, 커널 헤더가 다 있어야 하고, 이 랩 기준으로 디스크 200 MB가량, 도구 하나 시작에 QEMU 에뮬레이션 기준 30초~16분, 피크 RSS 137~244 MiB가 든다. libbpf CO-RE는 개발 PC에서 한 번 컴파일하고 BTF로 로드 시점에 재배치하니까, 타깃에는 수 MB짜리 정적 바이너리 하나와 커널 BTF만 있으면 된다.

> bcc compiles the BPF C code on the target every time a tool starts, using embedded LLVM and clang. So the target needs Python, libbcc, LLVM, clang libraries and kernel headers — around 200 MB of storage on my lab image — and each tool takes anywhere from half a minute to sixteen minutes under emulation to start with a peak RSS of 137 to 244 MiB. With libbpf CO-RE, I compile once on the build host; libbpf relocates field offsets at load time using the kernel's BTF, so the target only needs a few-megabyte static binary and CONFIG_DEBUG_INFO_BTF.

**꼬리 질문**: "그럼 bpftrace는?" → bpftrace도 내장 LLVM이 있어서 150 MB급이다. 개발 보드·Jetson처럼 여유 있는 장비의 즉석 진단용으로는 최고지만, 양산 이미지에는 CO-RE 도구가 맞다.

**Q.** "What does the eBPF verifier check, and why can't it just check at runtime?"

**A.** 로드 시점에 프로그램의 모든 경로를 레지스터 타입·값 범위를 추적하며 시뮬레이션한다. 메모리 접근이 범위 안인지, NULL 가능 포인터를 확인 후 쓰는지, 반드시 종료하는지(복잡도 한계 100만 명령), 프로그램 타입에 허용된 helper만 부르는지 본다. 런타임 검사는 커널의 핫패스(스케줄러, 네트워크 RX)에 비용을 더하고, 실패했을 때 할 수 있는 게 없다. 증명이 통과하면 JIT 코드는 검사 없이 네이티브 속도로 돈다.

> At load time it symbolically executes every path, tracking each register's type and value range. It proves memory accesses are in bounds, nullable pointers are checked before use, the program terminates within the complexity limit of one million instructions, and only helpers allowed for that program type are called. Runtime checks would add cost to hot kernel paths and there'd be no good way to fail; once proven, the JITed code runs at native speed.

**Q.** "kprobe vs fentry vs tracepoint — which do you use?"

**A.** tracepoint가 있으면 tracepoint — 안정적이고 버전이 바뀌어도 유지된다. 없으면 fentry/fexit — BTF로 타입이 붙고, 예외 없이 trampoline으로 들어가 kprobe보다 싸고, fexit에서 인자와 리턴값을 같이 본다. BTF가 없거나 함수 중간 오프셋이 필요하면 kprobe. arm64에서도 6.x 커널이면 fentry가 vmlinux와 모듈 함수 모두에 동작하는 걸 확인했다.

> Tracepoints first, because they're a stable interface. If there isn't one, fentry/fexit: typed arguments from BTF, a direct trampoline call instead of a breakpoint exception so it's cheaper, and fexit sees both arguments and return value. I fall back to kprobes when there's no BTF or I need a mid-function offset. On arm64 with a 6.x kernel, I've verified fentry works for both vmlinux and module functions.

**Q.** "What is CO-RE and what does it need from the kernel?"

**A.** Compile Once – Run Everywhere. clang이 구조체 필드 접근을 "타입 이름 + 필드 경로" 재배치 기록으로 남기고, libbpf가 로드 때 타깃 커널의 BTF(`/sys/kernel/btf/vmlinux`)에서 실제 오프셋을 찾아 패치한다. 커널에는 `CONFIG_DEBUG_INFO_BTF`가 필요하고, 빌드 호스트에는 pahole이 필요하다.

> Compile Once – Run Everywhere. Clang emits relocation records for each struct field access — type name plus field path — and libbpf patches the real offsets at load time from the target kernel's BTF in /sys/kernel/btf/vmlinux. The kernel needs CONFIG_DEBUG_INFO_BTF, which needs pahole on the build host.

**Q.** "How do you trace a user-space application function, and what does it cost?"

**A.** uprobe는 바이너리 오프셋에 브레이크포인트를 넣는다 — 그 함수가 불릴 때마다 예외, 커널 진입, BPF 실행, 복귀가 일어나서 호출당 µs 단위다. 앱을 고칠 수 있으면 USDT 프로브를 의미 있는 지점(요청 시작/끝)에 심어 둔다. 꺼져 있을 땐 NOP 하나라 비용이 거의 없고, 인자 위치가 ELF 노트에 적혀 있어서 도구가 정확히 읽는다.

> A uprobe replaces an instruction in the binary with a breakpoint, so every call costs an exception, a kernel entry, the BPF program and a return — microseconds per call. If I own the app, I add USDT probes at meaningful points like request start and end: they're a single NOP when not traced, and the ELF note tells the tool exactly where each argument lives.

**Q.** "How do you keep eBPF from becoming a security hole on a fielded device?"

**A.** 비특권 BPF는 끈다(`unprivileged_bpf_disabled=1`, 기본 config `BPF_UNPRIV_DEFAULT_OFF`). 진단 서비스는 root 대신 `CAP_BPF`+`CAP_PERFMON`만 준다. 양산 빌드는 lockdown `integrity` 이상, 기밀 데이터가 있으면 `confidentiality`로 커널 메모리 읽기를 막는다 — 그 대가로 현장 트레이싱이 제한되니까 개발/양산 빌드를 나눠 관리한다. BPF LSM으로 반대로 보안 정책을 구현할 수도 있다(L6).

> Disable unprivileged BPF, give diagnostic services CAP_BPF and CAP_PERFMON instead of full root, and run production kernels with lockdown in integrity mode, or confidentiality if there are secrets in kernel memory — accepting that this limits field tracing, so dev and production builds differ. BPF LSM can also be used the other way, to enforce policy.

---

## 19. 직접 해보기

1. (개념) kretprobe에서 `arg0`을 읽으면 왜 틀린 값이 나오나? fexit는 어떻게 이 문제를 피하나?
   정답/힌트: 리턴 시점에는 x0이 리턴값으로 덮였고 x1..x7도 보존되지 않는다. fexit는 trampoline이 입구에서 인자를 스택에 저장해 두었다가 원래 함수 호출 뒤에 프로그램에 함께 넘긴다.
2. (bpftrace) `tracepoint:sched:sched_switch`를 써서 CPU별로 "가장 오래 연속 실행된 태스크 이름과 시간"을 구하는 스크립트를 써라.
   정답/힌트: `@start[cpu] = nsecs; @prev[cpu] = args->next_comm` 저장, 다음 switch에서 `@max[@prev[cpu]] = max(nsecs - @start[cpu])`.
3. (모듈) `m7_lat.c`의 `udelay` 대신 `msleep`을 쓰면 kprobe/kretprobe 지연 히스토그램과 `offcputime-bpfcc` 결과가 어떻게 달라지나? 직접 바꿔 확인하라.
   정답/힌트: 지연은 ms 단위로 늘고(잠들어 있는 시간 포함), offcputime에 `m7_worker`의 `msleep → schedule_timeout` 스택이 크게 나타난다. udelay는 on-CPU busy-wait라 profile에 잡힌다.
4. (libbpf) `m7_count.bpf.c`를 `BPF_MAP_TYPE_RINGBUF`로 바꿔 파일 이름을 이벤트로 유저에 보내도록 고쳐라.
   정답/힌트: `bpf_ringbuf_reserve()` → `bpf_probe_read_user_str()` → `bpf_ringbuf_submit()`, 유저는 `ring_buffer__new()` + `ring_buffer__poll()`.
5. (verifier) §13의 OOB 예제를 **두 가지 방법**으로 통과시켜라(조건 분기, 비트 마스크). 각각 `bpftool prog dump xlated`로 결과 코드를 비교하라.
   정답/힌트: `if (idx >= 8) return 0;` 또는 `idx &= 7;` — 둘 다 verifier가 `umax_value=7`을 추적할 수 있다.
6. (관찰) `bpftool prog list`에서 bpftrace가 올린 프로그램의 `run_cnt`·`run_time_ns`를 보려면 무엇을 켜야 하나? 켠 뒤 이벤트당 평균 실행 시간을 구하라.
   정답/힌트: `sysctl kernel.bpf_stats_enabled=1` (게스트 안에서만) — 켜는 동안 약간의 오버헤드가 생긴다.
7. (설계) 512 MB RAM, 4 GB eMMC 센서 보드에 "현장 지연 진단 키트"를 넣는다면 무엇을 넣고 무엇을 뺄지, 커널 config와 rootfs 패키지 목록으로 써라.
   정답/힌트: 커널 BTF·kprobe·uprobe·BPF_EVENTS·JIT on, rootfs에는 bpftool + libbpf-tools 몇 개(runqlat, offcputime, biolatency, funclatency)의 정적 바이너리, bcc·bpftrace·LLVM 제외, CAP_BPF 서비스 계정, lockdown integrity.

---

## 20. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| eBPF | extended Berkeley Packet Filter | 커널 안에서 검증 후 실행되는 64비트 RISC 바이트코드와 그 생태계 |
| cBPF | classic BPF | 옛 32비트 패킷 필터 ISA. seccomp·tcpdump 필터가 사용 |
| verifier | 검증기 | 로드 시점에 모든 경로를 시뮬레이션해 안전성을 증명하는 커널 코드 |
| JIT | Just-In-Time 컴파일러 | 검증된 바이트코드를 arm64 네이티브 코드로 번역 |
| map | BPF map | 커널이 소유하고 BPF 프로그램과 유저가 공유하는 key/value 저장소 |
| helper | BPF helper | 번호가 UAPI로 고정된 커널 함수 목록 (`bpf_map_lookup_elem` 등) |
| kfunc | kernel function | BTF로 기술된 커널 함수를 직접 호출, 안정성 보장 없음 |
| BTF | BPF Type Format | 커널 타입 정보를 압축한 형식, `/sys/kernel/btf/vmlinux` |
| CO-RE | Compile Once – Run Everywhere | 필드 오프셋을 로드 시점에 BTF로 재배치하는 방식 |
| libbpf | — | BPF 오브젝트를 로드·재배치·attach하는 C 라이브러리 (커널 tools/lib/bpf) |
| skeleton | — | `bpftool gen skeleton`이 만든 헤더, 오브젝트를 내장하고 open/load/attach 함수를 제공 |
| kprobe | kernel probe | 커널 명령을 BRK/ftrace로 가로채는 동적 계측 |
| fentry/fexit | function entry/exit | BTF trampoline으로 함수 입구·출구에 붙는 tracing 프로그램 |
| tracepoint | — | 소스에 심어진 정적 계측 지점, 안정적 인터페이스 |
| uprobe | user probe | 유저 바이너리 오프셋에 거는 동적 계측 |
| USDT | User Statically-Defined Tracing | 앱이 `DTRACE_PROBE`로 심은 정적 지점 + ELF 노트 |
| bpffs | BPF filesystem | `/sys/fs/bpf`, 프로그램·map을 파일로 고정(pin)해 프로세스 수명과 분리 |
| off-CPU | — | 태스크가 실행되지 않고 잠들어 있던 시간 (블록·락·sleep) |
| runqlat | run-queue latency | 깨어난 뒤 실제로 CPU를 받을 때까지 기다린 시간 |
| lockdown | — | 커널 무결성/기밀성 보호 LSM, BPF의 커널 메모리 읽기를 막을 수 있음 (L6) |

---

## 21. 요약 & 체크리스트

eBPF는 실행 중인 커널의 거의 모든 지점(kprobe, fentry, tracepoint, uprobe, USDT, perf_event)에 작은 프로그램을 주입해, 이벤트를 커널 안에서 걸러 map에 요약하게 해 준다. 커널은 verifier로 안전을 증명한 프로그램만 JIT해서 붙이므로 운영 중인 장비에 써도 된다. 같은 커널 기능을 쓰는 세 가지 도구 형태가 있다: 즉석 진단에 최고인 bpftrace, 검증된 도구 모음인 bcc, 그리고 타깃에서 컴파일하지 않는 libbpf CO-RE. 이 랩에서 직접 잰 것처럼 bcc·bpftrace는 LLVM 때문에 디스크 150~200 MB와 큰 RAM·긴 시작 시간을 요구하고, CO-RE 정적 바이너리는 수 MB에 즉시 시작한다. 임베디드 장비에서는 커널 config(BTF, JIT, kprobe, uprobe)를 미리 준비하고 CO-RE 도구를 배포하는 것이 정답이다.

- [ ] eBPF 프로그램이 소스 → 바이트코드 → verifier → JIT → hook → map으로 가는 과정을 그림으로 설명할 수 있다
- [ ] verifier가 무엇을 증명하는지 세 가지 이상 말하고, 거부 메시지를 읽고 코드를 고칠 수 있다
- [ ] kprobe, fentry/fexit, tracepoint, uprobe, USDT, perf_event의 차이와 선택 기준을 말할 수 있다
- [ ] BTF와 CO-RE가 무엇이고 커널 config에 무엇이 필요한지 말할 수 있다
- [ ] bpftrace 원라이너로 syscall 수, 파일 open, kmalloc 크기 히스토그램, 블록 I/O 지연, run-queue 지연을 뽑을 수 있다
- [ ] 내 커널 모듈 함수의 지연 히스토그램을 kprobe/kretprobe와 fentry/fexit 두 방식으로 잴 수 있다
- [ ] 유저 프로그램 함수에 uprobe/uretprobe를, 앱에 USDT를 심고 인자를 읽을 수 있다
- [ ] `bpftool`로 로드된 프로그램·map·BTF를 보고 xlated/JIT 코드를 덤프할 수 있다
- [ ] bcc 도구(execsnoop, opensnoop, biolatency, offcputime)를 쓰고, 그 시작 비용·메모리를 bpftrace·libbpf와 숫자로 비교할 수 있다
- [ ] libbpf CO-RE 프로그램을 skeleton으로 빌드해 정적 바이너리로 타깃에 배포할 수 있다
- [ ] "재부팅도 printk도 안 되는 장비의 지연 스파이크" 질문에 단계별 진단 순서로 답할 수 있다
- [ ] 양산 장비에서 eBPF의 보안 설정(unprivileged 금지, CAP_BPF, lockdown)을 설명할 수 있다

## 참고 자료

- docs.kernel.org — BPF Documentation: https://docs.kernel.org/bpf/index.html (verifier, BTF, libbpf, kfuncs 장)
- docs.kernel.org — Kernel Probes (Kprobes): https://docs.kernel.org/trace/kprobes.html
- docs.kernel.org — Uprobe-tracer: https://docs.kernel.org/trace/uprobetracer.html
- man7.org — bpf(2): https://man7.org/linux/man-pages/man2/bpf.2.html
- Brendan Gregg, "BPF Performance Tools" (Addison-Wesley, 2019) — bcc/bpftrace 도구 대부분의 원 저자 설명
- bpftrace 레퍼런스 가이드: https://github.com/bpftrace/bpftrace (docs/ 디렉터리, 버전별 문법 차이 주의 — 이 랩은 0.20.2)
- bcc: https://github.com/iovisor/bcc (tools/, libbpf-tools/)
- libbpf-bootstrap (CO-RE 프로젝트 템플릿): https://github.com/libbpf/libbpf-bootstrap
- Andrii Nakryiko, "BPF CO-RE reference guide": https://nakryiko.com/posts/bpf-core-reference-guide/
- 커널 소스(elixir.bootlin.com, v6.18): `kernel/bpf/verifier.c`, `kernel/bpf/syscall.c`, `kernel/bpf/trampoline.c`, `kernel/trace/bpf_trace.c`, `arch/arm64/net/bpf_jit_comp.c`, `tools/bpf/bpftool/`, `tools/lib/bpf/`
