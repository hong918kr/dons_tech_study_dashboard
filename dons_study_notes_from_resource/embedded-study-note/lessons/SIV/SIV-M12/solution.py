#!/usr/bin/env python3
"""SIV-M12 Python 테스트 시퀀서 — YAML 테스트 리스트 → C 테스트(ctypes / 바이너리) → 장비 mock → SQLite → 리포트

  Part A  YAML 부분집합 파서 + 테스트 리스트 검증 + ctypes 로 C 테스트 라이브러리(siv_tests.c) 호출
  Part B  바이너리 러너(subprocess 타임아웃·크래시 격리), 재시도와 FLAKY 판정, 사전조건(requires), teardown 보장
  Part C  nightly 시나리오: 3 코너 × 6 테스트, 불량 샘플(결함 4종) → SQLite 결과 DB → 요약 리포트 · 종료 코드

  python3 -I solution.py                                    # 테스트 ("N/M passed", 실패 시 exit 1)
  python3 -I solution.py --run testlist.yaml --defects 0xF  # 시퀀스 1회 + 리포트 출력, exit 0/1/2
        [--db out.db] [--started 2026-11-08T02:00:00Z] [--flaky-fails]

Python 3.9 표준 라이브러리만 쓴다 (ctypes, sqlite3, subprocess, tempfile, json, re, zlib, difflib, dataclasses).
PyYAML 이 없으므로 YAML 은 문서화된 부분집합만 직접 파싱한다 (md §5 Part A).
siv_tests.c 는 하네스가 임시 디렉터리에 dylib + 바이너리로 직접 빌드한다 (strict cc 플래그).
"""
import ctypes
import dataclasses
import difflib
import json
import os
import re
import signal
import sqlite3
import subprocess
import sys
import tempfile
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
SDK_FALLBACK = "/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk"  # 이 맥의 27.2 SDK 링크 문제 회피
CFLAGS = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-O2"]
API_MAJOR = 1
RUNNERS = ("ctypes", "binary")
BAD = ("FAIL", "TIMEOUT", "CRASH", "ERROR")  # 하나라도 있으면 exit 1
STATUS_ORDER = ("PASS", "FLAKY", "FAIL", "TIMEOUT", "CRASH", "ERROR", "SKIP")


def build_c(outdir):
    """siv_tests.c → (dylib, 바이너리). 같은 소스를 strict 플래그로 두 번 빌드한다."""
    env = dict(os.environ)
    if "SDKROOT" not in env and os.path.isdir(SDK_FALLBACK):
        env["SDKROOT"] = SDK_FALLBACK
    src = os.path.join(HERE, "siv_tests.c")
    lib = os.path.join(outdir, "siv_tests.dylib")
    exe = os.path.join(outdir, "siv_tests_bin")
    for cmd in (["cc"] + CFLAGS + ["-dynamiclib", src, "-o", lib],
                ["cc"] + CFLAGS + [src, "-o", exe]):
        p = subprocess.run(cmd, capture_output=True, text=True, env=env)
        if p.returncode != 0:
            raise RuntimeError("build failed: %s\n%s" % (" ".join(cmd), p.stderr))
    return lib, exe


# ======================================================================== Part A: YAML 부분집합
class YamlError(ValueError):
    def __init__(self, line, msg):
        super().__init__("line %d: %s" % (line, msg))
        self.line = line


INT_RE = re.compile(r"^[-+]?(0[xX][0-9a-fA-F]+|0|[1-9][0-9]*)$")
FLOAT_RE = re.compile(r"^[-+]?(\d+\.\d*|\.\d+|\d+(\.\d*)?[eE][-+]?\d+)$")
KEY_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_.-]*)\s*:(?:\s+(.*))?$")


def parse_scalar(s):
    """YAML 스칼라 하나 → Python 값.
    null ~ → None / true false → bool (소문자만) / 10진·0x 정수 → int / 소수·지수 → float /
    "..." '...' → str / 그 밖 → 맨 문자열.  0750 처럼 앞자리 0 정수, & * ! | > { 로 시작하는 문법,
    따옴표 없는 ': ' 는 ValueError (모호하거나 지원 안 함 — 조용히 틀리게 읽느니 거부)."""
    s = s.strip()
    if s == "":
        return None
    if s[0] == '"':
        if len(s) < 2 or s[-1] != '"':
            raise ValueError("unterminated double-quoted string %r" % s)
        return re.sub(r'\\(["\\])', r"\1", s[1:-1])
    if s[0] == "'":
        if len(s) < 2 or s[-1] != "'":
            raise ValueError("unterminated single-quoted string %r" % s)
        return s[1:-1].replace("''", "'")
    if s in ("null", "~"):
        return None
    if s == "true":
        return True
    if s == "false":
        return False
    if re.match(r"^[-+]?0[0-9]+$", s):
        raise ValueError("ambiguous leading-zero number %r (YAML 1.1 octal?) - quote it or use 0x" % s)
    if INT_RE.match(s):
        return int(s, 0)
    if FLOAT_RE.match(s):
        return float(s)
    if s[0] in "&*!|>{}%@`":
        raise ValueError("unsupported YAML syntax %r (anchors/tags/block scalars/flow maps)" % s)
    if ": " in s:
        raise ValueError("quote strings containing ': ' -> %r" % s)
    return s


def parse_value(s):
    """스칼라 또는 한 줄 flow 리스트 [a, b, 0x10] (중첩 없음)"""
    s = s.strip()
    if s.startswith("["):
        if not s.endswith("]"):
            raise ValueError("unterminated flow list %r" % s)
        inner = s[1:-1].strip()
        if inner == "":
            return []
        items = [x.strip() for x in inner.split(",")]
        if any(x == "" for x in items) or any(x[0] in "[{" for x in items):
            raise ValueError("bad flow list %r (empty item or nesting)" % s)
        return [parse_scalar(x) for x in items]
    return parse_scalar(s)


def _strip_comment(line):
    """따옴표 밖의 ' #' 또는 줄 맨 앞 '#' 부터 끝까지 지운다"""
    q = None
    for i, ch in enumerate(line):
        if q:
            if ch == q:
                q = None
        elif ch in "\"'":
            q = ch
        elif ch == "#" and (i == 0 or line[i - 1] in " \t"):
            return line[:i].rstrip()
    return line.rstrip()


def _logical_lines(text):
    """→ [[줄번호, 들여쓰기, 내용], ...] (빈 줄·주석 제거). 탭 들여쓰기·다중 문서는 거부."""
    out = []
    for n, raw in enumerate(text.splitlines(), 1):
        lead = raw[:len(raw) - len(raw.lstrip(" \t"))]
        if "\t" in lead:
            raise YamlError(n, "tab in indentation (YAML forbids tabs)")
        body = _strip_comment(raw)
        if body.strip() == "":
            continue
        if body.strip() in ("---", "..."):
            raise YamlError(n, "multiple documents ('---') not supported")
        out.append([n, len(body) - len(body.lstrip(" ")), body.strip()])
    return out


def _value(line, s):
    try:
        return parse_value(s)
    except ValueError as e:
        raise YamlError(line, str(e))


def _is_item(content):
    return content == "-" or content.startswith("- ")


def _parse_block(lines, i, indent):
    if _is_item(lines[i][2]):
        return _parse_list(lines, i, indent)
    return _parse_map(lines, i, indent)


def _parse_map(lines, i, indent):
    """같은 들여쓰기의 'key: value' 줄들 → dict. 반환 (dict, 다음 줄 index)"""
    out = {}
    while i < len(lines) and lines[i][1] == indent and not _is_item(lines[i][2]):
        ln, _, content = lines[i]
        m = KEY_RE.match(content)
        if not m:
            raise YamlError(ln, "expected 'key: value', got %r" % content)
        key, rest = m.group(1), m.group(2)
        if key in out:
            raise YamlError(ln, "duplicate key %r" % key)
        i += 1
        if rest is None or rest.strip() == "":
            if i < len(lines) and lines[i][1] > indent:
                out[key], i = _parse_block(lines, i, lines[i][1])
            elif i < len(lines) and lines[i][1] == indent and _is_item(lines[i][2]):
                out[key], i = _parse_list(lines, i, indent)  # "key:" 밑에 같은 칸 "- ..." 도 YAML 에선 합법
            else:
                out[key] = None
        else:
            out[key] = _value(ln, rest)
    if i < len(lines) and lines[i][1] > indent:
        raise YamlError(lines[i][0], "unexpected indentation")
    return out, i


def _parse_list(lines, i, indent):
    """같은 들여쓰기의 '- ...' 줄들 → list. '- key: v' 항목은 매핑 (나머지 키는 '-' 다음 칸에 정렬)"""
    out = []
    while i < len(lines) and lines[i][1] == indent and _is_item(lines[i][2]):
        ln, _, content = lines[i]
        rest = content[1:].lstrip(" ")
        if rest == "":
            i += 1
            if i < len(lines) and lines[i][1] > indent:
                v, i = _parse_block(lines, i, lines[i][1])
                out.append(v)
            else:
                out.append(None)
        elif KEY_RE.match(rest):
            sub = indent + (len(content) - len(rest))
            lines[i] = [ln, sub, rest]  # "- name: x" 를 칸 sub 에 있는 "name: x" 로 바꿔 읽는다
            v, i = _parse_map(lines, i, sub)
            out.append(v)
        else:
            out.append(_value(ln, rest))
            i += 1
    if i < len(lines) and lines[i][1] > indent:
        raise YamlError(lines[i][0], "unexpected indentation")
    return out, i


def parse_yaml(text):
    lines = _logical_lines(text)
    if not lines:
        return None
    if lines[0][1] != 0:
        raise YamlError(lines[0][0], "document must start at column 0")
    val, i = _parse_block(lines, 0, 0)
    if i != len(lines):
        raise YamlError(lines[i][0], "unexpected content at indent %d" % lines[i][1])
    return val


# ======================================================================== Part A: 테스트 리스트 검증
@dataclasses.dataclass
class Corner:
    name: str
    vdd_mv: int
    temp_c: int


@dataclasses.dataclass
class TestSpec:
    name: str
    func: str
    runner: str = "ctypes"
    timeout_s: float = 5.0
    retries: int = 0
    requires: list = dataclasses.field(default_factory=list)
    corners: list = dataclasses.field(default_factory=list)  # 빈 리스트 = 모든 코너
    params: dict = dataclasses.field(default_factory=dict)


@dataclasses.dataclass
class Plan:
    run: dict
    limits: dict
    corners: list
    tests: list


SCHEMA = {
    "top": {"run": ("map", True), "limits": ("map", True), "corners": ("list", True), "tests": ("list", True)},
    "run": {"name": ("str", True), "board": ("str", True), "fw": ("str", True), "seed": ("int", True)},
    "limits": {"vdd_mv_max": ("int", True), "temp_c_min": ("int", True), "temp_c_max": ("int", True)},
    "corner": {"name": ("str", True), "vdd_mv": ("int", True), "temp_c": ("int", True)},
    "test": {"name": ("str", True), "func": ("str", True), "runner": ("str", False), "timeout_s": ("num", False),
             "retries": ("int", False), "requires": ("list", False), "corners": ("list", False),
             "params": ("map", False)},
}
TEST_NAME_RE = re.compile(r"^[a-z][a-z0-9_]*(\.[a-z0-9_]+)*$")


def _is(v, kind):
    if isinstance(v, bool):
        return kind == "bool"
    return {"int": isinstance(v, int), "num": isinstance(v, (int, float)), "str": isinstance(v, str),
            "list": isinstance(v, list), "map": isinstance(v, dict)}[kind]


def _check_keys(where, obj, schema, errs):
    """모르는 키(오타 → did you mean), 빠진 필수 키, 타입. 매핑이 아니면 False"""
    if not isinstance(obj, dict):
        errs.append("E_TYPE %s: expected mapping, got %s" % (where, type(obj).__name__))
        return False
    for k in obj:
        if k not in schema:
            near = difflib.get_close_matches(k, list(schema), n=1)
            errs.append("E_KEY %s: unknown key '%s'%s" % (where, k, " (did you mean '%s'?)" % near[0] if near else ""))
    for k, (kind, req) in schema.items():
        if k not in obj:
            if req:
                errs.append("E_MISSING %s: required key '%s'" % (where, k))
        elif not _is(obj[k], kind):
            v = obj[k]
            hint = " (quote it)" if kind == "str" and isinstance(v, (int, float)) else ""
            errs.append("E_TYPE %s.%s: expected %s, got %s %r%s" % (where, k, kind, type(v).__name__, v, hint))
    return True


def _validate_test(i, t, defined, corner_names, known_funcs, errs):
    """tests[i] 하나 검증 → TestSpec (에러가 있어도 만들 수 있는 만큼 만들어 반환)"""
    where = "tests[%d]" % i
    if not _check_keys(where, t, SCHEMA["test"], errs):
        return None
    name = t.get("name") if isinstance(t.get("name"), str) else "?"
    where = "%s %s" % (where, name)
    if name != "?" and not TEST_NAME_RE.match(name):
        errs.append("E_NAME %s: test name must match %s" % (where, TEST_NAME_RE.pattern))
    if name in defined:
        errs.append("E_DUPNAME %s: duplicate test name '%s'" % (where, name))
    func = t.get("func")
    if isinstance(func, str) and func not in known_funcs:
        errs.append("E_FUNC %s: func '%s' not in C library (have: %s)" % (where, func, ", ".join(sorted(known_funcs))))
    runner = t.get("runner", "ctypes")
    if isinstance(runner, str) and runner not in RUNNERS:
        errs.append("E_RUNNER %s: runner '%s' not in %s" % (where, runner, "/".join(RUNNERS)))
    tmo = t.get("timeout_s", 5.0)
    if _is(tmo, "num") and not (0 < tmo <= 600):
        errs.append("E_RANGE %s: timeout_s %r not in (0, 600]" % (where, tmo))
    retries = t.get("retries", 0)
    if _is(retries, "int") and not (0 <= retries <= 3):
        errs.append("E_RANGE %s: retries %r not in 0..3" % (where, retries))
    requires = t.get("requires", [])
    for r in requires if isinstance(requires, list) else []:
        if r not in defined:
            errs.append("E_REQUIRES %s: requires '%s' which is not defined earlier" % (where, r))
    corners = t.get("corners", [])
    for c in corners if isinstance(corners, list) else []:
        if c not in corner_names:
            errs.append("E_CORNER %s: unknown corner '%s'" % (where, c))
    params = t.get("params", {})
    for k, v in (params.items() if isinstance(params, dict) else []):
        if not re.match(r"^[a-z_][a-z0-9_]*$", k) or not _is(v, "int") or not (0 <= v <= 0xFFFFFFFF):
            errs.append("E_PARAM %s: param %s=%r must be a u32 integer" % (where, k, v))
    defined.add(name)
    return TestSpec(name=name, func=func if isinstance(func, str) else "?", runner=runner,
                    timeout_s=float(tmo) if _is(tmo, "num") else 5.0,
                    retries=retries if _is(retries, "int") else 0,
                    requires=list(requires) if isinstance(requires, list) else [],
                    corners=list(corners) if isinstance(corners, list) else [],
                    params=dict(params) if isinstance(params, dict) else {})


def validate_plan(doc, known_funcs):
    """parse_yaml 결과 → (Plan 또는 None, 에러 목록). 장비를 건드리기 전에 전부 검사한다."""
    errs = []
    if not _check_keys("top", doc, SCHEMA["top"], errs):
        return None, errs
    run = doc.get("run") if isinstance(doc.get("run"), dict) else {}
    _check_keys("run", run, SCHEMA["run"], errs)
    lim = doc.get("limits") if isinstance(doc.get("limits"), dict) else {}
    _check_keys("limits", lim, SCHEMA["limits"], errs)
    vmax, tmin, tmax = lim.get("vdd_mv_max", 0), lim.get("temp_c_min", 0), lim.get("temp_c_max", 0)
    corners, cnames = [], set()
    for i, c in enumerate(doc.get("corners") or []):
        if not _check_keys("corners[%d]" % i, c, SCHEMA["corner"], errs):
            continue
        name, v, t = c.get("name"), c.get("vdd_mv"), c.get("temp_c")
        if name in cnames:
            errs.append("E_DUPNAME corners[%d]: duplicate corner '%s'" % (i, name))
        if _is(v, "int") and _is(vmax, "int") and not (0 < v <= vmax):
            errs.append("E_LIMIT corners[%d] %s: vdd_mv %d not in (0, vdd_mv_max %d]" % (i, name, v, vmax))
        if _is(t, "int") and _is(tmin, "int") and _is(tmax, "int") and not (tmin <= t <= tmax):
            errs.append("E_LIMIT corners[%d] %s: temp_c %d not in [%d, %d]" % (i, name, t, tmin, tmax))
        cnames.add(name)
        corners.append(Corner(name, v, t))
    if not corners:
        errs.append("E_MISSING corners: at least one corner")
    tests, defined = [], set()
    for i, t in enumerate(doc.get("tests") or []):
        spec = _validate_test(i, t, defined, cnames, known_funcs, errs)
        if spec:
            tests.append(spec)
    if not tests:
        errs.append("E_MISSING tests: at least one test")
    if errs:
        return None, errs
    return Plan(run=run, limits=lim, corners=corners, tests=tests), []


# ======================================================================== Part A: ctypes 러너
class SivResult(ctypes.Structure):
    """siv_tests.c 의 siv_result_t 와 바이트 단위로 같아야 한다 (siv_result_size() 로 확인)"""
    _fields_ = [("status", ctypes.c_int32), ("checks", ctypes.c_uint32), ("addr", ctypes.c_uint32),
                ("expected", ctypes.c_uint32), ("actual", ctypes.c_uint32),
                ("reg", ctypes.c_char * 16), ("msg", ctypes.c_char * 96)]


C_STATUS = {0: "PASS", 1: "FAIL", 2: "ERROR"}


@dataclasses.dataclass
class Outcome:
    status: str
    msg: str = ""
    reg: str = ""
    addr: int = 0
    expected: int = 0
    actual: int = 0
    checks: int = 0
    dur_s: float = 0.0


def params_str(params):
    return ";".join("%s=%d" % (k, params[k]) for k in sorted(params))


class CTypesRunner:
    """같은 프로세스에서 dylib 함수를 직접 부른다: 빠르고 구조화된 결과. 대신 행(hang)·크래시를 막지 못한다."""
    name = "ctypes"

    def __init__(self, path):
        self.lib = ctypes.CDLL(path)
        self._bind()

    def _bind(self):
        L = self.lib
        L.siv_api_version.argtypes, L.siv_api_version.restype = [], ctypes.c_uint32
        L.siv_result_size.argtypes, L.siv_result_size.restype = [], ctypes.c_size_t
        L.siv_test_count.argtypes, L.siv_test_count.restype = [], ctypes.c_int
        L.siv_test_name.argtypes, L.siv_test_name.restype = [ctypes.c_int], ctypes.c_char_p
        L.siv_set_env.argtypes, L.siv_set_env.restype = [ctypes.c_int, ctypes.c_int], None
        L.siv_set_defects.argtypes, L.siv_set_defects.restype = [ctypes.c_uint32], None
        L.siv_run.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint32, ctypes.c_int,
                              ctypes.POINTER(SivResult)]
        L.siv_run.restype = ctypes.c_int
        ver = L.siv_api_version()
        if ver >> 16 != API_MAJOR:
            raise RuntimeError("siv_tests API %d.%d, sequencer expects major %d" % (ver >> 16, ver & 0xFFFF, API_MAJOR))
        if L.siv_result_size() != ctypes.sizeof(SivResult):
            raise RuntimeError("siv_result_t layout mismatch: C %d bytes, Python %d bytes"
                               % (L.siv_result_size(), ctypes.sizeof(SivResult)))

    def funcs(self):
        return [self.lib.siv_test_name(i).decode() for i in range(self.lib.siv_test_count())]

    def run(self, spec, env, seed, attempt):
        L = self.lib
        L.siv_set_env(env["vdd_mv"], env["temp_c"])
        L.siv_set_defects(env["defects"])
        r = SivResult()
        t0 = time.monotonic()
        st = L.siv_run(spec.func.encode(), params_str(spec.params).encode(), seed & 0xFFFFFFFF, attempt,
                       ctypes.byref(r))
        dur = time.monotonic() - t0
        return Outcome(C_STATUS.get(st, "ERROR"), r.msg.decode(errors="replace"), r.reg.decode(errors="replace"),
                       r.addr, r.expected, r.actual, r.checks, dur)


# ======================================================================== Part B: 바이너리 러너
class BinaryRunner:
    """테스트 하나 = 프로세스 하나. 느리지만 타임아웃으로 죽일 수 있고, 크래시가 시퀀서를 못 죽인다."""
    name = "binary"

    def __init__(self, path):
        self.path = path

    def funcs(self):
        p = subprocess.run([self.path, "--list"], capture_output=True, text=True, timeout=10)
        return p.stdout.split()

    def run(self, spec, env, seed, attempt):
        cmd = [self.path, "--run", spec.func, "--params", params_str(spec.params), "--seed", str(seed & 0xFFFFFFFF),
               "--attempt", str(attempt), "--vdd", str(env["vdd_mv"]), "--temp", str(env["temp_c"]),
               "--defects", str(env["defects"])]
        t0 = time.monotonic()
        try:
            p = subprocess.run(cmd, capture_output=True, text=True, timeout=spec.timeout_s)
        except subprocess.TimeoutExpired:
            return Outcome("TIMEOUT", msg="no result within %gs, process killed" % spec.timeout_s,
                           dur_s=time.monotonic() - t0)
        dur = time.monotonic() - t0
        if p.returncode < 0:
            try:
                sig = signal.Signals(-p.returncode).name
            except ValueError:
                sig = "signal %d" % -p.returncode
            return Outcome("CRASH", msg=sig, dur_s=dur)
        lines = [x for x in p.stdout.splitlines() if x.strip()]
        try:
            rec = json.loads(lines[-1])
        except (IndexError, ValueError):
            return Outcome("ERROR", msg="no/garbled result line (exit %d)" % p.returncode, dur_s=dur)
        status = rec.get("status")
        if {"PASS": 0, "FAIL": 1, "ERROR": 2}.get(status) != p.returncode:
            return Outcome("ERROR", msg="status %r disagrees with exit code %d" % (status, p.returncode), dur_s=dur)
        return Outcome(status, rec.get("msg", ""), rec.get("reg", ""), int(rec.get("addr", 0)),
                       int(rec.get("expected", 0)), int(rec.get("actual", 0)), int(rec.get("checks", 0)), dur)


# ======================================================================== Part B: 재시도
def run_with_retries(runner, spec, env, seed):
    """→ (최종 Outcome, [시도별 Outcome]).  재시도는 FAIL 에만 (TIMEOUT/CRASH/ERROR 는 재시도 안 함).
    첫 시도 PASS → PASS / FAIL 뒤 PASS → FLAKY (첫 실패 정보 보존) / 끝까지 FAIL → FAIL (첫 실패 정보)"""
    tries = []
    for n in range(spec.retries + 1):
        o = runner.run(spec, env, seed, n)
        tries.append(o)
        if o.status != "FAIL":
            break
    total = sum(o.dur_s for o in tries)
    last, first = tries[-1], tries[0]
    if last.status == "PASS" and len(tries) > 1:
        final = dataclasses.replace(first, status="FLAKY", dur_s=total,
                                    msg="passed on attempt %d/%d; first: %s" % (len(tries), spec.retries + 1, first.msg))
    elif last.status == "FAIL" and spec.retries > 0:
        final = dataclasses.replace(first, dur_s=total,
                                    msg="%s (failed %d/%d attempts)" % (first.msg, len(tries), spec.retries + 1))
    else:
        final = dataclasses.replace(last, dur_s=total)
    return final, tries


# ======================================================================== Part B: 장비 mock
class FakeClock:
    """장비 대기(챔버 램프·소크, 전원 settle)는 가짜 시간으로 센다 — 테스트가 실제로 자지 않는다."""

    def __init__(self):
        self.t = 0.0

    def now(self):
        return self.t

    def sleep(self, s):
        self.t += s


class InstrumentError(RuntimeError):
    pass


class MockPSU:
    """SCPI 비슷한 명령을 로그에 남기는 1채널 전원 mock. OVP 를 넘는 설정은 거부한다."""

    def __init__(self, clock, log, ovp_mv=1000, settle_s=0.05):
        self.clock, self.log, self.ovp_mv, self.settle_s = clock, log, ovp_mv, settle_s
        self.set_mv, self.on = 0, False

    def _cmd(self, s):
        self.log.append((round(self.clock.now(), 3), "PSU", s))

    def set_voltage(self, mv):
        if not (0 <= mv <= self.ovp_mv):
            raise InstrumentError("PSU: %d mV beyond OVP %d mV" % (mv, self.ovp_mv))
        self._cmd("VOLT %.3f" % (mv / 1000.0))
        self.set_mv = mv

    def output(self, on):
        self._cmd("OUTP %s" % ("ON" if on else "OFF"))
        self.on = on
        if on:
            self.clock.sleep(self.settle_s)

    def measure_mv(self):
        return self.set_mv if self.on else 0


class MockChamber:
    """온도 챔버 mock: 램프 속도(°C/min)만큼 시간이 걸리고, 도달 후 소크(soak) 시간을 기다린다."""

    def __init__(self, clock, log, rate_c_per_min=10.0, soak_s=300.0, ambient_c=25.0):
        self.clock, self.log, self.rate, self.soak_s = clock, log, rate_c_per_min, soak_s
        self.ambient_c = self.temp = ambient_c
        self.transitions = 0

    def set_temp(self, c):
        self.log.append((round(self.clock.now(), 3), "CHAMBER", "TEMP %.1f" % c))
        if c != self.temp:
            self.clock.sleep(abs(c - self.temp) / self.rate * 60.0 + self.soak_s)
            self.transitions += 1
        self.temp = c

    def measure_c(self):
        return self.temp


class Bench:
    """장비 + DUT 소켓. defects = 소켓에 꽂힌 샘플의 결함 비트 (0 = 골든 샘플)."""

    def __init__(self, defects=0, ovp_mv=1000):
        self.clock, self.log = FakeClock(), []
        self.psu = MockPSU(self.clock, self.log, ovp_mv)
        self.chamber = MockChamber(self.clock, self.log)
        self.defects = defects

    def apply_corner(self, c):
        """setup: 전원 끔 → 온도(램프+소크) → 전압 설정 → 전원 켬 (이 레슨의 순서)"""
        self.psu.output(False)
        self.chamber.set_temp(float(c.temp_c))
        self.psu.set_voltage(c.vdd_mv)
        self.psu.output(True)

    def safe_state(self):
        """teardown: 무슨 일이 있어도 전원 끄고 상온 복귀. 한 단계가 실패해도 다음 단계는 시도한다."""
        errs = []
        for step in (lambda: self.psu.output(False), lambda: self.chamber.set_temp(self.chamber.ambient_c)):
            try:
                step()
            except Exception as e:  # noqa: BLE001
                errs.append(e)
        if errs:
            raise InstrumentError("safe_state incomplete: %s" % errs)

    def env(self):
        """DUT 가 실제로 받는 조건 = 설정값이 아니라 장비가 읽은 값"""
        return {"vdd_mv": self.psu.measure_mv(), "temp_c": int(round(self.chamber.measure_c())),
                "defects": self.defects}


# ======================================================================== Part B/C: 결과 DB · 시퀀서
DB_SCHEMA = """
CREATE TABLE IF NOT EXISTS run(id INTEGER PRIMARY KEY, name TEXT, board TEXT, fw TEXT, seed INTEGER,
    defects INTEGER, started TEXT, exit_code INTEGER, bench_s REAL);
CREATE TABLE IF NOT EXISTS result(run_id INTEGER, ord INTEGER, corner TEXT, test TEXT, status TEXT,
    attempts INTEGER, seed INTEGER, reg TEXT, addr INTEGER, expected INTEGER, actual INTEGER, msg TEXT,
    dur_s REAL, PRIMARY KEY(run_id, corner, test));
CREATE TABLE IF NOT EXISTS attempt(run_id INTEGER, corner TEXT, test TEXT, n INTEGER, status TEXT, msg TEXT,
    dur_s REAL);
CREATE TABLE IF NOT EXISTS instr(run_id INTEGER, seq INTEGER, t_s REAL, dev TEXT, cmd TEXT);
"""


class ResultDB:
    """SQLite 결과 DB. 결과 한 줄마다 commit → 시퀀서가 밤중에 죽어도 그때까지의 결과는 남는다."""

    def __init__(self, path=":memory:"):
        self.con = sqlite3.connect(path)
        self.con.executescript(DB_SCHEMA)

    def close(self):
        self.con.close()

    def new_run(self, run, defects, started):
        with self.con:
            cur = self.con.execute("INSERT INTO run(name, board, fw, seed, defects, started) VALUES (?,?,?,?,?,?)",
                                   (run["name"], run["board"], run["fw"], run["seed"], defects, started))
        return cur.lastrowid

    def add_result(self, run_id, ord_, corner, test, final, tries, seed):
        with self.con:
            self.con.execute("INSERT INTO result VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)",
                             (run_id, ord_, corner, test, final.status, len(tries), seed, final.reg, final.addr,
                              final.expected, final.actual, final.msg, final.dur_s))
            self.con.executemany("INSERT INTO attempt VALUES (?,?,?,?,?,?,?)",
                                 [(run_id, corner, test, n, o.status, o.msg, o.dur_s) for n, o in enumerate(tries)])

    def add_instr(self, run_id, log):
        with self.con:
            self.con.executemany("INSERT INTO instr VALUES (?,?,?,?,?)",
                                 [(run_id, n, t, dev, cmd) for n, (t, dev, cmd) in enumerate(log)])

    def set_bench_time(self, run_id, t):
        with self.con:
            self.con.execute("UPDATE run SET bench_s=? WHERE id=?", (round(t, 3), run_id))

    def finish(self, run_id, code):
        with self.con:
            self.con.execute("UPDATE run SET exit_code=? WHERE id=?", (code, run_id))


class AbortRun(Exception):
    """운영자 중단, 장비 인터록 등 — 즉시 멈추되 teardown 은 반드시 한다."""


def test_seed(run_seed, corner, test):
    """테스트별 seed = run seed ^ crc32("코너/테스트"): 한 테스트만 다시 돌려도 같은 난수열이 나온다"""
    return (run_seed ^ zlib.crc32(("%s/%s" % (corner, test)).encode())) & 0xFFFFFFFF


class Sequencer:
    def __init__(self, bench, runners, db, started="1970-01-01T00:00:00Z"):
        self.bench, self.runners, self.db, self.started = bench, runners, db, started

    def run(self, plan):
        """코너마다 setup → 테스트(사전조건·재시도) → 결과 기록. 끝나면(예외여도) safe_state + 장비 로그 저장.
        → run_id.  AbortRun 은 run.exit_code=3 으로 표시하고 다시 던진다."""
        run_id = self.db.new_run(plan.run, self.bench.defects, self.started)
        n = 0
        try:
            for corner in plan.corners:
                self.bench.apply_corner(corner)
                here = {}
                for spec in plan.tests:
                    if spec.corners and corner.name not in spec.corners:
                        continue
                    seed = test_seed(plan.run["seed"], corner.name, spec.name)
                    blocked = [r for r in spec.requires if here.get(r) not in ("PASS", "FLAKY")]
                    if blocked:
                        final = Outcome("SKIP", msg="requires %s = %s" % (blocked[0], here.get(blocked[0], "NOT_RUN")))
                        tries = []
                    else:
                        try:
                            final, tries = run_with_retries(self.runners[spec.runner], spec, self.bench.env(), seed)
                        except AbortRun:
                            raise
                        except Exception as e:  # noqa: BLE001  하네스 버그 하나가 밤 전체를 날리지 않게
                            final = Outcome("ERROR", msg="sequencer: %s: %s" % (type(e).__name__, e))
                            tries = [final]
                    here[spec.name] = final.status
                    self.db.add_result(run_id, n, corner.name, spec.name, final, tries, seed)
                    n += 1
        except AbortRun:
            self.db.finish(run_id, 3)
            raise
        finally:
            self.bench.safe_state()
            self.db.add_instr(run_id, self.bench.log)
            self.db.set_bench_time(run_id, self.bench.clock.now())
        return run_id


# ======================================================================== Part C: 리포트
def make_report(db, run_id, flaky_fails=False):
    """SQL 로 요약 → (리포트 텍스트, 종료 코드). 0 = 나쁜 결과 없음, 1 = FAIL/TIMEOUT/CRASH/ERROR
    (flaky_fails 면 FLAKY 도), 2 = 결과가 하나도 없음. run.exit_code 도 갱신한다."""
    c = db.con
    name, board, fw, seed, defects, started, bench_s = c.execute(
        "SELECT name, board, fw, seed, defects, started, bench_s FROM run WHERE id=?", (run_id,)).fetchone()
    corners = [r[0] for r in c.execute(
        "SELECT corner FROM result WHERE run_id=? GROUP BY corner ORDER BY MIN(ord)", (run_id,))]
    tests = [r[0] for r in c.execute(
        "SELECT test FROM result WHERE run_id=? GROUP BY test ORDER BY MIN(ord)", (run_id,))]
    cell = {(t, k): s for t, k, s in c.execute("SELECT test, corner, status FROM result WHERE run_id=?", (run_id,))}
    counts = dict(c.execute("SELECT status, COUNT(*) FROM result WHERE run_id=? GROUP BY status", (run_id,)))
    total = sum(counts.values())
    out = ["== SIV-M12 sequencer report: run %d '%s' ==" % (run_id, name),
           "board=%s  fw=%s  seed=0x%04X  defects=0x%X  started=%s" % (board, fw, seed, defects, started),
           "%-14s" % "test" + "".join("%-10s" % k for k in corners)]
    for t in tests:
        out.append("%-14s" % t + "".join("%-10s" % cell.get((t, k), "-") for k in corners))
    out.append("counts: " + " ".join("%s=%d" % (s, counts.get(s, 0)) for s in STATUS_ORDER) + " (total %d)" % total)
    sections = (("failures (first failure per test/corner; rerun with its seed)", BAD),
                ("flaky (passed on retry - track it, do not forget it)", ("FLAKY",)),
                ("skipped", ("SKIP",)))
    for title, sts in sections:
        rows = c.execute("SELECT status, test, corner, attempts, seed, reg, addr, expected, actual, msg FROM result "
                         "WHERE run_id=? AND status IN (%s) ORDER BY ord" % ",".join("?" * len(sts)),
                         (run_id,) + tuple(sts)).fetchall()
        if not rows:
            continue
        out.append("-- %s --" % title)
        for st, t, k, att, sd, reg, addr, exp, act, msg in rows:
            loc = ""
            if reg:
                loc = " reg=%s addr=0x%08X exp=0x%08X act=0x%08X diff=0x%08X" % (reg, addr, exp, act, exp ^ act)
            out.append("%-8s %-13s @%-9s att=%d seed=0x%08X%s | %s" % (st, t, k, att, sd, loc, msg))
    temps = c.execute("SELECT COUNT(*) FROM instr WHERE run_id=? AND dev='CHAMBER'", (run_id,)).fetchone()[0]
    last_psu = c.execute("SELECT cmd FROM instr WHERE run_id=? AND dev='PSU' ORDER BY seq DESC LIMIT 1",
                         (run_id,)).fetchone()
    out.append("bench: %.0f s simulated, %d chamber setpoints, last PSU cmd '%s'"
               % (bench_s or 0, temps, last_psu[0] if last_psu else "-"))
    bad = sum(counts.get(s, 0) for s in BAD) + (counts.get("FLAKY", 0) if flaky_fails else 0)
    code = 2 if total == 0 else (1 if bad else 0)
    out.append("RESULT: %s (exit %d)" % ({0: "PASS", 1: "FAIL", 2: "NO RESULTS"}[code], code))
    db.finish(run_id, code)
    return "\n".join(out) + "\n", code


# ======================================================================== 전체 흐름 (주어짐)
def sequence(text, defects, db, runners, started="1970-01-01T00:00:00Z", flaky_fails=False):
    """YAML 텍스트 → 검증 → (통과 시에만) 장비 setup·실행 → 리포트. → (exit, report, run_id, bench)
    설정 오류는 exit 2 이고 장비 명령은 하나도 나가지 않는다."""
    bench = Bench(defects)
    try:
        doc = parse_yaml(text)
    except YamlError as e:
        return 2, "CONFIG ERROR yaml %s\n" % e, None, bench
    known = set(runners["ctypes"].funcs())
    plan, errs = validate_plan(doc, known)
    if errs:
        return 2, "".join("CONFIG ERROR %s\n" % e for e in errs), None, bench
    run_id = Sequencer(bench, runners, db, started).run(plan)
    report, code = make_report(db, run_id, flaky_fails)
    return code, report, run_id, bench


# ======================================================================== 테스트 하네스
g_pass = 0
g_total = 0


def check(name, fn):
    """fn() 이 True 면 PASS. 예외도 FAIL 로 센다 (starter 의 미구현 함수가 하네스를 죽이지 않게)."""
    global g_pass, g_total
    g_total += 1
    try:
        ok = bool(fn())
        why = ""
    except Exception as e:  # noqa: BLE001
        ok, why = False, "  (%s: %s)" % (type(e).__name__, e)
    if ok:
        g_pass += 1
        print("PASS  %s" % name)
    else:
        print("FAIL  %s%s" % (name, why))


def raises(exc, fn, *words):
    try:
        fn()
    except exc as e:
        return all(w in str(e) for w in words)
    return False


def has(errs, code, *words):
    return any(e.startswith(code + " ") and all(w in e for w in words) for e in errs)


def read(name):
    with open(os.path.join(HERE, name), encoding="utf-8") as f:
        return f.read()


def safe(fn, default=None):
    """준비 단계 값 계산. starter 에서 미구현 함수가 예외를 내도 하네스는 계속 간다 (그 값을 쓰는 CHECK 가 FAIL)."""
    try:
        return fn()
    except Exception as e:  # noqa: BLE001
        print("....  setup: %s: %s" % (type(e).__name__, e))
        return default


class ScriptRunner:
    """C 없이 재시도·시퀀서 로직만 시험하는 가짜 러너: 시도마다 정해진 status 를 돌려준다"""
    name = "script"

    def __init__(self, statuses, exc=None, exc_at=None):
        self.statuses, self.exc, self.exc_at, self.calls = list(statuses), exc, exc_at, 0

    def run(self, spec, env, seed, attempt):
        self.calls += 1
        if self.exc is not None and self.calls == self.exc_at:
            raise self.exc
        st = self.statuses[min(attempt, len(self.statuses) - 1)]
        return Outcome(st, msg="scripted %s #%d" % (st, attempt), reg="X" if st == "FAIL" else "")


FUNCS = {"id_check", "reg_reset", "sram_march", "i2c_loopback", "pll_lock", "dma_desc"}
GOOD_ENV = {"vdd_mv": 800, "temp_c": 25, "defects": 0}


def harness_specs():
    """testlist.yaml 의 tests 를 손으로 옮긴 것 — Part B 가 Part A(파서) 없이도 돌게 한다"""
    return [TestSpec("reg.id", "id_check", timeout_s=2.0, params={"expect_id": 0x5A1000A0}),
            TestSpec("reg.reset", "reg_reset", requires=["reg.id"]),
            TestSpec("sram.march", "sram_march", requires=["reg.id"], params={"words": 1024}),
            TestSpec("i2c.loopback", "i2c_loopback", retries=2, requires=["reg.id"], params={"bytes": 8}),
            TestSpec("pll.lock", "pll_lock", runner="binary", timeout_s=0.5, requires=["reg.id"],
                     params={"max_polls": 0}),
            TestSpec("dma.ring", "dma_desc", runner="binary", timeout_s=2.0, requires=["pll.lock"],
                     params={"desc_count": 24})]


def mini_plan(edit=None):
    tests = harness_specs()
    if edit:
        edit(tests)
    return Plan(run={"name": "mini", "board": "bench-7", "fw": "x", "seed": 0x5EE9},
                limits={"vdd_mv_max": 950, "temp_c_min": -45, "temp_c_max": 125},
                corners=[Corner("NOM_25C", 800, 25)], tests=tests)


def statuses(db, rid):
    return dict(db.con.execute("SELECT test, status FROM result WHERE run_id=?", (rid,)))


def run_tests(lib_path, exe_path):
    yaml_text = read("testlist.yaml")

    print("-- Part A: YAML subset --")
    check("parse_scalar('0x5EED') == 24301", lambda: parse_scalar("0x5EED") == 24301)
    check("parse_scalar('-40') == -40 and '0.5' == 0.5",
          lambda: parse_scalar("-40") == -40 and parse_scalar("0.5") == 0.5)
    check("parse_scalar: true/null/'NO' -> True/None/'NO' (no YAML 1.1 Norway)",
          lambda: parse_scalar("true") is True and parse_scalar("null") is None and parse_scalar("NO") == "NO")
    check("parse_scalar('1.10') is float 1.1 but '\"1.10\"' stays str",
          lambda: parse_scalar("1.10") == 1.1 and parse_scalar('"1.10"') == "1.10")
    check("parse_scalar(\"'a: b'\") == 'a: b'", lambda: parse_scalar("'a: b'") == "a: b")
    check("parse_scalar('0750') raises (ambiguous octal)",
          lambda: raises(ValueError, lambda: parse_scalar("0750"), "leading-zero"))
    doc = safe(lambda: parse_yaml(yaml_text))
    check("testlist.yaml: run.seed == 0x5EE9, fw str",
          lambda: doc["run"]["seed"] == 0x5EE9 and doc["run"]["fw"] == "siv-0.4.10")
    check("testlist.yaml: 3 corners, LV_HOT = 720 mV / 105 C",
          lambda: len(doc["corners"]) == 3 and doc["corners"][1] == {"name": "LV_HOT", "vdd_mv": 720, "temp_c": 105})
    check("testlist.yaml: 6 tests, sram params.words == 1024, requires flow list",
          lambda: len(doc["tests"]) == 6 and doc["tests"][2]["params"] == {"words": 1024}
          and doc["tests"][2]["requires"] == ["reg.id"])
    check("list under key at same indent ('tests:\\n- a: 1')",
          lambda: parse_yaml("tests:\n- a: 1\n  b: [x, 2]\n- a: 3\nk: v\n")
          == {"tests": [{"a": 1, "b": ["x", 2]}, {"a": 3}], "k": "v"})
    check("comment after value, '#' inside quotes kept",
          lambda: parse_yaml('a: 1   # c\nb: "x # y"\n') == {"a": 1, "b": "x # y"})
    check("tab indentation -> YamlError line 3",
          lambda: raises(YamlError, lambda: parse_yaml("a:\n  b: 1\n\tc: 2\n"), "line 3", "tab"))
    check("duplicate key -> YamlError line 4",
          lambda: raises(YamlError, lambda: parse_yaml("run:\n  name: x\n  seed: 1\n  name: y\n"), "line 4", "duplicate"))
    check("anchor '&base' -> YamlError (unsupported)",
          lambda: raises(YamlError, lambda: parse_yaml("a: &base 1\n"), "line 1", "unsupported"))
    check("bad indentation -> YamlError line 3",
          lambda: raises(YamlError, lambda: parse_yaml("a:\n  b: 1\n    c: 2\n"), "line 3", "indentation"))

    print("-- Part A: test-list validation + ctypes --")
    plan, errs = safe(lambda: validate_plan(doc, FUNCS), (None, ["E_SETUP"]))
    check("validate(testlist.yaml) -> no errors, specs == harness_specs()",
          lambda: errs == [] and plan.tests == harness_specs())

    def bad(edit):
        d = parse_yaml(yaml_text)
        edit(d)
        return validate_plan(d, FUNCS)[1]

    check("typo 'timout_s' -> E_KEY with did-you-mean",
          lambda: has(bad(lambda d: d["tests"][0].update(timout_s=3)), "E_KEY", "tests[0]", "timeout_s"))
    check("fw: 1.10 (unquoted float) -> E_TYPE run.fw (quote it)",
          lambda: has(bad(lambda d: d["run"].update(fw=1.1)), "E_TYPE", "run.fw", "quote it"))
    check("corner vdd 1000 mV > vdd_mv_max 950 -> E_LIMIT",
          lambda: has(bad(lambda d: d["corners"][1].update(vdd_mv=1000)), "E_LIMIT", "LV_HOT", "1000"))
    check("dup test name -> E_DUPNAME",
          lambda: has(bad(lambda d: d["tests"][3].update(name="sram.march")), "E_DUPNAME", "sram.march"))
    check("requires a later test -> E_REQUIRES",
          lambda: has(bad(lambda d: d["tests"][1].update(requires=["pll.lock"])), "E_REQUIRES", "reg.reset", "pll.lock"))
    check("func not in C library -> E_FUNC",
          lambda: has(bad(lambda d: d["tests"][0].update(func="id_chk")), "E_FUNC", "id_chk"))
    check("retries 9 / runner 'thread' / param float -> E_RANGE, E_RUNNER, E_PARAM",
          lambda: (lambda e: has(e, "E_RANGE", "retries") and has(e, "E_RUNNER", "thread") and has(e, "E_PARAM", "words"))(
              bad(lambda d: (d["tests"][2].update(retries=9, runner="thread"), d["tests"][2]["params"].update(words=1.5)))))
    ct = CTypesRunner(lib_path)
    check("ctypes: C library exports the 6 tests", lambda: set(ct.funcs()) == FUNCS)
    check("ctypes: sizeof(SivResult) == siv_result_size() == 132",
          lambda: ctypes.sizeof(SivResult) == ct.lib.siv_result_size() == 132)
    sp = harness_specs()[0]
    o = safe(lambda: ct.run(sp, GOOD_ENV, 1, 0))
    check("ctypes id_check golden -> PASS, 1 check", lambda: o.status == "PASS" and o.checks == 1)
    o2 = safe(lambda: ct.run(dataclasses.replace(sp, params={"expect_id": 0x5A1000B0}), GOOD_ENV, 1, 0))
    check("ctypes id_check expect B0 -> FAIL reg=ID addr=0x40000000 act=0x5A1000A0",
          lambda: (o2.status, o2.reg, o2.addr, o2.expected, o2.actual)
          == ("FAIL", "ID", 0x40000000, 0x5A1000B0, 0x5A1000A0))
    o3 = safe(lambda: ct.run(dataclasses.replace(sp, params={"expect_idd": 1}), GOOD_ENV, 1, 0))
    check("ctypes unknown param -> ERROR (config, not silicon)",
          lambda: o3.status == "ERROR" and "unknown param 'expect_idd'" in o3.msg)

    print("-- Part B: binary runner, retries, instruments, sequencer --")
    br = BinaryRunner(exe_path)
    specs = harness_specs()
    sram, i2c, pll, dma = specs[2], specs[3], dataclasses.replace(specs[4], timeout_s=0.3), specs[5]
    lvhot = {"vdd_mv": 720, "temp_c": 105, "defects": 0xF}
    a, b = safe(lambda: ct.run(sram, lvhot, 0, 0)), safe(lambda: br.run(sram, lvhot, 0, 0))
    check("sram @720mV/105C defect: ctypes and binary agree (FAIL 0x20000A9C bit13)",
          lambda: a.status == b.status == "FAIL" and (a.addr, a.expected, a.actual) == (b.addr, b.expected, b.actual)
          == (0x20000A9C, 0xFFFFFFFF, 0xFFFFDFFF))
    check("sram @800mV/105C defect -> PASS (needs low V AND high T)",
          lambda: ct.run(sram, {"vdd_mv": 800, "temp_c": 105, "defects": 0xF}, 0, 0).status == "PASS")
    t = safe(lambda: br.run(pll, {"vdd_mv": 800, "temp_c": -40, "defects": 0x4}, 0, 0))
    check("binary pll_lock max_polls=0 cold defect -> TIMEOUT, killed < 2 s",
          lambda: t.status == "TIMEOUT" and t.dur_s < 2.0 and "killed" in t.msg)
    check("binary pll_lock golden cold -> PASS",
          lambda: br.run(pll, {"vdd_mv": 800, "temp_c": -40, "defects": 0}, 0, 0).status == "PASS")
    cr = safe(lambda: br.run(dma, {"vdd_mv": 800, "temp_c": 25, "defects": 0x8}, 0, 0))
    check("binary dma_desc 24 desc, ring defect -> CRASH SIGSEGV", lambda: (cr.status, cr.msg) == ("CRASH", "SIGSEGV"))
    check("binary dma_desc 24 desc golden -> PASS (ring wraps)", lambda: br.run(dma, GOOD_ENV, 0, 0).status == "PASS")
    check("binary vdd=0 (PSU off) -> ERROR 'DUT not powered'",
          lambda: (lambda r: r.status == "ERROR" and "not powered" in r.msg)(
              br.run(sp, {"vdd_mv": 0, "temp_c": 25, "defects": 0}, 0, 0)))
    flaky_env = {"vdd_mv": 800, "temp_c": 25, "defects": 0x2}
    fin, tries = safe(lambda: run_with_retries(ct, i2c, flaky_env, 7), (None, None))
    check("i2c seed 7 retries 2 -> FLAKY: FAIL, PASS (first NACK kept)",
          lambda: fin.status == "FLAKY" and [x.status for x in tries] == ["FAIL", "PASS"]
          and fin.reg == "I2C_STATUS" and "first: NACK at byte 3" in fin.msg)
    fin0, tries0 = safe(lambda: run_with_retries(ct, dataclasses.replace(i2c, retries=0), flaky_env, 7), (None, None))
    check("same seed, retries 0 -> FAIL after 1 attempt", lambda: fin0.status == "FAIL" and len(tries0) == 1)
    fin1, tries1 = safe(lambda: run_with_retries(ScriptRunner(["FAIL"]), i2c, GOOD_ENV, 0), (None, None))
    check("scripted FAIL x3 -> FAIL '(failed 3/3 attempts)'",
          lambda: fin1.status == "FAIL" and len(tries1) == 3 and "failed 3/3" in fin1.msg)
    fin2, tries2 = safe(lambda: run_with_retries(br, dataclasses.replace(pll, retries=2),
                                                 {"vdd_mv": 800, "temp_c": -40, "defects": 4}, 0), (None, None))
    check("TIMEOUT is not retried (1 attempt)", lambda: fin2.status == "TIMEOUT" and len(tries2) == 1)
    clk, log = FakeClock(), []
    psu, ch = MockPSU(clk, log, ovp_mv=1000), MockChamber(clk, log)
    check("PSU refuses 1200 mV (OVP 1000) and sends nothing",
          lambda: raises(InstrumentError, lambda: psu.set_voltage(1200), "OVP") and log == [])
    ch.set_temp(-40.0)
    check("chamber 25 -> -40 C @10 C/min + 300 s soak = 690 s fake time", lambda: clk.now() == 690.0)

    db, bench = ResultDB(), Bench(0)
    rid = safe(lambda: Sequencer(bench, {"ctypes": ct, "binary": br}, db).run(
        mini_plan(lambda ts: ts[0].params.update(expect_id=0x5A1000B0))))
    st = statuses(db, rid)
    reasons = [r[0] for r in db.con.execute("SELECT msg FROM result WHERE status='SKIP'")]
    check("precondition: reg.id FAIL -> 4 dependents SKIP, dma.ring SKIP (pll.lock not run)",
          lambda: st["reg.id"] == "FAIL" and [st[k] for k in ("reg.reset", "sram.march", "i2c.loopback", "pll.lock",
                                                               "dma.ring")] == ["SKIP"] * 5
          and "requires reg.id = FAIL" in reasons and "requires pll.lock = SKIP" in reasons)
    db.close()
    db, bench = ResultDB(), Bench(0)
    sq = Sequencer(bench, {"ctypes": ScriptRunner(["PASS"], AbortRun("operator Ctrl-C"), 2), "binary": br}, db)
    check("AbortRun mid-run -> re-raised", lambda: raises(AbortRun, lambda: sq.run(mini_plan())))
    check("... teardown still ran: last cmds 'OUTP OFF', 'TEMP 25.0'",
          lambda: [(x[1], x[2]) for x in bench.log[-2:]] == [("PSU", "OUTP OFF"), ("CHAMBER", "TEMP 25.0")])
    check("... run.exit_code == 3, 1 result kept, instrument log saved",
          lambda: db.con.execute("SELECT exit_code FROM run").fetchone()[0] == 3
          and db.con.execute("SELECT COUNT(*) FROM result").fetchone()[0] == 1
          and db.con.execute("SELECT COUNT(*) FROM instr").fetchone()[0] == len(bench.log) > 0)
    db.close()
    db, bench = ResultDB(), Bench(0)
    rid = safe(lambda: Sequencer(bench, {"ctypes": ScriptRunner(["PASS"], RuntimeError("bug in harness"), 2),
                                         "binary": br}, db).run(mini_plan()))
    st = statuses(db, rid)
    check("Python exception in one test -> that test ERROR, run continues",
          lambda: st["reg.reset"] == "ERROR" and st["sram.march"] == "PASS" and st["dma.ring"] == "PASS")
    db.close()

    print("-- Part C: nightly scenario (3 corners x 6 tests) --")
    runners = {"ctypes": ct, "binary": br}
    with tempfile.TemporaryDirectory() as td:
        dbp = os.path.join(td, "results.db")
        db = ResultDB(dbp)
        code, rep, rid, bench = safe(lambda: sequence(yaml_text, 0xF, db, runners, "2026-11-08T02:00:00Z"),
                                     (None, "", None, None))
        st = {(t, k): s for t, k, s in db.con.execute("SELECT test, corner, status FROM result WHERE run_id=?", (rid,))}
        col = lambda test: [st.get((test, k)) for k in ("NOM_25C", "LV_HOT", "NOM_COLD")]  # noqa: E731
        check("defective sample: exit 1, 18 results", lambda: code == 1 and len(st) == 18)
        check("sram.march FAIL only @LV_HOT", lambda: col("sram.march") == ["PASS", "FAIL", "PASS"])
        check("pll.lock TIMEOUT only @NOM_COLD; dma.ring CRASH, CRASH, SKIP",
              lambda: col("pll.lock") == ["PASS", "PASS", "TIMEOUT"] and col("dma.ring") == ["CRASH", "CRASH", "SKIP"])
        check("i2c.loopback FLAKY @NOM_25C (attempts FAIL, PASS), not a hidden PASS",
              lambda: col("i2c.loopback") == ["FLAKY", "PASS", "PASS"]
              and [r[0] for r in db.con.execute("SELECT status FROM attempt WHERE run_id=? AND test='i2c.loopback' "
                                                "AND corner='NOM_25C' ORDER BY n", (rid,))] == ["FAIL", "PASS"])
        check("report: SRAM failure line with addr/exp/act/diff",
              lambda: re.search(r"^FAIL +sram\.march +@LV_HOT .*reg=SRAM addr=0x20000A9C exp=0xFFFFFFFF "
                                r"act=0xFFFFDFFF diff=0x00002000", rep, re.M))
        check("report: TIMEOUT pll.lock, CRASH SIGSEGV, SKIP reason, flaky section",
              lambda: "TIMEOUT  pll.lock" in rep and "| SIGSEGV" in rep and "requires pll.lock = TIMEOUT" in rep
              and "-- flaky" in rep and "RESULT: FAIL (exit 1)" in rep)
        tc = [r[0] for r in db.con.execute("SELECT cmd FROM instr WHERE run_id=? AND dev='CHAMBER' ORDER BY seq", (rid,))]
        last = db.con.execute("SELECT cmd FROM instr WHERE run_id=? AND dev='PSU' ORDER BY seq DESC LIMIT 1",
                              (rid,)).fetchone()
        check("instrument log: chamber 25 -> 105 -> -40 -> 25, ends with PSU OUTP OFF",
              lambda: tc == ["TEMP 25.0", "TEMP 105.0", "TEMP -40.0", "TEMP 25.0"] and last[0] == "OUTP OFF")
        check("attempt rows == SUM(result.attempts) == 18 (+1 retry, -1 skip)",
              lambda: db.con.execute("SELECT COUNT(*) FROM attempt WHERE run_id=?", (rid,)).fetchone()[0]
              == db.con.execute("SELECT SUM(attempts) FROM result WHERE run_id=?", (rid,)).fetchone()[0] == 18)
        gcode, grep_, _, _ = safe(lambda: sequence(yaml_text, 0, db, runners, "2026-11-08T03:00:00Z"),
                                  (None, "", None, None))
        check("golden sample: 18 PASS, exit 0", lambda: gcode == 0 and "PASS=18" in grep_ and "RESULT: PASS (exit 0)" in grep_)
        db.close()
        con = sqlite3.connect(dbp)
        rows = con.execute("SELECT id, exit_code, bench_s FROM run ORDER BY id").fetchall()
        con.close()
        check("DB file persisted: run 1 exit 1, run 2 exit 0, bench 2640 s each",
              lambda: rows == [(1, 1, 2640.15), (2, 0, 2640.15)])
    db = ResultDB()
    c2, r2, rid2, b2 = safe(lambda: sequence(yaml_text.replace("vdd_mv: 720", "vdd_mv: 1000"), 0xF, db, runners, "x"),
                            (None, "", None, None))
    check("config error (1000 mV > limit) -> exit 2, zero instrument commands",
          lambda: c2 == 2 and "E_LIMIT" in r2 and rid2 is None and b2.log == [])
    c3, r3, _, _ = safe(lambda: sequence(yaml_text, 0x2, db, runners, "x"), (None, "", None, None))
    c4, r4, _, _ = safe(lambda: sequence(yaml_text, 0x2, db, runners, "x", flaky_fails=True), (None, "", None, None))
    check("I2C-only sample: FLAKY=1 -> exit 0 by default, exit 1 with flaky_fails",
          lambda: c3 == 0 and c4 == 1 and "FLAKY=1" in r3)
    db.close()

    print("%d/%d passed" % (g_pass, g_total))
    return g_pass == g_total


def main(argv):
    yaml_path, defects, dbp, started, flaky_fails = None, 0, ":memory:", None, False
    i = 1
    while i < len(argv):
        a = argv[i]
        if a == "--run" and i + 1 < len(argv):
            yaml_path, i = argv[i + 1], i + 2
        elif a == "--defects" and i + 1 < len(argv):
            defects, i = int(argv[i + 1], 0), i + 2
        elif a == "--db" and i + 1 < len(argv):
            dbp, i = argv[i + 1], i + 2
        elif a == "--started" and i + 1 < len(argv):
            started, i = argv[i + 1], i + 2
        elif a == "--flaky-fails":
            flaky_fails, i = True, i + 1
        else:
            print("usage: %s [--run testlist.yaml [--defects MASK] [--db out.db] [--started ISO] [--flaky-fails]]"
                  % argv[0], file=sys.stderr)
            return 64
    with tempfile.TemporaryDirectory() as td:
        lib, exe = build_c(td)
        if yaml_path is None:
            return 0 if run_tests(lib, exe) else 1
        with open(os.path.join(HERE, yaml_path) if not os.path.isabs(yaml_path) else yaml_path,
                  encoding="utf-8") as f:
            text = f.read()
        db = ResultDB(dbp)
        try:
            started = started or time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
            code, report, _, _ = sequence(text, defects, db, {"ctypes": CTypesRunner(lib), "binary": BinaryRunner(exe)},
                                          started, flaky_fails)
        finally:
            db.close()
        sys.stdout.write(report)
        return code


if __name__ == "__main__":
    sys.exit(main(sys.argv))
