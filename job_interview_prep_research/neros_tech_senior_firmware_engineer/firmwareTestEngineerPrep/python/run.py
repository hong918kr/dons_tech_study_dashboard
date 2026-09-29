#!/usr/bin/env python3
"""Python 코딩 문제 채점기 (표준 라이브러리만).

  python3 python/run.py 01          # starters/01_*.py 의 테스트 실행 (내가 푼 것)
  python3 python/run.py 01 --sol    # solutions/01_*.py 로 실행 (모범답안)
  python3 python/run.py all --sol   # 전체

각 문제 파일은 test_* 함수를 가지고 있고, 이 러너가 그 함수들을 차례로 호출한다.
pytest가 있으면 `pytest python/solutions/01_*.py` 로도 똑같이 돌아간다.
"""
import importlib.util
import sys
import traceback
from pathlib import Path

HERE = Path(__file__).resolve().parent


def load(path):
    spec = importlib.util.spec_from_file_location(path.stem, path)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = mod          # dataclass + __future__ annotations 가 모듈을 찾을 수 있게
    spec.loader.exec_module(mod)
    return mod


def run_file(path):
    mod = load(path)
    tests = [(n, f) for n, f in vars(mod).items() if n.startswith("test_") and callable(f)]
    ok = 0
    for name, fn in tests:
        try:
            fn()
            ok += 1
            print(f"  PASS  {name}")
        except NotImplementedError:
            print(f"  TODO  {name}  (아직 안 풂)")
        except Exception:                         # noqa: BLE001
            print(f"  FAIL  {name}")
            print("        " + traceback.format_exc().strip().splitlines()[-1])
    print(f"{path.name}: {ok}/{len(tests)} passed\n")
    return ok == len(tests)


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    which, sol = argv[0], "--sol" in argv
    folder = HERE / ("solutions" if sol else "starters")
    files = sorted(folder.glob("[0-9]*.py"))
    if which != "all":
        files = [f for f in files if f.name.startswith(which.zfill(2))]
    if not files:
        print(f"{folder}에서 '{which}' 문제를 찾지 못함")
        return 2
    results = [run_file(f) for f in files]
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
