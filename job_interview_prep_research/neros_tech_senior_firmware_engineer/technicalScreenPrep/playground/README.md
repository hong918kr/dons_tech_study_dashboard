# 🧪 Playground — 빈 파일에서 직접 쓰는 곳

> 템플릿도 정답도 없는 연습장이다. `py/`나 `c/`에 새 파일을 만들고 처음부터 쓴 다음, 한 줄 명령으로 실행한다. 정답 확인은 다 쓴 **뒤에** [드릴 트레이너](../site/drills.html)나 [Python 문제](../site/coding.html)에서 한다.

## 폴더

| 경로 | 용도 |
|---|---|
| `py/` | Python 파일 (`2026-10-04_ring.py`처럼 날짜를 앞에 붙이면 진도가 보인다) |
| `c/` | C 파일 (`main()`까지 직접 작성) |
| `build/` | C 빌드 결과 (자동 생성, git 무시) |
| `Makefile` | 실행 명령 모음 |
| `pytest.ini` | 이 폴더 전용 pytest 설정 (파일 이름과 상관없이 `test_*` 함수 실행) |

## 실행

```bash
cd ~/workspace/dons_tech_study_dashboard/job_interview_prep_research/neros_tech_senior_firmware_engineer/technicalScreenPrep/playground

make py   F=py/2026-10-04_ring.py     # Python 실행 (.venv 의 python 3.9 + pytest)
make test F=py/2026-10-04_ring.py     # 그 파일의 test_* 함수를 pytest 로
make c    F=c/2026-10-04_ring.c       # C: -Wall -Wextra -Werror + ASan/UBSan 빌드 후 실행
make clean
```

- C 경고 하나도 에러로 처리한다 (`-Werror`). 메모리 버그는 ASan이 실행 중에 잡는다
- Python은 `if __name__ == "__main__":` 아래에 assert를 두면 `make py`로, `def test_…`로 쓰면 `make test`로 확인

## 쓰는 법 (권장 루틴)

1. 문제를 하나 정한다 — [드릴](../site/drills.html)의 prompt, [Python 문제](../site/coding.html) 01~07, 또는 직접 만든 것
2. **빈 파일**을 만들고 타이머를 켠다. 정답 · 이전 파일 · 자동완성 없이
3. 구현 → 직접 테스트 몇 줄 → 실행
4. 막힌 곳만 정답과 비교, 다음 날 같은 문제를 다시 빈 파일에서

| 날짜 | 추천 스크래치 과제 |
|---|---|
| 10-04 | Python `RingBuffer` (push/pop/len/iter) + `test_*` 5개 · C `rb_push`/`rb_pop` + `main()` assert |
| 10-05 | Python `struct`로 헤더 파싱 + CRC-8 · C 엔디언 읽기/쓰기 + CRC-8 |
| 10-06 | Python `Condition` blocking ring + 스레드 테스트 · C SPSC atomics · **15분 타이머로 ring buffer 판단** |
| 10-07 | pytest: fixture · parametrize · Mock 으로 남이 쓴 함수 테스트 (빈 파일에서) |
