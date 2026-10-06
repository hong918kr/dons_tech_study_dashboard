#!/usr/bin/env python3
"""특별강의 md 생성기 — src/*.md.in 의 {{include 경로}} / {{include 경로#구역}} 을 실제 파일 내용으로 바꿔
lecture/ 아래 md 를 만든다. 그래서 강의에 보이는 코드 = 실제로 실행·검증한 코드.

  python3 lecture/build_lecture.py          # md 생성 (build_notes_site.py 가 자동 호출)
  python3 lecture/build_lecture.py --test   # lecture/code/ 의 pytest 전부 (L00 · L01) + code_c/ C (L03) + code_cpp/ C++ (L04) + warmup/ (L05)
  bash lecture/frameworks/run_all.sh        # L02 프레임워크 예제 10종 실행

구역: 파일 안의 '# --- 이름 ---' (C/C++ 는 '// --- 이름 ---') 부터 '--- end ---' 줄 직전까지.
"""
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
SOURCES = {
    "special_lecture.md.in": "2026-10-04_L00_special_lecture.md",
    "l01_python_syntax.md.in": "2026-10-04_L01_python_syntax.md",
    "l02_embedded_test_frameworks.md.in": "2026-10-04_L02_embedded_test_frameworks.md",
    "l03_embedded_c_basics.md.in": "2026-10-04_L03_embedded_c_basics.md",
    "l04_embedded_cpp_basics.md.in": "2026-10-04_L04_embedded_cpp_basics.md",
    "l05_warmup.md.in": "2026-10-04_L05_warmup.md",
    "l06_screen_scenarios.md.in": "2026-10-05_L06_screen_scenarios.md",
}
WARMUP = HERE / "warmup"
WARM = re.compile(r"^\{\{warmup (py|c)\}\}$")
INC = re.compile(r"^\{\{include ([\w./-]+)(?:#(\w+))?\}\}$")
LANG = {".py": "python", ".c": "c", ".h": "c", ".cpp": "cpp", ".cc": "cpp", ".robot": "robot",
        ".feature": "gherkin", ".yml": "yaml", ".yaml": "yaml", ".sh": "bash", ".json": "json"}


def lang_of(path):
    p = Path(path)
    return "make" if p.name == "Makefile" else LANG.get(p.suffix, "")


def section(path, name):
    text = (HERE / path).read_text(encoding="utf-8")
    if not name:
        return text.rstrip()
    m = re.search(rf"(?:#|//) --- {name} ---\n(.*?)(?:#|//) --- end ---", text, re.S)
    if not m:
        raise SystemExit(f"{path}: '--- {name} ---' 구역 없음")
    return m.group(1).rstrip()


def warmup_items():
    """warmup/py/NN_*.py · warmup/c/NN_*.c → 문제 목록 (파일을 추가하면 L05 페이지와 트레이너에 자동 반영).
    헤더: 첫 줄 'NN · 제목', 'prompt: …' 한 줄, 'note: …' 여러 줄. 칠 코드 = '--- solution ---' ~ '--- end ---' (구현 + 테스트 + main)."""
    items = []
    for lang, ext in (("py", ".py"), ("c", ".c")):
        for f in sorted((WARMUP / lang).glob(f"[0-9]*{ext}")):
            text = f.read_text(encoding="utf-8")
            head = text.split("\n", 1)[0].strip().lstrip('"/* ').strip()
            prompt = re.search(r"^[ *]*prompt:\s*(.+)$", text, re.M)
            items.append({
                "id": f"{'W' if lang == 'py' else 'WC'}-{f.stem[:2]}",
                "lang": lang, "title": head.split("·", 1)[-1].strip(), "num": f.stem[:2], "head": head,
                "prompt": prompt.group(1).strip() if prompt else "",
                "notes": [n.strip() for n in re.findall(r"^[ *]*note:\s*(.+)$", text, re.M)],
                "answer": section(f.relative_to(HERE).as_posix(), "solution"),
                "file": f.relative_to(HERE).as_posix(),
            })
    return items


def warmup_md(lang):
    out = []
    for it in [x for x in warmup_items() if x["lang"] == lang]:
        fence = "python" if lang == "py" else "c"
        name = it["file"].split("/")[-1]
        run = f"python3 lecture/warmup/py/{name}" if lang == "py" else f"make -C lecture/warmup c   (playground: make c F=c/<파일>)"
        new = f"python3 lecture/warmup/new.py {lang} {it['num'].lstrip('0')}"
        out += [f"### {it['head']}", "", f"> {it['prompt']}", ""]
        out += [f"- {n}" for n in it["notes"]]
        out += ["", f"```{fence}", it["answer"], "```", "",
                f"- 빈 파일: `{new}` · 정답 파일: [{name}]({it['file']}) · 정답 실행: `{run}`", ""]
    return out


def build():
    outs = []
    for src, dst in SOURCES.items():
        sp = HERE / "src" / src
        if not sp.exists():
            continue
        lines = []
        for line in sp.read_text(encoding="utf-8").splitlines():
            w = WARM.match(line.strip())
            if w:
                lines += warmup_md(w.group(1))
                continue
            m = INC.match(line.strip())
            if m:
                lines += [f"```{lang_of(m.group(1))}", section(m.group(1), m.group(2)), "```"]
            else:
                lines.append(line)
        (HERE / dst).write_text("\n".join(lines) + "\n", encoding="utf-8")
        outs.append(dst)
    return outs


def test():
    venv = HERE.parent / ".venv" / "bin" / "python"
    py = str(venv) if venv.exists() else sys.executable
    files = sorted(str(p) for p in (HERE / "code").glob("*.py"))
    rc = subprocess.run([py, "-m", "pytest", "-q", "-p", "no:cacheprovider",
                         "--rootdir", str(HERE / "code"), *files]).returncode
    mock = HERE / "mock"                                 # L06 모의 문제: 정답은 통과, 버그 버전은 정확히 3개 실패
    rc |= subprocess.run([py, "-m", "pytest", "-q", "-p", "no:cacheprovider", "--rootdir", str(mock),
                          str(mock / "telemetry_check.py"), str(mock / "test_ring_mock.py")]).returncode
    bug = subprocess.run([py, "-m", "pytest", "-q", "-p", "no:cacheprovider", "--rootdir", str(mock),
                          str(mock / "test_ring_mock.py")], env={**__import__("os").environ, "MOCK_RING": "ring_buggy"},
                         capture_output=True, text=True)
    print("L06 buggy ring:", (bug.stdout.strip().splitlines() or ["?"])[-1])
    rc |= 0 if "3 failed" in bug.stdout else 1
    for d in ("code_c", "code_cpp", "warmup"):          # L03 C · L04 C++ · L05 몸풀기
        rc |= subprocess.run(["make", "-s", "-C", str(HERE / d), "test"]).returncode
        subprocess.run(["make", "-s", "-C", str(HERE / d), "clean"])
    return rc


if __name__ == "__main__":
    if "--test" in sys.argv:
        sys.exit(test())
    for o in build():
        print(f"built lecture/{o}")
