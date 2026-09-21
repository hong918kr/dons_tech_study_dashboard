# concurrency_practice — 면접 시뮬레이션 10문제 + 기초 노트

`../verkada_concurrency_top10.md` 의 빈출 10문제를 **통짜 구현본**으로 담은 폴더다.
세분화된 드릴은 `../verkada_prep/` 문제 은행이고, 여기는 "한 문제를 면접처럼 처음부터 끝까지"
연습하는 용도다.

```
notes/      NN_*.md   ← 기초 노트 (동시성을 처음부터. HTML: ../notes_site/basics/)
starters/   NN_*.c    ← 구현부가 비어 있는 연습 파일
solutions/  NN_*.c    ← 검증된 모범답안 (경고 0 · 전 테스트 PASS · TSan 클린)
```

## 쓰는 법

```sh
open ../notes_site/index.html     # 노트 사이트 (기초 노트 + 면접 복습 노트)

make run  N=01   # starters 구현한 것 채점
make sol  N=01   # 모범답안 실행
make tsan N=01   # ThreadSanitizer
make all-sol     # 10문제 회귀
```

## 순서

1. `notes/00_start_here.md` 로 기초 개념을 훑는다 (스레드 → race → mutex → condvar → atomic).
2. 문제별 노트를 읽고 → `make run N=NN` 으로 직접 구현 → `make sol N=NN` 으로 대조.
3. 우선순위: **01 bounded queue → 03 double buffer → 10 testable driver**
   (리크루터 메일이 지목한 주제 순).

노트의 모든 C 코드 인용은 `solutions/` 의 실제 코드와 일치하는지 자동 검사로 확인했다.
