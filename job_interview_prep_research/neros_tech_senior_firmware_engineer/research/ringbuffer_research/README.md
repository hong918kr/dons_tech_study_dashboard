# 링버퍼 완전정복 — 레벨 0에서 5까지

가장 단순한 `%` 버전에서 시작해 **lock-free SPSC · 덮어쓰기 · zero-copy DMA** 까지,
한 레벨씩 손으로 올라가는 스터디 세트.

**7레벨 · 42문제 · 224체크 · 노트 8편.**
포맷은 `../../practice/` 와 동일하다 — `problems/`에 직접 채워넣고, `solutions/`로 답을 맞춰보고, `notes/`로 개념을 정리한다.

> **왜 링버퍼만 따로 파는가**: 임베디드 면접에서 "자료구조 하나만 완벽하게 물어본다면"의 답은
> 거의 항상 링버퍼다. 동적 할당 없이, 고정 시간 안에, 생산자·소비자의 속도 차이를 흡수하는
> 유일한 기본 부품이기 때문이다. 그리고 레벨 4 이후로는 **자료구조 문제가 아니라 동시성·메모리 모델
> 문제**로 바뀐다 — 시니어 라운드가 실제로 보는 지점이 거기다.

---

## 🚀 사용법

```sh
cd job_interview_prep_research/neros_tech_senior_firmware_engineer/research/ringbuffer_research

make list                    # 레벨 목록
make prob N=L0_modulo        # 연습 stub — // TODO 를 채우고 FAIL 이 PASS 로 바뀌는지
make sol  N=L0_modulo        # 정답 실행 (전부 PASS 확인)
make diff N=L0_modulo        # 내 답과 정답의 차이
make test                    # 전체 회귀 (정답 7개 컴파일 + 실행)
make notes                   # notes/*.md → html/ 재생성 + 스터디 대시보드 열기
make clean
```

노트는 브라우저로 읽는 쪽이 훨씬 낫다:

```sh
make notes          # 또는
open html/index.html
```

### 연습 루프 (하루 한 레벨)

1. 노트를 **읽는다** (10~20분).
2. 노트를 **덮고** `problems/LN_*.c` 의 `// TODO` 를 맨손으로 채운다.
3. `make prob N=...` → FAIL 이 남으면 **왜 FAIL 인지 먼저 추측**하고 고친다.
4. 전부 PASS 하면 `make diff N=...` 로 정답과 비교. 다르면 **어느 쪽이 나은지 근거를 대 본다.**
5. **다음 날 다시 맨손으로.** 두 번째에 30분 안에 못 짜면 아직 내 것이 아니다.

---

## 🪜 레벨 사다리

각 레벨은 **바로 앞 레벨의 약점을 하나씩** 없앤다. 그리고 그 대가로 새 문제를 하나씩 얻는다.

| 레벨 | `N` 값 | 무엇 | 없애는 약점 | 새로 생기는 대가 | 문제 |
|---|---|---|---|---|---|
| **0** | `L0_modulo` | 고정 크기 + `%` + 한 칸 희생 | — (출발점) | 슬롯 1개 낭비, 나눗셈 비용 | 5 |
| **1** | `L1_count` | `count` 필드 | 낭비되는 한 칸 | **양쪽이 count 를 갱신** → 무락이 막힌다 | 6 |
| **2** | `L2_mask` | 2의 거듭제곱 + `& mask` + free-running | 나눗셈·경합·낭비 **전부** | size 가 2의 거듭제곱으로 제한 | 6 |
| **3** | `L3_generic` | `void*`+`esz` / 매크로 생성 | `int` 전용 제약 | `memcpy` 오버헤드, **정렬(alignment)** | 6 |
| **4** | `L4_spsc` | `stdatomic` acquire/release | 크리티컬 섹션(인터럽트 지연) | 메모리 모델을 정확히 알아야 함 | 7 |
| **5a** | `L5a_overwrite` | 덮어쓰기 블랙박스 링 | "가득 차면 새 데이터 유실" | **생산자도 tail 을 민다** → 무락 붕괴 | 6 |
| **5b** | `L5b_zerocopy` | acquire/commit zero-copy | `memcpy` 자체 | 랩 분할, DMA 캐시 일관성 | 6 |

> **레벨 2가 진짜 분기점이다.** 실무 펌웨어 링버퍼의 90%는 레벨 2 구조에 필요한 만큼
> 레벨 3/4/5의 성질을 덧댄 것이다. 레벨 0·1은 **왜 레벨 2가 그 모양인지**를 이해하기 위한 계단이다.

---

## 📚 노트

| 노트 | 내용 |
|---|---|
| `00_overview.md` | 링버퍼가 뭐고 왜 1번 자료구조인가, 용어, 레벨 지도, **면접 단골 질문 8개** |
| `01_level0_modulo.md` | 한 칸 희생의 논리, `%` 의 실제 비용(M0 에서 20~40사이클), 인덱스를 `unsigned` 로 잡는 이유 |
| `02_level1_count.md` | `count++` 가 기계어 3단계인 것과 ISR 경합, 크리티컬 섹션, 벌크 2조각 `memcpy`, 부분성공 vs all-or-nothing |
| `03_level2_mask.md` | ⭐ 마스킹 + free-running, **인덱스 오버플로가 왜 안전한가**(C11 §6.2.5p9), 2의 거듭제곱의 대가 |
| `04_level3_generic.md` | `void*` vs 매크로, **정렬과 HardFault**, 구조체 패딩이 어디서 문제인가 |
| `05_level4_spsc.md` | ⭐⭐ ISR 에서 mutex/printf 가 안 되는 이유, `volatile` 이 왜 부족한가, **acquire/release 4줄**, 보수적 `used()`, 보존 법칙 |
| `06_level5_overwrite_zerocopy.md` | 덮어쓰기가 SPSC 를 깨는 이유와 해법 3가지, 레코드 단위 드롭, acquire/commit, bip-buffer, **DMA 함정 3가지**, circular DMA HT/TC |
| `07_pitfalls_mpmc.md` | ⭐ 실무 함정 12개, **MPMC 고민**(CAS 만으론 왜 부족한가 · Vyukov 시퀀스 링 · ABA), 락을 써야 할 때, 검증 전략 4가지 |

HTML 판(`html/index.html`)은 체크박스 진행률이 브라우저에 저장되고, 노트 간 이동·목차·읽기 진행 바가 붙는다.

---

## 🎯 면접에서 반드시 나오는 질문 8개

전부 답할 수 있으면 링버퍼는 끝난 것이다. (출처: `notes/00_overview.md` §5)

1. `head == tail` 이 empty 인지 full 인지 어떻게 구분하나? 방법 3가지와 각각의 대가는?
2. 왜 버퍼 크기를 2의 거듭제곱으로 잡나? 1000바이트가 필요하면 어떻게 하나?
3. 인덱스를 계속 증가시키면 오버플로가 나지 않나?
4. ISR 에서 `printf` 를 쓰면 왜 안 되나? **4가지** 이유를 대라.
5. `volatile` 만으로 ISR ↔ 메인 공유가 안전한가? 아니라면 무엇이 더 필요한가?
6. `count` 필드를 쓰면 왜 lock-free SPSC 가 깨지나?
7. 가득 찼을 때 새 데이터를 버릴 것인가, 오래된 데이터를 버릴 것인가? 판단 기준은?
8. 생산자가 둘 이상(MPMC)이면 무엇이 달라지나? CAS 하나로 되나?

---

## 📁 구조

```
research/ringbuffer_research/
├── README.md                 ← 지금 이 파일
├── Makefile                  ← make prob / sol / diff / test / notes
├── build_notes_html.py       ← notes/*.md → html/ (의존성 없음, Python 3.9+)
├── notes/                    ← 노트 원본 (md 가 원본, HTML 은 생성물)
│   ├── 00_overview.md
│   ├── 01_level0_modulo.md
│   ├── 02_level1_count.md
│   ├── 03_level2_mask.md
│   ├── 04_level3_generic.md
│   ├── 05_level4_spsc.md
│   ├── 06_level5_overwrite_zerocopy.md
│   └── 07_pitfalls_mpmc.md
├── problems/                 ← 맨손으로 채울 stub 7개 (// TODO)
├── solutions/                ← 정답 + 해설 주석 7개
├── html/                     ← make notes 로 생성 (직접 고치지 말 것)
└── build/                    ← 컴파일 산출물 (make clean 으로 삭제)
```

각 `.c` 는 **자체 `main()` 과 PASS/FAIL 하네스를 가진 독립 실행 파일**이다. 라이브러리도 링크도 없다.

---

## 🔗 관련

- `../../practice/` — Neros Senior Firmware Engineer 대비 7토픽 42문제.
  그중 `01_ring_logging` 은 **레벨 4의 로깅 응용판**이다 (deferred formatting, fmt_id, drain).
  이 폴더가 원리, 저쪽이 응용.
- `../../../anduril_firmware_engineer/` — `05_circular_buffer` 세트 (기본기 드릴)
- `../../neros_tech_senior_firmware_engineer_context.md` — 회사·적합도·예상 인터뷰

---

## ✅ 회귀 상태

```
make test  →  ==> ALL SOLUTIONS OK
```

정답 7개 전부 `cc -std=c11 -Wall -Wextra -O0 -g` 경고 0, **224체크 전부 PASS**.
(추가로 `-fsanitize=address,undefined` 에서도 clean — stub 상태에서 실행해도 크래시하지 않는다.)
