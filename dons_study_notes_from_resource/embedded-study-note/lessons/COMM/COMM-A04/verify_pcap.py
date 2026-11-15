#!/usr/bin/env python3
"""COMM-A04 독립 검증기 (stdlib 만: struct, sys, hashlib).

C 코드와 별개로 다시 짠 구현이다.
  python3 -I verify_pcap.py build out.pcap   시험 벡터 17개를 직접 만들어 pcap 으로 저장 (C 의 `./solution vectors` 와 바이트 비교)
  python3 -I verify_pcap.py dump file.pcap   C 의 `./solution dump` 와 같은 한 줄 형식으로 해독 (diff 로 비교)
"""
import struct
import sys
import hashlib


# ---------- 인터넷 체크섬 (RFC 1071) ----------
def ones_sum(data, start=0):
    """16비트 big-endian 워드의 1의 보수 합 (접힌 값). 홀수 길이는 0 을 덧댄다."""
    if len(data) % 2:
        data = data + b"\x00"
    s = start
    for (w,) in struct.iter_unpack("!H", data):
        s += w
    while s >> 16:
        s = (s & 0xFFFF) + (s >> 16)
    return s


def csum(data, start=0):
    return (~ones_sum(data, start)) & 0xFFFF


def pseudo(src, dst, ulen):
    return struct.pack("!IIBBH", src, dst, 0, 17, ulen)


# ---------- 프레임 만들기 ----------
MAC_A = bytes([2, 0, 0, 0, 0, 1])
MAC_B = bytes([2, 0, 0, 0, 0, 2])
IP_A = 0xC0A8010A  # 192.168.1.10
IP_B = 0xC0A80114  # 192.168.1.20


def ipv4(payload, *, ident, frag=0x4000, ttl=64, proto=17, src=IP_A, dst=IP_B, opts=b"", ihl=None, tot=None, hcs_xor=0):
    hl = 20 + len(opts)
    ihl = hl // 4 if ihl is None else ihl
    tot = hl + len(payload) if tot is None else tot
    h = struct.pack("!BBHHHBBHII", 0x40 | ihl, 0, tot, ident, frag, ttl, proto, 0, src, dst) + opts
    c = csum(h) ^ hcs_xor
    return h[:10] + struct.pack("!H", c) + h[12:] + payload


def udp(payload, *, sport=5000, dport=6000, src=IP_A, dst=IP_B, mode="auto", ulen=None, xor=0):
    real = 8 + len(payload)
    hdr = struct.pack("!HHHH", sport, dport, real if ulen is None else ulen, 0)
    if mode == "auto":
        c = csum(pseudo(src, dst, real) + hdr + payload)
        if c == 0:
            c = 0xFFFF
    else:
        c = 0
    c ^= xor
    return hdr[:6] + struct.pack("!H", c) + payload


def eth(l3, ethertype=0x0800, tags=(), pad_to=60):
    f = MAC_B + MAC_A
    for tpid, tci in tags:
        f += struct.pack("!HH", tpid, tci)
    f += struct.pack("!H", ethertype) + l3
    if len(f) < pad_to:
        f += bytes(pad_to - len(f))
    return f


def vectors():
    """C 의 make_vectors() 와 같은 정의를 따로 구현. (이름, frame, caplen)"""
    v = []
    ident = lambda i: 0x1C40 + i
    v.append(("basic_odd5", eth(ipv4(udp(b"hello"), ident=ident(0)))))
    v.append(("vlan100", eth(ipv4(udp(b"vlan-tagged!"), ident=ident(1)), tags=[(0x8100, (5 << 13) | 100)])))
    v.append(("qinq", eth(ipv4(udp(b"qinq"), ident=ident(2)), tags=[(0x88A8, 200), (0x8100, (3 << 13) | (1 << 12) | 100)])))
    v.append(("ip_options", eth(ipv4(udp(b"opt"), ident=ident(3), opts=bytes([0x94, 0x04, 0, 0])))))
    dgram = udp(bytes((k * 7 + 3) & 0xFF for k in range(1600)))          # 1608 B 데이터그램
    v.append(("frag1_mf", eth(ipv4(dgram[:1480], ident=0x2222, frag=0x2000))))
    v.append(("frag2_last", eth(ipv4(dgram[1480:], ident=0x2222, frag=185))))
    v.append(("ucs_zero", eth(ipv4(udp(b"nocsum", mode="zero"), ident=ident(6)))))
    # 체크섬 계산 결과가 0 이 되게: 조정 워드 = (조정 0 일 때의 체크섬)
    adj = csum(pseudo(IP_A, IP_B, 12) + struct.pack("!HHHH", 5000, 6000, 12, 0) + b"zz\x00\x00")
    v.append(("ucs_ffff", eth(ipv4(udp(b"zz" + struct.pack("!H", adj)), ident=ident(7)))))
    v.append(("bad_ip_csum", eth(ipv4(udp(b"hello"), ident=ident(8), hcs_xor=0x0100))))
    v.append(("bad_udp_csum", eth(ipv4(udp(b"hello", xor=1), ident=ident(9)))))
    arp = struct.pack("!HHBBH", 1, 0x0800, 6, 4, 1) + MAC_A + struct.pack("!I", IP_A) + bytes(6) + struct.pack("!I", IP_B)
    v.append(("arp", eth(arp, ethertype=0x0806)))
    icmp = bytearray([8, 0, 0, 0, 0x12, 0x34, 0, 1])
    icmp[2:4] = struct.pack("!H", csum(bytes(icmp)))
    v.append(("icmp", eth(ipv4(bytes(icmp), ident=ident(11), proto=1))))
    v.append(("ihl4", eth(ipv4(udp(b"hello"), ident=ident(12), ihl=4))))
    v.append(("tot_gt_frame", eth(ipv4(udp(b"hello"), ident=ident(13), tot=200))))
    v.append(("snap_trunc", eth(ipv4(udp(bytes(0xA0 + k for k in range(64))), ident=ident(14)))))
    v.append(("ulen_gt_ip", eth(ipv4(udp(b"hello", ulen=100), ident=ident(15)))))
    v.append(("empty_payload", eth(ipv4(udp(b""), ident=ident(16)))))
    out = []
    for name, f in v:
        cap = 54 if name == "snap_trunc" else len(f)
        out.append((name, f, cap))
    return out


def build(path):
    data = struct.pack("<IHHiIII", 0xA1B2C3D4, 2, 4, 0, 0, 65535, 1)
    for i, (name, f, cap) in enumerate(vectors()):
        data += struct.pack("<IIII", 1700000000 + i, 1000 * i + 7, cap, len(f)) + f[:cap]
    with open(path, "wb") as fh:
        fh.write(data)
    print("wrote %d bytes, %d records, sha256 %s" % (len(data), len(vectors()), hashlib.sha256(data).hexdigest()[:16]))


# ---------- 해독 ----------
CS = ["na", "ok", "BAD", "none", "partial", "frag", "trunc"]


def decode(frame, cap, wire):
    """C 의 pkt_parse + fmt_pkt 의 꼬리 부분을 독립 구현. 반환: (문자열 조각 리스트, err 이름)"""
    p = frame[:cap]
    out = []
    if wire < 14:
        return out, "eth_short"
    if cap < 14:
        return out, "trunc"
    out.append("eth %s>%s" % (":".join("%02x" % b for b in p[6:12]), ":".join("%02x" % b for b in p[0:6])))
    (et,) = struct.unpack_from("!H", p, 12)
    off, ntag = 14, 0
    while et in (0x8100, 0x88A8):
        if ntag == 2:
            return out, "vlan_depth"
        if wire < off + 4:
            return out, "eth_short"
        if cap < off + 4:
            return out, "trunc"
        (tci,) = struct.unpack_from("!H", p, off)
        out.append("vlan 0x%04x:p%d:d%d:v%d" % (et, tci >> 13, (tci >> 12) & 1, tci & 0xFFF))
        (et,) = struct.unpack_from("!H", p, off + 2)
        off += 4
        ntag += 1
    out.append("type 0x%04x" % et)
    if et != 0x0800:
        return out, None
    aw, ac = wire - off, cap - off
    if aw < 20:
        return out, "ip_short"
    if ac < 20:
        return out, "trunc"
    vihl, tos, tot, ident, fw, ttl, proto, hcs, src, dst = struct.unpack_from("!BBHHHBBHII", p, off)
    if vihl >> 4 != 4:
        return out, "ip_ver"
    ihl = vihl & 15
    if ihl < 5:
        return out, "ip_ihl"
    hl = ihl * 4
    if tot < hl or tot > aw:
        return out, "ip_totlen"
    if ac < hl:
        return out, "trunc"
    hdr = p[off:off + hl]
    ok = ones_sum(hdr) == 0xFFFF
    ip4 = lambda a: "%d.%d.%d.%d" % (a >> 24, (a >> 16) & 255, (a >> 8) & 255, a & 255)
    s = "ip %s>%s ihl %d tot %d id 0x%04x ttl %d p %d frag %s%s%d hcs 0x%04x %s" % (
        ip4(src), ip4(dst), ihl, tot, ident, ttl, proto, "DF" if fw & 0x4000 else "",
        "MF+" if fw & 0x2000 else "+", fw & 0x1FFF, hcs, "ok" if ok else "BAD")
    if not ok:
        s += " want 0x%04x" % csum(hdr[:10] + b"\0\0" + hdr[12:])
    if aw - tot:
        s += " pad %d" % (aw - tot)
    out.append(s)
    if proto != 17 or (fw & 0x1FFF):
        return out, None
    ipl, l4 = tot - hl, off + hl
    if ipl < 8:
        return out, "udp_short"
    if cap - l4 < 8:
        return out, "trunc"
    sport, dport, ulen, ucs = struct.unpack_from("!HHHH", p, l4)
    mf = bool(fw & 0x2000)
    head = "udp %d>%d ulen %d cs 0x%04x" % (sport, dport, ulen, ucs)
    if ulen < 8 or (not mf and ulen > ipl):
        out.append(head + " na")
        return out, "udp_len"
    pay = (ipl if mf else ulen) - 8
    if mf:
        st, want = ("frag" if ucs else "none"), None
    elif cap - l4 < ulen:
        st, want = ("trunc" if ucs else "none"), None
    else:
        seg = p[l4:l4 + ulen]
        ps = ones_sum(pseudo(src, dst, ulen))
        want = csum(pseudo(src, dst, ulen) + seg[:6] + b"\0\0" + seg[8:]) or 0xFFFF
        if ucs == 0:
            st = "none"
        elif ones_sum(pseudo(src, dst, ulen) + seg) == 0xFFFF:
            st = "ok"
        elif ucs == ps:
            st = "partial"
        else:
            st = "BAD"
    s = head + " " + st
    if st in ("BAD", "partial"):
        s += " want 0x%04x" % want
    out.append(s + " pay %d" % pay)
    return out, None


def dump(path):
    data = open(path, "rb").read()
    if len(data) < 24:
        print("pcap_open error -1")
        return 1
    magic = data[:4]
    table = {b"\xd4\xc3\xb2\xa1": ("<", False), b"\x4d\x3c\xb2\xa1": ("<", True),
             b"\xa1\xb2\xc3\xd4": (">", False), b"\xa1\xb2\x3c\x4d": (">", True)}
    if magic not in table:
        print("pcap_open error -2")
        return 1
    e, nano = table[magic]
    vmaj, vmin, _, _, snap, lt = struct.unpack_from(e + "HHiIII", data, 4)
    if (lt & 0xFFFF) != 1:
        print("pcap_open error -4")
        return 1
    off, n = 24, 0
    while off + 16 <= len(data):
        sec, frac, caplen, wire = struct.unpack_from(e + "IIII", data, off)
        if caplen > wire or caplen > len(data) - off - 16:
            break
        frame = data[off + 16: off + 16 + caplen]
        off += 16 + caplen
        n += 1
        parts, err = decode(frame, caplen, wire)
        line = ("#%d t=%d.%09d" if nano else "#%d t=%d.%06d") % (n, sec, frac) + " cap=%d len=%d" % (caplen, wire)
        if parts:
            line += " " + " ".join(parts)
        if err:
            line += " err " + err
        print(line)
    return 0


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "build":
        build(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == "dump":
        sys.exit(dump(sys.argv[2]))
    else:
        print(__doc__)
        sys.exit(2)
