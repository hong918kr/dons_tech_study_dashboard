#!/usr/bin/env python3
"""BV12 - modeled shmoo (Vcore x frequency) for a fake SoC core.

Everything here is a MODEL, not silicon data. Purpose: practice reading a
shmoo plot, computing margin at the nominal operating point, and seeing why
"edge search" automation can hide a hole.

Model pieces
  * critical-path delay  : alpha-power law  d = K * V / (mu(T) * (V - Vth(T))**a)
      - Vth drops with temperature, mobility drops with temperature
        -> at low V the cold corner is slower ("temperature inversion")
  * process corner       : SS / TT / FF shift Vth and K
  * SRAM Vmin wall       : below Vmin_sram every frequency fails
  * noise                : per-trial Gaussian jitter on the delay (supply noise)
  * injected hole        : deterministic fail pocket at f=1.3 GHz, V=0.725-0.750
  * intermittent fails   : tiny random per-trial fail probability anywhere

Usage
  python3 -I shmoo_model.py            # text report only
  python3 -I shmoo_model.py --png out.png   # also save a PNG (needs matplotlib)

stdlib only, except the optional --png path.
"""
import math
import random
import statistics
import sys

# ----------------------------------------------------------------- settings
V_NOM = 0.750          # V   nominal core voltage
F_NOM = 1.50           # GHz nominal core frequency
V_GRID = [round(0.550 + 0.025 * i, 3) for i in range(15)]   # 0.550 .. 0.900
F_GRID = [round(0.8 + 0.1 * i, 1) for i in range(17)]       # 0.8 .. 2.4 GHz
TRIALS = 3             # runs per (V, f) point: like a test "loop count"
SETUP_NS = 0.040       # flop setup + clock uncertainty, ns
NOISE_SIGMA = 0.015    # 1.5 % of delay, per-trial (supply noise / jitter)
P_INTERMITTENT = 0.004 # per-trial random fail (rare bug / flaky infra)
SPEC_V_MARGIN = 0.05   # require >= 5 % voltage margin at nominal
SPEC_F_MARGIN = 0.10   # require >= 10 % frequency margin at nominal

CORNERS = {            # process corner: (dVth [V], K scale)
    "SS": (+0.020, 1.05),
    "TT": (0.000, 1.00),
    "FF": (-0.020, 0.95),
}
VTH0 = 0.350           # V at 25 C, TT
KVT = 0.0015           # V/C   Vth decreases ~1.5 mV per C
ALPHA = 1.3
SRAM_VMIN = {"SS": 0.610, "TT": 0.595, "FF": 0.585}   # V at 25 C
SRAM_COLD_PENALTY = 0.010                              # +10 mV at -40 C


def vth(corner, temp_c):
    return VTH0 + CORNERS[corner][0] - KVT * (temp_c - 25.0)


def mobility(temp_c):
    return ((temp_c + 273.15) / 298.15) ** -1.5


# K is calibrated so that TT / 25 C / 0.75 V has Fmax ~= 2.0 GHz
def _raw_delay(v, corner, temp_c):
    ov = v - vth(corner, temp_c)
    if ov <= 0.02:
        return float("inf")
    return CORNERS[corner][1] * v / (mobility(temp_c) * ov ** ALPHA)


K_CAL = (1.0 / 2.00 - SETUP_NS) / _raw_delay(0.75, "TT", 25.0)


def path_delay_ns(v, corner="TT", temp_c=25.0, dvth_die=0.0):
    ov = v - vth(corner, temp_c) - dvth_die
    if ov <= 0.02:
        return float("inf")
    return K_CAL * CORNERS[corner][1] * v / (mobility(temp_c) * ov ** ALPHA)


def sram_vmin(corner, temp_c, dvth_die=0.0):
    vm = SRAM_VMIN[corner] + 0.6 * dvth_die
    if temp_c < 0:
        vm += SRAM_COLD_PENALTY
    return vm


def in_hole(v, f):
    """Injected, repeatable fail pocket (e.g. clock-ratio-dependent bug)."""
    return abs(f - 1.3) < 1e-6 and 0.7249 <= v <= 0.7501


def run_test(v, f, corner, temp_c, rng, dvth_die=0.0,
             hole=True, noise=True, intermittent=True):
    """One test execution at (v, f). Returns True = PASS."""
    if v < sram_vmin(corner, temp_c, dvth_die):
        return False                        # SRAM Vmin wall
    if hole and in_hole(v, f):
        return False                        # injected hole
    if intermittent and rng.random() < P_INTERMITTENT:
        return False                        # rare random fail
    d = path_delay_ns(v, corner, temp_c, dvth_die)
    if noise:
        d *= 1.0 + rng.gauss(0.0, NOISE_SIGMA)
    return (1.0 / f) >= d + SETUP_NS


def shmoo_grid(corner, temp_c, rng, **kw):
    """grid[iv][jf] = number of passing trials out of TRIALS."""
    grid = []
    for v in V_GRID:
        row = []
        for f in F_GRID:
            row.append(sum(run_test(v, f, corner, temp_c, rng, **kw)
                           for _ in range(TRIALS)))
        grid.append(row)
    return grid


def cell_char(npass):
    if npass == TRIALS:
        return "*"
    if npass == 0:
        return "."
    return "~"


def print_shmoo(grid, title):
    print(title)
    print("  legend: * = pass all %d trials   ~ = mixed   . = fail all"
          "   [N] = nominal point" % TRIALS)
    hdr = "  Vcore(V) |"
    for j, f in enumerate(F_GRID):
        hdr += ("%-4.1f" % f) if j % 2 == 0 else "    "
    print(hdr.rstrip())
    print("  ---------+" + "-" * (2 * len(F_GRID) * 2))
    for i in range(len(V_GRID) - 1, -1, -1):
        v = V_GRID[i]
        line = "   %.3f   |" % v
        for j, f in enumerate(F_GRID):
            c = cell_char(grid[i][j])
            if abs(v - V_NOM) < 1e-6 and abs(f - F_NOM) < 1e-6:
                line += "[N]" if c == "*" else "[X]"
                line += " "
            else:
                line += " %s  " % c
        print(line.rstrip())
    print("            " + "freq (GHz) ->")


def idx(lst, x):
    for k, val in enumerate(lst):
        if abs(val - x) < 1e-6:
            return k
    raise ValueError(x)


def margins_from_grid(grid):
    """Vmin at F_NOM column and Fmax at V_NOM row, contiguous from nominal."""
    jn, iv = idx(F_GRID, F_NOM), idx(V_GRID, V_NOM)
    i = iv
    while i - 1 >= 0 and grid[i - 1][jn] == TRIALS:
        i -= 1
    vmin = V_GRID[i] if grid[iv][jn] == TRIALS else None
    j = jn
    while j + 1 < len(F_GRID) and grid[iv][j + 1] == TRIALS:
        j += 1
    fmax = F_GRID[j] if grid[iv][jn] == TRIALS else None
    return vmin, fmax


def find_holes(grid):
    """Fail cells that have a solid pass on BOTH sides along V or along f.
    The normal shmoo boundary is monotonic, so such a cell is suspicious."""
    holes = []
    nv, nf = len(V_GRID), len(F_GRID)
    for i in range(nv):
        for j in range(nf):
            if grid[i][j] == TRIALS:
                continue
            below = any(grid[k][j] == TRIALS for k in range(0, i))
            above = any(grid[k][j] == TRIALS for k in range(i + 1, nv))
            left = any(grid[i][k] == TRIALS for k in range(0, j))
            right = any(grid[i][k] == TRIALS for k in range(j + 1, nf))
            if (below and above) or (left and right):
                holes.append((V_GRID[i], F_GRID[j], grid[i][j]))
    return holes


# ------------------------------------------------- deterministic edge finding
def vmin_at(f, corner, temp_c, dvth_die=0.0, step=0.001):
    """Smallest V (1 mV steps, 0.45..1.0) that passes - noiseless model."""
    v = 0.450
    while v <= 1.0:
        ok = (v >= sram_vmin(corner, temp_c, dvth_die) and
              1.0 / f >= path_delay_ns(v, corner, temp_c, dvth_die) + SETUP_NS)
        if ok:
            return round(v, 3)
        v += step
    return None


def fmax_at(v, corner, temp_c, dvth_die=0.0):
    if v < sram_vmin(corner, temp_c, dvth_die):
        return 0.0
    d = path_delay_ns(v, corner, temp_c, dvth_die) + SETUP_NS
    return 1.0 / d


# ------------------------------------------------------- automation strategies
def edge_search_counts(corner, temp_c, rng, direction):
    """Per frequency column, sweep V and stop at the first change.
    direction='down': start at top V, stop at first fail  -> reports Vmin
    direction='up'  : start at low V, stop at first pass  -> reports Vmin
    Returns (tests_run, {f: reported_vmin})."""
    tests, result = 0, {}
    vs = V_GRID[::-1] if direction == "down" else V_GRID
    for f in F_GRID:
        reported, last_pass = None, None
        for v in vs:
            npass = sum(run_test(v, f, corner, temp_c, rng) for _ in range(TRIALS))
            tests += TRIALS
            ok = npass == TRIALS
            if direction == "down":
                if ok:
                    last_pass = v
                else:
                    break
            else:
                if ok:
                    reported = v
                    break
        result[f] = last_pass if direction == "down" else reported
    return tests, result


def rerun_points(points, corner, temp_c, rng, n=20):
    out = []
    for v, f in points:
        fails = sum(not run_test(v, f, corner, temp_c, rng) for _ in range(n))
        out.append((v, f, fails, n))
    return out


# ------------------------------------------------------------------- report
def main(argv):
    rng = random.Random(12)     # fixed seed -> repeatable output
    png = None
    if "--png" in argv:
        png = argv[argv.index("--png") + 1]

    print("=" * 78)
    print("BV12 modeled shmoo  (MODEL ONLY - not silicon data)")
    print("nominal point: Vcore=%.3f V, f=%.2f GHz, corner=TT, T=25 C, %d trials/pt"
          % (V_NOM, F_NOM, TRIALS))
    print("=" * 78)

    grid = shmoo_grid("TT", 25.0, rng)
    print_shmoo(grid, "\n[1] Shmoo: TT corner, 25 C")

    vmin, fmax = margins_from_grid(grid)
    print("\n[2] Margin at nominal (grid resolution 25 mV x 0.1 GHz)")
    if vmin is None:
        print("  nominal point FAILS -> no margin")
    else:
        vm = (V_NOM - vmin) / V_NOM
        fm = (fmax - F_NOM) / F_NOM
        print("  Vmin @ %.2f GHz  = %.3f V  -> voltage margin %.1f %% (spec >= %.0f %%) %s"
              % (F_NOM, vmin, 100 * vm, 100 * SPEC_V_MARGIN,
                 "PASS" if vm >= SPEC_V_MARGIN else "FAIL"))
        print("  Fmax @ %.3f V    = %.1f GHz -> frequency margin %.1f %% (spec >= %.0f %%) %s"
              % (V_NOM, fmax, 100 * fm, 100 * SPEC_F_MARGIN,
                 "PASS" if fm >= SPEC_F_MARGIN else "FAIL"))
    print("  fine (noiseless model, 1 mV / continuous): Vmin@1.5GHz = %.3f V,"
          " Fmax@0.75V = %.3f GHz" % (vmin_at(F_NOM, "TT", 25.0),
                                      fmax_at(V_NOM, "TT", 25.0)))

    holes = find_holes(grid)
    print("\n[3] Suspicious cells (fail/mixed with solid pass on both sides)")
    for v, f, npass in holes:
        print("  V=%.3f f=%.1f  passes %d/%d" % (v, f, npass, TRIALS))
    print("  -> rerun each suspicious cell 20x to separate repeatable vs intermittent:")
    for v, f, fails, n in rerun_points([(v, f) for v, f, _ in holes], "TT", 25.0, rng):
        kind = "REPEATABLE -> real conditional failure, debug it" if fails == n else \
               "intermittent -> noise/rare bug, track fail rate" if fails else \
               "did not reproduce"
        print("  V=%.3f f=%.1f  fails %2d/%d  %s" % (v, f, fails, n, kind))

    print("\n[4] Automation strategy vs. the hole (column f=1.3 GHz)")
    full_tests = len(V_GRID) * len(F_GRID) * TRIALS
    t_down, r_down = edge_search_counts("TT", 25.0, rng, "down")
    t_up, r_up = edge_search_counts("TT", 25.0, rng, "up")
    fine_13 = vmin_at(1.3, "TT", 25.0)
    print("  full grid      : %4d test runs, sees the hole at 0.725-0.750 V" % full_tests)
    print("  edge search top-down (stop at first fail): %4d runs, reports Vmin@1.3 = %s V"
          % (t_down, r_down[1.3]))
    print("  edge search bottom-up (stop at first pass): %4d runs, reports Vmin@1.3 = %s V"
          % (t_up, r_up[1.3]))
    print("  true boundary (noiseless model, no hole): Vmin@1.3 = %.3f V" % fine_13)
    print("  -> top-down mistakes the hole for the edge; bottom-up never visits it.")

    print("\n[5] PVT corner table (noiseless model): Vmin@%.2fGHz / Fmax@%.3fV"
          % (F_NOM, V_NOM))
    temps = [-40.0, 25.0, 125.0]
    print("  corner |" + "".join("   T=%5.0f C        " % t for t in temps))
    worst_v, worst_f = (0, None), (99, None)
    for c in ("SS", "TT", "FF"):
        line = "    %s   |" % c
        for t in temps:
            vm, fm = vmin_at(F_NOM, c, t), fmax_at(V_NOM, c, t)
            line += "  %.3f V / %.2f GHz " % (vm, fm)
            if vm > worst_v[0]:
                worst_v = (vm, (c, t))
            if fm < worst_f[0]:
                worst_f = (fm, (c, t))
        print(line)
    print("  worst Vmin : %.3f V at %s %+.0f C" % (worst_v[0], worst_v[1][0], worst_v[1][1]))
    print("  worst Fmax : %.2f GHz at %s %+.0f C" % (worst_f[0], worst_f[1][0], worst_f[1][1]))
    print("  temperature inversion check (TT, low V = 0.65 V):  "
          "Fmax(-40C)=%.3f GHz  Fmax(125C)=%.3f GHz"
          % (fmax_at(0.65, "TT", -40.0), fmax_at(0.65, "TT", 125.0)))
    print("  high V = 0.85 V:  Fmax(-40C)=%.3f GHz  Fmax(125C)=%.3f GHz"
          % (fmax_at(0.85, "TT", -40.0), fmax_at(0.85, "TT", 125.0)))

    print("\n[6] Die-to-die statistics: 40 dies per corner, die Vth sigma = 12 mV")
    print("  metric = Vmin @ %.2f GHz at the die's worst temperature (-40/25/125 C)"
          % F_NOM)
    usl = round(V_NOM * (1 - SPEC_V_MARGIN), 4)   # 0.7125 V: keep 5 % below nominal
    print("  spec: Vmin <= %.4f V (one-sided, USL)  ->  Cpk = (USL - mean) / (3 sigma)" % usl)
    srng = random.Random(7)
    for c in ("SS", "TT", "FF"):
        vals = []
        for _ in range(40):
            dv = srng.gauss(0.0, 0.012)
            vals.append(max(vmin_at(F_NOM, c, t, dv) for t in temps))
        mu, sd = statistics.mean(vals), statistics.stdev(vals)
        cpk = (usl - mu) / (3 * sd)
        print("  %s: mean %.3f V  sigma %.4f V  min %.3f  max %.3f  Cpk %.2f %s"
              % (c, mu, sd, min(vals), max(vals), cpk,
                 "(>=1.33 ok)" if cpk >= 1.33 else "(<1.33 -> guardband / screen needed)"))

    if png:
        save_png(grid, png)


def save_png(grid, path):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        from matplotlib.colors import ListedColormap
    except ImportError:
        print("\n[png] matplotlib not available - skipped")
        return
    # 0 = fail all, 1 = mixed, 2 = pass all ; status colors + letters (not color alone)
    cat = [[0 if n == 0 else (2 if n == TRIALS else 1) for n in row] for row in grid]
    cmap = ListedColormap(["#d03b3b", "#fab219", "#0ca30c"])
    fig, ax = plt.subplots(figsize=(8.5, 5.2), dpi=120)
    ax.imshow(cat, origin="lower", cmap=cmap, vmin=0, vmax=2, aspect="auto",
              extent=(F_GRID[0] - 0.05, F_GRID[-1] + 0.05,
                      V_GRID[0] - 0.0125, V_GRID[-1] + 0.0125))
    for i, v in enumerate(V_GRID):
        for j, f in enumerate(F_GRID):
            ax.text(f, v, {0: "F", 1: "~", 2: "P"}[cat[i][j]],
                    ha="center", va="center", fontsize=6,
                    color="#0b0b0b" if cat[i][j] == 1 else "#ffffff")
    ax.plot([F_NOM], [V_NOM], marker="s", markersize=11, markerfacecolor="none",
            markeredgecolor="#0b0b0b", markeredgewidth=2)
    ax.annotate("nominal", (F_NOM, V_NOM), xytext=(F_NOM + 0.12, V_NOM + 0.03),
                fontsize=8, color="#0b0b0b")
    ax.set_xlabel("core frequency (GHz)")
    ax.set_ylabel("Vcore (V)")
    ax.set_title("Modeled shmoo, TT 25 C (P = pass all, ~ = mixed, F = fail)  - MODEL",
                 fontsize=9)
    fig.tight_layout()
    fig.savefig(path)
    print("\n[png] saved %s" % path)


if __name__ == "__main__":
    main(sys.argv[1:])
