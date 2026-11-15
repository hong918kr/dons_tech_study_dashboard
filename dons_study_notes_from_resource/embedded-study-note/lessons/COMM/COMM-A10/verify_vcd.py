#!/usr/bin/env python3
"""COMM-A10 독립 검증: solution.c 와 코드를 공유하지 않는 파이썬(표준 라이브러리만) 디코더.

같은 VCD 를 읽어 C CLI 와 같은 형식의 protocol table 줄을 찍는다 → diff 로 비교.
auto-baud 는 일부러 다른 방법(1비트 후보 = 최소 폭, low 펄스의 w/k 중앙값)을 쓴다.

  python3 -I verify_vcd.py demo_capture.vcd uart TX 100        # 채널, 글리치 필터(ns)
  python3 -I verify_vcd.py demo_capture.vcd i2c SDA SCL
  python3 -I verify_vcd.py hwlab_b07_evt1_spi_100M.vcd spi CS_N SCLK MOSI MISO 0
"""
import bisect
import sys

UNITS = {"s": 10**12, "ms": 10**9, "us": 10**6, "ns": 10**3, "ps": 1}


def load_vcd(path):
    toks = open(path).read().split()
    i, unit, ids, chans, now, t0 = 0, None, {}, {}, None, None
    while toks[i] != "$enddefinitions":
        if toks[i] == "$timescale":
            j = toks.index("$end", i)
            ts = "".join(toks[i + 1:j])
            num = ts.rstrip("smunp")
            unit = int(num) * UNITS[ts[len(num):]]
            i = j
        elif toks[i] == "$var":
            _, _, width, ident, name = toks[i:i + 5]
            assert width == "1"
            ids[ident] = name
            chans[name] = [None, []]          # [init, toggles]
            i = toks.index("$end", i)
        i += 1
    i = toks.index("$end", i) + 1
    for tk in toks[i:]:
        if tk[0] == "#":
            now = int(tk[1:]) * unit
            t0 = now if t0 is None else t0
        elif tk[0] in "01":
            ch = chans[ids[tk[1:]]]
            v = int(tk[0])
            if now == t0:
                ch[0] = v
            elif v != ch[0] ^ (len(ch[1]) & 1):
                ch[1].append(now)
    return chans, now


def deglitch(ts, min_ps):
    out = []
    for t in ts:
        if out and t - out[-1] < min_ps:
            out.pop()
        else:
            out.append(t)
    return out


def level(ch, t):
    return ch[0] ^ (bisect.bisect_right(ch[1], t) & 1)


def us(t_ps):
    ns = t_ps // 1000
    return "%8d.%03d us" % (ns // 1000, ns % 1000)


def uart(chans, t_end, name, glitch_ns):
    ch = chans[name]
    ch[1] = deglitch(ch[1], glitch_ns * 1000)
    ts = ch[1]
    widths = [(ts[i + 1] - ts[i], ch[0] ^ ((i + 1) & 1)) for i in range(len(ts) - 1)]
    t1 = min(w for w, _ in widths)
    est = sorted(w / round(w / t1) for w, lv in widths if lv == 0 and 1 <= round(w / t1) <= 10)
    T = est[len(est) // 2]
    print("# python auto-baud: %.0f baud (median of %d low pulses)" % (1e12 / T, len(est)))
    rows, i = [], 0
    while i < len(ts):
        if ch[0] ^ ((i + 1) & 1):
            i += 1
            continue
        t0 = ts[i]
        if t0 + 9.5 * T > t_end:
            rows.append((t0, "TRUNC", "capture ends inside a frame (0 bits pending)"))
            break
        if level(ch, t0 + T / 2):
            i += 1
            continue
        v = sum(level(ch, t0 + (b + 1.5) * T) << b for b in range(8))
        stop = level(ch, t0 + 9.5 * T)
        c = chr(v) if 0x20 <= v < 0x7F else "."
        rows.append((t0, "UART", "0x%02X '%s'%s" % (v, c, "" if stop else " FERR")))
        i = bisect.bisect_right(ts, t0 + 9.5 * T)
    return rows


def i2c(chans, sda_n, scl_n):
    sda, scl = chans[sda_n], chans[scl_n]
    ev = [(t, 0, "scl") for t in scl[1]] + [(t, 1, "sda") for t in sda[1]]
    rows, d, c = [], sda[0], scl[0]
    in_tx, bits, byte, idx, tb, hi = False, 0, 0, 0, 0, False
    # 같은 시각: SCL 하강 → SDA → SCL 상승. 정렬 키로 표현
    def key(e):
        t, _, who = e
        if who == "sda":
            return (t, 1)
        rising = level(scl, t) == 1
        return (t, 2 if rising else 0)
    for t, _, who in sorted(ev, key=key):
        if who == "scl":
            c ^= 1
            hi = False
            if c and in_tx:
                hi = True
                if bits < 8:
                    tb = t if bits == 0 else tb
                    byte, bits = byte << 1 | d, bits + 1
                else:
                    ack = "NACK" if d else "ACK"
                    if idx == 0:
                        rows.append((tb, "ADDR", "0x%02X %s %s" % (byte >> 1, "R" if byte & 1 else "W", ack)))
                    else:
                        rows.append((tb, "DATA", "0x%02X %s" % (byte, ack)))
                    idx, bits, byte = idx + 1, 0, 0
        else:
            d ^= 1
            if c:
                eb = bits - (1 if hi else 0)
                if in_tx and eb > 0:
                    rows.append((t, "ERR", "SDA changed while SCL high after %d bits" % eb))
                if d == 0:
                    rows.append((t, "Sr" if in_tx else "START", ""))
                    in_tx = True
                elif in_tx:
                    rows.append((t, "STOP", ""))
                    in_tx = False
                bits, idx, byte, hi = 0, 0, 0, False
    return rows


def spi(chans, t_end, cs_n, sck_n, mosi_n, miso_n, mode):
    cs, sck, mo, mi = chans[cs_n], chans[sck_n], chans[mosi_n], chans[miso_n]
    cpol, cpha = mode >> 1, mode & 1
    rows = []
    for k, ta in enumerate(cs[1]):
        if cs[0] ^ ((k + 1) & 1):
            continue
        tb = cs[1][k + 1] if k + 1 < len(cs[1]) else t_end + 1
        edges = [t for t in sck[1] if ta < t < tb and level(sck, t) == (1 if cpol == cpha else 0)]
        for w in range(len(edges) // 8):
            e8 = edges[8 * w:8 * w + 8]
            vo = vi = 0
            for t in e8:
                vo, vi = vo << 1 | level(mo, t), vi << 1 | level(mi, t)
            rows.append((e8[0], "SPI", "MOSI 0x%02X MISO 0x%02X" % (vo, vi)))
    return rows


def main():
    path, proto = sys.argv[1], sys.argv[2]
    chans, t_end = load_vcd(path)
    if proto == "uart":
        rows = uart(chans, t_end, sys.argv[3], int(sys.argv[4]))
    elif proto == "i2c":
        rows = i2c(chans, sys.argv[3], sys.argv[4])
    else:
        rows = spi(chans, t_end, *sys.argv[3:7], int(sys.argv[7]))
    for i, (t, typ, data) in enumerate(rows):
        print("%4d %s  %-8s %s" % (i, us(t), typ, data))


if __name__ == "__main__":
    main()
