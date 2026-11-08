#!/usr/bin/env python3
"""HWLAB-M11 Part C 데이터 생성기 (결정적: 같은 seed → 바이트 단위로 같은 CSV)

가상의 보드 VALBRD-B rev A, 10G 리타이머 U7 의 lane0 / lane3 를 BERT 로 잰 결과를 흉내 낸다.
  sweep : 샘플링 위상 k/256 UI (k=1..255) 마다 1e7 비트 (PRBS7) → 오류 수   (= BER 1e-6 깊이까지)
  long  : 마스크 위상 0.30 / 0.70 UI 에서 2.5e7 비트 장시간 측정 → 오류 수
지터 모델(정답)은 spec.json 의 lanes.*.injected. lane3 에는 '드문 큰 지터'(확률 1e-6 로 ±0.20 UI 추가)를
심어 두었다 — 예: 전원 버스트가 PLL 공급을 흔들 때만 생기는 저확률 DJ.

  python3 -I gen_bert.py        # bert_sweep.csv 를 다시 쓴다 (~20 s)

참고: 실제 BERT 는 위상마다 따로 비트를 보내지만, 여기선 lane 마다 지터 레코드 하나(sweep 용)를 모든 위상에
재사용한다 — 에지 지터 카운트(solution.bathtub_counts)가 위상 수와 무관하게 O(에지 수)로 끝나게 하기 위해서.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)            # -I 는 스크립트 디렉터리를 sys.path 에서 뺀다
sys.dont_write_bytecode = True
import solution as S                # noqa: E402  (같은 레슨: jitter_sampler / measure_bathtub)


def long_run(model, n_bits, phases, seed, P=10):
    """마스크 위상들에서 같은 지터 레코드로 오류 수 #(j > x) + #(j < x − 1).
    마스크 위상이 k/10 격자(0.30, 0.70) 위에 있으므로 bathtub_counts(P=10) 를 그대로 쓴다."""
    ks = [int(round(x * P)) for x in phases]
    assert all(abs(k / P - x) < 1e-12 for k, x in zip(ks, phases)), "mask phases must be on the k/P grid"
    errs = S.bathtub_counts(S.edge_jitters(model, int(round(S.RHO_T * n_bits)), seed), P)
    return [errs[k - 1] for k in ks]


def main():
    with open(os.path.join(HERE, "spec.json"), encoding="utf-8") as f:
        spec = json.load(f)
    P, nb = spec["sweep"]["P"], spec["sweep"]["bits_per_phase"]
    nl, mph = spec["long_run"]["bits_per_point"], spec["mask_phases_ui"]
    lines = ["# HWLAB-M11 synthetic BERT export: %s, %s, %.1f Gb/s %s" % (spec["board"], spec["dut"],
                                                                       spec["line_rate_gbps"], spec["pattern"]),
             "# kind=sweep: phase k/%d UI, %d bits each; kind=long: mask phases, %d bits each" % (P, nb, nl),
             "lane,kind,phase_ui,bits,errors"]
    for lane in sorted(spec["lanes"]):
        cfg = spec["lanes"][lane]
        for x, n, e in S.measure_bathtub(cfg["injected"], nb, P, cfg["seed_sweep"]):
            lines.append("%s,sweep,%.8f,%d,%d" % (lane, x, n, e))
        for x, e in zip(mph, long_run(cfg["injected"], nl, mph, cfg["seed_long"])):
            lines.append("%s,long,%.8f,%d,%d" % (lane, x, nl, e))
    path = os.path.join(HERE, "bert_sweep.csv")
    with open(path, "w", encoding="ascii", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote %s (%d rows)" % (os.path.basename(path), len(lines) - 3))


if __name__ == "__main__":
    main()
