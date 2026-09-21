#!/usr/bin/env python3
"""Assemble the self-contained study dashboard.

Reads data/roles.json, data/interview.json, and data/NN_*.json (the 10 topic
sets), inlines them into dashboard.template.html at the __BANK_DATA__ marker,
and writes index.html.  Run after editing any data/*.json:

    python3 build_dashboard.py
"""
import json, glob, os, sys, datetime

ROOT = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(ROOT, "data")
TEMPLATE = os.path.join(ROOT, "dashboard.template.html")
OUT = os.path.join(ROOT, "index.html")

def load(name):
    with open(os.path.join(DATA, name), encoding="utf-8") as f:
        return json.load(f)

def main():
    topics = []
    for path in sorted(glob.glob(os.path.join(DATA, "[0-9]*.json"))):
        topics.append(json.load(open(path, encoding="utf-8")))
    topics.sort(key=lambda t: t.get("order", 0))

    def load_opt(name, default):
        p = os.path.join(DATA, name)
        return json.load(open(p, encoding="utf-8")) if os.path.exists(p) else default

    bank = {
        "generated": datetime.date.today().isoformat(),
        "profile": load("profile.json"),
        "roles": load("roles.json"),
        "interview": load("interview.json"),
        "mocks": load_opt("mocks.json", []),
        "companies": load_opt("companies.json", {}),
        "topics": topics,
    }

    # ensure_ascii=False keeps Korean readable; escape '<' so no "</script>" can
    # terminate the host <script> tag early. JSON.parse handles < fine.
    payload = json.dumps(bank, ensure_ascii=False).replace("<", "\\u003c")

    with open(TEMPLATE, encoding="utf-8") as f:
        html = f.read()
    if "__BANK_DATA__" not in html:
        sys.exit("ERROR: __BANK_DATA__ marker not found in template")
    html = html.replace("__BANK_DATA__", payload)

    with open(OUT, "w", encoding="utf-8") as f:
        f.write(html)

    nprob = sum(len(t.get("problems", [])) for t in topics)
    print(f"built {OUT}")
    print(f"  topics : {len(topics)}")
    print(f"  problems: {nprob}")
    print(f"  size    : {os.path.getsize(OUT)//1024} KB")

if __name__ == "__main__":
    main()
