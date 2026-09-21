#!/usr/bin/env python3
"""verkada_prep 문제 은행 전체 검증.

  python3 validate.py          # JSON 스키마 + 컴파일 + 실행 + stub 확인
  python3 validate.py --tsan   # 위 + ThreadSanitizer (동시성 세트)
"""
import json, glob, os, re, subprocess, sys

ROOT = os.path.dirname(os.path.abspath(__file__))
CC = os.environ.get("CC", "cc")
CFLAGS = ["-std=c11", "-Wall", "-Wextra", "-O1", "-g", "-pthread"]
BUILD = os.path.join(ROOT, "build")
REQ_SET = ["id", "order", "range", "icon", "title_en", "title_ko", "summary_ko",
           "summary_en", "verkada_relevance_ko", "concepts", "study_note_md", "problems"]
REQ_PROB = ["num", "slug", "title_ko", "title_en", "difficulty", "tags", "prompt_ko",
            "prompt_en", "signature", "examples", "hints", "solution", "explanation_ko",
            "complexity", "pitfalls", "verkada_context"]
DIFFS = {"easy", "medium", "hard"}


def run(cmd, timeout=180):
    return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, cwd=ROOT)


def main():
    want_tsan = "--tsan" in sys.argv
    os.makedirs(BUILD, exist_ok=True)
    sets = sorted(glob.glob(os.path.join(ROOT, "data", "[0-9]*.json")))
    if not sets:
        print("data/NN_*.json 이 없습니다"); return 1

    errors, warns, total_prob, total_checks = [], [], 0, 0
    print(f"{'세트':<24} {'문제':>4} {'체크':>5} {'JSON':>6} {'sol':>6} {'prob':>6}")
    print("-" * 60)

    for jf in sets:
        name = os.path.basename(jf)[:-5]
        row = {"json": "?", "sol": "-", "prob": "-", "nprob": 0, "checks": 0}
        # ---- JSON 스키마
        try:
            d = json.load(open(jf, encoding="utf-8"))
            miss = [k for k in REQ_SET if k not in d]
            if miss:
                errors.append(f"{name}: 세트 키 누락 {miss}")
            probs = d.get("problems", [])
            row["nprob"] = len(probs)
            nums = []
            for p in probs:
                pm = [k for k in REQ_PROB if k not in p]
                if pm:
                    errors.append(f"{name}/{p.get('slug','?')}: 문제 키 누락 {pm}")
                if p.get("difficulty") not in DIFFS:
                    errors.append(f"{name}/{p.get('slug')}: difficulty={p.get('difficulty')}")
                if len(p.get("hints", [])) != 3:
                    warns.append(f"{name}/{p.get('slug')}: hints {len(p.get('hints', []))}개 (3개 권장)")
                if not str(p.get("solution", "")).strip():
                    errors.append(f"{name}/{p.get('slug')}: solution 비어 있음")
                nums.append(p.get("num"))
            if nums != sorted(nums):
                errors.append(f"{name}: 문제 num 이 오름차순이 아님")
            if not d.get("study_note_md", "").strip():
                errors.append(f"{name}: study_note_md 비어 있음")
            row["json"] = "OK"
        except Exception as e:                       # noqa: BLE001
            errors.append(f"{name}: JSON 파싱 실패 {e}")
            row["json"] = "FAIL"

        # ---- 노트 파일
        if not os.path.exists(os.path.join(ROOT, "notes", name + ".md")):
            errors.append(f"{name}: notes/{name}.md 없음")

        # ---- 솔루션 컴파일 + 실행
        src = os.path.join(ROOT, "solutions", name + ".c")
        if not os.path.exists(src):
            errors.append(f"{name}: solutions/{name}.c 없음")
        else:
            exe = os.path.join(BUILD, name + "_sol")
            c = run([CC, *CFLAGS, src, "-o", exe])
            if c.returncode != 0:
                errors.append(f"{name}: 컴파일 실패\n{c.stderr[:800]}")
                row["sol"] = "CC!"
            else:
                if c.stderr.strip():
                    warns.append(f"{name}: 컴파일 경고 {c.stderr.count('warning:')}건")
                try:
                    r = run([exe], timeout=90)
                    m = re.search(r"====\s*(\d+) passed,\s*(\d+) failed\s*====", r.stdout)
                    if not m:
                        errors.append(f"{name}: 요약 줄 없음 (출력 끝: {r.stdout[-200:]!r})")
                        row["sol"] = "FMT!"
                    else:
                        npass, nfail = int(m.group(1)), int(m.group(2))
                        row["checks"] = npass + nfail
                        row["sol"] = f"{npass}P" if nfail == 0 else f"{nfail}F!"
                        if nfail or r.returncode != 0:
                            errors.append(f"{name}: 실행 실패 {nfail} FAIL (exit {r.returncode})")
                except subprocess.TimeoutExpired:
                    errors.append(f"{name}: 실행 타임아웃(90s) — 데드락 의심")
                    row["sol"] = "HANG!"

        # ---- stub 컴파일 + 실행 (FAIL 나는 건 정상, 멈추면 안 됨)
        psrc = os.path.join(ROOT, "problems", name + ".c")
        if not os.path.exists(psrc):
            errors.append(f"{name}: problems/{name}.c 없음")
        else:
            body = open(psrc, encoding="utf-8").read()
            if "TODO" not in body:
                errors.append(f"{name}: stub 에 TODO 가 없음")
            exe = os.path.join(BUILD, name + "_prob")
            c = run([CC, *CFLAGS, psrc, "-o", exe])
            if c.returncode != 0:
                errors.append(f"{name}: stub 컴파일 실패\n{c.stderr[:500]}")
                row["prob"] = "CC!"
            else:
                try:
                    r = run([exe], timeout=90)
                    row["prob"] = "run"
                except subprocess.TimeoutExpired:
                    errors.append(f"{name}: stub 실행이 멈춤(90s) — 미구현 상태에서 hang")
                    row["prob"] = "HANG!"

        # ---- TSan (선택)
        if want_tsan and os.path.exists(src) and "pthread_create" in open(src, encoding="utf-8").read():
            exe = os.path.join(BUILD, name + "_tsan")
            c = run([CC, *CFLAGS, "-fsanitize=thread", src, "-o", exe])
            if c.returncode == 0:
                try:
                    r = run([exe], timeout=180)
                    nrace = r.stderr.count("WARNING: ThreadSanitizer") + r.stdout.count("WARNING: ThreadSanitizer")
                    if nrace:
                        warns.append(f"{name}: TSan 경고 {nrace}건 (의도된 race 인지 확인)")
                except subprocess.TimeoutExpired:
                    warns.append(f"{name}: TSan 실행 타임아웃")

        total_prob += row["nprob"]
        total_checks += row["checks"]
        print(f"{name:<24} {row['nprob']:>4} {row['checks']:>5} {row['json']:>6} {row['sol']:>6} {row['prob']:>6}")

    print("-" * 60)
    print(f"{'합계':<24} {total_prob:>4} {total_checks:>5}")
    for w in warns:
        print("  [warn]", w)
    for e in errors:
        print("  [ERROR]", e)
    print(f"\n세트 {len(sets)}개 · 문제 {total_prob}개 · 체크 {total_checks}개 · "
          f"오류 {len(errors)}건 · 경고 {len(warns)}건")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
