#!/usr/bin/env python3
"""L05 몸풀기 → playground 에 '빈 파일' 만들기. 문제 설명(주석)만 있고 구현 · 테스트 · main 은 전부 직접 쓴다.

  python3 lecture/warmup/new.py py 1       # Python 1번 → playground/py/2026-10-05_W01_two_sum.py
  python3 lecture/warmup/new.py c 5        # C 5번
  python3 lecture/warmup/new.py            # 전부 (14개)
쓰고 나서:  cd playground && make py F=py/<파일>   /   make c F=c/<파일>      → PASS 가 나오면 지우고 다음 문제
같은 날 다시 만들면 _2, _3 … 을 붙인다 (덮어쓰지 않음).
"""
import datetime
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
PLAY = HERE.parent.parent / "playground"


def skeleton(src):
    text = src.read_text(encoding="utf-8")
    head = text.split("# --- solution ---")[0] if src.suffix == ".py" else text.split("// --- solution ---")[0]
    head = "\n".join(l for l in head.splitlines() if not re.match(r"^[ *]*note:", l)) + "\n"   # 힌트(note)는 빼고 문제만
    todo = ("# 1) 구현  2) def test_…(): assert 1~2개  3) if __name__ == \"__main__\": 테스트 호출 + print(\"PASS\")\n\n"
            if src.suffix == ".py" else
            "/* 1) #include  2) 구현  3) static void test_…(void) { assert 1~2개 }  4) main: 테스트 호출 + puts(\"PASS\") */\n\n")
    return head + todo


def main(argv):
    lang = argv[0] if argv and argv[0] in ("py", "c") else None
    num = argv[1].zfill(2) if len(argv) > 1 else None
    today = datetime.date.today().isoformat()
    for sub, prefix in (("py", "W"), ("c", "WC")):
        if lang and lang != sub:
            continue
        for f in sorted((HERE / sub).glob("[0-9]*.*")):
            if num and not f.name.startswith(num):
                continue
            stem = f"{today}_{prefix}{f.stem}"
            out, k = PLAY / sub / f"{stem}{f.suffix}", 2
            while out.exists():
                out, k = PLAY / sub / f"{stem}_{k}{f.suffix}", k + 1
            out.write_text(skeleton(f), encoding="utf-8")
            rel = out.relative_to(PLAY)
            print(f"  {rel}    →  cd playground && make {'py' if sub == 'py' else 'c'} F={rel}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
