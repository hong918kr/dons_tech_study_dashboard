#!/usr/bin/env python3
"""커리큘럼 아이템 리스트 검토 · 한눈에 보기.

  python3 review_items.py            # 검증 리포트 출력 + 개요 md/html 생성
  python3 review_items.py --dups     # 트랙 간 비슷한 제목 후보도 출력

입력  curriculum/stages.json, curriculum/tracks/<TRACK>.json
입력  curriculum/envs.json (실습환경 기록: 환경별 준비 방법·현재 상태)
출력  2026-11-07_curriculum_items_overview.md (표) / .html (임베디드 스터디 노트: 전체 보기 + 트랙별 보기 + 실습환경)
"""
import difflib
import html
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
CUR = ROOT / "curriculum"
STEM = "2026-11-07_curriculum_items_overview"
FIELDS = ("id", "level", "title", "goal", "task", "tags", "env", "env_note")
LV = {"B": "Basic", "M": "Mid", "A": "Advanced"}


ST = {"ready": "바로 가능", "partial": "일부 준비됨", "todo": "준비 필요", "optional": "선택"}


def load_envs():
    return json.loads((CUR / "envs.json").read_text())


def load():
    stages = json.loads((CUR / "stages.json").read_text())
    tracks = {}
    for p in sorted((CUR / "tracks").glob("*.json")):
        try:
            tracks[p.stem] = json.loads(p.read_text())
        except json.JSONDecodeError as e:
            tracks[p.stem] = {"_error": str(e)}
    return stages, tracks


def validate(stages, tracks):
    probs = []
    envs = set(stages["env"])
    rec = [x["id"] for x in load_envs()["envs"]]
    if set(rec) != envs:
        probs.append(f"envs.json 환경 목록 불일치: {sorted(set(rec) ^ envs)}")
    known = {t["id"] for s in stages["stages"] for t in s["tracks"]}
    for s in stages["stages"]:
        for t in s["tracks"]:
            tid = t["id"]
            d = tracks.get(tid)
            if d is None:
                probs.append(f"{tid}: 파일 없음")
                continue
            if "_error" in d:
                probs.append(f"{tid}: JSON 오류 {d['_error']}")
                continue
            if d.get("id") != tid:
                probs.append(f"{tid}: id 필드 불일치 ({d.get('id')})")
            for k in ("intro", "goals", "prereq", "refs", "items"):
                if k not in d:
                    probs.append(f"{tid}: 트랙 필드 누락 {k}")
            for p in d.get("prereq", []):
                if p not in known:
                    probs.append(f"{tid}: 알 수 없는 prereq {p}")
            seen, seq = set(), {"B": 0, "M": 0, "A": 0}
            last_lv = "B"
            for it in d.get("items", []):
                iid = it.get("id", "?")
                miss = [f for f in FIELDS if f not in it]
                if miss:
                    probs.append(f"{iid}: 필드 누락 {miss}")
                m = re.fullmatch(rf"{tid}-([BMA])(\d\d)", iid)
                if not m:
                    probs.append(f"{iid}: id 형식 오류")
                    continue
                lv, n = m.group(1), int(m.group(2))
                if it.get("level") != lv:
                    probs.append(f"{iid}: level 필드({it.get('level')})와 id 불일치")
                if "BMA".index(lv) < "BMA".index(last_lv):
                    probs.append(f"{iid}: 레벨 순서 역전")
                last_lv = lv
                if n != seq[lv] + 1:
                    probs.append(f"{iid}: 번호 건너뜀/중복 (기대 {lv}{seq[lv] + 1:02d})")
                seq[lv] = n
                if iid in seen:
                    probs.append(f"{iid}: 중복 id")
                seen.add(iid)
                if it.get("env") not in envs:
                    probs.append(f"{iid}: env 값 오류 {it.get('env')}")
                if it.get("env") != "mac" and not it.get("env_note"):
                    probs.append(f"{iid}: env={it.get('env')} 인데 env_note 비어 있음")
                if not str(it.get("task", "")).strip():
                    probs.append(f"{iid}: task 비어 있음")
    for extra in set(tracks) - known:
        probs.append(f"{extra}: stages.json 에 없는 트랙 파일")
    return probs


def dup_candidates(tracks, thresh=0.72):
    items = [(tid, it["id"], re.sub(r"\s+", " ", it["title"]).lower())
             for tid, d in tracks.items() if "items" in d for it in d["items"]]
    out = []
    for i in range(len(items)):
        for j in range(i + 1, len(items)):
            a, b = items[i], items[j]
            if a[0] == b[0]:
                continue
            r = difflib.SequenceMatcher(None, a[2], b[2]).ratio()
            if r >= thresh:
                out.append((r, a, b))
    return sorted(out, reverse=True)


def counts(d):
    c = {"B": 0, "M": 0, "A": 0}
    for it in d.get("items", []):
        c[it["level"]] = c.get(it["level"], 0) + 1
    return c


def ext(d):
    return sum(1 for it in d.get("items", []) if it.get("env") != "mac")


# ------------------------------------------------------------------ 출력
def build_md(stages, tracks):
    L = ["# 임베디드 스터디 노트 — 커리큘럼 아이템 목록", "",
         "생성: `python3 review_items.py` (curriculum/tracks/*.json 에서 생성. 손으로 고치지 말 것)", ""]
    tot = sum(len(d.get("items", [])) for d in tracks.values())
    L += [f"총 {len(tracks)}개 트랙 · {tot}개 아이템", "",
          "| 단계 | 트랙 | 이름 | B | M | A | 합계 | 외부환경 |", "|---|---|---|---|---|---|---|---|"]
    for s in stages["stages"]:
        for t in s["tracks"]:
            d = tracks.get(t["id"], {})
            c = counts(d)
            L.append(f"| {s['n']} | {t['id']} | {t['name']} | {c['B']} | {c['M']} | {c['A']} | "
                     f"{sum(c.values())} | {ext(d)} |")
    L += env_md(stages, tracks)
    for s in stages["stages"]:
        L += ["", f"## {s['n']}단계 · {s['name']} ({s['en']})"]
        for t in s["tracks"]:
            d = tracks.get(t["id"], {})
            L += ["", f"### {t['id']} — {t['name']} ({t['en']})", ""]
            if d.get("intro"):
                L += [d["intro"], ""]
            if not d.get("items"):
                L += ["_(아직 없음)_"]
                continue
            L += ["| ID | 제목 | 과제 | env |", "|---|---|---|---|"]
            for it in d["items"]:
                env = it["env"] if it["env"] == "mac" else f"**{it['env']}**"
                L.append(f"| {it['id']} | {it['title']} | {it['task']} | {env} |".replace("\n", " "))
    return "\n".join(L) + "\n"


def env_items(stages, tracks):
    """env id -> [(track id, track name, [items])] (커리큘럼 순서)"""
    out = {}
    for s in stages["stages"]:
        for t in s["tracks"]:
            for it in tracks.get(t["id"], {}).get("items", []):
                grp = out.setdefault(it["env"], [])
                if not grp or grp[-1][0] != t["id"]:
                    grp.append((t["id"], t["name"], []))
                grp[-1][2].append(it)
    return out


def env_md(stages, tracks):
    E = load_envs()
    by = env_items(stages, tracks)
    L = ["", "## 실습환경", "", f"호스트: {E['host']}", "",
         "| env | 이름 | 상태 | 아이템 | 어디서 |", "|---|---|---|---|---|"]
    for v in E["envs"]:
        n = sum(len(g[2]) for g in by.get(v["id"], []))
        L.append(f"| {v['id']} | {v['name']} | {ST[v['status']]} | {n} | {v['where']} |")
    for v in E["envs"]:
        grp = by.get(v["id"], [])
        L += ["", f"### {v['id']} — {v['name']} ({ST[v['status']]})", "",
              f"- 설명: {stages['env'][v['id']]}", f"- 어디서: {v['where']}", f"- 현재 있는 것: {v['have']}", "",
              "준비:", "", "```sh", *v["setup"], "```", "", f"확인: `{v['check']}`", ""]
        L += [f"- {t}" for t in v.get("tips", [])]
        if v["id"] == "mac":
            L += ["", f"해당 아이템: {sum(len(g[2]) for g in grp)}개 (전체 목록은 아래 트랙별 표)"]
            continue
        L += ["", "| 트랙 | 아이템 | 환경 메모 |", "|---|---|---|"]
        for tid, _, its in grp:
            for it in its:
                L.append(f"| {tid} | {it['id']} {it['title']} | {it.get('env_note', '')} |".replace("\n", " "))
    return L


CSS = """
:root{--bg:#fff;--soft:#f5f8fa;--ink:#14181c;--muted:#6a7178;--faint:#a2a9b0;--line:#e3e8ec;
 --sky:#2b8fd6;--sky-ink:#1c6fa8;--sky-soft:#eaf5fd;--sky-line:#b9dcf4;
 --b:#cfe4f3;--m:#7db4dc;--a:#24577c;--hl:#dff0fc}
@media (prefers-color-scheme:dark){:root:not([data-theme=light]){--bg:#101316;--soft:#171b1f;--ink:#e8ecef;
 --muted:#9aa3ab;--faint:#5f676e;--line:#283037;--sky:#5cb6ef;--sky-ink:#8ccdf5;--sky-soft:#13232f;--sky-line:#244a63;
 --b:#2a3e4c;--m:#4f8db8;--a:#a8d6f3;--hl:#18303f}}
:root[data-theme=dark]{--bg:#101316;--soft:#171b1f;--ink:#e8ecef;--muted:#9aa3ab;--faint:#5f676e;--line:#283037;
 --sky:#5cb6ef;--sky-ink:#8ccdf5;--sky-soft:#13232f;--sky-line:#244a63;--b:#2a3e4c;--m:#4f8db8;--a:#a8d6f3;--hl:#18303f}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);font-size:14px;line-height:1.5;
 font-family:-apple-system,BlinkMacSystemFont,"Apple SD Gothic Neo","Pretendard",sans-serif}
a{color:inherit}
/* header */
header{position:sticky;top:0;z-index:10;background:var(--bg);border-bottom:1px solid var(--line)}
header::before{content:"";display:block;height:3px;background:var(--sky)}
.hrow{max-width:1360px;margin:0 auto;padding:10px 20px;display:flex;gap:10px;align-items:center;flex-wrap:wrap}
.back{font-size:13px;text-decoration:none;color:var(--muted,#666);border:1px solid currentColor;border-radius:999px;padding:4px 11px;white-space:nowrap;margin-right:4px}.back:hover{color:var(--sky)}
.brand{font-size:17px;font-weight:700;letter-spacing:-.01em;margin-right:10px;white-space:nowrap}
.brand i{display:inline-block;width:9px;height:9px;border-radius:2px;background:var(--sky);margin-right:8px;vertical-align:1px}
.tabs{display:flex;border:1px solid var(--line);border-radius:8px;overflow:hidden}
.tabs button{border:0;border-radius:0;padding:6px 14px}
.tabs button+button{border-left:1px solid var(--line)}
.sp{flex:1}
input{width:230px;max-width:100%;font:inherit;font-size:13px;padding:6px 10px;border:1px solid var(--line);
 border-radius:8px;background:var(--bg);color:var(--ink)}
input:focus{outline:none;border-color:var(--sky)}
button{font:inherit;font-size:12.5px;padding:5px 11px;border:1px solid var(--line);border-radius:8px;
 background:var(--bg);color:var(--muted);cursor:pointer}
button:hover{color:var(--ink);border-color:var(--sky-line)}
button.on{background:var(--sky);border-color:var(--sky);color:#fff}
.filters{display:flex;gap:6px;flex-wrap:wrap}
/* swatches */
.sw{width:10px;height:10px;border-radius:2px;display:inline-block;flex:none}
.B{--lc:var(--b)}.M{--lc:var(--m)}.A{--lc:var(--a)}
.sw,.sq i{background:var(--lc)}
.sw.x,.sq i.x{background:repeating-linear-gradient(135deg,var(--lc) 0 2px,transparent 2px 4px);
 box-shadow:inset 0 0 0 1px var(--lc)}
main{max-width:1360px;margin:0 auto;padding:0 20px 80px}
body[data-view=all] #vtrack,body[data-view=all] #venv,body[data-view=track] #vall,body[data-view=track] #venv,body[data-view=env] #vall,body[data-view=env] #vtrack{display:none}
/* overview */
.stats{display:flex;gap:28px;flex-wrap:wrap;padding:20px 0 12px}
.stat b{display:block;font-size:24px;font-weight:650;font-variant-numeric:tabular-nums;line-height:1.2}
.stat:first-child b,.stat:nth-child(2) b{color:var(--sky-ink)}
.stat span{font-size:12px;color:var(--muted)}
.legend{display:flex;gap:14px;flex-wrap:wrap;font-size:12px;color:var(--muted);padding:0 0 12px}
.legend span{display:inline-flex;align-items:center;gap:6px}
.tw{overflow-x:auto;border:1px solid var(--line);border-radius:10px}
table.ov{border-collapse:collapse;width:100%;font-size:13px}
.ov th{position:sticky;top:0;background:var(--sky-soft);color:var(--sky-ink);font-size:11.5px;font-weight:600;
 text-align:left;padding:8px 10px;border-bottom:1px solid var(--sky-line);white-space:nowrap}
.ov td{padding:7px 10px;border-bottom:1px solid var(--line);vertical-align:middle}
.ov tr:last-child td{border-bottom:0}
.ov td.st{background:var(--soft);vertical-align:top;width:120px;border-right:1px solid var(--line)}
.ov td.st b{display:block;font-size:12.5px}.ov td.st span{font-size:11.5px;color:var(--muted)}
.ov td.st .no{color:var(--sky-ink);font-weight:700;font-size:11.5px;display:block}
.ov tr.row{cursor:pointer}
.ov tr.row:hover td:not(.st){background:var(--sky-soft)}
.ov tr.row.dim td:not(.st){opacity:.3}
.ov td.n{text-align:right;font-variant-numeric:tabular-nums;width:46px;color:var(--muted)}
.ov td.n.tot{color:var(--ink);font-weight:600}
.ov td.nm{white-space:nowrap}
.ov .tid{margin-right:8px}
.ov .hit{color:var(--sky-ink);font-weight:600;font-size:12px;margin-left:6px}
.ov td.map{min-width:260px}
.tid{font-family:ui-monospace,Menlo,monospace;font-size:11.5px;font-weight:700;color:var(--sky-ink)}
.sq{display:flex;flex-wrap:wrap;gap:2px}
.sq i{width:9px;height:9px;border-radius:2px;display:block}
.sq i.off{opacity:.12}
#tip{position:fixed;z-index:30;pointer-events:none;background:var(--ink);color:var(--bg);font-size:12.5px;
 padding:5px 9px;border-radius:6px;max-width:320px;line-height:1.4;display:none}
#tip .i{font-family:ui-monospace,Menlo,monospace;opacity:.65;margin-right:6px}
/* track view */
#vtrack{display:grid;grid-template-columns:230px minmax(0,1fr);gap:28px}
.side{position:sticky;top:var(--hh,60px);align-self:start;max-height:calc(100vh - var(--hh,60px));overflow-y:auto;
 padding:16px 4px 30px 0;border-right:1px solid var(--line)}
.side h3{font-size:11px;font-weight:600;color:var(--faint);letter-spacing:.04em;margin:16px 8px 4px}
.side h3:first-child{margin-top:0}
.side a{display:flex;gap:8px;align-items:baseline;padding:5px 8px;border-radius:6px;text-decoration:none;
 font-size:13px;color:var(--muted);margin-right:8px;border-left:3px solid transparent}
.side a:hover{color:var(--ink);background:var(--soft)}
.side a.cur{background:var(--sky-soft);color:var(--ink);border-left-color:var(--sky);font-weight:600}
.side a .nm{flex:1;line-height:1.35}
.side a .n{font-size:11px;font-variant-numeric:tabular-nums;opacity:.75}
.side a.dim{opacity:.35}
.side .tid{width:44px;flex:none}
#tsel{display:none;width:100%;font:inherit;padding:8px;border:1px solid var(--line);border-radius:8px;
 background:var(--bg);color:var(--ink);margin:14px 0 0}
.content{padding:20px 0 0;min-width:0}
.thead{border:1px solid var(--line);border-radius:12px;padding:16px 18px;background:linear-gradient(180deg,var(--sky-soft),var(--bg) 70%)}
.crumb{font-size:12px;color:var(--sky-ink);font-weight:600}
.content h2{font-size:23px;margin:2px 0 0;letter-spacing:-.01em}
.content h2 .tid{font-size:15px;margin-right:8px}
.content .en{color:var(--muted);font-size:13px}
.meta{display:flex;gap:6px;flex-wrap:wrap;margin:10px 0}
.meta a{display:inline-flex;gap:6px;align-items:center;font-size:12px;text-decoration:none;border:1px solid var(--line);
 border-radius:999px;padding:2px 10px;color:var(--muted);background:var(--bg)}
.meta a:hover{border-color:var(--sky);color:var(--ink)}
.intro{font-size:13.5px;line-height:1.7;margin:4px 0 8px}
.prq{font-size:12.5px;color:var(--muted);margin:2px 0 8px}
.prq a{color:var(--sky-ink);cursor:pointer;text-decoration:underline;text-underline-offset:2px}
.more{display:flex;gap:8px;flex-wrap:wrap;align-items:flex-start}
details.box{border:1px solid var(--line);border-radius:8px;background:var(--bg);flex:1 1 300px}
details.box summary{cursor:pointer;font-size:12.5px;font-weight:600;color:var(--sky-ink);padding:7px 12px;
 list-style:none;user-select:none}
details.box summary::-webkit-details-marker{display:none}
details.box summary::before{content:"▸";display:inline-block;width:14px;transition:transform .12s}
details.box[open] summary::before{transform:rotate(90deg)}
details.box ul{margin:0;padding:2px 14px 10px 30px;font-size:13px;line-height:1.65}
details.box a{color:var(--sky-ink)}
.lvh{display:flex;align-items:center;gap:8px;font-size:12px;font-weight:700;letter-spacing:.05em;
 text-transform:uppercase;margin:24px 0 10px;padding-bottom:6px;border-bottom:2px solid var(--sky)}
.lvh .c{font-weight:400;color:var(--muted);letter-spacing:0}
.cards{display:grid;grid-template-columns:repeat(auto-fill,minmax(270px,1fr));gap:10px}
.item{border:1px solid var(--line);border-top:3px solid var(--lc);border-radius:10px;padding:11px 13px;
 display:flex;flex-direction:column;gap:5px;background:var(--bg);transition:border-color .12s,box-shadow .12s}
.item:hover{border-color:var(--sky-line);border-top-color:var(--lc);box-shadow:0 2px 10px rgba(43,143,214,.08)}
.item.flash{background:var(--hl)}
.ih{display:flex;align-items:center;gap:6px}
.ih .iid{font-family:ui-monospace,Menlo,monospace;font-size:11.5px;color:var(--muted);flex:1}
.item h4{margin:0;font-size:14.5px;font-weight:650;line-height:1.4}
.note{font-size:12.5px;color:var(--sky-ink);line-height:1.5}
.desc{font-size:12.5px;line-height:1.6;color:var(--ink)}
.desc b,.envn b{font-weight:600;font-size:11px;color:var(--muted);margin-right:5px}
.envn{font-size:12px;line-height:1.5;padding:5px 8px;border-left:2px dashed var(--sky);background:var(--soft);border-radius:0 4px 4px 0}
.chips{margin-top:auto;padding-top:4px;display:flex;gap:4px;flex-wrap:wrap}
.chip{font-size:10.5px;color:var(--muted);background:var(--soft);border-radius:4px;padding:0 6px}
.chip.ext{color:var(--sky-ink);border:1px dashed var(--sky);background:transparent}
.empty{color:var(--muted);padding:30px 0}
/* env view */
.host{font-size:12.5px;color:var(--muted);padding:18px 0 12px}
.host b{color:var(--ink);font-weight:600;margin-right:6px}
.etiles{display:grid;grid-template-columns:repeat(auto-fill,minmax(170px,1fr));gap:8px;margin-bottom:8px}
.etile{display:flex;flex-direction:column;gap:3px;text-align:left;padding:10px 12px;border-radius:10px;color:var(--ink)}
.etile b{font-size:22px;font-weight:650;font-variant-numeric:tabular-nums;line-height:1.15;color:var(--sky-ink)}
.etile .k{font-size:12.5px;font-weight:600}
.etile .eid{font-family:ui-monospace,Menlo,monospace;font-size:11px;color:var(--muted);font-weight:400;margin-left:4px}
.pill{display:inline-block;font-size:10.5px;font-weight:600;border-radius:999px;padding:0 8px;line-height:17px;
 white-space:nowrap;width:max-content}
.pill.ready{background:var(--sky);color:#fff}
.pill.partial{background:var(--sky-soft);color:var(--sky-ink);border:1px solid var(--sky-line)}
.pill.todo{color:var(--sky-ink);border:1px dashed var(--sky)}
.pill.optional{color:var(--muted);border:1px solid var(--line)}
.esec{border:1px solid var(--line);border-radius:12px;margin:18px 0 0;overflow:hidden}
.esec>h2{display:flex;align-items:center;gap:10px;flex-wrap:wrap;margin:0;padding:12px 16px;font-size:18px;
 background:linear-gradient(180deg,var(--sky-soft),var(--bg));border-bottom:1px solid var(--line)}
.esec>h2 .tid{font-size:13px}
.esec>h2 .c{font-size:12.5px;font-weight:400;color:var(--muted)}
.esec .ed{font-size:12.5px;color:var(--muted);padding:10px 16px 0}
.ebody{display:grid;grid-template-columns:minmax(0,1fr) minmax(0,1fr);gap:0}
.ebody>div{padding:12px 16px 16px;min-width:0}
.ebody>div+div{border-left:1px solid var(--line)}
.ebody h5{margin:10px 0 4px;font-size:11px;font-weight:600;color:var(--faint);letter-spacing:.04em}
.ebody h5:first-child{margin-top:0}
.ebody p{margin:0;font-size:13px;line-height:1.6}
.ebody pre{margin:0;font:12px/1.6 ui-monospace,Menlo,monospace;background:var(--soft);border:1px solid var(--line);
 border-radius:8px;padding:8px 10px;overflow-x:auto;white-space:pre}
.ebody code{font:12px ui-monospace,Menlo,monospace;background:var(--soft);border-radius:4px;padding:1px 5px;word-break:break-all}
.ebody ul{margin:0;padding-left:18px;font-size:13px;line-height:1.6}
.egrp{margin:0 0 10px}
.egrp .gh{font-size:12px;font-weight:600;margin:0 0 3px;cursor:pointer}
.egrp .gh:hover{color:var(--sky-ink)}
.egrp ul{list-style:none;padding:0}
.egrp li{font-size:12.5px;line-height:1.45;padding:4px 0;border-top:1px dashed var(--line)}
.egrp li a{cursor:pointer;text-decoration:none}
.egrp li a:hover .t{color:var(--sky-ink);text-decoration:underline}
.egrp li .iid{font-family:ui-monospace,Menlo,monospace;font-size:11px;color:var(--muted);margin-right:6px}
.egrp li .en2{display:block;font-size:11.5px;color:var(--muted);margin-top:1px}
.ebig{font-size:13px;color:var(--muted)}
.hide{display:none!important}
@media (max-width:980px){#vtrack{grid-template-columns:190px minmax(0,1fr);gap:20px}.side a .n{display:none}
 .ov td.st{width:96px}}
@media (max-width:800px){.ebody{grid-template-columns:1fr}.ebody>div+div{border-left:0;border-top:1px solid var(--line)}}
@media (max-width:640px){
 .hrow{padding:10px 16px}main{padding:0 16px 60px}.sp{display:none}input{flex:1;width:auto}
 #vtrack{grid-template-columns:1fr;gap:0}.side{display:none}#tsel{display:block}
 .cards{grid-template-columns:1fr}.stats{gap:18px}}
"""

JS = r"""
const D=JSON.parse(document.getElementById('data').textContent);
const LV={B:'Basic',M:'Mid',A:'Advanced'};
const $=s=>document.querySelector(s),$$=s=>[...document.querySelectorAll(s)];
const esc=s=>String(s==null?'':s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const IT={},TR={};
D.tracks.forEach(t=>{TR[t.id]=t;t.items.forEach(i=>{IT[i.id]=i;i.tr=t.id;i.s=[i.id,i.t,i.k,i.g,i.n,i.tags.join(' ')].join(' ').toLowerCase()})});
let q='',lv='',extOnly=false,view='all',cur=D.tracks[0].id;
const ok=i=>(!q||i.s.includes(q))&&(!lv||i.l===lv)&&(!extOnly||i.e!=='mac');
const active=()=>!!(q||lv||extOnly);
const hh=()=>$('header').offsetHeight;
function setHH(){document.documentElement.style.setProperty('--hh',hh()+'px')}
addEventListener('resize',setHH);setHH();

function apply(){
 $$('tr.row').forEach(row=>{let n=0;
  row.querySelectorAll('.sq i').forEach(sq=>{const m=ok(IT[sq.dataset.id]);sq.classList.toggle('off',!m);if(m)n++});
  row.classList.toggle('dim',active()&&n===0);
  row.querySelector('.hit').textContent=active()?n+'건 일치':''});
 $$('.side a').forEach(a=>{const t=TR[a.dataset.t],n=t.items.filter(ok).length;
  a.querySelector('.n').textContent=active()?n+'/'+t.items.length:t.items.length;a.classList.toggle('dim',active()&&!n)});
 if(view==='track')renderItems();
 $$('.egrp li').forEach(li=>li.classList.toggle('hide',!ok(IT[li.dataset.id])));
 $$('.egrp').forEach(g=>g.classList.toggle('hide',!g.querySelector('li:not(.hide)')));
 $$('.esec').forEach(sec=>{const all=sec.querySelectorAll('.egrp li').length,n=sec.querySelectorAll('.egrp li:not(.hide)').length,
  c=sec.querySelector('h2 .hit');if(c)c.textContent=active()&&all?n+'/'+all+'건 일치':''});
}
$('#q').oninput=e=>{q=e.target.value.trim().toLowerCase();apply()};
$$('.filters [data-lv]').forEach(b=>b.onclick=()=>{lv=lv===b.dataset.lv?'':b.dataset.lv;
 $$('.filters [data-lv]').forEach(x=>x.classList.toggle('on',x.dataset.lv===lv));apply()});
$('#ext').onclick=e=>{extOnly=!extOnly;e.currentTarget.classList.toggle('on',extOnly);apply()};

function itemHTML(i){
 const env=i.e==='mac'?'':'<div class="envn"><b>외부환경</b>'+esc(i.e)+(i.n?' — '+esc(i.n):'')+'</div>';
 return '<article class="item '+i.l+'" id="i-'+i.id+'"><div class="ih"><span class="iid">'+i.id+'</span>'+
  (i.e!=='mac'?'<span class="chip ext">'+esc(i.e)+'</span>':'')+'<span class="sw '+i.l+'" title="'+LV[i.l]+'"></span></div>'+
  '<h4>'+esc(i.t)+'</h4><div class="note">'+esc(i.g)+'</div><div class="desc"><b>과제</b>'+esc(i.k)+'</div>'+env+
  '<div class="chips">'+i.tags.map(x=>'<span class="chip">#'+esc(x)+'</span>').join('')+'</div></article>';
}
function renderHead(){
 const t=TR[cur],c={B:0,M:0,A:0};let x=0;t.items.forEach(i=>{c[i.l]++;if(i.e!=='mac')x++});
 let h='<div class="thead"><div class="crumb">'+esc(t.stage)+'</div><h2><span class="tid">'+t.id+'</span>'+esc(t.name)+'</h2>'+
  '<div class="en">'+esc(t.en)+'</div><div class="meta">'+
  ['B','M','A'].map(l=>'<a href="#" data-jump="'+l+'"><span class="sw '+l+'"></span>'+LV[l]+' '+c[l]+'</a>').join('')+
  (x?'<a href="#" data-ext="1"><span class="sw M x"></span>외부환경 '+x+'</a>':'')+'</div>'+
  '<p class="intro">'+esc(t.intro)+'</p>';
 if(t.prereq.length)h+='<div class="prq">먼저 보면 좋은 트랙: '+t.prereq.map(p=>'<a data-go="'+p+'">'+p+(TR[p]?' '+esc(TR[p].name):'')+'</a>').join(', ')+'</div>';
 h+='<div class="more">';
 if(t.goals.length)h+='<details class="box"><summary>트랙 목표 '+t.goals.length+'개</summary><ul>'+t.goals.map(g=>'<li>'+esc(g)+'</li>').join('')+'</ul></details>';
 if(t.refs.length)h+='<details class="box"><summary>참고자료 '+t.refs.length+'개</summary><ul>'+t.refs.map(r=>'<li>'+(r.url?'<a href="'+esc(r.url)+'" target="_blank" rel="noopener">'+esc(r.title)+'</a>':esc(r.title))+'</li>').join('')+'</ul></details>';
 h+='</div></div>';
 $('#thead').innerHTML=h;
}
function renderItems(){
 const t=TR[cur];let h='',shown=0;
 ['B','M','A'].forEach(l=>{const xs=t.items.filter(i=>i.l===l&&ok(i));if(!xs.length)return;shown+=xs.length;
  h+='<div class="lvh" id="lv-'+l+'"><span class="sw '+l+'"></span>'+LV[l]+'<span class="c">'+xs.length+'개</span></div>'+
   '<div class="cards">'+xs.map(itemHTML).join('')+'</div>'});
 $('#titems').innerHTML=shown?h:'<p class="empty">검색·필터 조건에 맞는 아이템이 없습니다.</p>';
}
function show(v,tid,focus){
 view=v;document.body.dataset.view=v;
 $$('.tabs [data-view]').forEach(b=>b.classList.toggle('on',b.dataset.view===v));
 if(tid&&TR[tid])cur=tid;
 if(v==='track'){renderHead();renderItems();
  $$('.side a').forEach(a=>a.classList.toggle('cur',a.dataset.t===cur));$('#tsel').value=cur;
  const s=$('.side a.cur');if(s)s.scrollIntoView({block:'nearest'})}
 history.replaceState(null,'','#'+(v==='track'?cur+(focus?'/'+focus:''):v==='env'?'env'+(tid&&!TR[tid]?'/'+tid:''):'all'));
 if(v==='env'&&tid&&!TR[tid]){const el=document.getElementById('env-'+tid);
  if(el){scrollTo(0,el.getBoundingClientRect().top+scrollY-hh()-8);return}}
 if(focus){const el=document.getElementById('i-'+focus);if(el){
  scrollTo(0,el.getBoundingClientRect().top+scrollY-hh()-12);el.classList.add('flash');
  setTimeout(()=>el.classList.remove('flash'),1800);return}}
 scrollTo(0,0);
}
$$('.tabs [data-view]').forEach(b=>b.onclick=()=>show(b.dataset.view));
$$('tr.row').forEach(row=>{
 row.onclick=e=>{const sq=e.target.closest('.sq i');show('track',row.dataset.t,sq?sq.dataset.id:null)};
 row.onkeydown=e=>{if(e.key==='Enter'){e.preventDefault();show('track',row.dataset.t)}}});
$$('.side a').forEach(a=>a.onclick=e=>{e.preventDefault();show('track',a.dataset.t)});
$('#tsel').onchange=e=>show('track',e.target.value);
$('#thead').onclick=e=>{
 const j=e.target.closest('[data-jump]'),g=e.target.closest('[data-go]'),x=e.target.closest('[data-ext]');
 if(j){e.preventDefault();const el=document.getElementById('lv-'+j.dataset.jump);
  if(el)scrollTo(0,el.getBoundingClientRect().top+scrollY-hh()-8)}
 else if(g){e.preventDefault();show('track',g.dataset.go)}
 else if(x){e.preventDefault();if(!extOnly)$('#ext').click()}};

$('#venv').onclick=e=>{
 const a=e.target.closest('[data-i]'),g=e.target.closest('[data-gt]'),j=e.target.closest('[data-env]');
 if(a){e.preventDefault();show('track',IT[a.dataset.i].tr,a.dataset.i)}
 else if(g){show('track',g.dataset.gt)}
 else if(j){const el=document.getElementById('env-'+j.dataset.env);
  if(el){scrollTo(0,el.getBoundingClientRect().top+scrollY-hh()-8);history.replaceState(null,'','#env/'+j.dataset.env)}}};
const tip=$('#tip');
document.addEventListener('mouseover',e=>{const sq=e.target.closest('.sq i');if(!sq){tip.style.display='none';return}
 const i=IT[sq.dataset.id];tip.innerHTML='<span class="i">'+i.id+'</span>'+esc(i.t)+(i.e!=='mac'?' ['+esc(i.e)+']':'');
 tip.style.display='block'});
document.addEventListener('mousemove',e=>{if(tip.style.display!=='block')return;
 const w=tip.offsetWidth;tip.style.left=Math.min(e.clientX+12,innerWidth-w-8)+'px';tip.style.top=(e.clientY+16)+'px'});

const hs=decodeURIComponent(location.hash.slice(1));
if(hs&&hs!=='all'){const [t,f]=hs.split('/');if(t==='env')show('env',f);else show('track',t,f)}else show('all');
"""


def env_view(stages, tracks):
    e = lambda s: html.escape(str(s))
    E = load_envs()
    by = env_items(stages, tracks)
    tiles, secs = [], []
    for v in E["envs"]:
        grp = by.get(v["id"], [])
        n = sum(len(g[2]) for g in grp)
        pill = f'<span class="pill {v["status"]}">{ST[v["status"]]}</span>'
        tiles.append(f'<button class="etile" data-env="{v["id"]}"><b>{n}</b>'
                     f'<span class="k">{e(v["name"])}<span class="eid">{v["id"]}</span></span>{pill}</button>')
        left = (f'<h5>어디서</h5><p>{e(v["where"])}</p><h5>현재 있는 것</h5><p>{e(v["have"])}</p>'
                f'<h5>준비</h5><pre>{e(chr(10).join(v["setup"]))}</pre>'
                f'<h5>확인</h5><p><code>{e(v["check"])}</code></p>'
                + (f'<h5>메모</h5><ul>{"".join(f"<li>{e(t)}</li>" for t in v["tips"])}</ul>' if v.get("tips") else ""))
        if v["id"] == "mac":
            right = (f'<h5>해당 아이템</h5><p class="ebig">{n}개 — 나머지 전부. '
                     f'트랙별 목록은 <b>트랙별 보기</b>에서 확인 (검색·레벨 필터 그대로 적용).</p>')
        else:
            gs = []
            for tid, tname, its in grp:
                lis = "".join(
                    f'<li data-id="{e(i["id"])}"><a data-i="{e(i["id"])}" href="#{tid}/{e(i["id"])}">'
                    f'<span class="iid">{e(i["id"])}</span><span class="t">{e(i["title"])}</span></a>'
                    + (f'<span class="en2">{e(i["env_note"])}</span>' if i.get("env_note") else "") + '</li>'
                    for i in its)
                gs.append(f'<div class="egrp"><div class="gh" data-gt="{tid}"><span class="tid">{tid}</span>'
                          f'{e(tname)} · {len(its)}</div><ul>{lis}</ul></div>')
            right = f'<h5>해당 아이템 (누르면 트랙별 보기로 이동)</h5>{"".join(gs)}'
        secs.append(f'<section class="esec" id="env-{v["id"]}"><h2><span class="tid">{v["id"]}</span>{e(v["name"])}'
                    f'{pill}<span class="c">{n}개</span><span class="hit"></span></h2>'
                    f'<div class="ed">{e(stages["env"][v["id"]])}</div>'
                    f'<div class="ebody"><div>{left}</div><div>{right}</div></div></section>')
    return (f'<div id="venv"><div class="host"><b>호스트</b>{e(E["host"])}</div>'
            f'<div class="etiles">{"".join(tiles)}</div>{"".join(secs)}</div>')


def build_html(stages, tracks):
    e = lambda s: html.escape(str(s))
    tot = sum(len(d.get("items", [])) for d in tracks.values())
    lvc = {"B": 0, "M": 0, "A": 0}
    n_ext = 0
    data, rows, side, opts = [], [], [], []
    for s in stages["stages"]:
        side.append(f'<h3>{s["n"]}. {e(s["name"])}</h3>')
        opts.append(f'<optgroup label="{s["n"]}. {e(s["name"])}">')
        st_items = sum(len(tracks.get(t["id"], {}).get("items", [])) for t in s["tracks"])
        for k, t in enumerate(s["tracks"]):
            d = tracks.get(t["id"], {})
            its = d.get("items", [])
            c = counts(d)
            for lv in c:
                lvc[lv] += c[lv]
            x = ext(d)
            n_ext += x
            data.append({"id": t["id"], "name": t["name"], "en": t["en"], "stage": f'{s["n"]}단계 · {s["name"]}',
                         "intro": d.get("intro", ""), "goals": d.get("goals", []), "refs": d.get("refs", []),
                         "prereq": d.get("prereq", []),
                         "items": [{"id": i["id"], "l": i["level"], "t": i["title"], "g": i.get("goal", ""),
                                    "k": i.get("task", ""), "e": i.get("env", "mac"), "n": i.get("env_note", ""),
                                    "tags": i.get("tags", [])} for i in its]})
            sq = "".join(f'<i class="{i["level"]}{"" if i["env"] == "mac" else " x"}" data-id="{e(i["id"])}"></i>'
                         for i in its)
            stc = (f'<td class="st" rowspan="{len(s["tracks"])}"><span class="no">{s["n"]}단계</span>'
                   f'<b>{e(s["name"])}</b><span>{len(s["tracks"])}트랙 · {st_items}개</span></td>') if k == 0 else ""
            rows.append(f'<tr class="row" tabindex="0" data-t="{t["id"]}">{stc}'
                        f'<td class="nm"><span class="tid">{t["id"]}</span>{e(t["name"])}<span class="hit"></span></td>'
                        f'<td class="n">{c["B"]}</td><td class="n">{c["M"]}</td><td class="n">{c["A"]}</td>'
                        f'<td class="n tot">{len(its)}</td><td class="n">{x or "·"}</td>'
                        f'<td class="map"><div class="sq">{sq}</div></td></tr>')
            side.append(f'<a href="#{t["id"]}" data-t="{t["id"]}"><span class="tid">{t["id"]}</span>'
                        f'<span class="nm">{e(t["name"])}</span><span class="n">{len(its)}</span></a>')
            opts.append(f'<option value="{t["id"]}">{t["id"]} — {e(t["name"])} ({len(its)})</option>')
        opts.append('</optgroup>')
    blob = json.dumps({"tracks": data}, ensure_ascii=False).replace("</", "<\\/")
    stat = lambda n, lab: f'<div class="stat"><b>{n}</b><span>{lab}</span></div>'
    stats = (stat(len(tracks), "트랙") + stat(tot, "아이템") + stat(lvc["B"], "Basic") + stat(lvc["M"], "Mid")
             + stat(lvc["A"], "Advanced") + stat(n_ext, "외부환경필요"))
    legend = ('<div class="legend"><span><i class="sw B"></i>Basic</span><span><i class="sw M"></i>Mid</span>'
              '<span><i class="sw A"></i>Advanced</span><span><i class="sw M x"></i>빗금 = 외부환경필요</span>'
              '<span>네모 하나 = 아이템 하나 · 마우스를 올리면 제목 · 행을 누르면 트랙별 보기로 이동</span></div>')
    table = ('<div class="tw"><table class="ov"><thead><tr><th>단계</th><th>트랙</th><th>Basic</th><th>Mid</th>'
             '<th>Adv</th><th>합계</th><th>외부</th><th>아이템 맵</th></tr></thead><tbody>'
             + "".join(rows) + '</tbody></table></div>')
    head = ('<header><div class="hrow"><a class="back" href="site/index.html" title="커리큘럼 맵 · 문제 브라우저로 돌아가기">← 스터디 노트 홈</a>'
            '<span class="brand"><i></i>한눈에 보기</span>'
            '<div class="tabs"><button data-view="all">전체 보기</button><button data-view="track">트랙별 보기</button><button data-view="env">실습환경</button></div>'
            '<span class="sp"></span><input id="q" placeholder="검색: id, 제목, 과제, 태그">'
            '<div class="filters"><button data-lv="B">Basic</button><button data-lv="M">Mid</button>'
            '<button data-lv="A">Advanced</button><button id="ext">외부환경만</button></div></div></header>')
    vall = f'<div id="vall"><div class="stats">{stats}</div>{legend}{table}</div>'
    vtrack = (f'<div id="vtrack"><nav class="side">{"".join(side)}</nav><div class="content">'
              f'<select id="tsel" aria-label="트랙 선택">{"".join(opts)}</select>'
              '<div id="thead"></div><div id="titems"></div></div></div>')
    return ('<!doctype html><html lang="ko"><head><meta charset="utf-8">'
            '<meta name="viewport" content="width=device-width,initial-scale=1"><title>임베디드 스터디 노트</title>'
            f'<style>{CSS}</style></head><body data-view="all">{head}<main>{vall}{vtrack}{env_view(stages, tracks)}</main><div id="tip"></div>'
            f'<script type="application/json" id="data">{blob}</script><script>{JS}</script></body></html>\n')


def main():
    stages, tracks = load()
    probs = validate(stages, tracks)
    print(f"tracks {len(tracks)}/{sum(len(x['tracks']) for x in stages['stages'])}, items {sum(len(d.get('items', [])) for d in tracks.values())}")
    print(f"problems: {len(probs)}")
    for p in probs:
        print("  -", p)
    if "--dups" in sys.argv:
        for r, a, b in dup_candidates(tracks):
            print(f"  ~{r:.2f} {a[1]} {a[2]}  <->  {b[1]} {b[2]}")
    (ROOT / f"{STEM}.md").write_text(build_md(stages, tracks))
    (ROOT / f"{STEM}.html").write_text(build_html(stages, tracks))


if __name__ == "__main__":
    main()
