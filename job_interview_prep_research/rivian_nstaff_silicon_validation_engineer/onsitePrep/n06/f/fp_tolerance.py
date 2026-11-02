#!/usr/bin/env python3
"""N06F F1 - how to set an FP16/BF16 compare tolerance for an NPU dot-product / GEMM unit.

Question this answers: "INT8 is bit-exact. What do you compare against for FP16/BF16, and how
do you pick the tolerance so a correct NPU passes and a buggy one fails?"

Models (all see the SAME rounded fp16/bf16 inputs):
  ref        exact sum of the rounded-input products (math.fsum), i.e. only accumulation error is left
  spec_seq   spec-compliant HW: products exact, fp32 accumulate, k = 0..K-1 in order
  spec_tile  also spec-compliant: fp32 partial sums per 32-wide tile, then fp32 sum of partials
             (different order -> different bits, both are correct)
  bug_f16acc spec violation: accumulator kept in fp16
  bug_drop   tile-edge bug: last product (k = K-1) dropped

Tolerance rules compared:
  bound : worst-case forward error bound for recursive summation (Higham)
          |hw - ref| <= gamma_K * S,  gamma_K = K*u/(1-K*u),  S = sum_k |a_k*b_k|,  u = 2^-24
  stat  : probabilistic rule-of-thumb (rounding errors add like a random walk)
          |hw - ref| <= 2*sqrt(K) * u * S      -- much tighter, still passes correct HW
Python stdlib only. Deterministic seed.
"""
import math
import random
import struct

U32 = 2.0 ** -24          # unit roundoff fp32
TILE = 32


def f16(x):
    return struct.unpack('<e', struct.pack('<e', x))[0]


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


def bf16(x):
    u = struct.unpack('<I', struct.pack('<f', x))[0]
    u = (u + 0x7FFF + ((u >> 16) & 1)) & 0xFFFF0000      # round to nearest even
    return struct.unpack('<f', struct.pack('<I', u))[0]


def ref(a, b):
    return math.fsum(x * y for x, y in zip(a, b))


def spec_seq(a, b):
    acc = 0.0
    for x, y in zip(a, b):
        acc = f32(acc + f32(x * y))
    return acc


def spec_tile(a, b):
    parts = []
    for t in range(0, len(a), TILE):
        acc = 0.0
        for x, y in zip(a[t:t + TILE], b[t:t + TILE]):
            acc = f32(acc + f32(x * y))
        parts.append(acc)
    acc = 0.0
    for p in parts:
        acc = f32(acc + p)
    return acc


def bug_f16acc(a, b):
    acc = 0.0
    for x, y in zip(a, b):
        acc = f16(acc + x * y)
    return acc


def bug_drop(a, b):
    return spec_tile(a[:-1], b[:-1])


MODELS = [('spec_seq', spec_seq), ('spec_tile', spec_tile),
          ('bug_f16acc', bug_f16acc), ('bug_drop', bug_drop)]


def make_vectors(rng, K, rnd, n_random=48):
    vecs = []
    for _ in range(n_random):
        a = [rnd(rng.uniform(-1, 1)) for _ in range(K)]
        b = [rnd(rng.uniform(-1, 1)) for _ in range(K)]
        vecs.append(('random', a, b))
    h = K // 2                                    # exact sum is 0 -> relative error undefined
    x = [rnd(rng.uniform(-1, 1)) for _ in range(h)]
    y = [rnd(rng.uniform(-1, 1)) for _ in range(h)]
    vecs.append(('cancel', x + x, y + [-v for v in y]))
    a = [0.0] * K
    b = [0.0] * K
    a[-1], b[-1] = 0.5, 0.75                     # one-hot on the last k (tile edge)
    vecs.append(('onehot', a, b))
    return vecs


def campaign(K, rnd, rng):
    vecs = make_vectors(rng, K, rnd)
    rows = []
    for name, fn in MODELS:
        worst_ratio = 0.0
        worst_ulps = 0.0
        f_exact = f_bound = f_stat = f_rtol = f_atol = 0
        for _kind, a, b in vecs:
            r = ref(a, b)
            h = fn(a, b)
            S = math.fsum(abs(x * y) for x, y in zip(a, b))
            err = abs(h - r)
            tol = (K * U32 / (1 - K * U32)) * S
            worst_ratio = max(worst_ratio, err / tol if tol else 0.0)
            worst_ulps = max(worst_ulps, err / (U32 * S) if S else 0.0)
            f_exact += h != f32(r)
            f_bound += err > tol
            f_stat += err > 2 * math.sqrt(K) * U32 * S
            f_rtol += err > 1e-3 * abs(r)              # naive relative tolerance
            f_atol += err > 1e-2                       # naive absolute tolerance
        rows.append((name, worst_ulps, worst_ratio, f_exact, f_bound, f_stat, f_rtol, f_atol))
    return len(vecs), rows


def order_mismatch(K, rnd, rng):
    vecs = make_vectors(rng, K, rnd)
    return sum(spec_seq(a, b) != spec_tile(a, b) for _, a, b in vecs), len(vecs)


def input_quant_trap(K, rng):
    """Reference built from UNROUNDED fp64 inputs: error is dominated by input quantization."""
    worst = {'fp16': [0.0, 0.0], 'bf16': [0.0, 0.0]}
    for fmt, rnd in (('fp16', f16), ('bf16', bf16)):
        for _ in range(16):
            a = [rng.uniform(-1, 1) for _ in range(K)]
            b = [rng.uniform(-1, 1) for _ in range(K)]
            r_unrounded = math.fsum(x * y for x, y in zip(a, b))
            h = spec_seq([rnd(v) for v in a], [rnd(v) for v in b])
            S = math.fsum(abs(x * y) for x, y in zip(a, b))
            tol = (K * U32 / (1 - K * U32)) * S
            stat = 2 * math.sqrt(K) * U32 * S
            worst[fmt][0] = max(worst[fmt][0], abs(h - r_unrounded) / tol)
            worst[fmt][1] = max(worst[fmt][1], abs(h - r_unrounded) / stat)
    return worst


def main():
    rng = random.Random(2026_11_01)
    print("== F1 FP tolerance study: fp16 inputs, fp32 accumulate (spec) ==")
    print("worst_err_ulpS = max |hw-ref| / (u32 * sum|a*b|);  err/tol = vs gamma_K bound (>1 fails)")
    print("fail counts over N vectors: exact = bit-exact vs fp32(ref), bound = gamma_K*S,"
          " stat = 2*sqrt(K)*u*S, rtol = 1e-3*|ref|, atol = 1e-2")
    for K in (64, 256, 1024, 4096):
        n, rows = campaign(K, f16, rng)
        print(f"\nK={K:5d}  N={n} vectors (48 random + 1 cancel + 1 onehot)  gamma_K/u32={K / (1 - K * U32):.1f}")
        print(f"  {'model':<11} {'worst_err_ulpS':>14} {'err/tol':>10} {'exact':>6} {'bound':>6} {'stat':>5} {'rtol':>5} {'atol':>5}")
        for name, ulps, ratio, fe, fb, fs, fr, fa in rows:
            print(f"  {name:<11} {ulps:14.2f} {ratio:10.3f} {fe:6d} {fb:6d} {fs:5d} {fr:5d} {fa:5d}")

    print("\n== two CORRECT implementations, different summation order ==")
    for K in (256, 4096):
        m, n = order_mismatch(K, f16, rng)
        print(f"K={K:5d}: spec_seq vs spec_tile differ bit-wise in {m}/{n} outputs")

    print("\n== bf16 inputs, fp32 accumulate, K=1024 ==")
    n, rows = campaign(1024, bf16, rng)
    for name, ulps, ratio, fe, fb, fs, fr, fa in rows:
        print(f"  {name:<11} worst_err_ulpS={ulps:8.2f} fails: exact={fe} bound={fb} stat={fs} rtol={fr} atol={fa}")

    print("\n== trap: reference computed from UNROUNDED fp64 inputs (K=1024, spec_seq HW) ==")
    w = input_quant_trap(1024, rng)
    for fmt, (rb, rs) in w.items():
        print(f"  {fmt}: worst err/bound = {rb:6.1f} ({'FALSE FAIL' if rb > 1 else 'pass'}),"
              f" err/stat = {rs:7.1f} ({'FALSE FAIL' if rs > 1 else 'pass'})"
              "  <- input quantization, not HW error")


if __name__ == '__main__':
    main()
