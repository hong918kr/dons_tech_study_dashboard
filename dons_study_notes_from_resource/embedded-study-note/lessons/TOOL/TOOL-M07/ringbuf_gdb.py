# TOOL-M07 ringbuf_gdb.py — gdb pretty-printer: lldb 요약(rb_summary)과 같은 한 줄을 gdb 에서
#   (gdb) source ringbuf_gdb.py     (gdb) print dev.tx / print *rb
import gdb


class RingbufPrinter(object):
    def __init__(self, val):
        self.val = val

    def to_string(self):
        buf = self.val["buf"]
        cap = buf.type.sizeof // buf.type.target().sizeof
        head, tail, count = int(self.val["head"]), int(self.val["tail"]), int(self.val["count"])
        s = "count=%d/%d tail=%d head=%d" % (count, cap, tail, head)
        if count == 0:
            return s + " empty"
        data = [int(buf[(tail + i) % cap]) for i in range(min(count, cap))]
        return s + " [" + " ".join("%02x" % b for b in data) + "]"


def lookup(val):
    t = val.type.strip_typedefs()
    if t.code == gdb.TYPE_CODE_STRUCT and t.tag == "ringbuf":
        return RingbufPrinter(val)
    return None


gdb.pretty_printers.append(lookup)
