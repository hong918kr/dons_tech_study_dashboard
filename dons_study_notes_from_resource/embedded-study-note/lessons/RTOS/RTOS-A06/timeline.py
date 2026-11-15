#!/usr/bin/env python3
"""RTOS-A06 timeline.py — rtos-trace v1 텍스트 → 태스크별 실행 타임라인 (SVG / HTML) + CPU 표.

표준 라이브러리만 쓴다. 실행:  python3 -I timeline.py trace.txt [--svg out.svg] [--html out.html]
                                  [--from-us N] [--to-us N] [--title TEXT]
--from-us / --to-us 는 "첫 이벤트 기준 µs" (wrap 을 푼 시각) 로 그릴 창. 표는 항상 전체 트레이스 기준.

디코드 규칙은 solution.c Part B 와 같다 (같은 입력이면 같은 숫자):
  - 시각: 직전 줄과의 차이 (t - prev) mod 2^32, 차이가 2^31 이상이면 "시간이 거꾸로" 오류
  - 주인: SWITCH_IN 이 바꾼다. ISR_ENTER/EXIT 는 위에 쌓인다 (ISR 시간 = ISR 몫, 깔린 태스크 = stolen)
  - DROP n: 다음 SWITCH_IN 까지 모름 (unknown) — 아무 태스크에도 주지 않는다
  - 응답 시간: RELEASE X → BLOCK X delay|delay_until, DROP 를 건너면 버림
SVG 는 노트 그림 규칙을 따른다: viewBox 폭 760, 색은 currentColor + class f-acc/f-ok/f-warn/f-mut.
"""
import argparse
import sys

EVENTS = ("RELEASE", "SWITCH_IN", "SWITCH_OUT", "BLOCK", "UNBLOCK", "ISR_ENTER", "ISR_EXIT",
          "MUTEX_TAKE", "MUTEX_GIVE", "Q_SEND_BLOCK", "Q_RECV_BLOCK", "DEADLINE_MISS", "DROP")
HEADER = "# rtos-trace v1 unit=us"


class TraceError(Exception):
    pass


def is_name(s):
    return 0 < len(s) <= 15 and all(c.isascii() and (c.isalnum() or c == "_") for c in s)


def decode(text):
    ctx = {}            # name -> dict(run, stolen, n_in, isr, resp[], open, rel)
    order = []
    iv = []             # (owner or None, t0, t1)
    marks = []          # (t, event, name, arg)
    st = dict(now=0, last32=None, wraps=0, task=None, isr=[], seg_owner=None, seg_t0=0,
              unknown=0, switches=0, drop_lines=0, dropped=0, mismatch=0, events=0)

    def get(name):
        if name not in ctx:
            ctx[name] = dict(run=0, stolen=0, n_in=0, isr=False, resp=[], open=False, rel=0, lost=0)
            order.append(name)
        return ctx[name]

    def owner():
        return st["isr"][-1] if st["isr"] else st["task"]

    def advance(new_now):
        dt = new_now - st["now"]
        if dt:
            if st["isr"]:
                ctx[st["isr"][-1]]["run"] += dt
                if st["task"] is not None:
                    ctx[st["task"]]["stolen"] += dt
            elif st["task"] is not None:
                ctx[st["task"]]["run"] += dt
            else:
                st["unknown"] += dt
        st["now"] = new_now

    def segment(final=False):
        o = owner()
        if not final and o == st["seg_owner"]:
            return
        if st["now"] > st["seg_t0"]:
            iv.append((st["seg_owner"], st["seg_t0"], st["now"]))
        st["seg_owner"], st["seg_t0"] = o, st["now"]

    header = False
    for lineno, raw in enumerate(text.split("\n"), 1):
        line = raw.rstrip("\r")
        if not header:
            if not line:
                continue
            if line != HEADER:
                raise TraceError("line %d: missing header" % lineno)
            header = True
            continue
        if not line or line.startswith("#"):
            continue
        tok = line.split()
        if not (3 <= len(tok) <= 4) or not tok[0].isdigit() or int(tok[0]) > 0xFFFFFFFF \
                or not is_name(tok[2]) or (len(tok) == 4 and not is_name(tok[3])):
            raise TraceError("line %d: syntax" % lineno)
        if tok[1] not in EVENTS:
            raise TraceError("line %d: unknown event %s" % (lineno, tok[1]))
        t32, ev, name = int(tok[0]), tok[1], tok[2]
        arg = tok[3] if len(tok) == 4 else ""
        if ev == "DROP" and not arg.isdigit():
            raise TraceError("line %d: DROP needs a count" % lineno)
        if st["last32"] is None:
            st["last32"], now = t32, 0
        else:
            delta = (t32 - st["last32"]) & 0xFFFFFFFF
            if delta >= 0x80000000:
                raise TraceError("line %d: time went backwards" % lineno)
            if t32 < st["last32"]:
                st["wraps"] += 1
            st["last32"] = t32
            now = st["now"] + delta
        advance(now)
        if ev == "DROP":
            st["drop_lines"] += 1
            st["dropped"] += int(arg)
            st["task"], st["isr"] = None, []
            for c in ctx.values():
                if c["open"]:
                    c["open"], c["lost"] = False, c["lost"] + 1
        else:
            c = get(name)
            if ev == "SWITCH_IN":
                if st["task"] != name:
                    st["switches"] += 1
                    c["n_in"] += 1
                st["task"] = name
            elif ev == "SWITCH_OUT":
                if st["task"] is not None and st["task"] != name:
                    st["mismatch"] += 1
            elif ev == "ISR_ENTER":
                if len(st["isr"]) == 8:
                    raise TraceError("line %d: ISR nesting > 8" % lineno)
                st["isr"].append(name)
                c["isr"] = True
                c["n_in"] += 1
            elif ev == "ISR_EXIT":
                if st["isr"] and st["isr"][-1] == name:
                    st["isr"].pop()
                else:
                    st["mismatch"] += 1
            if ev == "RELEASE":
                if not c["open"]:
                    c["open"], c["rel"] = True, now
            elif ev == "BLOCK" and arg in ("delay", "delay_until") and c["open"]:
                c["resp"].append(now - c["rel"])
                c["open"] = False
        if ev in ("RELEASE", "BLOCK", "DEADLINE_MISS", "DROP", "MUTEX_TAKE", "MUTEX_GIVE",
                  "Q_RECV_BLOCK", "Q_SEND_BLOCK"):
            marks.append((now, ev, name, arg))
        segment()
        st["events"] += 1
    if not header:
        raise TraceError("missing header")
    segment(final=True)
    st["span"] = st["now"]
    return ctx, order, iv, marks, st


def bp(run, span):
    return (run * 10000 + span // 2) // span if span else 0


def table(ctx, order, st):
    rows = ["%-8s %10s %8s %8s %6s %6s %9s" % ("ctx", "run_us", "stolen", "cpu%", "in", "resp#", "max_resp")]
    for n in order:
        c = ctx[n]
        b = bp(c["run"], st["span"])
        rows.append("%-8s %10d %8d %5d.%02d %6d %6d %9d%s" % (n, c["run"], c["stolen"], b // 100, b % 100, c["n_in"],
                    len(c["resp"]), max(c["resp"]) if c["resp"] else 0, "  (ISR)" if c["isr"] else ""))
    rows.append("span=%d unknown=%d wraps=%d switches=%d drop_lines=%d dropped=%d mismatch=%d intervals=%d" % (
        st["span"], st["unknown"], st["wraps"], st["switches"], st["drop_lines"], st["dropped"], st["mismatch"],
        st["intervals"]))
    return rows


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def nice_step(span_us, max_ticks=8):
    for m in (1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000, 100000, 200000, 500000):
        if span_us / m <= max_ticks:
            return m
    return 1000000


def render_svg(ctx, order, iv, marks, st, t_from, t_to, title):
    seen = {o for o, t0, t1 in iv if o is not None and t1 > t_from and t0 < t_to}
    seen |= {name for t, ev, name, arg in marks if t_from <= t <= t_to}
    rows = [n for n in order if ctx[n]["isr"] and n in seen]          # 창 안에 아무것도 없는 줄은 뺀다
    rows += [n for n in order if not ctx[n]["isr"] and n != "IDLE" and n in seen]
    if "IDLE" in seen:
        rows.append("IDLE")
    has_gap = any(o is None and t1 > t_from and t0 < t_to for o, t0, t1 in iv)
    W, X0, X1, TOP, RH = 760, 78, 744, 30, 22
    H = TOP + RH * (len(rows) + (1 if has_gap else 0)) + 34
    span = max(1, t_to - t_from)

    def x(t):
        return X0 + (min(max(t, t_from), t_to) - t_from) * (X1 - X0) / span

    out = ['<svg viewBox="0 0 %d %d" xmlns="http://www.w3.org/2000/svg" font-size="12">' % (W, H),
           '  <text x="8" y="18" fill="currentColor" font-size="13" font-weight="bold">%s</text>' % esc(title)]
    rowy = {n: TOP + RH * i for i, n in enumerate(rows)}
    gap_y = TOP + RH * len(rows)
    for n, y in rowy.items():
        out.append('  <text x="8" y="%d" fill="currentColor">%s</text>' % (y + 15, esc(n)))
        out.append('  <line x1="%d" y1="%d" x2="%d" y2="%d" stroke="currentColor" stroke-opacity="0.25"/>'
                   % (X0, y + 19, X1, y + 19))
    if has_gap:
        out.append('  <text x="8" y="%d" fill="currentColor">모름</text>' % (gap_y + 15))
    for o, t0, t1 in iv:
        if t1 <= t_from or t0 >= t_to:
            continue
        xa, xb = x(t0), x(t1)
        w = max(1.0, xb - xa)
        if o is None:
            out.append('  <rect x="%.1f" y="%d" width="%.1f" height="14" class="f-mut" stroke="currentColor" '
                       'stroke-dasharray="3 2"/>' % (xa, gap_y + 4, w))
            continue
        cls = "f-warn" if ctx[o]["isr"] else ("f-mut" if o == "IDLE" else "f-acc")
        out.append('  <rect x="%.1f" y="%d" width="%.1f" height="14" class="%s"/>' % (xa, rowy[o] + 4, w, cls))
    for t, ev, name, arg in marks:
        if t < t_from or t > t_to or name not in rowy:
            continue
        xm, y = x(t), rowy[name]
        if ev == "RELEASE":
            out.append('  <polygon points="%.1f,%d %.1f,%d %.1f,%d" fill="currentColor"/>'
                       % (xm, y + 1, xm - 3.5, y + 7, xm + 3.5, y + 7))
        elif ev == "DEADLINE_MISS":
            out.append('  <text x="%.1f" y="%d" fill="currentColor" font-weight="bold">x</text>' % (xm - 3, y + 8))
        elif ev in ("BLOCK", "Q_RECV_BLOCK", "Q_SEND_BLOCK") and arg not in ("delay", "delay_until"):
            out.append('  <line x1="%.1f" y1="%d" x2="%.1f" y2="%d" stroke="currentColor" stroke-width="2"/>'
                       % (xm, y + 2, xm, y + 20))
    ya = TOP + RH * (len(rows) + (1 if has_gap else 0)) + 4
    out.append('  <line x1="%d" y1="%d" x2="%d" y2="%d" stroke="currentColor"/>' % (X0, ya, X1, ya))
    step = nice_step(span)
    k = -(-t_from // step) * step
    while k <= t_to:
        xk = x(k)
        out.append('  <line x1="%.1f" y1="%d" x2="%.1f" y2="%d" stroke="currentColor"/>' % (xk, ya, xk, ya + 4))
        anchor = "end" if xk > X1 - 20 else "middle"          # 오른쪽 끝 라벨이 viewBox 밖으로 나가지 않게
        out.append('  <text x="%.1f" y="%d" fill="currentColor" text-anchor="%s">%s</text>'
                   % (xk, ya + 17, anchor, ("%g ms" % (k / 1000.0))))
        k += step
    out.append("</svg>")
    return "\n".join(out)


def render_html(svg, rows, title):
    css = """:root{--fg:#1d2433;--bg:#ffffff;--acc:#5b7cfa;--ok:#2f9e6a;--warn:#e07b39;--mut:#b8bfcc}
@media (prefers-color-scheme: dark){:root{--fg:#e6e9f0;--bg:#151821;--acc:#7f9cff;--ok:#4cc38a;--warn:#f0a060;--mut:#4a5263}}
body{margin:0;padding:16px;background:var(--bg);color:var(--fg);font-family:system-ui,sans-serif}
svg{max-width:100%;height:auto;color:var(--fg)}
.f-acc{fill:var(--acc)}.f-ok{fill:var(--ok)}.f-warn{fill:var(--warn)}.f-mut{fill:var(--mut)}
pre{font-size:13px;overflow-x:auto}"""
    return ("<!doctype html>\n<html><head><meta charset=\"utf-8\"><meta name=\"viewport\" "
            "content=\"width=device-width,initial-scale=1\"><title>RTOS trace timeline</title><style>%s</style>"
            "</head><body><h1 style=\"font-size:18px\">%s</h1>\n%s\n<pre>%s</pre></body></html>\n"
            % (css, esc(title), svg, esc("\n".join(rows))))


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("trace")
    ap.add_argument("--svg")
    ap.add_argument("--html")
    ap.add_argument("--from-us", type=int, default=0)
    ap.add_argument("--to-us", type=int)
    ap.add_argument("--title", default="RTOS trace timeline")
    a = ap.parse_args(argv)
    with open(a.trace, encoding="utf-8") as f:
        text = f.read()
    try:
        ctx, order, iv, marks, st = decode(text)
    except TraceError as e:
        print("decode error: %s" % e, file=sys.stderr)
        return 1
    st["intervals"] = len(iv)
    rows = table(ctx, order, st)
    print("\n".join(rows))
    t_to = a.to_us if a.to_us is not None else st["span"]
    svg = render_svg(ctx, order, iv, marks, st, a.from_us, t_to, a.title)
    if a.svg:
        with open(a.svg, "w", encoding="utf-8") as f:
            f.write(svg + "\n")
    if a.html:
        with open(a.html, "w", encoding="utf-8") as f:
            f.write(render_html(svg, rows, a.title))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
