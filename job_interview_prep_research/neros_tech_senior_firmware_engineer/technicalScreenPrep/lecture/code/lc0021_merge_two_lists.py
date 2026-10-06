# --- solution ---
class ListNode:
    def __init__(self, val=0, next=None):
        self.val = val
        self.next = next


def mergeTwoLists(a, b):
    dummy = tail = ListNode()              # dummy 노드로 head 특수 처리 제거
    while a and b:
        if a.val <= b.val:
            tail.next, a = a, a.next
        else:
            tail.next, b = b, b.next
        tail = tail.next
    tail.next = a or b
    return dummy.next
# --- end ---


def build(xs):
    head = None
    for x in reversed(xs):
        head = ListNode(x, head)
    return head


def to_list(n):
    out = []
    while n:
        out.append(n.val)
        n = n.next
    return out


def test_lc21():
    assert to_list(mergeTwoLists(build([1, 2, 4]), build([1, 3, 4]))) == [1, 1, 2, 3, 4, 4]
    assert to_list(mergeTwoLists(None, build([0]))) == [0]
