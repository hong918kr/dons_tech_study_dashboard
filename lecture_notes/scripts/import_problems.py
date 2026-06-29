"""
인터뷰 준비 레포(.c/.cpp 프롬프트 파일)를 코딩 문제로 가져온다.

각 프롬프트 파일은 '스텁 + 테스트 main'으로 된 완전한 컴파일 가능 프로그램이다.
이를 다음으로 분해해 data/problems/imported.json 에 저장:
  - starter : 편집할 스텁 영역 (테스트 코드 전까지)
  - harness : 테스트 영역 (test_result 헬퍼/ int main ~ 끝)
  - description : 상단 주석(번호별 문제 설명)
  - expected_output : 말미 'Expected/Example' 주석 (있으면)
  - lang : c / cpp

사용:
    python scripts/import_problems.py            # 기본 대상 폴더 가져오기
    python scripts/import_problems.py --verify   # 가져온 각 문제 컴파일 점검
"""
from __future__ import annotations

import argparse
import html
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from config import DATA_DIR  # noqa: E402
from utils import setup_console, write_json  # noqa: E402

setup_console()

# 다운로드한 레포 루트 (github 대신 로컬 사용)
REPO = Path(r"C:\Users\haydn\Downloads\don-c-prac-master\don-c-prac-master")

# 가져올 대상: (상대폴더, 카테고리 라벨)
TARGETS = [
    ("0ESE_prep/Defense_Anduril_FW_prep/c_Coding_Questions_100ea", "Anduril · 100 Questions"),
    ("0past_questions_more_prep/c_proficiency_prep_bit-buf-ser-data", "Bit·Buffer·Serialization"),
    ("interview_repro", "Interview 재현"),
]

# 연습/스크래치/중복 변형은 제외
_SKIP_STEMS = ("_sol", "_an_empty", "_scratch", "_skeleton", "_don")

# 진짜 채점 문제가 아닌 파일(개념 예시·실제 테스트 없음)은 명시적으로 제외
_SKIP_FILES = {
    "32bit_two_timers_return_64bit_info.c",  # 미정의 하드웨어 심볼, 테스트 없음
    "ring_buffer_eched.c",  # 원본 다수 문법 오류(매개변수 콤마 누락·시그니처 불일치)
}


def _normalize(text: str) -> str:
    """원본의 사소한 결함 보정(컴파일 가능하게)."""
    # 1) 실수로 섞인 마크다운 코드펜스(``` / ```c) 줄 제거
    lines = [ln for ln in text.splitlines(keepends=True) if not re.match(r"^\s*```", ln)]
    text = "".join(lines)
    # 2) 잘못된 배열 초기화: char x[] = NULL;  →  char *x = NULL;
    text = re.sub(r"\bchar\s+(\w+)\s*\[\s*\]\s*=\s*NULL\b", r"char *\1 = NULL", text)
    return text


def _test_main_defines(text: str) -> str:
    """main 이 `#ifdef XXX_TEST_MAIN` 등으로 감싸진 경우, 그 매크로를 정의해
    main 이 컴파일되도록 prelude 에 추가할 #define 모음을 만든다."""
    macros = set(re.findall(r"#ifdef\s+(\w*(?:TEST|MAIN)\w*)", text))
    macros |= set(re.findall(r"#if\s+defined\s*\(\s*(\w*(?:TEST|MAIN)\w*)\s*\)", text))
    return "".join(f"#define {m}\n" for m in sorted(macros))

OUT = DATA_DIR / "problems" / "imported.json"

# Windows clang 호환 prelude (C). POSIX ssize_t 등이 없어서 보강.
COMPAT_C = r"""#if defined(_WIN32) && !defined(_SSIZE_T_DEFINED) && !defined(__ssize_t_defined)
typedef long long ssize_t;
#define _SSIZE_T_DEFINED
#endif
"""

_MAIN_RE = re.compile(r"^\s*int\s+main\s*\(", re.M)
_BOUNDARY_PATTERNS = [
    re.compile(r"^\s*//+\s*-*\s*Test\s*code", re.I),       # // --- Test code ---
    re.compile(r"^\s*/\*+\s*-*\s*Test\s*code", re.I),       # /* Test code */
    re.compile(r"^\s*(static\s+)?\w[\w ]*\btest_result\w*\s*\(", re.I),  # void test_result(
    re.compile(r"^\s*#define\s+RUN_TEST", re.I),
    re.compile(r"^\s*int\s+main\s*\("),                     # int main(
]
_COMMENT_BLOCK_RE = re.compile(r"/\*(.*?)\*/", re.S)
_NUM_RE = re.compile(r"(\d+)")
_FUNC_DEF_RE = re.compile(r"^[A-Za-z_].*\b(\w+)\s*\([^;{]*\)\s*\{", re.M)


def _mask(text: str) -> str:
    """주석/문자열/문자 리터럴을 공백으로 치환(개행 유지)한 코드 마스크.
    중괄호 스캔 정확도용."""
    out = []
    i, n = 0, len(text)
    state = "code"  # code | line | block | str | chr
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if state == "code":
            if c == "/" and nxt == "/":
                out.append("  "); i += 2; state = "line"; continue
            if c == "/" and nxt == "*":
                out.append("  "); i += 2; state = "block"; continue
            if c == '"':
                out.append('"'); i += 1; state = "str"; continue
            if c == "'":
                out.append("'"); i += 1; state = "chr"; continue
            out.append(c); i += 1; continue
        if state == "line":
            out.append("\n" if c == "\n" else " "); i += 1
            if c == "\n":
                state = "code"
            continue
        if state == "block":
            if c == "*" and nxt == "/":
                out.append("  "); i += 2; state = "code"; continue
            out.append("\n" if c == "\n" else " "); i += 1; continue
        if state == "str":
            if c == "\\":
                out.append("  "); i += 2; continue
            out.append('"' if c == '"' else (" " if c != "\n" else "\n"))
            i += 1
            if c == '"':
                state = "code"
            continue
        if state == "chr":
            if c == "\\":
                out.append("  "); i += 2; continue
            out.append("'" if c == "'" else (" " if c != "\n" else "\n"))
            i += 1
            if c == "'":
                state = "code"
            continue
    return "".join(out)


def stub_bodies(text: str) -> str:
    """최상위 함수 정의의 본문 `{...}`을 비워 스텁으로 만든다.
    구조체/typedef/전역 초기화 등 함수가 아닌 블록은 그대로 둔다."""
    mask = _mask(text)
    n = len(text)
    spans = []  # (body_start_brace_idx, body_end_brace_idx, ret_stub)
    i = 0
    last_boundary = 0  # 직전 최상위 ; 또는 } 또는 0
    while i < n:
        c = mask[i]
        if c == ";" :
            last_boundary = i + 1
            i += 1
            continue
        if c == "{":
            # depth 0 의 '{' — 직전 비공백 문자가 ')' 이면 함수 본문
            j = i - 1
            while j >= 0 and mask[j] in " \t\r\n":
                j -= 1
            is_func = j >= 0 and mask[j] == ")"
            # 매칭 '}' 찾기
            depth = 0
            k = i
            while k < n:
                if mask[k] == "{":
                    depth += 1
                elif mask[k] == "}":
                    depth -= 1
                    if depth == 0:
                        break
                k += 1
            if is_func:
                # 함수명 직전 타입 토큰만 추출(앞 줄/포함 #include 침범 방지).
                ret_part = re.sub(r"\s+", " ", mask[last_boundary:i]).split("(")[0]
                m = re.search(r"([A-Za-z_][\w\s\*]*?)\s*([A-Za-z_]\w*)\s*$", ret_part)
                rettype = ""
                if m:
                    rettype = re.sub(r"^(static|inline|extern)\s+", "", m.group(1).strip()).strip()
                # void → 반환 없음, 그 외 → return 0 (스칼라/포인터 호환).
                if rettype == "void":
                    ret = ""
                else:
                    ret = "    return 0;\n"
                spans.append((i, k, ret))
            last_boundary = k + 1
            i = k + 1
            continue
        i += 1
    if not spans:
        return text
    # 원본을 스텁으로 재조립
    res = []
    prev = 0
    for start, end, ret in spans:
        res.append(text[prev:start + 1])  # ...{
        res.append("\n    // TODO: implement\n" + ret)
        prev = end  # } 부터 이어서
    res.append(text[prev:])
    return "".join(res)


def split_index(lines: list[str]) -> int | None:
    """테스트 영역이 시작하는 줄 인덱스. 없으면 None."""
    best = None
    for i, ln in enumerate(lines):
        for pat in _BOUNDARY_PATTERNS:
            if pat.search(ln):
                if best is None or i < best:
                    best = i
                break
    return best


def extract_description(starter: str) -> str:
    """상단 주석 블록 중 '번호.'로 시작하는 문제 설명들을 모은다."""
    blocks = []
    for m in _COMMENT_BLOCK_RE.finditer(starter):
        body = m.group(1).strip("\n")
        if re.search(r"^\s*\d+\.", body, re.M) or "Input" in body or "Output" in body:
            blocks.append(body.strip())
    text = "\n\n".join(blocks).strip()
    return text[:6000]


def extract_expected(full: str) -> str:
    """말미의 Expected/Example 주석 블록."""
    found = ""
    for m in _COMMENT_BLOCK_RE.finditer(full):
        body = m.group(1)
        if re.search(r"Expected|Example Input/Output|Expected Result", body, re.I):
            found = body.strip()
    return found[:4000]


def difficulty_for(name: str) -> str:
    n = name.lower()
    if any(k in n for k in ("fsm", "linkedlist", "ll", "cirbuf", "buf", "cb",
                            "spsc", "mpmc", "concurr", "watchdog", "isr", "scheduler",
                            "priority", "interrupt", "dma")):
        return "Hard"
    if any(k in n for k in ("str", "mem", "data", "manip", "process", "serial")):
        return "Medium"
    if any(k in n for k in ("bit", "basic", "parity", "op")):
        return "Easy"
    return "Medium"


def title_and_range(stem: str) -> tuple[str, str]:
    """'1-10-basicOps' -> ('basicOps', '1-10')."""
    m = re.match(r"^([\d\-]+)[-_](.+)$", stem)
    if m:
        return m.group(2), m.group(1)
    return stem, ""


def lead_num(stem: str) -> int:
    m = _NUM_RE.search(stem)
    return int(m.group(1)) if m else 9999


def slugify_id(folder_key: str, stem: str) -> str:
    s = re.sub(r"[^a-zA-Z0-9]+", "-", f"{folder_key}-{stem}").strip("-").lower()
    return s


def import_file(path: Path, category: str, folder_key: str) -> dict | None:
    if path.name in _SKIP_FILES:
        return None
    text = _normalize(path.read_text(encoding="utf-8", errors="replace"))
    if not _MAIN_RE.search(text):
        return None  # 테스트 main 없음 → 채점 불가, 스킵
    if "pthread.h" in text:
        return None  # pthread는 Windows clang에서 컴파일 불가 → 스킵
    lines = text.splitlines(keepends=True)
    idx = split_index(lines)
    if idx is None:
        return None
    before = "".join(lines[:idx]).rstrip() + "\n"
    harness = "".join(lines[idx:]).rstrip() + "\n"
    if len(before.strip()) < 20 or "main" not in harness:
        return None

    # 본문이 TODO 스텁이면(Anduril 스타일) before가 곧 starter, 솔루션은 _sol에서.
    # 본문이 완성 구현이면 before가 솔루션, starter는 본문을 비운 스텁.
    is_stub = "TODO" in before
    if is_stub:
        starter = before
        solution = _solution_from_sibling(path, idx)
    else:
        solution = before
        starter = stub_bodies(before)

    grade_mode = "assert" if re.search(r"\bassert\s*\(", harness) else ""

    stem = path.stem
    title, rng = title_and_range(stem)
    lang = "cpp" if path.suffix == ".cpp" else "c"
    # main 이 #ifdef ..._TEST_MAIN 로 감싸진 경우 해당 매크로 정의
    prelude = (COMPAT_C if lang == "c" else "") + _test_main_defines(text)
    return {
        "id": slugify_id(folder_key, stem),
        "num": lead_num(stem),
        "title": title,
        "range": rng,
        "difficulty": difficulty_for(stem),
        "category": category,
        "lang": lang,
        "source_file": str(path.relative_to(REPO)),
        "description": extract_description(before),
        "expected_output": extract_expected(text),
        "starter": starter,
        "solution": solution,
        "harness": harness,
        "grade_mode": grade_mode,
        "prelude": prelude,
    }


def _has_written(pid: str) -> bool:
    """직접 작성한 솔루션 override(data/problems/written_solutions/<id>.c) 존재 여부."""
    f = DATA_DIR / "problems" / "written_solutions" / f"{pid}.c"
    return f.exists() and bool(f.read_text(encoding="utf-8").strip())


def _solution_from_sibling(path: Path, idx: int) -> str:
    """`<name>_sol.c` 형제 파일이 있으면 그 구현부(테스트 전까지)를 솔루션으로."""
    sol = path.with_name(path.stem + "_sol" + path.suffix)
    if not sol.exists():
        return ""
    text = sol.read_text(encoding="utf-8", errors="replace")
    if "TODO" in text.split("int main")[0]:
        return ""  # _sol도 미구현이면 솔루션 없음
    lines = text.splitlines(keepends=True)
    sidx = split_index(lines)
    if sidx is None:
        return ""
    return "".join(lines[:sidx]).rstrip() + "\n"


def gather() -> list[dict]:
    out: list[dict] = []
    for rel, category in TARGETS:
        folder = REPO / rel
        if not folder.exists():
            print(f"[warn] 폴더 없음: {folder}", file=sys.stderr)
            continue
        parts = Path(rel).parts
        key_src = parts[-2] if len(parts) >= 2 else parts[-1]
        folder_key = re.sub(r"[^a-zA-Z0-9]+", "-", key_src).lower()[:16]
        files = sorted(
            p for p in folder.glob("*")
            if p.suffix in (".c", ".cpp")
            and not any(s in p.stem for s in _SKIP_STEMS)
        )
        from code_runner import compile_and_run

        for p in files:
            prob = import_file(p, category, folder_key)
            if not prob:
                print(f"[skip] {p.name:38s} (main/pthread 없음·미지원)")
                continue
            # 자가검증: 스텁+하네스가 컴파일되어야 채점 가능 → 안 되면 제외
            r = compile_and_run(prob["starter"], prob["harness"], prob["prelude"], prob["lang"])
            if not r.get("compiled"):
                err = (r.get("compile_stderr") or r.get("error") or "")[:70].replace("\n", " ")
                print(f"[drop] {p.name:38s} (스텁 컴파일 실패: {err})")
                continue
            # 솔루션이 있으면 정답 검증. 솔루션이 컴파일조차 안 되면 풀 수 없는
            # 문제이므로 제외(원본 결함). written override 가 있으면 그건 통과로 간주.
            sol_ok = "—"
            if prob.get("solution") and not _has_written(prob["id"]):
                rs = compile_and_run(prob["solution"], prob["harness"], prob["prelude"],
                                     prob["lang"], prob.get("grade_mode", ""))
                if not rs.get("compiled"):
                    print(f"[drop] {p.name:38s} (솔루션 컴파일 실패 — 풀 수 없는 원본)")
                    continue
                if rs.get("mode") in ("assert", "result", "passfail"):
                    sol_ok = f"sol {rs.get('passed')}/{rs.get('total')}"
                else:
                    sol_ok = "sol(manual)"
            out.append(prob)
            print(f"[ok]   {p.name:38s} {prob['lang']:3s} "
                  f"grade={prob.get('grade_mode') or 'auto/manual':12s} {sol_ok}")
    out.sort(key=lambda x: (x["category"], x["num"]))
    return out


def verify(problems: list[dict]) -> None:
    """각 문제의 starter+harness 가 컴파일되는지 점검 (스텁이라 테스트는 실패해도 됨)."""
    from code_runner import compile_and_run
    ok = fail = 0
    for p in problems:
        r = compile_and_run(p["starter"], p["harness"], p.get("prelude", ""), p["lang"])
        if r.get("compiled"):
            ok += 1
        else:
            fail += 1
            print(f"[COMPILE-FAIL] {p['id']}\n   "
                  + (r.get("compile_stderr", "")[:300].replace("\n", "\n   ")),
                  file=sys.stderr)
    print(f"\n[verify] 컴파일 OK {ok} · 실패 {fail}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--verify", action="store_true")
    args = ap.parse_args()

    problems = gather()
    OUT.parent.mkdir(parents=True, exist_ok=True)
    write_json(OUT, problems)
    print(f"\n[done] {len(problems)}개 문제 → {OUT}")
    if args.verify:
        verify(problems)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
