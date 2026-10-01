#!/usr/bin/env python3
"""Embedded AI 스터디 노트 마크다운 → 읽기 좋은 HTML 사이트.

  python3 build_site.py            # site/ 전체 재생성

입력
  study_prep_list.md          나침반 (전체 목차)
  A_ml_foundations/*.md       모듈 A 노트
  B_…/ ~ O_…/                 이후 모듈 (폴더가 생기면 자동 포함)

출력
  site/index.html             허브 (카드 · 검색 · 진행률)
  site/<key>/<stem>.html      개별 노트 (사이드 TOC · 진행바 · 이전/다음)

의존성 없음(표준 라이브러리만). 마크다운 지원 범위는 NOTES_SPEC.md 참고:
제목 h1~h3, 문단, 목록(-, 1., - [ ]), 표, 인용, 코드펜스, 구분선, 인라인 코드/굵게/링크,
그리고 ```svg 펜스(그림·차트를 그대로 삽입).
원본은 Hark BSP 노트의 build_notes_site.py — 이 폴더용으로 수정한 사본.
"""
import html
import json
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
OUT = ROOT / "site"
META_DIR = ROOT / "meta"          # 선택: <stem>.json 에 icon/range 지정


def _mod(key, dirname, ko, en, icon, blurb):
    return {"key": key, "dir": ROOT / dirname, "glob": "*.md", "icon": icon,
            "title_ko": ko, "title_en": en, "blurb": blurb, "src_hint": dirname}


COLLECTIONS = [
    {"key": "compass", "dir": ROOT, "glob": "study_prep_list.md", "icon": "🧭",
     "title_ko": "나침반", "title_en": "Study Prep List",
     "blurb": "이 직군에 필요한 개념 전체 목록(모듈 A~O)과 진행 현황. 여기서 시작한다.",
     "src_hint": "."},
    _mod("A", "A_ml_foundations", "A. 수학 · ML 기초", "ML Foundations", "📐",
         "선형대수 · 확률 · 역전파 · 학습 워크플로 · PyTorch · 데이터 도구. 펌웨어 엔지니어가 ML에 처음 들어오는 문."),
    _mod("B", "B_model_architectures", "B. 모델 아키텍처", "Model Architectures", "🧠",
         "MLP · CNN · RNN · Transformer · 오디오/비전/IMU 모델 · 소형 LLM."),
    _mod("C", "C_model_compression", "C. 모델 경량화", "Model Compression", "🗜",
         "양자화 · PTQ/QAT · LLM 양자화 · pruning · distillation · 그래프 최적화."),
    _mod("D", "D_performance_model", "D. 성능 모델", "Performance Model", "📈",
         "FLOPs · 메모리 · roofline · KV-cache · 에너지."),
    _mod("E", "E_hw_architecture", "E. HW 아키텍처", "CPU · DSP · NPU · GPU", "🔩",
         "Cortex-M/A · RISC-V · DSP · NPU · 메모리 · 전력."),
    _mod("F", "F_ml_runtimes", "F. 런타임 · 툴체인", "Embedded ML Runtimes", "🧰",
         "TFLite · TFLite Micro · llama.cpp · QNN · ONNX · 벤더 SDK bring-up."),
    _mod("G", "G_sensors_dsp", "G. 센서 · 신호처리", "Sensors & DSP", "📡",
         "IMU · 센서 퓨전 · 마이크 · FFT/mel · 시간 동기화."),
    _mod("H", "H_data_pipelines", "H. 데이터 파이프라인", "Data Pipelines", "🗄",
         "온디바이스 로깅 → 전송 → ingestion · 라벨링 · 프라이버시."),
    _mod("I", "I_hw_sw_codesign", "I. HW/SW Co-design", "Co-design", "🤝",
         "예산 설정 · HW 친화 모델 · cascade · 벤치마크."),
    _mod("J", "J_firmware_integration", "J. 펌웨어 통합", "Firmware Integration", "🔧",
         "추론 통합 패턴 · 실시간 · 고정소수점/SIMD · C++/Rust."),
    _mod("K", "K_profiling", "K. 프로파일링", "Profiling", "⏱",
         "메모리 · 성능 · 전력 · 열 · 커널 최적화."),
    _mod("L", "L_on_device_llm", "L. 온디바이스 LLM", "On-device LLM & Hybrid", "💬",
         "SLM · 온디바이스 스택 · hybrid 라우팅 · 음성 파이프라인."),
    _mod("M", "M_silicon_selection", "M. 실리콘 선정", "Silicon Selection", "🏷",
         "평가 기준 · 벤치마크 방법 · 벤더 지형 · 데이터시트 해석."),
    _mod("N", "N_domain", "N. 도메인", "Wearables · Robotics · IoT", "⌚",
         "웨어러블 제약 · 제품 사례 · 로보틱스 센싱."),
    _mod("O", "O_tools", "O. 툴", "Tools", "🛠", "Python 파이프라인 · Linux/Android · ML CI."),
]

# ----------------------------------------------------------------- markdown
FENCE = re.compile(r"^\s*```\s*([\w+-]*)\s*$")
HEAD = re.compile(r"^(#{1,6})\s+(.*?)\s*#*\s*$")
HR = re.compile(r"^\s{0,3}(-{3,}|\*{3,}|_{3,})\s*$")
TABLE_SEP = re.compile(r"^\s*\|?[\s:|-]+\|[\s:|-]*$")
ULI = re.compile(r"^(\s*)[-*+]\s+(.*)$")
OLI = re.compile(r"^(\s*)(\d+)[.)]\s+(.*)$")
TASK = re.compile(r"^\[([ xX])\]\s+(.*)$")


def esc(s):
    return html.escape(s, quote=False)


def slug(text, used):
    base = re.sub(r"[^\w가-힣]+", "-", strip_md(text)).strip("-").lower() or "sec"
    s, n = base, 2
    while s in used:
        s, n = f"{base}-{n}", n + 1
    used.add(s)
    return s


def strip_md(s):
    s = re.sub(r"\[([^\]]+)\]\([^)]*\)", r"\1", s)
    s = s.replace("~~", "")                      # 취소선만 제거, "Q11~20" 의 ~ 는 보존
    return re.sub(r"[*`]", "", s).strip()


EMOJI_HEAD = re.compile(
    r"^(?:[\U0001F000-\U0001FAFF\u2190-\u21FF\u2300-\u27BF\uFE0F\u2B00-\u2BFF]+\s*)+")


def title_text(h1):
    """카드/네비에 쓸 제목 — 앞머리 이모지는 아이콘으로 따로 보여주므로 제거."""
    return EMOJI_HEAD.sub("", strip_md(h1)).strip()


def inline(s):
    keep = []

    def ph(h):
        keep.append(h)
        return f"\x00{len(keep) - 1}\x00"

    s = re.sub(r"`([^`]+)`", lambda m: ph(f"<code>{esc(m.group(1))}</code>"), s)
    s = esc(s).replace("&lt;br&gt;", "<br>")
    s = re.sub(r"\[([^\]]+)\]\(([^)\s]+)\)",
               lambda m: ph(f'<a href="{html.escape(m.group(2), quote=True)}">{m.group(1)}</a>'), s)
    s = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", s)
    s = re.sub(r"(?<![\w*])\*([^*\n]+)\*(?![\w*])", r"<em>\1</em>", s)
    s = re.sub(r"\x00(\d+)\x00", lambda m: keep[int(m.group(1))], s)
    return s


def split_row(line):
    line = line.strip()
    if line.startswith("|"):
        line = line[1:]
    if line.endswith("|"):
        line = line[:-1]
    return [c.strip() for c in line.split("|")]


def td_cls(c):
    for mark, cls in (("✅", "ok"), ("🟡", "mid"), ("❌", "gap")):
        if c.startswith(mark):
            return f' class="{cls}"'
    return ""


def render(md_lines, toc, used_ids):
    """마크다운 본문 → HTML. toc 에 (level, text, id) 를 채운다."""
    out, i, n = [], 0, len(md_lines)
    while i < n:
        line = md_lines[i]

        m = FENCE.match(line)
        if m:                                           # ---- 코드 블록
            lang, buf, i = m.group(1), [], i + 1
            while i < n and not FENCE.match(md_lines[i]):
                buf.append(md_lines[i])
                i += 1
            i += 1
            if lang == "svg":                           # 그림·차트: SVG 그대로 삽입
                out.append(f'<figure class="chart">{chr(10).join(buf)}</figure>')
                continue
            label = f'<span class="lang">{esc(lang)}</span>' if lang else ""
            out.append(f'<figure class="code">{label}<pre><code>'
                       f'{esc(chr(10).join(buf))}</code></pre></figure>')
            continue

        m = HEAD.match(line)
        if m:                                           # ---- 제목
            lvl, text = len(m.group(1)), m.group(2)
            if lvl == 1:                                # h1 은 hero 에서 처리
                i += 1
                continue
            hid = slug(text, used_ids)
            toc.append((lvl, strip_md(text), hid))
            out.append(f'<h{lvl} id="{hid}">{inline(text)}'
                       f'<a class="anchor" href="#{hid}" aria-label="link">#</a></h{lvl}>')
            i += 1
            continue

        if HR.match(line):
            out.append("<hr>")
            i += 1
            continue

        if line.startswith(">"):                        # ---- 인용(사이드노트)
            buf = []
            while i < n and md_lines[i].startswith(">"):
                buf.append(md_lines[i].lstrip(">").lstrip())
                i += 1
            inner_toc = []
            out.append(f'<blockquote>{render(buf, inner_toc, used_ids)}</blockquote>')
            continue

        if "|" in line and i + 1 < n and TABLE_SEP.match(md_lines[i + 1]):   # ---- 표
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
                body += "<tr>" + "".join(f"<td{td_cls(c)}>{inline(c)}</td>" for c in r) + "</tr>"
            out.append(f'<div class="tw"><table><thead><tr>{th}</tr></thead>'
                       f"<tbody>{body}</tbody></table></div>")
            continue

        if ULI.match(line) or OLI.match(line):          # ---- 목록 (중첩 없음)
            ordered = bool(OLI.match(line))
            items = []
            while i < n:
                cur = md_lines[i]
                mo, mu = OLI.match(cur), ULI.match(cur)
                if ordered and mo:
                    items.append(mo.group(3))
                elif (not ordered) and mu:
                    items.append(mu.group(2))
                elif (items and cur.strip()                      # lazy continuation:
                      and not mo and not mu                      # 들여쓰기 없이 다음 줄로
                      and not HEAD.match(cur) and not FENCE.match(cur)
                      and not HR.match(cur) and not cur.startswith(">")
                      and not ("|" in cur and i + 1 < n and TABLE_SEP.match(md_lines[i + 1]))):
                    items[-1] += " " + cur.strip()               # 이어붙여 **굵게**가 끊기지 않게
                else:
                    break
                i += 1
            lis = []
            for it in items:
                t = TASK.match(it)
                if t:
                    done = t.group(1).lower() == "x"
                    lis.append(f'<li class="task"><label><input type="checkbox" '
                               f'{"checked" if done else ""}> {inline(t.group(2))}</label></li>')
                else:
                    lis.append(f"<li>{inline(it)}</li>")
            tag = "ol" if ordered else "ul"
            cls = ' class="tasks"' if any("task" in x for x in lis) else ""
            out.append(f"<{tag}{cls}>{''.join(lis)}</{tag}>")
            continue

        if not line.strip():                            # ---- 빈 줄
            i += 1
            continue

        buf = []                                        # ---- 문단
        while i < n and md_lines[i].strip() and not (
                HEAD.match(md_lines[i]) or FENCE.match(md_lines[i]) or HR.match(md_lines[i])
                or md_lines[i].startswith(">") or ULI.match(md_lines[i]) or OLI.match(md_lines[i])
                or ("|" in md_lines[i] and i + 1 < n and TABLE_SEP.match(md_lines[i + 1]))):
            buf.append(md_lines[i].strip())
            i += 1
        out.append(f"<p>{inline(' '.join(buf))}</p>")
    return "\n".join(out)


# ----------------------------------------------------------------- 페이지
CSS = """
:root{
  --bg:#faf9f7; --paper:#fff; --ink:#1c1b19; --muted:#6b6862; --line:#e3e0da;
  --accent:#8a5a2b; --accent-soft:#f3ece2; --code-bg:#f6f4f0; --code-ink:#2a2724;
  --mark:#fff3cd; --ok:#2f7d55; --warn:#b4541f;
  --serif: Georgia, "Times New Roman", "Apple SD Gothic Neo", "Noto Serif KR", serif;
  --sans: -apple-system, BlinkMacSystemFont, "Apple SD Gothic Neo", "Segoe UI", sans-serif;
  --mono: ui-monospace, SFMono-Regular, "SF Mono", Menlo, Consolas, monospace;
}
:root:not([data-theme=light]) { color-scheme: light; }
@media (prefers-color-scheme: dark){
  :root:not([data-theme=light]){
    --bg:#16151a; --paper:#1d1c22; --ink:#e8e6e1; --muted:#a09c94; --line:#33313a;
    --accent:#d9a441; --accent-soft:#2a2620; --code-bg:#14141a; --code-ink:#e3e0d8;
    --mark:#4a3f1e; --ok:#63c08c; --warn:#e2895a; color-scheme: dark;
  }
}
:root[data-theme=dark]{
  --bg:#16151a; --paper:#1d1c22; --ink:#e8e6e1; --muted:#a09c94; --line:#33313a;
  --accent:#d9a441; --accent-soft:#2a2620; --code-bg:#14141a; --code-ink:#e3e0d8;
  --mark:#4a3f1e; --ok:#63c08c; --warn:#e2895a; color-scheme: dark;
}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);font-family:var(--sans);
     -webkit-font-smoothing:antialiased}
a{color:var(--accent)}
/* ---- top bar ---- */
.top{position:sticky;top:0;z-index:20;background:color-mix(in srgb,var(--bg) 88%,transparent);
     backdrop-filter:blur(8px);border-bottom:1px solid var(--line)}
.top-in{max-width:1180px;margin:0 auto;padding:10px 20px;display:flex;align-items:center;gap:14px}
.top a.home{text-decoration:none;color:var(--muted);font-size:13px;white-space:nowrap}
.top a.home:hover{color:var(--accent)}
.top .t{font-size:13px;color:var(--muted);overflow:hidden;text-overflow:ellipsis;white-space:nowrap;flex:1}
.btn{font:inherit;font-size:12px;padding:5px 10px;border:1px solid var(--line);border-radius:999px;
     background:var(--paper);color:var(--muted);cursor:pointer}
.btn:hover{color:var(--accent);border-color:var(--accent)}
.btn.on{background:var(--accent);border-color:var(--accent);color:#fff}
#bar{position:fixed;top:0;left:0;height:2px;background:var(--accent);width:0;z-index:30}
/* ---- layout ---- */
.wrap{max-width:1180px;margin:0 auto;padding:0 20px;display:grid;
      grid-template-columns:minmax(0,1fr) 250px;gap:44px;align-items:start}
main{max-width:760px;padding:40px 0 100px}
aside{position:sticky;top:64px;padding:40px 0;max-height:calc(100vh - 80px);overflow:auto}
aside .lbl{font-size:11px;letter-spacing:.14em;text-transform:uppercase;color:var(--muted);margin-bottom:10px}
aside ol{list-style:none;margin:0;padding:0;font-size:13px;line-height:1.5}
aside li{margin:2px 0}
aside li.l3{padding-left:14px;font-size:12.5px}
aside a{display:block;padding:3px 8px;border-radius:6px;text-decoration:none;color:var(--muted);
        border-left:2px solid transparent}
aside a:hover{color:var(--ink);background:var(--accent-soft)}
aside a.active{color:var(--accent);border-left-color:var(--accent);background:var(--accent-soft)}
/* ---- article ---- */
.hero{border-bottom:2px solid var(--ink);padding-bottom:22px;margin-bottom:34px}
.kicker{font-size:11px;letter-spacing:.16em;text-transform:uppercase;color:var(--accent);margin-bottom:10px}
h1{font-family:var(--serif);font-size:34px;line-height:1.25;margin:0 0 12px;letter-spacing:-.01em}
.meta{font-size:12.5px;color:var(--muted);display:flex;gap:14px;flex-wrap:wrap}
main h2{font-family:var(--serif);font-size:24px;margin:44px 0 14px;padding-top:10px;
        border-top:1px solid var(--line);line-height:1.3}
main h2:first-of-type{border-top:0}
main h3{font-family:var(--serif);font-size:18.5px;margin:30px 0 10px;line-height:1.35}
main p{font-family:var(--serif);font-size:17px;line-height:1.75;margin:0 0 16px}
main li{font-family:var(--serif);font-size:16.5px;line-height:1.7;margin:5px 0}
main ul,main ol{padding-left:22px;margin:0 0 16px}
ul.tasks{list-style:none;padding-left:2px}
li.task label{display:flex;gap:9px;align-items:flex-start;cursor:pointer}
li.task input{margin-top:6px;accent-color:var(--accent)}
li.task input:checked + *{opacity:.55}
code{font-family:var(--mono);font-size:.875em;background:var(--code-bg);padding:1.5px 5px;
     border-radius:4px;border:1px solid var(--line)}
figure.code{position:relative;margin:0 0 20px;background:var(--code-bg);border:1px solid var(--line);
            border-radius:10px;overflow:hidden}
figure.code .lang{position:absolute;top:0;right:0;font-family:var(--mono);font-size:10px;
     letter-spacing:.1em;text-transform:uppercase;color:var(--muted);padding:5px 10px}
figure.code pre{margin:0;padding:16px 18px;overflow-x:auto}
figure.code code{background:none;border:0;padding:0;color:var(--code-ink);font-size:13px;line-height:1.6}
blockquote{margin:0 0 20px;padding:14px 18px;background:var(--accent-soft);
           border-left:3px solid var(--accent);border-radius:0 8px 8px 0}
blockquote p{margin:0 0 8px;font-size:15.5px;line-height:1.65}
blockquote p:last-child{margin:0}
.tw{overflow-x:auto;margin:0 0 22px}
table{border-collapse:collapse;width:100%;font-family:var(--sans);font-size:14px}
th,td{padding:9px 12px;text-align:left;border-bottom:1px solid var(--line);vertical-align:top}
thead th{border-bottom:1.5px solid var(--ink);font-size:12px;letter-spacing:.04em;
         text-transform:uppercase;color:var(--muted)}
tbody tr:hover{background:var(--accent-soft)}
hr{border:0;border-top:1px solid var(--line);margin:34px 0}
.anchor{opacity:0;margin-left:8px;font-size:.6em;text-decoration:none;color:var(--muted)}
h2:hover .anchor,h3:hover .anchor{opacity:1}
.nav{display:flex;gap:12px;margin-top:60px;padding-top:24px;border-top:1px solid var(--line)}
.nav a{flex:1;text-decoration:none;border:1px solid var(--line);border-radius:10px;padding:14px 16px;
       background:var(--paper)}
.nav a:hover{border-color:var(--accent)}
.nav .dir{font-size:11px;letter-spacing:.12em;text-transform:uppercase;color:var(--muted)}
.nav .nm{font-family:var(--serif);font-size:15px;color:var(--ink);margin-top:4px;display:block}
.nav a.next{text-align:right}
/* ---- hub ---- */
.hub{max-width:1180px;margin:0 auto;padding:44px 20px 90px}
.hub h1{font-size:38px;margin-bottom:8px}
.lead{font-family:var(--serif);font-size:17.5px;line-height:1.7;color:var(--muted);max-width:680px}
.stats{display:flex;gap:26px;flex-wrap:wrap;margin:26px 0 8px;padding:16px 0;
       border-top:1px solid var(--line);border-bottom:1px solid var(--line)}
.stat b{display:block;font-family:var(--serif);font-size:26px;line-height:1.1}
.stat span{font-size:11.5px;color:var(--muted);letter-spacing:.06em;text-transform:uppercase}
.tools{display:flex;gap:10px;align-items:center;margin:22px 0 6px;flex-wrap:wrap}
#q{flex:1;min-width:220px;font:inherit;font-size:14px;padding:9px 13px;border:1px solid var(--line);
   border-radius:999px;background:var(--paper);color:var(--ink)}
#q:focus{outline:none;border-color:var(--accent)}
.track{margin-top:38px}
.track h2{font-family:var(--serif);font-size:23px;margin:0 0 4px}
.track .sub{font-size:14px;color:var(--muted);margin-bottom:18px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(285px,1fr));gap:16px}
.card{display:block;text-decoration:none;color:inherit;background:var(--paper);border:1px solid var(--line);
      border-radius:14px;padding:18px 18px 16px;transition:border-color .15s,transform .15s}
.card:hover{border-color:var(--accent);transform:translateY(-2px)}
.card .ic{font-size:22px;line-height:1}
.card h3{font-family:var(--serif);font-size:17px;margin:10px 0 6px;line-height:1.35}
.card p{font-size:13.5px;line-height:1.6;color:var(--muted);margin:0 0 12px;
        display:-webkit-box;-webkit-line-clamp:3;-webkit-box-orient:vertical;overflow:hidden}
.card .foot{display:flex;gap:10px;align-items:center;font-size:11.5px;color:var(--muted)}
.pill{border:1px solid var(--line);border-radius:999px;padding:2px 9px}
.pill.pri{border-color:var(--accent);color:var(--accent)}
.card.read{border-color:var(--ok)}
.card.read .ic::after{content:" ✓";color:var(--ok);font-size:14px}
.hide{display:none}
figure.chart{margin:0 0 22px;padding:14px;background:var(--paper);border:1px solid var(--line);
             border-radius:10px;text-align:center;color:var(--ink)}
figure.chart svg{max-width:100%;height:auto;font-family:var(--sans)}
figure.chart svg text{fill:currentColor}
figure.chart .cap, figure.chart figcaption{font-size:13px;color:var(--muted);margin-top:6px}
td.ok{background:color-mix(in srgb,var(--ok) 14%,transparent)}
td.mid{background:color-mix(in srgb,#d4a017 16%,transparent)}
td.gap{background:color-mix(in srgb,var(--warn) 14%,transparent)}
@media (max-width:900px){
  .wrap{grid-template-columns:minmax(0,1fr)} aside{display:none}
  main{padding-top:28px} h1{font-size:28px} main p{font-size:16px}
}
@media print{
  .top,aside,.nav,#bar{display:none} .wrap{display:block} main{max-width:none;padding:0}
  body{background:#fff}
}
"""

JS_PAGE = """
const KEY='eai_notes_read_v1', TKEY='eai_notes_theme', CKEY='eai_notes_check_v1';
const readSet=()=>{try{return new Set(JSON.parse(localStorage.getItem(KEY)||'[]'))}catch(e){return new Set()}};
const saveSet=s=>{try{localStorage.setItem(KEY,JSON.stringify([...s]))}catch(e){}};
(function(){
  try{const t=localStorage.getItem(TKEY); if(t)document.documentElement.dataset.theme=t;}catch(e){}
  const tb=document.getElementById('theme');
  if(tb)tb.onclick=()=>{const d=document.documentElement;
    const cur=d.dataset.theme||(matchMedia('(prefers-color-scheme: dark)').matches?'dark':'light');
    const nx=cur==='dark'?'light':'dark'; d.dataset.theme=nx;
    try{localStorage.setItem(TKEY,nx)}catch(e){}};
  // 진행 바
  const bar=document.getElementById('bar');
  const onScroll=()=>{const h=document.body.scrollHeight-innerHeight;
    bar.style.width=(h>0?Math.min(100,scrollY/h*100):0)+'%';};
  addEventListener('scroll',onScroll,{passive:true}); onScroll();
  // 읽음 표시
  const id=document.body.dataset.note, rb=document.getElementById('readbtn');
  const sync=()=>{const s=readSet(); const on=s.has(id);
    rb.classList.toggle('on',on); rb.textContent=on?'✓ 읽음':'읽음 표시';};
  if(rb){rb.onclick=()=>{const s=readSet(); s.has(id)?s.delete(id):s.add(id); saveSet(s); sync();}; sync();}
  // 체크박스 저장
  let cs={}; try{cs=JSON.parse(localStorage.getItem(CKEY)||'{}')}catch(e){}
  document.querySelectorAll('li.task input').forEach((el,i)=>{
    const k=id+':'+i; if(cs[k])el.checked=true;
    el.addEventListener('change',()=>{cs[k]=el.checked;
      try{localStorage.setItem(CKEY,JSON.stringify(cs))}catch(e){}});
  });
  // TOC 하이라이트
  const links=[...document.querySelectorAll('aside a')];
  const map=new Map(links.map(a=>[a.getAttribute('href').slice(1),a]));
  const heads=[...document.querySelectorAll('main h2[id],main h3[id]')];
  if(heads.length){
    const io=new IntersectionObserver(es=>{
      es.forEach(e=>{ if(e.isIntersecting){ links.forEach(a=>a.classList.remove('active'));
        const a=map.get(e.target.id); if(a)a.classList.add('active'); }});
    },{rootMargin:'-80px 0px -70% 0px',threshold:0});
    heads.forEach(h=>io.observe(h));
  }
  // 키보드: j/k 다음·이전 노트
  addEventListener('keydown',e=>{
    if(e.target.matches('input,textarea'))return;
    if(e.key==='j'){const n=document.querySelector('.nav a.next'); if(n)location.href=n.href;}
    if(e.key==='k'){const p=document.querySelector('.nav a.prev'); if(p)location.href=p.href;}
  });
})();
"""

JS_HUB = """
const KEY='eai_notes_read_v1', TKEY='eai_notes_theme';
(function(){
  try{const t=localStorage.getItem(TKEY); if(t)document.documentElement.dataset.theme=t;}catch(e){}
  const tb=document.getElementById('theme');
  if(tb)tb.onclick=()=>{const d=document.documentElement;
    const cur=d.dataset.theme||(matchMedia('(prefers-color-scheme: dark)').matches?'dark':'light');
    const nx=cur==='dark'?'light':'dark'; d.dataset.theme=nx; try{localStorage.setItem(TKEY,nx)}catch(e){}};
  let read=new Set(); try{read=new Set(JSON.parse(localStorage.getItem(KEY)||'[]'))}catch(e){}
  const cards=[...document.querySelectorAll('.card')];
  cards.forEach(c=>{ if(read.has(c.dataset.id)) c.classList.add('read'); });
  const rc=document.getElementById('readcount');
  if(rc)rc.textContent=cards.filter(c=>read.has(c.dataset.id)).length;
  const q=document.getElementById('q');
  if(q)q.addEventListener('input',()=>{
    const v=q.value.trim().toLowerCase();
    cards.forEach(c=>c.classList.toggle('hide', v && !c.dataset.search.includes(v)));
    document.querySelectorAll('.track').forEach(t=>{
      const any=[...t.querySelectorAll('.card')].some(c=>!c.classList.contains('hide'));
      t.classList.toggle('hide',!any);
    });
  });
  const rb=document.getElementById('resetread');
  if(rb)rb.onclick=()=>{ if(confirm('읽음 표시를 모두 지울까?')){
    try{localStorage.removeItem(KEY)}catch(e){} location.reload(); }};
})();
"""


def shell(title, body, js, extra_head=""):
    return f"""<!doctype html>
<html lang="ko"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>{esc(title)}</title>{extra_head}
<style>{CSS}</style></head>
<body>{body}<script>{js}</script></body></html>"""


def read_time(text):
    return max(1, round(len(re.sub(r"\s+", "", text)) / 500))


def note_page(note, prev_note, next_note, coll):
    toc, used = [], set()
    body = render(note["lines"], toc, used)
    toc_html = "".join(
        f'<li class="l{lvl}"><a href="#{hid}">{esc(text)}</a></li>'
        for lvl, text, hid in toc if lvl in (2, 3))
    nav = []
    if prev_note:
        nav.append(f'<a class="prev" href="{prev_note["file"]}">'
                   f'<span class="dir">← 이전</span><span class="nm">{esc(prev_note["title"])}</span></a>')
    if next_note:
        nav.append(f'<a class="next" href="{next_note["file"]}">'
                   f'<span class="dir">다음 →</span><span class="nm">{esc(next_note["title"])}</span></a>')
    meta = [f'{note["minutes"]}분 읽기', f'{note["lines_n"]}줄', f'{len(toc)}개 절']
    if note.get("badge"):
        meta.insert(0, note["badge"])
    return shell(note["title"], f"""
<div id="bar"></div>
<div class="top"><div class="top-in">
  <a class="home" href="../index.html">← 노트 목록</a>
  <span class="t">{esc(coll["title_ko"])} · {esc(note["title"])}</span>
  <button class="btn" id="readbtn">읽음 표시</button>
  <button class="btn" id="theme">◐</button>
</div></div>
<div class="wrap">
  <main>
    <div class="hero">
      <div class="kicker">{esc(coll["title_ko"])} · {esc(coll["title_en"])}</div>
      <h1>{inline(note["h1"])}</h1>
      <div class="meta">{"".join(f"<span>{esc(m)}</span>" for m in meta)}</div>
    </div>
    {body}
    <div class="nav">{"".join(nav)}</div>
  </main>
  <aside><div class="lbl">목차</div><ol>{toc_html}</ol></aside>
</div>""", JS_PAGE, extra_head="")


def hub_page(colls, totals):
    tracks = []
    for c in colls:
        cards = []
        for nt in c["notes"]:
            pills = [f'<span class="pill">{nt["minutes"]}분</span>']
            if nt.get("badge"):
                pills.append(f'<span class="pill">{esc(nt["badge"])}</span>')
            if nt.get("priority"):
                pills.append(f'<span class="pill pri">{esc(nt["priority"])}</span>')
            search = esc((nt["title"] + " " + nt["summary"] + " " + nt["headings"]).lower())
            cards.append(
                f'<a class="card" data-id="{esc(nt["id"])}" data-search="{search}" '
                f'href="{c["key"]}/{nt["file"]}">'
                f'<div class="ic">{esc(nt.get("icon", "📄"))}</div>'
                f'<h3>{esc(nt["title"])}</h3><p>{esc(nt["summary"])}</p>'
                f'<div class="foot">{"".join(pills)}</div></a>')
        tracks.append(f"""<section class="track">
  <h2>{esc(c["title_ko"])} <span style="color:var(--muted);font-size:15px">· {esc(c["title_en"])}</span></h2>
  <div class="sub">{esc(c["blurb"])} — <code>{esc(c["src_hint"])}/</code></div>
  <div class="grid">{"".join(cards)}</div>
</section>""")
    return shell("Embedded AI 스터디 노트", f"""
<div class="top"><div class="top-in">
  <span class="t"><b>Embedded AI Engineer</b> · 스터디 노트 (Hark JD 기준)</span>
  <button class="btn" id="resetread">읽음 초기화</button>
  <button class="btn" id="theme">◐</button>
</div></div>
<div class="hub">
  <h1>Embedded AI 준비 노트</h1>
  <p class="lead">펌웨어 엔지니어가 edge ML 엔지니어로 넘어가기 위한 노트. <b>나침반</b>에서 전체 목차와 진행 현황을 보고,
     모듈 A부터 순서대로 읽는다. 읽은 노트는 체크로 남고, 체크리스트도 이 브라우저에 저장된다.</p>
  <div class="stats">
    <div class="stat"><b>{totals["notes"]}</b><span>노트</span></div>
    <div class="stat"><b id="readcount">0</b><span>읽음</span></div>
    <div class="stat"><b>{totals["minutes"]}분</b><span>총 읽기 시간</span></div>
    <div class="stat"><b>{totals["sections"]}</b><span>절</span></div>
  </div>
  <p class="lead"><a href="compass/study_prep_list.html"><b>→ 나침반 (전체 목차 · 진행 현황)</b></a> ·
     <a href="../../../job_interview_prep_research/hark_ai_embedded_ai/hark_ai_embedded_ai_context.html">포지션 컨텍스트</a></p>
  <div class="tools"><input id="q" placeholder="노트 검색 — 제목·요약·소제목 (예: backprop, softmax, 행렬곱, DataLoader)"></div>
  {"".join(tracks)}
</div>""", JS_HUB)


# ----------------------------------------------------------------- 수집
PRIORITY = {}


def load_meta():
    meta = {}
    for f in sorted(META_DIR.glob("[0-9]*.json")):
        try:
            d = json.load(open(f, encoding="utf-8"))
            meta[f.stem] = d
        except Exception:                                 # noqa: BLE001
            pass
    return meta


def collect():
    meta = load_meta()
    colls = []
    for c in COLLECTIONS:
        notes = []
        if not c["dir"].exists():
            continue
        for f in sorted(c["dir"].glob(c.get("glob", "*.md"))):
            text = f.read_text(encoding="utf-8")
            lines = text.splitlines()
            h1 = next((HEAD.match(l).group(2) for l in lines if HEAD.match(l)
                       and len(HEAD.match(l).group(1)) == 1), f.stem)
            title = title_text(h1)
            m = meta.get(f.stem, {})
            # 요약: 첫 인용문 또는 첫 문단
            summary, buf = "", []
            for l in lines[1:]:
                s = l.strip()
                if s.startswith(">"):
                    q = s.lstrip("> ").strip()
                    q = re.sub(r"^[-*+]\s+", "", q)          # 인용 안 불릿 기호 제거
                    if q:
                        buf.append(strip_md(q))
                    continue
                if buf:
                    break
                if s and not s.startswith("#") and not s.startswith("---"):
                    buf.append(strip_md(s))
                    break
            summary = " · ".join(x for x in buf if x)
            summary = re.sub(r"\s+", " ", re.sub(r"\*\*|\[|\]", "", summary))[:200]
            heads = " ".join(strip_md(HEAD.match(l).group(2)) for l in lines if HEAD.match(l))
            notes.append({
                "id": f"{c['key']}/{f.stem}", "stem": f.stem, "file": f.stem + ".html",
                "h1": h1, "title": title, "summary": summary or m.get("summary_ko", "")[:190],
                "lines": lines, "lines_n": len(lines), "minutes": read_time(text),
                "headings": heads, "icon": m.get("icon", c.get("icon", "📗")),
                "badge": m.get("range", ""), "priority": PRIORITY.get(f.stem, ""),
                "sections": sum(1 for l in lines if HEAD.match(l) and len(HEAD.match(l).group(1)) == 2),
            })
        if notes:
            colls.append(dict(c, notes=notes))
    return colls


def main():
    colls = collect()
    if not any(c["notes"] for c in colls):
        print("노트를 찾지 못했습니다."); return 1
    OUT.mkdir(exist_ok=True)
    totals = {"notes": 0, "minutes": 0, "sections": 0}
    for c in colls:
        d = OUT / c["key"]
        d.mkdir(exist_ok=True)
        for i, nt in enumerate(c["notes"]):
            prev_n = c["notes"][i - 1] if i else None
            next_n = c["notes"][i + 1] if i + 1 < len(c["notes"]) else None
            (d / nt["file"]).write_text(
                note_page(nt, prev_n, next_n, c).replace(
                    "<body>", f'<body data-note="{esc(nt["id"])}">', 1), encoding="utf-8")
            totals["notes"] += 1
            totals["minutes"] += nt["minutes"]
            totals["sections"] += nt["sections"]
        print(f"  {c['key']:<8} {len(c['notes'])}개 → site/{c['key']}/")
    (OUT / "index.html").write_text(hub_page(colls, totals), encoding="utf-8")
    print(f"built {OUT/'index.html'}  (노트 {totals['notes']} · {totals['minutes']}분 · "
          f"{totals['sections']}절)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
