#!/usr/bin/env python3
"""SIV-A08 합성 웨이퍼 로트 생성기 (결정적: 같은 입력이면 바이트까지 같은 파일)

  python3 -I gen_wafer_lot.py        # lot_L7K12.csv, lot_L7K31.csv, lot_L7K31_truth.json 재생성

제품 RX7 (SIV-M01 의 가상 차량용 센싱 SoC), CP 테스트 프로그램 P2.1, 다이 16 x 16 mm, 300 mm 웨이퍼,
에지 제외 3 mm, 로트 25장. 숫자는 전부 교육용 대표값/가정이다 — 실제 공정·실제 제품의 값이 아니다.

  L7K12  기준 로트 (excursion 없음)
  L7K31  excursion 로트 4종 심음 → 정답은 lot_L7K31_truth.json (solution 은 테스트에서만 읽는다)

줄 형식: wafer,x,y,vmin_mv,fmax_mhz,leak_ma,fails      fails = "SA+TD" (없으면 빈 칸), 전 항목을 끝까지 돌린 full datalog.
Python 3.9 표준 라이브러리만 쓴다.
"""
import json
import math
import os
import random

HERE = os.path.dirname(os.path.abspath(__file__))
TESTS = ("CONT", "IDDQ", "PLL", "SA", "TD", "MBIST", "VMIN", "SERDES")
PITCH_MM, R_USABLE_MM, AREA_CM2 = 16.0, 147.0, 2.56
WAFERS = 25
D0_PER_CM2 = 0.11                     # 평균 킬러 결함 밀도 (대표값)
CLUSTER_ALPHA = 2.0                   # 2x2 다이 블록마다 공유하는 Gamma(alpha, 1/alpha) 배율 → 국소 군집
# 결함 클래스: (면적 비율, {test: 그 클래스 결함이 있을 때 그 테스트가 fail 할 확률})
CLASSES = {
    "LOGIC":  (0.34, {"SA": 0.92, "TD": 0.97, "IDDQ": 0.55, "VMIN": 0.35}),
    "DELAY":  (0.08, {"TD": 1.00, "VMIN": 0.50}),
    "MEM":    (0.30, {"MBIST": 1.00, "VMIN": 0.25}),
    "ANALOG": (0.10, {"PLL": 0.85, "SERDES": 0.90}),
    "IO":     (0.08, {"SERDES": 0.70, "IDDQ": 0.30}),
    "PAD":    (0.10, {t: 1.00 for t in TESTS}),    # 접촉 불량이면 전 항목이 깨진다
}
NOISE_P = 0.002     # 항목별 독립 오판정 (프로브 접촉 흔들림 등)
VMIN_FAIL_MV, LEAK_FAIL_MA = 770.0, 175.0

RING = {"kind": "EDGE_RING", "wafers": [5, 6, 7, 8], "test": "MBIST", "rn_min": 0.85, "p": 0.55}
CLUSTER = {"kind": "CLUSTER", "wafer": 3, "cx": -4, "cy": 3, "radius": 2.6, "p_sa": 0.85, "p_td": 0.80}
MASKED = {"kind": "MASKED", "wafers": [13, 14, 15, 16], "test": "MBIST", "behind": "SA", "p": 0.18}
SHIFT = {"kind": "PARAM_SHIFT", "wafers": [19, 20, 21, 22, 23, 24], "dz": -0.70}


def sites():
    """전체가 사용 영역 안에 들어오는 다이 격자 좌표 (x, y). (0,0) 다이 중심 = 웨이퍼 중심."""
    out = []
    for y in range(-10, 11):
        for x in range(-10, 11):
            cx = abs(x) * PITCH_MM + PITCH_MM / 2
            cy = abs(y) * PITCH_MM + PITCH_MM / 2
            if math.hypot(cx, cy) <= R_USABLE_MM:
                out.append((x, y))
    return out


def poisson(rng, lam):
    """Knuth. lam 이 작으므로 충분하다."""
    limit, k, p = math.exp(-lam), 0, 1.0
    while True:
        p *= rng.random()
        if p <= limit:
            return k
        k += 1


def gen_wafer(rng, wafer, sl, rmax, excursions, stat):
    tilt_x, tilt_y = rng.gauss(0, 0.012), rng.gauss(0, 0.012)
    w_z = rng.gauss(0, 0.10)
    w_defect = math.exp(rng.gauss(0, 0.15))
    blocks = {}
    rows = []
    for x, y in sl:
        rn = math.hypot(x, y) / rmax
        z = w_z - 0.15 * rn * rn + tilt_x * x + tilt_y * y + rng.gauss(0, 0.42)
        if excursions and wafer in SHIFT["wafers"]:
            z += SHIFT["dz"]
        fmax = 2280 + 170 * z + rng.gauss(0, 18)
        vmin = 720 - 28 * z + rng.gauss(0, 7)
        leak = 95 * math.exp(0.40 * z + rng.gauss(0, 0.10))
        key = (x // 2, y // 2)
        if key not in blocks:
            blocks[key] = rng.gammavariate(CLUSTER_ALPHA, 1.0 / CLUSTER_ALPHA)
        lam = AREA_CM2 * D0_PER_CM2 * w_defect * blocks[key] * (1.0 + 0.6 * rn ** 8)
        stat["lam_sum"] += lam
        stat["n"] += 1
        fails = set()
        for cname, (frac, plist) in CLASSES.items():
            for _ in range(poisson(rng, lam * frac)):
                for t, p in plist.items():
                    if rng.random() < p:
                        fails.add(t)
        for t in TESTS:
            if rng.random() < NOISE_P:
                fails.add(t)
        if vmin > VMIN_FAIL_MV:
            fails.add("VMIN")
        if leak > LEAK_FAIL_MA:
            fails.add("IDDQ")
        if excursions:
            if wafer in RING["wafers"] and rn >= RING["rn_min"] and rng.random() < RING["p"]:
                fails.add(RING["test"])
            if wafer == CLUSTER["wafer"]:
                if math.hypot(x - CLUSTER["cx"], y - CLUSTER["cy"]) <= CLUSTER["radius"]:
                    if rng.random() < CLUSTER["p_sa"]:
                        fails.add("SA")
                    if rng.random() < CLUSTER["p_td"]:
                        fails.add("TD")
            if wafer in MASKED["wafers"] and rng.random() < MASKED["p"]:
                fails.add(MASKED["behind"])    # 공유 wrapper 불량: scan 도 깨지고 MBIST 도 깨진다
                fails.add(MASKED["test"])
        names = "+".join(t for t in TESTS if t in fails)
        rows.append("%d,%d,%d,%.1f,%.1f,%.2f,%s" % (wafer, x, y, vmin, fmax, leak, names))
    return rows


def gen_lot(lot, seed, excursions):
    rng = random.Random(seed)
    sl = sites()
    rmax = max(math.hypot(x, y) for x, y in sl)
    stat = {"lam_sum": 0.0, "n": 0}
    lines = ["# lot=%s product=RX7 program=P2.1 die=16x16mm wafers=%d" % (lot, WAFERS),
             "wafer,x,y,vmin_mv,fmax_mhz,leak_ma,fails"]
    for w in range(1, WAFERS + 1):
        lines.extend(gen_wafer(rng, w, sl, rmax, excursions, stat))
    return "\n".join(lines) + "\n", stat["lam_sum"] / stat["n"]


def main():
    for lot, seed, exc in (("L7K12", 7012, False), ("L7K31", 7031, True)):
        text, lam = gen_lot(lot, seed, exc)
        with open(os.path.join(HERE, "lot_%s.csv" % lot), "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        print("lot_%s.csv %d bytes  mean_lambda=%.4f" % (lot, len(text), lam))
    truth = {"lot": "L7K31", "area_cm2": AREA_CM2, "d0_mean_per_cm2": D0_PER_CM2,
             "n_sites": len(sites()), "ring": RING, "cluster": CLUSTER, "masked": MASKED, "shift": SHIFT,
             "lambda_mean_L7K12": round(gen_lot("L7K12", 7012, False)[1], 6)}
    with open(os.path.join(HERE, "lot_L7K31_truth.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(truth, f, ensure_ascii=False, indent=1, sort_keys=True)
        f.write("\n")
    print("lot_L7K31_truth.json")


if __name__ == "__main__":
    main()
