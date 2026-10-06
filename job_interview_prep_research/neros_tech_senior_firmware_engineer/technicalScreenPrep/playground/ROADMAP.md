# 🎯 Playground Roadmap — C & Python 번갈아가며

> **목표**: 기본 자료구조(ring buffer, queue, hash), 동시성, 비트 연산을 C와 Python으로 각각 이해하기.  
> **패턴**: 문제를 읽고 **빈 파일에서 처음부터** 작성 (정답 먼저 안 봄) → 실행 → 정답 비교.

## Week 1: Fundamentals

| 날짜 | 주제 | C | Python | 예상 시간 |
|------|------|---|--------|----------|
| 10-04 | Ring Buffer | `rb_push/pop` + main assert | `RingBuffer` 클래스 + 5개 test | 45min |
| 10-05 | 비트/엔디언 | `write_u32_be/le` + CRC-8 | `struct` + CRC-8 검증 | 40min |
| 10-06 | 동시성 | SPSC atomics `push/pop` | `Condition` + reader/writer 스레드 | 60min |
| 10-07 | 테스트 | Makefile 테스트 + 엣지 케이스 | pytest fixture/parametrize/Mock | 50min |

## Week 2: Data Structures

| 날짜 | 주제 | C | Python | 예상 시간 |
|------|------|---|--------|----------|
| 10-08 | 큐 (FIFO) | 동적 배열 기반 + 재할당 | `collections.deque` vs 직접 구현 | 40min |
| 10-09 | 이진 탐색 | sorted array + BinSearch | bisect vs manual | 35min |
| 10-10 | 슬라이딩 윈도우 | monotonic deque | deque + maxlen | 45min |
| 10-11 | 해시맵 | open addressing + probe | dict vs OrderedDict + hash collision | 60min |

## 폴더 구조

각 주제마다:
```
playground/
├── 01_ring_buffer/
│   ├── problem.txt           # 문제 설명
│   ├── c/
│   │   ├── starter.c         # 스턱 (함수 시그니처만)
│   │   └── solution.c        # 풀이
│   └── py/
│       ├── starter.py        # 스턱
│       └── solution.py       # 풀이
├── 02_endian_crc/
│   └── ...
```

## 실행 방법

```bash
cd playground

# C 풀이: 직접 작성
make c F=01_ring_buffer/c/solution.c

# Python 풀이: 직접 작성
make py F=01_ring_buffer/py/solution.py

# 테스트 실행
make test F=01_ring_buffer/py/solution.py

# 정리
make clean
```

## 규칙

1. **프롬프트만 보고 빈 파일에 쓴다** — 정답 스니펫 먼저 보면 안 됨
2. **주제별로 C ↔ Python 번갈아가기** — 월, 수에 C / 화, 목에 Python (또는 반대)
3. **타이머: 각 문제 30~60분** — 막히면 해설 읽고 다음 날 다시
4. **커밋은 매일** — `git add . && git commit -m "2026-10-04 ring buffer"`

## 점검 목록

- [ ] `.venv` 작동 확인: `make py F=py/test.py`
- [ ] C 컴파일러 옵션 확인: `-Wall -Wextra -Werror` + ASan
- [ ] 각 주제별 starter/solution 파일 생성
- [ ] git 커밋 시작 (`git log --oneline | head`)
