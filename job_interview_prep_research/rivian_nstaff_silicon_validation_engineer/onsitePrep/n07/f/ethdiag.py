#!/usr/bin/env python3
"""N07F - ethtool -S snapshot diff + layer classifier + ranked hypotheses.

Usage:
  python3 -I ethdiag.py BEFORE.txt AFTER.txt          # diagnose one pair
  python3 -I ethdiag.py --selftest CASES_DIR          # run every case, PASS/FAIL

Snapshot format = plain `ethtool -S <dev>` text ("name: value" lines), plus
optional metadata comment lines the test harness adds:
  # meta: speed=1000 link=up sqi=7 sqi_max=7 interval_s=60 carrier_down=0

Counter names differ per driver. ALIASES maps the common spellings (rtnl
names from `ip -s -s link`, IEEE/ethtool --groups names, typical driver-style
names) onto one canonical key. Unknown names are still diffed and shown as
'unclassified' so nothing is silently ignored. Stdlib only.
"""
import os
import re
import sys

WRAP32 = 1 << 32

# canonical key -> (layer, regexes matched against the lower-cased name)
ALIASES = [
    # --- PHY / PCS (below the MAC) ---
    ("phy_symbol_err", "PHY/PCS", [r"symbolerrorduringcarrier", r"symbol_err", r"phy_.*err", r"rx_symbol"]),
    ("fec_uncorr",     "PHY/PCS", [r"fec.*uncorr", r"rs_fec_uncorr"]),   # must precede fec_corrected
    ("fec_corrected",  "PHY/PCS", [r"fec.*corr", r"rs_fec_corr"]),
    ("pcs_err",        "PHY/PCS", [r"pcs_.*err", r"block_lock_lost", r"remote_fault", r"local_fault"]),
    # --- MAC (frame checks) ---
    ("rx_fcs_err",     "MAC", [r"framechecksequenceerrors", r"rx_crc_err", r"rx_fcs_err", r"crc_err"]),
    ("rx_align_err",   "MAC", [r"alignmenterrors", r"rx_frame_err", r"align"]),
    ("rx_oversize",    "MAC", [r"frametoolongerrors", r"etherstatsoversizepkts", r"oversize", r"rx_long", r"giant", r"jabber"]),
    ("rx_undersize",   "MAC", [r"etherstatsundersizepkts", r"undersize", r"runt", r"fragments"]),
    ("rx_length_err",  "MAC", [r"rx_length_err", r"inrangelengtherrors", r"outofrangelengthfield"]),
    ("pause_rx",       "MAC", [r"pause.*rx", r"rx_pause", r"maccontrolframesreceived"]),
    # --- DMA / driver (host could not take the frame) ---
    ("rx_missed",      "DMA/driver", [r"rx_missed", r"rx_no_buf", r"no_desc", r"rx_fifo_err", r"rx_over_err", r"rx_overflow", r"framelostduetointmacrcverror"]),
    ("tx_underrun",    "DMA/driver", [r"tx_fifo_err", r"tx_underrun", r"framelostduetointmacxmiterror"]),
    ("tx_timeout",     "DMA/driver", [r"tx_timeout"]),
    # --- queue / stack (frame was received, dropped in SW) ---
    ("rx_sw_drop",     "queue/stack", [r"rx_dropped", r"rx_drops", r"rx_queue_\d+_drops", r"rx_.*_drop"]),
    # --- good traffic (denominators) ---
    ("rx_frames",      "good", [r"framesreceivedok", r"^rx_packets$", r"^rx_frames$", r"rx_pkts"]),
    ("tx_frames",      "good", [r"framestransmittedok", r"^tx_packets$", r"^tx_frames$", r"tx_pkts"]),
    ("rx_bytes",       "good", [r"octetsreceivedok", r"^rx_bytes$", r"rx_octets"]),
]
COMPILED = [(k, layer, [re.compile(p) for p in pats]) for k, layer, pats in ALIASES]


def canon(name):
    n = name.lower()
    for key, layer, pats in COMPILED:
        if any(p.search(n) for p in pats):
            return key, layer
    return None, "unclassified"


def parse(path):
    stats, meta = {}, {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            s = line.strip()
            if s.startswith("# meta:"):
                for tok in s[len("# meta:"):].split():
                    k, _, v = tok.partition("=")
                    meta[k] = v
                continue
            if not s or s.startswith("#") or s.endswith("statistics:"):
                continue
            m = re.match(r"^([^:]+):\s*(-?\d+)$", s)
            if m:
                stats[m.group(1).strip()] = int(m.group(2))
    return stats, meta


def diff(before, after):
    """Return {name: (delta, note)}; handles 32-bit wrap and counter reset."""
    out = {}
    for name in sorted(set(before) | set(after)):
        b, a = before.get(name, 0), after.get(name, 0)
        note = ""
        if a >= b:
            d = a - b
        elif b < WRAP32 and b > (WRAP32 >> 1):   # was near 2^32 -> wrapped
            d, note = a + WRAP32 - b, "32-bit wrap corrected"
        else:
            d, note = a, "counter went DOWN (reset/driver reload?) -> using after value"
        if d or note:
            out[name] = (d, note)
    return out


def fnum(meta, key, default=None):
    try:
        return float(meta[key])
    except (KeyError, ValueError):
        return default


def diagnose(before_path, after_path):
    b, mb = parse(before_path)
    a, ma = parse(after_path)
    deltas = diff(b, a)
    # Several names can count the SAME event (driver name + eth-mac + rmon
    # group). Per canonical key: per-queue counters are summed, then MAX is
    # taken across the remaining aliases (never sum aliases -> double count).
    per_key, rows = {}, []
    for name, (d, note) in deltas.items():
        key, layer = canon(name)
        rows.append((layer, name, d, key or "-", note))
        if key:
            slot = "queues" if re.search(r"queue_?\d+", name.lower()) else name
            per_key.setdefault(key, {}).setdefault(slot, 0)
            per_key[key][slot] += d
    agg = {k: max(v.values()) for k, v in per_key.items()}
    g = lambda k: agg.get(k, 0)
    rx = max(g("rx_frames"), 1)
    speed = fnum(ma, "speed")
    sqi0, sqi1 = fnum(mb, "sqi"), fnum(ma, "sqi")
    sqi_drop = (sqi0 - sqi1) if (sqi0 is not None and sqi1 is not None) else 0
    flaps = (fnum(ma, "carrier_down", 0) or 0) - (fnum(mb, "carrier_down", 0) or 0)
    fcs_rate = g("rx_fcs_err") / (rx + g("rx_fcs_err"))
    phy_bad = g("phy_symbol_err") + g("fec_uncorr") + g("pcs_err")
    hyps = []

    def add(hid, score, text, evidence, nxt):
        if score > 0:
            hyps.append((score, hid, text, evidence, nxt))

    # H1: MAC<->PHY parallel interface timing (RGMII delay): many FCS errors,
    # but the PHY line side is clean (no symbol errors, SQI fine), worse at 1G.
    s = 0
    if fcs_rate > 1e-3:
        s += 3
        if phy_bad == 0 and sqi_drop <= 0:
            s += 3
        if speed == 1000:
            s += 2
    add("H_RGMII", s, "MAC<->PHY interface timing (RGMII clock/data delay doubled or missing)",
        f"FCS rate {fcs_rate:.2%}, PHY symbol/PCS errors {phy_bad}, SQI drop {sqi_drop:g}, speed {speed:g}",
        "phy-mode vs PHY delay strap/regs vs MAC delay; PHY xMII loopback; scope RXC vs RXD at receiver")
    # H2: PHY / channel margin: line-side errors, SQI drop, FEC working hard, flaps.
    s = 0
    if g("phy_symbol_err") or g("pcs_err"):
        s += 3
    if g("fec_corrected") > 1000:
        s += 2
    if g("fec_uncorr"):
        s += 2
    if sqi_drop >= 2:
        s += 3
    if flaps > 0:
        s += 2
    add("H_CHANNEL", s, "PHY/channel margin (temperature, EMI, cable/connector, PHY supply)",
        f"symbol {g('phy_symbol_err')}, FEC corr {g('fec_corrected')} uncorr {g('fec_uncorr')}, "
        f"SQI {sqi0:g}->{sqi1:g}, carrier_down +{flaps:g}" if sqi0 is not None else
        f"symbol {g('phy_symbol_err')}, FEC corr {g('fec_corrected')}, carrier_down +{flaps:g}",
        "log SQI/FEC vs temperature; cable test (TDR); PRBS BER at the corner; CMC/termination")
    # H3: host RX path cannot drain: HW/driver drops without frame errors.
    s = 0
    if g("rx_missed"):
        s += 4
        if g("rx_fcs_err") == 0:
            s += 2
    if g("rx_sw_drop"):
        s += 1
    add("H_HOST_RX", s, "host RX path too slow (ring size, IRQ coalescing/NAPI budget, DDR/QoS)",
        f"rx_missed/fifo {g('rx_missed')} ({g('rx_missed') / rx:.3%} of rx), sw drops {g('rx_sw_drop')}, FCS {g('rx_fcs_err')}",
        "ethtool -g/-G ring, -c/-C coalesce, /proc/net/softnet_stat, IRQ affinity, DDR bandwidth")
    # H4: frame-size policy: oversize/length errors.
    s = 0
    if g("rx_oversize") or g("rx_length_err"):
        s += 5
        if g("rx_fcs_err") == 0:
            s += 1
    add("H_FRAMESIZE", s, "max frame length / MTU / VLAN +4 B mismatch (MAC max-length, RX buffer size)",
        f"oversize {g('rx_oversize')}, length errors {g('rx_length_err')}, FCS {g('rx_fcs_err')}",
        "ping -M do -s 1472/1473 (+VLAN); MAC max frame length register; RX buffer size; peer MTU")
    # H5: TX path.
    s = 3 * bool(g("tx_timeout")) + 3 * bool(g("tx_underrun"))
    add("H_TX", s, "TX DMA/descriptor path (underrun, stuck ring -> watchdog)",
        f"tx_timeout {g('tx_timeout')}, tx_underrun {g('tx_underrun')}",
        "TX ring head/tail regs, OWN bit + barrier, DMA bus errors")
    if not hyps:
        hyps.append((1, "H_ABOVE_L2", "no L1/L2 error counter moved -> look above the MAC "
                     "(ARP/IP config, VLAN filter, firewall, wrong interface)",
                     f"rx_frames +{g('rx_frames')}, tx_frames +{g('tx_frames')}",
                     "tcpdump -e on both ends; ip neigh; VLAN/filter config"))
    hyps.sort(key=lambda h: (-h[0], h[1]))
    return rows, hyps, agg, ma


def report(before_path, after_path, out=print):
    rows, hyps, agg, meta = diagnose(before_path, after_path)
    out(f"  meta(after): {' '.join(f'{k}={v}' for k, v in sorted(meta.items()))}")
    out("  nonzero deltas:")
    order = {"PHY/PCS": 0, "MAC": 1, "DMA/driver": 2, "queue/stack": 3, "good": 4, "unclassified": 5}
    for layer, name, d, key, note in sorted(rows, key=lambda r: (order[r[0]], r[1])):
        out(f"    {layer:<12} {name:<28} +{d:<12} {key}{'  [' + note + ']' if note else ''}")
    out("  ranked hypotheses:")
    for i, (score, hid, text, ev, nxt) in enumerate(hyps, 1):
        out(f"    {i}. [{score:>2}] {hid:<11} {text}")
        out(f"         evidence: {ev}")
        out(f"         next:     {nxt}")
    return hyps[0][1]


def selftest(cases_dir):
    names = sorted(d for d in os.listdir(cases_dir) if os.path.isdir(os.path.join(cases_dir, d)))
    fails = 0
    for n in names:
        d = os.path.join(cases_dir, n)
        with open(os.path.join(d, "expected.txt"), encoding="utf-8") as f:
            expected = f.read().split()[0]
        print(f"== {n} ==")
        top = report(os.path.join(d, "before.txt"), os.path.join(d, "after.txt"))
        ok = top == expected
        fails += not ok
        print(f"  {'PASS' if ok else 'FAIL'} {n}: top={top} expected={expected}\n")
    print(f"SUMMARY: {len(names) - fails}/{len(names)} PASS")
    return 1 if fails else 0


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--selftest":
        sys.exit(selftest(sys.argv[2]))
    if len(sys.argv) == 3:
        report(sys.argv[1], sys.argv[2])
        sys.exit(0)
    print(__doc__)
    sys.exit(2)
