#!/usr/bin/env python3
"""solutions/*.c → starters/*.c

/*@impl-begin*/ ... /*@impl-end*/ 사이의 구현만 TODO로 비운다.
문제 설명, API 선언, self-test는 그대로 남으므로 `make run N=01`로 바로 채점된다.
"""
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
SRC, DST = HERE / "solutions", HERE / "starters"
BLOCK = re.compile(r"/\*@impl-begin\*/\n(.*?)/\*@impl-end\*/\n", re.S)

TODO = """/* ------------------------------------------------------------------
 * 여기부터 직접 구현한다. 위의 선언부와 아래 self-test는 그대로 두고,
 * 아래 함수 목록을 채운 뒤 `make run N=%s` 로 검증한다.
 *
 * 구현할 함수:
%s *
 * 막히면 ../solutions/%s 를 열되, 먼저 15분은 스스로 해볼 것.
 * ------------------------------------------------------------------ */
"""

SIG = re.compile(r"^(?:static\s+)?[A-Za-z_][\w\s\*]*?([A-Za-z_]\w*)\s*\([^;{]*\)\s*\{", re.M)


def main():
    DST.mkdir(exist_ok=True)
    made = 0
    for src in sorted(SRC.glob("*.c")):
        text = src.read_text()
        m = BLOCK.search(text)
        if not m:
            print("skip (no markers):", src.name)
            continue
        names = [f" *   - {n}()\n" for n in dict.fromkeys(SIG.findall(m.group(1)))]
        stub = TODO % (src.name[:2], "".join(names), src.name)
        (DST / src.name).write_text(text[:m.start()] + stub + text[m.end():])
        made += 1
    print(f"{made} starters → {DST}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
