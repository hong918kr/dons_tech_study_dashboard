#!/usr/bin/env python3
"""technicalScreenPrep 채점기 — pytest 로 돌린다 (technicalScreenPrep/.venv 의 pytest 를 자동으로 사용).

  python3 python/run.py 01          # starters/01_*.py  (내 풀이)
  python3 python/run.py 01 --sol    # solutions/01_*.py (모범답안)
  python3 python/run.py all --sol   # 전체
  python3 python/run.py 02 -v       # pytest 출력 자세히

문제 종류별 채점
- 구현 문제 (01 05 06 07): 파일 안의 test_* 가 전부 통과하면 끝
- 테스트 작성 문제 (02 03): 파일 첫 docstring 에 '# mutants: <이름>' 이 있다
    1) 정상 구현(lib/<이름>.py)에서 전부 통과해야 하고
    2) lib/mutants/<이름>_m*.py 각각을 끼웠을 때 하나 이상 실패해야 'killed'
- conftest 문제 (04): python/graders/test_04_hil_conftest.py 가 pytester 로 내 conftest.py 를 검사

venv 가 없으면:  cd technicalScreenPrep && python3 -m venv .venv && .venv/bin/pip install pytest
"""
import functools
import os
import re
import subprocess
import sys
from pathlib import Path

print = functools.partial(print, flush=True)   # subprocess 출력과 순서가 섞이지 않게
PY = Path(__file__).resolve().parent              # technicalScreenPrep/python
VENV_PY = PY.parent / ".venv" / "bin" / "python"


def pytest_python():
    if VENV_PY.exists():
        return str(VENV_PY)
    try:
        import pytest  # noqa: F401
        return sys.executable
    except ImportError:
        sys.exit("pytest 없음 → cd technicalScreenPrep && python3 -m venv .venv && .venv/bin/pip install pytest")


def pytest_cmd(target, extra=()):
    return [pytest_python(), "-m", "pytest", "-c", str(PY / "pytest.ini"), "--rootdir", str(PY),
            "-p", "no:cacheprovider", *extra, str(target)]


def run(target, env=None, extra=(), quiet=False):
    e = dict(os.environ, **(env or {}))
    r = subprocess.run(pytest_cmd(target, extra), cwd=PY, env=e,
                       capture_output=quiet, text=True)
    return r.returncode


def mutants_of(path):
    m = re.search(r"#\s*mutants:\s*(\w+)", path.read_text(encoding="utf-8")[:2000])
    return m.group(1) if m else None


def grade(path, kind, verbose):
    print(f"━━ {path.relative_to(PY)} ━━")
    flags = ["-v", "--tb=short", "-rs"] if verbose else ["-q", "--tb=line", "-rs"]
    if path.is_dir():                                         # 04: conftest 문제
        rc = run(PY / "graders" / f"test_{path.name}.py",
                 env={"TS_04_DIR": f"{kind}/{path.name}"}, extra=flags)
        print("→ PASS\n" if rc == 0 else "→ 아직 (위 FAIL/SKIP 참고)\n")
        return rc == 0
    rc = run(path, extra=flags)
    name = mutants_of(path)
    if not name:
        print("→ PASS\n" if rc == 0 else "→ 아직\n")
        return rc == 0
    if rc != 0:
        print("→ 정상 구현에서 테스트가 실패 — 테스트가 틀렸다. mutant 채점은 건너뜀\n")
        return False
    killed, survived = [], []
    for m in sorted((PY / "lib" / "mutants").glob(f"{name}_m*.py")):
        r = run(path, env={f"TS_IMPL_{name.upper()}": f"lib.mutants.{m.stem}"},
                extra=["-q", "-x"], quiet=True)
        (killed if r == 1 else survived).append(m.stem.rsplit("_", 1)[1])
    total = len(killed) + len(survived)
    print(f"mutants: {len(killed)}/{total} killed"
          + (f" · survived: {' '.join(survived)}  (lib/mutants/{name}_<번호>.py 의 docstring 이 힌트)"
             if survived else " ✓"))
    print()
    return not survived


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        return 2
    which = argv[0]
    kind = "solutions" if "--sol" in argv else "starters"
    verbose = "-v" in argv
    items = sorted(p for p in (PY / kind).iterdir()
                   if re.match(r"\d\d_", p.name) and (p.is_dir() or p.suffix == ".py"))
    if which != "all":
        items = [p for p in items if p.name.startswith(which.zfill(2))]
    if not items:
        print(f"{kind}/ 에서 '{which}' 를 찾지 못함")
        return 2
    results = {p.name: grade(p, kind, verbose) for p in items}
    if len(results) > 1:
        print("요약: " + "  ".join(f"{n[:2]}{'✓' if ok else '✗'}" for n, ok in results.items()))
    return 0 if all(results.values()) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
