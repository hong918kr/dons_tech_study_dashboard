"""
C/C++ 코드를 clang/clang++ 으로 컴파일하고 테스트를 실행한다.

최종 소스 = prelude + 사용자코드 + harness
채점:
  1) "RESULT pass total" 라인이 있으면 그 값
  2) 없으면 "[PASS]"/"[FAIL]" 라인 개수를 카운트
  3) 둘 다 없으면 채점 불가(manual) — 출력만 보여줌
"""
from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
from pathlib import Path

# 직접 만든 문제(01~10)용 공통 헤더: CHECK/DONE 매크로.
COMMON_HEADER = r"""#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static int _pass = 0, _total = 0;
#define CHECK(cond, ...) do { _total++; \
    if (cond) { _pass++; printf("[PASS] "); } else { printf("[FAIL] "); } \
    printf(__VA_ARGS__); printf("\n"); } while (0)
#define DONE() printf("RESULT %d %d\n", _pass, _total)
"""

COMPILE_TIMEOUT = 40
RUN_TIMEOUT = 6

_C_COMPILERS = ("clang", "gcc", "cc")
_CPP_COMPILERS = ("clang++", "g++", "c++")


def find_compiler(lang: str = "c") -> str | None:
    cands = _CPP_COMPILERS if lang == "cpp" else _C_COMPILERS
    for cc in cands:
        if shutil.which(cc):
            return cc
    return None


def grade(stdout: str):
    """반환 (passed, total, mode). mode = 'result' | 'passfail' | 'manual'."""
    passed = total = 0
    saw_result = False
    for line in stdout.splitlines():
        if line.startswith("RESULT "):
            parts = line.split()
            if len(parts) >= 3:
                try:
                    passed, total = int(parts[1]), int(parts[2])
                    saw_result = True
                except ValueError:
                    pass
    if saw_result:
        return passed, total, "result"
    up = stdout.upper()
    np_, nf = up.count("[PASS]"), up.count("[FAIL]")
    if np_ + nf > 0:
        return np_, np_ + nf, "passfail"
    return 0, 0, "manual"


def compile_and_run(
    user_code: str, harness: str, prelude: str = "", lang: str = "c", grade_mode: str = ""
) -> dict:
    cc = find_compiler(lang)
    if not cc:
        need = "clang++/g++" if lang == "cpp" else "clang/gcc"
        return {"ok": False, "error": f"{need} 컴파일러를 찾을 수 없습니다."}

    src = (prelude or "") + "\n" + user_code + "\n\n" + harness + "\n"
    work = Path(tempfile.mkdtemp(prefix="cprob_"))
    try:
        ext = "cpp" if lang == "cpp" else "c"
        src_path = work / f"solution.{ext}"
        src_path.write_text(src, encoding="utf-8")
        exe_path = work / ("solution.exe" if os.name == "nt" else "solution")
        std = "-std=c++17" if lang == "cpp" else "-std=c11"

        try:
            comp = subprocess.run(
                [cc, std, "-O0", "-o", str(exe_path), str(src_path)],
                capture_output=True,
                text=True,
                timeout=COMPILE_TIMEOUT,
            )
        except subprocess.TimeoutExpired:
            return {"ok": True, "compiled": False, "compile_stderr": "컴파일 시간 초과", "compiler": cc}

        if comp.returncode != 0:
            return {
                "ok": True,
                "compiled": False,
                "compile_stderr": comp.stderr or comp.stdout or "컴파일 실패",
                "compiler": cc,
            }

        try:
            run = subprocess.run(
                [str(exe_path)], capture_output=True, text=True, timeout=RUN_TIMEOUT
            )
        except subprocess.TimeoutExpired:
            return {
                "ok": True,
                "compiled": True,
                "timed_out": True,
                "stdout": "",
                "runtime_stderr": f"실행 시간 초과 ({RUN_TIMEOUT}s) — 무한 루프 의심",
                "passed": 0,
                "total": 0,
                "mode": "manual",
                "compiler": cc,
            }

        passed, total, mode = grade(run.stdout)
        # assert 기반 테스트: stdout에 PASS/RESULT가 없으면 종료코드로 판정
        if mode == "manual" and grade_mode == "assert":
            ok_exit = run.returncode == 0
            passed, total, mode = (1 if ok_exit else 0), 1, "assert"
        return {
            "ok": True,
            "compiled": True,
            "timed_out": False,
            "stdout": run.stdout,
            "runtime_stderr": run.stderr,
            "returncode": run.returncode,
            "passed": passed,
            "total": total,
            "mode": mode,
            "compiler": cc,
        }
    finally:
        shutil.rmtree(work, ignore_errors=True)
