#!/usr/bin/env python3
"""HWLAB-M06 추가 연습 — 한 번에 검증

  연습 1  i2cscan_lab: I2C 위에 쌓는 "캡처판 i2cdetect" — 주소별 ACK/NACK 집계
  연습 2  내 디코더 위에 쌓기: ws2812_lab 에 OUTPUT_PYTHON ('FRAME') 을 더한 하위 클래스 + 프레임 diff 스택 디코더
  연습 3  decodable(fs): 샘플링률별로 WS2812 비트 판정이 '모든 위상에서' 보장되는지 정수 산술로 예측 -> 스윕과 대조

  python3 -I practice.py     # "N/M passed", 실패 시 exit 1
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True
sys.path.insert(0, HERE)
import solution as S  # noqa: E402  (shim 설치 + 디코더 + 로더)
import srd_shim  # noqa: E402
import sigrokdecode as srd  # noqa: E402
import gen_capture as G  # noqa: E402


# ---------------------------------------------------------------- 연습 1
class I2cScanLab(srd.Decoder):
    api_version = 3
    id = 'i2cscan_lab'
    name = 'I2C scan (lab)'
    longname = 'I2C address ACK/NACK census'
    desc = 'Marks every address phase with ACK/NACK (capture-side i2cdetect).'
    license = 'gplv2+'
    inputs = ['i2c']
    outputs = []
    tags = ['Embedded/industrial']
    annotations = (('ack', 'Address ACK'), ('nack', 'Address NACK'))
    annotation_rows = (('scan', 'Scan', (0, 1)),)

    def __init__(self):
        self.pending = None

    def start(self):
        self.out_ann = self.register(srd.OUTPUT_ANN)

    def decode(self, ss, es, data):
        cmd, val = data
        if cmd in ('ADDRESS READ', 'ADDRESS WRITE'):
            self.pending = (ss, val, 'R' if cmd.endswith('READ') else 'W')
        elif cmd in ('ACK', 'NACK') and self.pending is not None:
            s0, a, rw = self.pending
            nack = cmd == 'NACK'
            self.put(s0, es, self.out_ann, [1 if nack else 0, ['0x%02X %s %s' % (a, rw, cmd), '%s %02X' % (cmd, a)]])
            self.pending = None
        elif cmd in ('DATA READ', 'DATA WRITE'):
            self.pending = None


def scan_census(sess):
    out = {}
    for a in sess.ann_of('i2cscan_lab'):
        cmd, addr = a.texts[-1].split()
        d = out.setdefault(int(addr, 16), {"ACK": 0, "NACK": 0})
        d[cmd] += 1
    return out


# ---------------------------------------------------------------- 연습 2
class Ws2812Py(S.Ws2812Lab):
    """ws2812_lab + OUTPUT_PYTHON: 프레임이 끝날 때 ['FRAME', ['#RRGGBB', ...]] 을 내보낸다."""
    id = 'ws2812_py'
    outputs = ['ws2812']

    def start(self):
        super().start()
        self.out_python = self.register(srd.OUTPUT_PYTHON)
        self.frame, self.frame_ss = [], None

    def put(self, ss, es, output_id, data):
        super().put(ss, es, output_id, data)
        if output_id == self.out_ann and data[0] == 2:          # rgb annotation 을 가로채 프레임에 모은다
            if not self.frame:
                self.frame_ss = ss
            self.frame.append(data[1][-1])

    def end_frame(self, fall):
        super().end_frame(fall)
        if self.frame:
            self.put(self.frame_ss, fall, self.out_python, ['FRAME', list(self.frame)])
        self.frame = []


class LedDiffLab(srd.Decoder):
    api_version = 3
    id = 'leddiff_lab'
    name = 'LED frame diff (lab)'
    longname = 'Which LEDs changed between consecutive WS2812 frames'
    desc = 'Stacked on ws2812 Python output.'
    license = 'gplv2+'
    inputs = ['ws2812']
    outputs = []
    tags = ['Display']
    annotations = (('diff', 'Changed LEDs'), ('warning', 'Warning'))
    annotation_rows = (('diffs', 'Diffs', (0,)), ('warnings', 'Warnings', (1,)))

    def __init__(self):
        self.prev = None

    def start(self):
        self.out_ann = self.register(srd.OUTPUT_ANN)

    def decode(self, ss, es, data):
        if data[0] != 'FRAME':
            return
        cur = data[1]
        if self.prev is not None:
            if len(cur) != len(self.prev):
                self.put(ss, es, self.out_ann, [1, ['frame length %d != previous %d (early latch?)'
                                                    % (len(cur), len(self.prev)), 'LEN %d' % len(cur)]])
            else:
                ch = [i + 1 for i, (a, b) in enumerate(zip(self.prev, cur)) if a != b]
                self.put(ss, es, self.out_ann, [0, ['changed LEDs %s' % ch, 'CHG %d' % len(ch)]])
        self.prev = cur


# ---------------------------------------------------------------- 연습 3
def decodable(fs, w0_max_ns=437, w1_min_ns=813, split_ns=S.SPLIT_NS):
    """폭 w 인 펄스는 floor(w/Ts) 또는 ceil(w/Ts) 샘플로 잡힌다. 디코더는 n*Ts >= split 이면 1
    (= n >= m, m = ceil(split/Ts)). 모든 위상에서 맞으려면 ceil(w0_max/Ts) < m <= floor(w1_min/Ts).
    w0_max/w1_min = 공칭 416.7/833.3 ns +-20 ns (합성 지터 sigma 4 ns 의 5 sigma)."""
    def ceil_div(a, b):
        return -(-a // b)
    m = ceil_div(split_ns * fs, 10 ** 9)          # ceil(split / Ts) = ceil(split * fs / 1e9)
    n0_max = ceil_div(w0_max_ns * fs, 10 ** 9)
    n1_min = (w1_min_ns * fs) // 10 ** 9
    return n0_max < m <= n1_min


# ---------------------------------------------------------------- 하네스
g_pass = 0
g_total = 0


def check(name, fn):
    global g_pass, g_total
    g_total += 1
    try:
        ok = bool(fn())
        why = ""
    except Exception as e:  # noqa: BLE001
        ok, why = False, "  (%s: %s)" % (type(e).__name__, e)
    if ok:
        g_pass += 1
        print("PASS  %s" % name)
    else:
        print("FAIL  %s%s" % (name, why))


def run_tests():
    led = S.load_sr_raw(S.p("evt1_led_ws2812_16M.sr"))
    i2c = S.load_sr_raw(S.p("evt1_i2c_tmp117_2M.sr"))

    print("-- 연습 1: i2cscan_lab --")
    sc = srd_shim.run(S.I2cMini, i2c["data"], 1, i2c["fs"], {"scl": 0, "sda": 1}, None, [(I2cScanLab, None)])
    cen = scan_census(sc)
    print("      census: %s" % {"0x%02X" % k: v for k, v in sorted(cen.items())})
    check("metadata validates", lambda: srd_shim.validate_class(I2cScanLab))
    check("0x48: 8 ACK, 0 NACK (4 txns x W+R)", lambda: cen[0x48] == {"ACK": 8, "NACK": 0})
    check("0x49: 1 NACK (the planted CONFIG write)", lambda: cen[0x49] == {"ACK": 0, "NACK": 1})
    check("two decoders stacked side by side on the same i2c (tmp117 + scan) both run", lambda: _two_stacks(i2c))

    print("-- 연습 2: stack on your own decoder --")

    def frames(**opts):
        s = srd_shim.run(Ws2812Py, led["data"], 1, led["fs"], {"din": 0}, opts, [(LedDiffLab, None)])
        return s, [a.texts[-1] for a in s.ann_of('leddiff_lab')], [x.data for x in s.py]
    s1, d1, py1 = frames()
    print("      diff (reset 50 us): %s" % d1)
    check("Ws2812Py still emits the same annotations as ws2812_lab (subclass is transparent)", lambda: S.canon(
        s1, 'ws2812_py') == S.canon(srd_shim.run(S.Ws2812Lab, led["data"], 1, led["fs"], {"din": 0}), 'ws2812_lab'))
    check("3 FRAME python outputs, 8 colours each", lambda: [len(x[1]) for x in py1] == [8, 8, 8])
    check("diff: frame1->2 no change, frame2->3 all 8 changed", lambda: d1 == ["CHG 0", "CHG 8"])
    s9, d9, py9 = frames(reset_us=9)
    print("      diff (reset 9 us):  %s" % d9)
    check("reset_us=9: frames [8,5,3,8] -> LEN warnings show the early latch", lambda: [
        len(x[1]) for x in py9] == [8, 5, 3, 8] and d9 == ["LEN 5", "LEN 3", "LEN 8"])

    print("-- 연습 3: decodable(fs) vs sweep --")
    pred = {r: decodable(r * 10 ** 6) for r in range(1, 26)}
    print("      guaranteed MS/s: %s" % [r for r in range(1, 26) if pred[r]])
    print("      NOT guaranteed:  %s" % [r for r in range(1, 26) if not pred[r]])
    rates = [2, 4, 5, 8, 10, 16, 20, 25]
    sw = S.sweep(rates)
    check("prediction == sweep (colours ok 4/4 <=> guaranteed) at 2/4/5/8/10/16/20/25 MS/s",
          lambda: all((sw[r][0] == 4) == pred[r] for r in rates))
    check("non-monotonic: 4 MS/s guaranteed but 5 MS/s not", lambda: pred[4] and not pred[5])
    check("sufficient: Ts < min(split - w0_max, w1_min - split) = 163 ns -> fs > 6.13 MS/s; 7..25 all guaranteed",
          lambda: all(pred[r] for r in range(7, 26)) and math.isclose(1e3 / min(600 - 437, 813 - 600), 6.13,
                                                                      abs_tol=0.01))
    print("%d/%d passed" % (g_pass, g_total))
    return g_pass == g_total


def _two_stacks(i2c):
    # shim 의 stack 은 한 줄(체인)이라 두 번 돌린다. 실제 sigrok-cli 는 -P i2c,tmp117_lab -P i2c,i2cscan_lab 처럼
    # 별도 스택 두 개를 같은 입력에 걸 수 있다.
    a = srd_shim.run(S.I2cMini, i2c["data"], 1, i2c["fs"], {"scl": 0, "sda": 1}, None, [(S.Tmp117Lab, None)])
    b = srd_shim.run(S.I2cMini, i2c["data"], 1, i2c["fs"], {"scl": 0, "sda": 1}, None, [(I2cScanLab, None)])
    return a.ann_of('tmp117_lab') and b.ann_of('i2cscan_lab') and a.py == b.py


if __name__ == "__main__":
    sys.exit(0 if run_tests() else 1)
