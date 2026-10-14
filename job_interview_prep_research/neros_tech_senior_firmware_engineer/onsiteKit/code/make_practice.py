"""Create practice/<name>.py from each solution: bodies between the solution markers become TODO.

  python3 make_practice.py            # only creates files that don't exist (your work is safe)
  python3 make_practice.py c02 --reset   # wipe practice/c02_*.py back to a blank start
"""
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
BLOCK = re.compile(r"^([ \t]*)# >>> solution\n.*?^[ \t]*# <<< solution\n", re.S | re.M)


def blank(src):
    return BLOCK.sub(lambda m: f'{m.group(1)}raise NotImplementedError("TODO")\n', src)


def main(argv):
    reset = "--reset" in argv
    only = [a for a in argv if not a.startswith("--")]
    out = HERE / "practice"
    out.mkdir(exist_ok=True)
    for f in sorted(HERE.glob("c[0-9][0-9]_*.py")):
        if only and not any(f.name.startswith(o) for o in only):
            continue
        dst = out / f.name
        if dst.exists() and not reset:
            print(f"  keep   practice/{f.name}")
            continue
        dst.write_text(blank(f.read_text(encoding="utf-8")), encoding="utf-8")
        print(f"  write  practice/{f.name}")


if __name__ == "__main__":
    main(sys.argv[1:])
