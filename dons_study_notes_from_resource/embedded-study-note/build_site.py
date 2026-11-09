#!/usr/bin/env python3
"""임베디드 스터디 노트 — 레슨 md → HTML 사이트.

  python3 build_site.py        # site/ 전체 재생성 + 루트 index.html

입력
  curriculum/stages.json            단계 · 트랙 목록
  curriculum/tracks/<TRACK>.json    트랙 소개 · 아이템
  lessons/<TRACK>/<ID>/YYYY-MM-DD_<ID>_*.md   레슨 (있으면 '학습가능')
  lessons/<TRACK>/<ID>/*.c|*.cpp|...          <!-- code: file [diff=other] --> 로 삽입

출력
  index.html                         → site/index.html 로 이동
  site/index.html                    커리큘럼 맵 + 문제 브라우저
  site/tracks/<TRACK>.html           트랙 페이지 (아이템 표)
  site/lessons/<TRACK>/<ID>.html     레슨 페이지

상태: 준비중(md 없음) / 학습가능(md 있음) / 완료(브라우저에서 체크, localStorage) /
      외부환경필요(env ≠ mac). 의존성 없음(표준 라이브러리만).
렌더러 뼈대는 ../ARM-RISC-embeddedLinux-ETC-study-note/RTOS/build_site.py 계열.
"""
import difflib
import html
import json
import os
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent
OUT = ROOT / "site"
LESSONS = ROOT / "lessons"
CUR = ROOT / "curriculum"
LEVEL_NAME = {"B": "Basic", "M": "Mid", "A": "Advanced"}
MARKS = [("doing", "in progress"), ("done", "done"), ("later", "later"), ("fix", "need to fix")]

FENCE = re.compile(r"^(\s*)```\s*([\w+-]*)\s*$")
HEAD = re.compile(r"^(#{1,6})\s+(.*?)\s*#*\s*$")
HR = re.compile(r"^\s{0,3}(-{3,}|\*{3,}|_{3,})\s*$")
TABLE_SEP = re.compile(r"^\s*\|?[\s:|-]+\|[\s:|-]*$")
ULI = re.compile(r"^(\s*)[-*+]\s+(.*)$")
OLI = re.compile(r"^(\s*)(\d+)[.)]\s+(.*)$")
CODE_MARK = re.compile(r"^\s*<!--\s*code:\s*(\S+)(?:\s+diff=(\S+))?\s*-->\s*$")
COMMENT = re.compile(r"^\s*<!--.*-->\s*$")
DETAILS = re.compile(r"^\s*</?details\b[^>]*>\s*$")
SUMMARY = re.compile(r"^\s*<summary>(.*)</summary>\s*$")


def esc(s):
    return html.escape(s, quote=False)


def attr(s):
    return html.escape(s, quote=True)


def strip_md(s):
    s = re.sub(r"\[([^\]]+)\]\([^)]*\)", r"\1", s)
    return re.sub(r"[*`]", "", s).strip()


def slug(text, used):
    base = re.sub(r"[^\w가-힣]+", "-", strip_md(text)).strip("-").lower() or "sec"
    s, n = base, 2
    while s in used:
        s, n = f"{base}-{n}", n + 1
    used.add(s)
    return s


def inline(s):
    keep = []

    def ph(h):
        keep.append(h)
        return f"\x00{len(keep) - 1}\x00"

    s = re.sub(r"`([^`]+)`", lambda m: ph(f"<code>{esc(m.group(1))}</code>"), s)
    s = re.sub(r"<(br|kbd|/kbd|sub|/sub|sup|/sup)\s*/?>", lambda m: ph(f"<{m.group(1)}>"), s)
    s = esc(s)
    s = re.sub(r"\[([^\]]+)\]\(([^)\s]+)\)",
               lambda m: ph(f'<a href="{attr(m.group(2))}">{m.group(1)}</a>'), s)
    s = re.sub(r"(?<![\"'=])(https?://[^\s<)]+)", lambda m: ph(f'<a href="{attr(m.group(1))}">{m.group(1)}</a>'), s)
    s = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", s)
    s = re.sub(r"(?<![A-Za-z0-9_*])\*([^*\s](?:[^*\n]*?[^*\s])?)\*(?![A-Za-z0-9_*])", r"<em>\1</em>", s)
    s = re.sub(r"\x00(\d+)\x00", lambda m: keep[int(m.group(1))], s)
    return s


def split_row(line):
    line = line.strip()
    if line.startswith("|"):
        line = line[1:]
    if line.endswith("|") and not line.endswith("\\|"):
        line = line[:-1]
    cells = re.split(r"(?<!\\)\|", line)
    return [c.strip().replace("\\|", "|") for c in cells]


# ------------------------------------------------------------------ 구문 강조 (GitHub 테마)
# 의존성 없는 작은 토크나이저. 결과는 줄 단위 HTML 리스트(각 줄 안에서 태그가 닫힘).
C_KW = set("""auto break case const continue default do else enum extern for goto if inline
register restrict return sizeof static struct switch typedef union volatile while _Alignas _Alignof
_Atomic _Bool _Generic _Noreturn _Static_assert _Thread_local static_assert alignas alignof
class namespace template typename public private protected virtual override final constexpr
consteval constinit noexcept new delete operator using this friend explicit mutable try catch throw
decltype concept requires co_await co_return co_yield export import module static_cast
dynamic_cast reinterpret_cast const_cast""".split())
C_TYPE = set("""void char short int long float double signed unsigned bool size_t ssize_t ptrdiff_t
intptr_t uintptr_t int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t
uint_fast8_t uint_fast16_t uint_fast32_t uint_least8_t intmax_t uintmax_t wchar_t char8_t
char16_t char32_t FILE va_list atomic_int atomic_uint atomic_bool auto""".split())
C_CONST = {"NULL", "true", "false", "nullptr", "EOF", "stdin", "stdout", "stderr"}
SH_KW = set("""if then else elif fi for in do done while until case esac function return
local export set unset source exit""".split())

C_TOK = re.compile(r"""
 (?P<com>//[^\n]*|/\*[\s\S]*?(?:\*/|\Z))
|(?P<pp>^[ \t]*\#[ \t]*[A-Za-z_]+)
|(?P<str>"(?:\\.|[^"\\\n])*"?|'(?:\\.|[^'\\\n])+'|(?<=include)[ \t]*<[^>\n]*>)
|(?P<num>\b(?:0[xX][0-9A-Fa-f']+|0[bB][01']+|\d[\d']*\.?\d*(?:[eE][+-]?\d+)?|\.\d+)[uUlLfFzZ]*\b)
|(?P<id>[A-Za-z_]\w*)
|(?P<ws>\s+)
|(?P<other>.)
""", re.X | re.M)

SH_TOK = re.compile(r"""
 (?P<com>(?:(?<=\s)|^)\#[^\n]*)
|(?P<str>"(?:\\.|[^"\\])*"?|'[^']*'?)
|(?P<var>\$\{[^}\n]*\}|\$\(|\$[A-Za-z_?@#$0-9]\w*)
|(?P<opt>(?:(?<=\s)|^)--?[A-Za-z][\w-]*(?:=[^\s]*)?)
|(?P<num>\b\d+\b)
|(?P<id>[A-Za-z_./][\w./+-]*)
|(?P<sep>&&|\|\||[|;&()])
|(?P<ws>[ \t]+)
|(?P<nl>\n)
|(?P<other>.)
""", re.X | re.M)


def _tokens_c(src):
    prev_sig = ""
    toks = []
    for m in C_TOK.finditer(src):
        kind, t = m.lastgroup, m.group()
        cls = None
        if kind == "com":
            cls = "hc"
        elif kind == "pp":
            cls = "hk"
        elif kind == "str":
            cls = "hs"
        elif kind == "num":
            cls = "hn"
        elif kind == "id":
            rest = src[m.end():m.end() + 40].lstrip(" \t")
            if t in C_KW:
                cls = "hk"
            elif t in C_TYPE:
                cls = "hk"
            elif t in C_CONST:
                cls = "hn"
            elif prev_sig in ("struct", "enum", "union", "class", "typename") or t.endswith("_TypeDef"):
                cls = "ht"
            elif rest.startswith("("):
                cls = "hf"
            elif re.fullmatch(r"[A-Z][A-Z0-9_]+", t):
                cls = "hn"
            elif t.endswith("_t") and len(t) > 2:
                cls = "ht"
        if kind not in ("ws",):
            prev_sig = t
        toks.append((cls, t))
    return toks


def _tokens_sh(src):
    toks, cmd_pos = [], True
    for m in SH_TOK.finditer(src):
        kind, t = m.lastgroup, m.group()
        cls = None
        if kind == "com":
            cls = "hc"
        elif kind == "str":
            cls = "hs"
        elif kind == "var":
            cls = "hv"
        elif kind == "opt":
            cls = "hn"
        elif kind == "num":
            cls = "hn"
        elif kind == "id":
            if t in SH_KW:
                cls = "hk"
            elif cmd_pos and "=" not in t:
                cls = "hf"
        if kind in ("nl", "sep"):
            cmd_pos = True
        elif kind not in ("ws",):
            cmd_pos = kind == "id" and t in SH_KW
        toks.append((cls, t))
    return toks


LANG_ALIAS = {"c": "c", "h": "c", "cpp": "c", "c++": "c", "cc": "c", "hpp": "c", "cxx": "c", "m": "c", "metal": "c",
              "bash": "sh", "sh": "sh", "shell": "sh", "zsh": "sh", "console": "sh"}


def highlight_lines(src, lang):
    """src 문자열 → 줄 단위 HTML 리스트 (각 줄 안에서 span이 닫힌다)."""
    kind = LANG_ALIAS.get((lang or "").lower())
    if not kind:
        return [esc(x) for x in src.split("\n")]
    toks = _tokens_c(src) if kind == "c" else _tokens_sh(src)
    lines, cur = [], []
    for cls, t in toks:
        parts = t.split("\n")
        for k, part in enumerate(parts):
            if k:
                lines.append("".join(cur))
                cur = []
            if part:
                cur.append(f'<span class="{cls}">{esc(part)}</span>' if cls else esc(part))
    lines.append("".join(cur))
    return lines


# ------------------------------------------------------------------ 코드 파일 삽입
def code_block(lesson_dir, fname, diff_from, rel_prefix):
    p = lesson_dir / fname
    if not p.exists():
        return f'<p class="warn">⚠ 코드 파일 없음: {esc(fname)}</p>'
    src = p.read_text(encoding="utf-8", errors="replace").rstrip("\n").split("\n")
    changed = set()
    if diff_from and (lesson_dir / diff_from).exists():
        base = (lesson_dir / diff_from).read_text(encoding="utf-8", errors="replace").rstrip("\n").split("\n")
        sm = difflib.SequenceMatcher(a=base, b=src, autojunk=False)
        for tag, _a1, _a2, b1, b2 in sm.get_opcodes():
            if tag in ("replace", "insert"):
                changed.update(range(b1, b2))
    ext = fname.rsplit(".", 1)[-1]
    hl = highlight_lines("\n".join(src), ext)
    rows = []
    for k, line in enumerate(hl):
        cls = ' class="chg"' if k in changed else ""
        rows.append(f'<span{cls}><i>{k + 1}</i>{line or " "}</span>')
    note = (f'<span class="dnote">{diff_from} 대비 바뀐 줄 {len(changed)}개 강조</span>'
            if diff_from else "")
    build = ""
    if ext in ("c", "cpp"):
        cc = ("c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti"
              if ext == "cpp" else "cc -std=c11 -Wall -Wextra -Werror -O2")
        build = f'<span class="bcmd">{esc(cc)} {esc(fname)} &amp;&amp; ./a.out</span>'
    href = attr(rel_prefix + fname)
    return (f'<figure class="code file"><figcaption><b>{esc(fname)}</b>{note}'
            f'<span class="sp"></span><a href="{href}" target="_blank">원본</a>'
            f'<button class="copy" type="button">복사</button></figcaption>'
            f'<pre><code class="ln">{"".join(rows)}</code></pre>{build}</figure>')


# ------------------------------------------------------------------ 마크다운 렌더러
def render(md_lines, toc, used_ids, ctx):
    out, i, n = [], 0, len(md_lines)
    last_head = ""

    def is_block_start(j):
        L = md_lines[j]
        return (HEAD.match(L) or FENCE.match(L) or HR.match(L) or L.startswith(">")
                or ULI.match(L) or OLI.match(L) or DETAILS.match(L) or SUMMARY.match(L)
                or CODE_MARK.match(L) or COMMENT.match(L)
                or L.lstrip().startswith("<svg")
                or ("|" in L and j + 1 < n and TABLE_SEP.match(md_lines[j + 1])))

    while i < n:
        line = md_lines[i]

        m = FENCE.match(line)
        if m:
            indent, lang, buf, i = len(m.group(1)), m.group(2), [], i + 1
            while i < n and not FENCE.match(md_lines[i]):
                L = md_lines[i]
                buf.append(L[indent:] if L[:indent].strip() == "" else L)
                i += 1
            i += 1
            if lang == "svg":
                out.append(f'<figure class="chart">{chr(10).join(buf)}</figure>')
                continue
            label = f'<span class="lang">{esc(lang)}</span>' if lang else ""
            body = "\n".join(highlight_lines("\n".join(buf), lang))
            out.append(f'<figure class="code">{label}<pre><code>{body}</code></pre></figure>')
            continue

        m = CODE_MARK.match(line)
        if m:
            out.append(code_block(ctx["dir"], m.group(1), m.group(2), ctx["rel"]))
            i += 1
            continue

        if DETAILS.match(line):
            out.append(line.strip())
            i += 1
            continue
        m = SUMMARY.match(line)
        if m:
            out.append(f"<summary>{inline(m.group(1))}</summary>")
            i += 1
            continue
        if COMMENT.match(line):
            i += 1
            continue
        if line.lstrip().startswith("<svg"):           # 펜스 없는 인라인 svg
            buf = []
            while i < n:
                buf.append(md_lines[i])
                i += 1
                if "</svg>" in buf[-1]:
                    break
            out.append(f'<figure class="chart">{chr(10).join(buf)}</figure>')
            continue

        m = HEAD.match(line)
        if m:
            lvl, text = len(m.group(1)), m.group(2)
            last_head = text
            if lvl == 1:
                i += 1
                continue
            hid = slug(text, used_ids)
            if lvl <= 3:
                toc.append((lvl, strip_md(text), hid))
            out.append(f'<h{lvl} id="{hid}">{inline(text)}'
                       f'<a class="anchor" href="#{hid}" aria-label="link">#</a></h{lvl}>')
            i += 1
            continue

        if HR.match(line):
            out.append("<hr>")
            i += 1
            continue

        if line.startswith(">"):
            buf = []
            while i < n and md_lines[i].startswith(">"):
                buf.append(md_lines[i][1:].lstrip() if len(md_lines[i]) > 1 else "")
                i += 1
            en = "english" in last_head.lower()
            last_head = ""
            cls = ' class="prob-en" lang="en"' if en else ""
            out.append(f'<blockquote{cls}>{render(buf, [], used_ids, ctx)}</blockquote>')
            continue

        if "|" in line and i + 1 < n and TABLE_SEP.match(md_lines[i + 1]):
            head = split_row(line)
            i += 2
            rows = []
            while i < n and "|" in md_lines[i] and md_lines[i].strip():
                rows.append(split_row(md_lines[i]))
                i += 1
            th = "".join(f"<th>{inline(c)}</th>" for c in head)
            body = ""
            for r in rows:
                r = (r + [""] * len(head))[:len(head)]
                body += "<tr>" + "".join(f"<td>{inline(c)}</td>" for c in r) + "</tr>"
            out.append(f'<div class="tw"><table><thead><tr>{th}</tr></thead>'
                       f"<tbody>{body}</tbody></table></div>")
            continue

        if ULI.match(line) or OLI.match(line):          # 목록 (한 단계 중첩 지원)
            out.append(render_list(md_lines, i, ctx, used_ids))
            i = ctx.pop("_next")
            continue

        if not line.strip():
            i += 1
            continue

        buf = []
        while i < n and md_lines[i].strip() and not is_block_start(i):
            buf.append(md_lines[i].strip())
            i += 1
        if not buf:            # 방어: 알 수 없는 블록 시작 → 한 줄 문단
            buf.append(md_lines[i].strip())
            i += 1
        out.append(f"<p>{inline(' '.join(buf))}</p>")
    return "\n".join(out)


def render_list(md_lines, i, ctx, used_ids):
    n = len(md_lines)
    first = ULI.match(md_lines[i]) or OLI.match(md_lines[i])
    base_ind = len(first.group(1))
    ordered = bool(OLI.match(md_lines[i]))
    items = []          # [text, [sub-lines]]
    while i < n:
        cur = md_lines[i]
        mo, mu = OLI.match(cur), ULI.match(cur)
        mm = mo or mu
        if mm and len(mm.group(1)) <= base_ind + 1 and bool(mo) == ordered:
            items.append([mo.group(3) if mo else mu.group(2), []])
        elif mm and len(mm.group(1)) <= base_ind + 1:
            break                                   # 다른 종류 목록 시작
        elif items and cur.strip() == "":
            # 빈 줄 뒤에 들여쓴 내용이 이어지면 같은 항목
            j = i + 1
            if j < n and md_lines[j].startswith(" " * (base_ind + 2)) and md_lines[j].strip():
                items[-1][1].append("")
            else:
                break
        elif items and cur.startswith(" " * (base_ind + 2)):
            items[-1][1].append(cur[base_ind + 2:] if len(cur) > base_ind + 2 else "")
            if FENCE.match(cur):                    # 들여쓴 코드 펜스 통째로
                i += 1
                while i < n and not FENCE.match(md_lines[i]):
                    items[-1][1].append(md_lines[i][base_ind + 2:])
                    i += 1
                if i < n:
                    items[-1][1].append(md_lines[i][base_ind + 2:])
        elif (items and cur.strip() and not HEAD.match(cur) and not FENCE.match(cur)
              and not DETAILS.match(cur) and not SUMMARY.match(cur) and not CODE_MARK.match(cur)
              and not cur.startswith(">") and not HR.match(cur)
              and not ("|" in cur and i + 1 < n and TABLE_SEP.match(md_lines[i + 1]))):
            if items[-1][1]:
                items[-1][1].append(cur.strip())
            else:
                items[-1][0] += " " + cur.strip()
        else:
            break
        i += 1
    ctx["_next"] = i
    lis = []
    for text, sub in items:
        inner = render(sub, [], used_ids, dict(ctx)) if any(s.strip() for s in sub) else ""
        lis.append(f"<li>{inline(text)}{inner}</li>")
    tag = "ol" if ordered else "ul"
    start = ""
    if ordered:
        s = int(first.group(2))
        start = f' start="{s}"' if s != 1 else ""
    return f"<{tag}{start}>{''.join(lis)}</{tag}>"


# ------------------------------------------------------------------ 데이터 수집
def load():
    stages = json.loads((CUR / "stages.json").read_text())
    tracks, items = {}, []
    for st in stages["stages"]:
        for t in st["tracks"]:
            p = CUR / "tracks" / f"{t['id']}.json"
            tj = json.loads(p.read_text()) if p.exists() else {"items": []}
            tracks[t["id"]] = {**t, **tj, "stage": st["n"], "stage_name": st["name"]}
            for it in tj.get("items", []):
                it = dict(it)
                it["track"] = t["id"]
                it["stage"] = st["n"]
                d = LESSONS / t["id"] / it["id"]
                mds = sorted(d.glob(f"*_{it['id']}_*.md")) if d.is_dir() else []
                it["md"] = mds[-1] if mds else None
                it["status"] = "ready" if mds else ("ext" if it.get("env", "mac") != "mac" else "todo")
                items.append(it)
    return stages, tracks, items


def front_matter(text):
    fm = {}
    if text.startswith("---\n"):
        end = text.find("\n---", 4)
        if end > 0:
            for line in text[4:end].split("\n"):
                if ":" in line:
                    k, v = line.split(":", 1)
                    fm[k.strip()] = v.strip()
            text = text[end + 4:].lstrip("\n")
    return fm, text


# ------------------------------------------------------------------ 스타일 · 스크립트
CSS = r"""
:root{
  --bg:#faf9f7; --paper:#fff; --ink:#1c1b19; --muted:#6b6862; --line:#e3e0da;
  --accent:#2f5d8a; --accent-soft:#e8eff6; --code-bg:#f6f4f0; --code-ink:#2a2724;
  --ok:#2f7d55; --ok-soft:#dcefe4; --warn:#b4541f; --warn-soft:#f8e4d6; --mut:#d9d5ce;
  --chg:#fff1b8; --chg-bar:#d4a017;
  --gh-bg:#f6f8fa; --gh-ink:#1f2328; --gh-line:#d0d7de; --gh-ln:#8c959f; --gh-cap:#ffffff;
  --hk:#cf222e; --hs:#0a3069; --hc:#6e7781; --hn:#0550ae; --hf:#8250df; --ht:#953800; --hv:#953800;
  --en-font: "Inter", -apple-system, BlinkMacSystemFont, "SF Pro Text", "Segoe UI", "Helvetica Neue", Arial, sans-serif;
  --serif: Georgia, "Times New Roman", "Apple SD Gothic Neo", "Noto Serif KR", serif;
  --sans: -apple-system, BlinkMacSystemFont, "Apple SD Gothic Neo", "Segoe UI", sans-serif;
  --mono: ui-monospace, SFMono-Regular, "SF Mono", Menlo, Consolas, monospace;
  color-scheme: light;
}
@media (prefers-color-scheme: dark){
  :root:not([data-theme=light]){
    --bg:#16151a; --paper:#1d1c22; --ink:#e8e6e1; --muted:#a09c94; --line:#33313a;
    --accent:#7fb0e0; --accent-soft:#1f2a36; --code-bg:#14141a; --code-ink:#e3e0d8;
    --ok:#63c08c; --ok-soft:#1d3327; --warn:#e2895a; --warn-soft:#3a2518; --mut:#3a3842;
    --chg:#4a3f1e; --chg-bar:#d9a441; color-scheme: dark;
    --gh-bg:#0d1117; --gh-ink:#e6edf3; --gh-line:#30363d; --gh-ln:#6e7681; --gh-cap:#161b22;
    --hk:#ff7b72; --hs:#a5d6ff; --hc:#8b949e; --hn:#79c0ff; --hf:#d2a8ff; --ht:#ffa657; --hv:#ffa657;
  }
}
:root[data-theme=dark]{
  --bg:#16151a; --paper:#1d1c22; --ink:#e8e6e1; --muted:#a09c94; --line:#33313a;
  --accent:#7fb0e0; --accent-soft:#1f2a36; --code-bg:#14141a; --code-ink:#e3e0d8;
  --ok:#63c08c; --ok-soft:#1d3327; --warn:#e2895a; --warn-soft:#3a2518; --mut:#3a3842;
  --chg:#4a3f1e; --chg-bar:#d9a441; color-scheme: dark;
  --gh-bg:#0d1117; --gh-ink:#e6edf3; --gh-line:#30363d; --gh-ln:#6e7681; --gh-cap:#161b22;
  --hk:#ff7b72; --hs:#a5d6ff; --hc:#8b949e; --hn:#79c0ff; --hf:#d2a8ff; --ht:#ffa657; --hv:#ffa657;
}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);font-family:var(--sans);-webkit-font-smoothing:antialiased}
a{color:var(--accent)}
.top{position:sticky;top:0;z-index:20;background:color-mix(in srgb,var(--bg) 90%,transparent);
     backdrop-filter:blur(8px);border-bottom:1px solid var(--line)}
.top-in{max-width:1240px;margin:0 auto;padding:10px 20px;display:flex;align-items:center;gap:12px}
.top a.home{text-decoration:none;color:var(--muted);font-size:13px;white-space:nowrap}
.top a.home:hover{color:var(--accent)}
.top .t{font-size:13px;color:var(--muted);overflow:hidden;text-overflow:ellipsis;white-space:nowrap;flex:1}
.btn{font:inherit;font-size:12px;padding:5px 11px;border:1px solid var(--line);border-radius:999px;
     background:var(--paper);color:var(--muted);cursor:pointer}
.btn:hover{color:var(--accent);border-color:var(--accent)}
.btn.on{background:var(--ok);border-color:var(--ok);color:#fff}
#bar{position:fixed;top:0;left:0;height:2px;background:var(--accent);width:0;z-index:30}
.wrap{max-width:1240px;margin:0 auto;padding:0 20px;display:grid;
      grid-template-columns:minmax(0,1fr) 240px;gap:40px;align-items:start}
main{max-width:860px;min-width:0;padding:36px 0 100px}
aside.toc{position:sticky;top:60px;padding:36px 0;max-height:calc(100vh - 70px);overflow:auto}
aside.toc .lbl{font-size:11px;letter-spacing:.14em;text-transform:uppercase;color:var(--muted);margin-bottom:10px}
aside.toc ol{list-style:none;margin:0;padding:0;font-size:13px;line-height:1.5}
aside.toc li.l3{padding-left:14px;font-size:12.5px}
aside.toc a{display:block;padding:3px 8px;border-radius:6px;text-decoration:none;color:var(--muted);border-left:2px solid transparent}
aside.toc a:hover{color:var(--ink);background:var(--accent-soft)}
aside.toc a.active{color:var(--accent);border-left-color:var(--accent);background:var(--accent-soft)}
.hero{border-bottom:2px solid var(--ink);padding-bottom:20px;margin-bottom:30px}
.kicker{font-size:11.5px;letter-spacing:.12em;text-transform:uppercase;color:var(--accent);margin-bottom:10px}
h1{font-family:var(--serif);font-size:32px;line-height:1.25;margin:0 0 12px;letter-spacing:-.01em}
.meta{font-size:12.5px;color:var(--muted);display:flex;gap:8px;flex-wrap:wrap;align-items:center}
.goalbox{margin-top:14px;font-size:14.5px;line-height:1.6;color:var(--muted)}
.goalbox b{color:var(--ink)}
main h2{font-family:var(--serif);font-size:24px;margin:46px 0 14px;padding-top:12px;border-top:1px solid var(--line);line-height:1.3}
main h2:first-of-type{border-top:0}
main h3{font-family:var(--serif);font-size:18.5px;margin:28px 0 10px;line-height:1.35}
main h4{font-size:15px;margin:22px 0 8px}
main p{font-family:var(--serif);font-size:16.5px;line-height:1.75;margin:0 0 15px}
main li{font-family:var(--serif);font-size:16px;line-height:1.7;margin:4px 0}
main li p{margin:4px 0}
main ul,main ol{padding-left:22px;margin:0 0 15px}
code{font-family:var(--mono);font-size:.86em;background:var(--code-bg);padding:1.5px 5px;border-radius:4px;border:1px solid var(--line)}
figure.code{position:relative;margin:0 0 18px;background:var(--gh-bg);border:1px solid var(--gh-line);border-radius:8px;overflow:hidden}
figure.code .lang{position:absolute;top:0;right:0;font-family:var(--mono);font-size:10px;letter-spacing:.1em;text-transform:uppercase;color:var(--muted);padding:5px 10px}
figure.code pre{margin:0;padding:14px 16px;overflow-x:auto}
figure.code code{background:none;border:0;padding:0;color:var(--gh-ink);font-size:13px;line-height:1.6;tab-size:4}
figure.code .hk{color:var(--hk)} figure.code .hs{color:var(--hs)} figure.code .hc{color:var(--hc)}
figure.code .hn{color:var(--hn)} figure.code .hf{color:var(--hf)} figure.code .ht{color:var(--ht)}
figure.code .hv{color:var(--hv)}
figure.file figcaption{display:flex;gap:10px;align-items:center;padding:8px 12px;border-bottom:1px solid var(--gh-line);font-size:12.5px;font-family:var(--mono);background:var(--gh-cap);color:var(--gh-ink)}
figure.file figcaption .dnote{color:var(--chg-bar);font-family:var(--sans)}
figure.file figcaption .sp{flex:1}
figure.file figcaption a{font-family:var(--sans);font-size:12px}
figure.file pre{padding:10px 0;max-height:640px}
code.ln{display:block;counter-reset:none}
code.ln > span{display:block;padding:0 16px 0 0;white-space:pre}
code.ln > span > i{display:inline-block;width:3.2em;padding-right:1em;margin-right:.6em;text-align:right;color:var(--gh-ln);font-style:normal;user-select:none;border-right:1px solid var(--gh-line)}
code.ln > span.chg{background:var(--chg);box-shadow:inset 3px 0 0 var(--chg-bar)}
.bcmd{display:block;padding:7px 12px;border-top:1px solid var(--gh-line);font-family:var(--mono);font-size:11.5px;color:var(--gh-ln);background:var(--gh-cap)}
.copy{font:inherit;font-family:var(--sans);font-size:11.5px;border:1px solid var(--line);background:var(--bg);color:var(--muted);border-radius:6px;padding:2px 8px;cursor:pointer}
blockquote{margin:0 0 18px;padding:12px 16px;background:var(--accent-soft);border-left:3px solid var(--accent);border-radius:0 8px 8px 0}
blockquote p{margin:0 0 8px;font-size:15.5px;line-height:1.65}
blockquote p:last-child{margin:0}
blockquote.prob-en{position:relative;margin:4px 0 26px;padding:44px 30px 24px;background:var(--paper);
  border:1px solid var(--line);border-left:4px solid var(--accent);border-radius:4px 12px 12px 4px;
  font-family:var(--en-font);word-spacing:.08em;letter-spacing:.003em}
blockquote.prob-en::before{content:"PROBLEM · ENGLISH";position:absolute;top:16px;left:30px;
  font-family:var(--sans);font-size:11px;font-weight:700;letter-spacing:.16em;color:var(--accent)}
blockquote.prob-en p,blockquote.prob-en li{font-family:var(--en-font);font-size:17px;line-height:1.95;color:var(--ink)}
blockquote.prob-en p{margin:0 0 18px}
blockquote.prob-en > p:first-child{font-size:19px;line-height:1.5;margin-bottom:20px;padding-bottom:16px;border-bottom:1px dashed var(--line)}
blockquote.prob-en ol,blockquote.prob-en ul{padding-left:26px;margin:0 0 20px}
blockquote.prob-en li{margin:0 0 14px;padding-left:6px}
blockquote.prob-en li::marker{color:var(--accent);font-weight:700}
blockquote.prob-en code{font-size:.84em;padding:2px 6px;margin:0 1px;white-space:nowrap;-webkit-box-decoration-break:clone;box-decoration-break:clone;background:var(--gh-bg);border-color:var(--gh-line);color:var(--gh-ink)}
blockquote.prob-en strong{font-weight:700}
.tw{overflow-x:auto;margin:0 0 20px}
table{border-collapse:collapse;width:100%;font-family:var(--sans);font-size:14px}
th,td{padding:8px 11px;text-align:left;border-bottom:1px solid var(--line);vertical-align:top}
thead th{border-bottom:1.5px solid var(--ink);font-size:12px;letter-spacing:.03em;color:var(--muted)}
tbody tr:hover{background:var(--accent-soft)}
hr{border:0;border-top:1px solid var(--line);margin:30px 0}
.anchor{opacity:0;margin-left:8px;font-size:.6em;text-decoration:none;color:var(--muted)}
h2:hover .anchor,h3:hover .anchor{opacity:1}
details{margin:0 0 12px;border:1px solid var(--line);border-radius:10px;background:var(--paper);padding:0 16px}
details > summary{cursor:pointer;padding:11px 0;font-family:var(--sans);font-size:14.5px;font-weight:600;list-style:none}
details > summary::-webkit-details-marker{display:none}
details > summary::before{content:"▸ ";color:var(--accent)}
details[open] > summary::before{content:"▾ "}
details[open]{padding-bottom:6px}
details[open] > summary{border-bottom:1px dashed var(--line);margin-bottom:12px}
figure.chart{margin:0 0 20px;padding:12px;background:var(--paper);border:1px solid var(--line);border-radius:10px;text-align:center;color:var(--ink);overflow-x:auto}
figure.chart svg{max-width:100%;height:auto;font-family:var(--sans)}
figure.chart svg text{fill:currentColor}
figure.chart .f-acc{fill:color-mix(in srgb,var(--accent) 22%,transparent)}
figure.chart .f-ok{fill:color-mix(in srgb,var(--ok) 22%,transparent)}
figure.chart .f-warn{fill:color-mix(in srgb,var(--warn) 22%,transparent)}
figure.chart .f-mut{fill:color-mix(in srgb,var(--muted) 16%,transparent)}
figure.chart .f-acc.solid{fill:color-mix(in srgb,var(--accent) 18%,var(--paper))}
figure.chart .f-ok.solid{fill:color-mix(in srgb,var(--ok) 18%,var(--paper))}
figure.chart .f-warn.solid{fill:color-mix(in srgb,var(--warn) 20%,var(--paper))}
figure.chart .f-mut.solid{fill:color-mix(in srgb,var(--muted) 10%,var(--paper))}
figure.chart .s-acc{stroke:var(--accent)} figure.chart .s-warn{stroke:var(--warn)} figure.chart .s-mut{stroke:var(--muted)}
figure.chart text.f-acc{fill:var(--accent)} figure.chart text.f-ok{fill:var(--ok)}
figure.chart text.f-warn{fill:var(--warn)} figure.chart text.f-mut{fill:var(--muted)}
.warn{color:var(--warn)}
.nav{display:flex;gap:12px;margin-top:56px;padding-top:22px;border-top:1px solid var(--line)}
.nav a{flex:1;text-decoration:none;border:1px solid var(--line);border-radius:10px;padding:13px 15px;background:var(--paper)}
.nav a:hover{border-color:var(--accent)}
.nav .dir{font-size:11px;letter-spacing:.12em;text-transform:uppercase;color:var(--muted)}
.nav .nm{font-family:var(--serif);font-size:15px;color:var(--ink);margin-top:4px;display:block}
.nav a.next{text-align:right}
.pill{display:inline-block;border:1px solid var(--line);border-radius:999px;padding:1px 9px;font-size:11.5px;color:var(--muted);white-space:nowrap}
.pill.ready{border-color:var(--accent);color:var(--accent)}
.pill.doing{border-color:var(--doing);color:var(--doing)} .pill.later{border-color:var(--later);color:var(--later)} .pill.fix{border-color:var(--fix);color:var(--fix)}
.pill.done{border-color:var(--ok);color:var(--ok)}
.pill.ext{border-color:var(--warn);color:var(--warn)}
/* ---- hub ---- */
:root{--doing:#f97316;--later:#eab308;--fix:#ef4444}
.hub{max-width:1240px;margin:0 auto;padding:4px 20px 90px}
.hub h1{font-size:34px;margin-bottom:6px}
.lead{font-family:var(--serif);font-size:16.5px;line-height:1.7;color:var(--muted);max-width:760px;margin:0}
.stats{display:flex;gap:26px;flex-wrap:wrap;margin:22px 0 6px;padding:14px 0;border-top:1px solid var(--line);border-bottom:1px solid var(--line)}
.stat b{display:block;font-family:var(--serif);font-size:25px;line-height:1.1}
.stat span{font-size:11.5px;color:var(--muted);letter-spacing:.04em}
.tabs{display:flex;gap:6px;margin:6px 0 12px;border-bottom:1px solid var(--line)}
.tabs button{font:inherit;font-size:14px;padding:9px 14px;border:0;border-bottom:2px solid transparent;background:none;color:var(--muted);cursor:pointer}
.tabs button.on{color:var(--ink);border-bottom-color:var(--accent);font-weight:600}
.legend{display:flex;gap:16px;flex-wrap:wrap;font-size:12.5px;color:var(--muted);margin:0 0 14px;align-items:center}
.legend i{display:inline-block;width:12px;height:12px;border-radius:3px;vertical-align:-2px;margin-right:5px}
.sq{display:inline-block;width:12px;height:12px;border-radius:3px;margin:0 3px 0 0;flex:none;background:var(--mut);text-decoration:none;vertical-align:top}
.sq.doing{background:var(--doing)} .sq.later{background:var(--later)} .sq.fix{background:var(--fix)}
.sq.ready{background:var(--accent)} .sq.done{background:var(--ok)}
.sq.ext{background:transparent;box-shadow:inset 0 0 0 1.5px var(--warn)}
.sq:hover{outline:2px solid var(--ink);outline-offset:1px}
.stage{margin:0 0 12px}
.stage h2{font-family:var(--serif);font-size:17px;margin:14px 0 4px;padding-bottom:3px;border-bottom:1px solid var(--line)}
.tgrid{display:block}
.trow{display:grid;grid-template-columns:230px minmax(0,1fr) 44px;gap:12px;align-items:center;padding:5px 0;border-bottom:1px dashed var(--line)}
.trow .tn{font-size:14px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis} .trow .tn a{text-decoration:none;color:var(--ink)} .trow .tn a:hover{color:var(--accent)}
.trow .tn small{color:var(--muted);font-size:11px;margin-left:5px}
.trow .lv{display:flex;flex-wrap:nowrap;overflow:hidden;align-items:center}
.trow .lv b{font-size:10px;color:var(--muted);width:12px;flex:none}
.trow .lv .gap{width:10px;flex:none}
.trow .cnt{font-size:12px;color:var(--muted);text-align:right}
.browser{display:grid;grid-template-columns:230px minmax(0,1fr);gap:24px;align-items:start}
.filters{position:sticky;top:60px;max-height:calc(100vh - 70px);overflow:auto;font-size:13px}
.filters h4{font-size:11px;letter-spacing:.12em;text-transform:uppercase;color:var(--muted);margin:14px 0 6px}
.filters label{display:flex;gap:7px;align-items:center;padding:2px 0;cursor:pointer}
.filters label small{color:var(--muted);margin-left:auto}
#q{width:100%;font:inherit;font-size:13.5px;padding:8px 12px;border:1px solid var(--line);border-radius:999px;background:var(--paper);color:var(--ink)}
#q:focus{outline:none;border-color:var(--accent)}
.cards{display:grid;grid-template-columns:repeat(auto-fill,minmax(270px,1fr));gap:12px}
.card{position:relative;background:var(--paper);border:1px solid var(--line);border-radius:12px;padding:13px 14px 12px}
.card:hover{border-color:var(--accent)}
.card.done{border-color:var(--ok);background:color-mix(in srgb,var(--ok-soft) 50%,var(--paper))}
.card .id{font-family:var(--mono);font-size:11.5px;color:var(--muted)}
.card h3{font-family:var(--serif);font-size:15.5px;margin:5px 0 6px;line-height:1.35}
.card h3 a{color:var(--ink);text-decoration:none} .card h3 a:hover{color:var(--accent)}
.card p{font-size:12.5px;line-height:1.55;color:var(--muted);margin:0 0 9px;display:-webkit-box;-webkit-line-clamp:3;-webkit-box-orient:vertical;overflow:hidden}
.card .ft{display:flex;gap:6px;align-items:center;flex-wrap:wrap}
.card{padding-left:18px;overflow:hidden}
.card::before{content:"";position:absolute;left:0;top:0;bottom:0;width:6px;background:var(--lvc)}
.card.lv-B{--lvc:#06b6d4}.card.lv-M{--lvc:#6366f1}.card.lv-A{--lvc:#ec4899}
.card.st-doing{border-color:var(--doing)}.card.st-later{border-color:var(--later)}.card.st-fix{border-color:var(--fix)}
.card .mk{position:absolute;top:9px;right:10px;display:flex;gap:3px}
.card .mk button{font:inherit;font-size:10.5px;padding:1px 7px;border-radius:999px;border:1px solid var(--line);background:var(--paper);color:var(--muted);cursor:pointer}
.card .mk button.done.on{background:var(--ok);border-color:var(--ok);color:#fff}
.card .mk button.doing.on{background:var(--doing);border-color:var(--doing);color:#fff}
.card .mk button.later.on{background:var(--later);border-color:var(--later);color:#fff}
.card .mk button.fix.on{background:var(--fix);border-color:var(--fix);color:#fff}
.card .mk button:hover{border-color:var(--ink)}
.mkbar{display:flex;flex-wrap:wrap;gap:6px;align-items:center;margin:14px 0 0}
.mkbar.bottom{margin:48px 0 0;padding:16px 18px;border:1px solid var(--line);border-radius:12px;background:var(--paper)}
.mkbar .lbl{font-size:12.5px;color:var(--muted);margin-right:4px}
.mkbar button{font:inherit;font-size:13px;padding:5px 14px;border-radius:999px;border:1px solid var(--line);background:var(--paper);color:var(--ink);cursor:pointer}
.mkbar button:hover{border-color:var(--ink)}
.mkbar button.doing.on{background:var(--doing);border-color:var(--doing);color:#fff}
.mkbar button.done.on{background:var(--ok);border-color:var(--ok);color:#fff}
.mkbar button.later.on{background:var(--later);border-color:var(--later);color:#fff}
.mkbar button.fix.on{background:var(--fix);border-color:var(--fix);color:#fff}
.more{margin:16px 0;text-align:center}
.hide{display:none!important}
#tip{position:fixed;z-index:50;max-width:340px;pointer-events:none;opacity:0;transition:opacity .08s;
     background:var(--paper);color:var(--ink);border:1px solid var(--line);border-radius:10px;padding:10px 12px;
     box-shadow:0 6px 24px rgba(0,0,0,.18);font-size:13px;line-height:1.45}
#tip.on{opacity:1}
#tip .tid{font-family:var(--mono);font-size:11.5px;color:var(--muted);margin-bottom:5px}
#tip b{display:block;font-family:var(--serif);font-size:14.5px;line-height:1.35}
#tip p{margin:5px 0 0;color:var(--muted);font-size:12.5px}
#tip .hint{margin-top:6px;font-size:11.5px;color:var(--accent)}
a.btn.glance{display:inline-block;text-decoration:none;font-size:13.5px;padding:7px 14px;color:var(--accent);border-color:var(--accent)}
a.btn.glance small{color:var(--muted);font-size:12px}
a.btn.glance:hover{background:var(--accent-soft)}
.tpage table td:first-child{font-family:var(--mono);font-size:12.5px;white-space:nowrap}
.lvp{display:inline-block;border-radius:999px;padding:1px 9px;font-size:11.5px;font-weight:600;white-space:nowrap;margin-right:4px}
.lvp.lv-B{color:var(--ok);background:var(--ok-soft)}
.lvp.lv-M{color:var(--accent);background:var(--accent-soft)}
.lvp.lv-A{color:var(--warn);background:var(--warn-soft)}
table.items tr.lvrow th{text-align:left;font-family:var(--serif);font-size:15px;padding:16px 10px 6px;background:none;border-bottom:2px solid var(--line)}
table.items tr.lvrow.lv-B th{border-bottom-color:var(--ok)} table.items tr.lvrow.lv-M th{border-bottom-color:var(--accent)}
table.items tr.lvrow.lv-A th{border-bottom-color:var(--warn)}
table.items tr.lvrow th small{font-family:var(--sans);font-size:12px;color:var(--muted);font-weight:400}
/* hub: 한 줄 헤더 */
.hubtop{flex-wrap:wrap;row-gap:6px}
.hubtop .brand{font-family:var(--serif);font-size:17px;font-weight:600;color:var(--ink);text-decoration:none;white-space:nowrap;cursor:help}
.hubdocs a.hbtn{text-decoration:none;font-size:13px;padding:6px 12px;border:1px solid var(--line);border-radius:999px;color:var(--ink);background:var(--paper)}
.hubdocs a.hbtn.pri{border-color:var(--accent);color:var(--accent)}
.hubtop .hs{flex:1;font-size:12.5px;color:var(--muted);white-space:nowrap;overflow:hidden;text-overflow:ellipsis;min-width:0}
.hubtop .hs b{color:var(--ink);font-weight:600}
.hubtop .hb{display:flex;gap:6px;flex-wrap:wrap}
.hubtop a.hbtn{text-decoration:none;font-size:12.5px;padding:4px 10px;border:1px solid var(--line);border-radius:999px;color:var(--ink);white-space:nowrap;background:var(--paper)}
.hubtop a.hbtn:hover{border-color:var(--accent);color:var(--accent)}
.hubtop a.hbtn.pri{border-color:var(--accent);color:var(--accent)}
@media (max-width:900px){
  .wrap{grid-template-columns:minmax(0,1fr)} aside.toc{display:none}
  .browser{grid-template-columns:minmax(0,1fr)} .filters{position:static;max-height:none}
  .trow{grid-template-columns:minmax(0,1fr)} .trow .cnt{text-align:left}
  h1{font-size:26px} main p{font-size:15.5px}
  blockquote.prob-en{padding:40px 18px 18px}
  blockquote.prob-en::before{left:18px}
  blockquote.prob-en code{white-space:normal;overflow-wrap:anywhere}
}
@media print{.top,aside,.nav,#bar{display:none} .wrap{display:block} body{background:#fff}}
"""

JS_COMMON = r"""
const DKEY='esn_done_v1', TKEY='esn_theme';
const doneSet=()=>{try{return new Set(JSON.parse(localStorage.getItem(DKEY)||'[]'))}catch(e){return new Set()}};
const MK='esn_marks_v1';
const marksGet=()=>{try{return JSON.parse(localStorage.getItem(MK)||'{}')}catch(e){return {}}};
const marksSave=m=>{try{localStorage.setItem(MK,JSON.stringify(m))}catch(e){}};
const saveDone=s=>{try{localStorage.setItem(DKEY,JSON.stringify([...s]))}catch(e){}};
(function(){
  try{const t=localStorage.getItem(TKEY); if(t)document.documentElement.dataset.theme=t;}catch(e){}
  const tb=document.getElementById('theme');
  if(tb)tb.onclick=()=>{const d=document.documentElement;
    const cur=d.dataset.theme||(matchMedia('(prefers-color-scheme: dark)').matches?'dark':'light');
    const nx=cur==='dark'?'light':'dark'; d.dataset.theme=nx; try{localStorage.setItem(TKEY,nx)}catch(e){}};
})();
"""

JS_LESSON = r"""
(function(){
  const bar=document.getElementById('bar');
  const onScroll=()=>{const h=document.body.scrollHeight-innerHeight; bar.style.width=(h>0?Math.min(100,scrollY/h*100):0)+'%';};
  addEventListener('scroll',onScroll,{passive:true}); onScroll();
  const id=document.body.dataset.id, st=document.getElementById('mkst');
  const LAB={ready:'ready',doing:'in progress',done:'done',later:'later',fix:'need to fix'};
  const cur=()=>doneSet().has(id)?'done':(marksGet()[id]||'ready');
  const sync=()=>{const s=cur();
    document.querySelectorAll('.mkbar button').forEach(b=>b.classList.toggle('on',b.dataset.k===s));
    if(st){st.className='pill '+s; st.textContent=LAB[s];}};
  document.querySelectorAll('.mkbar button').forEach(b=>b.onclick=()=>{
    const k=b.dataset.k, was=cur()===k, d=doneSet(), m=marksGet();
    d.delete(id); delete m[id];
    if(!was){ if(k==='done')d.add(id); else m[id]=k; }
    saveDone(d); marksSave(m); sync();});
  addEventListener('storage',sync); sync();
  document.querySelectorAll('button.copy').forEach(b=>b.onclick=()=>{
    const txt=[...b.closest('figure').querySelectorAll('code.ln > span')].map(s=>s.textContent.replace(/^\d+/,'')).join('\n');
    navigator.clipboard.writeText(txt).then(()=>{b.textContent='복사됨'; setTimeout(()=>b.textContent='복사',1200)});
  });
  const links=[...document.querySelectorAll('aside.toc a')];
  const map=new Map(links.map(a=>[a.getAttribute('href').slice(1),a]));
  const heads=[...document.querySelectorAll('main h2[id],main h3[id]')];
  const io=new IntersectionObserver(es=>es.forEach(e=>{ if(e.isIntersecting){links.forEach(a=>a.classList.remove('active'));
      const a=map.get(e.target.id); if(a)a.classList.add('active');}}),{rootMargin:'-70px 0px -70% 0px'});
  heads.forEach(h=>io.observe(h));
  addEventListener('keydown',e=>{ if(e.target.matches('input,textarea'))return;
    if(e.key==='j'){const n=document.querySelector('.nav a.next'); if(n)location.href=n.href;}
    if(e.key==='k'){const p=document.querySelector('.nav a.prev'); if(p)location.href=p.href;}});
})();
"""

JS_HUB = r"""
(function(){
  const done=doneSet(), marks=marksGet();
  const stOf=it=>it.st!=='ready'?it.st:(done.has(it.id)?'done':(marks[it.id]||'ready'));
  const cnt=document.getElementById('donecount');
  function paintMap(){
    document.querySelectorAll('.sq').forEach(q=>{
      const s=q.dataset.st!=='ready'?q.dataset.st:(done.has(q.dataset.id)?'done':(marks[q.dataset.id]||'ready'));
      q.classList.remove('ready','ext','todo','done','later','fix','doing'); q.classList.add(s);});
    if(cnt)cnt.textContent=done.size;
  }
  paintMap();
  // tabs
  const tabs=[...document.querySelectorAll('.tabs button')];
  const show=k=>{tabs.forEach(b=>b.classList.toggle('on',b.dataset.k===k));
    document.querySelectorAll('[data-panel]').forEach(p=>p.classList.toggle('hide',p.dataset.panel!==k));
    try{history.replaceState(null,'','#'+k)}catch(e){}};
  tabs.forEach(b=>b.onclick=()=>show(b.dataset.k));
  show(['browse','docs'].includes(location.hash.slice(1))?location.hash.slice(1):'map');
  // browser
  const ITEMS=JSON.parse(document.getElementById('items-json').textContent);
  const box=document.getElementById('cards'), q=document.getElementById('q');
  const sel=name=>new Set([...document.querySelectorAll(`input[name=${name}]:checked`)].map(x=>x.value));
  let limit=120;
  const label={todo:'todo',ready:'ready',done:'done',doing:'in progress',later:'later',fix:'need to fix',ext:'ext env'};
  function draw(){
    const tr=sel('tr'), lv=sel('lv'), ss=sel('st'), v=q.value.trim().toLowerCase();
    const list=ITEMS.filter(it=>{const s=stOf(it);
      return (!tr.size||tr.has(it.tr))&&(!lv.size||lv.has(it.lv))&&(!ss.size||ss.has(s))
        &&(!v||(it.id+' '+it.t+' '+it.g+' '+it.tags).toLowerCase().includes(v));});
    document.getElementById('nres').textContent=list.length;
    box.innerHTML=list.slice(0,limit).map(it=>{const s=stOf(it);
      const href=it.href||it.thref; const e=x=>x.replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
      return `<div class="card lv-${it.lv} st-${s}"><div class="id">${it.id} · ${it.lvn}</div>
        ${it.st==='ready'?`<span class="mk" data-id="${it.id}">${['doing','done','later','fix'].map(k=>`<button type="button" data-k="${k}" class="${s===k?'on ':''}${k}">${label[k]}</button>`).join('')}</span>`:''}
        <h3><a href="${href}">${e(it.t)}</a></h3><p>${e(it.g)}</p>
        <div class="ft"><span class="pill ${s}">${label[s]}</span><span class="pill">${it.tr}</span>${it.env!=='mac'?`<span class="pill ext">${it.env}</span>`:''}${it.time?`<span class="pill">${it.time}</span>`:''}</div></div>`;}).join('');
    document.getElementById('morebtn').classList.toggle('hide',list.length<=limit);
    box.querySelectorAll('.mk button').forEach(b=>b.onclick=()=>{
      const id=b.parentNode.dataset.id, k=b.dataset.k, was=b.classList.contains('on');
      done.delete(id); delete marks[id];
      if(!was){ if(k==='done')done.add(id); else marks[id]=k; }
      saveDone(done); marksSave(marks); paintMap(); draw();});
  }
  document.querySelectorAll('.filters input').forEach(x=>x.addEventListener('change',()=>{limit=120;draw();}));
  q.addEventListener('input',()=>{limit=120;draw();});
  document.getElementById('morebtn').onclick=()=>{limit+=240;draw();};
  document.getElementById('allready').onclick=()=>{document.querySelectorAll('input[name=st]').forEach(x=>x.checked=['ready','doing','done','later','fix'].includes(x.value)); draw();};
  draw();
})();
"""

JS_TIP = r"""
(function(){
  const tip=document.getElementById('tip'); if(!tip)return;
  const ITEMS=JSON.parse(document.getElementById('items-json').textContent);
  const by=Object.fromEntries(ITEMS.map(x=>[x.id,x]));
  const LAB={todo:'준비중',ready:'ready',done:'done',doing:'in progress',later:'later',fix:'need to fix',ext:'외부환경필요'};
  const e=x=>String(x||'').replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
  const stOf=it=>{if(it.st!=='ready')return it.st; if(doneSet().has(it.id))return 'done'; return marksGet()[it.id]||'ready';};
  const place=(x,y)=>{const r=tip.getBoundingClientRect(); let L=x+14, T=y+16;
    if(L+r.width>innerWidth-8)L=x-r.width-14; if(T+r.height>innerHeight-8)T=y-r.height-12;
    tip.style.left=Math.max(8,L)+'px'; tip.style.top=Math.max(8,T)+'px';};
  const show=(q,x,y)=>{const it=by[q.dataset.id]; if(!it)return; const s=stOf(it);
    tip.innerHTML=`<div class="tid">${e(it.id)} · ${e(it.lvn)} · <span class="pill ${s}">${LAB[s]||s}</span>`
      +`${it.env&&it.env!=='mac'?' · '+e(it.env):''}${it.time?' · '+e(it.time):''}</div><b>${e(it.t)}</b>`
      +(it.g?`<p>${e(it.g)}</p>`:'')+`<div class="hint">${it.href?'클릭 → 레슨 열기':'클릭 → 트랙 표 (레슨 준비중)'}</div>`;
    tip.classList.add('on'); place(x,y);};
  const hide=()=>tip.classList.remove('on');
  document.querySelectorAll('.sq').forEach(q=>{
    q.addEventListener('mouseenter',ev=>show(q,ev.clientX,ev.clientY));
    q.addEventListener('mousemove',ev=>place(ev.clientX,ev.clientY));
    q.addEventListener('mouseleave',hide);
    q.addEventListener('focus',()=>{const r=q.getBoundingClientRect(); show(q,r.right,r.bottom);});
    q.addEventListener('blur',hide);});
  addEventListener('scroll',hide,{passive:true});
})();
"""


JS_DOCST = r"""
(function(){
  const el=document.getElementById('st-json'); if(!el)return;
  const S=JSON.parse(el.textContent);  // id -> [status, lessonHref, env, title]
  const LAB={todo:'준비중',ready:'ready',doing:'in progress',done:'done',later:'later',fix:'need to fix'};
  const KS=['ready','doing','done','later','fix'];
  const e=x=>String(x||'').replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
  const stOf=id=>{const x=S[id]; if(x[0]!=='ready')return 'todo'; if(doneSet().has(id))return 'done'; return marksGet()[id]||'ready';};
  const re=/tracks\/[A-Za-z0-9]+\.html#([A-Z0-9]+-[BMA]\d+)$/;
  const links=[], ctrls=[];
  document.querySelectorAll('main a[href]').forEach(a=>{
    const m=a.getAttribute('href').match(re); if(!m||!S[m[1]])return; const id=m[1];
    a.dataset.iid=id; a.classList.add('ilink'); if(S[id][1])a.setAttribute('href',S[id][1]);
    a.title=S[id][1]?'레슨 열기':'레슨 준비중 — 트랙 표로 이동';
    links.push(a);
    const td=a.closest('td');
    if(td&&td.cellIndex===0&&td.querySelector('a')===a){
      const c=document.createElement('span'); c.className='stc'; c.dataset.iid=id;
      if(S[id][0]==='ready'){
        const sel=document.createElement('select'); sel.className='stsel';
        sel.innerHTML=KS.map(k=>`<option value="${k}">${LAB[k]}</option>`).join('');
        sel.onchange=()=>setMark(id,sel.value); c.appendChild(sel);
      } else c.innerHTML=`<span class="pill todo">준비중${S[id][2]!=='mac'?' · '+e(S[id][2]):''}</span>`;
      td.appendChild(c); ctrls.push(c);
    }
  });
  function setMark(id,k){const d=doneSet(),m=marksGet(); d.delete(id); delete m[id];
    if(k==='done')d.add(id); else if(k!=='ready')m[id]=k; saveDone(d); marksSave(m); paint();}
  // 섹션(h2)별 아이템 묶기 — 문서 순서
  const secs=[]; let cur=null;
  document.querySelectorAll('main h2, main a.ilink').forEach(n=>{
    if(n.tagName==='H2'){cur={h:n,ids:new Set()}; secs.push(cur);} else if(cur)cur.ids.add(n.dataset.iid);});
  const useSecs=secs.filter(x=>x.ids.size);
  useSecs.forEach(x=>{const b=document.createElement('span'); b.className='secst'; x.h.appendChild(b); x.b=b;});
  const hero=document.querySelector('main header.hero');
  const box=document.createElement('section'); box.id='stsum'; hero.after(box);
  const cnt=ids=>{const c={n:ids.length,ready:0,todo:0,doing:0,done:0,later:0,fix:0};
    ids.forEach(id=>{const s=stOf(id); c[s]++; if(s!=='todo')c.lesson=(c.lesson||0)+1;}); c.lesson=c.lesson||0; return c;};
  const lk=id=>`<a href="${S[id][1]||'#'}">${e(id)}</a> <span class="sm">${e(S[id][3])}</span>`;
  function paint(){
    links.forEach(a=>{const s=stOf(a.dataset.iid); a.className='ilink is-'+s;});
    ctrls.forEach(c=>{const sel=c.querySelector('select'); if(sel){const s=stOf(c.dataset.iid); sel.value=s; sel.className='stsel '+s;}});
    const all=Object.keys(S), c=cnt(all);
    useSecs.forEach(x=>{const k=cnt([...x.ids]);
      x.b.innerHTML=`레슨 ${k.lesson}/${k.n} · done ${k.done}`+(k.fix?` · <b class="fx">need to fix ${k.fix}</b>`:'')+(k.doing?` · in progress ${k.doing}`:'');});
    const pct=(a,b)=>b?Math.round(a/b*100):0;
    const list=(k,label)=>{const ids=all.filter(id=>stOf(id)===k);
      return `<div class="sl ${k}"><div class="slh"><span class="pill ${k}">${label}</span> ${ids.length}</div>`+
        (ids.length?`<ul>${ids.map(id=>`<li>${lk(id)}</li>`).join('')}</ul>`:'<p class="sm">없음</p>')+'</div>';};
    const rows=useSecs.map(x=>{const k=cnt([...x.ids]);
      const t=x.h.childNodes[0].textContent.trim();
      return `<tr><td><a href="#${x.h.id}">${e(t)}</a></td><td>${k.n}</td><td>${k.lesson}</td><td>${k.n-k.lesson}</td>`+
        `<td>${k.done}</td><td>${k.doing}</td><td class="${k.fix?'fx':''}">${k.fix}</td><td>${k.later}</td></tr>`;}).join('');
    box.innerHTML=`<h2 class="sth">📋 아이템 현황 <small>이 브라우저에 저장된 표시 기준 · 실시간</small></h2>
      <div class="stchips"><span>아이템 <b>${c.n}</b></span><span>레슨 준비됨 <b>${c.lesson}</b></span><span>준비중 <b>${c.todo}</b></span>
      <span class="pill done">done ${c.done}</span><span class="pill doing">in progress ${c.doing}</span>
      <span class="pill fix">need to fix ${c.fix}</span><span class="pill later">later ${c.later}</span></div>
      <div class="stbar"><i class="b-done" style="width:${pct(c.done,c.n)}%"></i><i class="b-rd" style="width:${pct(c.lesson-c.done,c.n)}%"></i></div>
      <div class="sm">초록 = done · 파랑 = 레슨은 준비됐고 아직 안 끝남 · 회색 = 레슨 준비중. 상태는 각 표 첫 칸의 선택 상자나 레슨 페이지 상태 바에서 바꾼다 (같은 저장소라 서로 반영됨).</div>
      <div class="stlists">${list('fix','need to fix')}${list('doing','in progress')}${list('later','later')}</div>
      <div class="tw"><table class="sttab"><thead><tr><th>섹션</th><th>아이템</th><th>레슨 준비</th><th>준비중</th><th>done</th><th>in progress</th><th>need to fix</th><th>later</th></tr></thead><tbody>${rows}</tbody></table></div>`;
  }
  addEventListener('storage',paint); paint();
})();
"""

DOC_CSS = """<style>
#stsum{border:1px solid var(--line);border-radius:12px;padding:14px 18px 10px;margin:0 0 28px;background:var(--paper)}
#stsum h2.sth{margin:0 0 10px;font-size:19px;border:0;padding:0}
#stsum h2.sth small{font-size:12px;color:var(--muted);font-weight:400;margin-left:6px}
#stsum .stchips{display:flex;flex-wrap:wrap;gap:8px 12px;align-items:center;font-size:13.5px;margin-bottom:8px}
#stsum .stbar{height:8px;border-radius:99px;background:var(--mut);overflow:hidden;display:flex;margin:6px 0}
#stsum .stbar i{display:block;height:100%} #stsum .b-done{background:var(--ok)} #stsum .b-rd{background:var(--accent)}
#stsum .sm,.sm{font-size:12px;color:var(--muted)}
#stsum .stlists{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:10px;margin:10px 0}
#stsum .sl{border:1px solid var(--line);border-radius:10px;padding:8px 10px}
#stsum .sl.fix{border-color:var(--fix)} #stsum .sl ul{margin:6px 0 0;padding-left:18px;font-size:13px} #stsum .sl p{margin:6px 0 0}
#stsum table.sttab{font-size:13px;width:100%} #stsum td.fx,.secst b.fx{color:var(--fix)}
#stsum .tw{overflow-x:auto}
.secst{display:inline-block;margin-left:10px;font-size:12px;font-weight:400;color:var(--muted);font-family:var(--sans);vertical-align:middle}
a.ilink{position:relative;padding-left:12px}
a.ilink::before{content:"";position:absolute;left:0;top:50%;width:7px;height:7px;margin-top:-4px;border-radius:50%;background:var(--mut)}
a.ilink.is-ready::before{background:var(--accent)} a.ilink.is-done::before{background:var(--ok)}
a.ilink.is-doing::before{background:var(--doing)} a.ilink.is-fix::before{background:var(--fix)} a.ilink.is-later::before{background:var(--later)}
a.ilink.is-todo{color:var(--muted)}
.stc{display:block;margin-top:4px}
select.stsel{font:inherit;font-size:11.5px;border:1px solid var(--accent);color:var(--accent);background:var(--paper);border-radius:999px;padding:1px 6px}
select.stsel.done{border-color:var(--ok);color:var(--ok)} select.stsel.doing{border-color:var(--doing);color:var(--doing)}
select.stsel.fix{border-color:var(--fix);color:var(--fix)} select.stsel.later{border-color:var(--later);color:var(--later)}
</style>"""



def shell(title, body, js, depth):
    return f"""<!doctype html>
<html lang="ko"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>{esc(title)}</title>
<link rel="preconnect" href="https://fonts.googleapis.com"><link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;600;700&display=swap" rel="stylesheet">
<style>{CSS}</style></head>
<body{body[0]}>{body[1]}<script>{JS_COMMON}{js}</script></body></html>"""


def topbar(depth, crumb, extra=""):
    up = "../" * depth
    return (f'<div class="top"><div class="top-in"><a class="home" href="{up}index.html">← Embedded Study Note</a>'
            f'<span class="t">{crumb}</span>{extra}<button class="btn" id="theme" type="button">◐</button></div></div>')


# ------------------------------------------------------------------ 페이지
def lesson_page(it, tracks, prev_it, next_it):
    raw = it["md"].read_text(encoding="utf-8")
    fm, text = front_matter(raw)
    lines = text.split("\n")
    lesson_dir = it["md"].parent
    rel = os.path.relpath(lesson_dir, OUT / "lessons" / it["track"]) + "/"
    toc, used = [], set()
    body = render(lines, toc, used, {"dir": lesson_dir, "rel": rel})
    tr = tracks[it["track"]]
    toc_html = "".join(f'<li class="l{lv}"><a href="#{hid}">{esc(t)}</a></li>' for lv, t, hid in toc if lv <= 3)
    env = it.get("env", "mac")
    meta = (f'<span class="pill">{LEVEL_NAME[it["level"]]}</span>'
            f'<span class="pill">⏱ {esc(fm.get("time", "?"))}</span>'
            f'<span class="pill {"ext" if env != "mac" else ""}">env: {esc(env)}</span>'
            + "".join(f'<span class="pill">#{esc(t)}</span>' for t in it.get("tags", []))
            + f'<span class="pill">updated {esc(fm.get("updated", ""))}</span>')
    nav = '<nav class="nav">'
    nav += (f'<a class="prev" href="{prev_it["id"]}.html"><span class="dir">← 이전 (k)</span>'
            f'<span class="nm">{esc(prev_it["id"])} {esc(prev_it["title"])}</span></a>' if prev_it else "<span style='flex:1'></span>")
    nav += (f'<a class="next" href="{next_it["id"]}.html"><span class="dir">다음 (j) →</span>'
            f'<span class="nm">{esc(next_it["id"])} {esc(next_it["title"])}</span></a>' if next_it else "<span style='flex:1'></span>")
    nav += "</nav>"
    crumb = f'<a href="../../tracks/{it["track"]}.html">{esc(it["track"])} · {esc(tr["name"])}</a> / {esc(it["id"])}'

    def mkbar(where):
        return (f'<div class="mkbar {where}"><span class="lbl">{"내 상태" if where == "top" else "다 풀었으면 상태 표시"}</span>'
                + "".join(f'<button type="button" data-k="{k}" class="{k}">{lab}</button>' for k, lab in MARKS)
                + '</div>')
    html_body = (
        topbar(2, crumb, '<span class="pill" id="mkst">ready</span>')
        + '<div id="bar"></div><div class="wrap"><main>'
        + f'<header class="hero"><div class="kicker">Stage {it["stage"]} · {esc(tr["name"])} · {esc(it["id"])}</div>'
        + f'<h1>{esc(it["title"])}</h1><div class="meta">{meta}</div>'
        + f'<div class="goalbox"><b>목표</b> {esc(it.get("goal", ""))}<br><b>과제</b> {esc(it.get("task", ""))}</div>'
        + mkbar("top") + '</header>'
        + body + mkbar("bottom") + nav + '</main>'
        + f'<aside class="toc"><div class="lbl">목차</div><ol>{toc_html}</ol></aside></div>')
    return shell(f'{it["id"]} {it["title"]}', (f' data-id="{attr(it["id"])}"', html_body), JS_LESSON, 2)


def track_page(tid, tr, titems):
    rows = []
    lv_count = {lv: sum(1 for x in titems if x["level"] == lv) for lv in "BMA"}
    for lv in "BMA":
        lv_items = [x for x in titems if x["level"] == lv]
        if not lv_items:
            continue
        nr = sum(1 for x in lv_items if x["md"])
        rows.append(f'<tr class="lvrow lv-{lv}"><th colspan="5">{LEVEL_NAME[lv]} <small>· {len(lv_items)}개'
                    f'{f" · 학습가능 {nr}" if nr else ""}</small></th></tr>')
        for it in lv_items:
            st = {"ready": "학습가능", "ext": "외부환경필요", "todo": "준비중"}[it["status"]]
            cls = it["status"]
            name = (f'<a href="../lessons/{tid}/{it["id"]}.html">{esc(it["title"])}</a>' if it["md"]
                    else esc(it["title"]))
            rows.append(f'<tr id="{it["id"]}"><td>{it["id"]}</td><td><span class="lvp lv-{lv}">{LEVEL_NAME[lv]}</span></td>'
                        f'<td>{name}<br><small style="color:var(--muted)">'
                        f'{esc(it.get("task", ""))}</small></td><td><span class="pill {cls}">{st}</span></td>'
                        f'<td>{esc(it.get("env", "mac"))}</td></tr>')
    goals = "".join(f"<li>{esc(g)}</li>" for g in tr.get("goals", []))
    refs = "".join(
        f'<li>{"<a href=" + chr(34) + attr(r["url"]) + chr(34) + ">" + esc(r["title"]) + "</a>" if r.get("url") else esc(r["title"])}</li>'
        for r in tr.get("refs", []))
    nready = sum(1 for x in titems if x["md"])
    body = (topbar(1, f'Stage {tr["stage"]} · {esc(tr["stage_name"])} / {esc(tid)}')
            + '<div class="wrap" style="grid-template-columns:minmax(0,1fr)"><main class="tpage" style="max-width:1000px">'
            + f'<header class="hero"><div class="kicker">Stage {tr["stage"]} · {esc(tid)} · {esc(tr.get("en", ""))}</div>'
            + f'<h1>{esc(tr["name"])}</h1><div class="meta"><span class="pill">아이템 {len(titems)}</span>'
            + "".join(f'<span class="lvp lv-{lv}">{LEVEL_NAME[lv]} {n}</span>' for lv, n in lv_count.items() if n)
            + f'<span class="pill ready">학습가능 {nready}</span></div></header>'
            + f'<p>{esc(tr.get("intro", ""))}</p>'
            + (f'<h2>트랙 목표</h2><ul>{goals}</ul>' if goals else "")
            + '<h2>아이템</h2><div class="tw"><table class="items"><thead><tr><th>ID</th><th>난이도</th><th>제목 · 과제</th><th>상태</th><th>env</th></tr></thead><tbody>'
            + "".join(rows) + '</tbody></table></div>'
            + (f'<h2>참고자료</h2><ul>{refs}</ul>' if refs else "")
            + '</main></div>')
    return shell(f'{tid} {tr["name"]}', ("", body), "", 1)


def hub_page(stages, tracks, items, docs=()):
    by_track = {}
    for it in items:
        by_track.setdefault(it["track"], []).append(it)
    n_ready = sum(1 for x in items if x["md"])
    n_ext = sum(1 for x in items if x["status"] == "ext")
    # ---- 커리큘럼 맵
    mp = []
    for st in stages["stages"]:
        rows = []
        for t in st["tracks"]:
            tid = t["id"]
            its = by_track.get(tid, [])
            lvs = []
            for lv in "BMA":
                sqs = []
                for it in its:
                    if it["level"] != lv:
                        continue
                    href = (f'lessons/{tid}/{it["id"]}.html' if it["md"] else f'tracks/{tid}.html#{it["id"]}')
                    sqs.append(f'<a class="sq {it["status"]}" data-id="{it["id"]}" data-st="{it["status"]}" '
                               f'href="{href}" aria-label="{attr(it["id"] + " " + it["title"])}"></a>')
                if sqs:
                    lvs.append(f'<b>{lv}</b>{"".join(sqs)}<span class="gap"></span>')
            nr = sum(1 for x in its if x["md"])
            rows.append(f'<div class="trow"><div class="tn"><a href="tracks/{tid}.html">{esc(t["en"])}</a>'
                        f'<small>{tid}</small></div><div class="lv">{"".join(lvs)}</div>'
                        f'<div class="cnt">{nr}/{len(its)}</div></div>')
        mp.append(f'<section class="stage"><h2>{st["n"]}. {esc(st["en"])}</h2><div class="tgrid">{"".join(rows)}</div></section>')
    # ---- 문제 브라우저 데이터
    data = []
    for it in items:
        fm = front_matter(it["md"].read_text(encoding="utf-8"))[0] if it["md"] else {}
        data.append({"id": it["id"], "tr": it["track"], "lv": it["level"], "lvn": LEVEL_NAME[it["level"]],
                     "t": it["title"], "g": it.get("goal", ""), "tags": " ".join(it.get("tags", [])),
                     "st": it["status"], "env": it.get("env", "mac"), "time": fm.get("time", ""),
                     "href": f'lessons/{it["track"]}/{it["id"]}.html' if it["md"] else "",
                     "thref": f'tracks/{it["track"]}.html#{it["id"]}'})
    tr_filters = ""
    for st in stages["stages"]:
        tr_filters += f'<h4>{st["n"]}. {esc(st["name"])}</h4>'
        for t in st["tracks"]:
            its = by_track.get(t["id"], [])
            nr = sum(1 for x in its if x["md"])
            tr_filters += (f'<label><input type="checkbox" name="tr" value="{t["id"]}">{esc(t["name"])}'
                           f'<small>{nr}/{len(its)}</small></label>')
    filters = ('<input id="q" placeholder="검색: id · 제목 · 태그">'
               '<h4>상태</h4>'
               '<label><input type="checkbox" name="st" value="ready" checked>ready</label>'
               '<label><input type="checkbox" name="st" value="doing" checked>in progress</label>'
               '<label><input type="checkbox" name="st" value="done" checked>done</label>'
               '<label><input type="checkbox" name="st" value="later" checked>later</label>'
               '<label><input type="checkbox" name="st" value="fix" checked>need to fix</label>'
               '<label><input type="checkbox" name="st" value="todo">todo</label>'
               '<label><input type="checkbox" name="st" value="ext">ext env</label>'
               '<h4>레벨</h4>'
               + "".join(f'<label><input type="checkbox" name="lv" value="{k}">{v}</label>' for k, v in LEVEL_NAME.items())
               + '<h4>트랙</h4>' + tr_filters)
    legend = ('<div class="legend"><span><i style="background:var(--mut)"></i>todo</span>'
              '<span><i style="background:var(--accent)"></i>ready</span>'
              '<span><i style="background:var(--doing)"></i>in progress</span>'
              '<span><i style="background:var(--ok)"></i>done</span>'
              '<span><i style="background:var(--later)"></i>later</span>'
              '<span><i style="background:var(--fix)"></i>need to fix</span>'
              '<span><i style="box-shadow:inset 0 0 0 1.5px var(--warn)"></i>ext env</span>'
              '<span>· click: lesson (marks from the problem browser sync here)</span></div>')
    js_data = json.dumps(data, ensure_ascii=False).replace("</", "<\\/")
    lead = (f'{len(stages["stages"])}단계 · {len(tracks)}트랙 · 아이템 하나 = 인터뷰형 코딩 문제 하나. '
            'Basic → Mid → Advanced 순서로 직접 풀며 익힌다. 레슨마다 문제(Part A/B/C) · Problem · 시작 코드 · 힌트 · '
            '풀이 · 정답 코드(바뀐 줄 강조) · 실수 · 면접 Q&A · 추가 연습 · 핵심 정리.')
    DOC_BTN = [("learning_roadmap", "🧭", "학습 로드맵", True), ("duplicates", "📝", "중복 검토", False), ("concurrency_practice", "🧵", "동시성 연습 가이드", False), ("hpc_learning", "⚡", "HPC 학습 가이드", False), ("ipc_learning", "🔀", "IPC 학습 가이드", False), ("quant_lowlevel", "📈", "퀀트 로우레벨 로드맵", False)]

    def doc_btn(title, href):
        for key, icon, label, pri in DOC_BTN:
            if key in href:
                return (DOC_BTN.index((key, icon, label, pri)),
                        f'<a class="hbtn{" pri" if pri else ""}" href="{attr(href)}" title="{attr(title)}">{icon} {label}</a>')
        return (99, f'<a class="hbtn" href="{attr(href)}">📄 {esc(title)}</a>')
    btns = sorted(doc_btn(t, h) for t, h in docs)
    head = ('<div class="top"><div class="top-in hubtop">'
            f'<a class="brand" href="index.html" title="{attr(lead)}">Embedded Study Note</a>'
            '<span class="hs"></span>'
            '<button class="btn" id="theme" type="button">◐</button></div></div>')
    stat = lambda v, l: f'<div class="stat"><b>{v}</b><span>{l}</span></div>'
    res = ('<div class="stats">' + stat(len(items), "아이템") + stat(len(tracks), "트랙")
           + stat(n_ready, "학습가능") + stat('<span id="donecount" style="font-size:inherit;color:inherit;letter-spacing:0">0</span>', "완료")
           + stat(n_ext, "외부환경필요") + '</div>'
           '<h2 style="margin-top:22px">문서 · 링크</h2><div class="hb" style="display:flex;gap:8px;flex-wrap:wrap;margin:10px 0">'
           + "".join(b for _, b in btns)
           + '<a class="hbtn pri" href="../2026-11-07_curriculum_items_overview.html" '
             'title="아이템 전체 목록 · 트랙별 · 실습환경">🗂 한눈에 보기</a></div>'
           f'<p class="lead" style="margin-top:18px">{esc(lead)}</p>')
    body = (head
            + '<div class="hub">'
            + '<div class="tabs"><button data-k="map" class="on">Overview</button><button data-k="browse">Browse</button><button data-k="docs">Pipeline / Resource</button></div>'
            + f'<div data-panel="map">{legend}{"".join(mp)}</div>'
            + '<div data-panel="browse" class="hide"><div class="browser">'
            + f'<div class="filters">{filters}</div>'
            + '<div><div class="legend"><span><b id="nres">0</b>개</span>'
            + '<button class="btn" id="allready" type="button">ready only</button></div>'
            + '<div class="cards" id="cards"></div><div class="more"><button class="btn" id="morebtn" type="button">더 보기</button></div></div>'
            + '</div></div>'
            + f'<div data-panel="docs" class="hide hubdocs">{res}</div></div>'
            + '<div id="tip" role="tooltip"></div>'
            + f'<script type="application/json" id="items-json">{js_data}</script>')
    return shell("Embedded Study Note", ("", body), JS_HUB + JS_TIP, 0)


def build_docs(items=()):
    """루트의 YYYY-MM-DD_*.md 검토 문서 → site/docs/<stem>.html (overview는 자체 HTML이 있어 제외)."""
    docs = []
    for md in sorted(ROOT.glob("[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]_*.md")):
        if "items_overview" in md.name:
            continue
        text_md = md.read_text(encoding="utf-8")
        lines = text_md.split("\n")
        by_id = {x["id"]: x for x in items}
        ref_ids = [i for i in dict.fromkeys(re.findall(r"tracks/[A-Za-z0-9]+\.html#([A-Z0-9]+-[BMA]\d+)", text_md)) if i in by_id]
        stj = {i: [by_id[i]["status"], f'../lessons/{by_id[i]["track"]}/{i}.html' if by_id[i]["md"] else "",
                   by_id[i].get("env", "mac"), by_id[i]["title"]] for i in ref_ids}
        h1 = next((l[2:].strip() for l in lines if l.startswith("# ")), md.stem)
        toc, used = [], set()
        body = render(lines, toc, used, {"dir": ROOT, "rel": "../../"})
        toc_html = "".join(f'<li class="l{lv}"><a href="#{hid}">{esc(t)}</a></li>' for lv, t, hid in toc if lv <= 3)
        page = (topbar(1, esc(h1))
                + '<div id="bar"></div><div class="wrap"><main style="max-width:980px">'
                + f'<header class="hero"><div class="kicker">문서 · {esc(md.name)}</div><h1>{esc(h1)}</h1></header>'
                + body + '</main>'
                + f'<aside class="toc"><div class="lbl">목차</div><ol>{toc_html}</ol></aside></div>')
        if len(stj) >= 5:
            page += DOC_CSS + '<script type="application/json" id="st-json">' + json.dumps(stj, ensure_ascii=False).replace("</", "<\\/") + '</script>'
        else:
            stj = {}
        js = ("(function(){const bar=document.getElementById('bar');addEventListener('scroll',()=>{"
              "const h=document.body.scrollHeight-innerHeight;bar.style.width=(h>0?scrollY/h*100:0)+'%'},{passive:true});})();")
        (OUT / "docs").mkdir(exist_ok=True)
        (OUT / "docs" / f"{md.stem}.html").write_text(shell(h1, ("", page), js + (JS_DOCST if stj else ""), 1), encoding="utf-8")
        docs.append((h1, f"docs/{md.stem}.html"))
    return docs


def main():
    stages, tracks, items = load()
    OUT.mkdir(exist_ok=True)
    by_track = {}
    for it in items:
        by_track.setdefault(it["track"], []).append(it)
    n_pages = 0
    for tid, its in by_track.items():
        (OUT / "tracks").mkdir(exist_ok=True)
        (OUT / "tracks" / f"{tid}.html").write_text(track_page(tid, tracks[tid], its), encoding="utf-8")
        ready = [x for x in its if x["md"]]
        for k, it in enumerate(ready):
            d = OUT / "lessons" / tid
            d.mkdir(parents=True, exist_ok=True)
            prev_it = ready[k - 1] if k > 0 else None
            next_it = ready[k + 1] if k + 1 < len(ready) else None
            (d / f'{it["id"]}.html').write_text(lesson_page(it, tracks, prev_it, next_it), encoding="utf-8")
            n_pages += 1
    docs = build_docs(items)
    (OUT / "index.html").write_text(hub_page(stages, tracks, items, docs), encoding="utf-8")
    (ROOT / "index.html").write_text(
        '<!doctype html><meta charset="utf-8"><meta http-equiv="refresh" content="0; url=site/index.html">'
        '<title>Embedded Study Note</title><a href="site/index.html">site/index.html</a>', encoding="utf-8")
    print(f"site/: {n_pages} lessons, {len(by_track)} tracks, {len(items)} items")


if __name__ == "__main__":
    main()
