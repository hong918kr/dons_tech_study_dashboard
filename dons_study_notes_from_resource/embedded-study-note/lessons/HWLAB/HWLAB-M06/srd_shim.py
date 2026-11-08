#!/usr/bin/env python3
"""HWLAB-M06 srd_shim — libsigrokdecode 의 파이썬 모듈 `sigrokdecode` 를 흉내 내는 최소 shim.

목적: sigrok-cli / PulseView 가 없는 맥에서 **진짜 디코더 파일(pd.py)을 한 글자도 바꾸지 않고** 실행·테스트한다.
디코더는 평소처럼 `import sigrokdecode as srd` 를 쓰고, 테스트 쪽이 먼저
    import srd_shim; sys.modules["sigrokdecode"] = srd_shim
를 해 둔다 (install() 함수).

흉내 내는 API (protocol decoder API v3):
  class Decoder        api_version / id / name / longname / desc / license / inputs / outputs / tags /
                       channels / optional_channels / options / annotations / annotation_rows
  self.register(srd.OUTPUT_ANN | OUTPUT_PYTHON | OUTPUT_BINARY | OUTPUT_META) -> output id
  self.put(ss, es, output_id, data)       ANN: [cls_idx, [긴 텍스트, ..., 짧은 텍스트]]
                                          PYTHON: 아무 객체 -> 위에 쌓인 디코더의 decode(ss, es, data)
  self.wait(conds) -> pins tuple          conds = dict 하나 또는 dict 의 list (list = OR, dict 안 = AND)
                                          term: {ch: 'r'|'f'|'e'|'h'|'l'|'n'}, {'skip': n}
  self.matched                            마지막 wait() 에서 각 조건이 맞았는지 (bool tuple)
  self.samplenum                          마지막 wait() 가 멈춘 샘플 번호 (절대 번호)
  self.options                            options 기본값 + 사용자가 준 값
  self.has_channel(i)                     (optional 포함) 채널 i 가 매핑돼 있는지
  metadata(key, value)                    srd.SRD_CONF_SAMPLERATE 로 샘플링률을 알려 준다
  start() -> decode()                     logic 입력: decode(self)  /  스택 디코더: decode(self, ss, es, data)

shim 이 정한 경계 의미론 (실제 libsigrokdecode 의 C 구현과 대조 실행은 못 했다 — md 참고):
  * 첫 wait() 는 샘플 0 부터, 이후 wait() 는 직전 매치 샘플 + 1 부터 검사한다 (같은 에지를 두 번 매치하지 않음).
  * 에지(r/f/e)는 샘플 k-1 -> k 의 변화. 샘플 0 에서는 에지가 없다. 'n' = 변화 없음.
  * {'skip': n} (n >= 1) 은 wait() 를 부른 시점의 samplenum + n 번째 샘플에서 참 (첫 호출은 0 + n).
  * 샘플이 끝날 때까지 맞는 것이 없으면 EndOfSamples (BaseException) -> run() 이 잡고 세션을 끝낸다.
"""
import bisect
import collections
import sys

OUTPUT_ANN, OUTPUT_PYTHON, OUTPUT_BINARY, OUTPUT_META = 0, 1, 2, 3
SRD_CONF_SAMPLERATE = 10000          # 실제 값과 무관; 디코더는 이름(srd.SRD_CONF_SAMPLERATE)으로만 쓴다
_EDGE = {"r", "f", "e"}
_TERMS = {"r", "f", "e", "h", "l", "n"}

Ann = collections.namedtuple("Ann", "pd ss es cls cls_id texts")
Py = collections.namedtuple("Py", "pd ss es data")


class EndOfSamples(BaseException):
    """샘플이 다 떨어짐. BaseException 이라 디코더의 `except Exception` 이 삼키지 못한다."""


class DecoderError(Exception):
    """디코더 메타데이터 / put() 인자 / wait() 조건이 API 규칙을 어김."""


def install():
    """`import sigrokdecode` 가 이 모듈을 가리키게 한다."""
    sys.modules["sigrokdecode"] = sys.modules[__name__]
    return sys.modules[__name__]


# ---------------------------------------------------------------- 메타데이터 검증
def validate_class(cls):
    """libsigrokdecode 가 디코더를 로드할 때 확인하는 것들의 부분집합."""
    for a in ("id", "name", "longname", "desc", "license"):
        if not isinstance(getattr(cls, a, None), str) or not getattr(cls, a):
            raise DecoderError("%s: missing/empty str attribute %r" % (cls.__name__, a))
    if getattr(cls, "api_version", None) != 3:
        raise DecoderError("%s: api_version must be 3" % cls.id)
    for a in ("inputs", "outputs"):
        v = getattr(cls, a, None)
        if not isinstance(v, (list, tuple)) or not all(isinstance(x, str) for x in v):
            raise DecoderError("%s: %s must be a list of str" % (cls.id, a))
    if not cls.inputs:
        raise DecoderError("%s: inputs must not be empty ('logic' or a stacked output id)" % cls.id)
    chans = tuple(getattr(cls, "channels", ())) + tuple(getattr(cls, "optional_channels", ()))
    for ch in chans:
        if not isinstance(ch, dict) or not all(isinstance(ch.get(k), str) for k in ("id", "name", "desc")):
            raise DecoderError("%s: channel entries need str id/name/desc" % cls.id)
    if "logic" in cls.inputs and not chans:
        raise DecoderError("%s: logic-input decoder without channels" % cls.id)
    anns = getattr(cls, "annotations", ())
    if not isinstance(anns, tuple) or not all(isinstance(a, tuple) and len(a) == 2 for a in anns):
        raise DecoderError("%s: annotations must be a tuple of (id, desc) tuples" % cls.id)
    for row in getattr(cls, "annotation_rows", ()):
        if len(row) != 3 or not all(isinstance(i, int) and 0 <= i < len(anns) for i in row[2]):
            raise DecoderError("%s: annotation_rows %r references unknown class" % (cls.id, row))
    for opt in getattr(cls, "options", ()):
        if "id" not in opt or "desc" not in opt or "default" not in opt:
            raise DecoderError("%s: option needs id/desc/default: %r" % (cls.id, opt))
        if "values" in opt and opt["default"] not in opt["values"]:
            raise DecoderError("%s: option %s default not in values" % (cls.id, opt["id"]))
    return True


# ---------------------------------------------------------------- 세션
class Session:
    def __init__(self):
        self.ann = []          # [Ann]
        self.py = []           # [Py]
        self.above = {}        # id(instance) -> [stacked instance]

    def ann_of(self, pd, cls_id=None):
        return [a for a in self.ann if a.pd == pd and (cls_id is None or a.cls_id == cls_id)]


class Decoder:
    """디코더의 베이스 클래스. 서브클래스는 __init__ 에서 super().__init__() 를 안 불러도 된다
    (upstream 디코더 대부분이 그렇다) — 런타임 속성은 run() 이 붙인다."""

    # ---- 출력
    def register(self, output_type, meta=None):
        if output_type not in (OUTPUT_ANN, OUTPUT_PYTHON, OUTPUT_BINARY, OUTPUT_META):
            raise DecoderError("register: unknown output type %r" % (output_type,))
        self._srd_outs.append(output_type)
        return len(self._srd_outs) - 1

    def put(self, ss, es, output_id, data):
        if not (isinstance(ss, int) and isinstance(es, int)) or isinstance(ss, bool) or ss < 0 or es < ss:
            raise DecoderError("%s: put(ss=%r, es=%r): need ints with 0 <= ss <= es" % (self.id, ss, es))
        try:
            typ = self._srd_outs[output_id]
        except (IndexError, TypeError):
            raise DecoderError("%s: put() with unregistered output id %r" % (self.id, output_id))
        if typ == OUTPUT_ANN:
            ok = (isinstance(data, list) and len(data) == 2 and isinstance(data[0], int)
                  and 0 <= data[0] < len(self.annotations) and isinstance(data[1], list) and data[1]
                  and all(isinstance(t, str) for t in data[1]))
            if not ok:
                raise DecoderError("%s: OUTPUT_ANN data must be [cls, [str, ...]]: %r" % (self.id, data))
            cls = data[0]
            self._srd_sess.ann.append(Ann(self.id, ss, es, cls, self.annotations[cls][0], tuple(data[1])))
        elif typ == OUTPUT_PYTHON:
            self._srd_sess.py.append(Py(self.id, ss, es, data))
            for up in self._srd_sess.above.get(id(self), []):
                up.decode(ss, es, data)
        # OUTPUT_BINARY / OUTPUT_META: 이 레슨에서는 버린다

    # ---- 채널
    def has_channel(self, index):
        return index in self._srd_bits

    # ---- wait()
    def wait(self, conds=None):
        if self._srd_levels is None:
            raise DecoderError("%s: wait() is only for decoders with 'logic' input" % self.id)
        if conds is None:
            conds = [{"skip": 1}]
        elif isinstance(conds, dict):
            conds = [conds]
        if not isinstance(conds, (list, tuple)) or not conds:
            raise DecoderError("%s: wait() needs a dict or a non-empty list of dicts" % self.id)
        for c in conds:
            for k, v in c.items():
                if k == "skip":
                    if not isinstance(v, int) or v < 1:
                        raise DecoderError("%s: skip must be an int >= 1 (got %r)" % (self.id, v))
                elif k not in self._srd_levels or v not in _TERMS:
                    raise DecoderError("%s: bad wait term %r:%r" % (self.id, k, v))
        base = self.samplenum
        k = 0 if self._srd_first else base + 1
        self._srd_first = False
        n = self._srd_n
        # 모든 조건에 에지 또는 skip 항이 있으면 "후보 샘플"만 보면 된다 (빠른 경로)
        jumpable = all(c and any(t == "skip" or c[t] in _EDGE for t in c) for c in conds)
        while k < n:
            if jumpable:
                k = self._next_candidate(conds, base, k)
                if k >= n:
                    break
            m = tuple(self._match(c, base, k) for c in conds)
            if any(m):
                self.samplenum = k
                self.matched = m
                return self._pins(k)
            k += 1
        self.samplenum = n
        raise EndOfSamples()

    def _next_candidate(self, conds, base, k):
        best = self._srd_n
        for c in conds:
            cand = []
            for t, v in c.items():
                if t == "skip":
                    cand.append(base + v if base + v >= k else self._srd_n)
                elif v in _EDGE:
                    ch = self._srd_changes[t]
                    i = bisect.bisect_left(ch, k)
                    cand.append(ch[i] if i < len(ch) else self._srd_n)
            # dict 안은 AND -> 모든 에지/skip 항이 동시에 참이어야 하므로 max(cand) 가 하한.
            # 그 샘플에서 AND 가 거짓이면 wait() 루프가 k+1 부터 다시 후보를 찾는다.
            best = min(best, max(cand))
        return best

    def _match(self, c, base, k):
        for t, v in c.items():
            if t == "skip":
                if k != base + v:
                    return False
                continue
            lv = self._srd_levels[t]
            cur = lv[k]
            if v == "h":
                if cur != 1:
                    return False
            elif v == "l":
                if cur != 0:
                    return False
            else:
                if k == 0:
                    if v != "n":
                        return False
                    continue
                prev = lv[k - 1]
                if v == "r" and not (prev == 0 and cur == 1):
                    return False
                if v == "f" and not (prev == 1 and cur == 0):
                    return False
                if v == "e" and prev == cur:
                    return False
                if v == "n" and prev != cur:
                    return False
        return True

    def _pins(self, k):
        return tuple(self._srd_levels[i][k] if i in self._srd_levels else 0
                     for i in range(len(self._srd_all_ch)))


# ---------------------------------------------------------------- 실행기
def _instantiate(cls, sess, samplerate, options):
    validate_class(cls)
    inst = cls()
    inst._srd_sess = sess
    inst._srd_outs = []
    inst.samplenum = 0
    inst.matched = None
    inst._srd_first = True
    inst._srd_levels = None
    inst._srd_bits = {}
    inst._srd_all_ch = tuple(getattr(cls, "channels", ())) + tuple(getattr(cls, "optional_channels", ()))
    opts = {o["id"]: o["default"] for o in getattr(cls, "options", ())}
    for k, v in (options or {}).items():
        if k not in opts:
            raise DecoderError("%s: unknown option %r" % (cls.id, k))
        spec = [o for o in cls.options if o["id"] == k][0]
        if "values" in spec and v not in spec["values"]:
            raise DecoderError("%s: option %s=%r not in %r" % (cls.id, k, v, spec["values"]))
        opts[k] = v
    inst.options = opts
    if samplerate is not None and hasattr(inst, "metadata"):
        inst.metadata(SRD_CONF_SAMPLERATE, samplerate)
    return inst


def run(decoder_cls, data, unitsize, samplerate, channels, options=None, stack=()):
    """한 번의 디코드 세션 (sigrok-cli -P a:ch=..,b,c 와 같은 모양).
    data: 샘플 바이트열 (샘플당 unitsize 바이트, little-endian; probe N = 비트 N-1)
    channels: {채널 id: 비트 번호} (디코더 channels/optional_channels 의 id)
    stack: [(class, options), ...] 아래에서 위로 쌓을 디코더들 (inputs 가 아래 디코더 outputs 와 맞아야 함)
    반환: Session (.ann, .py)"""
    sess = Session()
    bottom = _instantiate(decoder_cls, sess, samplerate, options)
    if "logic" not in decoder_cls.inputs:
        raise DecoderError("%s: bottom decoder must take 'logic'" % decoder_cls.id)
    ids = [c["id"] for c in bottom._srd_all_ch]
    for need in getattr(decoder_cls, "channels", ()):
        if need["id"] not in channels:
            raise DecoderError("%s: required channel %r not mapped" % (decoder_cls.id, need["id"]))
    for cid in channels:
        if cid not in ids:
            raise DecoderError("%s: no channel %r" % (decoder_cls.id, cid))
    if len(data) % unitsize:
        raise DecoderError("sample data length %d not a multiple of unitsize %d" % (len(data), unitsize))
    n = len(data) // unitsize
    words = [int.from_bytes(data[k * unitsize:(k + 1) * unitsize], "little") for k in range(n)] \
        if unitsize > 1 else list(data)
    bottom._srd_n = n
    bottom._srd_levels, bottom._srd_changes = {}, {}
    for cid, bit in channels.items():
        idx = ids.index(cid)
        lv = bytes((w >> bit) & 1 for w in words)
        bottom._srd_levels[idx] = lv
        bottom._srd_bits[idx] = bit
        bottom._srd_changes[idx] = [k for k in range(1, n) if lv[k] != lv[k - 1]]
    chain = [bottom]
    for cls, opts in stack:
        up = _instantiate(cls, sess, samplerate, opts)
        below = chain[-1]
        if not set(cls.inputs) & set(type(below).outputs):
            raise DecoderError("cannot stack %s (inputs %r) on %s (outputs %r)"
                               % (cls.id, cls.inputs, type(below).id, type(below).outputs))
        sess.above.setdefault(id(below), []).append(up)
        chain.append(up)
    for inst in chain:
        if hasattr(inst, "start"):
            inst.start()
    try:
        bottom.decode()
    except EndOfSamples:
        pass
    return sess
