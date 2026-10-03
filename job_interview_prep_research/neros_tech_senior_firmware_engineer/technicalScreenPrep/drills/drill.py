#!/usr/bin/env python3
"""매일 타이핑 드릴 — 에디터에서 직접 치고 채점.

  python3 drills/drill.py new 1          # drills/workspace/day1/ 에 빈 문제 파일 20개 (P1_01.py … C1_10.c)
  python3 drills/drill.py new 1 --force  # 이미 있으면 덮어쓰기 (어제 친 것 지우고 다시)
  python3 drills/drill.py check 1        # 내가 친 것 채점 → ✓ / ✗ / TODO
  python3 drills/drill.py check 1 P1_03  # 하나만
  python3 drills/drill.py answer P1_03   # 정답 보기
  python3 drills/drill.py verify         # (관리용) 정답 100개가 전부 통과하는지
  python3 drills/drill.py md             # (관리용) drills/*.md 다시 생성 — build_notes_site.py 가 자동 호출

브라우저로 하려면 site/drills.html (따라 치기 / 기억해서 치기 · 정답과 줄 단위 비교 · 타이머).
C 는 cc -std=c11 -Wall -Wextra -Werror, pytest 문제(P5)는 technicalScreenPrep/.venv 의 pytest 로 돈다.
"""
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
import drills_data as D  # noqa: E402

WS = HERE / "workspace"
VENV_PY = ROOT / ".venv" / "bin" / "python"
C_INCLUDES = """#include <assert.h>
#include <ctype.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
"""
MARK_BEGIN = "▼ 여기부터 작성 ▼"
MARK_END = "▲ 여기까지 ▲"


def fname(it):
    return it["id"].replace("-", "_") + (".py" if it["lang"] == "py" else ".c")


def by_id(key):
    key = key.replace("_", "-").upper()
    for it in D.ITEMS:
        if it["id"] == key:
            return it
    sys.exit(f"없는 문제: {key}")


def indent(code, pad="    "):
    return "\n".join(pad + l if l.strip() else "" for l in code.splitlines())


# ---------------------------------------------------------------- 파일 만들기
def py_source(it, body):
    head = f'"""{it["id"]} · {it["title"]}\n\n{it["prompt"]}\n"""\n'
    given = f"# ---- given (치지 않음)\n{it['given']}\n\n" if it["given"] else ""
    user = f"# {MARK_BEGIN}\n{body}\n# {MARK_END}\n"
    if it["pytest"]:
        return head + given + user
    check = f"\n\nif __name__ == \"__main__\":\n{indent(it['check'])}\n    print(\"OK\")\n"
    return head + given + user + check


def c_source(it, body):
    head = f"/* {it['id']} · {it['title']}\n * {it['prompt']}\n */\n"
    given = f"/* ---- given (치지 않음) */\n{it['given']}\n\n" if it["given"] else ""
    user = f"/* {MARK_BEGIN} */\n{body}\n/* {MARK_END} */\n"
    support = f"\n/* ---- 채점용 */\n{it['support']}\n" if it["support"] else ""
    main = f"\nint main(void)\n{{\n{indent(it['check'])}\n    puts(\"OK\");\n    return 0;\n}}\n"
    return head + C_INCLUDES + "\n" + given + user + support + main


def source(it, body):
    return (py_source if it["lang"] == "py" else c_source)(it, body)


# ---------------------------------------------------------------- 실행
def run_file(it, path):
    """→ (ok, message)"""
    if it["lang"] == "py":
        if it["pytest"]:
            py = str(VENV_PY) if VENV_PY.exists() else sys.executable
            r = subprocess.run([py, "-m", "pytest", "-q", "-p", "no:cacheprovider", str(path)],
                               capture_output=True, text=True, cwd=path.parent)
            ok = r.returncode == 0 and " passed" in r.stdout
            return ok, (r.stdout.strip().splitlines() or [""])[-1]
        r = subprocess.run([sys.executable, str(path)], capture_output=True, text=True, timeout=30)
        return r.returncode == 0, (r.stderr.strip().splitlines() or ["OK"])[-1]
    cc = shutil.which("cc") or shutil.which("gcc")
    exe = path.with_suffix("")
    r = subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", str(path), "-o", str(exe)],
                       capture_output=True, text=True)
    if r.returncode:
        err = [l for l in r.stderr.splitlines() if "error" in l]
        return False, "compile: " + (err[0].split("error:")[-1].strip() if err else r.stderr.strip()[:120])
    r = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
    exe.unlink(missing_ok=True)
    return r.returncode == 0, (r.stdout + r.stderr).strip().splitlines()[-1] if (r.stdout + r.stderr).strip() else f"exit {r.returncode}"


def user_body(text, lang):
    a = text.find(MARK_BEGIN)
    b = text.find(MARK_END)
    if a < 0 or b < 0:
        return text
    body = text[text.index("\n", a) + 1:b]
    body = body.rsplit("\n", 1)[0] if lang == "py" else body.rsplit("\n", 1)[0]
    return body.strip()


# ---------------------------------------------------------------- 명령
def cmd_verify():
    bad = 0
    with tempfile.TemporaryDirectory() as td:
        for it in D.ITEMS:
            p = Path(td) / fname(it)
            p.write_text(source(it, it["answer"]), encoding="utf-8")
            ok, msg = run_file(it, p)
            if not ok:
                bad += 1
                print(f"✗ {it['id']}: {msg}")
    print(f"verify: {len(D.ITEMS) - bad}/{len(D.ITEMS)} 정답 통과")
    return 1 if bad else 0


def cmd_new(day, force):
    d = WS / f"day{day}"
    d.mkdir(parents=True, exist_ok=True)
    n = 0
    for it in D.ITEMS:
        if it["day"] != day:
            continue
        p = d / fname(it)
        if p.exists() and not force:
            continue
        p.write_text(source(it, ""), encoding="utf-8")
        n += 1
    print(f"{d.relative_to(ROOT)}/ 에 {n}개 생성 (이미 있는 건 건너뜀, --force 로 덮어쓰기)")
    print(f"에디터로 열어서 '{MARK_BEGIN}' 아래를 채운 뒤:  python3 drills/drill.py check {day}")


def cmd_check(day, only=None):
    d = WS / f"day{day}"
    if not d.exists():
        sys.exit(f"먼저: python3 drills/drill.py new {day}")
    results = []
    for it in D.ITEMS:
        if it["day"] != day or (only and it["id"] != by_id(only)["id"]):
            continue
        p = d / fname(it)
        if not p.exists():
            continue
        body = user_body(p.read_text(encoding="utf-8"), it["lang"])
        if not body:
            print(f"  ·  {it['id']}  TODO  {it['title']}")
            results.append(None)
            continue
        ok, msg = run_file(it, p)
        print(f"  {'✓' if ok else '✗'}  {it['id']}  {it['title']}" + ("" if ok else f"   ← {msg}"))
        results.append(ok)
    done = [r for r in results if r is not None]
    print(f"\nday{day}: ✓ {sum(done)} · ✗ {len(done) - sum(done)} · TODO {results.count(None)}")
    return 0 if done and all(done) else 1


def cmd_answer(key):
    it = by_id(key)
    print(f"{it['id']} · {it['title']}\n{it['prompt']}\n")
    if it["given"]:
        print("---- given\n" + it["given"] + "\n")
    print("---- answer\n" + it["answer"])


def md_for_day(day):
    date, theme = D.DAYS[day]
    out = [f"# ⌨️ D{day} · {date} 드릴 — {theme}", "",
           f"> 매일 아침 20분 + 저녁 20분. **정답을 보지 않고** 빈 파일에 친다. 브라우저: [드릴 트레이너](../site/drills.html) (Day {day} 탭) · "
           f"에디터: `python3 drills/drill.py new {day}` → `python3 drills/drill.py check {day}`. 틀린 항목은 다음 날 다시.",
           ""]
    for lang, label in (("py", "Python"), ("c", "C")):
        out += [f"## {label} 10개", ""]
        out += ["| ID | 주제 | 칠 것 |", "|---|---|---|"]
        for it in D.ITEMS:
            if it["day"] == day and it["lang"] == lang:
                out.append(f"| {it['id']} | {it['title']} | {it['prompt'].replace('|', '/')} |")
        out.append("")
        for it in D.ITEMS:
            if it["day"] != day or it["lang"] != lang:
                continue
            fence = "python" if lang == "py" else "c"
            out += [f"### {it['id']} · {it['title']}", "", it["prompt"], ""]
            if it["given"]:
                out += ["주어진 코드 (치지 않음):", "", f"```{fence}", it["given"], "```", ""]
            out += ["정답:", "", f"```{fence}", it["answer"], "```", ""]
    out += ["## 체크", "",
            f"- [ ] Python 10개를 정답 안 보고 → `python3 drills/drill.py check {day}` 에서 ✓ 10",
            "- [ ] C 10개를 정답 안 보고 → ✓ 10",
            "- [ ] 틀린 것 ID를 적어 두고 다음 날 아침 첫 5분에 다시", ""]
    return "\n".join(out)


def cmd_md():
    for day in D.DAYS:
        date = "2026-10-03"           # 파일 이름 규칙: 생성일 접두어
        slug = {1: "basics", 2: "classes_memory", 3: "bytes_bits", 4: "ring_threads", 5: "pytest_ctests"}[day]
        (HERE / f"{date}_D{day}_{slug}.md").write_text(md_for_day(day), encoding="utf-8")
    return 0


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        return 2
    cmd = argv[0]
    if cmd == "verify":
        return cmd_verify()
    if cmd == "md":
        return cmd_md()
    if cmd == "answer" and len(argv) > 1:
        return cmd_answer(argv[1]) or 0
    if cmd in ("new", "check") and len(argv) > 1 and argv[1].isdigit():
        day = int(argv[1])
        if day not in D.DAYS:
            sys.exit("day 는 1~5")
        if cmd == "new":
            return cmd_new(day, "--force" in argv) or 0
        return cmd_check(day, argv[2] if len(argv) > 2 else None)
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
