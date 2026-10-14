"""Run every problem's tests.

  python3 run_all.py               # reference solutions (should all be OK)
  python3 run_all.py --practice    # your versions in practice/
  python3 run_all.py --practice c03
"""
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def main(argv):
    base = HERE / "practice" if "--practice" in argv else HERE
    only = [a for a in argv if not a.startswith("--")]
    files = sorted(base.glob("c[0-9][0-9]_*.py"))
    files = [f for f in files if not only or any(f.name.startswith(o) for o in only)]
    bad = 0
    for f in files:
        r = subprocess.run([sys.executable, str(f)], capture_output=True, text=True)
        last = (r.stdout + r.stderr).strip().splitlines()[-1:] or [""]
        print(f"{'PASS' if r.returncode == 0 else 'FAIL'}  {f.name:<28} {last[0][:90]}")
        bad += r.returncode != 0
    print(f"{len(files) - bad}/{len(files)} passed")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
