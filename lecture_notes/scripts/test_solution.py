"""
작성한 솔루션(.c)을 해당 문제의 하네스로 컴파일·실행해 채점한다.
솔루션 작성/검증용 헬퍼.

사용:
    python scripts/test_solution.py <problem_id> <solution.c 경로>
출력:
    COMPILE FAIL + 에러  /  mode=.. passed=x/y  + stdout + expected_output
종료코드: 0=통과, 1=컴파일실패, 2=테스트 일부 실패/수동확인
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from code_runner import compile_and_run  # noqa: E402
from problems_data import get_problem  # noqa: E402
from utils import setup_console  # noqa: E402

setup_console()


def main() -> int:
    if len(sys.argv) < 3:
        print("usage: test_solution.py <problem_id> <solution.c>", file=sys.stderr)
        return 1
    pid, fpath = sys.argv[1], sys.argv[2]
    p = get_problem(pid)
    if not p:
        print(f"unknown problem id: {pid}", file=sys.stderr)
        return 1
    code = Path(fpath).read_text(encoding="utf-8")
    r = compile_and_run(code, p["harness"], p.get("prelude", ""), p["lang"], p.get("grade_mode", ""))

    if not r.get("compiled"):
        print("COMPILE FAIL")
        print((r.get("compile_stderr") or r.get("error") or "")[:2000])
        return 1
    if r.get("timed_out"):
        print("TIMEOUT (무한 루프 의심)")
        return 2

    mode, passed, total = r.get("mode"), r.get("passed"), r.get("total")
    print(f"mode={mode} passed={passed}/{total}")
    print("--- stdout ---")
    print((r.get("stdout") or "")[:3000])
    exp = p.get("expected_output", "")
    if exp:
        print("--- expected_output (수동 비교) ---")
        print(exp[:3000])

    if mode in ("result", "passfail", "assert"):
        return 0 if (total and passed == total) else 2
    return 2  # manual → 사람이 stdout vs expected 비교


if __name__ == "__main__":
    raise SystemExit(main())
