#!/usr/bin/env python3
"""N06F F1 - roofline calculator for INT8 NPU layers (hypothetical NPU, NOT RAP1 numbers).

NPU assumption [assumed]: 64x64 = 4096 INT8 MACs @ 1.0 GHz  -> 8.192 TOPS dense
                          LPDDR5-6400 x64                   -> 51.2 GB/s
                          array maps Cin (rows) x Cout (cols), 64 lanes each
Per layer:
  ops       = 2 * MACs                         (multiply + add)
  bytes     = weights + input*refetch + output (INT8; refetch = how many times input is re-read)
  AI        = ops / bytes                      (arithmetic intensity, ops/B)
  map_eff   = how full the 64x64 array is for this shape (Cin, Cout padding; depthwise uses 1 row)
  attain    = min(peak * map_eff, AI * BW)     (roofline)
Python stdlib only.
"""
import math

MACS = 64 * 64
F_HZ = 1.0e9
PEAK = 2 * MACS * F_HZ           # ops/s
BW = 6400e6 * 8                  # B/s  (6400 MT/s * 8 B)
LANES = 64


def lane_eff(c):
    return c / (math.ceil(c / LANES) * LANES)


def layer(name, ho, wo, cin, cout, k, stride=1, dw=False, batch=1, refetch=1):
    hi, wi = ho * stride, wo * stride
    if dw:
        macs = batch * ho * wo * cout * k * k
        wbytes = k * k * cout
        map_eff = (1 / LANES) * lane_eff(cout)        # one input channel per output channel
    else:
        macs = batch * ho * wo * cout * k * k * cin
        wbytes = k * k * cin * cout
        map_eff = lane_eff(cin) * lane_eff(cout)
    in_b = batch * hi * wi * cin
    out_b = batch * ho * wo * cout
    bytes_ = wbytes + in_b * refetch + out_b + 4 * cout   # + int32 bias
    ops = 2 * macs
    ai = ops / bytes_
    compute_roof = PEAK * map_eff
    mem_roof = ai * BW
    attain = min(compute_roof, mem_roof)
    if compute_roof > mem_roof:
        bound = 'MEMORY'
    else:
        bound = 'compute' if map_eff > 0.9 else 'MAPPING'    # array mostly idle by shape
    return dict(name=name, macs=macs, bytes=bytes_, ai=ai, map_eff=map_eff, attain=attain,
                bound=bound, t_us=ops / attain * 1e6, util=attain / PEAK)


def show(rows):
    print(f"  {'layer':<34} {'MMACs':>8} {'MB':>7} {'AI':>7} {'map':>5} {'TOPS':>6} {'%peak':>6} {'bound':>8} {'t_us':>8}")
    for r in rows:
        print(f"  {r['name']:<34} {r['macs'] / 1e6:8.1f} {r['bytes'] / 1e6:7.2f} {r['ai']:7.1f} "
              f"{r['map_eff']:5.2f} {r['attain'] / 1e12:6.2f} {100 * r['util']:5.1f}% {r['bound']:>8} {r['t_us']:8.1f}")


def main():
    print("== hypothetical NPU ==")
    print(f"  peak = 2 x {MACS} MAC x {F_HZ / 1e9:.1f} GHz = {PEAK / 1e12:.3f} TOPS ; DDR BW = {BW / 1e9:.1f} GB/s ;"
          f" ridge point = {PEAK / BW:.0f} ops/B")

    print("\n== 1. per-layer roofline (batch 1, INT8, ideal reuse: every tensor touches DDR once) ==")
    show([
        layer('conv3x3 56x56 64->64 (ResNet st1)', 56, 56, 64, 64, 3),
        layer('conv1x1 28x28 512->128', 28, 28, 512, 128, 1),
        layer('conv3x3 480x272 64->64 (camera)', 480, 272, 64, 64, 3),
        layer('dwconv3x3 112x112 C=32', 112, 112, 32, 32, 3, dw=True),
        layer('conv3x3 56x56 3->64 (stem-like)', 56, 56, 3, 64, 3),
        layer('FC 2048->1000 batch 1', 1, 1, 2048, 1000, 1),
        layer('FC 2048->1000 batch 16', 1, 1, 2048, 1000, 1, batch=16),
    ])

    print("\n== 2. same camera layer, SRAM too small -> output-channel tiling re-reads the input ==")
    rows = []
    for co_tile in (64, 32, 16, 8, 4):
        rf = 64 // co_tile
        rows.append(layer(f'camera layer, Cout tile={co_tile:2d} (x{rf} input)', 480, 272, 64, 64, 3, refetch=rf))
    show(rows)

    print("\n== 3. measured vs model  [example numbers, not real silicon] ==")
    cam = layer('cam', 480, 272, 64, 64, 3)
    ops = 2 * cam['macs']
    t_meas_us = 1650.0                         # [example] measured with cycle counter
    p_meas_w = 6.0                             # [example] NPU rail power during the layer
    achieved = ops / (t_meas_us * 1e-6)
    print(f"  ops = {ops / 1e9:.2f} GOP ; roofline time = {cam['t_us']:.0f} us ; measured = {t_meas_us:.0f} us")
    print(f"  achieved = {achieved / 1e12:.2f} TOPS = {100 * achieved / PEAK:.1f}% of peak,"
          f" {100 * achieved / cam['attain']:.1f}% of this layer's roof")
    print(f"  TOPS/W  = {achieved / 1e12:.2f} / {p_meas_w:.1f} W = {achieved / 1e12 / p_meas_w:.2f} TOPS/W")
    total_cyc = t_meas_us * 1e-6 * F_HZ
    busy_cyc = 1_174_000                       # [example] NPU 'MAC busy' perf counter
    print(f"  cross-check: MAC-busy counter {busy_cyc} / {total_cyc:.0f} cycles = {busy_cyc / total_cyc:.3f}"
          f" vs timer-based {achieved / PEAK:.3f} -> {'consistent' if abs(busy_cyc / total_cyc - achieved / PEAK) < 0.02 else 'MISMATCH: check counter/timer'}")
    print("  sparse marketing: 2:4 structured sparsity doubles nominal ops -> 'sparse TOPS' = 2 x dense TOPS")


if __name__ == '__main__':
    main()
