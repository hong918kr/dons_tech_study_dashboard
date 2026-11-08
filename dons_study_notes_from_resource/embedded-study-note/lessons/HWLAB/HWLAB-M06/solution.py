#!/usr/bin/env python3
"""HWLAB-M06 커스텀 sigrok 프로토콜 디코더 작성 (libsigrokdecode)

  Part A  WS2812 디코더를 libsigrokdecode API v3 모양 그대로 작성 (Decoder 클래스, wait() 조건, put annotation)
          -> srd_shim 위에서 합성 .sr (evt1_led_ws2812_16M.sr) 을 디코드
  Part B  어려운 부분: wait() 경계(같은 샘플에서 두 조건 동시 매치), 캡처가 프레임 중간에서 시작, ±1 샘플
          가드밴드, 샘플링률 스윕 + B07 로더 기반 **독립 구현**과 출력이 annotation 단위로 같은지 교차 검증
  Part C  I2C 디코더 위에 쌓는(stacked) TMP117 레지스터 디코더 + EVT1 보드 리포트 (심어 둔 결함 2개)

  python3 -I solution.py      # "N/M passed", 실패 시 exit 1

같은 디렉터리: srd_shim.py (libsigrokdecode 흉내), gen_capture.py (합성 캡처), gen_decoders.py
(# ==== pd:<id> begin/end ==== 구역을 decoders/<id>/pd.py 로 내보내 PulseView 에 그대로 넣을 수 있게 함).
Python 3 표준 라이브러리만.
"""
import importlib
import io
import os
import random
import re
import sys
import time
import zipfile
from fractions import Fraction

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True   # 같은 디렉터리 모듈 import 시 __pycache__ 를 남기지 않는다
sys.path.insert(0, HERE)          # python3 -I 는 스크립트 디렉터리를 sys.path 에 넣지 않는다
import srd_shim  # noqa: E402
srd_shim.install()                # 이제 `import sigrokdecode` = shim. PulseView 안에서는 진짜 libsigrokdecode.
import sigrokdecode as srd  # noqa: E402
import gen_capture as G  # noqa: E402

PS_PER_S = 10 ** 12


# ---------------------------------------------------------------- HWLAB-B07 로더 축약 (주어짐)
def parse_ini(text):
    out, sec = {}, None
    for line in re.split(r"\r\n|\n\r|\r|\n", text):
        s = line.strip()
        if not s or s.startswith("#"):
            continue
        if s.startswith("[") and s.endswith("]"):
            sec = s[1:-1]
            out[sec] = {}
        elif "=" in s and sec is not None:
            k, v = s.split("=", 1)
            out[sec][k.strip()] = v.strip()
    return out


def parse_samplerate(s):
    m = re.match(r"^\s*(\d+(?:\.\d+)?)\s*([kMG]?)(?:Hz)?\s*$", s)
    if not m:
        raise ValueError("bad samplerate %r" % s)
    v = Fraction(m.group(1)) * {"": 1, "k": 10 ** 3, "M": 10 ** 6, "G": 10 ** 9}[m.group(2)]
    if v.denominator != 1 or v <= 0:
        raise ValueError("bad samplerate %r" % s)
    return int(v)


def load_sr_raw(src):
    """.sr -> {"names", "fs", "unitsize", "data"} (샘플 그대로; srd_shim.run 에 넣는다)"""
    with zipfile.ZipFile(src) as z:
        if z.read("version").decode("ascii").strip() != "2":
            raise ValueError("unsupported .sr version")
        dev = parse_ini(z.read("metadata").decode("utf-8"))["device 1"]
        base = dev["capturefile"]
        pat = re.compile(re.escape(base) + r"-(\d+)$")
        chunks = sorted((int(m.group(1)), n) for n in z.namelist() for m in [pat.match(n)] if m)
        data = b"".join(z.read(n) for _, n in chunks) if chunks else z.read(base)
    n_pr = int(dev["total probes"])
    names = [dev["probe%d" % i] for i in range(1, n_pr + 1)]
    return {"names": names, "fs": parse_samplerate(dev["samplerate"]), "unitsize": int(dev["unitsize"]), "data": data}


def samples_to_transitions(data, unitsize, names, ts_ps):
    n = len(data) // unitsize
    if len(data) % unitsize:
        raise ValueError("truncated sample data")
    tr = {nm: [] for nm in names}
    prev = None
    for k in range(n):
        w = int.from_bytes(data[k * unitsize:(k + 1) * unitsize], "little")
        if prev is None or w != prev:
            for i, nm in enumerate(names):
                b = (w >> i) & 1
                if prev is None or b != ((prev >> i) & 1):
                    tr[nm].append((k * ts_ps, b))
        prev = w
    return tr


def load_sr(src):
    """B07 공통 표현: {"names", "tr": {ch: [(t_ps, lvl)]}, "ts_ps", "t_end"}"""
    raw = load_sr_raw(src)
    if PS_PER_S % raw["fs"]:
        raise ValueError("sample period not an integer ps")
    ts = PS_PER_S // raw["fs"]
    n = len(raw["data"]) // raw["unitsize"]
    return {"names": raw["names"], "tr": samples_to_transitions(raw["data"], raw["unitsize"], raw["names"], ts),
            "ts_ps": ts, "t_end": n * ts}


def probe_bits(names, mapping):
    """sigrok-cli 의 -P pd:din=LED_DIN 처럼 {채널 id: probe 이름} -> {채널 id: 비트 번호}"""
    return {cid: names.index(pn) for cid, pn in mapping.items()}


# ==== pd:ws2812_lab begin ====
# WS2812B 대표 타이밍 [ns] (Worldsemi WS2812B 데이터시트 계열 값 — 칩 버전/클론마다 다르다, 확인 필요)
T0H_NS = (250, 550)       # 0.40 us +-150 ns
T1H_NS = (650, 950)       # 0.80 us +-150 ns
SPLIT_NS = 600            # high >= 600 ns 이면 1 (두 창의 가운데)
PERIOD_NS = (650, 1850)   # TH + TL = 1.25 us +-600 ns
NOM_NS = 1250             # 마지막 비트(뒤가 reset)의 es 를 정할 때 쓰는 공칭 주기


class SamplerateError(Exception):
    pass


def ns_ge(n, ns, fs):
    """n 샘플 >= ns 나노초 ?  정수 연산이라 float 경계 오차가 없다 (독립 구현과 비트 단위로 같아야 함)."""
    return n * 10 ** 9 >= ns * fs


def outside(n, lo_ns, hi_ns, fs, guard):
    """n 샘플로 잰 구간이 [lo, hi] ns 밖인가.
    guard: 샘플링된 구간의 진짜 길이는 ((n-1)Ts, (n+1)Ts) (HWLAB-B07) -> '확실히 밖'일 때만 True."""
    if guard:
        return (n + 1) * 10 ** 9 <= lo_ns * fs or (n - 1) * 10 ** 9 >= hi_ns * fs
    return n * 10 ** 9 < lo_ns * fs or n * 10 ** 9 > hi_ns * fs


class Ws2812Lab(srd.Decoder):
    api_version = 3
    id = 'ws2812_lab'
    name = 'WS2812 (lab)'
    longname = 'WS2812/WS2812B RGB LED chain with timing checks'
    desc = 'Single-wire NRZ RGB LED protocol (HWLAB-M06 study decoder).'
    license = 'gplv2+'
    inputs = ['logic']
    outputs = []
    tags = ['Display', 'IC']
    channels = (
        {'id': 'din', 'name': 'DIN', 'desc': 'DIN data line'},
    )
    options = (
        {'id': 'reset_us', 'desc': 'Reset (latch) low time [us]', 'default': 50},
        {'id': 'order', 'desc': 'Colour order on the wire', 'default': 'GRB', 'values': ('GRB', 'RGB')},
        {'id': 'guard', 'desc': '+-1 sample guard band on timing checks', 'default': 'yes',
         'values': ('yes', 'no')},
    )
    annotations = (
        ('bit', 'Bit'),
        ('reset', 'Reset'),
        ('rgb', 'RGB'),
        ('warning', 'Warning'),
    )
    annotation_rows = (
        ('bits', 'Bits', (0, 1)),
        ('leds', 'LEDs', (2,)),
        ('warnings', 'Warnings', (3,)),
    )

    def __init__(self):
        self.reset()

    def reset(self):
        self.samplerate = None
        self.bits = []
        self.led = 0

    def metadata(self, key, value):
        if key == srd.SRD_CONF_SAMPLERATE:
            self.samplerate = value

    def start(self):
        self.out_ann = self.register(srd.OUTPUT_ANN)

    def putw(self, ss, es, code, msg):
        self.put(ss, es, self.out_ann, [3, [msg, code]])

    def put_bit(self, ss, es, bit):
        self.put(ss, es, self.out_ann, [0, ['%d' % bit]])
        self.bits.append((ss, es, bit))
        if len(self.bits) == 24:
            v = 0
            for _, _, b in self.bits:
                v = (v << 1) | b
            c0, c1, c2 = (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF
            r, g, b = (c1, c0, c2) if self.options['order'] == 'GRB' else (c0, c1, c2)
            self.led += 1
            txt = '#%02X%02X%02X' % (r, g, b)
            self.put(self.bits[0][0], self.bits[-1][1], self.out_ann,
                     [2, ['LED %d: %s (R=%d G=%d B=%d)' % (self.led, txt, r, g, b), txt]])
            self.bits = []

    def end_frame(self, fall):
        if self.bits:
            self.putw(self.bits[0][0], self.bits[-1][1], 'PARTIAL',
                      '%d bits left over (not a multiple of 24)' % len(self.bits))
        self.put(fall, fall + self.n_reset, self.out_ann, [1, ['RESET after %d LEDs' % self.led, 'RESET']])
        self.bits, self.led = [], 0

    def decode(self):
        if not self.samplerate:
            raise SamplerateError('Cannot decode without samplerate.')
        fs = self.samplerate
        guard = self.options['guard'] == 'yes'
        self.n_reset = -(-self.options['reset_us'] * fs // 10 ** 6)     # ceil(reset_us * fs / 1e6)
        n_nom = (NOM_NS * fs + 5 * 10 ** 8) // 10 ** 9                   # round(1.25 us * fs)
        self.bits, self.led = [], 0
        self.wait({0: 'r'})
        if self.samplenum < self.n_reset:
            self.putw(0, self.samplenum, 'NOSYNC', 'no reset-length low before first bit: frame may be misaligned')
        while True:
            rise = self.samplenum
            self.wait({0: 'f'})
            fall = self.samplenum
            h = fall - rise
            bit = 1 if ns_ge(h, SPLIT_NS, fs) else 0
            lo, hi = T1H_NS if bit else T0H_NS
            if outside(h, lo, hi, fs, guard):
                self.putw(rise, fall, 'T%dH' % bit,
                          'T%dH %.0f ns outside %d..%d ns' % (bit, h * 1e9 / fs, lo, hi))
            self.wait([{0: 'r'}, {'skip': self.n_reset}])
            if self.matched[1]:
                # low >= reset: 프레임 끝. 같은 샘플에서 rise 도 맞았으면(matched[0]) 그 rise 가 다음 비트의 시작이다.
                self.put_bit(rise, max(fall, rise + n_nom), bit)
                self.end_frame(fall)
                if not self.matched[0]:
                    self.wait({0: 'r'})
                continue
            nxt = self.samplenum
            self.put_bit(rise, nxt, bit)
            p = nxt - rise
            if outside(p, PERIOD_NS[0], PERIOD_NS[1], fs, guard):
                if p * 10 ** 9 > PERIOD_NS[1] * fs:
                    self.putw(fall, nxt, 'GAP', 'low gap %.1f us (> bit %d ns, < reset %d us)'
                              % ((nxt - fall) * 1e6 / fs, PERIOD_NS[1], self.options['reset_us']))
                else:
                    self.putw(rise, nxt, 'PERIOD', 'bit period %.0f ns < %d ns' % (p * 1e9 / fs, PERIOD_NS[0]))
# ==== pd:ws2812_lab end ====


class I2cMini(srd.Decoder):
    """upstream i2c 디코더의 OUTPUT_PYTHON 형식만 흉내 낸 축약판 (주어짐). 스택 디코더 시험용.
    ['START'|'START REPEAT'|'STOP'|'ACK'|'NACK', None], ['ADDRESS READ'|'ADDRESS WRITE', 7비트 주소],
    ['DATA READ'|'DATA WRITE', 바이트], ['BITS', [[bit, ss, es], ...] LSB first]"""
    api_version = 3
    id = 'i2c_mini'
    name = 'I2C (mini)'
    longname = 'I2C, compact stand-in for the upstream i2c decoder'
    desc = 'I2C bus decoder with upstream-compatible Python output.'
    license = 'gplv2+'
    inputs = ['logic']
    outputs = ['i2c']
    tags = ['Embedded/industrial']
    channels = (
        {'id': 'scl', 'name': 'SCL', 'desc': 'Serial clock line'},
        {'id': 'sda', 'name': 'SDA', 'desc': 'Serial data line'},
    )
    annotations = (('start', 'Start'), ('repeat-start', 'Repeat start'), ('stop', 'Stop'), ('ack', 'ACK'),
                   ('nack', 'NACK'), ('address-read', 'Address read'), ('address-write', 'Address write'),
                   ('data-read', 'Data read'), ('data-write', 'Data write'))
    annotation_rows = (('addr-data', 'Address/data', tuple(range(9))),)

    def __init__(self):
        self.samplerate = None

    def metadata(self, key, value):
        if key == srd.SRD_CONF_SAMPLERATE:
            self.samplerate = value

    def start(self):
        self.out_ann = self.register(srd.OUTPUT_ANN)
        self.out_python = self.register(srd.OUTPUT_PYTHON)

    def putp(self, ss, es, cmd, val, cls, txt):
        self.put(ss, es, self.out_python, [cmd, val])
        self.put(ss, es, self.out_ann, [cls, [txt]])

    def decode(self):
        busy, bits, first, rw = False, [], False, 'WRITE'
        while True:
            pins = self.wait([{0: 'h', 1: 'f'}, {0: 'h', 1: 'r'}, {0: 'r'}])
            k = self.samplenum
            if self.matched[0]:
                self.putp(k, k, 'START REPEAT' if busy else 'START', None, 1 if busy else 0, 'Sr' if busy else 'S')
                busy, bits, first = True, [], True
            elif self.matched[1]:
                if busy:
                    self.putp(k, k, 'STOP', None, 2, 'P')
                busy, bits = False, []
            elif busy:
                bits.append((pins[1], k))
                if len(bits) == 9:
                    v = 0
                    for b, _ in bits[:8]:
                        v = (v << 1) | b
                    ss, ack_ss = bits[0][1], bits[8][1]
                    w = bits[8][1] - bits[7][1]
                    self.put(ss, ack_ss, self.out_python, ['BITS', [[b, s, s + w] for b, s in reversed(bits[:8])]])
                    if first:
                        rw = 'READ' if v & 1 else 'WRITE'
                        cmd, val, cls, first = 'ADDRESS ' + rw, v >> 1, 5 if v & 1 else 6, False
                    else:
                        cmd, val, cls = 'DATA ' + rw, v, 7 if rw == 'READ' else 8
                    self.putp(ss, ack_ss, cmd, val, cls, '%s: %02X' % (cmd.title(), val))
                    nack = bits[8][0]
                    self.putp(ack_ss, ack_ss + w, 'NACK' if nack else 'ACK', None, 4 if nack else 3,
                              'N' if nack else 'A')
                    bits = []


# ==== pd:tmp117_lab begin ====
# TI TMP117 레지스터 (대표값 — 데이터시트 확인 필요). 16비트, MSB 먼저. Temp 1 LSB = 7.8125 m°C = 1/128 °C.
TMP117_REGS = {0x00: 'Temp_Result', 0x01: 'Configuration', 0x02: 'THigh_Limit', 0x03: 'TLow_Limit',
               0x04: 'EEPROM_UL', 0x05: 'EEPROM1', 0x06: 'EEPROM2', 0x07: 'Temp_Offset',
               0x08: 'EEPROM3', 0x0F: 'Device_ID'}
TMP117_DID = 0x117
TMP117_MOD = ('CC', 'SD', 'CC', 'OS')     # Configuration[11:10]: continuous / shutdown / continuous / one-shot


class Tmp117Lab(srd.Decoder):
    api_version = 3
    id = 'tmp117_lab'
    name = 'TMP117 (lab)'
    longname = 'TI TMP117 temperature sensor registers'
    desc = 'TMP117 register reads/writes on top of I2C (HWLAB-M06 study decoder).'
    license = 'gplv2+'
    inputs = ['i2c']
    outputs = []
    tags = ['IC', 'Sensor']
    options = (
        {'id': 'address', 'desc': '7-bit I2C address (ADD0 strap)', 'default': 0x48,
         'values': (0x48, 0x49, 0x4A, 0x4B)},
    )
    annotations = (
        ('read', 'Register read'),
        ('write', 'Register write'),
        ('temp', 'Temperature'),
        ('warning', 'Warning'),
    )
    annotation_rows = (
        ('regs', 'Registers', (0, 1)),
        ('temps', 'Temperature', (2,)),
        ('warnings', 'Warnings', (3,)),
    )

    def __init__(self):
        self.reset()

    def reset(self):
        self.state = 'IDLE'
        self.pointer = None          # TMP117 은 pointer 를 트랜잭션 사이에도 기억한다
        self.addr, self.rw, self.addr_ss = None, None, 0
        self.data = []

    def start(self):
        self.out_ann = self.register(srd.OUTPUT_ANN)

    def putw(self, ss, es, code, msg):
        self.put(ss, es, self.out_ann, [3, [msg, code]])

    def detail(self, reg, v):
        if reg == 0x01:
            return ' (MOD=%s)' % TMP117_MOD[(v >> 10) & 3]
        if reg == 0x0F:
            return ' (DID=0x%03X REV=%d)' % (v & 0xFFF, v >> 12)
        return ''

    def finish(self):
        """세그먼트(START/Sr ~ 다음 Sr/STOP) 하나를 레지스터 접근으로 해석"""
        if self.state != 'DATA' or not self.data:
            self.data = []
            return
        ss, es = self.data[0][0], self.data[-1][1]
        vals = [v for _, _, v in self.data]
        self.data = []
        if self.rw == 'W':
            self.pointer = vals[0]
            if len(vals) == 3:
                v = (vals[1] << 8) | vals[2]
                name = TMP117_REGS.get(self.pointer, 'reg?')
                self.put(ss, es, self.out_ann, [1, ['Write %s (0x%02X) = 0x%04X%s'
                                                    % (name, self.pointer, v, self.detail(self.pointer, v)),
                                                    'W %02X=%04X' % (self.pointer, v)]])
            elif len(vals) != 1:
                self.putw(ss, es, 'BADLEN', 'write of %d bytes (expected pointer [+ 2 data])' % len(vals))
            return
        if len(vals) != 2:
            self.putw(ss, es, 'SHORT', 'read of %d bytes (registers are 16-bit)' % len(vals))
            return
        if self.pointer is None:
            self.putw(ss, es, 'NOPTR', 'read before any pointer write in this capture')
            return
        reg, v = self.pointer, (vals[0] << 8) | vals[1]
        name = TMP117_REGS.get(reg, 'reg?')
        self.put(ss, es, self.out_ann, [0, ['Read %s (0x%02X) = 0x%04X%s' % (name, reg, v, self.detail(reg, v)),
                                            'R %02X=%04X' % (reg, v)]])
        if reg == 0x00:
            t = (v - 0x10000 if v & 0x8000 else v) / 128.0
            self.put(ss, es, self.out_ann, [2, ['T = %.4f °C (raw 0x%04X)' % (t, v), 'T %.4f' % t]])
        if reg == 0x0F and (v & 0xFFF) != TMP117_DID:
            self.putw(ss, es, 'DID', 'Device_ID 0x%04X: DID 0x%03X != 0x117 (not a TMP117?)' % (v, v & 0xFFF))

    def decode(self, ss, es, data):
        cmd, val = data
        if cmd in ('START', 'START REPEAT', 'STOP'):
            self.finish()
            self.state = 'IDLE' if cmd == 'STOP' else 'ADDR'
        elif cmd in ('ADDRESS READ', 'ADDRESS WRITE') and self.state == 'ADDR':
            self.addr, self.rw, self.addr_ss = val, 'R' if cmd.endswith('READ') else 'W', ss
            self.state = 'ADDR_ACK'
        elif cmd in ('ACK', 'NACK') and self.state == 'ADDR_ACK':
            if cmd == 'NACK':
                if 0x48 <= self.addr <= 0x4B:
                    self.putw(self.addr_ss, es, 'NACK %02X' % self.addr,
                              'address 0x%02X NACK: no TMP117 there (this decoder: 0x%02X)'
                              % (self.addr, self.options['address']))
                self.state = 'IDLE'
            else:
                self.state = 'DATA' if self.addr == self.options['address'] else 'OTHER'
                self.data = []
        elif cmd in ('DATA READ', 'DATA WRITE') and self.state == 'DATA':
            self.data.append((ss, es, val))
        # 'BITS', 데이터 바이트의 ACK/NACK, 다른 주소의 트래픽: 무시
# ==== pd:tmp117_lab end ====


# ---------------------------------------------------------------- Part B: B07 로더 기반 독립 구현
def ws2812_ref(cap, ch="LED_DIN", reset_us=50, order="GRB", guard=True):
    """wait() 없이 transition 리스트(B07 공통 표현)만으로 같은 규칙을 구현 -> canonical annotation 리스트
    [(ss, es, cls_id, key)]  key = 짧은 텍스트 (비트 '0'/'1', '#RRGGBB', 'RESET', 경고 코드)"""
    ts = cap["ts_ps"]
    fs = PS_PER_S // ts
    if PS_PER_S % ts:
        raise ValueError("ts_ps must divide 1 s")
    n = cap["t_end"] // ts
    n_reset = -(-reset_us * fs // 10 ** 6)
    n_nom = (NOM_NS * fs + 5 * 10 ** 8) // 10 ** 9
    edges = cap["tr"][ch][1:]                       # 첫 원소 = 초기 레벨 (에지 아님)
    rises = [t // ts for t, v in edges if v == 1]
    falls = [t // ts for t, v in edges if v == 0]
    out = []
    bits = []
    leds = [0]

    def warn(ss, es, code):
        out.append((ss, es, "warning", code))

    def put_bit(ss, es, bit):
        out.append((ss, es, "bit", str(bit)))
        bits.append((ss, es, bit))
        if len(bits) == 24:
            v = 0
            for _, _, b in bits:
                v = (v << 1) | b
            c0, c1, c2 = (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF
            r, g, b = (c1, c0, c2) if order == "GRB" else (c0, c1, c2)
            out.append((bits[0][0], bits[-1][1], "rgb", "#%02X%02X%02X" % (r, g, b)))
            leds[0] += 1
            del bits[:]

    if not rises:
        return out
    if rises[0] < n_reset:
        warn(0, rises[0], "NOSYNC")
    fi = 0
    for i, r in enumerate(rises):
        while fi < len(falls) and falls[fi] <= r:
            fi += 1
        if fi == len(falls):
            break                                   # high 인 채로 캡처 끝
        f = falls[fi]
        h = f - r
        bit = 1 if ns_ge(h, SPLIT_NS, fs) else 0
        lo, hi = T1H_NS if bit else T0H_NS
        if outside(h, lo, hi, fs, guard):
            warn(r, f, "T%dH" % bit)
        r2 = rises[i + 1] if i + 1 < len(rises) else None
        if (r2 is not None and r2 - f >= n_reset) or (r2 is None and f + n_reset <= n - 1):
            put_bit(r, max(f, r + n_nom), bit)
            if bits:
                warn(bits[0][0], bits[-1][1], "PARTIAL")
            out.append((f, f + n_reset, "reset", "RESET"))
            del bits[:]
            leds[0] = 0
            continue
        if r2 is None:
            break                                   # reset 판정 전에 캡처 끝 -> 이 비트는 내보내지 않는다
        put_bit(r, r2, bit)
        p = r2 - r
        if outside(p, PERIOD_NS[0], PERIOD_NS[1], fs, guard):
            warn(f, r2, "GAP") if p * 10 ** 9 > PERIOD_NS[1] * fs else warn(r, r2, "PERIOD")
    return out


def canon(sess, pd):
    """shim 세션의 annotation -> 독립 구현과 같은 canonical 형식"""
    return sorted((a.ss, a.es, a.cls_id, a.texts[-1]) for a in sess.ann_of(pd))


def run_ws(raw, **opts):
    return srd_shim.run(Ws2812Lab, raw["data"], raw["unitsize"], raw["fs"],
                        probe_bits(raw["names"], {"din": "LED_DIN"}), opts)


def run_i2c(raw, stack_cls=None, **opts):
    stack = [(stack_cls or Tmp117Lab, opts)]
    return srd_shim.run(I2cMini, raw["data"], raw["unitsize"], raw["fs"],
                        probe_bits(raw["names"], {"scl": "I2C2_SCL", "sda": "I2C2_SDA"}), None, stack)


# ---------------------------------------------------------------- Part C: EVT1 보드 리포트
def board_report(led_ann, i2c_ann, led_fs, i2c_fs, tmp_addr=0x48):
    """led_ann: ws2812_lab canonical 리스트, i2c_ann: tmp117_lab canonical 리스트 -> 리포트 줄 리스트.
    LED: 프레임별 LED 수, 경고를 'frame N, after LED k' 위치로. I2C: Device_ID/T/CONFIG, NACK 를 원인 힌트와 함께."""
    lines = []
    ev = sorted(led_ann)
    frames, cur, where = [], 0, []
    for ss, es, cls, key in ev:
        if cls == "rgb":
            cur += 1
        elif cls == "reset":
            frames.append(cur)
            cur = 0
        elif cls == "warning":
            where.append((len(frames) + 1, cur, key, ss, es))
    lines.append("LED  net=LED_DIN fs=%dMS/s frames=%d leds=%s" % (led_fs // 10 ** 6, len(frames), frames))
    bad = False
    for fr, after, key, ss, es in where:
        bad = True
        if key == "GAP":
            lines.append("FAIL net=LED_DIN frame %d: GAP %.1f us after LED %d (bit period max 1.85 us < gap < "
                         "reset 50 us) -> SPI DMA underrun / refill ISR latency; a strip that latches on a "
                         "shorter low would split this frame" % (fr, (es - ss) * 1e6 / led_fs, after))
        else:
            lines.append("FAIL net=LED_DIN frame %d: %s near LED %d" % (fr, key, after + 1))
    if not bad:
        lines.append("PASS net=LED_DIN all bits within WS2812B timing (+-1 sample guard)")
    regs = [(k, ss) for ss, es, cls, k in sorted(i2c_ann) if cls in ("read", "write")]
    temps = [float(k.split()[1]) for _, _, cls, k in sorted(i2c_ann) if cls == "temp"]
    did = [k[5:] for k, _ in regs if k.startswith("R 0F=")]
    cfg = [k[5:] for k, _ in regs if k.startswith("R 01=")]
    lines.append("I2C  net=I2C2 fs=%dMS/s TMP117@0x%02X Device_ID=%s T=[%s] C CONFIG=%s" % (
        i2c_fs // 10 ** 6, tmp_addr, "0x" + did[-1] if did else "none",
        ", ".join("%.4f" % t for t in temps), "0x" + cfg[-1] if cfg else "unread"))
    nacks = [k for _, _, cls, k in i2c_ann if cls == "warning" and k.startswith("NACK")]
    others = [k for _, _, cls, k in i2c_ann if cls == "warning" and not k.startswith("NACK")]
    for a in sorted(set(nacks)):
        msg = "FAIL net=I2C2 addr 0x%s NACK x%d -> a transfer meant for TMP117 went to the wrong address " \
              "(U7 answers at 0x%02X: check ADD0 strap vs firmware)" % (a[5:], nacks.count(a), tmp_addr)
        if cfg and cfg[-1] == "0220":
            msg += "; CONFIG readback 0x0220 = power-on default -> the config write never landed"
        lines.append(msg)
    for k in others:
        lines.append("FAIL net=I2C2 TMP117 %s" % k)
    if not nacks and not others:
        lines.append("PASS net=I2C2 TMP117@0x%02X all transfers ACKed, Device_ID ok" % tmp_addr)
    return lines


# ---------------------------------------------------------------- 테스트 하네스
g_pass = 0
g_total = 0


def check(name, fn):
    """fn() 이 True 면 PASS. 예외도 FAIL 로 센다 (starter 의 미구현 함수가 하네스를 죽이지 않게)."""
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


def raises(exc, fn):
    try:
        fn()
    except exc:
        return True
    return False


def p(name):
    return os.path.join(HERE, name)


def safe(fn, default=None):
    try:
        return fn()
    except Exception as e:  # noqa: BLE001
        print("      (%s: %s)" % (type(e).__name__, e))
        return default


def colors(lst):
    return ["#%02X%02X%02X" % c for c in lst]


def keys(ann, cls):
    return [k for _, _, c, k in sorted(ann) if c == cls]


def raw_from_samples(levels, fs, names=("LED_DIN",)):
    return {"names": list(names), "fs": fs, "unitsize": 1, "data": bytes(levels)}


def cap_from_raw(raw):
    ts = PS_PER_S // raw["fs"]
    return {"names": raw["names"], "tr": samples_to_transitions(raw["data"], raw["unitsize"], raw["names"], ts),
            "ts_ps": ts, "t_end": len(raw["data"]) // raw["unitsize"] * ts}


def synth_levels(frames_bits, lead=1000, tail=1000, start_high=0, h0=6, h1=13, per=20):
    """16 MS/s 정수 샘플 격자 위의 DIN (Part B 경계 실험용). 단위 = 샘플 (62.5 ns):
    T0H 6 (375 ns), T1H 13 (812.5 ns), 주기 20 (1250 ns), 프레임 뒤 low = 비트 low + tail."""
    lv = [1] * start_high + [0] * lead
    for bits in frames_bits:
        for b in bits:
            h = h1 if b else h0
            lv += [1] * h + [0] * (per - h)
        lv += [0] * tail
    return lv


def sweep(rates_msps, phases=4, seed=5):
    """연속 파형(프레임 2 의 GAP 포함) -> fs 별 샘플링 -> srd 디코더(guard yes/no) + 독립 구현.
    반환 {rate: (색 맞은 run 수, 가짜 타이밍 경고 guard=yes 합, guard=no 합, srd==ref 인 run 수)}"""
    wave = G.ws2812_waveform()
    truth = colors([c for fr in G.FRAMES for c in fr])
    rng = random.Random(seed)
    res = {}
    for r in rates_msps:
        fs = int(r * 10 ** 6)
        ok = fy = fn = agree = 0
        for _ in range(phases):
            data, us = G.sample_bytes(wave, fs, rng.uniform(0, 1e12 / fs))
            raw = {"names": G.LED_NAMES, "fs": fs, "unitsize": us, "data": data}
            sy, sn = canon(run_ws(raw), "ws2812_lab"), canon(run_ws(raw, guard="no"), "ws2812_lab")
            ok += keys(sy, "rgb") == truth
            fy += sum(1 for k in keys(sy, "warning") if k in ("T0H", "T1H", "PERIOD"))
            fn += sum(1 for k in keys(sn, "warning") if k in ("T0H", "T1H", "PERIOD"))
            agree += (sy == sorted(ws2812_ref(cap_from_raw(raw)))) and (sn == sorted(ws2812_ref(cap_from_raw(raw),
                                                                                                guard=False)))
        res[r] = (ok, fy, fn, agree)
    return res


def run_tests():
    t_start = time.time()
    truth = colors([c for fr in G.FRAMES for c in fr])
    led = safe(lambda: load_sr_raw(p("evt1_led_ws2812_16M.sr")))
    i2c = safe(lambda: load_sr_raw(p("evt1_i2c_tmp117_2M.sr")))

    print("-- Part A: shim + WS2812 decoder (API v3) --")
    check("shim: wait r/f/e/h/l/skip semantics on a 10-sample list", lambda: _shim_selftest())
    check("decoder metadata validates (Ws2812Lab, I2cMini, Tmp117Lab)", lambda: all(
        srd_shim.validate_class(c) for c in (Ws2812Lab, I2cMini, Tmp117Lab)))
    check("capture: LED_DIN 16 MS/s, 1 probe", lambda: led["names"] == ["LED_DIN"] and led["fs"] == 16_000_000)
    s16 = safe(lambda: canon(run_ws(led), "ws2812_lab"), [])
    check("24 LEDs decoded, colours == injected truth (3 frames x 8)", lambda: keys(s16, "rgb") == truth)
    check("576 bit annotations, 3 RESET", lambda: len(keys(s16, "bit")) == 576 and len(keys(s16, "reset")) == 3)
    check("first LED annotation text 'LED 1: #FF0000 (R=255 G=0 B=0)'", lambda: [
        a.texts[0] for a in run_ws(led).ann_of("ws2812_lab") if a.cls_id == "rgb"][0] == "LED 1: #FF0000 (R=255 G=0 B=0)")
    check("no samplerate -> SamplerateError", lambda: raises(SamplerateError, lambda: srd_shim.run(
        Ws2812Lab, led["data"], 1, None, {"din": 0})))
    check("order=RGB option swaps R/G of LED 1 -> #00FF00", lambda: keys(
        canon(run_ws(led, order="RGB"), "ws2812_lab"), "rgb")[0] == "#00FF00")

    print("-- Part B: edge cases, guard band, independent implementation --")
    check("planted DMA gap: GAP warning in frame 2 after LED 5, colours still right",
          lambda: [k for k in keys(s16, "warning")] == ["GAP"] and _gap_where(s16) == (2, 5))
    s9 = safe(lambda: canon(run_ws(led, reset_us=9), "ws2812_lab"), [])
    check("reset_us=9 (early-latching clone): frame 2 splits -> LEDs per frame [8, 5, 3, 8]",
          lambda: _leds_per_frame(s9) == [8, 5, 3, 8] and keys(s9, "rgb") == truth)
    fs, nreset = 16_000_000, 800                                           # 50 us x 16 MS/s
    lv = synth_levels([[1] * 24], tail=0)[:-7]                             # 마지막 비트의 low(20-13) 제거
    lv += [0] * nreset + synth_levels([[0] * 24], lead=0)                  # fall 뒤 low 정확히 n_reset 샘플
    edge = raw_from_samples(lv, fs)
    se = safe(lambda: canon(run_ws(edge), "ws2812_lab"), [])
    check("rise exactly at fall + n_reset (both conditions match): 2 LEDs, no lost bit",
          lambda: keys(se, "rgb") == ["#FFFFFF", "#000000"] and len(keys(se, "bit")) == 48)
    mid = raw_from_samples(synth_levels([[1, 0] * 12, [0] * 24], lead=48, start_high=5), fs)
    sm = safe(lambda: canon(run_ws(mid), "ws2812_lab"), [])
    check("capture starts high, mid-frame -> NOSYNC warning, then decodes", lambda: keys(sm, "warning")[0] == "NOSYNC"
          and keys(sm, "rgb")[-1] == "#000000")
    check("guard band: 4 MS/s, 4 samples (1000 ns) vs T1H max 950 -> naive warns, guard does not",
          lambda: outside(4, 650, 950, 4_000_000, False) and not outside(4, 650, 950, 4_000_000, True))
    check("guard band: 16 MS/s, 20 samples (1250 ns) vs T1H max 950 -> both warn",
          lambda: outside(20, 650, 950, 16_000_000, False) and outside(20, 650, 950, 16_000_000, True))
    cap16 = safe(lambda: load_sr(p("evt1_led_ws2812_16M.sr")))
    check("independent (B07 loader + transitions) == srd decoder on the .sr", lambda: sorted(ws2812_ref(cap16)) == s16)
    check("independent == srd with reset_us=9", lambda: sorted(ws2812_ref(cap16, reset_us=9)) == s9)
    check("independent == srd on the both-match edge case and the NOSYNC capture", lambda: sorted(
        ws2812_ref(cap_from_raw(edge))) == se and sorted(ws2812_ref(cap_from_raw(mid))) == sm)
    t0 = time.time()
    rates = [2, 4, 5, 8, 10, 16, 20, 25]
    sw = safe(lambda: sweep(rates), {})
    dt = time.time() - t0
    for r in rates:
        if r in sw:
            ok, fy, fn, ag = sw[r]
            print("      %2d MS/s Ts=%5.1f ns  colours ok %d/4  false timing warnings guard=yes %3d  guard=no %3d"
                  "  srd==ref %d/4" % (r, 1e3 / r, ok, fy, fn, ag))
    print("      (sweep %.1f s)" % dt)
    check("sweep: colours right at every phase for >= 8 MS/s; 2 and 5 MS/s break (4 MS/s only by grid luck)",
          lambda: all(sw[r][0] == 4 for r in rates if r >= 8) and sw[2][0] < 4 and sw[5][0] < 4)
    check("sweep: guard=yes -> 0 false timing warnings at >= 4 MS/s; guard=no -> some at 4/5 MS/s",
          lambda: all(sw[r][1] == 0 for r in rates if r >= 4) and sw[4][2] + sw[5][2] > 0)
    check("sweep: srd decoder == independent implementation on every run (8 rates x 4 phases x 2 modes)",
          lambda: all(sw[r][3] == 4 for r in rates))

    print("-- Part C: stacked TMP117 decoder on I2C + EVT1 report --")
    si = safe(lambda: run_i2c(i2c))
    check("i2c_mini python output: START, ADDRESS WRITE 0x48, ACK, DATA WRITE 0x0F, ACK, START REPEAT, ...",
          lambda: [tuple(x.data) for x in si.py if x.data[0] != "BITS"][:7] == [
              ("START", None), ("ADDRESS WRITE", 0x48), ("ACK", None), ("DATA WRITE", 0x0F), ("ACK", None),
              ("START REPEAT", None), ("ADDRESS READ", 0x48)])
    ti = safe(lambda: canon(si, "tmp117_lab"), [])
    check("Device_ID read 0x0117", lambda: "R 0F=0117" in keys(ti, "read"))
    check("temperatures 23.5000 / 23.5078 C (0x0BC0, 0x0BC1 x 1/128)", lambda: keys(ti, "temp") == [
        "T 23.5000", "T 23.5078"])
    check("planted defect: CONFIG write to 0x49 -> 'NACK 49' warning", lambda: keys(ti, "warning") == ["NACK 49"])
    check("CONFIG readback 0x0220 (MOD=CC)", lambda: "R 01=0220" in keys(ti, "read") and any(
        "MOD=CC" in a.texts[0] for a in si.ann_of("tmp117_lab") if a.cls_id == "read"))
    check("negative temp 0xFF80 -> -1.0000 C; read without pointer -> NOPTR", lambda: _tmp_unit())
    check("stacking tmp117_lab (inputs i2c) on ws2812_lab (outputs []) -> DecoderError", lambda: raises(
        srd_shim.DecoderError, lambda: srd_shim.run(Ws2812Lab, led["data"], 1, led["fs"], {"din": 0}, None,
                                                    [(Tmp117Lab, None)])))
    rep = safe(lambda: board_report(s16, ti, led["fs"], i2c["fs"]), [])
    for ln in rep:
        print("      " + ln)
    check("report: LED FAIL line = frame 2, after LED 5, gap 20.5..21.0 us (20 us injected + T0L 0.83 us)",
          lambda: any(re.match(r"FAIL net=LED_DIN frame 2: GAP (\d+\.\d) us after LED 5 ", ln)
                      and 20.5 <= float(ln.split()[5]) <= 21.0 for ln in rep))
    check("report: I2C FAIL line names 0x49 and the CONFIG default readback", lambda: any(
        ln.startswith("FAIL net=I2C2 addr 0x49 NACK x1") and "0x0220" in ln for ln in rep))
    check("report: clean board (no gap, right address) -> two PASS lines", lambda: _clean_report())
    check("decoders/ws2812_lab + tmp117_lab packages (gen_decoders.py) == in-file classes", lambda: _packages(
        led, i2c, s16, ti))
    print("%d/%d passed  (%.1f s)" % (g_pass, g_total, time.time() - t_start))
    return g_pass == g_total


# ---- 테스트 보조 (주어짐)
class _Probe(srd.Decoder):
    api_version = 3
    id = 'probe'
    name = longname = desc = 'probe'
    license = 'gplv2+'
    inputs = ['logic']
    outputs = []
    channels = ({'id': 'a', 'name': 'A', 'desc': 'a'}, {'id': 'b', 'name': 'B', 'desc': 'b'})
    annotations = (('x', 'x'),)

    def __init__(self):
        self.log = []

    def decode(self):
        for cond in ({0: 'r'}, {0: 'f'}, {0: 'e'}, {0: 'h', 1: 'l'}, {'skip': 1}, [{1: 'r'}, {'skip': 1}], {0: 'r'}):
            self.wait(cond)                       # 마지막 {0:'r'} 은 매치가 없어 EndOfSamples -> run() 이 끝낸다
            self.log.append((self.samplenum, self.matched))
            _Probe.last = list(self.log)


def _shim_selftest():
    #        k: 0  1  2  3  4  5  6  7  8  9
    a = [0, 0, 1, 1, 0, 0, 1, 1, 1, 0]
    b = [0, 0, 0, 0, 0, 1, 1, 0, 0, 1]
    data = bytes(x | (y << 1) for x, y in zip(a, b))
    srd_shim.run(_Probe, data, 1, 1_000_000, {"a": 0, "b": 1})
    return _Probe.last == [(2, (True,)), (4, (True,)), (6, (True,)), (7, (True,)), (8, (True,)), (9, (True, True))]


def _gap_where(ann):
    fr, cur = 1, 0
    for ss, es, cls, k in sorted(ann):
        if cls == "rgb":
            cur += 1
        elif cls == "reset":
            fr, cur = fr + 1, 0
        elif k == "GAP":
            return (fr, cur)
    return None


def _leds_per_frame(ann):
    out, cur = [], 0
    for _, _, cls, _ in sorted(ann):
        if cls == "rgb":
            cur += 1
        elif cls == "reset":
            out.append(cur)
            cur = 0
    return out


def _tmp_unit():
    t = Tmp117Lab()
    sess = srd_shim.Session()
    t._srd_sess, t._srd_outs, t.options = sess, [], {"address": 0x48}
    t.start()
    for ev in (["START", None], ["ADDRESS READ", 0x48], ["ACK", None], ["DATA READ", 0x01], ["DATA READ", 0x02],
               ["STOP", None], ["START", None], ["ADDRESS WRITE", 0x48], ["ACK", None], ["DATA WRITE", 0x00],
               ["START REPEAT", None], ["ADDRESS READ", 0x48], ["ACK", None], ["DATA READ", 0xFF],
               ["DATA READ", 0x80], ["STOP", None]):
        t.decode(0, 1, ev)
    c = canon(sess, "tmp117_lab")
    return keys(c, "warning") == ["NOPTR"] and keys(c, "temp") == ["T -1.0000"]


def _clean_report():
    w = G.ws2812_waveform(gap=None)
    d, us = G.sample_bytes(w, 16_000_000, 0.0)
    led = {"names": G.LED_NAMES, "fs": 16_000_000, "unitsize": us, "data": d}
    tx = [(desc, [(0x48, rw, [0x0C, 0x20] if desc == "read CONFIG back" and rw == "R" else b, True)
                  for _a, rw, b, _k in segs]) for desc, segs in G.I2C_TXNS]
    w2 = G.i2c_waveform(tx)
    d2, us2 = G.sample_bytes(w2, 2_000_000, 137000.0)
    i2c = {"names": G.I2C_NAMES, "fs": 2_000_000, "unitsize": us2, "data": d2}
    rep = board_report(canon(run_ws(led), "ws2812_lab"), canon(run_i2c(i2c), "tmp117_lab"), 16_000_000, 2_000_000)
    print("      clean: " + " | ".join(rep))
    return [ln.split()[0] for ln in rep] == ["LED", "PASS", "I2C", "PASS"] and "CONFIG=0x0C20" in rep[2]


def _packages(led, i2c, s16, ti):
    pw = importlib.import_module("decoders.ws2812_lab")
    pt = importlib.import_module("decoders.tmp117_lab")
    with open(p("decoders/ws2812_lab/pd.py"), encoding="utf-8") as f:
        src = f.read()
    a = canon(srd_shim.run(pw.Decoder, led["data"], 1, led["fs"], {"din": 0}), "ws2812_lab")
    b = canon(srd_shim.run(I2cMini, i2c["data"], 1, i2c["fs"], {"scl": 0, "sda": 1}, None, [(pt.Decoder, None)]),
              "tmp117_lab")
    return a == s16 and b == ti and "srd_shim" not in src and "import sigrokdecode as srd" in src


if __name__ == "__main__":
    sys.exit(0 if run_tests() else 1)
