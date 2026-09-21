#!/usr/bin/env python3
"""data/*.json → README.md 생성 (세트 표/통계를 항상 최신으로)."""
import json, glob, os, re, subprocess

ROOT = os.path.dirname(os.path.abspath(__file__))
sets = []
for f in sorted(glob.glob(os.path.join(ROOT, "data", "[0-9]*.json"))):
    d = json.load(open(f, encoding="utf-8"))
    d["_file"] = os.path.basename(f)[:-5]
    sets.append(d)
sets.sort(key=lambda d: d.get("order", 0))

# 체크 수는 solutions 실행 결과에서 수집 (build 되어 있으면 재사용)
checks = {}
for s in sets:
    exe = os.path.join(ROOT, "build", s["_file"] + "_sol")
    if os.path.exists(exe):
        try:
            out = subprocess.run([exe], capture_output=True, text=True, timeout=90).stdout
            m = re.search(r"====\s*(\d+) passed,\s*(\d+) failed", out)
            if m:
                checks[s["_file"]] = int(m.group(1)) + int(m.group(2))
        except Exception:
            pass

nprob = sum(len(s.get("problems", [])) for s in sets)
ncheck = sum(checks.values())

rows = "\n".join(
    "| {order:02d} | {icon} {ko} | {n} | {rng} | {why} |".format(
        order=s.get("order", 0), icon=s.get("icon", ""), ko=s.get("title_ko", ""),
        n=len(s.get("problems", [])), rng=s.get("range", ""),
        why=(s.get("summary_ko", "").split(".")[0][:70]))
    for s in sets)

readme = f"""# Verkada 임베디드 면접 — C 문제 은행 & 스터디 대시보드

**Senior Embedded Linux Engineer, Connectivity (San Mateo)** 기술 세션 대비 문제 은행.
2026-09-15 리크루터(Davis) 안내 메일이 지정한 4개 주제 — thread safety · 동기화(mutex/
condvar/atomic) · 실시간 데이터 처리(double buffering) · 모듈화·테스트 가능한 설계 — 를
드릴 가능한 C 문제로 쪼갠 것이다. 형식은 `anduril_firmware_engineer/` 키트와 동일하다.

> 이 세션은 알고리즘 퍼즐이 아니라 **동시성 자료구조 구현 + 설계 설명**이다.
> 코드가 돌아가는 것만으로는 부족하고 *왜 이 설계인지*를 말로 설명해야 한다.

---

## 🚀 빠르게 시작

```sh
open index.html                     # 대시보드 (오프라인 동작, 진행률 자동 저장)

make list                           # 세트 목록
make prob N=02_condvar_queues       # 연습 stub 컴파일+실행 → 직접 구현 (FAIL→PASS)
make sol  N=02_condvar_queues       # 정답 컴파일+실행 (전부 PASS 확인)
make tsan N=02_condvar_queues       # ThreadSanitizer 로 레이스 검출
make test                           # 전 세트 회귀
make check                          # JSON 스키마 + 컴파일 + 실행 + stub 전체 검증
make dash                           # data/*.json → index.html 재생성
```

**연습 루프:** `problems/NN.c` 의 `// TODO` 를 구현 → `make prob N=NN` → FAIL 이 PASS 로
바뀌는지 확인 → 막히면 대시보드에서 hint 1→2→3 → 그래도 막히면 `solutions/NN.c` 를 본 뒤
**다시 맨손으로**.

---

## 🧩 문제 은행 ({len(sets)} 세트 · {nprob} 문제 · {ncheck} 체크)

| # | 세트 | 문항 | 범위 | 한 줄 |
|---|------|-----|------|-------|
{rows}

모든 정답은 `cc -std=c11 -Wall -Wextra` **경고 0 · 전 테스트 PASS**, 동시성 세트는
**ThreadSanitizer 클린**(의도된 race 데모 제외)으로 검증했다.

---

## 📁 구조

```
verkada_prep/
├── index.html                # ★ 자체완결형 스터디 대시보드
├── dashboard.template.html   #   템플릿 (데이터 주입 전)
├── build_dashboard.py        #   data/*.json → index.html
├── validate.py               #   전체 검증 (make check)
├── Makefile                  #   prob / sol / tsan / test / check / dash
├── problems/   NN_topic.c    # ★ 연습용 stub — 여기에 직접 구현
├── solutions/  NN_topic.c    #   정답 (검증 완료)
├── notes/      NN_topic.md   #   세트별 스터디 노트 (면접에서 말할 문장 포함)
├── data/       NN_topic.json #   대시보드 데이터 (+ profile / roles / interview)
└── SPEC.md                   #   이 은행의 작성 규칙
```

## 🔗 같이 보는 문서

- `../verkada_concurrency_top10.md` — **빈출 10문제 통합 문서** (문제+해설+시스템 설계+영어 스크립트+3일 플랜)
- `../concurrency_practice/` — 위 10문제의 통짜 구현본 (면접 시뮬레이션용)
- `../verkada_sr_embedded_linux_engineer_connectivity_context.md` — 회사·적합도·인터뷰 프로세스 분석

## ⚠️ 플랫폼

macOS(Apple clang, arm64)에서 컴파일·실행하도록 작성했다. Linux 전용 API
(`epoll`, `timerfd`, `signalfd`, `pthread_condattr_setclock`)는 코드 대신 **노트에서
이론으로** 다룬다 — 면접에서는 말로 설명해야 하기 때문이다.
"""
open(os.path.join(ROOT, "README.md"), "w", encoding="utf-8").write(readme)
print(f"README.md: {len(sets)} sets, {nprob} problems, {ncheck} checks")
