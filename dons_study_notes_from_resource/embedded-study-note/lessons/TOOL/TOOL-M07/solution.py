# TOOL-M07 ringbuf_cmd.py — lldb Python 확장: `rbdump <expr>` 명령 + `struct ringbuf` 요약(summary) 제공자
#   (lldb) command script import ringbuf_cmd.py      ← check.sh 는 <variant>.py 를 이 이름으로 복사해서 쓴다
#   (lldb) rbdump dev.tx        rbdump rb        rbdump &dev.tx
#   (lldb) p dev.tx   /  frame variable rb  /  bt  ← 요약이 자동으로 붙는다
# lldb 내장 Python 3.9 — 3.10+ 문법(match, X | Y 타입) 쓰지 말 것.
import lldb


class RB(object):
    """SBValue 에서 꺼낸 링 버퍼 스냅샷. 여기부터는 파이썬 int 만 다룬다."""

    def __init__(self, addr, head, tail, count, cap, data):
        self.addr, self.head, self.tail = addr, head, tail
        self.count, self.cap, self.data = count, cap, data


def rb_address(valobj):
    # TODO(4): 구조체의 주소. valobj 는 `struct ringbuf` 일 수도 `struct ringbuf *` 일 수도 있다.
    if valobj.GetType().IsPointerType():
        return valobj.GetValueAsUnsigned()
    return valobj.GetLoadAddress()


def read_bytes(buf):
    # TODO(3): uint8_t[N] 배열 SBValue → 파이썬 int 리스트 (길이 N)
    return [buf.GetChildAtIndex(i).GetValueAsUnsigned() for i in range(buf.GetNumChildren())]


def snapshot(valobj):
    """struct 든 포인터든 GetChildMemberWithName 은 멤버를 돌려준다 (포인터면 자동으로 따라간다)."""
    buf = valobj.GetChildMemberWithName("buf")
    def u(name):
        return valobj.GetChildMemberWithName(name).GetValueAsUnsigned()
    return RB(rb_address(valobj), u("head"), u("tail"), u("count"),
              buf.GetNumChildren(), read_bytes(buf))


def logical_slots(rb):
    # TODO(1): 논리 순서(가장 오래된 바이트부터)로 물리 인덱스 목록. count 가 깨져 cap 보다 커도 cap 개까지만.
    return [(rb.tail + i) % rb.cap for i in range(min(rb.count, rb.cap))]


def rb_state(rb):
    # TODO(2): "empty" / "full" / "wrapped"(끝을 넘어 0번으로 이어짐) / "linear"
    if rb.count == 0:
        return "empty"
    if rb.count >= rb.cap:
        return "full"
    return "wrapped" if rb.tail + rb.count > rb.cap else "linear"


def rb_summary(valobj, internal_dict):
    """type summary add -F 용: 한 줄 요약. 예외를 던지면 lldb 가 요약을 그냥 비워 버린다."""
    rb = snapshot(valobj)
    s = "count=%d/%d tail=%d head=%d" % (rb.count, rb.cap, rb.tail, rb.head)
    if rb_state(rb) == "empty":
        return s + " empty"
    return s + " [" + " ".join("%02x" % rb.data[i] for i in logical_slots(rb)) + "]"


def find_value(frame, expr):
    v = frame.GetValueForVariablePath(expr)          # 변수 경로(dev.tx, rb, prb): 코드 실행 없이 빠르다
    if not v.IsValid() or v.GetError().Fail():
        v = frame.EvaluateExpression(expr)           # &dev.tx, (struct ringbuf *)0x... 같은 식
    return v


def rbdump(debugger, command, exe_ctx, result, internal_dict):
    """rbdump <expr> — struct ringbuf (또는 그 포인터) 를 논리 순서로 덤프한다"""
    expr = command.strip()
    frame = exe_ctx.GetFrame()
    if not expr:
        result.SetError("usage: rbdump <expr>")
        return
    if not frame.IsValid():
        result.SetError("rbdump: no frame (process not stopped?)")
        return
    v = find_value(frame, expr)
    if not v.IsValid() or v.GetError().Fail():
        result.SetError("rbdump: cannot evaluate '%s'" % expr)
        return
    if not v.GetChildMemberWithName("buf").IsValid():
        result.SetError("rbdump: '%s' is not a struct ringbuf" % expr)
        return
    rb = snapshot(v)
    result.AppendMessage("ringbuf @ 0x%x cap=%d count=%d tail=%d head=%d %s"
                         % (rb.addr, rb.cap, rb.count, rb.tail, rb.head, rb_state(rb)))
    for n, slot in enumerate(logical_slots(rb)):
        result.AppendMessage("  [%d] buf[%d] = 0x%02x" % (n, slot, rb.data[slot]))


def __lldb_init_module(debugger, internal_dict):
    # __name__ = 모듈 이름 (파일 이름에서 .py 뺀 것). 하드코딩하면 파일 이름을 바꿀 때 깨진다.
    debugger.HandleCommand("command script add -o -f %s.rbdump rbdump" % __name__)
    debugger.HandleCommand("type summary add -F %s.rb_summary ringbuf" % __name__)
